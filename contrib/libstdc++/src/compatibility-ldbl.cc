// Compatibility symbols for -mlong-double-64 compatibility -*- C++ -*-

// Copyright (C) 2006
// Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#include <locale>

#ifdef _GLIBCXX_LONG_DOUBLE_COMPAT

#ifdef __LONG_DOUBLE_128__
#error "compatibility-ldbl.cc must be compiled with -mlong-double-64"
#endif

namespace std
{
#define C char
  template class num_get<C, istreambuf_iterator<C> >;
  template class num_put<C, ostreambuf_iterator<C> >;
  template class money_get<C, istreambuf_iterator<C> >;
  template class money_put<C, ostreambuf_iterator<C> >;
  template const num_put<C>& use_facet<num_put<C> >(const locale&);
  template const num_get<C>& use_facet<num_get<C> >(const locale&);
  template const money_put<C>& use_facet<money_put<C> >(const locale&);
  template const money_get<C>& use_facet<money_get<C> >(const locale&);
  template bool has_facet<num_put<C> >(const locale&);
  template bool has_facet<num_get<C> >(const locale&);
  template bool has_facet<money_put<C> >(const locale&);
  template bool has_facet<money_get<C> >(const locale&);
#undef C
#ifdef _GLIBCXX_USE_WCHAR_T
#define C wchar_t
  template class num_get<C, istreambuf_iterator<C> >;
  template class num_put<C, ostreambuf_iterator<C> >;
  template class money_get<C, istreambuf_iterator<C> >;
  template class money_put<C, ostreambuf_iterator<C> >;
  template const num_put<C>& use_facet<num_put<C> >(const locale&);
  template const num_get<C>& use_facet<num_get<C> >(const locale&);
  template const money_put<C>& use_facet<money_put<C> >(const locale&);
  template const money_get<C>& use_facet<money_get<C> >(const locale&);
  template bool has_facet<num_put<C> >(const locale&);
  template bool has_facet<num_get<C> >(const locale&);
  template bool has_facet<money_put<C> >(const locale&);
  template bool has_facet<money_get<C> >(const locale&);
#undef C
#endif
}

#endif
