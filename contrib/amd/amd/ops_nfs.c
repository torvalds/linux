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
 * File: am-utils/amd/ops_nfs.c
 *
 */

/*
 * Network file system
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/*
 * Convert from nfsstat to UN*X error code
 */
#define unx_error(e)	((int)(e))

/*
 * FH_TTL is the time a file handle will remain in the cache since
 * last being used.  If the file handle becomes invalid, then it
 * will be flushed anyway.
 */
#define	FH_TTL			(5 * 60) /* five minutes */
#define	FH_TTL_ERROR		(30) /* 30 seconds */
#define	FHID_ALLOC()		(++fh_id)

/*
 * The NFS layer maintains a cache of file handles.
 * This is *fundamental* to the implementation and
 * also allows quick remounting when a filesystem
 * is accessed soon after timing out.
 *
 * The NFS server layer knows to flush this cache
 * when a server goes down so avoiding stale handles.
 *
 * Each cache entry keeps a hard reference to
 * the corresponding server.  This ensures that
 * the server keepalive information is maintained.
 *
 * The copy of the sockaddr_in here is taken so
 * that the port can be twiddled to talk to mountd
 * instead of portmap or the NFS server as used
 * elsewhere.
 * The port# is flushed if a server goes down.
 * The IP address is never flushed - we assume
 * that the address of a mounted machine never
 * changes.  If it does, then you have other
 * problems...
 */
typedef struct fh_cache fh_cache;
struct fh_cache {
  qelem			fh_q;		/* List header */
  wchan_t		fh_wchan;	/* Wait channel */
  int			fh_error;	/* Valid data? */
  int			fh_id;		/* Unique id */
  int			fh_cid;		/* Callout id */
  u_long		fh_nfs_version;	/* highest NFS version on host */
  am_nfs_handle_t	fh_nfs_handle;	/* Handle on filesystem */
  int			fh_status;	/* Status of last rpc */
  struct sockaddr_in	fh_sin;		/* Address of mountd */
  fserver		*fh_fs;		/* Server holding filesystem */
  char			*fh_path;	/* Filesystem on host */
};

/* forward definitions */
static int nfs_init(mntfs *mf);
static char *nfs_match(am_opts *fo);
static int nfs_mount(am_node *am, mntfs *mf);
static int nfs_umount(am_node *am, mntfs *mf);
static void nfs_umounted(mntfs *mf);
static int call_mountd(fh_cache *fp, u_long proc, fwd_fun f, wchan_t wchan);
static int webnfs_lookup(fh_cache *fp, fwd_fun f, wchan_t wchan);
static int fh_id = 0;

/*
 * clamp the filehandle version to 3, so that we can fail back to nfsv3
 * since nfsv4 does not have file handles
 */
#define SET_FH_VERSION(fs) \
    (fs)->fs_version > NFS_VERSION3 ? NFS_VERSION3 : (fs)->fs_version;

/* globals */
AUTH *nfs_auth;
qelem fh_head = {&fh_head, &fh_head};

/*
 * Network file system operations
 */
am_ops nfs_ops =
{
  "nfs",
  nfs_match,
  nfs_init,
  nfs_mount,
  nfs_umount,
  amfs_error_lookup_child,
  amfs_error_mount_child,
  amfs_error_readdir,
  0,				/* nfs_readlink */
  0,				/* nfs_mounted */
  nfs_umounted,
  find_nfs_srvr,
  0,				/* nfs_get_wchan */
  FS_MKMNT | FS_BACKGROUND | FS_AMQINFO,	/* nfs_fs_flags */
#ifdef HAVE_FS_AUTOFS
  AUTOFS_NFS_FS_FLAGS,
#endif /* HAVE_FS_AUTOFS */
};


static fh_cache *
find_nfs_fhandle_cache(opaque_t arg, int done)
{
  fh_cache *fp, *fp2 = NULL;
  int id = (long) arg;		/* for 64-bit archs */

  ITER(fp, fh_cache, &fh_head) {
    if (fp->fh_id == id) {
      fp2 = fp;
      break;
    }
  }

  if (fp2) {
    dlog("fh cache gives fp %#lx, fs %s", (unsigned long) fp2, fp2->fh_path);
  } else {
    dlog("fh cache search failed");
  }

  if (fp2 && !done) {
    fp2->fh_error = ETIMEDOUT;
    return 0;
  }

  return fp2;
}


/*
 * Called when a filehandle appears via the mount protocol
 */
