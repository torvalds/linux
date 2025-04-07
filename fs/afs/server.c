// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS server record management
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include "afs_fs.h"
#include "internal.h"
#include "protocol_yfs.h"

static unsigned afs_server_gc_delay = 10;	/* Server record timeout in seconds */
static atomic_t afs_server_debug_id;

static void __afs_put_server(struct afs_net *, struct afs_server *);
static void afs_server_timer(struct timer_list *timer);
static void afs_server_destroyer(struct work_struct *work);

/*
 * Find a server by one of its addresses.
 */
struct afs_server *afs_find_server(const struct rxrpc_peer *peer)
{
	struct afs_server *server = (struct afs_server *)rxrpc_kernel_get_peer_data(peer);

	if (!server)
		return NULL;
	return afs_use_server(server, false, afs_server_trace_use_cm_call);
}

/*
 * Look up a server by its UUID and mark it active.  The caller must hold
 * cell->fs_lock.
 */
static struct afs_server *afs_find_server_by_uuid(struct afs_cell *cell, const uuid_t *uuid)
{
	struct afs_server *server;
	struct rb_node *p;
	int diff;

	_enter("%pU", uuid);

	p = cell->fs_servers.rb_node;
	while (p) {
		server = rb_entry(p, struct afs_server, uuid_rb);

		diff = memcmp(uuid, &server->uuid, sizeof(*uuid));
		if (diff < 0) {
			p = p->rb_left;
		} else if (diff > 0) {
			p = p->rb_right;
		} else {
			if (test_bit(AFS_SERVER_FL_UNCREATED, &server->flags))
				return NULL; /* Need a write lock */
			afs_use_server(server, true, afs_server_trace_use_by_uuid);
			return server;
		}
	}

	return NULL;
}

/*
 * Install a server record in the cell tree.  The caller must hold an exclusive
 * lock on cell->fs_lock.
 */
static struct afs_server *afs_install_server(struct afs_cell *cell,
					     struct afs_server **candidate)
{
	struct afs_server *server;
	struct afs_net *net = cell->net;
	struct rb_node **pp, *p;
	int diff;

	_enter("%p", candidate);

	/* Firstly install the server in the UUID lookup tree */
	pp = &cell->fs_servers.rb_node;
	p = NULL;
	while (*pp) {
		p = *pp;
		_debug("- consider %p", p);
		server = rb_entry(p, struct afs_server, uuid_rb);
		diff = memcmp(&(*candidate)->uuid, &server->uuid, sizeof(uuid_t));
		if (diff < 0)
			pp = &(*pp)->rb_left;
		else if (diff > 0)
			pp = &(*pp)->rb_right;
		else
			goto exists;
	}

	server = *candidate;
	*candidate = NULL;
	rb_link_node(&server->uuid_rb, p, pp);
	rb_insert_color(&server->uuid_rb, &cell->fs_servers);
	write_seqlock(&net->fs_lock);
	hlist_add_head_rcu(&server->proc_link, &net->fs_proc);
	write_sequnlock(&net->fs_lock);

	afs_get_cell(cell, afs_cell_trace_get_server);

exists:
	afs_use_server(server, true, afs_server_trace_use_install);
	return server;
}

/*
 * Allocate a new server record and mark it as active but uncreated.
 */
static struct afs_server *afs_alloc_server(struct afs_cell *cell, const uuid_t *uuid)
{
	struct afs_server *server;
	struct afs_net *net = cell->net;

	_enter("");

	server = kzalloc(sizeof(struct afs_server), GFP_KERNEL);
	if (!server)
		return NULL;

