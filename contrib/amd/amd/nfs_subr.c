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
 * File: am-utils/amd/nfs_subr.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/*
 * Convert from UN*X to NFS error code.
 * Some systems like linux define their own (see
 * conf/mount/mount_linux.h).
 */
#ifndef nfs_error
# define nfs_error(e) ((nfsstat)(e))
#endif /* nfs_error */

/*
 * File Handle structure
 *
 * This is interpreted by indexing the exported array
 * by fhh_id (for old-style filehandles), or by retrieving
 * the node name from fhh_path (for new-style filehandles).
 *
 * The whole structure is mapped onto a standard fhandle_t
 * when transmitted.
 */
struct am_fh {
  u_int fhh_gen;				/* generation number */
  union {
    struct {
      int fhh_type;				/* old or new am_fh */
      pid_t fhh_pid;				/* process id */
      int fhh_id;				/* map id */
    } s;
    char fhh_path[NFS_FHSIZE-sizeof(u_int)];	/* path to am_node */
  } u;
};

struct am_fh3 {
  u_int fhh_gen;				/* generation number */
  union {
    struct {
      int fhh_type;				/* old or new am_fh */
      pid_t fhh_pid;				/* process id */
      int fhh_id;				/* map id */
    } s;
    char fhh_path[AM_FHSIZE3-sizeof(u_int)];	/* path to am_node */
  } u;
};

/* forward declarations */
/* converting am-filehandles to mount-points */
static am_node *fh_to_mp3(am_nfs_fh *fhp, int *rp, int vop);
static am_node *fh_to_mp(am_nfs_fh *fhp);
static void count_map_entries(const am_node *mp, u_int *out_blocks, u_int *out_bfree, u_int *out_bavail);


static char *
do_readlink(am_node *mp, int *error_return)
{
  char *ln;

  /*
   * If there is a readlink method then use it,
   * otherwise if a link exists use that,
   * otherwise use the mount point.
   */
  if (mp->am_al->al_mnt->mf_ops->readlink) {
    int retry = 0;
    mp = (*mp->am_al->al_mnt->mf_ops->readlink) (mp, &retry);
    if (mp == NULL) {
      *error_return = retry;
      return 0;
    }
    /* reschedule_timeout_mp(); */
  }

  if (mp->am_link) {
    ln = mp->am_link;
  } else {
    ln = mp->am_al->al_mnt->mf_mount;
  }

  return ln;
}


voidp
nfsproc_null_2_svc(voidp argp, struct svc_req *rqstp)
{
  static char res;

  return (voidp) &res;
}


nfsattrstat *
nfsproc_getattr_2_svc(am_nfs_fh *argp, struct svc_req *rqstp)
{
  static nfsattrstat res;
  am_node *mp;
  int retry = 0;
  time_t now = clocktime(NULL);

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "getattr:");

  mp = fh_to_mp3(argp, &retry, VLOOK_CREATE);
  if (mp == NULL) {
    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "\tretry=%d", retry);

    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    res.ns_status = nfs_error(retry);
    return &res;
  }

  res = mp->am_attr;
  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "\tstat(%s), size = %d, mtime=%ld.%ld",
	 mp->am_path,
	 (int) res.ns_u.ns_attr_u.na_size,
	 (long) res.ns_u.ns_attr_u.na_mtime.nt_seconds,
	 (long) res.ns_u.ns_attr_u.na_mtime.nt_useconds);

  /* Delay unmount of what was looked up */
  if (mp->am_timeo_w < 4 * gopt.am_timeo_w)
    mp->am_timeo_w += gopt.am_timeo_w;
  mp->am_ttl = now + mp->am_timeo_w;

  mp->am_stats.s_getattr++;
  return &res;
}


nfsattrstat *
nfsproc_setattr_2_svc(nfssattrargs *argp, struct svc_req *rqstp)
{
  static nfsattrstat res;

  if (!fh_to_mp(&argp->sag_fhandle))
    res.ns_status = nfs_error(ESTALE);
  else
    res.ns_status = nfs_error(EROFS);

  return &res;
}


voidp
nfsproc_root_2_svc(voidp argp, struct svc_req *rqstp)
{
  static char res;

  return (voidp) &res;
}


nfsdiropres *
nfsproc_lookup_2_svc(nfsdiropargs *argp, struct svc_req *rqstp)
{
  static nfsdiropres res;
  am_node *mp;
  int retry;
  uid_t uid;
  gid_t gid;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "lookup:");

  /* finally, find the effective uid/gid from RPC request */
  if (getcreds(rqstp, &uid, &gid, nfsxprt) < 0)
    plog(XLOG_ERROR, "cannot get uid/gid from RPC credentials");
  xsnprintf(opt_uid, sizeof(uid_str), "%d", (int) uid);
  xsnprintf(opt_gid, sizeof(gid_str), "%d", (int) gid);

  mp = fh_to_mp3(&argp->da_fhandle, &retry, VLOOK_CREATE);
  if (mp == NULL) {
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    res.dr_status = nfs_error(retry);
  } else {
    int error;
    am_node *ap;
    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "\tlookup(%s, %s)", mp->am_path, argp->da_name);
    ap = mp->am_al->al_mnt->mf_ops->lookup_child(mp, argp->da_name, &error, VLOOK_CREATE);
    if (ap && error < 0)
      ap = mp->am_al->al_mnt->mf_ops->mount_child(ap, &error);
    if (ap == 0) {
      if (error < 0) {
	amd_stats.d_drops++;
	return 0;
      }
      res.dr_status = nfs_error(error);
    } else {
      /*
       * XXX: EXPERIMENTAL! Delay unmount of what was looked up.  This
       * should reduce the chance for race condition between unmounting an
       * entry synchronously, and re-mounting it asynchronously.
       */
      if (ap->am_ttl < mp->am_ttl)
 	ap->am_ttl = mp->am_ttl;
      mp_to_fh(ap, &res.dr_u.dr_drok_u.drok_fhandle);
      res.dr_u.dr_drok_u.drok_attributes = ap->am_fattr;
      res.dr_status = NFS_OK;
    }
    mp->am_stats.s_lookup++;
    /* reschedule_timeout_mp(); */
  }

  return &res;
}


void
nfs_quick_reply(am_node *mp, int error)
{
  SVCXPRT *transp = mp->am_transp;
  nfsdiropres res;
  xdrproc_t xdr_result = (xdrproc_t) xdr_diropres;

  /*
   * If there's a transp structure then we can reply to the client's
   * nfs lookup request.
   */
  if (transp) {
    if (error == 0) {
      /*
       * Construct a valid reply to a lookup request.  Same
       * code as in nfsproc_lookup_2_svc() above.
       */
      mp_to_fh(mp, &res.dr_u.dr_drok_u.drok_fhandle);
      res.dr_u.dr_drok_u.drok_attributes = mp->am_fattr;
      res.dr_status = NFS_OK;
    } else
      /*
       * Return the error that was passed to us.
       */
      res.dr_status = nfs_error(error);

    /*
     * Send off our reply
     */
    if (!svc_sendreply(transp, (XDRPROC_T_TYPE) xdr_result, (SVC_IN_ARG_TYPE) & res))
      svcerr_systemerr(transp);

    /*
     * Free up transp.  It's only used for one reply.
     */
    XFREE(mp->am_transp);
    dlog("Quick reply sent for %s", mp->am_al->al_mnt->mf_mount);
  }
}


