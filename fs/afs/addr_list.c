/* Server address list management
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/dns_resolver.h>
#include <linux/inet.h>
#include <keys/rxrpc-type.h>
#include "internal.h"
#include "afs_fs.h"

//#define AFS_MAX_ADDRESSES
//	((unsigned int)((PAGE_SIZE - sizeof(struct afs_addr_list)) /
//			sizeof(struct sockaddr_rxrpc)))
#define AFS_MAX_ADDRESSES ((unsigned int)(sizeof(unsigned long) * 8))

/*
 * Release an address list.
 */
void afs_put_addrlist(struct afs_addr_list *alist)
{
	if (alist && refcount_dec_and_test(&alist->usage))
		call_rcu(&alist->rcu, (rcu_callback_t)kfree);
}

/*
 * Allocate an address list.
 */
struct afs_addr_list *afs_alloc_addrlist(unsigned int nr,
					 unsigned short service,
					 unsigned short port)
{
	struct afs_addr_list *alist;
	unsigned int i;

	_enter("%u,%u,%u", nr, service, port);

	alist = kzalloc(sizeof(*alist) + sizeof(alist->addrs[0]) * nr,
			GFP_KERNEL);
	if (!alist)
		return NULL;

	refcount_set(&alist->usage, 1);

	for (i = 0; i < nr; i++) {
		struct sockaddr_rxrpc *srx = &alist->addrs[i];
		srx->srx_family			= AF_RXRPC;
		srx->srx_service		= service;
		srx->transport_type		= SOCK_DGRAM;
		srx->transport_len		= sizeof(srx->transport.sin6);
		srx->transport.sin6.sin6_family	= AF_INET6;
		srx->transport.sin6.sin6_port	= htons(port);
	}

	return alist;
}

/*
 * Parse a text string consisting of delimited addresses.
 */
