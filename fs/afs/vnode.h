/* vnode.h: AFS vnode record
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_VNODE_H
#define _LINUX_AFS_VNODE_H

#include <linux/fs.h>
#include "server.h"
#include "kafstimod.h"
#include "cache.h"

#ifdef __KERNEL__

struct afs_rxfs_fetch_descriptor;

/*****************************************************************************/
/*
 * vnode catalogue entry
 */
struct afs_cache_vnode
{
	afs_vnodeid_t		vnode_id;	/* vnode ID */
	unsigned		vnode_unique;	/* vnode ID uniquifier */
	afs_dataversion_t	data_version;	/* data version */
};

#ifdef AFS_CACHING_SUPPORT
extern struct cachefs_index_def afs_vnode_cache_index_def;
#endif

/*****************************************************************************/
/*
 * AFS inode private data
 */
struct afs_vnode
{
	struct inode		vfs_inode;	/* the VFS's inode record */

	struct afs_volume	*volume;	/* volume on which vnode resides */
	struct afs_fid		fid;		/* the file identifier for this inode */
	struct afs_file_status	status;		/* AFS status info for this file */
#ifdef AFS_CACHING_SUPPORT
	struct cachefs_cookie	*cache;		/* caching cookie */
#endif

	wait_queue_head_t	update_waitq;	/* status fetch waitqueue */
	unsigned		update_cnt;	/* number of outstanding ops that will update the
						 * status */
	spinlock_t		lock;		/* waitqueue/flags lock */
	unsigned		flags;
#define AFS_VNODE_CHANGED	0x00000001	/* set if vnode reported changed by callback */
#define AFS_VNODE_DELETED	0x00000002	/* set if vnode deleted on server */
#define AFS_VNODE_MOUNTPOINT	0x00000004	/* set if vnode is a mountpoint symlink */

	/* outstanding callback notification on this file */
	struct afs_server	*cb_server;	/* server that made the current promise */
	struct list_head	cb_link;	/* link in server's promises list */
	struct list_head	cb_hash_link;	/* link in master callback hash */
	struct afs_timer	cb_timeout;	/* timeout on promise */
	unsigned		cb_version;	/* callback version */
	unsigned		cb_expiry;	/* callback expiry time */
	afs_callback_type_t	cb_type;	/* type of callback */
};

static inline struct afs_vnode *AFS_FS_I(struct inode *inode)
{
	return container_of(inode,struct afs_vnode,vfs_inode);
}

static inline struct inode *AFS_VNODE_TO_I(struct afs_vnode *vnode)
{
	return &vnode->vfs_inode;
}

extern int afs_vnode_fetch_status(struct afs_vnode *vnode);

extern int afs_vnode_fetch_data(struct afs_vnode *vnode,
				struct afs_rxfs_fetch_descriptor *desc);

extern int afs_vnode_give_up_callback(struct afs_vnode *vnode);

extern struct afs_timer_ops afs_vnode_cb_timed_out_ops;

#endif /* __KERNEL__ */

#endif /* _LINUX_AFS_VNODE_H */
