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
 * File: am-utils/amd/mntfs.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

qelem mfhead = {&mfhead, &mfhead};

int mntfs_allocated;


am_loc *
dup_loc(am_loc *loc)
{
  loc->al_refc++;
  if (loc->al_mnt) {
    dup_mntfs(loc->al_mnt);
  }
  return loc;
}

mntfs *
dup_mntfs(mntfs *mf)
{
  if (mf->mf_refc == 0) {
    if (mf->mf_cid)
      untimeout(mf->mf_cid);
    mf->mf_cid = 0;
  }
  mf->mf_refc++;

  return mf;
}


static void
init_mntfs(mntfs *mf, am_ops *ops, am_opts *mo, char *mp, char *info, char *auto_opts, char *mopts, char *remopts)
{
  mf->mf_ops = ops;
  mf->mf_fsflags = ops->nfs_fs_flags;
  mf->mf_fo = 0;
  if (mo)
    mf->mf_fo = copy_opts(mo);

  mf->mf_mount = xstrdup(mp);
  mf->mf_info = xstrdup(info);
  mf->mf_auto = xstrdup(auto_opts);
  mf->mf_mopts = xstrdup(mopts);
  mf->mf_remopts = xstrdup(remopts);
  mf->mf_loopdev = NULL;
  mf->mf_refc = 1;
  mf->mf_flags = 0;
  mf->mf_error = -1;
  mf->mf_cid = 0;
  mf->mf_private = NULL;
  mf->mf_prfree = NULL;

  if (ops->ffserver)
    mf->mf_server = (*ops->ffserver) (mf);
  else
    mf->mf_server = NULL;
}


static mntfs *
alloc_mntfs(am_ops *ops, am_opts *mo, char *mp, char *info, char *auto_opts, char *mopts, char *remopts)
{
  mntfs *mf = ALLOC(struct mntfs);

  init_mntfs(mf, ops, mo, mp, info, auto_opts, mopts, remopts);
  ins_que(&mf->mf_q, &mfhead);
  mntfs_allocated++;

  return mf;
}


/* find a matching mntfs in our list */
mntfs *
locate_mntfs(am_ops *ops, am_opts *mo, char *mp, char *info, char *auto_opts, char *mopts, char *remopts)
{
  mntfs *mf;

  dlog("Locating mntfs reference to (%s,%s)", mp, info);

  ITER(mf, mntfs, &mfhead) {
    /*
     * For backwards compatibility purposes, we treat already-mounted
     * filesystems differently and only require a match of their mount point,
     * not of their server info. After all, there is little we can do if
     * the user asks us to mount two different things onto the same mount: one
     * will always cover the other one.
     */
    if (STREQ(mf->mf_mount, mp) &&
	((mf->mf_flags & MFF_MOUNTED && !(mf->mf_fsflags & FS_DIRECT))
	 || (STREQ(mf->mf_info, info) && mf->mf_ops == ops))) {
      /*
       * Handle cases where error ops are involved
       */
      if (ops == &amfs_error_ops) {
	/*
	 * If the existing ops are not amfs_error_ops
	 * then continue...
	 */
	if (mf->mf_ops != &amfs_error_ops)
	  continue;
	return dup_mntfs(mf);
      }

      dlog("mf->mf_flags = %#x", mf->mf_flags);

      if ((mf->mf_flags & MFF_RESTART) && amd_state < Finishing) {
	/*
	 * Restart a previously mounted filesystem.
	 */
	dlog("Restarting filesystem %s", mf->mf_mount);

	/*
	 * If we are restarting an amd internal filesystem,
	 * we need to initialize it a bit.
	 *
	 * We know it's internal because it is marked as toplvl.
	 */
	if (mf->mf_ops == &amfs_toplvl_ops) {
	  mf->mf_ops = ops;
	  mf->mf_info = strealloc(mf->mf_info, info);
	  ops->mounted(mf);	/* XXX: not right, but will do for now */
	}

	return mf;
      }

      if (!(mf->mf_flags & (MFF_MOUNTED | MFF_MOUNTING | MFF_UNMOUNTING))) {
	fserver *fs;
	mf->mf_flags &= ~MFF_ERROR;
	mf->mf_error = -1;
	mf->mf_auto = strealloc(mf->mf_auto, auto_opts);
	mf->mf_mopts = strealloc(mf->mf_mopts, mopts);
	mf->mf_remopts = strealloc(mf->mf_remopts, remopts);
	mf->mf_info = strealloc(mf->mf_info, info);

	if (mf->mf_private && mf->mf_prfree) {
	  mf->mf_prfree(mf->mf_private);
	  mf->mf_private = NULL;
	}

	fs = ops->ffserver ? (*ops->ffserver) (mf) : (fserver *) NULL;
	if (mf->mf_server)
	  free_srvr(mf->mf_server);
	mf->mf_server = fs;
      }
      return dup_mntfs(mf);
    } /* end of "if (STREQ(mf-> ..." */
  } /* end of ITER */

  return 0;
}


/* find a matching mntfs in our list, create a new one if none is found */
mntfs *
find_mntfs(am_ops *ops, am_opts *mo, char *mp, char *info, char *auto_opts, char *mopts, char *remopts)
{
  mntfs *mf = locate_mntfs(ops, mo, mp, info, auto_opts, mopts, remopts);
  if (mf)
    return mf;

  return alloc_mntfs(ops, mo, mp, info, auto_opts, mopts, remopts);
}


mntfs *
new_mntfs(void)
{
  return alloc_mntfs(&amfs_error_ops, (am_opts *) NULL, "//nil//", ".", "", "", "");
}