nfsreadlinkres *
nfsproc_readlink_2_svc(am_nfs_fh *argp, struct svc_req *rqstp)
{
  static nfsreadlinkres res;
  am_node *mp;
  int retry;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "readlink:");

  mp = fh_to_mp3(argp, &retry, VLOOK_CREATE);
  if (mp == NULL) {
  readlink_retry:
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    res.rlr_status = nfs_error(retry);
  } else {
    char *ln = do_readlink(mp, &retry);
    if (ln == 0)
      goto readlink_retry;
    res.rlr_status = NFS_OK;
    if (amuDebug(D_TRACE) && ln)
      plog(XLOG_DEBUG, "\treadlink(%s) = %s", mp->am_path, ln);
    res.rlr_u.rlr_data_u = ln;
    mp->am_stats.s_readlink++;
  }

  return &res;
}


nfsreadres *
nfsproc_read_2_svc(nfsreadargs *argp, struct svc_req *rqstp)
{
  static nfsreadres res;

  memset((char *) &res, 0, sizeof(res));
  res.rr_status = nfs_error(EACCES);

  return &res;
}


voidp
nfsproc_writecache_2_svc(voidp argp, struct svc_req *rqstp)
{
  static char res;

  return (voidp) &res;
}


nfsattrstat *
nfsproc_write_2_svc(nfswriteargs *argp, struct svc_req *rqstp)
{
  static nfsattrstat res;

  if (!fh_to_mp(&argp->wra_fhandle))
    res.ns_status = nfs_error(ESTALE);
  else
    res.ns_status = nfs_error(EROFS);

  return &res;
}


nfsdiropres *
nfsproc_create_2_svc(nfscreateargs *argp, struct svc_req *rqstp)
{
  static nfsdiropres res;

  if (!fh_to_mp(&argp->ca_where.da_fhandle))
    res.dr_status = nfs_error(ESTALE);
  else
    res.dr_status = nfs_error(EROFS);

  return &res;
}


static nfsstat *
unlink_or_rmdir(nfsdiropargs *argp, struct svc_req *rqstp, int unlinkp)
{
  static nfsstat res;
  int retry;

  am_node *mp = fh_to_mp3(&argp->da_fhandle, &retry, VLOOK_DELETE);
  if (mp == NULL) {
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    res = nfs_error(retry);
    goto out;
  }

  if (mp->am_fattr.na_type != NFDIR) {
    res = nfs_error(ENOTDIR);
    goto out;
  }

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "\tremove(%s, %s)", mp->am_path, argp->da_name);

  mp = mp->am_al->al_mnt->mf_ops->lookup_child(mp, argp->da_name, &retry, VLOOK_DELETE);
  if (mp == NULL) {
    /*
     * Ignore retries...
     */
    if (retry < 0)
      retry = 0;
    /*
     * Usual NFS workaround...
     */
    else if (retry == ENOENT)
      retry = 0;
    res = nfs_error(retry);
  } else {
    forcibly_timeout_mp(mp);
    res = NFS_OK;
  }

out:
  return &res;
}


nfsstat *
nfsproc_remove_2_svc(nfsdiropargs *argp, struct svc_req *rqstp)
{
  return unlink_or_rmdir(argp, rqstp, TRUE);
}


nfsstat *
nfsproc_rename_2_svc(nfsrenameargs *argp, struct svc_req *rqstp)
{
  static nfsstat res;

  if (!fh_to_mp(&argp->rna_from.da_fhandle) || !fh_to_mp(&argp->rna_to.da_fhandle))
    res = nfs_error(ESTALE);
  /*
   * If the kernel is doing clever things with referenced files
   * then let it pretend...
   */
  else if (NSTREQ(argp->rna_to.da_name, ".nfs", 4))
    res = NFS_OK;
  /*
   * otherwise a failure
   */
  else
    res = nfs_error(EROFS);

  return &res;
}


nfsstat *
nfsproc_link_2_svc(nfslinkargs *argp, struct svc_req *rqstp)
{
  static nfsstat res;

  if (!fh_to_mp(&argp->la_fhandle) || !fh_to_mp(&argp->la_to.da_fhandle))
    res = nfs_error(ESTALE);
  else
    res = nfs_error(EROFS);

  return &res;
}


nfsstat *
nfsproc_symlink_2_svc(nfssymlinkargs *argp, struct svc_req *rqstp)
{
  static nfsstat res;

  if (!fh_to_mp(&argp->sla_from.da_fhandle))
    res = nfs_error(ESTALE);
  else
    res = nfs_error(EROFS);

  return &res;
}


nfsdiropres *
nfsproc_mkdir_2_svc(nfscreateargs *argp, struct svc_req *rqstp)
{
  static nfsdiropres res;

  if (!fh_to_mp(&argp->ca_where.da_fhandle))
    res.dr_status = nfs_error(ESTALE);
  else
    res.dr_status = nfs_error(EROFS);

  return &res;
}


nfsstat *
nfsproc_rmdir_2_svc(nfsdiropargs *argp, struct svc_req *rqstp)
{
  return unlink_or_rmdir(argp, rqstp, FALSE);
}


nfsreaddirres *
nfsproc_readdir_2_svc(nfsreaddirargs *argp, struct svc_req *rqstp)
{
  static nfsreaddirres res;
  static nfsentry e_res[MAX_READDIR_ENTRIES];
  am_node *mp;
  int retry;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "readdir:");

  mp = fh_to_mp3(&argp->rda_fhandle, &retry, VLOOK_CREATE);
  if (mp == NULL) {
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    res.rdr_status = nfs_error(retry);
  } else {
    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "\treaddir(%s)", mp->am_path);
    res.rdr_status = nfs_error((*mp->am_al->al_mnt->mf_ops->readdir)
			   (mp, argp->rda_cookie,
			    &res.rdr_u.rdr_reply_u, e_res, argp->rda_count));
    mp->am_stats.s_readdir++;
  }

  return &res;
}


