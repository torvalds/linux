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
 * @file constructors_destructor_fn_imps.hpp
 * Contains an implementation class for ov_tree_.
 */

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::value_allocator
PB_DS_CLASS_C_DEC::s_value_alloc;

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::metadata_allocator
PB_DS_CLASS_C_DEC::s_metadata_alloc;

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_OV_TREE_CLASS_NAME() :
  m_a_values(NULL),
  m_a_metadata(NULL),
  m_end_it(NULL),
  m_size(0)
{ _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();) }

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_OV_TREE_CLASS_NAME(const Cmp_Fn& r_cmp_fn) :
  cmp_fn_base(r_cmp_fn),
  m_a_values(NULL),
  m_a_metadata(NULL),
  m_end_it(NULL),
  m_size(0)
{ _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();) }

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_OV_TREE_CLASS_NAME(const Cmp_Fn& r_cmp_fn, const node_update& r_node_update) :
  cmp_fn_base(r_cmp_fn),
  node_update(r_node_update),
  m_a_values(NULL),
  m_a_metadata(NULL),
  m_end_it(NULL),
  m_size(0)
{ _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();) }

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_OV_TREE_CLASS_NAME(const PB_DS_CLASS_C_DEC& other) :
#ifdef _GLIBCXX_DEBUG
  map_debug_base(other),
#endif 
#ifdef PB_DS_TREE_TRACE
  PB_DS_TREE_TRACE_BASE_C_DEC(other),
#endif 
  cmp_fn_base(other),
  node_update(other),
  m_a_values(NULL),
  m_a_metadata(NULL),
  m_end_it(NULL),
  m_size(0)
{
  copy_from_ordered_range(other.begin(), other.end());
  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
}

PB_DS_CLASS_T_DEC
template<typename It>
inline void
PB_DS_CLASS_C_DEC::
copy_from_range(It first_it, It last_it)
{
#ifdef PB_DS_DATA_TRUE_INDICATOR
  typedef
    std::map<
    key_type,
    mapped_type,
    Cmp_Fn,
    typename Allocator::template rebind<
    value_type>::other>
    map_type;
#else 
  typedef
    std::set<
    key_type,
    Cmp_Fn,
    typename Allocator::template rebind<
    Key>::other>
    map_type;
#endif 

  map_type m(first_it, last_it);
  copy_from_ordered_range(m.begin(), m.end());
}

PB_DS_CLASS_T_DEC
template<typename It>
void
PB_DS_CLASS_C_DEC::
copy_from_ordered_range(It first_it, It last_it)
{
  const size_type len = std::distance(first_it, last_it);
  if (len == 0)
    return;

  value_vector a_values = s_value_alloc.allocate(len);
  iterator target_it = a_values;
  It source_it = first_it;
  It source_end_it = last_it;

  cond_dtor<size_type> cd(a_values, target_it, len);
  while (source_it != source_end_it)
    {
      new (const_cast<void* >(static_cast<const void* >(target_it)))
	value_type(*source_it++);

      ++target_it;
    }

  reallocate_metadata((node_update* )this, len);
  cd.set_no_action();
  m_a_values = a_values;
  m_size = len;
  m_end_it = m_a_values + m_size;
  update(PB_DS_node_begin_imp(), (node_update* )this);

#ifdef _GLIBCXX_DEBUG
  const_iterator dbg_it = m_a_values;
  while (dbg_it != m_end_it)
    {
      map_debug_base::insert_new(PB_DS_V2F(*dbg_it));
      dbg_it++;
    }
  PB_DS_CLASS_C_DEC::assert_valid();
#endif 
}

PB_DS_CLASS_T_DEC
template<typename It>
void
PB_DS_CLASS_C_DEC::
copy_from_ordered_range(It first_it, It last_it, It other_first_it, 
			It other_last_it)
{
  clear();
  const size_type len = std::distance(first_it, last_it) 
    		         + std::distance(other_first_it, other_last_it);

  value_vector a_values = s_value_alloc.allocate(len);

  iterator target_it = a_values;
  It source_it = first_it;
  It source_end_it = last_it;

  cond_dtor<size_type> cd(a_values, target_it, len);
  while (source_it != source_end_it)
    {
      new (const_cast<void* >(static_cast<const void* >(target_it)))
	value_type(*source_it++);
      ++target_it;
    }

  source_it = other_first_it;
  source_end_it = other_last_it;

  while (source_it != source_end_it)
    {
      new (const_cast<void* >(static_cast<const void* >(target_it)))
	value_type(*source_it++);
      ++target_it;
    }

  reallocate_metadata((node_update* )this, len);
  cd.set_no_action();
  m_a_values = a_values;
  m_size = len;
  m_end_it = m_a_values + m_size;
  update(PB_DS_node_begin_imp(), (node_update* )this);

#ifdef _GLIBCXX_DEBUG
  const_iterator dbg_it = m_a_values;
  while (dbg_it != m_end_it)
    {
      map_debug_base::insert_new(PB_DS_V2F(*dbg_it));
      dbg_it++;
    }
  PB_DS_CLASS_C_DEC::assert_valid();
#endif 
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
swap(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  value_swap(other);
  std::swap((Cmp_Fn& )(*this), (Cmp_Fn& )other);
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
value_swap(PB_DS_CLASS_C_DEC& other)
{
  std::swap(m_a_values, other.m_a_values);
  std::swap(m_a_metadata, other.m_a_metadata);
  std::swap(m_size, other.m_size);
  std::swap(m_end_it, other.m_end_it);
  _GLIBCXX_DEBUG_ONLY(map_debug_base::swap(other);)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
~PB_DS_OV_TREE_CLASS_NAME()
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  cond_dtor<size_type> cd(m_a_values, m_end_it, m_size);
  reallocate_metadata((node_update* )this, 0);
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
update(node_iterator /*it*/, null_node_update_pointer)
{ }

PB_DS_CLASS_T_DEC
template<typename Node_Update>
void
PB_DS_CLASS_C_DEC::
update(node_iterator nd_it, Node_Update* p_update)
{
  const_node_iterator end_it = PB_DS_node_end_imp();
  if (nd_it == end_it)
    return;
  update(nd_it.get_l_child(), p_update);
  update(nd_it.get_r_child(), p_update);
  node_update::operator()(nd_it, end_it);
}
