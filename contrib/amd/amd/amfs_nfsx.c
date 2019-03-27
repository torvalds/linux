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
 * File: am-utils/amd/amfs_nfsx.c
 *
 */

/*
 * NFS hierarchical mounts
 *
 * TODO: Re-implement.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/*
 * The rfs field contains a list of mounts to be done from
 * the remote host.
 */
typedef struct amfs_nfsx_mnt {
  mntfs *n_mnt;
  int n_error;
} amfs_nfsx_mnt;

struct amfs_nfsx {
  int nx_c;			/* Number of elements in nx_v */
  amfs_nfsx_mnt *nx_v;		/* Underlying mounts */
  amfs_nfsx_mnt *nx_try;
  am_node *nx_mp;
};

/* forward definitions */
static char *amfs_nfsx_match(am_opts *fo);
static int amfs_nfsx_mount(am_node *am, mntfs *mf);
static int amfs_nfsx_umount(am_node *am, mntfs *mf);
static int amfs_nfsx_init(mntfs *mf);

/*
 * Ops structure
 */
am_ops amfs_nfsx_ops =
{
  "nfsx",
  amfs_nfsx_match,
  amfs_nfsx_init,
  amfs_nfsx_mount,
  amfs_nfsx_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* amfs_nfsx_readlink */
  0,				/* amfs_nfsx_mounted */
  0,				/* amfs_nfsx_umounted */
  find_nfs_srvr,		/* XXX */
  0,				/* amfs_nfsx_get_wchan */
  /* FS_UBACKGROUND| */ FS_AMQINFO,	/* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_NFSX_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


static char *
amfs_nfsx_match(am_opts *fo)
{
  char *xmtab;
  char *ptr;
  int len;

  if (!fo->opt_rfs) {
    plog(XLOG_USER, "amfs_nfsx: no remote filesystem specified");
    return FALSE;
  }

  if (!fo->opt_rhost) {
    plog(XLOG_USER, "amfs_nfsx: no remote host specified");
    return FALSE;
  }

  /* set default sublink */
  if (fo->opt_sublink == NULL || fo->opt_sublink[0] == '\0') {
    ptr = strchr(fo->opt_rfs, ',');
    if (ptr && ptr > (fo->opt_rfs + 1))
      fo->opt_sublink = strnsave(fo->opt_rfs + 1, ptr - fo->opt_rfs - 1);
  }

  /*
   * Remove trailing ",..." from ${fs}
   * After deslashifying, overwrite the end of ${fs} with "/"
   * to make sure it is unique.
   */
  if ((ptr = strchr(fo->opt_fs, ',')))
    *ptr = '\0';
  deslashify(fo->opt_fs);

  /*
   * Bump string length to allow trailing /
   */
  len = strlen(fo->opt_fs);
  fo->opt_fs = xrealloc(fo->opt_fs, len + 1 + 1);
  ptr = fo->opt_fs + len;

  /*
   * Make unique...
   */
  *ptr++ = '/';
  *ptr = '\0';

  /*
   * Determine magic cookie to put in mtab
   */
  xmtab = str3cat((char *) NULL, fo->opt_rhost, ":", fo->opt_rfs);
  dlog("NFSX: mounting remote server \"%s\", remote fs \"%s\" on \"%s\"",
       fo->opt_rhost, fo->opt_rfs, fo->opt_fs);

  return xmtab;
}


static void
amfs_nfsx_prfree(opaque_t vp)
{
  struct amfs_nfsx *nx = (struct amfs_nfsx *) vp;
  int i;

  for (i = 0; i < nx->nx_c; i++) {
    mntfs *m = nx->nx_v[i].n_mnt;
    if (m)
      free_mntfs(m);
  }

  XFREE(nx->nx_v);
  XFREE(nx);
}


