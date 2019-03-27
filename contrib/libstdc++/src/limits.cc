// Static data members of -*- C++ -*- numeric_limits classes

// Copyright (C) 1999, 2001, 2002, 2005, 2006 Free Software Foundation, Inc.
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

// Written by Gabriel Dos Reis <Gabriel.Dos-Reis@cmla.ens-cachan.fr>

//
// ISO C++ 14882:1998
// 18.2.1
//

#include <limits>

_GLIBCXX_BEGIN_NAMESPACE(std)

  const bool __numeric_limits_base::is_specialized;
  const int  __numeric_limits_base::digits;
  const int  __numeric_limits_base::digits10;
  const bool __numeric_limits_base::is_signed;
  const bool __numeric_limits_base::is_integer;
  const bool __numeric_limits_base::is_exact;
  const int  __numeric_limits_base::radix;
  const int  __numeric_limits_base::min_exponent;
  const int  __numeric_limits_base::min_exponent10;
  const int  __numeric_limits_base::max_exponent;
  const int  __numeric_limits_base::max_exponent10;
  const bool __numeric_limits_base::has_infinity;
  const bool __numeric_limits_base::has_quiet_NaN;
  const bool __numeric_limits_base::has_signaling_NaN;
  const float_denorm_style __numeric_limits_base::has_denorm;
  const bool __numeric_limits_base::has_denorm_loss;
  const bool __numeric_limits_base::is_iec559;
  const bool __numeric_limits_base::is_bounded;
  const bool __numeric_limits_base::is_modulo;
  const bool __numeric_limits_base::traps;
  const bool __numeric_limits_base::tinyness_before;
  const float_round_style __numeric_limits_base::round_style;

  // bool
  const bool numeric_limits<bool>::is_specialized;
  const int  numeric_limits<bool>::digits;
  const int  numeric_limits<bool>::digits10;
  const bool numeric_limits<bool>::is_signed;
  const bool numeric_limits<bool>::is_integer;
  const bool numeric_limits<bool>::is_exact;
  const int  numeric_limits<bool>::radix;
  const int  numeric_limits<bool>::min_exponent;
  const int  numeric_limits<bool>::min_exponent10;
  const int  numeric_limits<bool>::max_exponent;
  const int  numeric_limits<bool>::max_exponent10;
  const bool numeric_limits<bool>::has_infinity;
  const bool numeric_limits<bool>::has_quiet_NaN;
  const bool numeric_limits<bool>::has_signaling_NaN;
  const float_denorm_style numeric_limits<bool>::has_denorm;
  const bool numeric_limits<bool>::has_denorm_loss;
  const bool numeric_limits<bool>::is_iec559;
  const bool numeric_limits<bool>::is_bounded;
  const bool numeric_limits<bool>::is_modulo;
  const bool numeric_limits<bool>::traps;
  const bool numeric_limits<bool>::tinyness_before;
  const float_round_style numeric_limits<bool>::round_style;

  // char
  const bool numeric_limits<char>::is_specialized;
  const int  numeric_limits<char>::digits;
  const int  numeric_limits<char>::digits10;
  const bool numeric_limits<char>::is_signed;
  const bool numeric_limits<char>::is_integer;
  const bool numeric_limits<char>::is_exact;
  const int  numeric_limits<char>::radix;
  const int  numeric_limits<char>::min_exponent;
  const int  numeric_limits<char>::min_exponent10;
  const int  numeric_limits<char>::max_exponent;
  const int  numeric_limits<char>::max_exponent10;
  const bool numeric_limits<char>::has_infinity;
  const bool numeric_limits<char>::has_quiet_NaN;
  const bool numeric_limits<char>::has_signaling_NaN;
  const float_denorm_style numeric_limits<char>::has_denorm;
  const bool numeric_limits<char>::has_denorm_loss;
  const bool numeric_limits<char>::is_iec559;
  const bool numeric_limits<char>::is_bounded;
  const bool numeric_limits<char>::is_modulo;
  const bool numeric_limits<char>::traps;
  const bool numeric_limits<char>::tinyness_before;
  const float_round_style numeric_limits<char>::round_style;

  // signed char
  const bool numeric_limits<signed char>::is_specialized;
  const int  numeric_limits<signed char>::digits;
  const int  numeric_limits<signed char>::digits10;
  const bool numeric_limits<signed char>::is_signed;
  const bool numeric_limits<signed char>::is_integer;
  const bool numeric_limits<signed char>::is_exact;
  const int  numeric_limits<signed char>::radix;
  const int  numeric_limits<signed char>::min_exponent;
  const int  numeric_limits<signed char>::min_exponent10;
  const int  numeric_limits<signed char>::max_exponent;
  const int  numeric_limits<signed char>::max_exponent10;
  const bool numeric_limits<signed char>::has_infinity;
  const bool numeric_limits<signed char>::has_quiet_NaN;
  const bool numeric_limits<signed char>::has_signaling_NaN;
  const float_denorm_style numeric_limits<signed char>::has_denorm;
  const bool numeric_limits<signed char>::has_denorm_loss;
  const bool numeric_limits<signed char>::is_iec559;
  const bool numeric_limits<signed char>::is_bounded;
  const bool numeric_limits<signed char>::is_modulo;
  const bool numeric_limits<signed char>::traps;
  const bool numeric_limits<signed char>::tinyness_before;
  const float_round_style numeric_limits<signed char>::round_style;

  // unsigned char
  const bool numeric_limits<unsigned char>::is_specialized;
  const int  numeric_limits<unsigned char>::digits;
  const int  numeric_limits<unsigned char>::digits10;
  const bool numeric_limits<unsigned char>::is_signed;
  const bool numeric_limits<unsigned char>::is_integer;
  const bool numeric_limits<unsigned char>::is_exact;
  const int  numeric_limits<unsigned char>::radix;
  const int  numeric_limits<unsigned char>::min_exponent;
  const int  numeric_limits<unsigned char>::min_exponent10;
  const int  numeric_limits<unsigned char>::max_exponent;
  const int  numeric_limits<unsigned char>::max_exponent10;
  const bool numeric_limits<unsigned char>::has_infinity;
  const bool numeric_limits<unsigned char>::has_quiet_NaN;
  const bool numeric_limits<unsigned char>::has_signaling_NaN;
  const float_denorm_style numeric_limits<unsigned char>::has_denorm;
  const bool numeric_limits<unsigned char>::has_denorm_loss;
  const bool numeric_limits<unsigned char>::is_iec559;
  const bool numeric_limits<unsigned char>::is_bounded;
  const bool numeric_limits<unsigned char>::is_modulo;
  const bool numeric_limits<unsigned char>::traps;
  const bool numeric_limits<unsigned char>::tinyness_before;
  const float_round_style numeric_limits<unsigned char>::round_style;

  // wchar_t
  // This used to be problematic...
