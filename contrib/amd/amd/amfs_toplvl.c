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
 * File: am-utils/amd/amfs_toplvl.c
 *
 */

/*
 * Top-level file system
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/****************************************************************************
 *** FORWARD DEFINITIONS                                                  ***
 ****************************************************************************/
static int amfs_toplvl_init(mntfs *mf);

/****************************************************************************
 *** OPS STRUCTURES                                                       ***
 ****************************************************************************/
am_ops amfs_toplvl_ops =
{
  "toplvl",
  amfs_generic_match,
  amfs_toplvl_init,		/* amfs_toplvl_init */
  amfs_toplvl_mount,
  amfs_toplvl_umount,
  amfs_generic_lookup_child,
  amfs_generic_mount_child,
  amfs_generic_readdir,
  0,				/* amfs_toplvl_readlink */
  amfs_generic_mounted,
  0,				/* amfs_toplvl_umounted */
  amfs_generic_find_srvr,
  0,				/* amfs_toplvl_get_wchan */
  FS_MKMNT | FS_NOTIMEOUT | FS_BACKGROUND |
	  FS_AMQINFO | FS_DIRECTORY, /* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_TOPLVL_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


/****************************************************************************
 *** FUNCTIONS                                                             ***
 ****************************************************************************/

static void
set_auto_attrcache_timeout(char *preopts, char *opts, size_t l)
{

#ifdef MNTTAB_OPT_NOAC
  /*
   * Don't cache attributes - they are changing under the kernel's feet.
   * For example, IRIX5.2 will dispense with nfs lookup calls and hand stale
   * filehandles to getattr unless we disable attribute caching on the
   * automount points.
   */
  if (gopt.auto_attrcache == 0) {
    xsnprintf(preopts, l, ",%s", MNTTAB_OPT_NOAC);
    xstrlcat(opts, preopts, l);
  }
#endif /* MNTTAB_OPT_NOAC */

  /*
   * XXX: note that setting these to 0 in the past resulted in an error on
   * some systems, which is why it's better to use "noac" if possible.  For
   * now, we're setting everything possible, but if this will cause trouble,
   * then we'll have to condition the remainder of this on OPT_NOAC.
   */
#ifdef MNTTAB_OPT_ACTIMEO
  xsnprintf(preopts, l, ",%s=%d", MNTTAB_OPT_ACTIMEO, gopt.auto_attrcache);
  xstrlcat(opts, preopts, l);
#else /* MNTTAB_OPT_ACTIMEO */
# ifdef MNTTAB_OPT_ACDIRMIN
  xsnprintf(preopts, l, ",%s=%d", MNTTAB_OPT_ACTDIRMIN, gopt.auto_attrcache);
  xstrlcat(opts, preopts, l);
# endif /* MNTTAB_OPT_ACDIRMIN */
# ifdef MNTTAB_OPT_ACDIRMAX
  xsnprintf(preopts, l, ",%s=%d", MNTTAB_OPT_ACTDIRMAX, gopt.auto_attrcache);
  xstrlcat(opts, preopts, l);
# endif /* MNTTAB_OPT_ACDIRMAX */
# ifdef MNTTAB_OPT_ACREGMIN
  xsnprintf(preopts, l, ",%s=%d", MNTTAB_OPT_ACTREGMIN, gopt.auto_attrcache);
  xstrlcat(opts, preopts, l);
# endif /* MNTTAB_OPT_ACREGMIN */
# ifdef MNTTAB_OPT_ACREGMAX
  xsnprintf(preopts, l, ",%s=%d", MNTTAB_OPT_ACTREGMAX, gopt.auto_attrcache);
  xstrlcat(opts, preopts, l);
# endif /* MNTTAB_OPT_ACREGMAX */
#endif /* MNTTAB_OPT_ACTIMEO */
}


/*
 * Initialize a top-level mount.  In our case, if the user asked for
 * forced_unmounts, and the OS supports it, then we try forced/lazy unmounts
 * on any previous toplvl mounts.  This is useful if a previous Amd died and
 * left behind toplvl mount points (this Amd will clean them up).
 *
 * WARNING: Don't use forced/lazy unmounts if you have another valid Amd
 * running, because this code WILL force those valid toplvl mount points to
 * be detached as well!
 */
static int
amfs_toplvl_init(mntfs *mf)
{
  int error = 0;

#if (defined(MNT2_GEN_OPT_FORCE) || defined(MNT2_GEN_OPT_DETACH)) && (defined(HAVE_UVMOUNT) || defined(HAVE_UMOUNT2))
  if (gopt.flags & CFM_FORCED_UNMOUNTS) {
    plog(XLOG_INFO, "amfs_toplvl_init: trying forced/lazy unmount of %s",
	 mf->mf_mount);
    error = umount2_fs(mf->mf_mount, AMU_UMOUNT_FORCE | AMU_UMOUNT_DETACH);
    if (error)
      plog(XLOG_INFO, "amfs_toplvl_init: forced/lazy unmount failed: %m");
    else
      dlog("amfs_toplvl_init: forced/lazy unmount succeeded");
  }
#endif /* (MNT2_GEN_OPT_FORCE || MNT2_GEN_OPT_DETACH) && (HAVE_UVMOUNT || HAVE_UMOUNT2) */
  return error;
}


/*
 * Mount the top-level
 */
int
amfs_toplvl_mount(am_node *mp, mntfs *mf)
{
  struct stat stb;
  char opts[SIZEOF_OPTS], preopts[SIZEOF_OPTS], toplvl_opts[40];
  int error;

  /*
   * Mounting the automounter.
   * Make sure the mount directory exists, construct
   * the mount options and call the mount_amfs_toplvl routine.
   */

  if (stat(mp->am_path, &stb) < 0) {
    return errno;
  } else if ((stb.st_mode & S_IFMT) != S_IFDIR) {
    plog(XLOG_WARNING, "%s is not a directory", mp->am_path);
    return ENOTDIR;
  }

  /*
   * Construct some mount options:
   *
   * Tack on magic map=<mapname> option in mtab to emulate
   * SunOS automounter behavior.
   */

#ifdef HAVE_FS_AUTOFS
  if (mf->mf_flags & MFF_IS_AUTOFS) {
    autofs_get_opts(opts, sizeof(opts), mp->am_autofs_fh);
  } else
#endif /* HAVE_FS_AUTOFS */
  {
    preopts[0] = '\0';
#ifdef MNTTAB_OPT_INTR
    xstrlcat(preopts, MNTTAB_OPT_INTR, sizeof(preopts));
    xstrlcat(preopts, ",", sizeof(preopts));
#endif /* MNTTAB_OPT_INTR */
#ifdef MNTTAB_OPT_IGNORE
    xstrlcat(preopts, MNTTAB_OPT_IGNORE, sizeof(preopts));
    xstrlcat(preopts, ",", sizeof(preopts));
#endif /* MNTTAB_OPT_IGNORE */
    /* write most of the initial options + preopts */
    xsnprintf(opts, sizeof(opts), "%s%s,%s=%d,%s,map=%s",
	      preopts,
	      MNTTAB_OPT_RW,
	      MNTTAB_OPT_PORT, nfs_port,
	      mf->mf_ops->fs_type, mf->mf_info);

    /* process toplvl timeo/retrans options, if any */
    if (gopt.amfs_auto_timeo[AMU_TYPE_TOPLVL] > 0) {
      xsnprintf(toplvl_opts, sizeof(toplvl_opts), ",%s=%d",
		MNTTAB_OPT_TIMEO, gopt.amfs_auto_timeo[AMU_TYPE_TOPLVL]);
      xstrlcat(opts, toplvl_opts, sizeof(opts));
    }
    if (gopt.amfs_auto_retrans[AMU_TYPE_TOPLVL] > 0) {
      xsnprintf(toplvl_opts, sizeof(toplvl_opts), ",%s=%d",
		MNTTAB_OPT_RETRANS, gopt.amfs_auto_retrans[AMU_TYPE_TOPLVL]);
      xstrlcat(opts, toplvl_opts, sizeof(opts));
    }

#ifdef MNTTAB_OPT_NOLOCK
    xstrlcat(opts, ",", sizeof(opts));
    xstrlcat(opts, MNTTAB_OPT_NOLOCK, sizeof(opts));
#endif /* MNTTAB_OPT_NOLOCK */

#ifdef MNTTAB_OPT_NOAC
    if (gopt.auto_attrcache == 0) {
      xstrlcat(opts, ",", sizeof(opts));
      xstrlcat(opts, MNTTAB_OPT_NOAC, sizeof(opts));
    } else
#endif /* MNTTAB_OPT_NOAC */
      set_auto_attrcache_timeout(preopts, opts, sizeof(preopts));
  }

  /* now do the mount */
  error = amfs_mount(mp, mf, opts);
  if (error) {
    errno = error;
    plog(XLOG_FATAL, "amfs_toplvl_mount: amfs_mount failed: %m");
    return error;
  }
  return 0;
}


/*
 * Unmount a top-level automount node
 */
int
amfs_toplvl_umount(am_node *mp, mntfs *mf)
{
  struct stat stb;
  int unmount_flags = (mf->mf_flags & MFF_ON_AUTOFS) ? AMU_UMOUNT_AUTOFS : 0;
  int error;
  int count = 0;		/* how many times did we try to unmount? */

again:
  /*
   * The lstat is needed if this mount is type=direct.
   * When that happens, the kernel cache gets confused
   * between the underlying type (dir) and the mounted
   * type (link) and so needs to be re-synced before
   * the unmount.  This is all because the unmount system
   * call follows links and so can't actually unmount
   * a link (stupid!).  It was noted that doing an ls -ld
   * of the mount point to see why things were not working
   * actually fixed the problem - so simulate an ls -ld here.
   */
  if (lstat(mp->am_path, &stb) < 0) {
    error = errno;
    dlog("lstat(%s): %m", mp->am_path);
    goto out;
  }
  if ((stb.st_mode & S_IFMT) != S_IFDIR) {
    plog(XLOG_ERROR, "amfs_toplvl_umount: %s is not a directory, aborting.", mp->am_path);
    error = ENOTDIR;
    goto out;
  }

  error = UMOUNT_FS(mp->am_path, mnttab_file_name, unmount_flags);
  if (error == EBUSY) {
#ifdef HAVE_FS_AUTOFS
    /*
     * autofs mounts are "in place", so it is possible
     * that we can't just unmount our mount points and go away.
     * If that's the case, just give up.
     */
    if (mf->mf_flags & MFF_IS_AUTOFS)
      return error;
#endif /* HAVE_FS_AUTOFS */
    plog(XLOG_WARNING, "amfs_toplvl_unmount retrying %s in 1s", mp->am_path);
    count++;
    sleep(1);
    /*
     * If user wants forced/lazy unmount semantics, then set those flags,
     * but only after we've tried normal lstat/umount a few times --
     * otherwise forced unmounts may hang this very same Amd (by preventing
     * it from achieving a clean unmount).
     */
    if (gopt.flags & CFM_FORCED_UNMOUNTS) {
      if (count == 5) {		/* after 5 seconds, try MNT_FORCE */
	dlog("enabling forced unmounts for toplvl node %s", mp->am_path);
	unmount_flags |= AMU_UMOUNT_FORCE;
      }
      if (count == 10) {	/* after 10 seconds, try MNT_DETACH */
	dlog("enabling detached unmounts for toplvl node %s", mp->am_path);
	unmount_flags |= AMU_UMOUNT_DETACH;
      }
    }
    goto again;
  }
out:
  return error;
}
