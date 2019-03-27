/*	$NetBSD: t_etfs.c,v 1.11 2017/01/13 21:30:43 christos Exp $	*/

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
#include <sys/mount.h>
#include <sys/sysctl.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <atf-c.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "h_macros.h"

ATF_TC(reregister_reg);
ATF_TC_HEAD(reregister_reg, tc)
{

	atf_tc_set_md_var(tc, "descr", "Tests register/unregister/register "
	    "for a regular file");
}

#define TESTSTR1 "hi, it's me again!"
#define TESTSTR1SZ (sizeof(TESTSTR1)-1)

#define TESTSTR2 "what about the old vulcan proverb?"
#define TESTSTR2SZ (sizeof(TESTSTR2)-1)

#define TESTPATH1 "/trip/to/the/moon"
#define TESTPATH2 "/but/not/the/dark/size"
ATF_TC_BODY(reregister_reg, tc)
{
	char buf[1024];
	int localfd, etcfd;
	ssize_t n;
	int tfd;

	etcfd = open("/etc/passwd", O_RDONLY);
	ATF_REQUIRE(etcfd != -1);

	localfd = open("./testfile", O_RDWR | O_CREAT, 0666);
	ATF_REQUIRE(localfd != -1);

	ATF_REQUIRE_EQ(write(localfd, TESTSTR1, TESTSTR1SZ), TESTSTR1SZ);
	/* testfile now contains test string */

	rump_init();

	ATF_REQUIRE_EQ(rump_pub_etfs_register(TESTPATH1, "/etc/passwd",
	    RUMP_ETFS_REG), 0);
	tfd = rump_sys_open(TESTPATH1, O_RDONLY);
	ATF_REQUIRE(tfd != -1);
	ATF_REQUIRE(rump_sys_read(tfd, buf, sizeof(buf)) > 0);
	rump_sys_close(tfd);
	rump_pub_etfs_remove(TESTPATH1);

	ATF_REQUIRE_EQ(rump_pub_etfs_register(TESTPATH2, "./testfile",
	    RUMP_ETFS_REG), 0);
	tfd = rump_sys_open(TESTPATH2, O_RDWR);
	ATF_REQUIRE(tfd != -1);
	memset(buf, 0, sizeof(buf));
	ATF_REQUIRE((n = rump_sys_read(tfd, buf, sizeof(buf))) > 0);

	/* check that we have what we expected */
	ATF_REQUIRE_STREQ(buf, TESTSTR1);

	/* ... while here, check that writing works too */
	ATF_REQUIRE_EQ(rump_sys_lseek(tfd, 0, SEEK_SET), 0);
	ATF_REQUIRE(TESTSTR1SZ <= TESTSTR2SZ);
	ATF_REQUIRE_EQ(rump_sys_write(tfd, TESTSTR2, TESTSTR2SZ), TESTSTR2SZ);

	memset(buf, 0, sizeof(buf));
	ATF_REQUIRE_EQ(lseek(localfd, 0, SEEK_SET), 0);
	ATF_REQUIRE(read(localfd, buf, sizeof(buf)) > 0);
	ATF_REQUIRE_STREQ(buf, TESTSTR2);
	close(etcfd);
	close(localfd);
}

ATF_TC(reregister_blk);
ATF_TC_HEAD(reregister_blk, tc)
{

	atf_tc_set_md_var(tc, "descr", "Tests register/unregister/register "
	    "for a block device");
}

ATF_TC_BODY(reregister_blk, tc)
{
	char buf[512 * 128];
	char cmpbuf[512 * 128];
	int rv, tfd;

	/* first, create some image files */
	rv = system("dd if=/dev/zero bs=512 count=64 "
	    "| tr '\\0' '\\1' > disk1.img");
	ATF_REQUIRE_EQ(rv, 0);

	rv = system("dd if=/dev/zero bs=512 count=128 "
	    "| tr '\\0' '\\2' > disk2.img");
	ATF_REQUIRE_EQ(rv, 0);

	rump_init();

	ATF_REQUIRE_EQ(rump_pub_etfs_register(TESTPATH1, "./disk1.img",
	    RUMP_ETFS_BLK), 0);
	tfd = rump_sys_open(TESTPATH1, O_RDONLY);
	ATF_REQUIRE(tfd != -1);
	ATF_REQUIRE_EQ(rump_sys_read(tfd, buf, sizeof(buf)), 64*512);
	memset(cmpbuf, 1, sizeof(cmpbuf));
	ATF_REQUIRE_EQ(memcmp(buf, cmpbuf, 64*512), 0);
	ATF_REQUIRE_EQ(rump_sys_close(tfd), 0);
	ATF_REQUIRE_EQ(rump_pub_etfs_remove(TESTPATH1), 0);

	ATF_REQUIRE_EQ(rump_pub_etfs_register(TESTPATH2, "./disk2.img",
	    RUMP_ETFS_BLK), 0);
	tfd = rump_sys_open(TESTPATH2, O_RDONLY);
	ATF_REQUIRE(tfd != -1);
	ATF_REQUIRE_EQ(rump_sys_read(tfd, buf, sizeof(buf)), 128*512);
	memset(cmpbuf, 2, sizeof(cmpbuf));
	ATF_REQUIRE_EQ(memcmp(buf, cmpbuf, 128*512), 0);
	ATF_REQUIRE_EQ(rump_sys_close(tfd), 0);
	ATF_REQUIRE_EQ(rump_pub_etfs_remove(TESTPATH2), 0);
}

ATF_TC_WITH_CLEANUP(large_blk);
ATF_TC_HEAD(large_blk, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check etfs block devices work for "
	    ">2TB images");
}

