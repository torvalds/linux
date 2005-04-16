/* types.h: AFS types
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_TYPES_H
#define _LINUX_AFS_TYPES_H

#ifdef __KERNEL__
#include <rxrpc/types.h>
#endif /* __KERNEL__ */

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

#ifdef __KERNEL__

struct afs_cell;
struct afs_vnode;

/*****************************************************************************/
/*
 * AFS file identifier
 */
struct afs_fid
{
	afs_volid_t	vid;		/* volume ID */
	afs_vnodeid_t	vnode;		/* file index within volume */
	unsigned	unique;		/* unique ID number (file index version) */
};

/*****************************************************************************/
/*
 * AFS callback notification
 */
typedef enum {
	AFSCM_CB_UNTYPED	= 0,	/* no type set on CB break */
	AFSCM_CB_EXCLUSIVE	= 1,	/* CB exclusive to CM [not implemented] */
	AFSCM_CB_SHARED		= 2,	/* CB shared by other CM's */
	AFSCM_CB_DROPPED	= 3,	/* CB promise cancelled by file server */
} afs_callback_type_t;

struct afs_callback
{
	struct afs_server	*server;	/* server that made the promise */
	struct afs_fid		fid;		/* file identifier */
	unsigned		version;	/* callback version */
	unsigned		expiry;		/* time at which expires */
	afs_callback_type_t	type;		/* type of callback */
};

#define AFSCBMAX 50

/*****************************************************************************/
/*
 * AFS volume information
 */
struct afs_volume_info
{
	afs_volid_t		vid;		/* volume ID */
	afs_voltype_t		type;		/* type of this volume */
	afs_volid_t		type_vids[5];	/* volume ID's for possible types for this vol */
	
	/* list of fileservers serving this volume */
	size_t			nservers;	/* number of entries used in servers[] */
	struct {
		struct in_addr	addr;		/* fileserver address */
	} servers[8];
};

/*****************************************************************************/
/*
 * AFS file status information
 */
struct afs_file_status
{
	unsigned		if_version;	/* interface version */
#define AFS_FSTATUS_VERSION	1

	afs_file_type_t		type;		/* file type */
	unsigned		nlink;		/* link count */
	size_t			size;		/* file size */
	afs_dataversion_t	version;	/* current data version */
	unsigned		author;		/* author ID */
	unsigned		owner;		/* owner ID */
	unsigned		caller_access;	/* access rights for authenticated caller */
	unsigned		anon_access;	/* access rights for unauthenticated caller */
	umode_t			mode;		/* UNIX mode */
	struct afs_fid		parent;		/* parent file ID */
	time_t			mtime_client;	/* last time client changed data */
	time_t			mtime_server;	/* last time server changed data */
};

/*****************************************************************************/
/*
 * AFS volume synchronisation information
 */
struct afs_volsync
{
	time_t			creation;	/* volume creation time */
};

#endif /* __KERNEL__ */

#endif /* _LINUX_AFS_TYPES_H */