#ifdef _GLIBCXX_USE_WCHAR_T  
  const bool numeric_limits<wchar_t>::is_specialized;
  const int  numeric_limits<wchar_t>::digits;
  const int  numeric_limits<wchar_t>::digits10;
  const bool numeric_limits<wchar_t>::is_signed;
  const bool numeric_limits<wchar_t>::is_integer;
  const bool numeric_limits<wchar_t>::is_exact;
  const int  numeric_limits<wchar_t>::radix;
  const int  numeric_limits<wchar_t>::min_exponent;
  const int  numeric_limits<wchar_t>::min_exponent10;
  const int  numeric_limits<wchar_t>::max_exponent;
  const int  numeric_limits<wchar_t>::max_exponent10;
  const bool numeric_limits<wchar_t>::has_infinity;
  const bool numeric_limits<wchar_t>::has_quiet_NaN;
  const bool numeric_limits<wchar_t>::has_signaling_NaN;
  const float_denorm_style numeric_limits<wchar_t>::has_denorm;
  const bool numeric_limits<wchar_t>::has_denorm_loss;
  const bool numeric_limits<wchar_t>::is_iec559;
  const bool numeric_limits<wchar_t>::is_bounded;
  const bool numeric_limits<wchar_t>::is_modulo;
  const bool numeric_limits<wchar_t>::traps;
  const bool numeric_limits<wchar_t>::tinyness_before;
  const float_round_style numeric_limits<wchar_t>::round_style;
