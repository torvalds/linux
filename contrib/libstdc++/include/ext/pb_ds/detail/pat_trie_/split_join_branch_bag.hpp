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
 * @file split_join_branch_bag.hpp
 * Contains an implementation class for pat_trie_.
 */

class split_join_branch_bag
{
private:
  typedef
  std::list<
  internal_node_pointer,
  typename Allocator::template rebind<
  internal_node_pointer>::other>
  bag_t;

public:

  void
  add_branch()
  {
    internal_node_pointer p_nd = s_internal_node_allocator.allocate(1);
    try
      {
	m_bag.push_back(p_nd);
      }
    catch(...)
      {
	s_internal_node_allocator.deallocate(p_nd, 1);
	__throw_exception_again;
      }
  }

  internal_node_pointer
  get_branch()
  {
    _GLIBCXX_DEBUG_ASSERT(!m_bag.empty());
    internal_node_pointer p_nd =* m_bag.begin();
    m_bag.pop_front();
    return p_nd;
  }

  ~split_join_branch_bag()
  {
    while (!m_bag.empty())
      {
	internal_node_pointer p_nd =* m_bag.begin();
	s_internal_node_allocator.deallocate(p_nd, 1);
	m_bag.pop_front();
      }
  }

  inline bool
  empty() const
  { return m_bag.empty(); }

private:
  bag_t m_bag;
};