static void
got_nfs_fh_mount(voidp pkt, int len, struct sockaddr_in *sa, struct sockaddr_in *ia, opaque_t arg, int done)
{
  fh_cache *fp;
  struct fhstatus res;
#ifdef HAVE_FS_NFS3
  struct am_mountres3 res3;
#endif /* HAVE_FS_NFS3 */

  fp = find_nfs_fhandle_cache(arg, done);
  if (!fp)
    return;

  /*
   * retrieve the correct RPC reply for the file handle, based on the
   * NFS protocol version.
   */
#ifdef HAVE_FS_NFS3
  if (fp->fh_nfs_version == NFS_VERSION3) {
    memset(&res3, 0, sizeof(res3));
    fp->fh_error = pickup_rpc_reply(pkt, len, (voidp) &res3,
				    (XDRPROC_T_TYPE) xdr_am_mountres3);
    fp->fh_status = unx_error(res3.fhs_status);
    memset(&fp->fh_nfs_handle.v3, 0, sizeof(am_nfs_fh3));
    fp->fh_nfs_handle.v3.am_fh3_length = res3.mountres3_u.mountinfo.fhandle.fhandle3_len;
    memmove(fp->fh_nfs_handle.v3.am_fh3_data,
	    res3.mountres3_u.mountinfo.fhandle.fhandle3_val,
	    fp->fh_nfs_handle.v3.am_fh3_length);

    XFREE(res3.mountres3_u.mountinfo.fhandle.fhandle3_val);
    if (res3.mountres3_u.mountinfo.auth_flavors.auth_flavors_val)
      XFREE(res3.mountres3_u.mountinfo.auth_flavors.auth_flavors_val);
  } else {
#endif /* HAVE_FS_NFS3 */
    memset(&res, 0, sizeof(res));
    fp->fh_error = pickup_rpc_reply(pkt, len, (voidp) &res,
				    (XDRPROC_T_TYPE) xdr_fhstatus);
    fp->fh_status = unx_error(res.fhs_status);
    memmove(&fp->fh_nfs_handle.v2, &res.fhs_fh, NFS_FHSIZE);
#ifdef HAVE_FS_NFS3
  }
#endif /* HAVE_FS_NFS3 */

  if (!fp->fh_error) {
    dlog("got filehandle for %s:%s", fp->fh_fs->fs_host, fp->fh_path);
  } else {
    plog(XLOG_USER, "filehandle denied for %s:%s", fp->fh_fs->fs_host, fp->fh_path);
    /*
     * Force the error to be EACCES. It's debatable whether it should be
     * ENOENT instead, but the server really doesn't give us any clues, and
     * EACCES is more in line with the "filehandle denied" message.
     */
    fp->fh_error = EACCES;
  }

  /*
   * Wakeup anything sleeping on this filehandle
   */
  if (fp->fh_wchan) {
    dlog("Calling wakeup on %#lx", (unsigned long) fp->fh_wchan);
    wakeup(fp->fh_wchan);
  }
}


/*
 * Called when a filehandle appears via WebNFS
 */
static void
got_nfs_fh_webnfs(voidp pkt, int len, struct sockaddr_in *sa, struct sockaddr_in *ia, opaque_t arg, int done)
{
  fh_cache *fp;
  nfsdiropres res;
#ifdef HAVE_FS_NFS3
  am_LOOKUP3res res3;
#endif /* HAVE_FS_NFS3 */

  fp = find_nfs_fhandle_cache(arg, done);
  if (!fp)
    return;

  /*
   * retrieve the correct RPC reply for the file handle, based on the
   * NFS protocol version.
   */
#ifdef HAVE_FS_NFS3
  if (fp->fh_nfs_version == NFS_VERSION3) {
    memset(&res3, 0, sizeof(res3));
    fp->fh_error = pickup_rpc_reply(pkt, len, (voidp) &res3,
				    (XDRPROC_T_TYPE) xdr_am_LOOKUP3res);
    fp->fh_status = unx_error(res3.status);
    memset(&fp->fh_nfs_handle.v3, 0, sizeof(am_nfs_fh3));
    fp->fh_nfs_handle.v3.am_fh3_length = res3.res_u.ok.object.am_fh3_length;
    memmove(fp->fh_nfs_handle.v3.am_fh3_data,
	    res3.res_u.ok.object.am_fh3_data,
	    fp->fh_nfs_handle.v3.am_fh3_length);
  } else {
#endif /* HAVE_FS_NFS3 */
    memset(&res, 0, sizeof(res));
    fp->fh_error = pickup_rpc_reply(pkt, len, (voidp) &res,
				    (XDRPROC_T_TYPE) xdr_diropres);
    fp->fh_status = unx_error(res.dr_status);
    memmove(&fp->fh_nfs_handle.v2, &res.dr_u.dr_drok_u.drok_fhandle, NFS_FHSIZE);
#ifdef HAVE_FS_NFS3
  }
#endif /* HAVE_FS_NFS3 */

  if (!fp->fh_error) {
    dlog("got filehandle for %s:%s", fp->fh_fs->fs_host, fp->fh_path);
  } else {
    plog(XLOG_USER, "filehandle denied for %s:%s", fp->fh_fs->fs_host, fp->fh_path);
    /*
     * Force the error to be EACCES. It's debatable whether it should be
     * ENOENT instead, but the server really doesn't give us any clues, and
     * EACCES is more in line with the "filehandle denied" message.
     */
    fp->fh_error = EACCES;
  }

  /*
   * Wakeup anything sleeping on this filehandle
   */
  if (fp->fh_wchan) {
    dlog("Calling wakeup on %#lx", (unsigned long) fp->fh_wchan);
    wakeup(fp->fh_wchan);
  }
}


