/*	NetBSD: nfs.h,v 1.1 1996/05/23 22:49:53 fvdl Exp 	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)nfsproto.h	8.2 (Berkeley) 3/30/95
 */

/*
 * nfs definitions as per the Version 2 and 3 specs
 */

/*
 * Constants as defined in the Sun NFS Version 2 and 3 specs.
 * "NFS: Network File System Protocol Specification" RFC1094
 * and in the "NFS: Network File System Version 3 Protocol
 * Specification"
 */

#define NFS_PORT	2049
#define	NFS_PROG	100003
#define NFS_VER2	2
#define	NFS_VER3	3
#define NFS_V2MAXDATA	8192
#define	NFS_MAXDGRAMDATA 16384
#define	NFS_MAXDATA	32768
#define	NFS_MAXPATHLEN	1024
#define	NFS_MAXNAMLEN	255
#define	NFS_MAXPKTHDR	404
#define NFS_MAXPACKET	(NFS_MAXPKTHDR + NFS_MAXDATA)
#define	NFS_MINPACKET	20
#define	NFS_FABLKSIZE	512	/* Size in bytes of a block wrt fa_blocks */

/* Stat numbers for rpc returns (version 2 and 3) */
#define	NFS_OK			0
#define	NFSERR_PERM		1
#define	NFSERR_NOENT		2
#define	NFSERR_IO		5
#define	NFSERR_NXIO		6
#define	NFSERR_ACCES		13
#define	NFSERR_EXIST		17
#define	NFSERR_XDEV		18	/* Version 3 only */
#define	NFSERR_NODEV		19
#define	NFSERR_NOTDIR		20
#define	NFSERR_ISDIR		21
#define	NFSERR_INVAL		22	/* Version 3 only */
#define	NFSERR_FBIG		27
#define	NFSERR_NOSPC		28
#define	NFSERR_ROFS		30
#define	NFSERR_MLINK		31	/* Version 3 only */
#define	NFSERR_NAMETOL		63
#define	NFSERR_NOTEMPTY		66
#define	NFSERR_DQUOT		69
#define	NFSERR_STALE		70
#define	NFSERR_REMOTE		71	/* Version 3 only */
#define	NFSERR_WFLUSH		99	/* Version 2 only */
#define	NFSERR_BADHANDLE	10001	/* The rest Version 3 only */
#define	NFSERR_NOT_SYNC		10002
#define	NFSERR_BAD_COOKIE	10003
#define	NFSERR_NOTSUPP		10004
#define	NFSERR_TOOSMALL		10005
#define	NFSERR_SERVERFAULT	10006
#define	NFSERR_BADTYPE		10007
#define	NFSERR_JUKEBOX		10008
#define NFSERR_TRYLATER		NFSERR_JUKEBOX
#define	NFSERR_STALEWRITEVERF	30001	/* Fake return for nfs_commit() */

#define NFSERR_RETVOID		0x20000000 /* Return void, not error */
#define NFSERR_AUTHERR		0x40000000 /* Mark an authentication error */
#define NFSERR_RETERR		0x80000000 /* Mark an error return for V3 */

/* Sizes in bytes of various nfs rpc components */
#define	NFSX_UNSIGNED	4

/* specific to NFS Version 2 */
#define	NFSX_V2FH	32
#define	NFSX_V2FATTR	68
#define	NFSX_V2SATTR	32
#define	NFSX_V2COOKIE	4
#define NFSX_V2STATFS	20

/* specific to NFS Version 3 */
#if 0
#define NFSX_V3FH		(sizeof (fhandle_t)) /* size this server uses */
#endif
#define	NFSX_V3FHMAX		64	/* max. allowed by protocol */
#define NFSX_V3FATTR		84
#define NFSX_V3SATTR		60	/* max. all fields filled in */
#define NFSX_V3SRVSATTR		(sizeof (struct nfsv3_sattr))
#define NFSX_V3POSTOPATTR	(NFSX_V3FATTR + NFSX_UNSIGNED)
#define NFSX_V3WCCDATA		(NFSX_V3POSTOPATTR + 8 * NFSX_UNSIGNED)
#define NFSX_V3COOKIEVERF 	8
#define NFSX_V3WRITEVERF 	8
#define NFSX_V3CREATEVERF	8
#define NFSX_V3STATFS		52
#define NFSX_V3FSINFO		48
#define NFSX_V3PATHCONF		24

/* variants for both versions */
#define NFSX_FH(v3)		((v3) ? (NFSX_V3FHMAX + NFSX_UNSIGNED) : \
					NFSX_V2FH)
