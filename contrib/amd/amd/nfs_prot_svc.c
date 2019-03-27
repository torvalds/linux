/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989 The Regents of the University of California.
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
 * File: am-utils/amd/nfs_prot_svc.c
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* external definitions */
extern voidp nfsproc_null_2_svc(voidp, struct svc_req *);
extern nfsattrstat *nfsproc_getattr_2_svc(am_nfs_fh *, struct svc_req *);
extern nfsattrstat *nfsproc_setattr_2_svc(nfssattrargs *, struct svc_req *);
extern voidp nfsproc_root_2_svc(voidp, struct svc_req *);
extern nfsdiropres *nfsproc_lookup_2_svc(nfsdiropargs *, struct svc_req *);
extern nfsreadlinkres *nfsproc_readlink_2_svc(am_nfs_fh *, struct svc_req *);
extern nfsreadres *nfsproc_read_2_svc(nfsreadargs *, struct svc_req *);
extern voidp nfsproc_writecache_2_svc(voidp, struct svc_req *);
extern nfsattrstat *nfsproc_write_2_svc(nfswriteargs *, struct svc_req *);
extern nfsdiropres *nfsproc_create_2_svc(nfscreateargs *, struct svc_req *);
extern nfsstat *nfsproc_remove_2_svc(nfsdiropargs *, struct svc_req *);
extern nfsstat *nfsproc_rename_2_svc(nfsrenameargs *, struct svc_req *);
extern nfsstat *nfsproc_link_2_svc(nfslinkargs *, struct svc_req *);
extern nfsstat *nfsproc_symlink_2_svc(nfssymlinkargs *, struct svc_req *);
extern nfsdiropres *nfsproc_mkdir_2_svc(nfscreateargs *, struct svc_req *);
extern nfsstat *nfsproc_rmdir_2_svc(nfsdiropargs *, struct svc_req *);
extern nfsreaddirres *nfsproc_readdir_2_svc(nfsreaddirargs *, struct svc_req *);
extern nfsstatfsres *nfsproc_statfs_2_svc(am_nfs_fh *, struct svc_req *);

/* global variables */
SVCXPRT *current_transp;
dispatcher_t nfs_dispatcher = nfs_program_2;

/* typedefs */
typedef char *(*nfssvcproc_t)(voidp, struct svc_req *);


