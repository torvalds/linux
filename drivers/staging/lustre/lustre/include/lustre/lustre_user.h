/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2010, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/lustre/lustre_user.h
 *
 * Lustre public user-space interface definitions.
 */

#ifndef _LUSTRE_USER_H
#define _LUSTRE_USER_H

/** \defgroup lustreuser lustreuser
 *
 * @{
 */

#ifdef __KERNEL__
# include <linux/quota.h>
# include <linux/string.h> /* snprintf() */
# include <linux/version.h>
#else /* !__KERNEL__ */
# define NEED_QUOTA_DEFS
# include <stdio.h> /* snprintf() */
# include <string.h>
# include <sys/quota.h>
# include <sys/stat.h>
#endif /* __KERNEL__ */
#include "ll_fiemap.h"

/*
 * We need to always use 64bit version because the structure
 * is shared across entire cluster where 32bit and 64bit machines
 * are co-existing.
 */
#if __BITS_PER_LONG != 64 || defined(__ARCH_WANT_STAT64)
typedef struct stat64   lstat_t;
#define lstat_f  lstat64
#else
typedef struct stat     lstat_t;
#define lstat_f  lstat
#endif

#define HAVE_LOV_USER_MDS_DATA

#define LUSTRE_EOF 0xffffffffffffffffULL

/* for statfs() */
#define LL_SUPER_MAGIC 0x0BD00BD0

#ifndef FSFILT_IOC_GETFLAGS
#define FSFILT_IOC_GETFLAGS	       _IOR('f', 1, long)
#define FSFILT_IOC_SETFLAGS	       _IOW('f', 2, long)
#define FSFILT_IOC_GETVERSION	     _IOR('f', 3, long)
#define FSFILT_IOC_SETVERSION	     _IOW('f', 4, long)
#define FSFILT_IOC_GETVERSION_OLD	 _IOR('v', 1, long)
#define FSFILT_IOC_SETVERSION_OLD	 _IOW('v', 2, long)
#define FSFILT_IOC_FIEMAP		 _IOWR('f', 11, struct ll_user_fiemap)
#endif

/* FIEMAP flags supported by Lustre */
#define LUSTRE_FIEMAP_FLAGS_COMPAT (FIEMAP_FLAG_SYNC | FIEMAP_FLAG_DEVICE_ORDER)

enum obd_statfs_state {
	OS_STATE_DEGRADED       = 0x00000001, /**< RAID degraded/rebuilding */
	OS_STATE_READONLY       = 0x00000002, /**< filesystem is read-only */
	OS_STATE_RDONLY_1       = 0x00000004, /**< obsolete 1.6, was EROFS=30 */
	OS_STATE_RDONLY_2       = 0x00000008, /**< obsolete 1.6, was EROFS=30 */
	OS_STATE_RDONLY_3       = 0x00000010, /**< obsolete 1.6, was EROFS=30 */
};

struct obd_statfs {
	__u64	   os_type;
	__u64	   os_blocks;
	__u64	   os_bfree;
	__u64	   os_bavail;
	__u64	   os_files;
	__u64	   os_ffree;
	__u8	    os_fsid[40];
	__u32	   os_bsize;
	__u32	   os_namelen;
	__u64	   os_maxbytes;
	__u32	   os_state;       /**< obd_statfs_state OS_STATE_* flag */
	__u32	   os_fprecreated; /* objs available now to the caller */
				   /* used in QoS code to find preferred OSTs */
	__u32	   os_spare2;
	__u32	   os_spare3;
	__u32	   os_spare4;
	__u32	   os_spare5;
	__u32	   os_spare6;
	__u32	   os_spare7;
	__u32	   os_spare8;
	__u32	   os_spare9;
};

/**
 * File IDentifier.
 *
 * FID is a cluster-wide unique identifier of a file or an object (stripe).
 * FIDs are never reused.
 **/
struct lu_fid {
       /**
	* FID sequence. Sequence is a unit of migration: all files (objects)
	* with FIDs from a given sequence are stored on the same server.
	* Lustre should support 2^64 objects, so even if each sequence
	* has only a single object we can still enumerate 2^64 objects.
	**/
	__u64 f_seq;
	/* FID number within sequence. */
	__u32 f_oid;
	/**
	 * FID version, used to distinguish different versions (in the sense
	 * of snapshots, etc.) of the same file system object. Not currently
	 * used.
	 **/
	__u32 f_ver;
};

static inline bool fid_is_zero(const struct lu_fid *fid)
{
	return !fid->f_seq && !fid->f_oid;
}

struct filter_fid {
	struct lu_fid	ff_parent;  /* ff_parent.f_ver == file stripe number */
};

/* keep this one for compatibility */
struct filter_fid_old {
	struct lu_fid	ff_parent;
	__u64		ff_objid;
	__u64		ff_seq;
};

/* Userspace should treat lu_fid as opaque, and only use the following methods
 * to print or parse them.  Other functions (e.g. compare, swab) could be moved
 * here from lustre_idl.h if needed.
 */
struct lu_fid;

/**
 * Following struct for object attributes, that will be kept inode's EA.
 * Introduced in 2.0 release (please see b15993, for details)
 * Added to all objects since Lustre 2.4 as contains self FID
 */
struct lustre_mdt_attrs {
	/**
	 * Bitfield for supported data in this structure. From enum lma_compat.
	 * lma_self_fid and lma_flags are always available.
	 */
	__u32   lma_compat;
	/**
	 * Per-file incompat feature list. Lustre version should support all
	 * flags set in this field. The supported feature mask is available in
	 * LMA_INCOMPAT_SUPP.
	 */
	__u32   lma_incompat;
	/** FID of this inode */
	struct lu_fid  lma_self_fid;
};

/**
 * Prior to 2.4, the LMA structure also included SOM attributes which has since
 * been moved to a dedicated xattr
 * lma_flags was also removed because of lma_compat/incompat fields.
 */
#define LMA_OLD_SIZE (sizeof(struct lustre_mdt_attrs) + 5 * sizeof(__u64))

/**
 * OST object IDentifier.
 */
struct ost_id {
	union {
		struct {
			__u64	oi_id;
			__u64	oi_seq;
		} oi;
		struct lu_fid oi_fid;
	};
};

#define DOSTID "%#llx:%llu"
#define POSTID(oi) ostid_seq(oi), ostid_id(oi)

