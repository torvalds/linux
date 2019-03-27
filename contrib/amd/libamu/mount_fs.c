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
 * File: am-utils/libamu/mount_fs.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amu.h>
#include <nfs_common.h>


/* ensure that mount table options are delimited by a comma */
#define append_opts(old, l, new) { \
	if (*(old) != '\0') \
	  xstrlcat(old, ",", l); \
	xstrlcat(old, new, l); }

/*
 * Standard mount flags
 */
struct opt_tab mnt_flags[] =
{
#if defined(MNT2_GEN_OPT_RDONLY) && defined(MNTTAB_OPT_RO)
  {MNTTAB_OPT_RO, MNT2_GEN_OPT_RDONLY},
#endif /* defined(MNT2_GEN_OPT_RDONLY) && defined(MNTTAB_OPT_RO) */

#if defined(MNT2_GEN_OPT_NOCACHE) && defined(MNTTAB_OPT_NOCACHE)
  {MNTTAB_OPT_NOCACHE, MNT2_GEN_OPT_NOCACHE},
#endif /* defined(MNT2_GEN_OPT_NOCACHE) && defined(MNTTAB_OPT_NOCACHE) */

  /* the "grpid" mount option can be offered as generic of NFS */
#ifdef MNTTAB_OPT_GRPID
# ifdef MNT2_GEN_OPT_GRPID
  {MNTTAB_OPT_GRPID, MNT2_GEN_OPT_GRPID},
# endif /* MNT2_GEN_OPT_GRPID */
# ifdef MNT2_NFS_OPT_GRPID
  {MNTTAB_OPT_GRPID, MNT2_NFS_OPT_GRPID},
# endif /* MNT2_NFS_OPT_GRPID */
#endif /* MNTTAB_OPT_GRPID */

#if defined(MNT2_GEN_OPT_MULTI) && defined(MNTTAB_OPT_MULTI)
  {MNTTAB_OPT_MULTI, MNT2_GEN_OPT_MULTI},
#endif /* defined(MNT2_GEN_OPT_MULTI) && defined(MNTTAB_OPT_MULTI) */

#if defined(MNT2_GEN_OPT_NODEV) && defined(MNTTAB_OPT_NODEV)
  {MNTTAB_OPT_NODEV, MNT2_GEN_OPT_NODEV},
#endif /* defined(MNT2_GEN_OPT_NODEV) && defined(MNTTAB_OPT_NODEV) */

#if defined(MNT2_GEN_OPT_NOEXEC) && defined(MNTTAB_OPT_NOEXEC)
  {MNTTAB_OPT_NOEXEC, MNT2_GEN_OPT_NOEXEC},
#endif /* defined(MNT2_GEN_OPT_NOEXEC) && defined(MNTTAB_OPT_NOEXEC) */

#if defined(MNT2_GEN_OPT_NOSUB) && defined(MNTTAB_OPT_NOSUB)
  {MNTTAB_OPT_NOSUB, MNT2_GEN_OPT_NOSUB},
#endif /* defined(MNT2_GEN_OPT_NOSUB) && defined(MNTTAB_OPT_NOSUB) */

#if defined(MNT2_GEN_OPT_NOSUID) && defined(MNTTAB_OPT_NOSUID)
  {MNTTAB_OPT_NOSUID, MNT2_GEN_OPT_NOSUID},
#endif /* defined(MNT2_GEN_OPT_NOSUID) && defined(MNTTAB_OPT_NOSUID) */

#if defined(MNT2_GEN_OPT_SYNC) && defined(MNTTAB_OPT_SYNC)
  {MNTTAB_OPT_SYNC, MNT2_GEN_OPT_SYNC},
#endif /* defined(MNT2_GEN_OPT_SYNC) && defined(MNTTAB_OPT_SYNC) */

#if defined(MNT2_GEN_OPT_OVERLAY) && defined(MNTTAB_OPT_OVERLAY)
  {MNTTAB_OPT_OVERLAY, MNT2_GEN_OPT_OVERLAY},
#endif /* defined(MNT2_GEN_OPT_OVERLAY) && defined(MNTTAB_OPT_OVERLAY) */

#if defined(MNT2_GEN_OPT_LOG) && defined(MNTTAB_OPT_LOG)
  {MNTTAB_OPT_LOG, MNT2_GEN_OPT_LOG},
#endif /* defined(MNT2_GEN_OPT_LOG) && defined(MNTTAB_OPT_LOG) */

#if defined(MNT2_GEN_OPT_NOATIME) && defined(MNTTAB_OPT_NOATIME)
  {MNTTAB_OPT_NOATIME, MNT2_GEN_OPT_NOATIME},
#endif /* defined(MNT2_GEN_OPT_NOATIME) && defined(MNTTAB_OPT_NOATIME) */

#if defined(MNT2_GEN_OPT_NODEVMTIME) && defined(MNTTAB_OPT_NODEVMTIME)
  {MNTTAB_OPT_NODEVMTIME, MNT2_GEN_OPT_NODEVMTIME},
#endif /* defined(MNT2_GEN_OPT_NODEVMTIME) && defined(MNTTAB_OPT_NODEVMTIME) */

#if defined(MNT2_GEN_OPT_SOFTDEP) && defined(MNTTAB_OPT_SOFTDEP)
  {MNTTAB_OPT_SOFTDEP, MNT2_GEN_OPT_SOFTDEP},
#endif /* defined(MNT2_GEN_OPT_SOFTDEP) && defined(MNTTAB_OPT_SOFTDEP) */

#if defined(MNT2_GEN_OPT_SYMPERM) && defined(MNTTAB_OPT_SYMPERM)
  {MNTTAB_OPT_SYMPERM, MNT2_GEN_OPT_SYMPERM},
#endif /* defined(MNT2_GEN_OPT_SYMPERM) && defined(MNTTAB_OPT_SYMPERM) */

#if defined(MNT2_GEN_OPT_UNION) && defined(MNTTAB_OPT_UNION)
  {MNTTAB_OPT_UNION, MNT2_GEN_OPT_UNION},
#endif /* defined(MNT2_GEN_OPT_UNION) && defined(MNTTAB_OPT_UNION) */

  /*
   * Do not define MNT2_NFS_OPT_* entries here!  This is for generic
   * mount(2) options only, not for NFS mount options.  If you need to put
   * something here, it's probably not the right place: see
   * include/am_compat.h.
   */

  {0, 0}
};


/* compute generic mount flags */
int
compute_mount_flags(mntent_t *mntp)
{
  struct opt_tab *opt;
  int flags = 0;

#ifdef MNT2_GEN_OPT_NEWTYPE
  flags |= MNT2_GEN_OPT_NEWTYPE;
#endif /* MNT2_GEN_OPT_NEWTYPE */
#ifdef MNT2_GEN_OPT_AUTOMOUNTED
  flags |= MNT2_GEN_OPT_AUTOMOUNTED;
#endif /* not MNT2_GEN_OPT_AUTOMOUNTED */

  /*
   * Crack basic mount options
   */
  for (opt = mnt_flags; opt->opt; opt++) {
    flags |= amu_hasmntopt(mntp, opt->opt) ? opt->flag : 0;
  }

  return flags;
}


/* compute generic mount flags for automounter mounts */
int
compute_automounter_mount_flags(mntent_t *mntp)
{
  int flags = 0;

#ifdef MNT2_GEN_OPT_IGNORE
  flags |= MNT2_GEN_OPT_IGNORE;
#endif /* not MNT2_GEN_OPT_IGNORE */
#ifdef MNT2_GEN_OPT_AUTOMNTFS
  flags |= MNT2_GEN_OPT_AUTOMNTFS;
#endif /* not MNT2_GEN_OPT_AUTOMNTFS */

  return flags;
}


#if defined(MOUNT_TABLE_ON_FILE) && defined(MNTTAB_OPT_VERS)
/*
 * add the extra vers={2,3} field to the mount table,
 * unless already specified by user
 */
static void
addvers(char *zopts, size_t l, mntent_t *mnt, u_long have_vers,
  u_long want_vers)
{
  if (have_vers == want_vers &&
      hasmntval(mnt, MNTTAB_OPT_VERS) != want_vers) {
    char optsbuf[48];
    xsnprintf(optsbuf, sizeof(optsbuf),
	      "%s=%d", MNTTAB_OPT_VERS, want_vers);
    append_opts(zopts, l, optsbuf);
  }
}
#endif /* MOUNT_TABLE_ON_FILE && MNTTAB_OPT_VERS */