nfsstatfsres *
nfsproc_statfs_2_svc(am_nfs_fh *argp, struct svc_req *rqstp)
{
  static nfsstatfsres res;
  am_node *mp;
  int retry;
  mntent_t mnt;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "statfs:");

  mp = fh_to_mp3(argp, &retry, VLOOK_CREATE);
  if (mp == NULL) {
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    res.sfr_status = nfs_error(retry);
  } else {
    nfsstatfsokres *fp;
    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "\tstat_fs(%s)", mp->am_path);

    /*
     * just return faked up file system information
     */
    fp = &res.sfr_u.sfr_reply_u;

    fp->sfrok_tsize = 1024;
    fp->sfrok_bsize = 1024;

    /* check if map is browsable and show_statfs_entries=yes  */
    if ((gopt.flags & CFM_SHOW_STATFS_ENTRIES) &&
	mp->am_al->al_mnt && mp->am_al->al_mnt->mf_mopts) {
      mnt.mnt_opts = mp->am_al->al_mnt->mf_mopts;
      if (amu_hasmntopt(&mnt, "browsable")) {
	count_map_entries(mp,
			  &fp->sfrok_blocks,
			  &fp->sfrok_bfree,
			  &fp->sfrok_bavail);
      }
    } else {
      fp->sfrok_blocks = 0; /* set to 1 if you don't want empty automounts */
      fp->sfrok_bfree = 0;
      fp->sfrok_bavail = 0;
    }

    res.sfr_status = NFS_OK;
    mp->am_stats.s_statfs++;
  }

  return &res;
}


/*
 * count how many total entries there are in a map, and how many
 * of them are in use.
 */
static void
count_map_entries(const am_node *mp, u_int *out_blocks, u_int *out_bfree, u_int *out_bavail)
{
  u_int blocks, bfree, bavail, i;
  mntfs *mf;
  mnt_map *mmp;
  kv *k;

  blocks = bfree = bavail = 0;
  if (!mp)
    goto out;
  mf = mp->am_al->al_mnt;
  if (!mf)
    goto out;
  mmp = (mnt_map *) mf->mf_private;
  if (!mmp)
    goto out;

  /* iterate over keys */
  for (i = 0; i < NKVHASH; i++) {
    for (k = mmp->kvhash[i]; k ; k = k->next) {
      if (!k->key)
	continue;
      blocks++;
      /*
       * XXX: Need to count how many are actively in use and recompute
       * bfree and bavail based on it.
       */
    }
  }

out:
  *out_blocks = blocks;
  *out_bfree = bfree;
  *out_bavail = bavail;
}

static am_node *
validate_ap(am_node *node, int *rp, u_int fhh_gen)
{
  am_node *ap = node;
  /*
   * Check the generation number in the node
   * matches the one from the kernel.  If not
   * then the old node has been timed out and
   * a new one allocated.
   */
  if (node != NULL && node->am_gen != fhh_gen)
    ap = NULL;

  /*
   * If it doesn't exists then drop the request
   */
  if (!ap)
    goto drop;

#if 0
  /*
   * If the node is hung then locate a new node
   * for it.  This implements the replicated filesystem
   * retries.
   */
  if (ap->am_al->al_mnt && FSRV_ISDOWN(ap->am_al->al_mnt->mf_server) && ap->am_parent) {
    int error;
    am_node *orig_ap = ap;

    dlog("%s: %s (%s) is hung: lookup alternative file server", __func__,
	 orig_ap->am_path, orig_ap->am_al->al_mnt->mf_info);

    /*
     * Update modify time of parent node.
     * With any luck the kernel will re-stat
     * the child node and get new information.
     */
    clocktime(&orig_ap->am_fattr.na_mtime);

    /*
     * Call the parent's lookup routine for an object
     * with the same name.  This may return -1 in error
     * if a mount is in progress.  In any case, if no
     * mount node is returned the error code is propagated
     * to the caller.
     */
    if (vop == VLOOK_CREATE) {
      ap = orig_ap->am_parent->am_al->al_mnt->mf_ops->lookup_child(orig_ap->am_parent, orig_ap->am_name, &error, vop);
      if (ap && error < 0)
	ap = orig_ap->am_parent->am_al->al_mnt->mf_ops->mount_child(ap, &error);
    } else {
      ap = NULL;
      error = ESTALE;
    }
    if (ap == 0) {
      if (error < 0 && amd_state == Finishing)
	error = ENOENT;
      *rp = error;
      return 0;
    }

    /*
     * Update last access to original node.  This
     * avoids timing it out and so sending ESTALE
     * back to the kernel.
     * XXX - Not sure we need this anymore (jsp, 90/10/6).
     */
    new_ttl(orig_ap);

  }
#endif /* 0 */

  /*
   * Disallow references to objects being unmounted, unless
   * they are automount points.
   */
  if (ap->am_al->al_mnt && (ap->am_al->al_mnt->mf_flags & MFF_UNMOUNTING) &&
      !(ap->am_flags & AMF_ROOT)) {
    if (amd_state == Finishing)
      *rp = ENOENT;
    else
      *rp = -1;
    return 0;
  }
  new_ttl(ap);

drop:
  if (!ap || !ap->am_al->al_mnt) {
    /*
     * If we are shutting down then it is likely
     * that this node has disappeared because of
     * a fast timeout.  To avoid things thrashing
     * just pretend it doesn't exist at all.  If
     * ESTALE is returned, some NFS clients just
     * keep retrying (stupid or what - if it's
     * stale now, what's it going to be in 5 minutes?)
     */
    if (amd_state == Finishing)
      *rp = ENOENT;
    else {
      *rp = ESTALE;
      amd_stats.d_stale++;
    }
  }

  return ap;
}

/*
 * Convert from file handle to automount node.
 */
static am_node *
fh_to_mp3(am_nfs_fh *fhp, int *rp, int vop)
{
  struct am_fh *fp = (struct am_fh *) fhp;
  am_node *ap = NULL;

  if (fp->u.s.fhh_type != 0) {
    /* New filehandle type */
    int len = sizeof(*fhp) - sizeof(fp->fhh_gen);
    char *path = xmalloc(len+1);
    /*
     * Because fhp is treated as a filehandle we use memcpy
     * instead of xstrlcpy.
     */
    memcpy(path, (char *) fp->u.fhh_path, len);
    path[len] = '\0';
    dlog("%s: new filehandle: %s", __func__, path);

    ap = path_to_exported_ap(path);
    XFREE(path);
  } else {
    dlog("%s: old filehandle: %d", __func__, fp->u.s.fhh_id);
    /*
     * Check process id matches
     * If it doesn't then it is probably
     * from an old kernel-cached filehandle
     * which is now out of date.
     */
    if (fp->u.s.fhh_pid != get_server_pid()) {
      dlog("%s: wrong pid %ld != my pid %ld", __func__,
	   (long) fp->u.s.fhh_pid, get_server_pid());
      goto done;
    }

    /*
     * Get hold of the supposed mount node
     */
    ap = get_exported_ap(fp->u.s.fhh_id);
  }
done:
  return validate_ap(ap, rp, fp->fhh_gen);
}

static am_node *
fh_to_mp(am_nfs_fh *fhp)
{
  int dummy;

  return fh_to_mp3(fhp, &dummy, VLOOK_CREATE);
}

