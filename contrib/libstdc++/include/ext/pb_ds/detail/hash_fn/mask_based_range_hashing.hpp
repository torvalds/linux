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
 * @file mask_based_range_hashing.hpp
 * Contains a range hashing policy base.
 */

#ifndef PB_DS_MASK_BASED_RANGE_HASHING_HPP
#define PB_DS_MASK_BASED_RANGE_HASHING_HPP

namespace pb_ds
{
  namespace detail
  {
#define PB_DS_CLASS_T_DEC template<typename Size_Type>
#define PB_DS_CLASS_C_DEC mask_based_range_hashing<Size_Type>

    template<typename Size_Type>
    class mask_based_range_hashing
    {
    protected:
      typedef Size_Type size_type;

      void
      swap(mask_based_range_hashing& other)
      { std::swap(m_mask, other.m_mask); }

      void
      notify_resized(size_type size);

      inline size_type
      range_hash(size_type hash) const
      { return size_type(hash & m_mask); }

    private:
      size_type 		m_mask;
      const static size_type 	s_num_bits_in_size_type;
      const static size_type 	s_highest_bit_1;
    };

    PB_DS_CLASS_T_DEC
    const typename PB_DS_CLASS_C_DEC::size_type
    PB_DS_CLASS_C_DEC::s_num_bits_in_size_type =
      sizeof(typename PB_DS_CLASS_C_DEC::size_type) << 3;

    PB_DS_CLASS_T_DEC
    const typename PB_DS_CLASS_C_DEC::size_type PB_DS_CLASS_C_DEC::s_highest_bit_1 = static_cast<typename PB_DS_CLASS_C_DEC::size_type>(1) << (s_num_bits_in_size_type - 1);

 
    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    notify_resized(size_type size)
    {
      size_type i = 0;
      while (size ^ s_highest_bit_1)
	{
	  size <<= 1;
	  ++i;
	}

      m_mask = 1;
      i += 2;
      while (i++ < s_num_bits_in_size_type)
        m_mask = (m_mask << 1) ^ 1;
    }

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

  } // namespace detail
} // namespace pb_ds

#endif