int
mount_fs(mntent_t *mnt, int flags, caddr_t mnt_data, int retry, MTYPE_TYPE type, u_long nfs_version, const char *nfs_proto, const char *mnttabname, int on_autofs)
{
  int error = 0;
#ifdef MOUNT_TABLE_ON_FILE
  char *zopts = NULL, *xopts = NULL;
  size_t l;
#endif /* MOUNT_TABLE_ON_FILE */
  char *mnt_dir = NULL;

#ifdef NEED_AUTOFS_SPACE_HACK
  char *old_mnt_dir = NULL;
  /* perform space hack */
  if (on_autofs) {
    old_mnt_dir = mnt->mnt_dir;
    mnt->mnt_dir = mnt_dir = autofs_strdup_space_hack(old_mnt_dir);
  } else
#endif /* NEED_AUTOFS_SPACE_HACK */
    mnt_dir = xstrdup(mnt->mnt_dir);

  dlog("'%s' fstype " MTYPE_PRINTF_TYPE " (%s) flags %#x (%s)",
       mnt_dir, type, mnt->mnt_type, flags, mnt->mnt_opts);

again:
  error = MOUNT_TRAP(type, mnt, flags, mnt_data);

  if (error < 0) {
    plog(XLOG_ERROR, "'%s': mount: %m", mnt_dir);
    /*
     * The following code handles conditions which shouldn't
     * occur.  They are possible either because amd screws up
     * in preparing for the mount, or because some human
     * messed with the mount point.  Both have been known to
     * happen. -- stolcke 2/22/95
     */
    if (errno == EBUSY) {
      /*
       * Also, sometimes unmount isn't called, e.g., because
       * our mountlist is garbled.  This leaves old mount
       * points around which need to be removed before we
       * can mount something new in their place.
       */
      errno = umount_fs(mnt_dir, mnttabname, on_autofs);
      if (errno != 0)
	plog(XLOG_ERROR, "'%s': umount: %m", mnt_dir);
      else {
	plog(XLOG_WARNING, "extra umount required for '%s'", mnt_dir);
	error = MOUNT_TRAP(type, mnt, flags, mnt_data);
      }
    }
  }

  if (error < 0 && --retry > 0) {
    sleep(1);
    goto again;
  }

#ifdef NEED_AUTOFS_SPACE_HACK
  /* Undo space hack */
  if (on_autofs)
    mnt->mnt_dir = old_mnt_dir;
#endif /* NEED_AUTOFS_SPACE_HACK */

  if (error < 0) {
    error = errno;
    goto out;
  }

#ifdef MOUNT_TABLE_ON_FILE
  /*
   * Allocate memory for options:
   *        dev=..., vers={2,3}, proto={tcp,udp}
   */
  l = strlen(mnt->mnt_opts) + 48;
  zopts = (char *) xmalloc(l);

  /* copy standard options */
  xopts = mnt->mnt_opts;

  xstrlcpy(zopts, xopts, l);

# ifdef MNTTAB_OPT_DEV
  {
    /* add the extra dev= field to the mount table */
    struct stat stb;
    if (lstat(mnt_dir, &stb) == 0) {
      char optsbuf[48];
      if (sizeof(stb.st_dev) == 2) /* e.g. SunOS 4.1 */
	xsnprintf(optsbuf, sizeof(optsbuf), "%s=%04lx",
		  MNTTAB_OPT_DEV, (u_long) stb.st_dev & 0xffff);
      else			/* e.g. System Vr4 */
	xsnprintf(optsbuf, sizeof(optsbuf), "%s=%08lx",
		  MNTTAB_OPT_DEV, (u_long) stb.st_dev);
      append_opts(zopts, l, optsbuf);
    }
  }
# endif /* MNTTAB_OPT_DEV */

# if defined(HAVE_FS_NFS4) && defined(MNTTAB_OPT_VERS)
  addvers(zopts, l, mnt, nfs_version, NFS_VERSION4);
# endif /* defined(HAVE_FS_NFS4) && defined(MNTTAB_OPT_VERS) */
# if defined(HAVE_FS_NFS3) && defined(MNTTAB_OPT_VERS)
  addvers(zopts, l, mnt, nfs_version, NFS_VERSION3);
# endif /* defined(HAVE_FS_NFS3) && defined(MNTTAB_OPT_VERS) */
# ifdef MNTTAB_OPT_VERS
  addvers(zopts, l, mnt, nfs_version, NFS_VERSION2);
# endif /* MNTTAB_OPT_VERS */

# ifdef MNTTAB_OPT_PROTO
  /*
   * add the extra proto={tcp,udp} field to the mount table,
   * unless already specified by user.
   */
  if (nfs_proto && !amu_hasmntopt(mnt, MNTTAB_OPT_PROTO)) {
    char optsbuf[48];
    xsnprintf(optsbuf, sizeof(optsbuf), "%s=%s", MNTTAB_OPT_PROTO, nfs_proto);
    append_opts(zopts, l, optsbuf);
  }
# endif /* MNTTAB_OPT_PROTO */

  /* finally, store the options into the mount table structure */
  mnt->mnt_opts = zopts;

  /*
   * Additional fields in mntent_t
   * are fixed up here
   */
# ifdef HAVE_MNTENT_T_MNT_CNODE
  mnt->mnt_cnode = 0;
# endif /* HAVE_MNTENT_T_MNT_CNODE */

# ifdef HAVE_MNTENT_T_MNT_RO
  mnt->mnt_ro = (amu_hasmntopt(mnt, MNTTAB_OPT_RO) != NULL);
# endif /* HAVE_MNTENT_T_MNT_RO */

# ifdef HAVE_MNTENT_T_MNT_TIME
#  ifdef HAVE_MNTENT_T_MNT_TIME_STRING
  {				/* allocate enough space for a long */
    size_t l = 13 * sizeof(char);
    char *str = (char *) xmalloc(l);
    xsnprintf(str, l, "%ld", time((time_t *) NULL));
    mnt->mnt_time = str;
  }
#  else /* not HAVE_MNTENT_T_MNT_TIME_STRING */
  mnt->mnt_time = time((time_t *) NULL);
#  endif /* not HAVE_MNTENT_T_MNT_TIME_STRING */
# endif /* HAVE_MNTENT_T_MNT_TIME */

  write_mntent(mnt, mnttabname);

# ifdef MNTTAB_OPT_DEV
  if (xopts) {
    XFREE(mnt->mnt_opts);
    mnt->mnt_opts = xopts;
  }
# endif /* MNTTAB_OPT_DEV */
#endif /* MOUNT_TABLE_ON_FILE */

 out:
  XFREE(mnt_dir);
  return error;
}


/*
 * Compute all NFS attribute cache related flags separately.  Note that this
 * function now computes attribute-cache flags for both Amd's automount
 * points (NFS) as well as any normal NFS mount that Amd performs.  Edit
 * with caution.
 */
