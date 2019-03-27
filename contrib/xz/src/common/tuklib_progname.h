///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_progname.h
/// \brief      Program name to be displayed in messages
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TUKLIB_PROGNAME_H
#define TUKLIB_PROGNAME_H

#include "tuklib_common.h"
#include <errno.h>

TUKLIB_DECLS_BEGIN

#if HAVE_DECL_PROGRAM_INVOCATION_NAME
#	define progname program_invocation_name
#else
#	define progname TUKLIB_SYMBOL(tuklib_progname)
	extern char *progname;
#endif

#define tuklib_progname_init TUKLIB_SYMBOL(tuklib_progname_init)
extern void tuklib_progname_init(char **argv);

TUKLIB_DECLS_END
#endif
