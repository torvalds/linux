/* server.c: AFS server record management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <rxrpc/peer.h>
#include <rxrpc/connection.h>
#include "volume.h"
#include "cell.h"
#include "server.h"
#include "transport.h"
#include "vlclient.h"
#include "kafstimod.h"
#include "internal.h"

DEFINE_SPINLOCK(afs_server_peer_lock);

#define FS_SERVICE_ID		1	/* AFS Volume Location Service ID */
#define VL_SERVICE_ID		52	/* AFS Volume Location Service ID */

static void __afs_server_timeout(struct afs_timer *timer)
{
	struct afs_server *server =
		list_entry(timer, struct afs_server, timeout);

	_debug("SERVER TIMEOUT [%p{u=%d}]",
	       server, atomic_read(&server->usage));

	afs_server_do_timeout(server);
}

static const struct afs_timer_ops afs_server_timer_ops = {
	.timed_out	= __afs_server_timeout,
};

/*****************************************************************************/
/*
 * lookup a server record in a cell
 * - TODO: search the cell's server list
 */
int afs_server_lookup(struct afs_cell *cell, const struct in_addr *addr,
		      struct afs_server **_server)
{
	struct afs_server *server, *active, *zombie;
	int loop;

	_enter("%p,%08x,", cell, ntohl(addr->s_addr));

	/* allocate and initialise a server record */
	server = kzalloc(sizeof(struct afs_server), GFP_KERNEL);
	if (!server) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	atomic_set(&server->usage, 1);

	INIT_LIST_HEAD(&server->link);
	init_rwsem(&server->sem);
	INIT_LIST_HEAD(&server->fs_callq);
	spin_lock_init(&server->fs_lock);
	INIT_LIST_HEAD(&server->cb_promises);
	spin_lock_init(&server->cb_lock);

	for (loop = 0; loop < AFS_SERVER_CONN_LIST_SIZE; loop++)
		server->fs_conn_cnt[loop] = 4;

	memcpy(&server->addr, addr, sizeof(struct in_addr));
	server->addr.s_addr = addr->s_addr;

	afs_timer_init(&server->timeout, &afs_server_timer_ops);

	/* add to the cell */
	write_lock(&cell->sv_lock);

	/* check the active list */
	list_for_each_entry(active, &cell->sv_list, link) {
		if (active->addr.s_addr == addr->s_addr)
			goto use_active_server;
	}

	/* check the inactive list */
	spin_lock(&cell->sv_gylock);
	list_for_each_entry(zombie, &cell->sv_graveyard, link) {
		if (zombie->addr.s_addr == addr->s_addr)
			goto resurrect_server;
	}
	spin_unlock(&cell->sv_gylock);

	afs_get_cell(cell);
	server->cell = cell;
	list_add_tail(&server->link, &cell->sv_list);

	write_unlock(&cell->sv_lock);

	*_server = server;
	_leave(" = 0 (%p)", server);
	return 0;

	/* found a matching active server */
 use_active_server:
	_debug("active server");
	afs_get_server(active);
	write_unlock(&cell->sv_lock);

	kfree(server);

	*_server = active;
	_leave(" = 0 (%p)", active);
	return 0;

	/* found a matching server in the graveyard, so resurrect it and
	 * dispose of the new record */
 resurrect_server:
	_debug("resurrecting server");

	list_move_tail(&zombie->link, &cell->sv_list);
	afs_get_server(zombie);
	afs_kafstimod_del_timer(&zombie->timeout);
	spin_unlock(&cell->sv_gylock);
	write_unlock(&cell->sv_lock);

	kfree(server);

	*_server = zombie;
	_leave(" = 0 (%p)", zombie);
	return 0;

} /* end afs_server_lookup() */

/*****************************************************************************/
/*
 * destroy a server record
 * - removes from the cell list
 */
void afs_put_server(struct afs_server *server)
{
	struct afs_cell *cell;

	if (!server)
		return;

	_enter("%p", server);

	cell = server->cell;

	/* sanity check */
	BUG_ON(atomic_read(&server->usage) <= 0);

	/* to prevent a race, the decrement and the dequeue must be effectively
	 * atomic */
	write_lock(&cell->sv_lock);

	if (likely(!atomic_dec_and_test(&server->usage))) {
		write_unlock(&cell->sv_lock);
		_leave("");
		return;
	}

	spin_lock(&cell->sv_gylock);
	list_move_tail(&server->link, &cell->sv_graveyard);

	/* time out in 10 secs */
	afs_kafstimod_add_timer(&server->timeout, 10 * HZ);

	spin_unlock(&cell->sv_gylock);
	write_unlock(&cell->sv_lock);

	_leave(" [killed]");
} /* end afs_put_server() */

