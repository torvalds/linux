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
 * @file sample_update_policy.hpp
 * Contains a sample policy for list update containers.
 */

#ifndef PB_DS_SAMPLE_UPDATE_POLICY_HPP
#define PB_DS_SAMPLE_UPDATE_POLICY_HPP

// A sample list-update policy.
struct sample_update_policy
{
  // Default constructor.
  sample_update_policy();

  // Copy constructor.
  sample_update_policy(const sample_update_policy&);

  // Swaps content.
  inline void
  swap(sample_update_policy& other);

protected:
  // Metadata on which this functor operates.
  typedef some_metadata_type metadata_type;

  // Creates a metadata object.
  metadata_type
  operator()() const;

  // Decides whether a metadata object should be moved to the front of
  // the list. A list-update based containers object will call this
  // method to decide whether to move a node to the front of the
  // list. The method shoule return true if the node should be moved
  // to the front of the list.
  bool
  operator()(metadata_reference) const;
};

#endif 
