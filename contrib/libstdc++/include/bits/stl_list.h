// List implementation -*- C++ -*-

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
 * Copyright (c) 1996,1997
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

/** @file stl_list.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _LIST_H
#define _LIST_H 1

#include <bits/concept_check.h>

_GLIBCXX_BEGIN_NESTED_NAMESPACE(std, _GLIBCXX_STD)

  // Supporting structures are split into common and templated types; the
  // latter publicly inherits from the former in an effort to reduce code
  // duplication.  This results in some "needless" static_cast'ing later on,
  // but it's all safe downcasting.

  /// @if maint Common part of a node in the %list.  @endif
  struct _List_node_base
  {
    _List_node_base* _M_next;   ///< Self-explanatory
    _List_node_base* _M_prev;   ///< Self-explanatory

    static void
    swap(_List_node_base& __x, _List_node_base& __y);

    void
    transfer(_List_node_base * const __first,
	     _List_node_base * const __last);

    void
    reverse();

    void
    hook(_List_node_base * const __position);

    void
    unhook();
  };

  /// @if maint An actual node in the %list.  @endif
  template<typename _Tp>
    struct _List_node : public _List_node_base
    {
      _Tp _M_data;                ///< User's data.
    };

  /**
   *  @brief A list::iterator.
   *
   *  @if maint
   *  All the functions are op overloads.
   *  @endif
  */
  template<typename _Tp>
    struct _List_iterator
    {
      typedef _List_iterator<_Tp>                _Self;
      typedef _List_node<_Tp>                    _Node;

      typedef ptrdiff_t                          difference_type;
      typedef std::bidirectional_iterator_tag    iterator_category;
      typedef _Tp                                value_type;
      typedef _Tp*                               pointer;
      typedef _Tp&                               reference;

      _List_iterator()
      : _M_node() { }

      explicit
      _List_iterator(_List_node_base* __x)
      : _M_node(__x) { }

      // Must downcast from List_node_base to _List_node to get to _M_data.
      reference
      operator*() const
      { return static_cast<_Node*>(_M_node)->_M_data; }

      pointer
      operator->() const
      { return &static_cast<_Node*>(_M_node)->_M_data; }

      _Self&
      operator++()
      {
	_M_node = _M_node->_M_next;
	return *this;
      }

      _Self
      operator++(int)
      {
	_Self __tmp = *this;
	_M_node = _M_node->_M_next;
	return __tmp;
      }

      _Self&
      operator--()
      {
	_M_node = _M_node->_M_prev;
	return *this;
      }

      _Self
      operator--(int)
      {
	_Self __tmp = *this;
	_M_node = _M_node->_M_prev;
	return __tmp;
      }

      bool
      operator==(const _Self& __x) const
      { return _M_node == __x._M_node; }

      bool
      operator!=(const _Self& __x) const
      { return _M_node != __x._M_node; }

      // The only member points to the %list element.
      _List_node_base* _M_node;
    };

  /**
   *  @brief A list::const_iterator.
   *
   *  @if maint
   *  All the functions are op overloads.
   *  @endif
  */
  template<typename _Tp>
    struct _List_const_iterator
    {
      typedef _List_const_iterator<_Tp>          _Self;
      typedef const _List_node<_Tp>              _Node;
      typedef _List_iterator<_Tp>                iterator;

      typedef ptrdiff_t                          difference_type;
      typedef std::bidirectional_iterator_tag    iterator_category;
      typedef _Tp                                value_type;
      typedef const _Tp*                         pointer;
      typedef const _Tp&                         reference;

      _List_const_iterator()
      : _M_node() { }

      explicit
      _List_const_iterator(const _List_node_base* __x)
      : _M_node(__x) { }

      _List_const_iterator(const iterator& __x)
      : _M_node(__x._M_node) { }

      // Must downcast from List_node_base to _List_node to get to
      // _M_data.
      reference
      operator*() const
      { return static_cast<_Node*>(_M_node)->_M_data; }

      pointer
      operator->() const
      { return &static_cast<_Node*>(_M_node)->_M_data; }

      _Self&
      operator++()
      {
	_M_node = _M_node->_M_next;
	return *this;
      }

      _Self
      operator++(int)
      {
	_Self __tmp = *this;
	_M_node = _M_node->_M_next;
	return __tmp;
      }

      _Self&
      operator--()
      {
	_M_node = _M_node->_M_prev;
	return *this;
      }

      _Self
      operator--(int)
      {
	_Self __tmp = *this;
	_M_node = _M_node->_M_prev;
	return __tmp;
      }

      bool
      operator==(const _Self& __x) const
      { return _M_node == __x._M_node; }

      bool
      operator!=(const _Self& __x) const
      { return _M_node != __x._M_node; }

      // The only member points to the %list element.
      const _List_node_base* _M_node;
    };

  template<typename _Val>
    inline bool
    operator==(const _List_iterator<_Val>& __x,
	       const _List_const_iterator<_Val>& __y)
    { return __x._M_node == __y._M_node; }

  template<typename _Val>
    inline bool
    operator!=(const _List_iterator<_Val>& __x,
               const _List_const_iterator<_Val>& __y)
    { return __x._M_node != __y._M_node; }


  /**
   *  @if maint
   *  See bits/stl_deque.h's _Deque_base for an explanation.
   *  @endif
  */
  template<typename _Tp, typename _Alloc>
    class _List_base
    {
    protected:
      // NOTA BENE
      // The stored instance is not actually of "allocator_type"'s
      // type.  Instead we rebind the type to
      // Allocator<List_node<Tp>>, which according to [20.1.5]/4
      // should probably be the same.  List_node<Tp> is not the same
      // size as Tp (it's two pointers larger), and specializations on
      // Tp may go unused because List_node<Tp> is being bound
      // instead.
      //
      // We put this to the test in the constructors and in
      // get_allocator, where we use conversions between
      // allocator_type and _Node_alloc_type. The conversion is
      // required by table 32 in [20.1.5].
      typedef typename _Alloc::template rebind<_List_node<_Tp> >::other
        _Node_alloc_type;

      typedef typename _Alloc::template rebind<_Tp>::other _Tp_alloc_type;

      struct _List_impl 
      : public _Node_alloc_type
      {
	_List_node_base _M_node;

	_List_impl()
	: _Node_alloc_type(), _M_node()
	{ }

	_List_impl(const _Node_alloc_type& __a)
	: _Node_alloc_type(__a), _M_node()
	{ }
      };

      _List_impl _M_impl;

      _List_node<_Tp>*
      _M_get_node()
      { return _M_impl._Node_alloc_type::allocate(1); }
      
      void
      _M_put_node(_List_node<_Tp>* __p)
      { _M_impl._Node_alloc_type::deallocate(__p, 1); }
      
  public:
      typedef _Alloc allocator_type;

      _Node_alloc_type&
      _M_get_Node_allocator()
      { return *static_cast<_Node_alloc_type*>(&this->_M_impl); }

      const _Node_alloc_type&
      _M_get_Node_allocator() const
      { return *static_cast<const _Node_alloc_type*>(&this->_M_impl); }

      _Tp_alloc_type
      _M_get_Tp_allocator() const
      { return _Tp_alloc_type(_M_get_Node_allocator()); }

      allocator_type
      get_allocator() const
      { return allocator_type(_M_get_Node_allocator()); }

      _List_base()
      : _M_impl()
      { _M_init(); }

      _List_base(const allocator_type& __a)
      : _M_impl(__a)
      { _M_init(); }

      // This is what actually destroys the list.
      ~_List_base()
      { _M_clear(); }

      void
      _M_clear();

      void
      _M_init()
      {
        this->_M_impl._M_node._M_next = &this->_M_impl._M_node;
        this->_M_impl._M_node._M_prev = &this->_M_impl._M_node;
      }
    };

  /**
   *  @brief A standard container with linear time access to elements,
   *  and fixed time insertion/deletion at any point in the sequence.
   *
   *  @ingroup Containers
   *  @ingroup Sequences
   *
   *  Meets the requirements of a <a href="tables.html#65">container</a>, a
   *  <a href="tables.html#66">reversible container</a>, and a
   *  <a href="tables.html#67">sequence</a>, including the
   *  <a href="tables.html#68">optional sequence requirements</a> with the
   *  %exception of @c at and @c operator[].
   *
   *  This is a @e doubly @e linked %list.  Traversal up and down the
   *  %list requires linear time, but adding and removing elements (or
   *  @e nodes) is done in constant time, regardless of where the
   *  change takes place.  Unlike std::vector and std::deque,
   *  random-access iterators are not provided, so subscripting ( @c
   *  [] ) access is not allowed.  For algorithms which only need
   *  sequential access, this lack makes no difference.
   *
   *  Also unlike the other standard containers, std::list provides
   *  specialized algorithms %unique to linked lists, such as
   *  splicing, sorting, and in-place reversal.
   *
   *  @if maint
   *  A couple points on memory allocation for list<Tp>:
   *
   *  First, we never actually allocate a Tp, we allocate
   *  List_node<Tp>'s and trust [20.1.5]/4 to DTRT.  This is to ensure
   *  that after elements from %list<X,Alloc1> are spliced into
   *  %list<X,Alloc2>, destroying the memory of the second %list is a
   *  valid operation, i.e., Alloc1 giveth and Alloc2 taketh away.
   *
   *  Second, a %list conceptually represented as
   *  @code
   *    A <---> B <---> C <---> D
   *  @endcode
   *  is actually circular; a link exists between A and D.  The %list
   *  class holds (as its only data member) a private list::iterator
   *  pointing to @e D, not to @e A!  To get to the head of the %list,
   *  we start at the tail and move forward by one.  When this member
   *  iterator's next/previous pointers refer to itself, the %list is
   *  %empty.  @endif
  */
  template<typename _Tp, typename _Alloc = std::allocator<_Tp> >
    class list : protected _List_base<_Tp, _Alloc>
    {
      // concept requirements
      typedef typename _Alloc::value_type                _Alloc_value_type;
      __glibcxx_class_requires(_Tp, _SGIAssignableConcept)
      __glibcxx_class_requires2(_Tp, _Alloc_value_type, _SameTypeConcept)

      typedef _List_base<_Tp, _Alloc>                    _Base;
      typedef typename _Base::_Tp_alloc_type		 _Tp_alloc_type;

    public:
      typedef _Tp                                        value_type;
      typedef typename _Tp_alloc_type::pointer           pointer;
      typedef typename _Tp_alloc_type::const_pointer     const_pointer;
      typedef typename _Tp_alloc_type::reference         reference;
      typedef typename _Tp_alloc_type::const_reference   const_reference;
      typedef _List_iterator<_Tp>                        iterator;
      typedef _List_const_iterator<_Tp>                  const_iterator;
      typedef std::reverse_iterator<const_iterator>      const_reverse_iterator;
      typedef std::reverse_iterator<iterator>            reverse_iterator;
      typedef size_t                                     size_type;
      typedef ptrdiff_t                                  difference_type;
      typedef _Alloc                                     allocator_type;

    protected:
      // Note that pointers-to-_Node's can be ctor-converted to
      // iterator types.
      typedef _List_node<_Tp>				 _Node;

      using _Base::_M_impl;
      using _Base::_M_put_node;
      using _Base::_M_get_node;
      using _Base::_M_get_Tp_allocator;
      using _Base::_M_get_Node_allocator;

      /**
       *  @if maint
       *  @param  x  An instance of user data.
       *
       *  Allocates space for a new node and constructs a copy of @a x in it.
       *  @endif
       */
      _Node*
      _M_create_node(const value_type& __x)
      {
	_Node* __p = this->_M_get_node();
	try
	  {
	    _M_get_Tp_allocator().construct(&__p->_M_data, __x);
	  }
	catch(...)
	  {
	    _M_put_node(__p);
	    __throw_exception_again;
	  }
	return __p;
      }

    public:
      // [23.2.2.1] construct/copy/destroy
      // (assign() and get_allocator() are also listed in this section)
      /**
       *  @brief  Default constructor creates no elements.
       */
      list()
      : _Base() { }

      explicit
      list(const allocator_type& __a)
      : _Base(__a) { }

      /**
       *  @brief  Create a %list with copies of an exemplar element.
       *  @param  n  The number of elements to initially create.
       *  @param  value  An element to copy.
       *
       *  This constructor fills the %list with @a n copies of @a value.
       */
      explicit
      list(size_type __n, const value_type& __value = value_type(),
	   const allocator_type& __a = allocator_type())
      : _Base(__a)
      { _M_fill_initialize(__n, __value); }

      /**
       *  @brief  %List copy constructor.
       *  @param  x  A %list of identical element and allocator types.
       *
       *  The newly-created %list uses a copy of the allocation object used
       *  by @a x.
       */
      list(const list& __x)
      : _Base(__x._M_get_Node_allocator())
      { _M_initialize_dispatch(__x.begin(), __x.end(), __false_type()); }

      /**
       *  @brief  Builds a %list from a range.
       *  @param  first  An input iterator.
       *  @param  last  An input iterator.
       *
       *  Create a %list consisting of copies of the elements from
       *  [@a first,@a last).  This is linear in N (where N is
       *  distance(@a first,@a last)).
       */
      template<typename _InputIterator>
        list(_InputIterator __first, _InputIterator __last,
	     const allocator_type& __a = allocator_type())
        : _Base(__a)
        { 
	  // Check whether it's an integral type.  If so, it's not an iterator.
	  typedef typename std::__is_integer<_InputIterator>::__type _Integral;
	  _M_initialize_dispatch(__first, __last, _Integral());
	}

      /**
       *  No explicit dtor needed as the _Base dtor takes care of
       *  things.  The _Base dtor only erases the elements, and note
       *  that if the elements themselves are pointers, the pointed-to
       *  memory is not touched in any way.  Managing the pointer is
       *  the user's responsibilty.
       */

      /**
       *  @brief  %List assignment operator.
       *  @param  x  A %list of identical element and allocator types.
       *
       *  All the elements of @a x are copied, but unlike the copy
       *  constructor, the allocator object is not copied.
       */
      list&
      operator=(const list& __x);

      /**
       *  @brief  Assigns a given value to a %list.
       *  @param  n  Number of elements to be assigned.
       *  @param  val  Value to be assigned.
       *
       *  This function fills a %list with @a n copies of the given
       *  value.  Note that the assignment completely changes the %list
       *  and that the resulting %list's size is the same as the number
       *  of elements assigned.  Old data may be lost.
       */
      void
      assign(size_type __n, const value_type& __val)
      { _M_fill_assign(__n, __val); }

      /**
       *  @brief  Assigns a range to a %list.
       *  @param  first  An input iterator.
       *  @param  last   An input iterator.
       *
       *  This function fills a %list with copies of the elements in the
       *  range [@a first,@a last).
       *
       *  Note that the assignment completely changes the %list and
       *  that the resulting %list's size is the same as the number of
       *  elements assigned.  Old data may be lost.
       */
      template<typename _InputIterator>
        void
        assign(_InputIterator __first, _InputIterator __last)
        {
	  // Check whether it's an integral type.  If so, it's not an iterator.
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
       *  %list.  Iteration is done in ordinary element order.
       */
      iterator
      begin()
      { return iterator(this->_M_impl._M_node._M_next); }

      /**
       *  Returns a read-only (constant) iterator that points to the
       *  first element in the %list.  Iteration is done in ordinary
       *  element order.
       */
      const_iterator
      begin() const
      { return const_iterator(this->_M_impl._M_node._M_next); }

      /**
       *  Returns a read/write iterator that points one past the last
       *  element in the %list.  Iteration is done in ordinary element
       *  order.
       */
      iterator
      end()
      { return iterator(&this->_M_impl._M_node); }

      /**
       *  Returns a read-only (constant) iterator that points one past
       *  the last element in the %list.  Iteration is done in ordinary
       *  element order.
       */
      const_iterator
      end() const
      { return const_iterator(&this->_M_impl._M_node); }

      /**
       *  Returns a read/write reverse iterator that points to the last
       *  element in the %list.  Iteration is done in reverse element
       *  order.
       */
      reverse_iterator
      rbegin()
      { return reverse_iterator(end()); }

      /**
       *  Returns a read-only (constant) reverse iterator that points to
       *  the last element in the %list.  Iteration is done in reverse
       *  element order.
       */
      const_reverse_iterator
      rbegin() const
      { return const_reverse_iterator(end()); }

      /**
       *  Returns a read/write reverse iterator that points to one
       *  before the first element in the %list.  Iteration is done in
       *  reverse element order.
       */
      reverse_iterator
      rend()
      { return reverse_iterator(begin()); }

      /**
       *  Returns a read-only (constant) reverse iterator that points to one
       *  before the first element in the %list.  Iteration is done in reverse
       *  element order.
       */
      const_reverse_iterator
      rend() const
      { return const_reverse_iterator(begin()); }

      // [23.2.2.2] capacity
      /**
       *  Returns true if the %list is empty.  (Thus begin() would equal
       *  end().)
       */
      bool
      empty() const
      { return this->_M_impl._M_node._M_next == &this->_M_impl._M_node; }

      /**  Returns the number of elements in the %list.  */
      size_type
      size() const
      { return std::distance(begin(), end()); }

      /**  Returns the size() of the largest possible %list.  */
      size_type
      max_size() const
      { return _M_get_Tp_allocator().max_size(); }

      /**
       *  @brief Resizes the %list to the specified number of elements.
       *  @param new_size Number of elements the %list should contain.
       *  @param x Data with which new elements should be populated.
       *
       *  This function will %resize the %list to the specified number
       *  of elements.  If the number is smaller than the %list's
       *  current size the %list is truncated, otherwise the %list is
       *  extended and new elements are populated with given data.
       */
      void
      resize(size_type __new_size, value_type __x = value_type());

      // element access
      /**
       *  Returns a read/write reference to the data at the first
       *  element of the %list.
       */
      reference
      front()
      { return *begin(); }

      /**
       *  Returns a read-only (constant) reference to the data at the first
       *  element of the %list.
       */
      const_reference
      front() const
      { return *begin(); }

      /**
       *  Returns a read/write reference to the data at the last element
       *  of the %list.
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
       *  element of the %list.
       */
      const_reference
      back() const
      { 
	const_iterator __tmp = end();
	--__tmp;
	return *__tmp;
      }

      // [23.2.2.3] modifiers
      /**
       *  @brief  Add data to the front of the %list.
       *  @param  x  Data to be added.
       *
       *  This is a typical stack operation.  The function creates an
       *  element at the front of the %list and assigns the given data
       *  to it.  Due to the nature of a %list this operation can be
       *  done in constant time, and does not invalidate iterators and
       *  references.
       */
      void
      push_front(const value_type& __x)
      { this->_M_insert(begin(), __x); }

      /**
       *  @brief  Removes first element.
       *
       *  This is a typical stack operation.  It shrinks the %list by
       *  one.  Due to the nature of a %list this operation can be done
       *  in constant time, and only invalidates iterators/references to
       *  the element being removed.
       *
       *  Note that no data is returned, and if the first element's data
       *  is needed, it should be retrieved before pop_front() is
       *  called.
       */
      void
      pop_front()
      { this->_M_erase(begin()); }

      /**
       *  @brief  Add data to the end of the %list.
       *  @param  x  Data to be added.
       *
       *  This is a typical stack operation.  The function creates an
       *  element at the end of the %list and assigns the given data to
       *  it.  Due to the nature of a %list this operation can be done
       *  in constant time, and does not invalidate iterators and
       *  references.
       */
      void
      push_back(const value_type& __x)
      { this->_M_insert(end(), __x); }

      /**
       *  @brief  Removes last element.
       *
       *  This is a typical stack operation.  It shrinks the %list by
       *  one.  Due to the nature of a %list this operation can be done
       *  in constant time, and only invalidates iterators/references to
       *  the element being removed.
       *
       *  Note that no data is returned, and if the last element's data
       *  is needed, it should be retrieved before pop_back() is called.
       */
      void
      pop_back()
      { this->_M_erase(iterator(this->_M_impl._M_node._M_prev)); }

      /**
       *  @brief  Inserts given value into %list before specified iterator.
       *  @param  position  An iterator into the %list.
       *  @param  x  Data to be inserted.
       *  @return  An iterator that points to the inserted data.
       *
       *  This function will insert a copy of the given value before
       *  the specified location.  Due to the nature of a %list this
       *  operation can be done in constant time, and does not
       *  invalidate iterators and references.
       */
      iterator
      insert(iterator __position, const value_type& __x);

      /**
       *  @brief  Inserts a number of copies of given data into the %list.
       *  @param  position  An iterator into the %list.
       *  @param  n  Number of elements to be inserted.
       *  @param  x  Data to be inserted.
       *
       *  This function will insert a specified number of copies of the
       *  given data before the location specified by @a position.
       *
       *  This operation is linear in the number of elements inserted and
       *  does not invalidate iterators and references.
       */
      void
      insert(iterator __position, size_type __n, const value_type& __x)
      {  
	list __tmp(__n, __x, _M_get_Node_allocator());
	splice(__position, __tmp);
      }

      /**
       *  @brief  Inserts a range into the %list.
       *  @param  position  An iterator into the %list.
       *  @param  first  An input iterator.
       *  @param  last   An input iterator.
       *
       *  This function will insert copies of the data in the range [@a
       *  first,@a last) into the %list before the location specified by
       *  @a position.
       *
       *  This operation is linear in the number of elements inserted and
       *  does not invalidate iterators and references.
       */
      template<typename _InputIterator>
        void
        insert(iterator __position, _InputIterator __first,
	       _InputIterator __last)
        {
	  list __tmp(__first, __last, _M_get_Node_allocator());
	  splice(__position, __tmp);
	}

      /**
       *  @brief  Remove element at given position.
       *  @param  position  Iterator pointing to element to be erased.
       *  @return  An iterator pointing to the next element (or end()).
       *
       *  This function will erase the element at the given position and thus
       *  shorten the %list by one.
       *
       *  Due to the nature of a %list this operation can be done in
       *  constant time, and only invalidates iterators/references to
       *  the element being removed.  The user is also cautioned that
       *  this function only erases the element, and that if the element
       *  is itself a pointer, the pointed-to memory is not touched in
       *  any way.  Managing the pointer is the user's responsibilty.
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
       *  This function will erase the elements in the range @a
       *  [first,last) and shorten the %list accordingly.
       *
       *  This operation is linear time in the size of the range and only
       *  invalidates iterators/references to the element being removed.
       *  The user is also cautioned that this function only erases the
       *  elements, and that if the elements themselves are pointers, the
       *  pointed-to memory is not touched in any way.  Managing the pointer
       *  is the user's responsibilty.
       */
      iterator
      erase(iterator __first, iterator __last)
      {
	while (__first != __last)
	  __first = erase(__first);
	return __last;
      }

      /**
       *  @brief  Swaps data with another %list.
       *  @param  x  A %list of the same element and allocator types.
       *
       *  This exchanges the elements between two lists in constant
       *  time.  Note that the global std::swap() function is
       *  specialized such that std::swap(l1,l2) will feed to this
       *  function.
       */
      void
      swap(list& __x)
      {
	_List_node_base::swap(this->_M_impl._M_node, __x._M_impl._M_node);

	// _GLIBCXX_RESOLVE_LIB_DEFECTS
	// 431. Swapping containers with unequal allocators.
	std::__alloc_swap<typename _Base::_Node_alloc_type>::
	  _S_do_it(_M_get_Node_allocator(), __x._M_get_Node_allocator());
      }

      /**
       *  Erases all the elements.  Note that this function only erases
       *  the elements, and that if the elements themselves are
       *  pointers, the pointed-to memory is not touched in any way.
       *  Managing the pointer is the user's responsibilty.
       */
      void
      clear()
      {
        _Base::_M_clear();
        _Base::_M_init();
      }

      // [23.2.2.4] list operations
      /**
       *  @brief  Insert contents of another %list.
       *  @param  position  Iterator referencing the element to insert before.
       *  @param  x  Source list.
       *
       *  The elements of @a x are inserted in constant time in front of
       *  the element referenced by @a position.  @a x becomes an empty
       *  list.
       *
       *  Requires this != @a x.
       */
      void
      splice(iterator __position, list& __x)
      {
	if (!__x.empty())
	  {
	    _M_check_equal_allocators(__x);

	    this->_M_transfer(__position, __x.begin(), __x.end());
	  }
      }

      /**
       *  @brief  Insert element from another %list.
       *  @param  position  Iterator referencing the element to insert before.
       *  @param  x  Source list.
       *  @param  i  Iterator referencing the element to move.
       *
       *  Removes the element in list @a x referenced by @a i and
       *  inserts it into the current list before @a position.
       */
      void
      splice(iterator __position, list& __x, iterator __i)
      {
	iterator __j = __i;
	++__j;
	if (__position == __i || __position == __j)
	  return;

	if (this != &__x)
	  _M_check_equal_allocators(__x);

	this->_M_transfer(__position, __i, __j);
      }

      /**
       *  @brief  Insert range from another %list.
       *  @param  position  Iterator referencing the element to insert before.
       *  @param  x  Source list.
       *  @param  first  Iterator referencing the start of range in x.
       *  @param  last  Iterator referencing the end of range in x.
       *
       *  Removes elements in the range [first,last) and inserts them
       *  before @a position in constant time.
       *
       *  Undefined if @a position is in [first,last).
       */
      void
      splice(iterator __position, list& __x, iterator __first, iterator __last)
      {
	if (__first != __last)
	  {
	    if (this != &__x)
	      _M_check_equal_allocators(__x);

	    this->_M_transfer(__position, __first, __last);
	  }
      }

      /**
       *  @brief  Remove all elements equal to value.
       *  @param  value  The value to remove.
       *
       *  Removes every element in the list equal to @a value.
       *  Remaining elements stay in list order.  Note that this
       *  function only erases the elements, and that if the elements
       *  themselves are pointers, the pointed-to memory is not
       *  touched in any way.  Managing the pointer is the user's
       *  responsibilty.
       */
      void
      remove(const _Tp& __value);

      /**
       *  @brief  Remove all elements satisfying a predicate.
       *  @param  Predicate  Unary predicate function or object.
       *
       *  Removes every element in the list for which the predicate
       *  returns true.  Remaining elements stay in list order.  Note
       *  that this function only erases the elements, and that if the
       *  elements themselves are pointers, the pointed-to memory is
       *  not touched in any way.  Managing the pointer is the user's
       *  responsibilty.
       */
      template<typename _Predicate>
        void
        remove_if(_Predicate);

      /**
       *  @brief  Remove consecutive duplicate elements.
       *
       *  For each consecutive set of elements with the same value,
       *  remove all but the first one.  Remaining elements stay in
       *  list order.  Note that this function only erases the
       *  elements, and that if the elements themselves are pointers,
       *  the pointed-to memory is not touched in any way.  Managing
       *  the pointer is the user's responsibilty.
       */
      void
      unique();

      /**
       *  @brief  Remove consecutive elements satisfying a predicate.
       *  @param  BinaryPredicate  Binary predicate function or object.
       *
       *  For each consecutive set of elements [first,last) that
       *  satisfy predicate(first,i) where i is an iterator in
       *  [first,last), remove all but the first one.  Remaining
       *  elements stay in list order.  Note that this function only
       *  erases the elements, and that if the elements themselves are
       *  pointers, the pointed-to memory is not touched in any way.
       *  Managing the pointer is the user's responsibilty.
       */
      template<typename _BinaryPredicate>
        void
        unique(_BinaryPredicate);

      /**
       *  @brief  Merge sorted lists.
       *  @param  x  Sorted list to merge.
       *
       *  Assumes that both @a x and this list are sorted according to
       *  operator<().  Merges elements of @a x into this list in
       *  sorted order, leaving @a x empty when complete.  Elements in
       *  this list precede elements in @a x that are equal.
       */
      void
      merge(list& __x);

      /**
       *  @brief  Merge sorted lists according to comparison function.
       *  @param  x  Sorted list to merge.
       *  @param StrictWeakOrdering Comparison function definining
       *  sort order.
       *
       *  Assumes that both @a x and this list are sorted according to
       *  StrictWeakOrdering.  Merges elements of @a x into this list
       *  in sorted order, leaving @a x empty when complete.  Elements
       *  in this list precede elements in @a x that are equivalent
       *  according to StrictWeakOrdering().
       */
      template<typename _StrictWeakOrdering>
        void
        merge(list&, _StrictWeakOrdering);

      /**
       *  @brief  Reverse the elements in list.
       *
       *  Reverse the order of elements in the list in linear time.
       */
      void
      reverse()
      { this->_M_impl._M_node.reverse(); }

      /**
       *  @brief  Sort the elements.
       *
       *  Sorts the elements of this list in NlogN time.  Equivalent
       *  elements remain in list order.
       */
      void
      sort();

      /**
       *  @brief  Sort the elements according to comparison function.
       *
       *  Sorts the elements of this list in NlogN time.  Equivalent
       *  elements remain in list order.
       */
      template<typename _StrictWeakOrdering>
        void
        sort(_StrictWeakOrdering);

    protected:
      // Internal constructor functions follow.

      // Called by the range constructor to implement [23.1.1]/9
      template<typename _Integer>
        void
        _M_initialize_dispatch(_Integer __n, _Integer __x, __true_type)
        {
	  _M_fill_initialize(static_cast<size_type>(__n),
			     static_cast<value_type>(__x));
	}

      // Called by the range constructor to implement [23.1.1]/9
      template<typename _InputIterator>
        void
        _M_initialize_dispatch(_InputIterator __first, _InputIterator __last,
			       __false_type)
        {
	  for (; __first != __last; ++__first)
	    push_back(*__first);
	}

      // Called by list(n,v,a), and the range constructor when it turns out
      // to be the same thing.
      void
      _M_fill_initialize(size_type __n, const value_type& __x)
      {
	for (; __n > 0; --__n)
	  push_back(__x);
      }


      // Internal assign functions follow.

      // Called by the range assign to implement [23.1.1]/9
      template<typename _Integer>
        void
        _M_assign_dispatch(_Integer __n, _Integer __val, __true_type)
        {
	  _M_fill_assign(static_cast<size_type>(__n),
			 static_cast<value_type>(__val));
	}

      // Called by the range assign to implement [23.1.1]/9
      template<typename _InputIterator>
        void
        _M_assign_dispatch(_InputIterator __first, _InputIterator __last,
			   __false_type);

      // Called by assign(n,t), and the range assign when it turns out
      // to be the same thing.
      void
      _M_fill_assign(size_type __n, const value_type& __val);


      // Moves the elements from [first,last) before position.
      void
      _M_transfer(iterator __position, iterator __first, iterator __last)
      { __position._M_node->transfer(__first._M_node, __last._M_node); }

      // Inserts new element at position given and with value given.
      void
      _M_insert(iterator __position, const value_type& __x)
      {
        _Node* __tmp = _M_create_node(__x);
        __tmp->hook(__position._M_node);
      }

      // Erases element at position given.
      void
      _M_erase(iterator __position)
      {
        __position._M_node->unhook();
        _Node* __n = static_cast<_Node*>(__position._M_node);
        _M_get_Tp_allocator().destroy(&__n->_M_data);
        _M_put_node(__n);
      }

      // To implement the splice (and merge) bits of N1599.
      void
      _M_check_equal_allocators(list& __x)
      {
	if (_M_get_Node_allocator() != __x._M_get_Node_allocator())
	  __throw_runtime_error(__N("list::_M_check_equal_allocators"));
      }
    };

  /**
   *  @brief  List equality comparison.
   *  @param  x  A %list.
   *  @param  y  A %list of the same type as @a x.
   *  @return  True iff the size and elements of the lists are equal.
   *
   *  This is an equivalence relation.  It is linear in the size of
   *  the lists.  Lists are considered equivalent if their sizes are
   *  equal, and if corresponding elements compare equal.
  */
  template<typename _Tp, typename _Alloc>
    inline bool
    operator==(const list<_Tp, _Alloc>& __x, const list<_Tp, _Alloc>& __y)
    {
      typedef typename list<_Tp, _Alloc>::const_iterator const_iterator;
      const_iterator __end1 = __x.end();
      const_iterator __end2 = __y.end();

      const_iterator __i1 = __x.begin();
      const_iterator __i2 = __y.begin();
      while (__i1 != __end1 && __i2 != __end2 && *__i1 == *__i2)
	{
	  ++__i1;
	  ++__i2;
	}
      return __i1 == __end1 && __i2 == __end2;
    }

  /**
   *  @brief  List ordering relation.
   *  @param  x  A %list.
   *  @param  y  A %list of the same type as @a x.
   *  @return  True iff @a x is lexicographically less than @a y.
   *
   *  This is a total ordering relation.  It is linear in the size of the
   *  lists.  The elements must be comparable with @c <.
   *
   *  See std::lexicographical_compare() for how the determination is made.
  */
  template<typename _Tp, typename _Alloc>
    inline bool
    operator<(const list<_Tp, _Alloc>& __x, const list<_Tp, _Alloc>& __y)
    { return std::lexicographical_compare(__x.begin(), __x.end(),
					  __y.begin(), __y.end()); }

  /// Based on operator==
  template<typename _Tp, typename _Alloc>
    inline bool
    operator!=(const list<_Tp, _Alloc>& __x, const list<_Tp, _Alloc>& __y)
    { return !(__x == __y); }

  /// Based on operator<
  template<typename _Tp, typename _Alloc>
    inline bool
    operator>(const list<_Tp, _Alloc>& __x, const list<_Tp, _Alloc>& __y)
    { return __y < __x; }

  /// Based on operator<
  template<typename _Tp, typename _Alloc>
    inline bool
    operator<=(const list<_Tp, _Alloc>& __x, const list<_Tp, _Alloc>& __y)
    { return !(__y < __x); }

  /// Based on operator<
  template<typename _Tp, typename _Alloc>
    inline bool
    operator>=(const list<_Tp, _Alloc>& __x, const list<_Tp, _Alloc>& __y)
    { return !(__x < __y); }

  /// See std::list::swap().
  template<typename _Tp, typename _Alloc>
    inline void
    swap(list<_Tp, _Alloc>& __x, list<_Tp, _Alloc>& __y)
    { __x.swap(__y); }

_GLIBCXX_END_NESTED_NAMESPACE

#endif /* _LIST_H */

