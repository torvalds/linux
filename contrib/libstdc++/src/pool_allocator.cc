// Allocator details.

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

//
// ISO C++ 14882:
//

#include <bits/c++config.h>
#include <cstdlib>
#include <ext/pool_allocator.h>

namespace
{
  __gnu_cxx::__mutex palloc_init_mutex;
} // anonymous namespace

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  // Definitions for __pool_alloc_base.
  __pool_alloc_base::_Obj* volatile*
  __pool_alloc_base::_M_get_free_list(size_t __bytes)
  { 
    size_t __i = ((__bytes + (size_t)_S_align - 1) / (size_t)_S_align - 1);
    return _S_free_list + __i;
  }

  __mutex&
  __pool_alloc_base::_M_get_mutex()
  { return palloc_init_mutex; }

  // Allocate memory in large chunks in order to avoid fragmenting the
  // heap too much.  Assume that __n is properly aligned.  We hold the
  // allocation lock.
  char*
  __pool_alloc_base::_M_allocate_chunk(size_t __n, int& __nobjs)
  {
    char* __result;
    size_t __total_bytes = __n * __nobjs;
    size_t __bytes_left = _S_end_free - _S_start_free;
    
    if (__bytes_left >= __total_bytes)
      {
	__result = _S_start_free;
	_S_start_free += __total_bytes;
	return __result ;
      }
    else if (__bytes_left >= __n)
      {
	__nobjs = (int)(__bytes_left / __n);
	__total_bytes = __n * __nobjs;
	__result = _S_start_free;
	_S_start_free += __total_bytes;
	return __result;
      }
    else
      {
	// Try to make use of the left-over piece.
	if (__bytes_left > 0)
	  {
	    _Obj* volatile* __free_list = _M_get_free_list(__bytes_left);
	    ((_Obj*)(void*)_S_start_free)->_M_free_list_link = *__free_list;
	    *__free_list = (_Obj*)(void*)_S_start_free;
	  }
	
	size_t __bytes_to_get = (2 * __total_bytes
				 + _M_round_up(_S_heap_size >> 4));
	try
	  {
	    _S_start_free = static_cast<char*>(::operator new(__bytes_to_get));
	  }
	catch (...)
	  {
	    // Try to make do with what we have.  That can't hurt.  We
	    // do not try smaller requests, since that tends to result
	    // in disaster on multi-process machines.
	    size_t __i = __n;
	    for (; __i <= (size_t) _S_max_bytes; __i += (size_t) _S_align)
	      {
		_Obj* volatile* __free_list = _M_get_free_list(__i);
		_Obj* __p = *__free_list;
		if (__p != 0)
		  {
		    *__free_list = __p->_M_free_list_link;
		    _S_start_free = (char*)__p;
		    _S_end_free = _S_start_free + __i;
		    return _M_allocate_chunk(__n, __nobjs);
		    // Any leftover piece will eventually make it to the
		    // right free list.
		  }
	      }
	    // What we have wasn't enough.  Rethrow.
	    _S_start_free = _S_end_free = 0;   // We have no chunk.
	    __throw_exception_again;
	  }
	_S_heap_size += __bytes_to_get;
	_S_end_free = _S_start_free + __bytes_to_get;
	return _M_allocate_chunk(__n, __nobjs);
      }
  }
  
  // Returns an object of size __n, and optionally adds to "size
  // __n"'s free list.  We assume that __n is properly aligned.  We
  // hold the allocation lock.
  void*
  __pool_alloc_base::_M_refill(size_t __n)
  {
    int __nobjs = 20;
    char* __chunk = _M_allocate_chunk(__n, __nobjs);
    _Obj* volatile* __free_list;
    _Obj* __result;
    _Obj* __current_obj;
    _Obj* __next_obj;
    
    if (__nobjs == 1)
      return __chunk;
    __free_list = _M_get_free_list(__n);
    
    // Build free list in chunk.
    __result = (_Obj*)(void*)__chunk;
    *__free_list = __next_obj = (_Obj*)(void*)(__chunk + __n);
    for (int __i = 1; ; __i++)
      {
	__current_obj = __next_obj;
	__next_obj = (_Obj*)(void*)((char*)__next_obj + __n);
	if (__nobjs - 1 == __i)
	  {
	    __current_obj->_M_free_list_link = 0;
	    break;
	  }
	else
	  __current_obj->_M_free_list_link = __next_obj;
      }
    return __result;
  }

  __pool_alloc_base::_Obj* volatile __pool_alloc_base::_S_free_list[_S_free_list_size];
  
  char* __pool_alloc_base::_S_start_free = 0;
  
  char* __pool_alloc_base::_S_end_free = 0;
  
  size_t __pool_alloc_base::_S_heap_size = 0;

  // Instantiations.
  template class __pool_alloc<char>;
  template class __pool_alloc<wchar_t>;

_GLIBCXX_END_NAMESPACE