#define NFSX_SRVFH(v3)		((v3) ? NFSX_V3FH : NFSX_V2FH)
#define	NFSX_FATTR(v3)		((v3) ? NFSX_V3FATTR : NFSX_V2FATTR)
#define NFSX_PREOPATTR(v3)	((v3) ? (7 * NFSX_UNSIGNED) : 0)
#define NFSX_POSTOPATTR(v3)	((v3) ? (NFSX_V3FATTR + NFSX_UNSIGNED) : 0)
#define NFSX_POSTOPORFATTR(v3)	((v3) ? (NFSX_V3FATTR + NFSX_UNSIGNED) : \
					NFSX_V2FATTR)
#define NFSX_WCCDATA(v3)	((v3) ? NFSX_V3WCCDATA : 0)
#define NFSX_WCCORFATTR(v3)	((v3) ? NFSX_V3WCCDATA : NFSX_V2FATTR)
#define	NFSX_SATTR(v3)		((v3) ? NFSX_V3SATTR : NFSX_V2SATTR)
#define	NFSX_COOKIEVERF(v3)	((v3) ? NFSX_V3COOKIEVERF : 0)
#define	NFSX_WRITEVERF(v3)	((v3) ? NFSX_V3WRITEVERF : 0)
#define NFSX_READDIR(v3)	((v3) ? (5 * NFSX_UNSIGNED) : \
					(2 * NFSX_UNSIGNED))
#define	NFSX_STATFS(v3)		((v3) ? NFSX_V3STATFS : NFSX_V2STATFS)

/* nfs rpc procedure numbers (before version mapping) */
#define	NFSPROC_NULL		0
#define	NFSPROC_GETATTR		1
#define	NFSPROC_SETATTR		2
#define	NFSPROC_LOOKUP		3
#define	NFSPROC_ACCESS		4
#define	NFSPROC_READLINK	5
#define	NFSPROC_READ		6
#define	NFSPROC_WRITE		7
#define	NFSPROC_CREATE		8
#define	NFSPROC_MKDIR		9
#define	NFSPROC_SYMLINK		10
#define	NFSPROC_MKNOD		11
#define	NFSPROC_REMOVE		12
#define	NFSPROC_RMDIR		13
#define	NFSPROC_RENAME		14
#define	NFSPROC_LINK		15
#define	NFSPROC_READDIR		16
#define	NFSPROC_READDIRPLUS	17
#define	NFSPROC_FSSTAT		18
#define	NFSPROC_FSINFO		19
#define	NFSPROC_PATHCONF	20
#define	NFSPROC_COMMIT		21

/* And leasing (nqnfs) procedure numbers (must be last) */
#define	NQNFSPROC_GETLEASE	22
#define	NQNFSPROC_VACATED	23
#define	NQNFSPROC_EVICTED	24

#define NFSPROC_NOOP		25
#define	NFS_NPROCS		26

/* Actual Version 2 procedure numbers */
#define	NFSV2PROC_NULL		0
#define	NFSV2PROC_GETATTR	1
#define	NFSV2PROC_SETATTR	2
#define	NFSV2PROC_NOOP		3
#define	NFSV2PROC_ROOT		NFSV2PROC_NOOP	/* Obsolete */
#define	NFSV2PROC_LOOKUP	4
#define	NFSV2PROC_READLINK	5
#define	NFSV2PROC_READ		6
#define	NFSV2PROC_WRITECACHE	NFSV2PROC_NOOP	/* Obsolete */
#define	NFSV2PROC_WRITE		8
#define	NFSV2PROC_CREATE	9
#define	NFSV2PROC_REMOVE	10
#define	NFSV2PROC_RENAME	11
#define	NFSV2PROC_LINK		12
#define	NFSV2PROC_SYMLINK	13
#define	NFSV2PROC_MKDIR		14
#define	NFSV2PROC_RMDIR		15
#define	NFSV2PROC_READDIR	16
#define	NFSV2PROC_STATFS	17

/*
 * Constants used by the Version 3 protocol for various RPCs
 */
#define NFSV3SATTRTIME_DONTCHANGE	0
#define NFSV3SATTRTIME_TOSERVER		1
#define NFSV3SATTRTIME_TOCLIENT		2

#define NFSV3ATTRTIME_NMODES		3

#define NFSV3ACCESS_READ		0x01
#define NFSV3ACCESS_LOOKUP		0x02
#define NFSV3ACCESS_MODIFY		0x04
#define NFSV3ACCESS_EXTEND		0x08
#define NFSV3ACCESS_DELETE		0x10
#define NFSV3ACCESS_EXECUTE		0x20
#define NFSV3ACCESS_FULL		0x3f

#define NFSV3WRITE_UNSTABLE		0
#define NFSV3WRITE_DATASYNC		1
#define NFSV3WRITE_FILESYNC		2

#define NFSV3WRITE_NMODES		3

