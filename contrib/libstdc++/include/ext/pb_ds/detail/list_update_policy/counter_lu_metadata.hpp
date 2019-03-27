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
 * @file counter_lu_metadata.hpp
 * Contains implementation of a lu counter policy's metadata.
 */

namespace pb_ds
{
  namespace detail
  {
    template<typename Size_Type>
    class counter_lu_policy_base;

    // A list-update metadata type that moves elements to the front of
    // the list based on the counter algorithm.
    template<typename Size_Type = size_t>
    class counter_lu_metadata
    {
    public:
      typedef Size_Type size_type;

    private:
      counter_lu_metadata(size_type init_count) : m_count(init_count)
      { }

      friend class counter_lu_policy_base<size_type>;

      mutable size_type m_count;
    };

    template<typename Size_Type>
    class counter_lu_policy_base
    {
    protected:
      typedef Size_Type size_type;

      counter_lu_metadata<size_type>
      operator()(size_type max_size) const
      { return counter_lu_metadata<Size_Type>(rand() % max_size); }

      template<typename Metadata_Reference>
      bool
      operator()(Metadata_Reference r_data, size_type m_max_count) const
      {
	if (++r_data.m_count != m_max_count)
	  return false;
	r_data.m_count = 0;
	return true;
      }
    };
  } // namespace detail
} // namespace pb_ds
