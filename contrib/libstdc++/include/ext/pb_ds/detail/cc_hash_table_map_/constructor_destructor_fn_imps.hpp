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
 * @file constructor_destructor_fn_imps.hpp
 * Contains implementations of cc_ht_map_'s constructors, destructor,
 *    and related functions.
 */

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::entry_allocator
PB_DS_CLASS_C_DEC::s_entry_allocator;

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::entry_pointer_allocator
PB_DS_CLASS_C_DEC::s_entry_pointer_allocator;

PB_DS_CLASS_T_DEC
template<typename It>
void
PB_DS_CLASS_C_DEC::
copy_from_range(It first_it, It last_it)
{
  while (first_it != last_it)
    insert(*(first_it++));
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME() :
  ranged_hash_fn_base(resize_base::get_nearest_larger_size(1)),
  m_num_e(resize_base::get_nearest_larger_size(1)), m_num_used_e(0),
  m_entries(s_entry_pointer_allocator.allocate(m_num_e))
{
  initialize();
  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME(const Hash_Fn& r_hash_fn) :
  ranged_hash_fn_base(resize_base::get_nearest_larger_size(1), r_hash_fn),
  m_num_e(resize_base::get_nearest_larger_size(1)), m_num_used_e(0),
  m_entries(s_entry_pointer_allocator.allocate(m_num_e))
{
  initialize();
  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME(const Hash_Fn& r_hash_fn, const Eq_Fn& r_eq_fn) :
  PB_DS_HASH_EQ_FN_C_DEC(r_eq_fn),
  ranged_hash_fn_base(resize_base::get_nearest_larger_size(1), r_hash_fn),
  m_num_e(resize_base::get_nearest_larger_size(1)), m_num_used_e(0),
  m_entries(s_entry_pointer_allocator.allocate(m_num_e))
{
  std::fill(m_entries, m_entries + m_num_e, (entry_pointer)NULL);
  Resize_Policy::notify_cleared();
  ranged_hash_fn_base::notify_resized(m_num_e);
  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME(const Hash_Fn& r_hash_fn, const Eq_Fn& r_eq_fn, const Comb_Hash_Fn& r_comb_hash_fn) :
  PB_DS_HASH_EQ_FN_C_DEC(r_eq_fn),
  ranged_hash_fn_base(resize_base::get_nearest_larger_size(1),
		      r_hash_fn, r_comb_hash_fn),
  m_num_e(resize_base::get_nearest_larger_size(1)), m_num_used_e(0),
  m_entries(s_entry_pointer_allocator.allocate(m_num_e))
{
  initialize();
  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME(const Hash_Fn& r_hash_fn, const Eq_Fn& r_eq_fn, const Comb_Hash_Fn& r_comb_hash_fn, const Resize_Policy& r_resize_policy) :
  PB_DS_HASH_EQ_FN_C_DEC(r_eq_fn),
  Resize_Policy(r_resize_policy),
  ranged_hash_fn_base(resize_base::get_nearest_larger_size(1),
		      r_hash_fn, r_comb_hash_fn),
  m_num_e(resize_base::get_nearest_larger_size(1)), m_num_used_e(0),
  m_entries(s_entry_pointer_allocator.allocate(m_num_e))
{
  initialize();
  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
PB_DS_CLASS_NAME(const PB_DS_CLASS_C_DEC& other) :
#ifdef _GLIBCXX_DEBUG
  map_debug_base(other),
#endif 
  PB_DS_HASH_EQ_FN_C_DEC(other),
  resize_base(other), ranged_hash_fn_base(other),
  m_num_e(resize_base::get_nearest_larger_size(1)), m_num_used_e(0),
  m_entries(m_entries = s_entry_pointer_allocator.allocate(m_num_e))
{
  initialize();
  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
    try
      {
        copy_from_range(other.begin(), other.end());
      }
    catch(...)
      {
        deallocate_all();
        __throw_exception_again;
      }
  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
~PB_DS_CLASS_NAME()
{ deallocate_all(); }

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
swap(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  _GLIBCXX_DEBUG_ONLY(other.assert_valid());

  std::swap(m_entries, other.m_entries);
  std::swap(m_num_e, other.m_num_e);
  std::swap(m_num_used_e, other.m_num_used_e);
  ranged_hash_fn_base::swap(other);
  hash_eq_fn_base::swap(other);
  resize_base::swap(other);

  _GLIBCXX_DEBUG_ONLY(map_debug_base::swap(other));
  _GLIBCXX_DEBUG_ONLY(assert_valid());
  _GLIBCXX_DEBUG_ONLY(other.assert_valid());
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
deallocate_all()
{
  clear();
  s_entry_pointer_allocator.deallocate(m_entries, m_num_e);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
initialize()
{
  std::fill(m_entries, m_entries + m_num_e, entry_pointer(NULL));
  Resize_Policy::notify_resized(m_num_e);
  Resize_Policy::notify_cleared();
  ranged_hash_fn_base::notify_resized(m_num_e);
}
