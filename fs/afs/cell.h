/* cell.h: AFS cell record
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_CELL_H
#define _LINUX_AFS_CELL_H

#include "types.h"
#include "cache.h"

#define AFS_CELL_MAX_ADDRS 15

extern volatile int afs_cells_being_purged; /* T when cells are being purged by rmmod */

/*****************************************************************************/
/*
 * entry in the cached cell catalogue
 */
struct afs_cache_cell
{
	char			name[64];	/* cell name (padded with NULs) */
	struct in_addr		vl_servers[15];	/* cached cell VL servers */
};

/*****************************************************************************/
/*
 * AFS cell record
 */
struct afs_cell
{
	atomic_t		usage;
	struct list_head	link;		/* main cell list link */
	struct list_head	proc_link;	/* /proc cell list link */
	struct proc_dir_entry	*proc_dir;	/* /proc dir for this cell */
#ifdef AFS_CACHING_SUPPORT
	struct cachefs_cookie	*cache;		/* caching cookie */
#endif

	/* server record management */
	rwlock_t		sv_lock;	/* active server list lock */
	struct list_head	sv_list;	/* active server list */
	struct list_head	sv_graveyard;	/* inactive server list */
	spinlock_t		sv_gylock;	/* inactive server list lock */

	/* volume location record management */
	struct rw_semaphore	vl_sem;		/* volume management serialisation semaphore */
	struct list_head	vl_list;	/* cell's active VL record list */
	struct list_head	vl_graveyard;	/* cell's inactive VL record list */
	spinlock_t		vl_gylock;	/* graveyard lock */
	unsigned short		vl_naddrs;	/* number of VL servers in addr list */
	unsigned short		vl_curr_svix;	/* current server index */
	struct in_addr		vl_addrs[AFS_CELL_MAX_ADDRS];	/* cell VL server addresses */

	char			name[0];	/* cell name - must go last */
};

extern int afs_cell_init(char *rootcell);

extern int afs_cell_create(const char *name, char *vllist, struct afs_cell **_cell);

extern int afs_cell_lookup(const char *name, unsigned nmsize, struct afs_cell **_cell);

#define afs_get_cell(C) do { atomic_inc(&(C)->usage); } while(0)

extern struct afs_cell *afs_get_cell_maybe(struct afs_cell **_cell);

extern void afs_put_cell(struct afs_cell *cell);

extern void afs_cell_purge(void);

#endif /* _LINUX_AFS_CELL_H */
