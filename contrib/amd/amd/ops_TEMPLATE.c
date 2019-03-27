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
 * File: am-utils/amd/ops_TEMPLATE.c
 *
 */

/*
 * An empty template for an amd pseudo filesystem "foofs".
 */

/*
 * NOTE: if this is an Amd file system, prepend "amfs_" to all foofs symbols
 * and renamed the file name to amfs_foofs.c.  If it is a native file system
 * (such as pcfs, isofs, or ffs), then you can keep the names as is, and
 * just rename the file to ops_foofs.c.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* forward declarations */
static char *foofs_match(am_opts *fo);
static int foofs_init(mntfs *mf);
static int foofs_mount(am_node *mp, mntfs *mf);
static int foofs_umount(am_node *mp, mntfs *mf);
static am_node *foofs_lookuppn(am_node *mp, char *fname, int *error_return, int op);
static int foofs_readdir(am_node *mp, void cookie, voidp dp, voidp ep, u_int count);
static am_node *foofs_readlink(am_node *mp, int *error_return);
static void foofs_mounted(am_node *am, mntfs *mf);
static void foofs_umounted(am_node *mp, mntfs *mf);
static fserver *foofs_ffserver(mntfs *mf);


/*
 * Foofs operations.
 * Define only those you need, others set to 0 (NULL)
 */
am_ops foofs_ops =
{
  "foofs",			/* name of file system */
  foofs_match,			/* match */
  foofs_init,			/* initialize */
  foofs_mount,			/* mount vnode */
  foofs_umount,			/* unmount vnode */
  foofs_lookup_child,		/* lookup path-name */
  foofs_mount_child,		/* mount path-name */
  foofs_readdir,		/* read directory */
  foofs_readlink,		/* read link */
  foofs_mounted,		/* after-mount extra actions */
  foofs_umounted,		/* after-umount extra actions */
  foofs_ffserver,		/* find a file server */
  foofs_get_wchan,		/* return the waiting channel */
  FS_MKMNT | FS_BACKGROUND | FS_AMQINFO,	/* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_TEMPLATE_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


/*
 * Check that f/s has all needed fields.
 * Returns: matched string if found, NULL otherwise.
 */
static char *
foofs_match(am_opts *fo)
{
  char *cp = "fill this with a way to find the match";

  plog(XLOG_INFO, "entering foofs_match...");

  if (cp)
    return cp;			/* OK */

  return NULL;			/* not OK */
}


/*
 * Initialize.
 * Returns: 0 if OK, non-zero (errno) if failed.
 */
static int
foofs_init(mntfs *mf)
{
  int error = 0;

  plog(XLOG_INFO, "entering foofs_init...");

  error = EPERM;		/* XXX: fixme */
  return error;
}


/*
 * Mount vnode.
 * Returns: 0 if OK, non-zero (errno) if failed.
 */
static int
foofs_mount(am_node *mp)
{
  int error = 0;

  plog(XLOG_INFO, "entering foofs_mount...");

  error = EPERM;		/* XXX: fixme */
  return error;
}


/*
 * Mount vfs.
 * Returns: 0 if OK, non-zero (errno) if failed.
 */
static int
foofs_fmount(mntfs *mf)
{
  int error = 0;

  plog(XLOG_INFO, "entering foofs_fmount...");

  error = EPERM;		/* XXX: fixme */
  return error;
}


/*
 * Unmount vnode.
 * Returns: 0 if OK, non-zero (errno) if failed.
 */
static int
foofs_umount(am_node *mp)
{
  int error = 0;

  plog(XLOG_INFO, "entering foofs_umount...");

  error = EPERM;		/* XXX: fixme */
  return error;
}


/*
 * Unmount VFS.
 * Returns: 0 if OK, non-zero (errno) if failed.
 */
static int
foofs_fumount(mntfs *mf)
{
  int error = 0;

  plog(XLOG_INFO, "entering foofs_fumount...");

  error = EPERM;		/* XXX: fixme */
  return error;
}


/*
 * Lookup path-name.
 * Returns: the am_node that was found, or NULL if failed.
 * If failed, also fills in errno in error_return.
 */
static am_node *
foofs_lookuppn(am_node *mp, char *fname, int *error_return, int op)
{
  int error = 0;

  plog(XLOG_INFO, "entering foofs_lookuppn...");

  error = EPERM;			/* XXX: fixme */

  *error_return = error;
  return NULL;
}


/*
 * Read directory.
 * Returns: 0 if OK, non-zero (errno) if failed.
 * If OK, fills in ep with chain of directory entries.
 */
static int
foofs_readdir(am_node *mp, void cookie, voidp dp, voidp ep, u_int count)
{
  int error = 0;

  plog(XLOG_INFO, "entering foofs_readdir...");

  error = EPERM;		/* XXX: fixme */
  return error;
}


/*
 * Read link.
 * Returns: am_node found, or NULL if not found.
 * If failed, fills in errno in error_return.
 */
static am_node *
foofs_readlink(am_node *mp, int *error_return)
{
  int error = 0;

  plog(XLOG_INFO, "entering foofs_readlink...");

  error = EPERM;			/* XXX: fixme */

  *error_return = error;
  return NULL;
}


/*
 * Async mount callback function.
 * After the base mount went OK, sometimes
 * there are additional actions that are needed.  See union_mounted() and
 * toplvl_mounted().
 */
static void
foofs_mounted(mntfs *mf)
{
  plog(XLOG_INFO, "entering foofs_mounted...");

  return;
}


/*
 * Async unmount callback function.
 * After the base umount() succeeds, we may want to take extra actions,
 * such as informing remote mount daemons that we've unmounted them.
 * See amfs_auto_umounted(), host_umounted(), nfs_umounted().
 */
static void
foofs_umounted(am_node *mp)
{
  plog(XLOG_INFO, "entering foofs_umounted...");

  return;
}


/*
 * Find a file server.
 * Returns: fserver of found server, or NULL if not found.
 */
static fserver *
foofs_ffserver(mntfs *mf)
{
  plog(XLOG_INFO, "entering foofs_ffserver...");

  return NULL;
}


/*
 * Normally just return mf. Only inherit needs to do special tricks.
 */
static wchan_t *
foofs_get_wchan(mntfs *mf)
{
  plog(XLOG_INFO, "entering foofs_get_wchan...");

  return mf;
}
