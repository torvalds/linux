/* AFS cell and server record management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/key.h>
#include <linux/ctype.h>
#include <linux/dns_resolver.h>
#include <linux/sched.h>
#include <keys/rxrpc-type.h>
#include "internal.h"

/*
 * allocate a cell record and fill in its name, VL server address list and
 * allocate an anonymous key
 */
static struct afs_cell *afs_cell_alloc(struct afs_net *net,
				       const char *name, unsigned namelen,
				       char *vllist)
{
	struct afs_cell *cell;
	struct key *key;
	char keyname[4 + AFS_MAXCELLNAME + 1], *cp, *dp, *next;
	char  *dvllist = NULL, *_vllist = NULL;
	char  delimiter = ':';
	int ret;

	_enter("%*.*s,%s", namelen, namelen, name ?: "", vllist);

	BUG_ON(!name); /* TODO: want to look up "this cell" in the cache */

	if (namelen > AFS_MAXCELLNAME) {
		_leave(" = -ENAMETOOLONG");
		return ERR_PTR(-ENAMETOOLONG);
	}

	/* allocate and initialise a cell record */
	cell = kzalloc(sizeof(struct afs_cell) + namelen + 1, GFP_KERNEL);
	if (!cell) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	memcpy(cell->name, name, namelen);
	cell->name[namelen] = 0;

	atomic_set(&cell->usage, 1);
	INIT_LIST_HEAD(&cell->link);
	cell->net = net;
	rwlock_init(&cell->servers_lock);
	INIT_LIST_HEAD(&cell->servers);
	init_rwsem(&cell->vl_sem);
	INIT_LIST_HEAD(&cell->vl_list);
	spin_lock_init(&cell->vl_lock);

	/* if the ip address is invalid, try dns query */
	if (!vllist || strlen(vllist) < 7) {
		ret = dns_query("afsdb", name, namelen, "ipv4", &dvllist, NULL);
		if (ret < 0) {
			if (ret == -ENODATA || ret == -EAGAIN || ret == -ENOKEY)
				/* translate these errors into something
				 * userspace might understand */
				ret = -EDESTADDRREQ;
			_leave(" = %d", ret);
			return ERR_PTR(ret);
		}
		_vllist = dvllist;

		/* change the delimiter for user-space reply */
		delimiter = ',';

	} else {
		_vllist = vllist;
	}

	/* fill in the VL server list from the rest of the string */
	do {
		unsigned a, b, c, d;

		next = strchr(_vllist, delimiter);
		if (next)
			*next++ = 0;

		if (sscanf(_vllist, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
			goto bad_address;

		if (a > 255 || b > 255 || c > 255 || d > 255)
			goto bad_address;

		cell->vl_addrs[cell->vl_naddrs++].s_addr =
			htonl((a << 24) | (b << 16) | (c << 8) | d);

	} while (cell->vl_naddrs < AFS_CELL_MAX_ADDRS && (_vllist = next));

	/* create a key to represent an anonymous user */
	memcpy(keyname, "afs@", 4);
	dp = keyname + 4;
	cp = cell->name;
	do {
		*dp++ = toupper(*cp);
	} while (*cp++);

	key = rxrpc_get_null_key(keyname);
	if (IS_ERR(key)) {
		_debug("no key");
		ret = PTR_ERR(key);
		goto error;
	}
	cell->anonymous_key = key;

	_debug("anon key %p{%x}",
	       cell->anonymous_key, key_serial(cell->anonymous_key));

	_leave(" = %p", cell);
	return cell;

bad_address:
	printk(KERN_ERR "kAFS: bad VL server IP address\n");
	ret = -EINVAL;
error:
	key_put(cell->anonymous_key);
	kfree(dvllist);
	kfree(cell);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * afs_cell_crate() - create a cell record
 * @net:	The network namespace
 * @name:	is the name of the cell.
 * @namsesz:	is the strlen of the cell name.
 * @vllist:	is a colon separated list of IP addresses in "a.b.c.d" format.
 * @retref:	is T to return the cell reference when the cell exists.
 */
struct afs_cell *afs_cell_create(struct afs_net *net,
				 const char *name, unsigned namesz,
				 char *vllist, bool retref)
{
	struct afs_cell *cell;
	int ret;

	_enter("%*.*s,%s", namesz, namesz, name ?: "", vllist);

	down_write(&net->cells_sem);
	read_lock(&net->cells_lock);
	list_for_each_entry(cell, &net->cells, link) {
		if (strncasecmp(cell->name, name, namesz) == 0)
			goto duplicate_name;
	}
	read_unlock(&net->cells_lock);

	cell = afs_cell_alloc(net, name, namesz, vllist);
	if (IS_ERR(cell)) {
		_leave(" = %ld", PTR_ERR(cell));
		up_write(&net->cells_sem);
		return cell;
	}

	/* add a proc directory for this cell */
	ret = afs_proc_cell_setup(net, cell);
	if (ret < 0)
		goto error;

#ifdef CONFIG_AFS_FSCACHE
	/* put it up for caching (this never returns an error) */
	cell->cache = fscache_acquire_cookie(afs_cache_netfs.primary_index,
					     &afs_cell_cache_index_def,
					     cell, true);
#endif

	/* add to the cell lists */
	write_lock(&net->cells_lock);
	list_add_tail(&cell->link, &net->cells);
	write_unlock(&net->cells_lock);

	down_write(&net->proc_cells_sem);
	list_add_tail(&cell->proc_link, &net->proc_cells);
	up_write(&net->proc_cells_sem);
	up_write(&net->cells_sem);

	_leave(" = %p", cell);
	return cell;

error:
	up_write(&net->cells_sem);
	key_put(cell->anonymous_key);
	kfree(cell);
	_leave(" = %d", ret);
	return ERR_PTR(ret);

duplicate_name:
	if (retref && !IS_ERR(cell))
		afs_get_cell(cell);

	read_unlock(&net->cells_lock);
	up_write(&net->cells_sem);

	if (retref) {
		_leave(" = %p", cell);
		return cell;
	}

	_leave(" = -EEXIST");
	return ERR_PTR(-EEXIST);
}

/*
 * set the root cell information
 * - can be called with a module parameter string
 * - can be called from a write to /proc/fs/afs/rootcell
 */
int afs_cell_init(struct afs_net *net, char *rootcell)
{
	struct afs_cell *old_root, *new_root;
	char *cp;

	_enter("");

	if (!rootcell) {
		/* module is loaded with no parameters, or built statically.
		 * - in the future we might initialize cell DB here.
		 */
		_leave(" = 0 [no root]");
		return 0;
	}

	cp = strchr(rootcell, ':');
	if (!cp)
		_debug("kAFS: no VL server IP addresses specified");
	else
		*cp++ = 0;

	/* allocate a cell record for the root cell */
	new_root = afs_cell_create(net, rootcell, strlen(rootcell), cp, false);
	if (IS_ERR(new_root)) {
		_leave(" = %ld", PTR_ERR(new_root));
		return PTR_ERR(new_root);
	}

	/* install the new cell */
	write_lock(&net->cells_lock);
	old_root = net->ws_cell;
	net->ws_cell = new_root;
	write_unlock(&net->cells_lock);
	afs_put_cell(net, old_root);

	_leave(" = 0");
	return 0;
}

/*
 * lookup a cell record
 */
struct afs_cell *afs_cell_lookup(struct afs_net *net,
				 const char *name, unsigned namesz,
				 bool dns_cell)
{
	struct afs_cell *cell;

	_enter("\"%*.*s\",", namesz, namesz, name ?: "");

	down_read(&net->cells_sem);
	read_lock(&net->cells_lock);

	if (name) {
		/* if the cell was named, look for it in the cell record list */
		list_for_each_entry(cell, &net->cells, link) {
			if (strncmp(cell->name, name, namesz) == 0) {
				afs_get_cell(cell);
				goto found;
			}
		}
		cell = ERR_PTR(-ENOENT);
		if (dns_cell)
			goto create_cell;
	found:
		;
	} else {
		cell = net->ws_cell;
		if (!cell) {
			/* this should not happen unless user tries to mount
			 * when root cell is not set. Return an impossibly
			 * bizarre errno to alert the user. Things like
			 * ENOENT might be "more appropriate" but they happen
			 * for other reasons.
			 */
			cell = ERR_PTR(-EDESTADDRREQ);
		} else {
			afs_get_cell(cell);
		}

	}

	read_unlock(&net->cells_lock);
	up_read(&net->cells_sem);
	_leave(" = %p", cell);
	return cell;

create_cell:
	read_unlock(&net->cells_lock);
	up_read(&net->cells_sem);

	cell = afs_cell_create(net, name, namesz, NULL, true);

	_leave(" = %p", cell);
	return cell;
}

#if 0
/*
 * try and get a cell record
 */
struct afs_cell *afs_get_cell_maybe(struct afs_cell *cell)
{
	write_lock(&net->cells_lock);

	if (cell && !list_empty(&cell->link))
		afs_get_cell(cell);
	else
		cell = NULL;

	write_unlock(&net->cells_lock);
	return cell;
}
#endif  /*  0  */

/*
 * destroy a cell record
 */
void afs_put_cell(struct afs_net *net, struct afs_cell *cell)
{
	if (!cell)
		return;

	_enter("%p{%d,%s}", cell, atomic_read(&cell->usage), cell->name);

	ASSERTCMP(atomic_read(&cell->usage), >, 0);

	/* to prevent a race, the decrement and the dequeue must be effectively
	 * atomic */
	write_lock(&net->cells_lock);

	if (likely(!atomic_dec_and_test(&cell->usage))) {
		write_unlock(&net->cells_lock);
		_leave("");
		return;
	}

	ASSERT(list_empty(&cell->servers));
	ASSERT(list_empty(&cell->vl_list));

	wake_up(&net->cells_freeable_wq);

	write_unlock(&net->cells_lock);

	_leave(" [unused]");
}

/*
 * destroy a cell record
 * - must be called with the net->cells_sem write-locked
 * - cell->link should have been broken by the caller
 */
static void afs_cell_destroy(struct afs_net *net, struct afs_cell *cell)
{
	_enter("%p{%d,%s}", cell, atomic_read(&cell->usage), cell->name);

	ASSERTCMP(atomic_read(&cell->usage), >=, 0);
	ASSERT(list_empty(&cell->link));

	/* wait for everyone to stop using the cell */
	if (atomic_read(&cell->usage) > 0) {
		DECLARE_WAITQUEUE(myself, current);

		_debug("wait for cell %s", cell->name);
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&net->cells_freeable_wq, &myself);

		while (atomic_read(&cell->usage) > 0) {
			schedule();
			set_current_state(TASK_UNINTERRUPTIBLE);
		}

		remove_wait_queue(&net->cells_freeable_wq, &myself);
		set_current_state(TASK_RUNNING);
	}

	_debug("cell dead");
	ASSERTCMP(atomic_read(&cell->usage), ==, 0);
	ASSERT(list_empty(&cell->servers));
	ASSERT(list_empty(&cell->vl_list));

	afs_proc_cell_remove(net, cell);

	down_write(&net->proc_cells_sem);
	list_del_init(&cell->proc_link);
	up_write(&net->proc_cells_sem);

#ifdef CONFIG_AFS_FSCACHE
	fscache_relinquish_cookie(cell->cache, 0);
#endif
	key_put(cell->anonymous_key);
	kfree(cell);

	_leave(" [destroyed]");
}

/*
 * purge in-memory cell database on module unload or afs_init() failure
 * - the timeout daemon is stopped before calling this
 */
void afs_cell_purge(struct afs_net *net)
{
	struct afs_cell *cell;

	_enter("");

	afs_put_cell(net, net->ws_cell);

	down_write(&net->cells_sem);

	while (!list_empty(&net->cells)) {
		cell = NULL;

		/* remove the next cell from the front of the list */
		write_lock(&net->cells_lock);

		if (!list_empty(&net->cells)) {
			cell = list_entry(net->cells.next,
					  struct afs_cell, link);
			list_del_init(&cell->link);
		}

		write_unlock(&net->cells_lock);

		if (cell) {
			_debug("PURGING CELL %s (%d)",
			       cell->name, atomic_read(&cell->usage));

			/* now the cell should be left with no references */
			afs_cell_destroy(net, cell);
		}
	}

	up_write(&net->cells_sem);
	_leave("");
}
