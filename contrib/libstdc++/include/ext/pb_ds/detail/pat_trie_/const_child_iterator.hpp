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
 * @file const_child_iterator.hpp
 * Contains a const_iterator for a patricia tree.
 */

struct const_iterator
{
public:
  typedef std::forward_iterator_tag iterator_category;

  typedef typename Allocator::difference_type difference_type;

  typedef node_pointer value_type;

  typedef node_pointer_pointer pointer;

  typedef node_pointer_reference reference;

public:
  inline
  const_iterator(node_pointer_pointer p_p_cur = NULL,  
		 node_pointer_pointer p_p_end = NULL) 
  : m_p_p_cur(p_p_cur), m_p_p_end(p_p_end)
  { }

  inline bool
  operator==(const const_iterator& other) const
  { return m_p_p_cur == other.m_p_p_cur; }

  inline bool
  operator!=(const const_iterator& other) const
  { return m_p_p_cur != other.m_p_p_cur; }

  inline const_iterator& 
  operator++()
  {
    do
      ++m_p_p_cur;
    while (m_p_p_cur != m_p_p_end&& * m_p_p_cur == NULL);
    return *this;
  }

  inline const_iterator
  operator++(int)
  {
    const_iterator ret_it(*this);
    operator++();
    return ret_it;
  }

  const node_pointer_pointer
  operator->() const
  {
    _GLIBCXX_DEBUG_ONLY(assert_referencible();)
    return (m_p_p_cur);
  }

  const_node_pointer
  operator*() const
  {
    _GLIBCXX_DEBUG_ONLY(assert_referencible();)
    return (*m_p_p_cur);
  }

protected:
#ifdef _GLIBCXX_DEBUG
  void
  assert_referencible() const
  { _GLIBCXX_DEBUG_ASSERT(m_p_p_cur != m_p_p_end&& * m_p_p_cur != NULL); }
#endif 

public:
  node_pointer_pointer m_p_p_cur;
  node_pointer_pointer m_p_p_end;
};

