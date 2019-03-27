// Allocators -*- C++ -*-

// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006
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

/*
 * Copyright (c) 1996-1997
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

/** @file ext/pool_allocator.h
 *  This file is a GNU extension to the Standard C++ Library.
 */

#ifndef _POOL_ALLOCATOR_H
#define _POOL_ALLOCATOR_H 1

#include <bits/c++config.h>
#include <cstdlib>
#include <new>
#include <bits/functexcept.h>
#include <ext/atomicity.h>
#include <ext/concurrence.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  using std::size_t;
  using std::ptrdiff_t;

  /**
   *  @brief  Base class for __pool_alloc.
   *
   *  @if maint
   *  Uses various allocators to fulfill underlying requests (and makes as
   *  few requests as possible when in default high-speed pool mode).
   *
   *  Important implementation properties:
   *  0. If globally mandated, then allocate objects from new
   *  1. If the clients request an object of size > _S_max_bytes, the resulting
   *     object will be obtained directly from new
   *  2. In all other cases, we allocate an object of size exactly
   *     _S_round_up(requested_size).  Thus the client has enough size
   *     information that we can return the object to the proper free list
   *     without permanently losing part of the object.
   *
   *  @endif
   */
    class __pool_alloc_base
    {
    protected:

      enum { _S_align = 8 };
      enum { _S_max_bytes = 128 };
      enum { _S_free_list_size = (size_t)_S_max_bytes / (size_t)_S_align };
      
      union _Obj
      {
	union _Obj* _M_free_list_link;
	char        _M_client_data[1];    // The client sees this.
      };
      
      static _Obj* volatile         _S_free_list[_S_free_list_size];

      // Chunk allocation state.
      static char*                  _S_start_free;
      static char*                  _S_end_free;
      static size_t                 _S_heap_size;     
      
      size_t
      _M_round_up(size_t __bytes)
      { return ((__bytes + (size_t)_S_align - 1) & ~((size_t)_S_align - 1)); }
      
      _Obj* volatile*
      _M_get_free_list(size_t __bytes);
    
      __mutex&
      _M_get_mutex();

      // Returns an object of size __n, and optionally adds to size __n
      // free list.
      void*
      _M_refill(size_t __n);
      
      // Allocates a chunk for nobjs of size size.  nobjs may be reduced
      // if it is inconvenient to allocate the requested number.
      char*
      _M_allocate_chunk(size_t __n, int& __nobjs);
    };


  /// @brief  class __pool_alloc.
  template<typename _Tp>
    class __pool_alloc : private __pool_alloc_base
    {
    private:
      static _Atomic_word	    _S_force_new;

    public:
      typedef size_t     size_type;
      typedef ptrdiff_t  difference_type;
      typedef _Tp*       pointer;
      typedef const _Tp* const_pointer;
      typedef _Tp&       reference;
      typedef const _Tp& const_reference;
      typedef _Tp        value_type;

      template<typename _Tp1>
        struct rebind
        { typedef __pool_alloc<_Tp1> other; };

      __pool_alloc() throw() { }

      __pool_alloc(const __pool_alloc&) throw() { }

      template<typename _Tp1>
        __pool_alloc(const __pool_alloc<_Tp1>&) throw() { }

      ~__pool_alloc() throw() { }

      pointer
      address(reference __x) const { return &__x; }

      const_pointer
      address(const_reference __x) const { return &__x; }

      size_type
      max_size() const throw() 
      { return size_t(-1) / sizeof(_Tp); }

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 402. wrong new expression in [some_] allocator::construct
      void 
      construct(pointer __p, const _Tp& __val) 
      { ::new(__p) _Tp(__val); }

      void 
      destroy(pointer __p) { __p->~_Tp(); }

      pointer
      allocate(size_type __n, const void* = 0);

      void
      deallocate(pointer __p, size_type __n);      
    };

  template<typename _Tp>
    inline bool
    operator==(const __pool_alloc<_Tp>&, const __pool_alloc<_Tp>&)
    { return true; }

  template<typename _Tp>
    inline bool
    operator!=(const __pool_alloc<_Tp>&, const __pool_alloc<_Tp>&)
    { return false; }

  template<typename _Tp>
    _Atomic_word
    __pool_alloc<_Tp>::_S_force_new;

  template<typename _Tp>
    _Tp*
    __pool_alloc<_Tp>::allocate(size_type __n, const void*)
    {
      pointer __ret = 0;
      if (__builtin_expect(__n != 0, true))
	{
	  if (__builtin_expect(__n > this->max_size(), false))
	    std::__throw_bad_alloc();

	  // If there is a race through here, assume answer from getenv
	  // will resolve in same direction.  Inspired by techniques
	  // to efficiently support threading found in basic_string.h.
	  if (_S_force_new == 0)
	    {
	      if (std::getenv("GLIBCXX_FORCE_NEW"))
		__atomic_add_dispatch(&_S_force_new, 1);
	      else
		__atomic_add_dispatch(&_S_force_new, -1);
	    }

	  const size_t __bytes = __n * sizeof(_Tp);	      
	  if (__bytes > size_t(_S_max_bytes) || _S_force_new == 1)
	    __ret = static_cast<_Tp*>(::operator new(__bytes));
	  else
	    {
	      _Obj* volatile* __free_list = _M_get_free_list(__bytes);
	      
	      __scoped_lock sentry(_M_get_mutex());
	      _Obj* __restrict__ __result = *__free_list;
	      if (__builtin_expect(__result == 0, 0))
		__ret = static_cast<_Tp*>(_M_refill(_M_round_up(__bytes)));
	      else
		{
		  *__free_list = __result->_M_free_list_link;
		  __ret = reinterpret_cast<_Tp*>(__result);
		}
	      if (__builtin_expect(__ret == 0, 0))
		std::__throw_bad_alloc();
	    }
	}
      return __ret;
    }

  template<typename _Tp>
    void
    __pool_alloc<_Tp>::deallocate(pointer __p, size_type __n)
    {
      if (__builtin_expect(__n != 0 && __p != 0, true))
	{
	  const size_t __bytes = __n * sizeof(_Tp);
	  if (__bytes > static_cast<size_t>(_S_max_bytes) || _S_force_new == 1)
	    ::operator delete(__p);
	  else
	    {
	      _Obj* volatile* __free_list = _M_get_free_list(__bytes);
	      _Obj* __q = reinterpret_cast<_Obj*>(__p);

	      __scoped_lock sentry(_M_get_mutex());
	      __q ->_M_free_list_link = *__free_list;
	      *__free_list = __q;
	    }
	}
    }

_GLIBCXX_END_NAMESPACE

#endif
