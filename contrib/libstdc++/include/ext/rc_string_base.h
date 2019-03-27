// Reference-counted versatile string base -*- C++ -*-

// Copyright (C) 2005, 2006 Free Software Foundation, Inc.
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

/** @file ext/rc_string_base.h
 *  This file is a GNU extension to the Standard C++ Library.
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _RC_STRING_BASE_H
#define _RC_STRING_BASE_H 1

#include <ext/atomicity.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  /**
   *  @if maint
   *  Documentation?  What's that?
   *  Nathan Myers <ncm@cantrip.org>.
   *
   *  A string looks like this:
   *
   *  @code
   *                                        [_Rep]
   *                                        _M_length
   *   [__rc_string_base<char_type>]        _M_capacity
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
   *  a single "add" instruction: _Rep::_M_refdata(), and
   *  __rc_string_base::_M_rep(); and the allocation function which gets a
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
 template<typename _CharT, typename _Traits, typename _Alloc>
    class __rc_string_base
    : protected __vstring_utility<_CharT, _Traits, _Alloc>
    {
    public:
      typedef _Traits					    traits_type;
      typedef typename _Traits::char_type		    value_type;
      typedef _Alloc					    allocator_type;

      typedef __vstring_utility<_CharT, _Traits, _Alloc>    _Util_Base;
      typedef typename _Util_Base::_CharT_alloc_type        _CharT_alloc_type;
      typedef typename _CharT_alloc_type::size_type	    size_type;

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
      //   4. All fields == 0 is an empty string, given the extra storage
      //      beyond-the-end for a null terminator; thus, the shared
      //      empty string representation needs no constructor.
      struct _Rep
      {
	union
	{
	  struct
	  {
	    size_type	    _M_length;
	    size_type	    _M_capacity;
	    _Atomic_word    _M_refcount;
	  }                 _M_info;
	  
	  // Only for alignment purposes.
	  _CharT            _M_align;
	};

	typedef typename _Alloc::template rebind<_Rep>::other _Rep_alloc_type;

 	_CharT*
	_M_refdata() throw()
	{ return reinterpret_cast<_CharT*>(this + 1); }

	_CharT*
	_M_refcopy() throw()
	{
	  __atomic_add_dispatch(&_M_info._M_refcount, 1);
	  return _M_refdata();
	}  // XXX MT
	
	void
	_M_set_length(size_type __n)
	{ 
	  _M_info._M_refcount = 0;  // One reference.
	  _M_info._M_length = __n;
	  // grrr. (per 21.3.4)
	  // You cannot leave those LWG people alone for a second.
	  traits_type::assign(_M_refdata()[__n], _CharT());
	}

	// Create & Destroy
	static _Rep*
	_S_create(size_type, size_type, const _Alloc&);

	void
	_M_destroy(const _Alloc&) throw();

	_CharT*
	_M_clone(const _Alloc&, size_type __res = 0);
      };

      struct _Rep_empty
      : public _Rep
      {
	_CharT              _M_terminal;
      };

      static _Rep_empty     _S_empty_rep;

      // The maximum number of individual char_type elements of an
      // individual string is determined by _S_max_size. This is the
      // value that will be returned by max_size().  (Whereas npos
      // is the maximum number of bytes the allocator can allocate.)
      // If one was to divvy up the theoretical largest size string,
      // with a terminating character and m _CharT elements, it'd
      // look like this:
      // npos = sizeof(_Rep) + (m * sizeof(_CharT)) + sizeof(_CharT)
      //        + sizeof(_Rep) - 1
      // (NB: last two terms for rounding reasons, see _M_create below)
      // Solving for m:
      // m = ((npos - 2 * sizeof(_Rep) + 1) / sizeof(_CharT)) - 1
      // In addition, this implementation halfs this amount.
      enum { _S_max_size = (((static_cast<size_type>(-1) - 2 * sizeof(_Rep)
			      + 1) / sizeof(_CharT)) - 1) / 2 };

      // Data Member (private):
      mutable typename _Util_Base::template _Alloc_hider<_Alloc>  _M_dataplus;

      void
      _M_data(_CharT* __p)
      { _M_dataplus._M_p = __p; }

      _Rep*
      _M_rep() const
      { return &((reinterpret_cast<_Rep*>(_M_data()))[-1]); }

      _CharT*
      _M_grab(const _Alloc& __alloc) const
      {
	return (!_M_is_leaked() && _M_get_allocator() == __alloc)
	        ? _M_rep()->_M_refcopy() : _M_rep()->_M_clone(__alloc);
      }

      void
      _M_dispose()
      {
	if (__exchange_and_add_dispatch(&_M_rep()->_M_info._M_refcount,
					-1) <= 0)
	  _M_rep()->_M_destroy(_M_get_allocator());
      }  // XXX MT

      bool
      _M_is_leaked() const
      { return _M_rep()->_M_info._M_refcount < 0; }

      void
      _M_set_sharable()
      { _M_rep()->_M_info._M_refcount = 0; }

      void
      _M_leak_hard();

      // _S_construct_aux is used to implement the 21.3.1 para 15 which
      // requires special behaviour if _InIterator is an integral type
      template<typename _InIterator>
        static _CharT*
        _S_construct_aux(_InIterator __beg, _InIterator __end,
			 const _Alloc& __a, std::__false_type)
	{
          typedef typename iterator_traits<_InIterator>::iterator_category _Tag;
          return _S_construct(__beg, __end, __a, _Tag());
	}

      template<typename _InIterator>
        static _CharT*
        _S_construct_aux(_InIterator __beg, _InIterator __end,
			 const _Alloc& __a, std::__true_type)
	{ return _S_construct(static_cast<size_type>(__beg),
			      static_cast<value_type>(__end), __a); }

      template<typename _InIterator>
        static _CharT*
        _S_construct(_InIterator __beg, _InIterator __end, const _Alloc& __a)
	{
	  typedef typename std::__is_integer<_InIterator>::__type _Integral;
	  return _S_construct_aux(__beg, __end, __a, _Integral());
        }

      // For Input Iterators, used in istreambuf_iterators, etc.
      template<typename _InIterator>
        static _CharT*
         _S_construct(_InIterator __beg, _InIterator __end, const _Alloc& __a,
		      std::input_iterator_tag);
      
      // For forward_iterators up to random_access_iterators, used for
      // string::iterator, _CharT*, etc.
      template<typename _FwdIterator>
        static _CharT*
        _S_construct(_FwdIterator __beg, _FwdIterator __end, const _Alloc& __a,
		     std::forward_iterator_tag);

      static _CharT*
      _S_construct(size_type __req, _CharT __c, const _Alloc& __a);

    public:
      size_type
      _M_max_size() const
      { return size_type(_S_max_size); }

      _CharT*
      _M_data() const
      { return _M_dataplus._M_p; }

      size_type
      _M_length() const
      { return _M_rep()->_M_info._M_length; }

      size_type
      _M_capacity() const
      { return _M_rep()->_M_info._M_capacity; }

      bool
      _M_is_shared() const
      { return _M_rep()->_M_info._M_refcount > 0; }

      void
      _M_set_leaked()
      { _M_rep()->_M_info._M_refcount = -1; }

      void
      _M_leak()    // for use in begin() & non-const op[]
      {
	if (!_M_is_leaked())
	  _M_leak_hard();
      }

      void
      _M_set_length(size_type __n)
      { _M_rep()->_M_set_length(__n); }

      __rc_string_base()
      : _M_dataplus(_Alloc(), _S_empty_rep._M_refcopy()) { }

      __rc_string_base(const _Alloc& __a);

      __rc_string_base(const __rc_string_base& __rcs);

      __rc_string_base(size_type __n, _CharT __c, const _Alloc& __a);

      template<typename _InputIterator>
        __rc_string_base(_InputIterator __beg, _InputIterator __end,
			 const _Alloc& __a);

      ~__rc_string_base()
      { _M_dispose(); }      

      allocator_type&
      _M_get_allocator()
      { return _M_dataplus; }

      const allocator_type&
      _M_get_allocator() const
      { return _M_dataplus; }

      void
      _M_swap(__rc_string_base& __rcs);

      void
      _M_assign(const __rc_string_base& __rcs);

      void
      _M_reserve(size_type __res);

      void
      _M_mutate(size_type __pos, size_type __len1, const _CharT* __s,
		size_type __len2);
      
      void
      _M_erase(size_type __pos, size_type __n);

      void
      _M_clear()
      { _M_erase(size_type(0), _M_length()); }

      bool
      _M_compare(const __rc_string_base&) const
      { return false; }
    };

  template<typename _CharT, typename _Traits, typename _Alloc>
    typename __rc_string_base<_CharT, _Traits, _Alloc>::_Rep_empty
    __rc_string_base<_CharT, _Traits, _Alloc>::_S_empty_rep;

  template<typename _CharT, typename _Traits, typename _Alloc>
    typename __rc_string_base<_CharT, _Traits, _Alloc>::_Rep*
    __rc_string_base<_CharT, _Traits, _Alloc>::_Rep::
    _S_create(size_type __capacity, size_type __old_capacity,
	      const _Alloc& __alloc)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 83.  String::npos vs. string::max_size()
      if (__capacity > size_type(_S_max_size))
	std::__throw_length_error(__N("__rc_string_base::_Rep::_S_create"));

      // The standard places no restriction on allocating more memory
      // than is strictly needed within this layer at the moment or as
      // requested by an explicit application call to reserve().

      // Many malloc implementations perform quite poorly when an
      // application attempts to allocate memory in a stepwise fashion
      // growing each allocation size by only 1 char.  Additionally,
      // it makes little sense to allocate less linear memory than the
      // natural blocking size of the malloc implementation.
      // Unfortunately, we would need a somewhat low-level calculation
      // with tuned parameters to get this perfect for any particular
      // malloc implementation.  Fortunately, generalizations about
      // common features seen among implementations seems to suffice.

      // __pagesize need not match the actual VM page size for good
      // results in practice, thus we pick a common value on the low
      // side.  __malloc_header_size is an estimate of the amount of
      // overhead per memory allocation (in practice seen N * sizeof
      // (void*) where N is 0, 2 or 4).  According to folklore,
      // picking this value on the high side is better than
      // low-balling it (especially when this algorithm is used with
      // malloc implementations that allocate memory blocks rounded up
      // to a size which is a power of 2).
      const size_type __pagesize = 4096;
      const size_type __malloc_header_size = 4 * sizeof(void*);

      // The below implements an exponential growth policy, necessary to
      // meet amortized linear time requirements of the library: see
      // http://gcc.gnu.org/ml/libstdc++/2001-07/msg00085.html.
      if (__capacity > __old_capacity && __capacity < 2 * __old_capacity)
	{
	  __capacity = 2 * __old_capacity;
	  // Never allocate a string bigger than _S_max_size.
	  if (__capacity > size_type(_S_max_size))
	    __capacity = size_type(_S_max_size);
	}

      // NB: Need an array of char_type[__capacity], plus a terminating
      // null char_type() element, plus enough for the _Rep data structure,
      // plus sizeof(_Rep) - 1 to upper round to a size multiple of
      // sizeof(_Rep).
      // Whew. Seemingly so needy, yet so elemental.
      size_type __size = ((__capacity + 1) * sizeof(_CharT)
			  + 2 * sizeof(_Rep) - 1);

      const size_type __adj_size = __size + __malloc_header_size;
      if (__adj_size > __pagesize && __capacity > __old_capacity)
	{
	  const size_type __extra = __pagesize - __adj_size % __pagesize;
	  __capacity += __extra / sizeof(_CharT);
	  if (__capacity > size_type(_S_max_size))
	    __capacity = size_type(_S_max_size);
	  __size = (__capacity + 1) * sizeof(_CharT) + 2 * sizeof(_Rep) - 1;
	}

      // NB: Might throw, but no worries about a leak, mate: _Rep()
      // does not throw.
      _Rep* __place = _Rep_alloc_type(__alloc).allocate(__size / sizeof(_Rep));
      _Rep* __p = new (__place) _Rep;
      __p->_M_info._M_capacity = __capacity;
      return __p;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __rc_string_base<_CharT, _Traits, _Alloc>::_Rep::
    _M_destroy(const _Alloc& __a) throw ()
    {
      const size_type __size = ((_M_info._M_capacity + 1) * sizeof(_CharT)
				+ 2 * sizeof(_Rep) - 1);
      _Rep_alloc_type(__a).deallocate(this, __size / sizeof(_Rep));
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    _CharT*
    __rc_string_base<_CharT, _Traits, _Alloc>::_Rep::
    _M_clone(const _Alloc& __alloc, size_type __res)
    {
      // Requested capacity of the clone.
      const size_type __requested_cap = _M_info._M_length + __res;
      _Rep* __r = _Rep::_S_create(__requested_cap, _M_info._M_capacity,
				  __alloc);

      if (_M_info._M_length)
	_S_copy(__r->_M_refdata(), _M_refdata(), _M_info._M_length);

      __r->_M_set_length(_M_info._M_length);
      return __r->_M_refdata();
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    __rc_string_base<_CharT, _Traits, _Alloc>::
    __rc_string_base(const _Alloc& __a)
    : _M_dataplus(__a, _S_construct(size_type(), _CharT(), __a)) { }

  template<typename _CharT, typename _Traits, typename _Alloc>
    __rc_string_base<_CharT, _Traits, _Alloc>::
    __rc_string_base(const __rc_string_base& __rcs)
    : _M_dataplus(__rcs._M_get_allocator(),
		  __rcs._M_grab(__rcs._M_get_allocator())) { }

  template<typename _CharT, typename _Traits, typename _Alloc>
    __rc_string_base<_CharT, _Traits, _Alloc>::
    __rc_string_base(size_type __n, _CharT __c, const _Alloc& __a)
    : _M_dataplus(__a, _S_construct(__n, __c, __a)) { }

  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InputIterator>
    __rc_string_base<_CharT, _Traits, _Alloc>::
    __rc_string_base(_InputIterator __beg, _InputIterator __end,
		     const _Alloc& __a)
    : _M_dataplus(__a, _S_construct(__beg, __end, __a)) { }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __rc_string_base<_CharT, _Traits, _Alloc>::
    _M_leak_hard()
    {
      if (_M_is_shared())
	_M_erase(0, 0);
      _M_set_leaked();
    }

  // NB: This is the special case for Input Iterators, used in
  // istreambuf_iterators, etc.
  // Input Iterators have a cost structure very different from
  // pointers, calling for a different coding style.
  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InIterator>
      _CharT*
      __rc_string_base<_CharT, _Traits, _Alloc>::
      _S_construct(_InIterator __beg, _InIterator __end, const _Alloc& __a,
		   std::input_iterator_tag)
      {
	if (__beg == __end && __a == _Alloc())
	  return _S_empty_rep._M_refcopy();

	// Avoid reallocation for common case.
	_CharT __buf[128];
	size_type __len = 0;
	while (__beg != __end && __len < sizeof(__buf) / sizeof(_CharT))
	  {
	    __buf[__len++] = *__beg;
	    ++__beg;
	  }
	_Rep* __r = _Rep::_S_create(__len, size_type(0), __a);
	_S_copy(__r->_M_refdata(), __buf, __len);
	try
	  {
	    while (__beg != __end)
	      {
		if (__len == __r->_M_info._M_capacity)
		  {
		    // Allocate more space.
		    _Rep* __another = _Rep::_S_create(__len + 1, __len, __a);
		    _S_copy(__another->_M_refdata(), __r->_M_refdata(), __len);
		    __r->_M_destroy(__a);
		    __r = __another;
		  }
		__r->_M_refdata()[__len++] = *__beg;
		++__beg;
	      }
	  }
	catch(...)
	  {
	    __r->_M_destroy(__a);
	    __throw_exception_again;
	  }
	__r->_M_set_length(__len);
	return __r->_M_refdata();
      }

  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InIterator>
      _CharT*
      __rc_string_base<_CharT, _Traits, _Alloc>::
      _S_construct(_InIterator __beg, _InIterator __end, const _Alloc& __a,
		   std::forward_iterator_tag)
      {
	if (__beg == __end && __a == _Alloc())
	  return _S_empty_rep._M_refcopy();

	// NB: Not required, but considered best practice.
	if (__builtin_expect(_S_is_null_pointer(__beg) && __beg != __end, 0))
	  std::__throw_logic_error(__N("__rc_string_base::"
				       "_S_construct NULL not valid"));

	const size_type __dnew = static_cast<size_type>(std::distance(__beg,
								      __end));
	// Check for out_of_range and length_error exceptions.
	_Rep* __r = _Rep::_S_create(__dnew, size_type(0), __a);
	try
	  { _S_copy_chars(__r->_M_refdata(), __beg, __end); }
	catch(...)
	  {
	    __r->_M_destroy(__a);
	    __throw_exception_again;
	  }
	__r->_M_set_length(__dnew);
	return __r->_M_refdata();
      }

  template<typename _CharT, typename _Traits, typename _Alloc>
    _CharT*
    __rc_string_base<_CharT, _Traits, _Alloc>::
    _S_construct(size_type __n, _CharT __c, const _Alloc& __a)
    {
      if (__n == 0 && __a == _Alloc())
	return _S_empty_rep._M_refcopy();

      // Check for out_of_range and length_error exceptions.
      _Rep* __r = _Rep::_S_create(__n, size_type(0), __a);
      if (__n)
	_S_assign(__r->_M_refdata(), __n, __c);

      __r->_M_set_length(__n);
      return __r->_M_refdata();
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __rc_string_base<_CharT, _Traits, _Alloc>::
    _M_swap(__rc_string_base& __rcs)
    {
      if (_M_is_leaked())
	_M_set_sharable();
      if (__rcs._M_is_leaked())
	__rcs._M_set_sharable();
      
      _CharT* __tmp = _M_data();
      _M_data(__rcs._M_data());
      __rcs._M_data(__tmp);

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 431. Swapping containers with unequal allocators.
      std::__alloc_swap<allocator_type>::_S_do_it(_M_get_allocator(),
						  __rcs._M_get_allocator());
    } 

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __rc_string_base<_CharT, _Traits, _Alloc>::
    _M_assign(const __rc_string_base& __rcs)
    {
      if (_M_rep() != __rcs._M_rep())
	{
	  _CharT* __tmp = __rcs._M_grab(_M_get_allocator());
	  _M_dispose();
	  _M_data(__tmp);
	}
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __rc_string_base<_CharT, _Traits, _Alloc>::
    _M_reserve(size_type __res)
    {
      // Make sure we don't shrink below the current size.
      if (__res < _M_length())
	__res = _M_length();
      
      if (__res != _M_capacity() || _M_is_shared())
	{
	  _CharT* __tmp = _M_rep()->_M_clone(_M_get_allocator(),
					     __res - _M_length());
	  _M_dispose();
	  _M_data(__tmp);
	}
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __rc_string_base<_CharT, _Traits, _Alloc>::
    _M_mutate(size_type __pos, size_type __len1, const _CharT* __s,
	      size_type __len2)
    {
      const size_type __how_much = _M_length() - __pos - __len1;
      
      _Rep* __r = _Rep::_S_create(_M_length() + __len2 - __len1,
				  _M_capacity(), _M_get_allocator());
      
      if (__pos)
	_S_copy(__r->_M_refdata(), _M_data(), __pos);
      if (__s && __len2)
	_S_copy(__r->_M_refdata() + __pos, __s, __len2);
      if (__how_much)
	_S_copy(__r->_M_refdata() + __pos + __len2,
		_M_data() + __pos + __len1, __how_much);
      
      _M_dispose();
      _M_data(__r->_M_refdata());
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __rc_string_base<_CharT, _Traits, _Alloc>::
    _M_erase(size_type __pos, size_type __n)
    {
      const size_type __new_size = _M_length() - __n;
      const size_type __how_much = _M_length() - __pos - __n;
      
      if (_M_is_shared())
	{
	  // Must reallocate.
	  _Rep* __r = _Rep::_S_create(__new_size, _M_capacity(),
				      _M_get_allocator());

	  if (__pos)
	    _S_copy(__r->_M_refdata(), _M_data(), __pos);
	  if (__how_much)
	    _S_copy(__r->_M_refdata() + __pos,
		    _M_data() + __pos + __n, __how_much);

	  _M_dispose();
	  _M_data(__r->_M_refdata());
	}
      else if (__how_much && __n)
	{
	  // Work in-place.
	  _S_move(_M_data() + __pos,
		  _M_data() + __pos + __n, __how_much);
	}

      _M_rep()->_M_set_length(__new_size);      
    }

  template<>
    inline bool
    __rc_string_base<char, std::char_traits<char>,
		     std::allocator<char> >::
    _M_compare(const __rc_string_base& __rcs) const
    {
      if (_M_rep() == __rcs._M_rep())
	return true;
      return false;
    }

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    inline bool
    __rc_string_base<wchar_t, std::char_traits<wchar_t>,
		     std::allocator<wchar_t> >::
    _M_compare(const __rc_string_base& __rcs) const
    {
      if (_M_rep() == __rcs._M_rep())
	return true;
      return false;
    }
#endif

_GLIBCXX_END_NAMESPACE

#endif /* _RC_STRING_BASE_H */
