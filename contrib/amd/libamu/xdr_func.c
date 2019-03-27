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
 * File: am-utils/libamu/xdr_func.c
 *
 */

/*
 * Complete list of all possible xdr functions which may be needed.
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amu.h>

#ifdef __RPCSVC_MOUNT_H__
# error IRIX6 should not include rpcsvc/mount.h
#endif /* __RPCSVC_MOUNT_H__ */

/*
 * MACROS:
 */
#ifdef HAVE_FS_AUTOFS
# ifndef AUTOFS_MAXCOMPONENTLEN
#  define AUTOFS_MAXCOMPONENTLEN 255
# endif /* not AUTOFS_MAXCOMPONENTLEN */
# ifndef AUTOFS_MAXOPTSLEN
#  define AUTOFS_MAXOPTSLEN 255
# endif /* not AUTOFS_MAXOPTSLEN */
# ifndef AUTOFS_MAXPATHLEN
#  define AUTOFS_MAXPATHLEN 1024
# endif /* not AUTOFS_MAXPATHLEN */
#endif /* HAVE_FS_AUTOFS */

/* forward definitions, are they needed? */
extern bool_t xdr_exportnode(XDR *xdrs, exportnode *objp);
extern bool_t xdr_groupnode(XDR *xdrs, groupnode *objp);
extern bool_t xdr_name(XDR *xdrs, name *objp);


