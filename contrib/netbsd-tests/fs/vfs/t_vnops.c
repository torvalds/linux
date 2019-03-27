/*	$NetBSD: t_vnops.c,v 1.59 2017/01/13 21:30:40 christos Exp $	*/

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
#include <sys/statvfs.h>
#include <sys/time.h>

#include <assert.h>
#include <atf-c.h>
#include <ctype.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rump/rump_syscalls.h>
#include <rump/rump.h>

#include "../common/h_fsmacros.h"
#include "h_macros.h"

#define TESTFILE "afile"

#define USES_DIRS					\
    if (FSTYPE_SYSVBFS(tc))				\
	atf_tc_skip("directories not supported by file system")

#define USES_SYMLINKS					\
    if (FSTYPE_SYSVBFS(tc) || FSTYPE_MSDOS(tc))		\
	atf_tc_skip("symlinks not supported by file system")

static char *
md(char *buf, size_t buflen, const char *base, const char *tail)
{

	snprintf(buf, buflen, "%s/%s", base, tail);
	return buf;
}

static void
lookup_simple(const atf_tc_t *tc, const char *mountpath)
{
	char pb[MAXPATHLEN], final[MAXPATHLEN];
	struct stat sb1, sb2;

	strcpy(final, mountpath);
	snprintf(pb, sizeof(pb), "%s/../%s", mountpath, basename(final));
	if (rump_sys_stat(pb, &sb1) == -1)
		atf_tc_fail_errno("stat 1");

	snprintf(pb, sizeof(pb), "%s/./../%s", mountpath, basename(final));
	if (rump_sys_stat(pb, &sb2) == -1)
		atf_tc_fail_errno("stat 2");

	ATF_REQUIRE(memcmp(&sb1, &sb2, sizeof(sb1)) == 0);
}

static void
lookup_complex(const atf_tc_t *tc, const char *mountpath)
{
	char pb[MAXPATHLEN];
	struct stat sb1, sb2;
	struct timespec atplus1, onesec;

	USES_DIRS;

	snprintf(pb, sizeof(pb), "%s/dir", mountpath);
	if (rump_sys_mkdir(pb, 0777) == -1)
		atf_tc_fail_errno("mkdir");
	if (rump_sys_stat(pb, &sb1) == -1)
		atf_tc_fail_errno("stat 1");

	snprintf(pb, sizeof(pb), "%s/./dir/../././dir/.", mountpath);
	if (rump_sys_stat(pb, &sb2) == -1)
		atf_tc_fail_errno("stat 2");

	/*
	 * The lookup is permitted to modify the access time of
	 * any directories searched - such a directory is the
	 * subject of this test.   Any difference should cause
	 * the 2nd lookup atime tp be >= the first, if it is ==, all is
	 * OK (atime is not required to be modified by the search, or
	 * both references may happen within the came clock tick), if the
	 * 2nd lookup atime is > the first, but not "too much" greater,
	 * just set it back, so the memcmp just below succeeds
	 * (assuming all else is OK).
	 */
	onesec.tv_sec = 1;
	onesec.tv_nsec = 0;
	timespecadd(&sb1.st_atimespec, &onesec, &atplus1);
	if (timespeccmp(&sb2.st_atimespec, &sb1.st_atimespec, >) &&
	    timespeccmp(&sb2.st_atimespec, &atplus1, <))
		sb2.st_atimespec = sb1.st_atimespec;

	if (memcmp(&sb1, &sb2, sizeof(sb1)) != 0) {
		printf("what\tsb1\t\tsb2\n");

#define FIELD(FN)	\
		printf(#FN "\t%lld\t%lld\n", \
		(long long)sb1.FN, (long long)sb2.FN)
#define TIME(FN)	\
		printf(#FN "\t%lld.%ld\t%lld.%ld\n", \
		(long long)sb1.FN.tv_sec, sb1.FN.tv_nsec, \
		(long long)sb2.FN.tv_sec, sb2.FN.tv_nsec)

		FIELD(st_dev);
		FIELD(st_mode);
		FIELD(st_ino);
		FIELD(st_nlink);
		FIELD(st_uid);
		FIELD(st_gid);
		FIELD(st_rdev);
		TIME(st_atimespec);
		TIME(st_mtimespec);
		TIME(st_ctimespec);
		TIME(st_birthtimespec);
		FIELD(st_size);
		FIELD(st_blocks);
		FIELD(st_flags);
		FIELD(st_gen);

#undef FIELD
#undef TIME

		atf_tc_fail("stat results differ, see ouput for more details");
	}
}