#endif // _GLIBCXX_USE_WCHAR_T

  // short
  const bool numeric_limits<short>::is_specialized;
  const int  numeric_limits<short>::digits;
  const int  numeric_limits<short>::digits10;
  const bool numeric_limits<short>::is_signed;
  const bool numeric_limits<short>::is_integer;
  const bool numeric_limits<short>::is_exact;
  const int  numeric_limits<short>::radix;
  const int  numeric_limits<short>::min_exponent;
  const int  numeric_limits<short>::min_exponent10;
  const int  numeric_limits<short>::max_exponent;
  const int  numeric_limits<short>::max_exponent10;
  const bool numeric_limits<short>::has_infinity;
  const bool numeric_limits<short>::has_quiet_NaN;
  const bool numeric_limits<short>::has_signaling_NaN;
  const float_denorm_style numeric_limits<short>::has_denorm;
  const bool numeric_limits<short>::has_denorm_loss;
  const bool numeric_limits<short>::is_iec559;
  const bool numeric_limits<short>::is_bounded;
  const bool numeric_limits<short>::is_modulo;
  const bool numeric_limits<short>::traps;
  const bool numeric_limits<short>::tinyness_before;
  const float_round_style numeric_limits<short>::round_style;

  // unsigned short
  const bool numeric_limits<unsigned short>::is_specialized;
  const int  numeric_limits<unsigned short>::digits;
  const int  numeric_limits<unsigned short>::digits10;
  const bool numeric_limits<unsigned short>::is_signed;
  const bool numeric_limits<unsigned short>::is_integer;
  const bool numeric_limits<unsigned short>::is_exact;
  const int  numeric_limits<unsigned short>::radix;
  const int  numeric_limits<unsigned short>::min_exponent;
  const int  numeric_limits<unsigned short>::min_exponent10;
  const int  numeric_limits<unsigned short>::max_exponent;
  const int  numeric_limits<unsigned short>::max_exponent10;
  const bool numeric_limits<unsigned short>::has_infinity;
  const bool numeric_limits<unsigned short>::has_quiet_NaN;
  const bool numeric_limits<unsigned short>::has_signaling_NaN;
  const float_denorm_style numeric_limits<unsigned short>::has_denorm;
  const bool numeric_limits<unsigned short>::has_denorm_loss;
  const bool numeric_limits<unsigned short>::is_iec559;
  const bool numeric_limits<unsigned short>::is_bounded;
  const bool numeric_limits<unsigned short>::is_modulo;
  const bool numeric_limits<unsigned short>::traps;
  const bool numeric_limits<unsigned short>::tinyness_before;
  const float_round_style numeric_limits<unsigned short>::round_style;

  // int
  const bool numeric_limits<int>::is_specialized;
  const int  numeric_limits<int>::digits;
  const int  numeric_limits<int>::digits10;
  const bool numeric_limits<int>::is_signed;
  const bool numeric_limits<int>::is_integer;
  const bool numeric_limits<int>::is_exact;
  const int  numeric_limits<int>::radix;
  const int  numeric_limits<int>::min_exponent;
  const int  numeric_limits<int>::min_exponent10;
  const int  numeric_limits<int>::max_exponent;
  const int  numeric_limits<int>::max_exponent10;
  const bool numeric_limits<int>::has_infinity;
  const bool numeric_limits<int>::has_quiet_NaN;
  const bool numeric_limits<int>::has_signaling_NaN;
  const float_denorm_style numeric_limits<int>::has_denorm;
  const bool numeric_limits<int>::has_denorm_loss;
  const bool numeric_limits<int>::is_iec559;
  const bool numeric_limits<int>::is_bounded;
  const bool numeric_limits<int>::is_modulo;
  const bool numeric_limits<int>::traps;
  const bool numeric_limits<int>::tinyness_before;
  const float_round_style numeric_limits<int>::round_style;

  // unsigned int
  const bool numeric_limits<unsigned int>::is_specialized;
  const int  numeric_limits<unsigned int>::digits;
  const int  numeric_limits<unsigned int>::digits10;
  const bool numeric_limits<unsigned int>::is_signed;
  const bool numeric_limits<unsigned int>::is_integer;
  const bool numeric_limits<unsigned int>::is_exact;
  const int  numeric_limits<unsigned int>::radix;
  const int  numeric_limits<unsigned int>::min_exponent;
  const int  numeric_limits<unsigned int>::min_exponent10;
  const int  numeric_limits<unsigned int>::max_exponent;
  const int  numeric_limits<unsigned int>::max_exponent10;
  const bool numeric_limits<unsigned int>::has_infinity;
  const bool numeric_limits<unsigned int>::has_quiet_NaN;
  const bool numeric_limits<unsigned int>::has_signaling_NaN;
  const float_denorm_style numeric_limits<unsigned int>::has_denorm;
  const bool numeric_limits<unsigned int>::has_denorm_loss;
  const bool numeric_limits<unsigned int>::is_iec559;
  const bool numeric_limits<unsigned int>::is_bounded;
  const bool numeric_limits<unsigned int>::is_modulo;
  const bool numeric_limits<unsigned int>::traps;
  const bool numeric_limits<unsigned int>::tinyness_before;
  const float_round_style numeric_limits<unsigned int>::round_style;

  // long
  const bool numeric_limits<long>::is_specialized;
  const int  numeric_limits<long>::digits;
  const int  numeric_limits<long>::digits10;
  const bool numeric_limits<long>::is_signed;
  const bool numeric_limits<long>::is_integer;
  const bool numeric_limits<long>::is_exact;
  const int  numeric_limits<long>::radix;
  const int  numeric_limits<long>::min_exponent;
  const int  numeric_limits<long>::min_exponent10;
  const int  numeric_limits<long>::max_exponent;
  const int  numeric_limits<long>::max_exponent10;
  const bool numeric_limits<long>::has_infinity;
  const bool numeric_limits<long>::has_quiet_NaN;
  const bool numeric_limits<long>::has_signaling_NaN;
  const float_denorm_style numeric_limits<long>::has_denorm;
  const bool numeric_limits<long>::has_denorm_loss;
  const bool numeric_limits<long>::is_iec559;
  const bool numeric_limits<long>::is_bounded;
  const bool numeric_limits<long>::is_modulo;
  const bool numeric_limits<long>::traps;
  const bool numeric_limits<long>::tinyness_before;
  const float_round_style numeric_limits<long>::round_style;

  // unsigned long
  const bool numeric_limits<unsigned long>::is_specialized;
  const int  numeric_limits<unsigned long>::digits;
  const int  numeric_limits<unsigned long>::digits10;
  const bool numeric_limits<unsigned long>::is_signed;
  const bool numeric_limits<unsigned long>::is_integer;
  const bool numeric_limits<unsigned long>::is_exact;
  const int  numeric_limits<unsigned long>::radix;
  const int  numeric_limits<unsigned long>::min_exponent;
  const int  numeric_limits<unsigned long>::min_exponent10;
  const int  numeric_limits<unsigned long>::max_exponent;
  const int  numeric_limits<unsigned long>::max_exponent10;
  const bool numeric_limits<unsigned long>::has_infinity;
  const bool numeric_limits<unsigned long>::has_quiet_NaN;
  const bool numeric_limits<unsigned long>::has_signaling_NaN;
  const float_denorm_style numeric_limits<unsigned long>::has_denorm;
  const bool numeric_limits<unsigned long>::has_denorm_loss;
  const bool numeric_limits<unsigned long>::is_iec559;
  const bool numeric_limits<unsigned long>::is_bounded;
  const bool numeric_limits<unsigned long>::is_modulo;
  const bool numeric_limits<unsigned long>::traps;
  const bool numeric_limits<unsigned long>::tinyness_before;
  const float_round_style numeric_limits<unsigned long>::round_style;

  // NOTA BENE:  long long is an extension
  const bool numeric_limits<long long>::is_specialized;
  const int  numeric_limits<long long>::digits;
  const int  numeric_limits<long long>::digits10;
  const bool numeric_limits<long long>::is_signed;
  const bool numeric_limits<long long>::is_integer;
  const bool numeric_limits<long long>::is_exact;
  const int  numeric_limits<long long>::radix;
  const int  numeric_limits<long long>::min_exponent;
  const int  numeric_limits<long long>::min_exponent10;
  const int  numeric_limits<long long>::max_exponent;
  const int  numeric_limits<long long>::max_exponent10;
  const bool numeric_limits<long long>::has_infinity;
  const bool numeric_limits<long long>::has_quiet_NaN;
  const bool numeric_limits<long long>::has_signaling_NaN;
  const float_denorm_style numeric_limits<long long>::has_denorm;
  const bool numeric_limits<long long>::has_denorm_loss;
  const bool numeric_limits<long long>::is_iec559;
  const bool numeric_limits<long long>::is_bounded;
  const bool numeric_limits<long long>::is_modulo;
  const bool numeric_limits<long long>::traps;
  const bool numeric_limits<long long>::tinyness_before;
  const float_round_style numeric_limits<long long>::round_style;

  const bool numeric_limits<unsigned long long>::is_specialized;
  const int  numeric_limits<unsigned long long>::digits;
  const int  numeric_limits<unsigned long long>::digits10;
  const bool numeric_limits<unsigned long long>::is_signed;
  const bool numeric_limits<unsigned long long>::is_integer;
  const bool numeric_limits<unsigned long long>::is_exact;
  const int  numeric_limits<unsigned long long>::radix;
  const int  numeric_limits<unsigned long long>::min_exponent;
  const int  numeric_limits<unsigned long long>::min_exponent10;
  const int  numeric_limits<unsigned long long>::max_exponent;
  const int  numeric_limits<unsigned long long>::max_exponent10;
  const bool numeric_limits<unsigned long long>::has_infinity;
  const bool numeric_limits<unsigned long long>::has_quiet_NaN;
  const bool numeric_limits<unsigned long long>::has_signaling_NaN;
  const float_denorm_style numeric_limits<unsigned long long>::has_denorm;
  const bool numeric_limits<unsigned long long>::has_denorm_loss;
  const bool numeric_limits<unsigned long long>::is_iec559;
  const bool numeric_limits<unsigned long long>::is_bounded;
  const bool numeric_limits<unsigned long long>::is_modulo;
  const bool numeric_limits<unsigned long long>::traps;
  const bool numeric_limits<unsigned long long>::tinyness_before;
  const float_round_style numeric_limits<unsigned long long>::round_style;

  // float
  const bool numeric_limits<float>::is_specialized;
  const int  numeric_limits<float>::digits;
  const int  numeric_limits<float>::digits10;
  const bool numeric_limits<float>::is_signed;
  const bool numeric_limits<float>::is_integer;
  const bool numeric_limits<float>::is_exact;
  const int  numeric_limits<float>::radix;
  const int  numeric_limits<float>::min_exponent;
  const int  numeric_limits<float>::min_exponent10;
  const int  numeric_limits<float>::max_exponent;
  const int  numeric_limits<float>::max_exponent10;
  const bool numeric_limits<float>::has_infinity;
  const bool numeric_limits<float>::has_quiet_NaN;
  const bool numeric_limits<float>::has_signaling_NaN;
  const float_denorm_style numeric_limits<float>::has_denorm;
  const bool numeric_limits<float>::has_denorm_loss;
  const bool numeric_limits<float>::is_iec559;
  const bool numeric_limits<float>::is_bounded;
  const bool numeric_limits<float>::is_modulo;
  const bool numeric_limits<float>::traps;
  const bool numeric_limits<float>::tinyness_before;
  const float_round_style numeric_limits<float>::round_style;

  // double
  const bool numeric_limits<double>::is_specialized;
  const int  numeric_limits<double>::digits;
  const int  numeric_limits<double>::digits10;
  const bool numeric_limits<double>::is_signed;
  const bool numeric_limits<double>::is_integer;
  const bool numeric_limits<double>::is_exact;
  const int  numeric_limits<double>::radix;
  const int  numeric_limits<double>::min_exponent;
  const int  numeric_limits<double>::min_exponent10;
  const int  numeric_limits<double>::max_exponent;
  const int  numeric_limits<double>::max_exponent10;
  const bool numeric_limits<double>::has_infinity;
  const bool numeric_limits<double>::has_quiet_NaN;
  const bool numeric_limits<double>::has_signaling_NaN;
  const float_denorm_style numeric_limits<double>::has_denorm;
  const bool numeric_limits<double>::has_denorm_loss;
  const bool numeric_limits<double>::is_iec559;
  const bool numeric_limits<double>::is_bounded;
  const bool numeric_limits<double>::is_modulo;
  const bool numeric_limits<double>::traps;
  const bool numeric_limits<double>::tinyness_before;
  const float_round_style numeric_limits<double>::round_style;

  // long double
  const bool numeric_limits<long double>::is_specialized;
  const int  numeric_limits<long double>::digits;
  const int  numeric_limits<long double>::digits10;
  const bool numeric_limits<long double>::is_signed;
  const bool numeric_limits<long double>::is_integer;
  const bool numeric_limits<long double>::is_exact;
  const int  numeric_limits<long double>::radix;
  const int  numeric_limits<long double>::min_exponent;
  const int  numeric_limits<long double>::min_exponent10;
  const int  numeric_limits<long double>::max_exponent;
  const int  numeric_limits<long double>::max_exponent10;
  const bool numeric_limits<long double>::has_infinity;
  const bool numeric_limits<long double>::has_quiet_NaN;
  const bool numeric_limits<long double>::has_signaling_NaN;
  const float_denorm_style numeric_limits<long double>::has_denorm;
  const bool numeric_limits<long double>::has_denorm_loss;
  const bool numeric_limits<long double>::is_iec559;
  const bool numeric_limits<long double>::is_bounded;
  const bool numeric_limits<long double>::is_modulo;
  const bool numeric_limits<long double>::traps;
  const bool numeric_limits<long double>::tinyness_before;
  const float_round_style numeric_limits<long double>::round_style;

