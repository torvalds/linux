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
 * Contains an implementation class for a base of binomial heaps.
 */

PB_DS_CLASS_T_DEC
template<typename Pred>
void
PB_DS_CLASS_C_DEC::
split(Pred pred, PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
    _GLIBCXX_DEBUG_ONLY(other.assert_valid(true);)

    other.clear();

  if (base_type::empty())
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
        _GLIBCXX_DEBUG_ONLY(other.assert_valid(true);)

        return;
    }

  base_type::to_linked_list();

  node_pointer p_out = base_type::prune(pred);

  while (p_out != NULL)
    {
      _GLIBCXX_DEBUG_ASSERT(base_type::m_size > 0);
      --base_type::m_size;

      ++other.m_size;

      node_pointer p_next = p_out->m_p_next_sibling;

      p_out->m_p_l_child = p_out->m_p_prev_or_parent = NULL;

      p_out->m_metadata = 0;

      p_out->m_p_next_sibling = other.m_p_root;

      if (other.m_p_root != NULL)
	other.m_p_root->m_p_prev_or_parent = p_out;

      other.m_p_root = p_out;

      other.m_p_root = other.fix(other.m_p_root);

      p_out = p_next;
    }

  _GLIBCXX_DEBUG_ONLY(other.assert_valid(true);)

    node_pointer p_cur = base_type::m_p_root;

  base_type::m_p_root = NULL;

  while (p_cur != NULL)
    {
      node_pointer p_next = p_cur->m_p_next_sibling;

      p_cur->m_p_l_child = p_cur->m_p_prev_or_parent = NULL;

      p_cur->m_metadata = 0;

      p_cur->m_p_next_sibling = base_type::m_p_root;

      if (base_type::m_p_root != NULL)
	base_type::m_p_root->m_p_prev_or_parent = p_cur;

      base_type::m_p_root = p_cur;

      base_type::m_p_root = fix(base_type::m_p_root);

      p_cur = p_next;
    }

  m_p_max = NULL;

  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
    _GLIBCXX_DEBUG_ONLY(other.assert_valid(true);)
    }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
join(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
    _GLIBCXX_DEBUG_ONLY(other.assert_valid(true);)

    node_pointer p_other = other.m_p_root;

  if (p_other != NULL)
    do
      {
	node_pointer p_next = p_other->m_p_next_sibling;

	std::swap(p_other->m_p_next_sibling, p_other->m_p_prev_or_parent);

	p_other = p_next;
      }
    while (p_other != NULL);

  base_type::m_p_root = join(base_type::m_p_root, other.m_p_root);
  base_type::m_size += other.m_size;
  m_p_max = NULL;

  other.m_p_root = NULL;
  other.m_size = 0;
  other.m_p_max = NULL;

  _GLIBCXX_DEBUG_ONLY(assert_valid(true);)
    _GLIBCXX_DEBUG_ONLY(other.assert_valid(true);)
    }

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::node_pointer
PB_DS_CLASS_C_DEC::
join(node_pointer p_lhs, node_pointer p_rhs) const
{
  node_pointer p_ret = NULL;

  node_pointer p_cur = NULL;

  while (p_lhs != NULL || p_rhs != NULL)
    {
      if (p_rhs == NULL)
        {
	  if (p_cur == NULL)
	    p_ret = p_cur = p_lhs;
	  else
            {
	      p_cur->m_p_next_sibling = p_lhs;

	      p_lhs->m_p_prev_or_parent = p_cur;
            }

	  p_cur = p_lhs = NULL;
        }
      else if (p_lhs == NULL || p_rhs->m_metadata < p_lhs->m_metadata)
        {
	  if (p_cur == NULL)
            {
	      p_ret = p_cur = p_rhs;

	      p_rhs = p_rhs->m_p_prev_or_parent;
            }
	  else
            {
	      p_cur->m_p_next_sibling = p_rhs;

	      p_rhs = p_rhs->m_p_prev_or_parent;

	      p_cur->m_p_next_sibling->m_p_prev_or_parent = p_cur;

	      p_cur = p_cur->m_p_next_sibling;
            }
        }
      else if (p_lhs->m_metadata < p_rhs->m_metadata)
        {
	  if (p_cur == NULL)
	    p_ret = p_cur = p_lhs;
	  else
            {
	      p_cur->m_p_next_sibling = p_lhs;

	      p_lhs->m_p_prev_or_parent = p_cur;

	      p_cur = p_cur->m_p_next_sibling;
            }

	  p_lhs = p_cur->m_p_next_sibling;
        }
      else
        {
	  node_pointer p_next_rhs = p_rhs->m_p_prev_or_parent;

	  p_rhs->m_p_next_sibling = p_lhs;

	  p_lhs = fix(p_rhs);

	  p_rhs = p_next_rhs;
        }
    }

  if (p_cur != NULL)
    p_cur->m_p_next_sibling = NULL;

  if (p_ret != NULL)
    p_ret->m_p_prev_or_parent = NULL;

  return p_ret;
}

