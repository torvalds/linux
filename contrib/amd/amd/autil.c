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
 * File: am-utils/amd/autil.c
 *
 */

/*
 * utilities specified to amd, taken out of the older amd/util.c.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

int NumChildren = 0;		/* number of children of primary amd */
static char invalid_keys[] = "\"'!;@ \t\n";

/****************************************************************************
 *** MACROS                                                               ***
 ****************************************************************************/

#ifdef HAVE_TRANSPORT_TYPE_TLI
# define PARENT_USLEEP_TIME	100000 /* 0.1 seconds */
#endif /* HAVE_TRANSPORT_TYPE_TLI */


/****************************************************************************
 *** FORWARD DEFINITIONS                                                  ***
 ****************************************************************************/
static void domain_strip(char *otherdom, char *localdom);
static int dofork(void);


/****************************************************************************
 *** FUNCTIONS                                                             ***
 ****************************************************************************/

/*
 * Copy s into p, reallocating p if necessary
 */
char *
strealloc(char *p, char *s)
{
  size_t len = strlen(s) + 1;

  p = (char *) xrealloc((voidp) p, len);

  xstrlcpy(p, s, len);
#ifdef DEBUG_MEM
# if defined(HAVE_MALLINFO) && defined(HAVE_MALLOC_VERIFY)
  malloc_verify();
# endif /* not defined(HAVE_MALLINFO) && defined(HAVE_MALLOC_VERIFY) */
#endif /* DEBUG_MEM */
  return p;
}


/*
 * Strip off the trailing part of a domain
 * to produce a short-form domain relative
 * to the local host domain.
 * Note that this has no effect if the domain
 * names do not have the same number of
 * components.  If that restriction proves
 * to be a problem then the loop needs recoding
 * to skip from right to left and do partial
 * matches along the way -- ie more expensive.
 */
static void
domain_strip(char *otherdom, char *localdom)
{
  char *p1, *p2;

  if ((p1 = strchr(otherdom, '.')) &&
      (p2 = strchr(localdom, '.')) &&
      STREQ(p1 + 1, p2 + 1))
    *p1 = '\0';
}


/*
 * Normalize a host name: replace cnames with real names, and decide if to
 * strip domain name or not.
 */
void
host_normalize(char **chp)
{
  /*
   * Normalize hosts is used to resolve host name aliases
   * and replace them with the standard-form name.
   * Invoked with "-n" command line option.
   */
  if (gopt.flags & CFM_NORMALIZE_HOSTNAMES) {
    struct hostent *hp;
    hp = gethostbyname(*chp);
    if (hp && hp->h_addrtype == AF_INET) {
      dlog("Hostname %s normalized to %s", *chp, hp->h_name);
      *chp = strealloc(*chp, (char *) hp->h_name);
    }
  }
  if (gopt.flags & CFM_DOMAIN_STRIP) {
    domain_strip(*chp, hostd);
  }
}


/*
 * Keys are not allowed to contain " ' ! or ; to avoid
 * problems with macro expansions.
 */
int
valid_key(char *key)
{
  while (*key)
    if (strchr(invalid_keys, *key++))
      return FALSE;
  return TRUE;
}


void
forcibly_timeout_mp(am_node *mp)
{
  mntfs *mf = mp->am_al->al_mnt;
  /*
   * Arrange to timeout this node
   */
  if (mf && ((mp->am_flags & AMF_ROOT) ||
	     (mf->mf_flags & (MFF_MOUNTING | MFF_UNMOUNTING)))) {
    /*
     * We aren't going to schedule a timeout, so we need to notify the
     * child here unless we are already unmounting, in which case that
     * process is responsible for notifying the child.
     */
    if (mf->mf_flags & MFF_UNMOUNTING)
      plog(XLOG_WARNING, "node %s is currently being unmounted, ignoring timeout request", mp->am_path);
    else {
      plog(XLOG_WARNING, "ignoring timeout request for active node %s", mp->am_path);
      notify_child(mp, AMQ_UMNT_FAILED, EBUSY, 0);
    }
  } else {
    plog(XLOG_INFO, "\"%s\" forcibly timed out", mp->am_path);
    mp->am_flags &= ~AMF_NOTIMEOUT;
    mp->am_ttl = clocktime(NULL);
    /*
     * Force mtime update of parent dir, to prevent DNLC/dcache from caching
     * the old entry, which could result in ESTALE errors, bad symlinks, and
     * more.
     */
    clocktime(&mp->am_parent->am_fattr.na_mtime);
    reschedule_timeout_mp();
  }
}