static void
compute_nfs_attrcache_flags(struct nfs_common_args *nap, mntent_t *mntp)
{
  int acval = 0;
  int err_acval = 1;		/* 1 means we found no 'actimeo' value */
#if defined(HAVE_NFS_ARGS_T_ACREGMIN) || defined(HAVE_NFS_ARGS_T_ACREGMAX) || defined(HAVE_NFS_ARGS_T_ACDIRMIN) || defined(HAVE_NFS_ARGS_T_ACDIRMAX)
  int err_acrdmm;		/* for ac{reg,dir}{min,max} */
#endif /* HAVE_NFS_ARGS_T_AC{REG,DIR}{MIN,MAX} */

  /************************************************************************/
  /***	ATTRIBUTE CACHES						***/
  /************************************************************************/
  /*
   * acval is set to 0 at the top of the function.  If actimeo mount option
   * exists and defined in mntopts, then its acval is set to it.
   * If the value is non-zero, then we set all attribute cache fields to it.
   * If acval is zero, it means it was never defined in mntopts or the
   * actimeo mount option does not exist, in which case we check for
   * individual mount options per attribute cache.
   * Regardless of the value of acval, mount flags are set based directly
   * on the values of the attribute caches.
   */
#ifdef MNTTAB_OPT_ACTIMEO
  err_acval = hasmntvalerr(mntp, MNTTAB_OPT_ACTIMEO, &acval);	/* attr cache timeout (sec) */
#endif /* MNTTAB_OPT_ACTIMEO */

  /*** acregmin ***/
#ifdef HAVE_NFS_ARGS_T_ACREGMIN
  err_acrdmm = 1;		/* 1 means we found no acregmin value */
  if (!err_acval) {
    nap->acregmin = acval;	/* min ac timeout for reg files (sec) */
  } else {
# ifdef MNTTAB_OPT_ACREGMIN
    int tmp;
    err_acrdmm = hasmntvalerr(mntp, MNTTAB_OPT_ACREGMIN, &tmp);
    nap->acregmin = tmp;
# else /* not MNTTAB_OPT_ACREGMIN */
    nap->acregmin = 0;
# endif /* not MNTTAB_OPT_ACREGMIN */
  }
  /* set this flag iff we changed acregmin (possibly to zero) */
# ifdef MNT2_NFS_OPT_ACREGMIN
  if (!err_acval || !err_acrdmm)
    nap->flags |= MNT2_NFS_OPT_ACREGMIN;
# endif /* MNT2_NFS_OPT_ACREGMIN */
#endif /* HAVE_NFS_ARGS_T_ACREGMIN */

  /*** acregmax ***/
#ifdef HAVE_NFS_ARGS_T_ACREGMAX
  err_acrdmm = 1;		/* 1 means we found no acregmax value */
  if (!err_acval) {
    nap->acregmax = acval;	/* max ac timeout for reg files (sec) */
  } else {
# ifdef MNTTAB_OPT_ACREGMAX
    int tmp;
    err_acrdmm = hasmntvalerr(mntp, MNTTAB_OPT_ACREGMAX, &tmp);
    nap->acregmax = tmp;
# else /* not MNTTAB_OPT_ACREGMAX */
    nap->acregmax = 0;
# endif /* not MNTTAB_OPT_ACREGMAX */
  }
  /* set this flag iff we changed acregmax (possibly to zero) */
# ifdef MNT2_NFS_OPT_ACREGMAX
  if (!err_acval || !err_acrdmm)
    nap->flags |= MNT2_NFS_OPT_ACREGMAX;
# endif /* MNT2_NFS_OPT_ACREGMAX */
#endif /* HAVE_NFS_ARGS_T_ACREGMAX */

  /*** acdirmin ***/
#ifdef HAVE_NFS_ARGS_T_ACDIRMIN
  err_acrdmm = 1;		/* 1 means we found no acdirmin value */
  if (!err_acval) {
    nap->acdirmin = acval;	/* min ac timeout for dirs (sec) */
  } else {
# ifdef MNTTAB_OPT_ACDIRMIN
    int tmp;
    err_acrdmm = hasmntvalerr(mntp, MNTTAB_OPT_ACDIRMIN, &tmp);
    nap->acdirmin = tmp;
# else /* not MNTTAB_OPT_ACDIRMIN */
    nap->acdirmin = 0;
# endif /* not MNTTAB_OPT_ACDIRMIN */
  }
  /* set this flag iff we changed acdirmin (possibly to zero) */
# ifdef MNT2_NFS_OPT_ACDIRMIN
  if (!err_acval || !err_acrdmm)
    nap->flags |= MNT2_NFS_OPT_ACDIRMIN;
# endif /* MNT2_NFS_OPT_ACDIRMIN */
#endif /* HAVE_NFS_ARGS_T_ACDIRMIN */

  /*** acdirmax ***/
#ifdef HAVE_NFS_ARGS_T_ACDIRMAX
  err_acrdmm = 1;		/* 1 means we found no acdirmax value */
  if (!err_acval) {
    nap->acdirmax = acval;	/* max ac timeout for dirs (sec) */
  } else {
# ifdef MNTTAB_OPT_ACDIRMAX
    int tmp;
    err_acrdmm = hasmntvalerr(mntp, MNTTAB_OPT_ACDIRMAX, &tmp);
    nap->acdirmax = tmp;
# else /* not MNTTAB_OPT_ACDIRMAX */
    nap->acdirmax = 0;
# endif /* not MNTTAB_OPT_ACDIRMAX */
  }
  /* set this flag iff we changed acdirmax (possibly to zero) */
# ifdef MNT2_NFS_OPT_ACDIRMAX
  if (!err_acval || !err_acrdmm)
    nap->flags |= MNT2_NFS_OPT_ACDIRMAX;
# endif /* MNT2_NFS_OPT_ACDIRMAX */
#endif /* HAVE_NFS_ARGS_T_ACDIRMAX */


  /* don't cache attributes */
#if defined(MNTTAB_OPT_NOAC) && defined(MNT2_NFS_OPT_NOAC)
  if (amu_hasmntopt(mntp, MNTTAB_OPT_NOAC) != NULL)
    nap->flags |= MNT2_NFS_OPT_NOAC;
#endif /* defined(MNTTAB_OPT_NOAC) && defined(MNT2_NFS_OPT_NOAC) */
}