void
flush_nfs_fhandle_cache(fserver *fs)
{
  fh_cache *fp;

  ITER(fp, fh_cache, &fh_head) {
    if (fp->fh_fs == fs || fs == NULL) {
      /*
       * Only invalidate port info for non-WebNFS servers
       */
      if (!(fp->fh_fs->fs_flags & FSF_WEBNFS))
	fp->fh_sin.sin_port = (u_short) 0;
      fp->fh_error = -1;
    }
  }
}


static void
discard_fh(opaque_t arg)
{
  fh_cache *fp = (fh_cache *) arg;

  rem_que(&fp->fh_q);
  if (fp->fh_fs) {
    dlog("Discarding filehandle for %s:%s", fp->fh_fs->fs_host, fp->fh_path);
    free_srvr(fp->fh_fs);
  }
  XFREE(fp->fh_path);
  XFREE(fp);
}


/*
 * Determine the file handle for a node
 */
static int
prime_nfs_fhandle_cache(char *path, fserver *fs, am_nfs_handle_t *fhbuf, mntfs *mf)
{
  fh_cache *fp, *fp_save = NULL;
  int error;
  int reuse_id = FALSE;

  dlog("Searching cache for %s:%s", fs->fs_host, path);

  /*
   * First search the cache
   */
  ITER(fp, fh_cache, &fh_head) {
    if (fs != fp->fh_fs  ||  !STREQ(path, fp->fh_path))
      continue;			/* skip to next ITER item */
    /* else we got a match */
    switch (fp->fh_error) {
    case 0:
      plog(XLOG_INFO, "prime_nfs_fhandle_cache: NFS version %d", (int) fp->fh_nfs_version);

      error = fp->fh_error = fp->fh_status;

      if (error == 0) {
	if (mf->mf_flags & MFF_NFS_SCALEDOWN) {
	  fp_save = fp;
	  /* XXX: why reuse the ID? */
	  reuse_id = TRUE;
	  break;
	}

	if (fhbuf) {
#ifdef HAVE_FS_NFS3
	  if (fp->fh_nfs_version == NFS_VERSION3) {
	    memmove((voidp) &(fhbuf->v3), (voidp) &(fp->fh_nfs_handle.v3),
		    sizeof(fp->fh_nfs_handle.v3));
	  } else
#endif /* HAVE_FS_NFS3 */
	    {
	      memmove((voidp) &(fhbuf->v2), (voidp) &(fp->fh_nfs_handle.v2),
		      sizeof(fp->fh_nfs_handle.v2));
	    }
	}
	if (fp->fh_cid)
	  untimeout(fp->fh_cid);
	fp->fh_cid = timeout(FH_TTL, discard_fh, (opaque_t) fp);
      } else if (error == EACCES) {
	/*
	 * Now decode the file handle return code.
	 */
	plog(XLOG_INFO, "Filehandle denied for \"%s:%s\"",
	     fs->fs_host, path);
      } else {
	errno = error;	/* XXX */
	plog(XLOG_INFO, "Filehandle error for \"%s:%s\": %m",
	     fs->fs_host, path);
      }

      /*
       * The error was returned from the remote mount daemon.
       * Policy: this error will be cached for now...
       */
      return error;

    case -1:
      /*
       * Still thinking about it, but we can re-use.
       */
      fp_save = fp;
      reuse_id = TRUE;
      break;

    default:
      /*
       * Return the error.
       * Policy: make sure we recompute if required again
       * in case this was caused by a network failure.
       * This can thrash mountd's though...  If you find
       * your mountd going slowly then:
       * 1.  Add a fork() loop to main.
       * 2.  Remove the call to innetgr() and don't use
       *     netgroups, especially if you don't use YP.
       */
      error = fp->fh_error;
      fp->fh_error = -1;
      return error;
    }	/* end of switch statement */
  } /* end of ITER loop */

  /*
   * Not in cache
   */
  if (fp_save) {
    fp = fp_save;
    /*
     * Re-use existing slot
     */
    untimeout(fp->fh_cid);
    free_srvr(fp->fh_fs);
    XFREE(fp->fh_path);
  } else {
    fp = ALLOC(struct fh_cache);
    memset((voidp) fp, 0, sizeof(struct fh_cache));
    ins_que(&fp->fh_q, &fh_head);
  }
  if (!reuse_id)
    fp->fh_id = FHID_ALLOC();
  fp->fh_wchan = get_mntfs_wchan(mf);
  fp->fh_error = -1;
  fp->fh_cid = timeout(FH_TTL, discard_fh, (opaque_t) fp);

  /*
   * If fs->fs_ip is null, remote server is probably down.
   */
  if (!fs->fs_ip) {
    /* Mark the fileserver down and invalid again */
    fs->fs_flags &= ~FSF_VALID;
    fs->fs_flags |= FSF_DOWN;
    error = AM_ERRNO_HOST_DOWN;
    return error;
  }

  /*
   * Either fp has been freshly allocated or the address has changed.
   * Initialize address and nfs version.  Don't try to re-use the port
   * information unless using WebNFS where the port is fixed either by
   * the spec or the "port" mount option.
   */
  if (fp->fh_sin.sin_addr.s_addr != fs->fs_ip->sin_addr.s_addr) {
    fp->fh_sin = *fs->fs_ip;
    if (!(mf->mf_flags & MFF_WEBNFS))
	fp->fh_sin.sin_port = 0;
    fp->fh_nfs_version = SET_FH_VERSION(fs);
  }

  fp->fh_fs = dup_srvr(fs);
  fp->fh_path = xstrdup(path);

  if (mf->mf_flags & MFF_WEBNFS)
    error = webnfs_lookup(fp, got_nfs_fh_webnfs, get_mntfs_wchan(mf));
  else
    error = call_mountd(fp, MOUNTPROC_MNT, got_nfs_fh_mount, get_mntfs_wchan(mf));
  if (error) {
    /*
     * Local error - cache for a short period
     * just to prevent thrashing.
     */
    untimeout(fp->fh_cid);
    fp->fh_cid = timeout(error < 0 ? 2 * ALLOWED_MOUNT_TIME : FH_TTL_ERROR,
			 discard_fh, (opaque_t) fp);
    fp->fh_error = error;
  } else {
    error = fp->fh_error;
  }

  return error;
}


