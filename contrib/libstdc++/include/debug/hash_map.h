// Debugging hash_map implementation -*- C++ -*-

// Copyright (C) 2003, 2005, 2006
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

/** @file debug/hash_map.h
 *  This file is a GNU debug extension to the Standard C++ Library.
 */

#ifndef _GLIBCXX_DEBUG_HASH_MAP_H
#define _GLIBCXX_DEBUG_HASH_MAP_H 1

#include <debug/safe_sequence.h>
#include <debug/safe_iterator.h>

namespace __gnu_cxx
{
namespace __debug
{
  template<typename _Value, typename _Tp,
	   typename _HashFcn  = __gnu_cxx::hash<_Value>,
	   typename _EqualKey = std::equal_to<_Value>,
	   typename _Alloc = std::allocator<_Value> >
    class hash_map
    : public _GLIBCXX_EXT::hash_map<_Value, _Tp, _HashFcn, _EqualKey, _Alloc>,
      public __gnu_debug::_Safe_sequence<hash_map<_Value, _Tp, _HashFcn,
						 _EqualKey, _Alloc> >
    {
      typedef _GLIBCXX_EXT::hash_map<_Value, _Tp, _HashFcn, _EqualKey, _Alloc>
      							_Base;
      typedef __gnu_debug::_Safe_sequence<hash_map> 	_Safe_base;

    public:
      typedef typename _Base::key_type        key_type;
      typedef typename _Base::data_type       data_type;
      typedef typename _Base::mapped_type     mapped_type;
      typedef typename _Base::value_type      value_type;
      typedef typename _Base::hasher          hasher;
      typedef typename _Base::key_equal       key_equal;
      typedef typename _Base::size_type       size_type;
      typedef typename _Base::difference_type difference_type;
      typedef typename _Base::pointer         pointer;
      typedef typename _Base::const_pointer   const_pointer;
      typedef typename _Base::reference       reference;
      typedef typename _Base::const_reference const_reference;

      typedef __gnu_debug::_Safe_iterator<typename _Base::iterator, hash_map>
					      iterator;
      typedef __gnu_debug::_Safe_iterator<typename _Base::const_iterator,
					  hash_map>
					      const_iterator;

      typedef typename _Base::allocator_type  allocator_type;

      using _Base::hash_funct;
      using _Base::key_eq;
      using _Base::get_allocator;

      hash_map() { }

      explicit hash_map(size_type __n) : _Base(__n) { }

      hash_map(size_type __n, const hasher& __hf) : _Base(__n, __hf) { }

      hash_map(size_type __n, const hasher& __hf, const key_equal& __eql,
	       const allocator_type& __a = allocator_type())
      : _Base(__n, __hf, __eql, __a) { }

      template<typename _InputIterator>
        hash_map(_InputIterator __f, _InputIterator __l)
        : _Base(__gnu_debug::__check_valid_range(__f, __l), __l) { }

      template<typename _InputIterator>
        hash_map(_InputIterator __f, _InputIterator __l, size_type __n)
	: _Base(__gnu_debug::__check_valid_range(__f, __l), __l, __n) { }

      template<typename _InputIterator>
        hash_map(_InputIterator __f, _InputIterator __l, size_type __n,
		 const hasher& __hf)
        : _Base(__gnu_debug::__check_valid_range(__f, __l), __l, __n, __hf) { }

      template<typename _InputIterator>
        hash_map(_InputIterator __f, _InputIterator __l, size_type __n,
		 const hasher& __hf, const key_equal& __eql,
		 const allocator_type& __a = allocator_type())
	: _Base(__gnu_debug::__check_valid_range(__f, __l), __l, __n, __hf,
		__eql, __a) { }

      hash_map(const _Base& __x) : _Base(__x), _Safe_base() { }

      using _Base::size;
      using _Base::max_size;
      using _Base::empty;

      void
      swap(hash_map& __x)
      {
	_Base::swap(__x);
	this->_M_swap(__x);
      }

      iterator
      begin() { return iterator(_Base::begin(), this); }

      iterator
      end() { return iterator(_Base::end(),   this); }

      const_iterator
      begin() const
      { return const_iterator(_Base::begin(), this); }

      const_iterator
      end() const
      { return const_iterator(_Base::end(),   this); }

      std::pair<iterator, bool>
      insert(const value_type& __obj)
      {
	std::pair<typename _Base::iterator, bool> __res = _Base::insert(__obj);
	return std::make_pair(iterator(__res.first, this), __res.second);
      }

      void
      insert(const value_type* __first, const value_type* __last)
      {
	__glibcxx_check_valid_range(__first, __last);
	_Base::insert(__first, __last);
      }

     template<typename _InputIterator>
        void
        insert(_InputIterator __first, _InputIterator __last)
        {
	  __glibcxx_check_valid_range(__first, __last);
	  _Base::insert(__first.base(), __last.base());
	}


      std::pair<iterator, bool>
      insert_noresize(const value_type& __obj)
      {
	std::pair<typename _Base::iterator, bool> __res =
	                                        _Base::insert_noresize(__obj);
	return std::make_pair(iterator(__res.first, this), __res.second);
      }

      iterator
      find(const key_type& __key)
      { return iterator(_Base::find(__key), this); }

      const_iterator
      find(const key_type& __key) const
      { return const_iterator(_Base::find(__key), this); }

      using _Base::operator[];
      using _Base::count;

      std::pair<iterator, iterator>
      equal_range(const key_type& __key)
      {
	typedef typename _Base::iterator _Base_iterator;
	std::pair<_Base_iterator, _Base_iterator> __res =
	                  _Base::equal_range(__key);
	return std::make_pair(iterator(__res.first, this),
			      iterator(__res.second, this));
      }

      std::pair<const_iterator, const_iterator>
      equal_range(const key_type& __key) const
      {
	typedef typename _Base::const_iterator _Base_iterator;
	std::pair<_Base_iterator, _Base_iterator> __res =
	_Base::equal_range(__key);
	return std::make_pair(const_iterator(__res.first, this),
			      const_iterator(__res.second, this));
      }

      size_type
      erase(const key_type& __key)
      {
	iterator __victim(_Base::find(__key), this);
	if (__victim != end())
	  return this->erase(__victim), 1;
	else
	  return 0;
      }

      void
      erase(iterator __it)
      {
	__glibcxx_check_erase(__it);
	__it._M_invalidate();
	_Base::erase(__it.base());
      }

      void
      erase(iterator __first, iterator __last)
      {
	__glibcxx_check_erase_range(__first, __last);
	for (iterator __tmp = __first; __tmp != __last;)
	{
	  iterator __victim = __tmp++;
	  __victim._M_invalidate();
	}
	_Base::erase(__first.base(), __last.base());
      }

      void
      clear()
      {
	_Base::clear();
	this->_M_invalidate_all();
      }

      using _Base::resize;
      using _Base::bucket_count;
      using _Base::max_bucket_count;
      using _Base::elems_in_bucket;

      _Base&
      _M_base()       { return *this; }

      const _Base&
      _M_base() const { return *this; }

    private:
      void
      _M_invalidate_all()
      {
	typedef typename _Base::const_iterator _Base_const_iterator;
	typedef __gnu_debug::_Not_equal_to<_Base_const_iterator> _Not_equal;
	this->_M_invalidate_if(_Not_equal(_M_base().end()));
      }
    };

  template<typename _Value, typename _Tp, typename _HashFcn,
	   typename _EqualKey, typename _Alloc>
    inline bool
    operator==(const hash_map<_Value, _Tp, _HashFcn, _EqualKey, _Alloc>& __x,
	       const hash_map<_Value, _Tp, _HashFcn, _EqualKey, _Alloc>& __y)
    { return __x._M_base() == __y._M_base(); }

  template<typename _Value, typename _Tp, typename _HashFcn,
	   typename _EqualKey, typename _Alloc>
    inline bool
    operator!=(const hash_map<_Value, _Tp, _HashFcn, _EqualKey, _Alloc>& __x,
	       const hash_map<_Value, _Tp, _HashFcn, _EqualKey, _Alloc>& __y)
    { return __x._M_base() != __y._M_base(); }

  template<typename _Value, typename _Tp, typename _HashFcn,
	   typename _EqualKey, typename _Alloc>
    inline void
    swap(hash_map<_Value, _Tp, _HashFcn, _EqualKey, _Alloc>& __x,
	 hash_map<_Value, _Tp, _HashFcn, _EqualKey, _Alloc>& __y)
    { __x.swap(__y); }
} // namespace __debug
} // namespace __gnu_cxx

#endif
