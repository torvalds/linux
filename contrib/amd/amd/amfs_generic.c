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
 * File: am-utils/amd/amfs_generic.c
 *
 */

/*
 * generic functions used by amfs filesystems, ripped out of amfs_auto.c.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>


/****************************************************************************
 *** MACROS                                                               ***
 ****************************************************************************/
#define	IN_PROGRESS(cp) ((cp)->mp->am_al->al_mnt->mf_flags & MFF_MOUNTING)


/****************************************************************************
 *** STRUCTURES                                                           ***
 ****************************************************************************/
/*
 * Mounting a file system may take a significant period of time.  The
 * problem is that if this is done in the main process thread then the
 * entire automounter could be blocked, possibly hanging lots of processes
 * on the system.  Instead we use a continuation scheme to allow mounts to
 * be attempted in a sub-process.  When the sub-process exits we pick up the
 * exit status (by convention a UN*X error number) and continue in a
 * notifier.  The notifier gets handed a data structure and can then
 * determine whether the mount was successful or not.  If not, it updates
 * the data structure and tries again until there are no more ways to try
 * the mount, or some other permanent error occurs.  In the mean time no RPC
 * reply is sent, even after the mount is successful.  We rely on the RPC
 * retry mechanism to resend the lookup request which can then be handled.
 */
struct continuation {
  am_node *mp;			/* Node we are trying to mount */
  int retry;			/* Try again? */
  time_t start;			/* Time we started this mount */
  int callout;			/* Callout identifier */
  am_loc **al;			/* Current location */
};


/****************************************************************************
 *** FORWARD DEFINITIONS                                                  ***
 ****************************************************************************/
static am_node *amfs_lookup_node(am_node *mp, char *fname, int *error_return);
static am_loc *amfs_lookup_one_location(am_node *new_mp, mntfs *mf, char *ivec,
				    char *def_opts, char *pfname);
static am_loc **amfs_lookup_loc(am_node *new_mp, int *error_return);
static void amfs_cont(int rc, int term, opaque_t arg);
static void amfs_retry(int rc, int term, opaque_t arg);
static void free_continuation(struct continuation *cp);
static int amfs_bgmount(struct continuation *cp);
static char *amfs_parse_defaults(am_node *mp, mntfs *mf, char *def_opts);


/****************************************************************************
 *** FUNCTIONS                                                             ***
 ****************************************************************************/
static am_node *
amfs_lookup_node(am_node *mp, char *fname, int *error_return)
{
  am_node *new_mp;
  int error = 0;		/* Error so far */
  int in_progress = 0;		/* # of (un)mount in progress */
  mntfs *mf;
  char *expanded_fname = NULL;

  dlog("in amfs_lookup_node");

  /*
   * If the server is shutting down
   * then don't return information
   * about the mount point.
   */
  if (amd_state == Finishing) {
    if (mp->am_al == NULL || mp->am_al->al_mnt == NULL || mp->am_al->al_mnt->mf_fsflags & FS_DIRECT) {
      dlog("%s mount ignored - going down", fname);
    } else {
      dlog("%s/%s mount ignored - going down", mp->am_path, fname);
    }
    ereturn(ENOENT);
  }

  /*
   * Handle special case of "." and ".."
   */
  if (fname[0] == '.') {
    if (fname[1] == '\0')
      return mp;		/* "." is the current node */
    if (fname[1] == '.' && fname[2] == '\0') {
      if (mp->am_parent) {
	dlog(".. in %s gives %s", mp->am_path, mp->am_parent->am_path);
	return mp->am_parent;	/* ".." is the parent node */
      }
      ereturn(ESTALE);
    }
  }

  /*
   * Check for valid key name.
   * If it is invalid then pretend it doesn't exist.
   */
  if (!valid_key(fname)) {
    plog(XLOG_WARNING, "Key \"%s\" contains a disallowed character", fname);
    ereturn(ENOENT);
  }

  /*
   * Expand key name.
   * expanded_fname is now a private copy.
   */
  expanded_fname = expand_selectors(fname);

  /*
   * Search children of this node
   */
  for (new_mp = mp->am_child; new_mp; new_mp = new_mp->am_osib) {
    if (FSTREQ(new_mp->am_name, expanded_fname)) {
      if (new_mp->am_error) {
	error = new_mp->am_error;
	continue;
      }

      /*
       * If the error code is undefined then it must be
       * in progress.
       */
      mf = new_mp->am_al->al_mnt;
      if (mf->mf_error < 0)
	goto in_progrss;

      /*
       * If there was a previous error with this node
       * then return that error code.
       */
      if (mf->mf_flags & MFF_ERROR) {
	error = mf->mf_error;
	continue;
      }
      if (!(mf->mf_flags & MFF_MOUNTED) || (mf->mf_flags & MFF_UNMOUNTING)) {
      in_progrss:
	/*
	 * If the fs is not mounted or it is unmounting then there
	 * is a background (un)mount in progress.  In this case
	 * we just drop the RPC request (return nil) and
	 * wait for a retry, by which time the (un)mount may
	 * have completed.
	 */
	dlog("ignoring mount of %s in %s -- %smounting in progress, flags %x",
	     expanded_fname, mf->mf_mount,
	     (mf->mf_flags & MFF_UNMOUNTING) ? "un" : "", mf->mf_flags);
	in_progress++;
	if (mf->mf_flags & MFF_UNMOUNTING) {
	  dlog("will remount later");
	  new_mp->am_flags |= AMF_REMOUNT;
	}
	continue;
      }

      /*
       * Otherwise we have a hit: return the current mount point.
       */
      dlog("matched %s in %s", expanded_fname, new_mp->am_path);
      XFREE(expanded_fname);
      return new_mp;
    }
  }

  if (in_progress) {
    dlog("Waiting while %d mount(s) in progress", in_progress);
    XFREE(expanded_fname);
    ereturn(-1);
  }

  /*
   * If an error occurred then return it.
   */
  if (error) {
    dlog("Returning error: %s", strerror(error));
    XFREE(expanded_fname);
    ereturn(error);
  }

  /*
   * If the server is going down then just return,
   * don't try to mount any more file systems
   */
  if ((int) amd_state >= (int) Finishing) {
    dlog("not found - server going down anyway");
    ereturn(ENOENT);
  }

  /*
   * Allocate a new map
   */
  new_mp = get_ap_child(mp, expanded_fname);
  XFREE(expanded_fname);
  if (new_mp == NULL)
    ereturn(ENOSPC);

  *error_return = -1;
  return new_mp;
}