int
make_nfs_auth(void)
{
  AUTH_CREATE_GIDLIST_TYPE group_wheel = 0;

  /* Some NFS mounts (particularly cross-domain) require FQDNs to succeed */

#ifdef HAVE_TRANSPORT_TYPE_TLI
  if (gopt.flags & CFM_FULLY_QUALIFIED_HOSTS) {
    plog(XLOG_INFO, "Using NFS auth for FQHN \"%s\"", hostd);
    nfs_auth = authsys_create(hostd, 0, 0, 1, &group_wheel);
  } else {
    nfs_auth = authsys_create_default();
  }
#else /* not HAVE_TRANSPORT_TYPE_TLI */
  if (gopt.flags & CFM_FULLY_QUALIFIED_HOSTS) {
    plog(XLOG_INFO, "Using NFS auth for FQHN \"%s\"", hostd);
    nfs_auth = authunix_create(hostd, 0, 0, 1, &group_wheel);
  } else {
    nfs_auth = authunix_create_default();
  }
#endif /* not HAVE_TRANSPORT_TYPE_TLI */

  if (!nfs_auth)
    return ENOBUFS;

  return 0;
}


static int
call_mountd(fh_cache *fp, u_long proc, fwd_fun fun, wchan_t wchan)
{
  struct rpc_msg mnt_msg;
  int len;
  char iobuf[UDPMSGSIZE];
  int error;
  u_long mnt_version;

  if (!nfs_auth) {
    error = make_nfs_auth();
    if (error)
      return error;
  }

  if (fp->fh_sin.sin_port == 0) {
    u_short mountd_port;
    error = get_mountd_port(fp->fh_fs, &mountd_port, wchan);
    if (error)
      return error;
    fp->fh_sin.sin_port = mountd_port;
    dlog("%s: New %d mountd port", __func__, fp->fh_sin.sin_port);
  } else
    dlog("%s: Already had %d mountd port", __func__, fp->fh_sin.sin_port);

  /* find the right version of the mount protocol */
#ifdef HAVE_FS_NFS3
  if (fp->fh_nfs_version == NFS_VERSION3)
    mnt_version = AM_MOUNTVERS3;
  else
#endif /* HAVE_FS_NFS3 */
    mnt_version = MOUNTVERS;
  plog(XLOG_INFO, "call_mountd: NFS version %d, mount version %d",
       (int) fp->fh_nfs_version, (int) mnt_version);

  rpc_msg_init(&mnt_msg, MOUNTPROG, mnt_version, MOUNTPROC_NULL);
  len = make_rpc_packet(iobuf,
			sizeof(iobuf),
			proc,
			&mnt_msg,
			(voidp) &fp->fh_path,
			(XDRPROC_T_TYPE) xdr_nfspath,
			nfs_auth);

  if (len > 0) {
    error = fwd_packet(MK_RPC_XID(RPC_XID_MOUNTD, fp->fh_id),
		       iobuf,
		       len,
		       &fp->fh_sin,
		       &fp->fh_sin,
		       (opaque_t) ((long) fp->fh_id), /* cast to long needed for 64-bit archs */
		       fun);
  } else {
    error = -len;
  }

  /*
   * It may be the case that we're sending to the wrong MOUNTD port.  This
   * occurs if mountd is restarted on the server after the port has been
   * looked up and stored in the filehandle cache somewhere.  The correct
   * solution, if we're going to cache port numbers is to catch the ICMP
   * port unreachable reply from the server and cause the portmap request
   * to be redone.  The quick solution here is to invalidate the MOUNTD
   * port.
   */
  fp->fh_sin.sin_port = 0;

  return error;
}


