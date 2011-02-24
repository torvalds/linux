/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2010 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/in.h>
#include "efx.h"
#include "filter.h"
#include "io.h"
#include "nic.h"
#include "regs.h"

/* "Fudge factors" - difference between programmed value and actual depth.
 * Due to pipelined implementation we need to program H/W with a value that
 * is larger than the hop limit we want.
 */
#define FILTER_CTL_SRCH_FUDGE_WILD 3
#define FILTER_CTL_SRCH_FUDGE_FULL 1

/* Hard maximum hop limit.  Hardware will time-out beyond 200-something.
 * We also need to avoid infinite loops in efx_filter_search() when the
 * table is full.
 */
#define FILTER_CTL_SRCH_MAX 200

enum efx_filter_table_id {
	EFX_FILTER_TABLE_RX_IP = 0,
	EFX_FILTER_TABLE_RX_MAC,
	EFX_FILTER_TABLE_COUNT,
};

struct efx_filter_table {
	enum efx_filter_table_id id;
	u32		offset;		/* address of table relative to BAR */
	unsigned	size;		/* number of entries */
	unsigned	step;		/* step between entries */
	unsigned	used;		/* number currently used */
	unsigned long	*used_bitmap;
	struct efx_filter_spec *spec;
	unsigned	search_depth[EFX_FILTER_TYPE_COUNT];
};

struct efx_filter_state {
	spinlock_t	lock;
	struct efx_filter_table table[EFX_FILTER_TABLE_COUNT];
};

/* The filter hash function is LFSR polynomial x^16 + x^3 + 1 of a 32-bit
 * key derived from the n-tuple.  The initial LFSR state is 0xffff. */
static u16 efx_filter_hash(u32 key)
{
	u16 tmp;

	/* First 16 rounds */
	tmp = 0x1fff ^ key >> 16;
	tmp = tmp ^ tmp >> 3 ^ tmp >> 6;
	tmp = tmp ^ tmp >> 9;
	/* Last 16 rounds */
	tmp = tmp ^ tmp << 13 ^ key;
	tmp = tmp ^ tmp >> 3 ^ tmp >> 6;
	return tmp ^ tmp >> 9;
}

/* To allow for hash collisions, filter search continues at these
 * increments from the first possible entry selected by the hash. */
static u16 efx_filter_increment(u32 key)
{
	return key * 2 - 1;
}

static enum efx_filter_table_id
efx_filter_spec_table_id(const struct efx_filter_spec *spec)
{
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_IP != (EFX_FILTER_TCP_FULL >> 2));
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_IP != (EFX_FILTER_TCP_WILD >> 2));
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_IP != (EFX_FILTER_UDP_FULL >> 2));
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_IP != (EFX_FILTER_UDP_WILD >> 2));
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_MAC != (EFX_FILTER_MAC_FULL >> 2));
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_MAC != (EFX_FILTER_MAC_WILD >> 2));
	EFX_BUG_ON_PARANOID(spec->type == EFX_FILTER_UNSPEC);
	return spec->type >> 2;
}

static struct efx_filter_table *
efx_filter_spec_table(struct efx_filter_state *state,
		      const struct efx_filter_spec *spec)
{
	if (spec->type == EFX_FILTER_UNSPEC)
		return NULL;
	else
		return &state->table[efx_filter_spec_table_id(spec)];
}

static void efx_filter_table_reset_search_depth(struct efx_filter_table *table)
{
	memset(table->search_depth, 0, sizeof(table->search_depth));
}