	refcount_set(&server->ref, 1);
	atomic_set(&server->active, 0);
	__set_bit(AFS_SERVER_FL_UNCREATED, &server->flags);
	server->debug_id = atomic_inc_return(&afs_server_debug_id);
	server->uuid = *uuid;
	rwlock_init(&server->fs_lock);
	INIT_WORK(&server->destroyer, &afs_server_destroyer);
	timer_setup(&server->timer, afs_server_timer, 0);
	INIT_LIST_HEAD(&server->volumes);
	init_waitqueue_head(&server->probe_wq);
	INIT_LIST_HEAD(&server->probe_link);
	INIT_HLIST_NODE(&server->proc_link);
	spin_lock_init(&server->probe_lock);
	server->cell = cell;
	server->rtt = UINT_MAX;
	server->service_id = FS_SERVICE;
	server->probe_counter = 1;
	server->probed_at = jiffies - LONG_MAX / 2;

	afs_inc_servers_outstanding(net);
	_leave(" = %p", server);
	return server;
}

/*
 * Look up an address record for a server
 */
static struct afs_addr_list *afs_vl_lookup_addrs(struct afs_server *server,
						 struct key *key)
{
	struct afs_vl_cursor vc;
	struct afs_addr_list *alist = NULL;
	int ret;

	ret = -ERESTARTSYS;
	if (afs_begin_vlserver_operation(&vc, server->cell, key)) {
		while (afs_select_vlserver(&vc)) {
			if (test_bit(AFS_VLSERVER_FL_IS_YFS, &vc.server->flags))
				alist = afs_yfsvl_get_endpoints(&vc, &server->uuid);
			else
				alist = afs_vl_get_addrs_u(&vc, &server->uuid);
		}

		ret = afs_end_vlserver_operation(&vc);
	}

	return ret < 0 ? ERR_PTR(ret) : alist;
}

/*
 * Get or create a fileserver record and return it with an active-use count on
 * it.
 */
struct afs_server *afs_lookup_server(struct afs_cell *cell, struct key *key,
				     const uuid_t *uuid, u32 addr_version)
{
	struct afs_addr_list *alist = NULL;
	struct afs_server *server, *candidate = NULL;
	bool creating = false;
	int ret;

	_enter("%p,%pU", cell->net, uuid);

	down_read(&cell->fs_lock);
	server = afs_find_server_by_uuid(cell, uuid);
	/* Won't see servers marked uncreated. */
	up_read(&cell->fs_lock);

	if (server) {
		timer_delete_sync(&server->timer);
		if (test_bit(AFS_SERVER_FL_CREATING, &server->flags))
			goto wait_for_creation;
		if (server->addr_version != addr_version)
			set_bit(AFS_SERVER_FL_NEEDS_UPDATE, &server->flags);
		return server;
	}

	candidate = afs_alloc_server(cell, uuid);
	if (!candidate) {
		afs_put_addrlist(alist, afs_alist_trace_put_server_oom);
		return ERR_PTR(-ENOMEM);
	}

	down_write(&cell->fs_lock);
	server = afs_install_server(cell, &candidate);
	if (test_bit(AFS_SERVER_FL_CREATING, &server->flags)) {
		/* We need to wait for creation to complete. */
		up_write(&cell->fs_lock);
		goto wait_for_creation;
	}
	if (test_bit(AFS_SERVER_FL_UNCREATED, &server->flags)) {
		set_bit(AFS_SERVER_FL_CREATING, &server->flags);
		clear_bit(AFS_SERVER_FL_UNCREATED, &server->flags);
		creating = true;
	}
	up_write(&cell->fs_lock);
	timer_delete_sync(&server->timer);

	/* If we get to create the server, we look up the addresses and then
	 * immediately dispatch an asynchronous probe to each interface on the
	 * fileserver.  This will make sure the repeat-probing service is
	 * started.
	 */
	if (creating) {
		alist = afs_vl_lookup_addrs(server, key);
		if (IS_ERR(alist)) {
			ret = PTR_ERR(alist);
			goto create_failed;
		}

		ret = afs_fs_probe_fileserver(cell->net, server, alist, key);
		if (ret)
			goto create_failed;

		clear_and_wake_up_bit(AFS_SERVER_FL_CREATING, &server->flags);
	}

out:
	afs_put_addrlist(alist, afs_alist_trace_put_server_create);
	if (candidate) {
		kfree(rcu_access_pointer(server->endpoint_state));
		kfree(candidate);
		afs_dec_servers_outstanding(cell->net);
	}
	return server ?: ERR_PTR(ret);

wait_for_creation:
	afs_see_server(server, afs_server_trace_wait_create);
	wait_on_bit(&server->flags, AFS_SERVER_FL_CREATING, TASK_UNINTERRUPTIBLE);
	if (test_bit_acquire(AFS_SERVER_FL_UNCREATED, &server->flags)) {
		/* Barrier: read flag before error */
		ret = READ_ONCE(server->create_error);
		afs_put_server(cell->net, server, afs_server_trace_unuse_create_fail);
		server = NULL;
		goto out;
	}

	ret = 0;
	goto out;

create_failed:
	down_write(&cell->fs_lock);

	WRITE_ONCE(server->create_error, ret);
	smp_wmb(); /* Barrier: set error before flag. */
	set_bit(AFS_SERVER_FL_UNCREATED, &server->flags);

	clear_and_wake_up_bit(AFS_SERVER_FL_CREATING, &server->flags);

	if (test_bit(AFS_SERVER_FL_UNCREATED, &server->flags)) {
		clear_bit(AFS_SERVER_FL_UNCREATED, &server->flags);
		creating = true;
	}
	afs_unuse_server(cell->net, server, afs_server_trace_unuse_create_fail);
	server = NULL;

	up_write(&cell->fs_lock);
	goto out;
}

