// Components for manipulating sequences of characters -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005,
// 2006, 2007
// Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/** @file basic_string.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

//
// ISO C++ 14882: 21 Strings library
//

#ifndef _BASIC_STRING_H
#define _BASIC_STRING_H 1

#pragma GCC system_header

#include <ext/atomicity.h>
#include <debug/debug.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  /**
   *  @class basic_string basic_string.h <string>
   *  @brief  Managing sequences of characters and character-like objects.
   *
   *  @ingroup Containers
   *  @ingroup Sequences
   *
   *  Meets the requirements of a <a href="tables.html#65">container</a>, a
   *  <a href="tables.html#66">reversible container</a>, and a
   *  <a href="tables.html#67">sequence</a>.  Of the
   *  <a href="tables.html#68">optional sequence requirements</a>, only
   *  @c push_back, @c at, and array access are supported.
   *
   *  @doctodo
   *
   *
   *  @if maint
   *  Documentation?  What's that?
   *  Nathan Myers <ncm@cantrip.org>.
   *
   *  A string looks like this:
   *
   *  @code
   *                                        [_Rep]
   *                                        _M_length
   *   [basic_string<char_type>]            _M_capacity
   *   _M_dataplus                          _M_refcount
   *   _M_p ---------------->               unnamed array of char_type
   *  @endcode
   *
   *  Where the _M_p points to the first character in the string, and
   *  you cast it to a pointer-to-_Rep and subtract 1 to get a
   *  pointer to the header.
   *
   *  This approach has the enormous advantage that a string object
   *  requires only one allocation.  All the ugliness is confined
   *  within a single pair of inline functions, which each compile to
   *  a single "add" instruction: _Rep::_M_data(), and
   *  string::_M_rep(); and the allocation function which gets a
   *  block of raw bytes and with room enough and constructs a _Rep
   *  object at the front.
   *
   *  The reason you want _M_data pointing to the character array and
   *  not the _Rep is so that the debugger can see the string
   *  contents. (Probably we should add a non-inline member to get
   *  the _Rep for the debugger to use, so users can check the actual
   *  string length.)
   *
   *  Note that the _Rep object is a POD so that you can have a
   *  static "empty string" _Rep object already "constructed" before
   *  static constructors have run.  The reference-count encoding is
   *  chosen so that a 0 indicates one reference, so you never try to
   *  destroy the empty-string _Rep object.
   *
   *  All but the last paragraph is considered pretty conventional
   *  for a C++ string implementation.
   *  @endif
  */
  // 21.3  Template class basic_string
  template<typename _CharT, typename _Traits, typename _Alloc>
    class basic_string
    {
      typedef typename _Alloc::template rebind<_CharT>::other _CharT_alloc_type;

      // Types:
    public:
      typedef _Traits					    traits_type;
      typedef typename _Traits::char_type		    value_type;
      typedef _Alloc					    allocator_type;
      typedef typename _CharT_alloc_type::size_type	    size_type;
      typedef typename _CharT_alloc_type::difference_type   difference_type;
      typedef typename _CharT_alloc_type::reference	    reference;
      typedef typename _CharT_alloc_type::const_reference   const_reference;
      typedef typename _CharT_alloc_type::pointer	    pointer;
      typedef typename _CharT_alloc_type::const_pointer	    const_pointer;
      typedef __gnu_cxx::__normal_iterator<pointer, basic_string>  iterator;
      typedef __gnu_cxx::__normal_iterator<const_pointer, basic_string>
                                                            const_iterator;
      typedef std::reverse_iterator<const_iterator>	const_reverse_iterator;
      typedef std::reverse_iterator<iterator>		    reverse_iterator;

    private:
      // _Rep: string representation
      //   Invariants:
      //   1. String really contains _M_length + 1 characters: due to 21.3.4
      //      must be kept null-terminated.
      //   2. _M_capacity >= _M_length
      //      Allocated memory is always (_M_capacity + 1) * sizeof(_CharT).
      //   3. _M_refcount has three states:
      //      -1: leaked, one reference, no ref-copies allowed, non-const.
      //       0: one reference, non-const.
      //     n>0: n + 1 references, operations require a lock, const.
      //   4. All fields==0 is an empty string, given the extra storage
      //      beyond-the-end for a null terminator; thus, the shared
      //      empty string representation needs no constructor.

      struct _Rep_base
      {
	size_type		_M_length;
	size_type		_M_capacity;
	_Atomic_word		_M_refcount;
      };

      struct _Rep : _Rep_base
      {
	// Types:
	typedef typename _Alloc::template rebind<char>::other _Raw_bytes_alloc;

	// (Public) Data members:

	// The maximum number of individual char_type elements of an
	// individual string is determined by _S_max_size. This is the
	// value that will be returned by max_size().  (Whereas npos
	// is the maximum number of bytes the allocator can allocate.)
	// If one was to divvy up the theoretical largest size string,
	// with a terminating character and m _CharT elements, it'd
	// look like this:
	// npos = sizeof(_Rep) + (m * sizeof(_CharT)) + sizeof(_CharT)
	// Solving for m:
	// m = ((npos - sizeof(_Rep))/sizeof(CharT)) - 1
	// In addition, this implementation quarters this amount.
	static const size_type	_S_max_size;
	static const _CharT	_S_terminal;

	// The following storage is init'd to 0 by the linker, resulting
        // (carefully) in an empty string with one reference.
        static size_type _S_empty_rep_storage[];

        static _Rep&
        _S_empty_rep()
        { 
	  // NB: Mild hack to avoid strict-aliasing warnings.  Note that
	  // _S_empty_rep_storage is never modified and the punning should
	  // be reasonably safe in this case.
	  void* __p = reinterpret_cast<void*>(&_S_empty_rep_storage);
	  return *reinterpret_cast<_Rep*>(__p);
	}

        bool
	_M_is_leaked() const
        { return this->_M_refcount < 0; }

        bool
	_M_is_shared() const
        { return this->_M_refcount > 0; }

        void
	_M_set_leaked()
        { this->_M_refcount = -1; }

        void
	_M_set_sharable()
        { this->_M_refcount = 0; }

	void
	_M_set_length_and_sharable(size_type __n)
	{ 
	  this->_M_set_sharable();  // One reference.
	  this->_M_length = __n;
	  traits_type::assign(this->_M_refdata()[__n], _S_terminal);
	  // grrr. (per 21.3.4)
	  // You cannot leave those LWG people alone for a second.
	}

	_CharT*
	_M_refdata() throw()
	{ return reinterpret_cast<_CharT*>(this + 1); }

	_CharT*
	_M_grab(const _Alloc& __alloc1, const _Alloc& __alloc2)
	{
	  return (!_M_is_leaked() && __alloc1 == __alloc2)
	          ? _M_refcopy() : _M_clone(__alloc1);
	}

	// Create & Destroy
	static _Rep*
	_S_create(size_type, size_type, const _Alloc&);

	void
	_M_dispose(const _Alloc& __a)
	{
#ifndef _GLIBCXX_FULLY_DYNAMIC_STRING
	  if (__builtin_expect(this != &_S_empty_rep(), false))
#endif
	    if (__gnu_cxx::__exchange_and_add_dispatch(&this->_M_refcount,
						       -1) <= 0)
	      _M_destroy(__a);
	}  // XXX MT

	void
	_M_destroy(const _Alloc&) throw();

	_CharT*
	_M_refcopy() throw()
	{
#ifndef _GLIBCXX_FULLY_DYNAMIC_STRING
	  if (__builtin_expect(this != &_S_empty_rep(), false))
#endif
            __gnu_cxx::__atomic_add_dispatch(&this->_M_refcount, 1);
	  return _M_refdata();
	}  // XXX MT

	_CharT*
	_M_clone(const _Alloc&, size_type __res = 0);
      };

      // Use empty-base optimization: http://www.cantrip.org/emptyopt.html
      struct _Alloc_hider : _Alloc
      {
	_Alloc_hider(_CharT* __dat, const _Alloc& __a)
	: _Alloc(__a), _M_p(__dat) { }

	_CharT* _M_p; // The actual data.
      };

    public:
      // Data Members (public):
      // NB: This is an unsigned type, and thus represents the maximum
      // size that the allocator can hold.
      ///  Value returned by various member functions when they fail.
      static const size_type	npos = static_cast<size_type>(-1);

    private:
      // Data Members (private):
      mutable _Alloc_hider	_M_dataplus;

      _CharT*
      _M_data() const
      { return  _M_dataplus._M_p; }

      _CharT*
      _M_data(_CharT* __p)
      { return (_M_dataplus._M_p = __p); }

      _Rep*
      _M_rep() const
      { return &((reinterpret_cast<_Rep*> (_M_data()))[-1]); }

      // For the internal use we have functions similar to `begin'/`end'
      // but they do not call _M_leak.
      iterator
      _M_ibegin() const
      { return iterator(_M_data()); }

      iterator
      _M_iend() const
      { return iterator(_M_data() + this->size()); }

      void
      _M_leak()    // for use in begin() & non-const op[]
      {
	if (!_M_rep()->_M_is_leaked())
	  _M_leak_hard();
      }

      size_type
      _M_check(size_type __pos, const char* __s) const
      {
	if (__pos > this->size())
	  __throw_out_of_range(__N(__s));
	return __pos;
      }

      void
      _M_check_length(size_type __n1, size_type __n2, const char* __s) const
      {
	if (this->max_size() - (this->size() - __n1) < __n2)
	  __throw_length_error(__N(__s));
      }

      // NB: _M_limit doesn't check for a bad __pos value.
      size_type
      _M_limit(size_type __pos, size_type __off) const
      {
	const bool __testoff =  __off < this->size() - __pos;
	return __testoff ? __off : this->size() - __pos;
      }

      // True if _Rep and source do not overlap.
      bool
      _M_disjunct(const _CharT* __s) const
      {
	return (less<const _CharT*>()(__s, _M_data())
		|| less<const _CharT*>()(_M_data() + this->size(), __s));
      }

      // When __n = 1 way faster than the general multichar
      // traits_type::copy/move/assign.
      static void
      _M_copy(_CharT* __d, const _CharT* __s, size_type __n)
      {
	if (__n == 1)
	  traits_type::assign(*__d, *__s);
	else
	  traits_type::copy(__d, __s, __n);
      }

      static void
      _M_move(_CharT* __d, const _CharT* __s, size_type __n)
      {
	if (__n == 1)
	  traits_type::assign(*__d, *__s);
	else
	  traits_type::move(__d, __s, __n);	  
      }

      static void
      _M_assign(_CharT* __d, size_type __n, _CharT __c)
      {
	if (__n == 1)
	  traits_type::assign(*__d, __c);
	else
	  traits_type::assign(__d, __n, __c);	  
      }

      // _S_copy_chars is a separate template to permit specialization
      // to optimize for the common case of pointers as iterators.
      template<class _Iterator>
        static void
        _S_copy_chars(_CharT* __p, _Iterator __k1, _Iterator __k2)
        {
	  for (; __k1 != __k2; ++__k1, ++__p)
	    traits_type::assign(*__p, *__k1); // These types are off.
	}

      static void
      _S_copy_chars(_CharT* __p, iterator __k1, iterator __k2)
      { _S_copy_chars(__p, __k1.base(), __k2.base()); }

      static void
      _S_copy_chars(_CharT* __p, const_iterator __k1, const_iterator __k2)
      { _S_copy_chars(__p, __k1.base(), __k2.base()); }

      static void
      _S_copy_chars(_CharT* __p, _CharT* __k1, _CharT* __k2)
      { _M_copy(__p, __k1, __k2 - __k1); }

      static void
      _S_copy_chars(_CharT* __p, const _CharT* __k1, const _CharT* __k2)
      { _M_copy(__p, __k1, __k2 - __k1); }

      static int
      _S_compare(size_type __x, size_type __y)
      {
         if (__x > __y)
            return 1;
         if (__x < __y)
            return -1;
         return 0;
      }

      void
      _M_mutate(size_type __pos, size_type __len1, size_type __len2);

      void
      _M_leak_hard();

      static _Rep&
      _S_empty_rep()
      { return _Rep::_S_empty_rep(); }

    public:
      // Construct/copy/destroy:
      // NB: We overload ctors in some cases instead of using default
      // arguments, per 17.4.4.4 para. 2 item 2.

      /**
       *  @brief  Default constructor creates an empty string.
       */
      inline
      basic_string();

      /**
       *  @brief  Construct an empty string using allocator @a a.
       */
      explicit
      basic_string(const _Alloc& __a);

      // NB: per LWG issue 42, semantics different from IS:
      /**
       *  @brief  Construct string with copy of value of @a str.
       *  @param  str  Source string.
       */
      basic_string(const basic_string& __str);
      /**
       *  @brief  Construct string as copy of a substring.
       *  @param  str  Source string.
       *  @param  pos  Index of first character to copy from.
       *  @param  n  Number of characters to copy (default remainder).
       */
      basic_string(const basic_string& __str, size_type __pos,
		   size_type __n = npos);
      /**
       *  @brief  Construct string as copy of a substring.
       *  @param  str  Source string.
       *  @param  pos  Index of first character to copy from.
       *  @param  n  Number of characters to copy.
       *  @param  a  Allocator to use.
       */
      basic_string(const basic_string& __str, size_type __pos,
		   size_type __n, const _Alloc& __a);

      /**
       *  @brief  Construct string initialized by a character array.
       *  @param  s  Source character array.
       *  @param  n  Number of characters to copy.
       *  @param  a  Allocator to use (default is default allocator).
       *
       *  NB: @a s must have at least @a n characters, '\0' has no special
       *  meaning.
       */
      basic_string(const _CharT* __s, size_type __n,
		   const _Alloc& __a = _Alloc());
      /**
       *  @brief  Construct string as copy of a C string.
       *  @param  s  Source C string.
       *  @param  a  Allocator to use (default is default allocator).
       */
      basic_string(const _CharT* __s, const _Alloc& __a = _Alloc());
      /**
       *  @brief  Construct string as multiple characters.
       *  @param  n  Number of characters.
       *  @param  c  Character to use.
       *  @param  a  Allocator to use (default is default allocator).
       */
      basic_string(size_type __n, _CharT __c, const _Alloc& __a = _Alloc());

      /**
       *  @brief  Construct string as copy of a range.
       *  @param  beg  Start of range.
       *  @param  end  End of range.
       *  @param  a  Allocator to use (default is default allocator).
       */
      template<class _InputIterator>
        basic_string(_InputIterator __beg, _InputIterator __end,
		     const _Alloc& __a = _Alloc());

      /**
       *  @brief  Destroy the string instance.
       */
      ~basic_string()
      { _M_rep()->_M_dispose(this->get_allocator()); }

      /**
       *  @brief  Assign the value of @a str to this string.
       *  @param  str  Source string.
       */
      basic_string&
      operator=(const basic_string& __str) 
      { return this->assign(__str); }

      /**
       *  @brief  Copy contents of @a s into this string.
       *  @param  s  Source null-terminated string.
       */
      basic_string&
      operator=(const _CharT* __s) 
      { return this->assign(__s); }

      /**
       *  @brief  Set value to string of length 1.
       *  @param  c  Source character.
       *
       *  Assigning to a character makes this string length 1 and
       *  (*this)[0] == @a c.
       */
      basic_string&
      operator=(_CharT __c) 
      { 
	this->assign(1, __c); 
	return *this;
      }

      // Iterators:
      /**
       *  Returns a read/write iterator that points to the first character in
       *  the %string.  Unshares the string.
       */
      iterator
      begin()
      {
	_M_leak();
	return iterator(_M_data());
      }

      /**
       *  Returns a read-only (constant) iterator that points to the first
       *  character in the %string.
       */
      const_iterator
      begin() const
      { return const_iterator(_M_data()); }

      /**
       *  Returns a read/write iterator that points one past the last
       *  character in the %string.  Unshares the string.
       */
      iterator
      end()
      {
	_M_leak();
	return iterator(_M_data() + this->size());
      }

      /**
       *  Returns a read-only (constant) iterator that points one past the
       *  last character in the %string.
       */
      const_iterator
      end() const
      { return const_iterator(_M_data() + this->size()); }

      /**
       *  Returns a read/write reverse iterator that points to the last
       *  character in the %string.  Iteration is done in reverse element
       *  order.  Unshares the string.
       */
      reverse_iterator
      rbegin()
      { return reverse_iterator(this->end()); }

      /**
       *  Returns a read-only (constant) reverse iterator that points
       *  to the last character in the %string.  Iteration is done in
       *  reverse element order.
       */
      const_reverse_iterator
      rbegin() const
      { return const_reverse_iterator(this->end()); }

      /**
       *  Returns a read/write reverse iterator that points to one before the
       *  first character in the %string.  Iteration is done in reverse
       *  element order.  Unshares the string.
       */
      reverse_iterator
      rend()
      { return reverse_iterator(this->begin()); }

      /**
       *  Returns a read-only (constant) reverse iterator that points
       *  to one before the first character in the %string.  Iteration
       *  is done in reverse element order.
       */
      const_reverse_iterator
      rend() const
      { return const_reverse_iterator(this->begin()); }

    public:
      // Capacity:
      ///  Returns the number of characters in the string, not including any
      ///  null-termination.
      size_type
      size() const
      { return _M_rep()->_M_length; }

      ///  Returns the number of characters in the string, not including any
      ///  null-termination.
      size_type
      length() const
      { return _M_rep()->_M_length; }

      /// Returns the size() of the largest possible %string.
      size_type
      max_size() const
      { return _Rep::_S_max_size; }

      /**
       *  @brief  Resizes the %string to the specified number of characters.
       *  @param  n  Number of characters the %string should contain.
       *  @param  c  Character to fill any new elements.
       *
       *  This function will %resize the %string to the specified
       *  number of characters.  If the number is smaller than the
       *  %string's current size the %string is truncated, otherwise
       *  the %string is extended and new elements are set to @a c.
       */
      void
      resize(size_type __n, _CharT __c);

      /**
       *  @brief  Resizes the %string to the specified number of characters.
       *  @param  n  Number of characters the %string should contain.
       *
       *  This function will resize the %string to the specified length.  If
       *  the new size is smaller than the %string's current size the %string
       *  is truncated, otherwise the %string is extended and new characters
       *  are default-constructed.  For basic types such as char, this means
       *  setting them to 0.
       */
      void
      resize(size_type __n)
      { this->resize(__n, _CharT()); }

      /**
       *  Returns the total number of characters that the %string can hold
       *  before needing to allocate more memory.
       */
      size_type
      capacity() const
      { return _M_rep()->_M_capacity; }

      /**
       *  @brief  Attempt to preallocate enough memory for specified number of
       *          characters.
       *  @param  res_arg  Number of characters required.
       *  @throw  std::length_error  If @a res_arg exceeds @c max_size().
       *
       *  This function attempts to reserve enough memory for the
       *  %string to hold the specified number of characters.  If the
       *  number requested is more than max_size(), length_error is
       *  thrown.
       *
       *  The advantage of this function is that if optimal code is a
       *  necessity and the user can determine the string length that will be
       *  required, the user can reserve the memory in %advance, and thus
       *  prevent a possible reallocation of memory and copying of %string
       *  data.
       */
      void
      reserve(size_type __res_arg = 0);

      /**
       *  Erases the string, making it empty.
       */
      void
      clear()
      { _M_mutate(0, this->size(), 0); }

      /**
       *  Returns true if the %string is empty.  Equivalent to *this == "".
       */
      bool
      empty() const
      { return this->size() == 0; }

      // Element access:
      /**
       *  @brief  Subscript access to the data contained in the %string.
       *  @param  pos  The index of the character to access.
       *  @return  Read-only (constant) reference to the character.
       *
       *  This operator allows for easy, array-style, data access.
       *  Note that data access with this operator is unchecked and
       *  out_of_range lookups are not defined. (For checked lookups
       *  see at().)
       */
      const_reference
      operator[] (size_type __pos) const
      {
	_GLIBCXX_DEBUG_ASSERT(__pos <= size());
	return _M_data()[__pos];
      }

      /**
       *  @brief  Subscript access to the data contained in the %string.
       *  @param  pos  The index of the character to access.
       *  @return  Read/write reference to the character.
       *
       *  This operator allows for easy, array-style, data access.
       *  Note that data access with this operator is unchecked and
       *  out_of_range lookups are not defined. (For checked lookups
       *  see at().)  Unshares the string.
       */
      reference
      operator[](size_type __pos)
      {
        // allow pos == size() as v3 extension:
	_GLIBCXX_DEBUG_ASSERT(__pos <= size());
        // but be strict in pedantic mode:
	_GLIBCXX_DEBUG_PEDASSERT(__pos < size());
	_M_leak();
	return _M_data()[__pos];
      }

      /**
       *  @brief  Provides access to the data contained in the %string.
       *  @param n The index of the character to access.
       *  @return  Read-only (const) reference to the character.
       *  @throw  std::out_of_range  If @a n is an invalid index.
       *
       *  This function provides for safer data access.  The parameter is
       *  first checked that it is in the range of the string.  The function
       *  throws out_of_range if the check fails.
       */
      const_reference
      at(size_type __n) const
      {
	if (__n >= this->size())
	  __throw_out_of_range(__N("basic_string::at"));
	return _M_data()[__n];
      }

      /**
       *  @brief  Provides access to the data contained in the %string.
       *  @param n The index of the character to access.
       *  @return  Read/write reference to the character.
       *  @throw  std::out_of_range  If @a n is an invalid index.
       *
       *  This function provides for safer data access.  The parameter is
       *  first checked that it is in the range of the string.  The function
       *  throws out_of_range if the check fails.  Success results in
       *  unsharing the string.
       */
      reference
      at(size_type __n)
      {
	if (__n >= size())
	  __throw_out_of_range(__N("basic_string::at"));
	_M_leak();
	return _M_data()[__n];
      }

      // Modifiers:
      /**
       *  @brief  Append a string to this string.
       *  @param str  The string to append.
       *  @return  Reference to this string.
       */
      basic_string&
      operator+=(const basic_string& __str)
      { return this->append(__str); }

      /**
       *  @brief  Append a C string.
       *  @param s  The C string to append.
       *  @return  Reference to this string.
       */
      basic_string&
      operator+=(const _CharT* __s)
      { return this->append(__s); }

      /**
       *  @brief  Append a character.
       *  @param c  The character to append.
       *  @return  Reference to this string.
       */
      basic_string&
      operator+=(_CharT __c)
      { 
	this->push_back(__c);
	return *this;
      }

      /**
       *  @brief  Append a string to this string.
       *  @param str  The string to append.
       *  @return  Reference to this string.
       */
      basic_string&
      append(const basic_string& __str);

      /**
       *  @brief  Append a substring.
       *  @param str  The string to append.
       *  @param pos  Index of the first character of str to append.
       *  @param n  The number of characters to append.
       *  @return  Reference to this string.
       *  @throw  std::out_of_range if @a pos is not a valid index.
       *
       *  This function appends @a n characters from @a str starting at @a pos
       *  to this string.  If @a n is is larger than the number of available
       *  characters in @a str, the remainder of @a str is appended.
       */
      basic_string&
      append(const basic_string& __str, size_type __pos, size_type __n);

      /**
       *  @brief  Append a C substring.
       *  @param s  The C string to append.
       *  @param n  The number of characters to append.
       *  @return  Reference to this string.
       */
      basic_string&
      append(const _CharT* __s, size_type __n);

      /**
       *  @brief  Append a C string.
       *  @param s  The C string to append.
       *  @return  Reference to this string.
       */
      basic_string&
      append(const _CharT* __s)
      {
	__glibcxx_requires_string(__s);
	return this->append(__s, traits_type::length(__s));
      }

      /**
       *  @brief  Append multiple characters.
       *  @param n  The number of characters to append.
       *  @param c  The character to use.
       *  @return  Reference to this string.
       *
       *  Appends n copies of c to this string.
       */
      basic_string&
      append(size_type __n, _CharT __c);

      /**
       *  @brief  Append a range of characters.
       *  @param first  Iterator referencing the first character to append.
       *  @param last  Iterator marking the end of the range.
       *  @return  Reference to this string.
       *
       *  Appends characters in the range [first,last) to this string.
       */
      template<class _InputIterator>
        basic_string&
        append(_InputIterator __first, _InputIterator __last)
        { return this->replace(_M_iend(), _M_iend(), __first, __last); }

      /**
       *  @brief  Append a single character.
       *  @param c  Character to append.
       */
      void
      push_back(_CharT __c)
      { 
	const size_type __len = 1 + this->size();
	if (__len > this->capacity() || _M_rep()->_M_is_shared())
	  this->reserve(__len);
	traits_type::assign(_M_data()[this->size()], __c);
	_M_rep()->_M_set_length_and_sharable(__len);
      }

      /**
       *  @brief  Set value to contents of another string.
       *  @param  str  Source string to use.
       *  @return  Reference to this string.
       */
      basic_string&
      assign(const basic_string& __str);

      /**
       *  @brief  Set value to a substring of a string.
       *  @param str  The string to use.
       *  @param pos  Index of the first character of str.
       *  @param n  Number of characters to use.
       *  @return  Reference to this string.
       *  @throw  std::out_of_range if @a pos is not a valid index.
       *
       *  This function sets this string to the substring of @a str consisting
       *  of @a n characters at @a pos.  If @a n is is larger than the number
       *  of available characters in @a str, the remainder of @a str is used.
       */
      basic_string&
      assign(const basic_string& __str, size_type __pos, size_type __n)
      { return this->assign(__str._M_data()
			    + __str._M_check(__pos, "basic_string::assign"),
			    __str._M_limit(__pos, __n)); }

      /**
       *  @brief  Set value to a C substring.
       *  @param s  The C string to use.
       *  @param n  Number of characters to use.
       *  @return  Reference to this string.
       *
       *  This function sets the value of this string to the first @a n
       *  characters of @a s.  If @a n is is larger than the number of
       *  available characters in @a s, the remainder of @a s is used.
       */
      basic_string&
      assign(const _CharT* __s, size_type __n);

      /**
       *  @brief  Set value to contents of a C string.
       *  @param s  The C string to use.
       *  @return  Reference to this string.
       *
       *  This function sets the value of this string to the value of @a s.
       *  The data is copied, so there is no dependence on @a s once the
       *  function returns.
       */
      basic_string&
      assign(const _CharT* __s)
      {
	__glibcxx_requires_string(__s);
	return this->assign(__s, traits_type::length(__s));
      }

      /**
       *  @brief  Set value to multiple characters.
       *  @param n  Length of the resulting string.
       *  @param c  The character to use.
       *  @return  Reference to this string.
       *
       *  This function sets the value of this string to @a n copies of
       *  character @a c.
       */
      basic_string&
      assign(size_type __n, _CharT __c)
      { return _M_replace_aux(size_type(0), this->size(), __n, __c); }

      /**
       *  @brief  Set value to a range of characters.
       *  @param first  Iterator referencing the first character to append.
       *  @param last  Iterator marking the end of the range.
       *  @return  Reference to this string.
       *
       *  Sets value of string to characters in the range [first,last).
      */
      template<class _InputIterator>
        basic_string&
        assign(_InputIterator __first, _InputIterator __last)
        { return this->replace(_M_ibegin(), _M_iend(), __first, __last); }

      /**
       *  @brief  Insert multiple characters.
       *  @param p  Iterator referencing location in string to insert at.
       *  @param n  Number of characters to insert
       *  @param c  The character to insert.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Inserts @a n copies of character @a c starting at the position
       *  referenced by iterator @a p.  If adding characters causes the length
       *  to exceed max_size(), length_error is thrown.  The value of the
       *  string doesn't change if an error is thrown.
      */
      void
      insert(iterator __p, size_type __n, _CharT __c)
      {	this->replace(__p, __p, __n, __c);  }

      /**
       *  @brief  Insert a range of characters.
       *  @param p  Iterator referencing location in string to insert at.
       *  @param beg  Start of range.
       *  @param end  End of range.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Inserts characters in range [beg,end).  If adding characters causes
       *  the length to exceed max_size(), length_error is thrown.  The value
       *  of the string doesn't change if an error is thrown.
      */
      template<class _InputIterator>
        void
        insert(iterator __p, _InputIterator __beg, _InputIterator __end)
        { this->replace(__p, __p, __beg, __end); }

      /**
       *  @brief  Insert value of a string.
       *  @param pos1  Iterator referencing location in string to insert at.
       *  @param str  The string to insert.
       *  @return  Reference to this string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Inserts value of @a str starting at @a pos1.  If adding characters
       *  causes the length to exceed max_size(), length_error is thrown.  The
       *  value of the string doesn't change if an error is thrown.
      */
      basic_string&
      insert(size_type __pos1, const basic_string& __str)
      { return this->insert(__pos1, __str, size_type(0), __str.size()); }

      /**
       *  @brief  Insert a substring.
       *  @param pos1  Iterator referencing location in string to insert at.
       *  @param str  The string to insert.
       *  @param pos2  Start of characters in str to insert.
       *  @param n  Number of characters to insert.
       *  @return  Reference to this string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *  @throw  std::out_of_range  If @a pos1 > size() or
       *  @a pos2 > @a str.size().
       *
       *  Starting at @a pos1, insert @a n character of @a str beginning with
       *  @a pos2.  If adding characters causes the length to exceed
       *  max_size(), length_error is thrown.  If @a pos1 is beyond the end of
       *  this string or @a pos2 is beyond the end of @a str, out_of_range is
       *  thrown.  The value of the string doesn't change if an error is
       *  thrown.
      */
      basic_string&
      insert(size_type __pos1, const basic_string& __str,
	     size_type __pos2, size_type __n)
      { return this->insert(__pos1, __str._M_data()
			    + __str._M_check(__pos2, "basic_string::insert"),
			    __str._M_limit(__pos2, __n)); }

      /**
       *  @brief  Insert a C substring.
       *  @param pos  Iterator referencing location in string to insert at.
       *  @param s  The C string to insert.
       *  @param n  The number of characters to insert.
       *  @return  Reference to this string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *  @throw  std::out_of_range  If @a pos is beyond the end of this
       *  string.
       *
       *  Inserts the first @a n characters of @a s starting at @a pos.  If
       *  adding characters causes the length to exceed max_size(),
       *  length_error is thrown.  If @a pos is beyond end(), out_of_range is
       *  thrown.  The value of the string doesn't change if an error is
       *  thrown.
      */
      basic_string&
      insert(size_type __pos, const _CharT* __s, size_type __n);

      /**
       *  @brief  Insert a C string.
       *  @param pos  Iterator referencing location in string to insert at.
       *  @param s  The C string to insert.
       *  @return  Reference to this string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *  @throw  std::out_of_range  If @a pos is beyond the end of this
       *  string.
       *
       *  Inserts the first @a n characters of @a s starting at @a pos.  If
       *  adding characters causes the length to exceed max_size(),
       *  length_error is thrown.  If @a pos is beyond end(), out_of_range is
       *  thrown.  The value of the string doesn't change if an error is
       *  thrown.
      */
      basic_string&
      insert(size_type __pos, const _CharT* __s)
      {
	__glibcxx_requires_string(__s);
	return this->insert(__pos, __s, traits_type::length(__s));
      }

      /**
       *  @brief  Insert multiple characters.
       *  @param pos  Index in string to insert at.
       *  @param n  Number of characters to insert
       *  @param c  The character to insert.
       *  @return  Reference to this string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *  @throw  std::out_of_range  If @a pos is beyond the end of this
       *  string.
       *
       *  Inserts @a n copies of character @a c starting at index @a pos.  If
       *  adding characters causes the length to exceed max_size(),
       *  length_error is thrown.  If @a pos > length(), out_of_range is
       *  thrown.  The value of the string doesn't change if an error is
       *  thrown.
      */
      basic_string&
      insert(size_type __pos, size_type __n, _CharT __c)
      { return _M_replace_aux(_M_check(__pos, "basic_string::insert"),
			      size_type(0), __n, __c); }

      /**
       *  @brief  Insert one character.
       *  @param p  Iterator referencing position in string to insert at.
       *  @param c  The character to insert.
       *  @return  Iterator referencing newly inserted char.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Inserts character @a c at position referenced by @a p.  If adding
       *  character causes the length to exceed max_size(), length_error is
       *  thrown.  If @a p is beyond end of string, out_of_range is thrown.
       *  The value of the string doesn't change if an error is thrown.
      */
      iterator
      insert(iterator __p, _CharT __c)
      {
	_GLIBCXX_DEBUG_PEDASSERT(__p >= _M_ibegin() && __p <= _M_iend());
	const size_type __pos = __p - _M_ibegin();
	_M_replace_aux(__pos, size_type(0), size_type(1), __c);
	_M_rep()->_M_set_leaked();
	return iterator(_M_data() + __pos);
      }

      /**
       *  @brief  Remove characters.
       *  @param pos  Index of first character to remove (default 0).
       *  @param n  Number of characters to remove (default remainder).
       *  @return  Reference to this string.
       *  @throw  std::out_of_range  If @a pos is beyond the end of this
       *  string.
       *
       *  Removes @a n characters from this string starting at @a pos.  The
       *  length of the string is reduced by @a n.  If there are < @a n
       *  characters to remove, the remainder of the string is truncated.  If
       *  @a p is beyond end of string, out_of_range is thrown.  The value of
       *  the string doesn't change if an error is thrown.
      */
      basic_string&
      erase(size_type __pos = 0, size_type __n = npos)
      { 
	_M_mutate(_M_check(__pos, "basic_string::erase"),
		  _M_limit(__pos, __n), size_type(0));
	return *this;
      }

      /**
       *  @brief  Remove one character.
       *  @param position  Iterator referencing the character to remove.
       *  @return  iterator referencing same location after removal.
       *
       *  Removes the character at @a position from this string. The value
       *  of the string doesn't change if an error is thrown.
      */
      iterator
      erase(iterator __position)
      {
	_GLIBCXX_DEBUG_PEDASSERT(__position >= _M_ibegin()
				 && __position < _M_iend());
	const size_type __pos = __position - _M_ibegin();
	_M_mutate(__pos, size_type(1), size_type(0));
	_M_rep()->_M_set_leaked();
	return iterator(_M_data() + __pos);
      }

      /**
       *  @brief  Remove a range of characters.
       *  @param first  Iterator referencing the first character to remove.
       *  @param last  Iterator referencing the end of the range.
       *  @return  Iterator referencing location of first after removal.
       *
       *  Removes the characters in the range [first,last) from this string.
       *  The value of the string doesn't change if an error is thrown.
      */
      iterator
      erase(iterator __first, iterator __last)
      {
	_GLIBCXX_DEBUG_PEDASSERT(__first >= _M_ibegin() && __first <= __last
				 && __last <= _M_iend());
        const size_type __pos = __first - _M_ibegin();
	_M_mutate(__pos, __last - __first, size_type(0));
	_M_rep()->_M_set_leaked();
	return iterator(_M_data() + __pos);
      }

      /**
       *  @brief  Replace characters with value from another string.
       *  @param pos  Index of first character to replace.
       *  @param n  Number of characters to be replaced.
       *  @param str  String to insert.
       *  @return  Reference to this string.
       *  @throw  std::out_of_range  If @a pos is beyond the end of this
       *  string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Removes the characters in the range [pos,pos+n) from this string.
       *  In place, the value of @a str is inserted.  If @a pos is beyond end
       *  of string, out_of_range is thrown.  If the length of the result
       *  exceeds max_size(), length_error is thrown.  The value of the string
       *  doesn't change if an error is thrown.
      */
      basic_string&
      replace(size_type __pos, size_type __n, const basic_string& __str)
      { return this->replace(__pos, __n, __str._M_data(), __str.size()); }

      /**
       *  @brief  Replace characters with value from another string.
       *  @param pos1  Index of first character to replace.
       *  @param n1  Number of characters to be replaced.
       *  @param str  String to insert.
       *  @param pos2  Index of first character of str to use.
       *  @param n2  Number of characters from str to use.
       *  @return  Reference to this string.
       *  @throw  std::out_of_range  If @a pos1 > size() or @a pos2 >
       *  str.size().
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Removes the characters in the range [pos1,pos1 + n) from this
       *  string.  In place, the value of @a str is inserted.  If @a pos is
       *  beyond end of string, out_of_range is thrown.  If the length of the
       *  result exceeds max_size(), length_error is thrown.  The value of the
       *  string doesn't change if an error is thrown.
      */
      basic_string&
      replace(size_type __pos1, size_type __n1, const basic_string& __str,
	      size_type __pos2, size_type __n2)
      { return this->replace(__pos1, __n1, __str._M_data()
			     + __str._M_check(__pos2, "basic_string::replace"),
			     __str._M_limit(__pos2, __n2)); }

      /**
       *  @brief  Replace characters with value of a C substring.
       *  @param pos  Index of first character to replace.
       *  @param n1  Number of characters to be replaced.
       *  @param s  C string to insert.
       *  @param n2  Number of characters from @a s to use.
       *  @return  Reference to this string.
       *  @throw  std::out_of_range  If @a pos1 > size().
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Removes the characters in the range [pos,pos + n1) from this string.
       *  In place, the first @a n2 characters of @a s are inserted, or all
       *  of @a s if @a n2 is too large.  If @a pos is beyond end of string,
       *  out_of_range is thrown.  If the length of result exceeds max_size(),
       *  length_error is thrown.  The value of the string doesn't change if
       *  an error is thrown.
      */
      basic_string&
      replace(size_type __pos, size_type __n1, const _CharT* __s,
	      size_type __n2);

      /**
       *  @brief  Replace characters with value of a C string.
       *  @param pos  Index of first character to replace.
       *  @param n1  Number of characters to be replaced.
       *  @param s  C string to insert.
       *  @return  Reference to this string.
       *  @throw  std::out_of_range  If @a pos > size().
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Removes the characters in the range [pos,pos + n1) from this string.
       *  In place, the first @a n characters of @a s are inserted.  If @a
       *  pos is beyond end of string, out_of_range is thrown.  If the length
       *  of result exceeds max_size(), length_error is thrown.  The value of
       *  the string doesn't change if an error is thrown.
      */
      basic_string&
      replace(size_type __pos, size_type __n1, const _CharT* __s)
      {
	__glibcxx_requires_string(__s);
	return this->replace(__pos, __n1, __s, traits_type::length(__s));
      }

      /**
       *  @brief  Replace characters with multiple characters.
       *  @param pos  Index of first character to replace.
       *  @param n1  Number of characters to be replaced.
       *  @param n2  Number of characters to insert.
       *  @param c  Character to insert.
       *  @return  Reference to this string.
       *  @throw  std::out_of_range  If @a pos > size().
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Removes the characters in the range [pos,pos + n1) from this string.
       *  In place, @a n2 copies of @a c are inserted.  If @a pos is beyond
       *  end of string, out_of_range is thrown.  If the length of result
       *  exceeds max_size(), length_error is thrown.  The value of the string
       *  doesn't change if an error is thrown.
      */
      basic_string&
      replace(size_type __pos, size_type __n1, size_type __n2, _CharT __c)
      { return _M_replace_aux(_M_check(__pos, "basic_string::replace"),
			      _M_limit(__pos, __n1), __n2, __c); }

      /**
       *  @brief  Replace range of characters with string.
       *  @param i1  Iterator referencing start of range to replace.
       *  @param i2  Iterator referencing end of range to replace.
       *  @param str  String value to insert.
       *  @return  Reference to this string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Removes the characters in the range [i1,i2).  In place, the value of
       *  @a str is inserted.  If the length of result exceeds max_size(),
       *  length_error is thrown.  The value of the string doesn't change if
       *  an error is thrown.
      */
      basic_string&
      replace(iterator __i1, iterator __i2, const basic_string& __str)
      { return this->replace(__i1, __i2, __str._M_data(), __str.size()); }

      /**
       *  @brief  Replace range of characters with C substring.
       *  @param i1  Iterator referencing start of range to replace.
       *  @param i2  Iterator referencing end of range to replace.
       *  @param s  C string value to insert.
       *  @param n  Number of characters from s to insert.
       *  @return  Reference to this string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Removes the characters in the range [i1,i2).  In place, the first @a
       *  n characters of @a s are inserted.  If the length of result exceeds
       *  max_size(), length_error is thrown.  The value of the string doesn't
       *  change if an error is thrown.
      */
      basic_string&
      replace(iterator __i1, iterator __i2, const _CharT* __s, size_type __n)
      {
	_GLIBCXX_DEBUG_PEDASSERT(_M_ibegin() <= __i1 && __i1 <= __i2
				 && __i2 <= _M_iend());
	return this->replace(__i1 - _M_ibegin(), __i2 - __i1, __s, __n);
      }

      /**
       *  @brief  Replace range of characters with C string.
       *  @param i1  Iterator referencing start of range to replace.
       *  @param i2  Iterator referencing end of range to replace.
       *  @param s  C string value to insert.
       *  @return  Reference to this string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Removes the characters in the range [i1,i2).  In place, the
       *  characters of @a s are inserted.  If the length of result exceeds
       *  max_size(), length_error is thrown.  The value of the string doesn't
       *  change if an error is thrown.
      */
      basic_string&
      replace(iterator __i1, iterator __i2, const _CharT* __s)
      {
	__glibcxx_requires_string(__s);
	return this->replace(__i1, __i2, __s, traits_type::length(__s));
      }

      /**
       *  @brief  Replace range of characters with multiple characters
       *  @param i1  Iterator referencing start of range to replace.
       *  @param i2  Iterator referencing end of range to replace.
       *  @param n  Number of characters to insert.
       *  @param c  Character to insert.
       *  @return  Reference to this string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Removes the characters in the range [i1,i2).  In place, @a n copies
       *  of @a c are inserted.  If the length of result exceeds max_size(),
       *  length_error is thrown.  The value of the string doesn't change if
       *  an error is thrown.
      */
      basic_string&
      replace(iterator __i1, iterator __i2, size_type __n, _CharT __c)
      {
	_GLIBCXX_DEBUG_PEDASSERT(_M_ibegin() <= __i1 && __i1 <= __i2
				 && __i2 <= _M_iend());
	return _M_replace_aux(__i1 - _M_ibegin(), __i2 - __i1, __n, __c);
      }

      /**
       *  @brief  Replace range of characters with range.
       *  @param i1  Iterator referencing start of range to replace.
       *  @param i2  Iterator referencing end of range to replace.
       *  @param k1  Iterator referencing start of range to insert.
       *  @param k2  Iterator referencing end of range to insert.
       *  @return  Reference to this string.
       *  @throw  std::length_error  If new length exceeds @c max_size().
       *
       *  Removes the characters in the range [i1,i2).  In place, characters
       *  in the range [k1,k2) are inserted.  If the length of result exceeds
       *  max_size(), length_error is thrown.  The value of the string doesn't
       *  change if an error is thrown.
      */
      template<class _InputIterator>
        basic_string&
        replace(iterator __i1, iterator __i2,
		_InputIterator __k1, _InputIterator __k2)
        {
	  _GLIBCXX_DEBUG_PEDASSERT(_M_ibegin() <= __i1 && __i1 <= __i2
				   && __i2 <= _M_iend());
	  __glibcxx_requires_valid_range(__k1, __k2);
	  typedef typename std::__is_integer<_InputIterator>::__type _Integral;
	  return _M_replace_dispatch(__i1, __i2, __k1, __k2, _Integral());
	}

      // Specializations for the common case of pointer and iterator:
      // useful to avoid the overhead of temporary buffering in _M_replace.
      basic_string&
      replace(iterator __i1, iterator __i2, _CharT* __k1, _CharT* __k2)
      {
	_GLIBCXX_DEBUG_PEDASSERT(_M_ibegin() <= __i1 && __i1 <= __i2
				 && __i2 <= _M_iend());
	__glibcxx_requires_valid_range(__k1, __k2);
	return this->replace(__i1 - _M_ibegin(), __i2 - __i1,
			     __k1, __k2 - __k1);
      }

      basic_string&
      replace(iterator __i1, iterator __i2,
	      const _CharT* __k1, const _CharT* __k2)
      {
	_GLIBCXX_DEBUG_PEDASSERT(_M_ibegin() <= __i1 && __i1 <= __i2
				 && __i2 <= _M_iend());
	__glibcxx_requires_valid_range(__k1, __k2);
	return this->replace(__i1 - _M_ibegin(), __i2 - __i1,
			     __k1, __k2 - __k1);
      }

      basic_string&
      replace(iterator __i1, iterator __i2, iterator __k1, iterator __k2)
      {
	_GLIBCXX_DEBUG_PEDASSERT(_M_ibegin() <= __i1 && __i1 <= __i2
				 && __i2 <= _M_iend());
	__glibcxx_requires_valid_range(__k1, __k2);
	return this->replace(__i1 - _M_ibegin(), __i2 - __i1,
			     __k1.base(), __k2 - __k1);
      }

      basic_string&
      replace(iterator __i1, iterator __i2,
	      const_iterator __k1, const_iterator __k2)
      {
	_GLIBCXX_DEBUG_PEDASSERT(_M_ibegin() <= __i1 && __i1 <= __i2
				 && __i2 <= _M_iend());
	__glibcxx_requires_valid_range(__k1, __k2);
	return this->replace(__i1 - _M_ibegin(), __i2 - __i1,
			     __k1.base(), __k2 - __k1);
      }
      
    private:
      template<class _Integer>
	basic_string&
	_M_replace_dispatch(iterator __i1, iterator __i2, _Integer __n,
			    _Integer __val, __true_type)
        { return _M_replace_aux(__i1 - _M_ibegin(), __i2 - __i1, __n, __val); }

      template<class _InputIterator>
	basic_string&
	_M_replace_dispatch(iterator __i1, iterator __i2, _InputIterator __k1,
			    _InputIterator __k2, __false_type);

      basic_string&
      _M_replace_aux(size_type __pos1, size_type __n1, size_type __n2,
		     _CharT __c);

      basic_string&
      _M_replace_safe(size_type __pos1, size_type __n1, const _CharT* __s,
		      size_type __n2);

      // _S_construct_aux is used to implement the 21.3.1 para 15 which
      // requires special behaviour if _InIter is an integral type
      template<class _InIterator>
        static _CharT*
        _S_construct_aux(_InIterator __beg, _InIterator __end,
			 const _Alloc& __a, __false_type)
	{
          typedef typename iterator_traits<_InIterator>::iterator_category _Tag;
          return _S_construct(__beg, __end, __a, _Tag());
	}

      template<class _InIterator>
        static _CharT*
        _S_construct_aux(_InIterator __beg, _InIterator __end,
			 const _Alloc& __a, __true_type)
	{ return _S_construct(static_cast<size_type>(__beg),
			      static_cast<value_type>(__end), __a); }

      template<class _InIterator>
        static _CharT*
        _S_construct(_InIterator __beg, _InIterator __end, const _Alloc& __a)
	{
	  typedef typename std::__is_integer<_InIterator>::__type _Integral;
	  return _S_construct_aux(__beg, __end, __a, _Integral());
        }

      // For Input Iterators, used in istreambuf_iterators, etc.
      template<class _InIterator>
        static _CharT*
         _S_construct(_InIterator __beg, _InIterator __end, const _Alloc& __a,
		      input_iterator_tag);

      // For forward_iterators up to random_access_iterators, used for
      // string::iterator, _CharT*, etc.
      template<class _FwdIterator>
        static _CharT*
        _S_construct(_FwdIterator __beg, _FwdIterator __end, const _Alloc& __a,
		     forward_iterator_tag);

      static _CharT*
      _S_construct(size_type __req, _CharT __c, const _Alloc& __a);

    public:

      /**
       *  @brief  Copy substring into C string.
       *  @param s  C string to copy value into.
       *  @param n  Number of characters to copy.
       *  @param pos  Index of first character to copy.
       *  @return  Number of characters actually copied
       *  @throw  std::out_of_range  If pos > size().
       *
       *  Copies up to @a n characters starting at @a pos into the C string @a
       *  s.  If @a pos is greater than size(), out_of_range is thrown.
      */
      size_type
      copy(_CharT* __s, size_type __n, size_type __pos = 0) const;

      /**
       *  @brief  Swap contents with another string.
       *  @param s  String to swap with.
       *
       *  Exchanges the contents of this string with that of @a s in constant
       *  time.
      */
      void
      swap(basic_string& __s);

      // String operations:
      /**
       *  @brief  Return const pointer to null-terminated contents.
       *
       *  This is a handle to internal data.  Do not modify or dire things may
       *  happen.
      */
      const _CharT*
      c_str() const
      { return _M_data(); }

      /**
       *  @brief  Return const pointer to contents.
       *
       *  This is a handle to internal data.  Do not modify or dire things may
       *  happen.
      */
      const _CharT*
      data() const
      { return _M_data(); }

      /**
       *  @brief  Return copy of allocator used to construct this string.
      */
      allocator_type
      get_allocator() const
      { return _M_dataplus; }

      /**
       *  @brief  Find position of a C substring.
       *  @param s  C string to locate.
       *  @param pos  Index of character to search from.
       *  @param n  Number of characters from @a s to search for.
       *  @return  Index of start of first occurrence.
       *
       *  Starting from @a pos, searches forward for the first @a n characters
       *  in @a s within this string.  If found, returns the index where it
       *  begins.  If not found, returns npos.
      */
      size_type
      find(const _CharT* __s, size_type __pos, size_type __n) const;

      /**
       *  @brief  Find position of a string.
       *  @param str  String to locate.
       *  @param pos  Index of character to search from (default 0).
       *  @return  Index of start of first occurrence.
       *
       *  Starting from @a pos, searches forward for value of @a str within
       *  this string.  If found, returns the index where it begins.  If not
       *  found, returns npos.
      */
      size_type
      find(const basic_string& __str, size_type __pos = 0) const
      { return this->find(__str.data(), __pos, __str.size()); }

      /**
       *  @brief  Find position of a C string.
       *  @param s  C string to locate.
       *  @param pos  Index of character to search from (default 0).
       *  @return  Index of start of first occurrence.
       *
       *  Starting from @a pos, searches forward for the value of @a s within
       *  this string.  If found, returns the index where it begins.  If not
       *  found, returns npos.
      */
      size_type
      find(const _CharT* __s, size_type __pos = 0) const
      {
	__glibcxx_requires_string(__s);
	return this->find(__s, __pos, traits_type::length(__s));
      }

      /**
       *  @brief  Find position of a character.
       *  @param c  Character to locate.
       *  @param pos  Index of character to search from (default 0).
       *  @return  Index of first occurrence.
       *
       *  Starting from @a pos, searches forward for @a c within this string.
       *  If found, returns the index where it was found.  If not found,
       *  returns npos.
      */
      size_type
      find(_CharT __c, size_type __pos = 0) const;

      /**
       *  @brief  Find last position of a string.
       *  @param str  String to locate.
       *  @param pos  Index of character to search back from (default end).
       *  @return  Index of start of last occurrence.
       *
       *  Starting from @a pos, searches backward for value of @a str within
       *  this string.  If found, returns the index where it begins.  If not
       *  found, returns npos.
      */
      size_type
      rfind(const basic_string& __str, size_type __pos = npos) const
      { return this->rfind(__str.data(), __pos, __str.size()); }

      /**
       *  @brief  Find last position of a C substring.
       *  @param s  C string to locate.
       *  @param pos  Index of character to search back from.
       *  @param n  Number of characters from s to search for.
       *  @return  Index of start of last occurrence.
       *
       *  Starting from @a pos, searches backward for the first @a n
       *  characters in @a s within this string.  If found, returns the index
       *  where it begins.  If not found, returns npos.
      */
      size_type
      rfind(const _CharT* __s, size_type __pos, size_type __n) const;

      /**
       *  @brief  Find last position of a C string.
       *  @param s  C string to locate.
       *  @param pos  Index of character to start search at (default end).
       *  @return  Index of start of  last occurrence.
       *
       *  Starting from @a pos, searches backward for the value of @a s within
       *  this string.  If found, returns the index where it begins.  If not
       *  found, returns npos.
      */
      size_type
      rfind(const _CharT* __s, size_type __pos = npos) const
      {
	__glibcxx_requires_string(__s);
	return this->rfind(__s, __pos, traits_type::length(__s));
      }

      /**
       *  @brief  Find last position of a character.
       *  @param c  Character to locate.
       *  @param pos  Index of character to search back from (default end).
       *  @return  Index of last occurrence.
       *
       *  Starting from @a pos, searches backward for @a c within this string.
       *  If found, returns the index where it was found.  If not found,
       *  returns npos.
      */
      size_type
      rfind(_CharT __c, size_type __pos = npos) const;

      /**
       *  @brief  Find position of a character of string.
       *  @param str  String containing characters to locate.
       *  @param pos  Index of character to search from (default 0).
       *  @return  Index of first occurrence.
       *
       *  Starting from @a pos, searches forward for one of the characters of
       *  @a str within this string.  If found, returns the index where it was
       *  found.  If not found, returns npos.
      */
      size_type
      find_first_of(const basic_string& __str, size_type __pos = 0) const
      { return this->find_first_of(__str.data(), __pos, __str.size()); }

      /**
       *  @brief  Find position of a character of C substring.
       *  @param s  String containing characters to locate.
       *  @param pos  Index of character to search from.
       *  @param n  Number of characters from s to search for.
       *  @return  Index of first occurrence.
       *
       *  Starting from @a pos, searches forward for one of the first @a n
       *  characters of @a s within this string.  If found, returns the index
       *  where it was found.  If not found, returns npos.
      */
      size_type
      find_first_of(const _CharT* __s, size_type __pos, size_type __n) const;

      /**
       *  @brief  Find position of a character of C string.
       *  @param s  String containing characters to locate.
       *  @param pos  Index of character to search from (default 0).
       *  @return  Index of first occurrence.
       *
       *  Starting from @a pos, searches forward for one of the characters of
       *  @a s within this string.  If found, returns the index where it was
       *  found.  If not found, returns npos.
      */
      size_type
      find_first_of(const _CharT* __s, size_type __pos = 0) const
      {
	__glibcxx_requires_string(__s);
	return this->find_first_of(__s, __pos, traits_type::length(__s));
      }

      /**
       *  @brief  Find position of a character.
       *  @param c  Character to locate.
       *  @param pos  Index of character to search from (default 0).
       *  @return  Index of first occurrence.
       *
       *  Starting from @a pos, searches forward for the character @a c within
       *  this string.  If found, returns the index where it was found.  If
       *  not found, returns npos.
       *
       *  Note: equivalent to find(c, pos).
      */
      size_type
      find_first_of(_CharT __c, size_type __pos = 0) const
      { return this->find(__c, __pos); }

      /**
       *  @brief  Find last position of a character of string.
       *  @param str  String containing characters to locate.
       *  @param pos  Index of character to search back from (default end).
       *  @return  Index of last occurrence.
       *
       *  Starting from @a pos, searches backward for one of the characters of
       *  @a str within this string.  If found, returns the index where it was
       *  found.  If not found, returns npos.
      */
      size_type
      find_last_of(const basic_string& __str, size_type __pos = npos) const
      { return this->find_last_of(__str.data(), __pos, __str.size()); }

      /**
       *  @brief  Find last position of a character of C substring.
       *  @param s  C string containing characters to locate.
       *  @param pos  Index of character to search back from.
       *  @param n  Number of characters from s to search for.
       *  @return  Index of last occurrence.
       *
       *  Starting from @a pos, searches backward for one of the first @a n
       *  characters of @a s within this string.  If found, returns the index
       *  where it was found.  If not found, returns npos.
      */
      size_type
      find_last_of(const _CharT* __s, size_type __pos, size_type __n) const;

      /**
       *  @brief  Find last position of a character of C string.
       *  @param s  C string containing characters to locate.
       *  @param pos  Index of character to search back from (default end).
       *  @return  Index of last occurrence.
       *
       *  Starting from @a pos, searches backward for one of the characters of
       *  @a s within this string.  If found, returns the index where it was
       *  found.  If not found, returns npos.
      */
      size_type
      find_last_of(const _CharT* __s, size_type __pos = npos) const
      {
	__glibcxx_requires_string(__s);
	return this->find_last_of(__s, __pos, traits_type::length(__s));
      }

      /**
       *  @brief  Find last position of a character.
       *  @param c  Character to locate.
       *  @param pos  Index of character to search back from (default end).
       *  @return  Index of last occurrence.
       *
       *  Starting from @a pos, searches backward for @a c within this string.
       *  If found, returns the index where it was found.  If not found,
       *  returns npos.
       *
       *  Note: equivalent to rfind(c, pos).
      */
      size_type
      find_last_of(_CharT __c, size_type __pos = npos) const
      { return this->rfind(__c, __pos); }

      /**
       *  @brief  Find position of a character not in string.
       *  @param str  String containing characters to avoid.
       *  @param pos  Index of character to search from (default 0).
       *  @return  Index of first occurrence.
       *
       *  Starting from @a pos, searches forward for a character not contained
       *  in @a str within this string.  If found, returns the index where it
       *  was found.  If not found, returns npos.
      */
      size_type
      find_first_not_of(const basic_string& __str, size_type __pos = 0) const
      { return this->find_first_not_of(__str.data(), __pos, __str.size()); }

      /**
       *  @brief  Find position of a character not in C substring.
       *  @param s  C string containing characters to avoid.
       *  @param pos  Index of character to search from.
       *  @param n  Number of characters from s to consider.
       *  @return  Index of first occurrence.
       *
       *  Starting from @a pos, searches forward for a character not contained
       *  in the first @a n characters of @a s within this string.  If found,
       *  returns the index where it was found.  If not found, returns npos.
      */
      size_type
      find_first_not_of(const _CharT* __s, size_type __pos,
			size_type __n) const;

      /**
       *  @brief  Find position of a character not in C string.
       *  @param s  C string containing characters to avoid.
       *  @param pos  Index of character to search from (default 0).
       *  @return  Index of first occurrence.
       *
       *  Starting from @a pos, searches forward for a character not contained
       *  in @a s within this string.  If found, returns the index where it
       *  was found.  If not found, returns npos.
      */
      size_type
      find_first_not_of(const _CharT* __s, size_type __pos = 0) const
      {
	__glibcxx_requires_string(__s);
	return this->find_first_not_of(__s, __pos, traits_type::length(__s));
      }

      /**
       *  @brief  Find position of a different character.
       *  @param c  Character to avoid.
       *  @param pos  Index of character to search from (default 0).
       *  @return  Index of first occurrence.
       *
       *  Starting from @a pos, searches forward for a character other than @a c
       *  within this string.  If found, returns the index where it was found.
       *  If not found, returns npos.
      */
      size_type
      find_first_not_of(_CharT __c, size_type __pos = 0) const;

      /**
       *  @brief  Find last position of a character not in string.
       *  @param str  String containing characters to avoid.
       *  @param pos  Index of character to search back from (default end).
       *  @return  Index of last occurrence.
       *
       *  Starting from @a pos, searches backward for a character not
       *  contained in @a str within this string.  If found, returns the index
       *  where it was found.  If not found, returns npos.
      */
      size_type
      find_last_not_of(const basic_string& __str, size_type __pos = npos) const
      { return this->find_last_not_of(__str.data(), __pos, __str.size()); }

      /**
       *  @brief  Find last position of a character not in C substring.
       *  @param s  C string containing characters to avoid.
       *  @param pos  Index of character to search back from.
       *  @param n  Number of characters from s to consider.
       *  @return  Index of last occurrence.
       *
       *  Starting from @a pos, searches backward for a character not
       *  contained in the first @a n characters of @a s within this string.
       *  If found, returns the index where it was found.  If not found,
       *  returns npos.
      */
      size_type
      find_last_not_of(const _CharT* __s, size_type __pos,
		       size_type __n) const;
      /**
       *  @brief  Find last position of a character not in C string.
       *  @param s  C string containing characters to avoid.
       *  @param pos  Index of character to search back from (default end).
       *  @return  Index of last occurrence.
       *
       *  Starting from @a pos, searches backward for a character not
       *  contained in @a s within this string.  If found, returns the index
       *  where it was found.  If not found, returns npos.
      */
      size_type
      find_last_not_of(const _CharT* __s, size_type __pos = npos) const
      {
	__glibcxx_requires_string(__s);
	return this->find_last_not_of(__s, __pos, traits_type::length(__s));
      }

      /**
       *  @brief  Find last position of a different character.
       *  @param c  Character to avoid.
       *  @param pos  Index of character to search back from (default end).
       *  @return  Index of last occurrence.
       *
       *  Starting from @a pos, searches backward for a character other than
       *  @a c within this string.  If found, returns the index where it was
       *  found.  If not found, returns npos.
      */
      size_type
      find_last_not_of(_CharT __c, size_type __pos = npos) const;

      /**
       *  @brief  Get a substring.
       *  @param pos  Index of first character (default 0).
       *  @param n  Number of characters in substring (default remainder).
       *  @return  The new string.
       *  @throw  std::out_of_range  If pos > size().
       *
       *  Construct and return a new string using the @a n characters starting
       *  at @a pos.  If the string is too short, use the remainder of the
       *  characters.  If @a pos is beyond the end of the string, out_of_range
       *  is thrown.
      */
      basic_string
      substr(size_type __pos = 0, size_type __n = npos) const
      { return basic_string(*this,
			    _M_check(__pos, "basic_string::substr"), __n); }

      /**
       *  @brief  Compare to a string.
       *  @param str  String to compare against.
       *  @return  Integer < 0, 0, or > 0.
       *
       *  Returns an integer < 0 if this string is ordered before @a str, 0 if
       *  their values are equivalent, or > 0 if this string is ordered after
       *  @a str.  Determines the effective length rlen of the strings to
       *  compare as the smallest of size() and str.size().  The function
       *  then compares the two strings by calling traits::compare(data(),
       *  str.data(),rlen).  If the result of the comparison is nonzero returns
       *  it, otherwise the shorter one is ordered first.
      */
      int
      compare(const basic_string& __str) const
      {
	const size_type __size = this->size();
	const size_type __osize = __str.size();
	const size_type __len = std::min(__size, __osize);

	int __r = traits_type::compare(_M_data(), __str.data(), __len);
	if (!__r)
	  __r =  _S_compare(__size, __osize);
	return __r;
      }

      /**
       *  @brief  Compare substring to a string.
       *  @param pos  Index of first character of substring.
       *  @param n  Number of characters in substring.
       *  @param str  String to compare against.
       *  @return  Integer < 0, 0, or > 0.
       *
       *  Form the substring of this string from the @a n characters starting
       *  at @a pos.  Returns an integer < 0 if the substring is ordered
       *  before @a str, 0 if their values are equivalent, or > 0 if the
       *  substring is ordered after @a str.  Determines the effective length
       *  rlen of the strings to compare as the smallest of the length of the
       *  substring and @a str.size().  The function then compares the two
       *  strings by calling traits::compare(substring.data(),str.data(),rlen).
       *  If the result of the comparison is nonzero returns it, otherwise the
       *  shorter one is ordered first.
      */
      int
      compare(size_type __pos, size_type __n, const basic_string& __str) const;

      /**
       *  @brief  Compare substring to a substring.
       *  @param pos1  Index of first character of substring.
       *  @param n1  Number of characters in substring.
       *  @param str  String to compare against.
       *  @param pos2  Index of first character of substring of str.
       *  @param n2  Number of characters in substring of str.
       *  @return  Integer < 0, 0, or > 0.
       *
       *  Form the substring of this string from the @a n1 characters starting
       *  at @a pos1.  Form the substring of @a str from the @a n2 characters
       *  starting at @a pos2.  Returns an integer < 0 if this substring is
       *  ordered before the substring of @a str, 0 if their values are
       *  equivalent, or > 0 if this substring is ordered after the substring
       *  of @a str.  Determines the effective length rlen of the strings
       *  to compare as the smallest of the lengths of the substrings.  The
       *  function then compares the two strings by calling
       *  traits::compare(substring.data(),str.substr(pos2,n2).data(),rlen).
       *  If the result of the comparison is nonzero returns it, otherwise the
       *  shorter one is ordered first.
      */
      int
      compare(size_type __pos1, size_type __n1, const basic_string& __str,
	      size_type __pos2, size_type __n2) const;

      /**
       *  @brief  Compare to a C string.
       *  @param s  C string to compare against.
       *  @return  Integer < 0, 0, or > 0.
       *
       *  Returns an integer < 0 if this string is ordered before @a s, 0 if
       *  their values are equivalent, or > 0 if this string is ordered after
       *  @a s.  Determines the effective length rlen of the strings to
       *  compare as the smallest of size() and the length of a string
       *  constructed from @a s.  The function then compares the two strings
       *  by calling traits::compare(data(),s,rlen).  If the result of the
       *  comparison is nonzero returns it, otherwise the shorter one is
       *  ordered first.
      */
      int
      compare(const _CharT* __s) const;

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 5 String::compare specification questionable
      /**
       *  @brief  Compare substring to a C string.
       *  @param pos  Index of first character of substring.
       *  @param n1  Number of characters in substring.
       *  @param s  C string to compare against.
       *  @return  Integer < 0, 0, or > 0.
       *
       *  Form the substring of this string from the @a n1 characters starting
       *  at @a pos.  Returns an integer < 0 if the substring is ordered
       *  before @a s, 0 if their values are equivalent, or > 0 if the
       *  substring is ordered after @a s.  Determines the effective length
       *  rlen of the strings to compare as the smallest of the length of the 
       *  substring and the length of a string constructed from @a s.  The
       *  function then compares the two string by calling
       *  traits::compare(substring.data(),s,rlen).  If the result of the
       *  comparison is nonzero returns it, otherwise the shorter one is
       *  ordered first.
      */
      int
      compare(size_type __pos, size_type __n1, const _CharT* __s) const;

      /**
       *  @brief  Compare substring against a character array.
       *  @param pos1  Index of first character of substring.
       *  @param n1  Number of characters in substring.
       *  @param s  character array to compare against.
       *  @param n2  Number of characters of s.
       *  @return  Integer < 0, 0, or > 0.
       *
       *  Form the substring of this string from the @a n1 characters starting
       *  at @a pos1.  Form a string from the first @a n2 characters of @a s.
       *  Returns an integer < 0 if this substring is ordered before the string
       *  from @a s, 0 if their values are equivalent, or > 0 if this substring
       *  is ordered after the string from @a s.   Determines the effective
       *  length rlen of the strings to compare as the smallest of the length
       *  of the substring and @a n2.  The function then compares the two
       *  strings by calling traits::compare(substring.data(),s,rlen).  If the
       *  result of the comparison is nonzero returns it, otherwise the shorter
       *  one is ordered first.
       *
       *  NB: s must have at least n2 characters, '\0' has no special
       *  meaning.
      */
      int
      compare(size_type __pos, size_type __n1, const _CharT* __s,
	      size_type __n2) const;
  };

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline basic_string<_CharT, _Traits, _Alloc>::
    basic_string()
