// SPDX-License-Identifier: GPL-2.0-or-later
/* Address preferences management
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": addr_prefs: " fmt
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/inet.h>
#include <linux/seq_file.h>
#include <keys/rxrpc-type.h>
#include "internal.h"

static inline struct afs_net *afs_seq2net_single(struct seq_file *m)
{
	return afs_net(seq_file_single_net(m));
}

/*
 * Split a NUL-terminated string up to the first newline around spaces.  The
 * source string will be modified to have NUL-terminations inserted.
 */
static int afs_split_string(char **pbuf, char *strv[], unsigned int maxstrv)
{
	unsigned int count = 0;
	char *p = *pbuf;

	maxstrv--; /* Allow for terminal NULL */
	for (;;) {
		/* Skip over spaces */
		while (isspace(*p)) {
			if (*p == '\n') {
				p++;
				break;
			}
			p++;
		}
		if (!*p)
			break;

		/* Mark start of word */
		if (count >= maxstrv) {
			pr_warn("Too many elements in string\n");
			return -EINVAL;
		}
		strv[count++] = p;

		/* Skip over word */
		while (!isspace(*p))
			p++;
		if (!*p)
			break;

		/* Mark end of word */
		if (*p == '\n') {
			*p++ = 0;
			break;
		}
		*p++ = 0;
	}

	*pbuf = p;
	strv[count] = NULL;
	return count;
}

/*
 * Parse an address with an optional subnet mask.
 */
static int afs_parse_address(char *p, struct afs_addr_preference *pref)
{
	const char *stop;
	unsigned long mask, tmp;
	char *end = p + strlen(p);
	bool bracket = false;

	if (*p == '[') {
		p++;
		bracket = true;
	}

#if 0
	if (*p == '[') {
		p++;
		q = memchr(p, ']', end - p);
		if (!q) {
			pr_warn("Can't find closing ']'\n");
			return -EINVAL;
		}
	} else {
		for (q = p; q < end; q++)
			if (*q == '/')
				break;
	}
#endif

	if (in4_pton(p, end - p, (u8 *)&pref->ipv4_addr, -1, &stop)) {
		pref->family = AF_INET;
		mask = 32;
	} else if (in6_pton(p, end - p, (u8 *)&pref->ipv6_addr, -1, &stop)) {
		pref->family = AF_INET6;
		mask = 128;
	} else {
		pr_warn("Can't determine address family\n");
		return -EINVAL;
	}

	p = (char *)stop;
	if (bracket) {
		if (*p != ']') {
			pr_warn("Can't find closing ']'\n");
			return -EINVAL;
		}
		p++;
	}

	if (*p == '/') {
		p++;
		tmp = simple_strtoul(p, &p, 10);
		if (tmp > mask) {
			pr_warn("Subnet mask too large\n");
			return -EINVAL;
		}
		if (tmp == 0) {
			pr_warn("Subnet mask too small\n");
			return -EINVAL;
		}
		mask = tmp;
	}

	if (*p) {
		pr_warn("Invalid address\n");
		return -EINVAL;
	}

	pref->subnet_mask = mask;
	return 0;
}

enum cmp_ret {
	CONTINUE_SEARCH,
	INSERT_HERE,
	EXACT_MATCH,
	SUBNET_MATCH,
};

/*
 * See if a candidate address matches a listed address.
 */
