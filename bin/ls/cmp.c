/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Michael Fischbein.
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

#if 0
#ifndef lint
static char sccsid[] = "@(#)cmp.c	8.1 (Berkeley) 5/31/93";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


#include <sys/types.h>
#include <sys/stat.h>

#include <fts.h>
#include <string.h>

#include "ls.h"
#include "extern.h"

int
namecmp(const FTSENT *a, const FTSENT *b)
{

	return (strcoll(a->fts_name, b->fts_name));
}

int
revnamecmp(const FTSENT *a, const FTSENT *b)
{

	return (strcoll(b->fts_name, a->fts_name));
}

int
modcmp(const FTSENT *a, const FTSENT *b)
{

	if (b->fts_statp->st_mtim.tv_sec >
	    a->fts_statp->st_mtim.tv_sec)
		return (1);
	if (b->fts_statp->st_mtim.tv_sec <
	    a->fts_statp->st_mtim.tv_sec)
		return (-1);
	if (b->fts_statp->st_mtim.tv_nsec >
	    a->fts_statp->st_mtim.tv_nsec)
		return (1);
	if (b->fts_statp->st_mtim.tv_nsec <
	    a->fts_statp->st_mtim.tv_nsec)
		return (-1);
	if (f_samesort)
		return (strcoll(b->fts_name, a->fts_name));
	else
		return (strcoll(a->fts_name, b->fts_name));
}

int
revmodcmp(const FTSENT *a, const FTSENT *b)
{

	return (modcmp(b, a));
}

int
acccmp(const FTSENT *a, const FTSENT *b)
{

	if (b->fts_statp->st_atim.tv_sec >
	    a->fts_statp->st_atim.tv_sec)
		return (1);
	if (b->fts_statp->st_atim.tv_sec <
	    a->fts_statp->st_atim.tv_sec)
		return (-1);
	if (b->fts_statp->st_atim.tv_nsec >
	    a->fts_statp->st_atim.tv_nsec)
		return (1);
	if (b->fts_statp->st_atim.tv_nsec <
	    a->fts_statp->st_atim.tv_nsec)
		return (-1);
	if (f_samesort)
		return (strcoll(b->fts_name, a->fts_name));
	else
		return (strcoll(a->fts_name, b->fts_name));
}

int
revacccmp(const FTSENT *a, const FTSENT *b)
{

	return (acccmp(b, a));
}

int
birthcmp(const FTSENT *a, const FTSENT *b)
{

	if (b->fts_statp->st_birthtim.tv_sec >
	    a->fts_statp->st_birthtim.tv_sec)
		return (1);
	if (b->fts_statp->st_birthtim.tv_sec <
	    a->fts_statp->st_birthtim.tv_sec)
		return (-1);
	if (b->fts_statp->st_birthtim.tv_nsec >
	    a->fts_statp->st_birthtim.tv_nsec)
		return (1);
	if (b->fts_statp->st_birthtim.tv_nsec <
	    a->fts_statp->st_birthtim.tv_nsec)
		return (-1);
	if (f_samesort)
		return (strcoll(b->fts_name, a->fts_name));
	else
		return (strcoll(a->fts_name, b->fts_name));
}

int
revbirthcmp(const FTSENT *a, const FTSENT *b)
{

	return (birthcmp(b, a));
}

int
statcmp(const FTSENT *a, const FTSENT *b)
{

	if (b->fts_statp->st_ctim.tv_sec >
	    a->fts_statp->st_ctim.tv_sec)
		return (1);
	if (b->fts_statp->st_ctim.tv_sec <
	    a->fts_statp->st_ctim.tv_sec)
		return (-1);
	if (b->fts_statp->st_ctim.tv_nsec >
	    a->fts_statp->st_ctim.tv_nsec)
		return (1);
	if (b->fts_statp->st_ctim.tv_nsec <
	    a->fts_statp->st_ctim.tv_nsec)
		return (-1);
	if (f_samesort)
		return (strcoll(b->fts_name, a->fts_name));
	else
		return (strcoll(a->fts_name, b->fts_name));
}

int
revstatcmp(const FTSENT *a, const FTSENT *b)
{

	return (statcmp(b, a));
}

int
sizecmp(const FTSENT *a, const FTSENT *b)
{

	if (b->fts_statp->st_size > a->fts_statp->st_size)
		return (1);
	if (b->fts_statp->st_size < a->fts_statp->st_size)
		return (-1);
	return (strcoll(a->fts_name, b->fts_name));
}

int
revsizecmp(const FTSENT *a, const FTSENT *b)
{

	return (sizecmp(b, a));
}
