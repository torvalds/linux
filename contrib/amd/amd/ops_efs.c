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
 * File: am-utils/amd/ops_efs.c
 *
 */

/*
 * Irix UN*X file system: EFS (Extent File System)
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* forward declarations */
static char *efs_match(am_opts *fo);
static int efs_mount(am_node *am, mntfs *mf);
static int efs_umount(am_node *am, mntfs *mf);

/*
 * Ops structure
 */
am_ops efs_ops =
{
  "efs",
  efs_match,
  0,				/* efs_init */
  efs_mount,
  efs_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* efs_readlink */
  0,				/* efs_mounted */
  0,				/* efs_umounted */
  amfs_generic_find_srvr,
  0,				/* efs_get_wchan */
  FS_MKMNT | FS_NOTIMEOUT | FS_UBACKGROUND | FS_AMQINFO, /* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_EFS_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


/*
 * EFS needs local filesystem and device.
 */
static char *
efs_match(am_opts *fo)
{

  if (!fo->opt_dev) {
    plog(XLOG_USER, "efs: no device specified");
    return 0;
  }

  dlog("EFS: mounting device \"%s\" on \"%s\"", fo->opt_dev, fo->opt_fs);

  /*
   * Determine magic cookie to put in mtab
   */
  return xstrdup(fo->opt_dev);
}


static int
mount_efs(char *mntdir, char *fs_name, char *opts, int on_autofs)
{
  efs_args_t efs_args;
  mntent_t mnt;
  int flags;

  /*
   * Figure out the name of the file system type.
   */
  MTYPE_TYPE type = MOUNT_TYPE_EFS;

  memset((voidp) &efs_args, 0, sizeof(efs_args)); /* Paranoid */

  /*
   * Fill in the mount structure
   */
  memset((voidp) &mnt, 0, sizeof(mnt));
  mnt.mnt_dir = mntdir;
  mnt.mnt_fsname = fs_name;
  mnt.mnt_type = MNTTAB_TYPE_EFS;
  mnt.mnt_opts = opts;

  flags = compute_mount_flags(&mnt);
#ifdef HAVE_FS_AUTOFS
  if (on_autofs)
    flags |= autofs_compute_mount_flags(&mnt);
#endif /* HAVE_FS_AUTOFS */

#ifdef HAVE_EFS_ARGS_T_FLAGS
  efs_args.flags = 0;		/* XXX: fix this to correct flags */
#endif /* HAVE_EFS_ARGS_T_FLAGS */
#ifdef HAVE_EFS_ARGS_T_FSPEC
  efs_args.fspec = fs_name;
#endif /* HAVE_EFS_ARGS_T_FSPEC */
#if defined(HAVE_EFS_ARGS_T_VERSION) && defined(EFS_MNT_VERSION)
  efs_args.version = EFS_MNT_VERSION;
#endif /* HAVE_EFS_ARGS_T_VERSION && EFS_MNT_VERSION */

  /*
   * Call generic mount routine
   */
  return mount_fs(&mnt, flags, (caddr_t) &efs_args, 0, type, 0, NULL, mnttab_file_name, on_autofs);
}


static int
efs_mount(am_node *am, mntfs *mf)
{
  int on_autofs = mf->mf_flags & MFF_ON_AUTOFS;
  int error;

  error = mount_efs(mf->mf_mount, mf->mf_info, mf->mf_mopts, on_autofs);
  if (error) {
    errno = error;
    plog(XLOG_ERROR, "mount_efs: %m");
    return error;
  }

  return 0;
}


static int
efs_umount(am_node *am, mntfs *mf)
{
  int unmount_flags = (mf->mf_flags & MFF_ON_AUTOFS) ? AMU_UMOUNT_AUTOFS : 0;

  return UMOUNT_FS(mf->mf_mount, mnttab_file_name, unmount_flags);
}

