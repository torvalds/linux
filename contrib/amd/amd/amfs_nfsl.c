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
 * File: am-utils/amd/amfs_nfsl.c
 *
 */

/*
 * NFSL: Network file system with local existence check.  If the local
 * path denoted by $rfs exists, it behaves as type:=link.
 *
 * Example:
 *	pkg	type:=nfsl;rhost:=jonny;rfs:=/n/johnny/src/pkg;fs:=${rfs}
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>


/* forward declarations */
static char *amfs_nfsl_match(am_opts *fo);
static int amfs_nfsl_init(mntfs *mf);
static int amfs_nfsl_mount(am_node *mp, mntfs *mf);
static int amfs_nfsl_umount(am_node *mp, mntfs *mf);
static void amfs_nfsl_umounted(mntfs *mf);
static fserver *amfs_nfsl_ffserver(mntfs *mf);

/*
 * NFS-Link operations
 */
am_ops amfs_nfsl_ops =
{
  "nfsl",
  amfs_nfsl_match,
  amfs_nfsl_init,
  amfs_nfsl_mount,
  amfs_nfsl_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* amfs_nfsl_readlink */
  0,				/* amfs_nfsl_mounted */
  amfs_nfsl_umounted,
  amfs_nfsl_ffserver,
  0,				/* amfs_nfsl_get_wchan */
  FS_MKMNT | FS_BACKGROUND | FS_AMQINFO,	/* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_NFSL_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


/*
 * Check that f/s has all needed fields.
 * Returns: matched string if found, NULL otherwise.
 */
static char *
amfs_nfsl_match(am_opts *fo)
{
  char *cp;
  char *ho = fo->opt_rhost;
  char *retval;
  struct stat stb;

  if (fo->opt_sublink && fo->opt_sublink[0])
    cp = fo->opt_sublink;
  else
    cp = fo->opt_fs;

  if (!cp || !ho) {
    plog(XLOG_USER, "amfs_nfsl: host $fs and $rhost must be specified");
    return NULL;
  }

  /*
   * If this host is not the same as $rhost, or if link does not exist,
   * call nfs_ops.fs_match().
   * If link value exists (or same host), call amfs_link_ops.fs_match().
   */
  if (!STRCEQ(ho, am_get_hostname()) && !STRCEQ(ho, hostd)) {
    plog(XLOG_INFO, "amfs_nfsl: \"%s\" is not the local host \"%s\", "
	"or \"%s\" using type:=nfs", ho, am_get_hostname(), hostd);
    retval = nfs_ops.fs_match(fo);
  } else if (lstat(cp, &stb) < 0) {
    plog(XLOG_INFO, "amfs_nfsl: \"%s\" does not exist, using type:=nfs", cp);
    retval = nfs_ops.fs_match(fo);
  } else {
    plog(XLOG_INFO, "amfs_nfsl: \"%s\" exists, using type:=link", cp);
    retval = amfs_link_ops.fs_match(fo);
  }
  return retval;
}


/*
 * Initialize.
 * Returns: 0 if OK, non-zero (errno) if failed.
 */
static int
amfs_nfsl_init(mntfs *mf)
{
  int ret = 0;
  if (mf->mf_flags & MFF_NFSLINK) {
    if (amfs_link_ops.fs_init)
      ret = amfs_link_ops.fs_init(mf);
  } else {
    if (nfs_ops.fs_init)
      ret = nfs_ops.fs_init(mf);
  }
  return ret;
}


/*
 * Mount vfs.
 * Returns: 0 if OK, non-zero (errno) if failed.
 */
static int
amfs_nfsl_mount(am_node *mp, mntfs *mf)
{
  int ret = 0;
  if (mf->mf_flags & MFF_NFSLINK) {
    if (amfs_link_ops.mount_fs)
      ret = amfs_link_ops.mount_fs(mp, mf);
  } else {
    if (nfs_ops.mount_fs)
      ret = nfs_ops.mount_fs(mp, mf);
  }
  return ret;
}


/*
 * Unmount VFS.
 * Returns: 0 if OK, non-zero (errno) if failed.
 */
static int
amfs_nfsl_umount(am_node *mp, mntfs *mf)
{
  int ret = 0;
  if (mf->mf_flags & MFF_NFSLINK) {
    if (amfs_link_ops.umount_fs)
      ret = amfs_link_ops.umount_fs(mp, mf);
  } else {
    if (nfs_ops.umount_fs)
      ret = nfs_ops.umount_fs(mp, mf);
  }
  return ret;
}


/*
 * Async unmount callback function.
 * After the base umount() succeeds, we may want to take extra actions,
 * such as informing remote mount daemons that we've unmounted them.
 * See amfs_auto_umounted(), host_umounted(), nfs_umounted().
 */
static void
amfs_nfsl_umounted(mntfs *mf)
{
  if (mf->mf_flags & MFF_NFSLINK) {
    if (amfs_link_ops.umounted)
      amfs_link_ops.umounted(mf);
  } else {
    if (nfs_ops.umounted)
      nfs_ops.umounted(mf);
  }
}


/*
 * Find a file server.
 * Returns: fserver of found server, or NULL if not found.
 */
static fserver *
amfs_nfsl_ffserver(mntfs *mf)
{
  char *cp, *ho;
  struct stat stb;

  if (mf->mf_fo == NULL) {
    plog(XLOG_ERROR, "%s: NULL mf_fo", __func__);
    return NULL;
  }
  ho = mf->mf_fo->opt_rhost;

  if (mf->mf_fo->opt_sublink && mf->mf_fo->opt_sublink[0])
    cp = mf->mf_fo->opt_sublink;
  else
    cp = mf->mf_fo->opt_fs;

  /*
   * If this host is not the same as $rhost, or if link does not exist,
   * call amfs_link_ops.ffserver().
   * If link value exists (or same host), then call ops_nfs.ffserver().
   */
  if ((!STRCEQ(ho, am_get_hostname()) &&
       !STRCEQ(ho, hostd)) || lstat(cp, &stb) < 0) {
    return nfs_ops.ffserver(mf);
  } else {
    mf->mf_flags |= MFF_NFSLINK;
    /* remove the FS_MKMNT flag, we don't want amd touching the mountpoint */
    mf->mf_fsflags &= ~FS_MKMNT;
    return amfs_link_ops.ffserver(mf);
  }
}
