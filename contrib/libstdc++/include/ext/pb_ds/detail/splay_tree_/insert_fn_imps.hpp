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
 * Contains an implementation class for splay_tree_.
 */

PB_DS_CLASS_T_DEC
inline std::pair<typename PB_DS_CLASS_C_DEC::point_iterator, bool>
PB_DS_CLASS_C_DEC::
insert(const_reference r_value)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  std::pair<point_iterator, bool> ins_pair = insert_leaf_imp(r_value);
  ins_pair.first.m_p_nd->m_special = false;
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  splay(ins_pair.first.m_p_nd);
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  return ins_pair;
}

PB_DS_CLASS_T_DEC
inline std::pair<typename PB_DS_CLASS_C_DEC::point_iterator, bool>
PB_DS_CLASS_C_DEC::
insert_leaf_imp(const_reference r_value)
{
  _GLIBCXX_DEBUG_ONLY(base_type::structure_only_assert_valid();)
  if (base_type::m_size == 0)
    return std::make_pair(base_type::insert_imp_empty(r_value), true);

  node_pointer p_nd = base_type::m_p_head->m_p_parent;
  node_pointer p_pot = base_type::m_p_head;

  while (p_nd != NULL)
    if (!Cmp_Fn::operator()(PB_DS_V2F(p_nd->m_value), PB_DS_V2F(r_value)))
      {
	if (!Cmp_Fn::operator()(PB_DS_V2F(r_value), PB_DS_V2F(p_nd->m_value)))
	  {
	    return std::make_pair(point_iterator(p_nd), false);
	  }
	p_pot = p_nd;
	p_nd = p_nd->m_p_left;
      }
    else
      p_nd = p_nd->m_p_right;

  if (p_pot == base_type::m_p_head)
    return std::make_pair(base_type::insert_leaf_new(r_value, base_type::m_p_head->m_p_right, false), true);

  _GLIBCXX_DEBUG_ONLY(base_type::check_key_does_not_exist(PB_DS_V2F(r_value)));

  p_nd = p_pot->m_p_left;
  if (p_nd == NULL)
    return (std::make_pair(base_type::insert_leaf_new(r_value, p_pot, true), true));

  while (p_nd->m_p_right != NULL)
    p_nd = p_nd->m_p_right;

  return std::make_pair(insert_leaf_new(r_value, p_nd, false), true);
}
