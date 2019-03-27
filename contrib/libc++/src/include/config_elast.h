//===----------------------- config_elast.h -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_CONFIG_ELAST
#define _LIBCPP_CONFIG_ELAST

#include <__config>

#if defined(_LIBCPP_MSVCRT_LIKE)
#include <stdlib.h>
#else
#include <errno.h>
#endif

#if defined(ELAST)
#define _LIBCPP_ELAST ELAST
#elif defined(_NEWLIB_VERSION)
#define _LIBCPP_ELAST __ELASTERROR
#elif defined(__Fuchsia__)
// No _LIBCPP_ELAST needed on Fuchsia
#elif defined(__linux__) || defined(_LIBCPP_HAS_MUSL_LIBC)
#define _LIBCPP_ELAST 4095
#elif defined(__APPLE__)
// No _LIBCPP_ELAST needed on Apple
#elif defined(__sun__)
#define _LIBCPP_ELAST ESTALE
#elif defined(_LIBCPP_MSVCRT_LIKE)
#define _LIBCPP_ELAST (_sys_nerr - 1)
#else
// Warn here so that the person doing the libcxx port has an easier time:
#warning ELAST for this platform not yet implemented
#endif

#endif // _LIBCPP_CONFIG_ELAST
