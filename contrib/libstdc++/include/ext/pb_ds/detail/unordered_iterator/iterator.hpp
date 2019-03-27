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
 * @file iterator.hpp
 * Contains an iterator_ class used for ranging over the elements of the
 *    table.
 */

// Range-type iterator.
class iterator_ : 
  public const_iterator_

{

public:

  // Category.
  typedef std::forward_iterator_tag iterator_category;

  // Difference type.
  typedef typename Allocator::difference_type difference_type;

  // Iterator's value type.
  typedef value_type_ value_type;

  // Iterator's pointer type.
  typedef pointer_ pointer;

  // Iterator's const pointer type.
  typedef const_pointer_ const_pointer;

  // Iterator's reference type.
  typedef reference_ reference;

  // Iterator's const reference type.
  typedef const_reference_ const_reference;

public:

  // Default constructor.
  inline
  iterator_()

    : const_iterator_(NULL, PB_DS_GEN_POS(), NULL)
  { }

  // Conversion to a point-type iterator.
  inline
  operator point_iterator_()
  {
    return (point_iterator_(
			    const_cast<pointer>(const_iterator_::m_p_value)));
  }

  // Conversion to a point-type iterator.
  inline
  operator const point_iterator_() const
  {
    return (point_iterator_(
			    const_cast<pointer>(const_iterator_::m_p_value)));
  }

  // Access.
  inline pointer
  operator->() const
  {
    _GLIBCXX_DEBUG_ASSERT(base_type::m_p_value != NULL);

    return (const_cast<pointer>(base_type::m_p_value));
  }

  // Access.
  inline reference
  operator*() const
  {
    _GLIBCXX_DEBUG_ASSERT(base_type::m_p_value != NULL);

    return (const_cast<reference>(*base_type::m_p_value));
  }

  // Increments.
  inline iterator_& 
  operator++()
  {
    base_type::m_p_tbl->inc_it_state(base_type::m_p_value, base_type::m_pos);

    return (*this);
  }

  // Increments.
  inline iterator_
  operator++(int)
  {
    iterator_ ret =* this;

    base_type::m_p_tbl->inc_it_state(base_type::m_p_value, base_type::m_pos);

    return (ret);
  }

protected:
  typedef const_iterator_ base_type;

protected:

  /**
   *  Constructor used by the table to initiate the generalized
   *      pointer and position (e.g., this is called from within a find()
   *      of a table.
   * */
  inline
  iterator_(pointer p_value, PB_DS_GEN_POS pos, PB_DS_CLASS_C_DEC* p_tbl) : const_iterator_(p_value, pos, p_tbl)
  { }

  friend class PB_DS_CLASS_C_DEC;
};