static void
dir_simple(const atf_tc_t *tc, const char *mountpath)
{
	char pb[MAXPATHLEN];
	struct stat sb;

	USES_DIRS;

	/* check we can create directories */
	snprintf(pb, sizeof(pb), "%s/dir", mountpath);
	if (rump_sys_mkdir(pb, 0777) == -1)
		atf_tc_fail_errno("mkdir");
	if (rump_sys_stat(pb, &sb) == -1)
		atf_tc_fail_errno("stat new directory");

	/* check we can remove then and that it makes them unreachable */
	if (rump_sys_rmdir(pb) == -1)
		atf_tc_fail_errno("rmdir");
	if (rump_sys_stat(pb, &sb) != -1 || errno != ENOENT)
		atf_tc_fail("ENOENT expected from stat");
}

static void
dir_notempty(const atf_tc_t *tc, const char *mountpath)
{
	char pb[MAXPATHLEN], pb2[MAXPATHLEN];
	int fd, rv;

	USES_DIRS;

	/* check we can create directories */
	snprintf(pb, sizeof(pb), "%s/dir", mountpath);
	if (rump_sys_mkdir(pb, 0777) == -1)
		atf_tc_fail_errno("mkdir");

	snprintf(pb2, sizeof(pb2), "%s/dir/file", mountpath);
	fd = rump_sys_open(pb2, O_RDWR | O_CREAT, 0777);
	if (fd == -1)
		atf_tc_fail_errno("create file");
	rump_sys_close(fd);

	rv = rump_sys_rmdir(pb);
	if (rv != -1 || errno != ENOTEMPTY)
		atf_tc_fail("non-empty directory removed succesfully");

	if (rump_sys_unlink(pb2) == -1)
		atf_tc_fail_errno("cannot remove dir/file");

	if (rump_sys_rmdir(pb) == -1)
		atf_tc_fail_errno("remove directory");
}

static void
dir_rmdirdotdot(const atf_tc_t *tc, const char *mp)
{
	char pb[MAXPATHLEN];
	int xerrno;

	USES_DIRS;

	FSTEST_ENTER();
	RL(rump_sys_mkdir("test", 0777));
	RL(rump_sys_chdir("test"));

	RL(rump_sys_mkdir("subtest", 0777));
	RL(rump_sys_chdir("subtest"));

	md(pb, sizeof(pb), mp, "test/subtest");
	RL(rump_sys_rmdir(pb));
	md(pb, sizeof(pb), mp, "test");
	RL(rump_sys_rmdir(pb));

	if (FSTYPE_NFS(tc))
		xerrno = ESTALE;
	else
		xerrno = ENOENT;
	ATF_REQUIRE_ERRNO(xerrno, rump_sys_chdir("..") == -1);
	FSTEST_EXIT();
}

static void
checkfile(const char *path, struct stat *refp)
{
	char buf[MAXPATHLEN];
	struct stat sb;
	static int n = 1;

	md(buf, sizeof(buf), path, "file");
	if (rump_sys_stat(buf, &sb) == -1)
		atf_tc_fail_errno("cannot stat file %d (%s)", n, buf);
	if (memcmp(&sb, refp, sizeof(sb)) != 0)
		atf_tc_fail("stat mismatch %d", n);
	n++;
}