static am_loc *
amfs_lookup_one_location(am_node *new_mp, mntfs *mf, char *ivec,
			char *def_opts, char *pfname)
{
  am_ops *p;
  am_opts *fs_opts;
  am_loc *new_al;
  mntfs *new_mf;
  char *mp_dir = NULL;
#ifdef HAVE_FS_AUTOFS
  int on_autofs = 1;
#endif /* HAVE_FS_AUTOFS */

  /* match the operators */
  /*
   * although we alloc the fs_opts here, the pointer is 'owned' by the am_loc and will
   * be free'd on destruction of the am_loc. If we don't allocate a loc, then we need
   * to free this.
   */
  fs_opts = CALLOC(am_opts);
  p = ops_match(fs_opts, ivec, def_opts, new_mp->am_path,
		pfname, mf->mf_info);
#ifdef HAVE_FS_AUTOFS
  /* XXX: this should be factored out into an autofs-specific function */
  if (new_mp->am_flags & AMF_AUTOFS) {
    /* ignore user-provided fs if we're using autofs */
    if (fs_opts->opt_sublink && fs_opts->opt_sublink[0]) {
      /*
       * For sublinks we need to use a hack with autofs:
       * mount the filesystem on the original opt_fs (which is NOT an
       * autofs mountpoint) and symlink (or lofs-mount) to it from
       * the autofs mountpoint.
       */
      on_autofs = 0;
      mp_dir = fs_opts->opt_fs;
    } else {
      if (p->autofs_fs_flags & FS_ON_AUTOFS) {
	mp_dir = new_mp->am_path;
      } else {
	mp_dir = fs_opts->opt_fs;
	on_autofs = 0;
      }
    }
  } else
#endif /* HAVE_FS_AUTOFS */
    mp_dir = fs_opts->opt_fs;

  /*
   * Find or allocate a filesystem for this node.
   * we search for a matching backend share, since
   * we will construct our own al_loc to handle
   * any customisations for this usage.
   */
  new_mf = find_mntfs(p, fs_opts,
		      mp_dir,
		      fs_opts->fs_mtab,
		      def_opts,
		      fs_opts->opt_opts,
		      fs_opts->opt_remopts);


  /*
   * See whether this is a real filesystem
   */
  p = new_mf->mf_ops;
  if (p == &amfs_error_ops) {
    plog(XLOG_MAP, "Map entry %s for %s did not match", ivec, new_mp->am_path);
    free_mntfs(new_mf);
    free_opts(fs_opts);
    XFREE(fs_opts);
    return NULL;
  }

  dlog("Got a hit with %s", p->fs_type);
  new_al = new_loc();
  free_mntfs(new_al->al_mnt);
  new_al->al_mnt = new_mf;
  new_al->al_fo = fs_opts; /* now the loc is in charge of free'ing this mem */

#ifdef HAVE_FS_AUTOFS
  if (new_mp->am_flags & AMF_AUTOFS && on_autofs) {
    new_mf->mf_flags |= MFF_ON_AUTOFS;
    new_mf->mf_fsflags = new_mf->mf_ops->autofs_fs_flags;
  }
  /*
   * A new filesystem is an autofs filesystems if:
   * 1. it claims it can be one (has the FS_AUTOFS flag)
   * 2. autofs is enabled system-wide
   * 3. either has an autofs parent,
   *    or it is explicitly requested to be autofs.
   */
  if (new_mf->mf_ops->autofs_fs_flags & FS_AUTOFS &&
      amd_use_autofs &&
      ((mf->mf_flags & MFF_IS_AUTOFS) ||
       (new_mf->mf_fo && new_mf->mf_fo->opt_mount_type &&
	STREQ(new_mf->mf_fo->opt_mount_type, "autofs"))))
    new_mf->mf_flags |= MFF_IS_AUTOFS;
#endif /* HAVE_FS_AUTOFS */

  return new_al;
}


