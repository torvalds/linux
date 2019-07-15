/* SPDX-License-Identifier: GPL-2.0-or-later */
/* AFS common types
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef AFS_H
#define AFS_H

#include <linux/in.h>

#define AFS_MAXCELLNAME		64  	/* Maximum length of a cell name */
#define AFS_MAXVOLNAME		64  	/* Maximum length of a volume name */
#define AFS_MAXNSERVERS		8   	/* Maximum servers in a basic volume record */
#define AFS_NMAXNSERVERS	13  	/* Maximum servers in a N/U-class volume record */
#define AFS_MAXTYPES		3	/* Maximum number of volume types */
#define AFSNAMEMAX		256 	/* Maximum length of a filename plus NUL */
#define AFSPATHMAX		1024	/* Maximum length of a pathname plus NUL */
#define AFSOPAQUEMAX		1024	/* Maximum length of an opaque field */

#define AFS_VL_MAX_LIFESPAN	(120 * HZ)
#define AFS_PROBE_MAX_LIFESPAN	(30 * HZ)

typedef u64			afs_volid_t;
typedef u64			afs_vnodeid_t;
typedef u64			afs_dataversion_t;

typedef enum {
	AFSVL_RWVOL,			/* read/write volume */
	AFSVL_ROVOL,			/* read-only volume */
	AFSVL_BACKVOL,			/* backup volume */
} __attribute__((packed)) afs_voltype_t;

typedef enum {
	AFS_FTYPE_INVALID	= 0,
	AFS_FTYPE_FILE		= 1,
	AFS_FTYPE_DIR		= 2,
	AFS_FTYPE_SYMLINK	= 3,
} afs_file_type_t;

typedef enum {
	AFS_LOCK_READ		= 0,	/* read lock request */
	AFS_LOCK_WRITE		= 1,	/* write lock request */
} afs_lock_type_t;

#define AFS_LOCKWAIT		(5 * 60) /* time until a lock times out (seconds) */

/*
 * AFS file identifier
 */
struct afs_fid {
	afs_volid_t	vid;		/* volume ID */
	afs_vnodeid_t	vnode;		/* Lower 64-bits of file index within volume */
	u32		vnode_hi;	/* Upper 32-bits of file index */
	u32		unique;		/* unique ID number (file index version) */
};

/*
 * AFS callback notification
 */
typedef enum {
	AFSCM_CB_UNTYPED	= 0,	/* no type set on CB break */
	AFSCM_CB_EXCLUSIVE	= 1,	/* CB exclusive to CM [not implemented] */
	AFSCM_CB_SHARED		= 2,	/* CB shared by other CM's */
	AFSCM_CB_DROPPED	= 3,	/* CB promise cancelled by file server */
} afs_callback_type_t;

struct afs_callback {
	time64_t		expires_at;	/* Time at which expires */
	//unsigned		version;	/* Callback version */
	//afs_callback_type_t	type;		/* Type of callback */
};

struct afs_callback_break {
	struct afs_fid		fid;		/* File identifier */
	//struct afs_callback	cb;		/* Callback details */
};

#define AFSCBMAX 50	/* maximum callbacks transferred per bulk op */

struct afs_uuid {
	__be32		time_low;			/* low part of timestamp */
	__be16		time_mid;			/* mid part of timestamp */
	__be16		time_hi_and_version;		/* high part of timestamp and version  */
	__s8		clock_seq_hi_and_reserved;	/* clock seq hi and variant */
	__s8		clock_seq_low;			/* clock seq low */
	__s8		node[6];			/* spatially unique node ID (MAC addr) */
};

/*
 * AFS volume information
 */
struct afs_volume_info {
	afs_volid_t		vid;		/* volume ID */
	afs_voltype_t		type;		/* type of this volume */
	afs_volid_t		type_vids[5];	/* volume ID's for possible types for this vol */

	/* list of fileservers serving this volume */
	size_t			nservers;	/* number of entries used in servers[] */
	struct {
		struct in_addr	addr;		/* fileserver address */
	} servers[8];
};

/*
 * AFS security ACE access mask
 */
