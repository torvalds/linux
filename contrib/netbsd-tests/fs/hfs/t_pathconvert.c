/*	$NetBSD: t_pathconvert.c,v 1.6 2017/01/13 21:30:40 christos Exp $	*/

#include <sys/types.h>
#include <sys/mount.h>

#include <atf-c.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <fs/hfs/hfs.h>

#include "h_macros.h"

ATF_TC(colonslash);
ATF_TC_HEAD(colonslash, tc)
{
	atf_tc_set_md_var(tc, "descr", "HFS+ colons/slashes (PR kern/44523)");
	atf_tc_set_md_var(tc, "timeout", "20");
}

#define IMGNAME "colon.hfs"
#define FAKEBLK "/dev/blk"
#define FUNNY_FILENAME "foo:bar"
ATF_TC_BODY(colonslash, tc)
{
	struct hfs_args args;
	int dirfd, fd;
	char thecmd[1024];
	char buf[DIRBLKSIZ];
	struct dirent *dirent;
	int offset, nbytes;
	bool ok = false;

	snprintf(thecmd, sizeof(thecmd), "uudecode %s/colon.hfs.bz2.uue",
	    atf_tc_get_config_var(tc, "srcdir"));
	RZ(system(thecmd));

	snprintf(thecmd, sizeof(thecmd), "bunzip2 " IMGNAME ".bz2");
	RZ(system(thecmd));

	memset(&args, 0, sizeof args);
	args.fspec = __UNCONST(FAKEBLK);
	RZ(rump_init());

	RL(rump_sys_mkdir("/mp", 0777));
	RZ(rump_pub_etfs_register(FAKEBLK, IMGNAME, RUMP_ETFS_BLK));
	RL(rump_sys_mount(MOUNT_HFS, "/mp", 0, &args, sizeof args));

	RL(dirfd = rump_sys_open("/mp", O_RDONLY));

	RL(nbytes = rump_sys_getdents(dirfd, buf, sizeof buf));

	for (offset = 0; offset < nbytes; offset += dirent->d_reclen) {
		dirent = (struct dirent *)(buf + offset);
		if (strchr(dirent->d_name, '/'))
			atf_tc_fail("dirent with slash: %s", dirent->d_name);
		if (0 == strcmp(FUNNY_FILENAME, dirent->d_name))
			ok = true;
	}

	if (!ok)
		atf_tc_fail("no dirent for file: %s", FUNNY_FILENAME);

	RL(rump_sys_close(dirfd));
	RL(fd = rump_sys_open("/mp/" FUNNY_FILENAME, O_RDONLY));
	RL(rump_sys_close(fd));
	RL(rump_sys_unmount("/mp", 0));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, colonslash);
	return 0;
}
