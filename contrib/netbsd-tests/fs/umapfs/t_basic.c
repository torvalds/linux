/*	$NetBSD: t_basic.c,v 1.5 2017/01/13 21:30:40 christos Exp $	*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/mount.h>

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
#include <rump/rumpvfs_if_pub.h>

#include <fs/tmpfs/tmpfs_args.h>
#include <miscfs/umapfs/umap.h>

#include "h_macros.h"

ATF_TC(basic);
ATF_TC_HEAD(basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "basic umapfs mapping");
}

static void
xtouch(const char *path)
{
	int fd;

	fd = rump_sys_open(path, O_CREAT | O_RDWR, 0777);
	if (fd == -1)
		atf_tc_fail_errno("create %s", path);
	rump_sys_close(fd);
}

static void
xchown(const char *path, uid_t uid, gid_t gid)
{

	if (rump_sys_chown(path, uid, gid) == -1)
		atf_tc_fail_errno("chown %s failed", path);
}

static void
testuidgid(const char *path, uid_t uid, gid_t gid)
{
	struct stat sb;

	if (rump_sys_stat(path, &sb) == -1)
		atf_tc_fail_errno("stat %s", path);
	if (uid != (uid_t)-1) {
		if (sb.st_uid != uid)
			atf_tc_fail("%s: expected uid %d, got %d",
			    path, uid, sb.st_uid);
	}
	if (gid != (gid_t)-1) {
		if (sb.st_gid != gid)
			atf_tc_fail("%s: expected gid %d, got %d",
			    path, gid, sb.st_gid);
	}
}

ATF_TC_BODY(basic, tc)
{
	struct umap_args umargs;
	struct tmpfs_args targs;
	u_long umaps[2][2];
	u_long gmaps[2][2];

	rump_init();
	if (rump_sys_mkdir("/td1", 0777) == -1)
		atf_tc_fail_errno("mp1");
	if (rump_sys_mkdir("/td2", 0777) == -1)
		atf_tc_fail_errno("mp1");

	/* use tmpfs because rumpfs doesn't support ownership */
	memset(&targs, 0, sizeof(targs));
	targs.ta_version = TMPFS_ARGS_VERSION;
	targs.ta_root_mode = 0777;
	if (rump_sys_mount(MOUNT_TMPFS, "/td1", 0, &targs, sizeof(targs)) == -1)
		atf_tc_fail_errno("could not mount tmpfs td1");

	memset(&umargs, 0, sizeof(umargs));

	/*
	 * Map td1 uid 555 to td2 uid 777 (yes, IMHO the umapfs
	 * mapping format is counter-intuitive).
	 */
	umaps[0][0] = 777;
	umaps[0][1] = 555;
	umaps[1][0] = 0;
	umaps[1][1] = 0;
	gmaps[0][0] = 4321;
	gmaps[0][1] = 1234;
	gmaps[1][0] = 0;
	gmaps[1][1] = 0;

	umargs.umap_target = __UNCONST("/td1");
	umargs.nentries = 2;
	umargs.gnentries = 2;
	umargs.mapdata = umaps;
	umargs.gmapdata = gmaps;

	if (rump_sys_mount(MOUNT_UMAP, "/td2", 0, &umargs,sizeof(umargs)) == -1)
		atf_tc_fail_errno("could not mount umapfs");

	xtouch("/td1/noch");
	testuidgid("/td1/noch", 0, 0);
	testuidgid("/td2/noch", 0, 0);

	xtouch("/td1/nomap");
	xchown("/td1/nomap", 1, 2);
	testuidgid("/td1/nomap", 1, 2);
	testuidgid("/td2/nomap", -1, -1);

	xtouch("/td1/forwmap");
	xchown("/td1/forwmap", 555, 1234);
	testuidgid("/td1/forwmap", 555, 1234);
	testuidgid("/td2/forwmap", 777, 4321);

	/*
	 * this *CANNOT* be correct???
	 */
	xtouch("/td1/revmap");
	/*
	 * should be 777 / 4321 (?), but makes first test fail since
	 * it gets 777 / 4321, i.e. unmapped results.
	 */
	xchown("/td2/revmap", 555, 1234);
	testuidgid("/td1/revmap", 555, 1234);
	testuidgid("/td2/revmap", 777, 4321);
	
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, basic);
	return 0; /*XXX?*/
}
