// -*- C++ -*-
//===---------------------- __bsd_locale_fallbacks.h ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// The BSDs have lots of *_l functions.  This file provides reimplementations
// of those functions for non-BSD platforms.
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_BSD_LOCALE_FALLBACKS_DEFAULTS_H
#define _LIBCPP_BSD_LOCALE_FALLBACKS_DEFAULTS_H

#include <stdlib.h>
#include <stdarg.h>
#include <memory>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

inline _LIBCPP_INLINE_VISIBILITY
decltype(MB_CUR_MAX) __libcpp_mb_cur_max_l(locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return MB_CUR_MAX;
}

inline _LIBCPP_INLINE_VISIBILITY
wint_t __libcpp_btowc_l(int __c, locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return btowc(__c);
}

inline _LIBCPP_INLINE_VISIBILITY
int __libcpp_wctob_l(wint_t __c, locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return wctob(__c);
}

inline _LIBCPP_INLINE_VISIBILITY
size_t __libcpp_wcsnrtombs_l(char *__dest, const wchar_t **__src, size_t __nwc,
                         size_t __len, mbstate_t *__ps, locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return wcsnrtombs(__dest, __src, __nwc, __len, __ps);
}

inline _LIBCPP_INLINE_VISIBILITY
size_t __libcpp_wcrtomb_l(char *__s, wchar_t __wc, mbstate_t *__ps, locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return wcrtomb(__s, __wc, __ps);
}

inline _LIBCPP_INLINE_VISIBILITY
size_t __libcpp_mbsnrtowcs_l(wchar_t * __dest, const char **__src, size_t __nms,
                      size_t __len, mbstate_t *__ps, locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return mbsnrtowcs(__dest, __src, __nms, __len, __ps);
}

inline _LIBCPP_INLINE_VISIBILITY
size_t __libcpp_mbrtowc_l(wchar_t *__pwc, const char *__s, size_t __n,
                   mbstate_t *__ps, locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return mbrtowc(__pwc, __s, __n, __ps);
}

inline _LIBCPP_INLINE_VISIBILITY
int __libcpp_mbtowc_l(wchar_t *__pwc, const char *__pmb, size_t __max, locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return mbtowc(__pwc, __pmb, __max);
}

inline _LIBCPP_INLINE_VISIBILITY
size_t __libcpp_mbrlen_l(const char *__s, size_t __n, mbstate_t *__ps, locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return mbrlen(__s, __n, __ps);
}

inline _LIBCPP_INLINE_VISIBILITY
lconv *__libcpp_localeconv_l(locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return localeconv();
}

inline _LIBCPP_INLINE_VISIBILITY
size_t __libcpp_mbsrtowcs_l(wchar_t *__dest, const char **__src, size_t __len,
                     mbstate_t *__ps, locale_t __l)
{
    __libcpp_locale_guard __current(__l);
    return mbsrtowcs(__dest, __src, __len, __ps);
}

inline
int __libcpp_snprintf_l(char *__s, size_t __n, locale_t __l, const char *__format, ...) {
    va_list __va;
    va_start(__va, __format);
    __libcpp_locale_guard __current(__l);
    int __res = vsnprintf(__s, __n, __format, __va);
    va_end(__va);
    return __res;
}

inline
int __libcpp_asprintf_l(char **__s, locale_t __l, const char *__format, ...) {
    va_list __va;
    va_start(__va, __format);
    __libcpp_locale_guard __current(__l);
    int __res = vasprintf(__s, __format, __va);
    va_end(__va);
    return __res;
}

inline
int __libcpp_sscanf_l(const char *__s, locale_t __l, const char *__format, ...) {
    va_list __va;
    va_start(__va, __format);
    __libcpp_locale_guard __current(__l);
    int __res = vsscanf(__s, __format, __va);
    va_end(__va);
    return __res;
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_BSD_LOCALE_FALLBACKS_DEFAULTS_H