static am_loc **
amfs_lookup_loc(am_node *new_mp, int *error_return)
{
  am_node *mp;
  char *info;			/* Mount info - where to get the file system */
  char **ivecs, **cur_ivec;	/* Split version of info */
  int num_ivecs;
  char *orig_def_opts;          /* Original Automount options */
  char *def_opts;	       	/* Automount options */
  int error = 0;		/* Error so far */
  char path_name[MAXPATHLEN];	/* General path name buffer */
  char *pfname;			/* Path for database lookup */
  mntfs* mf;			/* The mntfs for the map of our parent */
  am_loc **al_array;		/* the generated list of locations */
  int count;

  dlog("in amfs_lookup_loc");

  mp = new_mp->am_parent;

  /*
   * If we get here then this is a reference to an,
   * as yet, unknown name so we need to search the mount
   * map for it.
   */
  if (mp->am_pref) {
    if (strlen(mp->am_pref) + strlen(new_mp->am_name) >= sizeof(path_name))
      ereturn(ENAMETOOLONG);
    xsnprintf(path_name, sizeof(path_name), "%s%s", mp->am_pref, new_mp->am_name);
    pfname = path_name;
  } else {
    pfname = new_mp->am_name;
  }

  mf = mp->am_al->al_mnt;

  dlog("will search map info in %s to find %s", mf->mf_info, pfname);
  /*
   * Consult the oracle for some mount information.
   * info is malloc'ed and belongs to this routine.
   * It ends up being free'd in free_continuation().
   *
   * Note that this may return -1 indicating that information
   * is not yet available.
   */
  error = mapc_search((mnt_map *) mf->mf_private, pfname, &info);
  if (error) {
    if (error > 0)
      plog(XLOG_MAP, "No map entry for %s", pfname);
    else
      plog(XLOG_MAP, "Waiting on map entry for %s", pfname);
    ereturn(error);
  }
  dlog("mount info is %s", info);

  /*
   * Split info into an argument vector.
   * The vector is malloc'ed and belongs to
   * this routine.  It is free'd further down.
   *
   * Note: the vector pointers point into info, so don't free it!
   */
  ivecs = strsplit(info, ' ', '\"');

  if (mf->mf_auto)
    def_opts = mf->mf_auto;
  else
    def_opts = "";

  orig_def_opts = amfs_parse_defaults(mp, mf, xstrdup(def_opts));
  def_opts = xstrdup(orig_def_opts);

  /* first build our defaults */
  num_ivecs = 0;
  for (cur_ivec = ivecs; *cur_ivec; cur_ivec++) {
    if (**cur_ivec == '-') {
      /*
       * Pick up new defaults
       */
      char *new_def_opts = str3cat(NULL, def_opts, ";", *cur_ivec + 1);
      XFREE(def_opts);
      def_opts = new_def_opts;
      dlog("Setting def_opts to \"%s\"", def_opts);
      continue;
    } else
      num_ivecs++;
  }

  al_array = calloc(num_ivecs + 1, sizeof(am_loc *));

  /* construct the array of struct locations for this key */
  for (count = 0, cur_ivec = ivecs; *cur_ivec; cur_ivec++) {
    am_loc *new_al;

    if (**cur_ivec == '-') {
      XFREE(def_opts);
      if ((*cur_ivec)[1] == '\0') {
	/*
	 * If we have a single dash '-' than we need to reset the
	 * default options.
	 */
	def_opts = xstrdup(orig_def_opts);
	dlog("Resetting the default options, a single dash '-' was found.");
      } else {
	/* append options to /default options */
	def_opts = str3cat((char *) NULL, orig_def_opts, ";", *cur_ivec + 1);
	dlog("Resetting def_opts to \"%s\"", def_opts);
      }
      continue;
    }

    /*
     * If a loc has already been found, and we find
     * a cut then don't try any more locations.
     *
     * XXX: we do not know when the "/" was added as an equivalent for "||".
     * It's undocumented, it might go away at any time. Caveat emptor.
     */
    if (STREQ(*cur_ivec, "/") || STREQ(*cur_ivec, "||")) {
      if (count > 0) {
	dlog("Cut: not trying any more locations for %s", pfname);
	break;
      }
      continue;
    }

    new_al = amfs_lookup_one_location(new_mp, mf, *cur_ivec, def_opts, pfname);
    if (new_al == NULL)
      continue;
    al_array[count++] = new_al;
  }

  /* We're done with ivecs */
  XFREE(ivecs);
  XFREE(info);
  XFREE(orig_def_opts);
  XFREE(def_opts);
  if (count == 0) {			/* no match */
    XFREE(al_array);
    ereturn(ENOENT);
  }

  return al_array;
}


