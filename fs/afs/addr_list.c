// SPDX-License-Identifier: GPL-2.0-or-later
/* Server address list management
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/dns_resolver.h>
#include <linux/inet.h>
#include <keys/rxrpc-type.h>
#include "internal.h"
#include "afs_fs.h"

static void afs_free_addrlist(struct rcu_head *rcu)
{
	struct afs_addr_list *alist = container_of(rcu, struct afs_addr_list, rcu);
	unsigned int i;

	for (i = 0; i < alist->nr_addrs; i++)
		rxrpc_kernel_put_peer(alist->addrs[i].peer);
	trace_afs_alist(alist->debug_id, refcount_read(&alist->usage), afs_alist_trace_free);
	kfree(alist);
}

/*
 * Release an address list.
 */
void afs_put_addrlist(struct afs_addr_list *alist, enum afs_alist_trace reason)
{
	unsigned int debug_id;
	bool dead;
	int r;

	if (!alist)
		return;
	debug_id = alist->debug_id;
	dead = __refcount_dec_and_test(&alist->usage, &r);
	trace_afs_alist(debug_id, r - 1, reason);
	if (dead)
		call_rcu(&alist->rcu, afs_free_addrlist);
}

struct afs_addr_list *afs_get_addrlist(struct afs_addr_list *alist, enum afs_alist_trace reason)
{
	int r;

	if (alist) {
		__refcount_inc(&alist->usage, &r);
		trace_afs_alist(alist->debug_id, r + 1, reason);
	}
	return alist;
}

/*
 * Allocate an address list.
 */
struct afs_addr_list *afs_alloc_addrlist(unsigned int nr)
{
	struct afs_addr_list *alist;
	static atomic_t debug_id;

	_enter("%u", nr);

	if (nr > AFS_MAX_ADDRESSES)
		nr = AFS_MAX_ADDRESSES;

	alist = kzalloc(struct_size(alist, addrs, nr), GFP_KERNEL);
	if (!alist)
		return NULL;

	refcount_set(&alist->usage, 1);
	alist->max_addrs = nr;
	alist->debug_id = atomic_inc_return(&debug_id);
	trace_afs_alist(alist->debug_id, 1, afs_alist_trace_alloc);
	return alist;
}

/*
 * Parse a text string consisting of delimited addresses.
 */