static int
webnfs_lookup(fh_cache *fp, fwd_fun fun, wchan_t wchan)
{
  struct rpc_msg wnfs_msg;
  int len;
  char iobuf[UDPMSGSIZE];
  int error;
  u_long proc;
  XDRPROC_T_TYPE xdr_fn;
  voidp argp;
  nfsdiropargs args;
#ifdef HAVE_FS_NFS3
  am_LOOKUP3args args3;
#endif /* HAVE_FS_NFS3 */
  char *wnfs_path;
  size_t l;

  if (!nfs_auth) {
    error = make_nfs_auth();
    if (error)
      return error;
  }

  if (fp->fh_sin.sin_port == 0) {
    /* FIXME: wrong, don't discard sin_port in the first place for WebNFS. */
    plog(XLOG_WARNING, "webnfs_lookup: port == 0 for nfs on %s, fixed",
	 fp->fh_fs->fs_host);
    fp->fh_sin.sin_port = htons(NFS_PORT);
  }

  /*
   * Use native path like the rest of amd (cf. RFC 2054, 6.1).
   */
  l = strlen(fp->fh_path) + 2;
  wnfs_path = (char *) xmalloc(l);
  wnfs_path[0] = 0x80;
  xstrlcpy(wnfs_path + 1, fp->fh_path, l - 1);

  /* find the right program and lookup procedure */
#ifdef HAVE_FS_NFS3
  if (fp->fh_nfs_version == NFS_VERSION3) {
    proc = AM_NFSPROC3_LOOKUP;
    xdr_fn = (XDRPROC_T_TYPE) xdr_am_LOOKUP3args;
    argp = &args3;
    /* WebNFS public file handle */
    args3.what.dir.am_fh3_length = 0;
    args3.what.name = wnfs_path;
  } else {
#endif /* HAVE_FS_NFS3 */
    proc = NFSPROC_LOOKUP;
    xdr_fn = (XDRPROC_T_TYPE) xdr_diropargs;
    argp = &args;
    /* WebNFS public file handle */
    memset(&args.da_fhandle, 0, NFS_FHSIZE);
    args.da_name = wnfs_path;
#ifdef HAVE_FS_NFS3
  }
#endif /* HAVE_FS_NFS3 */

  plog(XLOG_INFO, "webnfs_lookup: NFS version %d", (int) fp->fh_nfs_version);

  rpc_msg_init(&wnfs_msg, NFS_PROGRAM, fp->fh_nfs_version, proc);
  len = make_rpc_packet(iobuf,
			sizeof(iobuf),
			proc,
			&wnfs_msg,
			argp,
			(XDRPROC_T_TYPE) xdr_fn,
			nfs_auth);

  if (len > 0) {
    error = fwd_packet(MK_RPC_XID(RPC_XID_WEBNFS, fp->fh_id),
		       iobuf,
		       len,
		       &fp->fh_sin,
		       &fp->fh_sin,
		       (opaque_t) ((long) fp->fh_id), /* cast to long needed for 64-bit archs */
		       fun);
  } else {
    error = -len;
  }

  XFREE(wnfs_path);
  return error;
}


/*
 * NFS needs the local filesystem, remote filesystem
 * remote hostname.
 * Local filesystem defaults to remote and vice-versa.
 */
static char *
nfs_match(am_opts *fo)
{
  char *xmtab;
  size_t l;

  if (fo->opt_fs && !fo->opt_rfs)
    fo->opt_rfs = fo->opt_fs;
  if (!fo->opt_rfs) {
    plog(XLOG_USER, "nfs: no remote filesystem specified");
    return NULL;
  }
  if (!fo->opt_rhost) {
    plog(XLOG_USER, "nfs: no remote host specified");
    return NULL;
  }

  /*
   * Determine magic cookie to put in mtab
   */
  l = strlen(fo->opt_rhost) + strlen(fo->opt_rfs) + 2;
  xmtab = (char *) xmalloc(l);
  xsnprintf(xmtab, l, "%s:%s", fo->opt_rhost, fo->opt_rfs);
  dlog("NFS: mounting remote server \"%s\", remote fs \"%s\" on \"%s\"",
       fo->opt_rhost, fo->opt_rfs, fo->opt_fs);

  return xmtab;
}


/*
 * Initialize am structure for nfs
 */