static void
rename_dir(const atf_tc_t *tc, const char *mp)
{
	char pb1[MAXPATHLEN], pb2[MAXPATHLEN], pb3[MAXPATHLEN];
	struct stat ref, sb;

	if (FSTYPE_RUMPFS(tc))
		atf_tc_skip("rename not supported by file system");

	USES_DIRS;

	md(pb1, sizeof(pb1), mp, "dir1");
	if (rump_sys_mkdir(pb1, 0777) == -1)
		atf_tc_fail_errno("mkdir 1");

	md(pb2, sizeof(pb2), mp, "dir2");
	if (rump_sys_mkdir(pb2, 0777) == -1)
		atf_tc_fail_errno("mkdir 2");
	md(pb2, sizeof(pb2), mp, "dir2/subdir");
	if (rump_sys_mkdir(pb2, 0777) == -1)
		atf_tc_fail_errno("mkdir 3");

	md(pb3, sizeof(pb3), mp, "dir1/file");
	if (rump_sys_mknod(pb3, S_IFREG | 0777, -1) == -1)
		atf_tc_fail_errno("create file");
	if (rump_sys_stat(pb3, &ref) == -1)
		atf_tc_fail_errno("stat of file");

	/*
	 * First try ops which should succeed.
	 */

	/* rename within directory */
	md(pb3, sizeof(pb3), mp, "dir3");
	if (rump_sys_rename(pb1, pb3) == -1)
		atf_tc_fail_errno("rename 1");
	checkfile(pb3, &ref);

	/* rename directory onto itself (two ways, should fail) */
	md(pb1, sizeof(pb1), mp, "dir3/.");
	if (rump_sys_rename(pb1, pb3) != -1 || errno != EINVAL)
		atf_tc_fail_errno("rename 2");
	if (rump_sys_rename(pb3, pb1) != -1 || errno != EISDIR)
		atf_tc_fail_errno("rename 3");

	checkfile(pb3, &ref);

	/* rename father of directory into directory */
	md(pb1, sizeof(pb1), mp, "dir2/dir");
	md(pb2, sizeof(pb2), mp, "dir2");
	if (rump_sys_rename(pb2, pb1) != -1 || errno != EINVAL)
		atf_tc_fail_errno("rename 4");

	/* same for grandfather */
	md(pb1, sizeof(pb1), mp, "dir2/subdir/dir2");
	if (rump_sys_rename(pb2, pb1) != -1 || errno != EINVAL)
		atf_tc_fail("rename 5");

	checkfile(pb3, &ref);

	/* rename directory over a non-empty directory */
	if (rump_sys_rename(pb2, pb3) != -1 || errno != ENOTEMPTY)
		atf_tc_fail("rename 6");

	/* cross-directory rename */
	md(pb1, sizeof(pb1), mp, "dir3");
	md(pb2, sizeof(pb2), mp, "dir2/somedir");
	if (rump_sys_rename(pb1, pb2) == -1)
		atf_tc_fail_errno("rename 7");
	checkfile(pb2, &ref);

	/* move to parent directory */
	md(pb1, sizeof(pb1), mp, "dir2/somedir/../../dir3");
	if (rump_sys_rename(pb2, pb1) == -1)
		atf_tc_fail_errno("rename 8");
	md(pb1, sizeof(pb1), mp, "dir2/../dir3");
	checkfile(pb1, &ref);

	/* atomic cross-directory rename */
	md(pb3, sizeof(pb3), mp, "dir2/subdir");
	if (rump_sys_rename(pb1, pb3) == -1)
		atf_tc_fail_errno("rename 9");
	checkfile(pb3, &ref);

	/* rename directory over an empty directory */
	md(pb1, sizeof(pb1), mp, "parent");
	md(pb2, sizeof(pb2), mp, "parent/dir1");
	md(pb3, sizeof(pb3), mp, "parent/dir2");
	RL(rump_sys_mkdir(pb1, 0777));
	RL(rump_sys_mkdir(pb2, 0777));
	RL(rump_sys_mkdir(pb3, 0777));
	RL(rump_sys_rename(pb2, pb3));

	RL(rump_sys_stat(pb1, &sb));
	if (! FSTYPE_MSDOS(tc))
		ATF_CHECK_EQ(sb.st_nlink, 3);
	RL(rump_sys_rmdir(pb3));
	RL(rump_sys_rmdir(pb1));
}

static void
rename_dotdot(const atf_tc_t *tc, const char *mp)
{

	if (FSTYPE_RUMPFS(tc))
		atf_tc_skip("rename not supported by file system");

	USES_DIRS;

	if (rump_sys_chdir(mp) == -1)
		atf_tc_fail_errno("chdir mountpoint");

	if (rump_sys_mkdir("dir1", 0777) == -1)
		atf_tc_fail_errno("mkdir 1");
	if (rump_sys_mkdir("dir2", 0777) == -1)
		atf_tc_fail_errno("mkdir 2");

	if (rump_sys_rename("dir1", "dir1/..") != -1 || errno != EINVAL)
		atf_tc_fail_errno("self-dotdot to");

	if (rump_sys_rename("dir1/..", "sometarget") != -1 || errno != EINVAL)
		atf_tc_fail_errno("self-dotdot from");

	if (rump_sys_rename("dir1", "dir2/..") != -1 || errno != EINVAL)
		atf_tc_fail("other-dotdot");

	rump_sys_chdir("/");
}

