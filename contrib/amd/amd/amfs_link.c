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
 * File: am-utils/amd/amfs_link.c
 *
 */

/*
 * Symbol-link file system
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* forward declarations */
static int amfs_link_mount(am_node *mp, mntfs *mf);
static int amfs_link_umount(am_node *mp, mntfs *mf);

/*
 * Ops structures
 */
am_ops amfs_link_ops =
{
  "link",
  amfs_link_match,
  0,				/* amfs_link_init */
  amfs_link_mount,
  amfs_link_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* amfs_link_readlink */
  0,				/* amfs_link_mounted */
  0,				/* amfs_link_umounted */
  amfs_generic_find_srvr,
  0,				/* nfs_fs_flags */
  0,				/* amfs_link_get_wchan */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_LINK_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


/*
 * SFS needs a link.
 */
char *
amfs_link_match(am_opts *fo)
{

  if (!fo->opt_fs) {
    plog(XLOG_USER, "link: no fs specified");
    return 0;
  }

  /*
   * If the link target points to another mount point, then we could
   * end up with an unpleasant situation, where the link f/s simply
   * "assumes" the mntfs of that mount point.
   *
   * For example, if the link points to /usr, and /usr is a real ufs
   * filesystem, then the link f/s will use the inherited ufs mntfs,
   * and the end result will be that it will become unmountable.
   *
   * To prevent this, we use a hack: we prepend a dot ('.') to opt_fs if
   * its original value was an absolute path, so that it will never match
   * any other mntfs.
   *
   * XXX: a less hacky solution should be used...
   */
  if (fo->opt_fs[0] == '/') {
    char *link_hack = str3cat(NULL, ".", fo->opt_fs, "");
    if (fo->opt_sublink == NULL || fo->opt_sublink[0] == '\0')
      fo->opt_sublink = xstrdup(fo->opt_fs);
    XFREE(fo->opt_fs);
    fo->opt_fs = link_hack;
  }

  return xstrdup(fo->opt_fs);
}


static int
amfs_link_mount(am_node *mp, mntfs *mf)
{
  return 0;
}


static int
amfs_link_umount(am_node *mp, mntfs *mf)
{
  return 0;
}
