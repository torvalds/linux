///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_common.h
/// \brief      Common definitions for tuklib modules
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TUKLIB_COMMON_H
#define TUKLIB_COMMON_H

// The config file may be replaced by a package-specific file.
// It should include at least stddef.h, inttypes.h, and limits.h.
#include "tuklib_config.h"

// TUKLIB_SYMBOL_PREFIX is prefixed to all symbols exported by
// the tuklib modules. If you use a tuklib module in a library,
// you should use TUKLIB_SYMBOL_PREFIX to make sure that there
// are no symbol conflicts in case someone links your library
// into application that also uses the same tuklib module.
#ifndef TUKLIB_SYMBOL_PREFIX
#	define TUKLIB_SYMBOL_PREFIX
#endif

#define TUKLIB_CAT_X(a, b) a ## b
#define TUKLIB_CAT(a, b) TUKLIB_CAT_X(a, b)

#ifndef TUKLIB_SYMBOL
#	define TUKLIB_SYMBOL(sym) TUKLIB_CAT(TUKLIB_SYMBOL_PREFIX, sym)
#endif

#ifndef TUKLIB_DECLS_BEGIN
#	ifdef __cplusplus
#		define TUKLIB_DECLS_BEGIN extern "C" {
#	else
#		define TUKLIB_DECLS_BEGIN
#	endif
#endif

#ifndef TUKLIB_DECLS_END
#	ifdef __cplusplus
#		define TUKLIB_DECLS_END }
#	else
#		define TUKLIB_DECLS_END
#	endif
#endif

#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#	define TUKLIB_GNUC_REQ(major, minor) \
		((__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)) \
			|| __GNUC__ > (major))
#else
#	define TUKLIB_GNUC_REQ(major, minor) 0
#endif

#if TUKLIB_GNUC_REQ(2, 5)
#	define tuklib_attr_noreturn __attribute__((__noreturn__))
#else
#	define tuklib_attr_noreturn
#endif

#if (defined(_WIN32) && !defined(__CYGWIN__)) \
		|| defined(__OS2__) || defined(__MSDOS__)
#	define TUKLIB_DOSLIKE 1
#endif

#endif
