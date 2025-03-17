// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS cell and server record management
 *
 * Copyright (C) 2002, 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <linux/key.h>
#include <linux/ctype.h>
#include <linux/dns_resolver.h>
#include <linux/sched.h>
#include <linux/inet.h>
#include <linux/namei.h>
#include <keys/rxrpc-type.h>
#include "internal.h"

static unsigned __read_mostly afs_cell_gc_delay = 10;
static unsigned __read_mostly afs_cell_min_ttl = 10 * 60;
static unsigned __read_mostly afs_cell_max_ttl = 24 * 60 * 60;
static atomic_t cell_debug_id;

static void afs_queue_cell_manager(struct afs_net *);
static void afs_manage_cell_work(struct work_struct *);

static void afs_dec_cells_outstanding(struct afs_net *net)
{
	if (atomic_dec_and_test(&net->cells_outstanding))
		wake_up_var(&net->cells_outstanding);
}

/*
 * Set the cell timer to fire after a given delay, assuming it's not already
 * set for an earlier time.
 */
static void afs_set_cell_timer(struct afs_net *net, time64_t delay)
{
	if (net->live) {
		atomic_inc(&net->cells_outstanding);
		if (timer_reduce(&net->cells_timer, jiffies + delay * HZ))
			afs_dec_cells_outstanding(net);
	} else {
		afs_queue_cell_manager(net);
	}
}

/*
 * Look up and get an activation reference on a cell record.  The caller must
 * hold net->cells_lock at least read-locked.
 */
static struct afs_cell *afs_find_cell_locked(struct afs_net *net,
					     const char *name, unsigned int namesz,
					     enum afs_cell_trace reason)
{
	struct afs_cell *cell = NULL;
	struct rb_node *p;
	int n;

	_enter("%*.*s", namesz, namesz, name);

	if (name && namesz == 0)
		return ERR_PTR(-EINVAL);
	if (namesz > AFS_MAXCELLNAME)
		return ERR_PTR(-ENAMETOOLONG);

	if (!name) {
		cell = rcu_dereference_protected(net->ws_cell,
						 lockdep_is_held(&net->cells_lock));
		if (!cell)
			return ERR_PTR(-EDESTADDRREQ);
		goto found;
	}

	p = net->cells.rb_node;
	while (p) {
		cell = rb_entry(p, struct afs_cell, net_node);

		n = strncasecmp(cell->name, name,
				min_t(size_t, cell->name_len, namesz));
		if (n == 0)
			n = cell->name_len - namesz;
		if (n < 0)
			p = p->rb_left;
		else if (n > 0)
			p = p->rb_right;
		else
			goto found;
	}

	return ERR_PTR(-ENOENT);

found:
	return afs_use_cell(cell, reason);
}

/*
 * Look up and get an activation reference on a cell record.
 */
struct afs_cell *afs_find_cell(struct afs_net *net,
			       const char *name, unsigned int namesz,
			       enum afs_cell_trace reason)
{
	struct afs_cell *cell;

	down_read(&net->cells_lock);
	cell = afs_find_cell_locked(net, name, namesz, reason);
	up_read(&net->cells_lock);
	return cell;
}

/*
 * Set up a cell record and fill in its name, VL server address list and
 * allocate an anonymous key
 */