static void
rename_reg_nodir(const atf_tc_t *tc, const char *mp)
{
	bool haslinks;
	struct stat sb;
	ino_t f1ino;

	if (FSTYPE_RUMPFS(tc))
		atf_tc_skip("rename not supported by file system");

	if (rump_sys_chdir(mp) == -1)
		atf_tc_fail_errno("chdir mountpoint");

	if (FSTYPE_MSDOS(tc) || FSTYPE_SYSVBFS(tc))
		haslinks = false;
	else
		haslinks = true;

	if (rump_sys_mknod("file1", S_IFREG | 0777, -1) == -1)
		atf_tc_fail_errno("create file");
	if (rump_sys_mknod("file2", S_IFREG | 0777, -1) == -1)
		atf_tc_fail_errno("create file");

	if (rump_sys_stat("file1", &sb) == -1)
		atf_tc_fail_errno("stat");
	f1ino = sb.st_ino;

	if (haslinks) {
		if (rump_sys_link("file1", "file_link") == -1)
			atf_tc_fail_errno("link");
		if (rump_sys_stat("file_link", &sb) == -1)
			atf_tc_fail_errno("stat");
		ATF_REQUIRE_EQ(sb.st_ino, f1ino);
		ATF_REQUIRE_EQ(sb.st_nlink, 2);
	}

	if (rump_sys_stat("file2", &sb) == -1)
		atf_tc_fail_errno("stat");

	if (rump_sys_rename("file1", "file3") == -1)
		atf_tc_fail_errno("rename 1");
	if (rump_sys_stat("file3", &sb) == -1)
		atf_tc_fail_errno("stat 1");
	if (haslinks) {
		ATF_REQUIRE_EQ(sb.st_ino, f1ino);
	}
	if (rump_sys_stat("file1", &sb) != -1 || errno != ENOENT)
		atf_tc_fail_errno("source 1");

	if (rump_sys_rename("file3", "file2") == -1)
		atf_tc_fail_errno("rename 2");
	if (rump_sys_stat("file2", &sb) == -1)
		atf_tc_fail_errno("stat 2");
	if (haslinks) {
		ATF_REQUIRE_EQ(sb.st_ino, f1ino);
	}

	if (rump_sys_stat("file3", &sb) != -1 || errno != ENOENT)
		atf_tc_fail_errno("source 2");

	if (haslinks) {
		if (rump_sys_rename("file2", "file_link") == -1)
			atf_tc_fail_errno("rename hardlink");
		if (rump_sys_stat("file2", &sb) != -1 || errno != ENOENT)
			atf_tc_fail_errno("source 3");
		if (rump_sys_stat("file_link", &sb) == -1)
			atf_tc_fail_errno("stat 2");
		ATF_REQUIRE_EQ(sb.st_ino, f1ino);
		ATF_REQUIRE_EQ(sb.st_nlink, 1);
	}

	ATF_CHECK_ERRNO(EFAULT, rump_sys_rename("file2", NULL) == -1);
	ATF_CHECK_ERRNO(EFAULT, rump_sys_rename(NULL, "file2") == -1);

	rump_sys_chdir("/");
}

/* PR kern/50607 */
static void
create_many(const atf_tc_t *tc, const char *mp)
{
	char buf[64];
	int nfiles = 2324; /* #Nancy */
	int i;

	/* takes forever with many files */
	if (FSTYPE_MSDOS(tc))
		nfiles /= 4;

	RL(rump_sys_chdir(mp));

	if (FSTYPE_SYSVBFS(tc)) {
		/* fs doesn't support many files or subdirectories */
		nfiles = 5;
	} else {
		/* msdosfs doesn't like many entries in the root directory */
		RL(rump_sys_mkdir("subdir", 0777));
		RL(rump_sys_chdir("subdir"));
	}

	/* create them */
#define TESTFN "testfile"
	for (i = 0; i < nfiles; i++) {
		int fd;

		snprintf(buf, sizeof(buf), TESTFN "%d", i);
		RL(fd = rump_sys_open(buf, O_RDWR|O_CREAT|O_EXCL, 0666));
		RL(rump_sys_close(fd));
	}

	/* wipe them out */
	for (i = 0; i < nfiles; i++) {
		snprintf(buf, sizeof(buf), TESTFN "%d", i);
		RLF(rump_sys_unlink(buf), "%s", buf);
	}
#undef TESTFN

	rump_sys_chdir("/");
}

/*
 * Test creating files with one-character names using all possible
 * character values.  Failures to create the file are ignored as the
 * characters allowed in file names vary by file system, but at least
 * we can check that the fs does not crash, and if the file is
 * successfully created, unlinking it should also succeed.
 */
static void
create_nonalphanum(const atf_tc_t *tc, const char *mp)
{
	char buf[64];
	int i;

	RL(rump_sys_chdir(mp));

	for (i = 0; i < 256; i++) {
		int fd;
		snprintf(buf, sizeof(buf), "%c", i);
		fd = rump_sys_open(buf, O_RDWR|O_CREAT|O_EXCL, 0666);
		if (fd == -1)
			continue;
		RLF(rump_sys_close(fd), "%d", fd);
		RLF(rump_sys_unlink(buf), "%s", buf);
	}
	printf("\n");

	rump_sys_chdir("/");
}

static void
create_nametoolong(const atf_tc_t *tc, const char *mp)
{
	char *name;
	int fd;
	long val;
	size_t len;

	if (rump_sys_chdir(mp) == -1)
		atf_tc_fail_errno("chdir mountpoint");

	val = rump_sys_pathconf(".", _PC_NAME_MAX);
	if (val == -1)
		atf_tc_fail_errno("pathconf");

	len = val + 1;
	name = malloc(len+1);
	if (name == NULL)
		atf_tc_fail_errno("malloc");

	memset(name, 'a', len);
	*(name+len) = '\0';

	val = rump_sys_pathconf(".", _PC_NO_TRUNC);
	if (val == -1)
		atf_tc_fail_errno("pathconf");

	fd = rump_sys_open(name, O_RDWR|O_CREAT, 0666);
	if (val != 0 && (fd != -1 || errno != ENAMETOOLONG))
		atf_tc_fail_errno("open");

	if (val == 0 && rump_sys_close(fd) == -1)
		atf_tc_fail_errno("close");
	if (val == 0 && rump_sys_unlink(name) == -1)
		atf_tc_fail_errno("unlink");

	free(name);

	rump_sys_chdir("/");
}

