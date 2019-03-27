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
 * @file split_fn_imps.hpp
 * Contains an implementation class for bin_search_tree_.
 */

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
split(const_key_reference r_key, PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid(););
  _GLIBCXX_DEBUG_ONLY(other.assert_valid(););
  split_join_branch_bag bag;
  leaf_pointer p_split_lf = split_prep(r_key, other, bag);
  if (p_split_lf == NULL)
    {
      _GLIBCXX_DEBUG_ASSERT(bag.empty());
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
      return;
    }

  _GLIBCXX_DEBUG_ASSERT(!bag.empty());
  other.clear();
  m_p_head->m_p_parent = rec_split(m_p_head->m_p_parent,
				   pref_begin(p_split_lf),
				   pref_end(p_split_lf),
				   other,
				   bag);

  m_p_head->m_p_parent->m_p_parent = m_p_head;

  other.m_p_head->m_p_max = m_p_head->m_p_max;
  m_p_head->m_p_max = rightmost_descendant(m_p_head->m_p_parent);
  other.m_p_head->m_p_min =
    other.leftmost_descendant(other.m_p_head->m_p_parent);

  other.m_size = std::distance(other.PB_DS_CLASS_C_DEC::begin(),
			       other.PB_DS_CLASS_C_DEC::end());
  m_size -= other.m_size;
  _GLIBCXX_DEBUG_ONLY(assert_valid(););
  _GLIBCXX_DEBUG_ONLY(other.assert_valid(););
}

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::leaf_pointer
PB_DS_CLASS_C_DEC::
split_prep(const_key_reference r_key, PB_DS_CLASS_C_DEC& other, split_join_branch_bag& r_bag)
{
  _GLIBCXX_DEBUG_ASSERT(r_bag.empty());
  if (m_size == 0)
    {
      other.clear();
      _GLIBCXX_DEBUG_ONLY(assert_valid(););
      _GLIBCXX_DEBUG_ONLY(other.assert_valid(););
      return (NULL);
    }

  if (synth_e_access_traits::cmp_keys(r_key,
				      PB_DS_V2F(static_cast<const_leaf_pointer>(m_p_head->m_p_min)->value())))
    {
      other.clear();
      value_swap(other);
      _GLIBCXX_DEBUG_ONLY(assert_valid(););
      _GLIBCXX_DEBUG_ONLY(other.assert_valid(););
      return (NULL);
    }

  if (!synth_e_access_traits::cmp_keys(r_key,
				       PB_DS_V2F(static_cast<const_leaf_pointer>(m_p_head->m_p_max)->value())))
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid(););
      _GLIBCXX_DEBUG_ONLY(other.assert_valid(););
      return (NULL);
    }

  iterator it = lower_bound(r_key);

  if (!synth_e_access_traits::equal_keys(PB_DS_V2F(*it), r_key))
    --it;

  node_pointer p_nd = it.m_p_nd;
  _GLIBCXX_DEBUG_ASSERT(p_nd->m_type == pat_trie_leaf_node_type);
  leaf_pointer p_ret_l = static_cast<leaf_pointer>(p_nd);
  while (p_nd->m_type != pat_trie_head_node_type)
    {
      r_bag.add_branch();
      p_nd = p_nd->m_p_parent;
    }
  _GLIBCXX_DEBUG_ONLY(map_debug_base::split(r_key,(synth_e_access_traits& )(*this), other);)

  return (p_ret_l);
}

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::node_pointer
PB_DS_CLASS_C_DEC::
rec_split(node_pointer p_nd, const_e_iterator b_it, const_e_iterator e_it, PB_DS_CLASS_C_DEC& other, split_join_branch_bag& r_bag)
{
  if (p_nd->m_type == pat_trie_leaf_node_type)
    {
      _GLIBCXX_DEBUG_ASSERT(other.m_p_head->m_p_parent == NULL);
      return (p_nd);
    }

  _GLIBCXX_DEBUG_ASSERT(p_nd->m_type == pat_trie_internal_node_type);
  internal_node_pointer p_internal_nd = static_cast<internal_node_pointer>(p_nd);

  node_pointer p_child_ret = rec_split(p_internal_nd->get_child_node(b_it, e_it, this), b_it, e_it, other, r_bag);

  _GLIBCXX_DEBUG_ONLY(p_child_ret->assert_valid(this);)
  p_internal_nd->replace_child(p_child_ret, b_it, e_it, this);
  apply_update(p_internal_nd, (node_update* )this);

  typename internal_node::iterator child_it  =
    p_internal_nd->get_child_it(b_it, e_it, this);

  const size_type lhs_num_children =
    std::distance(p_internal_nd->begin(), child_it) + 1;

  _GLIBCXX_DEBUG_ASSERT(lhs_num_children > 0);

  size_type rhs_num_children =
    std::distance(p_internal_nd->begin(), p_internal_nd->end()) -
    lhs_num_children;

  if (rhs_num_children == 0)
    {
      apply_update(p_internal_nd, (node_update* )this);
      return (p_internal_nd);
    }

  ++child_it;
  other.split_insert_branch(p_internal_nd->get_e_ind(),
			    b_it, child_it, rhs_num_children, r_bag);

  child_it = p_internal_nd->get_child_it(b_it, e_it, this);
  ++child_it;
  while (rhs_num_children != 0)
    {
      child_it = p_internal_nd->remove_child(child_it);
      --rhs_num_children;
    }

  apply_update(p_internal_nd, (node_update* )this);
  _GLIBCXX_DEBUG_ASSERT(std::distance(p_internal_nd->begin(),
				      p_internal_nd->end()) >= 1);

  if (std::distance(p_internal_nd->begin(), p_internal_nd->end()) > 1)
    {
      p_internal_nd->update_prefixes(this);
      _GLIBCXX_DEBUG_ONLY(p_internal_nd->assert_valid(this);)
      apply_update(p_internal_nd, (node_update* )this);
      return (p_internal_nd);
    }

  node_pointer p_ret =* p_internal_nd->begin();
  p_internal_nd->~internal_node();
  s_internal_node_allocator.deallocate(p_internal_nd, 1);
  apply_update(p_ret, (node_update* )this);
  return (p_ret);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
split_insert_branch(size_type e_ind, const_e_iterator b_it, typename internal_node::iterator child_b_it, size_type num_children, split_join_branch_bag& r_bag)
{
#ifdef _GLIBCXX_DEBUG
  if (m_p_head->m_p_parent != NULL)
    m_p_head->m_p_parent->assert_valid(this);
#endif 

  const size_type total_num_children =((m_p_head->m_p_parent == NULL)? 0 : 1) + num_children;

  if (total_num_children == 0)
    {
      _GLIBCXX_DEBUG_ASSERT(m_p_head->m_p_parent == NULL);
      return;
    }

  if (total_num_children == 1)
    {
      if (m_p_head->m_p_parent != NULL)
        {
	  _GLIBCXX_DEBUG_ONLY(m_p_head->m_p_parent->assert_valid(this);)
          return;
        }

      _GLIBCXX_DEBUG_ASSERT(m_p_head->m_p_parent == NULL);
      m_p_head->m_p_parent =* child_b_it;
      m_p_head->m_p_parent->m_p_parent = m_p_head;
      apply_update(m_p_head->m_p_parent, (node_update* )this);
      _GLIBCXX_DEBUG_ONLY(m_p_head->m_p_parent->assert_valid(this);)
      return;
    }

  _GLIBCXX_DEBUG_ASSERT(total_num_children > 1);
  internal_node_pointer p_new_root = r_bag.get_branch();
  new (p_new_root) internal_node(e_ind, b_it);
  size_type num_inserted = 0;
  while (num_inserted++ < num_children)
    {
      _GLIBCXX_DEBUG_ONLY((*child_b_it)->assert_valid(this);)
        p_new_root->add_child(*child_b_it, pref_begin(*child_b_it),
			      pref_end(*child_b_it), this);
      ++child_b_it;
    }

  if (m_p_head->m_p_parent != NULL)
    p_new_root->add_child(m_p_head->m_p_parent, 
			  pref_begin(m_p_head->m_p_parent),
			  pref_end(m_p_head->m_p_parent), this);

  m_p_head->m_p_parent = p_new_root;
  p_new_root->m_p_parent = m_p_head;
  apply_update(m_p_head->m_p_parent, (node_update* )this);
  _GLIBCXX_DEBUG_ONLY(m_p_head->m_p_parent->assert_valid(this);)
}