static int
nfs_init(mntfs *mf)
{
  int error;
  am_nfs_handle_t fhs;
  char *colon;

#ifdef NO_FALLBACK
  /*
   * We don't need file handles for NFS version 4, but we can fall back to
   * version 3, so we allocate anyway
   */
#ifdef HAVE_FS_NFS4
  if (mf->mf_server->fs_version == NFS_VERSION4)
    return 0;
#endif /* HAVE_FS_NFS4 */
#endif /* NO_FALLBACK */

  if (mf->mf_private) {
    if (mf->mf_flags & MFF_NFS_SCALEDOWN) {
      fserver *fs;

      /* tell remote mountd that we're done with this filehandle */
      mf->mf_ops->umounted(mf);

      mf->mf_prfree(mf->mf_private);
      mf->mf_private = NULL;
      mf->mf_prfree = NULL;

      fs = mf->mf_ops->ffserver(mf);
      free_srvr(mf->mf_server);
      mf->mf_server = fs;
    } else
      return 0;
  }

  colon = strchr(mf->mf_info, ':');
  if (colon == 0)
    return ENOENT;

  error = prime_nfs_fhandle_cache(colon + 1, mf->mf_server, &fhs, mf);
  if (!error) {
    mf->mf_private = (opaque_t) ALLOC(am_nfs_handle_t);
    mf->mf_prfree = (void (*)(opaque_t)) free;
    memmove(mf->mf_private, (voidp) &fhs, sizeof(fhs));
  }
  return error;
}