/*
 * Set/reduce a server's timer.
 */
static void afs_set_server_timer(struct afs_server *server, unsigned int delay_secs)
{
	mod_timer(&server->timer, jiffies + delay_secs * HZ);
}

/*
 * Get a reference on a server object.
 */
struct afs_server *afs_get_server(struct afs_server *server,
				  enum afs_server_trace reason)
{
	unsigned int a;
	int r;

	__refcount_inc(&server->ref, &r);
	a = atomic_read(&server->active);
	trace_afs_server(server->debug_id, r + 1, a, reason);
	return server;
}

/*
 * Get an active count on a server object and maybe remove from the inactive
 * list.
 */
struct afs_server *afs_use_server(struct afs_server *server, bool activate,
				  enum afs_server_trace reason)
{
	unsigned int a;
	int r;

	__refcount_inc(&server->ref, &r);
	a = atomic_inc_return(&server->active);
	if (a == 1 && activate &&
	    !test_bit(AFS_SERVER_FL_EXPIRED, &server->flags))
		timer_delete(&server->timer);

	trace_afs_server(server->debug_id, r + 1, a, reason);
	return server;
}

/*
 * Release a reference on a server record.
 */
void afs_put_server(struct afs_net *net, struct afs_server *server,
		    enum afs_server_trace reason)
{
	unsigned int a, debug_id = server->debug_id;
	bool zero;
	int r;

	if (!server)
		return;

	a = atomic_read(&server->active);
	zero = __refcount_dec_and_test(&server->ref, &r);
	trace_afs_server(debug_id, r - 1, a, reason);
	if (unlikely(zero))
		__afs_put_server(net, server);
}

/*
 * Drop an active count on a server object without updating the last-unused
 * time.
 */
void afs_unuse_server_notime(struct afs_net *net, struct afs_server *server,
			     enum afs_server_trace reason)
{
	if (!server)
		return;

	if (atomic_dec_and_test(&server->active)) {
		if (test_bit(AFS_SERVER_FL_EXPIRED, &server->flags) ||
		    READ_ONCE(server->cell->state) >= AFS_CELL_REMOVING)
			schedule_work(&server->destroyer);
	}

	afs_put_server(net, server, reason);
}

/*
 * Drop an active count on a server object.
 */
void afs_unuse_server(struct afs_net *net, struct afs_server *server,
		      enum afs_server_trace reason)
{
	if (!server)
		return;

