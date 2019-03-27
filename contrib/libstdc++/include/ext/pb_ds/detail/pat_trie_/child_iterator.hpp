// -*- C++ -*-

// Copyright (C) 2005, 2006 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software
// Foundation; either version 2, or (at your option) any later
// version.

// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this library; see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330, Boston,
// MA 02111-1307, USA.

// As a special exception, you may use this file as part of a free
// software library without restriction.  Specifically, if other files
// instantiate templates or use macros or inline functions from this
// file, or you compile this file and link it with other files to
// produce an executable, this file does not by itself cause the
// resulting executable to be covered by the GNU General Public
// License.  This exception does not however invalidate any other
// reasons why the executable file might be covered by the GNU General
// Public License.

// Copyright (C) 2004 Ami Tavory and Vladimir Dreizin, IBM-HRL.

// Permission to use, copy, modify, sell, and distribute this software
// is hereby granted without fee, provided that the above copyright
// notice appears in all copies, and that both that copyright notice
// and this permission notice appear in supporting documentation. None
// of the above authors, nor IBM Haifa Research Laboratories, make any
// representation about the suitability of this software for any
// purpose. It is provided "as is" without express or implied
// warranty.

/**
 * @file child_iterator.hpp
 * Contains a iterator for a patricia tree.
 */

struct iterator : public const_iterator
{
public:
  typedef std::forward_iterator_tag iterator_category;
  typedef typename Allocator::difference_type difference_type;
  typedef node_pointer value_type;
  typedef node_pointer_pointer pointer;
  typedef node_pointer_reference reference;

  inline
  iterator(node_pointer_pointer p_p_cur = NULL,  
	   node_pointer_pointer p_p_end = NULL) 
  : const_iterator(p_p_cur, p_p_end)
  { }

  inline bool
  operator==(const iterator& other) const
  { return const_iterator::m_p_p_cur == other.m_p_p_cur; }

  inline bool
  operator!=(const iterator& other) const
  { return const_iterator::m_p_p_cur != other.m_p_p_cur; }

  inline iterator& 
  operator++()
  {
    const_iterator::operator++();
    return *this;
  }

  inline iterator
  operator++(int)
  {
    iterator ret_it(*this);
    operator++();
    return ret_it;
  }

  node_pointer_pointer
  operator->()
  {
    _GLIBCXX_DEBUG_ONLY(const_iterator::assert_referencible();)
    return const_iterator::m_p_p_cur;
  }

  node_pointer
  operator*()
  {
    _GLIBCXX_DEBUG_ONLY(const_iterator::assert_referencible();)
    return *const_iterator::m_p_p_cur;
  }
};

