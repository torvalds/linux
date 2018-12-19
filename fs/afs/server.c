/* AFS server record management
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include "internal.h"

static unsigned afs_server_timeout = 10;	/* server timeout in seconds */

static void afs_reap_server(struct work_struct *);

/* tree of all the servers, indexed by IP address */
static struct rb_root afs_servers = RB_ROOT;
static DEFINE_RWLOCK(afs_servers_lock);

/* LRU list of all the servers not currently in use */
static LIST_HEAD(afs_server_graveyard);
static DEFINE_SPINLOCK(afs_server_graveyard_lock);
static DECLARE_DELAYED_WORK(afs_server_reaper, afs_reap_server);

/*
 * install a server record in the master tree
 */
static int afs_install_server(struct afs_server *server)
{
	struct afs_server *xserver;
	struct rb_node **pp, *p;
	int ret;

	_enter("%p", server);

	write_lock(&afs_servers_lock);

	ret = -EEXIST;
	pp = &afs_servers.rb_node;
	p = NULL;
	while (*pp) {
		p = *pp;
		_debug("- consider %p", p);
		xserver = rb_entry(p, struct afs_server, master_rb);
		if (server->addr.s_addr < xserver->addr.s_addr)
			pp = &(*pp)->rb_left;
		else if (server->addr.s_addr > xserver->addr.s_addr)
			pp = &(*pp)->rb_right;
		else
			goto error;
	}

	rb_link_node(&server->master_rb, p, pp);
	rb_insert_color(&server->master_rb, &afs_servers);
	ret = 0;

error:
	write_unlock(&afs_servers_lock);
	return ret;
}

/*
 * allocate a new server record
 */
static struct afs_server *afs_alloc_server(struct afs_cell *cell,
					   const struct in_addr *addr)
{
	struct afs_server *server;

	_enter("");

	server = kzalloc(sizeof(struct afs_server), GFP_KERNEL);
	if (server) {
		atomic_set(&server->usage, 1);
		server->cell = cell;

		INIT_LIST_HEAD(&server->link);
		INIT_LIST_HEAD(&server->grave);
		init_rwsem(&server->sem);
		spin_lock_init(&server->fs_lock);
		server->fs_vnodes = RB_ROOT;
		server->cb_promises = RB_ROOT;
		spin_lock_init(&server->cb_lock);
		init_waitqueue_head(&server->cb_break_waitq);
		INIT_DELAYED_WORK(&server->cb_break_work,
				  afs_dispatch_give_up_callbacks);

		memcpy(&server->addr, addr, sizeof(struct in_addr));
		server->addr.s_addr = addr->s_addr;
		_leave(" = %p{%d}", server, atomic_read(&server->usage));
	} else {
		_leave(" = NULL [nomem]");
	}
	return server;
}

/*
 * get an FS-server record for a cell
 */
struct afs_server *afs_lookup_server(struct afs_cell *cell,
				     const struct in_addr *addr)
{
	struct afs_server *server, *candidate;

	_enter("%p,%pI4", cell, &addr->s_addr);

	/* quick scan of the list to see if we already have the server */
	read_lock(&cell->servers_lock);

	list_for_each_entry(server, &cell->servers, link) {
		if (server->addr.s_addr == addr->s_addr)
			goto found_server_quickly;
	}
	read_unlock(&cell->servers_lock);

	candidate = afs_alloc_server(cell, addr);
	if (!candidate) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	write_lock(&cell->servers_lock);

	/* check the cell's server list again */
	list_for_each_entry(server, &cell->servers, link) {
		if (server->addr.s_addr == addr->s_addr)
			goto found_server;
	}

	_debug("new");
	server = candidate;
	if (afs_install_server(server) < 0)
		goto server_in_two_cells;

	afs_get_cell(cell);
	list_add_tail(&server->link, &cell->servers);

	write_unlock(&cell->servers_lock);
	_leave(" = %p{%d}", server, atomic_read(&server->usage));
	return server;

	/* found a matching server quickly */
found_server_quickly:
	_debug("found quickly");
	afs_get_server(server);
	read_unlock(&cell->servers_lock);
no_longer_unused:
	if (!list_empty(&server->grave)) {
		spin_lock(&afs_server_graveyard_lock);
		list_del_init(&server->grave);
		spin_unlock(&afs_server_graveyard_lock);
	}
	_leave(" = %p{%d}", server, atomic_read(&server->usage));
	return server;

	/* found a matching server on the second pass */
found_server:
	_debug("found");
	afs_get_server(server);
	write_unlock(&cell->servers_lock);
	kfree(candidate);
	goto no_longer_unused;

	/* found a server that seems to be in two cells */
server_in_two_cells:
	write_unlock(&cell->servers_lock);
	kfree(candidate);
	printk(KERN_NOTICE "kAFS: Server %pI4 appears to be in two cells\n",
	       addr);
	_leave(" = -EEXIST");
	return ERR_PTR(-EEXIST);
}