static void
create_exist(const atf_tc_t *tc, const char *mp)
{
	const char *name = "hoge";
	int fd;

	RL(rump_sys_chdir(mp));
	RL(fd = rump_sys_open(name, O_RDWR|O_CREAT|O_EXCL, 0666));
	RL(rump_sys_close(fd));
	RL(rump_sys_unlink(name));
	RL(fd = rump_sys_open(name, O_RDWR|O_CREAT, 0666));
	RL(rump_sys_close(fd));
	RL(fd = rump_sys_open(name, O_RDWR|O_CREAT, 0666));
	RL(rump_sys_close(fd));
	ATF_REQUIRE_ERRNO(EEXIST,
	    (fd = rump_sys_open(name, O_RDWR|O_CREAT|O_EXCL, 0666)));
	RL(rump_sys_unlink(name));
	RL(rump_sys_chdir("/"));
}

static void
rename_nametoolong(const atf_tc_t *tc, const char *mp)
{
	char *name;
	int res, fd;
	long val;
	size_t len;

	if (FSTYPE_RUMPFS(tc))
		atf_tc_skip("rename not supported by file system");

	if (rump_sys_chdir(mp) == -1)
		atf_tc_fail_errno("chdir mountpoint");

	val = rump_sys_pathconf(".", _PC_NAME_MAX);
	if (val == -1)
		atf_tc_fail_errno("pathconf");

	len = val + 1;
	name = malloc(len+1);
	if (name == NULL)
		atf_tc_fail_errno("malloc");

	memset(name, 'a', len);
	*(name+len) = '\0';

	fd = rump_sys_open("dummy", O_RDWR|O_CREAT, 0666);
	if (fd == -1)
		atf_tc_fail_errno("open");
	if (rump_sys_close(fd) == -1)
		atf_tc_fail_errno("close");

	val = rump_sys_pathconf(".", _PC_NO_TRUNC);
	if (val == -1)
		atf_tc_fail_errno("pathconf");

	res = rump_sys_rename("dummy", name);
	if (val != 0 && (res != -1 || errno != ENAMETOOLONG))
		atf_tc_fail_errno("rename");

	if (val == 0 && rump_sys_unlink(name) == -1)
		atf_tc_fail_errno("unlink");

	free(name);

	rump_sys_chdir("/");
}

/*
 * Test creating a symlink whose length is "len" bytes, not including
 * the terminating NUL.
 */
static void
symlink_len(const atf_tc_t *tc, const char *mp, size_t len)
{
	char *buf;
	int r;

	USES_SYMLINKS;

	RLF(rump_sys_chdir(mp), "%s", mp);

	buf = malloc(len + 1);
	ATF_REQUIRE(buf);
	memset(buf, 'a', len);
	buf[len] = '\0';
	r = rump_sys_symlink(buf, "afile");
	if (r == -1) {
		ATF_REQUIRE_ERRNO(ENAMETOOLONG, r);
	} else {
		RL(rump_sys_unlink("afile"));
	}
	free(buf);

	RL(rump_sys_chdir("/"));
}

static void
symlink_zerolen(const atf_tc_t *tc, const char *mp)
{
	symlink_len(tc, mp, 0);
}

static void
symlink_long(const atf_tc_t *tc, const char *mp)
{
	/*
	 * Test lengths close to powers of two, as those are likely
	 * to be edge cases.
	 */
	size_t len;
	int fuzz;
	for (len = 2; len <= 65536; len *= 2) {
		for (fuzz = -1; fuzz <= 1; fuzz++) {
			symlink_len(tc, mp, len + fuzz);
		}
	}
}

static void
symlink_root(const atf_tc_t *tc, const char *mp)
{

	USES_SYMLINKS;

	RL(rump_sys_chdir(mp));
	RL(rump_sys_symlink("/", "foo"));
	RL(rump_sys_chdir("foo"));
}