/*
 * The ioctl naming rules:
 * LL_*     - works on the currently opened filehandle instead of parent dir
 * *_OBD_*  - gets data for both OSC or MDC (LOV, LMV indirectly)
 * *_MDC_*  - gets/sets data related to MDC
 * *_LOV_*  - gets/sets data related to OSC/LOV
 * *FILE*   - called on parent dir and passes in a filename
 * *STRIPE* - set/get lov_user_md
 * *INFO    - set/get lov_user_mds_data
 */
/*	lustre_ioctl.h			101-150 */
#define LL_IOC_GETFLAGS		 _IOR('f', 151, long)
#define LL_IOC_SETFLAGS		 _IOW('f', 152, long)
#define LL_IOC_CLRFLAGS		 _IOW('f', 153, long)
#define LL_IOC_LOV_SETSTRIPE	    _IOW('f', 154, long)
#define LL_IOC_LOV_GETSTRIPE	    _IOW('f', 155, long)
#define LL_IOC_LOV_SETEA		_IOW('f', 156, long)
/*	LL_IOC_RECREATE_OBJ		157 obsolete */
/*	LL_IOC_RECREATE_FID		158 obsolete */
#define LL_IOC_GROUP_LOCK	       _IOW('f', 158, long)
#define LL_IOC_GROUP_UNLOCK	     _IOW('f', 159, long)
/* #define LL_IOC_QUOTACHECK		160 OBD_IOC_QUOTACHECK */
/* #define LL_IOC_POLL_QUOTACHECK	161 OBD_IOC_POLL_QUOTACHECK */
/* #define LL_IOC_QUOTACTL		162 OBD_IOC_QUOTACTL */
#define IOC_OBD_STATFS		  _IOWR('f', 164, struct obd_statfs *)
#define IOC_LOV_GETINFO		 _IOWR('f', 165, struct lov_user_mds_data *)
#define LL_IOC_FLUSHCTX		 _IOW('f', 166, long)
/* LL_IOC_RMTACL			167 obsolete */
#define LL_IOC_GETOBDCOUNT	      _IOR('f', 168, long)
#define LL_IOC_LLOOP_ATTACH	     _IOWR('f', 169, long)
#define LL_IOC_LLOOP_DETACH	     _IOWR('f', 170, long)
#define LL_IOC_LLOOP_INFO	       _IOWR('f', 171, struct lu_fid)
#define LL_IOC_LLOOP_DETACH_BYDEV       _IOWR('f', 172, long)
#define LL_IOC_PATH2FID		 _IOR('f', 173, long)
#define LL_IOC_GET_CONNECT_FLAGS	_IOWR('f', 174, __u64 *)
#define LL_IOC_GET_MDTIDX	       _IOR('f', 175, int)

/*	lustre_ioctl.h			177-210 */
#define LL_IOC_HSM_STATE_GET		_IOR('f', 211, struct hsm_user_state)
#define LL_IOC_HSM_STATE_SET		_IOW('f', 212, struct hsm_state_set)
#define LL_IOC_HSM_CT_START		_IOW('f', 213, struct lustre_kernelcomm)
#define LL_IOC_HSM_COPY_START		_IOW('f', 214, struct hsm_copy *)
#define LL_IOC_HSM_COPY_END		_IOW('f', 215, struct hsm_copy *)
#define LL_IOC_HSM_PROGRESS		_IOW('f', 216, struct hsm_user_request)
#define LL_IOC_HSM_REQUEST		_IOW('f', 217, struct hsm_user_request)
#define LL_IOC_DATA_VERSION		_IOR('f', 218, struct ioc_data_version)
#define LL_IOC_LOV_SWAP_LAYOUTS		_IOW('f', 219, \
						struct lustre_swap_layouts)
#define LL_IOC_HSM_ACTION		_IOR('f', 220, \
						struct hsm_current_action)
/* see <lustre_lib.h> for ioctl numbers 221-232 */

#define LL_IOC_LMV_SETSTRIPE	    _IOWR('f', 240, struct lmv_user_md)
#define LL_IOC_LMV_GETSTRIPE	    _IOWR('f', 241, struct lmv_user_md)
#define LL_IOC_SET_LEASE		_IOWR('f', 243, long)
#define LL_IOC_GET_LEASE		_IO('f', 244)
#define LL_IOC_HSM_IMPORT		_IOWR('f', 245, struct hsm_user_import)
#define LL_IOC_LMV_SET_DEFAULT_STRIPE	_IOWR('f', 246, struct lmv_user_md)
#define LL_IOC_MIGRATE			_IOR('f', 247, int)
#define LL_IOC_FID2MDTIDX		_IOWR('f', 248, struct lu_fid)
#define LL_IOC_GETPARENT		_IOWR('f', 249, struct getparent)

/* Lease types for use as arg and return of LL_IOC_{GET,SET}_LEASE ioctl. */
enum ll_lease_type {
	LL_LEASE_RDLCK	= 0x1,
	LL_LEASE_WRLCK	= 0x2,
	LL_LEASE_UNLCK	= 0x4,
};

#define LL_STATFS_LMV	   1
#define LL_STATFS_LOV	   2
#define LL_STATFS_NODELAY	4

#define IOC_MDC_TYPE	    'i'
#define IOC_MDC_LOOKUP	  _IOWR(IOC_MDC_TYPE, 20, struct obd_device *)
#define IOC_MDC_GETFILESTRIPE   _IOWR(IOC_MDC_TYPE, 21, struct lov_user_md *)
#define IOC_MDC_GETFILEINFO     _IOWR(IOC_MDC_TYPE, 22, struct lov_user_mds_data *)
#define LL_IOC_MDC_GETINFO      _IOWR(IOC_MDC_TYPE, 23, struct lov_user_mds_data *)

#define MAX_OBD_NAME 128 /* If this changes, a NEW ioctl must be added */

/* Define O_LOV_DELAY_CREATE to be a mask that is not useful for regular
 * files, but are unlikely to be used in practice and are not harmful if
 * used incorrectly.  O_NOCTTY and FASYNC are only meaningful for character
 * devices and are safe for use on new files (See LU-812, LU-4209).
 */
#define O_LOV_DELAY_CREATE	(O_NOCTTY | FASYNC)