static struct afs_cell *afs_alloc_cell(struct afs_net *net,
				       const char *name, unsigned int namelen,
				       const char *addresses)
{
	struct afs_vlserver_list *vllist;
	struct afs_cell *cell;
	int i, ret;

	ASSERT(name);
	if (namelen == 0)
		return ERR_PTR(-EINVAL);
	if (namelen > AFS_MAXCELLNAME) {
		_leave(" = -ENAMETOOLONG");
		return ERR_PTR(-ENAMETOOLONG);
	}

	/* Prohibit cell names that contain unprintable chars, '/' and '@' or
	 * that begin with a dot.  This also precludes "@cell".
	 */
	if (name[0] == '.')
		return ERR_PTR(-EINVAL);
	for (i = 0; i < namelen; i++) {
		char ch = name[i];
		if (!isprint(ch) || ch == '/' || ch == '@')
			return ERR_PTR(-EINVAL);
	}

	_enter("%*.*s,%s", namelen, namelen, name, addresses);

	cell = kzalloc(sizeof(struct afs_cell), GFP_KERNEL);
	if (!cell) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	cell->name = kmalloc(1 + namelen + 1, GFP_KERNEL);
	if (!cell->name) {
		kfree(cell);
		return ERR_PTR(-ENOMEM);
	}

	cell->name[0] = '.';
	cell->name++;
	cell->name_len = namelen;
	for (i = 0; i < namelen; i++)
		cell->name[i] = tolower(name[i]);
	cell->name[i] = 0;

	cell->net = net;
	refcount_set(&cell->ref, 1);
	atomic_set(&cell->active, 0);
	INIT_WORK(&cell->manager, afs_manage_cell_work);
	init_rwsem(&cell->vs_lock);
	cell->volumes = RB_ROOT;
	INIT_HLIST_HEAD(&cell->proc_volumes);
	seqlock_init(&cell->volume_lock);
	cell->fs_servers = RB_ROOT;
	seqlock_init(&cell->fs_lock);
	rwlock_init(&cell->vl_servers_lock);
	cell->flags = (1 << AFS_CELL_FL_CHECK_ALIAS);

	/* Provide a VL server list, filling it in if we were given a list of
	 * addresses to use.
	 */
	if (addresses) {
		vllist = afs_parse_text_addrs(net,
					      addresses, strlen(addresses), ':',
					      VL_SERVICE, AFS_VL_PORT);
		if (IS_ERR(vllist)) {
			ret = PTR_ERR(vllist);
			goto parse_failed;
		}

		vllist->source = DNS_RECORD_FROM_CONFIG;
		vllist->status = DNS_LOOKUP_NOT_DONE;
		cell->dns_expiry = TIME64_MAX;
	} else {
		ret = -ENOMEM;
		vllist = afs_alloc_vlserver_list(0);
		if (!vllist)
			goto error;
		vllist->source = DNS_RECORD_UNAVAILABLE;
		vllist->status = DNS_LOOKUP_NOT_DONE;
		cell->dns_expiry = ktime_get_real_seconds();
	}

	rcu_assign_pointer(cell->vl_servers, vllist);

	cell->dns_source = vllist->source;
	cell->dns_status = vllist->status;
	smp_store_release(&cell->dns_lookup_count, 1); /* vs source/status */
	atomic_inc(&net->cells_outstanding);
	cell->debug_id = atomic_inc_return(&cell_debug_id);
	trace_afs_cell(cell->debug_id, 1, 0, afs_cell_trace_alloc);

	_leave(" = %p", cell);
	return cell;

parse_failed:
	if (ret == -EINVAL)
		printk(KERN_ERR "kAFS: bad VL server IP address\n");
error:
	kfree(cell->name - 1);
	kfree(cell);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * afs_lookup_cell - Look up or create a cell record.
 * @net:	The network namespace
 * @name:	The name of the cell.
 * @namesz:	The strlen of the cell name.
 * @vllist:	A colon/comma separated list of numeric IP addresses or NULL.
 * @excl:	T if an error should be given if the cell name already exists.
 *
 * Look up a cell record by name and query the DNS for VL server addresses if
 * needed.  Note that that actual DNS query is punted off to the manager thread
 * so that this function can return immediately if interrupted whilst allowing
 * cell records to be shared even if not yet fully constructed.
 */
struct afs_cell *afs_lookup_cell(struct afs_net *net,
				 const char *name, unsigned int namesz,
				 const char *vllist, bool excl)
{
	struct afs_cell *cell, *candidate, *cursor;
	struct rb_node *parent, **pp;
	enum afs_cell_state state;
	int ret, n;

	_enter("%s,%s", name, vllist);

	if (!excl) {
		cell = afs_find_cell(net, name, namesz, afs_cell_trace_use_lookup);
		if (!IS_ERR(cell))
			goto wait_for_cell;
	}

	/* Assume we're probably going to create a cell and preallocate and
	 * mostly set up a candidate record.  We can then use this to stash the
	 * name, the net namespace and VL server addresses.
	 *
	 * We also want to do this before we hold any locks as it may involve
	 * upcalling to userspace to make DNS queries.
	 */
	candidate = afs_alloc_cell(net, name, namesz, vllist);
	if (IS_ERR(candidate)) {
		_leave(" = %ld", PTR_ERR(candidate));
		return candidate;
	}

	/* Find the insertion point and check to see if someone else added a
	 * cell whilst we were allocating.
	 */
	down_write(&net->cells_lock);

	pp = &net->cells.rb_node;
	parent = NULL;
	while (*pp) {
		parent = *pp;
		cursor = rb_entry(parent, struct afs_cell, net_node);

		n = strncasecmp(cursor->name, name,
				min_t(size_t, cursor->name_len, namesz));
		if (n == 0)
			n = cursor->name_len - namesz;
		if (n < 0)
			pp = &(*pp)->rb_left;
		else if (n > 0)
			pp = &(*pp)->rb_right;
		else
			goto cell_already_exists;
	}

	cell = candidate;
	candidate = NULL;
	atomic_set(&cell->active, 2);
	trace_afs_cell(cell->debug_id, refcount_read(&cell->ref), 2, afs_cell_trace_insert);
	rb_link_node_rcu(&cell->net_node, parent, pp);
	rb_insert_color(&cell->net_node, &net->cells);
	up_write(&net->cells_lock);

	afs_queue_cell(cell, afs_cell_trace_get_queue_new);

wait_for_cell:
	trace_afs_cell(cell->debug_id, refcount_read(&cell->ref), atomic_read(&cell->active),
		       afs_cell_trace_wait);
	_debug("wait_for_cell");
	wait_var_event(&cell->state,
		       ({
			       state = smp_load_acquire(&cell->state); /* vs error */
			       state == AFS_CELL_ACTIVE || state == AFS_CELL_REMOVED;
		       }));

	/* Check the state obtained from the wait check. */
	if (state == AFS_CELL_REMOVED) {
		ret = cell->error;
		goto error;
	}

	_leave(" = %p [cell]", cell);
	return cell;

cell_already_exists:
	_debug("cell exists");
	cell = cursor;
	if (excl) {
		ret = -EEXIST;
	} else {
		afs_use_cell(cursor, afs_cell_trace_use_lookup);
		ret = 0;
	}
	up_write(&net->cells_lock);
	if (candidate)
		afs_put_cell(candidate, afs_cell_trace_put_candidate);
	if (ret == 0)
		goto wait_for_cell;
	goto error_noput;
error:
	afs_unuse_cell(net, cell, afs_cell_trace_unuse_lookup);
error_noput:
	_leave(" = %d [error]", ret);
	return ERR_PTR(ret);
}

/*
 * set the root cell information
 * - can be called with a module parameter string
 * - can be called from a write to /proc/fs/afs/rootcell
 */
int afs_cell_init(struct afs_net *net, const char *rootcell)
{
	struct afs_cell *old_root, *new_root;
	const char *cp, *vllist;
	size_t len;

	_enter("");

	if (!rootcell) {
		/* module is loaded with no parameters, or built statically.
		 * - in the future we might initialize cell DB here.
		 */
		_leave(" = 0 [no root]");
		return 0;
	}

	cp = strchr(rootcell, ':');
	if (!cp) {
		_debug("kAFS: no VL server IP addresses specified");
		vllist = NULL;
		len = strlen(rootcell);
	} else {
		vllist = cp + 1;
		len = cp - rootcell;
	}

	if (len == 0 || !rootcell[0] || rootcell[0] == '.' || rootcell[len - 1] == '.')
		return -EINVAL;
	if (memchr(rootcell, '/', len))
		return -EINVAL;
	cp = strstr(rootcell, "..");
	if (cp && cp < rootcell + len)
		return -EINVAL;

	/* allocate a cell record for the root cell */
	new_root = afs_lookup_cell(net, rootcell, len, vllist, false);
	if (IS_ERR(new_root)) {
		_leave(" = %ld", PTR_ERR(new_root));
		return PTR_ERR(new_root);
	}

	if (!test_and_set_bit(AFS_CELL_FL_NO_GC, &new_root->flags))
		afs_use_cell(new_root, afs_cell_trace_use_pin);

	/* install the new cell */
	down_write(&net->cells_lock);
	afs_see_cell(new_root, afs_cell_trace_see_ws);
	old_root = rcu_replace_pointer(net->ws_cell, new_root,
				       lockdep_is_held(&net->cells_lock));
	up_write(&net->cells_lock);

	afs_unuse_cell(net, old_root, afs_cell_trace_unuse_ws);
	_leave(" = 0");
	return 0;
}

/*
 * Update a cell's VL server address list from the DNS.
 */
static int afs_update_cell(struct afs_cell *cell)
{
	struct afs_vlserver_list *vllist, *old = NULL, *p;
	unsigned int min_ttl = READ_ONCE(afs_cell_min_ttl);
	unsigned int max_ttl = READ_ONCE(afs_cell_max_ttl);
	time64_t now, expiry = 0;
	int ret = 0;

	_enter("%s", cell->name);

	vllist = afs_dns_query(cell, &expiry);
	if (IS_ERR(vllist)) {
		ret = PTR_ERR(vllist);

		_debug("%s: fail %d", cell->name, ret);
		if (ret == -ENOMEM)
			goto out_wake;

		vllist = afs_alloc_vlserver_list(0);
		if (!vllist) {
			if (ret >= 0)
				ret = -ENOMEM;
			goto out_wake;
		}

		switch (ret) {
		case -ENODATA:
		case -EDESTADDRREQ:
			vllist->status = DNS_LOOKUP_GOT_NOT_FOUND;
			break;
		case -EAGAIN:
		case -ECONNREFUSED:
			vllist->status = DNS_LOOKUP_GOT_TEMP_FAILURE;
			break;
		default:
			vllist->status = DNS_LOOKUP_GOT_LOCAL_FAILURE;
			break;
		}
	}

	_debug("%s: got list %d %d", cell->name, vllist->source, vllist->status);
	cell->dns_status = vllist->status;

	now = ktime_get_real_seconds();
	if (min_ttl > max_ttl)
		max_ttl = min_ttl;
	if (expiry < now + min_ttl)
		expiry = now + min_ttl;
	else if (expiry > now + max_ttl)
		expiry = now + max_ttl;

	_debug("%s: status %d", cell->name, vllist->status);
	if (vllist->source == DNS_RECORD_UNAVAILABLE) {
		switch (vllist->status) {
		case DNS_LOOKUP_GOT_NOT_FOUND:
			/* The DNS said that the cell does not exist or there
			 * weren't any addresses to be had.
			 */
			cell->dns_expiry = expiry;
			break;

		case DNS_LOOKUP_BAD:
		case DNS_LOOKUP_GOT_LOCAL_FAILURE:
		case DNS_LOOKUP_GOT_TEMP_FAILURE:
		case DNS_LOOKUP_GOT_NS_FAILURE:
		default:
			cell->dns_expiry = now + 10;
			break;
		}
	} else {
		cell->dns_expiry = expiry;
	}

	/* Replace the VL server list if the new record has servers or the old
	 * record doesn't.
	 */
	write_lock(&cell->vl_servers_lock);
	p = rcu_dereference_protected(cell->vl_servers, true);
	if (vllist->nr_servers > 0 || p->nr_servers == 0) {
		rcu_assign_pointer(cell->vl_servers, vllist);
		cell->dns_source = vllist->source;
		old = p;
	}
	write_unlock(&cell->vl_servers_lock);
	afs_put_vlserverlist(cell->net, old);

out_wake:
	smp_store_release(&cell->dns_lookup_count,
			  cell->dns_lookup_count + 1); /* vs source/status */
	wake_up_var(&cell->dns_lookup_count);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Destroy a cell record
 */
static void afs_cell_destroy(struct rcu_head *rcu)
{
	struct afs_cell *cell = container_of(rcu, struct afs_cell, rcu);
	struct afs_net *net = cell->net;
	int r;

	_enter("%p{%s}", cell, cell->name);

	r = refcount_read(&cell->ref);
	ASSERTCMP(r, ==, 0);
	trace_afs_cell(cell->debug_id, r, atomic_read(&cell->active), afs_cell_trace_free);

	afs_put_vlserverlist(net, rcu_access_pointer(cell->vl_servers));
	afs_unuse_cell(net, cell->alias_of, afs_cell_trace_unuse_alias);
	key_put(cell->anonymous_key);
	kfree(cell->name - 1);
	kfree(cell);

	afs_dec_cells_outstanding(net);
	_leave(" [destroyed]");
}

/*
 * Queue the cell manager.
 */
static void afs_queue_cell_manager(struct afs_net *net)
{
	int outstanding = atomic_inc_return(&net->cells_outstanding);

	_enter("%d", outstanding);

	if (!queue_work(afs_wq, &net->cells_manager))
		afs_dec_cells_outstanding(net);
}

/*
 * Cell management timer.  We have an increment on cells_outstanding that we
 * need to pass along to the work item.
 */
void afs_cells_timer(struct timer_list *timer)
{
	struct afs_net *net = container_of(timer, struct afs_net, cells_timer);

	_enter("");
	if (!queue_work(afs_wq, &net->cells_manager))
		afs_dec_cells_outstanding(net);
}

/*
 * Get a reference on a cell record.
 */
struct afs_cell *afs_get_cell(struct afs_cell *cell, enum afs_cell_trace reason)
{
	int r;

	__refcount_inc(&cell->ref, &r);
	trace_afs_cell(cell->debug_id, r + 1, atomic_read(&cell->active), reason);
	return cell;
}

/*
 * Drop a reference on a cell record.
 */
void afs_put_cell(struct afs_cell *cell, enum afs_cell_trace reason)
{
	if (cell) {
		unsigned int debug_id = cell->debug_id;
		unsigned int a;
		bool zero;
		int r;

		a = atomic_read(&cell->active);
		zero = __refcount_dec_and_test(&cell->ref, &r);
		trace_afs_cell(debug_id, r - 1, a, reason);
		if (zero) {
			a = atomic_read(&cell->active);
			WARN(a != 0, "Cell active count %u > 0\n", a);
			call_rcu(&cell->rcu, afs_cell_destroy);
		}
	}
}

/*
 * Note a cell becoming more active.
 */
struct afs_cell *afs_use_cell(struct afs_cell *cell, enum afs_cell_trace reason)
{
	int r, a;

	r = refcount_read(&cell->ref);
	WARN_ON(r == 0);
	a = atomic_inc_return(&cell->active);
	trace_afs_cell(cell->debug_id, r, a, reason);
	return cell;
}

/*
 * Record a cell becoming less active.  When the active counter reaches 1, it
 * is scheduled for destruction, but may get reactivated.
 */
void afs_unuse_cell(struct afs_net *net, struct afs_cell *cell, enum afs_cell_trace reason)
{
	unsigned int debug_id;
	time64_t now, expire_delay;
	int r, a;

	if (!cell)
		return;

	_enter("%s", cell->name);

	now = ktime_get_real_seconds();
	cell->last_inactive = now;
	expire_delay = 0;
	if (cell->vl_servers->nr_servers)
		expire_delay = afs_cell_gc_delay;

	debug_id = cell->debug_id;
	r = refcount_read(&cell->ref);
	a = atomic_dec_return(&cell->active);
	trace_afs_cell(debug_id, r, a, reason);
	WARN_ON(a == 0);
	if (a == 1)
		/* 'cell' may now be garbage collected. */
		afs_set_cell_timer(net, expire_delay);
}

/*
 * Note that a cell has been seen.
 */
void afs_see_cell(struct afs_cell *cell, enum afs_cell_trace reason)
{
	int r, a;

	r = refcount_read(&cell->ref);
	a = atomic_read(&cell->active);
	trace_afs_cell(cell->debug_id, r, a, reason);
}

/*
 * Queue a cell for management, giving the workqueue a ref to hold.
 */
void afs_queue_cell(struct afs_cell *cell, enum afs_cell_trace reason)
{
	afs_get_cell(cell, reason);
	if (!queue_work(afs_wq, &cell->manager))
		afs_put_cell(cell, afs_cell_trace_put_queue_fail);
}

/*
 * Allocate a key to use as a placeholder for anonymous user security.
 */
static int afs_alloc_anon_key(struct afs_cell *cell)
{
	struct key *key;
	char keyname[4 + AFS_MAXCELLNAME + 1], *cp, *dp;

	/* Create a key to represent an anonymous user. */
	memcpy(keyname, "afs@", 4);
	dp = keyname + 4;
	cp = cell->name;
	do {
		*dp++ = tolower(*cp);
	} while (*cp++);

	key = rxrpc_get_null_key(keyname);
	if (IS_ERR(key))
		return PTR_ERR(key);

	cell->anonymous_key = key;

	_debug("anon key %p{%x}",
	       cell->anonymous_key, key_serial(cell->anonymous_key));
	return 0;
}

/*
 * Activate a cell.
 */
static int afs_activate_cell(struct afs_net *net, struct afs_cell *cell)
{
	struct hlist_node **p;
	struct afs_cell *pcell;
	int ret;

	if (!cell->anonymous_key) {
		ret = afs_alloc_anon_key(cell);
		if (ret < 0)
			return ret;
	}

	ret = afs_proc_cell_setup(cell);
	if (ret < 0)
		return ret;

	mutex_lock(&net->proc_cells_lock);
	for (p = &net->proc_cells.first; *p; p = &(*p)->next) {
		pcell = hlist_entry(*p, struct afs_cell, proc_link);
		if (strcmp(cell->name, pcell->name) < 0)
			break;
	}

	cell->proc_link.pprev = p;
	cell->proc_link.next = *p;
	rcu_assign_pointer(*p, &cell->proc_link.next);
	if (cell->proc_link.next)
		cell->proc_link.next->pprev = &cell->proc_link.next;

	afs_dynroot_mkdir(net, cell);
	mutex_unlock(&net->proc_cells_lock);
	return 0;
}

/*
 * Deactivate a cell.
 */
static void afs_deactivate_cell(struct afs_net *net, struct afs_cell *cell)
{
	_enter("%s", cell->name);

	afs_proc_cell_remove(cell);

	mutex_lock(&net->proc_cells_lock);
	if (!hlist_unhashed(&cell->proc_link))
		hlist_del_rcu(&cell->proc_link);
	afs_dynroot_rmdir(net, cell);
	mutex_unlock(&net->proc_cells_lock);

	_leave("");
}

/*
 * Manage a cell record, initialising and destroying it, maintaining its DNS
 * records.
 */
static void afs_manage_cell(struct afs_cell *cell)
{
	struct afs_net *net = cell->net;
	int ret, active;

	_enter("%s", cell->name);

again:
	_debug("state %u", cell->state);
	switch (cell->state) {
	case AFS_CELL_INACTIVE:
	case AFS_CELL_FAILED:
		down_write(&net->cells_lock);
		active = 1;
		if (atomic_try_cmpxchg_relaxed(&cell->active, &active, 0)) {
			rb_erase(&cell->net_node, &net->cells);
			trace_afs_cell(cell->debug_id, refcount_read(&cell->ref), 0,
				       afs_cell_trace_unuse_delete);
			smp_store_release(&cell->state, AFS_CELL_REMOVED);
		}
		up_write(&net->cells_lock);
		if (cell->state == AFS_CELL_REMOVED) {
			wake_up_var(&cell->state);
			goto final_destruction;
		}
		if (cell->state == AFS_CELL_FAILED)
			goto done;
		smp_store_release(&cell->state, AFS_CELL_UNSET);
		wake_up_var(&cell->state);
		goto again;

	case AFS_CELL_UNSET:
		smp_store_release(&cell->state, AFS_CELL_ACTIVATING);
		wake_up_var(&cell->state);
		goto again;

	case AFS_CELL_ACTIVATING:
		ret = afs_activate_cell(net, cell);
		if (ret < 0)
			goto activation_failed;

		smp_store_release(&cell->state, AFS_CELL_ACTIVE);
		wake_up_var(&cell->state);
		goto again;

	case AFS_CELL_ACTIVE:
		if (atomic_read(&cell->active) > 1) {
			if (test_and_clear_bit(AFS_CELL_FL_DO_LOOKUP, &cell->flags)) {
				ret = afs_update_cell(cell);
				if (ret < 0)
					cell->error = ret;
			}
			goto done;
		}
		smp_store_release(&cell->state, AFS_CELL_DEACTIVATING);
		wake_up_var(&cell->state);
		goto again;

	case AFS_CELL_DEACTIVATING:
		if (atomic_read(&cell->active) > 1)
			goto reverse_deactivation;
		afs_deactivate_cell(net, cell);
		smp_store_release(&cell->state, AFS_CELL_INACTIVE);
		wake_up_var(&cell->state);
		goto again;

	case AFS_CELL_REMOVED:
		goto done;

	default:
		break;
	}
	_debug("bad state %u", cell->state);
	BUG(); /* Unhandled state */

activation_failed:
	cell->error = ret;
	afs_deactivate_cell(net, cell);

	smp_store_release(&cell->state, AFS_CELL_FAILED); /* vs error */
	wake_up_var(&cell->state);
	goto again;

reverse_deactivation:
	smp_store_release(&cell->state, AFS_CELL_ACTIVE);
	wake_up_var(&cell->state);
	_leave(" [deact->act]");
	return;

done:
	_leave(" [done %u]", cell->state);
	return;

final_destruction:
	/* The root volume is pinning the cell */
	afs_put_volume(cell->root_volume, afs_volume_trace_put_cell_root);
	cell->root_volume = NULL;
	afs_put_cell(cell, afs_cell_trace_put_destroy);
}

static void afs_manage_cell_work(struct work_struct *work)
{
	struct afs_cell *cell = container_of(work, struct afs_cell, manager);

	afs_manage_cell(cell);
	afs_put_cell(cell, afs_cell_trace_put_queue_work);
}

/*
 * Manage the records of cells known to a network namespace.  This includes
 * updating the DNS records and garbage collecting unused cells that were
 * automatically added.
 *
 * Note that constructed cell records may only be removed from net->cells by
 * this work item, so it is safe for this work item to stash a cursor pointing
 * into the tree and then return to caller (provided it skips cells that are
 * still under construction).
 *
 * Note also that we were given an increment on net->cells_outstanding by
 * whoever queued us that we need to deal with before returning.
 */
void afs_manage_cells(struct work_struct *work)
{
	struct afs_net *net = container_of(work, struct afs_net, cells_manager);
	struct rb_node *cursor;
	time64_t now = ktime_get_real_seconds(), next_manage = TIME64_MAX;
	bool purging = !net->live;

	_enter("");

	/* Trawl the cell database looking for cells that have expired from
	 * lack of use and cells whose DNS results have expired and dispatch
	 * their managers.
	 */
	down_read(&net->cells_lock);

	for (cursor = rb_first(&net->cells); cursor; cursor = rb_next(cursor)) {
		struct afs_cell *cell =
			rb_entry(cursor, struct afs_cell, net_node);
		unsigned active;
		bool sched_cell = false;

		active = atomic_read(&cell->active);
		trace_afs_cell(cell->debug_id, refcount_read(&cell->ref),
			       active, afs_cell_trace_manage);

		ASSERTCMP(active, >=, 1);

		if (purging) {
			if (test_and_clear_bit(AFS_CELL_FL_NO_GC, &cell->flags)) {
				active = atomic_dec_return(&cell->active);
				trace_afs_cell(cell->debug_id, refcount_read(&cell->ref),
					       active, afs_cell_trace_unuse_pin);
			}
		}

		if (active == 1) {
			struct afs_vlserver_list *vllist;
			time64_t expire_at = cell->last_inactive;

			read_lock(&cell->vl_servers_lock);
			vllist = rcu_dereference_protected(
				cell->vl_servers,
				lockdep_is_held(&cell->vl_servers_lock));
			if (vllist->nr_servers > 0)
				expire_at += afs_cell_gc_delay;
			read_unlock(&cell->vl_servers_lock);
			if (purging || expire_at <= now)
				sched_cell = true;
			else if (expire_at < next_manage)
				next_manage = expire_at;
		}

		if (!purging) {
			if (test_bit(AFS_CELL_FL_DO_LOOKUP, &cell->flags))
				sched_cell = true;
		}

		if (sched_cell)
			afs_queue_cell(cell, afs_cell_trace_get_queue_manage);
	}

	up_read(&net->cells_lock);

	/* Update the timer on the way out.  We have to pass an increment on
	 * cells_outstanding in the namespace that we are in to the timer or
	 * the work scheduler.
	 */
	if (!purging && next_manage < TIME64_MAX) {
		now = ktime_get_real_seconds();

		if (next_manage - now <= 0) {
			if (queue_work(afs_wq, &net->cells_manager))
				atomic_inc(&net->cells_outstanding);
		} else {
			afs_set_cell_timer(net, next_manage - now);
		}
	}

	afs_dec_cells_outstanding(net);
	_leave(" [%d]", atomic_read(&net->cells_outstanding));
}

/*
 * Purge in-memory cell database.
 */
void afs_cell_purge(struct afs_net *net)
{
	struct afs_cell *ws;

	_enter("");

	down_write(&net->cells_lock);
	ws = rcu_replace_pointer(net->ws_cell, NULL,
				 lockdep_is_held(&net->cells_lock));
	up_write(&net->cells_lock);
	afs_unuse_cell(net, ws, afs_cell_trace_unuse_ws);

	_debug("del timer");
	if (del_timer_sync(&net->cells_timer))
		atomic_dec(&net->cells_outstanding);

	_debug("kick mgr");
	afs_queue_cell_manager(net);

	_debug("wait");
	wait_var_event(&net->cells_outstanding,
		       !atomic_read(&net->cells_outstanding));
	_leave("");
}