	if (atomic_dec_and_test(&server->active)) {
		if (!test_bit(AFS_SERVER_FL_EXPIRED, &server->flags) &&
		    READ_ONCE(server->cell->state) < AFS_CELL_REMOVING) {
			time64_t unuse_time = ktime_get_real_seconds();

			server->unuse_time = unuse_time;
			afs_set_server_timer(server, afs_server_gc_delay);
		} else {
			schedule_work(&server->destroyer);
		}
	}

	afs_put_server(net, server, reason);
}

static void afs_server_rcu(struct rcu_head *rcu)
{
	struct afs_server *server = container_of(rcu, struct afs_server, rcu);

	trace_afs_server(server->debug_id, refcount_read(&server->ref),
			 atomic_read(&server->active), afs_server_trace_free);
	afs_put_endpoint_state(rcu_access_pointer(server->endpoint_state),
			       afs_estate_trace_put_server);
	afs_put_cell(server->cell, afs_cell_trace_put_server);
	kfree(server);
}

static void __afs_put_server(struct afs_net *net, struct afs_server *server)
{
	call_rcu(&server->rcu, afs_server_rcu);
	afs_dec_servers_outstanding(net);
}

static void afs_give_up_callbacks(struct afs_net *net, struct afs_server *server)
{
	struct afs_endpoint_state *estate = rcu_access_pointer(server->endpoint_state);
	struct afs_addr_list *alist = estate->addresses;

	afs_fs_give_up_all_callbacks(net, server, &alist->addrs[alist->preferred], NULL);
}

/*
 * Check to see if the server record has expired.
 */
static bool afs_has_server_expired(const struct afs_server *server)
{
	time64_t expires_at;

	if (atomic_read(&server->active))
		return false;

	if (server->cell->net->live ||
	    server->cell->state >= AFS_CELL_REMOVING) {
		trace_afs_server(server->debug_id, refcount_read(&server->ref),
				 0, afs_server_trace_purging);
		return true;
	}

	expires_at = server->unuse_time;
	if (!test_bit(AFS_SERVER_FL_VL_FAIL, &server->flags) &&
	    !test_bit(AFS_SERVER_FL_NOT_FOUND, &server->flags))
		expires_at += afs_server_gc_delay;

	return ktime_get_real_seconds() > expires_at;
}

/*
 * Remove a server record from it's parent cell's database.
 */
static bool afs_remove_server_from_cell(struct afs_server *server)
{
	struct afs_cell *cell = server->cell;

	down_write(&cell->fs_lock);

	if (!afs_has_server_expired(server)) {
		up_write(&cell->fs_lock);
		return false;
	}

	set_bit(AFS_SERVER_FL_EXPIRED, &server->flags);
	_debug("expire %pU %u", &server->uuid, atomic_read(&server->active));
	afs_see_server(server, afs_server_trace_see_expired);
	rb_erase(&server->uuid_rb, &cell->fs_servers);
	up_write(&cell->fs_lock);
	return true;
}

static void afs_server_destroyer(struct work_struct *work)
{
	struct afs_endpoint_state *estate;
	struct afs_server *server = container_of(work, struct afs_server, destroyer);
	struct afs_net *net = server->cell->net;

	afs_see_server(server, afs_server_trace_see_destroyer);

	if (test_bit(AFS_SERVER_FL_EXPIRED, &server->flags))
		return;

	if (!afs_remove_server_from_cell(server))
		return;

	timer_shutdown_sync(&server->timer);
	cancel_work(&server->destroyer);

	if (test_bit(AFS_SERVER_FL_MAY_HAVE_CB, &server->flags))
		afs_give_up_callbacks(net, server);

	/* Unbind the rxrpc_peer records from the server. */
	estate = rcu_access_pointer(server->endpoint_state);
	if (estate)
		afs_set_peer_appdata(server, estate->addresses, NULL);

	write_seqlock(&net->fs_lock);
	list_del_init(&server->probe_link);
	if (!hlist_unhashed(&server->proc_link))
		hlist_del_rcu(&server->proc_link);
	write_sequnlock(&net->fs_lock);

	afs_put_server(net, server, afs_server_trace_destroy);
}