static enum cmp_ret afs_cmp_address_pref(const struct afs_addr_preference *a,
					 const struct afs_addr_preference *b)
{
	int subnet = min(a->subnet_mask, b->subnet_mask);
	const __be32 *pa, *pb;
	u32 mask, na, nb;
	int diff;

	if (a->family != b->family)
		return INSERT_HERE;

	switch (a->family) {
	case AF_INET6:
		pa = a->ipv6_addr.s6_addr32;
		pb = b->ipv6_addr.s6_addr32;
		break;
	case AF_INET:
		pa = &a->ipv4_addr.s_addr;
		pb = &b->ipv4_addr.s_addr;
		break;
	}

	while (subnet > 32) {
		diff = ntohl(*pa++) - ntohl(*pb++);
		if (diff < 0)
			return INSERT_HERE; /* a<b */
		if (diff > 0)
			return CONTINUE_SEARCH; /* a>b */
		subnet -= 32;
	}

	if (subnet == 0)
		return EXACT_MATCH;

	mask = 0xffffffffU << (32 - subnet);
	na = ntohl(*pa);
	nb = ntohl(*pb);
	diff = (na & mask) - (nb & mask);
	//kdebug("diff %08x %08x %08x %d", na, nb, mask, diff);
	if (diff < 0)
		return INSERT_HERE; /* a<b */
	if (diff > 0)
		return CONTINUE_SEARCH; /* a>b */
	if (a->subnet_mask == b->subnet_mask)
		return EXACT_MATCH;
	if (a->subnet_mask > b->subnet_mask)
		return SUBNET_MATCH; /* a binds tighter than b */
	return CONTINUE_SEARCH; /* b binds tighter than a */
}

/*
 * Insert an address preference.
 */
static int afs_insert_address_pref(struct afs_addr_preference_list **_preflist,
				   struct afs_addr_preference *pref,
				   int index)
{
	struct afs_addr_preference_list *preflist = *_preflist, *old = preflist;
	size_t size, max_prefs;

	_enter("{%u/%u/%u},%u", preflist->ipv6_off, preflist->nr, preflist->max_prefs, index);

	if (preflist->nr == 255)
		return -ENOSPC;
	if (preflist->nr >= preflist->max_prefs) {
		max_prefs = preflist->max_prefs + 1;
		size = struct_size(preflist, prefs, max_prefs);
		size = roundup_pow_of_two(size);
		max_prefs = min_t(size_t, (size - sizeof(*preflist)) / sizeof(*pref), 255);
		preflist = kmalloc(size, GFP_KERNEL);
		if (!preflist)
			return -ENOMEM;
		*preflist = **_preflist;
		preflist->max_prefs = max_prefs;
		*_preflist = preflist;

		if (index < preflist->nr)
			memcpy(preflist->prefs + index + 1, old->prefs + index,
			       sizeof(*pref) * (preflist->nr - index));
		if (index > 0)
			memcpy(preflist->prefs, old->prefs, sizeof(*pref) * index);
	} else {
		if (index < preflist->nr)
			memmove(preflist->prefs + index + 1, preflist->prefs + index,
			       sizeof(*pref) * (preflist->nr - index));
	}

	preflist->prefs[index] = *pref;
	preflist->nr++;
	if (pref->family == AF_INET)
		preflist->ipv6_off++;
	return 0;
}

/*
 * Add an address preference.
 *	echo "add <proto> <IP>[/<mask>] <prior>" >/proc/fs/afs/addr_prefs
 */
static int afs_add_address_pref(struct afs_net *net, struct afs_addr_preference_list **_preflist,
				int argc, char **argv)
{
	struct afs_addr_preference_list *preflist = *_preflist;
	struct afs_addr_preference pref;
	enum cmp_ret cmp;
	int ret, i, stop;

	if (argc != 3) {
		pr_warn("Wrong number of params\n");
		return -EINVAL;
	}

	if (strcmp(argv[0], "udp") != 0) {
		pr_warn("Unsupported protocol\n");
		return -EINVAL;
	}

	ret = afs_parse_address(argv[1], &pref);
	if (ret < 0)
		return ret;

	ret = kstrtou16(argv[2], 10, &pref.prio);
	if (ret < 0) {
		pr_warn("Invalid priority\n");
		return ret;
	}

	if (pref.family == AF_INET) {
		i = 0;
		stop = preflist->ipv6_off;
	} else {
		i = preflist->ipv6_off;
		stop = preflist->nr;
	}

	for (; i < stop; i++) {
		cmp = afs_cmp_address_pref(&pref, &preflist->prefs[i]);
		switch (cmp) {
		case CONTINUE_SEARCH:
			continue;
		case INSERT_HERE:
		case SUBNET_MATCH:
			return afs_insert_address_pref(_preflist, &pref, i);
		case EXACT_MATCH:
			preflist->prefs[i].prio = pref.prio;
			return 0;
		}
	}

	return afs_insert_address_pref(_preflist, &pref, i);
}

