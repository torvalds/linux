// RB tree utilities implementation -*- C++ -*-

// Copyright (C) 2003, 2005 Free Software Foundation, Inc.
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
 *
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
 */

#include <bits/stl_tree.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  _Rb_tree_node_base*
  _Rb_tree_increment(_Rb_tree_node_base* __x)
  {
    if (__x->_M_right != 0) 
      {
        __x = __x->_M_right;
        while (__x->_M_left != 0)
          __x = __x->_M_left;
      }
    else 
      {
        _Rb_tree_node_base* __y = __x->_M_parent;
        while (__x == __y->_M_right) 
          {
            __x = __y;
            __y = __y->_M_parent;
          }
        if (__x->_M_right != __y)
          __x = __y;
      }
    return __x;
  }

  const _Rb_tree_node_base*
  _Rb_tree_increment(const _Rb_tree_node_base* __x)
  {
    return _Rb_tree_increment(const_cast<_Rb_tree_node_base*>(__x));
  }

  _Rb_tree_node_base*
  _Rb_tree_decrement(_Rb_tree_node_base* __x)
  {
    if (__x->_M_color == _S_red 
        && __x->_M_parent->_M_parent == __x)
      __x = __x->_M_right;
    else if (__x->_M_left != 0) 
      {
        _Rb_tree_node_base* __y = __x->_M_left;
        while (__y->_M_right != 0)
          __y = __y->_M_right;
        __x = __y;
      }
    else 
      {
        _Rb_tree_node_base* __y = __x->_M_parent;
        while (__x == __y->_M_left) 
          {
            __x = __y;
            __y = __y->_M_parent;
          }
        __x = __y;
      }
    return __x;
  }

  const _Rb_tree_node_base*
  _Rb_tree_decrement(const _Rb_tree_node_base* __x)
  {
    return _Rb_tree_decrement(const_cast<_Rb_tree_node_base*>(__x));
  }

  void 
  _Rb_tree_rotate_left(_Rb_tree_node_base* const __x, 
		       _Rb_tree_node_base*& __root)
  {
    _Rb_tree_node_base* const __y = __x->_M_right;

    __x->_M_right = __y->_M_left;
    if (__y->_M_left !=0)
      __y->_M_left->_M_parent = __x;
    __y->_M_parent = __x->_M_parent;
    
    if (__x == __root)
      __root = __y;
    else if (__x == __x->_M_parent->_M_left)
      __x->_M_parent->_M_left = __y;
    else
      __x->_M_parent->_M_right = __y;
    __y->_M_left = __x;
    __x->_M_parent = __y;
  }

  void 
  _Rb_tree_rotate_right(_Rb_tree_node_base* const __x, 
			_Rb_tree_node_base*& __root)
  {
    _Rb_tree_node_base* const __y = __x->_M_left;

    __x->_M_left = __y->_M_right;
    if (__y->_M_right != 0)
      __y->_M_right->_M_parent = __x;
    __y->_M_parent = __x->_M_parent;

    if (__x == __root)
      __root = __y;
    else if (__x == __x->_M_parent->_M_right)
      __x->_M_parent->_M_right = __y;
    else
      __x->_M_parent->_M_left = __y;
    __y->_M_right = __x;
    __x->_M_parent = __y;
  }

  void 
  _Rb_tree_insert_and_rebalance(const bool          __insert_left,
                                _Rb_tree_node_base* __x,
                                _Rb_tree_node_base* __p,
                                _Rb_tree_node_base& __header)
  {
    _Rb_tree_node_base *& __root = __header._M_parent;

    // Initialize fields in new node to insert.
    __x->_M_parent = __p;
    __x->_M_left = 0;
    __x->_M_right = 0;
    __x->_M_color = _S_red;

    // Insert.
    // Make new node child of parent and maintain root, leftmost and
    // rightmost nodes.
    // N.B. First node is always inserted left.
    if (__insert_left)
      {
        __p->_M_left = __x; // also makes leftmost = __x when __p == &__header

        if (__p == &__header)
        {
            __header._M_parent = __x;
            __header._M_right = __x;
        }
        else if (__p == __header._M_left)
          __header._M_left = __x; // maintain leftmost pointing to min node
      }
    else
      {
        __p->_M_right = __x;

        if (__p == __header._M_right)
          __header._M_right = __x; // maintain rightmost pointing to max node
      }
    // Rebalance.
    while (__x != __root 
	   && __x->_M_parent->_M_color == _S_red) 
      {
	_Rb_tree_node_base* const __xpp = __x->_M_parent->_M_parent;

	if (__x->_M_parent == __xpp->_M_left) 
	  {
	    _Rb_tree_node_base* const __y = __xpp->_M_right;
	    if (__y && __y->_M_color == _S_red) 
	      {
		__x->_M_parent->_M_color = _S_black;
		__y->_M_color = _S_black;
		__xpp->_M_color = _S_red;
		__x = __xpp;
	      }
	    else 
	      {
		if (__x == __x->_M_parent->_M_right) 
		  {
		    __x = __x->_M_parent;
		    _Rb_tree_rotate_left(__x, __root);
		  }
		__x->_M_parent->_M_color = _S_black;
		__xpp->_M_color = _S_red;
		_Rb_tree_rotate_right(__xpp, __root);
	      }
	  }
	else 
	  {
	    _Rb_tree_node_base* const __y = __xpp->_M_left;
	    if (__y && __y->_M_color == _S_red) 
	      {
		__x->_M_parent->_M_color = _S_black;
		__y->_M_color = _S_black;
		__xpp->_M_color = _S_red;
		__x = __xpp;
	      }
	    else 
	      {
		if (__x == __x->_M_parent->_M_left) 
		  {
		    __x = __x->_M_parent;
		    _Rb_tree_rotate_right(__x, __root);
		  }
		__x->_M_parent->_M_color = _S_black;
		__xpp->_M_color = _S_red;
		_Rb_tree_rotate_left(__xpp, __root);
	      }
	  }
      }
    __root->_M_color = _S_black;
  }

  _Rb_tree_node_base*
  _Rb_tree_rebalance_for_erase(_Rb_tree_node_base* const __z, 
			       _Rb_tree_node_base& __header)
  {
    _Rb_tree_node_base *& __root = __header._M_parent;
    _Rb_tree_node_base *& __leftmost = __header._M_left;
    _Rb_tree_node_base *& __rightmost = __header._M_right;
    _Rb_tree_node_base* __y = __z;
    _Rb_tree_node_base* __x = 0;
    _Rb_tree_node_base* __x_parent = 0;

    if (__y->_M_left == 0)     // __z has at most one non-null child. y == z.
      __x = __y->_M_right;     // __x might be null.
    else
      if (__y->_M_right == 0)  // __z has exactly one non-null child. y == z.
	__x = __y->_M_left;    // __x is not null.
      else 
	{
	  // __z has two non-null children.  Set __y to
	  __y = __y->_M_right;   //   __z's successor.  __x might be null.
	  while (__y->_M_left != 0)
	    __y = __y->_M_left;
	  __x = __y->_M_right;
	}
    if (__y != __z) 
      {
	// relink y in place of z.  y is z's successor
	__z->_M_left->_M_parent = __y; 
	__y->_M_left = __z->_M_left;
	if (__y != __z->_M_right) 
	  {
	    __x_parent = __y->_M_parent;
	    if (__x) __x->_M_parent = __y->_M_parent;
	    __y->_M_parent->_M_left = __x;   // __y must be a child of _M_left
	    __y->_M_right = __z->_M_right;
	    __z->_M_right->_M_parent = __y;
	  }
	else
	  __x_parent = __y;  
	if (__root == __z)
	  __root = __y;
	else if (__z->_M_parent->_M_left == __z)
	  __z->_M_parent->_M_left = __y;
	else 
	  __z->_M_parent->_M_right = __y;
	__y->_M_parent = __z->_M_parent;
	std::swap(__y->_M_color, __z->_M_color);
	__y = __z;
	// __y now points to node to be actually deleted
      }
    else 
      {                        // __y == __z
	__x_parent = __y->_M_parent;
	if (__x) 
	  __x->_M_parent = __y->_M_parent;   
	if (__root == __z)
	  __root = __x;
	else 
	  if (__z->_M_parent->_M_left == __z)
	    __z->_M_parent->_M_left = __x;
	  else
	    __z->_M_parent->_M_right = __x;
	if (__leftmost == __z) 
	  {
	    if (__z->_M_right == 0)        // __z->_M_left must be null also
	      __leftmost = __z->_M_parent;
	    // makes __leftmost == _M_header if __z == __root
	    else
	      __leftmost = _Rb_tree_node_base::_S_minimum(__x);
	  }
	if (__rightmost == __z)  
	  {
	    if (__z->_M_left == 0)         // __z->_M_right must be null also
	      __rightmost = __z->_M_parent;  
	    // makes __rightmost == _M_header if __z == __root
	    else                      // __x == __z->_M_left
	      __rightmost = _Rb_tree_node_base::_S_maximum(__x);
	  }
      }
    if (__y->_M_color != _S_red) 
      { 
	while (__x != __root && (__x == 0 || __x->_M_color == _S_black))
	  if (__x == __x_parent->_M_left) 
	    {
	      _Rb_tree_node_base* __w = __x_parent->_M_right;
	      if (__w->_M_color == _S_red) 
		{
		  __w->_M_color = _S_black;
		  __x_parent->_M_color = _S_red;
		  _Rb_tree_rotate_left(__x_parent, __root);
		  __w = __x_parent->_M_right;
		}
	      if ((__w->_M_left == 0 || 
		   __w->_M_left->_M_color == _S_black) &&
		  (__w->_M_right == 0 || 
		   __w->_M_right->_M_color == _S_black)) 
		{
		  __w->_M_color = _S_red;
		  __x = __x_parent;
		  __x_parent = __x_parent->_M_parent;
		} 
	      else 
		{
		  if (__w->_M_right == 0 
		      || __w->_M_right->_M_color == _S_black) 
		    {
		      __w->_M_left->_M_color = _S_black;
		      __w->_M_color = _S_red;
		      _Rb_tree_rotate_right(__w, __root);
		      __w = __x_parent->_M_right;
		    }
		  __w->_M_color = __x_parent->_M_color;
		  __x_parent->_M_color = _S_black;
		  if (__w->_M_right) 
		    __w->_M_right->_M_color = _S_black;
		  _Rb_tree_rotate_left(__x_parent, __root);
		  break;
		}
	    } 
	  else 
	    {   
	      // same as above, with _M_right <-> _M_left.
	      _Rb_tree_node_base* __w = __x_parent->_M_left;
	      if (__w->_M_color == _S_red) 
		{
		  __w->_M_color = _S_black;
		  __x_parent->_M_color = _S_red;
		  _Rb_tree_rotate_right(__x_parent, __root);
		  __w = __x_parent->_M_left;
		}
	      if ((__w->_M_right == 0 || 
		   __w->_M_right->_M_color == _S_black) &&
		  (__w->_M_left == 0 || 
		   __w->_M_left->_M_color == _S_black)) 
		{
		  __w->_M_color = _S_red;
		  __x = __x_parent;
		  __x_parent = __x_parent->_M_parent;
		} 
	      else 
		{
		  if (__w->_M_left == 0 || __w->_M_left->_M_color == _S_black) 
		    {
		      __w->_M_right->_M_color = _S_black;
		      __w->_M_color = _S_red;
		      _Rb_tree_rotate_left(__w, __root);
		      __w = __x_parent->_M_left;
		    }
		  __w->_M_color = __x_parent->_M_color;
		  __x_parent->_M_color = _S_black;
		  if (__w->_M_left) 
		    __w->_M_left->_M_color = _S_black;
		  _Rb_tree_rotate_right(__x_parent, __root);
		  break;
		}
	    }
	if (__x) __x->_M_color = _S_black;
      }
    return __y;
  }

  unsigned int
  _Rb_tree_black_count(const _Rb_tree_node_base* __node,
                       const _Rb_tree_node_base* __root)
  {
    if (__node == 0)
      return 0;
    unsigned int __sum = 0;
    do 
      {
	if (__node->_M_color == _S_black) 
	  ++__sum;
	if (__node == __root) 
	  break;
	__node = __node->_M_parent;
      } 
    while (1);
    return __sum;
  }

_GLIBCXX_END_NAMESPACE
