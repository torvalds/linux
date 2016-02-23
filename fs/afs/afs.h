/* AFS common types
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef AFS_H
#define AFS_H

#include <linux/in.h>

#define AFS_MAXCELLNAME	64		/* maximum length of a cell name */
#define AFS_MAXVOLNAME	64		/* maximum length of a volume name */
#define AFSNAMEMAX	256		/* maximum length of a filename plus NUL */
#define AFSPATHMAX	1024		/* maximum length of a pathname plus NUL */
#define AFSOPAQUEMAX	1024		/* maximum length of an opaque field */

typedef unsigned			afs_volid_t;
typedef unsigned			afs_vnodeid_t;
typedef unsigned long long		afs_dataversion_t;

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
	afs_vnodeid_t	vnode;		/* file index within volume */
	unsigned	unique;		/* unique ID number (file index version) */
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
	struct afs_fid		fid;		/* file identifier */
	unsigned		version;	/* callback version */
	unsigned		expiry;		/* time at which expires */
	afs_callback_type_t	type;		/* type of callback */
};

#define AFSCBMAX 50	/* maximum callbacks transferred per bulk op */

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
	unsigned		if_version;	/* interface version */
#define AFS_FSTATUS_VERSION	1

	afs_file_type_t		type;		/* file type */
	unsigned		nlink;		/* link count */
	u64			size;		/* file size */
	afs_dataversion_t	data_version;	/* current data version */
	u32			author;		/* author ID */
	kuid_t			owner;		/* owner ID */
	kgid_t			group;		/* group ID */
	afs_access_t		caller_access;	/* access rights for authenticated caller */
	afs_access_t		anon_access;	/* access rights for unauthenticated caller */
	umode_t			mode;		/* UNIX mode */
	struct afs_fid		parent;		/* parent dir ID for non-dirs only */
	time_t			mtime_client;	/* last time client changed data */
	time_t			mtime_server;	/* last time server changed data */
	s32			lock_count;	/* file lock count (0=UNLK -1=WRLCK +ve=#RDLCK */
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
	time_t			creation;	/* volume creation time */
};

/*
 * AFS volume status record
 */
struct afs_volume_status {
	u32			vid;		/* volume ID */
	u32			parent_id;	/* parent volume ID */
	u8			online;		/* true if volume currently online and available */
	u8			in_service;	/* true if volume currently in service */
	u8			blessed;	/* same as in_service */
	u8			needs_salvage;	/* true if consistency checking required */
	u32			type;		/* volume type (afs_voltype_t) */
	u32			min_quota;	/* minimum space set aside (blocks) */
	u32			max_quota;	/* maximum space this volume may occupy (blocks) */
	u32			blocks_in_use;	/* space this volume currently occupies (blocks) */
	u32			part_blocks_avail; /* space available in volume's partition */
	u32			part_max_blocks; /* size of volume's partition */
};

#define AFS_BLOCK_SIZE	1024

#endif /* AFS_H */