static int
amfs_nfsx_init(mntfs *mf)
{
  /*
   * mf_info has the form:
   *   host:/prefix/path,sub,sub,sub
   */
  int i;
  int glob_error;
  struct amfs_nfsx *nx;
  int asked_for_wakeup = 0;

  nx = (struct amfs_nfsx *) mf->mf_private;

  if (nx == 0) {
    char **ivec;
    char *info = NULL;
    char *host;
    char *pref;
    int error = 0;

    info = xstrdup(mf->mf_info);
    if (info == NULL)
      return errno;

    host = strchr(info, ':');
    if (!host) {
      error = EINVAL;
      goto errexit;
    }
    pref = host + 1;
    host = info;

    /*
     * Split the prefix off from the suffices
     */
    ivec = strsplit(pref, ',', '\'');

    /*
     * Count array size
     */
    for (i = 0; ivec[i]; i++)
      /* nothing */;

    nx = ALLOC(struct amfs_nfsx);
    mf->mf_private = (opaque_t) nx;
    mf->mf_prfree = amfs_nfsx_prfree;

    nx->nx_c = i - 1;		/* i-1 because we don't want the prefix */
    nx->nx_v = (amfs_nfsx_mnt *) xmalloc(nx->nx_c * sizeof(amfs_nfsx_mnt));
    nx->nx_mp = NULL;
    {
      char *mp = NULL;
      char *xinfo = NULL;
      char *fs = mf->mf_fo->opt_fs;
      char *rfs = NULL;
      for (i = 0; i < nx->nx_c; i++) {
	char *path = ivec[i + 1];
	rfs = str3cat(rfs, pref, "/", path);
	/*
	 * Determine the mount point.
	 * If this is the root, then don't remove
	 * the trailing slash to avoid mntfs name clashes.
	 */
	mp = str3cat(mp, fs, "/", rfs);
	normalize_slash(mp);
	deslashify(mp);
	/*
	 * Determine the mount info
	 */
	xinfo = str3cat(xinfo, host, *path == '/' ? "" : "/", path);
	normalize_slash(xinfo);
	if (pref[1] != '\0')
	  deslashify(xinfo);
	dlog("amfs_nfsx: init mount for %s on %s", xinfo, mp);
	nx->nx_v[i].n_error = -1;
	nx->nx_v[i].n_mnt = find_mntfs(&nfs_ops, mf->mf_fo, mp, xinfo, "", mf->mf_mopts, mf->mf_remopts);
	/* propagate the on_autofs flag */
	nx->nx_v[i].n_mnt->mf_flags |= mf->mf_flags & MFF_ON_AUTOFS;
      }
      XFREE(rfs);
      XFREE(mp);
      XFREE(xinfo);
    }

    XFREE(ivec);
  errexit:
    XFREE(info);
    if (error)
      return error;
  }

  /*
   * Iterate through the mntfs's and call
   * the underlying init routine on each
   */
  glob_error = 0;

  for (i = 0; i < nx->nx_c; i++) {
    amfs_nfsx_mnt *n = &nx->nx_v[i];
    mntfs *m = n->n_mnt;
    int error = 0;
    if (m->mf_ops->fs_init && !(mf->mf_flags & MFF_RESTART))
      error = m->mf_ops->fs_init(m);
    /*
     * if you just "return error" here, you will have made a failure
     * in any submounts to fail the whole group.  There was old unused code
     * here before.
     */
    if (error > 0)
      n->n_error = error;

    else if (error < 0) {
      glob_error = -1;
      if (!asked_for_wakeup) {
	asked_for_wakeup = 1;
	sched_task(wakeup_task, (opaque_t) mf, get_mntfs_wchan(m));
      }
    }
  }

  return glob_error;
}


static void
amfs_nfsx_cont(int rc, int term, opaque_t arg)
{
  mntfs *mf = (mntfs *) arg;
  struct amfs_nfsx *nx = (struct amfs_nfsx *) mf->mf_private;
  am_node *mp = nx->nx_mp;
  amfs_nfsx_mnt *n = nx->nx_try;

  n->n_mnt->mf_flags &= ~(MFF_ERROR | MFF_MOUNTING);
  mf->mf_flags &= ~MFF_ERROR;

  /*
   * Wakeup anything waiting for this mount
   */
  wakeup(get_mntfs_wchan(n->n_mnt));

  if (rc || term) {
    if (term) {
      /*
       * Not sure what to do for an error code.
       */
      plog(XLOG_ERROR, "mount for %s got signal %d", n->n_mnt->mf_mount, term);
      n->n_error = EIO;
    } else {
      /*
       * Check for exit status
       */
      errno = rc;		/* XXX */
      plog(XLOG_ERROR, "%s: mount (amfs_nfsx_cont): %m", n->n_mnt->mf_mount);
      n->n_error = rc;
    }
    free_mntfs(n->n_mnt);
    n->n_mnt = new_mntfs();
    n->n_mnt->mf_error = n->n_error;
    n->n_mnt->mf_flags |= MFF_ERROR;
  } else {
    /*
     * The mount worked.
     */
    mf_mounted(n->n_mnt, FALSE); /* FALSE => don't free the n_mnt->am_opts */
    n->n_error = 0;
  }

  /*
   * Do the remaining bits
   */
  if (amfs_nfsx_mount(mp, mf) >= 0)
    wakeup(get_mntfs_wchan(mf));
}


static int
try_amfs_nfsx_mount(opaque_t mv)
{
  mntfs *mf = (mntfs *) mv;
  struct amfs_nfsx *nx = (struct amfs_nfsx *) mf->mf_private;
  am_node *mp = nx->nx_mp;
  int error;

  error = mf->mf_ops->mount_fs(mp, mf);

  return error;
}