static void afs_server_timer(struct timer_list *timer)
{
	struct afs_server *server = container_of(timer, struct afs_server, timer);

	afs_see_server(server, afs_server_trace_see_timer);
	if (!test_bit(AFS_SERVER_FL_EXPIRED, &server->flags))
		schedule_work(&server->destroyer);
}

/*
 * Wake up all the servers in a cell so that they can purge themselves.
 */
void afs_purge_servers(struct afs_cell *cell)
{
	struct afs_server *server;
	struct rb_node *rb;

	down_read(&cell->fs_lock);
	for (rb = rb_first(&cell->fs_servers); rb; rb = rb_next(rb)) {
		server = rb_entry(rb, struct afs_server, uuid_rb);
		afs_see_server(server, afs_server_trace_see_purge);
		schedule_work(&server->destroyer);
	}
	up_read(&cell->fs_lock);
}

/*
 * Wait for outstanding servers.
 */
void afs_wait_for_servers(struct afs_net *net)
{
	_enter("");

	atomic_dec(&net->servers_outstanding);
	wait_var_event(&net->servers_outstanding,
		       !atomic_read(&net->servers_outstanding));
	_leave("");
}

/*
 * Get an update for a server's address list.
 */
static noinline bool afs_update_server_record(struct afs_operation *op,
					      struct afs_server *server,
					      struct key *key)
{
	struct afs_endpoint_state *estate;
	struct afs_addr_list *alist;
	bool has_addrs;

	_enter("");

	trace_afs_server(server->debug_id, refcount_read(&server->ref),
			 atomic_read(&server->active),
			 afs_server_trace_update);

	alist = afs_vl_lookup_addrs(server, op->key);
	if (IS_ERR(alist)) {
		rcu_read_lock();
		estate = rcu_dereference(server->endpoint_state);
		has_addrs = estate->addresses;
		rcu_read_unlock();

		if ((PTR_ERR(alist) == -ERESTARTSYS ||
		     PTR_ERR(alist) == -EINTR) &&
		    (op->flags & AFS_OPERATION_UNINTR) &&
		    has_addrs) {
			_leave(" = t [intr]");
			return true;
		}
		afs_op_set_error(op, PTR_ERR(alist));
		_leave(" = f [%d]", afs_op_error(op));
		return false;
	}

	if (server->addr_version != alist->version)
		afs_fs_probe_fileserver(op->net, server, alist, key);

	afs_put_addrlist(alist, afs_alist_trace_put_server_update);
	_leave(" = t");
	return true;
}

/*
 * See if a server's address list needs updating.
 */
bool afs_check_server_record(struct afs_operation *op, struct afs_server *server,
			     struct key *key)
{
	bool success;
	int ret, retries = 0;

	_enter("");

	ASSERT(server);

retry:
	if (test_bit(AFS_SERVER_FL_UPDATING, &server->flags))
		goto wait;
	if (test_bit(AFS_SERVER_FL_NEEDS_UPDATE, &server->flags))
		goto update;
	_leave(" = t [good]");
	return true;

update:
	if (!test_and_set_bit_lock(AFS_SERVER_FL_UPDATING, &server->flags)) {
		clear_bit(AFS_SERVER_FL_NEEDS_UPDATE, &server->flags);
		success = afs_update_server_record(op, server, key);
		clear_bit_unlock(AFS_SERVER_FL_UPDATING, &server->flags);
		wake_up_bit(&server->flags, AFS_SERVER_FL_UPDATING);
		_leave(" = %d", success);
		return success;
	}

wait:
	ret = wait_on_bit(&server->flags, AFS_SERVER_FL_UPDATING,
			  (op->flags & AFS_OPERATION_UNINTR) ?
			  TASK_UNINTERRUPTIBLE : TASK_INTERRUPTIBLE);
	if (ret == -ERESTARTSYS) {
		afs_op_set_error(op, ret);
		_leave(" = f [intr]");
		return false;
	}

	retries++;
	if (retries == 4) {
		_leave(" = f [stale]");
		ret = -ESTALE;
		return false;
	}
	goto retry;
}
