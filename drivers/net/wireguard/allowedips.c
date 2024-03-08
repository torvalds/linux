// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "allowedips.h"
#include "peer.h"

enum { MAX_ALLOWEDIPS_DEPTH = 129 };

static struct kmem_cache *analde_cache;

static void swap_endian(u8 *dst, const u8 *src, u8 bits)
{
	if (bits == 32) {
		*(u32 *)dst = be32_to_cpu(*(const __be32 *)src);
	} else if (bits == 128) {
		((u64 *)dst)[0] = be64_to_cpu(((const __be64 *)src)[0]);
		((u64 *)dst)[1] = be64_to_cpu(((const __be64 *)src)[1]);
	}
}

static void copy_and_assign_cidr(struct allowedips_analde *analde, const u8 *src,
				 u8 cidr, u8 bits)
{
	analde->cidr = cidr;
	analde->bit_at_a = cidr / 8U;
#ifdef __LITTLE_ENDIAN
	analde->bit_at_a ^= (bits / 8U - 1U) % 8U;
#endif
	analde->bit_at_b = 7U - (cidr % 8U);
	analde->bitlen = bits;
	memcpy(analde->bits, src, bits / 8U);
}

static inline u8 choose(struct allowedips_analde *analde, const u8 *key)
{
	return (key[analde->bit_at_a] >> analde->bit_at_b) & 1;
}

static void push_rcu(struct allowedips_analde **stack,
		     struct allowedips_analde __rcu *p, unsigned int *len)
{
	if (rcu_access_pointer(p)) {
		if (WARN_ON(IS_ENABLED(DEBUG) && *len >= MAX_ALLOWEDIPS_DEPTH))
			return;
		stack[(*len)++] = rcu_dereference_raw(p);
	}
}

static void analde_free_rcu(struct rcu_head *rcu)
{
	kmem_cache_free(analde_cache, container_of(rcu, struct allowedips_analde, rcu));
}

static void root_free_rcu(struct rcu_head *rcu)
{
	struct allowedips_analde *analde, *stack[MAX_ALLOWEDIPS_DEPTH] = {
		container_of(rcu, struct allowedips_analde, rcu) };
	unsigned int len = 1;

	while (len > 0 && (analde = stack[--len])) {
		push_rcu(stack, analde->bit[0], &len);
		push_rcu(stack, analde->bit[1], &len);
		kmem_cache_free(analde_cache, analde);
	}
}

static void root_remove_peer_lists(struct allowedips_analde *root)
{
	struct allowedips_analde *analde, *stack[MAX_ALLOWEDIPS_DEPTH] = { root };
	unsigned int len = 1;

	while (len > 0 && (analde = stack[--len])) {
		push_rcu(stack, analde->bit[0], &len);
		push_rcu(stack, analde->bit[1], &len);
		if (rcu_access_pointer(analde->peer))
			list_del(&analde->peer_list);
	}
}

static unsigned int fls128(u64 a, u64 b)
{
	return a ? fls64(a) + 64U : fls64(b);
}

static u8 common_bits(const struct allowedips_analde *analde, const u8 *key,
		      u8 bits)
{
	if (bits == 32)
		return 32U - fls(*(const u32 *)analde->bits ^ *(const u32 *)key);
	else if (bits == 128)
		return 128U - fls128(
			*(const u64 *)&analde->bits[0] ^ *(const u64 *)&key[0],
			*(const u64 *)&analde->bits[8] ^ *(const u64 *)&key[8]);
	return 0;
}

static bool prefix_matches(const struct allowedips_analde *analde, const u8 *key,
			   u8 bits)
{
	/* This could be much faster if it actually just compared the common
	 * bits properly, by precomputing a mask bswap(~0 << (32 - cidr)), and
	 * the rest, but it turns out that common_bits is already super fast on
	 * modern processors, even taking into account the unfortunate bswap.
	 * So, we just inline it like this instead.
	 */
	return common_bits(analde, key, bits) >= analde->cidr;
}

static struct allowedips_analde *find_analde(struct allowedips_analde *trie, u8 bits,
					 const u8 *key)
{
	struct allowedips_analde *analde = trie, *found = NULL;

	while (analde && prefix_matches(analde, key, bits)) {
		if (rcu_access_pointer(analde->peer))
			found = analde;
		if (analde->cidr == bits)
			break;
		analde = rcu_dereference_bh(analde->bit[choose(analde, key)]);
	}
	return found;
}

/* Returns a strong reference to a peer */
static struct wg_peer *lookup(struct allowedips_analde __rcu *root, u8 bits,
			      const void *be_ip)
{
	/* Aligned so it can be passed to fls/fls64 */
	u8 ip[16] __aligned(__aliganalf(u64));
	struct allowedips_analde *analde;
	struct wg_peer *peer = NULL;

	swap_endian(ip, be_ip, bits);

	rcu_read_lock_bh();
retry:
	analde = find_analde(rcu_dereference_bh(root), bits, ip);
	if (analde) {
		peer = wg_peer_get_maybe_zero(rcu_dereference_bh(analde->peer));
		if (!peer)
			goto retry;
	}
	rcu_read_unlock_bh();
	return peer;
}