void
mf_mounted(mntfs *mf, bool_t call_free_opts)
{
  int quoted;
  int wasmounted = mf->mf_flags & MFF_MOUNTED;

  if (!wasmounted) {
    /*
     * If this is a freshly mounted
     * filesystem then update the
     * mntfs structure...
     */
    mf->mf_flags |= MFF_MOUNTED;
    mf->mf_error = 0;

    /*
     * Do mounted callback
     */
    if (mf->mf_ops->mounted)
      mf->mf_ops->mounted(mf);

    /*
     * We used to free the mf_mo (options) here, however they're now stored
     * and managed with the mntfs and do not need to be free'd here (this ensures
     * that we use the same options to monitor/unmount the system as we used
     * to mount it).
     */
  }

  if (mf->mf_flags & MFF_RESTART) {
    mf->mf_flags &= ~MFF_RESTART;
    dlog("Restarted filesystem %s, flags 0x%x", mf->mf_mount, mf->mf_flags);
  }

  /*
   * Log message
   */
  quoted = strchr(mf->mf_info, ' ') != 0;
  plog(XLOG_INFO, "%s%s%s %s fstype %s on %s",
       quoted ? "\"" : "",
       mf->mf_info,
       quoted ? "\"" : "",
       wasmounted ? "referenced" : "mounted",
       mf->mf_ops->fs_type, mf->mf_mount);
}


void
am_mounted(am_node *mp)
{
  int notimeout = 0;		/* assume normal timeouts initially */
  mntfs *mf = mp->am_al->al_mnt;

  /*
   * This is the parent mntfs which does the mf->mf_fo (am_opts type), and
   * we're passing TRUE here to tell mf_mounted to actually free the
   * am_opts.  See a related comment in mf_mounted().
   */
  mf_mounted(mf, TRUE);

#ifdef HAVE_FS_AUTOFS
  if (mf->mf_flags & MFF_IS_AUTOFS)
    autofs_mounted(mp);
#endif /* HAVE_FS_AUTOFS */

  /*
   * Patch up path for direct mounts
   */
  if (mp->am_parent && mp->am_parent->am_al->al_mnt->mf_fsflags & FS_DIRECT)
    mp->am_path = str3cat(mp->am_path, mp->am_parent->am_path, "/", ".");

  /*
   * Check whether this mount should be cached permanently or not,
   * and handle user-requested timeouts.
   */
  /* first check if file system was set to never timeout */
  if (mf->mf_fsflags & FS_NOTIMEOUT)
    notimeout = 1;
  /* next, alter that decision by map flags */

  if (mf->mf_mopts) {
    mntent_t mnt;
    mnt.mnt_opts = mf->mf_mopts;

    /* umount option: user wants to unmount this entry */
    if (amu_hasmntopt(&mnt, "unmount") || amu_hasmntopt(&mnt, "umount"))
      notimeout = 0;
    /* noumount option: user does NOT want to unmount this entry */
    if (amu_hasmntopt(&mnt, "nounmount") || amu_hasmntopt(&mnt, "noumount"))
      notimeout = 1;
    /* utimeout=N option: user wants to unmount this option AND set timeout */
    if ((mp->am_timeo = hasmntval(&mnt, "utimeout")) == 0)
      mp->am_timeo = gopt.am_timeo; /* otherwise use default timeout */
    else
      notimeout = 0;
    /* special case: don't try to unmount "/" (it can never succeed) */
    if (mf->mf_mount[0] == '/' && mf->mf_mount[1] == '\0')
      notimeout = 1;
  }
  /* finally set actual flags */
  if (notimeout) {
    mp->am_flags |= AMF_NOTIMEOUT;
    plog(XLOG_INFO, "%s set to never timeout", mp->am_path);
  } else {
    mp->am_flags &= ~AMF_NOTIMEOUT;
    plog(XLOG_INFO, "%s set to timeout in %d seconds", mp->am_path, mp->am_timeo);
  }

  /*
   * If this node is a symlink then
   * compute the length of the returned string.
   */
  if (mp->am_fattr.na_type == NFLNK)
    mp->am_fattr.na_size = strlen(mp->am_link ? mp->am_link : mf->mf_mount);

  /*
   * Record mount time, and update am_stats at the same time.
   */
  mp->am_stats.s_mtime = clocktime(&mp->am_fattr.na_mtime);
  new_ttl(mp);

  /*
   * Update mtime of parent node (copying "struct nfstime" in '=' below)
   */
  if (mp->am_parent && mp->am_parent->am_al->al_mnt)
    mp->am_parent->am_fattr.na_mtime = mp->am_fattr.na_mtime;

  /*
   * This is ugly, but essentially unavoidable
   * Sublinks must be treated separately as type==link
   * when the base type is different.
   */
  if (mp->am_link && mf->mf_ops != &amfs_link_ops)
    amfs_link_ops.mount_fs(mp, mf);

  /*
   * Now, if we can, do a reply to our client here
   * to speed things up.
   */
#ifdef HAVE_FS_AUTOFS
  if (mp->am_flags & AMF_AUTOFS)
    autofs_mount_succeeded(mp);
  else
#endif /* HAVE_FS_AUTOFS */
    nfs_quick_reply(mp, 0);

  /*
   * Update stats
   */
  amd_stats.d_mok++;
}


