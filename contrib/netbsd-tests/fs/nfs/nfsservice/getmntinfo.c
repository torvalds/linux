/*	$NetBSD: getmntinfo.c,v 1.1 2010/07/26 15:53:00 pooka Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
#if 0
static char sccsid[] = "@(#)getmntinfo.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: getmntinfo.c,v 1.1 2010/07/26 15:53:00 pooka Exp $");
#endif
#endif /* LIBC_SCCS and not lint */

#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#define getvfsstat(a,b,c) rump_sys_getvfsstat(a,b,c)

/*
 * Return information about mounted filesystems.
 */
int
getmntinfo(mntbufp, flags)
	struct statvfs **mntbufp;
	int flags;
{
	static struct statvfs *mntbuf;
	static int mntsize;
	static size_t bufsize;

	_DIAGASSERT(mntbufp != NULL);

	if (mntsize <= 0 &&
	    (mntsize = getvfsstat(NULL, (size_t)0, MNT_NOWAIT)) == -1)
		return (0);
	if (bufsize > 0 &&
	    (mntsize = getvfsstat(mntbuf, bufsize, flags)) == -1)
		return (0);
	while (bufsize <= mntsize * sizeof(struct statvfs)) {
		if (mntbuf)
			free(mntbuf);
		bufsize = (mntsize + 1) * sizeof(struct statvfs);
		if ((mntbuf = malloc(bufsize)) == NULL)
			return (0);
		if ((mntsize = getvfsstat(mntbuf, bufsize, flags)) == -1)
			return (0);
	}
	*mntbufp = mntbuf;
	return (mntsize);
}