/*
 * The continuation function.  This is called by
 * the task notifier when a background mount attempt
 * completes.
 */
static void
amfs_cont(int rc, int term, opaque_t arg)
{
  struct continuation *cp = (struct continuation *) arg;
  am_node *mp = cp->mp;
  mntfs *mf = mp->am_al->al_mnt;

  dlog("amfs_cont: '%s'", mp->am_path);

  /*
   * Definitely not trying to mount at the moment
   */
  mf->mf_flags &= ~MFF_MOUNTING;

  /*
   * While we are mounting - try to avoid race conditions
   */
  new_ttl(mp);

  /*
   * Wakeup anything waiting for this mount
   */
  wakeup(get_mntfs_wchan(mf));

  /*
   * Check for termination signal or exit status...
   */
  if (rc || term) {
#ifdef HAVE_FS_AUTOFS
    if (mf->mf_flags & MFF_IS_AUTOFS &&
	!(mf->mf_flags & MFF_MOUNTED))
      autofs_release_fh(mp);
#endif /* HAVE_FS_AUTOFS */

    if (term) {
      /*
       * Not sure what to do for an error code.
       */
      mf->mf_error = EIO;	/* XXX ? */
      mf->mf_flags |= MFF_ERROR;
      plog(XLOG_ERROR, "mount for %s got signal %d", mp->am_path, term);
    } else {
      /*
       * Check for exit status...
       */
#ifdef __linux__
      /*
       * HACK ALERT!
       *
       * On Linux (and maybe not only) it's possible to run
       * an amd which "knows" how to mount certain combinations
       * of nfs_proto/nfs_version which the kernel doesn't grok.
       * So if we got an EINVAL and we have a server that's not
       * using NFSv2/UDP, try again with NFSv2/UDP.
       *
       * Too bad that there is no way to dynamically determine
       * what combinations the _client_ supports, as opposed to
       * what the _server_ supports...
       */
      if (rc == EINVAL &&
	  mf->mf_server &&
	  (mf->mf_server->fs_version != 2 ||
	   !STREQ(mf->mf_server->fs_proto, "udp")))
	mf->mf_flags |= MFF_NFS_SCALEDOWN;
      else
#endif /* __linux__ */
      {
	mf->mf_error = rc;
	mf->mf_flags |= MFF_ERROR;
	errno = rc;		/* XXX */
	if (!STREQ(mp->am_al->al_mnt->mf_ops->fs_type, "linkx"))
	  plog(XLOG_ERROR, "%s: mount (amfs_cont): %m", mp->am_path);
      }
    }

    if (!(mf->mf_flags & MFF_NFS_SCALEDOWN)) {
      /*
       * If we get here then that attempt didn't work, so
       * move the info vector pointer along by one and
       * call the background mount routine again
       */
      amd_stats.d_merr++;
      cp->al++;
    }
    amfs_bgmount(cp);
    if (mp->am_error > 0)
      assign_error_mntfs(mp);
  } else {
    /*
     * The mount worked.
     */
    dlog("Mounting %s returned success", cp->mp->am_path);
    am_mounted(cp->mp);
    free_continuation(cp);
  }

  reschedule_timeout_mp();
}


/*
 * Retry a mount
 */