/*
 * Delete an address preference.
 */
static int afs_delete_address_pref(struct afs_addr_preference_list **_preflist,
				   int index)
{
	struct afs_addr_preference_list *preflist = *_preflist;

	_enter("{%u/%u/%u},%u", preflist->ipv6_off, preflist->nr, preflist->max_prefs, index);

	if (preflist->nr == 0)
		return -ENOENT;

	if (index < preflist->nr - 1)
		memmove(preflist->prefs + index, preflist->prefs + index + 1,
			sizeof(preflist->prefs[0]) * (preflist->nr - index - 1));

	if (index < preflist->ipv6_off)
		preflist->ipv6_off--;
	preflist->nr--;
	return 0;
}

/*
 * Delete an address preference.
 *	echo "del <proto> <IP>[/<mask>]" >/proc/fs/afs/addr_prefs
 */
static int afs_del_address_pref(struct afs_net *net, struct afs_addr_preference_list **_preflist,
				int argc, char **argv)
{
	struct afs_addr_preference_list *preflist = *_preflist;
	struct afs_addr_preference pref;
	enum cmp_ret cmp;
	int ret, i, stop;

	if (argc != 2) {
		pr_warn("Wrong number of params\n");
		return -EINVAL;
	}

	if (strcmp(argv[0], "udp") != 0) {
		pr_warn("Unsupported protocol\n");
		return -EINVAL;
	}

	ret = afs_parse_address(argv[1], &pref);
	if (ret < 0)
		return ret;

	if (pref.family == AF_INET) {
		i = 0;
		stop = preflist->ipv6_off;
	} else {
		i = preflist->ipv6_off;
		stop = preflist->nr;
	}

	for (; i < stop; i++) {
		cmp = afs_cmp_address_pref(&pref, &preflist->prefs[i]);
		switch (cmp) {
		case CONTINUE_SEARCH:
			continue;
		case INSERT_HERE:
		case SUBNET_MATCH:
			return 0;
		case EXACT_MATCH:
			return afs_delete_address_pref(_preflist, i);
		}
	}

	return -ENOANO;
}

/*
 * Handle writes to /proc/fs/afs/addr_prefs
 */
int afs_proc_addr_prefs_write(struct file *file, char *buf, size_t size)
{
	struct afs_addr_preference_list *preflist, *old;
	struct seq_file *m = file->private_data;
	struct afs_net *net = afs_seq2net_single(m);
	size_t psize;
	char *argv[5];
	int ret, argc, max_prefs;

	inode_lock(file_inode(file));

	/* Allocate a candidate new list and initialise it from the old. */
	old = rcu_dereference_protected(net->address_prefs,
					lockdep_is_held(&file_inode(file)->i_rwsem));

	if (old)
		max_prefs = old->nr + 1;
	else
		max_prefs = 1;

	psize = struct_size(old, prefs, max_prefs);
	psize = roundup_pow_of_two(psize);
	max_prefs = min_t(size_t, (psize - sizeof(*old)) / sizeof(old->prefs[0]), 255);

	ret = -ENOMEM;
	preflist = kmalloc(struct_size(preflist, prefs, max_prefs), GFP_KERNEL);
	if (!preflist)
		goto done;

	if (old)
		memcpy(preflist, old, struct_size(preflist, prefs, old->nr));
	else
		memset(preflist, 0, sizeof(*preflist));
	preflist->max_prefs = max_prefs;

	do {
		argc = afs_split_string(&buf, argv, ARRAY_SIZE(argv));
		if (argc < 0)
			return argc;
		if (argc < 2)
			goto inval;

		if (strcmp(argv[0], "add") == 0)
			ret = afs_add_address_pref(net, &preflist, argc - 1, argv + 1);
		else if (strcmp(argv[0], "del") == 0)
			ret = afs_del_address_pref(net, &preflist, argc - 1, argv + 1);
		else
			goto inval;
		if (ret < 0)
			goto done;
	} while (*buf);

	preflist->version++;
	rcu_assign_pointer(net->address_prefs, preflist);
	/* Store prefs before version */
	smp_store_release(&net->address_pref_version, preflist->version);
	kfree_rcu(old, rcu);
	preflist = NULL;
	ret = 0;

done:
	kfree(preflist);
	inode_unlock(file_inode(file));
	_leave(" = %d", ret);
	return ret;

inval:
	pr_warn("Invalid Command\n");
	ret = -EINVAL;
	goto done;
}

