// TR1 unordered_map -*- C++ -*-

// Copyright (C) 2005, 2006, 2007 Free Software Foundation, Inc.
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

/** @file tr1/unordered_map
 *  This is a TR1 C++ Library header. 
 */

#ifndef _TR1_UNORDERED_MAP
#define _TR1_UNORDERED_MAP 1

#include <tr1/hashtable>
#include <tr1/functional_hash.h>

namespace std
{
_GLIBCXX_BEGIN_NAMESPACE(tr1)

  // XXX When we get typedef templates these class definitions
  // will be unnecessary.
  template<class _Key, class _Tp,
	   class _Hash = hash<_Key>,
	   class _Pred = std::equal_to<_Key>,
	   class _Alloc = std::allocator<std::pair<const _Key, _Tp> >,
	   bool __cache_hash_code = false>
    class unordered_map
    : public _Hashtable<_Key, std::pair<const _Key, _Tp>, _Alloc,
			std::_Select1st<std::pair<const _Key, _Tp> >, _Pred, 
			_Hash, __detail::_Mod_range_hashing,
			__detail::_Default_ranged_hash,
			__detail::_Prime_rehash_policy,
			__cache_hash_code, false, true>
    {
      typedef _Hashtable<_Key, std::pair<const _Key, _Tp>, _Alloc,
			 std::_Select1st<std::pair<const _Key, _Tp> >, _Pred,
			 _Hash, __detail::_Mod_range_hashing,
			 __detail::_Default_ranged_hash,
			 __detail::_Prime_rehash_policy,
			 __cache_hash_code, false, true>
        _Base;

    public:
      typedef typename _Base::size_type       size_type;
      typedef typename _Base::hasher          hasher;
      typedef typename _Base::key_equal       key_equal;
      typedef typename _Base::allocator_type  allocator_type;

      explicit
      unordered_map(size_type __n = 10,
		    const hasher& __hf = hasher(),
		    const key_equal& __eql = key_equal(),
		    const allocator_type& __a = allocator_type())
      : _Base(__n, __hf, __detail::_Mod_range_hashing(),
	      __detail::_Default_ranged_hash(),
	      __eql, std::_Select1st<std::pair<const _Key, _Tp> >(), __a)
      { }

      template<typename _InputIterator>
        unordered_map(_InputIterator __f, _InputIterator __l, 
		      size_type __n = 10,
		      const hasher& __hf = hasher(), 
		      const key_equal& __eql = key_equal(), 
		      const allocator_type& __a = allocator_type())
	: _Base(__f, __l, __n, __hf, __detail::_Mod_range_hashing(),
		__detail::_Default_ranged_hash(),
		__eql, std::_Select1st<std::pair<const _Key, _Tp> >(), __a)
	{ }
    };
  
  template<class _Key, class _Tp,
	   class _Hash = hash<_Key>,
	   class _Pred = std::equal_to<_Key>,
	   class _Alloc = std::allocator<std::pair<const _Key, _Tp> >,
	   bool __cache_hash_code = false>
    class unordered_multimap
    : public _Hashtable<_Key, std::pair<const _Key, _Tp>,
			_Alloc,
			std::_Select1st<std::pair<const _Key, _Tp> >, _Pred,
			_Hash, __detail::_Mod_range_hashing,
			__detail::_Default_ranged_hash,
			__detail::_Prime_rehash_policy,
			__cache_hash_code, false, false>
    {
      typedef _Hashtable<_Key, std::pair<const _Key, _Tp>,
			 _Alloc,
			 std::_Select1st<std::pair<const _Key, _Tp> >, _Pred,
			 _Hash, __detail::_Mod_range_hashing,
			 __detail::_Default_ranged_hash,
			 __detail::_Prime_rehash_policy,
			 __cache_hash_code, false, false>
        _Base;

    public:
      typedef typename _Base::size_type       size_type;
      typedef typename _Base::hasher          hasher;
      typedef typename _Base::key_equal       key_equal;
      typedef typename _Base::allocator_type  allocator_type;
      
      explicit
      unordered_multimap(size_type __n = 10,
			 const hasher& __hf = hasher(),
			 const key_equal& __eql = key_equal(),
			 const allocator_type& __a = allocator_type())
      : _Base(__n, __hf, __detail::_Mod_range_hashing(),
	      __detail::_Default_ranged_hash(),
	      __eql, std::_Select1st<std::pair<const _Key, _Tp> >(), __a)
      { }


      template<typename _InputIterator>
        unordered_multimap(_InputIterator __f, _InputIterator __l, 
			   typename _Base::size_type __n = 0,
			   const hasher& __hf = hasher(), 
			   const key_equal& __eql = key_equal(), 
			   const allocator_type& __a = allocator_type())
	: _Base(__f, __l, __n, __hf, __detail::_Mod_range_hashing(),
		__detail::_Default_ranged_hash(),
		__eql, std::_Select1st<std::pair<const _Key, _Tp> >(), __a)
        { }
    };

  template<class _Key, class _Tp, class _Hash, class _Pred, class _Alloc,
	   bool __cache_hash_code>
    inline void
    swap(unordered_map<_Key, _Tp, _Hash, _Pred,
	 _Alloc, __cache_hash_code>& __x,
	 unordered_map<_Key, _Tp, _Hash, _Pred,
	 _Alloc, __cache_hash_code>& __y)
    { __x.swap(__y); }

  template<class _Key, class _Tp, class _Hash, class _Pred, class _Alloc,
	   bool __cache_hash_code>
    inline void
    swap(unordered_multimap<_Key, _Tp, _Hash, _Pred,
	 _Alloc, __cache_hash_code>& __x,
	 unordered_multimap<_Key, _Tp, _Hash, _Pred,
	 _Alloc, __cache_hash_code>& __y)
    { __x.swap(__y); }

_GLIBCXX_END_NAMESPACE
}

#endif // _TR1_UNORDERED_MAP
