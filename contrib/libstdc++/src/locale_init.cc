// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

#include <clocale>
#include <cstring>
#include <cstdlib>     // For getenv, free.
#include <cctype>
#include <cwctype>     // For towupper, etc.
#include <locale>
#include <ext/concurrence.h>

namespace 
{
  __gnu_cxx::__mutex&
  get_locale_mutex()
  {
    static __gnu_cxx::__mutex locale_mutex;
    return locale_mutex;
  }

  using namespace std;

  typedef char fake_locale_Impl[sizeof(locale::_Impl)]
  __attribute__ ((aligned(__alignof__(locale::_Impl))));
  fake_locale_Impl c_locale_impl;

  typedef char fake_locale[sizeof(locale)]
  __attribute__ ((aligned(__alignof__(locale))));
  fake_locale c_locale;

  typedef char fake_name_vec[sizeof(char*)]
  __attribute__ ((aligned(__alignof__(char*))));
  fake_name_vec name_vec[6 + _GLIBCXX_NUM_CATEGORIES];

  typedef char fake_names[sizeof(char[2])]
  __attribute__ ((aligned(__alignof__(char[2]))));
  fake_names name_c[6 + _GLIBCXX_NUM_CATEGORIES];

  typedef char fake_facet_vec[sizeof(locale::facet*)]
  __attribute__ ((aligned(__alignof__(locale::facet*))));
  fake_facet_vec facet_vec[_GLIBCXX_NUM_FACETS];

  typedef char fake_cache_vec[sizeof(locale::facet*)]
  __attribute__ ((aligned(__alignof__(locale::facet*))));
  fake_cache_vec cache_vec[_GLIBCXX_NUM_FACETS];

  typedef char fake_ctype_c[sizeof(std::ctype<char>)]
  __attribute__ ((aligned(__alignof__(std::ctype<char>))));
  fake_ctype_c ctype_c;

  typedef char fake_collate_c[sizeof(std::collate<char>)]
  __attribute__ ((aligned(__alignof__(std::collate<char>))));
  fake_collate_c collate_c;

  typedef char fake_numpunct_c[sizeof(numpunct<char>)]
  __attribute__ ((aligned(__alignof__(numpunct<char>))));
  fake_numpunct_c numpunct_c;

  typedef char fake_num_get_c[sizeof(num_get<char>)]
  __attribute__ ((aligned(__alignof__(num_get<char>))));
  fake_num_get_c num_get_c;

  typedef char fake_num_put_c[sizeof(num_put<char>)]
  __attribute__ ((aligned(__alignof__(num_put<char>))));
  fake_num_put_c num_put_c;

  typedef char fake_codecvt_c[sizeof(codecvt<char, char, mbstate_t>)]
  __attribute__ ((aligned(__alignof__(codecvt<char, char, mbstate_t>))));
  fake_codecvt_c codecvt_c;

  typedef char fake_moneypunct_c[sizeof(moneypunct<char, true>)]
  __attribute__ ((aligned(__alignof__(moneypunct<char, true>))));
  fake_moneypunct_c moneypunct_ct;
  fake_moneypunct_c moneypunct_cf;

  typedef char fake_money_get_c[sizeof(money_get<char>)]
  __attribute__ ((aligned(__alignof__(money_get<char>))));
  fake_money_get_c money_get_c;
  
  typedef char fake_money_put_c[sizeof(money_put<char>)]
  __attribute__ ((aligned(__alignof__(money_put<char>))));
  fake_money_put_c money_put_c;

  typedef char fake_timepunct_c[sizeof(__timepunct<char>)]
  __attribute__ ((aligned(__alignof__(__timepunct<char>))));
  fake_timepunct_c timepunct_c;

  typedef char fake_time_get_c[sizeof(time_get<char>)]
  __attribute__ ((aligned(__alignof__(time_get<char>))));
  fake_time_get_c time_get_c;

  typedef char fake_time_put_c[sizeof(time_put<char>)]
  __attribute__ ((aligned(__alignof__(time_put<char>))));
  fake_time_put_c time_put_c;

  typedef char fake_messages_c[sizeof(messages<char>)]
  __attribute__ ((aligned(__alignof__(messages<char>))));
  fake_messages_c messages_c;

#ifdef  _GLIBCXX_USE_WCHAR_T
  typedef char fake_wtype_w[sizeof(std::ctype<wchar_t>)]
  __attribute__ ((aligned(__alignof__(std::ctype<wchar_t>))));
  fake_wtype_w ctype_w;

  typedef char fake_wollate_w[sizeof(std::collate<wchar_t>)]
  __attribute__ ((aligned(__alignof__(std::collate<wchar_t>))));
  fake_wollate_w collate_w;