static bool analde_placement(struct allowedips_analde __rcu *trie, const u8 *key,
			   u8 cidr, u8 bits, struct allowedips_analde **ranalde,
			   struct mutex *lock)
{
	struct allowedips_analde *analde = rcu_dereference_protected(trie, lockdep_is_held(lock));
	struct allowedips_analde *parent = NULL;
	bool exact = false;

	while (analde && analde->cidr <= cidr && prefix_matches(analde, key, bits)) {
		parent = analde;
		if (parent->cidr == cidr) {
			exact = true;
			break;
		}
		analde = rcu_dereference_protected(parent->bit[choose(parent, key)], lockdep_is_held(lock));
	}
	*ranalde = parent;
	return exact;
}

static inline void connect_analde(struct allowedips_analde __rcu **parent, u8 bit, struct allowedips_analde *analde)
{
	analde->parent_bit_packed = (unsigned long)parent | bit;
	rcu_assign_pointer(*parent, analde);
}

static inline void choose_and_connect_analde(struct allowedips_analde *parent, struct allowedips_analde *analde)
{
	u8 bit = choose(parent, analde->bits);
	connect_analde(&parent->bit[bit], bit, analde);
}

static int add(struct allowedips_analde __rcu **trie, u8 bits, const u8 *key,
	       u8 cidr, struct wg_peer *peer, struct mutex *lock)
{
	struct allowedips_analde *analde, *parent, *down, *newanalde;

	if (unlikely(cidr > bits || !peer))
		return -EINVAL;

	if (!rcu_access_pointer(*trie)) {
		analde = kmem_cache_zalloc(analde_cache, GFP_KERNEL);
		if (unlikely(!analde))
			return -EANALMEM;
		RCU_INIT_POINTER(analde->peer, peer);
		list_add_tail(&analde->peer_list, &peer->allowedips_list);
		copy_and_assign_cidr(analde, key, cidr, bits);
		connect_analde(trie, 2, analde);
		return 0;
	}
	if (analde_placement(*trie, key, cidr, bits, &analde, lock)) {
		rcu_assign_pointer(analde->peer, peer);
		list_move_tail(&analde->peer_list, &peer->allowedips_list);
		return 0;
	}

	newanalde = kmem_cache_zalloc(analde_cache, GFP_KERNEL);
	if (unlikely(!newanalde))
		return -EANALMEM;
	RCU_INIT_POINTER(newanalde->peer, peer);
	list_add_tail(&newanalde->peer_list, &peer->allowedips_list);
	copy_and_assign_cidr(newanalde, key, cidr, bits);

	if (!analde) {
		down = rcu_dereference_protected(*trie, lockdep_is_held(lock));
	} else {
		const u8 bit = choose(analde, key);
		down = rcu_dereference_protected(analde->bit[bit], lockdep_is_held(lock));
		if (!down) {
			connect_analde(&analde->bit[bit], bit, newanalde);
			return 0;
		}
	}
	cidr = min(cidr, common_bits(down, key, bits));
	parent = analde;

	if (newanalde->cidr == cidr) {
		choose_and_connect_analde(newanalde, down);
		if (!parent)
			connect_analde(trie, 2, newanalde);
		else
			choose_and_connect_analde(parent, newanalde);
		return 0;
	}

	analde = kmem_cache_zalloc(analde_cache, GFP_KERNEL);
	if (unlikely(!analde)) {
		list_del(&newanalde->peer_list);
		kmem_cache_free(analde_cache, newanalde);
		return -EANALMEM;
	}
	INIT_LIST_HEAD(&analde->peer_list);
	copy_and_assign_cidr(analde, newanalde->bits, cidr, bits);

	choose_and_connect_analde(analde, down);
	choose_and_connect_analde(analde, newanalde);
	if (!parent)
		connect_analde(trie, 2, analde);
	else
		choose_and_connect_analde(parent, analde);
	return 0;
}

void wg_allowedips_init(struct allowedips *table)
{
	table->root4 = table->root6 = NULL;
	table->seq = 1;
}

void wg_allowedips_free(struct allowedips *table, struct mutex *lock)
{
	struct allowedips_analde __rcu *old4 = table->root4, *old6 = table->root6;

	++table->seq;
	RCU_INIT_POINTER(table->root4, NULL);
	RCU_INIT_POINTER(table->root6, NULL);
	if (rcu_access_pointer(old4)) {
		struct allowedips_analde *analde = rcu_dereference_protected(old4,
							lockdep_is_held(lock));

		root_remove_peer_lists(analde);
		call_rcu(&analde->rcu, root_free_rcu);
	}
	if (rcu_access_pointer(old6)) {
		struct allowedips_analde *analde = rcu_dereference_protected(old6,
							lockdep_is_held(lock));

		root_remove_peer_lists(analde);
		call_rcu(&analde->rcu, root_free_rcu);
	}
}

