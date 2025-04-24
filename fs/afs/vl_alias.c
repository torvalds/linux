// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS cell alias detection
 *
 * Copyright (C) 2020 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/namei.h>
#include <keys/rxrpc-type.h>
#include "internal.h"

/*
 * Sample a volume.
 */
static struct afs_volume *afs_sample_volume(struct afs_cell *cell, struct key *key,
					    const char *name, unsigned int namelen)
{
	struct afs_volume *volume;
	struct afs_fs_context fc = {
		.type		= 0, /* Explicitly leave it to the VLDB */
		.volnamesz	= namelen,
		.volname	= name,
		.net		= cell->net,
		.cell		= cell,
		.key		= key, /* This might need to be something */
	};

	volume = afs_create_volume(&fc);
	_leave(" = %p", volume);
	return volume;
}

/*
 * Compare the address lists of a pair of fileservers.
 */
static int afs_compare_fs_alists(const struct afs_server *server_a,
				 const struct afs_server *server_b)
{
	const struct afs_addr_list *la, *lb;
	int a = 0, b = 0, addr_matches = 0;

	la = rcu_dereference(server_a->endpoint_state)->addresses;
	lb = rcu_dereference(server_b->endpoint_state)->addresses;

	while (a < la->nr_addrs && b < lb->nr_addrs) {
		unsigned long pa = (unsigned long)la->addrs[a].peer;
		unsigned long pb = (unsigned long)lb->addrs[b].peer;
		long diff = pa - pb;

		if (diff < 0) {
			a++;
		} else if (diff > 0) {
			b++;
		} else {
			addr_matches++;
			a++;
			b++;
		}
	}

	return addr_matches;
}

/*
 * Compare the fileserver lists of two volumes.  The server lists are sorted in
 * order of ascending UUID.
 */
static int afs_compare_volume_slists(const struct afs_volume *vol_a,
				     const struct afs_volume *vol_b)
{
	const struct afs_server_list *la, *lb;
	int i, a = 0, b = 0, uuid_matches = 0, addr_matches = 0;

	la = rcu_dereference(vol_a->servers);
	lb = rcu_dereference(vol_b->servers);

	for (i = 0; i < AFS_MAXTYPES; i++)
		if (vol_a->vids[i] != vol_b->vids[i])
			return 0;

	while (a < la->nr_servers && b < lb->nr_servers) {
		const struct afs_server *server_a = la->servers[a].server;
		const struct afs_server *server_b = lb->servers[b].server;
		int diff = memcmp(&server_a->uuid, &server_b->uuid, sizeof(uuid_t));

		if (diff < 0) {
			a++;
		} else if (diff > 0) {
			b++;
		} else {
			uuid_matches++;
			addr_matches += afs_compare_fs_alists(server_a, server_b);
			a++;
			b++;
		}
	}

	_leave(" = %d [um %d]", addr_matches, uuid_matches);
	return addr_matches;
}

/*
 * Compare root.cell volumes.
 */
static int afs_compare_cell_roots(struct afs_cell *cell)
{
	struct afs_cell *p;

	_enter("");

	rcu_read_lock();

	hlist_for_each_entry_rcu(p, &cell->net->proc_cells, proc_link) {
		if (p == cell || p->alias_of)
			continue;
		if (!p->root_volume)
			continue; /* Ignore cells that don't have a root.cell volume. */

		if (afs_compare_volume_slists(cell->root_volume, p->root_volume) != 0)
			goto is_alias;
	}

	rcu_read_unlock();
	_leave(" = 0");
	return 0;

is_alias:
	rcu_read_unlock();
	cell->alias_of = afs_use_cell(p, afs_cell_trace_use_alias);
	return 1;
}

/*
 * Query the new cell for a volume from a cell we're already using.
 */
static int afs_query_for_alias_one(struct afs_cell *cell, struct key *key,
				   struct afs_cell *p)
{
	struct afs_volume *volume, *pvol = NULL;
	int ret;

	/* Arbitrarily pick a volume from the list. */
	read_seqlock_excl(&p->volume_lock);
	if (!RB_EMPTY_ROOT(&p->volumes))
		pvol = afs_get_volume(rb_entry(p->volumes.rb_node,
					       struct afs_volume, cell_node),
				      afs_volume_trace_get_query_alias);
	read_sequnlock_excl(&p->volume_lock);
	if (!pvol)
		return 0;

	_enter("%s:%s", cell->name, pvol->name);

	/* And see if it's in the new cell. */
	volume = afs_sample_volume(cell, key, pvol->name, pvol->name_len);
	if (IS_ERR(volume)) {
		afs_put_volume(pvol, afs_volume_trace_put_query_alias);
		if (PTR_ERR(volume) != -ENOMEDIUM)
			return PTR_ERR(volume);
		/* That volume is not in the new cell, so not an alias */
		return 0;
	}

	/* The new cell has a like-named volume also - compare volume ID,
	 * server and address lists.
	 */
	ret = 0;
	if (pvol->vid == volume->vid) {
		rcu_read_lock();
		if (afs_compare_volume_slists(volume, pvol))
			ret = 1;
		rcu_read_unlock();
	}

	afs_put_volume(volume, afs_volume_trace_put_query_alias);
	afs_put_volume(pvol, afs_volume_trace_put_query_alias);
	return ret;
}

