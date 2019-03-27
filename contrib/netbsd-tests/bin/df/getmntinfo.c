/*	$NetBSD: getmntinfo.c,v 1.1 2012/03/17 16:33:11 jruoho Exp $	*/
/*
 * Copyright (c) 2007 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>

#include <err.h>
#include <stdlib.h>
#include <string.h>

#define	KB		* 1024
#define	MB		* 1024 KB
#define	GB		* 1024 MB

static struct statvfs *getnewstatvfs(void);
static void other_variants(const struct statvfs *, const int *, int,
    const int *, int);
static void setup_filer(void);
static void setup_ld0g(void);
static void setup_strpct(void);

static struct statvfs *allstatvfs;
static int sftotal, sfused;

struct statvfs *
getnewstatvfs(void)
{

	if (sftotal == sfused) {
		sftotal = sftotal ? sftotal * 2 : 1;
		allstatvfs = realloc(allstatvfs,
		    sftotal * sizeof(struct statvfs));
		if (allstatvfs == NULL)
			err(EXIT_FAILURE, "realloc");
	}

	return (&allstatvfs[sfused++]);
}

void
other_variants(const struct statvfs *tmpl, const int *minfree, int minfreecnt,
    const int *consumed, int consumedcnt)
{
	int64_t total, used;
	struct statvfs *sf;
	int i, j;

	for (i = 0; i < minfreecnt; i++)
		for (j = 0; j < consumedcnt; j++) {
			sf = getnewstatvfs();
			*sf = *tmpl;
			total = (int64_t)(u_long)sf->f_blocks * sf->f_bsize;
			used =  total * consumed[j] / 100;
			sf->f_bfree = (total - used) / sf->f_bsize;
			sf->f_bavail = (total * (100 - minfree[i]) / 100 -
			    used) / (int)sf->f_bsize;
			sf->f_bresvd = sf->f_bfree - sf->f_bavail;
		}
}

/*
 * Parameter taken from:
 * http://mail-index.NetBSD.org/tech-userlevel/2004/03/24/0001.html
 */
void
setup_filer(void)
{
	static const struct statvfs tmpl = {
#define	BSIZE	512
#define	TOTAL	1147ULL GB
#define	USED	132ULL MB
		.f_bsize = BSIZE,
		.f_frsize = BSIZE,
		.f_blocks = TOTAL / BSIZE,
		.f_bfree = (TOTAL - USED) / BSIZE,
		.f_bavail = (TOTAL - USED) / BSIZE,
		.f_bresvd = 0,
		.f_mntfromname = "filer:/",
		.f_mntonname = "/filer",
#undef USED
#undef TOTAL
#undef BSIZE
	};
	static const int minfree[] = { 0, 5, 10, 15, };
	static const int consumed[] = { 0, 20, 60, 95, 100 };

	*getnewstatvfs() = tmpl;
	other_variants(&tmpl, minfree, sizeof(minfree) / sizeof(minfree[0]),
	    consumed, sizeof(consumed) / sizeof(consumed[0]));
}

/*
 * Parameter taken from:
 * http://mail-index.NetBSD.org/current-users/2004/03/01/0038.html
 */
void
setup_ld0g(void)
{
	static const struct statvfs tmpl = {
#define	BSIZE	4096			/* Guess */
#define	TOTAL	1308726116ULL KB
#define	USED	17901268ULL KB
#define	AVAIL	1225388540ULL KB
		.f_bsize = BSIZE,
		.f_frsize = BSIZE,
		.f_blocks = TOTAL / BSIZE,
		.f_bfree = (TOTAL - USED) / BSIZE,
		.f_bavail = AVAIL / BSIZE,
		.f_bresvd = (TOTAL - USED) / BSIZE - AVAIL / BSIZE,
		.f_mntfromname = "/dev/ld0g",
		.f_mntonname = "/anon-root",
#undef AVAIL
#undef USED
#undef TOTAL
#undef BSIZE
	};
	static const int minfree[] = { 0, 5, 10, 15, };
	static const int consumed[] = { 0, 20, 60, 95, 100 };

	*getnewstatvfs() = tmpl;
	other_variants(&tmpl, minfree, sizeof(minfree) / sizeof(minfree[0]),
	    consumed, sizeof(consumed) / sizeof(consumed[0]));
}

/*
 * Test of strpct() with huge number.
 */
void
setup_strpct(void)
{
	static const struct statvfs tmpl = {
#define	BSIZE	4096			/* Guess */
#define	TOTAL	0x4ffffffffULL KB
#define	USED	(TOTAL / 2)
#define	AVAIL	(TOTAL / 2)
		.f_bsize = BSIZE,
		.f_frsize = BSIZE,
		.f_blocks = TOTAL / BSIZE,
		.f_bfree = (TOTAL - USED) / BSIZE,
		.f_bavail = AVAIL / BSIZE,
		.f_bresvd = (TOTAL - USED) / BSIZE - AVAIL / BSIZE,
		.f_mntfromname = "/dev/strpct",
		.f_mntonname = "/strpct",
#undef AVAIL
#undef USED
#undef TOTAL
#undef BSIZE
	};

	*getnewstatvfs() = tmpl;
}

/*
 * Parameter taken from:
 * http://www.netbsd.org/cgi-bin/query-pr-single.pl?number=23600
 */
static void
setup_pr23600(void)
{
	static const struct statvfs tmpl = {
#define	BSIZE	512
#define	TOTAL	20971376ULL
#define	USED	5719864ULL
#define	AVAIL	15251512ULL
		.f_bsize = BSIZE,
		.f_frsize = BSIZE,
		.f_blocks = TOTAL,
		.f_bfree = TOTAL - USED,
		.f_bavail = AVAIL,
		.f_bresvd = TOTAL - USED - AVAIL,
		.f_mntfromname = "/dev/wd0e",
		.f_mntonname = "/mount/windows/C",
#undef AVAIL
#undef USED
#undef TOTAL
#undef BSIZE
	};

	*getnewstatvfs() = tmpl;
}

int
getmntinfo(struct statvfs **mntbuf, int flags)
{

	setup_filer();
	setup_ld0g();
	setup_strpct();
	setup_pr23600();

	*mntbuf = allstatvfs;
	return (sfused);
}
