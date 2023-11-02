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
		for (i = 0; i < slist->nr_servers; i++)
			afs_unuse_server(net, slist->servers[i].server,
					 afs_server_trace_put_slist);
		kfree_rcu(slist, rcu);
	}
}

/*
 * Build a server list from a VLDB record.
 */
struct afs_server_list *afs_alloc_server_list(struct afs_volume *volume,
					      struct key *key,
					      struct afs_vldb_entry *vldb)
{
	struct afs_server_list *slist;
	struct afs_server *server;
	unsigned int type_mask = 1 << volume->type;
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

		server = afs_lookup_server(volume->cell, key, &vldb->fs_server[i],
					   vldb->addr_version[i]);
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
				afs_put_server(volume->cell->net, server,
					       afs_server_trace_put_slist_isort);
				continue;
			}

			memmove(slist->servers + j + 1,
				slist->servers + j,
				(slist->nr_servers - j) * sizeof(struct afs_server_entry));
		}

		slist->servers[j].server = server;
		slist->servers[j].volume = volume;
		slist->nr_servers++;
	}

	if (slist->nr_servers == 0) {
		ret = -EDESTADDRREQ;
		goto error_2;
	}

	return slist;

error_2:
	afs_put_serverlist(volume->cell->net, slist);
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

	return true;
}

/*
 * Attach a volume to the servers it is going to use.
 */
void afs_attach_volume_to_servers(struct afs_volume *volume, struct afs_server_list *slist)
{
	struct afs_server_entry *se, *pe;
	struct afs_server *server;
	struct list_head *p;
	unsigned int i;

	spin_lock(&volume->cell->vs_lock);

	for (i = 0; i < slist->nr_servers; i++) {
		se = &slist->servers[i];
		server = se->server;

		list_for_each(p, &server->volumes) {
			pe = list_entry(p, struct afs_server_entry, slink);
			if (volume->vid <= pe->volume->vid)
				break;
		}
		list_add_tail_rcu(&se->slink, p);
	}

	slist->attached = true;
	spin_unlock(&volume->cell->vs_lock);
}

/*
 * Reattach a volume to the servers it is going to use when server list is
 * replaced.  We try to switch the attachment points to avoid rewalking the
 * lists.
 */
void afs_reattach_volume_to_servers(struct afs_volume *volume, struct afs_server_list *new,
				    struct afs_server_list *old)
{
	unsigned int n = 0, o = 0;

	spin_lock(&volume->cell->vs_lock);

	while (n < new->nr_servers || o < old->nr_servers) {
		struct afs_server_entry *pn = n < new->nr_servers ? &new->servers[n] : NULL;
		struct afs_server_entry *po = o < old->nr_servers ? &old->servers[o] : NULL;
		struct afs_server_entry *s;
		struct list_head *p;
		int diff;

		if (pn && po && pn->server == po->server) {
			list_replace_rcu(&po->slink, &pn->slink);
			n++;
			o++;
			continue;
		}

		if (pn && po)
			diff = memcmp(&pn->server->uuid, &po->server->uuid,
				      sizeof(pn->server->uuid));
		else
			diff = pn ? -1 : 1;

		if (diff < 0) {
			list_for_each(p, &pn->server->volumes) {
				s = list_entry(p, struct afs_server_entry, slink);
				if (volume->vid <= s->volume->vid)
					break;
			}
			list_add_tail_rcu(&pn->slink, p);
			n++;
		} else {
			list_del_rcu(&po->slink);
			o++;
		}
	}

	spin_unlock(&volume->cell->vs_lock);
}

/*
 * Detach a volume from the servers it has been using.
 */
void afs_detach_volume_from_servers(struct afs_volume *volume, struct afs_server_list *slist)
{
	unsigned int i;

	if (!slist->attached)
		return;

	spin_lock(&volume->cell->vs_lock);

	for (i = 0; i < slist->nr_servers; i++)
		list_del_rcu(&slist->servers[i].slink);

	slist->attached = false;
	spin_unlock(&volume->cell->vs_lock);
}
