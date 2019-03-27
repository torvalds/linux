/*	$NetBSD: t_modautoload.c,v 1.6 2017/01/13 21:30:42 christos Exp $	*/

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

ATF_TC(modautoload);
ATF_TC_HEAD(modautoload, tc)
{

	atf_tc_set_md_var(tc, "descr", "tests that kernel module "
	    "autoload works in rump");
}

static void
mountkernfs(void)
{
	bool old_autoload, new_autoload;
	size_t old_len, new_len;
	int error;

	if (!rump_nativeabi_p())
		atf_tc_skip("host kernel modules not supported");

	rump_init();

	if (rump_sys_mkdir("/kern", 0777) == -1)
		atf_tc_fail_errno("mkdir /kern");

	new_autoload = true;
	old_len = sizeof(old_autoload);
	new_len = sizeof(new_autoload);
	error = sysctlbyname("kern.module.autoload",
				  &old_autoload, &old_len,
				  &new_autoload, new_len);
	if (error != 0)
		atf_tc_fail_errno("could not enable module autoload");

	if (rump_sys_mount(MOUNT_KERNFS, "/kern", 0, NULL, 0) == -1)
		atf_tc_fail_errno("could not mount kernfs");
}

/*
 * Why use kernfs here?  It talks to plenty of other parts with the
 * kernel (e.g. vfs_attach() in modcmd), but is still easy to verify
 * it's working correctly.
 */

#define MAGICNUM 1323
ATF_TC_BODY(modautoload, tc)
{
	extern int rumpns_hz;
	char buf[64];
	int fd;

	mountkernfs();
	rumpns_hz = MAGICNUM;
	if ((fd = rump_sys_open("/kern/hz", O_RDONLY)) == -1)
		atf_tc_fail_errno("open /kern/hz");
	if (rump_sys_read(fd, buf, sizeof(buf)) <= 0)
		atf_tc_fail_errno("read");
	ATF_REQUIRE(atoi(buf) == MAGICNUM);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, modautoload);

	return atf_no_error();
}
