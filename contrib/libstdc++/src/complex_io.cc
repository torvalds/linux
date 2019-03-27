// The template and inlines for the -*- C++ -*- complex number classes.

// Copyright (C) 2000, 2001, 2005, 2006 Free Software Foundation, Inc.
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

#include <complex>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template
    basic_istream<char, char_traits<char> >&
    operator>>(basic_istream<char, char_traits<char> >&, complex<float>&);

  template
    basic_ostream<char, char_traits<char> >&
    operator<<(basic_ostream<char, char_traits<char> >&, 
	       const complex<float>&);

  template
    basic_istream<char, char_traits<char> >&
    operator>>(basic_istream<char, char_traits<char> >&, complex<double>&);

  template
    basic_ostream<char, char_traits<char> >&
    operator<<(basic_ostream<char, char_traits<char> >&, 
	       const complex<double>&);

  template
    basic_istream<char, char_traits<char> >&
    operator>>(basic_istream<char, char_traits<char> >&, 
	       complex<long double>&);

  template
    basic_ostream<char, char_traits<char> >&
    operator<<(basic_ostream<char, char_traits<char> >&,
               const complex<long double>&);

#ifdef _GLIBCXX_USE_WCHAR_T
  template
    basic_istream<wchar_t, char_traits<wchar_t> >&
    operator>>(basic_istream<wchar_t, char_traits<wchar_t> >&,
               complex<float>&);

  template
    basic_ostream<wchar_t, char_traits<wchar_t> >&
    operator<<(basic_ostream<wchar_t, char_traits<wchar_t> >&,
               const complex<float>&);

  template
    basic_istream<wchar_t, char_traits<wchar_t> >&
    operator>>(basic_istream<wchar_t, char_traits<wchar_t> >&,
               complex<double>&);

  template
    basic_ostream<wchar_t, char_traits<wchar_t> >&
    operator<<(basic_ostream<wchar_t, char_traits<wchar_t> >&,
               const complex<double>&);

  template
    basic_istream<wchar_t, char_traits<wchar_t> >&
    operator>>(basic_istream<wchar_t, char_traits<wchar_t> >&,
               complex<long double>&);

  template
    basic_ostream<wchar_t, char_traits<wchar_t> >&
    operator<<(basic_ostream<wchar_t, char_traits<wchar_t> >&,
               const complex<long double>&);
#endif //_GLIBCXX_USE_WCHAR_T

_GLIBCXX_END_NAMESPACE

// XXX GLIBCXX_ABI Deprecated
#ifdef _GLIBCXX_LONG_DOUBLE_COMPAT

#define _GLIBCXX_LDBL_COMPAT(dbl, ldbl) \
  extern "C" void ldbl (void) __attribute__ ((alias (#dbl), weak))

_GLIBCXX_LDBL_COMPAT (_ZStlsIdcSt11char_traitsIcEERSt13basic_ostreamIT0_T1_ES6_RKSt7complexIT_E,
		      _ZStlsIecSt11char_traitsIcEERSt13basic_ostreamIT0_T1_ES6_RKSt7complexIT_E);
_GLIBCXX_LDBL_COMPAT (_ZStlsIdwSt11char_traitsIwEERSt13basic_ostreamIT0_T1_ES6_RKSt7complexIT_E,
		      _ZStlsIewSt11char_traitsIwEERSt13basic_ostreamIT0_T1_ES6_RKSt7complexIT_E);
_GLIBCXX_LDBL_COMPAT (_ZStrsIdcSt11char_traitsIcEERSt13basic_istreamIT0_T1_ES6_RSt7complexIT_E,
		      _ZStrsIecSt11char_traitsIcEERSt13basic_istreamIT0_T1_ES6_RSt7complexIT_E);
_GLIBCXX_LDBL_COMPAT (_ZStrsIdwSt11char_traitsIwEERSt13basic_istreamIT0_T1_ES6_RSt7complexIT_E,
		      _ZStrsIewSt11char_traitsIwEERSt13basic_istreamIT0_T1_ES6_RSt7complexIT_E);

#endif // _GLIBCXX_LONG_DOUBLE_COMPAT