void
nfs_program_2(struct svc_req *rqstp, SVCXPRT *transp)
{
  union {
    am_nfs_fh		nfsproc_getattr_2_arg;
    nfssattrargs	nfsproc_setattr_2_arg;
    nfsdiropargs	nfsproc_lookup_2_arg;
    am_nfs_fh		nfsproc_readlink_2_arg;
    nfsreadargs		nfsproc_read_2_arg;
    nfswriteargs	nfsproc_write_2_arg;
    nfscreateargs	nfsproc_create_2_arg;
    nfsdiropargs	nfsproc_remove_2_arg;
    nfsrenameargs	nfsproc_rename_2_arg;
    nfslinkargs		nfsproc_link_2_arg;
    nfssymlinkargs	nfsproc_symlink_2_arg;
    nfscreateargs	nfsproc_mkdir_2_arg;
    nfsdiropargs	fsproc_rmdir_2_arg;
    nfsreaddirargs	nfsproc_readdir_2_arg;
    am_nfs_fh		nfsproc_statfs_2_arg;
  } argument;
  char *result;
  xdrproc_t xdr_argument, xdr_result;
  nfssvcproc_t local;

#ifdef HAVE_TRANSPORT_TYPE_TLI
  /*
   * On TLI systems we don't use an INET network type, but a "ticlts" (see
   * /etc/netconfig and conf/transp_tli.c:create_nfs_service).  This means
   * that packets could only come from the loopback interface, and we don't
   * need to check them and filter possibly spoofed packets.  Therefore we
   * only need to check if the UID caller is correct.
   */
# ifdef HAVE___RPC_GET_LOCAL_UID
  uid_t u;
  /* extern definition for an internal libnsl function */
  extern int __rpc_get_local_uid(SVCXPRT *transp, uid_t *uid);
  if (__rpc_get_local_uid(transp, &u) >= 0  &&  u != 0) {
    plog(XLOG_WARNING, "ignoring request from UID %ld, must be 0", (long) u);
    return;
  }
# else /* not HAVE___RPC_GET_LOCAL_UID */
  dlog("cannot verify local uid for rpc request");
# endif /* HAVE___RPC_GET_LOCAL_UID */
#else /* not HAVE_TRANPORT_TYPE_TLI */
  struct sockaddr_in *sinp;
  char dq[20], dq2[28];
  sinp = amu_svc_getcaller(rqstp->rq_xprt);
# ifdef MNT2_NFS_OPT_RESVPORT
  /* Verify that the request comes from a reserved port */
  if (sinp &&
      ntohs(sinp->sin_port) >= IPPORT_RESERVED &&
      !(gopt.flags & CFM_NFS_INSECURE_PORT)) {
    plog(XLOG_WARNING, "ignoring request from %s:%u, port not reserved",
	 inet_dquad(dq, sizeof(dq), sinp->sin_addr.s_addr),
	 ntohs(sinp->sin_port));
    return;
  }
# endif /* MNT2_NFS_OPT_RESVPORT */
  /* if the address does not match, ignore the request */
  if (sinp && (sinp->sin_addr.s_addr != myipaddr.s_addr)) {
    if (gopt.flags & CFM_NFS_ANY_INTERFACE) {
      if (!is_interface_local(sinp->sin_addr.s_addr)) {
	plog(XLOG_WARNING, "ignoring request from %s:%u, not a local interface",
	     inet_dquad(dq, sizeof(dq), sinp->sin_addr.s_addr),
	     ntohs(sinp->sin_port));
      }
    } else {
      plog(XLOG_WARNING, "ignoring request from %s:%u, expected %s",
	   inet_dquad(dq, sizeof(dq), sinp->sin_addr.s_addr),
	   ntohs(sinp->sin_port),
	   inet_dquad(dq2, sizeof(dq2), myipaddr.s_addr));
      return;
    }
  }
#endif /* not HAVE_TRANPORT_TYPE_TLI */

  current_transp = NULL;

  switch (rqstp->rq_proc) {

  case NFSPROC_NULL:
    xdr_argument = (xdrproc_t) xdr_void;
    xdr_result = (xdrproc_t) xdr_void;
    local = (nfssvcproc_t) nfsproc_null_2_svc;
    break;

  case NFSPROC_GETATTR:
    xdr_argument = (xdrproc_t) xdr_nfs_fh;
    xdr_result = (xdrproc_t) xdr_attrstat;
    local = (nfssvcproc_t) nfsproc_getattr_2_svc;
    break;

  case NFSPROC_SETATTR:
    xdr_argument = (xdrproc_t) xdr_sattrargs;
    xdr_result = (xdrproc_t) xdr_attrstat;
    local = (nfssvcproc_t) nfsproc_setattr_2_svc;
    break;

  case NFSPROC_ROOT:
    xdr_argument = (xdrproc_t) xdr_void;
    xdr_result = (xdrproc_t) xdr_void;
    local = (nfssvcproc_t) nfsproc_root_2_svc;
    break;

  case NFSPROC_LOOKUP:
    xdr_argument = (xdrproc_t) xdr_diropargs;
    xdr_result = (xdrproc_t) xdr_diropres;
    local = (nfssvcproc_t) nfsproc_lookup_2_svc;
    /*
     * Cheap way to pass transp down to amfs_auto_lookuppn so it can
     * be stored in the am_node structure and later used for
     * quick_reply().
     */
    current_transp = transp;
    break;

  case NFSPROC_READLINK:
    xdr_argument = (xdrproc_t) xdr_nfs_fh;
    xdr_result = (xdrproc_t) xdr_readlinkres;
    local = (nfssvcproc_t) nfsproc_readlink_2_svc;
    break;

  case NFSPROC_READ:
    xdr_argument = (xdrproc_t) xdr_readargs;
    xdr_result = (xdrproc_t) xdr_readres;
    local = (nfssvcproc_t) nfsproc_read_2_svc;
    break;

  case NFSPROC_WRITECACHE:
    xdr_argument = (xdrproc_t) xdr_void;
    xdr_result = (xdrproc_t) xdr_void;
    local = (nfssvcproc_t) nfsproc_writecache_2_svc;
    break;

  case NFSPROC_WRITE:
    xdr_argument = (xdrproc_t) xdr_writeargs;
    xdr_result = (xdrproc_t) xdr_attrstat;
    local = (nfssvcproc_t) nfsproc_write_2_svc;
    break;

  case NFSPROC_CREATE:
    xdr_argument = (xdrproc_t) xdr_createargs;
    xdr_result = (xdrproc_t) xdr_diropres;
    local = (nfssvcproc_t) nfsproc_create_2_svc;
    break;

  case NFSPROC_REMOVE:
    xdr_argument = (xdrproc_t) xdr_diropargs;
    xdr_result = (xdrproc_t) xdr_nfsstat;
    local = (nfssvcproc_t) nfsproc_remove_2_svc;
    break;

  case NFSPROC_RENAME:
    xdr_argument = (xdrproc_t) xdr_renameargs;
    xdr_result = (xdrproc_t) xdr_nfsstat;
    local = (nfssvcproc_t) nfsproc_rename_2_svc;
    break;

  case NFSPROC_LINK:
    xdr_argument = (xdrproc_t) xdr_linkargs;
    xdr_result = (xdrproc_t) xdr_nfsstat;
    local = (nfssvcproc_t) nfsproc_link_2_svc;
    break;

  case NFSPROC_SYMLINK:
    xdr_argument = (xdrproc_t) xdr_symlinkargs;
    xdr_result = (xdrproc_t) xdr_nfsstat;
    local = (nfssvcproc_t) nfsproc_symlink_2_svc;
    break;

  case NFSPROC_MKDIR:
    xdr_argument = (xdrproc_t) xdr_createargs;
    xdr_result = (xdrproc_t) xdr_diropres;
    local = (nfssvcproc_t) nfsproc_mkdir_2_svc;
    break;

  case NFSPROC_RMDIR:
    xdr_argument = (xdrproc_t) xdr_diropargs;
    xdr_result = (xdrproc_t) xdr_nfsstat;
    local = (nfssvcproc_t) nfsproc_rmdir_2_svc;
    break;

  case NFSPROC_READDIR:
    xdr_argument = (xdrproc_t) xdr_readdirargs;
    xdr_result = (xdrproc_t) xdr_readdirres;
    local = (nfssvcproc_t) nfsproc_readdir_2_svc;
    break;

  case NFSPROC_STATFS:
    xdr_argument = (xdrproc_t) xdr_nfs_fh;
    xdr_result = (xdrproc_t) xdr_statfsres;
    local = (nfssvcproc_t) nfsproc_statfs_2_svc;
    break;

  default:
    svcerr_noproc(transp);
    return;
  }

  memset((char *) &argument, 0, sizeof(argument));
  if (!svc_getargs(transp,
		   (XDRPROC_T_TYPE) xdr_argument,
		   (SVC_IN_ARG_TYPE) &argument)) {
    plog(XLOG_ERROR,
	 "NFS xdr decode failed for %d %d %d",
	 (int) rqstp->rq_prog, (int) rqstp->rq_vers, (int) rqstp->rq_proc);
    svcerr_decode(transp);
    return;
  }
  result = (*local) (&argument, rqstp);

  current_transp = NULL;

  if (result != NULL && !svc_sendreply(transp,
				       (XDRPROC_T_TYPE) xdr_result,
				       result)) {
    svcerr_systemerr(transp);
  }
  if (!svc_freeargs(transp,
		    (XDRPROC_T_TYPE) xdr_argument,
		    (SVC_IN_ARG_TYPE) & argument)) {
    plog(XLOG_FATAL, "unable to free rpc arguments in nfs_program_2");
    going_down(1);
  }
}