#define LL_FILE_IGNORE_LOCK     0x00000001
#define LL_FILE_GROUP_LOCKED    0x00000002
#define LL_FILE_READAHEA	0x00000004
#define LL_FILE_LOCKED_DIRECTIO 0x00000008 /* client-side locks with dio */
#define LL_FILE_LOCKLESS_IO     0x00000010 /* server-side locks with cio */
#define LL_FILE_RMTACL	  0x00000020

#define LOV_USER_MAGIC_V1	0x0BD10BD0
#define LOV_USER_MAGIC		LOV_USER_MAGIC_V1
#define LOV_USER_MAGIC_JOIN_V1	0x0BD20BD0
#define LOV_USER_MAGIC_V3	0x0BD30BD0
/* 0x0BD40BD0 is occupied by LOV_MAGIC_MIGRATE */
#define LOV_USER_MAGIC_SPECIFIC	0x0BD50BD0	/* for specific OSTs */

#define LMV_USER_MAGIC    0x0CD30CD0    /*default lmv magic*/

#define LOV_PATTERN_RAID0	0x001
#define LOV_PATTERN_RAID1	0x002
#define LOV_PATTERN_FIRST	0x100
#define LOV_PATTERN_CMOBD	0x200

#define LOV_PATTERN_F_MASK	0xffff0000
#define LOV_PATTERN_F_HOLE	0x40000000 /* there is hole in LOV EA */
#define LOV_PATTERN_F_RELEASED	0x80000000 /* HSM released file */

#define LOV_MAXPOOLNAME 15
#define LOV_POOLNAMEF "%.15s"

#define LOV_MIN_STRIPE_BITS 16   /* maximum PAGE_SIZE (ia64), power of 2 */
#define LOV_MIN_STRIPE_SIZE (1 << LOV_MIN_STRIPE_BITS)
#define LOV_MAX_STRIPE_COUNT_OLD 160
/* This calculation is crafted so that input of 4096 will result in 160
 * which in turn is equal to old maximal stripe count.
 * XXX: In fact this is too simplified for now, what it also need is to get
 * ea_type argument to clearly know how much space each stripe consumes.
 *
 * The limit of 12 pages is somewhat arbitrary, but is a reasonably large
 * allocation that is sufficient for the current generation of systems.
 *
 * (max buffer size - lov+rpc header) / sizeof(struct lov_ost_data_v1)
 */
#define LOV_MAX_STRIPE_COUNT 2000  /* ((12 * 4096 - 256) / 24) */
#define LOV_ALL_STRIPES       0xffff /* only valid for directories */
#define LOV_V1_INSANE_STRIPE_COUNT 65532 /* maximum stripe count bz13933 */

#define lov_user_ost_data lov_user_ost_data_v1
struct lov_user_ost_data_v1 {     /* per-stripe data structure */
	struct ost_id l_ost_oi;	  /* OST object ID */
	__u32 l_ost_gen;	  /* generation of this OST index */
	__u32 l_ost_idx;	  /* OST index in LOV */
} __packed;

#define lov_user_md lov_user_md_v1
struct lov_user_md_v1 {	   /* LOV EA user data (host-endian) */
	__u32 lmm_magic;	  /* magic number = LOV_USER_MAGIC_V1 */
	__u32 lmm_pattern;	/* LOV_PATTERN_RAID0, LOV_PATTERN_RAID1 */
	struct ost_id lmm_oi;	  /* LOV object ID */
	__u32 lmm_stripe_size;    /* size of stripe in bytes */
	__u16 lmm_stripe_count;   /* num stripes in use for this object */
	union {
		__u16 lmm_stripe_offset;  /* starting stripe offset in
					   * lmm_objects, use when writing
					   */
		__u16 lmm_layout_gen;     /* layout generation number
					   * used when reading
					   */
	};
	struct lov_user_ost_data_v1 lmm_objects[0]; /* per-stripe data */
} __attribute__((packed,  __may_alias__));

struct lov_user_md_v3 {	   /* LOV EA user data (host-endian) */
	__u32 lmm_magic;	  /* magic number = LOV_USER_MAGIC_V3 */
	__u32 lmm_pattern;	/* LOV_PATTERN_RAID0, LOV_PATTERN_RAID1 */
	struct ost_id lmm_oi;	  /* LOV object ID */
	__u32 lmm_stripe_size;    /* size of stripe in bytes */
	__u16 lmm_stripe_count;   /* num stripes in use for this object */
	union {
		__u16 lmm_stripe_offset;  /* starting stripe offset in
					   * lmm_objects, use when writing
					   */
		__u16 lmm_layout_gen;     /* layout generation number
					   * used when reading
					   */
	};
	char  lmm_pool_name[LOV_MAXPOOLNAME + 1];   /* pool name */
	struct lov_user_ost_data_v1 lmm_objects[0]; /* per-stripe data */
} __packed;

static inline __u32 lov_user_md_size(__u16 stripes, __u32 lmm_magic)
{
	if (lmm_magic == LOV_USER_MAGIC_V1)
		return sizeof(struct lov_user_md_v1) +
				stripes * sizeof(struct lov_user_ost_data_v1);
	return sizeof(struct lov_user_md_v3) +
	       stripes * sizeof(struct lov_user_ost_data_v1);
}

/* Compile with -D_LARGEFILE64_SOURCE or -D_GNU_SOURCE (or #define) to
 * use this.  It is unsafe to #define those values in this header as it
 * is possible the application has already #included <sys/stat.h>. */
#ifdef HAVE_LOV_USER_MDS_DATA
#define lov_user_mds_data lov_user_mds_data_v1
struct lov_user_mds_data_v1 {
	lstat_t lmd_st;		 /* MDS stat struct */
	struct lov_user_md_v1 lmd_lmm;  /* LOV EA V1 user data */
} __packed;

struct lov_user_mds_data_v3 {
	lstat_t lmd_st;		 /* MDS stat struct */
	struct lov_user_md_v3 lmd_lmm;  /* LOV EA V3 user data */
} __packed;
#endif

struct lmv_user_mds_data {
	struct lu_fid	lum_fid;
	__u32		lum_padding;
	__u32		lum_mds;
};

enum lmv_hash_type {
	LMV_HASH_TYPE_UNKNOWN	= 0,	/* 0 is reserved for testing purpose */
	LMV_HASH_TYPE_ALL_CHARS = 1,
	LMV_HASH_TYPE_FNV_1A_64 = 2,
};

#define LMV_HASH_NAME_ALL_CHARS		"all_char"
#define LMV_HASH_NAME_FNV_1A_64		"fnv_1a_64"