static void
amfs_retry(int rc, int term, opaque_t arg)
{
  struct continuation *cp = (struct continuation *) arg;
  am_node *mp = cp->mp;
  int error = 0;

  dlog("Commencing retry for mount of %s", mp->am_path);

  new_ttl(mp);

  if ((cp->start + ALLOWED_MOUNT_TIME) < clocktime(NULL)) {
    /*
     * The entire mount has timed out.  Set the error code and skip past all
     * the mntfs's so that amfs_bgmount will not have any more
     * ways to try the mount, thus causing an error.
     */
    plog(XLOG_INFO, "mount of \"%s\" has timed out", mp->am_path);
    error = ETIMEDOUT;
    while (*cp->al)
      cp->al++;
    /* explicitly forbid further retries after timeout */
    cp->retry = FALSE;
  }
  if (error || !IN_PROGRESS(cp))
    error = amfs_bgmount(cp);
  else
    /* Normally it's amfs_bgmount() which frees the continuation. However, if
     * the mount is already in progress and we're in amfs_retry() for another
     * node we don't try mounting the filesystem once again. Still, we have
     * to free the continuation as we won't get called again and thus would
     * leak the continuation structure and our am_loc references.
     */
    free_continuation(cp);

  reschedule_timeout_mp();
}


/*
 * Discard an old continuation
 */
static void
free_continuation(struct continuation *cp)
{
  am_loc **alp;

  dlog("free_continuation");
  if (cp->callout)
    untimeout(cp->callout);
  /*
   * we must free the mntfs's in the list.
   * so free all of them if there was an error,
   */
  for (alp = cp->mp->am_alarray; *alp; alp++) {
    free_loc(*alp);
  }
  XFREE(cp->mp->am_alarray);
  cp->mp->am_alarray = 0;
  XFREE(cp);
}


/*
 * Pick a file system to try mounting and
 * do that in the background if necessary
 *
For each location:
	discard previous mount location if required
	fetch next mount location
	if the filesystem failed to be mounted then
		this_error = error from filesystem
		goto failed
	if the filesystem is mounting or unmounting then
		goto retry;
	if the fileserver is down then
		this_error = EIO
		continue;
	if the filesystem is already mounted
		break
	fi

	this_error = initialize mount point

	if no error on this mount and mount is delayed then
		this_error = -1
	fi
	if this_error < 0 then
		retry = true
	fi
	if no error on this mount then
		if mount in background then
			run mount in background
			return -1
		else
			this_error = mount in foreground
		fi
	fi
	if an error occurred on this mount then
		update stats
		save error in mount point
	fi
endfor
 */