#ifndef _GLIBCXX_FULLY_DYNAMIC_STRING
    : _M_dataplus(_S_empty_rep()._M_refdata(), _Alloc()) { }
#else
    : _M_dataplus(_S_construct(size_type(), _CharT(), _Alloc()), _Alloc()) { }
#endif

  // operator+
  /**
   *  @brief  Concatenate two strings.
   *  @param lhs  First string.
   *  @param rhs  Last string.
   *  @return  New string with value of @a lhs followed by @a rhs.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>
    operator+(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    {
      basic_string<_CharT, _Traits, _Alloc> __str(__lhs);
      __str.append(__rhs);
      return __str;
    }

  /**
   *  @brief  Concatenate C string and string.
   *  @param lhs  First string.
   *  @param rhs  Last string.
   *  @return  New string with value of @a lhs followed by @a rhs.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT,_Traits,_Alloc>
    operator+(const _CharT* __lhs,
	      const basic_string<_CharT,_Traits,_Alloc>& __rhs);

  /**
   *  @brief  Concatenate character and string.
   *  @param lhs  First string.
   *  @param rhs  Last string.
   *  @return  New string with @a lhs followed by @a rhs.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT,_Traits,_Alloc>
    operator+(_CharT __lhs, const basic_string<_CharT,_Traits,_Alloc>& __rhs);

  /**
   *  @brief  Concatenate string and C string.
   *  @param lhs  First string.
   *  @param rhs  Last string.
   *  @return  New string with @a lhs followed by @a rhs.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline basic_string<_CharT, _Traits, _Alloc>
    operator+(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	     const _CharT* __rhs)
    {
      basic_string<_CharT, _Traits, _Alloc> __str(__lhs);
      __str.append(__rhs);
      return __str;
    }

  /**
   *  @brief  Concatenate string and character.
   *  @param lhs  First string.
   *  @param rhs  Last string.
   *  @return  New string with @a lhs followed by @a rhs.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline basic_string<_CharT, _Traits, _Alloc>
    operator+(const basic_string<_CharT, _Traits, _Alloc>& __lhs, _CharT __rhs)
    {
      typedef basic_string<_CharT, _Traits, _Alloc>	__string_type;
      typedef typename __string_type::size_type		__size_type;
      __string_type __str(__lhs);
      __str.append(__size_type(1), __rhs);
      return __str;
    }

  // operator ==
  /**
   *  @brief  Test equivalence of two strings.
   *  @param lhs  First string.
   *  @param rhs  Second string.
   *  @return  True if @a lhs.compare(@a rhs) == 0.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator==(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __lhs.compare(__rhs) == 0; }

  /**
   *  @brief  Test equivalence of C string and string.
   *  @param lhs  C string.
   *  @param rhs  String.
   *  @return  True if @a rhs.compare(@a lhs) == 0.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator==(const _CharT* __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) == 0; }

  /**
   *  @brief  Test equivalence of string and C string.
   *  @param lhs  String.
   *  @param rhs  C string.
   *  @return  True if @a lhs.compare(@a rhs) == 0.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator==(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const _CharT* __rhs)
    { return __lhs.compare(__rhs) == 0; }

  // operator !=
  /**
   *  @brief  Test difference of two strings.
   *  @param lhs  First string.
   *  @param rhs  Second string.
   *  @return  True if @a lhs.compare(@a rhs) != 0.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator!=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) != 0; }

  /**
   *  @brief  Test difference of C string and string.
   *  @param lhs  C string.
   *  @param rhs  String.
   *  @return  True if @a rhs.compare(@a lhs) != 0.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator!=(const _CharT* __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) != 0; }

  /**
   *  @brief  Test difference of string and C string.
   *  @param lhs  String.
   *  @param rhs  C string.
   *  @return  True if @a lhs.compare(@a rhs) != 0.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator!=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const _CharT* __rhs)
    { return __lhs.compare(__rhs) != 0; }

  // operator <
  /**
   *  @brief  Test if string precedes string.
   *  @param lhs  First string.
   *  @param rhs  Second string.
   *  @return  True if @a lhs precedes @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __lhs.compare(__rhs) < 0; }

  /**
   *  @brief  Test if string precedes C string.
   *  @param lhs  String.
   *  @param rhs  C string.
   *  @return  True if @a lhs precedes @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	      const _CharT* __rhs)
    { return __lhs.compare(__rhs) < 0; }

  /**
   *  @brief  Test if C string precedes string.
   *  @param lhs  C string.
   *  @param rhs  String.
   *  @return  True if @a lhs precedes @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<(const _CharT* __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) > 0; }

  // operator >
  /**
   *  @brief  Test if string follows string.
   *  @param lhs  First string.
   *  @param rhs  Second string.
   *  @return  True if @a lhs follows @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __lhs.compare(__rhs) > 0; }

  /**
   *  @brief  Test if string follows C string.
   *  @param lhs  String.
   *  @param rhs  C string.
   *  @return  True if @a lhs follows @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	      const _CharT* __rhs)
    { return __lhs.compare(__rhs) > 0; }

  /**
   *  @brief  Test if C string follows string.
   *  @param lhs  C string.
   *  @param rhs  String.
   *  @return  True if @a lhs follows @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>(const _CharT* __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) < 0; }

  // operator <=
  /**
   *  @brief  Test if string doesn't follow string.
   *  @param lhs  First string.
   *  @param rhs  Second string.
   *  @return  True if @a lhs doesn't follow @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __lhs.compare(__rhs) <= 0; }

  /**
   *  @brief  Test if string doesn't follow C string.
   *  @param lhs  String.
   *  @param rhs  C string.
   *  @return  True if @a lhs doesn't follow @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const _CharT* __rhs)
    { return __lhs.compare(__rhs) <= 0; }

  /**
   *  @brief  Test if C string doesn't follow string.
   *  @param lhs  C string.
   *  @param rhs  String.
   *  @return  True if @a lhs doesn't follow @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<=(const _CharT* __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) >= 0; }

  // operator >=
  /**
   *  @brief  Test if string doesn't precede string.
   *  @param lhs  First string.
   *  @param rhs  Second string.
   *  @return  True if @a lhs doesn't precede @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __lhs.compare(__rhs) >= 0; }

  /**
   *  @brief  Test if string doesn't precede C string.
   *  @param lhs  String.
   *  @param rhs  C string.
   *  @return  True if @a lhs doesn't precede @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const _CharT* __rhs)
    { return __lhs.compare(__rhs) >= 0; }

  /**
   *  @brief  Test if C string doesn't precede string.
   *  @param lhs  C string.
   *  @param rhs  String.
   *  @return  True if @a lhs doesn't precede @a rhs.  False otherwise.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>=(const _CharT* __lhs,
	     const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) <= 0; }

  /**
   *  @brief  Swap contents of two strings.
   *  @param lhs  First string.
   *  @param rhs  Second string.
   *
   *  Exchanges the contents of @a lhs and @a rhs in constant time.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline void
    swap(basic_string<_CharT, _Traits, _Alloc>& __lhs,
	 basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { __lhs.swap(__rhs); }

  /**
   *  @brief  Read stream into a string.
   *  @param is  Input stream.
   *  @param str  Buffer to store into.
   *  @return  Reference to the input stream.
   *
   *  Stores characters from @a is into @a str until whitespace is found, the
   *  end of the stream is encountered, or str.max_size() is reached.  If
   *  is.width() is non-zero, that is the limit on the number of characters
   *  stored into @a str.  Any previous contents of @a str are erased.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_istream<_CharT, _Traits>&
    operator>>(basic_istream<_CharT, _Traits>& __is,
	       basic_string<_CharT, _Traits, _Alloc>& __str);

  template<>
    basic_istream<char>&
    operator>>(basic_istream<char>& __is, basic_string<char>& __str);

  /**
   *  @brief  Write string to a stream.
   *  @param os  Output stream.
   *  @param str  String to write out.
   *  @return  Reference to the output stream.
   *
   *  Output characters of @a str into os following the same rules as for
   *  writing a C string.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __os,
	       const basic_string<_CharT, _Traits, _Alloc>& __str)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 586. string inserter not a formatted function
      return __ostream_insert(__os, __str.data(), __str.size());
    }

  /**
   *  @brief  Read a line from stream into a string.
   *  @param is  Input stream.
   *  @param str  Buffer to store into.
   *  @param delim  Character marking end of line.
   *  @return  Reference to the input stream.
   *
   *  Stores characters from @a is into @a str until @a delim is found, the
   *  end of the stream is encountered, or str.max_size() is reached.  If
   *  is.width() is non-zero, that is the limit on the number of characters
   *  stored into @a str.  Any previous contents of @a str are erased.  If @a
   *  delim was encountered, it is extracted but not stored into @a str.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_istream<_CharT, _Traits>&
    getline(basic_istream<_CharT, _Traits>& __is,
	    basic_string<_CharT, _Traits, _Alloc>& __str, _CharT __delim);

  /**
   *  @brief  Read a line from stream into a string.
   *  @param is  Input stream.
   *  @param str  Buffer to store into.
   *  @return  Reference to the input stream.
   *
   *  Stores characters from is into @a str until '\n' is found, the end of
   *  the stream is encountered, or str.max_size() is reached.  If is.width()
   *  is non-zero, that is the limit on the number of characters stored into
   *  @a str.  Any previous contents of @a str are erased.  If end of line was
   *  encountered, it is extracted but not stored into @a str.
   */
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline basic_istream<_CharT, _Traits>&
    getline(basic_istream<_CharT, _Traits>& __is,
	    basic_string<_CharT, _Traits, _Alloc>& __str)
    { return getline(__is, __str, __is.widen('\n')); }

  template<>
    basic_istream<char>&
    getline(basic_istream<char>& __in, basic_string<char>& __str,
	    char __delim);

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    basic_istream<wchar_t>&
    getline(basic_istream<wchar_t>& __in, basic_string<wchar_t>& __str,
	    wchar_t __delim);
#endif  

_GLIBCXX_END_NAMESPACE

#endif /* _BASIC_STRING_H */
