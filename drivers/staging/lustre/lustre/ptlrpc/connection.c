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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_RPC
#include <obd_support.h>
#include <obd_class.h>
#include <lustre_net.h>

#include "ptlrpc_internal.h"

static cfs_hash_t *conn_hash = NULL;
static cfs_hash_ops_t conn_hash_ops;

struct ptlrpc_connection *
ptlrpc_connection_get(lnet_process_id_t peer, lnet_nid_t self,
		      struct obd_uuid *uuid)
{
	struct ptlrpc_connection *conn, *conn2;

	conn = cfs_hash_lookup(conn_hash, &peer);
	if (conn)
		GOTO(out, conn);

	OBD_ALLOC_PTR(conn);
	if (!conn)
		return NULL;

	conn->c_peer = peer;
	conn->c_self = self;
	INIT_HLIST_NODE(&conn->c_hash);
	atomic_set(&conn->c_refcount, 1);
	if (uuid)
		obd_str2uuid(&conn->c_remote_uuid, uuid->uuid);

	/*
	 * Add the newly created conn to the hash, on key collision we
	 * lost a racing addition and must destroy our newly allocated
	 * connection.  The object which exists in the has will be
	 * returned and may be compared against out object.
	 */
	/* In the function below, .hs_keycmp resolves to
	 * conn_keycmp() */
	/* coverity[overrun-buffer-val] */
	conn2 = cfs_hash_findadd_unique(conn_hash, &peer, &conn->c_hash);
	if (conn != conn2) {
		OBD_FREE_PTR(conn);
		conn = conn2;
	}
out:
	CDEBUG(D_INFO, "conn=%p refcount %d to %s\n",
	       conn, atomic_read(&conn->c_refcount),
	       libcfs_nid2str(conn->c_peer.nid));
	return conn;
}
EXPORT_SYMBOL(ptlrpc_connection_get);

int ptlrpc_connection_put(struct ptlrpc_connection *conn)
{
	int rc = 0;

	if (!conn)
		return rc;

	LASSERT(atomic_read(&conn->c_refcount) > 1);

	/*
	 * We do not remove connection from hashtable and
	 * do not free it even if last caller released ref,
	 * as we want to have it cached for the case it is
	 * needed again.
	 *
	 * Deallocating it and later creating new connection
	 * again would be wastful. This way we also avoid
	 * expensive locking to protect things from get/put
	 * race when found cached connection is freed by
	 * ptlrpc_connection_put().
	 *
	 * It will be freed later in module unload time,
	 * when ptlrpc_connection_fini()->lh_exit->conn_exit()
	 * path is called.
	 */
	if (atomic_dec_return(&conn->c_refcount) == 1)
		rc = 1;

	CDEBUG(D_INFO, "PUT conn=%p refcount %d to %s\n",
	       conn, atomic_read(&conn->c_refcount),
	       libcfs_nid2str(conn->c_peer.nid));

	return rc;
}
EXPORT_SYMBOL(ptlrpc_connection_put);

struct ptlrpc_connection *
ptlrpc_connection_addref(struct ptlrpc_connection *conn)
{
	atomic_inc(&conn->c_refcount);
	CDEBUG(D_INFO, "conn=%p refcount %d to %s\n",
	       conn, atomic_read(&conn->c_refcount),
	       libcfs_nid2str(conn->c_peer.nid));

	return conn;
}
EXPORT_SYMBOL(ptlrpc_connection_addref);

int ptlrpc_connection_init(void)
{
	conn_hash = cfs_hash_create("CONN_HASH",
				    HASH_CONN_CUR_BITS,
				    HASH_CONN_MAX_BITS,
				    HASH_CONN_BKT_BITS, 0,
				    CFS_HASH_MIN_THETA,
				    CFS_HASH_MAX_THETA,
				    &conn_hash_ops, CFS_HASH_DEFAULT);
	if (!conn_hash)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(ptlrpc_connection_init);

void ptlrpc_connection_fini(void)
{
	cfs_hash_putref(conn_hash);
}
EXPORT_SYMBOL(ptlrpc_connection_fini);

/*
 * Hash operations for net_peer<->connection
 */
static unsigned
conn_hashfn(cfs_hash_t *hs, const void *key, unsigned mask)
{
	return cfs_hash_djb2_hash(key, sizeof(lnet_process_id_t), mask);
}

static int
conn_keycmp(const void *key, struct hlist_node *hnode)
{
	struct ptlrpc_connection *conn;
	const lnet_process_id_t *conn_key;

	LASSERT(key != NULL);
	conn_key = (lnet_process_id_t*)key;
	conn = hlist_entry(hnode, struct ptlrpc_connection, c_hash);

	return conn_key->nid == conn->c_peer.nid &&
	       conn_key->pid == conn->c_peer.pid;
}

static void *
conn_key(struct hlist_node *hnode)
{
	struct ptlrpc_connection *conn;
	conn = hlist_entry(hnode, struct ptlrpc_connection, c_hash);
	return &conn->c_peer;
}

static void *
conn_object(struct hlist_node *hnode)
{
	return hlist_entry(hnode, struct ptlrpc_connection, c_hash);
}

static void
conn_get(cfs_hash_t *hs, struct hlist_node *hnode)
{
	struct ptlrpc_connection *conn;

	conn = hlist_entry(hnode, struct ptlrpc_connection, c_hash);
	atomic_inc(&conn->c_refcount);
}

static void
conn_put_locked(cfs_hash_t *hs, struct hlist_node *hnode)
{
	struct ptlrpc_connection *conn;

	conn = hlist_entry(hnode, struct ptlrpc_connection, c_hash);
	atomic_dec(&conn->c_refcount);
}

static void
conn_exit(cfs_hash_t *hs, struct hlist_node *hnode)
{
	struct ptlrpc_connection *conn;

	conn = hlist_entry(hnode, struct ptlrpc_connection, c_hash);
	/*
	 * Nothing should be left. Connection user put it and
	 * connection also was deleted from table by this time
	 * so we should have 0 refs.
	 */
	LASSERTF(atomic_read(&conn->c_refcount) == 0,
		 "Busy connection with %d refs\n",
		 atomic_read(&conn->c_refcount));
	OBD_FREE_PTR(conn);
}

static cfs_hash_ops_t conn_hash_ops = {
	.hs_hash	= conn_hashfn,
	.hs_keycmp      = conn_keycmp,
	.hs_key	 = conn_key,
	.hs_object      = conn_object,
	.hs_get	 = conn_get,
	.hs_put_locked  = conn_put_locked,
	.hs_exit	= conn_exit,
};