  typedef char fake_numpunct_w[sizeof(numpunct<wchar_t>)]
  __attribute__ ((aligned(__alignof__(numpunct<wchar_t>))));
  fake_numpunct_w numpunct_w;

  typedef char fake_num_get_w[sizeof(num_get<wchar_t>)]
  __attribute__ ((aligned(__alignof__(num_get<wchar_t>))));
  fake_num_get_w num_get_w;

  typedef char fake_num_put_w[sizeof(num_put<wchar_t>)]
  __attribute__ ((aligned(__alignof__(num_put<wchar_t>))));
  fake_num_put_w num_put_w;

  typedef char fake_wodecvt_w[sizeof(codecvt<wchar_t, char, mbstate_t>)]
  __attribute__ ((aligned(__alignof__(codecvt<wchar_t, char, mbstate_t>))));
  fake_wodecvt_w codecvt_w;

  typedef char fake_moneypunct_w[sizeof(moneypunct<wchar_t, true>)]
  __attribute__ ((aligned(__alignof__(moneypunct<wchar_t, true>))));
  fake_moneypunct_w moneypunct_wt;
  fake_moneypunct_w moneypunct_wf;

  typedef char fake_money_get_w[sizeof(money_get<wchar_t>)]
  __attribute__ ((aligned(__alignof__(money_get<wchar_t>))));
  fake_money_get_w money_get_w;
  
  typedef char fake_money_put_w[sizeof(money_put<wchar_t>)]
  __attribute__ ((aligned(__alignof__(money_put<wchar_t>))));
  fake_money_put_w money_put_w;

  typedef char fake_timepunct_w[sizeof(__timepunct<wchar_t>)]
  __attribute__ ((aligned(__alignof__(__timepunct<wchar_t>))));
  fake_timepunct_w timepunct_w;

  typedef char fake_time_get_w[sizeof(time_get<wchar_t>)]
  __attribute__ ((aligned(__alignof__(time_get<wchar_t>))));
  fake_time_get_w time_get_w;

  typedef char fake_time_put_w[sizeof(time_put<wchar_t>)]
  __attribute__ ((aligned(__alignof__(time_put<wchar_t>))));
  fake_time_put_w time_put_w;

  typedef char fake_messages_w[sizeof(messages<wchar_t>)]
  __attribute__ ((aligned(__alignof__(messages<wchar_t>))));
  fake_messages_w messages_w;
#endif

  // Storage for "C" locale caches.
  typedef char fake_num_cache_c[sizeof(std::__numpunct_cache<char>)]
  __attribute__ ((aligned(__alignof__(std::__numpunct_cache<char>))));
  fake_num_cache_c numpunct_cache_c;

  typedef char fake_money_cache_c[sizeof(std::__moneypunct_cache<char, true>)]
  __attribute__ ((aligned(__alignof__(std::__moneypunct_cache<char, true>))));
  fake_money_cache_c moneypunct_cache_ct;
  fake_money_cache_c moneypunct_cache_cf;

  typedef char fake_time_cache_c[sizeof(std::__timepunct_cache<char>)]
  __attribute__ ((aligned(__alignof__(std::__timepunct_cache<char>))));
  fake_time_cache_c timepunct_cache_c;

#ifdef _GLIBCXX_USE_WCHAR_T
  typedef char fake_num_cache_w[sizeof(std::__numpunct_cache<wchar_t>)]
  __attribute__ ((aligned(__alignof__(std::__numpunct_cache<wchar_t>))));
  fake_num_cache_w numpunct_cache_w;

  typedef char fake_money_cache_w[sizeof(std::__moneypunct_cache<wchar_t,true>)]
  __attribute__ ((aligned(__alignof__(std::__moneypunct_cache<wchar_t,true>))));
  fake_money_cache_w moneypunct_cache_wt;
  fake_money_cache_w moneypunct_cache_wf;

  typedef char fake_time_cache_w[sizeof(std::__timepunct_cache<wchar_t>)]
  __attribute__ ((aligned(__alignof__(std::__timepunct_cache<wchar_t>))));
  fake_time_cache_w timepunct_cache_w;
#endif
} // anonymous namespace

