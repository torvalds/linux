// array allocator -*- C++ -*-

// Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.
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

/** @file ext/array_allocator.h
 *  This file is a GNU extension to the Standard C++ Library.
 */

#ifndef _ARRAY_ALLOCATOR_H
#define _ARRAY_ALLOCATOR_H 1

#include <cstddef>
#include <new>
#include <bits/functexcept.h>
#include <tr1/array>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

 using std::size_t;
 using std::ptrdiff_t;

  /// @brief  Base class.
 template<typename _Tp>
    class array_allocator_base
    {
    public:
      typedef size_t     	size_type;
      typedef ptrdiff_t  	difference_type;
      typedef _Tp*       	pointer;
      typedef const _Tp* 	const_pointer;
      typedef _Tp&       	reference;
      typedef const _Tp&	const_reference;
      typedef _Tp        	value_type;

      pointer
      address(reference __x) const { return &__x; }

      const_pointer
      address(const_reference __x) const { return &__x; }

      void
      deallocate(pointer, size_type)
      { 
	// Does nothing.
      }

      size_type
      max_size() const throw() 
      { return size_t(-1) / sizeof(_Tp); }

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 402. wrong new expression in [some_] allocator::construct
      void 
      construct(pointer __p, const _Tp& __val) 
      { ::new(__p) value_type(__val); }

      void 
      destroy(pointer __p) { __p->~_Tp(); }
    };  

  /**
   *  @brief  An allocator that uses previously allocated memory.
   *  This memory can be externally, globally, or otherwise allocated.
   */
  template<typename _Tp, typename _Array = std::tr1::array<_Tp, 1> >
    class array_allocator : public array_allocator_base<_Tp>
    {
    public:
      typedef size_t     	size_type;
      typedef ptrdiff_t  	difference_type;
      typedef _Tp*       	pointer;
      typedef const _Tp* 	const_pointer;
      typedef _Tp&       	reference;
      typedef const _Tp& 	const_reference;
      typedef _Tp        	value_type;
      typedef _Array		array_type;

    private:
      array_type* 	_M_array;
      size_type 	_M_used;

    public:
     template<typename _Tp1, typename _Array1 = _Array>
        struct rebind
        { typedef array_allocator<_Tp1, _Array1> other; };

      array_allocator(array_type* __array = NULL) throw() 
      : _M_array(__array), _M_used(size_type()) { }

      array_allocator(const array_allocator& __o)  throw() 
      : _M_array(__o._M_array), _M_used(__o._M_used) { }

      template<typename _Tp1, typename _Array1>
        array_allocator(const array_allocator<_Tp1, _Array1>&) throw()
	: _M_array(NULL), _M_used(size_type()) { }

      ~array_allocator() throw() { }

      pointer
      allocate(size_type __n, const void* = 0)
      {
	if (_M_array == 0 || _M_used + __n > _M_array->size())
	  std::__throw_bad_alloc();
	pointer __ret = _M_array->begin() + _M_used;
	_M_used += __n;
	return __ret;
      }
    };

  template<typename _Tp, typename _Array>
    inline bool
    operator==(const array_allocator<_Tp, _Array>&,
	       const array_allocator<_Tp, _Array>&)
    { return true; }
  
  template<typename _Tp, typename _Array>
    inline bool
    operator!=(const array_allocator<_Tp, _Array>&, 
	       const array_allocator<_Tp, _Array>&)
    { return false; }

_GLIBCXX_END_NAMESPACE

#endif
