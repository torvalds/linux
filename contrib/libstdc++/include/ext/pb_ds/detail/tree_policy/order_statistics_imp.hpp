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
 * @file order_statistics_imp.hpp
 * Contains forward declarations for order_statistics_key
 */

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::iterator
PB_DS_CLASS_C_DEC::
find_by_order(size_type order)
{
  node_iterator it = node_begin();

  node_iterator end_it = node_end();

  while (it != end_it)
    {
      node_iterator l_it = it.get_l_child();

      const size_type o = (l_it == end_it)?
	0 :
	l_it.get_metadata();

      if (order == o)
	return (*it);
      else if (order < o)
	it = l_it;
      else
        {
	  order -= o + 1;

	  it = it.get_r_child();
        }
    }

  return (PB_DS_BASE_C_DEC::end_iterator());
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_iterator
PB_DS_CLASS_C_DEC::
find_by_order(size_type order) const
{
  return (const_cast<PB_DS_CLASS_C_DEC* >(this)->find_by_order(order));
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
order_of_key(const_key_reference r_key) const
{
  const_node_iterator it = node_begin();

  const_node_iterator end_it = node_end();

  const cmp_fn& r_cmp_fn =
    const_cast<PB_DS_CLASS_C_DEC* >(this)->get_cmp_fn();

  size_type ord = 0;

  while (it != end_it)
    {
      const_node_iterator l_it = it.get_l_child();

      if (r_cmp_fn(r_key, extract_key(*(*it))))
	it = l_it;
      else if (r_cmp_fn(extract_key(*(*it)), r_key))
        {

	  ord += (l_it == end_it)?
	    1 :
	    1 + l_it.get_metadata();

	  it = it.get_r_child();
        }
      else
        {
	  ord += (l_it == end_it)?
	    0 :
	    l_it.get_metadata();

	  it = end_it;
        }
    }

  return (ord);
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
operator()(node_iterator node_it, const_node_iterator end_nd_it) const
{
  node_iterator l_child_it = node_it.get_l_child();
  const size_type l_rank =(l_child_it == end_nd_it)? 0 : l_child_it.get_metadata();

  node_iterator r_child_it = node_it.get_r_child();
  const size_type r_rank =(r_child_it == end_nd_it)? 0 : r_child_it.get_metadata();

  const_cast<metadata_reference>(node_it.get_metadata())=
    1 + l_rank + r_rank;
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
~tree_order_statistics_node_update()
{ }