struct afs_vlserver_list *afs_parse_text_addrs(struct afs_net *net,
					       const char *text, size_t len,
					       char delim,
					       unsigned short service,
					       unsigned short port)
{
	struct afs_vlserver_list *vllist;
	struct afs_addr_list *alist;
	const char *p, *end = text + len;
	const char *problem;
	unsigned int nr = 0;
	int ret = -ENOMEM;

	_enter("%*.*s,%c", (int)len, (int)len, text, delim);

	if (!len) {
		_leave(" = -EDESTADDRREQ [empty]");
		return ERR_PTR(-EDESTADDRREQ);
	}

	if (delim == ':' && (memchr(text, ',', len) || !memchr(text, '.', len)))
		delim = ',';

	/* Count the addresses */
	p = text;
	do {
		if (!*p) {
			problem = "nul";
			goto inval;
		}
		if (*p == delim)
			continue;
		nr++;
		if (*p == '[') {
			p++;
			if (p == end) {
				problem = "brace1";
				goto inval;
			}
			p = memchr(p, ']', end - p);
			if (!p) {
				problem = "brace2";
				goto inval;
			}
			p++;
			if (p >= end)
				break;
		}

		p = memchr(p, delim, end - p);
		if (!p)
			break;
		p++;
	} while (p < end);

	_debug("%u/%u addresses", nr, AFS_MAX_ADDRESSES);

	vllist = afs_alloc_vlserver_list(1);
	if (!vllist)
		return ERR_PTR(-ENOMEM);

	vllist->nr_servers = 1;
	vllist->servers[0].server = afs_alloc_vlserver("<dummy>", 7, AFS_VL_PORT);
	if (!vllist->servers[0].server)
		goto error_vl;

	alist = afs_alloc_addrlist(nr);
	if (!alist)
		goto error;

	/* Extract the addresses */
	p = text;
	do {
		const char *q, *stop;
		unsigned int xport = port;
		__be32 x[4];
		int family;

		if (*p == delim) {
			p++;
			continue;
		}

		if (*p == '[') {
			p++;
			q = memchr(p, ']', end - p);
		} else {
			for (q = p; q < end; q++)
				if (*q == '+' || *q == delim)
					break;
		}

		if (in4_pton(p, q - p, (u8 *)&x[0], -1, &stop)) {
			family = AF_INET;
		} else if (in6_pton(p, q - p, (u8 *)x, -1, &stop)) {
			family = AF_INET6;
		} else {
			problem = "family";
			goto bad_address;
		}

		p = q;
		if (stop != p) {
			problem = "nostop";
			goto bad_address;
		}

		if (q < end && *q == ']')
			p++;

		if (p < end) {
			if (*p == '+') {
				/* Port number specification "+1234" */
				xport = 0;
				p++;
				if (p >= end || !isdigit(*p)) {
					problem = "port";
					goto bad_address;
				}
				do {
					xport *= 10;
					xport += *p - '0';
					if (xport > 65535) {
						problem = "pval";
						goto bad_address;
					}
					p++;
				} while (p < end && isdigit(*p));
			} else if (*p == delim) {
				p++;
			} else {
				problem = "weird";
				goto bad_address;
			}
		}

		if (family == AF_INET)
			ret = afs_merge_fs_addr4(net, alist, x[0], xport);
		else
			ret = afs_merge_fs_addr6(net, alist, x, xport);
		if (ret < 0)
			goto error;

	} while (p < end);

	rcu_assign_pointer(vllist->servers[0].server->addresses, alist);
	_leave(" = [nr %u]", alist->nr_addrs);
	return vllist;

inval:
	_leave(" = -EINVAL [%s %zu %*.*s]",
	       problem, p - text, (int)len, (int)len, text);
	return ERR_PTR(-EINVAL);
bad_address:
	_leave(" = -EINVAL [%s %zu %*.*s]",
	       problem, p - text, (int)len, (int)len, text);
	ret = -EINVAL;
error:
	afs_put_addrlist(alist, afs_alist_trace_put_parse_error);
error_vl:
	afs_put_vlserverlist(net, vllist);
	return ERR_PTR(ret);
}

/*
 * Perform a DNS query for VL servers and build a up an address list.
 */
struct afs_vlserver_list *afs_dns_query(struct afs_cell *cell, time64_t *_expiry)
{
	struct afs_vlserver_list *vllist;
	char *result = NULL;
	int ret;

	_enter("%s", cell->name);

	ret = dns_query(cell->net->net, "afsdb", cell->name, cell->name_len,
			"srv=1", &result, _expiry, true);
	if (ret < 0) {
		_leave(" = %d [dns]", ret);
		return ERR_PTR(ret);
	}

	if (*_expiry == 0)
		*_expiry = ktime_get_real_seconds() + 60;

	if (ret > 1 && result[0] == 0)
		vllist = afs_extract_vlserver_list(cell, result, ret);
	else
		vllist = afs_parse_text_addrs(cell->net, result, ret, ',',
					      VL_SERVICE, AFS_VL_PORT);
	kfree(result);
	if (IS_ERR(vllist) && vllist != ERR_PTR(-ENOMEM))
		pr_err("Failed to parse DNS data %ld\n", PTR_ERR(vllist));

	return vllist;
}

/*
 * Merge an IPv4 entry into a fileserver address list.
 */