int
mount_nfs_fh(am_nfs_handle_t *fhp, char *mntdir, char *fs_name, mntfs *mf)
{
  MTYPE_TYPE type;
  char *colon;
  char *xopts=NULL, transp_timeo_opts[40], transp_retrans_opts[40];
  char host[MAXHOSTNAMELEN + MAXPATHLEN + 2];
  fserver *fs = mf->mf_server;
  u_long nfs_version = fs->fs_version;
  char *nfs_proto = fs->fs_proto; /* "tcp" or "udp" */
  int on_autofs = mf->mf_flags & MFF_ON_AUTOFS;
  int error;
  int genflags;
  int retry;
  int proto = AMU_TYPE_NONE;
  mntent_t mnt;
  void *argsp;
  nfs_args_t nfs_args;
#ifdef HAVE_FS_NFS4
  nfs4_args_t nfs4_args;
#endif /* HAVE_FS_NFS4 */

  /*
   * Extract HOST name to give to kernel.
   * Some systems like osf1/aix3/bsd44 variants may need old code
   * for NFS_ARGS_NEEDS_PATH.
   */
  if (!(colon = strchr(fs_name, ':')))
    return ENOENT;
#ifdef MOUNT_TABLE_ON_FILE
  *colon = '\0';
#endif /* MOUNT_TABLE_ON_FILE */
  xstrlcpy(host, fs_name, sizeof(host));
#ifdef MOUNT_TABLE_ON_FILE
  *colon = ':';
#endif /* MOUNT_TABLE_ON_FILE */
#ifdef MAXHOSTNAMELEN
  /* most kernels have a name length restriction */
  if (strlen(host) >= MAXHOSTNAMELEN)
    xstrlcpy(host + MAXHOSTNAMELEN - 3, "..",
	     sizeof(host) - MAXHOSTNAMELEN + 3);
#endif /* MAXHOSTNAMELEN */

  /*
   * Create option=VAL for udp/tcp specific timeouts and retrans values, but
   * only if these options were specified.
   */

  transp_timeo_opts[0] = transp_retrans_opts[0] = '\0';	/* initialize */
  if (STREQ(nfs_proto, "udp"))
    proto = AMU_TYPE_UDP;
  else if (STREQ(nfs_proto, "tcp"))
    proto = AMU_TYPE_TCP;
  if (proto != AMU_TYPE_NONE) {
    if (gopt.amfs_auto_timeo[proto] > 0)
      xsnprintf(transp_timeo_opts, sizeof(transp_timeo_opts), "%s=%d,",
		MNTTAB_OPT_TIMEO, gopt.amfs_auto_timeo[proto]);
    if (gopt.amfs_auto_retrans[proto] > 0)
      xsnprintf(transp_retrans_opts, sizeof(transp_retrans_opts), "%s=%d,",
		MNTTAB_OPT_RETRANS, gopt.amfs_auto_retrans[proto]);
  }

  if (mf->mf_remopts && *mf->mf_remopts &&
      !islocalnet(fs->fs_ip->sin_addr.s_addr)) {
    plog(XLOG_INFO, "Using remopts=\"%s\"", mf->mf_remopts);
    /* use transp_opts first, so map-specific opts will override */
    xopts = str3cat(xopts, transp_timeo_opts, transp_retrans_opts, mf->mf_remopts);
  } else {
    /* use transp_opts first, so map-specific opts will override */
    xopts = str3cat(xopts, transp_timeo_opts, transp_retrans_opts, mf->mf_mopts);
  }

  memset((voidp) &mnt, 0, sizeof(mnt));
  mnt.mnt_dir = mntdir;
  mnt.mnt_fsname = fs_name;
  mnt.mnt_opts = xopts;

  /*
   * Set mount types accordingly
   */
#ifdef HAVE_FS_NFS3
  if (nfs_version == NFS_VERSION3) {
    type = MOUNT_TYPE_NFS3;
    /*
     * Systems that include the mount table "vers" option generally do not
     * set the mnttab entry to "nfs3", but to "nfs" and then they set
     * "vers=3".  Setting it to "nfs3" works, but it may break some things
     * like "df -t nfs" and the "quota" program (esp. on Solaris and Irix).
     * So on those systems, set it to "nfs".
     * Note: MNTTAB_OPT_VERS is always set for NFS3 (see am_compat.h).
     */
    argsp = &nfs_args;
# if defined(MNTTAB_OPT_VERS) && defined(MOUNT_TABLE_ON_FILE)
    mnt.mnt_type = MNTTAB_TYPE_NFS;
# else /* defined(MNTTAB_OPT_VERS) && defined(MOUNT_TABLE_ON_FILE) */
    mnt.mnt_type = MNTTAB_TYPE_NFS3;
# endif /* defined(MNTTAB_OPT_VERS) && defined(MOUNT_TABLE_ON_FILE) */
# ifdef HAVE_FS_NFS4
  } else if (nfs_version == NFS_VERSION4) {
    argsp = &nfs4_args;
    type = MOUNT_TYPE_NFS4;
    mnt.mnt_type = MNTTAB_TYPE_NFS4;
# endif /* HAVE_FS_NFS4 */
  } else
#endif /* HAVE_FS_NFS3 */
  {
    argsp = &nfs_args;
    type = MOUNT_TYPE_NFS;
    mnt.mnt_type = MNTTAB_TYPE_NFS;
  }
  plog(XLOG_INFO, "mount_nfs_fh: NFS version %d", (int) nfs_version);
  plog(XLOG_INFO, "mount_nfs_fh: using NFS transport %s", nfs_proto);

  retry = hasmntval(&mnt, MNTTAB_OPT_RETRY);
  if (retry <= 0)
    retry = 1;			/* XXX */

  genflags = compute_mount_flags(&mnt);
#ifdef HAVE_FS_AUTOFS
  if (on_autofs)
    genflags |= autofs_compute_mount_flags(&mnt);
#endif /* HAVE_FS_AUTOFS */

   /* setup the many fields and flags within nfs_args */
   compute_nfs_args(argsp,
		    &mnt,
		    genflags,
		    NULL,	/* struct netconfig *nfsncp */
		    fs->fs_ip,
		    nfs_version,
		    nfs_proto,
		    fhp,
		    host,
		    fs_name);

  /* finally call the mounting function */
  if (amuDebug(D_TRACE)) {
    print_nfs_args(argsp, nfs_version);
    plog(XLOG_DEBUG, "Generic mount flags 0x%x used for NFS mount", genflags);
  }
  error = mount_fs(&mnt, genflags, argsp, retry, type,
		   nfs_version, nfs_proto, mnttab_file_name, on_autofs);
  XFREE(mnt.mnt_opts);
  discard_nfs_args(argsp, nfs_version);

#ifdef HAVE_FS_NFS4
# ifndef NO_FALLBACK
  /*
   * If we are using a v4 file handle, we try a v3 if we get back:
   * 	ENOENT: NFS v4 has a different export list than v3
   * 	EPERM: Kernels <= 2.6.18 return that, instead of ENOENT
   */
  if ((error == ENOENT || error == EPERM) && nfs_version == NFS_VERSION4) {
    plog(XLOG_DEBUG, "Could not find NFS 4 mount, trying again with NFS 3");
    fs->fs_version = NFS_VERSION3;
    error = mount_nfs_fh(fhp, mntdir, fs_name, mf);
    if (error)
      fs->fs_version = NFS_VERSION4;
  }
# endif /* NO_FALLBACK */
#endif /* HAVE_FS_NFS4 */

  return error;
}


static int
nfs_mount(am_node *am, mntfs *mf)
{
  int error = 0;
  mntent_t mnt;

  if (!mf->mf_private && mf->mf_server->fs_version != 4) {
    plog(XLOG_ERROR, "Missing filehandle for %s", mf->mf_info);
    return EINVAL;
  }

  if (mf->mf_mopts == NULL) {
    plog(XLOG_ERROR, "Missing mount options for %s", mf->mf_info);
    return EINVAL;
  }

  mnt.mnt_opts = mf->mf_mopts;
  if (amu_hasmntopt(&mnt, "softlookup") ||
      (amu_hasmntopt(&mnt, "soft") && !amu_hasmntopt(&mnt, "nosoftlookup")))
    am->am_flags |= AMF_SOFTLOOKUP;

  error = mount_nfs_fh((am_nfs_handle_t *) mf->mf_private,
		       mf->mf_mount,
		       mf->mf_info,
		       mf);

  if (error) {
    errno = error;
    dlog("mount_nfs: %m");
  }

  return error;
}


