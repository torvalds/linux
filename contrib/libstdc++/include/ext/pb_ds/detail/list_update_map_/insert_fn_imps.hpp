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
 * Contains implementations of lu_map_.
 */

PB_DS_CLASS_T_DEC
inline std::pair<
  typename PB_DS_CLASS_C_DEC::point_iterator,
  bool>
PB_DS_CLASS_C_DEC::
insert(const_reference r_val)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  entry_pointer p_l = find_imp(PB_DS_V2F(r_val));

  if (p_l != NULL)
    {
      _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(PB_DS_V2F(r_val));)
      return std::make_pair(point_iterator(&p_l->m_value), false);
    }

  _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(PB_DS_V2F(r_val));)

  p_l = allocate_new_entry(r_val, traits_base::m_no_throw_copies_indicator);
  p_l->m_p_next = m_p_l;
  m_p_l = p_l;
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  return std::make_pair(point_iterator(&p_l->m_value), true);
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::entry_pointer
PB_DS_CLASS_C_DEC::
allocate_new_entry(const_reference r_val, false_type)
{
  entry_pointer p_l = s_entry_allocator.allocate(1);
  cond_dealtor_t cond(p_l);
  new (const_cast<void* >(static_cast<const void* >(&p_l->m_value)))
    value_type(r_val);

  cond.set_no_action();
  _GLIBCXX_DEBUG_ONLY(map_debug_base::insert_new(PB_DS_V2F(r_val));)
  init_entry_metadata(p_l, s_metadata_type_indicator);
  return p_l;
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::entry_pointer
PB_DS_CLASS_C_DEC::
allocate_new_entry(const_reference    r_val, true_type)
{
  entry_pointer p_l = s_entry_allocator.allocate(1);
  new (&p_l->m_value) value_type(r_val);
  _GLIBCXX_DEBUG_ONLY(map_debug_base::insert_new(PB_DS_V2F(r_val));)
  init_entry_metadata(p_l, s_metadata_type_indicator);
  return p_l;
}

PB_DS_CLASS_T_DEC
template<typename Metadata>
inline void
PB_DS_CLASS_C_DEC::
init_entry_metadata(entry_pointer p_l, type_to_type<Metadata>)
{ new (&p_l->m_update_metadata) Metadata(s_update_policy()); }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
init_entry_metadata(entry_pointer, type_to_type<null_lu_metadata>)
{ }

