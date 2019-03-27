/*-
 * Copyright (c) 2011 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>

#include "libelftc.h"

#include "_libelftc.h"

ELFTC_VCSID("$Id$");

/*
 * Determine the field name for the timestamp fields inside a 'struct
 * stat'.
 */

#if	defined(__FreeBSD__) || defined(__NetBSD__)
#define	ATIME	st_atimespec
#define	MTIME	st_mtimespec
#define	LIBELFTC_HAVE_UTIMES	1
#endif

#if	defined(__DragonFly__) || defined(__linux__) || defined(__OpenBSD__)
#define	ATIME	st_atim
#define	MTIME	st_mtim
#define	LIBELFTC_HAVE_UTIMES	1
#endif

#if	LIBELFTC_HAVE_UTIMES
#include <sys/time.h>
#else
#include <utime.h>
#endif

int
elftc_set_timestamps(const char *fn, struct stat *sb)
{
#if	LIBELFTC_HAVE_UTIMES
	/*
	 * The BSD utimes() system call offers timestamps
	 * 1-microsecond granularity.
	 */
	struct timeval tv[2];

	tv[0].tv_sec = sb->ATIME.tv_sec;
	tv[0].tv_usec = sb->ATIME.tv_nsec / 1000;
	tv[1].tv_sec = sb->MTIME.tv_sec;
	tv[1].tv_usec = sb->MTIME.tv_nsec / 1000;

	return (utimes(fn, tv));
#else
	/*
	 * On OSes without utimes(), fall back to the POSIX utime()
	 * call, which offers 1-second granularity.
	 */
	struct utimbuf utb;

	utb.actime = sb->st_atime;
	utb.modtime = sb->st_mtime;
	return (utime(fn, &utb));
#endif
}