static void
compute_nfs_common_args(struct nfs_common_args *nap, mntent_t *mntp,
    const char *nfs_proto, u_long nfs_version)
{
#ifdef MNT2_NFS_OPT_TCP
  if (nfs_proto && STREQ(nfs_proto, "tcp"))
    nap->flags |= MNT2_NFS_OPT_TCP;
#endif /* MNT2_NFS_OPT_TCP */

#ifdef MNT2_NFS_OPT_NOCONN
  /* check if user specified to use unconnected or connected sockets */
  if (amu_hasmntopt(mntp, MNTTAB_OPT_NOCONN) != NULL)
    nap->flags |= MNT2_NFS_OPT_NOCONN;
  else if (amu_hasmntopt(mntp, MNTTAB_OPT_CONN) != NULL)
    nap->flags &= ~MNT2_NFS_OPT_NOCONN;
  else {
    /*
     * Some OSs want you to set noconn always.  Some want you to always turn
     * it off.  Others want you to turn it on/off only if NFS V.3 is used.
     * And all of that changes from revision to another.  This is
     * particularly true of OpenBSD, NetBSD, and FreeBSD.  So, rather than
     * attempt to auto-detect this, I'm forced to "fix" it in the individual
     * conf/nfs_prot/nfs_prot_*.h files.
     */
# ifdef USE_UNCONNECTED_NFS_SOCKETS
    if (!(nap->flags & MNT2_NFS_OPT_NOCONN)) {
      nap->flags |= MNT2_NFS_OPT_NOCONN;
      plog(XLOG_WARNING, "noconn option not specified, and was just turned ON (OS override)! (May cause NFS hangs on some systems...)");
    }
# endif /* USE_UNCONNECTED_NFS_SOCKETS */
# ifdef USE_CONNECTED_NFS_SOCKETS
    if (nap->flags & MNT2_NFS_OPT_NOCONN) {
      nap->flags &= ~MNT2_NFS_OPT_NOCONN;
      plog(XLOG_WARNING, "noconn option specified, and was just turned OFF (OS override)! (May cause NFS hangs on some systems...)");
    }
# endif /* USE_CONNECTED_NFS_SOCKETS */
  }
#endif /* MNT2_NFS_OPT_NOCONN */

#ifdef MNT2_NFS_OPT_RESVPORT
# ifdef MNTTAB_OPT_RESVPORT
  if (amu_hasmntopt(mntp, MNTTAB_OPT_RESVPORT) != NULL)
    nap->flags |= MNT2_NFS_OPT_RESVPORT;
# else /* not MNTTAB_OPT_RESVPORT */
  nap->flags |= MNT2_NFS_OPT_RESVPORT;
# endif /* not MNTTAB_OPT_RESVPORT */
#endif /* MNT2_NFS_OPT_RESVPORT */

  nap->rsize = hasmntval(mntp, MNTTAB_OPT_RSIZE);
#ifdef MNT2_NFS_OPT_RSIZE
  if (nap->rsize)
    nap->flags |= MNT2_NFS_OPT_RSIZE;
#endif /* MNT2_NFS_OPT_RSIZE */
  if (nfs_version == NFS_VERSION && nap->rsize > 8192)
    nap->rsize = 8192;

  nap->wsize = hasmntval(mntp, MNTTAB_OPT_WSIZE);
#ifdef MNT2_NFS_OPT_WSIZE
  if (nap->wsize)
    nap->flags |= MNT2_NFS_OPT_WSIZE;
#endif /* MNT2_NFS_OPT_WSIZE */
  if (nfs_version == NFS_VERSION && nap->wsize > 8192)
    nap->wsize = 8192;

  nap->timeo = hasmntval(mntp, MNTTAB_OPT_TIMEO);
#ifdef MNT2_NFS_OPT_TIMEO
  if (nap->timeo)
    nap->flags |= MNT2_NFS_OPT_TIMEO;
#endif /* MNT2_NFS_OPT_TIMEO */

  nap->retrans = hasmntval(mntp, MNTTAB_OPT_RETRANS);
#ifdef MNT2_NFS_OPT_RETRANS
  if (nap->retrans)
    nap->flags |= MNT2_NFS_OPT_RETRANS;
#endif /* MNT2_NFS_OPT_RETRANS */

#ifdef MNT2_NFS_OPT_SOFT
  if (amu_hasmntopt(mntp, MNTTAB_OPT_SOFT) != NULL)
    nap->flags |= MNT2_NFS_OPT_SOFT;
#endif /* MNT2_NFS_OPT_SOFT */

#ifdef MNT2_NFS_OPT_SPONGY
  if (amu_hasmntopt(mntp, MNTTAB_OPT_SPONGY) != NULL) {
    nap->flags |= MNT2_NFS_OPT_SPONGY;
    if (*flags & MNT2_NFS_OPT_SOFT) {
      plog(XLOG_USER, "Mount opts soft and spongy are incompatible - soft ignored");
      nap->flags &= ~MNT2_NFS_OPT_SOFT;
    }
  }
#endif /* MNT2_NFS_OPT_SPONGY */

#if defined(MNT2_GEN_OPT_RONLY) && defined(MNT2_NFS_OPT_RONLY)
  /* Ultrix has separate generic and NFS ro flags */
  if (genflags & MNT2_GEN_OPT_RONLY)
    nap->flags |= MNT2_NFS_OPT_RONLY;
#endif /* defined(MNT2_GEN_OPT_RONLY) && defined(MNT2_NFS_OPT_RONLY) */

#ifdef MNTTAB_OPT_INTR
  if (amu_hasmntopt(mntp, MNTTAB_OPT_INTR) != NULL)
    /*
     * Either turn on the "allow interrupts" option, or
     * turn off the "disallow interrupts" option"
     */
# ifdef MNT2_NFS_OPT_INTR
    nap->flags |= MNT2_NFS_OPT_INTR;
# endif /* MNT2_NFS_OPT_INTR */
# ifdef MNT2_NFS_OPT_NOINTR
    nap->flags &= ~MNT2_NFS_OPT_NOINTR;
# endif /* MNT2_NFS_OPT_NOINTR */
# ifdef MNT2_NFS_OPT_INT
    nap->flags |= MNT2_NFS_OPT_INT;
# endif /* MNT2_NFS_OPT_INT */
# ifdef MNT2_NFS_OPT_NOINT
    nap->flags &= ~MNT2_NFS_OPT_NOINT;
# endif /* MNT2_NFS_OPT_NOINT */
#endif /* MNTTAB_OPT_INTR */

#ifdef MNT2_NFS_OPT_NOACL
  if (amu_hasmntopt(mntp, MNTTAB_OPT_NOACL) != NULL)
    nap->flags |= MNT2_NFS_OPT_NOACL;
#endif /* MNT2_NFS_OPT_NOACL */

#ifdef MNTTAB_OPT_NODEVS
  if (amu_hasmntopt(mntp, MNTTAB_OPT_NODEVS) != NULL)
    nap->flags |= MNT2_NFS_OPT_NODEVS;
#endif /* MNTTAB_OPT_NODEVS */

#ifdef MNTTAB_OPT_COMPRESS
  if (amu_hasmntopt(mntp, MNTTAB_OPT_COMPRESS) != NULL)
    nap->flags |= MNT2_NFS_OPT_COMPRESS;
#endif /* MNTTAB_OPT_COMPRESS */

#ifdef MNTTAB_OPT_PRIVATE	/* mount private, single-client tree */
  if (amu_hasmntopt(mntp, MNTTAB_OPT_PRIVATE) != NULL)
    nap->flags |= MNT2_NFS_OPT_PRIVATE;
#endif /* MNTTAB_OPT_PRIVATE */


#if defined(MNT2_NFS_OPT_NOCTO) && defined(MNTTAB_OPT_NOCTO)
  if (amu_hasmntopt(mntp, MNTTAB_OPT_NOCTO) != NULL)
    nap->flags |= MNT2_NFS_OPT_NOCTO;
#endif /* defined(MNT2_NFS_OPT_NOCTO) && defined(MNTTAB_OPT_NOCTO) */

#if defined(MNT2_NFS_OPT_PROPLIST) && defined(MNTTAB_OPT_PROPLIST)
  if (amu_hasmntopt(mntp, MNTTAB_OPT_PROPLIST) != NULL)
    nap->flags |= MNT2_NFS_OPT_PROPLIST;
#endif /* defined(MNT2_NFS_OPT_PROPLIST) && defined(MNTTAB_OPT_PROPLIST) */

#if defined(MNT2_NFS_OPT_NONLM) && defined(MNTTAB_OPT_NOLOCK)
  if (amu_hasmntopt(mntp, MNTTAB_OPT_NOLOCK) != NULL)
    nap->flags |= MNT2_NFS_OPT_NONLM;
#endif /* defined(MNT2_NFS_OPT_NONLM) && defined(MNTTAB_OPT_NOLOCK) */

#if defined(MNT2_NFS_OPT_XLATECOOKIE) && defined(MNTTAB_OPT_XLATECOOKIE)
  if (amu_hasmntopt(mntp, MNTTAB_OPT_XLATECOOKIE) != NULL)
    nap->flags |= MNT2_NFS_OPT_XLATECOOKIE;
#endif /* defined(MNT2_NFS_OPT_XLATECOOKIE) && defined(MNTTAB_OPT_XLATECOOKIE) */
}

static void
print_nfs_common_args(const struct nfs_common_args *a)
{
  plog(XLOG_DEBUG, "NA->flags = 0x%lx", a->flags);

  plog(XLOG_DEBUG, "NA->rsize = %lu", a->rsize);
  plog(XLOG_DEBUG, "NA->wsize = %lu", a->wsize);
  plog(XLOG_DEBUG, "NA->timeo = %lu", a->timeo);
  plog(XLOG_DEBUG, "NA->retrans = %lu", a->retrans);

#ifdef HAVE_NFS_ARGS_T_ACREGMIN
  plog(XLOG_DEBUG, "NA->acregmin = %lu", a->acregmin);
  plog(XLOG_DEBUG, "NA->acregmax = %lu", a->acregmax);
  plog(XLOG_DEBUG, "NA->acdirmin = %lu", a->acdirmin);
  plog(XLOG_DEBUG, "NA->acdirmax = %lu", a->acdirmax);
#endif /* HAVE_NFS_ARGS_T_ACREGMIN */
}

static void
discard_nfs23_args(nfs_args_t *nap)
{
#ifdef HAVE_TRANSPORT_TYPE_TLI
  free_knetconfig(nap->knconf);
  if (nap->addr)
    XFREE(nap->addr);	/* allocated in compute_nfs_args() */
#endif /* HAVE_TRANSPORT_TYPE_TLI */
}

#ifdef DEBUG
/* get string version (in hex) of identifier */
static char *
get_hex_string(u_int len, const char *fhdata)
{
  u_int i;
  static u_int xlen;
  static char *buf;
  static u_short *arr;
  char str[16];

  if (!fhdata || len == 0 || len > 10240)
    return NULL;
  i = len * 4 + 1;
  if (xlen < i) {
    buf = xrealloc(buf, i);
    arr = xrealloc(arr, len * sizeof(*arr));
    xlen = i;
  }
	
  buf[0] = '\0';
  memset(arr, 0, len * sizeof(*arr));
  memcpy(arr, fhdata, len);
  len /= sizeof(*arr);
  for (i = 0; i < len; i++) {
    xsnprintf(str, sizeof(str), "%04x", ntohs(arr[i]));
    xstrlcat(buf, str, xlen);
  }
  return buf;
}

