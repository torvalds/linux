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
 * @file hash_load_check_resize_trigger_size_base.hpp
 * Contains an base holding size for some resize policies.
 */

#ifndef PB_DS_HASH_LOAD_CHECK_RESIZE_TRIGGER_SIZE_BASE_HPP
#define PB_DS_HASH_LOAD_CHECK_RESIZE_TRIGGER_SIZE_BASE_HPP

namespace pb_ds
{
  namespace detail
  {
    // Primary template.
    template<typename Size_Type, bool Hold_Size>
    class hash_load_check_resize_trigger_size_base;

    // Specializations.
    template<typename Size_Type>
    class hash_load_check_resize_trigger_size_base<Size_Type, true>
    {
    protected:
      typedef Size_Type size_type;

      hash_load_check_resize_trigger_size_base(): m_size(0)
      { }

      inline void
      swap(hash_load_check_resize_trigger_size_base& other)
      { std::swap(m_size, other.m_size); }

      inline void
      set_size(size_type size)
      { m_size = size; }

      inline size_type
      get_size() const
      { return m_size; }

    private:
      size_type m_size;
    };

    template<typename Size_Type>
    class hash_load_check_resize_trigger_size_base<Size_Type, false>
    {
    protected:
      typedef Size_Type size_type;

    protected:
      inline void
      swap(hash_load_check_resize_trigger_size_base& other) { }

      inline void
      set_size(size_type size) { }
    };
  } // namespace detail
} // namespace pb_ds

#endif