static am_node *
fh3_to_mp3(am_nfs_fh3 *fhp, int *rp, int vop)
{
  struct am_fh3 *fp = (struct am_fh3 *) fhp->am_fh3_data;
  am_node *ap = NULL;

  if (fp->u.s.fhh_type != 0) {
    /* New filehandle type */
    int len = sizeof(*fp) - sizeof(fp->fhh_gen);
    char *path = xmalloc(len+1);
    /*
     * Because fhp is treated as a filehandle we use memcpy
     * instead of xstrlcpy.
     */
    memcpy(path, (char *) fp->u.fhh_path, len);
    path[len] = '\0';
    dlog("%s: new filehandle: %s", __func__, path);

    ap = path_to_exported_ap(path);
    XFREE(path);
  } else {
    dlog("%s: old filehandle: %d", __func__, fp->u.s.fhh_id);
    /*
     * Check process id matches
     * If it doesn't then it is probably
     * from an old kernel-cached filehandle
     * which is now out of date.
     */
    if (fp->u.s.fhh_pid != get_server_pid()) {
      dlog("%s: wrong pid %ld != my pid %ld", __func__,
	   (long) fp->u.s.fhh_pid, get_server_pid());
      goto done;
    }

    /*
     * Get hold of the supposed mount node
     */
    ap = get_exported_ap(fp->u.s.fhh_id);
  }
done:
  return validate_ap(ap, rp, fp->fhh_gen);
}

static am_node *
fh3_to_mp(am_nfs_fh3 *fhp)
{
  int dummy;

  return fh3_to_mp3(fhp, &dummy, VLOOK_CREATE);
}

/*
 * Convert from automount node to file handle.
 */
void
mp_to_fh(am_node *mp, am_nfs_fh *fhp)
{
  u_int pathlen;
  struct am_fh *fp = (struct am_fh *) fhp;

  memset((char *) fhp, 0, sizeof(am_nfs_fh));

  /* Store the generation number */
  fp->fhh_gen = mp->am_gen;

  pathlen = strlen(mp->am_path);
  if (pathlen <= sizeof(*fhp) - sizeof(fp->fhh_gen)) {
    /* dlog("mp_to_fh: new filehandle: %s", mp->am_path); */

    /*
     * Because fhp is treated as a filehandle we use memcpy instead of
     * xstrlcpy.
     */
    memcpy(fp->u.fhh_path, mp->am_path, pathlen); /* making a filehandle */
  } else {
    /*
     * Take the process id
     */
    fp->u.s.fhh_pid = get_server_pid();

    /*
     * ... the map number
     */
    fp->u.s.fhh_id = mp->am_mapno;

    /*
     * ... and the generation number (previously stored)
     * to make a "unique" triple that will never
     * be reallocated except across reboots (which doesn't matter)
     * or if we are unlucky enough to be given the same
     * pid as a previous amd (very unlikely).
     */
    /* dlog("mp_to_fh: old filehandle: %d", fp->u.s.fhh_id); */
  }
}
void
mp_to_fh3(am_node *mp, am_nfs_fh3 *fhp)
{
  u_int pathlen;
  struct am_fh3 *fp = (struct am_fh3 *) fhp->am_fh3_data;

  memset((char *) fhp, 0, sizeof(am_nfs_fh3));
  fhp->am_fh3_length = AM_FHSIZE3;

  /* Store the generation number */
  fp->fhh_gen = mp->am_gen;

  pathlen = strlen(mp->am_path);
  if (pathlen <= sizeof(*fp) - sizeof(fp->fhh_gen)) {
    /* dlog("mp_to_fh: new filehandle: %s", mp->am_path); */

    /*
     * Because fhp is treated as a filehandle we use memcpy instead of
     * xstrlcpy.
     */
    memcpy(fp->u.fhh_path, mp->am_path, pathlen); /* making a filehandle */
  } else {
    /*
     * Take the process id
     */
    fp->u.s.fhh_pid = get_server_pid();

    /*
     * ... the map number
     */
    fp->u.s.fhh_id = mp->am_mapno;

    /*
     * ... and the generation number (previously stored)
     * to make a "unique" triple that will never
     * be reallocated except across reboots (which doesn't matter)
     * or if we are unlucky enough to be given the same
     * pid as a previous amd (very unlikely).
     */
    /* dlog("mp_to_fh: old filehandle: %d", fp->u.s.fhh_id); */
  }
}

#ifdef HAVE_FS_NFS3
static am_ftype3 ftype_to_ftype3(nfsftype ftype)
{
  if (ftype == NFFIFO)
    return AM_NF3FIFO;
  else
    return ftype;
}

static void nfstime_to_am_nfstime3(nfstime *time, am_nfstime3 *time3)
{
  time3->seconds = time->seconds;
  time3->nseconds = time->useconds * 1000;
}

static void rdev_to_am_specdata3(u_int rdev, am_specdata3 *rdev3)
{
  /* No device node here */
  rdev3->specdata1 = (u_int) -1;
  rdev3->specdata2 = (u_int) -1;
}

static void fattr_to_fattr3(nfsfattr *fattr, am_fattr3 *fattr3)
{
  fattr3->type = ftype_to_ftype3(fattr->na_type);
  fattr3->mode = (am_mode3) fattr->na_mode;
  fattr3->nlink = fattr->na_nlink;
  fattr3->uid = (am_uid3) fattr->na_uid;
  fattr3->gid = (am_uid3) fattr->na_gid;
  fattr3->size = (am_size3) fattr->na_size;
  fattr3->used = (am_size3) fattr->na_size;
  rdev_to_am_specdata3(fattr->na_rdev, &fattr3->rdev);
  fattr3->fsid = (uint64) fattr->na_fsid;
  fattr3->fileid = (uint64) fattr->na_fileid;
  nfstime_to_am_nfstime3(&fattr->na_atime, &fattr3->atime);
  nfstime_to_am_nfstime3(&fattr->na_mtime, &fattr3->mtime);
  nfstime_to_am_nfstime3(&fattr->na_ctime, &fattr3->ctime);
}

static void fattr_to_wcc_attr(nfsfattr *fattr, am_wcc_attr *wcc_attr)
{
  wcc_attr->size = (am_size3) fattr->na_size;
  nfstime_to_am_nfstime3(&fattr->na_mtime, &wcc_attr->mtime);
  nfstime_to_am_nfstime3(&fattr->na_ctime, &wcc_attr->ctime);
}

static am_nfsstat3 return_estale_or_rofs(am_nfs_fh3 *fh,
                                         am_pre_op_attr *pre_op,
                                         am_post_op_attr *post_op)
{
  am_node *mp;

  mp = fh3_to_mp(fh);
  if (!mp) {
    pre_op->attributes_follow = 0;
    post_op->attributes_follow = 0;
    return  nfs_error(ESTALE);
  } else {
    am_fattr3 *fattr3 = &post_op->am_post_op_attr_u.attributes;
    am_wcc_attr *wcc_attr = &pre_op->am_pre_op_attr_u.attributes;
    nfsfattr *fattr = &mp->am_fattr;
    pre_op->attributes_follow = 1;
    fattr_to_wcc_attr(fattr, wcc_attr);
    post_op->attributes_follow = 1;
    fattr_to_fattr3(fattr, fattr3);
    return nfs_error(EROFS);
  }
}

