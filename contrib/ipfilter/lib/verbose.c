/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#if defined(__STDC__)
# include <stdarg.h>
#else
# include <varargs.h>
#endif
#include <stdio.h>

#include "ipf.h"
#include "opts.h"


#if defined(__STDC__)
void	verbose(int level, char *fmt, ...)
#else
void	verbose(level, fmt, va_alist)
	char	*fmt;
	va_dcl
#endif
{
	va_list pvar;

	va_start(pvar, fmt);

	if (opts & OPT_VERBOSE)
		vprintf(fmt, pvar);
	va_end(pvar);
}


#if defined(__STDC__)
void	ipfkverbose(char *fmt, ...)
#else
void	ipfkverbose(fmt, va_alist)
	char	*fmt;
	va_dcl
#endif
{
	va_list pvar;

	va_start(pvar, fmt);

	if (opts & OPT_VERBOSE)
		verbose(0x1fffffff, fmt, pvar);
	va_end(pvar);
}