static int
nfs_umount(am_node *am, mntfs *mf)
{
  int unmount_flags, new_unmount_flags, error;

  dlog("attempting nfs umount");
  unmount_flags = (mf->mf_flags & MFF_ON_AUTOFS) ? AMU_UMOUNT_AUTOFS : 0;
  error = UMOUNT_FS(mf->mf_mount, mnttab_file_name, unmount_flags);

#if defined(HAVE_UMOUNT2) && (defined(MNT2_GEN_OPT_FORCE) || defined(MNT2_GEN_OPT_DETACH))
  /*
   * If the attempt to unmount failed with EBUSY, and this fserver was
   * marked for forced unmounts, then use forced/lazy unmounts.
   */
  if (error == EBUSY &&
      gopt.flags & CFM_FORCED_UNMOUNTS &&
      mf->mf_server->fs_flags & FSF_FORCE_UNMOUNT) {
    plog(XLOG_INFO, "EZK: nfs_umount: trying forced/lazy unmounts");
    /*
     * XXX: turning off the FSF_FORCE_UNMOUNT may not be perfectly
     * incorrect.  Multiple nodes may need to be timed out and restarted for
     * a single hung fserver.
     */
    mf->mf_server->fs_flags &= ~FSF_FORCE_UNMOUNT;
    new_unmount_flags = unmount_flags | AMU_UMOUNT_FORCE | AMU_UMOUNT_DETACH;
    error = UMOUNT_FS(mf->mf_mount, mnttab_file_name, new_unmount_flags);
  }
#endif /* HAVE_UMOUNT2 && (MNT2_GEN_OPT_FORCE || MNT2_GEN_OPT_DETACH) */

  /*
   * Here is some code to unmount 'restarted' file systems.
   * The restarted file systems are marked as 'nfs', not
   * 'host', so we only have the map information for the
   * the top-level mount.  The unmount will fail (EBUSY)
   * if there are anything else from the NFS server mounted
   * below the mount-point.  This code checks to see if there
   * is anything mounted with the same prefix as the
   * file system to be unmounted ("/a/b/c" when unmounting "/a/b").
   * If there is, and it is a 'restarted' file system, we unmount
   * it.
   * Added by Mike Mitchell, mcm@unx.sas.com, 09/08/93
   */
  if (error == EBUSY) {
    mntfs *new_mf;
    int len = strlen(mf->mf_mount);
    int didsome = 0;

    ITER(new_mf, mntfs, &mfhead) {
      if (new_mf->mf_ops != mf->mf_ops ||
	  new_mf->mf_refc > 1 ||
	  mf == new_mf ||
	  ((new_mf->mf_flags & (MFF_MOUNTED | MFF_UNMOUNTING | MFF_RESTART)) == (MFF_MOUNTED | MFF_RESTART)))
	continue;

      if (NSTREQ(mf->mf_mount, new_mf->mf_mount, len) &&
	  new_mf->mf_mount[len] == '/') {
	new_unmount_flags =
	  (new_mf->mf_flags & MFF_ON_AUTOFS) ? AMU_UMOUNT_AUTOFS : 0;
	UMOUNT_FS(new_mf->mf_mount, mnttab_file_name, new_unmount_flags);
	didsome = 1;
      }
    }
    if (didsome)
      error = UMOUNT_FS(mf->mf_mount, mnttab_file_name, unmount_flags);
  }
  if (error)
    return error;

  return 0;
}


static void
nfs_umounted(mntfs *mf)
{
  fserver *fs;
  char *colon, *path;

  if (mf->mf_error || mf->mf_refc > 1)
    return;

  /*
   * No need to inform mountd when WebNFS is in use.
   */
  if (mf->mf_flags & MFF_WEBNFS)
    return;

  /*
   * Call the mount daemon on the server to announce that we are not using
   * the fs any more.
   *
   * XXX: This is *wrong*.  The mountd should be called when the fhandle is
   * flushed from the cache, and a reference held to the cached entry while
   * the fs is mounted...
   */
  fs = mf->mf_server;
  colon = path = strchr(mf->mf_info, ':');
  if (fs && colon) {
    fh_cache f;

    dlog("calling mountd for %s", mf->mf_info);
    *path++ = '\0';
    f.fh_path = path;
    f.fh_sin = *fs->fs_ip;
    f.fh_sin.sin_port = (u_short) 0;
    f.fh_nfs_version = SET_FH_VERSION(fs);
    f.fh_fs = fs;
    f.fh_id = 0;
    f.fh_error = 0;
    prime_nfs_fhandle_cache(colon + 1, mf->mf_server, (am_nfs_handle_t *) NULL, mf);
    call_mountd(&f, MOUNTPROC_UMNT, (fwd_fun *) NULL, (wchan_t) NULL);
    *colon = ':';
  }
}