void
nfs_program_3(struct svc_req *rqstp, register SVCXPRT *transp)
{
  union {
    am_GETATTR3args am_nfs3_getattr_3_arg;
    am_SETATTR3args am_nfs3_setattr_3_arg;
    am_LOOKUP3args am_nfs3_lookup_3_arg;
    am_ACCESS3args am_nfs3_access_3_arg;
    am_READLINK3args am_nfs3_readlink_3_arg;
    am_READ3args am_nfs3_read_3_arg;
    am_WRITE3args am_nfs3_write_3_arg;
    am_CREATE3args am_nfs3_create_3_arg;
    am_MKDIR3args am_nfs3_mkdir_3_arg;
    am_SYMLINK3args am_nfs3_symlink_3_arg;
    am_MKNOD3args am_nfs3_mknod_3_arg;
    am_REMOVE3args am_nfs3_remove_3_arg;
    am_RMDIR3args am_nfs3_rmdir_3_arg;
    am_RENAME3args am_nfs3_rename_3_arg;
    am_LINK3args am_nfs3_link_3_arg;
    am_READDIR3args am_nfs3_readdir_3_arg;
    am_READDIRPLUS3args am_nfs3_readdirplus_3_arg;
    am_FSSTAT3args am_nfs3_fsstat_3_arg;
    am_FSINFO3args am_nfs3_fsinfo_3_arg;
    am_PATHCONF3args am_nfs3_pathconf_3_arg;
    am_COMMIT3args am_nfs3_commit_3_arg;
  } argument;
  char *result;
  xdrproc_t _xdr_argument, _xdr_result;
  nfssvcproc_t local;

  switch (rqstp->rq_proc) {
  case AM_NFS3_NULL:
    _xdr_argument = (xdrproc_t) xdr_void;
    _xdr_result = (xdrproc_t) xdr_void;
    local = (nfssvcproc_t) am_nfs3_null_3_svc;
    break;

  case AM_NFS3_GETATTR:
    _xdr_argument = (xdrproc_t) xdr_am_GETATTR3args;
    _xdr_result = (xdrproc_t) xdr_am_GETATTR3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_getattr_3_svc;
    break;

  case AM_NFS3_SETATTR:
    _xdr_argument = (xdrproc_t) xdr_am_SETATTR3args;
    _xdr_result = (xdrproc_t) xdr_am_SETATTR3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_setattr_3_svc;
    break;

  case AM_NFS3_LOOKUP:
    _xdr_argument = (xdrproc_t) xdr_am_LOOKUP3args;
    _xdr_result = (xdrproc_t) xdr_am_LOOKUP3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_lookup_3_svc;
    break;

  case AM_NFS3_ACCESS:
    _xdr_argument = (xdrproc_t) xdr_am_ACCESS3args;
    _xdr_result = (xdrproc_t) xdr_am_ACCESS3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_access_3_svc;
    break;

  case AM_NFS3_READLINK:
    _xdr_argument = (xdrproc_t) xdr_am_READLINK3args;
    _xdr_result = (xdrproc_t) xdr_am_READLINK3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_readlink_3_svc;
    break;

  case AM_NFS3_READ:
    _xdr_argument = (xdrproc_t) xdr_am_READ3args;
    _xdr_result = (xdrproc_t) xdr_am_READ3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_read_3_svc;
    break;

  case AM_NFS3_WRITE:
    _xdr_argument = (xdrproc_t) xdr_am_WRITE3args;
    _xdr_result = (xdrproc_t) xdr_am_WRITE3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_write_3_svc;
    break;

  case AM_NFS3_CREATE:
    _xdr_argument = (xdrproc_t) xdr_am_CREATE3args;
    _xdr_result = (xdrproc_t) xdr_am_CREATE3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_create_3_svc;
    break;

  case AM_NFS3_MKDIR:
    _xdr_argument = (xdrproc_t) xdr_am_MKDIR3args;
    _xdr_result = (xdrproc_t) xdr_am_MKDIR3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_mkdir_3_svc;
    break;

  case AM_NFS3_SYMLINK:
    _xdr_argument = (xdrproc_t) xdr_am_SYMLINK3args;
    _xdr_result = (xdrproc_t) xdr_am_SYMLINK3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_symlink_3_svc;
    break;

  case AM_NFS3_MKNOD:
    _xdr_argument = (xdrproc_t) xdr_am_MKNOD3args;
    _xdr_result = (xdrproc_t) xdr_am_MKNOD3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_mknod_3_svc;
    break;

  case AM_NFS3_REMOVE:
    _xdr_argument = (xdrproc_t) xdr_am_REMOVE3args;
    _xdr_result = (xdrproc_t) xdr_am_REMOVE3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_remove_3_svc;
    break;

  case AM_NFS3_RMDIR:
    _xdr_argument = (xdrproc_t) xdr_am_RMDIR3args;
    _xdr_result = (xdrproc_t) xdr_am_RMDIR3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_rmdir_3_svc;
    break;

  case AM_NFS3_RENAME:
    _xdr_argument = (xdrproc_t) xdr_am_RENAME3args;
    _xdr_result = (xdrproc_t) xdr_am_RENAME3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_rename_3_svc;
    break;

  case AM_NFS3_LINK:
    _xdr_argument = (xdrproc_t) xdr_am_LINK3args;
    _xdr_result = (xdrproc_t) xdr_am_LINK3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_link_3_svc;
    break;

  case AM_NFS3_READDIR:
    _xdr_argument = (xdrproc_t) xdr_am_READDIR3args;
    _xdr_result = (xdrproc_t) xdr_am_READDIR3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_readdir_3_svc;
    break;

  case AM_NFS3_READDIRPLUS:
    _xdr_argument = (xdrproc_t) xdr_am_READDIRPLUS3args;
    _xdr_result = (xdrproc_t) xdr_am_READDIRPLUS3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_readdirplus_3_svc;
    break;

  case AM_NFS3_FSSTAT:
    _xdr_argument = (xdrproc_t) xdr_am_FSSTAT3args;
    _xdr_result = (xdrproc_t) xdr_am_FSSTAT3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_fsstat_3_svc;
    break;

  case AM_NFS3_FSINFO:
    _xdr_argument = (xdrproc_t) xdr_am_FSINFO3args;
    _xdr_result = (xdrproc_t) xdr_am_FSINFO3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_fsinfo_3_svc;
    break;

  case AM_NFS3_PATHCONF:
    _xdr_argument = (xdrproc_t) xdr_am_PATHCONF3args;
    _xdr_result = (xdrproc_t) xdr_am_PATHCONF3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_pathconf_3_svc;
    break;

  case AM_NFS3_COMMIT:
    _xdr_argument = (xdrproc_t) xdr_am_COMMIT3args;
    _xdr_result = (xdrproc_t) xdr_am_COMMIT3res;
    local = (nfssvcproc_t) (char *(*)(char *, struct svc_req *)) am_nfs3_commit_3_svc;
    break;

  default:
    svcerr_noproc (transp);
    return;
  }

  memset ((char *)&argument, 0, sizeof (argument));

  if (!svc_getargs(transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
    plog(XLOG_ERROR,
	 "NFS xdr decode failed for %d %d %d",
	 (int) rqstp->rq_prog, (int) rqstp->rq_vers, (int) rqstp->rq_proc);
    svcerr_decode(transp);
    return;
  }

  result = (*local) (&argument, rqstp);
  if (result != NULL && !svc_sendreply(transp, (xdrproc_t) _xdr_result, result)) {
    svcerr_systemerr (transp);
  }

  if (!svc_freeargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
    plog(XLOG_FATAL, "unable to free rpc arguments in nfs_program_3");
    going_down(1);
  }
  return;
}
