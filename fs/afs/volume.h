/* volume.h: AFS volume management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_VOLUME_H
#define _LINUX_AFS_VOLUME_H

#include "types.h"
#include "fsclient.h"
#include "kafstimod.h"
#include "kafsasyncd.h"
#include "cache.h"

typedef enum {
	AFS_VLUPD_SLEEP,		/* sleeping waiting for update timer to fire */
	AFS_VLUPD_PENDING,		/* on pending queue */
	AFS_VLUPD_INPROGRESS,		/* op in progress */
	AFS_VLUPD_BUSYSLEEP,		/* sleeping because server returned EBUSY */
	
} __attribute__((packed)) afs_vlocation_upd_t;

/*****************************************************************************/
/*
 * entry in the cached volume location catalogue
 */
struct afs_cache_vlocation
{
	uint8_t			name[64];	/* volume name (lowercase, padded with NULs) */
	uint8_t			nservers;	/* number of entries used in servers[] */
	uint8_t			vidmask;	/* voltype mask for vid[] */
	uint8_t			srvtmask[8];	/* voltype masks for servers[] */
#define AFS_VOL_VTM_RW	0x01 /* R/W version of the volume is available (on this server) */
#define AFS_VOL_VTM_RO	0x02 /* R/O version of the volume is available (on this server) */
#define AFS_VOL_VTM_BAK	0x04 /* backup version of the volume is available (on this server) */

	afs_volid_t		vid[3];		/* volume IDs for R/W, R/O and Bak volumes */
	struct in_addr		servers[8];	/* fileserver addresses */
	time_t			rtime;		/* last retrieval time */
};

#ifdef AFS_CACHING_SUPPORT
extern struct cachefs_index_def afs_vlocation_cache_index_def;
#endif

/*****************************************************************************/
/*
 * volume -> vnode hash table entry
 */
struct afs_cache_vhash
{
	afs_voltype_t		vtype;		/* which volume variation */
	uint8_t			hash_bucket;	/* which hash bucket this represents */
} __attribute__((packed));

#ifdef AFS_CACHING_SUPPORT
extern struct cachefs_index_def afs_volume_cache_index_def;
#endif

/*****************************************************************************/
/*
 * AFS volume location record
 */
struct afs_vlocation
{
	atomic_t		usage;
	struct list_head	link;		/* link in cell volume location list */
	struct afs_timer	timeout;	/* decaching timer */
	struct afs_cell		*cell;		/* cell to which volume belongs */
#ifdef AFS_CACHING_SUPPORT
	struct cachefs_cookie	*cache;		/* caching cookie */
#endif
	struct afs_cache_vlocation vldb;	/* volume information DB record */
	struct afs_volume	*vols[3];	/* volume access record pointer (index by type) */
	rwlock_t		lock;		/* access lock */
	unsigned long		read_jif;	/* time at which last read from vlserver */
	struct afs_timer	upd_timer;	/* update timer */
	struct afs_async_op	upd_op;		/* update operation */
	afs_vlocation_upd_t	upd_state;	/* update state */
	unsigned short		upd_first_svix;	/* first server index during update */
	unsigned short		upd_curr_svix;	/* current server index during update */
	unsigned short		upd_rej_cnt;	/* ENOMEDIUM count during update */
	unsigned short		upd_busy_cnt;	/* EBUSY count during update */
	unsigned short		valid;		/* T if valid */
};

extern int afs_vlocation_lookup(struct afs_cell *cell,
				const char *name,
				unsigned namesz,
				struct afs_vlocation **_vlocation);

#define afs_get_vlocation(V) do { atomic_inc(&(V)->usage); } while(0)

extern void afs_put_vlocation(struct afs_vlocation *vlocation);
extern void afs_vlocation_do_timeout(struct afs_vlocation *vlocation);

/*****************************************************************************/
/*
 * AFS volume access record
 */
struct afs_volume
{
	atomic_t		usage;
	struct afs_cell		*cell;		/* cell to which belongs (unrefd ptr) */
	struct afs_vlocation	*vlocation;	/* volume location */
#ifdef AFS_CACHING_SUPPORT
	struct cachefs_cookie	*cache;		/* caching cookie */
#endif
	afs_volid_t		vid;		/* volume ID */
	afs_voltype_t		type;		/* type of volume */
	char			type_force;	/* force volume type (suppress R/O -> R/W) */
	unsigned short		nservers;	/* number of server slots filled */
	unsigned short		rjservers;	/* number of servers discarded due to -ENOMEDIUM */
	struct afs_server	*servers[8];	/* servers on which volume resides (ordered) */
	struct rw_semaphore	server_sem;	/* lock for accessing current server */
};

extern int afs_volume_lookup(const char *name,
			     struct afs_cell *cell,
			     int rwpath,
			     struct afs_volume **_volume);

#define afs_get_volume(V) do { atomic_inc(&(V)->usage); } while(0)

extern void afs_put_volume(struct afs_volume *volume);

extern int afs_volume_pick_fileserver(struct afs_volume *volume,
				      struct afs_server **_server);

extern int afs_volume_release_fileserver(struct afs_volume *volume,
					 struct afs_server *server,
					 int result);

#endif /* _LINUX_AFS_VOLUME_H */