/*
 * Got this according to how get LOV_MAX_STRIPE_COUNT, see above,
 * (max buffer size - lmv+rpc header) / sizeof(struct lmv_user_mds_data)
 */
#define LMV_MAX_STRIPE_COUNT 2000  /* ((12 * 4096 - 256) / 24) */
#define lmv_user_md lmv_user_md_v1
struct lmv_user_md_v1 {
	__u32	lum_magic;	 /* must be the first field */
	__u32	lum_stripe_count;  /* dirstripe count */
	__u32	lum_stripe_offset; /* MDT idx for default dirstripe */
	__u32	lum_hash_type;     /* Dir stripe policy */
	__u32	lum_type;	  /* LMV type: default or normal */
	__u32	lum_padding1;
	__u32	lum_padding2;
	__u32	lum_padding3;
	char	lum_pool_name[LOV_MAXPOOLNAME + 1];
	struct	lmv_user_mds_data  lum_objects[0];
} __packed;

static inline int lmv_user_md_size(int stripes, int lmm_magic)
{
	return sizeof(struct lmv_user_md) +
		      stripes * sizeof(struct lmv_user_mds_data);
}

void lustre_swab_lmv_user_md(struct lmv_user_md *lum);

struct ll_recreate_obj {
	__u64 lrc_id;
	__u32 lrc_ost_idx;
};

struct ll_fid {
	__u64 id;	 /* holds object id */
	__u32 generation; /* holds object generation */
	__u32 f_type;     /* holds object type or stripe idx when passing it to
			   * OST for saving into EA. */
};

#define UUID_MAX	40
struct obd_uuid {
	char uuid[UUID_MAX];
};

static inline bool obd_uuid_equals(const struct obd_uuid *u1,
				   const struct obd_uuid *u2)
{
	return strcmp((char *)u1->uuid, (char *)u2->uuid) == 0;
}

static inline int obd_uuid_empty(struct obd_uuid *uuid)
{
	return uuid->uuid[0] == '\0';
}

static inline void obd_str2uuid(struct obd_uuid *uuid, const char *tmp)
{
	strncpy((char *)uuid->uuid, tmp, sizeof(*uuid));
	uuid->uuid[sizeof(*uuid) - 1] = '\0';
}

/* For printf's only, make sure uuid is terminated */
static inline char *obd_uuid2str(const struct obd_uuid *uuid)
{
	if (!uuid)
		return NULL;

	if (uuid->uuid[sizeof(*uuid) - 1] != '\0') {
		/* Obviously not safe, but for printfs, no real harm done...
		 * we're always null-terminated, even in a race.
		 */
		static char temp[sizeof(*uuid)];

		memcpy(temp, uuid->uuid, sizeof(*uuid) - 1);
		temp[sizeof(*uuid) - 1] = '\0';
		return temp;
	}
	return (char *)(uuid->uuid);
}

/* Extract fsname from uuid (or target name) of a target
 * e.g. (myfs-OST0007_UUID -> myfs)
 * see also deuuidify.
 */
static inline void obd_uuid2fsname(char *buf, char *uuid, int buflen)
{
	char *p;

	strncpy(buf, uuid, buflen - 1);
	buf[buflen - 1] = '\0';
	p = strrchr(buf, '-');
	if (p)
		*p = '\0';
}

/* printf display format
 * e.g. printf("file FID is "DFID"\n", PFID(fid));
 */
#define FID_NOBRACE_LEN 40
#define FID_LEN (FID_NOBRACE_LEN + 2)
#define DFID_NOBRACE "%#llx:0x%x:0x%x"
#define DFID "["DFID_NOBRACE"]"
#define PFID(fid)     \
	(fid)->f_seq, \
	(fid)->f_oid, \
	(fid)->f_ver

/* scanf input parse format -- strip '[' first.
 * e.g. sscanf(fidstr, SFID, RFID(&fid));
 */
#define SFID "0x%llx:0x%x:0x%x"
#define RFID(fid)     \
	&((fid)->f_seq), \
	&((fid)->f_oid), \
	&((fid)->f_ver)

/********* Quotas **********/

#define Q_QUOTACHECK   0x800100 /* deprecated as of 2.4 */
#define Q_INITQUOTA    0x800101 /* deprecated as of 2.4  */
#define Q_GETOINFO     0x800102 /* get obd quota info */
#define Q_GETOQUOTA    0x800103 /* get obd quotas */
#define Q_FINVALIDATE  0x800104 /* deprecated as of 2.4 */

/* these must be explicitly translated into linux Q_* in ll_dir_ioctl */
#define LUSTRE_Q_QUOTAON    0x800002     /* turn quotas on */
#define LUSTRE_Q_QUOTAOFF   0x800003     /* turn quotas off */
#define LUSTRE_Q_GETINFO    0x800005     /* get information about quota files */
#define LUSTRE_Q_SETINFO    0x800006     /* set information about quota files */
#define LUSTRE_Q_GETQUOTA   0x800007     /* get user quota structure */
#define LUSTRE_Q_SETQUOTA   0x800008     /* set user quota structure */
/* lustre-specific control commands */
#define LUSTRE_Q_INVALIDATE  0x80000b     /* invalidate quota data */
#define LUSTRE_Q_FINVALIDATE 0x80000c     /* invalidate filter quota data */

#define UGQUOTA 2       /* set both USRQUOTA and GRPQUOTA */

struct if_quotacheck {
	char		    obd_type[16];
	struct obd_uuid	 obd_uuid;
};

#define IDENTITY_DOWNCALL_MAGIC 0x6d6dd629

/* permission */
#define N_PERMS_MAX      64

struct perm_downcall_data {
	__u64 pdd_nid;
	__u32 pdd_perm;
	__u32 pdd_padding;
};

struct identity_downcall_data {
	__u32			    idd_magic;
	__u32			    idd_err;
	__u32			    idd_uid;
	__u32			    idd_gid;
	__u32			    idd_nperms;
	__u32			    idd_ngroups;
	struct perm_downcall_data idd_perms[N_PERMS_MAX];
	__u32			    idd_groups[0];
};

/* lustre volatile file support
 * file name header: .^L^S^T^R:volatile"
 */
#define LUSTRE_VOLATILE_HDR	".\x0c\x13\x14\x12:VOLATILE"
#define LUSTRE_VOLATILE_HDR_LEN	14
/* hdr + MDT index */
#define LUSTRE_VOLATILE_IDX	LUSTRE_VOLATILE_HDR":%.4X:"