#define NFSV3CREATE_UNCHECKED		0
#define NFSV3CREATE_GUARDED		1
#define NFSV3CREATE_EXCLUSIVE		2

#define NFSV3CREATE_NMODES		3

#define NFSV3FSINFO_LINK		0x01
#define NFSV3FSINFO_SYMLINK		0x02
#define NFSV3FSINFO_HOMOGENEOUS		0x08
#define NFSV3FSINFO_CANSETTIME		0x10

/* Conversion macros */
#define	vtonfsv2_mode(t,m) \
		txdr_unsigned(((t) == VFIFO) ? MAKEIMODE(VCHR, (m)) : \
				MAKEIMODE((t), (m)))
#define vtonfsv3_mode(m)	txdr_unsigned((m) & 07777)
#define	nfstov_mode(a)		(fxdr_unsigned(uint16_t, (a))&07777)
#define	vtonfsv2_type(a)	txdr_unsigned(nfsv2_type[((int32_t)(a))])
#define	vtonfsv3_type(a)	txdr_unsigned(nfsv3_type[((int32_t)(a))])
#define	nfsv2tov_type(a)	nv2tov_type[fxdr_unsigned(uint32_t,(a))&0x7]
#define	nfsv3tov_type(a)	nv3tov_type[fxdr_unsigned(uint32_t,(a))&0x7]

/* File types */
typedef enum { NFNON=0, NFREG=1, NFDIR=2, NFBLK=3, NFCHR=4, NFLNK=5,
	NFSOCK=6, NFFIFO=7 } nfs_type;

/* Structs for common parts of the rpc's */
/*
 * File Handle (32 bytes for version 2), variable up to 64 for version 3.
 * File Handles of up to NFS_SMALLFH in size are stored directly in the
 * nfs node, whereas larger ones are malloc'd. (This never happens when
 * NFS_SMALLFH is set to 64.)
 * NFS_SMALLFH should be in the range of 32 to 64 and be divisible by 4.
 */
#ifndef NFS_SMALLFH
#define NFS_SMALLFH	64
#endif
union nfsfh {
/*	fhandle_t fh_generic; */
	u_char    fh_bytes[NFS_SMALLFH];
};
typedef union nfsfh nfsfh_t;

struct nfsv2_time {
	uint32_t nfsv2_sec;
	uint32_t nfsv2_usec;
};
typedef struct nfsv2_time	nfstime2;

struct nfsv3_time {
	uint32_t nfsv3_sec;
	uint32_t nfsv3_nsec;
};
typedef struct nfsv3_time	nfstime3;

/*
 * Quads are defined as arrays of 2 longs to ensure dense packing for the
 * protocol and to facilitate xdr conversion.
 */
struct nfs_uquad {
	uint32_t nfsuquad[2];
};
typedef	struct nfs_uquad	nfsuint64;

/*
 * NFS Version 3 special file number.
 */
struct nfsv3_spec {
	uint32_t specdata1;
	uint32_t specdata2;
};
typedef	struct nfsv3_spec	nfsv3spec;

/*
 * File attributes and setable attributes. These structures cover both
 * NFS version 2 and the version 3 protocol. Note that the union is only
 * used so that one pointer can refer to both variants. These structures
 * go out on the wire and must be densely packed, so no quad data types
 * are used. (all fields are longs or u_longs or structures of same)
 * NB: You can't do sizeof(struct nfs_fattr), you must use the
 *     NFSX_FATTR(v3) macro.
 */
struct nfs_fattr {
	uint32_t fa_type;
	uint32_t fa_mode;
	uint32_t fa_nlink;
	uint32_t fa_uid;
	uint32_t fa_gid;
	union {
		struct {
			uint32_t nfsv2fa_size;
			uint32_t nfsv2fa_blocksize;
			uint32_t nfsv2fa_rdev;
			uint32_t nfsv2fa_blocks;
			uint32_t nfsv2fa_fsid;
			uint32_t nfsv2fa_fileid;
			nfstime2  nfsv2fa_atime;
			nfstime2  nfsv2fa_mtime;
			nfstime2  nfsv2fa_ctime;
		} fa_nfsv2;
		struct {
			nfsuint64 nfsv3fa_size;
			nfsuint64 nfsv3fa_used;
			nfsv3spec nfsv3fa_rdev;
			nfsuint64 nfsv3fa_fsid;
			nfsuint64 nfsv3fa_fileid;
			nfstime3  nfsv3fa_atime;
			nfstime3  nfsv3fa_mtime;
			nfstime3  nfsv3fa_ctime;
		} fa_nfsv3;
	} fa_un;
};