typedef u32 afs_access_t;
#define AFS_ACE_READ		0x00000001U	/* - permission to read a file/dir */
#define AFS_ACE_WRITE		0x00000002U	/* - permission to write/chmod a file */
#define AFS_ACE_INSERT		0x00000004U	/* - permission to create dirent in a dir */
#define AFS_ACE_LOOKUP		0x00000008U	/* - permission to lookup a file/dir in a dir */
#define AFS_ACE_DELETE		0x00000010U	/* - permission to delete a dirent from a dir */
#define AFS_ACE_LOCK		0x00000020U	/* - permission to lock a file */
#define AFS_ACE_ADMINISTER	0x00000040U	/* - permission to change ACL */
#define AFS_ACE_USER_A		0x01000000U	/* - 'A' user-defined permission */
#define AFS_ACE_USER_B		0x02000000U	/* - 'B' user-defined permission */
#define AFS_ACE_USER_C		0x04000000U	/* - 'C' user-defined permission */
#define AFS_ACE_USER_D		0x08000000U	/* - 'D' user-defined permission */
#define AFS_ACE_USER_E		0x10000000U	/* - 'E' user-defined permission */
#define AFS_ACE_USER_F		0x20000000U	/* - 'F' user-defined permission */
#define AFS_ACE_USER_G		0x40000000U	/* - 'G' user-defined permission */
#define AFS_ACE_USER_H		0x80000000U	/* - 'H' user-defined permission */

/*
 * AFS file status information
 */
struct afs_file_status {
	u64			size;		/* file size */
	afs_dataversion_t	data_version;	/* current data version */
	struct timespec64	mtime_client;	/* Last time client changed data */
	struct timespec64	mtime_server;	/* Last time server changed data */
	s64			author;		/* author ID */
	s64			owner;		/* owner ID */
	s64			group;		/* group ID */
	afs_access_t		caller_access;	/* access rights for authenticated caller */
	afs_access_t		anon_access;	/* access rights for unauthenticated caller */
	umode_t			mode;		/* UNIX mode */
	afs_file_type_t		type;		/* file type */
	u32			nlink;		/* link count */
	s32			lock_count;	/* file lock count (0=UNLK -1=WRLCK +ve=#RDLCK */
	u32			abort_code;	/* Abort if bulk-fetching this failed */
};

struct afs_status_cb {
	struct afs_file_status	status;
	struct afs_callback	callback;
	unsigned int		cb_break;	/* Pre-op callback break counter */
	bool			have_status;	/* True if status record was retrieved */
	bool			have_cb;	/* True if cb record was retrieved */
	bool			have_error;	/* True if status.abort_code indicates an error */
};

/*
 * AFS file status change request
 */

#define AFS_SET_MTIME		0x01		/* set the mtime */
#define AFS_SET_OWNER		0x02		/* set the owner ID */
#define AFS_SET_GROUP		0x04		/* set the group ID (unsupported?) */
#define AFS_SET_MODE		0x08		/* set the UNIX mode */
#define AFS_SET_SEG_SIZE	0x10		/* set the segment size (unsupported) */

/*
 * AFS volume synchronisation information
 */
struct afs_volsync {
	time64_t		creation;	/* volume creation time */
};

/*
 * AFS volume status record
 */
struct afs_volume_status {
	afs_volid_t		vid;		/* volume ID */
	afs_volid_t		parent_id;	/* parent volume ID */
	u8			online;		/* true if volume currently online and available */
	u8			in_service;	/* true if volume currently in service */
	u8			blessed;	/* same as in_service */
	u8			needs_salvage;	/* true if consistency checking required */
	u32			type;		/* volume type (afs_voltype_t) */
	u64			min_quota;	/* minimum space set aside (blocks) */
	u64			max_quota;	/* maximum space this volume may occupy (blocks) */
	u64			blocks_in_use;	/* space this volume currently occupies (blocks) */
	u64			part_blocks_avail; /* space available in volume's partition */
	u64			part_max_blocks; /* size of volume's partition */
	s64			vol_copy_date;
	s64			vol_backup_date;
};

#define AFS_BLOCK_SIZE	1024

/*
 * XDR encoding of UUID in AFS.
 */
struct afs_uuid__xdr {
	__be32		time_low;
	__be32		time_mid;
	__be32		time_hi_and_version;
	__be32		clock_seq_hi_and_reserved;
	__be32		clock_seq_low;
	__be32		node[6];
};

#endif /* AFS_H */
