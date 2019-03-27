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
 * @file insert_fn_imps.hpp
 * Contains an implementation class for a base of binomial heaps.
 */

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::point_iterator
PB_DS_CLASS_C_DEC::
push(const_reference r_val)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)

    node_pointer p_nd = base_type::get_new_node_for_insert(r_val);

  insert_node(p_nd);

  m_p_max = NULL;

  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)

    return point_iterator(p_nd);
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
insert_node(node_pointer p_nd)
{
  if (base_type::m_p_root == NULL)
    {
      p_nd->m_p_next_sibling = p_nd->m_p_prev_or_parent =
	p_nd->m_p_l_child = NULL;

      p_nd->m_metadata = 0;

      base_type::m_p_root = p_nd;

      return;
    }

  if (base_type::m_p_root->m_metadata > 0)
    {
      p_nd->m_p_prev_or_parent = p_nd->m_p_l_child = NULL;

      p_nd->m_p_next_sibling = base_type::m_p_root;

      base_type::m_p_root->m_p_prev_or_parent = p_nd;

      base_type::m_p_root = p_nd;

      p_nd->m_metadata = 0;

      return;
    }

  if (Cmp_Fn::operator()(base_type::m_p_root->m_value, p_nd->m_value))
    {
      p_nd->m_p_next_sibling = base_type::m_p_root->m_p_next_sibling;

      p_nd->m_p_prev_or_parent = NULL;

      p_nd->m_metadata = 1;

      p_nd->m_p_l_child = base_type::m_p_root;

      base_type::m_p_root->m_p_prev_or_parent = p_nd;

      base_type::m_p_root->m_p_next_sibling = NULL;

      base_type::m_p_root = p_nd;
    }
  else
    {
      p_nd->m_p_next_sibling = NULL;

      p_nd->m_p_l_child = NULL;

      p_nd->m_p_prev_or_parent = base_type::m_p_root;

      p_nd->m_metadata = 0;

      _GLIBCXX_DEBUG_ASSERT(base_type::m_p_root->m_p_l_child == 0);
      base_type::m_p_root->m_p_l_child = p_nd;

      base_type::m_p_root->m_metadata = 1;
    }

  base_type::m_p_root = fix(base_type::m_p_root);
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::node_pointer
PB_DS_CLASS_C_DEC::
fix(node_pointer p_nd) const
{
  while (p_nd->m_p_next_sibling != NULL&& 
	 p_nd->m_metadata == p_nd->m_p_next_sibling->m_metadata)
    {
      node_pointer p_next = p_nd->m_p_next_sibling;

      if (Cmp_Fn::operator()(p_nd->m_value, p_next->m_value))
        {
	  p_next->m_p_prev_or_parent =
	    p_nd->m_p_prev_or_parent;

	  if (p_nd->m_p_prev_or_parent != NULL)
	    p_nd->m_p_prev_or_parent->m_p_next_sibling = p_next;

	  base_type::make_child_of(p_nd, p_next);

	  ++p_next->m_metadata;

	  p_nd = p_next;
        }
      else
        {
	  p_nd->m_p_next_sibling = p_next->m_p_next_sibling;

	  if (p_nd->m_p_next_sibling != NULL)
	    p_next->m_p_next_sibling = NULL;

	  base_type::make_child_of(p_next, p_nd);

	  ++p_nd->m_metadata;
        }
    }

  if (p_nd->m_p_next_sibling != NULL)
    p_nd->m_p_next_sibling->m_p_prev_or_parent = p_nd;

  return p_nd;
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
modify(point_iterator it, const_reference r_new_val)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
    node_pointer p_nd = it.m_p_nd;

  _GLIBCXX_DEBUG_ASSERT(p_nd != NULL);
  _GLIBCXX_DEBUG_ONLY(base_type::assert_node_consistent(p_nd, false);)

    const bool bubble_up = Cmp_Fn::operator()(p_nd->m_value, r_new_val);

  p_nd->m_value = r_new_val;

  if (bubble_up)
    {
      node_pointer p_parent = base_type::parent(p_nd);

      while (p_parent != NULL&& 
	     Cmp_Fn::operator()(p_parent->m_value, p_nd->m_value))
        {
	  base_type::swap_with_parent(p_nd, p_parent);

	  p_parent = base_type::parent(p_nd);
        }

      if (p_nd->m_p_prev_or_parent == NULL)
	base_type::m_p_root = p_nd;

      m_p_max = NULL;

      _GLIBCXX_DEBUG_ONLY(assert_valid(true);)

        return;
    }

  base_type::bubble_to_top(p_nd);

  remove_parentless_node(p_nd);

  insert_node(p_nd);

  m_p_max = NULL;

  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
    }