/*
 * look up a server by its IP address
 */
struct afs_server *afs_find_server(const struct in_addr *_addr)
{
	struct afs_server *server = NULL;
	struct rb_node *p;
	struct in_addr addr = *_addr;

	_enter("%pI4", &addr.s_addr);

	read_lock(&afs_servers_lock);

	p = afs_servers.rb_node;
	while (p) {
		server = rb_entry(p, struct afs_server, master_rb);

		_debug("- consider %p", p);

		if (addr.s_addr < server->addr.s_addr) {
			p = p->rb_left;
		} else if (addr.s_addr > server->addr.s_addr) {
			p = p->rb_right;
		} else {
			afs_get_server(server);
			goto found;
		}
	}

	server = NULL;
found:
	read_unlock(&afs_servers_lock);
	ASSERTIFCMP(server, server->addr.s_addr, ==, addr.s_addr);
	_leave(" = %p", server);
	return server;
}

/*
 * destroy a server record
 * - removes from the cell list
 */
void afs_put_server(struct afs_server *server)
{
	if (!server)
		return;

	_enter("%p{%d}", server, atomic_read(&server->usage));

	_debug("PUT SERVER %d", atomic_read(&server->usage));

	ASSERTCMP(atomic_read(&server->usage), >, 0);

	if (likely(!atomic_dec_and_test(&server->usage))) {
		_leave("");
		return;
	}

	afs_flush_callback_breaks(server);

	spin_lock(&afs_server_graveyard_lock);
	if (atomic_read(&server->usage) == 0) {
		list_move_tail(&server->grave, &afs_server_graveyard);
		server->time_of_death = ktime_get_real_seconds();
		queue_delayed_work(afs_wq, &afs_server_reaper,
				   afs_server_timeout * HZ);
	}
	spin_unlock(&afs_server_graveyard_lock);
	_leave(" [dead]");
}

/*
 * destroy a dead server
 */
static void afs_destroy_server(struct afs_server *server)
{
	_enter("%p", server);

	ASSERTIF(server->cb_break_head != server->cb_break_tail,
		 delayed_work_pending(&server->cb_break_work));

	ASSERTCMP(server->fs_vnodes.rb_node, ==, NULL);
	ASSERTCMP(server->cb_promises.rb_node, ==, NULL);
	ASSERTCMP(server->cb_break_head, ==, server->cb_break_tail);
	ASSERTCMP(atomic_read(&server->cb_break_n), ==, 0);

	afs_put_cell(server->cell);
	kfree(server);
}

/*
 * reap dead server records
 */
static void afs_reap_server(struct work_struct *work)
{
	LIST_HEAD(corpses);
	struct afs_server *server;
	unsigned long delay, expiry;
	time64_t now;

	now = ktime_get_real_seconds();
	spin_lock(&afs_server_graveyard_lock);

	while (!list_empty(&afs_server_graveyard)) {
		server = list_entry(afs_server_graveyard.next,
				    struct afs_server, grave);

		/* the queue is ordered most dead first */
		expiry = server->time_of_death + afs_server_timeout;
		if (expiry > now) {
			delay = (expiry - now) * HZ;
			mod_delayed_work(afs_wq, &afs_server_reaper, delay);
			break;
		}

		write_lock(&server->cell->servers_lock);
		write_lock(&afs_servers_lock);
		if (atomic_read(&server->usage) > 0) {
			list_del_init(&server->grave);
		} else {
			list_move_tail(&server->grave, &corpses);
			list_del_init(&server->link);
			rb_erase(&server->master_rb, &afs_servers);
		}
		write_unlock(&afs_servers_lock);
		write_unlock(&server->cell->servers_lock);
	}

	spin_unlock(&afs_server_graveyard_lock);

	/* now reap the corpses we've extracted */
	while (!list_empty(&corpses)) {
		server = list_entry(corpses.next, struct afs_server, grave);
		list_del(&server->grave);
		afs_destroy_server(server);
	}
}

/*
 * discard all the server records for rmmod
 */
void __exit afs_purge_servers(void)
{
	afs_server_timeout = 0;
	mod_delayed_work(afs_wq, &afs_server_reaper, 0);
}
