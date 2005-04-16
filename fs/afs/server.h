/* server.h: AFS server record
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_SERVER_H
#define _LINUX_AFS_SERVER_H

#include "types.h"
#include "kafstimod.h"
#include <rxrpc/peer.h>
#include <linux/rwsem.h>

extern spinlock_t afs_server_peer_lock;

/*****************************************************************************/
/*
 * AFS server record
 */
struct afs_server
{
	atomic_t		usage;
	struct afs_cell		*cell;		/* cell in which server resides */
	struct list_head	link;		/* link in cell's server list */
	struct rw_semaphore	sem;		/* access lock */
	struct afs_timer	timeout;	/* graveyard timeout */
	struct in_addr		addr;		/* server address */
	struct rxrpc_peer	*peer;		/* peer record for this server */
	struct rxrpc_connection	*vlserver;	/* connection to the volume location service */

	/* file service access */
#define AFS_SERVER_CONN_LIST_SIZE 2
	struct rxrpc_connection	*fs_conn[AFS_SERVER_CONN_LIST_SIZE]; /* FS connections */
	unsigned		fs_conn_cnt[AFS_SERVER_CONN_LIST_SIZE];	/* per conn call count */
	struct list_head	fs_callq;	/* queue of processes waiting to make a call */
	spinlock_t		fs_lock;	/* access lock */
	int			fs_state;      	/* 0 or reason FS currently marked dead (-errno) */
	unsigned		fs_rtt;		/* FS round trip time */
	unsigned long		fs_act_jif;	/* time at which last activity occurred */
	unsigned long		fs_dead_jif;	/* time at which no longer to be considered dead */

	/* callback promise management */
	struct list_head	cb_promises;	/* as yet unbroken promises from this server */
	spinlock_t		cb_lock;	/* access lock */
};

extern int afs_server_lookup(struct afs_cell *cell,
			     const struct in_addr *addr,
			     struct afs_server **_server);

#define afs_get_server(S) do { atomic_inc(&(S)->usage); } while(0)

extern void afs_put_server(struct afs_server *server);
extern void afs_server_do_timeout(struct afs_server *server);

extern int afs_server_find_by_peer(const struct rxrpc_peer *peer,
				   struct afs_server **_server);

extern int afs_server_get_vlconn(struct afs_server *server,
				 struct rxrpc_connection **_conn);

static inline
struct afs_server *afs_server_get_from_peer(struct rxrpc_peer *peer)
{
	struct afs_server *server;

	spin_lock(&afs_server_peer_lock);
	server = peer->user;
	if (server)
		afs_get_server(server);
	spin_unlock(&afs_server_peer_lock);

	return server;
}

/*****************************************************************************/
/*
 * AFS server callslot grant record
 */
struct afs_server_callslot
{
	struct list_head	link;		/* link in server's list */
	struct task_struct	*task;		/* process waiting to make call */
	struct rxrpc_connection	*conn;		/* connection to use (or NULL on error) */
	short			nconn;		/* connection slot number (-1 on error) */
	char			ready;		/* T when ready */
	int			errno;		/* error number if nconn==-1 */
};

extern int afs_server_request_callslot(struct afs_server *server,
				       struct afs_server_callslot *callslot);

extern void afs_server_release_callslot(struct afs_server *server,
					struct afs_server_callslot *callslot);

#endif /* _LINUX_AFS_SERVER_H */
