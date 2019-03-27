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
 * File: am-utils/amd/amfs_host.c
 *
 */

/*
 * NFS host file system.
 * Mounts all exported filesystems from a given host.
 * This has now degenerated into a mess but will not
 * be rewritten.  Amd 6 will support the abstractions
 * needed to make this work correctly.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

static char *amfs_host_match(am_opts *fo);
static int amfs_host_init(mntfs *mf);
static int amfs_host_mount(am_node *am, mntfs *mf);
static int amfs_host_umount(am_node *am, mntfs *mf);
static void amfs_host_umounted(mntfs *mf);

/*
 * Ops structure
 */
am_ops amfs_host_ops =
{
  "host",
  amfs_host_match,
  amfs_host_init,
  amfs_host_mount,
  amfs_host_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* amfs_host_readlink */
  0,				/* amfs_host_mounted */
  amfs_host_umounted,
  find_nfs_srvr,
  0,				/* amfs_host_get_wchan */
  FS_MKMNT | FS_BACKGROUND | FS_AMQINFO,
#ifdef HAVE_FS_AUTOFS
  AUTOFS_HOST_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


/*
 * Determine the mount point:
 *
 * The next change we put in to better handle PCs.  This is a bit
 * disgusting, so you'd better sit down.  We change the make_mntpt function
 * to look for exported file systems without a leading '/'.  If they don't
 * have a leading '/', we add one.  If the export is 'a:' through 'z:'
 * (without a leading slash), we change it to 'a%' (or b% or z%).  This
 * allows the entire PC disk to be mounted.
 */
static void
make_mntpt(char *mntpt, size_t l, const exports ex, const char *mf_mount)
{
  if (ex->ex_dir[0] == '/') {
    if (ex->ex_dir[1] == 0)
      xstrlcpy(mntpt, mf_mount, l);
    else
      xsnprintf(mntpt, l, "%s%s", mf_mount, ex->ex_dir);
  } else if (ex->ex_dir[0] >= 'a' &&
	     ex->ex_dir[0] <= 'z' &&
	     ex->ex_dir[1] == ':' &&
	     ex->ex_dir[2] == '/' &&
	     ex->ex_dir[3] == 0)
    xsnprintf(mntpt, l, "%s/%c%%", mf_mount, ex->ex_dir[0]);
  else
    xsnprintf(mntpt, l, "%s/%s", mf_mount, ex->ex_dir);
}


/*
 * Execute needs the same as NFS plus a helper command
 */
static char *
amfs_host_match(am_opts *fo)
{
  extern am_ops nfs_ops;

  /*
   * Make sure rfs is specified to keep nfs_match happy...
   */
  if (!fo->opt_rfs)
    fo->opt_rfs = "/";

  return (*nfs_ops.fs_match) (fo);
}


static int
amfs_host_init(mntfs *mf)
{
  u_short mountd_port;

  if (strchr(mf->mf_info, ':') == 0)
    return ENOENT;

  /*
   * This is primarily to schedule a wakeup so that as soon
   * as our fileserver is ready, we can continue setting up
   * the host filesystem.  If we don't do this, the standard
   * amfs_auto code will set up a fileserver structure, but it will
   * have to wait for another nfs request from the client to come
   * in before finishing.  Our way is faster since we don't have
   * to wait for the client to resend its request (which could
   * take a second or two).
   */
  /*
   * First, we find the fileserver for this mntfs and then call
   * get_mountd_port with our mntfs passed as the wait channel.
   * get_mountd_port will check some things and then schedule
   * it so that when the fileserver is ready, a wakeup is done
   * on this mntfs.   amfs_cont() is already sleeping on this mntfs
   * so as soon as that wakeup happens amfs_cont() is called and
   * this mount is retried.
   */
  if (mf->mf_server)
    /*
     * We don't really care if there's an error returned.
     * Since this is just to help speed things along, the
     * error will get handled properly elsewhere.
     */
    get_mountd_port(mf->mf_server, &mountd_port, get_mntfs_wchan(mf));

  return 0;
}


static int
do_mount(am_nfs_handle_t *fhp, char *mntdir, char *fs_name, mntfs *mf)
{
  struct stat stb;

  dlog("amfs_host: mounting fs %s on %s\n", fs_name, mntdir);

  (void) mkdirs(mntdir, 0555);
  if (stat(mntdir, &stb) < 0 || (stb.st_mode & S_IFMT) != S_IFDIR) {
    plog(XLOG_ERROR, "No mount point for %s - skipping", mntdir);
    return ENOENT;
  }

  return mount_nfs_fh(fhp, mntdir, fs_name, mf);
}


static int
sortfun(const voidp x, const voidp y)
{
  exports *a = (exports *) x;
  exports *b = (exports *) y;

  return strcmp((*a)->ex_dir, (*b)->ex_dir);
}


/*
 * Get filehandle
 */
static int
fetch_fhandle(CLIENT *client, char *dir, am_nfs_handle_t *fhp, u_long nfs_version)
{
  struct timeval tv;
  enum clnt_stat clnt_stat;
  struct fhstatus res;
#ifdef HAVE_FS_NFS3
  struct am_mountres3 res3;
#endif /* HAVE_FS_NFS3 */

  /*
   * Pick a number, any number...
   */
  tv.tv_sec = 20;
  tv.tv_usec = 0;

  dlog("Fetching fhandle for %s", dir);

  /*
   * Call the mount daemon on the remote host to
   * get the filehandle.  Use NFS version specific call.
   */

  plog(XLOG_INFO, "fetch_fhandle: NFS version %d", (int) nfs_version);
#ifdef HAVE_FS_NFS3
  if (nfs_version == NFS_VERSION3
#ifdef HAVE_FS_NFS4
#ifndef NO_FALLBACK
      || nfs_version == NFS_VERSION4
#endif /* NO_FALLBACK */
#endif /* HAVE_FS_NFS4 */
    ) {

    memset((char *) &res3, 0, sizeof(res3));
    clnt_stat = clnt_call(client,
			  MOUNTPROC_MNT,
			  (XDRPROC_T_TYPE) xdr_dirpath,
			  (SVC_IN_ARG_TYPE) &dir,
			  (XDRPROC_T_TYPE) xdr_am_mountres3,
			  (SVC_IN_ARG_TYPE) &res3,
			  tv);
    if (clnt_stat != RPC_SUCCESS) {
      plog(XLOG_ERROR, "mountd rpc failed: %s", clnt_sperrno(clnt_stat));
      return EIO;
    }
    /* Check the status of the filehandle */
    if ((errno = res3.fhs_status)) {
      dlog("fhandle fetch for mount version 3 failed: %m");
      return errno;
    }
    memset((voidp) &fhp->v3, 0, sizeof(am_nfs_fh3));
    fhp->v3.am_fh3_length = res3.mountres3_u.mountinfo.fhandle.fhandle3_len;
    memmove(fhp->v3.am_fh3_data,
	    res3.mountres3_u.mountinfo.fhandle.fhandle3_val,
	    fhp->v3.am_fh3_length);
  } else {			/* not NFS_VERSION3 mount */
#endif /* HAVE_FS_NFS3 */
    clnt_stat = clnt_call(client,
			  MOUNTPROC_MNT,
			  (XDRPROC_T_TYPE) xdr_dirpath,
			  (SVC_IN_ARG_TYPE) &dir,
			  (XDRPROC_T_TYPE) xdr_fhstatus,
			  (SVC_IN_ARG_TYPE) &res,
			  tv);
    if (clnt_stat != RPC_SUCCESS) {
      plog(XLOG_ERROR, "mountd rpc failed: %s", clnt_sperrno(clnt_stat));
      return EIO;
    }
    /* Check status of filehandle */
    if (res.fhs_status) {
      errno = res.fhs_status;
      dlog("fhandle fetch for mount version 1 failed: %m");
      return errno;
    }
    memmove(&fhp->v2, &res.fhs_fh, NFS_FHSIZE);
#ifdef HAVE_FS_NFS3
  } /* end of "if (nfs_version == NFS_VERSION3)" statement */
#endif /* HAVE_FS_NFS3 */

  /* all is well */
  return 0;
}


/*
 * Scan mount table to see if something already mounted
 */
static int
already_mounted(mntlist *mlist, char *dir)
{
  mntlist *ml;

  for (ml = mlist; ml; ml = ml->mnext)
    if (STREQ(ml->mnt->mnt_dir, dir))
      return 1;
  return 0;
}


static int
amfs_host_mount(am_node *am, mntfs *mf)
{
  struct timeval tv2;
  CLIENT *client;
  enum clnt_stat clnt_stat;
  int n_export;
  int j, k;
  exports exlist = 0, ex;
  exports *ep = NULL;
  am_nfs_handle_t *fp = NULL;
  char *host;
  int error = 0;
  struct sockaddr_in sin;
  int sock = RPC_ANYSOCK;
  int ok = FALSE;
  mntlist *mlist;
  char fs_name[MAXPATHLEN], *rfs_dir;
  char mntpt[MAXPATHLEN];
  struct timeval tv;
  u_long mnt_version;

  /*
   * WebNFS servers don't necessarily run mountd.
   */
  if (mf->mf_flags & MFF_WEBNFS) {
    plog(XLOG_ERROR, "amfs_host_mount: cannot support WebNFS");
    return EIO;
  }

  /*
   * Read the mount list
   */
  mlist = read_mtab(mf->mf_mount, mnttab_file_name);

#ifdef MOUNT_TABLE_ON_FILE
  /*
   * Unlock the mount list
   */
  unlock_mntlist();
#endif /* MOUNT_TABLE_ON_FILE */

  /*
   * Take a copy of the server hostname, address, and nfs version
   * to mount version conversion.
   */
  host = mf->mf_server->fs_host;
  sin = *mf->mf_server->fs_ip;
  plog(XLOG_INFO, "amfs_host_mount: NFS version %d", (int) mf->mf_server->fs_version);
#ifdef HAVE_FS_NFS3
  if (mf->mf_server->fs_version == NFS_VERSION3)
    mnt_version = AM_MOUNTVERS3;
  else
#endif /* HAVE_FS_NFS3 */
    mnt_version = MOUNTVERS;

  /*
   * The original 10 second per try timeout is WAY too large, especially
   * if we're only waiting 10 or 20 seconds max for the response.
   * That would mean we'd try only once in 10 seconds, and we could
   * lose the transmit or receive packet, and never try again.
   * A 2-second per try timeout here is much more reasonable.
   * 09/28/92 Mike Mitchell, mcm@unx.sas.com
   */
  tv.tv_sec = 2;
  tv.tv_usec = 0;

  /*
   * Create a client attached to mountd
   */
  client = get_mount_client(host, &sin, &tv, &sock, mnt_version);
  if (client == NULL) {
#ifdef HAVE_CLNT_SPCREATEERROR
    plog(XLOG_ERROR, "get_mount_client failed for %s: %s",
	 host, clnt_spcreateerror(""));
#else /* not HAVE_CLNT_SPCREATEERROR */
    plog(XLOG_ERROR, "get_mount_client failed for %s", host);
#endif /* not HAVE_CLNT_SPCREATEERROR */
    error = EIO;
    goto out;
  }
  if (!nfs_auth) {
    error = make_nfs_auth();
    if (error)
      goto out;
  }
  client->cl_auth = nfs_auth;

  dlog("Fetching export list from %s", host);

  /*
   * Fetch the export list
   */
  tv2.tv_sec = 10;
  tv2.tv_usec = 0;
  clnt_stat = clnt_call(client,
			MOUNTPROC_EXPORT,
			(XDRPROC_T_TYPE) xdr_void,
			0,
			(XDRPROC_T_TYPE) xdr_exports,
			(SVC_IN_ARG_TYPE) & exlist,
			tv2);
  if (clnt_stat != RPC_SUCCESS) {
    const char *msg = clnt_sperrno(clnt_stat);
    plog(XLOG_ERROR, "host_mount rpc failed: %s", msg);
    /* clnt_perror(client, "rpc"); */
    error = EIO;
    goto out;
  }

  /*
   * Figure out how many exports were returned
   */
  for (n_export = 0, ex = exlist; ex; ex = ex->ex_next) {
    n_export++;
  }

  /*
   * Allocate an array of pointers into the list
   * so that they can be sorted.  If the filesystem
   * is already mounted then ignore it.
   */
  ep = (exports *) xmalloc(n_export * sizeof(exports));
  for (j = 0, ex = exlist; ex; ex = ex->ex_next) {
    make_mntpt(mntpt, sizeof(mntpt), ex, mf->mf_mount);
    if (already_mounted(mlist, mntpt))
      /* we have at least one mounted f/s, so don't fail the mount */
      ok = TRUE;
    else
      ep[j++] = ex;
  }
  n_export = j;

  /*
   * Sort into order.
   * This way the mounts are done in order down the tree,
   * instead of any random order returned by the mount
   * daemon (the protocol doesn't specify...).
   */
  qsort(ep, n_export, sizeof(exports), sortfun);

  /*
   * Allocate an array of filehandles
   */
  fp = (am_nfs_handle_t *) xmalloc(n_export * sizeof(am_nfs_handle_t));

  /*
   * Try to obtain filehandles for each directory.
   * If a fetch fails then just zero out the array
   * reference but discard the error.
   */
  for (j = k = 0; j < n_export; j++) {
    /* Check and avoid a duplicated export entry */
    if (j > k && ep[k] && STREQ(ep[j]->ex_dir, ep[k]->ex_dir)) {
      dlog("avoiding dup fhandle requested for %s", ep[j]->ex_dir);
      ep[j] = NULL;
    } else {
      k = j;
      error = fetch_fhandle(client, ep[j]->ex_dir, &fp[j],
			    mf->mf_server->fs_version);
      if (error)
	ep[j] = NULL;
    }
  }

  /*
   * Mount each filesystem for which we have a filehandle.
   * If any of the mounts succeed then mark "ok" and return
   * error code 0 at the end.  If they all fail then return
   * the last error code.
   */
  xstrlcpy(fs_name, mf->mf_info, sizeof(fs_name));
  if ((rfs_dir = strchr(fs_name, ':')) == (char *) NULL) {
    plog(XLOG_FATAL, "amfs_host_mount: mf_info has no colon");
    error = EINVAL;
    goto out;
  }
  ++rfs_dir;
  for (j = 0; j < n_export; j++) {
    ex = ep[j];
    if (ex) {
      /*
       * Note: the sizeof space left in rfs_dir is what's left in fs_name
       * after strchr() above returned a pointer _inside_ fs_name.  The
       * calculation below also takes into account that rfs_dir was
       * incremented by the ++ above.
       */
      xstrlcpy(rfs_dir, ex->ex_dir, sizeof(fs_name) - (rfs_dir - fs_name));
      make_mntpt(mntpt, sizeof(mntpt), ex, mf->mf_mount);
      if (do_mount(&fp[j], mntpt, fs_name, mf) == 0)
	ok = TRUE;
    }
  }

  /*
   * Clean up and exit
   */
out:
  discard_mntlist(mlist);
  XFREE(ep);
  XFREE(fp);
  if (sock != RPC_ANYSOCK)
    (void) amu_close(sock);
  if (client)
    clnt_destroy(client);
  if (exlist)
    xdr_pri_free((XDRPROC_T_TYPE) xdr_exports, (caddr_t) &exlist);
  if (ok)
    return 0;
  return error;
}


/*
 * Return true if pref is a directory prefix of dir.
 *
 * XXX TODO:
 * Does not work if pref is "/".
 */
static int
directory_prefix(char *pref, char *dir)
{
  int len = strlen(pref);

  if (!NSTREQ(pref, dir, len))
    return FALSE;
  if (dir[len] == '/' || dir[len] == '\0')
    return TRUE;
  return FALSE;
}


/*
 * Unmount a mount tree
 */
static int
amfs_host_umount(am_node *am, mntfs *mf)
{
  mntlist *ml, *mprev;
  int unmount_flags = (mf->mf_flags & MFF_ON_AUTOFS) ? AMU_UMOUNT_AUTOFS : 0;
  int xerror = 0;

  /*
   * Read the mount list
   */
  mntlist *mlist = read_mtab(mf->mf_mount, mnttab_file_name);

#ifdef MOUNT_TABLE_ON_FILE
  /*
   * Unlock the mount list
   */
  unlock_mntlist();
#endif /* MOUNT_TABLE_ON_FILE */

  /*
   * Reverse list...
   */
  ml = mlist;
  mprev = NULL;
  while (ml) {
    mntlist *ml2 = ml->mnext;
    ml->mnext = mprev;
    mprev = ml;
    ml = ml2;
  }
  mlist = mprev;

  /*
   * Unmount all filesystems...
   */
  for (ml = mlist; ml && !xerror; ml = ml->mnext) {
    char *dir = ml->mnt->mnt_dir;
    if (directory_prefix(mf->mf_mount, dir)) {
      int error;
      dlog("amfs_host: unmounts %s", dir);
      /*
       * Unmount "dir"
       */
      error = UMOUNT_FS(dir, mnttab_file_name, unmount_flags);
      /*
       * Keep track of errors
       */
      if (error) {
	/*
	 * If we have not already set xerror and error is not ENOENT,
	 * then set xerror equal to error and log it.
	 * 'xerror' is the return value for this function.
	 *
	 * We do not want to pass ENOENT as an error because if the
	 * directory does not exists our work is done anyway.
	 */
	if (!xerror && error != ENOENT)
	  xerror = error;
	if (error != EBUSY) {
	  errno = error;
	  plog(XLOG_ERROR, "Tree unmount of %s failed: %m", ml->mnt->mnt_dir);
	}
      } else {
	(void) rmdirs(dir);
      }
    }
  }

  /*
   * Throw away mount list
   */
  discard_mntlist(mlist);

  /*
   * Try to remount, except when we are shutting down.
   */
  if (xerror && amd_state != Finishing) {
    xerror = amfs_host_mount(am, mf);
    if (!xerror) {
      /*
       * Don't log this - it's usually too verbose
       plog(XLOG_INFO, "Remounted host %s", mf->mf_info);
       */
      xerror = EBUSY;
    }
  }
  return xerror;
}


/*
 * Tell mountd we're done.
 * This is not quite right, because we may still
 * have other filesystems mounted, but the existing
 * mountd protocol is badly broken anyway.
 */
static void
amfs_host_umounted(mntfs *mf)
{
  char *host;
  CLIENT *client;
  enum clnt_stat clnt_stat;
  struct sockaddr_in sin;
  int sock = RPC_ANYSOCK;
  struct timeval tv;
  u_long mnt_version;

  if (mf->mf_error || mf->mf_refc > 1 || !mf->mf_server)
    return;

  /*
   * WebNFS servers shouldn't ever get here.
   */
  if (mf->mf_flags & MFF_WEBNFS) {
    plog(XLOG_ERROR, "amfs_host_umounted: cannot support WebNFS");
    return;
  }

  /*
   * Take a copy of the server hostname, address, and NFS version
   * to mount version conversion.
   */
  host = mf->mf_server->fs_host;
  sin = *mf->mf_server->fs_ip;
  plog(XLOG_INFO, "amfs_host_umounted: NFS version %d", (int) mf->mf_server->fs_version);
#ifdef HAVE_FS_NFS3
  if (mf->mf_server->fs_version == NFS_VERSION3)
    mnt_version = AM_MOUNTVERS3;
  else
#endif /* HAVE_FS_NFS3 */
    mnt_version = MOUNTVERS;

  /*
   * Create a client attached to mountd
   */
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  client = get_mount_client(host, &sin, &tv, &sock, mnt_version);
  if (client == NULL) {
#ifdef HAVE_CLNT_SPCREATEERROR
    plog(XLOG_ERROR, "get_mount_client failed for %s: %s",
	 host, clnt_spcreateerror(""));
#else /* not HAVE_CLNT_SPCREATEERROR */
    plog(XLOG_ERROR, "get_mount_client failed for %s", host);
#endif /* not HAVE_CLNT_SPCREATEERROR */
    goto out;
  }

  if (!nfs_auth) {
    if (make_nfs_auth())
      goto out;
  }
  client->cl_auth = nfs_auth;

  dlog("Unmounting all from %s", host);

  clnt_stat = clnt_call(client,
			MOUNTPROC_UMNTALL,
			(XDRPROC_T_TYPE) xdr_void,
			0,
			(XDRPROC_T_TYPE) xdr_void,
			0,
			tv);
  if (clnt_stat != RPC_SUCCESS && clnt_stat != RPC_SYSTEMERROR) {
    /* RPC_SYSTEMERROR seems to be returned for no good reason ... */
    const char *msg = clnt_sperrno(clnt_stat);
    plog(XLOG_ERROR, "unmount all from %s rpc failed: %s", host, msg);
    goto out;
  }

out:
  if (sock != RPC_ANYSOCK)
    (void) amu_close(sock);
  if (client)
    clnt_destroy(client);
}