/*
 * Mark the priorities on an address list if the address preferences table has
 * changed.  The caller must hold the RCU read lock.
 */
void afs_get_address_preferences_rcu(struct afs_net *net, struct afs_addr_list *alist)
{
	const struct afs_addr_preference_list *preflist =
		rcu_dereference(net->address_prefs);
	const struct sockaddr_in6 *sin6;
	const struct sockaddr_in *sin;
	const struct sockaddr *sa;
	struct afs_addr_preference test;
	enum cmp_ret cmp;
	int i, j;

	if (!preflist || !preflist->nr || !alist->nr_addrs ||
	    smp_load_acquire(&alist->addr_pref_version) == preflist->version)
		return;

	test.family = AF_INET;
	test.subnet_mask = 32;
	test.prio = 0;
	for (i = 0; i < alist->nr_ipv4; i++) {
		sa = rxrpc_kernel_remote_addr(alist->addrs[i].peer);
		sin = (const struct sockaddr_in *)sa;
		test.ipv4_addr = sin->sin_addr;
		for (j = 0; j < preflist->ipv6_off; j++) {
			cmp = afs_cmp_address_pref(&test, &preflist->prefs[j]);
			switch (cmp) {
			case CONTINUE_SEARCH:
				continue;
			case INSERT_HERE:
				break;
			case EXACT_MATCH:
			case SUBNET_MATCH:
				WRITE_ONCE(alist->addrs[i].prio, preflist->prefs[j].prio);
				break;
			}
		}
	}

	test.family = AF_INET6;
	test.subnet_mask = 128;
	test.prio = 0;
	for (; i < alist->nr_addrs; i++) {
		sa = rxrpc_kernel_remote_addr(alist->addrs[i].peer);
		sin6 = (const struct sockaddr_in6 *)sa;
		test.ipv6_addr = sin6->sin6_addr;
		for (j = preflist->ipv6_off; j < preflist->nr; j++) {
			cmp = afs_cmp_address_pref(&test, &preflist->prefs[j]);
			switch (cmp) {
			case CONTINUE_SEARCH:
				continue;
			case INSERT_HERE:
				break;
			case EXACT_MATCH:
			case SUBNET_MATCH:
				WRITE_ONCE(alist->addrs[i].prio, preflist->prefs[j].prio);
				break;
			}
		}
	}

	smp_store_release(&alist->addr_pref_version, preflist->version);
}

/*
 * Mark the priorities on an address list if the address preferences table has
 * changed.  Avoid taking the RCU read lock if we can.
 */
void afs_get_address_preferences(struct afs_net *net, struct afs_addr_list *alist)
{
	if (!net->address_prefs ||
	    /* Load version before prefs */
	    smp_load_acquire(&net->address_pref_version) == alist->addr_pref_version)
		return;

	rcu_read_lock();
	afs_get_address_preferences_rcu(net, alist);
	rcu_read_unlock();
}