/*
 * Query the new cell for volumes we know exist in cells we're already using.
 */
static int afs_query_for_alias(struct afs_cell *cell, struct key *key)
{
	struct afs_cell *p;

	_enter("%s", cell->name);

	if (mutex_lock_interruptible(&cell->net->proc_cells_lock) < 0)
		return -ERESTARTSYS;

	hlist_for_each_entry(p, &cell->net->proc_cells, proc_link) {
		if (p == cell || p->alias_of)
			continue;
		if (RB_EMPTY_ROOT(&p->volumes))
			continue;
		if (p->root_volume)
			continue; /* Ignore cells that have a root.cell volume. */
		afs_use_cell(p, afs_cell_trace_use_check_alias);
		mutex_unlock(&cell->net->proc_cells_lock);

		if (afs_query_for_alias_one(cell, key, p) != 0)
			goto is_alias;

		if (mutex_lock_interruptible(&cell->net->proc_cells_lock) < 0) {
			afs_unuse_cell(p, afs_cell_trace_unuse_check_alias);
			return -ERESTARTSYS;
		}

		afs_unuse_cell(p, afs_cell_trace_unuse_check_alias);
	}

	mutex_unlock(&cell->net->proc_cells_lock);
	_leave(" = 0");
	return 0;

is_alias:
	cell->alias_of = p; /* Transfer our ref */
	return 1;
}

/*
 * Look up a VLDB record for a volume.
 */
static char *afs_vl_get_cell_name(struct afs_cell *cell, struct key *key)
{
	struct afs_vl_cursor vc;
	char *cell_name = ERR_PTR(-EDESTADDRREQ);
	bool skipped = false, not_skipped = false;
	int ret;

	if (!afs_begin_vlserver_operation(&vc, cell, key))
		return ERR_PTR(-ERESTARTSYS);

	while (afs_select_vlserver(&vc)) {
		if (!test_bit(AFS_VLSERVER_FL_IS_YFS, &vc.server->flags)) {
			vc.call_error = -EOPNOTSUPP;
			skipped = true;
			continue;
		}
		not_skipped = true;
		cell_name = afs_yfsvl_get_cell_name(&vc);
	}

	ret = afs_end_vlserver_operation(&vc);
	if (skipped && !not_skipped)
		ret = -EOPNOTSUPP;
	return ret < 0 ? ERR_PTR(ret) : cell_name;
}

static int yfs_check_canonical_cell_name(struct afs_cell *cell, struct key *key)
{
	struct afs_cell *master;
	size_t name_len;
	char *cell_name;

	cell_name = afs_vl_get_cell_name(cell, key);
	if (IS_ERR(cell_name))
		return PTR_ERR(cell_name);

	if (strcmp(cell_name, cell->name) == 0) {
		kfree(cell_name);
		return 0;
	}

	name_len = strlen(cell_name);
	if (!name_len || name_len > AFS_MAXCELLNAME)
		master = ERR_PTR(-EOPNOTSUPP);
	else
		master = afs_lookup_cell(cell->net, cell_name, name_len, NULL, false,
					 afs_cell_trace_use_lookup_canonical);
	kfree(cell_name);
	if (IS_ERR(master))
		return PTR_ERR(master);

	cell->alias_of = master; /* Transfer our ref */
	return 1;
}

static int afs_do_cell_detect_alias(struct afs_cell *cell, struct key *key)
{
	struct afs_volume *root_volume;
	int ret;

	_enter("%s", cell->name);

	ret = yfs_check_canonical_cell_name(cell, key);
	if (ret != -EOPNOTSUPP)
		return ret;

	/* Try and get the root.cell volume for comparison with other cells */
	root_volume = afs_sample_volume(cell, key, "root.cell", 9);
	if (!IS_ERR(root_volume)) {
		cell->root_volume = root_volume;
		return afs_compare_cell_roots(cell);
	}

	if (PTR_ERR(root_volume) != -ENOMEDIUM)
		return PTR_ERR(root_volume);

	/* Okay, this cell doesn't have an root.cell volume.  We need to
	 * locate some other random volume and use that to check.
	 */
	return afs_query_for_alias(cell, key);
}

/*
 * Check to see if a new cell is an alias of a cell we already have.  At this
 * point we have the cell's volume server list.
 *
 * Returns 0 if we didn't detect an alias, 1 if we found an alias and an error
 * if we had problems gathering the data required.  In the case the we did
 * detect an alias, cell->alias_of is set to point to the assumed master.
 */
int afs_cell_detect_alias(struct afs_cell *cell, struct key *key)
{
	struct afs_net *net = cell->net;
	int ret;

	if (mutex_lock_interruptible(&net->cells_alias_lock) < 0)
		return -ERESTARTSYS;

	if (test_bit(AFS_CELL_FL_CHECK_ALIAS, &cell->flags)) {
		ret = afs_do_cell_detect_alias(cell, key);
		if (ret >= 0)
			clear_bit_unlock(AFS_CELL_FL_CHECK_ALIAS, &cell->flags);
	} else {
		ret = cell->alias_of ? 1 : 0;
	}

	mutex_unlock(&net->cells_alias_lock);

	if (ret == 1)
		pr_notice("kAFS: Cell %s is an alias of %s\n",
			  cell->name, cell->alias_of->name);
	return ret;
}