#ifndef HAVE_XDR_ATTRSTAT
bool_t
xdr_attrstat(XDR *xdrs, nfsattrstat *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_attrstat:");

  if (!xdr_nfsstat(xdrs, &objp->ns_status)) {
    return (FALSE);
  }
  switch (objp->ns_status) {
  case NFS_OK:
    if (!xdr_fattr(xdrs, &objp->ns_u.ns_attr_u)) {
      return (FALSE);
    }
    break;
  default:
    break;
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_ATTRSTAT */


#ifndef HAVE_XDR_CREATEARGS
bool_t
xdr_createargs(XDR *xdrs, nfscreateargs *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_createargs:");

  if (!xdr_diropargs(xdrs, &objp->ca_where)) {
    return (FALSE);
  }
  if (!xdr_sattr(xdrs, &objp->ca_attributes)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_CREATEARGS */


#ifndef HAVE_XDR_DIRLIST
bool_t
xdr_dirlist(XDR *xdrs, nfsdirlist *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_dirlist:");

  if (!xdr_pointer(xdrs, (char **) &objp->dl_entries, sizeof(nfsentry), (XDRPROC_T_TYPE) xdr_entry)) {
    return (FALSE);
  }
  if (!xdr_bool(xdrs, &objp->dl_eof)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_DIRLIST */


#ifndef HAVE_XDR_DIROPARGS
bool_t
xdr_diropargs(XDR *xdrs, nfsdiropargs *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_diropargs:");

  if (!xdr_nfs_fh(xdrs, &objp->da_fhandle)) {
    return (FALSE);
  }
  if (!xdr_filename(xdrs, &objp->da_name)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_DIROPARGS */


#ifndef HAVE_XDR_DIROPOKRES
bool_t
xdr_diropokres(XDR *xdrs, nfsdiropokres *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_diropokres:");

  if (!xdr_nfs_fh(xdrs, &objp->drok_fhandle)) {
    return (FALSE);
  }
  if (!xdr_fattr(xdrs, &objp->drok_attributes)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_DIROPOKRES */


#ifndef HAVE_XDR_DIROPRES
bool_t
xdr_diropres(XDR *xdrs, nfsdiropres *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_diropres:");

  if (!xdr_nfsstat(xdrs, &objp->dr_status)) {
    return (FALSE);
  }
  switch (objp->dr_status) {
  case NFS_OK:
    if (!xdr_diropokres(xdrs, &objp->dr_u.dr_drok_u)) {
      return (FALSE);
    }
    break;
  default:
    break;
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_DIROPRES */


#ifndef HAVE_XDR_DIRPATH
bool_t
xdr_dirpath(XDR *xdrs, dirpath *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_dirpath:");

  if (!xdr_string(xdrs, objp, MNTPATHLEN)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_DIRPATH */


#ifndef HAVE_XDR_ENTRY
bool_t
xdr_entry(XDR *xdrs, nfsentry *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_entry:");

  if (!xdr_u_int(xdrs, &objp->ne_fileid)) {
    return (FALSE);
  }
  if (!xdr_filename(xdrs, &objp->ne_name)) {
    return (FALSE);
  }
  if (!xdr_nfscookie(xdrs, objp->ne_cookie)) {
    return (FALSE);
  }
  if (!xdr_pointer(xdrs, (char **) &objp->ne_nextentry, sizeof(nfsentry), (XDRPROC_T_TYPE) xdr_entry)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_ENTRY */


#ifndef HAVE_XDR_EXPORTNODE
bool_t
xdr_exportnode(XDR *xdrs, exportnode *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_exportnode:");

  if (!xdr_dirpath(xdrs, &objp->ex_dir)) {
    return (FALSE);
  }
  if (!xdr_groups(xdrs, &objp->ex_groups)) {
    return (FALSE);
  }
  if (!xdr_exports(xdrs, &objp->ex_next)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_EXPORTNODE */


#ifndef HAVE_XDR_EXPORTS
bool_t
xdr_exports(XDR *xdrs, exports *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_exports:");

  if (!xdr_pointer(xdrs, (char **) objp, sizeof(exportnode), (XDRPROC_T_TYPE) xdr_exportnode)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_EXPORTS */


#ifndef HAVE_XDR_FATTR
bool_t
xdr_fattr(XDR *xdrs, nfsfattr *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_fattr:");

  if (!xdr_ftype(xdrs, &objp->na_type)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->na_mode)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->na_nlink)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->na_uid)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->na_gid)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->na_size)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->na_blocksize)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->na_rdev)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->na_blocks)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->na_fsid)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->na_fileid)) {
    return (FALSE);
  }
  if (!xdr_nfstime(xdrs, &objp->na_atime)) {
    return (FALSE);
  }
  if (!xdr_nfstime(xdrs, &objp->na_mtime)) {
    return (FALSE);
  }
  if (!xdr_nfstime(xdrs, &objp->na_ctime)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_FATTR */


#ifndef HAVE_XDR_FHANDLE
bool_t
xdr_fhandle(XDR *xdrs, fhandle objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_fhandle:");

  if (!xdr_opaque(xdrs, objp, NFS_FHSIZE)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_FHANDLE */


#ifndef HAVE_XDR_FHSTATUS
bool_t
xdr_fhstatus(XDR *xdrs, fhstatus *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_fhstatus:");

  if (!xdr_u_int(xdrs, &objp->fhs_status)) {
    return (FALSE);
  }
  if (objp->fhs_status == 0 && !xdr_fhandle(xdrs, objp->fhs_fh)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_FHSTATUS */


#ifndef HAVE_XDR_FILENAME
bool_t
xdr_filename(XDR *xdrs, filename *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_filename:");

  if (!xdr_string(xdrs, objp, NFS_MAXNAMLEN)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_FILENAME */


#ifndef HAVE_XDR_FTYPE
bool_t
xdr_ftype(XDR *xdrs, nfsftype *objp)
{
  enum_t local_obj = *objp;

  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_ftype:");

  if (!xdr_enum(xdrs, &local_obj)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_FTYPE */


#ifndef HAVE_XDR_GROUPNODE
bool_t
xdr_groupnode(XDR *xdrs, groupnode *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_groupnode:");

  if (!xdr_name(xdrs, &objp->gr_name)) {
    return (FALSE);
  }
  if (!xdr_groups(xdrs, &objp->gr_next)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_GROUPNODE */


#ifndef HAVE_XDR_GROUPS
bool_t
xdr_groups(XDR *xdrs, groups *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_groups:");

  if (!xdr_pointer(xdrs, (char **) objp, sizeof(groupnode), (XDRPROC_T_TYPE) xdr_groupnode)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_GROUPS */


#ifndef HAVE_XDR_LINKARGS
bool_t
xdr_linkargs(XDR *xdrs, nfslinkargs *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_linkargs:");

  if (!xdr_nfs_fh(xdrs, &objp->la_fhandle)) {
    return (FALSE);
  }
  if (!xdr_diropargs(xdrs, &objp->la_to)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_LINKARGS */


#ifndef HAVE_XDR_MOUNTBODY
bool_t
xdr_mountbody(XDR *xdrs, mountbody *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_mountbody:");

  if (!xdr_name(xdrs, &objp->ml_hostname)) {
    return (FALSE);
  }
  if (!xdr_dirpath(xdrs, &objp->ml_directory)) {
    return (FALSE);
  }
  if (!xdr_mountlist(xdrs, &objp->ml_next)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_MOUNTBODY */


#ifndef HAVE_XDR_MOUNTLIST
bool_t
xdr_mountlist(XDR *xdrs, mountlist *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_mountlist:");

  if (!xdr_pointer(xdrs, (char **) objp, sizeof(mountbody), (XDRPROC_T_TYPE) xdr_mountbody)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_MOUNTLIST */


#ifndef HAVE_XDR_NAME
bool_t
xdr_name(XDR *xdrs, name *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_name:");

  if (!xdr_string(xdrs, objp, MNTNAMLEN)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_NAME */


#ifndef HAVE_XDR_NFS_FH
bool_t
xdr_nfs_fh(XDR *xdrs, am_nfs_fh *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_nfs_fh:");

  if (!xdr_opaque(xdrs, (caddr_t) objp->fh_data, NFS_FHSIZE)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_NFS_FH */


#ifndef HAVE_XDR_NFSCOOKIE
bool_t
xdr_nfscookie(XDR *xdrs, nfscookie objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_nfscookie:");

  if (!xdr_opaque(xdrs, objp, NFS_COOKIESIZE)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_NFSCOOKIE */


#ifndef HAVE_XDR_NFSPATH
bool_t
xdr_nfspath(XDR *xdrs, nfspath *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_nfspath:");

  if (!xdr_string(xdrs, objp, NFS_MAXPATHLEN)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_NFSPATH */


#ifndef HAVE_XDR_NFSSTAT
bool_t
xdr_nfsstat(XDR *xdrs, nfsstat *objp)
{
  enum_t local_obj = *objp;

  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_nfsstat:");

  if (!xdr_enum(xdrs, &local_obj)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_NFSSTAT */


#ifndef HAVE_XDR_NFSTIME
bool_t
xdr_nfstime(XDR *xdrs, nfstime *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_nfstime:");

  if (!xdr_u_int(xdrs, (u_int *) &objp->nt_seconds)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, (u_int *) &objp->nt_useconds)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_NFSTIME */


#ifndef HAVE_XDR_POINTER
bool_t
xdr_pointer(register XDR *xdrs, char **objpp, u_int obj_size, XDRPROC_T_TYPE xdr_obj)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_pointer:");

  bool_t more_data;

  more_data = (*objpp != NULL);
  if (!xdr_bool(xdrs, &more_data)) {
    return (FALSE);
  }
  if (!more_data) {
    *objpp = NULL;
    return (TRUE);
  }

  return (xdr_reference(xdrs, objpp, obj_size, xdr_obj));
}
#endif /* not HAVE_XDR_POINTER */


#ifndef HAVE_XDR_READARGS
bool_t
xdr_readargs(XDR *xdrs, nfsreadargs *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_readargs:");

  if (!xdr_nfs_fh(xdrs, &objp->ra_fhandle)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->ra_offset)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->ra_count)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->ra_totalcount)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_READARGS */


#ifndef HAVE_XDR_READDIRARGS
bool_t
xdr_readdirargs(XDR *xdrs, nfsreaddirargs *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_readdirargs:");

  if (!xdr_nfs_fh(xdrs, &objp->rda_fhandle)) {
    return (FALSE);
  }
  if (!xdr_nfscookie(xdrs, objp->rda_cookie)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->rda_count)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_READDIRARGS */


#ifndef HAVE_XDR_READDIRRES
bool_t
xdr_readdirres(XDR *xdrs, nfsreaddirres *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_readdirres:");

  if (!xdr_nfsstat(xdrs, &objp->rdr_status)) {
    return (FALSE);
  }
  switch (objp->rdr_status) {
  case NFS_OK:
    if (!xdr_dirlist(xdrs, &objp->rdr_u.rdr_reply_u)) {
      return (FALSE);
    }
    break;
  default:
    break;
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_READDIRRES */


#ifndef HAVE_XDR_READLINKRES
bool_t
xdr_readlinkres(XDR *xdrs, nfsreadlinkres *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_readlinkres:");

  if (!xdr_nfsstat(xdrs, &objp->rlr_status)) {
    return (FALSE);
  }
  switch (objp->rlr_status) {
  case NFS_OK:
    if (!xdr_nfspath(xdrs, &objp->rlr_u.rlr_data_u)) {
      return (FALSE);
    }
    break;
  default:
    break;
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_READLINKRES */


#ifndef HAVE_XDR_READOKRES
bool_t
xdr_readokres(XDR *xdrs, nfsreadokres *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_readokres:");

  if (!xdr_fattr(xdrs, &objp->raok_attributes)) {
    return (FALSE);
  }
  if (!xdr_bytes(xdrs,
		 (char **) & objp->raok_u.raok_val_u,
		 (u_int *) & objp->raok_u.raok_len_u,
		 NFS_MAXDATA)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_READOKRES */


#ifndef HAVE_XDR_READRES
bool_t
xdr_readres(XDR *xdrs, nfsreadres *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_readres:");

  if (!xdr_nfsstat(xdrs, &objp->rr_status)) {
    return (FALSE);
  }
  switch (objp->rr_status) {
  case NFS_OK:
    if (!xdr_readokres(xdrs, &objp->rr_u.rr_reply_u)) {
      return (FALSE);
    }
    break;
  default:
    break;
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_READRES */


#ifndef HAVE_XDR_RENAMEARGS
bool_t
xdr_renameargs(XDR *xdrs, nfsrenameargs *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_renameargs:");

  if (!xdr_diropargs(xdrs, &objp->rna_from)) {
    return (FALSE);
  }
  if (!xdr_diropargs(xdrs, &objp->rna_to)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_RENAMEARGS */


#ifndef HAVE_XDR_SATTR
bool_t
xdr_sattr(XDR *xdrs, nfssattr *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_sattr:");

  if (!xdr_u_int(xdrs, &objp->sa_mode)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->sa_uid)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->sa_gid)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->sa_size)) {
    return (FALSE);
  }
  if (!xdr_nfstime(xdrs, &objp->sa_atime)) {
    return (FALSE);
  }
  if (!xdr_nfstime(xdrs, &objp->sa_mtime)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_SATTR */


#ifndef HAVE_XDR_SATTRARGS
bool_t
xdr_sattrargs(XDR *xdrs, nfssattrargs *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_sattrargs:");

  if (!xdr_nfs_fh(xdrs, &objp->sag_fhandle)) {
    return (FALSE);
  }
  if (!xdr_sattr(xdrs, &objp->sag_attributes)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_SATTRARGS */


#ifndef HAVE_XDR_STATFSOKRES
bool_t
xdr_statfsokres(XDR *xdrs, nfsstatfsokres *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_statfsokres:");

  if (!xdr_u_int(xdrs, &objp->sfrok_tsize)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->sfrok_bsize)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->sfrok_blocks)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->sfrok_bfree)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->sfrok_bavail)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_STATFSOKRES */


#ifndef HAVE_XDR_STATFSRES
bool_t
xdr_statfsres(XDR *xdrs, nfsstatfsres *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_statfsres:");

  if (!xdr_nfsstat(xdrs, &objp->sfr_status)) {
    return (FALSE);
  }
  switch (objp->sfr_status) {
  case NFS_OK:
    if (!xdr_statfsokres(xdrs, &objp->sfr_u.sfr_reply_u)) {
      return (FALSE);
    }
    break;
  default:
    break;
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_STATFSRES */


#ifndef HAVE_XDR_SYMLINKARGS
bool_t
xdr_symlinkargs(XDR *xdrs, nfssymlinkargs *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_symlinkargs:");

  if (!xdr_diropargs(xdrs, &objp->sla_from)) {
    return (FALSE);
  }
  if (!xdr_nfspath(xdrs, &objp->sla_to)) {
    return (FALSE);
  }
  if (!xdr_sattr(xdrs, &objp->sla_attributes)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_SYMLINKARGS */


#ifndef HAVE_XDR_WRITEARGS
bool_t
xdr_writeargs(XDR *xdrs, nfswriteargs *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_writeargs:");

  if (!xdr_nfs_fh(xdrs, &objp->wra_fhandle)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->wra_beginoffset)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->wra_offset)) {
    return (FALSE);
  }
  if (!xdr_u_int(xdrs, &objp->wra_totalcount)) {
    return (FALSE);
  }
  if (!xdr_bytes(xdrs,
		 (char **) & objp->wra_u.wra_val_u,
		 (u_int *) & objp->wra_u.wra_len_u,
		 NFS_MAXDATA)) {
    return (FALSE);
  }
  return (TRUE);
}
#endif /* not HAVE_XDR_WRITEARGS */


/*
 * NFS V3 XDR FUNCTIONS:
 */
#ifdef HAVE_FS_NFS3
bool_t
xdr_am_fhandle3(XDR *xdrs, am_fhandle3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_fhandle3:");

  if (!xdr_bytes(xdrs,
		 (char **) &objp->fhandle3_val,
		 (u_int *) &objp->fhandle3_len,
		 AM_FHSIZE3))
    return (FALSE);
  return (TRUE);
}


bool_t
xdr_am_mountstat3(XDR *xdrs, am_mountstat3 *objp)
{
  enum_t local_obj = *objp;

  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_mountstat3:");

  if (!xdr_enum(xdrs, &local_obj))
    return (FALSE);
  return (TRUE);
}


bool_t
xdr_am_mountres3_ok(XDR *xdrs, am_mountres3_ok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_mountres3_ok:");

  if (!xdr_am_fhandle3(xdrs, &objp->fhandle))
    return (FALSE);
  if (!xdr_array(xdrs,
		 (char **) ((voidp) &objp->auth_flavors.auth_flavors_val),
		 (u_int *) &objp->auth_flavors.auth_flavors_len,
		 ~0,
		 sizeof(int),
		 (XDRPROC_T_TYPE) xdr_int))
    return (FALSE);
  return (TRUE);
}

bool_t
xdr_am_mountres3(XDR *xdrs, am_mountres3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_mountres3:");

  if (!xdr_am_mountstat3(xdrs, &objp->fhs_status))
    return (FALSE);

  if (objp->fhs_status == AM_MNT3_OK) {
    if (!xdr_am_mountres3_ok(xdrs, &objp->mountres3_u.mountinfo))
      return (FALSE);
  }
  return (TRUE);
}

bool_t
xdr_am_cookieverf3(XDR *xdrs, am_cookieverf3 objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_cookieverf3:");

  if (!xdr_opaque(xdrs, objp, AM_NFS3_COOKIEVERFSIZE))
    return FALSE;
  return TRUE;
}

#if 0
/* In FreeBSD xdr_uint64() is defined in ../../../include/rpcsvc/nfs_prot.x */
/*
 * Not ideal, xdr_u_int64_t() is not defined in Linux glibc RPC
 * but xdr_u_quad_t() is. But in libtirpc xdr_u_quad_t() is not
 * defined and xdr_u_int64_t() is. So xdr_u_int64_t() is probably
 * an expected standard xdr function so, if it isn't defined use
 * an internal xdr_u_int64_t() that uses xdr_u_quad_t().
 */
#ifndef HAVE_XDR_U_INT64_T
#define xdr_u_int64_t(xdrs, objp) xdr_u_quad_t(xdrs, objp)
#endif /* HAVE_XDR_U_INT64_T */

bool_t
xdr_uint64(XDR *xdrs, uint64 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_uint64:");

  if (!xdr_u_int64_t(xdrs, objp))
    return FALSE;
  return TRUE;
}
#endif

bool_t
xdr_am_cookie3(XDR *xdrs, am_cookie3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_cookie3:");

  if (!xdr_uint64(xdrs, objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_nfs_fh3(XDR *xdrs, am_nfs_fh3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_nfs_fh3:");

  if (!xdr_u_int(xdrs, &objp->am_fh3_length))
    return (FALSE);
  if (objp->am_fh3_length > AM_FHSIZE3)
    return (FALSE);
  if (!xdr_opaque(xdrs, objp->am_fh3_data, objp->am_fh3_length))
    return (FALSE);
  return (TRUE);
}

bool_t
xdr_am_nfsstat3(XDR *xdrs, am_nfsstat3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_nfsstat3:");

  if (!xdr_enum(xdrs, (enum_t *)objp))
    return (FALSE);
  return (TRUE);
}

bool_t
xdr_am_diropargs3(XDR *xdrs, am_diropargs3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_diropargs3:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->dir))
    return (FALSE);
  if (!xdr_am_filename3(xdrs, &objp->name))
    return (FALSE);
  return (TRUE);
}

bool_t
xdr_am_filename3(XDR *xdrs, am_filename3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_filename3:");

  if (!xdr_string(xdrs, objp, ~0))
    return (FALSE);
  return (TRUE);
}

bool_t
xdr_am_ftype3(XDR *xdrs, am_ftype3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_ftype3:");

  if (!xdr_enum(xdrs, (enum_t *) objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_mode3(XDR *xdrs, am_mode3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_mode3:");

  if (!xdr_u_int(xdrs, objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_uid3(XDR *xdrs, am_uid3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_uid3:");

  if (!xdr_u_int(xdrs, objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_gid3(XDR *xdrs, am_gid3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_gid3:");

  if (!xdr_u_int(xdrs, objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_size3(XDR *xdrs, am_size3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_size3:");

  if (!xdr_uint64(xdrs, objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_fileid3(XDR *xdrs, am_fileid3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_fileid3:");

  if (!xdr_uint64(xdrs, objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_specdata3(XDR *xdrs, am_specdata3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_specdata3:");

  if (!xdr_u_int(xdrs, &objp->specdata1))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->specdata2))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_nfstime3(XDR *xdrs, am_nfstime3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_nfstime3:");

  if (!xdr_u_int(xdrs, &objp->seconds))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->nseconds))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_fattr3(XDR *xdrs, am_fattr3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_fattr3:");

  if (!xdr_am_ftype3(xdrs, &objp->type))
    return FALSE;
  if (!xdr_am_mode3(xdrs, &objp->mode))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->nlink))
    return FALSE;
  if (!xdr_am_uid3(xdrs, &objp->uid))
    return FALSE;
  if (!xdr_am_gid3(xdrs, &objp->gid))
    return FALSE;
  if (!xdr_am_size3(xdrs, &objp->size))
    return FALSE;
  if (!xdr_am_size3(xdrs, &objp->used))
    return FALSE;
  if (!xdr_am_specdata3(xdrs, &objp->rdev))
    return FALSE;
  if (!xdr_uint64(xdrs, &objp->fsid))
    return FALSE;
  if (!xdr_am_fileid3(xdrs, &objp->fileid))
    return FALSE;
  if (!xdr_am_nfstime3(xdrs, &objp->atime))
    return FALSE;
  if (!xdr_am_nfstime3(xdrs, &objp->mtime))
    return FALSE;
  if (!xdr_am_nfstime3(xdrs, &objp->ctime))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_post_op_attr(XDR *xdrs, am_post_op_attr *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_post_op_attr:");

  if (!xdr_bool(xdrs, &objp->attributes_follow))
    return FALSE;
  switch (objp->attributes_follow) {
  case TRUE:
    if (!xdr_am_fattr3(xdrs, &objp->am_post_op_attr_u.attributes))
      return FALSE;
    break;
  case FALSE:
    break;
  default:
    return FALSE;
  }
  return TRUE;
}

bool_t
xdr_am_stable_how(XDR *xdrs, am_stable_how *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_stable_how:");

  if (!xdr_enum(xdrs, (enum_t *) objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_offset3(XDR *xdrs, am_offset3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_offset3:");

  if (!xdr_uint64(xdrs, objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_count3(XDR *xdrs, am_count3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_count3:");

  if (!xdr_u_int(xdrs, objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_wcc_attr(XDR *xdrs, am_wcc_attr *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_wcc_attr:");

  if (!xdr_am_size3(xdrs, &objp->size))
    return FALSE;
  if (!xdr_am_nfstime3(xdrs, &objp->mtime))
    return FALSE;
  if (!xdr_am_nfstime3(xdrs, &objp->ctime))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_pre_op_attr(XDR *xdrs, am_pre_op_attr *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, ":xdr_am_pre_op_attr");

  if (!xdr_bool(xdrs, &objp->attributes_follow))
    return FALSE;
  switch (objp->attributes_follow) {
  case TRUE:
    if (!xdr_am_wcc_attr(xdrs, &objp->am_pre_op_attr_u.attributes))
      return FALSE;
    break;
  case FALSE:
    break;
  default:
    return FALSE;
  }
  return TRUE;
}

bool_t
xdr_am_wcc_data(XDR *xdrs, am_wcc_data *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_wcc_data:");

  if (!xdr_am_pre_op_attr(xdrs, &objp->before))
    return FALSE;
  if (!xdr_am_post_op_attr(xdrs, &objp->after))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_WRITE3args(XDR *xdrs, am_WRITE3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_WRITE3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->file))
    return FALSE;
  if (!xdr_am_offset3(xdrs, &objp->offset))
    return FALSE;
  if (!xdr_am_count3(xdrs, &objp->count))
    return FALSE;
  if (!xdr_am_stable_how(xdrs, &objp->stable))
    return FALSE;
  if (!xdr_bytes(xdrs, (char **)&objp->data.data_val,
                (u_int *) &objp->data.data_len, ~0))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_writeverf3(XDR *xdrs, am_writeverf3 objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_writeverf3:");

  if (!xdr_opaque(xdrs, objp, AM_NFS3_WRITEVERFSIZE))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_WRITE3resok(XDR *xdrs, am_WRITE3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_WRITE3resok:");

  if (!xdr_am_wcc_data(xdrs, &objp->file_wcc))
    return FALSE;
  if (!xdr_am_count3(xdrs, &objp->count))
    return FALSE;
  if (!xdr_am_stable_how(xdrs, &objp->committed))
    return FALSE;
  if (!xdr_am_writeverf3(xdrs, objp->verf))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_WRITE3resfail(XDR *xdrs, am_WRITE3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_WRITE3resfail:");

  if (!xdr_am_wcc_data(xdrs, &objp->file_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_WRITE3res(XDR *xdrs, am_WRITE3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_WRITE3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_WRITE3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_WRITE3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_LOOKUP3args(XDR *xdrs, am_LOOKUP3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_LOOKUP3args:");

  if (!xdr_am_diropargs3(xdrs, &objp->what))
    return (FALSE);
  return (TRUE);
}

bool_t
xdr_am_LOOKUP3res(XDR *xdrs, am_LOOKUP3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_LOOKUP3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return (FALSE);
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_LOOKUP3resok(xdrs, &objp->res_u.ok))
      return (FALSE);
    break;
  default:
    if (!xdr_am_LOOKUP3resfail(xdrs, &objp->res_u.fail))
      return (FALSE);
    break;
  }
  return (TRUE);
}

bool_t
xdr_am_LOOKUP3resfail(XDR *xdrs, am_LOOKUP3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_LOOKUP3resfail:");

  if (!xdr_am_post_op_attr(xdrs, &objp->dir_attributes))
    return (FALSE);
  return (TRUE);
}

bool_t
xdr_am_LOOKUP3resok(XDR *xdrs, am_LOOKUP3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_LOOKUP3resok:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->object))
    return (FALSE);
  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return (FALSE);
  if (!xdr_am_post_op_attr(xdrs, &objp->dir_attributes))
    return (FALSE);
  return (TRUE);
}

bool_t
xdr_am_COMMIT3args(XDR *xdrs, am_COMMIT3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_COMMIT3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->file))
    return FALSE;
  if (!xdr_am_offset3(xdrs, &objp->offset))
    return FALSE;
  if (!xdr_am_count3(xdrs, &objp->count))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_COMMIT3resok(XDR *xdrs, am_COMMIT3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_COMMIT3resok:");

  if (!xdr_am_wcc_data(xdrs, &objp->file_wcc))
    return FALSE;
  if (!xdr_am_writeverf3(xdrs, objp->verf))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_COMMIT3resfail(XDR *xdrs, am_COMMIT3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_COMMIT3resfail:");

  if (!xdr_am_wcc_data(xdrs, &objp->file_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_COMMIT3res(XDR *xdrs, am_COMMIT3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_COMMIT3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_COMMIT3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_COMMIT3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_ACCESS3args(XDR *xdrs, am_ACCESS3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_ACCESS3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->object))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->access))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_ACCESS3resok(XDR *xdrs, am_ACCESS3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_ACCESS3resok:");

  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->access))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_ACCESS3resfail(XDR *xdrs, am_ACCESS3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_ACCESS3resfail:");

  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_ACCESS3res(XDR *xdrs, am_ACCESS3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_ACCESS3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_ACCESS3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_ACCESS3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_GETATTR3args(XDR *xdrs, am_GETATTR3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_GETATTR3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->object))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_GETATTR3resok(XDR *xdrs, am_GETATTR3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_GETATTR3resok:");

  if (!xdr_am_fattr3(xdrs, &objp->obj_attributes))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_GETATTR3res(XDR *xdrs, am_GETATTR3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_GETATTR3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_GETATTR3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    break;
  }
  return TRUE;
}

bool_t
xdr_am_time_how(XDR *xdrs, am_time_how *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_time_how:");

  if (!xdr_enum(xdrs, (enum_t *) objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_set_mode3(XDR *xdrs, am_set_mode3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_set_mode3:");

  if (!xdr_bool(xdrs, &objp->set_it))
    return FALSE;
  switch (objp->set_it) {
  case TRUE:
    if (!xdr_am_mode3(xdrs, &objp->am_set_mode3_u.mode))
      return FALSE;
    break;
  default:
    break;
  }
  return TRUE;
}

bool_t
xdr_am_set_uid3(XDR *xdrs, am_set_uid3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_set_uid3:");

  if (!xdr_bool(xdrs, &objp->set_it))
    return FALSE;
  switch (objp->set_it) {
  case TRUE:
    if (!xdr_am_uid3(xdrs, &objp->am_set_uid3_u.uid))
      return FALSE;
    break;
  default:
    break;
  }
  return TRUE;
}

bool_t
xdr_am_set_gid3(XDR *xdrs, am_set_gid3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_set_gid3:");

  if (!xdr_bool(xdrs, &objp->set_it))
    return FALSE;
  switch (objp->set_it) {
  case TRUE:
    if (!xdr_am_gid3(xdrs, &objp->am_set_gid3_u.gid))
      return FALSE;
    break;
  default:
    break;
  }
  return TRUE;
}

bool_t
xdr_am_set_size3(XDR *xdrs, am_set_size3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_set_size3:");

  if (!xdr_bool(xdrs, &objp->set_it))
    return FALSE;
  switch (objp->set_it) {
  case TRUE:
    if (!xdr_am_size3(xdrs, &objp->am_set_size3_u.size))
      return FALSE;
    break;
  default:
    break;
  }
  return TRUE;
}

bool_t
xdr_am_set_atime(XDR *xdrs, am_set_atime *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_set_atime:");

  if (!xdr_am_time_how(xdrs, &objp->set_it))
    return FALSE;
  switch (objp->set_it) {
  case AM_SET_TO_CLIENT_TIME:
    if (!xdr_am_nfstime3(xdrs, &objp->am_set_atime_u.atime))
      return FALSE;
    break;
  default:
    break;
  }
  return TRUE;
}

bool_t
xdr_am_set_mtime(XDR *xdrs, am_set_mtime *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_set_mtime:");

  if (!xdr_am_time_how(xdrs, &objp->set_it))
    return FALSE;
  switch (objp->set_it) {
  case AM_SET_TO_CLIENT_TIME:
    if (!xdr_am_nfstime3(xdrs, &objp->am_set_mtime_u.mtime))
      return FALSE;
    break;
  default:
    break;
  }
  return TRUE;
}

bool_t
xdr_am_sattr3(XDR *xdrs, am_sattr3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_sattr3:");

  if (!xdr_am_set_mode3(xdrs, &objp->mode))
    return FALSE;
  if (!xdr_am_set_uid3(xdrs, &objp->uid))
    return FALSE;
  if (!xdr_am_set_gid3(xdrs, &objp->gid))
    return FALSE;
  if (!xdr_am_set_size3(xdrs, &objp->size))
     return FALSE;
  if (!xdr_am_set_atime(xdrs, &objp->atime))
    return FALSE;
  if (!xdr_am_set_mtime(xdrs, &objp->mtime))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_createmode3(XDR *xdrs, am_createmode3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_createmode3:");

  if (!xdr_enum(xdrs, (enum_t *) objp))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_createverf3(XDR *xdrs, am_createverf3 objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_createverf3:");

  if (!xdr_opaque(xdrs, objp, AM_NFS3_CREATEVERFSIZE))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_createhow3(XDR *xdrs, am_createhow3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_createhow3:");

   if (!xdr_am_createmode3(xdrs, &objp->mode))
     return FALSE;
  switch (objp->mode) {
  case AM_UNCHECKED:
    if (!xdr_am_sattr3(xdrs, &objp->am_createhow3_u.obj_attributes))
      return FALSE;
    break;
  case AM_GUARDED:
    if (!xdr_am_sattr3(xdrs, &objp->am_createhow3_u.g_obj_attributes))
      return FALSE;
    break;
  case AM_EXCLUSIVE:
    if (!xdr_am_createverf3(xdrs, objp->am_createhow3_u.verf))
      return FALSE;
    break;
  default:
    return FALSE;
  }
  return TRUE;
}

bool_t
xdr_am_CREATE3args(XDR *xdrs, am_CREATE3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_CREATE3args:");

  if (!xdr_am_diropargs3(xdrs, &objp->where))
    return FALSE;
  if (!xdr_am_createhow3(xdrs, &objp->how))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_post_op_fh3(XDR *xdrs, am_post_op_fh3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_post_op_fh3:");

  if (!xdr_bool(xdrs, &objp->handle_follows))
    return FALSE;
  switch (objp->handle_follows) {
  case TRUE:
    if (!xdr_am_nfs_fh3(xdrs, &objp->am_post_op_fh3_u.handle))
      return FALSE;
    break;
  case FALSE:
    break;
  default:
    return FALSE;
  }
  return TRUE;
}

bool_t
xdr_am_CREATE3resok(XDR *xdrs, am_CREATE3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_CREATE3resok:");

  if (!xdr_am_post_op_fh3(xdrs, &objp->obj))
    return FALSE;
  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_CREATE3resfail(XDR *xdrs, am_CREATE3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_CREATE3resfail:");

  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_CREATE3res(XDR *xdrs, am_CREATE3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_CREATE3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_CREATE3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_CREATE3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_REMOVE3args(XDR *xdrs, am_REMOVE3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_REMOVE3args:");

  if (!xdr_am_diropargs3(xdrs, &objp->object))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_REMOVE3resok(XDR *xdrs, am_REMOVE3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_REMOVE3resok:");

  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_REMOVE3resfail(XDR *xdrs, am_REMOVE3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_REMOVE3resfail:");

  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_REMOVE3res(XDR *xdrs, am_REMOVE3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_REMOVE3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_REMOVE3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_REMOVE3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_READ3args(XDR *xdrs, am_READ3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READ3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->file))
    return FALSE;
  if (!xdr_am_offset3(xdrs, &objp->offset))
    return FALSE;
  if (!xdr_am_count3(xdrs, &objp->count))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READ3resok(XDR *xdrs, am_READ3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READ3resok:");

  if (!xdr_am_post_op_attr(xdrs, &objp->file_attributes))
    return FALSE;
  if (!xdr_am_count3(xdrs, &objp->count))
    return FALSE;
  if (!xdr_bool(xdrs, &objp->eof))
    return FALSE;
  if (!xdr_bytes(xdrs, (char **)&objp->data.data_val, (u_int *) &objp->data.data_len, ~0))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READ3resfail(XDR *xdrs, am_READ3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READ3resfail:");

  if (!xdr_am_post_op_attr(xdrs, &objp->file_attributes))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READ3res(XDR *xdrs, am_READ3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READ3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_READ3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_READ3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_FSINFO3args(XDR *xdrs, am_FSINFO3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_FSINFO3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->fsroot))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_FSINFO3resok(XDR *xdrs, am_FSINFO3resok *objp)
{
  register int32_t *buf;

  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_FSINFO3resok:");

  if (xdrs->x_op == XDR_ENCODE) {
    if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
      return FALSE;
    buf = XDR_INLINE(xdrs, 7 * BYTES_PER_XDR_UNIT);
    if (buf == NULL) {
      if (!xdr_u_int(xdrs, &objp->rtmax))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->rtpref))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->rtmult))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->wtmax))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->wtpref))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->wtmult))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->dtpref))
        return FALSE;
    } else {
      IXDR_PUT_U_LONG(buf, objp->rtmax);
      IXDR_PUT_U_LONG(buf, objp->rtpref);
      IXDR_PUT_U_LONG(buf, objp->rtmult);
      IXDR_PUT_U_LONG(buf, objp->wtmax);
      IXDR_PUT_U_LONG(buf, objp->wtpref);
      IXDR_PUT_U_LONG(buf, objp->wtmult);
      IXDR_PUT_U_LONG(buf, objp->dtpref);
    }
    if (!xdr_am_size3(xdrs, &objp->maxfilesize))
      return FALSE;
    if (!xdr_am_nfstime3(xdrs, &objp->time_delta))
      return FALSE;
    if (!xdr_u_int(xdrs, &objp->properties))
      return FALSE;
    return TRUE;
  } else if (xdrs->x_op == XDR_DECODE) {
    if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
      return FALSE;
    buf = XDR_INLINE(xdrs, 7 * BYTES_PER_XDR_UNIT);
    if (buf == NULL) {
      if (!xdr_u_int (xdrs, &objp->rtmax))
        return FALSE;
      if (!xdr_u_int (xdrs, &objp->rtpref))
        return FALSE;
      if (!xdr_u_int (xdrs, &objp->rtmult))
        return FALSE;
      if (!xdr_u_int (xdrs, &objp->wtmax))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->wtpref))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->wtmult))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->dtpref))
        return FALSE;
    } else {
      objp->rtmax = IXDR_GET_U_LONG(buf);
      objp->rtpref = IXDR_GET_U_LONG(buf);
      objp->rtmult = IXDR_GET_U_LONG(buf);
      objp->wtmax = IXDR_GET_U_LONG(buf);
      objp->wtpref = IXDR_GET_U_LONG(buf);
      objp->wtmult = IXDR_GET_U_LONG(buf);
      objp->dtpref = IXDR_GET_U_LONG(buf);
    }
    if (!xdr_am_size3(xdrs, &objp->maxfilesize))
      return FALSE;
    if (!xdr_am_nfstime3(xdrs, &objp->time_delta))
      return FALSE;
    if (!xdr_u_int(xdrs, &objp->properties))
      return FALSE;
    return TRUE;
  }

  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->rtmax))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->rtpref))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->rtmult))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->wtmax))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->wtpref))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->wtmult))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->dtpref))
   return FALSE;
  if (!xdr_am_size3(xdrs, &objp->maxfilesize))
    return FALSE;
  if (!xdr_am_nfstime3(xdrs, &objp->time_delta))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->properties))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_FSINFO3resfail(XDR *xdrs, am_FSINFO3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_FSINFO3resfail:");

  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_FSINFO3res(XDR *xdrs, am_FSINFO3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_FSINFO3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_FSINFO3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_FSINFO3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_FSSTAT3args(XDR *xdrs, am_FSSTAT3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_FSSTAT3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->fsroot))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_FSSTAT3resok(XDR *xdrs, am_FSSTAT3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_FSSTAT3resok:");

  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  if (!xdr_am_size3(xdrs, &objp->tbytes))
    return FALSE;
  if (!xdr_am_size3(xdrs, &objp->fbytes))
    return FALSE;
  if (!xdr_am_size3(xdrs, &objp->abytes))
    return FALSE;
  if (!xdr_am_size3(xdrs, &objp->tfiles))
    return FALSE;
  if (!xdr_am_size3(xdrs, &objp->ffiles))
    return FALSE;
  if (!xdr_am_size3(xdrs, &objp->afiles))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->invarsec))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_FSSTAT3resfail(XDR *xdrs, am_FSSTAT3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_FSSTAT3resfail:");

  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_FSSTAT3res(XDR *xdrs, am_FSSTAT3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_FSSTAT3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_FSSTAT3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_FSSTAT3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_PATHCONF3args(XDR *xdrs, am_PATHCONF3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_PATHCONF3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->object))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_PATHCONF3resok(XDR *xdrs, am_PATHCONF3resok *objp)
{
  register int32_t *buf;

  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_PATHCONF3resok:");

  if (xdrs->x_op == XDR_ENCODE) {
    if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
      return FALSE;
    buf = XDR_INLINE(xdrs, 6 * BYTES_PER_XDR_UNIT);
    if (buf == NULL) {
      if (!xdr_u_int(xdrs, &objp->linkmax))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->name_max))
        return FALSE;
      if (!xdr_bool(xdrs, &objp->no_trunc))
        return FALSE;
      if (!xdr_bool(xdrs, &objp->chown_restricted))
        return FALSE;
      if (!xdr_bool(xdrs, &objp->case_insensitive))
        return FALSE;
      if (!xdr_bool(xdrs, &objp->case_preserving))
        return FALSE;
    } else {
      IXDR_PUT_U_LONG(buf, objp->linkmax);
      IXDR_PUT_U_LONG(buf, objp->name_max);
      IXDR_PUT_BOOL(buf, objp->no_trunc);
      IXDR_PUT_BOOL(buf, objp->chown_restricted);
      IXDR_PUT_BOOL(buf, objp->case_insensitive);
      IXDR_PUT_BOOL(buf, objp->case_preserving);
    }
    return TRUE;
  } else if (xdrs->x_op == XDR_DECODE) {
    if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
      return FALSE;
    buf = XDR_INLINE(xdrs, 6 * BYTES_PER_XDR_UNIT);
    if (buf == NULL) {
      if (!xdr_u_int(xdrs, &objp->linkmax))
        return FALSE;
      if (!xdr_u_int(xdrs, &objp->name_max))
        return FALSE;
      if (!xdr_bool(xdrs, &objp->no_trunc))
        return FALSE;
      if (!xdr_bool(xdrs, &objp->chown_restricted))
        return FALSE;
      if (!xdr_bool(xdrs, &objp->case_insensitive))
        return FALSE;
      if (!xdr_bool(xdrs, &objp->case_preserving))
        return FALSE;
    } else {
      objp->linkmax = IXDR_GET_U_LONG(buf);
      objp->name_max = IXDR_GET_U_LONG(buf);
      objp->no_trunc = IXDR_GET_BOOL(buf);
      objp->chown_restricted = IXDR_GET_BOOL(buf);
      objp->case_insensitive = IXDR_GET_BOOL(buf);
      objp->case_preserving = IXDR_GET_BOOL(buf);
    }
    return TRUE;
  }

  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->linkmax))
    return FALSE;
  if (!xdr_u_int(xdrs, &objp->name_max))
    return FALSE;
  if (!xdr_bool(xdrs, &objp->no_trunc))
    return FALSE;
  if (!xdr_bool(xdrs, &objp->chown_restricted))
    return FALSE;
  if (!xdr_bool(xdrs, &objp->case_insensitive))
    return FALSE;
  if (!xdr_bool(xdrs, &objp->case_preserving))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_PATHCONF3resfail(XDR *xdrs, am_PATHCONF3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_PATHCONF3resfail:");

  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_PATHCONF3res(XDR *xdrs, am_PATHCONF3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_PATHCONF3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_PATHCONF3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_PATHCONF3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_nfspath3(XDR *xdrs, am_nfspath3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_nfspath3:");

  if (!xdr_string(xdrs, objp, ~0))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_symlinkdata3(XDR *xdrs, am_symlinkdata3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_symlinkdata3:");

  if (!xdr_am_sattr3(xdrs, &objp->symlink_attributes))
    return FALSE;
  if (!xdr_am_nfspath3(xdrs, &objp->symlink_data))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_SYMLINK3args(XDR *xdrs, am_SYMLINK3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_SYMLINK3args:");

  if (!xdr_am_diropargs3(xdrs, &objp->where))
    return FALSE;
  if (!xdr_am_symlinkdata3(xdrs, &objp->symlink))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_SYMLINK3resok(XDR *xdrs, am_SYMLINK3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_SYMLINK3resok:");

  if (!xdr_am_post_op_fh3(xdrs, &objp->obj))
    return FALSE;
  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_SYMLINK3resfail(XDR *xdrs, am_SYMLINK3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_SYMLINK3resfail:");

  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_SYMLINK3res(XDR *xdrs, am_SYMLINK3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_SYMLINK3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_SYMLINK3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_SYMLINK3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_READLINK3args(XDR *xdrs, am_READLINK3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READLINK3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->symlink))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READLINK3resok(XDR *xdrs, am_READLINK3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READLINK3resok:");

  if (!xdr_am_post_op_attr(xdrs, &objp->symlink_attributes))
    return FALSE;
  if (!xdr_am_nfspath3(xdrs, &objp->data))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READLINK3resfail(XDR *xdrs, am_READLINK3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READLINK3resfail:");

  if (!xdr_am_post_op_attr(xdrs, &objp->symlink_attributes))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READLINK3res(XDR *xdrs, am_READLINK3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READLINK3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_READLINK3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_READLINK3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_devicedata3(XDR *xdrs, am_devicedata3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_devicedata3:");

  if (!xdr_am_sattr3(xdrs, &objp->dev_attributes))
    return FALSE;
  if (!xdr_am_specdata3(xdrs, &objp->spec))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_mknoddata3(XDR *xdrs, am_mknoddata3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_mknoddata3:");

  if (!xdr_am_ftype3(xdrs, &objp->type))
    return FALSE;
  switch (objp->type) {
  case AM_NF3CHR:
    if (!xdr_am_devicedata3(xdrs, &objp->am_mknoddata3_u.chr_device))
      return FALSE;
    break;
  case AM_NF3BLK:
    if (!xdr_am_devicedata3(xdrs, &objp->am_mknoddata3_u.blk_device))
      return FALSE;
    break;
  case AM_NF3SOCK:
    if (!xdr_am_sattr3(xdrs, &objp->am_mknoddata3_u.sock_attributes))
      return FALSE;
    break;
  case AM_NF3FIFO:
    if (!xdr_am_sattr3(xdrs, &objp->am_mknoddata3_u.pipe_attributes))
      return FALSE;
    break;
  default:
    break;
  }
  return TRUE;
}

bool_t
xdr_am_MKNOD3args(XDR *xdrs, am_MKNOD3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_MKNOD3args:");

  if (!xdr_am_diropargs3(xdrs, &objp->where))
    return FALSE;
  if (!xdr_am_mknoddata3(xdrs, &objp->what))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_MKNOD3resok(XDR *xdrs, am_MKNOD3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_MKNOD3resok:");

  if (!xdr_am_post_op_fh3(xdrs, &objp->obj))
    return FALSE;
  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_MKNOD3resfail(XDR *xdrs, am_MKNOD3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_MKNOD3resfail:");

  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_MKNOD3res(XDR *xdrs, am_MKNOD3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, ":");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_MKNOD3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_MKNOD3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_MKDIR3args(XDR *xdrs, am_MKDIR3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, ":");

  if (!xdr_am_diropargs3(xdrs, &objp->where))
    return FALSE;
  if (!xdr_am_sattr3(xdrs, &objp->attributes))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_MKDIR3resok(XDR *xdrs, am_MKDIR3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_MKDIR3resok:");

  if (!xdr_am_post_op_fh3(xdrs, &objp->obj))
    return FALSE;
  if (!xdr_am_post_op_attr(xdrs, &objp->obj_attributes))
    return FALSE;
  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_MKDIR3resfail(XDR *xdrs, am_MKDIR3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_MKDIR3resfail:");

  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_MKDIR3res(XDR *xdrs, am_MKDIR3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_MKDIR3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_MKDIR3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_MKDIR3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_RMDIR3args(XDR *xdrs, am_RMDIR3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_RMDIR3args:");

  if (!xdr_am_diropargs3(xdrs, &objp->object))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_RMDIR3resok(XDR *xdrs, am_RMDIR3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_RMDIR3resok:");

  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_RMDIR3resfail(XDR *xdrs, am_RMDIR3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_RMDIR3resfail:");

  if (!xdr_am_wcc_data(xdrs, &objp->dir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_RMDIR3res(XDR *xdrs, am_RMDIR3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_RMDIR3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_RMDIR3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_RMDIR3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_RENAME3args(XDR *xdrs, am_RENAME3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_RENAME3args:");

  if (!xdr_am_diropargs3(xdrs, &objp->from))
    return FALSE;
  if (!xdr_am_diropargs3(xdrs, &objp->to))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_RENAME3resok(XDR *xdrs, am_RENAME3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_RENAME3resok:");

  if (!xdr_am_wcc_data(xdrs, &objp->fromdir_wcc))
    return FALSE;
  if (!xdr_am_wcc_data(xdrs, &objp->todir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_RENAME3resfail(XDR *xdrs, am_RENAME3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_RENAME3resfail:");

  if (!xdr_am_wcc_data(xdrs, &objp->fromdir_wcc))
    return FALSE;
  if (!xdr_am_wcc_data(xdrs, &objp->todir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_RENAME3res(XDR *xdrs, am_RENAME3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_RENAME3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_RENAME3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_RENAME3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_READDIRPLUS3args(XDR *xdrs, am_READDIRPLUS3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READDIRPLUS3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->dir))
    return FALSE;
  if (!xdr_am_cookie3(xdrs, &objp->cookie))
    return FALSE;
  if (!xdr_am_cookieverf3(xdrs, objp->cookieverf))
    return FALSE;
  if (!xdr_am_count3(xdrs, &objp->dircount))
    return FALSE;
  if (!xdr_am_count3(xdrs, &objp->maxcount))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_entryplus3(XDR *xdrs, am_entryplus3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_entryplus3:");

   if (!xdr_am_fileid3(xdrs, &objp->fileid))
     return FALSE;
   if (!xdr_am_filename3(xdrs, &objp->name))
     return FALSE;
   if (!xdr_am_cookie3(xdrs, &objp->cookie))
     return FALSE;
   if (!xdr_am_post_op_attr(xdrs, &objp->name_attributes))
     return FALSE;
   if (!xdr_am_post_op_fh3(xdrs, &objp->name_handle))
     return FALSE;
   if (!xdr_pointer(xdrs, (char **)&objp->nextentry,
                   sizeof(am_entryplus3), (xdrproc_t) xdr_am_entryplus3))
     return FALSE;
  return TRUE;
}

bool_t
xdr_am_dirlistplus3(XDR *xdrs, am_dirlistplus3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_dirlistplus3:");

  if (!xdr_pointer(xdrs, (char **)&objp->entries,
                   sizeof(am_entryplus3), (xdrproc_t) xdr_am_entryplus3))
    return FALSE;
  if (!xdr_bool(xdrs, &objp->eof))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READDIRPLUS3resok(XDR *xdrs, am_READDIRPLUS3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READDIRPLUS3resok:");

  if (!xdr_am_post_op_attr(xdrs, &objp->dir_attributes))
    return FALSE;
  if (!xdr_am_cookieverf3(xdrs, objp->cookieverf))
    return FALSE;
  if (!xdr_am_dirlistplus3(xdrs, &objp->reply))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READDIRPLUS3resfail(XDR *xdrs, am_READDIRPLUS3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READDIRPLUS3resfail:");

  if (!xdr_am_post_op_attr(xdrs, &objp->dir_attributes))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READDIRPLUS3res(XDR *xdrs, am_READDIRPLUS3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READDIRPLUS3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_READDIRPLUS3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_READDIRPLUS3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_READDIR3args(XDR *xdrs, am_READDIR3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READDIR3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->dir))
    return FALSE;
  if (!xdr_am_cookie3(xdrs, &objp->cookie))
    return FALSE;
  if (!xdr_am_cookieverf3(xdrs, objp->cookieverf))
    return FALSE;
  if (!xdr_am_count3(xdrs, &objp->count))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_entry3(XDR *xdrs, am_entry3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_entry3:");

  if (!xdr_am_fileid3(xdrs, &objp->fileid))
    return FALSE;
  if (!xdr_am_filename3(xdrs, &objp->name))
    return FALSE;
  if (!xdr_am_cookie3(xdrs, &objp->cookie))
    return FALSE;
  if (!xdr_pointer(xdrs, (char **)&objp->nextentry,
                   sizeof(am_entry3), (xdrproc_t) xdr_am_entry3))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_dirlist3(XDR *xdrs, am_dirlist3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_dirlist3:");

  if (!xdr_pointer(xdrs, (char **)&objp->entries,
                   sizeof(am_entry3), (xdrproc_t) xdr_am_entry3))
    return FALSE;
  if (!xdr_bool (xdrs, &objp->eof))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READDIR3resok(XDR *xdrs, am_READDIR3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READDIR3resok:");

  if (!xdr_am_post_op_attr(xdrs, &objp->dir_attributes))
    return FALSE;
  if (!xdr_am_cookieverf3(xdrs, objp->cookieverf))
    return FALSE;
  if (!xdr_am_dirlist3(xdrs, &objp->reply))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READDIR3resfail(XDR *xdrs, am_READDIR3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READDIR3resfail:");

  if (!xdr_am_post_op_attr(xdrs, &objp->dir_attributes))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_READDIR3res(XDR *xdrs, am_READDIR3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_READDIR3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_READDIR3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_READDIR3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_LINK3args(XDR *xdrs, am_LINK3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_LINK3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->file))
    return FALSE;
  if (!xdr_am_diropargs3(xdrs, &objp->link))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_LINK3resok(XDR *xdrs, am_LINK3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_LINK3resok:");

  if (!xdr_am_post_op_attr(xdrs, &objp->file_attributes))
    return FALSE;
  if (!xdr_am_wcc_data(xdrs, &objp->linkdir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_LINK3resfail(XDR *xdrs, am_LINK3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_LINK3resfail:");

  if (!xdr_am_post_op_attr(xdrs, &objp->file_attributes))
    return FALSE;
  if (!xdr_am_wcc_data(xdrs, &objp->linkdir_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_LINK3res(XDR *xdrs, am_LINK3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_LINK3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_LINK3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_LINK3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}

bool_t
xdr_am_sattrguard3(XDR *xdrs, am_sattrguard3 *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_sattrguard3:");

  if (!xdr_bool(xdrs, &objp->check))
    return FALSE;
  switch (objp->check) {
  case TRUE:
    if (!xdr_am_nfstime3(xdrs, &objp->am_sattrguard3_u.obj_ctime))
      return FALSE;
    break;
  case FALSE:
    break;
  default:
    return FALSE;
  }
  return TRUE;
}

bool_t
xdr_am_SETATTR3args(XDR *xdrs, am_SETATTR3args *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_SETATTR3args:");

  if (!xdr_am_nfs_fh3(xdrs, &objp->object))
    return FALSE;
  if (!xdr_am_sattr3(xdrs, &objp->new_attributes))
    return FALSE;
  if (!xdr_am_sattrguard3(xdrs, &objp->guard))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_SETATTR3resok(XDR *xdrs, am_SETATTR3resok *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_SETATTR3resok:");

  if (!xdr_am_wcc_data(xdrs, &objp->obj_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_SETATTR3resfail(XDR *xdrs, am_SETATTR3resfail *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_SETATTR3resfail:");

  if (!xdr_am_wcc_data(xdrs, &objp->obj_wcc))
    return FALSE;
  return TRUE;
}

bool_t
xdr_am_SETATTR3res(XDR *xdrs, am_SETATTR3res *objp)
{
  if (amuDebug(D_XDRTRACE))
    plog(XLOG_DEBUG, "xdr_am_SETATTR3res:");

  if (!xdr_am_nfsstat3(xdrs, &objp->status))
    return FALSE;
  switch (objp->status) {
  case AM_NFS3_OK:
    if (!xdr_am_SETATTR3resok(xdrs, &objp->res_u.ok))
      return FALSE;
    break;
  default:
    if (!xdr_am_SETATTR3resfail(xdrs, &objp->res_u.fail))
      return FALSE;
    break;
  }
  return TRUE;
}
#endif /* HAVE_FS_NFS3 */
