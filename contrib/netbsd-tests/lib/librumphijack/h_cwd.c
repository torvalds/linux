/*      $NetBSD: h_cwd.c,v 1.3 2012/04/17 09:23:21 jruoho Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *prefix;
static size_t prefixlen;
static char buf[1024];
static char pwd[1024];

static const char *
makepath(const char *tail)
{

	strcpy(buf, prefix);
	if (prefix[prefixlen-1] != '/')
		strcat(buf, "/");
	strcat(buf, tail);

	return buf;
}

static void
dochdir(const char *path, const char *errmsg)
{

	if (chdir(path) == -1)
		err(EXIT_FAILURE, "%s", errmsg);
}

static void
dofchdir(const char *path, const char *errmsg)
{
	int fd;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		err(EXIT_FAILURE, "open %s", errmsg);
	if (fchdir(fd) == -1)
		err(EXIT_FAILURE, "fchdir %s", errmsg);
	close(fd);
}
static void (*thechdir)(const char *, const char *);

static void
simple(void)
{

	thechdir(prefix, "chdir1");
	if (getcwd(pwd, sizeof(pwd)) == NULL)
		err(EXIT_FAILURE, "getcwd1");
	if (strcmp(pwd, prefix) != 0)
		errx(EXIT_FAILURE, "strcmp1");

	if (mkdir("dir", 0777) == -1)
		err(EXIT_FAILURE, "mkdir2");
	thechdir("dir", "chdir2");
	if (getcwd(pwd, sizeof(pwd)) == NULL)
		err(EXIT_FAILURE, "getcwd2");
	if (strcmp(pwd, makepath("dir")) != 0)
		errx(EXIT_FAILURE, "strcmp2");

	if (mkdir("dir", 0777) == -1)
		err(EXIT_FAILURE, "mkdir3");
	thechdir("dir", "chdir3");
	if (getcwd(pwd, sizeof(pwd)) == NULL)
		err(EXIT_FAILURE, "getcwd3");
	if (strcmp(pwd, makepath("dir/dir")) != 0)
		errx(EXIT_FAILURE, "strcmp3");

	thechdir("..", "chdir4");
	if (getcwd(pwd, sizeof(pwd)) == NULL)
		err(EXIT_FAILURE, "getcwd4");
	if (strcmp(pwd, makepath("dir")) != 0)
		errx(EXIT_FAILURE, "strcmp4");


	thechdir("../../../../../../..", "chdir5");
	if (getcwd(pwd, sizeof(pwd)) == NULL)
		err(EXIT_FAILURE, "getcwd5");
	if (strcmp(pwd, prefix) != 0)
		errx(EXIT_FAILURE, "strcmp5");

	thechdir("/", "chdir6");
	if (getcwd(pwd, sizeof(pwd)) == NULL)
		err(EXIT_FAILURE, "getcwd6");
	if (strcmp(pwd, "/") != 0)
		errx(EXIT_FAILURE, "strcmp6");
}

static void
symlinktest(void)
{

	thechdir(prefix, "chdir1");
	if (mkdir("adir", 0777) == -1)
		err(EXIT_FAILURE, "mkdir1");
	if (mkdir("anotherdir", 0777) == -1)
		err(EXIT_FAILURE, "mkdir2");

	if (symlink("/adir", "anotherdir/lincthesink") == -1)
		err(EXIT_FAILURE, "symlink");

	thechdir("anotherdir/lincthesink", "chdir2");
	if (getcwd(pwd, sizeof(pwd)) == NULL)
		err(EXIT_FAILURE, "getcwd");
	if (strcmp(pwd, makepath("adir")) != 0)
		errx(EXIT_FAILURE, "strcmp");
}

int
main(int argc, char *argv[])
{

	if (argc != 4)
		errx(1, "usage");

	prefix = argv[1];
	prefixlen = strlen(argv[1]);

	if (strcmp(argv[3], "chdir") == 0)
		thechdir = dochdir;
	else if (strcmp(argv[3], "fchdir") == 0)
		thechdir = dofchdir;
	else
		errx(EXIT_FAILURE, "invalid chdir type");

	if (strcmp(argv[2], "simple") == 0)
		simple();
	if (strcmp(argv[2], "symlink") == 0)
		symlinktest();

	return EXIT_SUCCESS;
}
