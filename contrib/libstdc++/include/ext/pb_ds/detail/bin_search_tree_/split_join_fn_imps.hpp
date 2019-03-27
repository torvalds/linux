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
 * @file split_join_fn_imps.hpp
 * Contains an implementation class for bin_search_tree_.
 */

PB_DS_CLASS_T_DEC
bool
PB_DS_CLASS_C_DEC::
join_prep(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
  if (other.m_size == 0)
    return false;

  if (m_size == 0)
    {
      value_swap(other);
      return false;
    }

  const bool greater = Cmp_Fn::operator()(PB_DS_V2F(m_p_head->m_p_right->m_value), PB_DS_V2F(other.m_p_head->m_p_left->m_value));

  const bool lesser = Cmp_Fn::operator()(PB_DS_V2F(other.m_p_head->m_p_right->m_value), PB_DS_V2F(m_p_head->m_p_left->m_value));

  if (!greater && !lesser)
    __throw_join_error();

  if (lesser)
    value_swap(other);

  m_size += other.m_size;
  _GLIBCXX_DEBUG_ONLY(map_debug_base::join(other);)
  return true;
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
join_finish(PB_DS_CLASS_C_DEC& other)
{
  initialize_min_max();
  other.initialize();
}

PB_DS_CLASS_T_DEC
bool
PB_DS_CLASS_C_DEC::
split_prep(const_key_reference r_key, PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
  other.clear();

  if (m_size == 0)
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
      return false;
    }

  if (Cmp_Fn::operator()(r_key, PB_DS_V2F(m_p_head->m_p_left->m_value)))
    {
      value_swap(other);
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
      return false;
    }

  if (!Cmp_Fn::operator()(r_key, PB_DS_V2F(m_p_head->m_p_right->m_value)))
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
      return false;
    }

  if (m_size == 1)
    {
      value_swap(other);
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
      return false;
    }

  _GLIBCXX_DEBUG_ONLY(map_debug_base::split(r_key,(Cmp_Fn& )(*this), other);)
  return true;
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
split_finish(PB_DS_CLASS_C_DEC& other)
{
  other.initialize_min_max();
  other.m_size = std::distance(other.begin(), other.end());
  m_size -= other.m_size;
  initialize_min_max();
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
}

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
recursive_count(node_pointer p) const
{
  if (p == NULL)
    return 0;
  return 1 + recursive_count(p->m_p_left) + recursive_count(p->m_p_right);
}