static void efx_filter_push_rx_limits(struct efx_nic *efx)
{
	struct efx_filter_state *state = efx->filter_state;
	struct efx_filter_table *table;
	efx_oword_t filter_ctl;

	efx_reado(efx, &filter_ctl, FR_BZ_RX_FILTER_CTL);

	table = &state->table[EFX_FILTER_TABLE_RX_IP];
	EFX_SET_OWORD_FIELD(filter_ctl, FRF_BZ_TCP_FULL_SRCH_LIMIT,
			    table->search_depth[EFX_FILTER_TCP_FULL] +
			    FILTER_CTL_SRCH_FUDGE_FULL);
	EFX_SET_OWORD_FIELD(filter_ctl, FRF_BZ_TCP_WILD_SRCH_LIMIT,
			    table->search_depth[EFX_FILTER_TCP_WILD] +
			    FILTER_CTL_SRCH_FUDGE_WILD);
	EFX_SET_OWORD_FIELD(filter_ctl, FRF_BZ_UDP_FULL_SRCH_LIMIT,
			    table->search_depth[EFX_FILTER_UDP_FULL] +
			    FILTER_CTL_SRCH_FUDGE_FULL);
	EFX_SET_OWORD_FIELD(filter_ctl, FRF_BZ_UDP_WILD_SRCH_LIMIT,
			    table->search_depth[EFX_FILTER_UDP_WILD] +
			    FILTER_CTL_SRCH_FUDGE_WILD);

	table = &state->table[EFX_FILTER_TABLE_RX_MAC];
	if (table->size) {
		EFX_SET_OWORD_FIELD(
			filter_ctl, FRF_CZ_ETHERNET_FULL_SEARCH_LIMIT,
			table->search_depth[EFX_FILTER_MAC_FULL] +
			FILTER_CTL_SRCH_FUDGE_FULL);
		EFX_SET_OWORD_FIELD(
			filter_ctl, FRF_CZ_ETHERNET_WILDCARD_SEARCH_LIMIT,
			table->search_depth[EFX_FILTER_MAC_WILD] +
			FILTER_CTL_SRCH_FUDGE_WILD);
	}

	efx_writeo(efx, &filter_ctl, FR_BZ_RX_FILTER_CTL);
}

static inline void __efx_filter_set_ipv4(struct efx_filter_spec *spec,
					 __be32 host1, __be16 port1,
					 __be32 host2, __be16 port2)
{
	spec->data[0] = ntohl(host1) << 16 | ntohs(port1);
	spec->data[1] = ntohs(port2) << 16 | ntohl(host1) >> 16;
	spec->data[2] = ntohl(host2);
}

/**
 * efx_filter_set_ipv4_local - specify IPv4 host, transport protocol and port
 * @spec: Specification to initialise
 * @proto: Transport layer protocol number
 * @host: Local host address (network byte order)
 * @port: Local port (network byte order)
 */
int efx_filter_set_ipv4_local(struct efx_filter_spec *spec, u8 proto,
			      __be32 host, __be16 port)
{
	__be32 host1;
	__be16 port1;

	EFX_BUG_ON_PARANOID(!(spec->flags & EFX_FILTER_FLAG_RX));

	/* This cannot currently be combined with other filtering */
	if (spec->type != EFX_FILTER_UNSPEC)
		return -EPROTONOSUPPORT;

	if (port == 0)
		return -EINVAL;

	switch (proto) {
	case IPPROTO_TCP:
		spec->type = EFX_FILTER_TCP_WILD;
		break;
	case IPPROTO_UDP:
		spec->type = EFX_FILTER_UDP_WILD;
		break;
	default:
		return -EPROTONOSUPPORT;
	}

	/* Filter is constructed in terms of source and destination,
	 * with the odd wrinkle that the ports are swapped in a UDP
	 * wildcard filter.  We need to convert from local and remote
	 * (= zero for wildcard) addresses.
	 */
	host1 = 0;
	if (proto != IPPROTO_UDP) {
		port1 = 0;
	} else {
		port1 = port;
		port = 0;
	}

	__efx_filter_set_ipv4(spec, host1, port1, host, port);
	return 0;
}

/**
 * efx_filter_set_ipv4_full - specify IPv4 hosts, transport protocol and ports
 * @spec: Specification to initialise
 * @proto: Transport layer protocol number
 * @host: Local host address (network byte order)
 * @port: Local port (network byte order)
 * @rhost: Remote host address (network byte order)
 * @rport: Remote port (network byte order)
 */
int efx_filter_set_ipv4_full(struct efx_filter_spec *spec, u8 proto,
			     __be32 host, __be16 port,
			     __be32 rhost, __be16 rport)
{
	EFX_BUG_ON_PARANOID(!(spec->flags & EFX_FILTER_FLAG_RX));

	/* This cannot currently be combined with other filtering */
	if (spec->type != EFX_FILTER_UNSPEC)
		return -EPROTONOSUPPORT;

	if (port == 0 || rport == 0)
		return -EINVAL;

	switch (proto) {
	case IPPROTO_TCP:
		spec->type = EFX_FILTER_TCP_FULL;
		break;
	case IPPROTO_UDP:
		spec->type = EFX_FILTER_UDP_FULL;
		break;
	default:
		return -EPROTONOSUPPORT;
	}

	__efx_filter_set_ipv4(spec, rhost, rport, host, port);
	return 0;
}

