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
 * @file erase_fn_imps.hpp
 * Contains an implementation class for splay_tree_.
 */

PB_DS_CLASS_T_DEC
inline bool
PB_DS_CLASS_C_DEC::
erase(const_key_reference r_key)
{
  point_iterator it = find(r_key);
  if (it == base_type::end())
    return false;
  erase(it);
  return true;
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::iterator
PB_DS_CLASS_C_DEC::
erase(iterator it)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  if (it == base_type::end())
    return it;
  iterator ret_it = it;
  ++ret_it;
  erase_node(it.m_p_nd);
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  return ret_it;
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::reverse_iterator
PB_DS_CLASS_C_DEC::
erase(reverse_iterator it)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  if (it.m_p_nd == base_type::m_p_head)
    return (it);
  reverse_iterator ret_it = it;
  ++ret_it;
  erase_node(it.m_p_nd);
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  return ret_it;
}

PB_DS_CLASS_T_DEC
template<typename Pred>
inline typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
erase_if(Pred pred)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  size_type num_ersd = 0;
  iterator it = base_type::begin();
  while (it != base_type::end())
    {
      if (pred(*it))
        {
	  ++num_ersd;
	  it = erase(it);
        }
      else
	++it;
    }
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  return num_ersd;
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
erase_node(node_pointer p_nd)
{
  _GLIBCXX_DEBUG_ASSERT(p_nd != NULL);
  splay(p_nd);

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ASSERT(p_nd == this->m_p_head->m_p_parent);

  node_pointer p_l = p_nd->m_p_left;
  node_pointer p_r = p_nd->m_p_right;

  base_type::update_min_max_for_erased_node(p_nd);
  base_type::actual_erase_node(p_nd);
  if (p_r == NULL)
    {
      base_type::m_p_head->m_p_parent = p_l;
      if (p_l != NULL)
	p_l->m_p_parent = base_type::m_p_head;
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      return;
    }

  node_pointer p_target_r = leftmost(p_r);
  _GLIBCXX_DEBUG_ASSERT(p_target_r != NULL);
  p_r->m_p_parent = base_type::m_p_head;
  base_type::m_p_head->m_p_parent = p_r;
  splay(p_target_r);

  _GLIBCXX_DEBUG_ONLY(p_target_r->m_p_left = NULL);
  _GLIBCXX_DEBUG_ASSERT(p_target_r->m_p_parent == this->m_p_head);
  _GLIBCXX_DEBUG_ASSERT(this->m_p_head->m_p_parent == p_target_r);

  p_target_r->m_p_left = p_l;
  if (p_l != NULL)
    p_l->m_p_parent = p_target_r;
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  apply_update(p_target_r, (node_update* )this);
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::node_pointer
PB_DS_CLASS_C_DEC::
leftmost(node_pointer p_nd)
{
  _GLIBCXX_DEBUG_ASSERT(p_nd != NULL);
  while (p_nd->m_p_left != NULL)
    p_nd = p_nd->m_p_left;
  return p_nd;
}