#define IMG_ON_MFS "mfsdir/disk.img"
ATF_TC_BODY(large_blk, tc)
{
	char buf[128];
	char cmpbuf[128];
	ssize_t n;
	int rv, tfd;

	/*
	 * mount mfs.  it would be nice if this would not be required,
	 * but a) tmpfs doesn't "support" sparse files b) we don't really
	 * know what fs atf workdir is on anyway.
	 */
	if (mkdir("mfsdir", 0777) == -1)
		atf_tc_fail_errno("mkdir failed");
	if (system("mount_mfs -s 64m -o nosuid,nodev mfs mfsdir") != 0)
		atf_tc_skip("could not mount mfs");

	/* create a 8TB sparse file */
	rv = system("dd if=/dev/zero of=" IMG_ON_MFS " bs=1 count=1 seek=8t");
	ATF_REQUIRE_EQ(rv, 0);

	/*
	 * map it and issue write at 6TB, then unmap+remap and check
	 * we get the same stuff back
	 */

	rump_init();
	ATF_REQUIRE_EQ(rump_pub_etfs_register(TESTPATH1, IMG_ON_MFS,
	    RUMP_ETFS_BLK), 0);
	tfd = rump_sys_open(TESTPATH1, O_RDWR);
	ATF_REQUIRE(tfd != -1);
	memset(buf, 12, sizeof(buf));
	n = rump_sys_pwrite(tfd, buf, sizeof(buf), 6*1024*1024*1024ULL*1024ULL);
	ATF_REQUIRE_EQ(n, sizeof(buf));
	ATF_REQUIRE_EQ(rump_sys_close(tfd), 0);
	ATF_REQUIRE_EQ(rump_pub_etfs_remove(TESTPATH1), 0);

	ATF_REQUIRE_EQ(rump_pub_etfs_register(TESTPATH2, IMG_ON_MFS,
	    RUMP_ETFS_BLK), 0);
	tfd = rump_sys_open(TESTPATH2, O_RDWR);
	ATF_REQUIRE(tfd != -1);
	memset(buf, 0, sizeof(buf));
	n = rump_sys_pread(tfd, buf, sizeof(buf), 6*1024*1024*1024ULL*1024ULL);
	ATF_REQUIRE_EQ(n, sizeof(buf));

	memset(cmpbuf, 12, sizeof(cmpbuf));
	ATF_REQUIRE_EQ(memcmp(cmpbuf, buf, 128), 0);
}

ATF_TC_CLEANUP(large_blk, tc)
{

	system("umount mfsdir");
}

ATF_TC(range_blk);
ATF_TC_HEAD(range_blk, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks ranged (offset,size) mappings");
}

ATF_TC_BODY(range_blk, tc)
{
	char buf[32000];
	char cmpbuf[32000];
	ssize_t n;
	int rv, tfd;

	/* create a 64000 byte file with 16 1's at offset = 32000 */
	rv = system("dd if=/dev/zero of=disk.img bs=1000 count=64");
	ATF_REQUIRE_EQ(rv, 0);
	rv = system("yes | tr '\\ny' '\\1' "
	    "| dd of=disk.img conv=notrunc bs=1 count=16 seek=32000");
	ATF_REQUIRE_EQ(rv, 0);

	/* map the file at [16000,48000].  this puts our 1's at offset 16000 */
	rump_init();
	ATF_REQUIRE_EQ(rump_pub_etfs_register_withsize(TESTPATH1, "disk.img",
	    RUMP_ETFS_BLK, 16000, 32000), 0);
	tfd = rump_sys_open(TESTPATH1, O_RDWR);
	ATF_REQUIRE(tfd != -1);
	n = rump_sys_read(tfd, buf, sizeof(buf));
	ATF_REQUIRE_EQ(n, sizeof(buf));
	ATF_REQUIRE_EQ(rump_sys_close(tfd), 0);
	ATF_REQUIRE_EQ(rump_pub_etfs_remove(TESTPATH1), 0);

	/* check that we got what is expected */
	memset(cmpbuf, 0, sizeof(cmpbuf));
	memset(cmpbuf+16000, 1, 16);
	ATF_REQUIRE_EQ(memcmp(buf, cmpbuf, sizeof(buf)), 0);
}

ATF_TC(key);
ATF_TC_HEAD(key, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks key format");
}

ATF_TC_BODY(key, tc)
{

	RZ(rump_init());

	RL(open("hostfile", O_RDWR | O_CREAT, 0777));

	RZ(rump_pub_etfs_register("/key", "hostfile", RUMP_ETFS_REG));
	ATF_REQUIRE_EQ(rump_pub_etfs_register("key", "hostfile", RUMP_ETFS_REG),
	    EINVAL);

	RL(rump_sys_open("/key", O_RDONLY));
	RL(rump_sys_open("////////key", O_RDONLY));

	RZ(rump_pub_etfs_register("////key//with/slashes", "hostfile",
	    RUMP_ETFS_REG));

	RL(rump_sys_open("/key//with/slashes", O_RDONLY));
	RL(rump_sys_open("key//with/slashes", O_RDONLY));
	ATF_REQUIRE_ERRNO(ENOENT,
	    rump_sys_open("/key/with/slashes", O_RDONLY) == -1);

	RL(rump_sys_mkdir("/a", 0777));
	ATF_REQUIRE_ERRNO(ENOENT,
	    rump_sys_open("/a/key//with/slashes", O_RDONLY) == -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, reregister_reg);
	ATF_TP_ADD_TC(tp, reregister_blk);
	ATF_TP_ADD_TC(tp, large_blk);
	ATF_TP_ADD_TC(tp, range_blk);
	ATF_TP_ADD_TC(tp, key);

	return atf_no_error();
}
