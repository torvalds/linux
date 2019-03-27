/*	$NetBSD: t_glob.c,v 1.5 2017/01/14 20:47:41 christos Exp $	*/
/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_glob.c,v 1.5 2017/01/14 20:47:41 christos Exp $");

#include <atf-c.h>

#include <sys/param.h>
#include <sys/stat.h>

#include <dirent.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "h_macros.h"


#ifdef DEBUG
#define DPRINTF(a) printf a
#else
#define DPRINTF(a)
#endif

struct gl_file {
	const char *name;
	int dir;
};

static struct gl_file a[] = {
	{ "1", 0 },
	{ "b", 1 },
	{ "3", 0 },
	{ "4", 0 },
};

static struct gl_file b[] = {
	{ "x", 0 },
	{ "y", 0 },
	{ "z", 0 },
	{ "w", 0 },
};

struct gl_dir {
	const char *name;	/* directory name */
	const struct gl_file *dir;
	size_t len, pos;
};

static struct gl_dir d[] = {
	{ "a", a, __arraycount(a), 0 },
	{ "a/b", b, __arraycount(b), 0 },
};

#ifdef GLOB_STAR
static const char *glob_star[] = {
    "a/1", "a/3", "a/4", "a/b", "a/b/w", "a/b/x", "a/b/y", "a/b/z",
};
#endif

static const char *glob_star_not[] = {
	"a/1", "a/3", "a/4", "a/b",
};

static void
trim(char *buf, size_t len, const char *name)
{
	char *path = buf, *epath = buf + len;
	while (path < epath && (*path++ = *name++) != '\0')
		continue;
	path--;
	while (path > buf && *--path == '/')
		*path = '\0';
}

static void *
gl_opendir(const char *dir)
{
	size_t i;
	char buf[MAXPATHLEN];
	trim(buf, sizeof(buf), dir);

	for (i = 0; i < __arraycount(d); i++)
		if (strcmp(buf, d[i].name) == 0) {
			DPRINTF(("opendir %s %zu\n", buf, i));
			return &d[i];
		}
	errno = ENOENT;
	return NULL;
}

static struct dirent *
gl_readdir(void *v)
{
	static struct dirent dir;
	struct gl_dir *dd = v;
	if (dd->pos < dd->len) {
		const struct gl_file *f = &dd->dir[dd->pos++];
		strcpy(dir.d_name, f->name);
		dir.d_namlen = strlen(f->name);
		dir.d_ino = dd->pos;
		dir.d_type = f->dir ? DT_DIR : DT_REG;
		DPRINTF(("readdir %s %d\n", dir.d_name, dir.d_type));
#ifdef __FreeBSD__
		dir.d_reclen = -1; /* Does not have _DIRENT_RECLEN */
#else
		dir.d_reclen = _DIRENT_RECLEN(&dir, dir.d_namlen);
#endif
		return &dir;
	}
	return NULL;
}

static int
gl_stat(const char *name , __gl_stat_t *st)
{
	char buf[MAXPATHLEN];
	trim(buf, sizeof(buf), name);
	memset(st, 0, sizeof(*st));

	if (strcmp(buf, "a") == 0 || strcmp(buf, "a/b") == 0) {
		st->st_mode |= S_IFDIR;
		return 0;
	}

	if (buf[0] == 'a' && buf[1] == '/') {
		struct gl_file *f;
		size_t offs, count;

		if (buf[2] == 'b' && buf[3] == '/') {
			offs = 4;
			count = __arraycount(b);
			f = b;
		} else {
			offs = 2;
			count = __arraycount(a);
			f = a;
		}
		
		for (size_t i = 0; i < count; i++)
			if (strcmp(f[i].name, buf + offs) == 0)
				return 0;
	}
	DPRINTF(("stat %s %d\n", buf, st->st_mode));
	errno = ENOENT;
	return -1;
}

static int
gl_lstat(const char *name , __gl_stat_t *st)
{
	return gl_stat(name, st);
}

static void
gl_closedir(void *v)
{
	struct gl_dir *dd = v;
	dd->pos = 0;
	DPRINTF(("closedir %p\n", dd));
}

static void
run(const char *p, int flags, const char **res, size_t len)
{
	glob_t gl;
	size_t i;

	memset(&gl, 0, sizeof(gl));
	gl.gl_opendir = gl_opendir;
	gl.gl_readdir = gl_readdir;
	gl.gl_closedir = gl_closedir;
	gl.gl_stat = gl_stat;
	gl.gl_lstat = gl_lstat;

	RZ(glob(p, GLOB_ALTDIRFUNC | flags, NULL, &gl));

	for (i = 0; i < gl.gl_pathc; i++)
		DPRINTF(("%s\n", gl.gl_pathv[i]));

	ATF_CHECK(len == gl.gl_pathc);
	for (i = 0; i < gl.gl_pathc; i++)
		ATF_CHECK_STREQ(gl.gl_pathv[i], res[i]);

	globfree(&gl);
}


#ifdef GLOB_STAR
ATF_TC(glob_star);
ATF_TC_HEAD(glob_star, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test glob(3) ** with GLOB_STAR");
}

ATF_TC_BODY(glob_star, tc)
{
	run("a/**", GLOB_STAR, glob_star, __arraycount(glob_star));
}
#endif

ATF_TC(glob_star_not);
ATF_TC_HEAD(glob_star_not, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test glob(3) ** without GLOB_STAR");
}


ATF_TC_BODY(glob_star_not, tc)
{
	run("a/**", 0, glob_star_not, __arraycount(glob_star_not));
}

#if 0
ATF_TC(glob_nocheck);
ATF_TC_HEAD(glob_nocheck, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test glob(3) pattern with backslash and GLOB_NOCHECK");
}


ATF_TC_BODY(glob_nocheck, tc)
{
	static const char pattern[] = { 'f', 'o', 'o', '\\', ';', 'b', 'a',
	    'r', '\0' };
	static const char *glob_nocheck[] = {
	    pattern
	};
	run(pattern, GLOB_NOCHECK, glob_nocheck, __arraycount(glob_nocheck));
}
#endif

ATF_TP_ADD_TCS(tp)
{
#ifdef GLOB_STAR
	ATF_TP_ADD_TC(tp, glob_star);
#endif
	ATF_TP_ADD_TC(tp, glob_star_not);
/*
 * Remove this test for now - the GLOB_NOCHECK return value has been
 * re-defined to return a modified pattern in revision 1.33 of glob.c
 *
 *	ATF_TP_ADD_TC(tp, glob_nocheck);
 */

	return atf_no_error();
}
