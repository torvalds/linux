/*	$NetBSD: h_fsmacros.h,v 1.41 2017/01/13 21:30:39 christos Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nicolas Joly.
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

#ifndef __H_FSMACROS_H_
#define __H_FSMACROS_H_

#include <sys/mount.h>

#include <atf-c.h>
#include <puffsdump.h>
#include <string.h>

#include <rump/rump.h>

#include "h_macros.h"

#define FSPROTOS(_fs_)							\
int _fs_##_fstest_newfs(const atf_tc_t *, void **, const char *,	\
			off_t, void *);					\
int _fs_##_fstest_delfs(const atf_tc_t *, void *);			\
int _fs_##_fstest_mount(const atf_tc_t *, void *, const char *, int);	\
int _fs_##_fstest_unmount(const atf_tc_t *, const char *, int);

FSPROTOS(ext2fs);
FSPROTOS(ffs);
FSPROTOS(ffslog);
FSPROTOS(lfs);
FSPROTOS(msdosfs);
FSPROTOS(nfs);
FSPROTOS(nfsro);
FSPROTOS(p2k_ffs);
FSPROTOS(puffs);
FSPROTOS(rumpfs);
FSPROTOS(sysvbfs);
FSPROTOS(tmpfs);
FSPROTOS(udf);
FSPROTOS(v7fs);
FSPROTOS(zfs);

#ifndef FSTEST_IMGNAME
#define FSTEST_IMGNAME "image.fs"
#endif
#ifndef FSTEST_IMGSIZE
#define FSTEST_IMGSIZE (10000 * 512)
#endif
#ifndef FSTEST_MNTNAME
#define FSTEST_MNTNAME "/mnt"
#endif

#define FSTEST_CONSTRUCTOR(_tc_, _fs_, _args_)				\
do {									\
	if (_fs_##_fstest_newfs(_tc_, &_args_,				\
	    FSTEST_IMGNAME, FSTEST_IMGSIZE, NULL) != 0)			\
		atf_tc_fail_errno("newfs failed");			\
	if (_fs_##_fstest_mount(_tc_, _args_, FSTEST_MNTNAME, 0) != 0)	\
		atf_tc_fail_errno("mount failed");			\
} while (/*CONSTCOND*/0);

#define FSTEST_CONSTRUCTOR_FSPRIV(_tc_, _fs_, _args_, _privargs_)	\
do {									\
	if (_fs_##_fstest_newfs(_tc_, &_args_,				\
	    FSTEST_IMGNAME, FSTEST_IMGSIZE, _privargs_) != 0)		\
		atf_tc_fail_errno("newfs failed");			\
	if (_fs_##_fstest_mount(_tc_, _args_, FSTEST_MNTNAME, 0) != 0)	\
		atf_tc_fail_errno("mount failed");			\
} while (/*CONSTCOND*/0);