static am_nfsstat3 unlink3_or_rmdir3(am_diropargs3 *argp,
                                     am_wcc_data *wcc_data, int unlinkp)
{
  static am_nfsstat3 res;
  am_nfs_fh3 *dir = &argp->dir;
  am_filename3 name = argp->name;
  am_pre_op_attr *pre_op_dir = &wcc_data->before;
  am_post_op_attr *post_op_dir = &wcc_data->after;
  nfsfattr *fattr;
  am_wcc_attr *wcc_attr;
  am_node *mp, *ap;
  int retry;

  post_op_dir->attributes_follow = 0;

  mp = fh3_to_mp3(dir, &retry, VLOOK_DELETE);
  if (!mp) {
    pre_op_dir->attributes_follow = 0;
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    res = nfs_error(retry);
    goto out;
  }

  pre_op_dir->attributes_follow = 1;
  fattr = &mp->am_fattr;
  wcc_attr = &pre_op_dir->am_pre_op_attr_u.attributes;
  fattr_to_wcc_attr(fattr, wcc_attr);

  if (mp->am_fattr.na_type != NFDIR) {
    res = nfs_error(ENOTDIR);
    goto out;
  }

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "\tremove(%s, %s)", mp->am_path, name);

  ap = mp->am_al->al_mnt->mf_ops->lookup_child(mp, name, &retry, VLOOK_DELETE);
  if (!ap) {
    /*
     * Ignore retries...
     */
    if (retry < 0)
      retry = 0;
    /*
     * Usual NFS workaround...
     */
    else if (retry == ENOENT)
      retry = 0;
    res = nfs_error(retry);
  } else {
    forcibly_timeout_mp(mp);
    res = AM_NFS3_OK;
  }

out:
  return res;
}

voidp
am_nfs3_null_3_svc(voidp argp, struct svc_req *rqstp)
{
  static char * result;

  return (voidp) &result;
}

am_GETATTR3res *
am_nfs3_getattr_3_svc(am_GETATTR3args *argp, struct svc_req *rqstp)
{
  static am_GETATTR3res  result;
  am_nfs_fh3 *fh = (am_nfs_fh3 *) &argp->object;
  am_fattr3 *fattr3;
  nfsfattr *fattr;
  am_node *mp;
  int retry = 0;
  time_t now = clocktime(NULL);

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "getattr_3:");

  mp = fh3_to_mp3(fh, &retry, VLOOK_CREATE);
  if (!mp) {
    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "\tretry=%d", retry);

    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    result.status = nfs_error(retry);
    return &result;
  }

  fattr = &mp->am_fattr;
  fattr3 = (am_fattr3 *) &result.res_u.ok.obj_attributes;
  fattr_to_fattr3(fattr, fattr3);

  result.status = AM_NFS3_OK;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "\tstat(%s), size = %lu, mtime=%d.%d",
	 mp->am_path,
	 (u_long) fattr3->size,
	 (u_int) fattr3->mtime.seconds,
	 (u_int) fattr3->mtime.nseconds);

  /* Delay unmount of what was looked up */
  if (mp->am_timeo_w < 4 * gopt.am_timeo_w)
    mp->am_timeo_w += gopt.am_timeo_w;
  mp->am_ttl = now + mp->am_timeo_w;

  mp->am_stats.s_getattr++;

  return &result;
}

am_SETATTR3res *
am_nfs3_setattr_3_svc(am_SETATTR3args *argp, struct svc_req *rqstp)
{
  static am_SETATTR3res  result;
  am_nfs_fh3 *fh = (am_nfs_fh3 *) &argp->object;
  am_pre_op_attr *pre_op_obj = &result.res_u.fail.obj_wcc.before;
  am_post_op_attr *post_op_obj = &result.res_u.fail.obj_wcc.after;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "setattr_3:");

  result.status = return_estale_or_rofs(fh, pre_op_obj, post_op_obj);

  return &result;
}

am_LOOKUP3res *
am_nfs3_lookup_3_svc(am_LOOKUP3args *argp, struct svc_req *rqstp)
{
  static am_LOOKUP3res  result;
  am_nfs_fh3 *dir = &argp->what.dir;
  am_post_op_attr *post_op_dir;
  am_post_op_attr *post_op_obj;
  am_node *mp;
  int retry;
  uid_t uid;
  gid_t gid;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "lookup_3:");

  /* finally, find the effective uid/gid from RPC request */
  if (getcreds(rqstp, &uid, &gid, nfsxprt) < 0)
    plog(XLOG_ERROR, "cannot get uid/gid from RPC credentials");
  xsnprintf(opt_uid, sizeof(uid_str), "%d", (int) uid);
  xsnprintf(opt_gid, sizeof(gid_str), "%d", (int) gid);

  mp = fh3_to_mp3(dir, &retry, VLOOK_CREATE);
  if (!mp) {
    post_op_dir = &result.res_u.fail.dir_attributes;
    post_op_dir->attributes_follow = 0;
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    result.status = nfs_error(retry);
  } else {
    post_op_dir = &result.res_u.ok.dir_attributes;
    post_op_obj = &result.res_u.ok.obj_attributes;
    am_filename3 name;
    am_fattr3 *fattr3;
    nfsfattr *fattr;
    am_node *ap;
    int error;

    /* dir attributes */
    post_op_dir->attributes_follow = 1;
    fattr = &mp->am_fattr;
    fattr3 = &post_op_dir->am_post_op_attr_u.attributes;
    fattr_to_fattr3(fattr, fattr3);

    post_op_obj->attributes_follow = 0;

    name = argp->what.name;

    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "\tlookup_3(%s, %s)", mp->am_path, name);

    ap = mp->am_al->al_mnt->mf_ops->lookup_child(mp, name, &error, VLOOK_CREATE);
    if (ap && error < 0)
      ap = mp->am_al->al_mnt->mf_ops->mount_child(ap, &error);
    if (ap == 0) {
      if (error < 0) {
	amd_stats.d_drops++;
	return 0;
      }
      result.status = nfs_error(error);
    } else {
      /*
       * XXX: EXPERIMENTAL! Delay unmount of what was looked up.  This
       * should reduce the chance for race condition between unmounting an
       * entry synchronously, and re-mounting it asynchronously.
       */
      if (ap->am_ttl < mp->am_ttl)
        ap->am_ttl = mp->am_ttl;

      mp_to_fh3(ap, &result.res_u.ok.object);

      /* mount attributes */
      post_op_obj->attributes_follow = 1;
      fattr = &ap->am_fattr;
      fattr3 = &post_op_obj->am_post_op_attr_u.attributes;
      fattr_to_fattr3(fattr, fattr3);

      result.status = AM_NFS3_OK;
    }
    mp->am_stats.s_lookup++;
  }
  return &result;
}

