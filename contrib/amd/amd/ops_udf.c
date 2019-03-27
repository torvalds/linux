/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/amd/ops_udf.c
 *
 */

/*
 * UDF file system
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* forward definitions */
static char *udf_match(am_opts *fo);
static int udf_mount(am_node *am, mntfs *mf);
static int udf_umount(am_node *am, mntfs *mf);

/*
 * Ops structure
 */
am_ops udf_ops =
{
	"udf",
	udf_match,
	0,				/* udf_init */
	udf_mount,
	udf_umount,
	amfs_error_lookup_child,
	amfs_error_mount_child,
	amfs_error_readdir,
	0,				/* udf_readlink */
	0,				/* udf_mounted */
	0,				/* udf_umounted */
	amfs_generic_find_srvr,
	0,				/* udf_get_wchan */
	FS_MKMNT | FS_UBACKGROUND | FS_AMQINFO,	/* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
	AUTOFS_UDF_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};

#if defined(HAVE_UDF_ARGS_T_NOBODY_GID) || defined(HAVE_UDF_ARGS_T_NOBODY_UID)
static int
a_num(const char *s, const char *id_type)
{
	int id;
	char *ep;

	id = strtol(s, &ep, 0);
	if (*ep || s == ep || id < 0) {
		plog(XLOG_ERROR, "mount_udf: unknown %s: %s", id_type, s);
		return 0;
	}
	return id;
}
#endif /* defined(HAVE_UDF_ARGS_T_NOBODY_GID) || defined(HAVE_UDF_ARGS_T_NOBODY_UID) */

#if defined(HAVE_UDF_ARGS_T_NOBODY_GID)
static gid_t
a_gid(const char *s, const char *id_type)
{
	struct group *gr;

	if ((gr = getgrnam(s)) != NULL)
		return gr->gr_gid;
	return a_num(s, id_type);
}
#endif /* defined(HAVE_UDF_ARGS_T_NOBODY_GID) */

#if defined(HAVE_UDF_ARGS_T_NOBODY_UID)
static uid_t
a_uid(const char *s, const char *id_type)
{
	struct passwd *pw;

	if ((pw = getpwnam(s)) != NULL)
		return pw->pw_uid;
	return a_num(s, id_type);
}
#endif /* defined(HAVE_UDF_ARGS_T_NOBODY_UID) */

/*
 * UDF needs remote filesystem.
 */
static char *
udf_match(am_opts *fo)
{

	if (!fo->opt_dev) {
		plog(XLOG_USER, "udf: no source device specified");
		return 0;
	}
	dlog("UDF: mounting device \"%s\" on \"%s\"", fo->opt_dev, fo->opt_fs);

	/*
	 * Determine magic cookie to put in mtab
	 */
	return xstrdup(fo->opt_dev);
}

static int
mount_udf(char *mntdir, char *fs_name, char *opts, int on_autofs)
{
	udf_args_t udf_args;
	mntent_t mnt;
	int flags;
	char *str;
#if defined(HAVE_UDF_ARGS_T_NOBODY_UID) || defined(HAVE_UDF_ARGS_T_ANON_UID)
	uid_t uid_nobody;
	gid_t gid_nobody;
#endif /* defined(HAVE_UDF_ARGS_T_NOBODY_UID) || defined(HAVE_UDF_ARGS_T_ANON_UID) */
	/*
	 * Figure out the name of the file system type.
	 */
	MTYPE_TYPE type = MOUNT_TYPE_UDF;

#if defined(HAVE_UDF_ARGS_T_NOBODY_UID) || defined(HAVE_UDF_ARGS_T_ANON_UID)
	uid_nobody = a_uid("nobody", "user");
	if (uid_nobody == 0) {
		plog(XLOG_ERROR, "mount_udf: invalid uid for nobody");
		return EPERM;
	}
#endif /* defined(HAVE_UDF_ARGS_T_NOBODY_UID) || defined(HAVE_UDF_ARGS_T_ANON_UID) */

#if defined(HAVE_UDF_ARGS_T_NOBODY_GID) || defined(HAVE_UDF_ARGS_T_ANON_GID)
	gid_nobody = a_gid("nobody", "group");
	if (gid_nobody == 0) {
		plog(XLOG_ERROR, "mount_udf: invalid gid for nobody");
		return EPERM;
	}
#endif /* defined(HAVE_UDF_ARGS_T_NOBODY_GID) || defined(HAVE_UDF_ARGS_T_ANON_GID) */

	str = NULL;
	memset((voidp) &udf_args, 0, sizeof(udf_args)); /* Paranoid */

	/*
	 * Fill in the mount structure
	 */
	memset((voidp)&mnt, 0, sizeof(mnt));
	mnt.mnt_dir = mntdir;
	mnt.mnt_fsname = fs_name;
	mnt.mnt_type = MNTTAB_TYPE_UDF;
	mnt.mnt_opts = opts;

	flags = compute_mount_flags(&mnt);

#ifdef HAVE_UDF_ARGS_T_UDFMFLAGS
# if defined(MNT2_UDF_OPT_CLOSESESSION) && defined(MNTTAB_OPT_CLOSESESSION)
	if (amu_hasmntopt(&mnt, MNTTAB_OPT_CLOSESESSION))
		udf_args.udfmflags |= MNT2_UDF_OPT_CLOSESESSION;
# endif /* defined(MNT2_UDF_OPT_CLOSESESSION) && defined(MNTTAB_OPT_CLOSESESSION) */
#endif /* HAVE_UDF_ARGS_T_UDFMFLAGS */

#ifdef HAVE_UDF_ARGS_T_NOBODY_UID
	udf_args.nobody_uid = uid_nobody;
#endif /* HAVE_UDF_ARGS_T_NOBODY_UID */

#ifdef HAVE_UDF_ARGS_T_NOBODY_GID
	udf_args.nobody_gid = gid_nobody;
#endif /* HAVE_UDF_ARGS_T_NOBODY_GID */

#ifdef HAVE_UDF_ARGS_T_ANON_UID
	udf_args.anon_uid = uid_nobody;	/* default to nobody */
	if ((str = hasmntstr(&mnt, MNTTAB_OPT_USER)) != NULL) {
		udf_args.anon_uid = a_uid(str, MNTTAB_OPT_USER);
		XFREE(str);
	}
#endif /* HAVE_UDF_ARGS_T_ANON_UID */

#ifdef HAVE_UDF_ARGS_T_ANON_GID
	udf_args.anon_gid = gid_nobody;	/* default to nobody */
	if ((str = hasmntstr(&mnt, MNTTAB_OPT_GROUP)) != NULL) {
		udf_args.anon_gid = a_gid(str, MNTTAB_OPT_GROUP);
		XFREE(str);
	}
#endif /* HAVE_UDF_ARGS_T_ANON_GID */

#ifdef HAVE_UDF_ARGS_T_GMTOFF
	udf_args.gmtoff = 0;
	if ((str = hasmntstr(&mnt, MNTTAB_OPT_GMTOFF)) != NULL) {
		udf_args.gmtoff = a_num(str, MNTTAB_OPT_GMTOFF);
		XFREE(str);
	}
#endif /* HAVE_UDF_ARGS_T_GMTOFF */

#ifdef HAVE_UDF_ARGS_T_SESSIONNR
	udf_args.sessionnr = 0;
	if ((str = hasmntstr(&mnt, MNTTAB_OPT_SESSIONNR)) != NULL) {
		udf_args.sessionnr = a_num(str, MNTTAB_OPT_SESSIONNR);
		XFREE(str);
	}
#endif /* HAVE_UDF_ARGS_T_SESSIONNR */

#ifdef HAVE_UDF_ARGS_T_VERSION
# ifdef UDFMNT_VERSION
	udf_args.version = UDFMNT_VERSION;
# endif /* UDFMNT_VERSION */
#endif /* HAVE_UDF_ARGS_T_VERSION */

#ifdef HAVE_UDF_ARGS_T_FSPEC
	udf_args.fspec = fs_name;
#endif /* HAVE_UFS_ARGS_T_FSPEC */

	/*
	 * Call generic mount routine
	 */
	return mount_fs(&mnt, flags, (caddr_t)&udf_args, 0, type, 0, NULL,
	    mnttab_file_name, on_autofs);
}

static int
udf_mount(am_node *am, mntfs *mf)
{
	int on_autofs;
	int error;

	on_autofs = mf->mf_flags & MFF_ON_AUTOFS;
	error = mount_udf(mf->mf_mount, mf->mf_info, mf->mf_mopts, on_autofs);
	if (error) {
		errno = error;
		plog(XLOG_ERROR, "mount_udf: %m");
		return error;
	}
	return 0;
}


static int
udf_umount(am_node *am, mntfs *mf)
{
	int unmount_flags;

	unmount_flags = (mf->mf_flags & MFF_ON_AUTOFS) ? AMU_UMOUNT_AUTOFS : 0;
	return UMOUNT_FS(mf->mf_mount, mnttab_file_name, unmount_flags);
}