/*
 * Replace mount point with a reference to an error filesystem.
 * The mount point (struct mntfs) is NOT discarded,
 * the caller must do it if it wants to _before_ calling this function.
 */
void
assign_error_mntfs(am_node *mp)
{
  int error;
  dlog("assign_error_mntfs");

  if (mp->am_al == NULL) {
    plog(XLOG_ERROR, "%s: Can't assign error", __func__);
    return;
  }
  /*
   * Save the old error code
   */
  error = mp->am_error;
  if (error <= 0)
    error = mp->am_al->al_mnt->mf_error;
  /*
   * Allocate a new error reference
   */
  free_loc(mp->am_al);
  mp->am_al = new_loc();
  /*
   * Put back the error code
   */
  mp->am_al->al_mnt->mf_error = error;
  mp->am_al->al_mnt->mf_flags |= MFF_ERROR;
  /*
   * Zero the error in the mount point
   */
  mp->am_error = 0;
}


/*
 * Build a new map cache for this node, or re-use
 * an existing cache for the same map.
 */
void
amfs_mkcacheref(mntfs *mf)
{
  char *cache;

  if (mf->mf_fo && mf->mf_fo->opt_cache)
    cache = mf->mf_fo->opt_cache;
  else
    cache = "none";
  mf->mf_private = (opaque_t) mapc_find(mf->mf_info,
					cache,
					(mf->mf_fo ? mf->mf_fo->opt_maptype : NULL),
					mf->mf_mount);
  mf->mf_prfree = mapc_free;
}


/*
 * Locate next node in sibling list which is mounted
 * and is not an error node.
 */
am_node *
next_nonerror_node(am_node *xp)
{
  mntfs *mf;

  /*
   * Bug report (7/12/89) from Rein Tollevik <rein@ifi.uio.no>
   * Fixes a race condition when mounting direct automounts.
   * Also fixes a problem when doing a readdir on a directory
   * containing hung automounts.
   */
  while (xp &&
	 (!(mf = xp->am_al->al_mnt) ||	/* No mounted filesystem */
	  mf->mf_error != 0 ||	/* There was a mntfs error */
	  xp->am_error != 0 ||	/* There was a mount error */
	  !(mf->mf_flags & MFF_MOUNTED) ||	/* The fs is not mounted */
	  (mf->mf_server->fs_flags & FSF_DOWN))	/* The fs may be down */
	 )
    xp = xp->am_osib;

  return xp;
}


/*
 * Mount an automounter directory.
 * The automounter is connected into the system
 * as a user-level NFS server.  amfs_mount constructs
 * the necessary NFS parameters to be given to the
 * kernel so that it will talk back to us.
 *
 * NOTE: automounter mounts in themselves are using NFS Version 2 (UDP).
 *
 * NEW: on certain systems, mounting can be done using the
 * kernel-level automount (autofs) support. In that case,
 * we don't need NFS at all here.
 */