struct afs_addr_list *afs_parse_text_addrs(const char *text, size_t len,
					   char delim,
					   unsigned short service,
					   unsigned short port)
{
	struct afs_addr_list *alist;
	const char *p, *end = text + len;
	unsigned int nr = 0;

	_enter("%*.*s,%c", (int)len, (int)len, text, delim);

	if (!len)
		return ERR_PTR(-EDESTADDRREQ);

	if (delim == ':' && (memchr(text, ',', len) || !memchr(text, '.', len)))
		delim = ',';

	/* Count the addresses */
	p = text;
	do {
		if (!*p)
			return ERR_PTR(-EINVAL);
		if (*p == delim)
			continue;
		nr++;
		if (*p == '[') {
			p++;
			if (p == end)
				return ERR_PTR(-EINVAL);
			p = memchr(p, ']', end - p);
			if (!p)
				return ERR_PTR(-EINVAL);
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
	if (nr > AFS_MAX_ADDRESSES)
		nr = AFS_MAX_ADDRESSES;

	alist = afs_alloc_addrlist(nr, service, port);
	if (!alist)
		return ERR_PTR(-ENOMEM);

	/* Extract the addresses */
	p = text;
	do {
		struct sockaddr_rxrpc *srx = &alist->addrs[alist->nr_addrs];
		const char *q, *stop;

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

		if (in4_pton(p, q - p,
			     (u8 *)&srx->transport.sin6.sin6_addr.s6_addr32[3],
			     -1, &stop)) {
			srx->transport.sin6.sin6_addr.s6_addr32[0] = 0;
			srx->transport.sin6.sin6_addr.s6_addr32[1] = 0;
			srx->transport.sin6.sin6_addr.s6_addr32[2] = htonl(0xffff);
		} else if (in6_pton(p, q - p,
				    srx->transport.sin6.sin6_addr.s6_addr,
				    -1, &stop)) {
			/* Nothing to do */
		} else {
			goto bad_address;
		}

		if (stop != q)
			goto bad_address;

		p = q;
		if (q < end && *q == ']')
			p++;

		if (p < end) {
			if (*p == '+') {
				/* Port number specification "+1234" */
				unsigned int xport = 0;
				p++;
				if (p >= end || !isdigit(*p))
					goto bad_address;
				do {
					xport *= 10;
					xport += *p - '0';
					if (xport > 65535)
						goto bad_address;
					p++;
				} while (p < end && isdigit(*p));
				srx->transport.sin6.sin6_port = htons(xport);
			} else if (*p == delim) {
				p++;
			} else {
				goto bad_address;
			}
		}

		alist->nr_addrs++;
	} while (p < end && alist->nr_addrs < AFS_MAX_ADDRESSES);

	_leave(" = [nr %u]", alist->nr_addrs);
	return alist;

bad_address:
	kfree(alist);
	return ERR_PTR(-EINVAL);
}

/*
 * Compare old and new address lists to see if there's been any change.
 * - How to do this in better than O(Nlog(N)) time?
 *   - We don't really want to sort the address list, but would rather take the
 *     list as we got it so as not to undo record rotation by the DNS server.
 */
#if 0
static int afs_cmp_addr_list(const struct afs_addr_list *a1,
			     const struct afs_addr_list *a2)
{
}
#endif

/*
 * Perform a DNS query for VL servers and build a up an address list.
 */
struct afs_addr_list *afs_dns_query(struct afs_cell *cell, time64_t *_expiry)
{
	struct afs_addr_list *alist;
	char *vllist = NULL;
	int ret;

	_enter("%s", cell->name);

	ret = dns_query("afsdb", cell->name, cell->name_len,
			"ipv4", &vllist, _expiry);
	if (ret < 0)
		return ERR_PTR(ret);

	alist = afs_parse_text_addrs(vllist, strlen(vllist), ',',
				     VL_SERVICE, AFS_VL_PORT);
	if (IS_ERR(alist)) {
		kfree(vllist);
		if (alist != ERR_PTR(-ENOMEM))
			pr_err("Failed to parse DNS data\n");
		return alist;
	}

	kfree(vllist);
	return alist;
}

/*
 * Merge an IPv4 entry into a fileserver address list.
 */
void afs_merge_fs_addr4(struct afs_addr_list *alist, __be32 xdr, u16 port)
{
	struct sockaddr_in6 *a;
	__be16 xport = htons(port);
	int i;

	for (i = 0; i < alist->nr_ipv4; i++) {
		a = &alist->addrs[i].transport.sin6;
		if (xdr == a->sin6_addr.s6_addr32[3] &&
		    xport == a->sin6_port)
			return;
		if (xdr == a->sin6_addr.s6_addr32[3] &&
		    (u16 __force)xport < (u16 __force)a->sin6_port)
			break;
		if ((u32 __force)xdr < (u32 __force)a->sin6_addr.s6_addr32[3])
			break;
	}

	if (i < alist->nr_addrs)
		memmove(alist->addrs + i + 1,
			alist->addrs + i,
			sizeof(alist->addrs[0]) * (alist->nr_addrs - i));

	a = &alist->addrs[i].transport.sin6;
	a->sin6_port		  = xport;
	a->sin6_addr.s6_addr32[0] = 0;
	a->sin6_addr.s6_addr32[1] = 0;
	a->sin6_addr.s6_addr32[2] = htonl(0xffff);
	a->sin6_addr.s6_addr32[3] = xdr;
	alist->nr_ipv4++;
	alist->nr_addrs++;
}

/*
 * Merge an IPv6 entry into a fileserver address list.
 */
void afs_merge_fs_addr6(struct afs_addr_list *alist, __be32 *xdr, u16 port)
{
	struct sockaddr_in6 *a;
	__be16 xport = htons(port);
	int i, diff;

	for (i = alist->nr_ipv4; i < alist->nr_addrs; i++) {
		a = &alist->addrs[i].transport.sin6;
		diff = memcmp(xdr, &a->sin6_addr, 16);
		if (diff == 0 &&
		    xport == a->sin6_port)
			return;
		if (diff == 0 &&
		    (u16 __force)xport < (u16 __force)a->sin6_port)
			break;
		if (diff < 0)
			break;
	}

	if (i < alist->nr_addrs)
		memmove(alist->addrs + i + 1,
			alist->addrs + i,
			sizeof(alist->addrs[0]) * (alist->nr_addrs - i));

	a = &alist->addrs[i].transport.sin6;
	a->sin6_port		  = xport;
	a->sin6_addr.s6_addr32[0] = xdr[0];
	a->sin6_addr.s6_addr32[1] = xdr[1];
	a->sin6_addr.s6_addr32[2] = xdr[2];
	a->sin6_addr.s6_addr32[3] = xdr[3];
	alist->nr_addrs++;
}

/*
 * Get an address to try.
 */
bool afs_iterate_addresses(struct afs_addr_cursor *ac)
{
	_enter("%hu+%hd", ac->start, (short)ac->index);

	if (!ac->alist)
		return false;

	if (ac->begun) {
		ac->index++;
		if (ac->index == ac->alist->nr_addrs)
			ac->index = 0;

		if (ac->index == ac->start) {
			ac->error = -EDESTADDRREQ;
			return false;
		}
	}

	ac->begun = true;
	ac->responded = false;
	ac->addr = &ac->alist->addrs[ac->index];
	return true;
}

/*
 * Release an address list cursor.
 */
int afs_end_cursor(struct afs_addr_cursor *ac)
{
	struct afs_addr_list *alist;

	alist = ac->alist;
	if (alist) {
		if (ac->responded && ac->index != ac->start)
			WRITE_ONCE(alist->index, ac->index);
		afs_put_addrlist(alist);
	}

	ac->addr = NULL;
	ac->alist = NULL;
	ac->begun = false;
	return ac->error;
}

/*
 * Set the address cursor for iterating over VL servers.
 */
int afs_set_vl_cursor(struct afs_addr_cursor *ac, struct afs_cell *cell)
{
	struct afs_addr_list *alist;
	int ret;

	if (!rcu_access_pointer(cell->vl_addrs)) {
		ret = wait_on_bit(&cell->flags, AFS_CELL_FL_NO_LOOKUP_YET,
				  TASK_INTERRUPTIBLE);
		if (ret < 0)
			return ret;

		if (!rcu_access_pointer(cell->vl_addrs) &&
		    ktime_get_real_seconds() < cell->dns_expiry)
			return cell->error;
	}

	read_lock(&cell->vl_addrs_lock);
	alist = rcu_dereference_protected(cell->vl_addrs,
					  lockdep_is_held(&cell->vl_addrs_lock));
	if (alist->nr_addrs > 0)
		afs_get_addrlist(alist);
	else
		alist = NULL;
	read_unlock(&cell->vl_addrs_lock);

	if (!alist)
		return -EDESTADDRREQ;

	ac->alist = alist;
	ac->addr = NULL;
	ac->start = READ_ONCE(alist->index);
	ac->index = ac->start;
	ac->error = 0;
	ac->begun = false;
	return 0;
}