static int
amfs_bgmount(struct continuation *cp)
{
  am_node *mp = cp->mp;
  am_loc *loc;
  mntfs *mf;
  int this_error = -1;		/* Per-mount error */
  int hard_error = -1;		/* Cumulative per-node error */

  if (mp->am_al)
    free_loc(mp->am_al);

  /*
   * Try to mount each location.
   * At the end:
   * hard_error == 0 indicates something was mounted.
   * hard_error > 0 indicates everything failed with a hard error
   * hard_error < 0 indicates nothing could be mounted now
   */
  for (mp->am_al = *cp->al; *cp->al; cp->al++, mp->am_al = *cp->al) {
    am_ops *p;

    loc = dup_loc(mp->am_al);
    mf = loc->al_mnt;
    p = mf->mf_ops;

    if (hard_error < 0)
      hard_error = this_error;
    this_error = 0;

    if (mf->mf_error > 0) {
      this_error = mf->mf_error;
      goto failed;
    }

    if (mf->mf_flags & (MFF_MOUNTING | MFF_UNMOUNTING)) {
      /*
       * Still mounting - retry later
       */
      dlog("mount of \"%s\" already pending", mf->mf_info);
      goto retry;
    }

    if (FSRV_ISDOWN(mf->mf_server)) {
      /*
       * Would just mount from the same place
       * as a hung mount - so give up
       */
      dlog("%s is already hung - giving up", mf->mf_server->fs_host);
      this_error = EIO;
      goto failed;
    }

    XFREE(mp->am_link);
    mp->am_link = NULL;

    if (loc->al_fo && loc->al_fo->opt_sublink && loc->al_fo->opt_sublink[0])
      mp->am_link = xstrdup(loc->al_fo->opt_sublink);

    /*
     * Will usually need to play around with the mount nodes
     * file attribute structure.  This must be done here.
     * Try and get things initialized, even if the fileserver
     * is not known to be up.  In the common case this will
     * progress things faster.
     */

    /*
     * Fill in attribute fields.
     */
    if (mf->mf_fsflags & FS_DIRECTORY)
      mk_fattr(&mp->am_fattr, NFDIR);
    else
      mk_fattr(&mp->am_fattr, NFLNK);

    if (mf->mf_flags & MFF_MOUNTED) {
      dlog("duplicate mount of \"%s\" ...", mf->mf_info);
      /*
       * Skip initial processing of the mountpoint if already mounted.
       * This could happen if we have multiple sublinks into the same f/s,
       * or if we are restarting an already-mounted filesystem.
       */
      goto already_mounted;
    }

    if (mf->mf_fo && mf->mf_fo->fs_mtab) {
      plog(XLOG_MAP, "Trying mount of %s on %s fstype %s mount_type %s",
	   mf->mf_fo->fs_mtab, mf->mf_mount, p->fs_type,
	   mp->am_flags & AMF_AUTOFS ? "autofs" : "non-autofs");
    }

    if (p->fs_init && !(mf->mf_flags & MFF_RESTART))
      this_error = p->fs_init(mf);

    if (this_error > 0)
      goto failed;
    if (this_error < 0)
      goto retry;

    if (loc->al_fo && loc->al_fo->opt_delay) {
      /*
       * If there is a delay timer on the location
       * then don't try to mount if the timer
       * has not expired.
       */
      int i = atoi(loc->al_fo->opt_delay);
      time_t now = clocktime(NULL);
      if (i > 0 && now < (cp->start + i)) {
	dlog("Mount of %s delayed by %lds", mf->mf_mount, (long) (i - now + cp->start));
	goto retry;
      }
    }

    /*
     * If the directory is not yet made and it needs to be made, then make it!
     */
    if (!(mf->mf_flags & MFF_MKMNT) && mf->mf_fsflags & FS_MKMNT) {
      plog(XLOG_INFO, "creating mountpoint directory '%s'", mf->mf_mount);
      this_error = mkdirs(mf->mf_mount, 0555);
      if (this_error) {
	plog(XLOG_ERROR, "mkdirs failed: %s", strerror(this_error));
	goto failed;
      }
      mf->mf_flags |= MFF_MKMNT;
    }

#ifdef HAVE_FS_AUTOFS
    if (mf->mf_flags & MFF_IS_AUTOFS)
      if ((this_error = autofs_get_fh(mp)))
	goto failed;
#endif /* HAVE_FS_AUTOFS */

  already_mounted:
    mf->mf_flags |= MFF_MOUNTING;
    if (mf->mf_fsflags & FS_MBACKGROUND) {
      dlog("backgrounding mount of \"%s\"", mf->mf_mount);
      if (cp->callout) {
	untimeout(cp->callout);
	cp->callout = 0;
      }

      /* actually run the task, backgrounding as necessary */
      run_task(mount_node, (opaque_t) mp, amfs_cont, (opaque_t) cp);
      return -1;
    } else {
      dlog("foreground mount of \"%s\" ...", mf->mf_mount);
      this_error = mount_node((opaque_t) mp);
    }

    mf->mf_flags &= ~MFF_MOUNTING;
    if (this_error > 0)
      goto failed;
    if (this_error == 0) {
      am_mounted(mp);
      break;					/* Success */
    }

  retry:
    if (!cp->retry)
      continue;
    dlog("will retry ...\n");

    /*
     * Arrange that amfs_bgmount is called
     * after anything else happens.
     */
    dlog("Arranging to retry mount of %s", mp->am_path);
    sched_task(amfs_retry, (opaque_t) cp, get_mntfs_wchan(mf));
    if (cp->callout)
      untimeout(cp->callout);
    cp->callout = timeout(RETRY_INTERVAL, wakeup,
			  (opaque_t) get_mntfs_wchan(mf));

    mp->am_ttl = clocktime(NULL) + RETRY_INTERVAL;

    /*
     * Not done yet - so don't return anything
     */
    return -1;

  failed:
    if (!FSRV_ISDOWN(mf->mf_server)) {
      /* mark the mount as failed unless the server is down */
      amd_stats.d_merr++;
      mf->mf_error = this_error;
      mf->mf_flags |= MFF_ERROR;
#ifdef HAVE_FS_AUTOFS
      if (mp->am_autofs_fh)
	autofs_release_fh(mp);
#endif /* HAVE_FS_AUTOFS */
      if (mf->mf_flags & MFF_MKMNT) {
	rmdirs(mf->mf_mount);
	mf->mf_flags &= ~MFF_MKMNT;
      }
    }
    /*
     * Wakeup anything waiting for this mount
     */
    wakeup(get_mntfs_wchan(mf));
    free_loc(loc);
    /* continue */
  }

  /*
   * If we get here, then either the mount succeeded or
   * there is no more mount information available.
   */
  if (this_error) {
    if (mp->am_al)
      free_loc(mp->am_al);
    mp->am_al = loc = new_loc();
    mf = loc->al_mnt;

#ifdef HAVE_FS_AUTOFS
    if (mp->am_flags & AMF_AUTOFS)
      autofs_mount_failed(mp);
    else
#endif /* HAVE_FS_AUTOFS */
      nfs_quick_reply(mp, this_error);

    if (hard_error <= 0)
      hard_error = this_error;
    if (hard_error < 0)
      hard_error = ETIMEDOUT;

    /*
     * Set a small(ish) timeout on an error node if
     * the error was not a time out.
     */
    switch (hard_error) {
    case ETIMEDOUT:
    case EWOULDBLOCK:
    case EIO:
      mp->am_timeo = 17;
      break;
    default:
      mp->am_timeo = 5;
      break;
    }
    new_ttl(mp);
  } else {
    mf = loc->al_mnt;
    /*
     * Wakeup anything waiting for this mount
     */
    wakeup(get_mntfs_wchan(mf));
    hard_error = 0;
  }

  /*
   * Make sure that the error value in the mntfs has a
   * reasonable value.
   */
  if (mf->mf_error < 0) {
    mf->mf_error = hard_error;
    if (hard_error)
      mf->mf_flags |= MFF_ERROR;
  }

  /*
   * In any case we don't need the continuation any more
   */
  free_continuation(cp);

  return hard_error;
}