am_ACCESS3res *
am_nfs3_access_3_svc(am_ACCESS3args *argp, struct svc_req *rqstp)
{
  static am_ACCESS3res  result;

  am_nfs_fh3 *obj = &argp->object;
  u_int accessbits = argp->access;
  u_int accessmask = AM_ACCESS3_LOOKUP|AM_ACCESS3_READ;
  am_post_op_attr *post_op_obj;
  am_node *mp;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "access_3:");

  mp = fh3_to_mp(obj);
  if (!mp) {
    post_op_obj = &result.res_u.fail.obj_attributes;
    post_op_obj->attributes_follow = 0;
    result.status = nfs_error(ENOENT);
    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "access_3: ENOENT");
  } else {
    nfsfattr *fattr = &mp->am_fattr;
    am_fattr3 *fattr3;
    post_op_obj = &result.res_u.ok.obj_attributes;
    fattr3 = &post_op_obj->am_post_op_attr_u.attributes;
    post_op_obj->attributes_follow = 1;
    fattr_to_fattr3(fattr, fattr3);

    result.res_u.ok.access = accessbits & accessmask;
    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "access_3: b=%x m=%x", accessbits, accessmask);

    result.status = AM_NFS3_OK;
  }

  return &result;
}

am_READLINK3res *
am_nfs3_readlink_3_svc(am_READLINK3args *argp, struct svc_req *rqstp)
{
  static am_READLINK3res  result;

  am_nfs_fh3 *symlink = (am_nfs_fh3 *) &argp->symlink;
  am_post_op_attr *post_op_sym;
  am_node *mp;
  int retry = 0;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "readlink_3:");

  mp = fh3_to_mp3(symlink, &retry, VLOOK_CREATE);
  if (!mp) {
  readlink_retry:
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    post_op_sym = &result.res_u.fail.symlink_attributes;
    post_op_sym->attributes_follow = 0;
    result.status = nfs_error(retry);
  } else {
    nfsfattr *fattr;
    am_fattr3 *fattr3;
    char *ln;

    ln = do_readlink(mp, &retry);
    if (!ln)
      goto readlink_retry;

    if (amuDebug(D_TRACE) && ln)
      plog(XLOG_DEBUG, "\treadlink_3(%s) = %s", mp->am_path, ln);

    result.res_u.ok.data = ln;

    post_op_sym = &result.res_u.ok.symlink_attributes;
    post_op_sym->attributes_follow = 1;
    fattr = &mp->am_fattr;
    fattr3 = &post_op_sym->am_post_op_attr_u.attributes;
    fattr_to_fattr3(fattr, fattr3);

    mp->am_stats.s_readlink++;
    result.status = AM_NFS3_OK;
  }

  return &result;
}

am_READ3res *
am_nfs3_read_3_svc(am_READ3args *argp, struct svc_req *rqstp)
{
  static am_READ3res  result;

  am_nfs_fh3 *file = (am_nfs_fh3 *) &argp->file;
  am_post_op_attr *post_op_file;
  am_node *mp;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "read_3:");

  post_op_file = &result.res_u.fail.file_attributes;
  result.status = nfs_error(EACCES);

  mp = fh3_to_mp(file);
  if (!mp)
    post_op_file->attributes_follow = 0;
  else {
    nfsfattr *fattr = &mp->am_fattr;
    am_fattr3 *fattr3 = &post_op_file->am_post_op_attr_u.attributes;
    post_op_file->attributes_follow = 1;
    fattr_to_fattr3(fattr, fattr3);
  }

  return &result;
}

am_WRITE3res *
am_nfs3_write_3_svc(am_WRITE3args *argp, struct svc_req *rqstp)
{
  static am_WRITE3res  result;

  am_nfs_fh3 *file = (am_nfs_fh3 *) &argp->file;
  am_pre_op_attr *pre_op_file = &result.res_u.fail.file_wcc.before;
  am_post_op_attr *post_op_file = &result.res_u.fail.file_wcc.after;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "write_3:");

  result.status = return_estale_or_rofs(file, pre_op_file, post_op_file);

  return &result;
}

am_CREATE3res *
am_nfs3_create_3_svc(am_CREATE3args *argp, struct svc_req *rqstp)
{
  static am_CREATE3res  result;

  am_nfs_fh3 *dir = (am_nfs_fh3 *) &argp->where.dir;
  am_pre_op_attr *pre_op_dir = &result.res_u.fail.dir_wcc.before;
  am_post_op_attr *post_op_dir = &result.res_u.fail.dir_wcc.after;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "create_3:");

  result.status = return_estale_or_rofs(dir, pre_op_dir, post_op_dir);

  return &result;
}

am_MKDIR3res *
am_nfs3_mkdir_3_svc(am_MKDIR3args *argp, struct svc_req *rqstp)
{
  static am_MKDIR3res  result;

  am_nfs_fh3 *dir = (am_nfs_fh3 *) &argp->where.dir;
  am_pre_op_attr *pre_op_dir = &result.res_u.fail.dir_wcc.before;
  am_post_op_attr *post_op_dir = &result.res_u.fail.dir_wcc.after;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "mkdir_3:");

  result.status = return_estale_or_rofs(dir, pre_op_dir, post_op_dir);

  return &result;
}

am_SYMLINK3res *
am_nfs3_symlink_3_svc(am_SYMLINK3args *argp, struct svc_req *rqstp)
{
  static am_SYMLINK3res  result;

  am_nfs_fh3 *dir = (am_nfs_fh3 *) &argp->where.dir;
  am_pre_op_attr *pre_op_dir = &result.res_u.fail.dir_wcc.before;
  am_post_op_attr *post_op_dir = &result.res_u.fail.dir_wcc.after;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "symlink_3:");

  result.status = return_estale_or_rofs(dir, pre_op_dir, post_op_dir);

  return &result;
}

am_MKNOD3res *
am_nfs3_mknod_3_svc(am_MKNOD3args *argp, struct svc_req *rqstp)
{
  static am_MKNOD3res  result;

  am_nfs_fh3 *dir = (am_nfs_fh3 *) &argp->where.dir;
  am_pre_op_attr *pre_op_dir = &result.res_u.fail.dir_wcc.before;
  am_post_op_attr *post_op_dir =  &result.res_u.fail.dir_wcc.after;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "mknod_3:");

  result.status = return_estale_or_rofs(dir, pre_op_dir, post_op_dir);
  return &result;
}

am_REMOVE3res *
am_nfs3_remove_3_svc(am_REMOVE3args *argp, struct svc_req *rqstp)
{
  static am_REMOVE3res  result;

  am_diropargs3 *obj = &argp->object;
  am_wcc_data dir_wcc;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "remove_3:");

  result.status = unlink3_or_rmdir3(obj, &dir_wcc, TRUE);

  result.res_u.ok.dir_wcc = dir_wcc;
 
  return &result;
}