int
amfs_mount(am_node *mp, mntfs *mf, char *opts)
{
  char fs_hostname[MAXHOSTNAMELEN + MAXPATHLEN + 1];
  int retry, error = 0, genflags;
  int on_autofs = mf->mf_flags & MFF_ON_AUTOFS;
  char *dir = mf->mf_mount;
  mntent_t mnt;
  MTYPE_TYPE type;
  int forced_unmount = 0;	/* are we using forced unmounts? */
  u_long nfs_version = get_nfs_dispatcher_version(nfs_dispatcher);

  memset(&mnt, 0, sizeof(mnt));
  mnt.mnt_dir = dir;
  mnt.mnt_fsname = pid_fsname;
  mnt.mnt_opts = opts;

#ifdef HAVE_FS_AUTOFS
  if (mf->mf_flags & MFF_IS_AUTOFS) {
    type = MOUNT_TYPE_AUTOFS;
    /*
     * Make sure that amd's top-level autofs mounts are hidden by default
     * from df.
     * XXX: It works ok on Linux, might not work on other systems.
     */
    mnt.mnt_type = "autofs";
  } else
#endif /* HAVE_FS_AUTOFS */
  {
    type = MOUNT_TYPE_NFS;
    /*
     * Make sure that amd's top-level NFS mounts are hidden by default
     * from df.
     * If they don't appear to support the either the "ignore" mnttab
     * option entry, or the "auto" one, set the mount type to "nfs".
     */
    mnt.mnt_type = HIDE_MOUNT_TYPE;
  }

  retry = hasmntval(&mnt, MNTTAB_OPT_RETRY);
  if (retry <= 0)
    retry = 2;			/* XXX: default to 2 retries */

  /*
   * SET MOUNT ARGS
   */

  /*
   * Make a ``hostname'' string for the kernel
   */
  xsnprintf(fs_hostname, sizeof(fs_hostname), "pid%ld@%s:%s",
	    get_server_pid(), am_get_hostname(), dir);
  /*
   * Most kernels have a name length restriction (64 bytes)...
   */
  if (strlen(fs_hostname) >= MAXHOSTNAMELEN)
    xstrlcpy(fs_hostname + MAXHOSTNAMELEN - 3, "..",
	     sizeof(fs_hostname) - MAXHOSTNAMELEN + 3);
#ifdef HOSTNAMESZ
  /*
   * ... and some of these restrictions are 32 bytes (HOSTNAMESZ)
   * If you need to get the definition for HOSTNAMESZ found, you may
   * add the proper header file to the conf/nfs_prot/nfs_prot_*.h file.
   */
  if (strlen(fs_hostname) >= HOSTNAMESZ)
    xstrlcpy(fs_hostname + HOSTNAMESZ - 3, "..",
	     sizeof(fs_hostname) - HOSTNAMESZ + 3);
#endif /* HOSTNAMESZ */

  /*
   * Finally we can compute the mount genflags set above,
   * and add any automounter specific flags.
   */
  genflags = compute_mount_flags(&mnt);
#ifdef HAVE_FS_AUTOFS
  if (on_autofs)
    genflags |= autofs_compute_mount_flags(&mnt);
#endif /* HAVE_FS_AUTOFS */
  genflags |= compute_automounter_mount_flags(&mnt);

again:
  if (!(mf->mf_flags & MFF_IS_AUTOFS)) {
    nfs_args_t nfs_args;
    am_nfs_handle_t *fhp, anh;
#ifndef HAVE_TRANSPORT_TYPE_TLI
    u_short port;
    struct sockaddr_in sin;
#endif /* not HAVE_TRANSPORT_TYPE_TLI */

    /*
     * get fhandle of remote path for automount point
     */
    fhp = get_root_nfs_fh(dir, &anh);
    if (!fhp) {
      plog(XLOG_FATAL, "Can't find root file handle for %s", dir);
      return EINVAL;
    }

#ifndef HAVE_TRANSPORT_TYPE_TLI
    /*
     * Create sockaddr to point to the local machine.
     */
    memset(&sin, 0, sizeof(sin));
    /* as per POSIX, sin_len need not be set (used internally by kernel) */
    sin.sin_family = AF_INET;
    sin.sin_addr = myipaddr;
    port = hasmntval(&mnt, MNTTAB_OPT_PORT);
    if (port) {
      sin.sin_port = htons(port);
    } else {
      plog(XLOG_ERROR, "no port number specified for %s", dir);
      return EINVAL;
    }
#endif /* not HAVE_TRANSPORT_TYPE_TLI */

    /* setup the many fields and flags within nfs_args */
#ifdef HAVE_TRANSPORT_TYPE_TLI
    compute_nfs_args(&nfs_args,
		     &mnt,
		     genflags,
		     nfsncp,
		     NULL,	/* remote host IP addr is set below */
		     nfs_version,
		     "udp",
		     fhp,
		     fs_hostname,
		     pid_fsname);
    /*
     * IMPORTANT: set the correct IP address AFTERWARDS.  It cannot
     * be done using the normal mechanism of compute_nfs_args(), because
     * that one will allocate a new address and use NFS_SA_DREF() to copy
     * parts to it, while assuming that the ip_addr passed is always
     * a "struct sockaddr_in".  That assumption is incorrect on TLI systems,
     * because they define a special macro HOST_SELF which is DIFFERENT
     * than localhost (127.0.0.1)!
     */
    nfs_args.addr = &nfsxprt->xp_ltaddr;
#else /* not HAVE_TRANSPORT_TYPE_TLI */
    compute_nfs_args(&nfs_args,
		     &mnt,
		     genflags,
		     NULL,
		     &sin,
		     nfs_version,
		     "udp",
		     fhp,
		     fs_hostname,
		     pid_fsname);
#endif /* not HAVE_TRANSPORT_TYPE_TLI */

    /*************************************************************************
     * NOTE: while compute_nfs_args() works ok for regular NFS mounts	     *
     * the toplvl one is not quite regular, and so some options must be      *
     * corrected by hand more carefully, *after* compute_nfs_args() runs.    *
     *************************************************************************/
    compute_automounter_nfs_args(&nfs_args, &mnt);

    if (amuDebug(D_TRACE)) {
      print_nfs_args(&nfs_args, 0);
      plog(XLOG_DEBUG, "Generic mount flags 0x%x", genflags);
    }

    /* This is it!  Here we try to mount amd on its mount points */
    error = mount_fs(&mnt, genflags, (caddr_t) &nfs_args,
		     retry, type, 0, NULL, mnttab_file_name, on_autofs);

#ifdef HAVE_TRANSPORT_TYPE_TLI
    free_knetconfig(nfs_args.knconf);
    /*
     * local automounter mounts do not allocate a special address, so
     * no need to XFREE(nfs_args.addr) under TLI.
     */
#endif /* HAVE_TRANSPORT_TYPE_TLI */

#ifdef HAVE_FS_AUTOFS
  } else {
    /* This is it!  Here we try to mount amd on its mount points */
    error = mount_fs(&mnt, genflags, (caddr_t) mp->am_autofs_fh,
		     retry, type, 0, NULL, mnttab_file_name, on_autofs);
#endif /* HAVE_FS_AUTOFS */
  }
  if (error == 0 || forced_unmount)
     return error;

  /*
   * If user wants forced/lazy unmount semantics, then try it iff the
   * current mount failed with EIO or ESTALE.
   */
  if (gopt.flags & CFM_FORCED_UNMOUNTS) {
    switch (errno) {
    case ESTALE:
    case EIO:
      forced_unmount = errno;
      plog(XLOG_WARNING, "Mount %s failed (%m); force unmount.", mp->am_path);
      if ((error = UMOUNT_FS(mp->am_path, mnttab_file_name,
			     AMU_UMOUNT_FORCE | AMU_UMOUNT_DETACH)) < 0) {
	plog(XLOG_WARNING, "Forced umount %s failed: %m.", mp->am_path);
	errno = forced_unmount;
      } else
	goto again;
    default:
      break;
    }
  }

  return error;
}


