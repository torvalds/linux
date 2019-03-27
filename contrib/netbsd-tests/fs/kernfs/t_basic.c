/*	$NetBSD: t_basic.c,v 1.4 2017/01/13 21:30:40 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/module.h>
#include <sys/dirent.h>
#include <sys/sysctl.h>

#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <miscfs/kernfs/kernfs.h>

#include "h_macros.h"

ATF_TC(getdents);
ATF_TC_HEAD(getdents, tc)
{

	atf_tc_set_md_var(tc, "descr", "kernfs directory contains files");
}

static void
mountkernfs(void)
{

	rump_init();

	if (rump_sys_mkdir("/kern", 0777) == -1)
		atf_tc_fail_errno("mkdir /kern");
	if (rump_sys_mount(MOUNT_KERNFS, "/kern", 0, NULL, 0) == -1)
		atf_tc_fail_errno("could not mount kernfs");
}

ATF_TC_BODY(getdents, tc)
{
	struct dirent *dent;
	char buf[8192];
	int dfd;

	mountkernfs();

	if ((dfd = rump_sys_open("/kern", O_RDONLY)) == -1)
		atf_tc_fail_errno("can't open directory");
	if (rump_sys_getdents(dfd, buf, sizeof(buf)) == -1)
		atf_tc_fail_errno("getdents");

	/*
	 * Check that we get the first three values (., .., boottime).
	 * Make more complete by autogenerating list from kernfs_vnops.c?
	 */
	dent = (void *)buf;
	ATF_REQUIRE_STREQ(dent->d_name, ".");
	dent = _DIRENT_NEXT(dent);
	ATF_REQUIRE_STREQ(dent->d_name, "..");
	dent = _DIRENT_NEXT(dent);
	ATF_REQUIRE_STREQ(dent->d_name, "boottime");

	/* done */
}

ATF_TC(hostname);
ATF_TC_HEAD(hostname, tc)
{

	atf_tc_set_md_var(tc, "descr", "/kern/hostname changes hostname");
}

static char *
getthehost(void)
{
	static char buf[8192];
	int mib[2];
	size_t blen;

	mib[0] = CTL_KERN;
	mib[1] = KERN_HOSTNAME;
	blen = sizeof(buf);
	if (rump_sys___sysctl(mib, 2, buf, &blen, NULL, 0) == -1)
		atf_tc_fail_errno("sysctl gethostname");

	return buf;
}

#define NEWHOSTNAME "turboton roos-berg"
ATF_TC_BODY(hostname, tc)
{
	char buf[8192];
	char *shost, *p;
	int fd;

	mountkernfs();
	if ((fd = rump_sys_open("/kern/hostname", O_RDWR)) == -1)
		atf_tc_fail_errno("open hostname");

	/* check initial match */
	shost = getthehost();
	buf[0] = '\0';
	if (rump_sys_read(fd, buf, sizeof(buf)) == -1)
		atf_tc_fail_errno("read hostname");
	p = strchr(buf, '\n');
	if (p)
		 *p = '\0';
	ATF_REQUIRE_STREQ_MSG(buf, shost, "initial hostname mismatch");

	/* check changing hostname works */
	if (rump_sys_pwrite(fd, NEWHOSTNAME, strlen(NEWHOSTNAME), 0)
	    != strlen(NEWHOSTNAME)) {
		atf_tc_fail_errno("write new hostname");
	}

	shost = getthehost();
	ATF_REQUIRE_STREQ_MSG(NEWHOSTNAME, shost, "modified hostname mismatch");

	/* done */
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, hostname);
	ATF_TP_ADD_TC(tp, getdents);

	return atf_no_error();
}