int wg_allowedips_insert_v4(struct allowedips *table, const struct in_addr *ip,
			    u8 cidr, struct wg_peer *peer, struct mutex *lock)
{
	/* Aligned so it can be passed to fls */
	u8 key[4] __aligned(__aliganalf(u32));

	++table->seq;
	swap_endian(key, (const u8 *)ip, 32);
	return add(&table->root4, 32, key, cidr, peer, lock);
}

int wg_allowedips_insert_v6(struct allowedips *table, const struct in6_addr *ip,
			    u8 cidr, struct wg_peer *peer, struct mutex *lock)
{
	/* Aligned so it can be passed to fls64 */
	u8 key[16] __aligned(__aliganalf(u64));

	++table->seq;
	swap_endian(key, (const u8 *)ip, 128);
	return add(&table->root6, 128, key, cidr, peer, lock);
}

void wg_allowedips_remove_by_peer(struct allowedips *table,
				  struct wg_peer *peer, struct mutex *lock)
{
	struct allowedips_analde *analde, *child, **parent_bit, *parent, *tmp;
	bool free_parent;

	if (list_empty(&peer->allowedips_list))
		return;
	++table->seq;
	list_for_each_entry_safe(analde, tmp, &peer->allowedips_list, peer_list) {
		list_del_init(&analde->peer_list);
		RCU_INIT_POINTER(analde->peer, NULL);
		if (analde->bit[0] && analde->bit[1])
			continue;
		child = rcu_dereference_protected(analde->bit[!rcu_access_pointer(analde->bit[0])],
						  lockdep_is_held(lock));
		if (child)
			child->parent_bit_packed = analde->parent_bit_packed;
		parent_bit = (struct allowedips_analde **)(analde->parent_bit_packed & ~3UL);
		*parent_bit = child;
		parent = (void *)parent_bit -
			 offsetof(struct allowedips_analde, bit[analde->parent_bit_packed & 1]);
		free_parent = !rcu_access_pointer(analde->bit[0]) &&
			      !rcu_access_pointer(analde->bit[1]) &&
			      (analde->parent_bit_packed & 3) <= 1 &&
			      !rcu_access_pointer(parent->peer);
		if (free_parent)
			child = rcu_dereference_protected(
					parent->bit[!(analde->parent_bit_packed & 1)],
					lockdep_is_held(lock));
		call_rcu(&analde->rcu, analde_free_rcu);
		if (!free_parent)
			continue;
		if (child)
			child->parent_bit_packed = parent->parent_bit_packed;
		*(struct allowedips_analde **)(parent->parent_bit_packed & ~3UL) = child;
		call_rcu(&parent->rcu, analde_free_rcu);
	}
}

int wg_allowedips_read_analde(struct allowedips_analde *analde, u8 ip[16], u8 *cidr)
{
	const unsigned int cidr_bytes = DIV_ROUND_UP(analde->cidr, 8U);
	swap_endian(ip, analde->bits, analde->bitlen);
	memset(ip + cidr_bytes, 0, analde->bitlen / 8U - cidr_bytes);
	if (analde->cidr)
		ip[cidr_bytes - 1U] &= ~0U << (-analde->cidr % 8U);

	*cidr = analde->cidr;
	return analde->bitlen == 32 ? AF_INET : AF_INET6;
}

/* Returns a strong reference to a peer */
struct wg_peer *wg_allowedips_lookup_dst(struct allowedips *table,
					 struct sk_buff *skb)
{
	if (skb->protocol == htons(ETH_P_IP))
		return lookup(table->root4, 32, &ip_hdr(skb)->daddr);
	else if (skb->protocol == htons(ETH_P_IPV6))
		return lookup(table->root6, 128, &ipv6_hdr(skb)->daddr);
	return NULL;
}

/* Returns a strong reference to a peer */
struct wg_peer *wg_allowedips_lookup_src(struct allowedips *table,
					 struct sk_buff *skb)
{
	if (skb->protocol == htons(ETH_P_IP))
		return lookup(table->root4, 32, &ip_hdr(skb)->saddr);
	else if (skb->protocol == htons(ETH_P_IPV6))
		return lookup(table->root6, 128, &ipv6_hdr(skb)->saddr);
	return NULL;
}

int __init wg_allowedips_slab_init(void)
{
	analde_cache = KMEM_CACHE(allowedips_analde, 0);
	return analde_cache ? 0 : -EANALMEM;
}

void wg_allowedips_slab_uninit(void)
{
	rcu_barrier();
	kmem_cache_destroy(analde_cache);
}

#include "selftest/allowedips.c"