_GLIBCXX_END_NAMESPACE

// XXX GLIBCXX_ABI Deprecated
#ifdef _GLIBCXX_LONG_DOUBLE_COMPAT

#define _GLIBCXX_NUM_LIM_COMPAT(type, member, len) \
  extern "C" type _ZNSt14numeric_limitsIeE ## len ## member ## E \
  __attribute__ ((alias ("_ZNSt14numeric_limitsIdE" #len #member "E")))
_GLIBCXX_NUM_LIM_COMPAT (bool, is_specialized, 14);
_GLIBCXX_NUM_LIM_COMPAT (int, digits, 6);
_GLIBCXX_NUM_LIM_COMPAT (int, digits10, 8);
_GLIBCXX_NUM_LIM_COMPAT (bool, is_signed, 9);
_GLIBCXX_NUM_LIM_COMPAT (bool, is_integer, 10);
_GLIBCXX_NUM_LIM_COMPAT (bool, is_exact, 8);
_GLIBCXX_NUM_LIM_COMPAT (int, radix, 5);
_GLIBCXX_NUM_LIM_COMPAT (int, min_exponent, 12);
_GLIBCXX_NUM_LIM_COMPAT (int, min_exponent10, 14);
_GLIBCXX_NUM_LIM_COMPAT (int, max_exponent, 12);
_GLIBCXX_NUM_LIM_COMPAT (int, max_exponent10, 14);
_GLIBCXX_NUM_LIM_COMPAT (bool, has_infinity, 12);
_GLIBCXX_NUM_LIM_COMPAT (bool, has_quiet_NaN, 13);
_GLIBCXX_NUM_LIM_COMPAT (bool, has_signaling_NaN, 17);
_GLIBCXX_NUM_LIM_COMPAT (std::float_denorm_style, has_denorm, 10);
_GLIBCXX_NUM_LIM_COMPAT (bool, has_denorm_loss, 15);
_GLIBCXX_NUM_LIM_COMPAT (bool, is_iec559, 9);
_GLIBCXX_NUM_LIM_COMPAT (bool, is_bounded, 10);
_GLIBCXX_NUM_LIM_COMPAT (bool, is_modulo, 9);
_GLIBCXX_NUM_LIM_COMPAT (bool, traps, 5);
_GLIBCXX_NUM_LIM_COMPAT (bool, tinyness_before, 15);
_GLIBCXX_NUM_LIM_COMPAT (std::float_round_style, round_style, 11);

#endif // _GLIBCXX_LONG_DOUBLE_COMPAT
