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
 * File: am-utils/amd/amq_subr.c
 *
 */
/*
 * Auxiliary routines for amq tool
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* forward definitions */
bool_t xdr_amq_mount_tree_node(XDR *xdrs, amq_mount_tree *objp);
bool_t xdr_amq_mount_subtree(XDR *xdrs, amq_mount_tree *objp);


voidp
amqproc_null_1_svc(voidp argp, struct svc_req *rqstp)
{
  static char res;

  return (voidp) &res;
}


/*
 * Return a sub-tree of mounts
 */
amq_mount_tree_p *
amqproc_mnttree_1_svc(voidp argp, struct svc_req *rqstp)
{
  static am_node *mp;

  mp = find_ap(*(char **) argp);
  return (amq_mount_tree_p *) ((void *)&mp);
}


/*
 * Unmount a single node
 */
int *
amqproc_umnt_1_svc(voidp argp, struct svc_req *rqstp)
{
  static int res = AMQ_UMNT_OK;
  am_node *mp = find_ap(*(char **) argp);

  if (mp)
    forcibly_timeout_mp(mp);

  return &res;
}


/*
 * Synchronously unmount a single node - parent side.
 */
int *
amqproc_sync_umnt_1_svc_parent(voidp argp, struct svc_req *rqstp)
{
  amqproc_umnt_1_svc(argp, rqstp);
  return NULL;
}


/*
 * Synchronously unmount a single node - child side.
 */
amq_sync_umnt *
amqproc_sync_umnt_1_svc_child(voidp argp, struct svc_req *rqstp)
{
  static amq_sync_umnt rv;
  amq_sync_umnt buf;
  ssize_t n;

  am_node *mp = find_ap(*(char **) argp);

  memset(&rv, 0, sizeof(rv));
  rv.au_etype = AMQ_UMNT_READ;
  if (mp && mp->am_fd[0] >= 0) {
    n = read(mp->am_fd[0], &buf, sizeof(buf));
    if (n == sizeof(buf))
      rv = buf;
  }
  return &rv;
}


/*
 * Synchronously unmount a single node - use if we can't fork (asynchronous).
 */
amq_sync_umnt *
amqproc_sync_umnt_1_svc_async(voidp argp, struct svc_req *rqstp)
{
  static amq_sync_umnt rv;

  memset(&rv, 0, sizeof(rv));
  rv.au_etype = AMQ_UMNT_FORK;
  rv.au_errno = errno;

  amqproc_umnt_1_svc(argp, rqstp);

  return &rv;
}


/*
 * Return global statistics
 */
amq_mount_stats *
amqproc_stats_1_svc(voidp argp, struct svc_req *rqstp)
{
  return (amq_mount_stats *) ((void *)&amd_stats);
}


/*
 * Return the entire tree of mount nodes
 */
amq_mount_tree_list *
amqproc_export_1_svc(voidp argp, struct svc_req *rqstp)
{
  static amq_mount_tree_list aml;
  static am_node *mp;

  mp = get_exported_ap(0);
  aml.amq_mount_tree_list_val = (amq_mount_tree_p *) ((void *) &mp);
  aml.amq_mount_tree_list_len = 1;	/* XXX */

  return &aml;
}


int *
amqproc_setopt_1_svc(voidp argp, struct svc_req *rqstp)
{
  static int rc;
  amq_setopt *opt = (amq_setopt *) argp;

  rc = 0;

  switch (opt->as_opt) {

  case AMOPT_DEBUG:
    if (debug_option(opt->as_str))
      rc = EINVAL;
    break;

  case AMOPT_LOGFILE:
    if (gopt.logfile && opt->as_str
	&& STREQ(gopt.logfile, opt->as_str)) {
      if (switch_to_logfile(opt->as_str, orig_umask, 0))
	rc = EINVAL;
    } else {
      rc = EACCES;
    }
    break;

  case AMOPT_XLOG:
    if (switch_option(opt->as_str))
      rc = EINVAL;
    break;

  case AMOPT_FLUSHMAPC:
    if (amd_state == Run) {
      plog(XLOG_INFO, "amq says flush cache");
      do_mapc_reload = 0;
      flush_nfs_fhandle_cache((fserver *) NULL);
      flush_srvr_nfs_cache((fserver *) NULL);
    }
    break;
  }

  return &rc;
}


