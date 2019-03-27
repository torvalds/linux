// std::list utilities implementation -*- C++ -*-

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

#include <list>

_GLIBCXX_BEGIN_NESTED_NAMESPACE(std, _GLIBCXX_STD)

  void
  _List_node_base::swap(_List_node_base& __x, _List_node_base& __y)
  {
    if ( __x._M_next != &__x )
    {
      if ( __y._M_next != &__y )
      {
        // Both __x and __y are not empty.
        std::swap(__x._M_next,__y._M_next);
        std::swap(__x._M_prev,__y._M_prev);
        __x._M_next->_M_prev = __x._M_prev->_M_next = &__x;
        __y._M_next->_M_prev = __y._M_prev->_M_next = &__y;
      }
      else
      {
        // __x is not empty, __y is empty.
        __y._M_next = __x._M_next;
        __y._M_prev = __x._M_prev;
        __y._M_next->_M_prev = __y._M_prev->_M_next = &__y;        
        __x._M_next = __x._M_prev = &__x;
      }
    }
    else if ( __y._M_next != &__y )
    {
      // __x is empty, __y is not empty.
      __x._M_next = __y._M_next;
      __x._M_prev = __y._M_prev;
      __x._M_next->_M_prev = __x._M_prev->_M_next = &__x;      
      __y._M_next = __y._M_prev = &__y;
    }
  }

  void
  _List_node_base::transfer(_List_node_base * const __first,
                            _List_node_base * const __last)
  {
    if (this != __last)
    {
      // Remove [first, last) from its old position.
      __last->_M_prev->_M_next  = this;
      __first->_M_prev->_M_next = __last;
      this->_M_prev->_M_next    = __first;
  
      // Splice [first, last) into its new position.
      _List_node_base* const __tmp = this->_M_prev;
      this->_M_prev                = __last->_M_prev;
      __last->_M_prev              = __first->_M_prev;
      __first->_M_prev             = __tmp;
    }
  }

  void
  _List_node_base::reverse()
  {
    _List_node_base* __tmp = this;
    do
    {
      std::swap(__tmp->_M_next, __tmp->_M_prev);
      __tmp = __tmp->_M_prev;     // Old next node is now prev.
    } 
    while (__tmp != this);
  }

  void
  _List_node_base::hook(_List_node_base* const __position)
  {
    this->_M_next = __position;
    this->_M_prev = __position->_M_prev;
    __position->_M_prev->_M_next = this;
    __position->_M_prev = this;
  }

  void
  _List_node_base::unhook()
  {
    _List_node_base* const __next_node = this->_M_next;
    _List_node_base* const __prev_node = this->_M_prev;
    __prev_node->_M_next = __next_node;
    __next_node->_M_prev = __prev_node;
  }

_GLIBCXX_END_NESTED_NAMESPACE
