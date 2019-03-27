// Deque implementation -*- C++ -*-

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
 *
 * Copyright (c) 1994
 * Hewlett-Packard Company
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Hewlett-Packard Company makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 *
 * Copyright (c) 1997
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

/** @file stl_deque.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _DEQUE_H
#define _DEQUE_H 1

#include <bits/concept_check.h>
#include <bits/stl_iterator_base_types.h>
#include <bits/stl_iterator_base_funcs.h>

_GLIBCXX_BEGIN_NESTED_NAMESPACE(std, _GLIBCXX_STD)

  /**
   *  @if maint
   *  @brief This function controls the size of memory nodes.
   *  @param  size  The size of an element.
   *  @return   The number (not byte size) of elements per node.
   *
   *  This function started off as a compiler kludge from SGI, but seems to
   *  be a useful wrapper around a repeated constant expression.  The '512' is
   *  tuneable (and no other code needs to change), but no investigation has
   *  been done since inheriting the SGI code.
   *  @endif
  */
  inline size_t
  __deque_buf_size(size_t __size)
  { return __size < 512 ? size_t(512 / __size) : size_t(1); }


  /**
   *  @brief A deque::iterator.
   *
   *  Quite a bit of intelligence here.  Much of the functionality of
   *  deque is actually passed off to this class.  A deque holds two
   *  of these internally, marking its valid range.  Access to
   *  elements is done as offsets of either of those two, relying on
   *  operator overloading in this class.
   *
   *  @if maint
   *  All the functions are op overloads except for _M_set_node.
   *  @endif
  */
  template<typename _Tp, typename _Ref, typename _Ptr>
    struct _Deque_iterator
    {
      typedef _Deque_iterator<_Tp, _Tp&, _Tp*>             iterator;
      typedef _Deque_iterator<_Tp, const _Tp&, const _Tp*> const_iterator;

      static size_t _S_buffer_size()
      { return __deque_buf_size(sizeof(_Tp)); }

      typedef std::random_access_iterator_tag iterator_category;
      typedef _Tp                             value_type;
      typedef _Ptr                            pointer;
      typedef _Ref                            reference;
      typedef size_t                          size_type;
      typedef ptrdiff_t                       difference_type;
      typedef _Tp**                           _Map_pointer;
      typedef _Deque_iterator                 _Self;

      _Tp* _M_cur;
      _Tp* _M_first;
      _Tp* _M_last;
      _Map_pointer _M_node;

      _Deque_iterator(_Tp* __x, _Map_pointer __y)
      : _M_cur(__x), _M_first(*__y),
        _M_last(*__y + _S_buffer_size()), _M_node(__y) {}

      _Deque_iterator() : _M_cur(0), _M_first(0), _M_last(0), _M_node(0) {}

      _Deque_iterator(const iterator& __x)
      : _M_cur(__x._M_cur), _M_first(__x._M_first),
        _M_last(__x._M_last), _M_node(__x._M_node) {}

      reference
      operator*() const
      { return *_M_cur; }

      pointer
      operator->() const
      { return _M_cur; }

      _Self&
      operator++()
      {
	++_M_cur;
	if (_M_cur == _M_last)
	  {
	    _M_set_node(_M_node + 1);
	    _M_cur = _M_first;
	  }
	return *this;
      }

      _Self
      operator++(int)
      {
	_Self __tmp = *this;
	++*this;
	return __tmp;
      }

      _Self&
      operator--()
      {
	if (_M_cur == _M_first)
	  {
	    _M_set_node(_M_node - 1);
	    _M_cur = _M_last;
	  }
	--_M_cur;
	return *this;
      }

      _Self
      operator--(int)
      {
	_Self __tmp = *this;
	--*this;
	return __tmp;
      }

      _Self&
      operator+=(difference_type __n)
      {
	const difference_type __offset = __n + (_M_cur - _M_first);
	if (__offset >= 0 && __offset < difference_type(_S_buffer_size()))
	  _M_cur += __n;
	else
	  {
	    const difference_type __node_offset =
	      __offset > 0 ? __offset / difference_type(_S_buffer_size())
	                   : -difference_type((-__offset - 1)
					      / _S_buffer_size()) - 1;
	    _M_set_node(_M_node + __node_offset);
	    _M_cur = _M_first + (__offset - __node_offset
				 * difference_type(_S_buffer_size()));
	  }
	return *this;
      }

      _Self
      operator+(difference_type __n) const
      {
	_Self __tmp = *this;
	return __tmp += __n;
      }

      _Self&
      operator-=(difference_type __n)
      { return *this += -__n; }

      _Self
      operator-(difference_type __n) const
      {
	_Self __tmp = *this;
	return __tmp -= __n;
      }

      reference
      operator[](difference_type __n) const
      { return *(*this + __n); }

      /** @if maint
       *  Prepares to traverse new_node.  Sets everything except
       *  _M_cur, which should therefore be set by the caller
       *  immediately afterwards, based on _M_first and _M_last.
       *  @endif
       */
      void
      _M_set_node(_Map_pointer __new_node)
      {
	_M_node = __new_node;
	_M_first = *__new_node;
	_M_last = _M_first + difference_type(_S_buffer_size());
      }
    };

  // Note: we also provide overloads whose operands are of the same type in
  // order to avoid ambiguous overload resolution when std::rel_ops operators
  // are in scope (for additional details, see libstdc++/3628)
  template<typename _Tp, typename _Ref, typename _Ptr>
    inline bool
    operator==(const _Deque_iterator<_Tp, _Ref, _Ptr>& __x,
	       const _Deque_iterator<_Tp, _Ref, _Ptr>& __y)
    { return __x._M_cur == __y._M_cur; }

  template<typename _Tp, typename _RefL, typename _PtrL,
	   typename _RefR, typename _PtrR>
    inline bool
    operator==(const _Deque_iterator<_Tp, _RefL, _PtrL>& __x,
	       const _Deque_iterator<_Tp, _RefR, _PtrR>& __y)
    { return __x._M_cur == __y._M_cur; }

  template<typename _Tp, typename _Ref, typename _Ptr>
    inline bool
    operator!=(const _Deque_iterator<_Tp, _Ref, _Ptr>& __x,
	       const _Deque_iterator<_Tp, _Ref, _Ptr>& __y)
    { return !(__x == __y); }

  template<typename _Tp, typename _RefL, typename _PtrL,
	   typename _RefR, typename _PtrR>
    inline bool
    operator!=(const _Deque_iterator<_Tp, _RefL, _PtrL>& __x,
	       const _Deque_iterator<_Tp, _RefR, _PtrR>& __y)
    { return !(__x == __y); }

  template<typename _Tp, typename _Ref, typename _Ptr>
    inline bool
    operator<(const _Deque_iterator<_Tp, _Ref, _Ptr>& __x,
	      const _Deque_iterator<_Tp, _Ref, _Ptr>& __y)
    { return (__x._M_node == __y._M_node) ? (__x._M_cur < __y._M_cur)
                                          : (__x._M_node < __y._M_node); }

  template<typename _Tp, typename _RefL, typename _PtrL,
	   typename _RefR, typename _PtrR>
    inline bool
    operator<(const _Deque_iterator<_Tp, _RefL, _PtrL>& __x,
	      const _Deque_iterator<_Tp, _RefR, _PtrR>& __y)
    { return (__x._M_node == __y._M_node) ? (__x._M_cur < __y._M_cur)
	                                  : (__x._M_node < __y._M_node); }

  template<typename _Tp, typename _Ref, typename _Ptr>
    inline bool
    operator>(const _Deque_iterator<_Tp, _Ref, _Ptr>& __x,
	      const _Deque_iterator<_Tp, _Ref, _Ptr>& __y)
    { return __y < __x; }

  template<typename _Tp, typename _RefL, typename _PtrL,
	   typename _RefR, typename _PtrR>
    inline bool
    operator>(const _Deque_iterator<_Tp, _RefL, _PtrL>& __x,
	      const _Deque_iterator<_Tp, _RefR, _PtrR>& __y)
    { return __y < __x; }

  template<typename _Tp, typename _Ref, typename _Ptr>
    inline bool
    operator<=(const _Deque_iterator<_Tp, _Ref, _Ptr>& __x,
	       const _Deque_iterator<_Tp, _Ref, _Ptr>& __y)
    { return !(__y < __x); }

  template<typename _Tp, typename _RefL, typename _PtrL,
	   typename _RefR, typename _PtrR>
    inline bool
    operator<=(const _Deque_iterator<_Tp, _RefL, _PtrL>& __x,
	       const _Deque_iterator<_Tp, _RefR, _PtrR>& __y)
    { return !(__y < __x); }

  template<typename _Tp, typename _Ref, typename _Ptr>
    inline bool
    operator>=(const _Deque_iterator<_Tp, _Ref, _Ptr>& __x,
	       const _Deque_iterator<_Tp, _Ref, _Ptr>& __y)
    { return !(__x < __y); }

  template<typename _Tp, typename _RefL, typename _PtrL,
	   typename _RefR, typename _PtrR>
    inline bool
    operator>=(const _Deque_iterator<_Tp, _RefL, _PtrL>& __x,
	       const _Deque_iterator<_Tp, _RefR, _PtrR>& __y)
    { return !(__x < __y); }

  // _GLIBCXX_RESOLVE_LIB_DEFECTS
  // According to the resolution of DR179 not only the various comparison
  // operators but also operator- must accept mixed iterator/const_iterator
  // parameters.
  template<typename _Tp, typename _Ref, typename _Ptr>
    inline typename _Deque_iterator<_Tp, _Ref, _Ptr>::difference_type
    operator-(const _Deque_iterator<_Tp, _Ref, _Ptr>& __x,
	      const _Deque_iterator<_Tp, _Ref, _Ptr>& __y)
    {
      return typename _Deque_iterator<_Tp, _Ref, _Ptr>::difference_type
	(_Deque_iterator<_Tp, _Ref, _Ptr>::_S_buffer_size())
	* (__x._M_node - __y._M_node - 1) + (__x._M_cur - __x._M_first)
	+ (__y._M_last - __y._M_cur);
    }

  template<typename _Tp, typename _RefL, typename _PtrL,
	   typename _RefR, typename _PtrR>
    inline typename _Deque_iterator<_Tp, _RefL, _PtrL>::difference_type
    operator-(const _Deque_iterator<_Tp, _RefL, _PtrL>& __x,
	      const _Deque_iterator<_Tp, _RefR, _PtrR>& __y)
    {
      return typename _Deque_iterator<_Tp, _RefL, _PtrL>::difference_type
	(_Deque_iterator<_Tp, _RefL, _PtrL>::_S_buffer_size())
	* (__x._M_node - __y._M_node - 1) + (__x._M_cur - __x._M_first)
	+ (__y._M_last - __y._M_cur);
    }

  template<typename _Tp, typename _Ref, typename _Ptr>
    inline _Deque_iterator<_Tp, _Ref, _Ptr>
    operator+(ptrdiff_t __n, const _Deque_iterator<_Tp, _Ref, _Ptr>& __x)
    { return __x + __n; }

  template<typename _Tp>
    void
    fill(const _Deque_iterator<_Tp, _Tp&, _Tp*>& __first,
	 const _Deque_iterator<_Tp, _Tp&, _Tp*>& __last, const _Tp& __value);

  /**
   *  @if maint
   *  Deque base class.  This class provides the unified face for %deque's
   *  allocation.  This class's constructor and destructor allocate and
   *  deallocate (but do not initialize) storage.  This makes %exception
   *  safety easier.
   *
   *  Nothing in this class ever constructs or destroys an actual Tp element.
   *  (Deque handles that itself.)  Only/All memory management is performed
   *  here.
   *  @endif
  */
  template<typename _Tp, typename _Alloc>
    class _Deque_base
    {
    public:
      typedef _Alloc                  allocator_type;

      allocator_type
      get_allocator() const
      { return allocator_type(_M_get_Tp_allocator()); }

      typedef _Deque_iterator<_Tp, _Tp&, _Tp*>             iterator;
      typedef _Deque_iterator<_Tp, const _Tp&, const _Tp*> const_iterator;

      _Deque_base()
      : _M_impl()
      { _M_initialize_map(0); }

      _Deque_base(const allocator_type& __a, size_t __num_elements)
      : _M_impl(__a)
      { _M_initialize_map(__num_elements); }

      _Deque_base(const allocator_type& __a)
      : _M_impl(__a)
      { }

      ~_Deque_base();

    protected:
      //This struct encapsulates the implementation of the std::deque
      //standard container and at the same time makes use of the EBO
      //for empty allocators.
      typedef typename _Alloc::template rebind<_Tp*>::other _Map_alloc_type;

      typedef typename _Alloc::template rebind<_Tp>::other  _Tp_alloc_type;

      struct _Deque_impl
      : public _Tp_alloc_type
      {
	_Tp** _M_map;
	size_t _M_map_size;
	iterator _M_start;
	iterator _M_finish;

	_Deque_impl()
	: _Tp_alloc_type(), _M_map(0), _M_map_size(0),
	  _M_start(), _M_finish()
	{ }

	_Deque_impl(const _Tp_alloc_type& __a)
	: _Tp_alloc_type(__a), _M_map(0), _M_map_size(0),
	  _M_start(), _M_finish()
	{ }
      };

      _Tp_alloc_type&
      _M_get_Tp_allocator()
      { return *static_cast<_Tp_alloc_type*>(&this->_M_impl); }

      const _Tp_alloc_type&
      _M_get_Tp_allocator() const
      { return *static_cast<const _Tp_alloc_type*>(&this->_M_impl); }

      _Map_alloc_type
      _M_get_map_allocator() const
      { return _Map_alloc_type(_M_get_Tp_allocator()); }

      _Tp*
      _M_allocate_node()
      { 
	return _M_impl._Tp_alloc_type::allocate(__deque_buf_size(sizeof(_Tp)));
      }

      void
      _M_deallocate_node(_Tp* __p)
      {
	_M_impl._Tp_alloc_type::deallocate(__p, __deque_buf_size(sizeof(_Tp)));
      }

      _Tp**
      _M_allocate_map(size_t __n)
      { return _M_get_map_allocator().allocate(__n); }

      void
      _M_deallocate_map(_Tp** __p, size_t __n)
      { _M_get_map_allocator().deallocate(__p, __n); }

    protected:
      void _M_initialize_map(size_t);
      void _M_create_nodes(_Tp** __nstart, _Tp** __nfinish);
      void _M_destroy_nodes(_Tp** __nstart, _Tp** __nfinish);
      enum { _S_initial_map_size = 8 };

      _Deque_impl _M_impl;
    };

  template<typename _Tp, typename _Alloc>
    _Deque_base<_Tp, _Alloc>::
    ~_Deque_base()
    {
      if (this->_M_impl._M_map)
	{
	  _M_destroy_nodes(this->_M_impl._M_start._M_node,
			   this->_M_impl._M_finish._M_node + 1);
	  _M_deallocate_map(this->_M_impl._M_map, this->_M_impl._M_map_size);
	}
    }

  /**
   *  @if maint
   *  @brief Layout storage.
   *  @param  num_elements  The count of T's for which to allocate space
   *                        at first.
   *  @return   Nothing.
   *
   *  The initial underlying memory layout is a bit complicated...
   *  @endif
  */
  template<typename _Tp, typename _Alloc>
    void
    _Deque_base<_Tp, _Alloc>::
    _M_initialize_map(size_t __num_elements)
    {
      const size_t __num_nodes = (__num_elements/ __deque_buf_size(sizeof(_Tp))
				  + 1);

      this->_M_impl._M_map_size = std::max((size_t) _S_initial_map_size,
					   size_t(__num_nodes + 2));
      this->_M_impl._M_map = _M_allocate_map(this->_M_impl._M_map_size);

      // For "small" maps (needing less than _M_map_size nodes), allocation
      // starts in the middle elements and grows outwards.  So nstart may be
      // the beginning of _M_map, but for small maps it may be as far in as
      // _M_map+3.

      _Tp** __nstart = (this->_M_impl._M_map
			+ (this->_M_impl._M_map_size - __num_nodes) / 2);
      _Tp** __nfinish = __nstart + __num_nodes;

      try
	{ _M_create_nodes(__nstart, __nfinish); }
      catch(...)
	{
	  _M_deallocate_map(this->_M_impl._M_map, this->_M_impl._M_map_size);
	  this->_M_impl._M_map = 0;
	  this->_M_impl._M_map_size = 0;
	  __throw_exception_again;
	}

      this->_M_impl._M_start._M_set_node(__nstart);
      this->_M_impl._M_finish._M_set_node(__nfinish - 1);
      this->_M_impl._M_start._M_cur = _M_impl._M_start._M_first;
      this->_M_impl._M_finish._M_cur = (this->_M_impl._M_finish._M_first
					+ __num_elements
					% __deque_buf_size(sizeof(_Tp)));
    }

  template<typename _Tp, typename _Alloc>
    void
    _Deque_base<_Tp, _Alloc>::
    _M_create_nodes(_Tp** __nstart, _Tp** __nfinish)
    {
      _Tp** __cur;
      try
	{
	  for (__cur = __nstart; __cur < __nfinish; ++__cur)
	    *__cur = this->_M_allocate_node();
	}
      catch(...)
	{
	  _M_destroy_nodes(__nstart, __cur);
	  __throw_exception_again;
	}
    }

  template<typename _Tp, typename _Alloc>
    void
    _Deque_base<_Tp, _Alloc>::
    _M_destroy_nodes(_Tp** __nstart, _Tp** __nfinish)
    {
      for (_Tp** __n = __nstart; __n < __nfinish; ++__n)
	_M_deallocate_node(*__n);
    }

  /**
   *  @brief  A standard container using fixed-size memory allocation and
   *  constant-time manipulation of elements at either end.
   *
   *  @ingroup Containers
   *  @ingroup Sequences
   *
   *  Meets the requirements of a <a href="tables.html#65">container</a>, a
   *  <a href="tables.html#66">reversible container</a>, and a
   *  <a href="tables.html#67">sequence</a>, including the
   *  <a href="tables.html#68">optional sequence requirements</a>.
   *
   *  In previous HP/SGI versions of deque, there was an extra template
   *  parameter so users could control the node size.  This extension turned
   *  out to violate the C++ standard (it can be detected using template
   *  template parameters), and it was removed.
   *
   *  @if maint
   *  Here's how a deque<Tp> manages memory.  Each deque has 4 members:
   *
   *  - Tp**        _M_map
   *  - size_t      _M_map_size
   *  - iterator    _M_start, _M_finish
   *
   *  map_size is at least 8.  %map is an array of map_size
   *  pointers-to-"nodes".  (The name %map has nothing to do with the
   *  std::map class, and "nodes" should not be confused with
   *  std::list's usage of "node".)
   *
   *  A "node" has no specific type name as such, but it is referred
   *  to as "node" in this file.  It is a simple array-of-Tp.  If Tp
   *  is very large, there will be one Tp element per node (i.e., an
   *  "array" of one).  For non-huge Tp's, node size is inversely
   *  related to Tp size: the larger the Tp, the fewer Tp's will fit
   *  in a node.  The goal here is to keep the total size of a node
   *  relatively small and constant over different Tp's, to improve
   *  allocator efficiency.
   *
   *  Not every pointer in the %map array will point to a node.  If
   *  the initial number of elements in the deque is small, the
   *  /middle/ %map pointers will be valid, and the ones at the edges
   *  will be unused.  This same situation will arise as the %map
   *  grows: available %map pointers, if any, will be on the ends.  As
   *  new nodes are created, only a subset of the %map's pointers need
   *  to be copied "outward".
   *
   *  Class invariants:
   * - For any nonsingular iterator i:
   *    - i.node points to a member of the %map array.  (Yes, you read that
   *      correctly:  i.node does not actually point to a node.)  The member of
   *      the %map array is what actually points to the node.
   *    - i.first == *(i.node)    (This points to the node (first Tp element).)
   *    - i.last  == i.first + node_size
   *    - i.cur is a pointer in the range [i.first, i.last).  NOTE:
   *      the implication of this is that i.cur is always a dereferenceable
   *      pointer, even if i is a past-the-end iterator.
   * - Start and Finish are always nonsingular iterators.  NOTE: this
   * means that an empty deque must have one node, a deque with <N
   * elements (where N is the node buffer size) must have one node, a
   * deque with N through (2N-1) elements must have two nodes, etc.
   * - For every node other than start.node and finish.node, every
   * element in the node is an initialized object.  If start.node ==
   * finish.node, then [start.cur, finish.cur) are initialized
   * objects, and the elements outside that range are uninitialized
   * storage.  Otherwise, [start.cur, start.last) and [finish.first,
   * finish.cur) are initialized objects, and [start.first, start.cur)
   * and [finish.cur, finish.last) are uninitialized storage.
   * - [%map, %map + map_size) is a valid, non-empty range.
   * - [start.node, finish.node] is a valid range contained within
   *   [%map, %map + map_size).
   * - A pointer in the range [%map, %map + map_size) points to an allocated
   *   node if and only if the pointer is in the range
   *   [start.node, finish.node].
   *
   *  Here's the magic:  nothing in deque is "aware" of the discontiguous
   *  storage!
   *
   *  The memory setup and layout occurs in the parent, _Base, and the iterator
   *  class is entirely responsible for "leaping" from one node to the next.
   *  All the implementation routines for deque itself work only through the
   *  start and finish iterators.  This keeps the routines simple and sane,
   *  and we can use other standard algorithms as well.
   *  @endif
  */
  template<typename _Tp, typename _Alloc = std::allocator<_Tp> >
    class deque : protected _Deque_base<_Tp, _Alloc>
    {
      // concept requirements
      typedef typename _Alloc::value_type        _Alloc_value_type;
      __glibcxx_class_requires(_Tp, _SGIAssignableConcept)
      __glibcxx_class_requires2(_Tp, _Alloc_value_type, _SameTypeConcept)

      typedef _Deque_base<_Tp, _Alloc>           _Base;
      typedef typename _Base::_Tp_alloc_type	 _Tp_alloc_type;

    public:
      typedef _Tp                                        value_type;
      typedef typename _Tp_alloc_type::pointer           pointer;
      typedef typename _Tp_alloc_type::const_pointer     const_pointer;
      typedef typename _Tp_alloc_type::reference         reference;
      typedef typename _Tp_alloc_type::const_reference   const_reference;
      typedef typename _Base::iterator                   iterator;
      typedef typename _Base::const_iterator             const_iterator;
      typedef std::reverse_iterator<const_iterator>      const_reverse_iterator;
      typedef std::reverse_iterator<iterator>            reverse_iterator;
      typedef size_t                             size_type;
      typedef ptrdiff_t                          difference_type;
      typedef _Alloc                             allocator_type;

    protected:
      typedef pointer*                           _Map_pointer;

      static size_t _S_buffer_size()
      { return __deque_buf_size(sizeof(_Tp)); }

      // Functions controlling memory layout, and nothing else.
      using _Base::_M_initialize_map;
      using _Base::_M_create_nodes;
      using _Base::_M_destroy_nodes;
      using _Base::_M_allocate_node;
      using _Base::_M_deallocate_node;
      using _Base::_M_allocate_map;
      using _Base::_M_deallocate_map;
      using _Base::_M_get_Tp_allocator;

      /** @if maint
       *  A total of four data members accumulated down the heirarchy.
       *  May be accessed via _M_impl.*
       *  @endif
       */
      using _Base::_M_impl;

    public:
      // [23.2.1.1] construct/copy/destroy
      // (assign() and get_allocator() are also listed in this section)
      /**
       *  @brief  Default constructor creates no elements.
       */
      deque()
      : _Base() { }

      explicit
      deque(const allocator_type& __a)
      : _Base(__a, 0) {}

      /**
       *  @brief  Create a %deque with copies of an exemplar element.
       *  @param  n  The number of elements to initially create.
       *  @param  value  An element to copy.
       *
       *  This constructor fills the %deque with @a n copies of @a value.
       */
      explicit
      deque(size_type __n, const value_type& __value = value_type(),
	    const allocator_type& __a = allocator_type())
      : _Base(__a, __n)
      { _M_fill_initialize(__value); }

      /**
       *  @brief  %Deque copy constructor.
       *  @param  x  A %deque of identical element and allocator types.
       *
       *  The newly-created %deque uses a copy of the allocation object used
       *  by @a x.
       */
      deque(const deque& __x)
      : _Base(__x._M_get_Tp_allocator(), __x.size())
      { std::__uninitialized_copy_a(__x.begin(), __x.end(), 
				    this->_M_impl._M_start,
				    _M_get_Tp_allocator()); }

      /**
       *  @brief  Builds a %deque from a range.
       *  @param  first  An input iterator.
       *  @param  last  An input iterator.
       *
       *  Create a %deque consisting of copies of the elements from [first,
       *  last).
       *
       *  If the iterators are forward, bidirectional, or random-access, then
       *  this will call the elements' copy constructor N times (where N is
       *  distance(first,last)) and do no memory reallocation.  But if only
       *  input iterators are used, then this will do at most 2N calls to the
       *  copy constructor, and logN memory reallocations.
       */
      template<typename _InputIterator>
        deque(_InputIterator __first, _InputIterator __last,
	      const allocator_type& __a = allocator_type())
	: _Base(__a)
        {
	  // Check whether it's an integral type.  If so, it's not an iterator.
	  typedef typename std::__is_integer<_InputIterator>::__type _Integral;
	  _M_initialize_dispatch(__first, __last, _Integral());
	}

      /**
       *  The dtor only erases the elements, and note that if the elements
       *  themselves are pointers, the pointed-to memory is not touched in any
       *  way.  Managing the pointer is the user's responsibilty.
       */
      ~deque()
      { _M_destroy_data(begin(), end(), _M_get_Tp_allocator()); }

      /**
       *  @brief  %Deque assignment operator.
       *  @param  x  A %deque of identical element and allocator types.
       *
       *  All the elements of @a x are copied, but unlike the copy constructor,
       *  the allocator object is not copied.
       */
      deque&
      operator=(const deque& __x);

      /**
       *  @brief  Assigns a given value to a %deque.
       *  @param  n  Number of elements to be assigned.
       *  @param  val  Value to be assigned.
       *
       *  This function fills a %deque with @a n copies of the given
       *  value.  Note that the assignment completely changes the
       *  %deque and that the resulting %deque's size is the same as
       *  the number of elements assigned.  Old data may be lost.
       */
      void
      assign(size_type __n, const value_type& __val)
      { _M_fill_assign(__n, __val); }

      /**
       *  @brief  Assigns a range to a %deque.
       *  @param  first  An input iterator.
       *  @param  last   An input iterator.
       *
       *  This function fills a %deque with copies of the elements in the
       *  range [first,last).
       *
       *  Note that the assignment completely changes the %deque and that the
       *  resulting %deque's size is the same as the number of elements
       *  assigned.  Old data may be lost.
       */
      template<typename _InputIterator>
        void
        assign(_InputIterator __first, _InputIterator __last)
        {
	  typedef typename std::__is_integer<_InputIterator>::__type _Integral;
	  _M_assign_dispatch(__first, __last, _Integral());
	}

      /// Get a copy of the memory allocation object.
      allocator_type
      get_allocator() const
      { return _Base::get_allocator(); }

      // iterators
      /**
       *  Returns a read/write iterator that points to the first element in the
       *  %deque.  Iteration is done in ordinary element order.
       */
      iterator
      begin()
      { return this->_M_impl._M_start; }

      /**
       *  Returns a read-only (constant) iterator that points to the first
       *  element in the %deque.  Iteration is done in ordinary element order.
       */
      const_iterator
      begin() const
      { return this->_M_impl._M_start; }

      /**
       *  Returns a read/write iterator that points one past the last
       *  element in the %deque.  Iteration is done in ordinary
       *  element order.
       */
      iterator
      end()
      { return this->_M_impl._M_finish; }

      /**
       *  Returns a read-only (constant) iterator that points one past
       *  the last element in the %deque.  Iteration is done in
       *  ordinary element order.
       */
      const_iterator
      end() const
      { return this->_M_impl._M_finish; }

      /**
       *  Returns a read/write reverse iterator that points to the
       *  last element in the %deque.  Iteration is done in reverse
       *  element order.
       */
      reverse_iterator
      rbegin()
      { return reverse_iterator(this->_M_impl._M_finish); }

      /**
       *  Returns a read-only (constant) reverse iterator that points
       *  to the last element in the %deque.  Iteration is done in
       *  reverse element order.
       */
      const_reverse_iterator
      rbegin() const
      { return const_reverse_iterator(this->_M_impl._M_finish); }

      /**
       *  Returns a read/write reverse iterator that points to one
       *  before the first element in the %deque.  Iteration is done
       *  in reverse element order.
       */
      reverse_iterator
      rend()
      { return reverse_iterator(this->_M_impl._M_start); }

      /**
       *  Returns a read-only (constant) reverse iterator that points
       *  to one before the first element in the %deque.  Iteration is
       *  done in reverse element order.
       */
      const_reverse_iterator
      rend() const
      { return const_reverse_iterator(this->_M_impl._M_start); }

      // [23.2.1.2] capacity
      /**  Returns the number of elements in the %deque.  */
      size_type
      size() const
      { return this->_M_impl._M_finish - this->_M_impl._M_start; }

      /**  Returns the size() of the largest possible %deque.  */
      size_type
      max_size() const
      { return _M_get_Tp_allocator().max_size(); }

      /**
       *  @brief  Resizes the %deque to the specified number of elements.
       *  @param  new_size  Number of elements the %deque should contain.
       *  @param  x  Data with which new elements should be populated.
       *
       *  This function will %resize the %deque to the specified
       *  number of elements.  If the number is smaller than the
       *  %deque's current size the %deque is truncated, otherwise the
       *  %deque is extended and new elements are populated with given
       *  data.
       */
      void
      resize(size_type __new_size, value_type __x = value_type())
      {
	const size_type __len = size();
	if (__new_size < __len)
	  _M_erase_at_end(this->_M_impl._M_start + difference_type(__new_size));
	else
	  insert(this->_M_impl._M_finish, __new_size - __len, __x);
      }

      /**
       *  Returns true if the %deque is empty.  (Thus begin() would
       *  equal end().)
       */
      bool
      empty() const
      { return this->_M_impl._M_finish == this->_M_impl._M_start; }

      // element access
      /**
       *  @brief Subscript access to the data contained in the %deque.
       *  @param n The index of the element for which data should be
       *  accessed.
       *  @return  Read/write reference to data.
       *
       *  This operator allows for easy, array-style, data access.
       *  Note that data access with this operator is unchecked and
       *  out_of_range lookups are not defined. (For checked lookups
       *  see at().)
       */
      reference
      operator[](size_type __n)
      { return this->_M_impl._M_start[difference_type(__n)]; }

      /**
       *  @brief Subscript access to the data contained in the %deque.
       *  @param n The index of the element for which data should be
       *  accessed.
       *  @return  Read-only (constant) reference to data.
       *
       *  This operator allows for easy, array-style, data access.
       *  Note that data access with this operator is unchecked and
       *  out_of_range lookups are not defined. (For checked lookups
       *  see at().)
       */
      const_reference
      operator[](size_type __n) const
      { return this->_M_impl._M_start[difference_type(__n)]; }

    protected:
      /// @if maint Safety check used only from at().  @endif
      void
      _M_range_check(size_type __n) const
      {
	if (__n >= this->size())
	  __throw_out_of_range(__N("deque::_M_range_check"));
      }

    public:
      /**
       *  @brief  Provides access to the data contained in the %deque.
       *  @param n The index of the element for which data should be
       *  accessed.
       *  @return  Read/write reference to data.
       *  @throw  std::out_of_range  If @a n is an invalid index.
       *
       *  This function provides for safer data access.  The parameter
       *  is first checked that it is in the range of the deque.  The
       *  function throws out_of_range if the check fails.
       */
      reference
      at(size_type __n)
      {
	_M_range_check(__n);
	return (*this)[__n];
      }

      /**
       *  @brief  Provides access to the data contained in the %deque.
       *  @param n The index of the element for which data should be
       *  accessed.
       *  @return  Read-only (constant) reference to data.
       *  @throw  std::out_of_range  If @a n is an invalid index.
       *
       *  This function provides for safer data access.  The parameter is first
       *  checked that it is in the range of the deque.  The function throws
       *  out_of_range if the check fails.
       */
      const_reference
      at(size_type __n) const
      {
	_M_range_check(__n);
	return (*this)[__n];
      }

      /**
       *  Returns a read/write reference to the data at the first
       *  element of the %deque.
       */
      reference
      front()
      { return *begin(); }

      /**
       *  Returns a read-only (constant) reference to the data at the first
       *  element of the %deque.
       */
      const_reference
      front() const
      { return *begin(); }

      /**
       *  Returns a read/write reference to the data at the last element of the
       *  %deque.
       */
      reference
      back()
      {
	iterator __tmp = end();
	--__tmp;
	return *__tmp;
      }

      /**
       *  Returns a read-only (constant) reference to the data at the last
       *  element of the %deque.
       */
      const_reference
      back() const
      {
	const_iterator __tmp = end();
	--__tmp;
	return *__tmp;
      }

      // [23.2.1.2] modifiers
      /**
       *  @brief  Add data to the front of the %deque.
       *  @param  x  Data to be added.
       *
       *  This is a typical stack operation.  The function creates an
       *  element at the front of the %deque and assigns the given
       *  data to it.  Due to the nature of a %deque this operation
       *  can be done in constant time.
       */
      void
      push_front(const value_type& __x)
      {
	if (this->_M_impl._M_start._M_cur != this->_M_impl._M_start._M_first)
	  {
	    this->_M_impl.construct(this->_M_impl._M_start._M_cur - 1, __x);
	    --this->_M_impl._M_start._M_cur;
	  }
	else
	  _M_push_front_aux(__x);
      }

      /**
       *  @brief  Add data to the end of the %deque.
       *  @param  x  Data to be added.
       *
       *  This is a typical stack operation.  The function creates an
       *  element at the end of the %deque and assigns the given data
       *  to it.  Due to the nature of a %deque this operation can be
       *  done in constant time.
       */
      void
      push_back(const value_type& __x)
      {
	if (this->_M_impl._M_finish._M_cur
	    != this->_M_impl._M_finish._M_last - 1)
	  {
	    this->_M_impl.construct(this->_M_impl._M_finish._M_cur, __x);
	    ++this->_M_impl._M_finish._M_cur;
	  }
	else
	  _M_push_back_aux(__x);
      }

      /**
       *  @brief  Removes first element.
       *
       *  This is a typical stack operation.  It shrinks the %deque by one.
       *
       *  Note that no data is returned, and if the first element's data is
       *  needed, it should be retrieved before pop_front() is called.
       */
      void
      pop_front()
      {
	if (this->_M_impl._M_start._M_cur
	    != this->_M_impl._M_start._M_last - 1)
	  {
	    this->_M_impl.destroy(this->_M_impl._M_start._M_cur);
	    ++this->_M_impl._M_start._M_cur;
	  }
	else
	  _M_pop_front_aux();
      }

      /**
       *  @brief  Removes last element.
       *
       *  This is a typical stack operation.  It shrinks the %deque by one.
       *
       *  Note that no data is returned, and if the last element's data is
       *  needed, it should be retrieved before pop_back() is called.
       */
      void
      pop_back()
      {
	if (this->_M_impl._M_finish._M_cur
	    != this->_M_impl._M_finish._M_first)
	  {
	    --this->_M_impl._M_finish._M_cur;
	    this->_M_impl.destroy(this->_M_impl._M_finish._M_cur);
	  }
	else
	  _M_pop_back_aux();
      }

      /**
       *  @brief  Inserts given value into %deque before specified iterator.
       *  @param  position  An iterator into the %deque.
       *  @param  x  Data to be inserted.
       *  @return  An iterator that points to the inserted data.
       *
       *  This function will insert a copy of the given value before the
       *  specified location.
       */
      iterator
      insert(iterator __position, const value_type& __x);

      /**
       *  @brief  Inserts a number of copies of given data into the %deque.
       *  @param  position  An iterator into the %deque.
       *  @param  n  Number of elements to be inserted.
       *  @param  x  Data to be inserted.
       *
       *  This function will insert a specified number of copies of the given
       *  data before the location specified by @a position.
       */
      void
      insert(iterator __position, size_type __n, const value_type& __x)
      { _M_fill_insert(__position, __n, __x); }

      /**
       *  @brief  Inserts a range into the %deque.
       *  @param  position  An iterator into the %deque.
       *  @param  first  An input iterator.
       *  @param  last   An input iterator.
       *
       *  This function will insert copies of the data in the range
       *  [first,last) into the %deque before the location specified
       *  by @a pos.  This is known as "range insert."
       */
      template<typename _InputIterator>
        void
        insert(iterator __position, _InputIterator __first,
	       _InputIterator __last)
        {
	  // Check whether it's an integral type.  If so, it's not an iterator.
	  typedef typename std::__is_integer<_InputIterator>::__type _Integral;
	  _M_insert_dispatch(__position, __first, __last, _Integral());
	}

      /**
       *  @brief  Remove element at given position.
       *  @param  position  Iterator pointing to element to be erased.
       *  @return  An iterator pointing to the next element (or end()).
       *
       *  This function will erase the element at the given position and thus
       *  shorten the %deque by one.
       *
       *  The user is cautioned that
       *  this function only erases the element, and that if the element is
       *  itself a pointer, the pointed-to memory is not touched in any way.
       *  Managing the pointer is the user's responsibilty.
       */
      iterator
      erase(iterator __position);

      /**
       *  @brief  Remove a range of elements.
       *  @param  first  Iterator pointing to the first element to be erased.
       *  @param  last  Iterator pointing to one past the last element to be
       *                erased.
       *  @return  An iterator pointing to the element pointed to by @a last
       *           prior to erasing (or end()).
       *
       *  This function will erase the elements in the range [first,last) and
       *  shorten the %deque accordingly.
       *
       *  The user is cautioned that
       *  this function only erases the elements, and that if the elements
       *  themselves are pointers, the pointed-to memory is not touched in any
       *  way.  Managing the pointer is the user's responsibilty.
       */
      iterator
      erase(iterator __first, iterator __last);

      /**
       *  @brief  Swaps data with another %deque.
       *  @param  x  A %deque of the same element and allocator types.
       *
       *  This exchanges the elements between two deques in constant time.
       *  (Four pointers, so it should be quite fast.)
       *  Note that the global std::swap() function is specialized such that
       *  std::swap(d1,d2) will feed to this function.
       */
      void
      swap(deque& __x)
      {
	std::swap(this->_M_impl._M_start, __x._M_impl._M_start);
	std::swap(this->_M_impl._M_finish, __x._M_impl._M_finish);
	std::swap(this->_M_impl._M_map, __x._M_impl._M_map);
	std::swap(this->_M_impl._M_map_size, __x._M_impl._M_map_size);

	// _GLIBCXX_RESOLVE_LIB_DEFECTS
	// 431. Swapping containers with unequal allocators.
	std::__alloc_swap<_Tp_alloc_type>::_S_do_it(_M_get_Tp_allocator(),
						    __x._M_get_Tp_allocator());
      }

      /**
       *  Erases all the elements.  Note that this function only erases the
       *  elements, and that if the elements themselves are pointers, the
       *  pointed-to memory is not touched in any way.  Managing the pointer is
       *  the user's responsibilty.
       */
      void
      clear()
      { _M_erase_at_end(begin()); }

    protected:
      // Internal constructor functions follow.

      // called by the range constructor to implement [23.1.1]/9
      template<typename _Integer>
        void
        _M_initialize_dispatch(_Integer __n, _Integer __x, __true_type)
        {
	  _M_initialize_map(__n);
	  _M_fill_initialize(__x);
	}

      // called by the range constructor to implement [23.1.1]/9
      template<typename _InputIterator>
        void
        _M_initialize_dispatch(_InputIterator __first, _InputIterator __last,
			       __false_type)
        {
	  typedef typename std::iterator_traits<_InputIterator>::
	    iterator_category _IterCategory;
	  _M_range_initialize(__first, __last, _IterCategory());
	}

      // called by the second initialize_dispatch above
      //@{
      /**
       *  @if maint
       *  @brief Fills the deque with whatever is in [first,last).
       *  @param  first  An input iterator.
       *  @param  last  An input iterator.
       *  @return   Nothing.
       *
       *  If the iterators are actually forward iterators (or better), then the
       *  memory layout can be done all at once.  Else we move forward using
       *  push_back on each value from the iterator.
       *  @endif
       */
      template<typename _InputIterator>
        void
        _M_range_initialize(_InputIterator __first, _InputIterator __last,
			    std::input_iterator_tag);

      // called by the second initialize_dispatch above
      template<typename _ForwardIterator>
        void
        _M_range_initialize(_ForwardIterator __first, _ForwardIterator __last,
			    std::forward_iterator_tag);
      //@}

      /**
       *  @if maint
       *  @brief Fills the %deque with copies of value.
       *  @param  value  Initial value.
       *  @return   Nothing.
       *  @pre _M_start and _M_finish have already been initialized,
       *  but none of the %deque's elements have yet been constructed.
       *
       *  This function is called only when the user provides an explicit size
       *  (with or without an explicit exemplar value).
       *  @endif
       */
      void
      _M_fill_initialize(const value_type& __value);

      // Internal assign functions follow.  The *_aux functions do the actual
      // assignment work for the range versions.

      // called by the range assign to implement [23.1.1]/9
      template<typename _Integer>
        void
        _M_assign_dispatch(_Integer __n, _Integer __val, __true_type)
        {
	  _M_fill_assign(static_cast<size_type>(__n),
			 static_cast<value_type>(__val));
	}

      // called by the range assign to implement [23.1.1]/9
      template<typename _InputIterator>
        void
        _M_assign_dispatch(_InputIterator __first, _InputIterator __last,
			   __false_type)
        {
	  typedef typename std::iterator_traits<_InputIterator>::
	    iterator_category _IterCategory;
	  _M_assign_aux(__first, __last, _IterCategory());
	}

      // called by the second assign_dispatch above
      template<typename _InputIterator>
        void
        _M_assign_aux(_InputIterator __first, _InputIterator __last,
		      std::input_iterator_tag);

      // called by the second assign_dispatch above
      template<typename _ForwardIterator>
        void
        _M_assign_aux(_ForwardIterator __first, _ForwardIterator __last,
		      std::forward_iterator_tag)
        {
	  const size_type __len = std::distance(__first, __last);
	  if (__len > size())
	    {
	      _ForwardIterator __mid = __first;
	      std::advance(__mid, size());
	      std::copy(__first, __mid, begin());
	      insert(end(), __mid, __last);
	    }
	  else
	    _M_erase_at_end(std::copy(__first, __last, begin()));
	}

      // Called by assign(n,t), and the range assign when it turns out
      // to be the same thing.
      void
      _M_fill_assign(size_type __n, const value_type& __val)
      {
	if (__n > size())
	  {
	    std::fill(begin(), end(), __val);
	    insert(end(), __n - size(), __val);
	  }
	else
	  {
	    _M_erase_at_end(begin() + difference_type(__n));
	    std::fill(begin(), end(), __val);
	  }
      }

      //@{
      /**
       *  @if maint
       *  @brief Helper functions for push_* and pop_*.
       *  @endif
       */
      void _M_push_back_aux(const value_type&);

      void _M_push_front_aux(const value_type&);

      void _M_pop_back_aux();

      void _M_pop_front_aux();
      //@}

      // Internal insert functions follow.  The *_aux functions do the actual
      // insertion work when all shortcuts fail.

      // called by the range insert to implement [23.1.1]/9
      template<typename _Integer>
        void
        _M_insert_dispatch(iterator __pos,
			   _Integer __n, _Integer __x, __true_type)
        {
	  _M_fill_insert(__pos, static_cast<size_type>(__n),
			 static_cast<value_type>(__x));
	}

      // called by the range insert to implement [23.1.1]/9
      template<typename _InputIterator>
        void
        _M_insert_dispatch(iterator __pos,
			   _InputIterator __first, _InputIterator __last,
			   __false_type)
        {
	  typedef typename std::iterator_traits<_InputIterator>::
	    iterator_category _IterCategory;
          _M_range_insert_aux(__pos, __first, __last, _IterCategory());
	}

      // called by the second insert_dispatch above
      template<typename _InputIterator>
        void
        _M_range_insert_aux(iterator __pos, _InputIterator __first,
			    _InputIterator __last, std::input_iterator_tag);

      // called by the second insert_dispatch above
      template<typename _ForwardIterator>
        void
        _M_range_insert_aux(iterator __pos, _ForwardIterator __first,
			    _ForwardIterator __last, std::forward_iterator_tag);

      // Called by insert(p,n,x), and the range insert when it turns out to be
      // the same thing.  Can use fill functions in optimal situations,
      // otherwise passes off to insert_aux(p,n,x).
      void
      _M_fill_insert(iterator __pos, size_type __n, const value_type& __x);

      // called by insert(p,x)
      iterator
      _M_insert_aux(iterator __pos, const value_type& __x);

      // called by insert(p,n,x) via fill_insert
      void
      _M_insert_aux(iterator __pos, size_type __n, const value_type& __x);

      // called by range_insert_aux for forward iterators
      template<typename _ForwardIterator>
        void
        _M_insert_aux(iterator __pos,
		      _ForwardIterator __first, _ForwardIterator __last,
		      size_type __n);


      // Internal erase functions follow.

      void
      _M_destroy_data_aux(iterator __first, iterator __last);

      void
      _M_destroy_data_dispatch(iterator, iterator, __true_type) { }
      
      void
      _M_destroy_data_dispatch(iterator __first, iterator __last, __false_type)
      { _M_destroy_data_aux(__first, __last); }

      // Called by ~deque().
      // NB: Doesn't deallocate the nodes.
      template<typename _Alloc1>
        void
        _M_destroy_data(iterator __first, iterator __last, const _Alloc1&)
        { _M_destroy_data_aux(__first, __last); }

      void
      _M_destroy_data(iterator __first, iterator __last,
		      const std::allocator<_Tp>&)
      {
	typedef typename std::__is_scalar<value_type>::__type
	  _Has_trivial_destructor;
	_M_destroy_data_dispatch(__first, __last, _Has_trivial_destructor());
      }

      // Called by erase(q1, q2).
      void
      _M_erase_at_begin(iterator __pos)
      {
	_M_destroy_data(begin(), __pos, _M_get_Tp_allocator());
	_M_destroy_nodes(this->_M_impl._M_start._M_node, __pos._M_node);
	this->_M_impl._M_start = __pos;
      }

      // Called by erase(q1, q2), resize(), clear(), _M_assign_aux,
      // _M_fill_assign, operator=.
      void
      _M_erase_at_end(iterator __pos)
      {
	_M_destroy_data(__pos, end(), _M_get_Tp_allocator());
	_M_destroy_nodes(__pos._M_node + 1,
			 this->_M_impl._M_finish._M_node + 1);
	this->_M_impl._M_finish = __pos;
      }

      //@{
      /**
       *  @if maint
       *  @brief Memory-handling helpers for the previous internal insert
       *         functions.
       *  @endif
       */
      iterator
      _M_reserve_elements_at_front(size_type __n)
      {
	const size_type __vacancies = this->_M_impl._M_start._M_cur
	                              - this->_M_impl._M_start._M_first;
	if (__n > __vacancies)
	  _M_new_elements_at_front(__n - __vacancies);
	return this->_M_impl._M_start - difference_type(__n);
      }

      iterator
      _M_reserve_elements_at_back(size_type __n)
      {
	const size_type __vacancies = (this->_M_impl._M_finish._M_last
				       - this->_M_impl._M_finish._M_cur) - 1;
	if (__n > __vacancies)
	  _M_new_elements_at_back(__n - __vacancies);
	return this->_M_impl._M_finish + difference_type(__n);
      }

      void
      _M_new_elements_at_front(size_type __new_elements);

      void
      _M_new_elements_at_back(size_type __new_elements);
      //@}


      //@{
      /**
       *  @if maint
       *  @brief Memory-handling helpers for the major %map.
       *
       *  Makes sure the _M_map has space for new nodes.  Does not
       *  actually add the nodes.  Can invalidate _M_map pointers.
       *  (And consequently, %deque iterators.)
       *  @endif
       */
      void
      _M_reserve_map_at_back(size_type __nodes_to_add = 1)
      {
	if (__nodes_to_add + 1 > this->_M_impl._M_map_size
	    - (this->_M_impl._M_finish._M_node - this->_M_impl._M_map))
	  _M_reallocate_map(__nodes_to_add, false);
      }

      void
      _M_reserve_map_at_front(size_type __nodes_to_add = 1)
      {
	if (__nodes_to_add > size_type(this->_M_impl._M_start._M_node
				       - this->_M_impl._M_map))
	  _M_reallocate_map(__nodes_to_add, true);
      }

      void
      _M_reallocate_map(size_type __nodes_to_add, bool __add_at_front);
      //@}
    };


  /**
   *  @brief  Deque equality comparison.
   *  @param  x  A %deque.
   *  @param  y  A %deque of the same type as @a x.
   *  @return  True iff the size and elements of the deques are equal.
   *
   *  This is an equivalence relation.  It is linear in the size of the
   *  deques.  Deques are considered equivalent if their sizes are equal,
   *  and if corresponding elements compare equal.
  */
  template<typename _Tp, typename _Alloc>
    inline bool
    operator==(const deque<_Tp, _Alloc>& __x,
                         const deque<_Tp, _Alloc>& __y)
    { return __x.size() == __y.size()
             && std::equal(__x.begin(), __x.end(), __y.begin()); }

  /**
   *  @brief  Deque ordering relation.
   *  @param  x  A %deque.
   *  @param  y  A %deque of the same type as @a x.
   *  @return  True iff @a x is lexicographically less than @a y.
   *
   *  This is a total ordering relation.  It is linear in the size of the
   *  deques.  The elements must be comparable with @c <.
   *
   *  See std::lexicographical_compare() for how the determination is made.
  */
  template<typename _Tp, typename _Alloc>
    inline bool
    operator<(const deque<_Tp, _Alloc>& __x,
	      const deque<_Tp, _Alloc>& __y)
    { return std::lexicographical_compare(__x.begin(), __x.end(),
					  __y.begin(), __y.end()); }

  /// Based on operator==
  template<typename _Tp, typename _Alloc>
    inline bool
    operator!=(const deque<_Tp, _Alloc>& __x,
	       const deque<_Tp, _Alloc>& __y)
    { return !(__x == __y); }

  /// Based on operator<
  template<typename _Tp, typename _Alloc>
    inline bool
    operator>(const deque<_Tp, _Alloc>& __x,
	      const deque<_Tp, _Alloc>& __y)
    { return __y < __x; }

  /// Based on operator<
  template<typename _Tp, typename _Alloc>
    inline bool
    operator<=(const deque<_Tp, _Alloc>& __x,
	       const deque<_Tp, _Alloc>& __y)
    { return !(__y < __x); }

  /// Based on operator<
  template<typename _Tp, typename _Alloc>
    inline bool
    operator>=(const deque<_Tp, _Alloc>& __x,
	       const deque<_Tp, _Alloc>& __y)
    { return !(__x < __y); }

  /// See std::deque::swap().
  template<typename _Tp, typename _Alloc>
    inline void
    swap(deque<_Tp,_Alloc>& __x, deque<_Tp,_Alloc>& __y)
    { __x.swap(__y); }

_GLIBCXX_END_NESTED_NAMESPACE

#endif /* _DEQUE_H */
