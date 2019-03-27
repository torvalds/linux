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
 * File: am-utils/amd/ops_ext.c
 *
 */

/*
 * Irix UN*X file system: EXT (Extended File System)
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* forward declarations */
static char *ext_match(am_opts *fo);
static int ext2_mount(am_node *am, mntfs *mf);
static int ext3_mount(am_node *am, mntfs *mf);
static int ext4_mount(am_node *am, mntfs *mf);
static int ext_umount(am_node *am, mntfs *mf);

/*
 * Ops structure
 */
am_ops ext2_ops =
{
  "ext2",
  ext_match,
  0,				/* ext_init */
  ext2_mount,
  ext_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* ext_readlink */
  0,				/* ext_mounted */
  0,				/* ext_umounted */
  amfs_generic_find_srvr,
  0,				/* ext_get_wchan */
  FS_MKMNT | FS_NOTIMEOUT | FS_UBACKGROUND | FS_AMQINFO, /* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_EXT_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};

am_ops ext3_ops =
{
  "ext3",
  ext_match,
  0,				/* ext_init */
  ext3_mount,
  ext_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* ext_readlink */
  0,				/* ext_mounted */
  0,				/* ext_umounted */
  amfs_generic_find_srvr,
  0,				/* ext_get_wchan */
  FS_MKMNT | FS_NOTIMEOUT | FS_UBACKGROUND | FS_AMQINFO, /* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_EXT_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};

am_ops ext4_ops =
{
  "ext4",
  ext_match,
  0,				/* ext_init */
  ext4_mount,
  ext_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* ext_readlink */
  0,				/* ext_mounted */
  0,				/* ext_umounted */
  amfs_generic_find_srvr,
  0,				/* ext_get_wchan */
  FS_MKMNT | FS_NOTIMEOUT | FS_UBACKGROUND | FS_AMQINFO, /* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_EXT_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};

/*
 * EXT needs local filesystem and device.
 */
static char *
ext_match(am_opts *fo)
{

  if (!fo->opt_dev) {
    plog(XLOG_USER, "ext: no device specified");
    return 0;
  }

  dlog("EXT: mounting device \"%s\" on \"%s\"", fo->opt_dev, fo->opt_fs);

  /*
   * Determine magic cookie to put in mtab
   */
  return xstrdup(fo->opt_dev);
}


static int
mount_ext(char *mntdir, char *fs_name, char *opts, int on_autofs, char *
    mount_type, const char *mnttab_type)
{
  ext_args_t ext_args;
  mntent_t mnt;
  int flags;

  /*
   * Figure out the name of the file system type.
   */
  MTYPE_TYPE type = mount_type;

  memset((voidp) &ext_args, 0, sizeof(ext_args)); /* Paranoid */

  /*
   * Fill in the mount structure
   */
  memset((voidp) &mnt, 0, sizeof(mnt));
  mnt.mnt_dir = mntdir;
  mnt.mnt_fsname = fs_name;
  mnt.mnt_type = mnttab_type;
  mnt.mnt_opts = opts;

  flags = compute_mount_flags(&mnt);
#ifdef HAVE_FS_AUTOFS
  if (on_autofs)
    flags |= autofs_compute_mount_flags(&mnt);
#endif /* HAVE_FS_AUTOFS */

  /*
   * Call generic mount routine
   */
  return mount_fs(&mnt, flags, (caddr_t) &ext_args, 0, type, 0, NULL, mnttab_file_name, on_autofs);
}


static int
ext_mount(am_node *am, mntfs *mf, char *mount_type,
  const char *mnttab_type)
{
  int on_autofs = mf->mf_flags & MFF_ON_AUTOFS;
  int error;

  error = mount_ext(mf->mf_mount, mf->mf_info, mf->mf_mopts, on_autofs,
      mount_type, mnttab_type);
  if (error) {
    errno = error;
    plog(XLOG_ERROR, "mount_ext: %m");
    return error;
  }

  return 0;
}

static int
ext2_mount(am_node *am, mntfs *mf)
{
  return ext_mount(am, mf, MOUNT_TYPE_EXT2, MNTTAB_TYPE_EXT2);
}

static int
ext3_mount(am_node *am, mntfs *mf)
{
  return ext_mount(am, mf, MOUNT_TYPE_EXT3, MNTTAB_TYPE_EXT3);
}

static int
ext4_mount(am_node *am, mntfs *mf)
{
  return ext_mount(am, mf, MOUNT_TYPE_EXT4, MNTTAB_TYPE_EXT4);
}

static int
ext_umount(am_node *am, mntfs *mf)
{
  int unmount_flags = (mf->mf_flags & MFF_ON_AUTOFS) ? AMU_UMOUNT_AUTOFS : 0;

  return UMOUNT_FS(mf->mf_mount, mnttab_file_name, unmount_flags);
}