static void
attrs(const atf_tc_t *tc, const char *mp)
{
	struct stat sb, sb2;
	struct timeval tv[2];
	int fd;

	FSTEST_ENTER();
	RL(fd = rump_sys_open(TESTFILE, O_RDWR | O_CREAT, 0755));
	RL(rump_sys_close(fd));
	RL(rump_sys_stat(TESTFILE, &sb));
	if (!(FSTYPE_MSDOS(tc) || FSTYPE_SYSVBFS(tc))) {
		RL(rump_sys_chown(TESTFILE, 1, 2));
		sb.st_uid = 1;
		sb.st_gid = 2;
		RL(rump_sys_chmod(TESTFILE, 0123));
		sb.st_mode = (sb.st_mode & ~ACCESSPERMS) | 0123;
	}

	tv[0].tv_sec = 1000000000; /* need something >1980 for msdosfs */
	tv[0].tv_usec = 1;
	tv[1].tv_sec = 1000000002; /* need even seconds for msdosfs */
	tv[1].tv_usec = 3;
	RL(rump_sys_utimes(TESTFILE, tv));
	RL(rump_sys_utimes(TESTFILE, tv)); /* XXX: utimes & birthtime */
	sb.st_atimespec.tv_sec = 1000000000;
	sb.st_atimespec.tv_nsec = 1000;
	sb.st_mtimespec.tv_sec = 1000000002;
	sb.st_mtimespec.tv_nsec = 3000;

	RL(rump_sys_stat(TESTFILE, &sb2));
#define CHECK(a) ATF_REQUIRE_EQ(sb.a, sb2.a)
	if (!(FSTYPE_MSDOS(tc) || FSTYPE_SYSVBFS(tc))) {
		CHECK(st_uid);
		CHECK(st_gid);
		CHECK(st_mode);
	}
	if (!FSTYPE_MSDOS(tc)) {
		/* msdosfs has only access date, not time */
		CHECK(st_atimespec.tv_sec);
	}
	CHECK(st_mtimespec.tv_sec);
	if (!(FSTYPE_EXT2FS(tc) || FSTYPE_MSDOS(tc) ||
	      FSTYPE_SYSVBFS(tc) || FSTYPE_V7FS(tc))) {
		CHECK(st_atimespec.tv_nsec);
		CHECK(st_mtimespec.tv_nsec);
	}
#undef  CHECK

	FSTEST_EXIT();
}

static void
fcntl_lock(const atf_tc_t *tc, const char *mp)
{
	int fd, fd2;
	struct flock l;
	struct lwp *lwp1, *lwp2;

	FSTEST_ENTER();
	l.l_pid = 0;
	l.l_start = l.l_len = 1024;
	l.l_type = F_RDLCK | F_WRLCK;
	l.l_whence = SEEK_END;

	lwp1 = rump_pub_lwproc_curlwp();
	RL(fd = rump_sys_open(TESTFILE, O_RDWR | O_CREAT, 0755));
	RL(rump_sys_ftruncate(fd, 8192));

	RL(rump_sys_fcntl(fd, F_SETLK, &l));

	/* Next, we fork and try to lock the same area */
	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	lwp2 = rump_pub_lwproc_curlwp();
	RL(fd2 = rump_sys_open(TESTFILE, O_RDWR, 0));
	ATF_REQUIRE_ERRNO(EAGAIN, rump_sys_fcntl(fd2, F_SETLK, &l));

	/* Switch back and unlock... */
	rump_pub_lwproc_switch(lwp1);
	l.l_type = F_UNLCK;
	RL(rump_sys_fcntl(fd, F_SETLK, &l));

	/* ... and try to lock again */
	rump_pub_lwproc_switch(lwp2);
	l.l_type = F_RDLCK | F_WRLCK;
	RL(rump_sys_fcntl(fd2, F_SETLK, &l));

	RL(rump_sys_close(fd2));
	rump_pub_lwproc_releaselwp();

	RL(rump_sys_close(fd));

	FSTEST_EXIT();
}

static int
flock_compare(const void *p, const void *q)
{
	int a = ((const struct flock *)p)->l_start;
	int b = ((const struct flock *)q)->l_start;
	return a < b ? -1 : (a > b ? 1 : 0);
}

/*
 * Find all locks set by fcntl_getlock_pids test
 * using GETLK for a range [start, start+end], and,
 * if there is a blocking lock, recursively find
 * all locks to the left (toward the beginning of
 * a file) and to the right of the lock.
 * The function also understands "until end of file"
 * convention when len==0.
 */
static unsigned int
fcntl_getlocks(int fildes, off_t start, off_t len,
    struct flock *lock, struct flock *end)
{
	unsigned int rv = 0;
	const struct flock l = { start, len, 0, F_RDLCK, SEEK_SET };

	if (lock == end)
		return rv;

	RL(rump_sys_fcntl(fildes, F_GETLK, &l));

	if (l.l_type == F_UNLCK)
		return rv;

	*lock++ = l;
	rv += 1;

	ATF_REQUIRE(l.l_whence == SEEK_SET);

	if (l.l_start > start) {
		unsigned int n =
		    fcntl_getlocks(fildes, start, l.l_start - start, lock, end);
		rv += n;
		lock += n;
		if (lock == end)
			return rv;
	}

