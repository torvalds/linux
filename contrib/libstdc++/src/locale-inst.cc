// Locale support -*- C++ -*-

// Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

#include <locale>

// Instantiation configuration.
#ifndef C
# define C char
# define C_is_char
#endif

_GLIBCXX_BEGIN_NAMESPACE(std)

  // moneypunct, money_get, and money_put
  template class moneypunct<C, false>;
  template class moneypunct<C, true>;
  template struct __moneypunct_cache<C, false>;
  template struct __moneypunct_cache<C, true>;
  template class moneypunct_byname<C, false>;
  template class moneypunct_byname<C, true>;
_GLIBCXX_BEGIN_LDBL_NAMESPACE
  template class money_get<C, istreambuf_iterator<C> >;
  template class money_put<C, ostreambuf_iterator<C> >;
  template
    istreambuf_iterator<C>
    money_get<C, istreambuf_iterator<C> >::
    _M_extract<true>(istreambuf_iterator<C>, istreambuf_iterator<C>,
		     ios_base&, ios_base::iostate&, string&) const;

  template
    istreambuf_iterator<C>
    money_get<C, istreambuf_iterator<C> >::
    _M_extract<false>(istreambuf_iterator<C>, istreambuf_iterator<C>,
		      ios_base&, ios_base::iostate&, string&) const;

  template
    ostreambuf_iterator<C>
    money_put<C, ostreambuf_iterator<C> >::
    _M_insert<true>(ostreambuf_iterator<C>, ios_base&, C, 
		    const string_type&) const;

  template
    ostreambuf_iterator<C>
    money_put<C, ostreambuf_iterator<C> >::
    _M_insert<false>(ostreambuf_iterator<C>, ios_base&, C, 
		     const string_type&) const;
_GLIBCXX_END_LDBL_NAMESPACE

  // numpunct, numpunct_byname, num_get, and num_put
  template class numpunct<C>;
  template struct __numpunct_cache<C>;
  template class numpunct_byname<C>;
_GLIBCXX_BEGIN_LDBL_NAMESPACE
  template class num_get<C, istreambuf_iterator<C> >;
  template class num_put<C, ostreambuf_iterator<C> >; 
  template
    istreambuf_iterator<C>
    num_get<C, istreambuf_iterator<C> >::
    _M_extract_int(istreambuf_iterator<C>, istreambuf_iterator<C>,
		   ios_base&, ios_base::iostate&,
		   long&) const;

  template
    istreambuf_iterator<C>
    num_get<C, istreambuf_iterator<C> >::
    _M_extract_int(istreambuf_iterator<C>, istreambuf_iterator<C>,
		   ios_base&, ios_base::iostate&, 
		   unsigned short&) const;

  template
    istreambuf_iterator<C>
    num_get<C, istreambuf_iterator<C> >::
    _M_extract_int(istreambuf_iterator<C>, istreambuf_iterator<C>,
		   ios_base&, ios_base::iostate&,
		   unsigned int&) const;

  template
    istreambuf_iterator<C>
    num_get<C, istreambuf_iterator<C> >::
    _M_extract_int(istreambuf_iterator<C>, istreambuf_iterator<C>,
		   ios_base&, ios_base::iostate&,
		   unsigned long&) const;

#ifdef _GLIBCXX_USE_LONG_LONG
  template
    istreambuf_iterator<C>
    num_get<C, istreambuf_iterator<C> >::
    _M_extract_int(istreambuf_iterator<C>, istreambuf_iterator<C>,
		   ios_base&, ios_base::iostate&,
		   long long&) const;

  template
    istreambuf_iterator<C>
    num_get<C, istreambuf_iterator<C> >::
    _M_extract_int(istreambuf_iterator<C>, istreambuf_iterator<C>,
		   ios_base&, ios_base::iostate&,
		   unsigned long long&) const;
#endif

  template
    ostreambuf_iterator<C>
    num_put<C, ostreambuf_iterator<C> >::
    _M_insert_int(ostreambuf_iterator<C>, ios_base&, C, 
		  long) const;

  template
    ostreambuf_iterator<C>
    num_put<C, ostreambuf_iterator<C> >::
    _M_insert_int(ostreambuf_iterator<C>, ios_base&, C, 
		  unsigned long) const;

#ifdef _GLIBCXX_USE_LONG_LONG
  template
    ostreambuf_iterator<C>
    num_put<C, ostreambuf_iterator<C> >::
    _M_insert_int(ostreambuf_iterator<C>, ios_base&, C, 
		  long long) const;

  template
    ostreambuf_iterator<C>
    num_put<C, ostreambuf_iterator<C> >::
    _M_insert_int(ostreambuf_iterator<C>, ios_base&, C, 
		  unsigned long long) const;
#endif

  template
    ostreambuf_iterator<C>
    num_put<C, ostreambuf_iterator<C> >::
    _M_insert_float(ostreambuf_iterator<C>, ios_base&, C, char, 
		    double) const;

  template
    ostreambuf_iterator<C>
    num_put<C, ostreambuf_iterator<C> >::
    _M_insert_float(ostreambuf_iterator<C>, ios_base&, C, char, 
		    long double) const;
