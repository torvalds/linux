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
 * File: am-utils/conf/nfs_prot/nfs_prot_freebsd3.h
 * $Id: nfs_prot_freebsd3.h,v 1.5.2.7 2004/01/06 03:15:19 ezk Exp $
 * $FreeBSD$
 *
 */

#ifndef _AMU_NFS_PROT_H
#define _AMU_NFS_PROT_H

/* nfs_prot.h defines struct `nfs_fh3', but it is a ``dmr "unwarranted
 * chumminess with the C implementation".  We need the more complete
 * structure, which is defined below.  So get the stock `nfs_fh3'
 * out of the way.
 */
struct nfs_fh3;
#define nfs_fh3 nfs_fh3_fbsd_

#ifdef HAVE_RPCSVC_NFS_PROT_H
# include <rpcsvc/nfs_prot.h>
#endif /* HAVE_RPCSVC_NFS_PROT_H */

#undef nfs_fh3

#ifdef HAVE_NFS_RPCV2_H
# include <nfs/rpcv2.h>
#endif /* HAVE_NFS_RPCV2_H */
#ifdef HAVE_NFS_NFS_H
# include <nfsclient/nfs.h>
# include <nfsserver/nfs.h>
#endif /* HAVE_NFS_NFS_H */
#ifdef	HAVE_UFS_UFS_UFSMOUNT_H
# ifdef HAVE_UFS_UFS_EXTATTR_H
/*
 * Define a dummy struct ufs_extattr_per_mount, which is used in struct
 * ufsmount in <ufs/ufs/ufsmount.h>.
 */
struct ufs_extattr_per_mount;
# endif /* HAVE_UFS_UFS_EXTATTR_H */
# include <ufs/ufs/ufsmount.h>
#endif	/* HAVE_UFS_UFS_UFSMOUNT_H */

/* nfsclient/nfsargs.h was introduced in FreeBSD 5.0, and is needed */
#ifdef	HAVE_NFSCLIENT_NFSARGS_H
# include <nfsclient/nfsargs.h>
#endif	/* HAVE_NFSCLIENT_NFSARGS_H */

/*
 * MACROS:
 */
#define	dr_drok_u	diropres
#define ca_where	where
#define da_fhandle	dir
#define da_name		name
#define dl_entries	entries
#define dl_eof		eof
#define dr_status	status
#define dr_u		diropres_u
#define drok_attributes	attributes
#define drok_fhandle	file
#define fh_data		data
#define la_fhandle	from
#define la_to		to
#define na_atime	atime
#define na_ctime	ctime
#define na_fileid	fileid
#define na_fsid		fsid
#define na_gid		gid
#define na_mode		mode
#define na_mtime	mtime
#define na_nlink	nlink
#define na_rdev		rdev
#define na_size		size
#define na_type		type
#define na_uid		uid
#define ne_cookie	cookie
#define ne_fileid	fileid
#define ne_name		name
#define ne_nextentry	nextentry
#define ns_attr_u	attributes
#define ns_status	status
#define ns_u		attrstat_u
#define nt_seconds	seconds
#define nt_useconds	useconds
#define rda_cookie	cookie
#define rda_count	count
#define rda_fhandle	dir
#define rdr_reply_u	reply
#define rdr_status	status
#define rdr_u		readdirres_u
#define rlr_data_u	data
#define rlr_status	status
#define rlr_u		readlinkres_u
#define rna_from	from
#define rna_to		to
#define rr_status	status
#define sag_fhandle	file
#define sfr_reply_u	reply
#define sfr_status	status
#define sfr_u		statfsres_u
#define sfrok_bavail	bavail
#define sfrok_bfree	bfree
#define sfrok_blocks	blocks
#define sfrok_bsize	bsize
#define sfrok_tsize	tsize
#define sla_from	from
#define wra_fhandle	file


/*
 * TYPEDEFS:
 */
typedef attrstat nfsattrstat;
typedef createargs nfscreateargs;
typedef dirlist nfsdirlist;
typedef diropargs nfsdiropargs;
typedef diropres nfsdiropres;
typedef entry nfsentry;
typedef fattr nfsfattr;
typedef ftype nfsftype;
typedef linkargs nfslinkargs;
typedef readargs nfsreadargs;
typedef readdirargs nfsreaddirargs;
typedef readdirres nfsreaddirres;
typedef readlinkres nfsreadlinkres;
typedef readres nfsreadres;
typedef renameargs nfsrenameargs;
typedef sattrargs nfssattrargs;
typedef statfsokres nfsstatfsokres;
typedef statfsres nfsstatfsres;
typedef symlinkargs nfssymlinkargs;
typedef writeargs nfswriteargs;


/*
 *
 * FreeBSD-3.0-RELEASE has NFS V3.  Older versions had it only defined
 * in the rpcgen source file.  If you are on an older system, and you
 * want NFSv3 support, you need to regenerate the rpcsvc header files as
 * follows:
 *	cd /usr/include/rpcsvc
 *	rpcgen -h -C -DWANT_NFS3 mount.x
 *	rpcgen -h -C -DWANT_NFS3 nfs_prot.x
 * If you don't want NFSv3, then you will have to turn off the NFSMNT_NFSV3
 * macro below.  If the code doesn't compile, upgrade to the latest 3.0
 * version...
 */
#ifdef NFSMNT_NFSV3

# define MOUNT_NFS3	"nfs"	/* is this right? */
# define MNTOPT_NFS3	"nfs"

# ifndef HAVE_XDR_LOOKUP3RES
/*
 * FreeBSD uses different field names than are defined on most other
 * systems.
 */
#  define AMU_LOOKUP3RES_OK(x)		((x)->LOOKUP3res_u.resok)
#  define AMU_LOOKUP3RES_FAIL(x)	((x)->LOOKUP3res_u.resfail)
#  define AMU_LOOKUP3RES_FH_LEN(x)	(AMU_LOOKUP3RES_OK(x).object.data.data_len)
#  define AMU_LOOKUP3RES_FH_DATA(x)	(AMU_LOOKUP3RES_OK(x).object.data.data_val)
# endif /* not HAVE_XDR_LOOKUP3RES */

/* since we don't use Am-utils's aux/macros/struct_nfs_fh3.m4, we don't get
   their special searching.  So restore the standard name. */
typedef struct nfs_fh3_freebsd3 nfs_fh3;

#endif /* NFSMNT_NFSV3 */

#endif /* not _AMU_NFS_PROT_H */