/* and some ugly defines for accessing union components */
#define	fa2_size		fa_un.fa_nfsv2.nfsv2fa_size
#define	fa2_blocksize		fa_un.fa_nfsv2.nfsv2fa_blocksize
#define	fa2_rdev		fa_un.fa_nfsv2.nfsv2fa_rdev
#define	fa2_blocks		fa_un.fa_nfsv2.nfsv2fa_blocks
#define	fa2_fsid		fa_un.fa_nfsv2.nfsv2fa_fsid
#define	fa2_fileid		fa_un.fa_nfsv2.nfsv2fa_fileid
#define	fa2_atime		fa_un.fa_nfsv2.nfsv2fa_atime
#define	fa2_mtime		fa_un.fa_nfsv2.nfsv2fa_mtime
#define	fa2_ctime		fa_un.fa_nfsv2.nfsv2fa_ctime
#define	fa3_size		fa_un.fa_nfsv3.nfsv3fa_size
#define	fa3_used		fa_un.fa_nfsv3.nfsv3fa_used
#define	fa3_rdev		fa_un.fa_nfsv3.nfsv3fa_rdev
#define	fa3_fsid		fa_un.fa_nfsv3.nfsv3fa_fsid
#define	fa3_fileid		fa_un.fa_nfsv3.nfsv3fa_fileid
#define	fa3_atime		fa_un.fa_nfsv3.nfsv3fa_atime
#define	fa3_mtime		fa_un.fa_nfsv3.nfsv3fa_mtime
#define	fa3_ctime		fa_un.fa_nfsv3.nfsv3fa_ctime

struct nfsv2_sattr {
	uint32_t sa_mode;
	uint32_t sa_uid;
	uint32_t sa_gid;
	uint32_t sa_size;
	nfstime2  sa_atime;
	nfstime2  sa_mtime;
};

/*
 * NFS Version 3 sattr structure for the new node creation case.
 */
struct nfsv3_sattr {
	uint32_t   sa_modeset;
	uint32_t   sa_mode;
	uint32_t   sa_uidset;
	uint32_t   sa_uid;
	uint32_t   sa_gidset;
	uint32_t   sa_gid;
	uint32_t   sa_sizeset;
	uint32_t   sa_size;
	uint32_t   sa_atimetype;
	nfstime3  sa_atime;
	uint32_t   sa_mtimetype;
	nfstime3  sa_mtime;
};

struct nfs_statfs {
	union {
		struct {
			uint32_t nfsv2sf_tsize;
			uint32_t nfsv2sf_bsize;
			uint32_t nfsv2sf_blocks;
			uint32_t nfsv2sf_bfree;
			uint32_t nfsv2sf_bavail;
		} sf_nfsv2;
		struct {
			nfsuint64 nfsv3sf_tbytes;
			nfsuint64 nfsv3sf_fbytes;
			nfsuint64 nfsv3sf_abytes;
			nfsuint64 nfsv3sf_tfiles;
			nfsuint64 nfsv3sf_ffiles;
			nfsuint64 nfsv3sf_afiles;
			uint32_t nfsv3sf_invarsec;
		} sf_nfsv3;
	} sf_un;
};

#define sf_tsize	sf_un.sf_nfsv2.nfsv2sf_tsize
#define sf_bsize	sf_un.sf_nfsv2.nfsv2sf_bsize
#define sf_blocks	sf_un.sf_nfsv2.nfsv2sf_blocks
#define sf_bfree	sf_un.sf_nfsv2.nfsv2sf_bfree
#define sf_bavail	sf_un.sf_nfsv2.nfsv2sf_bavail
#define sf_tbytes	sf_un.sf_nfsv3.nfsv3sf_tbytes
#define sf_fbytes	sf_un.sf_nfsv3.nfsv3sf_fbytes
#define sf_abytes	sf_un.sf_nfsv3.nfsv3sf_abytes
#define sf_tfiles	sf_un.sf_nfsv3.nfsv3sf_tfiles
#define sf_ffiles	sf_un.sf_nfsv3.nfsv3sf_ffiles
#define sf_afiles	sf_un.sf_nfsv3.nfsv3sf_afiles
#define sf_invarsec	sf_un.sf_nfsv3.nfsv3sf_invarsec

struct nfsv3_fsinfo {
	uint32_t fs_rtmax;
	uint32_t fs_rtpref;
	uint32_t fs_rtmult;
	uint32_t fs_wtmax;
	uint32_t fs_wtpref;
	uint32_t fs_wtmult;
	uint32_t fs_dtpref;
	nfsuint64 fs_maxfilesize;
	nfstime3  fs_timedelta;
	uint32_t fs_properties;
};

struct nfsv3_pathconf {
	uint32_t pc_linkmax;
	uint32_t pc_namemax;
	uint32_t pc_notrunc;
	uint32_t pc_chownrestricted;
	uint32_t pc_caseinsensitive;
	uint32_t pc_casepreserving;
};
