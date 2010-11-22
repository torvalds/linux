/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2010 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

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

struct efx_filter_table {
	u32		offset;		/* address of table relative to BAR */
	unsigned	size;		/* number of entries */
	unsigned	step;		/* step between entries */
	unsigned	used;		/* number currently used */
	unsigned long	*used_bitmap;
	struct efx_filter_spec *spec;
};

struct efx_filter_state {
	spinlock_t	lock;
	struct efx_filter_table table[EFX_FILTER_TABLE_COUNT];
	unsigned	search_depth[EFX_FILTER_TYPE_COUNT];
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
efx_filter_type_table_id(enum efx_filter_type type)
{
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_IP != (EFX_FILTER_RX_TCP_FULL >> 2));
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_IP != (EFX_FILTER_RX_TCP_WILD >> 2));
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_IP != (EFX_FILTER_RX_UDP_FULL >> 2));
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_IP != (EFX_FILTER_RX_UDP_WILD >> 2));
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_MAC != (EFX_FILTER_RX_MAC_FULL >> 2));
	BUILD_BUG_ON(EFX_FILTER_TABLE_RX_MAC != (EFX_FILTER_RX_MAC_WILD >> 2));
	return type >> 2;
}

static void
efx_filter_table_reset_search_depth(struct efx_filter_state *state,
				    enum efx_filter_table_id table_id)
{
	memset(state->search_depth + (table_id << 2), 0,
	       sizeof(state->search_depth[0]) << 2);
}

static void efx_filter_push_rx_limits(struct efx_nic *efx)
{
	struct efx_filter_state *state = efx->filter_state;
	efx_oword_t filter_ctl;

	efx_reado(efx, &filter_ctl, FR_BZ_RX_FILTER_CTL);

	EFX_SET_OWORD_FIELD(filter_ctl, FRF_BZ_TCP_FULL_SRCH_LIMIT,
			    state->search_depth[EFX_FILTER_RX_TCP_FULL] +
			    FILTER_CTL_SRCH_FUDGE_FULL);
	EFX_SET_OWORD_FIELD(filter_ctl, FRF_BZ_TCP_WILD_SRCH_LIMIT,
			    state->search_depth[EFX_FILTER_RX_TCP_WILD] +
			    FILTER_CTL_SRCH_FUDGE_WILD);
	EFX_SET_OWORD_FIELD(filter_ctl, FRF_BZ_UDP_FULL_SRCH_LIMIT,
			    state->search_depth[EFX_FILTER_RX_UDP_FULL] +
			    FILTER_CTL_SRCH_FUDGE_FULL);
	EFX_SET_OWORD_FIELD(filter_ctl, FRF_BZ_UDP_WILD_SRCH_LIMIT,
			    state->search_depth[EFX_FILTER_RX_UDP_WILD] +
			    FILTER_CTL_SRCH_FUDGE_WILD);

	if (state->table[EFX_FILTER_TABLE_RX_MAC].size) {
		EFX_SET_OWORD_FIELD(
			filter_ctl, FRF_CZ_ETHERNET_FULL_SEARCH_LIMIT,
			state->search_depth[EFX_FILTER_RX_MAC_FULL] +
			FILTER_CTL_SRCH_FUDGE_FULL);
		EFX_SET_OWORD_FIELD(
			filter_ctl, FRF_CZ_ETHERNET_WILDCARD_SEARCH_LIMIT,
			state->search_depth[EFX_FILTER_RX_MAC_WILD] +
			FILTER_CTL_SRCH_FUDGE_WILD);
	}

	efx_writeo(efx, &filter_ctl, FR_BZ_RX_FILTER_CTL);
}

/* Build a filter entry and return its n-tuple key. */
static u32 efx_filter_build(efx_oword_t *filter, struct efx_filter_spec *spec)
{
	u32 data3;

	switch (efx_filter_type_table_id(spec->type)) {
	case EFX_FILTER_TABLE_RX_IP: {
		bool is_udp = (spec->type == EFX_FILTER_RX_UDP_FULL ||
			       spec->type == EFX_FILTER_RX_UDP_WILD);
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
		bool is_wild = spec->type == EFX_FILTER_RX_MAC_WILD;
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

/**
 * efx_filter_insert_filter - add or replace a filter
 * @efx: NIC in which to insert the filter
 * @spec: Specification for the filter
 * @replace: Flag for whether the specified filter may replace a filter
 *	with an identical match expression and equal or lower priority
 *
 * On success, return the filter index within its table.
 * On failure, return a negative error code.
 */
int efx_filter_insert_filter(struct efx_nic *efx, struct efx_filter_spec *spec,
			     bool replace)
{
	struct efx_filter_state *state = efx->filter_state;
	enum efx_filter_table_id table_id =
		efx_filter_type_table_id(spec->type);
	struct efx_filter_table *table = &state->table[table_id];
	struct efx_filter_spec *saved_spec;
	efx_oword_t filter;
	int filter_idx, depth;
	u32 key;
	int rc;

	if (table->size == 0)
		return -EINVAL;

	key = efx_filter_build(&filter, spec);

	netif_vdbg(efx, hw, efx->net_dev,
		   "%s: type %d search_depth=%d", __func__, spec->type,
		   state->search_depth[spec->type]);

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

	if (state->search_depth[spec->type] < depth) {
		state->search_depth[spec->type] = depth;
		efx_filter_push_rx_limits(efx);
	}

	efx_writeo(efx, &filter, table->offset + table->step * filter_idx);

	netif_vdbg(efx, hw, efx->net_dev,
		   "%s: filter type %d index %d rxq %u set",
		   __func__, spec->type, filter_idx, spec->dmaq_id);

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
	enum efx_filter_table_id table_id =
		efx_filter_type_table_id(spec->type);
	struct efx_filter_table *table = &state->table[table_id];
	struct efx_filter_spec *saved_spec;
	efx_oword_t filter;
	int filter_idx, depth;
	u32 key;
	int rc;

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
		efx_filter_table_reset_search_depth(state, table_id);
	rc = 0;

out:
	spin_unlock_bh(&state->lock);
	return rc;
}

/**
 * efx_filter_table_clear - remove filters from a table by priority
 * @efx: NIC from which to remove the filters
 * @table_id: Table from which to remove the filters
 * @priority: Maximum priority to remove
 */
void efx_filter_table_clear(struct efx_nic *efx,
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
		efx_filter_table_reset_search_depth(state, table_id);

	spin_unlock_bh(&state->lock);
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
		table->offset = FR_BZ_RX_FILTER_TBL0;
		table->size = FR_BZ_RX_FILTER_TBL0_ROWS;
		table->step = FR_BZ_RX_FILTER_TBL0_STEP;
	}

	if (efx_nic_rev(efx) >= EFX_REV_SIENA_A0) {
		table = &state->table[EFX_FILTER_TABLE_RX_MAC];
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