amq_mount_info_list *
amqproc_getmntfs_1_svc(voidp argp, struct svc_req *rqstp)
{
  return (amq_mount_info_list *) ((void *)&mfhead);	/* XXX */
}

extern qelem map_list_head;
amq_map_info_list *
amqproc_getmapinfo_1_svc(voidp argp, struct svc_req *rqstp)
{
  return (amq_map_info_list *) ((void *)&map_list_head);	/* XXX */
}

amq_string *
amqproc_getvers_1_svc(voidp argp, struct svc_req *rqstp)
{
  static amq_string res;

  res = get_version_string();
  return &res;
}


/* get PID of remote amd */
int *
amqproc_getpid_1_svc(voidp argp, struct svc_req *rqstp)
{
  static int res;

  res = getpid();
  return &res;
}


/*
 * Process PAWD string of remote pawd tool.
 *
 * We repeat the resolution of the string until the resolved string resolves
 * to itself.  This ensures that we follow path resolutions through all
 * possible Amd mount points until we reach some sort of convergence.  To
 * prevent possible infinite loops, we break out of this loop if the strings
 * do not converge after MAX_PAWD_TRIES times.
 */
amq_string *
amqproc_pawd_1_svc(voidp argp, struct svc_req *rqstp)
{
  static amq_string res;
#define MAX_PAWD_TRIES 10
  int index, len, maxagain = MAX_PAWD_TRIES;
  am_node *mp;
  char *mountpoint;
  char *dir = *(char **) argp;
  static char tmp_buf[MAXPATHLEN];
  char prev_buf[MAXPATHLEN];

  tmp_buf[0] = prev_buf[0] = '\0'; /* default is empty string: no match */
  do {
    for (mp = get_first_exported_ap(&index);
	 mp;
	 mp = get_next_exported_ap(&index)) {
      if (STREQ(mp->am_al->al_mnt->mf_ops->fs_type, "toplvl"))
	continue;
      if (STREQ(mp->am_al->al_mnt->mf_ops->fs_type, "auto"))
	continue;
      mountpoint = (mp->am_link ? mp->am_link : mp->am_al->al_mnt->mf_mount);
      len = strlen(mountpoint);
      if (len == 0)
	continue;
      if (!NSTREQ(mountpoint, dir, len))
	continue;
      if (dir[len] != '\0' && dir[len] != '/')
	continue;
      xstrlcpy(tmp_buf, mp->am_path, sizeof(tmp_buf));
      xstrlcat(tmp_buf, &dir[len], sizeof(tmp_buf));
      break;
    } /* end of "for" loop */
    /* once tmp_buf and prev_buf are equal, break out of "do" loop */
    if (STREQ(tmp_buf, prev_buf))
      break;
    else
      xstrlcpy(prev_buf, tmp_buf, sizeof(prev_buf));
  } while (--maxagain);
  /* check if we couldn't resolve the string after MAX_PAWD_TRIES times */
  if (maxagain <= 0)
    plog(XLOG_WARNING, "path \"%s\" did not resolve after %d tries",
	 tmp_buf, MAX_PAWD_TRIES);

  res = tmp_buf;
  return &res;
}


/*
 * XDR routines.
 */


bool_t
xdr_amq_setopt(XDR *xdrs, amq_setopt *objp)
{
  if (!xdr_enum(xdrs, (enum_t *) ((voidp) &objp->as_opt))) {
    return (FALSE);
  }
  if (!xdr_string(xdrs, &objp->as_str, AMQ_STRLEN)) {
    return (FALSE);
  }
  return (TRUE);
}


/*
 * More XDR routines  - Should be used for OUTPUT ONLY.
 */
