/*	$NetBSD: t_linkat.c,v 1.2 2013/03/17 04:46:06 jmmv Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emmanuel Dreyfus.
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
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_linkat.c,v 1.2 2013/03/17 04:46:06 jmmv Exp $");

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>

#define ODIR "olddir"
#define NDIR "newdir"
#define FILE "olddir/old"
#define BASEFILE "old"
#define RELFILE "../olddir/old"
#define TARGET "newdir/new"
#define BASETARGET "new"
#define LINK "olddir/symlink"
#define BASELINK "symlink"
#define FILEERR "olddir/olderr" 

ATF_TC(linkat_fd);
ATF_TC_HEAD(linkat_fd, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that linkat works with fd");
}
ATF_TC_BODY(linkat_fd, tc)
{
	int ofd, nfd, fd;
	struct stat ost, nst;

	ATF_REQUIRE(mkdir(ODIR, 0755) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) != -1);

	ATF_REQUIRE((ofd = open(ODIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE((nfd = open(NDIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(linkat(ofd, BASEFILE, nfd, BASETARGET, 0) == 0);
	ATF_REQUIRE(close(ofd) == 0);
	ATF_REQUIRE(close(nfd) == 0);

	ATF_REQUIRE(stat(FILE, &ost) == 0);
	ATF_REQUIRE(stat(TARGET, &nst) == 0);
	ATF_REQUIRE(ost.st_ino == nst.st_ino);
}

ATF_TC(linkat_fdcwd);
ATF_TC_HEAD(linkat_fdcwd, tc)
{
	atf_tc_set_md_var(tc, "descr", 
			  "See that linkat works with fd as AT_FDCWD");
}
ATF_TC_BODY(linkat_fdcwd, tc)
{
	int fd;
	struct stat ost, nst;

	ATF_REQUIRE(mkdir(ODIR, 0755) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) != -1);

	ATF_REQUIRE(linkat(AT_FDCWD, FILE, AT_FDCWD, TARGET, 0) == 0);

	ATF_REQUIRE(stat(FILE, &ost) == 0);
	ATF_REQUIRE(stat(TARGET, &nst) == 0);
	ATF_REQUIRE(ost.st_ino == nst.st_ino);
}

ATF_TC(linkat_fdcwderr);
ATF_TC_HEAD(linkat_fdcwderr, tc)
{
	atf_tc_set_md_var(tc, "descr", 
		  "See that linkat fails with fd as AT_FDCWD and bad path");
}
ATF_TC_BODY(linkat_fdcwderr, tc)
{
	int fd;

	ATF_REQUIRE(mkdir(ODIR, 0755) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) != -1);

	ATF_REQUIRE(linkat(AT_FDCWD, FILEERR, AT_FDCWD, TARGET, 0) == -1);
}

ATF_TC(linkat_fderr);
ATF_TC_HEAD(linkat_fderr, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that linkat fails with fd as -1");
}
ATF_TC_BODY(linkat_fderr, tc)
{
	int fd;

	ATF_REQUIRE(mkdir(ODIR, 0755) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) != -1);

	ATF_REQUIRE(linkat(-1, FILE, AT_FDCWD, TARGET, 0) == -1);
	ATF_REQUIRE(linkat(AT_FDCWD, FILE, -1, TARGET, 0) == -1);
	ATF_REQUIRE(linkat(-1, FILE, -1, TARGET, 0) == -1);
}

ATF_TC(linkat_fdlink1);
ATF_TC_HEAD(linkat_fdlink1, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that linkat works on symlink target");
}
ATF_TC_BODY(linkat_fdlink1, tc)
{
	int ofd, nfd, fd;
	struct stat ost, nst;

	ATF_REQUIRE(mkdir(ODIR, 0755) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) != -1);
	ATF_REQUIRE(symlink(RELFILE, LINK) == 0);

	ATF_REQUIRE((ofd = open(ODIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE((nfd = open(NDIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(linkat(ofd, BASELINK, nfd, BASETARGET, 
	    AT_SYMLINK_FOLLOW) == 0);
	ATF_REQUIRE(close(ofd) == 0);
	ATF_REQUIRE(close(nfd) == 0);

	ATF_REQUIRE(lstat(LINK, &ost) == 0);
	ATF_REQUIRE(lstat(TARGET, &nst) == 0);
	ATF_REQUIRE(ost.st_ino != nst.st_ino);

	ATF_REQUIRE(lstat(FILE, &ost) == 0);
	ATF_REQUIRE(lstat(TARGET, &nst) == 0);
	ATF_REQUIRE(ost.st_ino == nst.st_ino);
}


ATF_TC(linkat_fdlink2);
ATF_TC_HEAD(linkat_fdlink2, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that linkat works on symlink source");
}
ATF_TC_BODY(linkat_fdlink2, tc)
{
	int ofd, nfd, fd;
	struct stat ost, nst;

	ATF_REQUIRE(mkdir(ODIR, 0755) == 0);
	ATF_REQUIRE(mkdir(NDIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) != -1);
	ATF_REQUIRE(symlink(RELFILE, LINK) == 0);

	ATF_REQUIRE((ofd = open(ODIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE((nfd = open(NDIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(linkat(ofd, BASELINK, nfd, BASETARGET, 0) == 0);
	ATF_REQUIRE(close(ofd) == 0);
	ATF_REQUIRE(close(nfd) == 0);

	ATF_REQUIRE(lstat(LINK, &ost) == 0);
	ATF_REQUIRE(lstat(TARGET, &nst) == 0);
	ATF_REQUIRE(ost.st_ino == nst.st_ino);

	ATF_REQUIRE(lstat(FILE, &ost) == 0);
	ATF_REQUIRE(lstat(TARGET, &nst) == 0);
	ATF_REQUIRE(ost.st_ino != nst.st_ino);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, linkat_fd);
	ATF_TP_ADD_TC(tp, linkat_fdcwd);
	ATF_TP_ADD_TC(tp, linkat_fdcwderr);
	ATF_TP_ADD_TC(tp, linkat_fderr);
	ATF_TP_ADD_TC(tp, linkat_fdlink1);
	ATF_TP_ADD_TC(tp, linkat_fdlink2);

	return atf_no_error();
}
