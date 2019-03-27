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
 * @file cond_key_dtor_entry_dealtor.hpp
 * Contains a conditional key destructor, used for exception handling.
 */

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_CLASS_T_DEC template<typename HT_Map>
#define PB_DS_CLASS_C_DEC PB_DS_CKDED_CLASS_NAME<HT_Map>

    /**
     * A conditional key destructor, used for exception handling.
     **/
    template<typename HT_Map>
    class PB_DS_CKDED_CLASS_NAME
    {
    public:
      typedef typename HT_Map::entry entry;
      typedef typename HT_Map::entry_allocator entry_allocator;
      typedef typename HT_Map::key_type key_type;

      inline
      PB_DS_CKDED_CLASS_NAME(entry_allocator* p_a, entry* p_e);

      inline
      ~PB_DS_CKDED_CLASS_NAME();

      inline void
      set_key_destruct();

      inline void
      set_no_action_destructor();

    protected:
      entry_allocator* const m_p_a;
      entry* const m_p_e;

      bool m_key_destruct;
      bool m_no_action_destructor;
    };

    PB_DS_CLASS_T_DEC
    inline
    PB_DS_CLASS_C_DEC::
    PB_DS_CKDED_CLASS_NAME(entry_allocator* p_a, entry* p_e) 
    : m_p_a(p_a), m_p_e(p_e), m_key_destruct(false), 
      m_no_action_destructor(false)
    { }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    set_key_destruct()
    { m_key_destruct = true; }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    set_no_action_destructor()
    { m_no_action_destructor = true; }

    PB_DS_CLASS_T_DEC
    inline
    PB_DS_CLASS_C_DEC::
    ~PB_DS_CKDED_CLASS_NAME()
    {
      if (m_no_action_destructor)
	return;
      if (m_key_destruct)
	m_p_e->m_value.first.~key_type();
      m_p_a->deallocate(m_p_e, 1);
    }

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

  } // namespace detail
} // namespace pb_ds