static void
print_nfs_sockaddr_in(const char *tag, const struct sockaddr_in *sap)
{
  char name[64];
  plog(XLOG_DEBUG, "NA->%s.sin_family = %d", tag, sap->sin_family);
  plog(XLOG_DEBUG, "NA->%s.sin_port = %d", tag, ntohs(sap->sin_port));
  if (inet_ntop(AF_INET, &sap->sin_addr, name, sizeof(name)) == NULL)
    return;
  plog(XLOG_DEBUG, "NA->%s.sin_addr = \"%s\"", tag, name);
}

/*
 * print a subset of fields from "struct nfs_args" that are otherwise
 * not being provided anywhere else.
 */
static void
print_nfs23_args(const nfs_args_t *nap, u_long nfs_version)
{
  int fhlen = 32;	/* default: NFS V.2 file handle length is 32 */
#ifdef HAVE_TRANSPORT_TYPE_TLI
  struct netbuf *nbp;
  struct knetconfig *kncp;
#else /* not HAVE_TRANSPORT_TYPE_TLI */
  struct sockaddr_in *sap;
#endif /* not HAVE_TRANSPORT_TYPE_TLI */
  struct nfs_common_args a;

  if (!nap) {
    plog(XLOG_DEBUG, "NULL nfs_args!");
    return;
  }

  /* override default file handle size */
#ifdef FHSIZE
   fhlen = FHSIZE;
#endif /* FHSIZE */
#ifdef NFS_FHSIZE
   fhlen = NFS_FHSIZE;
#endif /* NFS_FHSIZE */

#ifdef HAVE_TRANSPORT_TYPE_TLI
  nbp = nap->addr;
  plog(XLOG_DEBUG, "NA->addr {netbuf} (maxlen=%d, len=%d) = \"%s\"",
       nbp->maxlen, nbp->len,
       get_hex_string(nbp->len, nbp->buf));
  nbp = nap->syncaddr;
  plog(XLOG_DEBUG, "NA->syncaddr {netbuf} %p", nbp);
  kncp = nap->knconf;
  plog(XLOG_DEBUG, "NA->knconf->semantics %lu", (u_long) kncp->knc_semantics);
  plog(XLOG_DEBUG, "NA->knconf->protofmly \"%s\"", kncp->knc_protofmly);
  plog(XLOG_DEBUG, "NA->knconf->proto \"%s\"", kncp->knc_proto);
  plog(XLOG_DEBUG, "NA->knconf->rdev %lu", (u_long) kncp->knc_rdev);
  /* don't print knconf->unused field */
#else /* not HAVE_TRANSPORT_TYPE_TLI */
# ifdef NFS_ARGS_T_ADDR_IS_POINTER
    sap = (struct sockaddr_in *) nap->addr;
# else /* not NFS_ARGS_T_ADDR_IS_POINTER */
    sap = (struct sockaddr_in *) &nap->addr;
# endif /* not NFS_ARGS_T_ADDR_IS_POINTER */
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
  /* as per POSIX, sin_len need not be set (used internally by kernel) */
  plog(XLOG_DEBUG, "NA->addr.sin_len = %d", sap->sin_len);
#endif /* HAVE_STRUCT_SOCKADDR_SA_LEN */
  print_nfs_sockaddr_in("addr", sap);
#endif /* not HAVE_TRANSPORT_TYPE_TLI */
#ifdef HAVE_NFS_ARGS_T_ADDRLEN
  plog(XLOG_DEBUG, "NA->addrlen = %d", nap->addrlen);
#endif /* ifdef HAVE_NFS_ARGS_T_ADDRLEN */

  plog(XLOG_DEBUG, "NA->hostname = \"%s\"", nap->hostname ? nap->hostname : "null");
#ifdef HAVE_NFS_ARGS_T_NAMLEN
  plog(XLOG_DEBUG, "NA->namlen = %d", nap->namlen);
#endif /* HAVE_NFS_ARGS_T_NAMLEN */

#ifdef MNT2_NFS_OPT_FSNAME
  plog(XLOG_DEBUG, "NA->fsname = \"%s\"", nap->fsname ? nap->fsname : "null");
#endif /* MNT2_NFS_OPT_FSNAME */

#ifdef HAVE_NFS_ARGS_T_FHSIZE
  plog(XLOG_DEBUG, "NA->fhsize = %d", nap->fhsize);
  fhlen = nap->fhsize;
#endif /* HAVE_NFS_ARGS_T_FHSIZE */
#ifdef HAVE_NFS_ARGS_T_FH_LEN
  plog(XLOG_DEBUG, "NA->fh_len = %d", nap->fh_len);
  fhlen = nap->fh_len;
#endif /* HAVE_NFS_ARGS_T_FH_LEN */

  /*
   * XXX: need to figure out how to correctly print file handles,
   * since some times they are pointers, and sometimes the real structure
   * is stored in nfs_args.  Even if it is a pointer, it can be the actual
   * char[] array, or a structure containing multiple fields.
   */
  plog(XLOG_DEBUG, "NA->filehandle = \"%s\"",
       get_hex_string(fhlen, (const char *) &nap->NFS_FH_FIELD));

#ifdef HAVE_NFS_ARGS_T_SOTYPE
  plog(XLOG_DEBUG, "NA->sotype = %d", nap->sotype);
#endif /* HAVE_NFS_ARGS_T_SOTYPE */
#ifdef HAVE_NFS_ARGS_T_PROTO
  plog(XLOG_DEBUG, "NA->proto = %d", (int) nap->proto);
#endif /* HAVE_NFS_ARGS_T_PROTO */
#ifdef HAVE_NFS_ARGS_T_VERSION
  plog(XLOG_DEBUG, "NA->version = %d", nap->version);
#endif /* HAVE_NFS_ARGS_T_VERSION */

  put_nfs_common_args(nap, a);
  print_nfs_common_args(&a);

#ifdef HAVE_NFS_ARGS_T_BSIZE
  plog(XLOG_DEBUG, "NA->bsize = %d", nap->bsize);
#endif /* HAVE_NFS_ARGS_T_BSIZE */

#ifdef MNTTAB_OPT_SYMTTL
  plog(XLOG_DEBUG, "NA->symttl = %d", nap->symttl);
#endif /* MNTTAB_OPT_SYMTTL */
#ifdef MNTTAB_OPT_PG_THRESH
  plog(XLOG_DEBUG, "NA->pg_thresh = %d", nap->pg_thresh);
#endif /* MNTTAB_OPT_PG_THRESH */

#ifdef MNT2_NFS_OPT_BIODS
  plog(XLOG_DEBUG, "NA->biods = %d", nap->biods);
#endif /* MNT2_NFS_OPT_BIODS */

}
#endif /* DEBUG */

/*
 * Fill in the many possible fields and flags of struct nfs_args.
 *
 * nap:		pre-allocated structure to fill in.
 * mntp:	mount entry structure (includes options)
 * genflags:	generic mount flags already determined
 * nfsncp:	(TLI only) netconfig entry for this NFS mount
 * ip_addr:	IP address of file server
 * nfs_version:	2, 3, or 0 if unknown
 * nfs_proto:	"udp", "tcp", or NULL.
 * fhp:		file handle structure pointer
 * host_name:	name of remote NFS host
 * fs_name:	remote file system name to mount
 */