/*****************************************************************************/
/*
 * timeout server record
 * - removes from the cell's graveyard if the usage count is zero
 */
void afs_server_do_timeout(struct afs_server *server)
{
	struct rxrpc_peer *peer;
	struct afs_cell *cell;
	int loop;

	_enter("%p", server);

	cell = server->cell;

	BUG_ON(atomic_read(&server->usage) < 0);

	/* remove from graveyard if still dead */
	spin_lock(&cell->vl_gylock);
	if (atomic_read(&server->usage) == 0)
		list_del_init(&server->link);
	else
		server = NULL;
	spin_unlock(&cell->vl_gylock);

	if (!server) {
		_leave("");
		return; /* resurrected */
	}

	/* we can now destroy it properly */
	afs_put_cell(cell);

	/* uncross-point the structs under a global lock */
	spin_lock(&afs_server_peer_lock);
	peer = server->peer;
	if (peer) {
		server->peer = NULL;
		peer->user = NULL;
	}
	spin_unlock(&afs_server_peer_lock);

	/* finish cleaning up the server */
	for (loop = AFS_SERVER_CONN_LIST_SIZE - 1; loop >= 0; loop--)
		if (server->fs_conn[loop])
			rxrpc_put_connection(server->fs_conn[loop]);

	if (server->vlserver)
		rxrpc_put_connection(server->vlserver);

	kfree(server);

	_leave(" [destroyed]");
} /* end afs_server_do_timeout() */

/*****************************************************************************/
/*
 * get a callslot on a connection to the fileserver on the specified server
 */
int afs_server_request_callslot(struct afs_server *server,
				struct afs_server_callslot *callslot)
{
	struct afs_server_callslot *pcallslot;
	struct rxrpc_connection *conn;
	int nconn, ret;

	_enter("%p,",server);

	INIT_LIST_HEAD(&callslot->link);
	callslot->task = current;
	callslot->conn = NULL;
	callslot->nconn = -1;
	callslot->ready = 0;

	ret = 0;
	conn = NULL;

	/* get hold of a callslot first */
	spin_lock(&server->fs_lock);

	/* resurrect the server if it's death timeout has expired */
	if (server->fs_state) {
		if (time_before(jiffies, server->fs_dead_jif)) {
			ret = server->fs_state;
			spin_unlock(&server->fs_lock);
			_leave(" = %d [still dead]", ret);
			return ret;
		}

		server->fs_state = 0;
	}

	/* try and find a connection that has spare callslots */
	for (nconn = 0; nconn < AFS_SERVER_CONN_LIST_SIZE; nconn++) {
		if (server->fs_conn_cnt[nconn] > 0) {
			server->fs_conn_cnt[nconn]--;
			spin_unlock(&server->fs_lock);
			callslot->nconn = nconn;
			goto obtained_slot;
		}
	}

	/* none were available - wait interruptibly for one to become
	 * available */
	set_current_state(TASK_INTERRUPTIBLE);
	list_add_tail(&callslot->link, &server->fs_callq);
	spin_unlock(&server->fs_lock);

