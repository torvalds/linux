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
 * File: am-utils/conf/mtab/mtab_bsd.c
 *
 */

/*
 * BSD 4.4 systems don't write their mount tables on a file.  Instead, they
 * use a (better) system where the kernel keeps this state, and you access
 * the mount tables via a known interface.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amu.h>

#if __NetBSD_Version__ > 200030000
#define statfs statvfs
#endif

static mntent_t *
mnt_dup(struct statfs *mp)
{
  mntent_t *new_mp = ALLOC(mntent_t);
  char *ty;

  new_mp->mnt_fsname = xstrdup(mp->f_mntfromname);
  new_mp->mnt_dir = xstrdup(mp->f_mntonname);

#ifdef HAVE_STRUCT_STATFS_F_FSTYPENAME
  ty = mp->f_fstypename;
#else /* not HAVE_STRUCT_STATFS_F_FSTYPENAME */
  switch (mp->f_type) {

# if defined(MOUNT_UFS) && defined(MNTTAB_TYPE_UFS)
  case MOUNT_UFS:
    ty = MNTTAB_TYPE_UFS;
    break;
# endif /* defined(MOUNT_UFS) && defined(MNTTAB_TYPE_UFS) */

# if defined(MOUNT_NFS) && defined(MNTTAB_TYPE_NFS)
  case MOUNT_NFS:
    ty = MNTTAB_TYPE_NFS;
    break;
# endif /* defined(MOUNT_NFS) && defined(MNTTAB_TYPE_NFS) */

# if defined(MOUNT_MFS) && defined(MNTTAB_TYPE_MFS)
  case MOUNT_MFS:
    ty = MNTTAB_TYPE_MFS;
    break;
# endif /* defined(MOUNT_MFS) && defined(MNTTAB_TYPE_MFS) */

  default:
    ty = "unknown";

    break;
  }
#endif /* not HAVE_STRUCT_STATFS_F_FSTYPENAME */

  new_mp->mnt_type = xstrdup(ty);
  new_mp->mnt_opts = xstrdup("unset");
  new_mp->mnt_freq = 0;
  new_mp->mnt_passno = 0;

  return new_mp;
}


/*
 * Read a mount table into memory
 */
mntlist *
read_mtab(char *fs, const char *mnttabname)
{
  mntlist **mpp, *mhp;
  struct statfs *mntbufp, *mntp;

  int nloc = getmntinfo(&mntbufp, MNT_NOWAIT);

  if (nloc == 0) {
    plog(XLOG_ERROR, "Can't read mount table");
    return 0;
  }
  mpp = &mhp;
  for (mntp = mntbufp; mntp < mntbufp + nloc; mntp++) {
    /*
     * Allocate a new slot
     */
    *mpp = ALLOC(struct mntlist);

    /*
     * Copy the data returned by getmntent
     */
    (*mpp)->mnt = mnt_dup(mntp);

    /*
     * Move to next pointer
     */
    mpp = &(*mpp)->mnext;
  }

  /*
   * Terminate the list
   */
  *mpp = NULL;

  return mhp;
}