_GLIBCXX_END_LDBL_NAMESPACE

  // time_get and time_put
  template class __timepunct<C>;
  template struct __timepunct_cache<C>;
  template class time_put<C, ostreambuf_iterator<C> >;
  template class time_put_byname<C, ostreambuf_iterator<C> >;
  template class time_get<C, istreambuf_iterator<C> >;
  template class time_get_byname<C, istreambuf_iterator<C> >;

  // messages
  template class messages<C>;
  template class messages_byname<C>;
  
  // ctype
  template class __ctype_abstract_base<C>;
  template class ctype_byname<C>;
  
  // codecvt
  template class __codecvt_abstract_base<C, char, mbstate_t>;
  template class codecvt_byname<C, char, mbstate_t>;

  // collate
  template class collate<C>;
  template class collate_byname<C>;
    
  // use_facet
  // NB: use_facet<ctype> is specialized
  template
    const codecvt<C, char, mbstate_t>& 
    use_facet<codecvt<C, char, mbstate_t> >(const locale&);

  template
    const collate<C>& 
    use_facet<collate<C> >(const locale&);

  template
    const numpunct<C>& 
    use_facet<numpunct<C> >(const locale&);

  template 
    const num_put<C>& 
    use_facet<num_put<C> >(const locale&);

  template 
    const num_get<C>& 
    use_facet<num_get<C> >(const locale&);

  template
    const moneypunct<C, true>& 
    use_facet<moneypunct<C, true> >(const locale&);

  template
    const moneypunct<C, false>& 
    use_facet<moneypunct<C, false> >(const locale&);

  template 
    const money_put<C>& 
    use_facet<money_put<C> >(const locale&);

  template 
    const money_get<C>& 
    use_facet<money_get<C> >(const locale&);

  template
    const __timepunct<C>& 
    use_facet<__timepunct<C> >(const locale&);

  template 
    const time_put<C>& 
    use_facet<time_put<C> >(const locale&);

  template 
    const time_get<C>& 
    use_facet<time_get<C> >(const locale&);

  template 
    const messages<C>& 
    use_facet<messages<C> >(const locale&);

  // has_facet
  template 
    bool
    has_facet<ctype<C> >(const locale&);

  template 
    bool
    has_facet<codecvt<C, char, mbstate_t> >(const locale&);

  template 
    bool
    has_facet<collate<C> >(const locale&);

  template 
    bool
    has_facet<numpunct<C> >(const locale&);

  template 
    bool
    has_facet<num_put<C> >(const locale&);

  template 
    bool
    has_facet<num_get<C> >(const locale&);

  template 
    bool
    has_facet<moneypunct<C> >(const locale&);

  template 
    bool
    has_facet<money_put<C> >(const locale&);

  template 
    bool
    has_facet<money_get<C> >(const locale&);

  template 
    bool
    has_facet<__timepunct<C> >(const locale&);

  template 
    bool
    has_facet<time_put<C> >(const locale&);

  template 
    bool
    has_facet<time_get<C> >(const locale&);

  template 
    bool
    has_facet<messages<C> >(const locale&);


  // locale functions.
  template
    C*
    __add_grouping<C>(C*, C, char const*, size_t, 
			 C const*, C const*);

  template class __pad<C, char_traits<C> >;

  template
    int
    __int_to_char(C*, unsigned long, const C*,
		  ios_base::fmtflags, bool);

#ifdef _GLIBCXX_USE_LONG_LONG
  template
    int
    __int_to_char(C*, unsigned long long, const C*, 
		  ios_base::fmtflags, bool);
#endif

_GLIBCXX_END_NAMESPACE

// XXX GLIBCXX_ABI Deprecated
#if defined _GLIBCXX_LONG_DOUBLE_COMPAT && defined C_is_char

#define _GLIBCXX_LDBL_COMPAT(dbl, ldbl) \
  extern "C" void ldbl (void) __attribute__ ((alias (#dbl), weak))

_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intIjEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intIjEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intIlEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intIlEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intImEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intImEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intItEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intItEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intIxEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intIxEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intIyEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRT_,
		     _ZNKSt7num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE14_M_extract_intIyEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE13_M_insert_intIlEES4_S4_RSt8ios_basecT_,
		     _ZNKSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE13_M_insert_intIlEES3_S3_RSt8ios_basecT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE13_M_insert_intImEES4_S4_RSt8ios_basecT_,
		     _ZNKSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE13_M_insert_intImEES3_S3_RSt8ios_basecT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE13_M_insert_intIxEES4_S4_RSt8ios_basecT_,
		     _ZNKSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE13_M_insert_intIxEES3_S3_RSt8ios_basecT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE13_M_insert_intIyEES4_S4_RSt8ios_basecT_,
		     _ZNKSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE13_M_insert_intIyEES3_S3_RSt8ios_basecT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1287num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE15_M_insert_floatIdEES4_S4_RSt8ios_baseccT_,
		     _ZNKSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE15_M_insert_floatIdEES3_S3_RSt8ios_baseccT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE15_M_insert_floatIdEES3_S3_RSt8ios_baseccT_,
		     _ZNKSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE15_M_insert_floatIeEES3_S3_RSt8ios_baseccT_);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1289money_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE10_M_extractILb0EEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRSs,
		     _ZNKSt9money_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE10_M_extractILb0EEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRSs);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1289money_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE10_M_extractILb1EEES4_S4_S4_RSt8ios_baseRSt12_Ios_IostateRSs,
		     _ZNKSt9money_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE10_M_extractILb1EEES3_S3_S3_RSt8ios_baseRSt12_Ios_IostateRSs);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1289money_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE9_M_insertILb0EEES4_S4_RSt8ios_basecRKSs,
		     _ZNKSt9money_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE9_M_insertILb0EEES3_S3_RSt8ios_basecRKSs);
_GLIBCXX_LDBL_COMPAT(_ZNKSt17__gnu_cxx_ldbl1289money_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE9_M_insertILb1EEES4_S4_RSt8ios_basecRKSs,
		     _ZNKSt9money_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE9_M_insertILb1EEES3_S3_RSt8ios_basecRKSs);

#endif // _GLIBCXX_LONG_DOUBLE_COMPAT
