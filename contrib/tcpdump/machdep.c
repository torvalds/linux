/*
 * Copyright (c) 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * XXX - all we need, on platforms other than DEC OSF/1 (a/k/a Digital UNIX,
 * a/k/a Tru64 UNIX), is "size_t", which is a standard C type; what do we
 * need to do to get it defined?  This is clearly wrong, as we shouldn't
 * have to include UNIX or Windows system header files to get it.
 */
#include <netdissect-stdinc.h>

#ifndef HAVE___ATTRIBUTE__
#define __attribute__(x)
#endif /* HAVE___ATTRIBUTE__ */

#ifdef __osf__
#include <sys/sysinfo.h>
#include <sys/proc.h>

#if !defined(HAVE_SNPRINTF)
int snprintf(char *, size_t, const char *, ...)
#ifdef __ATTRIBUTE___FORMAT_OK
     __attribute__((format(printf, 3, 4)))
#endif /* __ATTRIBUTE___FORMAT_OK */
     ;
#endif /* !defined(HAVE_SNPRINTF) */
#endif /* __osf__ */

#include "machdep.h"

/*
 * On platforms where the CPU doesn't support unaligned loads, force
 * unaligned accesses to abort with SIGBUS, rather than being fixed
 * up (slowly) by the OS kernel; on those platforms, misaligned accesses
 * are bugs, and we want tcpdump to crash so that the bugs are reported.
 *
 * The only OS on which this is necessary is DEC OSF/1^W^WDigital
 * UNIX^W^WTru64 UNIX.
 */
int
abort_on_misalignment(char *ebuf _U_, size_t ebufsiz _U_)
{
#ifdef __osf__
	static int buf[2] = { SSIN_UACPROC, UAC_SIGBUS };

	if (setsysinfo(SSI_NVPAIRS, (caddr_t)buf, 1, 0, 0) < 0) {
		(void)snprintf(ebuf, ebufsiz, "setsysinfo: errno %d", errno);
		return (-1);
	}
#endif
	return (0);
}