_GLIBCXX_BEGIN_NAMESPACE(std)

  locale::locale() throw() : _M_impl(0)
  { 
    _S_initialize();
    __gnu_cxx::__scoped_lock sentry(get_locale_mutex());
    _S_global->_M_add_reference();
    _M_impl = _S_global;
  }

  locale
  locale::global(const locale& __other)
  {
    _S_initialize();
    _Impl* __old;
    {
      __gnu_cxx::__scoped_lock sentry(get_locale_mutex());
      __old = _S_global;
      __other._M_impl->_M_add_reference();
      _S_global = __other._M_impl;
      const string __other_name = __other.name();
      if (__other_name != "*")
	setlocale(LC_ALL, __other_name.c_str());
    }

    // Reference count sanity check: one reference removed for the
    // subsition of __other locale, one added by return-by-value. Net
    // difference: zero. When the returned locale object's destrutor
    // is called, then the reference count is decremented and possibly
    // destroyed.
    return locale(__old);
  }

  const locale&
  locale::classic()
  {
    _S_initialize();
    return reinterpret_cast<const locale&>(c_locale);
  }

  void
  locale::_S_initialize_once()
  {
    // 2 references.
    // One reference for _S_classic, one for _S_global
    _S_classic = new (&c_locale_impl) _Impl(2);
    _S_global = _S_classic; 	    
    new (&c_locale) locale(_S_classic);
  }

  void  
  locale::_S_initialize()
  {
#ifdef __GTHREADS
    if (__gthread_active_p())
      __gthread_once(&_S_once, _S_initialize_once);
#endif
    if (!_S_classic)
      _S_initialize_once();
  }

  // Definitions for static const data members of locale::_Impl
  const locale::id* const
  locale::_Impl::_S_id_ctype[] =
  {
    &std::ctype<char>::id, 
    &codecvt<char, char, mbstate_t>::id,
#ifdef _GLIBCXX_USE_WCHAR_T
    &std::ctype<wchar_t>::id,
    &codecvt<wchar_t, char, mbstate_t>::id,
#endif
    0
  };

  const locale::id* const
  locale::_Impl::_S_id_numeric[] =
  {
    &num_get<char>::id,  
    &num_put<char>::id,  
    &numpunct<char>::id, 
#ifdef _GLIBCXX_USE_WCHAR_T
    &num_get<wchar_t>::id,
    &num_put<wchar_t>::id,
    &numpunct<wchar_t>::id,
#endif
    0
  };
  
  const locale::id* const
  locale::_Impl::_S_id_collate[] =
  {
    &std::collate<char>::id,
#ifdef _GLIBCXX_USE_WCHAR_T
    &std::collate<wchar_t>::id,
#endif
    0
  };

  const locale::id* const
  locale::_Impl::_S_id_time[] =
  {
    &__timepunct<char>::id, 
    &time_get<char>::id, 
    &time_put<char>::id, 
#ifdef _GLIBCXX_USE_WCHAR_T
    &__timepunct<wchar_t>::id, 
    &time_get<wchar_t>::id,
    &time_put<wchar_t>::id,
#endif
    0
  };
  
  const locale::id* const
  locale::_Impl::_S_id_monetary[] =
  {
    &money_get<char>::id,        
    &money_put<char>::id,        
    &moneypunct<char, false>::id, 
    &moneypunct<char, true >::id, 
#ifdef _GLIBCXX_USE_WCHAR_T
    &money_get<wchar_t>::id,
    &money_put<wchar_t>::id,
    &moneypunct<wchar_t, false>::id,
    &moneypunct<wchar_t, true >::id,
#endif
    0
  };

  const locale::id* const
  locale::_Impl::_S_id_messages[] =
  {
    &std::messages<char>::id, 
#ifdef _GLIBCXX_USE_WCHAR_T
    &std::messages<wchar_t>::id,
#endif
    0
  };
  
  const locale::id* const* const
  locale::_Impl::_S_facet_categories[] =
  {
    // Order must match the decl order in class locale.
    locale::_Impl::_S_id_ctype,
    locale::_Impl::_S_id_numeric,
    locale::_Impl::_S_id_collate,
    locale::_Impl::_S_id_time,
    locale::_Impl::_S_id_monetary,
    locale::_Impl::_S_id_messages,
    0
  };

  // Construct "C" _Impl.
  locale::_Impl::
  _Impl(size_t __refs) throw() 
  : _M_refcount(__refs), _M_facets(0), _M_facets_size(_GLIBCXX_NUM_FACETS),
  _M_caches(0), _M_names(0)    
  {
    _M_facets = new (&facet_vec) const facet*[_M_facets_size];
    _M_caches = new (&cache_vec) const facet*[_M_facets_size];
    for (size_t __i = 0; __i < _M_facets_size; ++__i)
      _M_facets[__i] = _M_caches[__i] = 0;

    // Name the categories.
    _M_names = new (&name_vec) char*[_S_categories_size];
    _M_names[0] = new (&name_c[0]) char[2];
    std::memcpy(_M_names[0], locale::facet::_S_get_c_name(), 2);
    for (size_t __j = 1; __j < _S_categories_size; ++__j)
      _M_names[__j] = 0;

    // This is needed as presently the C++ version of "C" locales
    // != data in the underlying locale model for __timepunct,
    // numpunct, and moneypunct. Also, the "C" locales must be
    // constructed in a way such that they are pre-allocated.
    // NB: Set locale::facets(ref) count to one so that each individual
    // facet is not destroyed when the locale (and thus locale::_Impl) is
    // destroyed.
    _M_init_facet(new (&ctype_c) std::ctype<char>(0, false, 1));
    _M_init_facet(new (&codecvt_c) codecvt<char, char, mbstate_t>(1));

    typedef __numpunct_cache<char> num_cache_c;
    num_cache_c* __npc = new (&numpunct_cache_c) num_cache_c(2);
    _M_init_facet(new (&numpunct_c) numpunct<char>(__npc, 1));

    _M_init_facet(new (&num_get_c) num_get<char>(1));
    _M_init_facet(new (&num_put_c) num_put<char>(1));
    _M_init_facet(new (&collate_c) std::collate<char>(1));

    typedef __moneypunct_cache<char, false> money_cache_cf;
    typedef __moneypunct_cache<char, true> money_cache_ct;
    money_cache_cf* __mpcf = new (&moneypunct_cache_cf) money_cache_cf(2);
    _M_init_facet(new (&moneypunct_cf) moneypunct<char, false>(__mpcf, 1));
    money_cache_ct* __mpct = new (&moneypunct_cache_ct) money_cache_ct(2);
    _M_init_facet(new (&moneypunct_ct) moneypunct<char, true>(__mpct, 1));

    _M_init_facet(new (&money_get_c) money_get<char>(1));
    _M_init_facet(new (&money_put_c) money_put<char>(1));

    typedef __timepunct_cache<char> time_cache_c;
    time_cache_c* __tpc = new (&timepunct_cache_c) time_cache_c(2);
    _M_init_facet(new (&timepunct_c) __timepunct<char>(__tpc, 1));

    _M_init_facet(new (&time_get_c) time_get<char>(1));
    _M_init_facet(new (&time_put_c) time_put<char>(1));
    _M_init_facet(new (&messages_c) std::messages<char>(1));	

#ifdef  _GLIBCXX_USE_WCHAR_T
    _M_init_facet(new (&ctype_w) std::ctype<wchar_t>(1));
    _M_init_facet(new (&codecvt_w) codecvt<wchar_t, char, mbstate_t>(1));

    typedef __numpunct_cache<wchar_t> num_cache_w;
    num_cache_w* __npw = new (&numpunct_cache_w) num_cache_w(2);
    _M_init_facet(new (&numpunct_w) numpunct<wchar_t>(__npw, 1));

    _M_init_facet(new (&num_get_w) num_get<wchar_t>(1));
    _M_init_facet(new (&num_put_w) num_put<wchar_t>(1));
    _M_init_facet(new (&collate_w) std::collate<wchar_t>(1));

    typedef __moneypunct_cache<wchar_t, false> money_cache_wf;
    typedef __moneypunct_cache<wchar_t, true> money_cache_wt;
    money_cache_wf* __mpwf = new (&moneypunct_cache_wf) money_cache_wf(2);
    _M_init_facet(new (&moneypunct_wf) moneypunct<wchar_t, false>(__mpwf, 1));
    money_cache_wt* __mpwt = new (&moneypunct_cache_wt) money_cache_wt(2);
    _M_init_facet(new (&moneypunct_wt) moneypunct<wchar_t, true>(__mpwt, 1));

    _M_init_facet(new (&money_get_w) money_get<wchar_t>(1));
    _M_init_facet(new (&money_put_w) money_put<wchar_t>(1));

    typedef __timepunct_cache<wchar_t> time_cache_w;
    time_cache_w* __tpw = new (&timepunct_cache_w) time_cache_w(2);
    _M_init_facet(new (&timepunct_w) __timepunct<wchar_t>(__tpw, 1));

    _M_init_facet(new (&time_get_w) time_get<wchar_t>(1));
    _M_init_facet(new (&time_put_w) time_put<wchar_t>(1));
    _M_init_facet(new (&messages_w) std::messages<wchar_t>(1));
#endif 
     
    // This locale is safe to pre-cache, after all the facets have
    // been created and installed.
    _M_caches[numpunct<char>::id._M_id()] = __npc;
    _M_caches[moneypunct<char, false>::id._M_id()] = __mpcf;
    _M_caches[moneypunct<char, true>::id._M_id()] = __mpct;
    _M_caches[__timepunct<char>::id._M_id()] = __tpc;
#ifdef  _GLIBCXX_USE_WCHAR_T
    _M_caches[numpunct<wchar_t>::id._M_id()] = __npw;
    _M_caches[moneypunct<wchar_t, false>::id._M_id()] = __mpwf;
    _M_caches[moneypunct<wchar_t, true>::id._M_id()] = __mpwt;
    _M_caches[__timepunct<wchar_t>::id._M_id()] = __tpw;
#endif
  }

_GLIBCXX_END_NAMESPACE
