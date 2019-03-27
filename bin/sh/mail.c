/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)mail.c	8.2 (Berkeley) 5/4/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Routines to check for mail.  (Perhaps make part of main.c?)
 */

#include "shell.h"
#include "mail.h"
#include "var.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>


#define MAXMBOXES 10


static int nmboxes;			/* number of mailboxes */
static time_t mailtime[MAXMBOXES];	/* times of mailboxes */



/*
 * Print appropriate message(s) if mail has arrived.  If the argument is
 * nozero, then the value of MAIL has changed, so we just update the
 * values.
 */

void
chkmail(int silent)
{
	int i;
	char *mpath;
	char *p;
	char *msg;
	struct stackmark smark;
	struct stat statb;

	if (silent)
		nmboxes = 10;
	if (nmboxes == 0)
		return;
	setstackmark(&smark);
	mpath = stsavestr(mpathset()? mpathval() : mailval());
	for (i = 0 ; i < nmboxes ; i++) {
		p = mpath;
		if (*p == '\0')
			break;
		mpath = strchrnul(mpath, ':');
		if (*mpath != '\0') {
			*mpath++ = '\0';
			if (p == mpath - 1)
				continue;
		}
		msg = strchr(p, '%');
		if (msg != NULL)
			*msg++ = '\0';
#ifdef notdef /* this is what the System V shell claims to do (it lies) */
		if (stat(p, &statb) < 0)
			statb.st_mtime = 0;
		if (statb.st_mtime > mailtime[i] && ! silent) {
			out2str(msg? msg : "you have mail");
			out2c('\n');
		}
		mailtime[i] = statb.st_mtime;
#else /* this is what it should do */
		if (stat(p, &statb) < 0)
			statb.st_size = 0;
		if (statb.st_size > mailtime[i] && ! silent) {
			out2str(msg? msg : "you have mail");
			out2c('\n');
		}
		mailtime[i] = statb.st_size;
#endif
	}
	nmboxes = i;
	popstackmark(&smark);
}
