/*	$NetBSD: t_io.c,v 1.2 2017/01/13 21:30:40 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/socket.h>

#include <assert.h>
#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <puffs.h>
#include <puffsdump.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"
#include "../common/h_fsmacros.h"

#define MAKEOPTS(...) \
    char *theopts[] = {NULL, "-s", __VA_ARGS__, "dtfs", "n/a", NULL}

ATF_TC(nocache);
ATF_TC_HEAD(nocache, tc)
{

	atf_tc_set_md_var(tc, "descr", "tests large i/o without page cache");
}

ATF_TC_BODY(nocache, tc)
{
	MAKEOPTS("-o", "nopagecache");
	char data[1024*1024];
	void *args;
	int fd;

	FSTEST_CONSTRUCTOR_FSPRIV(tc, puffs, args, theopts);
	FSTEST_ENTER();

	RL(fd = rump_sys_open("afile", O_CREAT | O_RDWR, 0755));
	RL(rump_sys_write(fd, data, sizeof(data)));
	rump_sys_close(fd);

	FSTEST_EXIT();
	FSTEST_DESTRUCTOR(tc, puffs, args);
}


ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, nocache);

	return atf_no_error();
}
