///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_exit.h
/// \brief      Close stdout and stderr, and exit
/// \note       Requires tuklib_progname and tuklib_gettext modules
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TUKLIB_EXIT_H
#define TUKLIB_EXIT_H

#include "tuklib_common.h"
TUKLIB_DECLS_BEGIN

#define tuklib_exit TUKLIB_SYMBOL(tuklib_exit)
extern void tuklib_exit(int status, int err_status, int show_error)
		tuklib_attr_noreturn;

TUKLIB_DECLS_END
#endif
