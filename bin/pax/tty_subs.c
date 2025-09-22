/*	$OpenBSD: tty_subs.c,v 1.18 2023/06/26 16:58:50 millert Exp $	*/
/*	$NetBSD: tty_subs.c,v 1.5 1995/03/21 09:07:52 cgd Exp $	*/

/*-
 * Copyright (c) 1992 Keith Muller.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pax.h"
#include "extern.h"

/*
 * routines that deal with I/O to and from the user
 */

#define DEVTTY		"/dev/tty"	/* device for interactive i/o */
static FILE *ttyoutf = NULL;		/* output pointing at control tty */
static FILE *ttyinf = NULL;		/* input pointing at control tty */

/*
 * tty_init()
 *	try to open the controlling terminal (if any) for this process. if the
 *	open fails, future ops that require user input will get an EOF
 */

int
tty_init(void)
{
	int ttyfd;

	if ((ttyfd = open(DEVTTY, O_RDWR | O_CLOEXEC)) >= 0) {
		if ((ttyoutf = fdopen(ttyfd, "w")) != NULL) {
			if ((ttyinf = fdopen(ttyfd, "r")) != NULL)
				return(0);
			(void)fclose(ttyoutf);
		}
		(void)close(ttyfd);
	}

	if (iflag) {
		paxwarn(1, "Fatal error, cannot open %s", DEVTTY);
		return(-1);
	}
	return(0);
}

/*
 * tty_prnt()
 *	print a message using the specified format to the controlling tty
 *	if there is no controlling terminal, just return.
 */

void
tty_prnt(const char *fmt, ...)
{
	va_list ap;
	char buf[8192];

	if (ttyoutf == NULL)
		return;
	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	safe_print(buf, ttyoutf);
	(void)fflush(ttyoutf);
}

/*
 * tty_read()
 *	read a string from the controlling terminal if it is open into the
 *	supplied buffer
 * Return:
 *	0 if data was read, -1 otherwise.
 */

int
tty_read(char *str, int len)
{
	if (ttyinf == NULL || fgets(str, len, ttyinf) == NULL)
		return(-1);

	/*
	 * strip off that trailing newline
	 */
	str[strcspn(str, "\n")] = '\0';
	return(0);
}

/*
 * paxwarn()
 *	write a warning message to stderr. if "set" the exit value of pax
 *	will be non-zero.
 */

void
paxwarn(int set, const char *fmt, ...)
{
	va_list ap;
	char buf[8192];

	if (set)
		exit_val = 1;
	/*
	 * when vflag we better ship out an extra \n to get this message on a
	 * line by itself
	 */
	if (vflag && vfpart) {
		(void)fflush(listf);
		(void)fputc('\n', stderr);
		vfpart = 0;
	}
	(void)fprintf(stderr, "%s: ", argv0);
	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	safe_print(buf, stderr);
	(void)fputc('\n', stderr);
}

/*
 * syswarn()
 *	write a warning message to stderr. if "set" the exit value of pax
 *	will be non-zero.
 */

void
syswarn(int set, int errnum, const char *fmt, ...)
{
	va_list ap;
	char buf[8192];

	if (set)
		exit_val = 1;
	/*
	 * when vflag we better ship out an extra \n to get this message on a
	 * line by itself
	 */
	if (vflag && vfpart) {
		(void)fflush(listf);
		(void)fputc('\n', stderr);
		vfpart = 0;
	}
	(void)fprintf(stderr, "%s: ", argv0);
	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	safe_print(buf, stderr);

	/*
	 * format and print the errno
	 */
	if (errnum > 0)
		(void)fprintf(stderr, ": %s", strerror(errnum));
	(void)fputc('\n', stderr);
}