enum lustre_quota_version {
	LUSTRE_QUOTA_V2 = 1
};

/* XXX: same as if_dqinfo struct in kernel */
struct obd_dqinfo {
	__u64 dqi_bgrace;
	__u64 dqi_igrace;
	__u32 dqi_flags;
	__u32 dqi_valid;
};

/* XXX: same as if_dqblk struct in kernel, plus one padding */
struct obd_dqblk {
	__u64 dqb_bhardlimit;
	__u64 dqb_bsoftlimit;
	__u64 dqb_curspace;
	__u64 dqb_ihardlimit;
	__u64 dqb_isoftlimit;
	__u64 dqb_curinodes;
	__u64 dqb_btime;
	__u64 dqb_itime;
	__u32 dqb_valid;
	__u32 dqb_padding;
};

enum {
	QC_GENERAL      = 0,
	QC_MDTIDX       = 1,
	QC_OSTIDX       = 2,
	QC_UUID	 = 3
};

struct if_quotactl {
	__u32		   qc_cmd;
	__u32		   qc_type;
	__u32		   qc_id;
	__u32		   qc_stat;
	__u32		   qc_valid;
	__u32		   qc_idx;
	struct obd_dqinfo       qc_dqinfo;
	struct obd_dqblk	qc_dqblk;
	char		    obd_type[16];
	struct obd_uuid	 obd_uuid;
};

/* swap layout flags */
#define SWAP_LAYOUTS_CHECK_DV1		(1 << 0)
#define SWAP_LAYOUTS_CHECK_DV2		(1 << 1)
#define SWAP_LAYOUTS_KEEP_MTIME		(1 << 2)
#define SWAP_LAYOUTS_KEEP_ATIME		(1 << 3)

/* Swap XATTR_NAME_HSM as well, only on the MDT so far */
#define SWAP_LAYOUTS_MDS_HSM		(1 << 31)
struct lustre_swap_layouts {
	__u64	sl_flags;
	__u32	sl_fd;
	__u32	sl_gid;
	__u64	sl_dv1;
	__u64	sl_dv2;
};

/********* Changelogs **********/
/** Changelog record types */
enum changelog_rec_type {
	CL_MARK     = 0,
	CL_CREATE   = 1,  /* namespace */
	CL_MKDIR    = 2,  /* namespace */
	CL_HARDLINK = 3,  /* namespace */
	CL_SOFTLINK = 4,  /* namespace */
	CL_MKNOD    = 5,  /* namespace */
	CL_UNLINK   = 6,  /* namespace */
	CL_RMDIR    = 7,  /* namespace */
	CL_RENAME   = 8,  /* namespace */
	CL_EXT      = 9,  /* namespace extended record (2nd half of rename) */
	CL_OPEN     = 10, /* not currently used */
	CL_CLOSE    = 11, /* may be written to log only with mtime change */
	CL_LAYOUT   = 12, /* file layout/striping modified */
	CL_TRUNC    = 13,
	CL_SETATTR  = 14,
	CL_XATTR    = 15,
	CL_HSM      = 16, /* HSM specific events, see flags */
	CL_MTIME    = 17, /* Precedence: setattr > mtime > ctime > atime */
	CL_CTIME    = 18,
	CL_ATIME    = 19,
	CL_LAST
};

static inline const char *changelog_type2str(int type)
{
	static const char *changelog_str[] = {
		"MARK",  "CREAT", "MKDIR", "HLINK", "SLINK", "MKNOD", "UNLNK",
		"RMDIR", "RENME", "RNMTO", "OPEN",  "CLOSE", "LYOUT", "TRUNC",
		"SATTR", "XATTR", "HSM",   "MTIME", "CTIME", "ATIME",
	};

	if (type >= 0 && type < CL_LAST)
		return changelog_str[type];
	return NULL;
}

/* per-record flags */
#define CLF_FLAGSHIFT   12
#define CLF_FLAGMASK    ((1U << CLF_FLAGSHIFT) - 1)
#define CLF_VERMASK     (~CLF_FLAGMASK)
enum changelog_rec_flags {
	CLF_VERSION	= 0x1000,
	CLF_RENAME	= 0x2000,
	CLF_JOBID	= 0x4000,
	CLF_SUPPORTED	= CLF_VERSION | CLF_RENAME | CLF_JOBID
};

/* Anything under the flagmask may be per-type (if desired) */
/* Flags for unlink */
#define CLF_UNLINK_LAST       0x0001 /* Unlink of last hardlink */
#define CLF_UNLINK_HSM_EXISTS 0x0002 /* File has something in HSM */
				     /* HSM cleaning needed */
/* Flags for rename */
#define CLF_RENAME_LAST		0x0001	/* rename unlink last hardlink of
					 * target
					 */
#define CLF_RENAME_LAST_EXISTS	0x0002	/* rename unlink last hardlink of target
					 * has an archive in backend
					 */

/* Flags for HSM */
/* 12b used (from high weight to low weight):
 * 2b for flags
 * 3b for event
 * 7b for error code
 */
#define CLF_HSM_ERR_L	0 /* HSM return code, 7 bits */
#define CLF_HSM_ERR_H	6
#define CLF_HSM_EVENT_L      7 /* HSM event, 3 bits, see enum hsm_event */
#define CLF_HSM_EVENT_H      9
#define CLF_HSM_FLAG_L      10 /* HSM flags, 2 bits, 1 used, 1 spare */
#define CLF_HSM_FLAG_H      11
#define CLF_HSM_SPARE_L     12 /* 4 spare bits */
#define CLF_HSM_SPARE_H     15
#define CLF_HSM_LAST	15

/* Remove bits higher than _h, then extract the value
 * between _h and _l by shifting lower weigth to bit 0.
 */
#define CLF_GET_BITS(_b, _h, _l) (((_b << (CLF_HSM_LAST - _h)) & 0xFFFF) \
				   >> (CLF_HSM_LAST - _h + _l))

#define CLF_HSM_SUCCESS      0x00
#define CLF_HSM_MAXERROR     0x7E
#define CLF_HSM_ERROVERFLOW  0x7F

#define CLF_HSM_DIRTY	1 /* file is dirty after HSM request end */