static char *
amfs_parse_defaults(am_node *mp, mntfs *mf, char *def_opts)
{
  char *dflts;
  char *dfl;
  char **rvec = NULL;
  struct mnt_map *mm = (mnt_map *) mf->mf_private;

  dlog("determining /defaults entry value");

  /*
   * Find out if amd.conf overrode any map-specific /defaults.
   */
  if (mm->cfm && mm->cfm->cfm_defaults) {
    dlog("map %s map_defaults override: %s", mf->mf_mount, mm->cfm->cfm_defaults);
    dflts = xstrdup(mm->cfm->cfm_defaults);
  } else if (mapc_search(mm, "/defaults", &dflts) == 0) {
    dlog("/defaults gave %s", dflts);
  } else {
    return def_opts;		/* if nothing found */
  }

  /* trim leading '-' in case thee's one */
  if (*dflts == '-')
    dfl = dflts + 1;
  else
    dfl = dflts;

  /*
   * Chop the defaults up
   */
  rvec = strsplit(dfl, ' ', '\"');

  if (gopt.flags & CFM_SELECTORS_IN_DEFAULTS) {
    /*
     * Pick whichever first entry matched the list of selectors.
     * Strip the selectors from the string, and assign to dfl the
     * rest of the string.
     */
    if (rvec) {
      am_opts ap;
      am_ops *pt;
      char **sp = rvec;
      while (*sp) {		/* loop until you find something, if any */
	memset((char *) &ap, 0, sizeof(am_opts));
	/*
	 * This next routine cause many spurious "expansion of ... is"
	 * messages, which are ignored, b/c all we need out of this
	 * routine is to match selectors.  These spurious messages may
	 * be wrong, esp. if they try to expand ${key} b/c it will
	 * get expanded to "/defaults"
	 */
	pt = ops_match(&ap, *sp, "", mp->am_path, "/defaults",
		       mp->am_parent->am_al->al_mnt->mf_info);
	free_opts(&ap);	/* don't leak */
	if (pt == &amfs_error_ops) {
	  plog(XLOG_MAP, "did not match defaults for \"%s\"", *sp);
	} else {
	  dfl = strip_selectors(*sp, "/defaults");
	  plog(XLOG_MAP, "matched default selectors \"%s\"", dfl);
	  break;
	}
	++sp;
      }
    }
  } else {			/* not selectors_in_defaults */
    /*
     * Extract first value
     */
    dfl = rvec[0];
  }

  /*
   * If there were any values at all...
   */
  if (dfl) {
    /*
     * Log error if there were other values
     */
    if (!(gopt.flags & CFM_SELECTORS_IN_DEFAULTS) && rvec[1]) {
      dlog("/defaults chopped into %s", dfl);
      plog(XLOG_USER, "More than a single value for /defaults in %s", mf->mf_info);
    }

    /*
     * Prepend to existing defaults if they exist,
     * otherwise just use these defaults.
     */
    if (*def_opts && *dfl) {
      size_t l = strlen(def_opts) + strlen(dfl) + 2;
      char *nopts = (char *) xmalloc(l);
      xsnprintf(nopts, l, "%s;%s", dfl, def_opts);
      XFREE(def_opts);
      def_opts = nopts;
    } else if (*dfl) {
      def_opts = strealloc(def_opts, dfl);
    }
  }

  XFREE(dflts);

  /* don't need info vector any more */
  if (rvec)
    XFREE(rvec);

  return def_opts;
}