void
am_unmounted(am_node *mp)
{
  mntfs *mf = mp->am_al->al_mnt;

  if (!foreground) {		/* firewall - should never happen */
    /*
     * This is a coding error.  Make sure we hear about it!
     */
    plog(XLOG_FATAL, "am_unmounted: illegal use in background (%s)",
	mp->am_name);
    notify_child(mp, AMQ_UMNT_OK, 0, 0);	/* XXX - be safe? */
    return;
  }

  /*
   * Do unmounted callback
   */
  if (mf->mf_ops->umounted)
    mf->mf_ops->umounted(mf);

  /*
   * This is ugly, but essentially unavoidable.
   * Sublinks must be treated separately as type==link
   * when the base type is different.
   */
  if (mp->am_link && mf->mf_ops != &amfs_link_ops)
    amfs_link_ops.umount_fs(mp, mf);

#ifdef HAVE_FS_AUTOFS
  if (mf->mf_flags & MFF_IS_AUTOFS)
    autofs_release_fh(mp);
  if (mp->am_flags & AMF_AUTOFS)
    autofs_umount_succeeded(mp);
#endif /* HAVE_FS_AUTOFS */

  /*
   * Clean up any directories that were made
   *
   * If we remove the mount point of a pending mount, any queued access
   * to it will fail. So don't do it in that case.
   * Also don't do it if the refcount is > 1.
   */
  if (mf->mf_flags & MFF_MKMNT &&
      mf->mf_refc == 1 &&
      !(mp->am_flags & AMF_REMOUNT)) {
    plog(XLOG_INFO, "removing mountpoint directory '%s'", mf->mf_mount);
    rmdirs(mf->mf_mount);
    mf->mf_flags &= ~MFF_MKMNT;
  }

  /*
   * If this is a pseudo-directory then adjust the link count
   * in the parent
   */
  if (mp->am_parent && mp->am_fattr.na_type == NFDIR)
    --mp->am_parent->am_fattr.na_nlink;

  /*
   * Update mtime of parent node
   */
  if (mp->am_parent && mp->am_parent->am_al->al_mnt)
    clocktime(&mp->am_parent->am_fattr.na_mtime);

  if (mp->am_parent && (mp->am_flags & AMF_REMOUNT)) {
    char *fname = xstrdup(mp->am_name);
    am_node *mp_parent = mp->am_parent;
    mntfs *mf_parent = mp_parent->am_al->al_mnt;
    am_node fake_mp;
    int error = 0;

    /*
     * We need to use notify_child() after free_map(), so save enough
     * to do that in fake_mp.
     */
    fake_mp.am_fd[1] = mp->am_fd[1];
    mp->am_fd[1] = -1;

    free_map(mp);
    plog(XLOG_INFO, "am_unmounted: remounting %s", fname);
    mp = mf_parent->mf_ops->lookup_child(mp_parent, fname, &error, VLOOK_CREATE);
    if (mp && error < 0)
      (void)mf_parent->mf_ops->mount_child(mp, &error);
    if (error > 0) {
      errno = error;
      plog(XLOG_ERROR, "am_unmounted: could not remount %s: %m", fname);
      notify_child(&fake_mp, AMQ_UMNT_OK, 0, 0);
    } else {
      notify_child(&fake_mp, AMQ_UMNT_FAILED, EBUSY, 0);
    }
    XFREE(fname);
  } else {
    /*
     * We have a race here.
     * If this node has a pending mount and amd is going down (unmounting
     * everything in the process), then we could potentially free it here
     * while a struct continuation still has a reference to it. So when
     * amfs_cont is called, it blows up.
     * We avoid the race by refusing to free any nodes that have
     * pending mounts (defined as having a non-NULL am_alarray).
     */
    notify_child(mp, AMQ_UMNT_OK, 0, 0);	/* do this regardless */
    if (!mp->am_alarray)
      free_map(mp);
  }
}


/*
 * Fork the automounter
 *
 * TODO: Need a better strategy for handling errors
 */
static int
dofork(void)
{
  int pid;

top:
  pid = fork();

  if (pid < 0) {		/* fork error, retry in 1 second */
    sleep(1);
    goto top;
  }
  if (pid == 0) {		/* child process (foreground==false) */
    am_set_mypid();
    foreground = 0;
  } else {			/* parent process, has one more child */
    NumChildren++;
  }

  return pid;
}


int
background(void)
{
  int pid = dofork();

  if (pid == 0) {
    dlog("backgrounded");
    foreground = 0;
  } else
    dlog("forked process %d", pid);
  return pid;
}