/* 3 bits field => 8 values allowed */
enum hsm_event {
	HE_ARCHIVE      = 0,
	HE_RESTORE      = 1,
	HE_CANCEL       = 2,
	HE_RELEASE      = 3,
	HE_REMOVE       = 4,
	HE_STATE	= 5,
	HE_SPARE1       = 6,
	HE_SPARE2       = 7,
};

static inline enum hsm_event hsm_get_cl_event(__u16 flags)
{
	return CLF_GET_BITS(flags, CLF_HSM_EVENT_H, CLF_HSM_EVENT_L);
}

static inline void hsm_set_cl_event(int *flags, enum hsm_event he)
{
	*flags |= (he << CLF_HSM_EVENT_L);
}

static inline __u16 hsm_get_cl_flags(int flags)
{
	return CLF_GET_BITS(flags, CLF_HSM_FLAG_H, CLF_HSM_FLAG_L);
}

static inline void hsm_set_cl_flags(int *flags, int bits)
{
	*flags |= (bits << CLF_HSM_FLAG_L);
}

static inline int hsm_get_cl_error(int flags)
{
	return CLF_GET_BITS(flags, CLF_HSM_ERR_H, CLF_HSM_ERR_L);
}

static inline void hsm_set_cl_error(int *flags, int error)
{
	*flags |= (error << CLF_HSM_ERR_L);
}

enum changelog_send_flag {
	/* Not yet implemented */
	CHANGELOG_FLAG_FOLLOW	= BIT(0),
	/*
	 * Blocking IO makes sense in case of slow user parsing of the records,
	 * but it also prevents us from cleaning up if the records are not
	 * consumed.
	 */
	CHANGELOG_FLAG_BLOCK	= BIT(1),
	/* Pack jobid into the changelog records if available. */
	CHANGELOG_FLAG_JOBID	= BIT(2),
};

#define CR_MAXSIZE cfs_size_round(2 * NAME_MAX + 2 + \
				  changelog_rec_offset(CLF_SUPPORTED))

/* 31 usable bytes string + null terminator. */
#define LUSTRE_JOBID_SIZE	32

/*
 * This is the minimal changelog record. It can contain extensions
 * such as rename fields or process jobid. Its exact content is described
 * by the cr_flags.
 *
 * Extensions are packed in the same order as their corresponding flags.
 */
struct changelog_rec {
	__u16		 cr_namelen;
	__u16		 cr_flags; /**< \a changelog_rec_flags */
	__u32		 cr_type;  /**< \a changelog_rec_type */
	__u64		 cr_index; /**< changelog record number */
	__u64		 cr_prev;  /**< last index for this target fid */
	__u64		 cr_time;
	union {
		struct lu_fid    cr_tfid;	/**< target fid */
		__u32	 cr_markerflags; /**< CL_MARK flags */
	};
	struct lu_fid	    cr_pfid;	/**< parent fid */
} __packed;

/* Changelog extension for RENAME. */
struct changelog_ext_rename {
	struct lu_fid	cr_sfid;	/**< source fid, or zero */
	struct lu_fid	cr_spfid;	/**< source parent fid, or zero */
};

/* Changelog extension to include JOBID. */
struct changelog_ext_jobid {
	char	cr_jobid[LUSTRE_JOBID_SIZE];	/**< zero-terminated string. */
};

static inline size_t changelog_rec_offset(enum changelog_rec_flags crf)
{
	size_t size = sizeof(struct changelog_rec);

	if (crf & CLF_RENAME)
		size += sizeof(struct changelog_ext_rename);

	if (crf & CLF_JOBID)
		size += sizeof(struct changelog_ext_jobid);

	return size;
}

static inline size_t changelog_rec_size(struct changelog_rec *rec)
{
	return changelog_rec_offset(rec->cr_flags);
}

static inline size_t changelog_rec_varsize(struct changelog_rec *rec)
{
	return changelog_rec_size(rec) - sizeof(*rec) + rec->cr_namelen;
}

static inline
struct changelog_ext_rename *changelog_rec_rename(struct changelog_rec *rec)
{
	enum changelog_rec_flags crf = rec->cr_flags & CLF_VERSION;

	return (struct changelog_ext_rename *)((char *)rec +
					       changelog_rec_offset(crf));
}

/* The jobid follows the rename extension, if present */
static inline
struct changelog_ext_jobid *changelog_rec_jobid(struct changelog_rec *rec)
{
	enum changelog_rec_flags crf = rec->cr_flags &
				       (CLF_VERSION | CLF_RENAME);

	return (struct changelog_ext_jobid *)((char *)rec +
					      changelog_rec_offset(crf));
}

/* The name follows the rename and jobid extensions, if present */
static inline char *changelog_rec_name(struct changelog_rec *rec)
{
	return (char *)rec + changelog_rec_offset(rec->cr_flags &
						  CLF_SUPPORTED);
}

static inline size_t changelog_rec_snamelen(struct changelog_rec *rec)
{
	return rec->cr_namelen - strlen(changelog_rec_name(rec)) - 1;
}

static inline char *changelog_rec_sname(struct changelog_rec *rec)
{
	char *cr_name = changelog_rec_name(rec);

	return cr_name + strlen(cr_name) + 1;
}

/**
 * Remap a record to the desired format as specified by the crf flags.
 * The record must be big enough to contain the final remapped version.
 * Superfluous extension fields are removed and missing ones are added
 * and zeroed. The flags of the record are updated accordingly.
 *
 * The jobid and rename extensions can be added to a record, to match the
 * format an application expects, typically. In this case, the newly added
 * fields will be zeroed.
 * The Jobid field can be removed, to guarantee compatibility with older
 * clients that don't expect this field in the records they process.
 *
 * The following assumptions are being made:
 *	- CLF_RENAME will not be removed
 *	- CLF_JOBID will not be added without CLF_RENAME being added too
 *
 * @param[in,out]  rec		The record to remap.
 * @param[in]	   crf_wanted	Flags describing the desired extensions.
 */
static inline void changelog_remap_rec(struct changelog_rec *rec,
				       enum changelog_rec_flags crf_wanted)
{
	char *jid_mov, *rnm_mov;

	crf_wanted &= CLF_SUPPORTED;

	if ((rec->cr_flags & CLF_SUPPORTED) == crf_wanted)
		return;

	/* First move the variable-length name field */
	memmove((char *)rec + changelog_rec_offset(crf_wanted),
		changelog_rec_name(rec), rec->cr_namelen);

