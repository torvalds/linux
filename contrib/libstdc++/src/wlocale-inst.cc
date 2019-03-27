// Locale support -*- C++ -*-

// Copyright (C) 1999, 2000, 2001, 2002, 2003, 2006
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
// ISO C++ 14882: 22.1  Locales
//

#include <bits/c++config.h>

#ifdef _GLIBCXX_USE_WCHAR_T
#define C wchar_t
#include "locale-inst.cc"

// XXX GLIBCXX_ABI Deprecated
#if defined _GLIBCXX_LONG_DOUBLE_COMPAT

#define _GLIBCXX_LDBL_COMPAT(dbl, ldbl) \
  extern "C" void ldbl (void) __attribute__ ((alias (#dbl), weak))

_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intIjEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intIjEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intIlEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intIlEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intImEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intImEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intItEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intItEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intIxEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intIxEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intIyEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE14_M_extract_intIyEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE13_M_insert_intIlEES4_S4_RSt8ios_basewT_,
		     _ZNKSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE13_M_insert_intIlEES3_S3_RSt8ios_basewT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE13_M_insert_intImEES4_S4_RSt8ios_basewT_,
		     _ZNKSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE13_M_insert_intImEES3_S3_RSt8ios_basewT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE13_M_insert_intIxEES4_S4_RSt8ios_basewT_,
		     _ZNKSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE13_M_insert_intIxEES3_S3_RSt8ios_basewT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE13_M_insert_intIyEES4_S4_RSt8ios_basewT_,
		     _ZNKSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE13_M_insert_intIyEES3_S3_RSt8ios_basewT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE15_M_insert_floatIdEES4_S4_RSt8ios_basewcT_,
		     _ZNKSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE15_M_insert_floatIdEES3_S3_RSt8ios_basewcT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE15_M_insert_floatIdEES3_S3_RSt8ios_basewcT_,
		     _ZNKSt7num_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE15_M_insert_floatIeEES3_S3_RSt8ios_basewcT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1289money_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE10_M_extractILb0EEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRSs,
		     _ZNKSt9money_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE10_M_extractILb0EEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRSs);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1289money_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE10_M_extractILb1EEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRSs,
		     _ZNKSt9money_getIwSt19istreambuf_iteratorIwSt11char_traitsIwEEE10_M_extractILb1EEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRSs);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1289money_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE9_M_insertILb0EEES4_S4_RSt8ios_basewRKSbIwS3_SaIwEE,
		     _ZNKSt9money_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE9_M_insertILb0EEES3_S3_RSt8ios_basewRKSbIwS2_SaIwEE);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1289money_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE9_M_insertILb1EEES4_S4_RSt8ios_basewRKSbIwS3_SaIwEE,
		     _ZNKSt9money_putIwSt19ostreambuf_iteratorIwSt11char_traitsIwEEE9_M_insertILb1EEES3_S3_RSt8ios_basewRKSbIwS2_SaIwEE);

#endif // _GLIBCXX_LONG_DOUBLE_COMPAT
#endif
