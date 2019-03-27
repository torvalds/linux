// Explicit instantiation file.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2005, 2006
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

//
// ISO C++ 14882:
//

#include <istream>
#include <iomanip>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template class basic_istream<char>;
  template istream& ws(istream&);
  template istream& operator>>(istream&, char&);
  template istream& operator>>(istream&, unsigned char&);
  template istream& operator>>(istream&, signed char&);
  template istream& operator>>(istream&, char*);
  template istream& operator>>(istream&, unsigned char*);
  template istream& operator>>(istream&, signed char*);

  template istream& operator>>(istream&, _Setfill<char>);
  template istream& operator>>(istream&, _Setiosflags);
  template istream& operator>>(istream&, _Resetiosflags);
  template istream& operator>>(istream&, _Setbase);
  template istream& operator>>(istream&, _Setprecision);
  template istream& operator>>(istream&, _Setw);

  template istream& istream::_M_extract(unsigned short&);
  template istream& istream::_M_extract(unsigned int&);  
  template istream& istream::_M_extract(long&);
  template istream& istream::_M_extract(unsigned long&);
  template istream& istream::_M_extract(bool&);
#ifdef _GLIBCXX_USE_LONG_LONG
  template istream& istream::_M_extract(long long&);
  template istream& istream::_M_extract(unsigned long long&);
#endif
  template istream& istream::_M_extract(float&);
  template istream& istream::_M_extract(double&);
  template istream& istream::_M_extract(long double&);
  template istream& istream::_M_extract(void*&);

#ifdef _GLIBCXX_USE_WCHAR_T
  template class basic_istream<wchar_t>;
  template wistream& ws(wistream&);
  template wistream& operator>>(wistream&, wchar_t&);
  template wistream& operator>>(wistream&, wchar_t*);

  template wistream& operator>>(wistream&, _Setfill<wchar_t>);
  template wistream& operator>>(wistream&, _Setiosflags);
  template wistream& operator>>(wistream&, _Resetiosflags);
  template wistream& operator>>(wistream&, _Setbase);
  template wistream& operator>>(wistream&, _Setprecision);
  template wistream& operator>>(wistream&, _Setw);

  template wistream& wistream::_M_extract(unsigned short&);
  template wistream& wistream::_M_extract(unsigned int&);  
  template wistream& wistream::_M_extract(long&);
  template wistream& wistream::_M_extract(unsigned long&);
  template wistream& wistream::_M_extract(bool&);
#ifdef _GLIBCXX_USE_LONG_LONG
  template wistream& wistream::_M_extract(long long&);
  template wistream& wistream::_M_extract(unsigned long long&);
#endif
  template wistream& wistream::_M_extract(float&);
  template wistream& wistream::_M_extract(double&);
  template wistream& wistream::_M_extract(long double&);
  template wistream& wistream::_M_extract(void*&);
#endif

_GLIBCXX_END_NAMESPACE

// XXX GLIBCXX_ABI Deprecated
#ifdef _GLIBCXX_LONG_DOUBLE_COMPAT

#define _GLIBCXX_LDBL_COMPAT(dbl, ldbl) \
  extern "C" void ldbl (void) __attribute__ ((alias (#dbl), weak))
_GLIBCXX_LDBL_COMPAT (_ZNSirsERd, _ZNSirsERe);
_GLIBCXX_LDBL_COMPAT (_ZNSt13basic_istreamIwSt11char_traitsIwEErsERd,
		      _ZNSt13basic_istreamIwSt11char_traitsIwEErsERe);
_GLIBCXX_LDBL_COMPAT (_ZNSi10_M_extractIdEERSiRT_,
		      _ZNSi10_M_extractIeEERSiRT_);
_GLIBCXX_LDBL_COMPAT (_ZNSt13basic_istreamIwSt11char_traitsIwEE10_M_extractIdEERS2_RT_,
		      _ZNSt13basic_istreamIwSt11char_traitsIwEE10_M_extractIeEERS2_RT_);

#endif // _GLIBCXX_LONG_DOUBLE_COMPAT