	if (l.l_len == 0) /* does l spans until the end? */
		return rv;

	if (len == 0) /* are we looking for locks until the end? */ {
		rv += fcntl_getlocks(fildes, l.l_start + l.l_len, len, lock, end);
	} else if (l.l_start + l.l_len < start + len) {
		len -= l.l_start + l.l_len - start;
		rv += fcntl_getlocks(fildes, l.l_start + l.l_len, len, lock, end);
	}

	return rv;
}

static void
fcntl_getlock_pids(const atf_tc_t *tc, const char *mp)
{
	/* test non-overlaping ranges */
	struct flock expect[4];
	const struct flock lock[4] = {
		{ 0, 2, 0, F_WRLCK, SEEK_SET },
		{ 2, 1, 0, F_WRLCK, SEEK_SET },
		{ 7, 5, 0, F_WRLCK, SEEK_SET },
		{ 4, 3, 0, F_WRLCK, SEEK_SET },
	};

    /* Add extra element to make sure recursion does't stop at array end */
	struct flock result[5];

	/* Add 5th process */
	int fd[5];
	pid_t pid[5];
	struct lwp *lwp[5];

	unsigned int i, j;
	const off_t sz = 8192;
	int omode  = 0755;
	int oflags = O_RDWR | O_CREAT;

	memcpy(expect, lock, sizeof(lock));

	FSTEST_ENTER();

	/*
	 * First, we create 4 processes and let each lock a range of the
	 * file.  Note that the third and fourth processes lock in
	 * "reverse" order, i.e. the greater pid locks a range before
	 * the lesser pid.
	 * Then, we create 5th process which doesn't lock anything.
	 */
	for (i = 0; i < __arraycount(lwp); i++) {
		RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));

		lwp[i] = rump_pub_lwproc_curlwp();
		pid[i] = rump_sys_getpid();

		RL(fd[i] = rump_sys_open(TESTFILE, oflags, omode));
		oflags = O_RDWR;
		omode  = 0;

		RL(rump_sys_ftruncate(fd[i], sz));

		if (i < __arraycount(lock)) {
			RL(rump_sys_fcntl(fd[i], F_SETLK, &lock[i]));
			expect[i].l_pid = pid[i];
		}
	}

	qsort(expect, __arraycount(expect), sizeof(expect[0]), &flock_compare);

	/*
	 * In the context of each process, recursively find all locks
	 * that would block the current process. Processes 1-4 don't
	 * see their own lock, we insert it to simplify checks.
	 * Process 5 sees all 4 locks.
	 */
	for (i = 0; i < __arraycount(lwp); i++) {
		unsigned int nlocks;

		rump_pub_lwproc_switch(lwp[i]);

		memset(result, 0, sizeof(result));
		nlocks = fcntl_getlocks(fd[i], 0, sz,
		    result, result + __arraycount(result));

		if (i < __arraycount(lock)) {
			ATF_REQUIRE(nlocks < __arraycount(result));
			result[nlocks] = lock[i];
			result[nlocks].l_pid = pid[i];
			nlocks++;
		}

		ATF_CHECK_EQ(nlocks, __arraycount(expect));

		qsort(result, nlocks, sizeof(result[0]), &flock_compare);

		for (j = 0; j < nlocks; j++) {
			ATF_CHECK_EQ(result[j].l_start,  expect[j].l_start );
			ATF_CHECK_EQ(result[j].l_len,    expect[j].l_len   );
			ATF_CHECK_EQ(result[j].l_pid,    expect[j].l_pid   );
			ATF_CHECK_EQ(result[j].l_type,   expect[j].l_type  );
			ATF_CHECK_EQ(result[j].l_whence, expect[j].l_whence);
		}
	}

	/*
	 * Release processes.  This also releases the fds and locks
	 * making fs unmount possible
	 */
	for (i = 0; i < __arraycount(lwp); i++) {
		rump_pub_lwproc_switch(lwp[i]);
		rump_pub_lwproc_releaselwp();
	}

	FSTEST_EXIT();
}

static void
access_simple(const atf_tc_t *tc, const char *mp)
{
	int fd;
	int tmode;

	FSTEST_ENTER();
	RL(fd = rump_sys_open("tfile", O_CREAT | O_RDWR, 0777));
	RL(rump_sys_close(fd));

#define ALLACC (F_OK | X_OK | W_OK | R_OK)
	if (FSTYPE_SYSVBFS(tc) || FSTYPE_MSDOS(tc))
		tmode = F_OK;
	else
		tmode = ALLACC;

	RL(rump_sys_access("tfile", tmode));

	/* PR kern/44648 */
	ATF_REQUIRE_ERRNO(EINVAL, rump_sys_access("tfile", ALLACC+1) == -1);
#undef ALLACC
	FSTEST_EXIT();
}