static void
compute_nfs23_args(nfs_args_t *nap,
		   mntent_t *mntp,
		   int genflags,
		   struct netconfig *nfsncp,
		   struct sockaddr_in *ip_addr,
		   u_long nfs_version,
		   char *nfs_proto,
		   am_nfs_handle_t *fhp,
		   char *host_name,
		   char *fs_name)
{
  struct nfs_common_args a;
  /* initialize just in case */
  memset((voidp) nap, 0, sizeof(nfs_args_t));

  /* compute all of the NFS attribute-cache flags */
  memset(&a, 0, sizeof(a));
  compute_nfs_attrcache_flags(&a, mntp);
  compute_nfs_common_args(&a, mntp, nfs_proto, nfs_version);
  get_nfs_common_args(nap, a);

  /************************************************************************/
  /***	FILEHANDLE DATA AND LENGTH					***/
  /************************************************************************/
#ifdef HAVE_FS_NFS3
  if (nfs_version == NFS_VERSION3) {
    if (fhp == NULL) {
      plog(XLOG_FATAL, "cannot pass NULL fh for NFSv%lu", nfs_version);
      going_down(1);
      return;
    }

# if defined(HAVE_NFS_ARGS_T_FHSIZE) || defined(HAVE_NFS_ARGS_T_FH_LEN)
    /*
     * Some systems (Irix/bsdi3) have a separate field in nfs_args for
     * the length of the file handle for NFS V3.  They insist that
     * the file handle set in nfs_args be plain bytes, and not
     * include the length field.
     */
    NFS_FH_DREF(nap->NFS_FH_FIELD, &fhp->v3.am_fh3_data);
# else /* not defined(HAVE_NFS_ARGS_T_FHSIZE) || defined(HAVE_NFS_ARGS_T_FH_LEN) */
    NFS_FH_DREF(nap->NFS_FH_FIELD, &fhp->v3);
# endif /* not defined(HAVE_NFS_ARGS_T_FHSIZE) || defined(HAVE_NFS_ARGS_T_FH_LEN) */
# ifdef MNT2_NFS_OPT_NFSV3
    nap->flags |= MNT2_NFS_OPT_NFSV3;
# endif /* MNT2_NFS_OPT_NFSV3 */
# ifdef MNT2_NFS_OPT_VER3
    nap->flags |= MNT2_NFS_OPT_VER3;
# endif /* MNT2_NFS_OPT_VER3 */
  } else
#endif /* HAVE_FS_NFS3 */
  {
    if (fhp == NULL) {
      plog(XLOG_FATAL, "cannot pass NULL fh for NFSv%lu", nfs_version);
      going_down(1);
      return;
    }
    NFS_FH_DREF(nap->NFS_FH_FIELD, &fhp->v2);
  }

#ifdef HAVE_NFS_ARGS_T_FHSIZE
# ifdef HAVE_FS_NFS3
  if (nfs_version == NFS_VERSION3)
    nap->fhsize = fhp->v3.am_fh3_length;
  else
# endif /* HAVE_FS_NFS3 */
    nap->fhsize = FHSIZE;
#endif /* HAVE_NFS_ARGS_T_FHSIZE */

  /* this is the version of the nfs_args structure, not of NFS! */
#ifdef HAVE_NFS_ARGS_T_FH_LEN
# ifdef HAVE_FS_NFS3
  if (nfs_version == NFS_VERSION3)
    nap->fh_len = fhp->v3.am_fh3_length;
  else
# endif /* HAVE_FS_NFS3 */
    nap->fh_len = FHSIZE;
#endif /* HAVE_NFS_ARGS_T_FH_LEN */

  /************************************************************************/
  /***	HOST NAME							***/
  /************************************************************************/
  /*
   * XXX: warning, using xstrlcpy in NFS_HN_DREF, which may corrupt a
   * struct nfs_args, or truncate our concocted "hostname:/path"
   * string prematurely.
   */
  NFS_HN_DREF(nap->hostname, host_name);
#ifdef MNT2_NFS_OPT_HOSTNAME
  nap->flags |= MNT2_NFS_OPT_HOSTNAME;
#endif /* MNT2_NFS_OPT_HOSTNAME */

  /************************************************************************/
  /***	IP ADDRESS OF REMOTE HOST					***/
  /************************************************************************/
  if (ip_addr) {
#ifdef HAVE_TRANSPORT_TYPE_TLI
    nap->addr = ALLOC(struct netbuf); /* free()'ed at end of mount_nfs_fh() */
#endif /* HAVE_TRANSPORT_TYPE_TLI */
    NFS_SA_DREF(nap, ip_addr);
  }

  /************************************************************************/
  /***	NFS PROTOCOL (UDP, TCP) AND VERSION				***/
  /************************************************************************/
#ifdef HAVE_NFS_ARGS_T_SOTYPE
  /* bsdi3 uses this */
  if (nfs_proto) {
    if (STREQ(nfs_proto, "tcp"))
      nap->sotype = SOCK_STREAM;
    else if (STREQ(nfs_proto, "udp"))
      nap->sotype = SOCK_DGRAM;
  }
#endif /* HAVE_NFS_ARGS_T_SOTYPE */

#ifdef HAVE_NFS_ARGS_T_PROTO
  nap->proto = 0;		/* bsdi3 sets this field to zero  */
# ifdef IPPROTO_TCP
  if (nfs_proto) {
    if (STREQ(nfs_proto, "tcp"))	/* AIX 4.2.x needs this */
      nap->proto = IPPROTO_TCP;
    else if (STREQ(nfs_proto, "udp"))
      nap->proto = IPPROTO_UDP;
  }
# endif /* IPPROTO_TCP */
#endif /* HAVE_NFS_ARGS_T_SOTYPE */

#ifdef HAVE_NFS_ARGS_T_VERSION
# ifdef NFS_ARGSVERSION
  nap->version = NFS_ARGSVERSION; /* BSDI 3.0 and OpenBSD 2.2 */
# endif /* NFS_ARGSVERSION */
# ifdef DG_MOUNT_NFS_VERSION
  nap->version = DG_MOUNT_NFS_VERSION; /* dg-ux */
# endif /* DG_MOUNT_NFS_VERSION */
#endif /* HAVE_NFS_ARGS_VERSION */

  /************************************************************************/
  /***	OTHER NFS SOCKET RELATED OPTIONS AND FLAGS			***/
  /************************************************************************/

  /************************************************************************/
  /***	OTHER FLAGS AND OPTIONS						***/
  /************************************************************************/

#ifdef MNT2_NFS_OPT_BIODS
  if ((nap->biods = hasmntval(mntp, MNTTAB_OPT_BIODS)))
    nap->flags |= MNT2_NFS_OPT_BIODS;
#endif /* MNT2_NFS_OPT_BIODS */

#ifdef MNTTAB_OPT_SYMTTL	/* symlink cache time-to-live */
  if ((nap->symttl = hasmntval(mntp, MNTTAB_OPT_SYMTTL)))
    nap->args.flags |= MNT2_NFS_OPT_SYMTTL;
#endif /* MNTTAB_OPT_SYMTTL */

#ifdef MNT2_NFS_OPT_PGTHRESH	/* paging threshold */
  if ((nap->pg_thresh = hasmntval(mntp, MNTTAB_OPT_PGTHRESH)))
    nap->args.flags |= MNT2_NFS_OPT_PGTHRESH;
#endif /* MNT2_NFS_OPT_PGTHRESH */

#if defined(MNT2_NFS_OPT_POSIX) && defined(MNTTAB_OPT_POSIX)
  if (amu_hasmntopt(mntp, MNTTAB_OPT_POSIX) != NULL) {
    nap->flags |= MNT2_NFS_OPT_POSIX;
# ifdef HAVE_NFS_ARGS_T_PATHCONF
    nap->pathconf = NULL;
# endif /* HAVE_NFS_ARGS_T_PATHCONF */
  }
#endif /* MNT2_NFS_OPT_POSIX && MNTTAB_OPT_POSIX */

#ifdef HAVE_TRANSPORT_TYPE_TLI
  /* set up syncaddr field */
  nap->syncaddr = (struct netbuf *) NULL;

  /* set up knconf field */
  if (get_knetconfig(&nap->knconf, nfsncp, nfs_proto) < 0) {
    plog(XLOG_FATAL, "cannot fill knetconfig structure for nfs_args");
    going_down(1);
    return;
  }
  /* update the flags field for knconf */
  nap->args.flags |= MNT2_NFS_OPT_KNCONF;
#endif /* HAVE_TRANSPORT_TYPE_TLI */

#ifdef MNT2_NFS_OPT_FSNAME
  nap->fsname = fs_name;
  nap->args.flags |= MNT2_NFS_OPT_FSNAME;
#endif /* MNT2_NFS_OPT_FSNAME */


#ifdef HAVE_NFS_ARGS_T_OPTSTR
  nap->optstr = mntp->mnt_opts;
#endif /* HAVE_NFS_ARGS_T_OPTSTR */

#if defined(MNT2_NFS_OPT_MAXGRPS) && defined(MNTTAB_OPT_MAXGROUPS)
  nap->maxgrouplist = hasmntval(mntp, MNTTAB_OPT_MAXGROUPS);
  if (nap->maxgrouplist != 0)
    nap->flags |= MNT2_NFS_OPT_MAXGRPS;
#endif /* defined(MNT2_NFS_OPT_MAXGRPS) && defined(MNTTAB_OPT_MAXGROUPS) */

  /************************************************************************/
  /***	FINAL ACTIONS							***/
  /************************************************************************/

#ifdef HAVE_NFS_ARGS_T_GFS_FLAGS
  /* Ultrix stores generic flags in nfs_args.gfs_flags. */
  nap->gfs_flags = genflags;
#endif /* HAVE_NFS_ARGS_T_FLAGS */

  return;			/* end of compute_nfs_args() function */
}

