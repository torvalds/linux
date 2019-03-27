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
 * Contains implementations of lu_map_.
 */

PB_DS_CLASS_T_DEC
inline bool
PB_DS_CLASS_C_DEC::
erase(const_key_reference r_key)
{
  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
  if (m_p_l == NULL)
    return false;

  if (s_eq_fn(r_key, PB_DS_V2F(m_p_l->m_value)))
    {
      entry_pointer p_next = m_p_l->m_p_next;
      actual_erase_entry(m_p_l);
      m_p_l = p_next;
      return true;
    }

  entry_pointer p_l = m_p_l;
  while (p_l->m_p_next != NULL)
    if (s_eq_fn(r_key, PB_DS_V2F(p_l->m_p_next->m_value)))
      {
	erase_next(p_l);
	return true;
      }
    else
      p_l = p_l->m_p_next;
  return false;
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
clear()
{
  deallocate_all();
}

PB_DS_CLASS_T_DEC
template<typename Pred>
inline typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
erase_if(Pred pred)
{
  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
  size_type num_ersd = 0;
  while (m_p_l != NULL && pred(m_p_l->m_value))
    {
      entry_pointer p_next = m_p_l->m_p_next;
      ++num_ersd;
      actual_erase_entry(m_p_l);
      m_p_l = p_next;
    }

  if (m_p_l == NULL)
    return num_ersd;

  entry_pointer p_l = m_p_l;
  while (p_l->m_p_next != NULL)
    {
      if (pred(p_l->m_p_next->m_value))
        {
	  ++num_ersd;
	  erase_next(p_l);
        }
      else
	p_l = p_l->m_p_next;
    }

  _GLIBCXX_DEBUG_ONLY(PB_DS_CLASS_C_DEC::assert_valid();)
  return num_ersd;
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
erase_next(entry_pointer p_l)
{
  _GLIBCXX_DEBUG_ASSERT(p_l != NULL);
  _GLIBCXX_DEBUG_ASSERT(p_l != m_p_l);
  _GLIBCXX_DEBUG_ASSERT(p_l->m_p_next != NULL);
  entry_pointer p_next_l = p_l->m_p_next->m_p_next;
  actual_erase_entry(p_l->m_p_next);
  p_l->m_p_next = p_next_l;
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
actual_erase_entry(entry_pointer p_l)
{
  _GLIBCXX_DEBUG_ONLY(map_debug_base::erase_existing(PB_DS_V2F(p_l->m_value));)
  p_l->~entry();
  s_entry_allocator.deallocate(p_l, 1);
}