/**
 * efx_filter_set_eth_local - specify local Ethernet address and optional VID
 * @spec: Specification to initialise
 * @vid: VLAN ID to match, or %EFX_FILTER_VID_UNSPEC
 * @addr: Local Ethernet MAC address
 */
int efx_filter_set_eth_local(struct efx_filter_spec *spec,
			     u16 vid, const u8 *addr)
{
	EFX_BUG_ON_PARANOID(!(spec->flags & EFX_FILTER_FLAG_RX));

	/* This cannot currently be combined with other filtering */
	if (spec->type != EFX_FILTER_UNSPEC)
		return -EPROTONOSUPPORT;

	if (vid == EFX_FILTER_VID_UNSPEC) {
		spec->type = EFX_FILTER_MAC_WILD;
		spec->data[0] = 0;
	} else {
		spec->type = EFX_FILTER_MAC_FULL;
		spec->data[0] = vid;
	}

	spec->data[1] = addr[2] << 24 | addr[3] << 16 | addr[4] << 8 | addr[5];
	spec->data[2] = addr[0] << 8 | addr[1];
	return 0;
}

/* Build a filter entry and return its n-tuple key. */
static u32 efx_filter_build(efx_oword_t *filter, struct efx_filter_spec *spec)
{
	u32 data3;

	switch (efx_filter_spec_table_id(spec)) {
	case EFX_FILTER_TABLE_RX_IP: {
		bool is_udp = (spec->type == EFX_FILTER_UDP_FULL ||
			       spec->type == EFX_FILTER_UDP_WILD);
		EFX_POPULATE_OWORD_7(
			*filter,
			FRF_BZ_RSS_EN,
			!!(spec->flags & EFX_FILTER_FLAG_RX_RSS),
			FRF_BZ_SCATTER_EN,
			!!(spec->flags & EFX_FILTER_FLAG_RX_SCATTER),
			FRF_BZ_TCP_UDP, is_udp,
			FRF_BZ_RXQ_ID, spec->dmaq_id,
			EFX_DWORD_2, spec->data[2],
			EFX_DWORD_1, spec->data[1],
			EFX_DWORD_0, spec->data[0]);
		data3 = is_udp;
		break;
	}

	case EFX_FILTER_TABLE_RX_MAC: {
		bool is_wild = spec->type == EFX_FILTER_MAC_WILD;
		EFX_POPULATE_OWORD_8(
			*filter,
			FRF_CZ_RMFT_RSS_EN,
			!!(spec->flags & EFX_FILTER_FLAG_RX_RSS),
			FRF_CZ_RMFT_SCATTER_EN,
			!!(spec->flags & EFX_FILTER_FLAG_RX_SCATTER),
			FRF_CZ_RMFT_IP_OVERRIDE,
			!!(spec->flags & EFX_FILTER_FLAG_RX_OVERRIDE_IP),
			FRF_CZ_RMFT_RXQ_ID, spec->dmaq_id,
			FRF_CZ_RMFT_WILDCARD_MATCH, is_wild,
			FRF_CZ_RMFT_DEST_MAC_HI, spec->data[2],
			FRF_CZ_RMFT_DEST_MAC_LO, spec->data[1],
			FRF_CZ_RMFT_VLAN_ID, spec->data[0]);
		data3 = is_wild;
		break;
	}

	default:
		BUG();
	}

	return spec->data[0] ^ spec->data[1] ^ spec->data[2] ^ data3;
}

static bool efx_filter_equal(const struct efx_filter_spec *left,
			     const struct efx_filter_spec *right)
{
	if (left->type != right->type ||
	    memcmp(left->data, right->data, sizeof(left->data)))
		return false;

	return true;
}

static int efx_filter_search(struct efx_filter_table *table,
			     struct efx_filter_spec *spec, u32 key,
			     bool for_insert, int *depth_required)
{
	unsigned hash, incr, filter_idx, depth;
	struct efx_filter_spec *cmp;

	hash = efx_filter_hash(key);
	incr = efx_filter_increment(key);

	for (depth = 1, filter_idx = hash & (table->size - 1);
	     depth <= FILTER_CTL_SRCH_MAX &&
		     test_bit(filter_idx, table->used_bitmap);
	     ++depth) {
		cmp = &table->spec[filter_idx];
		if (efx_filter_equal(spec, cmp))
			goto found;
		filter_idx = (filter_idx + incr) & (table->size - 1);
	}
	if (!for_insert)
		return -ENOENT;
	if (depth > FILTER_CTL_SRCH_MAX)
		return -EBUSY;
found:
	*depth_required = depth;
	return filter_idx;
}

