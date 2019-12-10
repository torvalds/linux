// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS vlserver list management.
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include "internal.h"

struct afs_vlserver *afs_alloc_vlserver(const char *name, size_t name_len,
					unsigned short port)
{
	struct afs_vlserver *vlserver;

	vlserver = kzalloc(struct_size(vlserver, name, name_len + 1),
			   GFP_KERNEL);
	if (vlserver) {
		atomic_set(&vlserver->usage, 1);
		rwlock_init(&vlserver->lock);
		init_waitqueue_head(&vlserver->probe_wq);
		spin_lock_init(&vlserver->probe_lock);
		vlserver->name_len = name_len;
		vlserver->port = port;
		memcpy(vlserver->name, name, name_len);
	}
	return vlserver;
}

static void afs_vlserver_rcu(struct rcu_head *rcu)
{
	struct afs_vlserver *vlserver = container_of(rcu, struct afs_vlserver, rcu);

	afs_put_addrlist(rcu_access_pointer(vlserver->addresses));
	kfree_rcu(vlserver, rcu);
}

void afs_put_vlserver(struct afs_net *net, struct afs_vlserver *vlserver)
{
	if (vlserver) {
		unsigned int u = atomic_dec_return(&vlserver->usage);
		//_debug("VL PUT %p{%u}", vlserver, u);

		if (u == 0)
			call_rcu(&vlserver->rcu, afs_vlserver_rcu);
	}
}

struct afs_vlserver_list *afs_alloc_vlserver_list(unsigned int nr_servers)
{
	struct afs_vlserver_list *vllist;

	vllist = kzalloc(struct_size(vllist, servers, nr_servers), GFP_KERNEL);
	if (vllist) {
		atomic_set(&vllist->usage, 1);
		rwlock_init(&vllist->lock);
	}

	return vllist;
}

void afs_put_vlserverlist(struct afs_net *net, struct afs_vlserver_list *vllist)
{
	if (vllist) {
		unsigned int u = atomic_dec_return(&vllist->usage);

		//_debug("VLLS PUT %p{%u}", vllist, u);
		if (u == 0) {
			int i;

			for (i = 0; i < vllist->nr_servers; i++) {
				afs_put_vlserver(net, vllist->servers[i].server);
			}
			kfree_rcu(vllist, rcu);
		}
	}
}

static u16 afs_extract_le16(const u8 **_b)
{
	u16 val;

	val  = (u16)*(*_b)++ << 0;
	val |= (u16)*(*_b)++ << 8;
	return val;
}

/*
 * Build a VL server address list from a DNS queried server list.
 */
static struct afs_addr_list *afs_extract_vl_addrs(const u8 **_b, const u8 *end,
						  u8 nr_addrs, u16 port)
{
	struct afs_addr_list *alist;
	const u8 *b = *_b;
	int ret = -EINVAL;

	alist = afs_alloc_addrlist(nr_addrs, VL_SERVICE, port);
	if (!alist)
		return ERR_PTR(-ENOMEM);
	if (nr_addrs == 0)
		return alist;

	for (; nr_addrs > 0 && end - b >= nr_addrs; nr_addrs--) {
		struct dns_server_list_v1_address hdr;
		__be32 x[4];

		hdr.address_type = *b++;

		switch (hdr.address_type) {
		case DNS_ADDRESS_IS_IPV4:
			if (end - b < 4) {
				_leave(" = -EINVAL [short inet]");
				goto error;
			}
			memcpy(x, b, 4);
			afs_merge_fs_addr4(alist, x[0], port);
			b += 4;
			break;

		case DNS_ADDRESS_IS_IPV6:
			if (end - b < 16) {
				_leave(" = -EINVAL [short inet6]");
				goto error;
			}
			memcpy(x, b, 16);
			afs_merge_fs_addr6(alist, x, port);
			b += 16;
			break;

		default:
			_leave(" = -EADDRNOTAVAIL [unknown af %u]",
			       hdr.address_type);
			ret = -EADDRNOTAVAIL;
			goto error;
		}
	}

	/* Start with IPv6 if available. */
	if (alist->nr_ipv4 < alist->nr_addrs)
		alist->preferred = alist->nr_ipv4;

	*_b = b;
	return alist;

error:
	*_b = b;
	afs_put_addrlist(alist);
	return ERR_PTR(ret);
}

/*
 * Build a VL server list from a DNS queried server list.
 */
