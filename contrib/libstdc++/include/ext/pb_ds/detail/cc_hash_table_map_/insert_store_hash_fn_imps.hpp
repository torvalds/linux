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
 * @file insert_store_hash_fn_imps.hpp
 * Contains implementations of cc_ht_map_'s insert related functions,
 * when the hash value is stored.
 */

PB_DS_CLASS_T_DEC
inline std::pair<typename PB_DS_CLASS_C_DEC::point_iterator, bool>
PB_DS_CLASS_C_DEC::
insert_imp(const_reference r_val, true_type)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  const_key_reference key = PB_DS_V2F(r_val);
  comp_hash pos_hash_pair = ranged_hash_fn_base::operator()(key);
  entry_pointer p_e = m_entries[pos_hash_pair.first];
  resize_base::notify_insert_search_start();

  while (p_e != NULL && !hash_eq_fn_base::operator()(PB_DS_V2F(p_e->m_value),
						     p_e->m_hash,
						    key, pos_hash_pair.second))
    {
      resize_base::notify_insert_search_collision();
      p_e = p_e->m_p_next;
    }

  resize_base::notify_insert_search_end();
  if (p_e != NULL)
    {
      _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(key);)
      return std::make_pair(&p_e->m_value, false);
    }

  _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(key);)
  return std::make_pair(insert_new_imp(r_val, pos_hash_pair), true);
}