am_RMDIR3res *
am_nfs3_rmdir_3_svc(am_RMDIR3args *argp, struct svc_req *rqstp)
{
  static am_RMDIR3res  result;

  am_diropargs3 *obj = &argp->object;
  am_wcc_data dir_wcc;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "rmdir_3:");

  result.status = unlink3_or_rmdir3(obj, &dir_wcc, TRUE);

  result.res_u.ok.dir_wcc = dir_wcc;
 
  return &result;
}

am_RENAME3res *
am_nfs3_rename_3_svc(am_RENAME3args *argp, struct svc_req *rqstp)
{
  static am_RENAME3res  result;

  am_nfs_fh3 *fromdir = (am_nfs_fh3 *) &argp->from.dir;
  am_nfs_fh3 *todir = (am_nfs_fh3 *) &argp->to.dir;
  am_filename3 name = argp->to.name;
  am_node *to_mp, *from_mp;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "rename_3:");

  if (!(from_mp = fh3_to_mp(fromdir)) || !(to_mp = fh3_to_mp(todir)))
    result.status = nfs_error(ESTALE);
  /*
   * If the kernel is doing clever things with referenced files
   * then let it pretend...
   */
  else {
    am_wcc_attr *wcc_attr;
    am_fattr3 *fattr3;
    am_wcc_data *to_wcc_data, *from_wcc_data;
    am_pre_op_attr *pre_op_to, *pre_op_from;
    am_post_op_attr *post_op_to, *post_op_from;
    nfsfattr *fattr;

    to_wcc_data = &result.res_u.ok.todir_wcc;

    pre_op_to = &to_wcc_data->before;
    post_op_to = &to_wcc_data->after;

    pre_op_to->attributes_follow = 1;
    fattr = &to_mp->am_fattr;
    wcc_attr = &pre_op_to->am_pre_op_attr_u.attributes;
    fattr_to_wcc_attr(fattr, wcc_attr);
    post_op_to->attributes_follow = 1;
    fattr3 = &post_op_to->am_post_op_attr_u.attributes;
    fattr_to_fattr3(fattr, fattr3);

    from_wcc_data = &result.res_u.ok.fromdir_wcc;

    pre_op_from = &from_wcc_data->before;
    post_op_from = &from_wcc_data->after;

    pre_op_from->attributes_follow = 1;
    fattr = &from_mp->am_fattr;
    wcc_attr = &pre_op_from->am_pre_op_attr_u.attributes;
    fattr_to_wcc_attr(fattr, wcc_attr);
    post_op_from->attributes_follow = 1;
    fattr3 = &post_op_from->am_post_op_attr_u.attributes;
    fattr_to_fattr3(fattr, fattr3);

    if (NSTREQ(name, ".nfs", 4))
      result.status = AM_NFS3_OK;
    /*
     * otherwise a failure
     */
    else
      result.status = nfs_error(EROFS);
  }

  return &result;
}

am_LINK3res *
am_nfs3_link_3_svc(am_LINK3args *argp, struct svc_req *rqstp)
{
  static am_LINK3res  result;

  am_nfs_fh3 *file = (am_nfs_fh3 *) &argp->file;
  am_nfs_fh3 *dir = (am_nfs_fh3 *) &argp->link.dir;
  am_post_op_attr *post_op_file;
  am_pre_op_attr *pre_op_dir;
  am_post_op_attr *post_op_dir;
  am_node *mp_file, *mp_dir;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "link_3:");

  post_op_file = &result.res_u.fail.file_attributes;
  post_op_file->attributes_follow = 0;

  mp_file = fh3_to_mp(file);
  if (mp_file) {
    nfsfattr *fattr = &mp_file->am_fattr;
    am_fattr3 *fattr3 = &post_op_file->am_post_op_attr_u.attributes;
    fattr_to_fattr3(fattr, fattr3);
  }

  pre_op_dir = &result.res_u.fail.linkdir_wcc.before;
  pre_op_dir->attributes_follow = 0;
  post_op_dir = &result.res_u.fail.linkdir_wcc.after;
  post_op_dir->attributes_follow = 0;

  mp_dir = fh3_to_mp(dir);
  if (mp_dir) {
    nfsfattr *fattr = &mp_dir->am_fattr;
    am_fattr3 *fattr3 = &post_op_dir->am_post_op_attr_u.attributes;
    am_wcc_attr *wcc_attr = &pre_op_dir->am_pre_op_attr_u.attributes;

    pre_op_dir->attributes_follow = 1;
    fattr_to_wcc_attr(fattr, wcc_attr);
    post_op_dir->attributes_follow = 1;
    fattr_to_fattr3(fattr, fattr3);
  }

  if (!mp_file || !mp_dir)
    result.status = nfs_error(ESTALE);
  else
    result.status = nfs_error(EROFS);

  return &result;
}

am_READDIR3res *
am_nfs3_readdir_3_svc(am_READDIR3args *argp, struct svc_req *rqstp)
{
  static am_READDIR3res  result;
  static am_entry3 entries[MAX_READDIR_ENTRIES];
  am_nfs_fh3 *dir = (am_nfs_fh3 *) &argp->dir;
  am_cookie3 cookie = argp->cookie;
  am_cookieverf3 cookieverf;
  am_count3 count = argp->count;
  am_post_op_attr *post_op_dir;
  am_node *mp;
  int retry;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "readdir_3:");

  memcpy(&cookieverf, &argp->cookieverf, sizeof(am_cookieverf3));

  mp = fh3_to_mp3(dir, &retry, VLOOK_CREATE);
  if (mp == NULL) {
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    post_op_dir = &result.res_u.fail.dir_attributes;
    post_op_dir->attributes_follow = 0;
    result.status = nfs_error(retry);
  } else {
    am_dirlist3 *list = &result.res_u.ok.reply;
    am_nfsstat3 status;

    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "\treaddir_3(%s)", mp->am_path);

    status = mp->am_al->al_mnt->mf_ops->readdir(mp,
					(voidp)&cookie, list, entries, count);
    if (status == 0) {
      post_op_dir = &result.res_u.ok.dir_attributes;
      nfsfattr *fattr;
      am_fattr3 *fattr3;

      fattr = &mp->am_fattr;
      fattr3 = &post_op_dir->am_post_op_attr_u.attributes;
      post_op_dir->attributes_follow = 1;
      fattr_to_fattr3(fattr, fattr3);
      result.status = AM_NFS3_OK;
    } else {
      post_op_dir = &result.res_u.fail.dir_attributes;
      post_op_dir->attributes_follow = 0;
      result.status = nfs_error(status);
    }

    mp->am_stats.s_readdir++;
  }

  return &result;
}

