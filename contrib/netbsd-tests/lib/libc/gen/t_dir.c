/* $NetBSD: t_dir.c,v 1.10 2017/01/11 18:15:02 christos Exp $ */

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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

#include <sys/stat.h>
#include <assert.h>
#include <atf-c.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



ATF_TC(seekdir_basic);
ATF_TC_HEAD(seekdir_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check telldir(3) and seekdir(3) "
	    "for correct behavior (PR lib/24324)");
}

ATF_TC_BODY(seekdir_basic, tc)
{
	DIR *dp;
	char *wasname;
	struct dirent *entry;
	long here;

#define	CREAT(x, m)	do {						\
		int _creat_fd;						\
		ATF_REQUIRE_MSG((_creat_fd = creat((x), (m))) != -1,	\
		    "creat(%s, %x) failed: %s", (x), (m),		\
		    strerror(errno));					\
		(void)close(_creat_fd);					\
	} while(0);

	ATF_REQUIRE_MSG(mkdir("t", 0755) == 0,
	    "mkdir failed: %s", strerror(errno));
	CREAT("t/a", 0600);
	CREAT("t/b", 0600);
	CREAT("t/c", 0600);

	dp = opendir("t");
	if ( dp == NULL)
		atf_tc_fail("Could not open temp directory.");

	/* skip two for . and .. */
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    ".", strerror(errno));

	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "..", strerror(errno));

	/* get first entry */
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "first", strerror(errno));

	here = telldir(dp);
	ATF_REQUIRE_MSG(here != -1, "telldir failed: %s", strerror(errno));

	/* get second entry */
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "second", strerror(errno));

	wasname = strdup(entry->d_name);
	if (wasname == NULL)
		atf_tc_fail("cannot allocate memory");

	/* get third entry */
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "third", strerror(errno));

	/* try to return to the position after the first entry */
	seekdir(dp, here);
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "first[1]", strerror(errno));
	if (strcmp(entry->d_name, wasname) != 0)
		atf_tc_fail("1st seekdir found wrong name");

	/* try again, and throw in a telldir() for good measure */
	seekdir(dp, here);
	here = telldir(dp);
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "second[1]", strerror(errno));
	if (strcmp(entry->d_name, wasname) != 0)
		atf_tc_fail("2nd seekdir found wrong name");

	/* One more time, to make sure that telldir() doesn't affect result */
	seekdir(dp, here);
	entry = readdir(dp);
	ATF_REQUIRE_MSG(entry != NULL, "readdir[%s] failed: %s",
	    "third[1]", strerror(errno));

	if (strcmp(entry->d_name, wasname) != 0)
		atf_tc_fail("3rd seekdir found wrong name");

	closedir(dp);
	free(wasname);
}

/* There is no sbrk on AArch64 and RISC-V */
#if !defined(__aarch64__) && !defined(__riscv)
ATF_TC(telldir_leak);
ATF_TC_HEAD(telldir_leak, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Check telldir(3) for memory leakage (PR lib/24324)");
}

ATF_TC_BODY(telldir_leak, tc)
{
	DIR *dp;
	char *memused;
	int i;
	int oktouse = 4096;

	dp = opendir(".");
	if (dp == NULL)
		atf_tc_fail("Could not open current directory");

	(void)telldir(dp);
	memused = sbrk(0);
	closedir(dp);

	for (i = 0; i < 1000; i++) {
		dp = opendir(".");
		if (dp == NULL)
			atf_tc_fail("Could not open current directory");

		(void)telldir(dp);
		closedir(dp);

		if ((char *)sbrk(0) - memused > oktouse) {
			(void)printf("Used %td extra bytes for %d telldir "
			    "calls", ((char *)sbrk(0) - memused), i);
			oktouse = (char *)sbrk(0) - memused;
		}
	}
	if (oktouse > 4096) {
		atf_tc_fail("Failure: leaked %d bytes", oktouse);
	} else {
		(void)printf("OK: used %td bytes\n", (char *)(sbrk(0))-memused);
	}
}
#endif

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, seekdir_basic);
#if !defined(__aarch64__) && !defined(__riscv)
	ATF_TP_ADD_TC(tp, telldir_leak);
#endif

	return atf_no_error();
}
