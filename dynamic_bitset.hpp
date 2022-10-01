#pragma once
#ifndef DYNAMIC_BITSET_HPP
#define DYNAMIC_BITSET_HPP

/*
* 可变长的bitset，采用sso优化，效率尚可。需要C++14。
*											2022.8.6
*/

#include <cstring>
#include <string>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <cstdint>


#if defined DEBUG || defined _DEBUG
#define NOEXCEPT_RELEASE
#else
#define NOEXCEPT_RELEASE noexcept
#endif

class dynamic_bitset
	:private std::allocator<std::uint8_t>/*空基类优化*/
{
private:
	using byte = std::uint8_t;
	using alloc = std::allocator<byte>;

private:
	/*sso优化，size() <= 14 * 8时不需要分配内存
	+--------+----------------+----------+
	|address |    __short     |  __long  |
	+--------+----------------+----------+
	|   0    |    __size      |  __cap   |
	|   1    |                +----------+
	+--------+----------------+          |
	|   2    |                |          |
	|   3    |                |          |
	|   4    |                |  __size  |
	|   5    |                |          |
	|   6    |                |          |
	|   7    |                |          |
	+--------|                |----------+
	|   8    |    __data      |          |
	|   9    |                |          |
	|   10   |                |          |
	|   11   |                |  __data  |
	|   12   |                |          |
	|   13   |                |          |
	|   14   |                |          |
	|   15   |                |          |
	+--------+----------------+----------+*/

	struct __short {
		std::uint16_t __size{};/*最低位用于标记是__short还是__long*/
		byte __data[14]{};
	};
	/*低字节表示cap，高字节表示size*/
	struct __long {
		union {
			/*cap = 2^(__cap>>1(这个1是__short::__size的标记位))*/
			std::uint8_t __cap;/*申请的内存的字节数log2*/
			size_t __size{};
		};
		byte* __data{};
	};
	union __pair {
		__short s{};
		__long l;
	};
	__pair __mypair{};

private:
	constexpr bool is_short() const noexcept {
		if ((__mypair.s.__size & 1) == 0) {
			return true;
		}
		else {
			return false;
		}
	}


	/*可容纳的bit数*/
	constexpr size_t cap() const noexcept {
		if (is_short()) {
			return max_cap_without_alloc();
		}
		else {
			return 1ULL << (__mypair.l.__cap >> 1);
		}
	}

	constexpr size_t byte_of_size() const noexcept {
		auto _size = size();
		if (_size % 8 == 0) {
			return _size / 8;
		}
		else {
			return _size / 8 + 1;
		}
	}

	/*上次申请的字节数*/
	constexpr size_t memory_allocated() const noexcept {
		if (is_short()) {
			return 0;
		}
		else {
			return cap() / 8;
		}
	}

	constexpr byte* data() noexcept {
		if (is_short()) {
			return &__mypair.s.__data[0];
		}
		else {
			return __mypair.l.__data;
		}
	}

	constexpr const byte* data() const noexcept {
		if (is_short()) {
			return &__mypair.s.__data[0];
		}
		else {
			return __mypair.l.__data;
		}
	}

	constexpr void set_size(size_t size) noexcept {
		if (is_short()) {
			/*                                    保留标记位，其他清空      跳过标记位*/
			__mypair.s.__size = (std::uint16_t)((__mypair.s.__size & 1) + (size << 1));
		}
		else {
			__mypair.l.__size = (__mypair.l.__size & 0xff) + (size << 8);
		}
	}

	/*cap必须是2的n次幂*/
	/*设置可容纳的bit数*/
	constexpr void set_cap(size_t cap) noexcept {
		if (!is_short()) {
			std::uint8_t cap_log2 = 0;
			while (cap != 0) {
				cap >>= 1ULL;
				++cap_log2;
			}
			--cap_log2;
			__mypair.l.__cap = (__mypair.l.__cap & 1) + (cap_log2 << 1);
		}
	}

	constexpr void set_flag(bool is_short) noexcept {
		__mypair.s.__size = (__mypair.s.__size & ~1) | ~(int)is_short;
	}

	static constexpr size_t max_cap_without_alloc() noexcept {
		return 8 * (sizeof(dynamic_bitset) - sizeof(__pair::s.__size));//8*(16-2)=112
	}

	/*调整为2的倍数，并且只有一位是1剩下全是0*/
	static constexpr size_t round_up_to_power_of_2(size_t num) noexcept {
		if (num <= 0) {
			return 1;
		}
		if ((num & (num - 1)) == 0) {
			return num;
		}
		num |= num >> 1;
		num |= num >> 2;
		num |= num >> 4;
		num |= num >> 8;
		num |= num >> 16;
		num |= num >> 32;
		return num + 1;
	}
public:
	dynamic_bitset() noexcept {}

	~dynamic_bitset() noexcept {
		if (!is_short()) {
			alloc::deallocate(data(), memory_allocated());
		}
	}

	dynamic_bitset(const dynamic_bitset& rhs) noexcept {
		copy(rhs);
	}

	dynamic_bitset(dynamic_bitset&& rhs) noexcept {
		copy(std::forward<dynamic_bitset&&>(rhs));
	}

	dynamic_bitset(const std::string& val) {
		resize(val.size());
		for (size_t i = 0; i < val.size(); ++i) {
			if (val[i] == '1') {
				at(i) = 1;
			}
		}
	}

	dynamic_bitset(size_t val) noexcept {
		std::string res;
		if (val == 0)
			res = "0";
		while (val != 0) {
			res.push_back(val % 2 == 0 ? '0' : '1');
			val /= 2;
		}
		*this = dynamic_bitset(std::string(res.crbegin(), res.crend()));
	}

	dynamic_bitset(size_t lenth, size_t val) NOEXCEPT_RELEASE {
#if defined _DEBUG || defined DEBUG
		if (lenth <= 63 && (lenth == 0 || val >= (1ULL << lenth)))
			throw std::out_of_range("不存在长度为lenth的二进制数val");
#endif
		auto&& _val = dynamic_bitset(val).to_string();
		auto&& front = std::string(lenth - _val.size(), '0');
		*this = dynamic_bitset(front + _val);
	}

	constexpr size_t size() const noexcept {
		if (is_short()) {
			return __mypair.s.__size >> 1;/*消去标记位*/
		}
		else {
			return __mypair.l.__size >> 8ULL;/*消去cap*/
		}
	}

	class bit_ref {
	private:
		dynamic_bitset* __bind;
		size_t __index;
	public:
		constexpr bit_ref() noexcept :__bind(), __index() {}

		constexpr bit_ref(dynamic_bitset* bind, size_t index) noexcept :__bind(bind), __index(index) {}

		constexpr bit_ref(const bit_ref& rhs) noexcept :__bind(rhs.__bind), __index(rhs.__index) {}

		void copy(const bit_ref& rhs) noexcept {
			std::memmove(this, &rhs, sizeof(bit_ref));
		}

		constexpr bit_ref& operator=(bool val) NOEXCEPT_RELEASE {
#if defined DEBUG || defined _DEBUG
			if (__index >= __bind->size())
				throw std::out_of_range("dynamic_bitset out of range");
#endif
			auto I1 = __index / 8;
			auto I2 = __index % 8;
			auto& _byte = __bind->data()[I1];
			byte _mask = byte(val) << byte(I2);
			_byte = (_byte & ~(1 << I2)) | _mask;
			return *this;
		}

		constexpr bit_ref& operator=(const bit_ref& rhs) NOEXCEPT_RELEASE {
			*this = (bool(rhs));
			return *this;
		}

		constexpr operator bool() const NOEXCEPT_RELEASE {
#if defined DEBUG || defined _DEBUG
			if (__index >= __bind->size())
				throw std::out_of_range("dynamic_bitset out of range");
#endif
			auto I1 = __index / 8;
			auto I2 = __index % 8;
			auto _byte = __bind->data()[I1];
			return _byte & (1 << I2);
		}

		constexpr const dynamic_bitset* bind() const noexcept {
			return __bind;
		}

		constexpr dynamic_bitset* bind() noexcept {
			return __bind;
		}

		constexpr size_t index() const noexcept {
			return __index;
		}

		constexpr void to_next(size_t n = 1) noexcept {
			__index += n;
		}

		constexpr bit_ref next(size_t n = 1) const noexcept {
			return bit_ref(__bind, __index + n);
		}

		constexpr void to_pre(size_t n = 1) noexcept {
			__index -= n;
		}

		constexpr bit_ref pre(size_t n = 1) const noexcept {
			return bit_ref(__bind, __index - n);
		}
	};

	bit_ref operator[](size_t index) NOEXCEPT_RELEASE {
		return at(index);
	}

	const bit_ref operator[](size_t index) const NOEXCEPT_RELEASE {
		return at(index);
	}

	constexpr bit_ref at(size_t index) NOEXCEPT_RELEASE {
#if defined DEBUG || defined _DEBUG
		if (index >= size())
			throw std::out_of_range("dynamic_bitset out of range");
#endif
		return bit_ref(this, index);
	}

	const bit_ref at(size_t index) const NOEXCEPT_RELEASE {
		return const_cast<dynamic_bitset*>(this)->at(index);
	}

	bit_ref back() NOEXCEPT_RELEASE {
		return at(size() - 1);
	}

	const bit_ref back() const NOEXCEPT_RELEASE {
		return at(size() - 1);
	}

	bit_ref front() NOEXCEPT_RELEASE {
		return at(0);
	}

	const bit_ref front() const NOEXCEPT_RELEASE {
		return at(0);
	}

	/*resize并用0初始化新内存*/
	void resize(size_t new_size) {
		if (new_size <= cap()) {
			set_size(new_size);/*无需分配内存*/
		}
		else {
			auto new_cap = round_up_to_power_of_2(new_size % 8 == 0 ? new_size / 8 : new_size / 8 + 1);
			byte* new_data = alloc::allocate(new_cap);
			if (new_data == nullptr)
				throw std::bad_alloc();

			memset(new_data, 0, new_cap);
			std::memmove(new_data, data(), byte_of_size());
			if (!is_short()) {
				alloc::deallocate(data(), memory_allocated());
			}
			else {
				set_flag(false);
			}

			__mypair.l.__data = new_data;
			set_size(new_size);
			set_cap(new_cap * 8);
		}
	}

	void push_back(bool val) {
		resize(size() + 1);
		back() = val;
	}

	void push_back(size_t n, bool val) {
		for (size_t i = 0; i < n; ++i) {
			push_back(val);
		}
	}

	void push_back_n(std::initializer_list<bool> list) {
		auto _size = size();
		resize(size() + list.size());
		for (size_t i = 0; i < list.size(); ++i) {
			at(_size + i) = *(list.begin() + i);
		}
	}

	constexpr void pop_back(size_t count = 1) NOEXCEPT_RELEASE {
#if defined DEBUG || defined _DEBUG
		if (size() < count)
			throw std::out_of_range("dynamic_bitset out of range");
#endif
		set_size(size() - count);
	}

	constexpr void clear() noexcept {
		set_size(0);
	}

	constexpr bool is_equal(const dynamic_bitset& rhs) const noexcept {
		if (size() != rhs.size())
			return false;
		return std::memcmp(data(), rhs.data(), size()) == 0;
	}

	constexpr bool operator==(const dynamic_bitset& rhs) const noexcept {
		return is_equal(rhs);
	}

	constexpr bool operator!=(const dynamic_bitset& rhs) const noexcept {
		return !is_equal(rhs);
	}

	void copy(const dynamic_bitset& rhs) {
		resize(rhs.size());
		std::memmove(data(), rhs.data(), rhs.byte_of_size());
	}

	void copy(dynamic_bitset&& rhs) noexcept {
		std::memmove(this, &rhs, sizeof(dynamic_bitset));
		rhs.set_flag(true);//防止回收空间
	}

	std::string to_string() const noexcept {
		size_t _size = size();
		std::string res(_size, '0');
		for (size_t i = 0; i < _size; ++i) {
			if (at(i) == 1) {
				res[i] = '1';
			}
		}
		return res;
	}

	constexpr size_t to_int() const noexcept
	{
		size_t res = 0;
		size_t j = 1;
		constexpr size_t end = -1;
		for (size_t i = size() - 1; i != end; i--)
		{
			res += (at(i) == 1) * j;
			j <<= 1;
		}
		return res;
	}

	dynamic_bitset& operator=(const dynamic_bitset& rhs) {
		copy(rhs);
		return *this;
	}

	dynamic_bitset& operator=(dynamic_bitset&& rhs) noexcept {
		copy(std::forward<dynamic_bitset&&>(rhs));
		rhs.set_flag(true);
		return *this;
	}

	dynamic_bitset& operator=(size_t n) noexcept {
		*this = dynamic_bitset(n);
		return *this;
	}

	dynamic_bitset& operator=(const std::string& rhs) noexcept {
		*this = dynamic_bitset(rhs);
		return *this;
	}

	dynamic_bitset operator~() const noexcept {
		auto res(*this);
		auto _size = res.byte_of_size();
		auto _start = res.data();
		for (size_t i = 0; i < _size; i++) {
			_start[i] = ~_start[i];
		}
		return res;
	}

	dynamic_bitset operator<<(size_t n) const noexcept {
		auto res(*this);
		res.push_back(n, 0);
		return res;
	}

	dynamic_bitset& operator<<=(size_t n) noexcept {
		*this = (*this) << n;
		return *this;
	}

	dynamic_bitset operator>>(size_t n) const {
		dynamic_bitset res;
		res.resize(size() + n);
		auto _size = size();
		for (size_t i = 0; i < _size; i++) {
			res[n + i] = at(i);
		}
		return res;
	}

	dynamic_bitset& operator>>=(size_t n) noexcept {
		*this = (*this) >> n;
		return *this;
	}

	/*移除前置的无效0*/
	void remove_useless_zero() noexcept {
		size_t count{};
		while (at(count) == 0) {
			++count;
		}
		*this <<= count;
		set_size(size() - count);
	}

	dynamic_bitset operator&(const dynamic_bitset& rhs) const {
		auto _lhs(*this);
		auto _rhs(rhs);
		auto _lhs_size = _lhs.size();
		auto _rhs_size = _rhs.size();
		size_t _max_byte{};

		if (_lhs_size > _rhs_size) {
			_rhs.resize(_lhs_size);
			_max_byte = _rhs.byte_of_size();
		}
		else {
			_lhs.resize(_rhs.size());
			_max_byte = _lhs.byte_of_size();
		}

		auto _lhs_data = _lhs.data();
		auto _rhs_data = _rhs.data();
		for (size_t i = 0; i < _max_byte; ++i) {
			_lhs_data[i] &= _rhs_data[i];
		}

		return _lhs;
	}

	dynamic_bitset& operator&=(size_t n) noexcept {
		*this = (*this) & n;
		return *this;
	}

	dynamic_bitset operator|(const dynamic_bitset& rhs) const {
		auto _lhs(*this);
		auto _rhs(rhs);
		auto _lhs_size = _lhs.size();
		auto _rhs_size = _rhs.size();
		size_t _max_byte{};

		if (_lhs_size > _rhs_size) {
			_rhs.resize(_lhs_size);
			_max_byte = _rhs.byte_of_size();
		}
		else {
			_lhs.resize(_rhs.size());
			_max_byte = _lhs.byte_of_size();
		}

		auto _lhs_data = _lhs.data();
		auto _rhs_data = _rhs.data();
		for (size_t i = 0; i < _max_byte; ++i) {
			_lhs_data[i] |= _rhs_data[i];
		}

		return _lhs;
	}

	dynamic_bitset& operator|=(size_t n) noexcept {
		*this = (*this) | n;
		return *this;
	}

	dynamic_bitset operator^(const dynamic_bitset& rhs) const {
		auto _lhs(*this);
		auto _rhs(rhs);
		auto _lhs_size = _lhs.size();
		auto _rhs_size = _rhs.size();
		size_t _max_byte{};

		if (_lhs_size > _rhs_size) {
			_rhs.resize(_lhs_size);
			_max_byte = _rhs.byte_of_size();
		}
		else {
			_lhs.resize(_rhs.size());
			_max_byte = _lhs.byte_of_size();
		}

		auto _lhs_data = _lhs.data();
		auto _rhs_data = _rhs.data();
		for (size_t i = 0; i < _max_byte; ++i) {
			_lhs_data[i] ^= _rhs_data[i];
		}

		return _lhs;
	}

	dynamic_bitset& operator^=(size_t n) noexcept {
		*this = (*this) ^ n;
		return *this;
	}

	void push_front(bool val) noexcept {
		*this >>= 1;
		if (val == 1) {
			at(0) = 1;
		}
	}

	void push_front(size_t n, bool val) noexcept {
		*this >>= n;
		if (val == 0) {
			return;
		}
		for (size_t i = 0; i < n; ++i) {
			at(i) = val;
		}
	}

	void push_front_n(std::initializer_list<bool> list) noexcept {
		*this >>= list.size();
		size_t i{};
		for (const auto& elem : list) {
			at(i++) = elem;
		}
	}

	void push_back(const dynamic_bitset& rhs) noexcept {
		for (auto& elem : rhs) {
			push_back(bool(elem));
		}
	}

	void push_front(const dynamic_bitset& rhs) noexcept {
		auto _Tmp(rhs);
		_Tmp.push_back(*this);
		swap(_Tmp);
	}

	void swap(dynamic_bitset& rhs) noexcept {
		dynamic_bitset _Tmp = std::move(rhs);
		rhs = std::move(*this);
		*this = std::move(_Tmp);
	}
public:
	class iterator;
	class const_iterator;
	class reverse_iterator;
	class const_reverse_iterator;

private:
	class dynamic_bitset_iterator
	{
	public:
		using iterator_category = std::random_access_iterator_tag;
		using value_type = bit_ref;
		using difference_type = std::ptrdiff_t;
		using pointer = bit_ref*;
		using reference = bit_ref&;
	public:
		constexpr dynamic_bitset_iterator() noexcept :__bit() {}
		constexpr dynamic_bitset_iterator(bit_ref ref) noexcept :__bit(ref) {}
		constexpr dynamic_bitset_iterator(const dynamic_bitset_iterator& iter) noexcept :__bit(iter.__bit) {}

		constexpr bool operator==(const dynamic_bitset_iterator& rhs) const noexcept {
			return __bit.bind() == rhs.__bit.bind() && __bit.index() == rhs.__bit.index();
		}

		constexpr bool operator!=(const dynamic_bitset_iterator& rhs) const noexcept {
			return !(*this == rhs);
		}
	protected:
		bit_ref __bit;
	};

public:
	class const_iterator :public dynamic_bitset_iterator
	{
	public:
		constexpr const_iterator() noexcept :dynamic_bitset_iterator() {}
		constexpr const_iterator(bit_ref ref) noexcept :dynamic_bitset_iterator(ref) {}
		constexpr const_iterator(const dynamic_bitset_iterator& iter) noexcept :dynamic_bitset_iterator(iter) {}

		constexpr const bit_ref& operator*() const NOEXCEPT_RELEASE {
#if defined DEBUG || defined _DEBUG
			if (__bit.index() >= __bit.bind()->size())
				throw std::out_of_range("dynamic_bitset out of range");
#endif
			return __bit;
		}

		constexpr const bit_ref* operator->() const NOEXCEPT_RELEASE
		{
#if defined DEBUG || defined _DEBUG
			if (__bit.index() >= __bit.bind()->size())
				throw std::out_of_range("dynamic_bitset out of range");
#endif
			return &__bit;
		}

		constexpr const_iterator& operator++() noexcept {
			__bit.to_next();
			return *this;
		}

		constexpr const_iterator operator++(int) noexcept {
			auto _tmp = *this;
			++* this;
			return _tmp;
		}

		constexpr const_iterator& operator--() noexcept {
			__bit.to_pre();
			return *this;
		}

		constexpr const_iterator operator--(int) noexcept {
			auto _tmp = *this;
			--* this;
			return _tmp;
		}

		constexpr const_iterator& operator+=(const difference_type off) noexcept {
			__bit.to_next(off);
			return *this;
		}

		constexpr const_iterator operator+(const difference_type off) const noexcept {
			return const_iterator(bit_ref(__bit.next(off)));
		}

		constexpr const_iterator& operator-=(const difference_type off) noexcept {
			return *this += -off;
		}

		constexpr const_iterator operator-(const difference_type off) const noexcept {
			return const_iterator(bit_ref(__bit.pre(off)));
		}

		constexpr difference_type operator-(const const_iterator& rhs) const noexcept {
			return __bit.index() - rhs.__bit.index();
		}

		constexpr const bit_ref operator[](const difference_type off) const noexcept {
			return *(*this + off);
		}

		constexpr bool operator<(const const_iterator& rhs) const noexcept {
			return __bit.index() < rhs.__bit.index();
		}

		constexpr bool operator>(const const_iterator& rhs) const noexcept {
			return rhs < *this;
		}

		constexpr bool operator<=(const const_iterator& rhs) const noexcept {
			return !(rhs < *this);
		}

		constexpr bool operator>=(const const_iterator& rhs) const noexcept {
			return !(*this < rhs);
		}
	};

	class iterator :public const_iterator
	{
	public:
		constexpr iterator() noexcept :const_iterator() {}
		constexpr iterator(bit_ref ref) noexcept :const_iterator(ref) {}
		constexpr iterator(const iterator& iter) noexcept :const_iterator(iter) {}
		constexpr iterator(const reverse_iterator& iter) noexcept :const_iterator(iter) {}

		constexpr bit_ref& operator*() NOEXCEPT_RELEASE {
#if defined DEBUG || defined _DEBUG
			if (__bit.index() >= __bit.bind()->size())
				throw std::out_of_range("dynamic_bitset out of range");
#endif
			return __bit;
		}

		constexpr bit_ref* operator->() NOEXCEPT_RELEASE {
#if defined DEBUG || defined _DEBUG
			if (__bit.index() >= __bit.bind()->size())
				throw std::out_of_range("dynamic_bitset out of range");
#endif
			return &__bit;
		}

		constexpr iterator& operator++() noexcept {
			__bit.to_next();
			return *this;
		}

		constexpr iterator operator++(int) noexcept {
			auto _tmp = *this;
			++* this;
			return _tmp;
		}

		constexpr iterator& operator--() noexcept {
			__bit.to_pre();
			return *this;
		}

		constexpr iterator operator--(int) noexcept {
			auto _tmp = *this;
			--* this;
			return _tmp;
		}

		constexpr iterator& operator+=(const difference_type off) noexcept {
			__bit.to_next(off);
			return *this;
		}

		constexpr iterator operator+(const difference_type off) const noexcept {
			auto _tmp = *this;
			_tmp->to_next(off);
			return _tmp;
		}

		constexpr iterator& operator-=(const difference_type off) noexcept {
			return *this += -off;
		}

		constexpr iterator operator-(const difference_type off) const noexcept {
			auto _tmp = *this;
			_tmp -= off;
			return _tmp;
		}

		constexpr bit_ref operator[](const difference_type off) const noexcept {
			return *(*this + off);
		}
	};


	class const_reverse_iterator :public dynamic_bitset_iterator
	{
	public:
		constexpr const_reverse_iterator() noexcept :dynamic_bitset_iterator() {}
		constexpr const_reverse_iterator(bit_ref ref) noexcept :dynamic_bitset_iterator(ref) {}
		constexpr const_reverse_iterator(const dynamic_bitset_iterator& iter) noexcept :dynamic_bitset_iterator(iter) {}

		constexpr const bit_ref& operator*() const NOEXCEPT_RELEASE {
#if defined DEBUG || defined _DEBUG
			if (__bit.index() >= __bit.bind()->size())
				throw std::out_of_range("dynamic_bitset out of range");
#endif
			return __bit;
		}

		constexpr const bit_ref* operator->() const NOEXCEPT_RELEASE
		{
#if defined DEBUG || defined _DEBUG
			if (__bit.index() >= __bit.bind()->size())
				throw std::out_of_range("dynamic_bitset out of range");
#endif
			return &__bit;
		}

		constexpr const_reverse_iterator& operator++() noexcept {
			__bit.to_pre();
			return *this;
		}

		constexpr const_reverse_iterator operator++(int) noexcept {
			auto _tmp = *this;
			++* this;
			return _tmp;
		}

		constexpr const_reverse_iterator& operator--() noexcept {
			__bit.to_next();
			return *this;
		}

		constexpr const_reverse_iterator operator--(int) noexcept {
			auto _tmp = *this;
			--* this;
			return _tmp;
		}

		constexpr const_reverse_iterator& operator+=(const difference_type off) noexcept {
			__bit.to_pre(off);
			return *this;
		}

		constexpr const_reverse_iterator operator+(const difference_type off) const noexcept {
			return const_reverse_iterator(bit_ref(__bit.pre(off)));
		}

		constexpr const_reverse_iterator& operator-=(const difference_type off) noexcept {
			return *this += -off;
		}

		constexpr const_reverse_iterator operator-(const difference_type off) const noexcept {
			return const_reverse_iterator(bit_ref(__bit.next(off)));
		}

		constexpr difference_type operator-(const const_reverse_iterator& rhs) const noexcept {
			return rhs.__bit.index() - __bit.index();
		}

		constexpr const bit_ref operator[](const difference_type off) const noexcept {
			return *(*this + off);
		}

		constexpr bool operator<(const const_reverse_iterator& rhs) const noexcept {
			return __bit.index() > rhs.__bit.index();
		}

		constexpr bool operator>(const const_reverse_iterator& rhs) const noexcept {
			return rhs < *this;
		}

		constexpr bool operator<=(const const_reverse_iterator& rhs) const noexcept {
			return !(rhs < *this);
		}

		constexpr bool operator>=(const const_reverse_iterator& rhs) const noexcept {
			return !(*this < rhs);
		}
	};
	class reverse_iterator :public const_reverse_iterator
	{
	public:
		constexpr reverse_iterator() noexcept :const_reverse_iterator() {}
		constexpr reverse_iterator(bit_ref ref) noexcept :const_reverse_iterator(ref) {}
		constexpr reverse_iterator(const reverse_iterator& iter) noexcept :const_reverse_iterator(iter) {}
		constexpr reverse_iterator(const iterator& iter) noexcept :const_reverse_iterator(iter) {}

		constexpr bit_ref& operator*() NOEXCEPT_RELEASE {
#if defined DEBUG || defined _DEBUG
			if (__bit.index() >= __bit.bind()->size())
				throw std::out_of_range("dynamic_bitset out of range");
#endif
			return __bit;
		}

		constexpr bit_ref* operator->() NOEXCEPT_RELEASE {
#if defined DEBUG || defined _DEBUG
			if (__bit.index() >= __bit.bind()->size())
				throw std::out_of_range("dynamic_bitset out of range");
#endif
			return &__bit;
		}

		constexpr reverse_iterator& operator++() noexcept {
			__bit.to_pre();
			return *this;
		}

		constexpr reverse_iterator operator++(int) noexcept {
			auto _tmp = *this;
			++* this;
			return _tmp;
		}

		constexpr reverse_iterator& operator--() noexcept {
			__bit.to_next();
			return *this;
		}

		constexpr reverse_iterator operator--(int) noexcept {
			auto _tmp = *this;
			--* this;
			return _tmp;
		}

		constexpr reverse_iterator& operator+=(const difference_type off) noexcept {
			__bit.to_pre(off);
			return *this;
		}

		constexpr reverse_iterator operator+(const difference_type off) const noexcept {
			auto _tmp = *this;
			_tmp->to_next(off);
			return _tmp;
		}

		constexpr reverse_iterator& operator-=(const difference_type off) noexcept {
			return *this += -off;
		}

		constexpr reverse_iterator operator-(const difference_type off) const noexcept {
			auto _tmp = *this;
			_tmp -= off;
			return _tmp;
		}

		constexpr bit_ref operator[](const difference_type off) const noexcept {
			return *(*this + off);
		}
	};


	constexpr iterator begin() noexcept {
		return iterator(bit_ref(this, 0));
	}

	constexpr iterator end() noexcept {
		return iterator(bit_ref(this, size()));
	}

	constexpr const_iterator cbegin() const noexcept {
		return const_iterator(bit_ref(const_cast<dynamic_bitset*>(this), 0));
	}

	constexpr const_iterator cend() const noexcept {
		return const_iterator(bit_ref(const_cast<dynamic_bitset*>(this), size()));
	}

	constexpr const_iterator begin() const noexcept {
		return cbegin();
	}

	constexpr const_iterator end() const noexcept {
		return cend();
	}

	constexpr reverse_iterator rbegin() noexcept {
		return reverse_iterator(end() - 1);
	}

	constexpr reverse_iterator rend() noexcept {
		return reverse_iterator(begin() - 1);
	}

	constexpr const_reverse_iterator crbegin() const noexcept {
		return reverse_iterator(const_cast<dynamic_bitset*>(this)->end() - 1);
	}

	constexpr const_reverse_iterator crend() const noexcept {
		return reverse_iterator(const_cast<dynamic_bitset*>(this)->begin() - 1);
	}

	constexpr const_reverse_iterator rbegin() const noexcept {
		return crbegin();
	}

	constexpr const_reverse_iterator rend() const noexcept {
		return crend();
	}
};

#undef NOEXCEPT_RELEASE
#endif // !DYNAMIC_BITSET_HPP
