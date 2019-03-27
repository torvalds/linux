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
 * Contains an implementation class for bin_search_tree_.
 */

PB_DS_CLASS_T_DEC
inline bool
PB_DS_CLASS_C_DEC::
erase(const_key_reference r_key)
{
  node_pointer p_nd = find_imp(r_key);
  if (p_nd == NULL || p_nd->m_type == pat_trie_internal_node_type)
    {
      _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(r_key));
      return false;
    }

  _GLIBCXX_DEBUG_ASSERT(p_nd->m_type == pat_trie_leaf_node_type);
  if (!synth_e_access_traits::equal_keys(PB_DS_V2F(reinterpret_cast<leaf_pointer>(p_nd)->value()), r_key))
    {
      _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(r_key));
      return false;
    }

  _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(r_key));
  erase_leaf(static_cast<leaf_pointer>(p_nd));
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  return true;
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
erase_fixup(internal_node_pointer p_nd)
{
  _GLIBCXX_DEBUG_ASSERT(std::distance(p_nd->begin(), p_nd->end()) >= 1);
  if (std::distance(p_nd->begin(), p_nd->end()) == 1)
    {
      node_pointer p_parent = p_nd->m_p_parent;
      if (p_parent == m_p_head)
	m_p_head->m_p_parent =* p_nd->begin();
      else
        {
	  _GLIBCXX_DEBUG_ASSERT(p_parent->m_type == pat_trie_internal_node_type);
	  node_pointer p_new_child =* p_nd->begin();
	  static_cast<internal_node_pointer>(p_parent)->replace_child(
								      p_new_child,
								      pref_begin(p_new_child),
								      pref_end(p_new_child),
								      this);
        }
      (*p_nd->begin())->m_p_parent = p_nd->m_p_parent;
      p_nd->~internal_node();
      s_internal_node_allocator.deallocate(p_nd, 1);

      if (p_parent == m_p_head)
	return;

      _GLIBCXX_DEBUG_ASSERT(p_parent->m_type == pat_trie_internal_node_type);
      p_nd = static_cast<internal_node_pointer>(p_parent);
    }

  while (true)
    {
      _GLIBCXX_DEBUG_ASSERT(std::distance(p_nd->begin(), p_nd->end()) > 1);
      p_nd->update_prefixes(this);
      apply_update(p_nd, (node_update* )this);
      _GLIBCXX_DEBUG_ONLY(p_nd->assert_valid(this);)
      if (p_nd->m_p_parent->m_type == pat_trie_head_node_type)
        return;

      _GLIBCXX_DEBUG_ASSERT(p_nd->m_p_parent->m_type ==
		       pat_trie_internal_node_type);

      p_nd = static_cast<internal_node_pointer>(p_nd->m_p_parent);
    }
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
actual_erase_leaf(leaf_pointer p_l)
{
  _GLIBCXX_DEBUG_ASSERT(m_size > 0);
  --m_size;
  _GLIBCXX_DEBUG_ONLY(erase_existing(PB_DS_V2F(p_l->value())));
  p_l->~leaf();
  s_leaf_allocator.deallocate(p_l, 1);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
clear()
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  if (empty())
    return;

  clear_imp(m_p_head->m_p_parent);
  m_size = 0;
  initialize();
  _GLIBCXX_DEBUG_ONLY(map_debug_base::clear();)
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
clear_imp(node_pointer p_nd)
{
  if (p_nd->m_type == pat_trie_internal_node_type)
    {
      _GLIBCXX_DEBUG_ASSERT(p_nd->m_type == pat_trie_internal_node_type);
      for (typename internal_node::iterator it =
	     static_cast<internal_node_pointer>(p_nd)->begin();
	   it != static_cast<internal_node_pointer>(p_nd)->end();
	   ++it)
        {
	  node_pointer p_child =* it;
	  clear_imp(p_child);
        }
      s_internal_node_allocator.deallocate(static_cast<internal_node_pointer>(p_nd), 1);
      return;
    }

  _GLIBCXX_DEBUG_ASSERT(p_nd->m_type == pat_trie_leaf_node_type);
  static_cast<leaf_pointer>(p_nd)->~leaf();
  s_leaf_allocator.deallocate(static_cast<leaf_pointer>(p_nd), 1);
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_iterator
PB_DS_CLASS_C_DEC::
erase(const_iterator it)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid());

  if (it == end())
    return it;

  const_iterator ret_it = it;
  ++ret_it;
  _GLIBCXX_DEBUG_ASSERT(it.m_p_nd->m_type == pat_trie_leaf_node_type);
  erase_leaf(static_cast<leaf_pointer>(it.m_p_nd));
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  return ret_it;
}

#ifdef PB_DS_DATA_TRUE_INDICATOR
PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::iterator
PB_DS_CLASS_C_DEC::
erase(iterator it)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid());

  if (it == end())
    return it;
  iterator ret_it = it;
  ++ret_it;
  _GLIBCXX_DEBUG_ASSERT(it.m_p_nd->m_type == pat_trie_leaf_node_type);
  erase_leaf(static_cast<leaf_pointer>(it.m_p_nd));
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  return ret_it;
}
#endif // #ifdef PB_DS_DATA_TRUE_INDICATOR

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_reverse_iterator
PB_DS_CLASS_C_DEC::
erase(const_reverse_iterator it)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid());

  if (it.m_p_nd == m_p_head)
    return it;
  const_reverse_iterator ret_it = it;
  ++ret_it;

  _GLIBCXX_DEBUG_ASSERT(it.m_p_nd->m_type == pat_trie_leaf_node_type);
  erase_leaf(static_cast<leaf_pointer>(it.m_p_nd));
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  return ret_it;
}