am_loc *
new_loc(void)
{
  am_loc *loc = CALLOC(struct am_loc);
  loc->al_fo = 0;
  loc->al_mnt = new_mntfs();
  loc->al_refc = 1;
  return loc;
}


static void
uninit_mntfs(mntfs *mf)
{
  if (mf->mf_fo) {
    free_opts(mf->mf_fo);
    XFREE(mf->mf_fo);
  }
  XFREE(mf->mf_auto);
  XFREE(mf->mf_mopts);
  XFREE(mf->mf_remopts);
  XFREE(mf->mf_info);
  if (mf->mf_private && mf->mf_prfree)
    (*mf->mf_prfree) (mf->mf_private);

  XFREE(mf->mf_mount);

  /*
   * Clean up the file server
   */
  if (mf->mf_server)
    free_srvr(mf->mf_server);

  /*
   * Don't do a callback on this mount
   */
  if (mf->mf_cid) {
    untimeout(mf->mf_cid);
    mf->mf_cid = 0;
  }
}


static void
discard_mntfs(voidp v)
{
  mntfs *mf = v;

  rem_que(&mf->mf_q);

  /*
   * Free memory
   */
  uninit_mntfs(mf);
  XFREE(mf);

  --mntfs_allocated;
}

static void
discard_loc(voidp v)
{
  am_loc *loc = v;
  if (loc->al_fo) {
    free_opts(loc->al_fo);
    XFREE(loc->al_fo);
  }
  XFREE(loc);
}

void
flush_mntfs(void)
{
  mntfs *mf;

  mf = AM_FIRST(mntfs, &mfhead);
  while (mf != HEAD(mntfs, &mfhead)) {
    mntfs *mf2 = mf;
    mf = NEXT(mntfs, mf);
    if (mf2->mf_refc == 0 && mf2->mf_cid)
      discard_mntfs(mf2);
  }
}

void
free_loc(opaque_t arg)
{
  am_loc *loc = (am_loc *) arg;
  dlog("free_loc %p", loc);

  if (loc->al_refc <= 0) {
    plog(XLOG_ERROR, "IGNORING free_loc for 0x%p", loc);
    return;
  }

  if (loc->al_mnt)
    free_mntfs(loc->al_mnt);
  if (--loc->al_refc == 0) {
    discard_loc(loc);
  }
}

void
free_mntfs(opaque_t arg)
{
  mntfs *mf = (mntfs *) arg;

  dlog("free_mntfs <%s> type %s mf_refc %d flags %x",
       mf->mf_mount, mf->mf_ops->fs_type, mf->mf_refc, mf->mf_flags);

  /*
   * We shouldn't ever be called to free something that has
   * a non-positive refcount.  Something is badly wrong if
   * we have been!  Ignore the request for now...
   */
  if (mf->mf_refc <= 0) {
    plog(XLOG_ERROR, "IGNORING free_mntfs for <%s>: refc %d, flags %x (bug?)",
         mf->mf_mount, mf->mf_refc, mf->mf_flags);
    return;
  }

  /* don't discard last reference of a restarted/kept mntfs */
  if (mf->mf_refc == 1 && mf->mf_flags & MFF_RSTKEEP) {
    plog(XLOG_ERROR, "IGNORING free_mntfs for <%s>: refc %d, flags %x (restarted)",
         mf->mf_mount, mf->mf_refc, mf->mf_flags);
    return;
  }

  if (--mf->mf_refc == 0) {
    if (mf->mf_flags & MFF_MOUNTED) {
      int quoted;
      mf->mf_flags &= ~MFF_MOUNTED;

      /*
       * Record for posterity
       */
      quoted = strchr(mf->mf_info, ' ') != 0;	/* cheap */
      plog(XLOG_INFO, "%s%s%s %sed fstype %s from %s",
	   quoted ? "\"" : "",
	   mf->mf_info,
	   quoted ? "\"" : "",
	   mf->mf_error ? "discard" : "unmount",
	   mf->mf_ops->fs_type, mf->mf_mount);
    }

    if (mf->mf_fsflags & FS_DISCARD) {
      dlog("Immediately discarding mntfs for %s", mf->mf_mount);
      discard_mntfs(mf);

    } else {

      if (mf->mf_flags & MFF_RESTART) {
	dlog("Discarding remount hook for %s", mf->mf_mount);
      } else {
	dlog("Discarding last mntfs reference to %s fstype %s",
	     mf->mf_mount, mf->mf_ops->fs_type);
      }
      if (mf->mf_flags & (MFF_MOUNTED | MFF_MOUNTING | MFF_UNMOUNTING))
	dlog("mntfs reference for %s still active", mf->mf_mount);
      mf->mf_cid = timeout(ALLOWED_MOUNT_TIME, discard_mntfs, (voidp) mf);
    }
  }
}


mntfs *
realloc_mntfs(mntfs *mf, am_ops *ops, am_opts *mo, char *mp, char *info, char *auto_opts, char *mopts, char *remopts)
{
  mntfs *mf2;

  if (mf->mf_refc == 1 &&
      mf->mf_flags & MFF_RESTART &&
      STREQ(mf->mf_mount, mp)) {
    /*
     * If we are inheriting then just return
     * the same node...
     */
    return mf;
  }

  /*
   * Re-use the existing mntfs if it is mounted.
   * This traps a race in nfsx.
   */
  if (mf->mf_ops != &amfs_error_ops &&
      (mf->mf_flags & MFF_MOUNTED) &&
      !FSRV_ISDOWN(mf->mf_server)) {
    return mf;
  }

  mf2 = find_mntfs(ops, mo, mp, info, auto_opts, mopts, remopts);
  free_mntfs(mf);
  return mf2;
}