struct afs_vlserver_list *afs_extract_vlserver_list(struct afs_cell *cell,
						    const void *buffer,
						    size_t buffer_size)
{
	const struct dns_server_list_v1_header *hdr = buffer;
	struct dns_server_list_v1_server bs;
	struct afs_vlserver_list *vllist, *previous;
	struct afs_addr_list *addrs;
	struct afs_vlserver *server;
	const u8 *b = buffer, *end = buffer + buffer_size;
	int ret = -ENOMEM, nr_servers, i, j;

	_enter("");

	/* Check that it's a server list, v1 */
	if (end - b < sizeof(*hdr) ||
	    hdr->hdr.content != DNS_PAYLOAD_IS_SERVER_LIST ||
	    hdr->hdr.version != 1) {
		pr_notice("kAFS: Got DNS record [%u,%u] len %zu\n",
			  hdr->hdr.content, hdr->hdr.version, end - b);
		ret = -EDESTADDRREQ;
		goto dump;
	}

	nr_servers = hdr->nr_servers;

	vllist = afs_alloc_vlserver_list(nr_servers);
	if (!vllist)
		return ERR_PTR(-ENOMEM);

	vllist->source = (hdr->source < NR__dns_record_source) ?
		hdr->source : NR__dns_record_source;
	vllist->status = (hdr->status < NR__dns_lookup_status) ?
		hdr->status : NR__dns_lookup_status;

	read_lock(&cell->vl_servers_lock);
	previous = afs_get_vlserverlist(
		rcu_dereference_protected(cell->vl_servers,
					  lockdep_is_held(&cell->vl_servers_lock)));
	read_unlock(&cell->vl_servers_lock);

	b += sizeof(*hdr);
	while (end - b >= sizeof(bs)) {
		bs.name_len	= afs_extract_le16(&b);
		bs.priority	= afs_extract_le16(&b);
		bs.weight	= afs_extract_le16(&b);
		bs.port		= afs_extract_le16(&b);
		bs.source	= *b++;
		bs.status	= *b++;
		bs.protocol	= *b++;
		bs.nr_addrs	= *b++;

		_debug("extract %u %u %u %u %u %u %*.*s",
		       bs.name_len, bs.priority, bs.weight,
		       bs.port, bs.protocol, bs.nr_addrs,
		       bs.name_len, bs.name_len, b);

		if (end - b < bs.name_len)
			break;

		ret = -EPROTONOSUPPORT;
		if (bs.protocol == DNS_SERVER_PROTOCOL_UNSPECIFIED) {
			bs.protocol = DNS_SERVER_PROTOCOL_UDP;
		} else if (bs.protocol != DNS_SERVER_PROTOCOL_UDP) {
			_leave(" = [proto %u]", bs.protocol);
			goto error;
		}

		if (bs.port == 0)
			bs.port = AFS_VL_PORT;
		if (bs.source > NR__dns_record_source)
			bs.source = NR__dns_record_source;
		if (bs.status > NR__dns_lookup_status)
			bs.status = NR__dns_lookup_status;

		/* See if we can update an old server record */
		server = NULL;
		for (i = 0; i < previous->nr_servers; i++) {
			struct afs_vlserver *p = previous->servers[i].server;

			if (p->name_len == bs.name_len &&
			    p->port == bs.port &&
			    strncasecmp(b, p->name, bs.name_len) == 0) {
				server = afs_get_vlserver(p);
				break;
			}
		}

		if (!server) {
			ret = -ENOMEM;
			server = afs_alloc_vlserver(b, bs.name_len, bs.port);
			if (!server)
				goto error;
		}

		b += bs.name_len;

		/* Extract the addresses - note that we can't skip this as we
		 * have to advance the payload pointer.
		 */
		addrs = afs_extract_vl_addrs(&b, end, bs.nr_addrs, bs.port);
		if (IS_ERR(addrs)) {
			ret = PTR_ERR(addrs);
			goto error_2;
		}

		if (vllist->nr_servers >= nr_servers) {
			_debug("skip %u >= %u", vllist->nr_servers, nr_servers);
			afs_put_addrlist(addrs);
			afs_put_vlserver(cell->net, server);
			continue;
		}

		addrs->source = bs.source;
		addrs->status = bs.status;

		if (addrs->nr_addrs == 0) {
			afs_put_addrlist(addrs);
			if (!rcu_access_pointer(server->addresses)) {
				afs_put_vlserver(cell->net, server);
				continue;
			}
		} else {
			struct afs_addr_list *old = addrs;

			write_lock(&server->lock);
			old = rcu_replace_pointer(server->addresses, old,
						  lockdep_is_held(&server->lock));
			write_unlock(&server->lock);
			afs_put_addrlist(old);
		}


		/* TODO: Might want to check for duplicates */

		/* Insertion-sort by priority and weight */
		for (j = 0; j < vllist->nr_servers; j++) {
			if (bs.priority < vllist->servers[j].priority)
				break; /* Lower preferable */
			if (bs.priority == vllist->servers[j].priority &&
			    bs.weight > vllist->servers[j].weight)
				break; /* Higher preferable */
		}

		if (j < vllist->nr_servers) {
			memmove(vllist->servers + j + 1,
				vllist->servers + j,
				(vllist->nr_servers - j) * sizeof(struct afs_vlserver_entry));
		}

		clear_bit(AFS_VLSERVER_FL_PROBED, &server->flags);

		vllist->servers[j].priority = bs.priority;
		vllist->servers[j].weight = bs.weight;
		vllist->servers[j].server = server;
		vllist->nr_servers++;
	}

	if (b != end) {
		_debug("parse error %zd", b - end);
		goto error;
	}

	afs_put_vlserverlist(cell->net, previous);
	_leave(" = ok [%u]", vllist->nr_servers);
	return vllist;

error_2:
	afs_put_vlserver(cell->net, server);
error:
	afs_put_vlserverlist(cell->net, vllist);
	afs_put_vlserverlist(cell->net, previous);
dump:
	if (ret != -ENOMEM) {
		printk(KERN_DEBUG "DNS: at %zu\n", (const void *)b - buffer);
		print_hex_dump_bytes("DNS: ", DUMP_PREFIX_NONE, buffer, buffer_size);
	}
	return ERR_PTR(ret);
}