am_node *
amfs_generic_mount_child(am_node *new_mp, int *error_return)
{
  int error;
  struct continuation *cp;	/* Continuation structure if need to mount */

  dlog("in amfs_generic_mount_child");

  *error_return = error = 0;	/* Error so far */

  /* we have an errorfs attached to the am_node, free it */
  if (new_mp->am_al)
    free_loc(new_mp->am_al);
  new_mp->am_al = NULL;

  /*
   * Construct a continuation
   */
  cp = ALLOC(struct continuation);
  cp->callout = 0;
  cp->mp = new_mp;
  cp->retry = TRUE;
  cp->start = clocktime(NULL);
  cp->al = new_mp->am_alarray;

  /*
   * Try and mount the file system.  If this succeeds immediately (possible
   * for a ufs file system) then return the attributes, otherwise just
   * return an error.
   */
  error = amfs_bgmount(cp);
  reschedule_timeout_mp();
  if (!error)
    return new_mp;

  /*
   * Code for quick reply.  If current_transp is set, then it's the
   * transp that's been passed down from nfs_dispatcher() or from
   * autofs_program_[123]().
   * If new_mp->am_transp is not already set, set it by copying in
   * current_transp.  Once am_transp is set, nfs_quick_reply() and
   * autofs_mount_succeeded() can use it to send a reply to the
   * client that requested this mount.
   */
  if (current_transp && !new_mp->am_transp) {
    dlog("Saving RPC transport for %s", new_mp->am_path);
    new_mp->am_transp = (SVCXPRT *) xmalloc(sizeof(SVCXPRT));
    *(new_mp->am_transp) = *current_transp;
  }
  if (error && new_mp->am_al && new_mp->am_al->al_mnt &&
      (new_mp->am_al->al_mnt->mf_ops == &amfs_error_ops))
    new_mp->am_error = error;

  if (new_mp->am_error > 0)
    assign_error_mntfs(new_mp);

  ereturn(error);
}


/*
 * Automount interface to RPC lookup routine
 * Find the corresponding entry and return
 * the file handle for it.
 */
am_node *
amfs_generic_lookup_child(am_node *mp, char *fname, int *error_return, int op)
{
  am_node *new_mp;
  am_loc **al_array;
  int mp_error;

  dlog("in amfs_generic_lookup_child");

  *error_return = 0;
  new_mp = amfs_lookup_node(mp, fname, error_return);

  /* return if we got an error */
  if (!new_mp || *error_return > 0)
    return new_mp;

  /* also return if it's already mounted and known to be up */
  if (*error_return == 0 && FSRV_ISUP(new_mp->am_al->al_mnt->mf_server))
    return new_mp;

  switch (op) {
  case VLOOK_DELETE:
    /*
     * If doing a delete then don't create again!
     */
    ereturn(ENOENT);
  case VLOOK_LOOKUP:
    return new_mp;
  }

  /* save error_return */
  mp_error = *error_return;

  al_array = amfs_lookup_loc(new_mp, error_return);
  if (!al_array) {
    new_mp->am_error = new_mp->am_al->al_mnt->mf_error = *error_return;
    free_map(new_mp);
    return NULL;
  }

  /* store the array inside the am_node */
  new_mp->am_alarray = al_array;

  /*
   * Note: while it might seem like a good idea to prioritize
   * the list of mntfs's we got here, it probably isn't.
   * It would ignore the ordering of entries specified by the user,
   * which is counterintuitive and confusing.
   */
  return new_mp;
}


void
amfs_generic_mounted(mntfs *mf)
{
  amfs_mkcacheref(mf);
}


/*
 * Unmount an automount sub-node
 */
int
amfs_generic_umount(am_node *mp, mntfs *mf)
{
  int error = 0;

#ifdef HAVE_FS_AUTOFS
  int unmount_flags = (mf->mf_flags & MFF_ON_AUTOFS) ? AMU_UMOUNT_AUTOFS : 0;
  if (mf->mf_flags & MFF_IS_AUTOFS)
    error = UMOUNT_FS(mp->am_path, mnttab_file_name, unmount_flags);
#endif /* HAVE_FS_AUTOFS */

  return error;
}


char *
amfs_generic_match(am_opts *fo)
{
  char *p;

  if (!fo->opt_rfs) {
    plog(XLOG_USER, "amfs_generic_match: no mount point named (rfs:=)");
    return 0;
  }
  if (!fo->opt_fs) {
    plog(XLOG_USER, "amfs_generic_match: no map named (fs:=)");
    return 0;
  }

  /*
   * Swap round fs:= and rfs:= options
   * ... historical (jsp)
   */
  p = fo->opt_rfs;
  fo->opt_rfs = fo->opt_fs;
  fo->opt_fs = p;

  /*
   * mtab entry turns out to be the name of the mount map
   */
  return xstrdup(fo->opt_rfs ? fo->opt_rfs : ".");
}