	/* Locations of jobid and rename extensions in the remapped record */
	jid_mov = (char *)rec +
		  changelog_rec_offset(crf_wanted & ~CLF_JOBID);
	rnm_mov = (char *)rec +
		  changelog_rec_offset(crf_wanted & ~(CLF_JOBID | CLF_RENAME));

	/* Move the extension fields to the desired positions */
	if ((crf_wanted & CLF_JOBID) && (rec->cr_flags & CLF_JOBID))
		memmove(jid_mov, changelog_rec_jobid(rec),
			sizeof(struct changelog_ext_jobid));

	if ((crf_wanted & CLF_RENAME) && (rec->cr_flags & CLF_RENAME))
		memmove(rnm_mov, changelog_rec_rename(rec),
			sizeof(struct changelog_ext_rename));

	/* Clear newly added fields */
	if ((crf_wanted & CLF_JOBID) && !(rec->cr_flags & CLF_JOBID))
		memset(jid_mov, 0, sizeof(struct changelog_ext_jobid));

	if ((crf_wanted & CLF_RENAME) && !(rec->cr_flags & CLF_RENAME))
		memset(rnm_mov, 0, sizeof(struct changelog_ext_rename));

	/* Update the record's flags accordingly */
	rec->cr_flags = (rec->cr_flags & CLF_FLAGMASK) | crf_wanted;
}

struct ioc_changelog {
	__u64 icc_recno;
	__u32 icc_mdtindex;
	__u32 icc_id;
	__u32 icc_flags;
};

enum changelog_message_type {
	CL_RECORD = 10, /* message is a changelog_rec */
	CL_EOF    = 11, /* at end of current changelog */
};

/********* Misc **********/

struct ioc_data_version {
	__u64 idv_version;
	__u64 idv_flags;     /* See LL_DV_xxx */
};

#define LL_DV_RD_FLUSH	BIT(0)	/* Flush dirty pages from clients */
#define LL_DV_WR_FLUSH	BIT(1)	/* Flush all caching pages from clients */

#ifndef offsetof
# define offsetof(typ, memb)     ((unsigned long)((char *)&(((typ *)0)->memb)))
#endif

#define dot_lustre_name ".lustre"

/********* HSM **********/

/** HSM per-file state
 * See HSM_FLAGS below.
 */
enum hsm_states {
	HS_EXISTS	= 0x00000001,
	HS_DIRTY	= 0x00000002,
	HS_RELEASED	= 0x00000004,
	HS_ARCHIVED	= 0x00000008,
	HS_NORELEASE	= 0x00000010,
	HS_NOARCHIVE	= 0x00000020,
	HS_LOST		= 0x00000040,
};

/* HSM user-setable flags. */
#define HSM_USER_MASK   (HS_NORELEASE | HS_NOARCHIVE | HS_DIRTY)

/* Other HSM flags. */
#define HSM_STATUS_MASK (HS_EXISTS | HS_LOST | HS_RELEASED | HS_ARCHIVED)

/*
 * All HSM-related possible flags that could be applied to a file.
 * This should be kept in sync with hsm_states.
 */
#define HSM_FLAGS_MASK  (HSM_USER_MASK | HSM_STATUS_MASK)

/**
 * HSM request progress state
 */
enum hsm_progress_states {
	HPS_WAITING	= 1,
	HPS_RUNNING	= 2,
	HPS_DONE	= 3,
};

#define HPS_NONE	0

static inline char *hsm_progress_state2name(enum hsm_progress_states s)
{
	switch  (s) {
	case HPS_WAITING:	return "waiting";
	case HPS_RUNNING:	return "running";
	case HPS_DONE:		return "done";
	default:		return "unknown";
	}
}

struct hsm_extent {
	__u64 offset;
	__u64 length;
} __packed;

/**
 * Current HSM states of a Lustre file.
 *
 * This structure purpose is to be sent to user-space mainly. It describes the
 * current HSM flags and in-progress action.
 */
struct hsm_user_state {
	/** Current HSM states, from enum hsm_states. */
	__u32			hus_states;
	__u32			hus_archive_id;
	/**  The current undergoing action, if there is one */
	__u32			hus_in_progress_state;
	__u32			hus_in_progress_action;
	struct hsm_extent	hus_in_progress_location;
	char			hus_extended_info[];
};

struct hsm_state_set_ioc {
	struct lu_fid	hssi_fid;
	__u64		hssi_setmask;
	__u64		hssi_clearmask;
};

/*
 * This structure describes the current in-progress action for a file.
 * it is returned to user space and send over the wire
 */
struct hsm_current_action {
	/**  The current undergoing action, if there is one */
	/* state is one of hsm_progress_states */
	__u32			hca_state;
	/* action is one of hsm_user_action */
	__u32			hca_action;
	struct hsm_extent	hca_location;
};

/***** HSM user requests ******/
/* User-generated (lfs/ioctl) request types */
enum hsm_user_action {
	HUA_NONE    =  1, /* no action (noop) */
	HUA_ARCHIVE = 10, /* copy to hsm */
	HUA_RESTORE = 11, /* prestage */
	HUA_RELEASE = 12, /* drop ost objects */
	HUA_REMOVE  = 13, /* remove from archive */
	HUA_CANCEL  = 14  /* cancel a request */
};

static inline char *hsm_user_action2name(enum hsm_user_action  a)
{
	switch  (a) {
	case HUA_NONE:    return "NOOP";
	case HUA_ARCHIVE: return "ARCHIVE";
	case HUA_RESTORE: return "RESTORE";
	case HUA_RELEASE: return "RELEASE";
	case HUA_REMOVE:  return "REMOVE";
	case HUA_CANCEL:  return "CANCEL";
	default:	  return "UNKNOWN";
	}
}

/*
 * List of hr_flags (bit field)
 */
#define HSM_FORCE_ACTION 0x0001
/* used by CT, connot be set by user */
#define HSM_GHOST_COPY   0x0002

/**
 * Contains all the fixed part of struct hsm_user_request.
 *
 */
struct hsm_request {
	__u32 hr_action;	/* enum hsm_user_action */
	__u32 hr_archive_id;	/* archive id, used only with HUA_ARCHIVE */
	__u64 hr_flags;		/* request flags */
	__u32 hr_itemcount;	/* item count in hur_user_item vector */
	__u32 hr_data_len;
};