bool_t
xdr_amq_mount_tree_node(XDR *xdrs, amq_mount_tree *objp)
{
  am_node *mp = (am_node *) objp;
  long mtime;

  if (!xdr_amq_string(xdrs, &mp->am_al->al_mnt->mf_info)) {
    return (FALSE);
  }
  if (!xdr_amq_string(xdrs, &mp->am_path)) {
    return (FALSE);
  }
  if (!xdr_amq_string(xdrs, mp->am_link ? &mp->am_link : &mp->am_al->al_mnt->mf_mount)) {
    return (FALSE);
  }
  if (!xdr_amq_string(xdrs, &mp->am_al->al_mnt->mf_ops->fs_type)) {
    return (FALSE);
  }
  mtime = mp->am_stats.s_mtime;
  if (!xdr_long(xdrs, &mtime)) {
    return (FALSE);
  }
  if (!xdr_u_short(xdrs, &mp->am_stats.s_uid)) {
    return (FALSE);
  }
  if (!xdr_int(xdrs, &mp->am_stats.s_getattr)) {
    return (FALSE);
  }
  if (!xdr_int(xdrs, &mp->am_stats.s_lookup)) {
    return (FALSE);
  }
  if (!xdr_int(xdrs, &mp->am_stats.s_readdir)) {
    return (FALSE);
  }
  if (!xdr_int(xdrs, &mp->am_stats.s_readlink)) {
    return (FALSE);
  }
  if (!xdr_int(xdrs, &mp->am_stats.s_statfs)) {
    return (FALSE);
  }
  return (TRUE);
}


bool_t
xdr_amq_mount_subtree(XDR *xdrs, amq_mount_tree *objp)
{
  am_node *mp = (am_node *) objp;

  if (!xdr_amq_mount_tree_node(xdrs, objp)) {
    return (FALSE);
  }
  if (!xdr_pointer(xdrs,
		   (char **) ((voidp) &mp->am_osib),
		   sizeof(amq_mount_tree),
		   (XDRPROC_T_TYPE) xdr_amq_mount_subtree)) {
    return (FALSE);
  }
  if (!xdr_pointer(xdrs,
		   (char **) ((voidp) &mp->am_child),
		   sizeof(amq_mount_tree),
		   (XDRPROC_T_TYPE) xdr_amq_mount_subtree)) {
    return (FALSE);
  }
  return (TRUE);
}


bool_t
xdr_amq_mount_tree(XDR *xdrs, amq_mount_tree *objp)
{
  am_node *mp = (am_node *) objp;
  am_node *mnil = NULL;

  if (!xdr_amq_mount_tree_node(xdrs, objp)) {
    return (FALSE);
  }
  if (!xdr_pointer(xdrs,
		   (char **) ((voidp) &mnil),
		   sizeof(amq_mount_tree),
		   (XDRPROC_T_TYPE) xdr_amq_mount_subtree)) {
    return (FALSE);
  }
  if (!xdr_pointer(xdrs,
		   (char **) ((voidp) &mp->am_child),
		   sizeof(amq_mount_tree),
		   (XDRPROC_T_TYPE) xdr_amq_mount_subtree)) {
    return (FALSE);
  }
  return (TRUE);
}


bool_t
xdr_amq_mount_tree_p(XDR *xdrs, amq_mount_tree_p *objp)
{
  if (!xdr_pointer(xdrs, (char **) objp, sizeof(amq_mount_tree), (XDRPROC_T_TYPE) xdr_amq_mount_tree)) {
    return (FALSE);
  }
  return (TRUE);
}


bool_t
xdr_amq_mount_stats(XDR *xdrs, amq_mount_stats *objp)
{
  if (!xdr_int(xdrs, &objp->as_drops)) {
    return (FALSE);
  }
  if (!xdr_int(xdrs, &objp->as_stale)) {
    return (FALSE);
  }
  if (!xdr_int(xdrs, &objp->as_mok)) {
    return (FALSE);
  }
  if (!xdr_int(xdrs, &objp->as_merr)) {
    return (FALSE);
  }
  if (!xdr_int(xdrs, &objp->as_uerr)) {
    return (FALSE);
  }
  return (TRUE);
}



