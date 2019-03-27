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
 * @file info_fn_imps.hpp
 * Contains implementations of cc_ht_map_'s entire container info related
 * functions.
 */

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
size() const
{ return m_num_used_e; }

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
max_size() const
{ return m_entry_allocator.max_size(); }

PB_DS_CLASS_T_DEC
inline bool
PB_DS_CLASS_C_DEC::
empty() const
{ return (size() == 0); }

PB_DS_CLASS_T_DEC
template<typename Other_HT_Map_Type>
bool
PB_DS_CLASS_C_DEC::
operator==(const Other_HT_Map_Type& other) const
{ return cmp_with_other(other); }

PB_DS_CLASS_T_DEC
template<typename Other_Map_Type>
bool
PB_DS_CLASS_C_DEC::
cmp_with_other(const Other_Map_Type& other) const
{
  if (size() != other.size())
    return false;

  for (typename Other_Map_Type::const_iterator it = other.begin();
       it != other.end(); ++it)
    {
      const_key_reference r_key =(const_key_reference)PB_DS_V2F(*it);
      const_mapped_pointer p_mapped_value =
	const_cast<PB_DS_CLASS_C_DEC& >(*this).
	find_key_pointer(r_key, traits_base::m_store_hash_indicator);

      if (p_mapped_value == NULL)
	return false;

#ifdef PB_DS_DATA_TRUE_INDICATOR
      if (p_mapped_value->second != it->second)
	return false;
#endif 
    }
  return true;
}

PB_DS_CLASS_T_DEC
template<typename Other_HT_Map_Type>
bool
PB_DS_CLASS_C_DEC::
operator!=(const Other_HT_Map_Type& other) const
{ return !operator==(other); }