static int
amfs_nfsx_remount(am_node *am, mntfs *mf, int fg)
{
  struct amfs_nfsx *nx = (struct amfs_nfsx *) mf->mf_private;
  amfs_nfsx_mnt *n;
  int glob_error = -1;

  /* Save the am_node pointer for later use */
  nx->nx_mp = am;

  /*
   * Iterate through the mntfs's and mount each filesystem
   * which is not yet mounted.
   */
  for (n = nx->nx_v; n < nx->nx_v + nx->nx_c; n++) {
    mntfs *m = n->n_mnt;

    if (m->mf_flags & MFF_MOUNTING)
      break;

    if (m->mf_flags & MFF_MOUNTED) {
      mf_mounted(m, FALSE);	/* FALSE => don't free the m->am_opts */
      n->n_error = glob_error = 0;
      continue;
    }

    if (n->n_error < 0) {
      /* Create the mountpoint, if and as required */
      if (!(m->mf_flags & MFF_MKMNT) && m->mf_fsflags & FS_MKMNT) {
	if (!mkdirs(m->mf_mount, 0555))
	  m->mf_flags |= MFF_MKMNT;
      }

      dlog("calling underlying mount on %s", m->mf_mount);
      if (!fg && foreground && (m->mf_fsflags & FS_MBACKGROUND)) {
	m->mf_flags |= MFF_MOUNTING;
	dlog("backgrounding mount of \"%s\"", m->mf_info);
	nx->nx_try = n;
	run_task(try_amfs_nfsx_mount, (opaque_t) m, amfs_nfsx_cont, (opaque_t) mf);
	n->n_error = -1;
	return -1;
      } else {
	dlog("foreground mount of \"%s\" ...", mf->mf_info);
	n->n_error = m->mf_ops->mount_fs(am, m);
      }

      if (n->n_error > 0)
	dlog("underlying fmount of %s failed: %s", m->mf_mount, strerror(n->n_error));

      if (n->n_error == 0) {
	glob_error = 0;
      } else if (glob_error < 0) {
	glob_error = n->n_error;
      }
    }
  }

  return glob_error < 0 ? 0 : glob_error;
}


static int
amfs_nfsx_mount(am_node *am, mntfs *mf)
{
  return amfs_nfsx_remount(am, mf, FALSE);
}


/*
 * Unmount an NFS hierarchy.
 * Note that this is called in the foreground
 * and so may hang under extremely rare conditions.
 */
static int
amfs_nfsx_umount(am_node *am, mntfs *mf)
{
  struct amfs_nfsx *nx = (struct amfs_nfsx *) mf->mf_private;
  amfs_nfsx_mnt *n;
  int glob_error = 0;

  /*
   * Iterate in reverse through the mntfs's and unmount each filesystem
   * which is mounted.
   */
  for (n = nx->nx_v + nx->nx_c - 1; n >= nx->nx_v; --n) {
    mntfs *m = n->n_mnt;
    /*
     * If this node has not been messed with
     * and there has been no error so far
     * then try and unmount.
     * If an error had occurred then zero
     * the error code so that the remount
     * only tries to unmount those nodes
     * which had been successfully unmounted.
     */
    if (n->n_error == 0) {
      dlog("calling underlying fumount on %s", m->mf_mount);
      n->n_error = m->mf_ops->umount_fs(am, m);
      if (n->n_error) {
	glob_error = n->n_error;
	n->n_error = 0;
      } else {
	/*
	 * Make sure remount gets this node
	 */
	n->n_error = -1;
      }
    }
  }

  /*
   * If any unmounts failed then remount the
   * whole lot...
   */
  if (glob_error) {
    glob_error = amfs_nfsx_remount(am, mf, TRUE);
    if (glob_error) {
      errno = glob_error;	/* XXX */
      plog(XLOG_USER, "amfs_nfsx: remount of %s failed: %m", mf->mf_mount);
    }
    glob_error = EBUSY;
  } else {
    /*
     * Remove all the mount points
     */
    for (n = nx->nx_v; n < nx->nx_v + nx->nx_c; n++) {
      mntfs *m = n->n_mnt;
      dlog("calling underlying umounted on %s", m->mf_mount);
      if (m->mf_ops->umounted)
	m->mf_ops->umounted(m);

      if (n->n_error < 0) {
	if (m->mf_fsflags & FS_MKMNT) {
	  (void) rmdirs(m->mf_mount);
	  m->mf_flags &= ~MFF_MKMNT;
	}
      }
      free_mntfs(m);
      n->n_mnt = NULL;
      n->n_error = -1;
    }
  }

  return glob_error;
}