/* Construct/deconstruct external filter IDs */

static inline int
efx_filter_make_id(enum efx_filter_table_id table_id, unsigned index)
{
	return table_id << 16 | index;
}

/**
 * efx_filter_insert_filter - add or replace a filter
 * @efx: NIC in which to insert the filter
 * @spec: Specification for the filter
 * @replace: Flag for whether the specified filter may replace a filter
 *	with an identical match expression and equal or lower priority
 *
 * On success, return the filter ID.
 * On failure, return a negative error code.
 */
int efx_filter_insert_filter(struct efx_nic *efx, struct efx_filter_spec *spec,
			     bool replace)
{
	struct efx_filter_state *state = efx->filter_state;
	struct efx_filter_table *table = efx_filter_spec_table(state, spec);
	struct efx_filter_spec *saved_spec;
	efx_oword_t filter;
	int filter_idx, depth;
	u32 key;
	int rc;

	if (!table || table->size == 0)
		return -EINVAL;

	key = efx_filter_build(&filter, spec);

	netif_vdbg(efx, hw, efx->net_dev,
		   "%s: type %d search_depth=%d", __func__, spec->type,
		   table->search_depth[spec->type]);

	spin_lock_bh(&state->lock);

	rc = efx_filter_search(table, spec, key, true, &depth);
	if (rc < 0)
		goto out;
	filter_idx = rc;
	BUG_ON(filter_idx >= table->size);
	saved_spec = &table->spec[filter_idx];

	if (test_bit(filter_idx, table->used_bitmap)) {
		/* Should we replace the existing filter? */
		if (!replace) {
			rc = -EEXIST;
			goto out;
		}
		if (spec->priority < saved_spec->priority) {
			rc = -EPERM;
			goto out;
		}
	} else {
		__set_bit(filter_idx, table->used_bitmap);
		++table->used;
	}
	*saved_spec = *spec;

	if (table->search_depth[spec->type] < depth) {
		table->search_depth[spec->type] = depth;
		efx_filter_push_rx_limits(efx);
	}

	efx_writeo(efx, &filter, table->offset + table->step * filter_idx);

	netif_vdbg(efx, hw, efx->net_dev,
		   "%s: filter type %d index %d rxq %u set",
		   __func__, spec->type, filter_idx, spec->dmaq_id);
	rc = efx_filter_make_id(table->id, filter_idx);

out:
	spin_unlock_bh(&state->lock);
	return rc;
}

static void efx_filter_table_clear_entry(struct efx_nic *efx,
					 struct efx_filter_table *table,
					 int filter_idx)
{
	static efx_oword_t filter;

	if (test_bit(filter_idx, table->used_bitmap)) {
		__clear_bit(filter_idx, table->used_bitmap);
		--table->used;
		memset(&table->spec[filter_idx], 0, sizeof(table->spec[0]));

		efx_writeo(efx, &filter,
			   table->offset + table->step * filter_idx);
	}
}

/**
 * efx_filter_remove_filter - remove a filter by specification
 * @efx: NIC from which to remove the filter
 * @spec: Specification for the filter
 *
 * On success, return zero.
 * On failure, return a negative error code.
 */
int efx_filter_remove_filter(struct efx_nic *efx, struct efx_filter_spec *spec)
{
	struct efx_filter_state *state = efx->filter_state;
	struct efx_filter_table *table = efx_filter_spec_table(state, spec);
	struct efx_filter_spec *saved_spec;
	efx_oword_t filter;
	int filter_idx, depth;
	u32 key;
	int rc;

	if (!table)
		return -EINVAL;

	key = efx_filter_build(&filter, spec);

	spin_lock_bh(&state->lock);

	rc = efx_filter_search(table, spec, key, false, &depth);
	if (rc < 0)
		goto out;
	filter_idx = rc;
	saved_spec = &table->spec[filter_idx];

	if (spec->priority < saved_spec->priority) {
		rc = -EPERM;
		goto out;
	}

	efx_filter_table_clear_entry(efx, table, filter_idx);
	if (table->used == 0)
		efx_filter_table_reset_search_depth(table);
	rc = 0;

out:
	spin_unlock_bh(&state->lock);
	return rc;
}