	while (!callslot->ready && !signal_pending(current)) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}

	set_current_state(TASK_RUNNING);

	/* even if we were interrupted we may still be queued */
	if (!callslot->ready) {
		spin_lock(&server->fs_lock);
		list_del_init(&callslot->link);
		spin_unlock(&server->fs_lock);
	}

	nconn = callslot->nconn;

	/* if interrupted, we must release any slot we also got before
	 * returning an error */
	if (signal_pending(current)) {
		ret = -EINTR;
		goto error_release;
	}

	/* if we were woken up with an error, then pass that error back to the
	 * called */
	if (nconn < 0) {
		_leave(" = %d", callslot->errno);
		return callslot->errno;
	}

	/* were we given a connection directly? */
	if (callslot->conn) {
		/* yes - use it */
		_leave(" = 0 (nc=%d)", nconn);
		return 0;
	}

	/* got a callslot, but no connection */
 obtained_slot:

	/* need to get hold of the RxRPC connection */
	down_write(&server->sem);

	/* quick check to see if there's an outstanding error */
	ret = server->fs_state;
	if (ret)
		goto error_release_upw;

	if (server->fs_conn[nconn]) {
		/* reuse an existing connection */
		rxrpc_get_connection(server->fs_conn[nconn]);
		callslot->conn = server->fs_conn[nconn];
	}
	else {
		/* create a new connection */
		ret = rxrpc_create_connection(afs_transport,
					      htons(7000),
					      server->addr.s_addr,
					      FS_SERVICE_ID,
					      NULL,
					      &server->fs_conn[nconn]);

		if (ret < 0)
			goto error_release_upw;

		callslot->conn = server->fs_conn[0];
		rxrpc_get_connection(callslot->conn);
	}

	up_write(&server->sem);

 	_leave(" = 0");
	return 0;

	/* handle an error occurring */
 error_release_upw:
	up_write(&server->sem);

 error_release:
	/* either release the callslot or pass it along to another deserving
	 * task */
	spin_lock(&server->fs_lock);

	if (nconn < 0) {
		/* no callslot allocated */
	}
	else if (list_empty(&server->fs_callq)) {
		/* no one waiting */
		server->fs_conn_cnt[nconn]++;
		spin_unlock(&server->fs_lock);
	}
	else {
		/* someone's waiting - dequeue them and wake them up */
		pcallslot = list_entry(server->fs_callq.next,
				       struct afs_server_callslot, link);
		list_del_init(&pcallslot->link);

		pcallslot->errno = server->fs_state;
		if (!pcallslot->errno) {
			/* pass them out callslot details */
			callslot->conn = xchg(&pcallslot->conn,
					      callslot->conn);
			pcallslot->nconn = nconn;
			callslot->nconn = nconn = -1;
		}
		pcallslot->ready = 1;
		wake_up_process(pcallslot->task);
		spin_unlock(&server->fs_lock);
	}

	rxrpc_put_connection(callslot->conn);
	callslot->conn = NULL;

	_leave(" = %d", ret);
	return ret;

} /* end afs_server_request_callslot() */

/*****************************************************************************/
/*
 * release a callslot back to the server
 * - transfers the RxRPC connection to the next pending callslot if possible
 */
void afs_server_release_callslot(struct afs_server *server,
				 struct afs_server_callslot *callslot)
{
	struct afs_server_callslot *pcallslot;

	_enter("{ad=%08x,cnt=%u},{%d}",
	       ntohl(server->addr.s_addr),
	       server->fs_conn_cnt[callslot->nconn],
	       callslot->nconn);

	BUG_ON(callslot->nconn < 0);

	spin_lock(&server->fs_lock);

	if (list_empty(&server->fs_callq)) {
		/* no one waiting */
		server->fs_conn_cnt[callslot->nconn]++;
		spin_unlock(&server->fs_lock);
	}
	else {
		/* someone's waiting - dequeue them and wake them up */
		pcallslot = list_entry(server->fs_callq.next,
				       struct afs_server_callslot, link);
		list_del_init(&pcallslot->link);

		pcallslot->errno = server->fs_state;
		if (!pcallslot->errno) {
			/* pass them out callslot details */
			callslot->conn = xchg(&pcallslot->conn, callslot->conn);
			pcallslot->nconn = callslot->nconn;
			callslot->nconn = -1;
		}

		pcallslot->ready = 1;
		wake_up_process(pcallslot->task);
		spin_unlock(&server->fs_lock);
	}

	rxrpc_put_connection(callslot->conn);

	_leave("");
} /* end afs_server_release_callslot() */

/*****************************************************************************/
/*
 * get a handle to a connection to the vlserver (volume location) on the
 * specified server
 */
int afs_server_get_vlconn(struct afs_server *server,
			  struct rxrpc_connection **_conn)
{
	struct rxrpc_connection *conn;
	int ret;

	_enter("%p,", server);

	ret = 0;
	conn = NULL;
	down_read(&server->sem);

	if (server->vlserver) {
		/* reuse an existing connection */
		rxrpc_get_connection(server->vlserver);
		conn = server->vlserver;
		up_read(&server->sem);
	}
	else {
		/* create a new connection */
		up_read(&server->sem);
		down_write(&server->sem);
		if (!server->vlserver) {
			ret = rxrpc_create_connection(afs_transport,
						      htons(7003),
						      server->addr.s_addr,
						      VL_SERVICE_ID,
						      NULL,
						      &server->vlserver);
		}
		if (ret == 0) {
			rxrpc_get_connection(server->vlserver);
			conn = server->vlserver;
		}
		up_write(&server->sem);
	}

	*_conn = conn;
	_leave(" = %d", ret);
	return ret;
} /* end afs_server_get_vlconn() */