static void
read_directory(const atf_tc_t *tc, const char *mp)
{
	char buf[1024];
	int fd, res;
	ssize_t size;

	FSTEST_ENTER();
	fd = rump_sys_open(".", O_DIRECTORY | O_RDONLY, 0777);
	ATF_REQUIRE(fd != -1);

	size = rump_sys_pread(fd, buf, sizeof(buf), 0);
	ATF_CHECK(size != -1 || errno == EISDIR);
	size = rump_sys_read(fd, buf, sizeof(buf));
	ATF_CHECK(size != -1 || errno == EISDIR);

	res = rump_sys_close(fd);
	ATF_REQUIRE(res != -1);
	FSTEST_EXIT();
}

static void
lstat_symlink(const atf_tc_t *tc, const char *mp)
{
	const char *src, *dst;
	int res;
	struct stat st;

	USES_SYMLINKS;

	FSTEST_ENTER();

	src = "source";
	dst = "destination";

	res = rump_sys_symlink(src, dst);
	ATF_REQUIRE(res != -1);
	res = rump_sys_lstat(dst, &st);
	ATF_REQUIRE(res != -1);

	ATF_CHECK(S_ISLNK(st.st_mode) != 0);
	ATF_CHECK(st.st_size == (off_t)strlen(src));

	FSTEST_EXIT();
}

ATF_TC_FSAPPLY(lookup_simple, "simple lookup (./.. on root)");
ATF_TC_FSAPPLY(lookup_complex, "lookup of non-dot entries");
ATF_TC_FSAPPLY(dir_simple, "mkdir/rmdir");
ATF_TC_FSAPPLY(dir_notempty, "non-empty directories cannot be removed");
ATF_TC_FSAPPLY(dir_rmdirdotdot, "remove .. and try to cd out (PR kern/44657)");
ATF_TC_FSAPPLY(rename_dir, "exercise various directory renaming ops "
"(PR kern/44288)");
ATF_TC_FSAPPLY(rename_dotdot, "rename dir .. (PR kern/43617)");
ATF_TC_FSAPPLY(rename_reg_nodir, "rename regular files, no subdirectories");
ATF_TC_FSAPPLY(create_nametoolong, "create file with name too long");
ATF_TC_FSAPPLY(create_exist, "create with O_EXCL");
ATF_TC_FSAPPLY(rename_nametoolong, "rename to file with name too long");
ATF_TC_FSAPPLY(symlink_zerolen, "symlink with target of length 0");
ATF_TC_FSAPPLY(symlink_long, "symlink with target of length > 0");
ATF_TC_FSAPPLY(symlink_root, "symlink to root directory");
ATF_TC_FSAPPLY(attrs, "check setting attributes works");
ATF_TC_FSAPPLY(fcntl_lock, "check fcntl F_SETLK");
ATF_TC_FSAPPLY(fcntl_getlock_pids,"fcntl F_GETLK w/ many procs, PR kern/44494");
ATF_TC_FSAPPLY(access_simple, "access(2)");
ATF_TC_FSAPPLY(read_directory, "read(2) on directories");
ATF_TC_FSAPPLY(lstat_symlink, "lstat(2) values for symbolic links");

#undef FSTEST_IMGSIZE
#define FSTEST_IMGSIZE (1024*1024*64)
ATF_TC_FSAPPLY(create_many, "create many directory entries");
ATF_TC_FSAPPLY(create_nonalphanum, "non-alphanumeric filenames");

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_FSAPPLY(lookup_simple);
	ATF_TP_FSAPPLY(lookup_complex);
	ATF_TP_FSAPPLY(dir_simple);
	ATF_TP_FSAPPLY(dir_notempty);
	ATF_TP_FSAPPLY(dir_rmdirdotdot);
	ATF_TP_FSAPPLY(rename_dir);
	ATF_TP_FSAPPLY(rename_dotdot);
	ATF_TP_FSAPPLY(rename_reg_nodir);
	ATF_TP_FSAPPLY(create_many);
	ATF_TP_FSAPPLY(create_nonalphanum);
	ATF_TP_FSAPPLY(create_nametoolong);
	ATF_TP_FSAPPLY(create_exist);
	ATF_TP_FSAPPLY(rename_nametoolong);
	ATF_TP_FSAPPLY(symlink_zerolen);
	ATF_TP_FSAPPLY(symlink_long);
	ATF_TP_FSAPPLY(symlink_root);
	ATF_TP_FSAPPLY(attrs);
	ATF_TP_FSAPPLY(fcntl_lock);
	ATF_TP_FSAPPLY(fcntl_getlock_pids);
	ATF_TP_FSAPPLY(access_simple);
	ATF_TP_FSAPPLY(read_directory);
	ATF_TP_FSAPPLY(lstat_symlink);

	return atf_no_error();
}