static void efx_filter_table_clear(struct efx_nic *efx,
				   enum efx_filter_table_id table_id,
				   enum efx_filter_priority priority)
{
	struct efx_filter_state *state = efx->filter_state;
	struct efx_filter_table *table = &state->table[table_id];
	int filter_idx;

	spin_lock_bh(&state->lock);

	for (filter_idx = 0; filter_idx < table->size; ++filter_idx)
		if (table->spec[filter_idx].priority <= priority)
			efx_filter_table_clear_entry(efx, table, filter_idx);
	if (table->used == 0)
		efx_filter_table_reset_search_depth(table);

	spin_unlock_bh(&state->lock);
}

/**
 * efx_filter_clear_rx - remove RX filters by priority
 * @efx: NIC from which to remove the filters
 * @priority: Maximum priority to remove
 */
void efx_filter_clear_rx(struct efx_nic *efx, enum efx_filter_priority priority)
{
	efx_filter_table_clear(efx, EFX_FILTER_TABLE_RX_IP, priority);
	efx_filter_table_clear(efx, EFX_FILTER_TABLE_RX_MAC, priority);
}

/* Restore filter stater after reset */
void efx_restore_filters(struct efx_nic *efx)
{
	struct efx_filter_state *state = efx->filter_state;
	enum efx_filter_table_id table_id;
	struct efx_filter_table *table;
	efx_oword_t filter;
	int filter_idx;

	spin_lock_bh(&state->lock);

	for (table_id = 0; table_id < EFX_FILTER_TABLE_COUNT; table_id++) {
		table = &state->table[table_id];
		for (filter_idx = 0; filter_idx < table->size; filter_idx++) {
			if (!test_bit(filter_idx, table->used_bitmap))
				continue;
			efx_filter_build(&filter, &table->spec[filter_idx]);
			efx_writeo(efx, &filter,
				   table->offset + table->step * filter_idx);
		}
	}

	efx_filter_push_rx_limits(efx);

	spin_unlock_bh(&state->lock);
}

int efx_probe_filters(struct efx_nic *efx)
{
	struct efx_filter_state *state;
	struct efx_filter_table *table;
	unsigned table_id;

	state = kzalloc(sizeof(*efx->filter_state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;
	efx->filter_state = state;

	spin_lock_init(&state->lock);

	if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0) {
		table = &state->table[EFX_FILTER_TABLE_RX_IP];
		table->id = EFX_FILTER_TABLE_RX_IP;
		table->offset = FR_BZ_RX_FILTER_TBL0;
		table->size = FR_BZ_RX_FILTER_TBL0_ROWS;
		table->step = FR_BZ_RX_FILTER_TBL0_STEP;
	}

	if (efx_nic_rev(efx) >= EFX_REV_SIENA_A0) {
		table = &state->table[EFX_FILTER_TABLE_RX_MAC];
		table->id = EFX_FILTER_TABLE_RX_MAC;
		table->offset = FR_CZ_RX_MAC_FILTER_TBL0;
		table->size = FR_CZ_RX_MAC_FILTER_TBL0_ROWS;
		table->step = FR_CZ_RX_MAC_FILTER_TBL0_STEP;
	}

	for (table_id = 0; table_id < EFX_FILTER_TABLE_COUNT; table_id++) {
		table = &state->table[table_id];
		if (table->size == 0)
			continue;
		table->used_bitmap = kcalloc(BITS_TO_LONGS(table->size),
					     sizeof(unsigned long),
					     GFP_KERNEL);
		if (!table->used_bitmap)
			goto fail;
		table->spec = vzalloc(table->size * sizeof(*table->spec));
		if (!table->spec)
			goto fail;
	}

	return 0;

fail:
	efx_remove_filters(efx);
	return -ENOMEM;
}

void efx_remove_filters(struct efx_nic *efx)
{
	struct efx_filter_state *state = efx->filter_state;
	enum efx_filter_table_id table_id;

	for (table_id = 0; table_id < EFX_FILTER_TABLE_COUNT; table_id++) {
		kfree(state->table[table_id].used_bitmap);
		vfree(state->table[table_id].spec);
	}
	kfree(state);
}
