// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS fileserver list management.
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include "internal.h"

void afs_put_serverlist(struct afs_net *net, struct afs_server_list *slist)
{
	int i;

	if (slist && refcount_dec_and_test(&slist->usage)) {
		for (i = 0; i < slist->nr_servers; i++) {
			afs_put_cb_interest(net, slist->servers[i].cb_interest);
			afs_put_server(net, slist->servers[i].server,
				       afs_server_trace_put_slist);
		}
		kfree(slist);
	}
}

/*
 * Build a server list from a VLDB record.
 */
struct afs_server_list *afs_alloc_server_list(struct afs_cell *cell,
					      struct key *key,
					      struct afs_vldb_entry *vldb,
					      u8 type_mask)
{
	struct afs_server_list *slist;
	struct afs_server *server;
	int ret = -ENOMEM, nr_servers = 0, i, j;

	for (i = 0; i < vldb->nr_servers; i++)
		if (vldb->fs_mask[i] & type_mask)
			nr_servers++;

	slist = kzalloc(struct_size(slist, servers, nr_servers), GFP_KERNEL);
	if (!slist)
		goto error;

	refcount_set(&slist->usage, 1);
	rwlock_init(&slist->lock);

	/* Make sure a records exists for each server in the list. */
	for (i = 0; i < vldb->nr_servers; i++) {
		if (!(vldb->fs_mask[i] & type_mask))
			continue;

		server = afs_lookup_server(cell, key, &vldb->fs_server[i]);
		if (IS_ERR(server)) {
			ret = PTR_ERR(server);
			if (ret == -ENOENT ||
			    ret == -ENOMEDIUM)
				continue;
			goto error_2;
		}

		/* Insertion-sort by UUID */
		for (j = 0; j < slist->nr_servers; j++)
			if (memcmp(&slist->servers[j].server->uuid,
				   &server->uuid,
				   sizeof(server->uuid)) >= 0)
				break;
		if (j < slist->nr_servers) {
			if (slist->servers[j].server == server) {
				afs_put_server(cell->net, server,
					       afs_server_trace_put_slist_isort);
				continue;
			}

			memmove(slist->servers + j + 1,
				slist->servers + j,
				(slist->nr_servers - j) * sizeof(struct afs_server_entry));
		}

		slist->servers[j].server = server;
		slist->nr_servers++;
	}

	if (slist->nr_servers == 0) {
		ret = -EDESTADDRREQ;
		goto error_2;
	}

	return slist;

error_2:
	afs_put_serverlist(cell->net, slist);
error:
	return ERR_PTR(ret);
}

/*
 * Copy the annotations from an old server list to its potential replacement.
 */
bool afs_annotate_server_list(struct afs_server_list *new,
			      struct afs_server_list *old)
{
	struct afs_server *cur;
	int i, j;

	if (old->nr_servers != new->nr_servers)
		goto changed;

	for (i = 0; i < old->nr_servers; i++)
		if (old->servers[i].server != new->servers[i].server)
			goto changed;

	return false;

changed:
	/* Maintain the same preferred server as before if possible. */
	cur = old->servers[old->preferred].server;
	for (j = 0; j < new->nr_servers; j++) {
		if (new->servers[j].server == cur) {
			new->preferred = j;
			break;
		}
	}

	/* Keep the old callback interest records where possible so that we
	 * maintain callback interception.
	 */
	i = 0;
	j = 0;
	while (i < old->nr_servers && j < new->nr_servers) {
		if (new->servers[j].server == old->servers[i].server) {
			struct afs_cb_interest *cbi = old->servers[i].cb_interest;
			if (cbi) {
				new->servers[j].cb_interest = cbi;
				refcount_inc(&cbi->usage);
			}
			i++;
			j++;
			continue;
		}

		if (new->servers[j].server < old->servers[i].server) {
			j++;
			continue;
		}

		i++;
		continue;
	}

	return true;
}