int afs_merge_fs_addr4(struct afs_net *net, struct afs_addr_list *alist,
		       __be32 xdr, u16 port)
{
	struct sockaddr_rxrpc srx;
	struct rxrpc_peer *peer;
	int i;

	if (alist->nr_addrs >= alist->max_addrs)
		return 0;

	srx.srx_family = AF_RXRPC;
	srx.transport_type = SOCK_DGRAM;
	srx.transport_len = sizeof(srx.transport.sin);
	srx.transport.sin.sin_family = AF_INET;
	srx.transport.sin.sin_port = htons(port);
	srx.transport.sin.sin_addr.s_addr = xdr;

	peer = rxrpc_kernel_lookup_peer(net->socket, &srx, GFP_KERNEL);
	if (!peer)
		return -ENOMEM;

	for (i = 0; i < alist->nr_ipv4; i++) {
		if (peer == alist->addrs[i].peer) {
			rxrpc_kernel_put_peer(peer);
			return 0;
		}
		if (peer <= alist->addrs[i].peer)
			break;
	}

	if (i < alist->nr_addrs)
		memmove(alist->addrs + i + 1,
			alist->addrs + i,
			sizeof(alist->addrs[0]) * (alist->nr_addrs - i));

	alist->addrs[i].peer = peer;
	alist->nr_ipv4++;
	alist->nr_addrs++;
	return 0;
}

/*
 * Merge an IPv6 entry into a fileserver address list.
 */
int afs_merge_fs_addr6(struct afs_net *net, struct afs_addr_list *alist,
		       __be32 *xdr, u16 port)
{
	struct sockaddr_rxrpc srx;
	struct rxrpc_peer *peer;
	int i;

	if (alist->nr_addrs >= alist->max_addrs)
		return 0;

	srx.srx_family = AF_RXRPC;
	srx.transport_type = SOCK_DGRAM;
	srx.transport_len = sizeof(srx.transport.sin6);
	srx.transport.sin6.sin6_family = AF_INET6;
	srx.transport.sin6.sin6_port = htons(port);
	memcpy(&srx.transport.sin6.sin6_addr, xdr, 16);

	peer = rxrpc_kernel_lookup_peer(net->socket, &srx, GFP_KERNEL);
	if (!peer)
		return -ENOMEM;

	for (i = alist->nr_ipv4; i < alist->nr_addrs; i++) {
		if (peer == alist->addrs[i].peer) {
			rxrpc_kernel_put_peer(peer);
			return 0;
		}
		if (peer <= alist->addrs[i].peer)
			break;
	}

	if (i < alist->nr_addrs)
		memmove(alist->addrs + i + 1,
			alist->addrs + i,
			sizeof(alist->addrs[0]) * (alist->nr_addrs - i));
	alist->addrs[i].peer = peer;
	alist->nr_addrs++;
	return 0;
}

/*
 * Set the app data on the rxrpc peers an address list points to
 */
void afs_set_peer_appdata(struct afs_server *server,
			  struct afs_addr_list *old_alist,
			  struct afs_addr_list *new_alist)
{
	unsigned long data = (unsigned long)server;
	int n = 0, o = 0;

	if (!old_alist) {
		/* New server.  Just set all. */
		for (; n < new_alist->nr_addrs; n++)
			rxrpc_kernel_set_peer_data(new_alist->addrs[n].peer, data);
		return;
	}
	if (!new_alist) {
		/* Dead server.  Just remove all. */
		for (; o < old_alist->nr_addrs; o++)
			rxrpc_kernel_set_peer_data(old_alist->addrs[o].peer, 0);
		return;
	}

	/* Walk through the two lists simultaneously, setting new peers and
	 * clearing old ones.  The two lists are ordered by pointer to peer
	 * record.
	 */
	while (n < new_alist->nr_addrs && o < old_alist->nr_addrs) {
		struct rxrpc_peer *pn = new_alist->addrs[n].peer;
		struct rxrpc_peer *po = old_alist->addrs[o].peer;

		if (pn == po)
			continue;
		if (pn < po) {
			rxrpc_kernel_set_peer_data(pn, data);
			n++;
		} else {
			rxrpc_kernel_set_peer_data(po, 0);
			o++;
		}
	}

	if (n < new_alist->nr_addrs)
		for (; n < new_alist->nr_addrs; n++)
			rxrpc_kernel_set_peer_data(new_alist->addrs[n].peer, data);
	if (o < old_alist->nr_addrs)
		for (; o < old_alist->nr_addrs; o++)
			rxrpc_kernel_set_peer_data(old_alist->addrs[o].peer, 0);
}