#ifdef PB_DS_DATA_TRUE_INDICATOR
PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::reverse_iterator
PB_DS_CLASS_C_DEC::
erase(reverse_iterator it)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid());

  if (it.m_p_nd == m_p_head)
    return it;
  reverse_iterator ret_it = it;
  ++ret_it;

  _GLIBCXX_DEBUG_ASSERT(it.m_p_nd->m_type == pat_trie_leaf_node_type);
  erase_leaf(static_cast<leaf_pointer>(it.m_p_nd));
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  return ret_it;
}
#endif // #ifdef PB_DS_DATA_TRUE_INDICATOR

PB_DS_CLASS_T_DEC
template<typename Pred>
inline typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
erase_if(Pred pred)
{
  size_type num_ersd = 0;
  _GLIBCXX_DEBUG_ONLY(assert_valid();)

  iterator it = begin();
  while (it != end())
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
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
erase_leaf(leaf_pointer p_l)
{
  update_min_max_for_erased_leaf(p_l);
  if (p_l->m_p_parent->m_type == pat_trie_head_node_type)
    {
      _GLIBCXX_DEBUG_ASSERT(size() == 1);
      clear();
      return;
    }

  _GLIBCXX_DEBUG_ASSERT(size() > 1);
  _GLIBCXX_DEBUG_ASSERT(p_l->m_p_parent->m_type ==
		   pat_trie_internal_node_type);

  internal_node_pointer p_parent =
    static_cast<internal_node_pointer>(p_l->m_p_parent);

  p_parent->remove_child(p_l);
  erase_fixup(p_parent);
  actual_erase_leaf(p_l);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
update_min_max_for_erased_leaf(leaf_pointer p_l)
{
  if (m_size == 1)
    {
      m_p_head->m_p_min = m_p_head;
      m_p_head->m_p_max = m_p_head;
      return;
    }

  if (p_l == static_cast<const_leaf_pointer>(m_p_head->m_p_min))
    {
      iterator it(p_l);
      ++it;
      m_p_head->m_p_min = it.m_p_nd;
      return;
    }

  if (p_l == static_cast<const_leaf_pointer>(m_p_head->m_p_max))
    {
      iterator it(p_l);
      --it;
      m_p_head->m_p_max = it.m_p_nd;
    }
}
