#include <config.h>

#if !HAVE_STRERROR
/*
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static const char sccsid[] = "@(#)strerror.c	5.1 (Berkeley) 4/9/89";
#endif /* LIBC_SCCS and not lint */

#include <sys/types.h>

#include <stdio.h>
#include <string.h>

#include "l_stdlib.h"

char *
strerror(
	int errnum
	)
{
	extern int sys_nerr;
	extern char *sys_errlist[];
	static char ebuf[20];

	if ((unsigned int)errnum < sys_nerr)
		return sys_errlist[errnum];
	snprintf(ebuf, sizeof(ebuf), "Unknown error: %d", errnum);

	return ebuf;
}
#else
int strerror_bs;
#endif