#ifdef HAVE_FS_NFS4

#define RPC_AUTH_GSS_KRB5       390003
#define RPC_AUTH_GSS_KRB5I      390004
#define RPC_AUTH_GSS_KRB5P      390005
#define RPC_AUTH_GSS_LKEY       390006
#define RPC_AUTH_GSS_LKEYI      390007
#define RPC_AUTH_GSS_LKEYP      390008
#define RPC_AUTH_GSS_SPKM       390009
#define RPC_AUTH_GSS_SPKMI      390010
#define RPC_AUTH_GSS_SPKMP      390011

struct {
  const char *name;
  int num;
} flavours[] = {
  { "unix",	AUTH_UNIX },
  { "krb5",	RPC_AUTH_GSS_KRB5 },
  { "krb5i",	RPC_AUTH_GSS_KRB5I },
  { "krb5p",	RPC_AUTH_GSS_KRB5P },
  { "lkey",	RPC_AUTH_GSS_LKEY },
  { "lkeyi",	RPC_AUTH_GSS_LKEYI },
  { "lkeyp",	RPC_AUTH_GSS_LKEYP },
  { "spkm",	RPC_AUTH_GSS_SPKM },
  { "spkmi",	RPC_AUTH_GSS_SPKMI },
  { "spkmp",	RPC_AUTH_GSS_SPKMP },
};

static char *
set_nfs4_security(nfs4_args_t *nap, mntent_t *mntp)
{
  const char *o = hasmnteq(mntp, MNTTAB_OPT_SEC);
  char *q, *s, *ss;
  size_t l, i;

  if (o == NULL)
    o = "unix";

  for (l = 1, q = strchr(o, ','); q; q = strchr(q + 1, ','))
    l++;

  nap->auth_flavours = xmalloc(l * sizeof(*nap->auth_flavours));

  s = ss = xstrdup(o);
  for (;;) {
    q = strchr(s, ',');
    if (q)
	*q = '\0';

    for (l = 0, i = 0; i < sizeof(flavours) / sizeof(flavours[0]); i++)
      if (strcmp(flavours[i].name, s) == 0) {
	nap->auth_flavours[l++] = flavours[i].num;
	break;
      }

    if (i == sizeof(flavours) / sizeof(flavours[0]))
      plog(XLOG_ERROR, "Unknown NFSv4 security mechanism %s\n", s);

    if (q == NULL)
      break;

    *q = ':';
    s = ++q;
  }

  nap->auth_flavourlen = l;
  return ss;
}

static int
get_my_ipv4addr(struct nfs_string *ns)
{
  struct hostent *hp;
  char myname[MAXHOSTNAMELEN];

  if (gethostname(myname, sizeof(myname)) == -1)
    return -1;
  if ((hp = gethostbyname(myname)) == NULL)
    return -1;
  if (inet_ntop(AF_INET, hp->h_addr, myname, sizeof(myname)) == NULL)
    return -1;
  ns->len = strlen(myname);
  ns->data = xmalloc(ns->len + 1);
  memcpy(ns->data, myname, ns->len + 1);
  return 0;
}

static void
add_nfs4_mntopts(const nfs4_args_t *nap, mntent_t *mntp, char *sec)
{
  char *opts = mntp->mnt_opts;
  char buf[1024], addr[128];
  size_t len = strlen(mntp->mnt_opts);

  if (inet_ntop(AF_INET,
      &((const struct sockaddr_in *)nap->host_addr)->sin_addr,
      addr, sizeof(addr)) == NULL)
    return;

  xsnprintf(buf, sizeof(buf), ",clientaddr=%s,addr=%s", nap->client_addr.data,
	    addr);

  len += strlen(buf) + 1;

  if (sec && strcmp(sec, "unix") != 0) {
    len += strlen(sec) + strlen(MNTTAB_OPT_SEC) + 2; /* 2 = ",=" */
  } else
    sec = NULL;

  opts = xrealloc(mntp->mnt_opts, len);
  xstrlcat(opts, buf, len);

  if (sec) {
    xstrlcat(opts, ",", len);
    xstrlcat(opts, MNTTAB_OPT_SEC, len);
    xstrlcat(opts, "=", len);
    xstrlcat(opts, sec, len);
  }

  mntp->mnt_opts = opts;
}

static void
print_nfs4_security(const nfs4_args_t *nap)
{
  char buf[1024];
  char num[64];
  size_t i, j;

  buf[0] = '\0';

  for (i = 0; i < nap->auth_flavourlen; i++) {

    for (j = 0; j < sizeof(flavours) / sizeof(flavours[0]); j++)
      if (flavours[j].num == nap->auth_flavours[i]) {
	xstrlcpy(num, flavours[j].name, sizeof(num));
	break;
      }

    if (j == sizeof(flavours) / sizeof(flavours[0])) {
      plog(XLOG_ERROR, "Unknown NFSv4 security mechanism %d\n",
           nap->auth_flavours[i]);
      xsnprintf(num, sizeof(num), "*%d*", nap->auth_flavours[i]);
    }

    if (buf[0])
      xstrlcat(buf, ":", sizeof(buf));

    xstrlcat(buf, num, sizeof(buf));
  }

  plog(XLOG_DEBUG, "NA->auth_flavours \"%s\"\n", buf);
}

static void
discard_nfs4_args(nfs4_args_t *nap)
{
  if (nap->client_addr.data)
    free(nap->client_addr.data);
  if (nap->hostname.data)
    free(nap->hostname.data);
  if (nap->mnt_path.data)
    free(nap->mnt_path.data);
  if (nap->host_addr)
    free(nap->host_addr);
  if (nap->auth_flavours)
    free(nap->auth_flavours);
}

/*
 * Fill in the many possible fields and flags of struct nfs4_args.
 *
 * nap:		pre-allocated structure to fill in.
 * mntp:	mount entry structure (includes options)
 * genflags:	generic mount flags already determined
 * nfsncp:	(TLI only) netconfig entry for this NFS mount
 * ip_addr:	IP address of file server
 * nfs_version:	4, or 0 if unknown
 * nfs_proto:	"udp", "tcp", or NULL.
 * fhp:		file handle structure pointer
 * host_name:	name of remote NFS host
 * fs_name:	remote file system name to mount
 */