am_READDIRPLUS3res *
am_nfs3_readdirplus_3_svc(am_READDIRPLUS3args *argp, struct svc_req *rqstp)
{
  static am_READDIRPLUS3res  result;
  am_nfs_fh3 *dir = (am_nfs_fh3 *) &argp->dir;
  am_post_op_attr *post_op_dir;
  nfsfattr *fattr;
  am_fattr3 *fattr3;
  am_node *mp;
  int retry;

  mp = fh3_to_mp3(dir, &retry, VLOOK_CREATE);
  if (mp == NULL) {
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    post_op_dir = &result.res_u.fail.dir_attributes;
    post_op_dir->attributes_follow = 0;
    result.status = nfs_error(retry);
  } else {
      post_op_dir = &result.res_u.ok.dir_attributes;
      fattr = &mp->am_fattr;
      fattr3 = &post_op_dir->am_post_op_attr_u.attributes;
      post_op_dir->attributes_follow = 1;
      fattr_to_fattr3(fattr, fattr3);
      result.status = AM_NFS3ERR_NOTSUPP;
  }

  return &result;
}

am_FSSTAT3res *
am_nfs3_fsstat_3_svc(am_FSSTAT3args *argp, struct svc_req *rqstp)
{
  static am_FSSTAT3res  result;

  am_nfs_fh3 *fsroot = (am_nfs_fh3 *) &argp->fsroot;
  am_post_op_attr *post_op_fsroot;
  am_node *mp;
  int retry;
 
  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "fsstat_3:");

  mp = fh3_to_mp3(fsroot, &retry, VLOOK_CREATE);
  if (!mp) {
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    post_op_fsroot = &result.res_u.fail.obj_attributes;
    post_op_fsroot->attributes_follow = 0;
    result.status = nfs_error(retry);
  } else {
    am_FSSTAT3resok *ok = &result.res_u.ok;
    u_int blocks, bfree, bavail;
    nfsfattr *fattr;
    am_fattr3 *fattr3;
    mntent_t mnt;

    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "\tfsstat_3(%s)", mp->am_path);

    fattr = &mp->am_fattr;
    post_op_fsroot = &ok->obj_attributes;
    post_op_fsroot->attributes_follow = 1;
    fattr3 = &post_op_fsroot->am_post_op_attr_u.attributes;
    fattr_to_fattr3(fattr, fattr3);

    /*
     * just return faked up file system information
     */
    ok->tbytes = 1024;
    ok->invarsec = 0;

    /* check if map is browsable and show_statfs_entries=yes  */
    if ((gopt.flags & CFM_SHOW_STATFS_ENTRIES) &&
	mp->am_al->al_mnt && mp->am_al->al_mnt->mf_mopts) {
      mnt.mnt_opts = mp->am_al->al_mnt->mf_mopts;
      if (amu_hasmntopt(&mnt, "browsable")) {
	count_map_entries(mp, &blocks, &bfree, &bavail);
      }
      ok->fbytes = bfree;
      ok->abytes = bavail;
      ok->ffiles = bfree;
      ok->afiles = bavail;
      ok->tfiles = blocks;
    } else {
      ok->fbytes = 0;
      ok->abytes = 0;
      ok->ffiles = 0;
      ok->afiles = 0;
      ok->tfiles = 0; /* set to 1 if you don't want empty automounts */
    }

    result.status = AM_NFS3_OK;
    mp->am_stats.s_statfs++;
  }

  return &result;
}

#define FSF3_HOMOGENEOUS 0x0008

am_FSINFO3res *
am_nfs3_fsinfo_3_svc(am_FSINFO3args *argp, struct svc_req *rqstp)
{
  static am_FSINFO3res  result;

  am_nfs_fh3 *fsroot = (am_nfs_fh3 *) &argp->fsroot;
  am_post_op_attr *post_op_fsroot;
  am_node *mp;
  int retry;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "fsinfo_3:");

  mp = fh3_to_mp3(fsroot, &retry, VLOOK_CREATE);
  if (!mp) {
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    post_op_fsroot = &result.res_u.fail.obj_attributes;
    post_op_fsroot->attributes_follow = 0;
    result.status = nfs_error(retry);
  } else {
    am_FSINFO3resok *ok = &result.res_u.ok;
    nfsfattr *fattr;
    am_fattr3 *fattr3;

    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "\tfsinfo_3(%s)", mp->am_path);

    fattr = &mp->am_fattr;
    post_op_fsroot = &ok->obj_attributes;
    post_op_fsroot->attributes_follow = 1;
    fattr3 = &post_op_fsroot->am_post_op_attr_u.attributes;
    fattr_to_fattr3(fattr, fattr3);

    /*
     * just return faked up file system information
     */
    ok->rtmax = 0;
    ok->rtpref = 0;
    ok->rtmult = 0;
    ok->wtmax = 0;
    ok->wtpref = 0;
    ok->wtmult = 0;
    ok->dtpref = 1024;
    ok->maxfilesize = 0;
    ok->time_delta.seconds = 1;
    ok->time_delta.nseconds = 0;
    ok->properties = FSF3_HOMOGENEOUS;

    result.status = AM_NFS3_OK;
    mp->am_stats.s_fsinfo++;
  }

  return &result;
}

am_PATHCONF3res *
am_nfs3_pathconf_3_svc(am_PATHCONF3args *argp, struct svc_req *rqstp)
{
  static am_PATHCONF3res  result;

  am_nfs_fh3 *obj = (am_nfs_fh3 *) &argp->object;
  am_post_op_attr *post_op_obj;
  am_node *mp;
  int retry;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "pathconf_3:");

  mp = fh3_to_mp3(obj, &retry, VLOOK_CREATE);
  if (!mp) {
    if (retry < 0) {
      amd_stats.d_drops++;
      return 0;
    }
    post_op_obj = &result.res_u.fail.obj_attributes;
    post_op_obj->attributes_follow = 0;
    result.status = nfs_error(retry);
  } else {
    am_PATHCONF3resok *ok = &result.res_u.ok;
    nfsfattr *fattr;
    am_fattr3 *fattr3;

    if (amuDebug(D_TRACE))
      plog(XLOG_DEBUG, "\tpathconf_3(%s)", mp->am_path);

    fattr = &mp->am_fattr;
    post_op_obj = &ok->obj_attributes;
    post_op_obj->attributes_follow = 1;
    fattr3 = &post_op_obj->am_post_op_attr_u.attributes;
    fattr_to_fattr3(fattr, fattr3);

    ok->linkmax = 0;
    ok->name_max = NAME_MAX;
    ok->no_trunc = 1;
    ok->chown_restricted = 1;
    ok->case_insensitive = 0;
    ok->case_preserving = 1;

    result.status = AM_NFS3_OK;
    mp->am_stats.s_pathconf++;
  }

  return &result;
}

am_COMMIT3res *
am_nfs3_commit_3_svc(am_COMMIT3args *argp, struct svc_req *rqstp)
{
  static am_COMMIT3res  result;

  am_nfs_fh3 *file = (am_nfs_fh3 *) &argp->file;
  am_pre_op_attr *pre_op_file = &result.res_u.fail.file_wcc.before;
  am_post_op_attr *post_op_file = &result.res_u.fail.file_wcc.after;

  if (amuDebug(D_TRACE))
    plog(XLOG_DEBUG, "commit_3:");

  result.status = return_estale_or_rofs(file, pre_op_file, post_op_file);

  return &result;
}
#endif /* HAVE_FS_NFS3 */
