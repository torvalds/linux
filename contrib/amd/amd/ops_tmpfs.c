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
 * File: am-utils/amd/ops_tmpfs.c
 *
 */

/*
 * TMPFS file system (combines RAM-fs and swap-fs)
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* forward declarations */
static char *tmpfs_match(am_opts *fo);
static int tmpfs_mount(am_node *am, mntfs *mf);
static int tmpfs_umount(am_node *am, mntfs *mf);

/*
 * Ops structure
 */
am_ops tmpfs_ops =
{
  "tmpfs",
  tmpfs_match,
  0,				/* tmpfs_init */
  tmpfs_mount,
  tmpfs_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* tmpfs_readlink */
  0,				/* tmpfs_mounted */
  0,				/* tmpfs_umounted */
  amfs_generic_find_srvr,
  0,				/* tmpfs_get_wchan */
  FS_MKMNT | FS_NOTIMEOUT | FS_UBACKGROUND | FS_AMQINFO, /* nfs_fs_flags */
#if defined(HAVE_FS_AUTOFS) && defined(AUTOFS_TMPFS_FS_FLAGS)
  AUTOFS_TMPFS_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


/*
 * EFS needs local filesystem and device.
 */
static char *
tmpfs_match(am_opts *fo)
{

  if (!fo->opt_dev) {
    plog(XLOG_USER, "tmpfs: no device specified");
    return 0;
  }

  dlog("EFS: mounting device \"%s\" on \"%s\"", fo->opt_dev, fo->opt_fs);

  /*
   * Determine magic cookie to put in mtab
   */
  return xstrdup(fo->opt_dev);
}


static int
mount_tmpfs(char *mntdir, char *fs_name, char *opts, int on_autofs)
{
  tmpfs_args_t tmpfs_args;
  mntent_t mnt;
  int flags;
  const char *p;

  /*
   * Figure out the name of the file system type.
   */
  MTYPE_TYPE type = MOUNT_TYPE_TMPFS;

  p = NULL;
  memset((voidp) &tmpfs_args, 0, sizeof(tmpfs_args)); /* Paranoid */

  /*
   * Fill in the mount structure
   */
  memset((voidp) &mnt, 0, sizeof(mnt));
  mnt.mnt_dir = mntdir;
  mnt.mnt_fsname = fs_name;
  mnt.mnt_type = MNTTAB_TYPE_TMPFS;
  mnt.mnt_opts = opts;

  flags = compute_mount_flags(&mnt);
#ifdef HAVE_FS_AUTOFS
  if (on_autofs)
    flags |= autofs_compute_mount_flags(&mnt);
#endif /* HAVE_FS_AUTOFS */

#if defined(HAVE_TMPFS_ARGS_T_TA_VERSION) && defined(TMPFS_ARGS_VERSION)
  tmpfs_args.ta_version = TMPFS_ARGS_VERSION;
#endif /* HAVE_TMPFS_ARGS_T_TA_VERSION && TMPFS_ARGS_VERSION */
#ifdef HAVE_TMPFS_ARGS_T_TA_NODES_MAX
  if ((p = amu_hasmntopt(&mnt, "nodes")) == NULL)
	p = "1000000";
  tmpfs_args.ta_nodes_max = atoi(p);
#endif /* HAVE_TMPFS_ARGS_T_TA_SIZE_MAX */
#ifdef HAVE_TMPFS_ARGS_T_TA_SIZE_MAX
  if ((p = amu_hasmntopt(&mnt, "size")) == NULL)
	p = "10000000";
  tmpfs_args.ta_size_max = atoi(p);
#endif /* HAVE_TMPFS_ARGS_T_TA_SIZE_MAX */
#ifdef HAVE_TMPFS_ARGS_T_TA_ROOT_UID
  if ((p = amu_hasmntopt(&mnt, "uid")) == NULL)
	p = "0";
  tmpfs_args.ta_root_uid = atoi(p);
#endif /* HAVE_TMPFS_ARGS_T_TA_ROOT_UID */
#ifdef HAVE_TMPFS_ARGS_T_TA_ROOT_GID
  if ((p = amu_hasmntopt(&mnt, "gid")) == NULL)
	p = "0";
  tmpfs_args.ta_root_gid = atoi(p);
#endif /* HAVE_TMPFS_ARGS_T_TA_ROOT_GID */
#ifdef HAVE_TMPFS_ARGS_T_TA_ROOT_MODE
  if ((p = amu_hasmntopt(&mnt, "mode")) == NULL)
	p = "01777";
  tmpfs_args.ta_root_mode = strtol(p, NULL, 8);
#endif /* HAVE_TMPFS_ARGS_T_TA_ROOT_MODE */

  /*
   * Call generic mount routine
   */
  return mount_fs(&mnt, flags, (caddr_t) &tmpfs_args, 0, type, 0, NULL, mnttab_file_name, on_autofs);
}


static int
tmpfs_mount(am_node *am, mntfs *mf)
{
  int on_autofs = mf->mf_flags & MFF_ON_AUTOFS;
  int error;

  error = mount_tmpfs(mf->mf_mount, mf->mf_info, mf->mf_mopts, on_autofs);
  if (error) {
    errno = error;
    plog(XLOG_ERROR, "mount_tmpfs: %m");
    return error;
  }

  return 0;
}


static int
tmpfs_umount(am_node *am, mntfs *mf)
{
  int unmount_flags = (mf->mf_flags & MFF_ON_AUTOFS) ? AMU_UMOUNT_AUTOFS : 0;

  return UMOUNT_FS(mf->mf_mount, mnttab_file_name, unmount_flags);
}