struct hsm_user_item {
	struct lu_fid	hui_fid;
	struct hsm_extent hui_extent;
} __packed;

struct hsm_user_request {
	struct hsm_request	hur_request;
	struct hsm_user_item	hur_user_item[0];
	/* extra data blob at end of struct (after all
	 * hur_user_items), only use helpers to access it
	 */
} __packed;

/** Return pointer to data field in a hsm user request */
static inline void *hur_data(struct hsm_user_request *hur)
{
	return &hur->hur_user_item[hur->hur_request.hr_itemcount];
}

/**
 * Compute the current length of the provided hsm_user_request.  This returns -1
 * instead of an errno because ssize_t is defined to be only [ -1, SSIZE_MAX ]
 *
 * return -1 on bounds check error.
 */
static inline ssize_t hur_len(struct hsm_user_request *hur)
{
	__u64	size;

	/* can't overflow a __u64 since hr_itemcount is only __u32 */
	size = offsetof(struct hsm_user_request, hur_user_item[0]) +
		(__u64)hur->hur_request.hr_itemcount *
		sizeof(hur->hur_user_item[0]) + hur->hur_request.hr_data_len;

	if (size != (ssize_t)size)
		return -1;

	return size;
}

/****** HSM RPCs to copytool *****/
/* Message types the copytool may receive */
enum hsm_message_type {
	HMT_ACTION_LIST = 100, /* message is a hsm_action_list */
};

/* Actions the copytool may be instructed to take for a given action_item */
enum hsm_copytool_action {
	HSMA_NONE    = 10, /* no action */
	HSMA_ARCHIVE = 20, /* arbitrary offset */
	HSMA_RESTORE = 21,
	HSMA_REMOVE  = 22,
	HSMA_CANCEL  = 23
};

static inline char *hsm_copytool_action2name(enum hsm_copytool_action  a)
{
	switch  (a) {
	case HSMA_NONE:    return "NOOP";
	case HSMA_ARCHIVE: return "ARCHIVE";
	case HSMA_RESTORE: return "RESTORE";
	case HSMA_REMOVE:  return "REMOVE";
	case HSMA_CANCEL:  return "CANCEL";
	default:	   return "UNKNOWN";
	}
}

/* Copytool item action description */
struct hsm_action_item {
	__u32      hai_len;     /* valid size of this struct */
	__u32      hai_action;  /* hsm_copytool_action, but use known size */
	struct lu_fid hai_fid;     /* Lustre FID to operated on */
	struct lu_fid hai_dfid;    /* fid used for data access */
	struct hsm_extent hai_extent;  /* byte range to operate on */
	__u64      hai_cookie;  /* action cookie from coordinator */
	__u64      hai_gid;     /* grouplock id */
	char       hai_data[0]; /* variable length */
} __packed;

/*
 * helper function which print in hexa the first bytes of
 * hai opaque field
 * \param hai [IN] record to print
 * \param buffer [OUT] output buffer
 * \param len [IN] max buffer len
 * \retval buffer
 */
static inline char *hai_dump_data_field(struct hsm_action_item *hai,
					char *buffer, int len)
{
	int i, sz, data_len;
	char *ptr;

	ptr = buffer;
	sz = len;
	data_len = hai->hai_len - sizeof(*hai);
	for (i = 0 ; (i < data_len) && (sz > 0) ; i++) {
		int cnt;

		cnt = snprintf(ptr, sz, "%.2X",
			       (unsigned char)hai->hai_data[i]);
		ptr += cnt;
		sz -= cnt;
	}
	*ptr = '\0';
	return buffer;
}

/* Copytool action list */
#define HAL_VERSION 1
#define HAL_MAXSIZE LNET_MTU /* bytes, used in userspace only */
struct hsm_action_list {
	__u32 hal_version;
	__u32 hal_count;       /* number of hai's to follow */
	__u64 hal_compound_id; /* returned by coordinator */
	__u64 hal_flags;
	__u32 hal_archive_id; /* which archive backend */
	__u32 padding1;
	char  hal_fsname[0];   /* null-terminated */
	/* struct hsm_action_item[hal_count] follows, aligned on 8-byte
	 * boundaries. See hai_first
	 */
} __packed;

#ifndef HAVE_CFS_SIZE_ROUND
static inline int cfs_size_round(int val)
{
	return (val + 7) & (~0x7);
}

#define HAVE_CFS_SIZE_ROUND
#endif

/* Return pointer to first hai in action list */
static inline struct hsm_action_item *hai_first(struct hsm_action_list *hal)
{
	return (struct hsm_action_item *)(hal->hal_fsname +
					  cfs_size_round(strlen(hal-> \
								hal_fsname)
							 + 1));
}

/* Return pointer to next hai */
static inline struct hsm_action_item *hai_next(struct hsm_action_item *hai)
{
	return (struct hsm_action_item *)((char *)hai +
					  cfs_size_round(hai->hai_len));
}

/* Return size of an hsm_action_list */
static inline int hal_size(struct hsm_action_list *hal)
{
	int i, sz;
	struct hsm_action_item *hai;

	sz = sizeof(*hal) + cfs_size_round(strlen(hal->hal_fsname) + 1);
	hai = hai_first(hal);
	for (i = 0; i < hal->hal_count; i++, hai = hai_next(hai))
		sz += cfs_size_round(hai->hai_len);

	return sz;
}

/* HSM file import
 * describe the attributes to be set on imported file
 */
struct hsm_user_import {
	__u64		hui_size;
	__u64		hui_atime;
	__u64		hui_mtime;
	__u32		hui_atime_ns;
	__u32		hui_mtime_ns;
	__u32		hui_uid;
	__u32		hui_gid;
	__u32		hui_mode;
	__u32		hui_archive_id;
};

/* Copytool progress reporting */
#define HP_FLAG_COMPLETED 0x01
#define HP_FLAG_RETRY     0x02

struct hsm_progress {
	struct lu_fid		hp_fid;
	__u64			hp_cookie;
	struct hsm_extent	hp_extent;
	__u16			hp_flags;
	__u16			hp_errval; /* positive val */
	__u32			padding;
};

struct hsm_copy {
	__u64			hc_data_version;
	__u16			hc_flags;
	__u16			hc_errval; /* positive val */
	__u32			padding;
	struct hsm_action_item	hc_hai;
};

/** @} lustreuser */

#endif /* _LUSTRE_USER_H */