#define FSTEST_DESTRUCTOR(_tc_, _fs_, _args_)				\
do {									\
	if (_fs_##_fstest_unmount(_tc_, FSTEST_MNTNAME, 0) != 0) {	\
		rump_pub_vfs_mount_print(FSTEST_MNTNAME, 1);		\
		atf_tc_fail_errno("unmount failed");			\
	}								\
	if (_fs_##_fstest_delfs(_tc_, _args_) != 0)			\
		atf_tc_fail_errno("delfs failed");			\
} while (/*CONSTCOND*/0);

#define ATF_TC_FSADD(fs,type,func,desc)					\
	ATF_TC(fs##_##func);						\
	ATF_TC_HEAD(fs##_##func,tc)					\
	{								\
		atf_tc_set_md_var(tc, "descr", type " test for " desc);	\
		atf_tc_set_md_var(tc, "X-fs.type", #fs);		\
		atf_tc_set_md_var(tc, "X-fs.mntname", type);		\
	}								\
	void *fs##func##tmp;						\
									\
	ATF_TC_BODY(fs##_##func,tc)					\
	{								\
		if (!atf_check_fstype(tc, #fs))				\
			atf_tc_skip("filesystem not selected");		\
		FSTEST_CONSTRUCTOR(tc,fs,fs##func##tmp);		\
		func(tc,FSTEST_MNTNAME);				\
		if (fs##_fstest_unmount(tc, FSTEST_MNTNAME, 0) != 0) {	\
			rump_pub_vfs_mount_print(FSTEST_MNTNAME, 1);	\
			atf_tc_fail_errno("unmount failed");		\
		}							\
	}

#define ATF_TC_FSADD_RO(_fs_,_type_,_func_,_desc_,_gen_)		\
	ATF_TC(_fs_##_##_func_);					\
	ATF_TC_HEAD(_fs_##_##_func_,tc)					\
	{								\
		atf_tc_set_md_var(tc, "descr",_type_" test for "_desc_);\
		atf_tc_set_md_var(tc, "X-fs.type", #_fs_);		\
		atf_tc_set_md_var(tc, "X-fs.mntname", _type_);		\
	}								\
	void *_fs_##_func_##tmp;					\
									\
	ATF_TC_BODY(_fs_##_##_func_,tc)					\
	{								\
		if (!atf_check_fstype(tc, #_fs_))			\
			atf_tc_skip("filesystem not selected");		\
		FSTEST_CONSTRUCTOR(tc,_fs_,_fs_##_func_##tmp);		\
		_gen_(tc,FSTEST_MNTNAME);				\
		if (_fs_##_fstest_unmount(tc, FSTEST_MNTNAME, 0) != 0)	\
			atf_tc_fail_errno("unmount r/w failed");	\
		if (_fs_##_fstest_mount(tc, _fs_##_func_##tmp,		\
		    FSTEST_MNTNAME, MNT_RDONLY) != 0)			\
			atf_tc_fail_errno("mount ro failed");		\
		_func_(tc,FSTEST_MNTNAME);				\
		if (_fs_##_fstest_unmount(tc, FSTEST_MNTNAME, 0) != 0) {\
			rump_pub_vfs_mount_print(FSTEST_MNTNAME, 1);	\
			atf_tc_fail_errno("unmount failed");		\
		}							\
	}

#define ATF_TP_FSADD(fs,func)						\
  ATF_TP_ADD_TC(tp,fs##_##func)

#define ATF_TC_FSAPPLY_NOZFS(func,desc)					\
  ATF_TC_FSADD(ext2fs,MOUNT_EXT2FS,func,desc)				\
  ATF_TC_FSADD(ffs,MOUNT_FFS,func,desc)					\
  ATF_TC_FSADD(ffslog,MOUNT_FFS,func,desc)				\
  ATF_TC_FSADD(lfs,MOUNT_LFS,func,desc)					\
  ATF_TC_FSADD(msdosfs,MOUNT_MSDOS,func,desc)				\
  ATF_TC_FSADD(nfs,MOUNT_NFS,func,desc)					\
  ATF_TC_FSADD(puffs,MOUNT_PUFFS,func,desc)				\
  ATF_TC_FSADD(p2k_ffs,MOUNT_PUFFS,func,desc)				\
  ATF_TC_FSADD(rumpfs,MOUNT_RUMPFS,func,desc)				\
  ATF_TC_FSADD(sysvbfs,MOUNT_SYSVBFS,func,desc)				\
  ATF_TC_FSADD(tmpfs,MOUNT_TMPFS,func,desc)				\
  ATF_TC_FSADD(udf,MOUNT_UDF,func,desc)				\
  ATF_TC_FSADD(v7fs,MOUNT_V7FS,func,desc)

#define ATF_TP_FSAPPLY_NOZFS(func)					\
  ATF_TP_FSADD(ext2fs,func);						\
  ATF_TP_FSADD(ffs,func);						\
  ATF_TP_FSADD(ffslog,func);						\
  ATF_TP_FSADD(lfs,func);						\
  ATF_TP_FSADD(msdosfs,func);						\
  ATF_TP_FSADD(nfs,func);						\
  ATF_TP_FSADD(puffs,func);						\
  ATF_TP_FSADD(p2k_ffs,func);						\
  ATF_TP_FSADD(rumpfs,func);						\
  ATF_TP_FSADD(sysvbfs,func);						\
  ATF_TP_FSADD(tmpfs,func);						\
  ATF_TP_FSADD(udf,func);						\
  ATF_TP_FSADD(v7fs,func);

/* XXX: this will not scale */
#ifdef WANT_ZFS_TESTS
#define ATF_TC_FSAPPLY(func,desc)					\
  ATF_TC_FSAPPLY_NOZFS(func,desc)					\
  ATF_TC_FSADD(zfs,MOUNT_ZFS,func,desc)
#define ATF_TP_FSAPPLY(func)						\
  ATF_TP_FSAPPLY_NOZFS(func)						\
  ATF_TP_FSADD(zfs,func);

#else /* !WANT_ZFS_TESTS */

#define ATF_TC_FSAPPLY(func,desc)					\
  ATF_TC_FSAPPLY_NOZFS(func,desc)
#define ATF_TP_FSAPPLY(func)						\
  ATF_TP_FSAPPLY_NOZFS(func)

#endif /* WANT_ZFS_TESTS */

/*
 * Same as above, but generate a file system image first and perform
 * tests for a r/o mount.
 *
 * Missing the following file systems:
 *   + lfs (fstest_lfs routines cannot handle remount.  FIXME!)
 *   + tmpfs (memory backend)
 *   + rumpfs (memory backend)
 *   + puffs (memory backend, but could be run in theory)
 */

#define ATF_TC_FSAPPLY_RO(func,desc,gen)				\
  ATF_TC_FSADD_RO(ext2fs,MOUNT_EXT2FS,func,desc,gen)			\
  ATF_TC_FSADD_RO(ffs,MOUNT_FFS,func,desc,gen)				\
  ATF_TC_FSADD_RO(ffslog,MOUNT_FFS,func,desc,gen)			\
  ATF_TC_FSADD_RO(msdosfs,MOUNT_MSDOS,func,desc,gen)			\
  ATF_TC_FSADD_RO(nfs,MOUNT_NFS,func,desc,gen)				\
  ATF_TC_FSADD_RO(nfsro,MOUNT_NFS,func,desc,gen)			\
  ATF_TC_FSADD_RO(sysvbfs,MOUNT_SYSVBFS,func,desc,gen)			\
  ATF_TC_FSADD_RO(udf,MOUNT_UDF,func,desc,gen)			\
  ATF_TC_FSADD_RO(v7fs,MOUNT_V7FS,func,desc,gen)

#define ATF_TP_FSAPPLY_RO(func)						\
  ATF_TP_FSADD(ext2fs,func);						\
  ATF_TP_FSADD(ffs,func);						\
  ATF_TP_FSADD(ffslog,func);						\
  ATF_TP_FSADD(msdosfs,func);						\
  ATF_TP_FSADD(nfs,func);						\
  ATF_TP_FSADD(nfsro,func);						\
  ATF_TP_FSADD(sysvbfs,func);						\
  ATF_TP_FSADD(udf,func);						\
  ATF_TP_FSADD(v7fs,func);

#define ATF_FSAPPLY(func,desc)						\
	ATF_TC_FSAPPLY(func,desc);					\
	ATF_TP_ADD_TCS(tp)						\
	{								\
		ATF_TP_FSAPPLY(func);					\
		return atf_no_error();					\
	}

static __inline bool
atf_check_fstype(const atf_tc_t *tc, const char *fs)
{
	const char *fstype;

	if (!atf_tc_has_config_var(tc, "fstype"))
		return true;

	fstype = atf_tc_get_config_var(tc, "fstype");
	if (strcmp(fstype, fs) == 0)
		return true;
	return false;
}

#define FSTYPE_EXT2FS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "ext2fs") == 0)
#define FSTYPE_FFS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "ffs") == 0)
#define FSTYPE_FFSLOG(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "ffslog") == 0)
#define FSTYPE_LFS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "lfs") == 0)
#define FSTYPE_MSDOS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "msdosfs") == 0)
#define FSTYPE_NFS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "nfs") == 0)
#define FSTYPE_NFSRO(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "nfsro") == 0)
#define FSTYPE_P2K_FFS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "p2k_ffs") == 0)
#define FSTYPE_PUFFS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "puffs") == 0)
#define FSTYPE_RUMPFS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "rumpfs") == 0)
#define FSTYPE_SYSVBFS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "sysvbfs") == 0)
#define FSTYPE_TMPFS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "tmpfs") == 0)
#define FSTYPE_UDF(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "udf") == 0)
#define FSTYPE_V7FS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "v7fs") == 0)
#define FSTYPE_ZFS(tc)\
    (strcmp(atf_tc_get_md_var(tc, "X-fs.type"), "zfs") == 0)

#define FSTEST_ENTER()							\
	if (rump_sys_chdir(FSTEST_MNTNAME) == -1)			\
		atf_tc_fail_errno("failed to cd into test mount")

#define FSTEST_EXIT()							\
	if (rump_sys_chdir("/") == -1)					\
		atf_tc_fail_errno("failed to cd out of test mount")

/*
 * file system args structures
 */

struct nfstestargs {
	pid_t ta_childpid;
	char ta_ethername[MAXPATHLEN];
};

struct puffstestargs {
	uint8_t			*pta_pargs;
	size_t			pta_pargslen;

	int			pta_pflags;
	pid_t			pta_childpid;

	int			pta_rumpfd;
	int			pta_servfd;

	char			pta_dev[MAXPATHLEN];
	char			pta_dir[MAXPATHLEN];

	int			pta_mntflags;

	int			pta_vfs_toserv_ops[PUFFS_VFS_MAX];
	int			pta_vn_toserv_ops[PUFFS_VN_MAX];
};

#endif /* __H_FSMACROS_H_ */