static void
compute_nfs4_args(nfs4_args_t *nap,
                  mntent_t *mntp,
                  int genflags,
                  struct netconfig *nfsncp,
                  struct sockaddr_in *ip_addr,
                  u_long nfs_version,
                  char *nfs_proto,
                  am_nfs_handle_t *fhp,
                  char *host_name,
                  char *fs_name)
{
  char *s;
  struct nfs_common_args a;
  uint16_t nfs_port;

  /* initialize just in case */
  memset((voidp) nap, 0, sizeof(nfs4_args_t));

  /* compute all of the NFS attribute-cache flags */
  memset(&a, 0, sizeof(a));
  compute_nfs_attrcache_flags(&a, mntp);
  compute_nfs_common_args(&a, mntp, nfs_proto, nfs_version);
  get_nfs_common_args(nap, a);

  get_my_ipv4addr(&nap->client_addr);

  /************************************************************************/
  /***	HOST NAME							***/
  /************************************************************************/
  nap->hostname.len = strlen(host_name);
  nap->hostname.data = xmalloc(nap->hostname.len + 1);
  memcpy(nap->hostname.data, host_name, nap->hostname.len + 1);

  if ((s = strchr(fs_name, ':')) != NULL)
    s++;
  else
    s = fs_name;

  nap->mnt_path.len = strlen(s);
  nap->mnt_path.data = xmalloc(nap->mnt_path.len + 1);
  memcpy(nap->mnt_path.data, s, nap->mnt_path.len + 1);
  plog(XLOG_DEBUG, "dir name %s\n", nap->mnt_path.data);

  /************************************************************************/
  /***  IP ADDRESS OF REMOTE HOST                                       ***/
  /************************************************************************/
  nap->host_addrlen = sizeof(*ip_addr);
  nap->host_addr = xmalloc(nap->host_addrlen);
  memcpy(nap->host_addr, ip_addr, nap->host_addrlen);

  nfs_port = hasmntval(mntp, MNTTAB_OPT_PORT);
  if (nfs_port == 0)
    nfs_port = htons(NFS_PORT);
  else
    nfs_port = htons(nfs_port);

  ((struct sockaddr_in *)nap->host_addr)->sin_port = nfs_port;

  nap->proto = 0;               /* bsdi3 sets this field to zero  */
  if (nfs_proto) {
    if (STREQ(nfs_proto, "tcp"))        /* AIX 4.2.x needs this */
      nap->proto = IPPROTO_TCP;
    else if (STREQ(nfs_proto, "udp"))
      nap->proto = IPPROTO_UDP;
  }

  nap->version = NFS4_MOUNT_VERSION; /* BSDI 3.0 and OpenBSD 2.2 */

  /************************************************************************/
  /***  OTHER NFS SOCKET RELATED OPTIONS AND FLAGS                      ***/
  /************************************************************************/


  /************************************************************************/
  /***  OTHER FLAGS AND OPTIONS                                         ***/
  /************************************************************************/

#if defined(MNT2_NFS_OPT_POSIX) && defined(MNTTAB_OPT_POSIX)
  if (amu_hasmntopt(mntp, MNTTAB_OPT_POSIX) != NULL) {
    nap->args.flags |= MNT2_NFS_OPT_POSIX;
# ifdef HAVE_NFS_ARGS_T_PATHCONF
    nap->pathconf = NULL;
# endif /* HAVE_NFS_ARGS_T_PATHCONF */
  }
#endif /* MNT2_NFS_OPT_POSIX && MNTTAB_OPT_POSIX */

#if defined(MNT2_NFS_OPT_MAXGRPS) && defined(MNTTAB_OPT_MAXGROUPS)
  nap->maxgrouplist = hasmntval(mntp, MNTTAB_OPT_MAXGROUPS);
  if (nap->maxgrouplist != 0)
    nap->args.flags |= MNT2_NFS_OPT_MAXGRPS;
#endif /* defined(MNT2_NFS_OPT_MAXGRPS) && defined(MNTTAB_OPT_MAXGROUPS) */

#ifdef HAVE_NFS_ARGS_T_OPTSTR
  nap->optstr = mntp->mnt_opts;
#endif /* HAVE_NFS_ARGS_T_OPTSTR */

  /************************************************************************/
  /***  FINAL ACTIONS                                                   ***/
  /************************************************************************/

#ifdef HAVE_NFS_ARGS_T_GFS_FLAGS
  /* Ultrix stores generic flags in nfs_args.gfs_flags. */
  nap->gfs_flags = genflags;
#endif /* HAVE_NFS_ARGS_T_FLAGS */

  s = set_nfs4_security(nap, mntp);

  /* Add addresses to the mount options */
  add_nfs4_mntopts(nap, mntp, s);

  return;                       /* end of compute_nfs4_args() function */
}

#ifdef DEBUG
static void
print_nfs4_args(const nfs4_args_t *nap, u_long nfs_version)
{
  struct sockaddr_in *sap;
  struct nfs_common_args a;

  if (!nap) {
    plog(XLOG_DEBUG, "NULL nfs_args!");
    return;
  }

  plog(XLOG_DEBUG, "NA->client_addr \"%s\"\n", nap->client_addr.data);
  plog(XLOG_DEBUG, "NA->mnt_path = \"%s\"", nap->mnt_path.data);
  plog(XLOG_DEBUG, "NA->hostname = \"%s\"", nap->hostname.data);
  sap = (struct sockaddr_in *) nap->host_addr;
  print_nfs_sockaddr_in("host_addr", sap);
  plog(XLOG_DEBUG, "NA->proto = %d", (int) nap->proto);
#ifdef HAVE_NFS_ARGS_T_VERSION
  plog(XLOG_DEBUG, "NA->version = %d", nap->version);
#endif /* HAVE_NFS_ARGS_T_VERSION */
  print_nfs4_security(nap);

  put_nfs_common_args(nap, a);
  print_nfs_common_args(&a);
}
#endif
#endif /* HAVE_FS_NFS4 */

void
compute_nfs_args(void *nap,
                 mntent_t *mntp,
                 int genflags,
                 struct netconfig *nfsncp,
                 struct sockaddr_in *ip_addr,
                 u_long nfs_version,
                 char *nfs_proto,
                 am_nfs_handle_t *fhp,
                 char *host_name,
                 char *fs_name)
{
#ifdef HAVE_FS_NFS4
  if (nfs_version == NFS_VERSION4)
    compute_nfs4_args(nap, mntp, genflags, nfsncp, ip_addr, nfs_version,
		      nfs_proto, fhp, host_name, fs_name);
  else
#endif /* HAVE_FS_NFS4 */
    compute_nfs23_args(nap, mntp, genflags, nfsncp, ip_addr, nfs_version,
		       nfs_proto, fhp, host_name, fs_name);
}

void
discard_nfs_args(void *nap, u_long nfs_version)
{
#ifdef HAVE_FS_NFS4
  if (nfs_version == NFS_VERSION4)
    discard_nfs4_args(nap);
  else
#endif /* HAVE_FS_NFS4 */
    discard_nfs23_args(nap);
}

#ifdef DEBUG
void
print_nfs_args(const void *nap, u_long nfs_version)
{
#ifdef HAVE_FS_NFS4
  if (nfs_version == NFS_VERSION4)
    print_nfs4_args(nap, nfs_version);
  else
#endif /* HAVE_FS_NFS4 */
    print_nfs23_args(nap, nfs_version);
}
#endif


/*
 * Fill in special values for flags and fields of nfs_args, for an
 * automounter NFS mount.
 */
void
compute_automounter_nfs_args(nfs_args_t *nap, mntent_t *mntp)
{
  struct nfs_common_args a;

#ifdef MNT2_NFS_OPT_SYMTTL
  /*
   * Don't let the kernel cache symbolic links we generate, or else lookups
   * will bypass amd and fail to remount stuff as needed.
   */
  plog(XLOG_INFO, "turning on NFS option symttl and setting value to 0");
  nap->flags |= MNT2_NFS_OPT_SYMTTL;
  nap->symttl = 0;
#endif /* MNT2_NFS_OPT_SYMTTL */

  /*
   * This completes the flags for the HIDE_MOUNT_TYPE  code in the
   * mount_amfs_toplvl() function in amd/amfs_toplvl.c.
   * Some systems don't have a mount type, but a mount flag.
   */
#ifdef MNT2_NFS_OPT_AUTO
  nap->flags |= MNT2_NFS_OPT_AUTO;
#endif /* MNT2_NFS_OPT_AUTO */
#ifdef MNT2_NFS_OPT_IGNORE
  nap->flags |= MNT2_NFS_OPT_IGNORE;
#endif /* MNT2_NFS_OPT_IGNORE */
#ifdef MNT2_GEN_OPT_AUTOMNTFS
  nap->flags |= MNT2_GEN_OPT_AUTOMNTFS;
#endif /* not MNT2_GEN_OPT_AUTOMNTFS */

#ifdef MNT2_NFS_OPT_DUMBTIMR
  /*
   * Don't let the kernel start computing throughput of Amd.  The numbers
   * will be meaningless because of the way Amd does mount retries.
   */
  plog(XLOG_INFO, "%s: disabling nfs congestion window", mntp->mnt_dir);
  nap->flags |= MNT2_NFS_OPT_DUMBTIMR;
#endif /* MNT2_NFS_OPT_DUMBTIMR */

  /* compute all of the NFS attribute-cache flags */
  memset(&a, 0, sizeof(a));
  a.flags = nap->flags;
  compute_nfs_attrcache_flags(&a, mntp);
  get_nfs_common_args(nap, a);

  /*
   * Provide a slight bit more security by requiring the kernel to use
   * reserved ports.
   */
#ifdef MNT2_NFS_OPT_RESVPORT
  nap->flags |= MNT2_NFS_OPT_RESVPORT;
#endif /* MNT2_NFS_OPT_RESVPORT */
}

int
nfs_valid_version(u_long v)
{
  return v >= NFS_VERS_MIN && v <= NFS_VERS_MAX;
}