bool_t
xdr_amq_mount_tree_list(XDR *xdrs, amq_mount_tree_list *objp)
{
  if (!xdr_array(xdrs,
		 (char **) ((voidp) &objp->amq_mount_tree_list_val),
		 (u_int *) &objp->amq_mount_tree_list_len,
		 ~0,
		 sizeof(amq_mount_tree_p),
		 (XDRPROC_T_TYPE) xdr_amq_mount_tree_p)) {
    return (FALSE);
  }
  return (TRUE);
}


bool_t
xdr_amq_mount_info_qelem(XDR *xdrs, qelem *qhead)
{
  mntfs *mf;
  u_int len = 0;

  /*
   * Compute length of list
   */
  for (mf = AM_LAST(mntfs, qhead); mf != HEAD(mntfs, qhead); mf = PREV(mntfs, mf)) {
    if (!(mf->mf_fsflags & FS_AMQINFO))
      continue;
    len++;
  }
  xdr_u_int(xdrs, &len);

  /*
   * Send individual data items
   */
  for (mf = AM_LAST(mntfs, qhead); mf != HEAD(mntfs, qhead); mf = PREV(mntfs, mf)) {
    int up;
    if (!(mf->mf_fsflags & FS_AMQINFO))
      continue;

    if (!xdr_amq_string(xdrs, &mf->mf_ops->fs_type)) {
      return (FALSE);
    }
    if (!xdr_amq_string(xdrs, &mf->mf_mount)) {
      return (FALSE);
    }
    if (!xdr_amq_string(xdrs, &mf->mf_info)) {
      return (FALSE);
    }
    if (!xdr_amq_string(xdrs, &mf->mf_server->fs_host)) {
      return (FALSE);
    }
    if (!xdr_int(xdrs, &mf->mf_error)) {
      return (FALSE);
    }
    if (!xdr_int(xdrs, &mf->mf_refc)) {
      return (FALSE);
    }
    if (FSRV_ERROR(mf->mf_server) || FSRV_ISDOWN(mf->mf_server))
      up = 0;
    else if (FSRV_ISUP(mf->mf_server))
      up = 1;
    else
      up = -1;
    if (!xdr_int(xdrs, &up)) {
      return (FALSE);
    }
  }
  return (TRUE);
}

bool_t
xdr_amq_map_info_qelem(XDR *xdrs, qelem *qhead)
{
  mnt_map *m;
  u_int len = 0;
  int x;
  char *n;

  /*
   * Compute length of list
   */
  ITER(m, mnt_map, qhead) {
     len++;
  }

  if (!xdr_u_int(xdrs, &len))
      return (FALSE);

  /*
   * Send individual data items
   */
  ITER(m, mnt_map, qhead) {
    if (!xdr_amq_string(xdrs, &m->map_name)) {
      return (FALSE);
    }

    n = m->wildcard ? m->wildcard : "";
    if (!xdr_amq_string(xdrs, &n)) {
      return (FALSE);
    }

    if (!xdr_long(xdrs, (long *) &m->modify)) {
      return (FALSE);
    }

    x = m->flags;
    if (!xdr_int(xdrs, &x)) {
      return (FALSE);
    }

    x = m->nentries;
    if (!xdr_int(xdrs, &x)) {
      return (FALSE);
    }

    x = m->reloads;
    if (!xdr_int(xdrs, &x)) {
      return (FALSE);
    }

    if (!xdr_int(xdrs, &m->refc)) {
      return (FALSE);
    }

    if (m->isup)
      x = (*m->isup)(m, m->map_name);
    else
      x = -1;
    if (!xdr_int(xdrs, &x)) {
      return (FALSE);
    }
  }
  return (TRUE);
}

bool_t
xdr_pri_free(XDRPROC_T_TYPE xdr_args, caddr_t args_ptr)
{
  XDR xdr;

  xdr.x_op = XDR_FREE;
  return ((*xdr_args) (&xdr, (caddr_t *) args_ptr));
}
