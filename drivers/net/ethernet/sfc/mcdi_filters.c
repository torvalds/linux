// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2018 Solarflare Communications Inc.
 * Copyright 2019-2020 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "mcdi_filters.h"
#include "mcdi.h"
#include "nic.h"
#include "rx_common.h"

/* The maximum size of a shared RSS context */
/* TODO: this should really be from the mcdi protocol export */
#define EFX_EF10_MAX_SHARED_RSS_CONTEXT_SIZE 64UL

#define EFX_EF10_FILTER_ID_INVALID 0xffff

/* An arbitrary search limit for the software hash table */
#define EFX_EF10_FILTER_SEARCH_LIMIT 200

static struct efx_filter_spec *
efx_mcdi_filter_entry_spec(const struct efx_mcdi_filter_table *table,
			   unsigned int filter_idx)
{
	return (struct efx_filter_spec *)(table->entry[filter_idx].spec &
					  ~EFX_EF10_FILTER_FLAGS);
}

static unsigned int
efx_mcdi_filter_entry_flags(const struct efx_mcdi_filter_table *table,
			   unsigned int filter_idx)
{
	return table->entry[filter_idx].spec & EFX_EF10_FILTER_FLAGS;
}

static u32 efx_mcdi_filter_get_unsafe_id(u32 filter_id)
{
	WARN_ON_ONCE(filter_id == EFX_EF10_FILTER_ID_INVALID);
	return filter_id & (EFX_MCDI_FILTER_TBL_ROWS - 1);
}

static unsigned int efx_mcdi_filter_get_unsafe_pri(u32 filter_id)
{
	return filter_id / (EFX_MCDI_FILTER_TBL_ROWS * 2);
}

static u32 efx_mcdi_filter_make_filter_id(unsigned int pri, u16 idx)
{
	return pri * EFX_MCDI_FILTER_TBL_ROWS * 2 + idx;
}

/*
 * Decide whether a filter should be exclusive or else should allow
 * delivery to additional recipients.  Currently we decide that
 * filters for specific local unicast MAC and IP addresses are
 * exclusive.
 */
static bool efx_mcdi_filter_is_exclusive(const struct efx_filter_spec *spec)
{
	if (spec->match_flags & EFX_FILTER_MATCH_LOC_MAC &&
	    !is_multicast_ether_addr(spec->loc_mac))
		return true;

	if ((spec->match_flags &
	     (EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_LOC_HOST)) ==
	    (EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_LOC_HOST)) {
		if (spec->ether_type == htons(ETH_P_IP) &&
		    !ipv4_is_multicast(spec->loc_host[0]))
			return true;
		if (spec->ether_type == htons(ETH_P_IPV6) &&
		    ((const u8 *)spec->loc_host)[0] != 0xff)
			return true;
	}

	return false;
}

static void
efx_mcdi_filter_set_entry(struct efx_mcdi_filter_table *table,
			  unsigned int filter_idx,
			  const struct efx_filter_spec *spec,
			  unsigned int flags)
{
	table->entry[filter_idx].spec =	(unsigned long)spec | flags;
}

static void
efx_mcdi_filter_push_prep_set_match_fields(struct efx_nic *efx,
					   const struct efx_filter_spec *spec,
					   efx_dword_t *inbuf)
{
	enum efx_encap_type encap_type = efx_filter_get_encap_type(spec);
	u32 match_fields = 0, uc_match, mc_match;

	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
		       efx_mcdi_filter_is_exclusive(spec) ?
		       MC_CMD_FILTER_OP_IN_OP_INSERT :
		       MC_CMD_FILTER_OP_IN_OP_SUBSCRIBE);

	/*
	 * Convert match flags and values.  Unlike almost
	 * everything else in MCDI, these fields are in
	 * network byte order.
	 */
#define COPY_VALUE(value, mcdi_field)					     \
	do {							     \
		match_fields |=					     \
			1 << MC_CMD_FILTER_OP_IN_MATCH_ ##	     \
			mcdi_field ## _LBN;			     \
		BUILD_BUG_ON(					     \
			MC_CMD_FILTER_OP_IN_ ## mcdi_field ## _LEN < \
			sizeof(value));				     \
		memcpy(MCDI_PTR(inbuf, FILTER_OP_IN_ ##	mcdi_field), \
		       &value, sizeof(value));			     \
	} while (0)
#define COPY_FIELD(gen_flag, gen_field, mcdi_field)			     \
	if (spec->match_flags & EFX_FILTER_MATCH_ ## gen_flag) {     \
		COPY_VALUE(spec->gen_field, mcdi_field);	     \
	}
	/*
	 * Handle encap filters first.  They will always be mismatch
	 * (unknown UC or MC) filters
	 */
	if (encap_type) {
		/*
		 * ether_type and outer_ip_proto need to be variables
		 * because COPY_VALUE wants to memcpy them
		 */
		__be16 ether_type =
			htons(encap_type & EFX_ENCAP_FLAG_IPV6 ?
			      ETH_P_IPV6 : ETH_P_IP);
		u8 vni_type = MC_CMD_FILTER_OP_EXT_IN_VNI_TYPE_GENEVE;
		u8 outer_ip_proto;

		switch (encap_type & EFX_ENCAP_TYPES_MASK) {
		case EFX_ENCAP_TYPE_VXLAN:
			vni_type = MC_CMD_FILTER_OP_EXT_IN_VNI_TYPE_VXLAN;
			fallthrough;
		case EFX_ENCAP_TYPE_GENEVE:
			COPY_VALUE(ether_type, ETHER_TYPE);
			outer_ip_proto = IPPROTO_UDP;
			COPY_VALUE(outer_ip_proto, IP_PROTO);
			/*
			 * We always need to set the type field, even
			 * though we're not matching on the TNI.
			 */
			MCDI_POPULATE_DWORD_1(inbuf,
				FILTER_OP_EXT_IN_VNI_OR_VSID,
				FILTER_OP_EXT_IN_VNI_TYPE,
				vni_type);
			break;
		case EFX_ENCAP_TYPE_NVGRE:
			COPY_VALUE(ether_type, ETHER_TYPE);
			outer_ip_proto = IPPROTO_GRE;
			COPY_VALUE(outer_ip_proto, IP_PROTO);
			break;
		default:
			WARN_ON(1);
		}

		uc_match = MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_UNKNOWN_UCAST_DST_LBN;
		mc_match = MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_UNKNOWN_MCAST_DST_LBN;
	} else {
		uc_match = MC_CMD_FILTER_OP_EXT_IN_MATCH_UNKNOWN_UCAST_DST_LBN;
		mc_match = MC_CMD_FILTER_OP_EXT_IN_MATCH_UNKNOWN_MCAST_DST_LBN;
	}

	if (spec->match_flags & EFX_FILTER_MATCH_LOC_MAC_IG)
		match_fields |=
			is_multicast_ether_addr(spec->loc_mac) ?
			1 << mc_match :
			1 << uc_match;
	COPY_FIELD(REM_HOST, rem_host, SRC_IP);
	COPY_FIELD(LOC_HOST, loc_host, DST_IP);
	COPY_FIELD(REM_MAC, rem_mac, SRC_MAC);
	COPY_FIELD(REM_PORT, rem_port, SRC_PORT);
	COPY_FIELD(LOC_MAC, loc_mac, DST_MAC);
	COPY_FIELD(LOC_PORT, loc_port, DST_PORT);
	COPY_FIELD(ETHER_TYPE, ether_type, ETHER_TYPE);
	COPY_FIELD(INNER_VID, inner_vid, INNER_VLAN);
	COPY_FIELD(OUTER_VID, outer_vid, OUTER_VLAN);
	COPY_FIELD(IP_PROTO, ip_proto, IP_PROTO);
#undef COPY_FIELD
#undef COPY_VALUE
	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_MATCH_FIELDS,
		       match_fields);
}

static void efx_mcdi_filter_push_prep(struct efx_nic *efx,
				      const struct efx_filter_spec *spec,
				      efx_dword_t *inbuf, u64 handle,
				      struct efx_rss_context_priv *ctx,
				      bool replacing)
{
	u32 flags = spec->flags;

	memset(inbuf, 0, MC_CMD_FILTER_OP_EXT_IN_LEN);

	/* If RSS filter, caller better have given us an RSS context */
	if (flags & EFX_FILTER_FLAG_RX_RSS) {
		/*
		 * We don't have the ability to return an error, so we'll just
		 * log a warning and disable RSS for the filter.
		 */
		if (WARN_ON_ONCE(!ctx))
			flags &= ~EFX_FILTER_FLAG_RX_RSS;
		else if (WARN_ON_ONCE(ctx->context_id == EFX_MCDI_RSS_CONTEXT_INVALID))
			flags &= ~EFX_FILTER_FLAG_RX_RSS;
	}

	if (replacing) {
		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
			       MC_CMD_FILTER_OP_IN_OP_REPLACE);
		MCDI_SET_QWORD(inbuf, FILTER_OP_IN_HANDLE, handle);
	} else {
		efx_mcdi_filter_push_prep_set_match_fields(efx, spec, inbuf);
	}

	if (flags & EFX_FILTER_FLAG_VPORT_ID)
		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_PORT_ID, spec->vport_id);
	else
		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_PORT_ID, efx->vport_id);
	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_RX_DEST,
		       spec->dmaq_id == EFX_FILTER_RX_DMAQ_ID_DROP ?
		       MC_CMD_FILTER_OP_IN_RX_DEST_DROP :
		       MC_CMD_FILTER_OP_IN_RX_DEST_HOST);
	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_TX_DOMAIN, 0);
	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_TX_DEST,
		       MC_CMD_FILTER_OP_IN_TX_DEST_DEFAULT);
	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_RX_QUEUE,
		       spec->dmaq_id == EFX_FILTER_RX_DMAQ_ID_DROP ?
		       0 : spec->dmaq_id);
	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_RX_MODE,
		       (flags & EFX_FILTER_FLAG_RX_RSS) ?
		       MC_CMD_FILTER_OP_IN_RX_MODE_RSS :
		       MC_CMD_FILTER_OP_IN_RX_MODE_SIMPLE);
	if (flags & EFX_FILTER_FLAG_RX_RSS)
		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_RX_CONTEXT, ctx->context_id);
}

static int efx_mcdi_filter_push(struct efx_nic *efx,
				const struct efx_filter_spec *spec, u64 *handle,
				struct efx_rss_context_priv *ctx, bool replacing)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FILTER_OP_EXT_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_FILTER_OP_EXT_OUT_LEN);
	size_t outlen;
	int rc;

	efx_mcdi_filter_push_prep(efx, spec, inbuf, *handle, ctx, replacing);
	rc = efx_mcdi_rpc_quiet(efx, MC_CMD_FILTER_OP, inbuf, sizeof(inbuf),
				outbuf, sizeof(outbuf), &outlen);
	if (rc && spec->priority != EFX_FILTER_PRI_HINT)
		efx_mcdi_display_error(efx, MC_CMD_FILTER_OP, sizeof(inbuf),
				       outbuf, outlen, rc);
	if (rc == 0)
		*handle = MCDI_QWORD(outbuf, FILTER_OP_OUT_HANDLE);
	if (rc == -ENOSPC)
		rc = -EBUSY; /* to match efx_farch_filter_insert() */
	return rc;
}

static u32 efx_mcdi_filter_mcdi_flags_from_spec(const struct efx_filter_spec *spec)
{
	enum efx_encap_type encap_type = efx_filter_get_encap_type(spec);
	unsigned int match_flags = spec->match_flags;
	unsigned int uc_match, mc_match;
	u32 mcdi_flags = 0;

#define MAP_FILTER_TO_MCDI_FLAG(gen_flag, mcdi_field, encap) {		\
		unsigned int  old_match_flags = match_flags;		\
		match_flags &= ~EFX_FILTER_MATCH_ ## gen_flag;		\
		if (match_flags != old_match_flags)			\
			mcdi_flags |=					\
				(1 << ((encap) ?			\
				       MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_ ## \
				       mcdi_field ## _LBN :		\
				       MC_CMD_FILTER_OP_EXT_IN_MATCH_ ##\
				       mcdi_field ## _LBN));		\
	}
	/* inner or outer based on encap type */
	MAP_FILTER_TO_MCDI_FLAG(REM_HOST, SRC_IP, encap_type);
	MAP_FILTER_TO_MCDI_FLAG(LOC_HOST, DST_IP, encap_type);
	MAP_FILTER_TO_MCDI_FLAG(REM_MAC, SRC_MAC, encap_type);
	MAP_FILTER_TO_MCDI_FLAG(REM_PORT, SRC_PORT, encap_type);
	MAP_FILTER_TO_MCDI_FLAG(LOC_MAC, DST_MAC, encap_type);
	MAP_FILTER_TO_MCDI_FLAG(LOC_PORT, DST_PORT, encap_type);
	MAP_FILTER_TO_MCDI_FLAG(ETHER_TYPE, ETHER_TYPE, encap_type);
	MAP_FILTER_TO_MCDI_FLAG(IP_PROTO, IP_PROTO, encap_type);
	/* always outer */
	MAP_FILTER_TO_MCDI_FLAG(INNER_VID, INNER_VLAN, false);
	MAP_FILTER_TO_MCDI_FLAG(OUTER_VID, OUTER_VLAN, false);
#undef MAP_FILTER_TO_MCDI_FLAG

	/* special handling for encap type, and mismatch */
	if (encap_type) {
		match_flags &= ~EFX_FILTER_MATCH_ENCAP_TYPE;
		mcdi_flags |=
			(1 << MC_CMD_FILTER_OP_EXT_IN_MATCH_ETHER_TYPE_LBN);
		mcdi_flags |= (1 << MC_CMD_FILTER_OP_EXT_IN_MATCH_IP_PROTO_LBN);

		uc_match = MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_UNKNOWN_UCAST_DST_LBN;
		mc_match = MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_UNKNOWN_MCAST_DST_LBN;
	} else {
		uc_match = MC_CMD_FILTER_OP_EXT_IN_MATCH_UNKNOWN_UCAST_DST_LBN;
		mc_match = MC_CMD_FILTER_OP_EXT_IN_MATCH_UNKNOWN_MCAST_DST_LBN;
	}

	if (match_flags & EFX_FILTER_MATCH_LOC_MAC_IG) {
		match_flags &= ~EFX_FILTER_MATCH_LOC_MAC_IG;
		mcdi_flags |=
			is_multicast_ether_addr(spec->loc_mac) ?
			1 << mc_match :
			1 << uc_match;
	}

	/* Did we map them all? */
	WARN_ON_ONCE(match_flags);

	return mcdi_flags;
}

static int efx_mcdi_filter_pri(struct efx_mcdi_filter_table *table,
			       const struct efx_filter_spec *spec)
{
	u32 mcdi_flags = efx_mcdi_filter_mcdi_flags_from_spec(spec);
	unsigned int match_pri;

	for (match_pri = 0;
	     match_pri < table->rx_match_count;
	     match_pri++)
		if (table->rx_match_mcdi_flags[match_pri] == mcdi_flags)
			return match_pri;

	return -EPROTONOSUPPORT;
}

static s32 efx_mcdi_filter_insert_locked(struct efx_nic *efx,
					 struct efx_filter_spec *spec,
					 bool replace_equal)
{
	DECLARE_BITMAP(mc_rem_map, EFX_EF10_FILTER_SEARCH_LIMIT);
	struct efx_rss_context_priv *ctx = NULL;
	struct efx_mcdi_filter_table *table;
	struct efx_filter_spec *saved_spec;
	unsigned int match_pri, hash;
	unsigned int priv_flags;
	bool rss_locked = false;
	bool replacing = false;
	unsigned int depth, i;
	int ins_index = -1;
	DEFINE_WAIT(wait);
	bool is_mc_recip;
	s32 rc;

	WARN_ON(!rwsem_is_locked(&efx->filter_sem));
	table = efx->filter_state;
	down_write(&table->lock);

	/* For now, only support RX filters */
	if ((spec->flags & (EFX_FILTER_FLAG_RX | EFX_FILTER_FLAG_TX)) !=
	    EFX_FILTER_FLAG_RX) {
		rc = -EINVAL;
		goto out_unlock;
	}

	rc = efx_mcdi_filter_pri(table, spec);
	if (rc < 0)
		goto out_unlock;
	match_pri = rc;

	hash = efx_filter_spec_hash(spec);
	is_mc_recip = efx_filter_is_mc_recipient(spec);
	if (is_mc_recip)
		bitmap_zero(mc_rem_map, EFX_EF10_FILTER_SEARCH_LIMIT);

	if (spec->flags & EFX_FILTER_FLAG_RX_RSS) {
		mutex_lock(&efx->net_dev->ethtool->rss_lock);
		rss_locked = true;
		if (spec->rss_context)
			ctx = efx_find_rss_context_entry(efx, spec->rss_context);
		else
			ctx = &efx->rss_context.priv;
		if (!ctx) {
			rc = -ENOENT;
			goto out_unlock;
		}
		if (ctx->context_id == EFX_MCDI_RSS_CONTEXT_INVALID) {
			rc = -EOPNOTSUPP;
			goto out_unlock;
		}
	}

	/* Find any existing filters with the same match tuple or
	 * else a free slot to insert at.
	 */
	for (depth = 1; depth < EFX_EF10_FILTER_SEARCH_LIMIT; depth++) {
		i = (hash + depth) & (EFX_MCDI_FILTER_TBL_ROWS - 1);
		saved_spec = efx_mcdi_filter_entry_spec(table, i);

		if (!saved_spec) {
			if (ins_index < 0)
				ins_index = i;
		} else if (efx_filter_spec_equal(spec, saved_spec)) {
			if (spec->priority < saved_spec->priority &&
			    spec->priority != EFX_FILTER_PRI_AUTO) {
				rc = -EPERM;
				goto out_unlock;
			}
			if (!is_mc_recip) {
				/* This is the only one */
				if (spec->priority ==
				    saved_spec->priority &&
				    !replace_equal) {
					rc = -EEXIST;
					goto out_unlock;
				}
				ins_index = i;
				break;
			} else if (spec->priority >
				   saved_spec->priority ||
				   (spec->priority ==
				    saved_spec->priority &&
				    replace_equal)) {
				if (ins_index < 0)
					ins_index = i;
				else
					__set_bit(depth, mc_rem_map);
			}
		}
	}

	/* Once we reach the maximum search depth, use the first suitable
	 * slot, or return -EBUSY if there was none
	 */
	if (ins_index < 0) {
		rc = -EBUSY;
		goto out_unlock;
	}

	/* Create a software table entry if necessary. */
	saved_spec = efx_mcdi_filter_entry_spec(table, ins_index);
	if (saved_spec) {
		if (spec->priority == EFX_FILTER_PRI_AUTO &&
		    saved_spec->priority >= EFX_FILTER_PRI_AUTO) {
			/* Just make sure it won't be removed */
			if (saved_spec->priority > EFX_FILTER_PRI_AUTO)
				saved_spec->flags |= EFX_FILTER_FLAG_RX_OVER_AUTO;
			table->entry[ins_index].spec &=
				~EFX_EF10_FILTER_FLAG_AUTO_OLD;
			rc = ins_index;
			goto out_unlock;
		}
		replacing = true;
		priv_flags = efx_mcdi_filter_entry_flags(table, ins_index);
	} else {
		saved_spec = kmalloc(sizeof(*spec), GFP_ATOMIC);
		if (!saved_spec) {
			rc = -ENOMEM;
			goto out_unlock;
		}
		*saved_spec = *spec;
		priv_flags = 0;
	}
	efx_mcdi_filter_set_entry(table, ins_index, saved_spec, priv_flags);

	/* Actually insert the filter on the HW */
	rc = efx_mcdi_filter_push(efx, spec, &table->entry[ins_index].handle,
				  ctx, replacing);

	if (rc == -EINVAL && efx->must_realloc_vis)
		/* The MC rebooted under us, causing it to reject our filter
		 * insertion as pointing to an invalid VI (spec->dmaq_id).
		 */
		rc = -EAGAIN;

	/* Finalise the software table entry */
	if (rc == 0) {
		if (replacing) {
			/* Update the fields that may differ */
			if (saved_spec->priority == EFX_FILTER_PRI_AUTO)
				saved_spec->flags |=
					EFX_FILTER_FLAG_RX_OVER_AUTO;
			saved_spec->priority = spec->priority;
			saved_spec->flags &= EFX_FILTER_FLAG_RX_OVER_AUTO;
			saved_spec->flags |= spec->flags;
			saved_spec->rss_context = spec->rss_context;
			saved_spec->dmaq_id = spec->dmaq_id;
			saved_spec->vport_id = spec->vport_id;
		}
	} else if (!replacing) {
		kfree(saved_spec);
		saved_spec = NULL;
	} else {
		/* We failed to replace, so the old filter is still present.
		 * Roll back the software table to reflect this.  In fact the
		 * efx_mcdi_filter_set_entry() call below will do the right
		 * thing, so nothing extra is needed here.
		 */
	}
	efx_mcdi_filter_set_entry(table, ins_index, saved_spec, priv_flags);

	/* Remove and finalise entries for lower-priority multicast
	 * recipients
	 */
	if (is_mc_recip) {
		MCDI_DECLARE_BUF(inbuf, MC_CMD_FILTER_OP_EXT_IN_LEN);
		unsigned int depth, i;

		memset(inbuf, 0, sizeof(inbuf));

		for (depth = 0; depth < EFX_EF10_FILTER_SEARCH_LIMIT; depth++) {
			if (!test_bit(depth, mc_rem_map))
				continue;

			i = (hash + depth) & (EFX_MCDI_FILTER_TBL_ROWS - 1);
			saved_spec = efx_mcdi_filter_entry_spec(table, i);
			priv_flags = efx_mcdi_filter_entry_flags(table, i);

			if (rc == 0) {
				MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
					       MC_CMD_FILTER_OP_IN_OP_UNSUBSCRIBE);
				MCDI_SET_QWORD(inbuf, FILTER_OP_IN_HANDLE,
					       table->entry[i].handle);
				rc = efx_mcdi_rpc(efx, MC_CMD_FILTER_OP,
						  inbuf, sizeof(inbuf),
						  NULL, 0, NULL);
			}

			if (rc == 0) {
				kfree(saved_spec);
				saved_spec = NULL;
				priv_flags = 0;
			}
			efx_mcdi_filter_set_entry(table, i, saved_spec,
						  priv_flags);
		}
	}

	/* If successful, return the inserted filter ID */
	if (rc == 0)
		rc = efx_mcdi_filter_make_filter_id(match_pri, ins_index);

out_unlock:
	if (rss_locked)
		mutex_unlock(&efx->net_dev->ethtool->rss_lock);
	up_write(&table->lock);
	return rc;
}

s32 efx_mcdi_filter_insert(struct efx_nic *efx, struct efx_filter_spec *spec,
			   bool replace_equal)
{
	s32 ret;

	down_read(&efx->filter_sem);
	ret = efx_mcdi_filter_insert_locked(efx, spec, replace_equal);
	up_read(&efx->filter_sem);

	return ret;
}

/*
 * Remove a filter.
 * If !by_index, remove by ID
 * If by_index, remove by index
 * Filter ID may come from userland and must be range-checked.
 * Caller must hold efx->filter_sem for read, and efx->filter_state->lock
 * for write.
 */
static int efx_mcdi_filter_remove_internal(struct efx_nic *efx,
					   unsigned int priority_mask,
					   u32 filter_id, bool by_index)
{
	unsigned int filter_idx = efx_mcdi_filter_get_unsafe_id(filter_id);
	struct efx_mcdi_filter_table *table = efx->filter_state;
	MCDI_DECLARE_BUF(inbuf,
			 MC_CMD_FILTER_OP_IN_HANDLE_OFST +
			 MC_CMD_FILTER_OP_IN_HANDLE_LEN);
	struct efx_filter_spec *spec;
	DEFINE_WAIT(wait);
	int rc;

	spec = efx_mcdi_filter_entry_spec(table, filter_idx);
	if (!spec ||
	    (!by_index &&
	     efx_mcdi_filter_pri(table, spec) !=
	     efx_mcdi_filter_get_unsafe_pri(filter_id)))
		return -ENOENT;

	if (spec->flags & EFX_FILTER_FLAG_RX_OVER_AUTO &&
	    priority_mask == (1U << EFX_FILTER_PRI_AUTO)) {
		/* Just remove flags */
		spec->flags &= ~EFX_FILTER_FLAG_RX_OVER_AUTO;
		table->entry[filter_idx].spec &= ~EFX_EF10_FILTER_FLAG_AUTO_OLD;
		return 0;
	}

	if (!(priority_mask & (1U << spec->priority)))
		return -ENOENT;

	if (spec->flags & EFX_FILTER_FLAG_RX_OVER_AUTO) {
		/* Reset to an automatic filter */

		struct efx_filter_spec new_spec = *spec;

		new_spec.priority = EFX_FILTER_PRI_AUTO;
		new_spec.flags = (EFX_FILTER_FLAG_RX |
				  (efx_rss_active(&efx->rss_context.priv) ?
				   EFX_FILTER_FLAG_RX_RSS : 0));
		new_spec.dmaq_id = 0;
		new_spec.rss_context = 0;
		rc = efx_mcdi_filter_push(efx, &new_spec,
					  &table->entry[filter_idx].handle,
					  &efx->rss_context.priv,
					  true);

		if (rc == 0)
			*spec = new_spec;
	} else {
		/* Really remove the filter */

		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
			       efx_mcdi_filter_is_exclusive(spec) ?
			       MC_CMD_FILTER_OP_IN_OP_REMOVE :
			       MC_CMD_FILTER_OP_IN_OP_UNSUBSCRIBE);
		MCDI_SET_QWORD(inbuf, FILTER_OP_IN_HANDLE,
			       table->entry[filter_idx].handle);
		rc = efx_mcdi_rpc_quiet(efx, MC_CMD_FILTER_OP,
					inbuf, sizeof(inbuf), NULL, 0, NULL);

		if ((rc == 0) || (rc == -ENOENT)) {
			/* Filter removed OK or didn't actually exist */
			kfree(spec);
			efx_mcdi_filter_set_entry(table, filter_idx, NULL, 0);
		} else {
			efx_mcdi_display_error(efx, MC_CMD_FILTER_OP,
					       MC_CMD_FILTER_OP_EXT_IN_LEN,
					       NULL, 0, rc);
		}
	}

	return rc;
}

/* Remove filters that weren't renewed. */
static void efx_mcdi_filter_remove_old(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	int remove_failed = 0;
	int remove_noent = 0;
	int rc;
	int i;

	down_write(&table->lock);
	for (i = 0; i < EFX_MCDI_FILTER_TBL_ROWS; i++) {
		if (READ_ONCE(table->entry[i].spec) &
		    EFX_EF10_FILTER_FLAG_AUTO_OLD) {
			rc = efx_mcdi_filter_remove_internal(efx,
					1U << EFX_FILTER_PRI_AUTO, i, true);
			if (rc == -ENOENT)
				remove_noent++;
			else if (rc)
				remove_failed++;
		}
	}
	up_write(&table->lock);

	if (remove_failed)
		netif_info(efx, drv, efx->net_dev,
			   "%s: failed to remove %d filters\n",
			   __func__, remove_failed);
	if (remove_noent)
		netif_info(efx, drv, efx->net_dev,
			   "%s: failed to remove %d non-existent filters\n",
			   __func__, remove_noent);
}

int efx_mcdi_filter_remove_safe(struct efx_nic *efx,
				enum efx_filter_priority priority,
				u32 filter_id)
{
	struct efx_mcdi_filter_table *table;
	int rc;

	down_read(&efx->filter_sem);
	table = efx->filter_state;
	down_write(&table->lock);
	rc = efx_mcdi_filter_remove_internal(efx, 1U << priority, filter_id,
					     false);
	up_write(&table->lock);
	up_read(&efx->filter_sem);
	return rc;
}

/* Caller must hold efx->filter_sem for read */
static void efx_mcdi_filter_remove_unsafe(struct efx_nic *efx,
					  enum efx_filter_priority priority,
					  u32 filter_id)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;

	if (filter_id == EFX_EF10_FILTER_ID_INVALID)
		return;

	down_write(&table->lock);
	efx_mcdi_filter_remove_internal(efx, 1U << priority, filter_id,
					true);
	up_write(&table->lock);
}

int efx_mcdi_filter_get_safe(struct efx_nic *efx,
			     enum efx_filter_priority priority,
			     u32 filter_id, struct efx_filter_spec *spec)
{
	unsigned int filter_idx = efx_mcdi_filter_get_unsafe_id(filter_id);
	const struct efx_filter_spec *saved_spec;
	struct efx_mcdi_filter_table *table;
	int rc;

	down_read(&efx->filter_sem);
	table = efx->filter_state;
	down_read(&table->lock);
	saved_spec = efx_mcdi_filter_entry_spec(table, filter_idx);
	if (saved_spec && saved_spec->priority == priority &&
	    efx_mcdi_filter_pri(table, saved_spec) ==
	    efx_mcdi_filter_get_unsafe_pri(filter_id)) {
		*spec = *saved_spec;
		rc = 0;
	} else {
		rc = -ENOENT;
	}
	up_read(&table->lock);
	up_read(&efx->filter_sem);
	return rc;
}

static int efx_mcdi_filter_insert_addr_list(struct efx_nic *efx,
					    struct efx_mcdi_filter_vlan *vlan,
					    bool multicast, bool rollback)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	struct efx_mcdi_dev_addr *addr_list;
	enum efx_filter_flags filter_flags;
	struct efx_filter_spec spec;
	u8 baddr[ETH_ALEN];
	unsigned int i, j;
	int addr_count;
	u16 *ids;
	int rc;

	if (multicast) {
		addr_list = table->dev_mc_list;
		addr_count = table->dev_mc_count;
		ids = vlan->mc;
	} else {
		addr_list = table->dev_uc_list;
		addr_count = table->dev_uc_count;
		ids = vlan->uc;
	}

	filter_flags = efx_rss_active(&efx->rss_context.priv) ? EFX_FILTER_FLAG_RX_RSS : 0;

	/* Insert/renew filters */
	for (i = 0; i < addr_count; i++) {
		EFX_WARN_ON_PARANOID(ids[i] != EFX_EF10_FILTER_ID_INVALID);
		efx_filter_init_rx(&spec, EFX_FILTER_PRI_AUTO, filter_flags, 0);
		efx_filter_set_eth_local(&spec, vlan->vid, addr_list[i].addr);
		rc = efx_mcdi_filter_insert_locked(efx, &spec, true);
		if (rc < 0) {
			if (rollback) {
				netif_info(efx, drv, efx->net_dev,
					   "efx_mcdi_filter_insert failed rc=%d\n",
					   rc);
				/* Fall back to promiscuous */
				for (j = 0; j < i; j++) {
					efx_mcdi_filter_remove_unsafe(
						efx, EFX_FILTER_PRI_AUTO,
						ids[j]);
					ids[j] = EFX_EF10_FILTER_ID_INVALID;
				}
				return rc;
			} else {
				/* keep invalid ID, and carry on */
			}
		} else {
			ids[i] = efx_mcdi_filter_get_unsafe_id(rc);
		}
	}

	if (multicast && rollback) {
		/* Also need an Ethernet broadcast filter */
		EFX_WARN_ON_PARANOID(vlan->default_filters[EFX_EF10_BCAST] !=
				     EFX_EF10_FILTER_ID_INVALID);
		efx_filter_init_rx(&spec, EFX_FILTER_PRI_AUTO, filter_flags, 0);
		eth_broadcast_addr(baddr);
		efx_filter_set_eth_local(&spec, vlan->vid, baddr);
		rc = efx_mcdi_filter_insert_locked(efx, &spec, true);
		if (rc < 0) {
			netif_warn(efx, drv, efx->net_dev,
				   "Broadcast filter insert failed rc=%d\n", rc);
			/* Fall back to promiscuous */
			for (j = 0; j < i; j++) {
				efx_mcdi_filter_remove_unsafe(
					efx, EFX_FILTER_PRI_AUTO,
					ids[j]);
				ids[j] = EFX_EF10_FILTER_ID_INVALID;
			}
			return rc;
		} else {
			vlan->default_filters[EFX_EF10_BCAST] =
				efx_mcdi_filter_get_unsafe_id(rc);
		}
	}

	return 0;
}

static int efx_mcdi_filter_insert_def(struct efx_nic *efx,
				      struct efx_mcdi_filter_vlan *vlan,
				      enum efx_encap_type encap_type,
				      bool multicast, bool rollback)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	enum efx_filter_flags filter_flags;
	struct efx_filter_spec spec;
	u8 baddr[ETH_ALEN];
	int rc;
	u16 *id;

	filter_flags = efx_rss_active(&efx->rss_context.priv) ? EFX_FILTER_FLAG_RX_RSS : 0;

	efx_filter_init_rx(&spec, EFX_FILTER_PRI_AUTO, filter_flags, 0);

	if (multicast)
		efx_filter_set_mc_def(&spec);
	else
		efx_filter_set_uc_def(&spec);

	if (encap_type) {
		if (efx_has_cap(efx, VXLAN_NVGRE))
			efx_filter_set_encap_type(&spec, encap_type);
		else
			/*
			 * don't insert encap filters on non-supporting
			 * platforms. ID will be left as INVALID.
			 */
			return 0;
	}

	if (vlan->vid != EFX_FILTER_VID_UNSPEC)
		efx_filter_set_eth_local(&spec, vlan->vid, NULL);

	rc = efx_mcdi_filter_insert_locked(efx, &spec, true);
	if (rc < 0) {
		const char *um = multicast ? "Multicast" : "Unicast";
		const char *encap_name = "";
		const char *encap_ipv = "";

		if ((encap_type & EFX_ENCAP_TYPES_MASK) ==
		    EFX_ENCAP_TYPE_VXLAN)
			encap_name = "VXLAN ";
		else if ((encap_type & EFX_ENCAP_TYPES_MASK) ==
			 EFX_ENCAP_TYPE_NVGRE)
			encap_name = "NVGRE ";
		else if ((encap_type & EFX_ENCAP_TYPES_MASK) ==
			 EFX_ENCAP_TYPE_GENEVE)
			encap_name = "GENEVE ";
		if (encap_type & EFX_ENCAP_FLAG_IPV6)
			encap_ipv = "IPv6 ";
		else if (encap_type)
			encap_ipv = "IPv4 ";

		/*
		 * unprivileged functions can't insert mismatch filters
		 * for encapsulated or unicast traffic, so downgrade
		 * those warnings to debug.
		 */
		netif_cond_dbg(efx, drv, efx->net_dev,
			       rc == -EPERM && (encap_type || !multicast), warn,
			       "%s%s%s mismatch filter insert failed rc=%d\n",
			       encap_name, encap_ipv, um, rc);
	} else if (multicast) {
		/* mapping from encap types to default filter IDs (multicast) */
		static enum efx_mcdi_filter_default_filters map[] = {
			[EFX_ENCAP_TYPE_NONE] = EFX_EF10_MCDEF,
			[EFX_ENCAP_TYPE_VXLAN] = EFX_EF10_VXLAN4_MCDEF,
			[EFX_ENCAP_TYPE_NVGRE] = EFX_EF10_NVGRE4_MCDEF,
			[EFX_ENCAP_TYPE_GENEVE] = EFX_EF10_GENEVE4_MCDEF,
			[EFX_ENCAP_TYPE_VXLAN | EFX_ENCAP_FLAG_IPV6] =
				EFX_EF10_VXLAN6_MCDEF,
			[EFX_ENCAP_TYPE_NVGRE | EFX_ENCAP_FLAG_IPV6] =
				EFX_EF10_NVGRE6_MCDEF,
			[EFX_ENCAP_TYPE_GENEVE | EFX_ENCAP_FLAG_IPV6] =
				EFX_EF10_GENEVE6_MCDEF,
		};

		/* quick bounds check (BCAST result impossible) */
		BUILD_BUG_ON(EFX_EF10_BCAST != 0);
		if (encap_type >= ARRAY_SIZE(map) || map[encap_type] == 0) {
			WARN_ON(1);
			return -EINVAL;
		}
		/* then follow map */
		id = &vlan->default_filters[map[encap_type]];

		EFX_WARN_ON_PARANOID(*id != EFX_EF10_FILTER_ID_INVALID);
		*id = efx_mcdi_filter_get_unsafe_id(rc);
		if (!table->mc_chaining && !encap_type) {
			/* Also need an Ethernet broadcast filter */
			efx_filter_init_rx(&spec, EFX_FILTER_PRI_AUTO,
					   filter_flags, 0);
			eth_broadcast_addr(baddr);
			efx_filter_set_eth_local(&spec, vlan->vid, baddr);
			rc = efx_mcdi_filter_insert_locked(efx, &spec, true);
			if (rc < 0) {
				netif_warn(efx, drv, efx->net_dev,
					   "Broadcast filter insert failed rc=%d\n",
					   rc);
				if (rollback) {
					/* Roll back the mc_def filter */
					efx_mcdi_filter_remove_unsafe(
							efx, EFX_FILTER_PRI_AUTO,
							*id);
					*id = EFX_EF10_FILTER_ID_INVALID;
					return rc;
				}
			} else {
				EFX_WARN_ON_PARANOID(
					vlan->default_filters[EFX_EF10_BCAST] !=
					EFX_EF10_FILTER_ID_INVALID);
				vlan->default_filters[EFX_EF10_BCAST] =
					efx_mcdi_filter_get_unsafe_id(rc);
			}
		}
		rc = 0;
	} else {
		/* mapping from encap types to default filter IDs (unicast) */
		static enum efx_mcdi_filter_default_filters map[] = {
			[EFX_ENCAP_TYPE_NONE] = EFX_EF10_UCDEF,
			[EFX_ENCAP_TYPE_VXLAN] = EFX_EF10_VXLAN4_UCDEF,
			[EFX_ENCAP_TYPE_NVGRE] = EFX_EF10_NVGRE4_UCDEF,
			[EFX_ENCAP_TYPE_GENEVE] = EFX_EF10_GENEVE4_UCDEF,
			[EFX_ENCAP_TYPE_VXLAN | EFX_ENCAP_FLAG_IPV6] =
				EFX_EF10_VXLAN6_UCDEF,
			[EFX_ENCAP_TYPE_NVGRE | EFX_ENCAP_FLAG_IPV6] =
				EFX_EF10_NVGRE6_UCDEF,
			[EFX_ENCAP_TYPE_GENEVE | EFX_ENCAP_FLAG_IPV6] =
				EFX_EF10_GENEVE6_UCDEF,
		};

		/* quick bounds check (BCAST result impossible) */
		BUILD_BUG_ON(EFX_EF10_BCAST != 0);
		if (encap_type >= ARRAY_SIZE(map) || map[encap_type] == 0) {
			WARN_ON(1);
			return -EINVAL;
		}
		/* then follow map */
		id = &vlan->default_filters[map[encap_type]];
		EFX_WARN_ON_PARANOID(*id != EFX_EF10_FILTER_ID_INVALID);
		*id = rc;
		rc = 0;
	}
	return rc;
}

/*
 * Caller must hold efx->filter_sem for read if race against
 * efx_mcdi_filter_table_remove() is possible
 */
static void efx_mcdi_filter_vlan_sync_rx_mode(struct efx_nic *efx,
					      struct efx_mcdi_filter_vlan *vlan)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;

	/*
	 * Do not install unspecified VID if VLAN filtering is enabled.
	 * Do not install all specified VIDs if VLAN filtering is disabled.
	 */
	if ((vlan->vid == EFX_FILTER_VID_UNSPEC) == table->vlan_filter)
		return;

	/* Insert/renew unicast filters */
	if (table->uc_promisc) {
		efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_NONE,
					   false, false);
		efx_mcdi_filter_insert_addr_list(efx, vlan, false, false);
	} else {
		/*
		 * If any of the filters failed to insert, fall back to
		 * promiscuous mode - add in the uc_def filter.  But keep
		 * our individual unicast filters.
		 */
		if (efx_mcdi_filter_insert_addr_list(efx, vlan, false, false))
			efx_mcdi_filter_insert_def(efx, vlan,
						   EFX_ENCAP_TYPE_NONE,
						   false, false);
	}
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_VXLAN,
				   false, false);
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_VXLAN |
					      EFX_ENCAP_FLAG_IPV6,
				   false, false);
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_NVGRE,
				   false, false);
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_NVGRE |
					      EFX_ENCAP_FLAG_IPV6,
				   false, false);
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_GENEVE,
				   false, false);
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_GENEVE |
					      EFX_ENCAP_FLAG_IPV6,
				   false, false);

	/*
	 * Insert/renew multicast filters
	 *
	 * If changing promiscuous state with cascaded multicast filters, remove
	 * old filters first, so that packets are dropped rather than duplicated
	 */
	if (table->mc_chaining && table->mc_promisc_last != table->mc_promisc)
		efx_mcdi_filter_remove_old(efx);
	if (table->mc_promisc) {
		if (table->mc_chaining) {
			/*
			 * If we failed to insert promiscuous filters, rollback
			 * and fall back to individual multicast filters
			 */
			if (efx_mcdi_filter_insert_def(efx, vlan,
						       EFX_ENCAP_TYPE_NONE,
						       true, true)) {
				/* Changing promisc state, so remove old filters */
				efx_mcdi_filter_remove_old(efx);
				efx_mcdi_filter_insert_addr_list(efx, vlan,
								 true, false);
			}
		} else {
			/*
			 * If we failed to insert promiscuous filters, don't
			 * rollback.  Regardless, also insert the mc_list,
			 * unless it's incomplete due to overflow
			 */
			efx_mcdi_filter_insert_def(efx, vlan,
						   EFX_ENCAP_TYPE_NONE,
						   true, false);
			if (!table->mc_overflow)
				efx_mcdi_filter_insert_addr_list(efx, vlan,
								 true, false);
		}
	} else {
		/*
		 * If any filters failed to insert, rollback and fall back to
		 * promiscuous mode - mc_def filter and maybe broadcast.  If
		 * that fails, roll back again and insert as many of our
		 * individual multicast filters as we can.
		 */
		if (efx_mcdi_filter_insert_addr_list(efx, vlan, true, true)) {
			/* Changing promisc state, so remove old filters */
			if (table->mc_chaining)
				efx_mcdi_filter_remove_old(efx);
			if (efx_mcdi_filter_insert_def(efx, vlan,
						       EFX_ENCAP_TYPE_NONE,
						       true, true))
				efx_mcdi_filter_insert_addr_list(efx, vlan,
								 true, false);
		}
	}
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_VXLAN,
				   true, false);
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_VXLAN |
					      EFX_ENCAP_FLAG_IPV6,
				   true, false);
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_NVGRE,
				   true, false);
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_NVGRE |
					      EFX_ENCAP_FLAG_IPV6,
				   true, false);
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_GENEVE,
				   true, false);
	efx_mcdi_filter_insert_def(efx, vlan, EFX_ENCAP_TYPE_GENEVE |
					      EFX_ENCAP_FLAG_IPV6,
				   true, false);
}

int efx_mcdi_filter_clear_rx(struct efx_nic *efx,
			     enum efx_filter_priority priority)
{
	struct efx_mcdi_filter_table *table;
	unsigned int priority_mask;
	unsigned int i;
	int rc;

	priority_mask = (((1U << (priority + 1)) - 1) &
			 ~(1U << EFX_FILTER_PRI_AUTO));

	down_read(&efx->filter_sem);
	table = efx->filter_state;
	down_write(&table->lock);
	for (i = 0; i < EFX_MCDI_FILTER_TBL_ROWS; i++) {
		rc = efx_mcdi_filter_remove_internal(efx, priority_mask,
						     i, true);
		if (rc && rc != -ENOENT)
			break;
		rc = 0;
	}

	up_write(&table->lock);
	up_read(&efx->filter_sem);
	return rc;
}

u32 efx_mcdi_filter_count_rx_used(struct efx_nic *efx,
				 enum efx_filter_priority priority)
{
	struct efx_mcdi_filter_table *table;
	unsigned int filter_idx;
	s32 count = 0;

	down_read(&efx->filter_sem);
	table = efx->filter_state;
	down_read(&table->lock);
	for (filter_idx = 0; filter_idx < EFX_MCDI_FILTER_TBL_ROWS; filter_idx++) {
		if (table->entry[filter_idx].spec &&
		    efx_mcdi_filter_entry_spec(table, filter_idx)->priority ==
		    priority)
			++count;
	}
	up_read(&table->lock);
	up_read(&efx->filter_sem);
	return count;
}

u32 efx_mcdi_filter_get_rx_id_limit(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;

	return table->rx_match_count * EFX_MCDI_FILTER_TBL_ROWS * 2;
}

s32 efx_mcdi_filter_get_rx_ids(struct efx_nic *efx,
			       enum efx_filter_priority priority,
			       u32 *buf, u32 size)
{
	struct efx_mcdi_filter_table *table;
	struct efx_filter_spec *spec;
	unsigned int filter_idx;
	s32 count = 0;

	down_read(&efx->filter_sem);
	table = efx->filter_state;
	down_read(&table->lock);

	for (filter_idx = 0; filter_idx < EFX_MCDI_FILTER_TBL_ROWS; filter_idx++) {
		spec = efx_mcdi_filter_entry_spec(table, filter_idx);
		if (spec && spec->priority == priority) {
			if (count == size) {
				count = -EMSGSIZE;
				break;
			}
			buf[count++] =
				efx_mcdi_filter_make_filter_id(
					efx_mcdi_filter_pri(table, spec),
					filter_idx);
		}
	}
	up_read(&table->lock);
	up_read(&efx->filter_sem);
	return count;
}

static int efx_mcdi_filter_match_flags_from_mcdi(bool encap, u32 mcdi_flags)
{
	int match_flags = 0;

#define MAP_FLAG(gen_flag, mcdi_field) do {				\
		u32 old_mcdi_flags = mcdi_flags;			\
		mcdi_flags &= ~(1 << MC_CMD_FILTER_OP_EXT_IN_MATCH_ ##	\
				     mcdi_field ## _LBN);		\
		if (mcdi_flags != old_mcdi_flags)			\
			match_flags |= EFX_FILTER_MATCH_ ## gen_flag;	\
	} while (0)

	if (encap) {
		/* encap filters must specify encap type */
		match_flags |= EFX_FILTER_MATCH_ENCAP_TYPE;
		/* and imply ethertype and ip proto */
		mcdi_flags &=
			~(1 << MC_CMD_FILTER_OP_EXT_IN_MATCH_IP_PROTO_LBN);
		mcdi_flags &=
			~(1 << MC_CMD_FILTER_OP_EXT_IN_MATCH_ETHER_TYPE_LBN);
		/* VLAN tags refer to the outer packet */
		MAP_FLAG(INNER_VID, INNER_VLAN);
		MAP_FLAG(OUTER_VID, OUTER_VLAN);
		/* everything else refers to the inner packet */
		MAP_FLAG(LOC_MAC_IG, IFRM_UNKNOWN_UCAST_DST);
		MAP_FLAG(LOC_MAC_IG, IFRM_UNKNOWN_MCAST_DST);
		MAP_FLAG(REM_HOST, IFRM_SRC_IP);
		MAP_FLAG(LOC_HOST, IFRM_DST_IP);
		MAP_FLAG(REM_MAC, IFRM_SRC_MAC);
		MAP_FLAG(REM_PORT, IFRM_SRC_PORT);
		MAP_FLAG(LOC_MAC, IFRM_DST_MAC);
		MAP_FLAG(LOC_PORT, IFRM_DST_PORT);
		MAP_FLAG(ETHER_TYPE, IFRM_ETHER_TYPE);
		MAP_FLAG(IP_PROTO, IFRM_IP_PROTO);
	} else {
		MAP_FLAG(LOC_MAC_IG, UNKNOWN_UCAST_DST);
		MAP_FLAG(LOC_MAC_IG, UNKNOWN_MCAST_DST);
		MAP_FLAG(REM_HOST, SRC_IP);
		MAP_FLAG(LOC_HOST, DST_IP);
		MAP_FLAG(REM_MAC, SRC_MAC);
		MAP_FLAG(REM_PORT, SRC_PORT);
		MAP_FLAG(LOC_MAC, DST_MAC);
		MAP_FLAG(LOC_PORT, DST_PORT);
		MAP_FLAG(ETHER_TYPE, ETHER_TYPE);
		MAP_FLAG(INNER_VID, INNER_VLAN);
		MAP_FLAG(OUTER_VID, OUTER_VLAN);
		MAP_FLAG(IP_PROTO, IP_PROTO);
	}
#undef MAP_FLAG

	/* Did we map them all? */
	if (mcdi_flags)
		return -EINVAL;

	return match_flags;
}

bool efx_mcdi_filter_match_supported(struct efx_mcdi_filter_table *table,
				     bool encap,
				     enum efx_filter_match_flags match_flags)
{
	unsigned int match_pri;
	int mf;

	for (match_pri = 0;
	     match_pri < table->rx_match_count;
	     match_pri++) {
		mf = efx_mcdi_filter_match_flags_from_mcdi(encap,
				table->rx_match_mcdi_flags[match_pri]);
		if (mf == match_flags)
			return true;
	}

	return false;
}

static int
efx_mcdi_filter_table_probe_matches(struct efx_nic *efx,
				    struct efx_mcdi_filter_table *table,
				    bool encap)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_GET_PARSER_DISP_INFO_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_PARSER_DISP_INFO_OUT_LENMAX);
	unsigned int pd_match_pri, pd_match_count;
	size_t outlen;
	int rc;

	/* Find out which RX filter types are supported, and their priorities */
	MCDI_SET_DWORD(inbuf, GET_PARSER_DISP_INFO_IN_OP,
		       encap ?
		       MC_CMD_GET_PARSER_DISP_INFO_IN_OP_GET_SUPPORTED_ENCAP_RX_MATCHES :
		       MC_CMD_GET_PARSER_DISP_INFO_IN_OP_GET_SUPPORTED_RX_MATCHES);
	rc = efx_mcdi_rpc(efx, MC_CMD_GET_PARSER_DISP_INFO,
			  inbuf, sizeof(inbuf), outbuf, sizeof(outbuf),
			  &outlen);
	if (rc)
		return rc;

	pd_match_count = MCDI_VAR_ARRAY_LEN(
		outlen, GET_PARSER_DISP_INFO_OUT_SUPPORTED_MATCHES);

	for (pd_match_pri = 0; pd_match_pri < pd_match_count; pd_match_pri++) {
		u32 mcdi_flags =
			MCDI_ARRAY_DWORD(
				outbuf,
				GET_PARSER_DISP_INFO_OUT_SUPPORTED_MATCHES,
				pd_match_pri);
		rc = efx_mcdi_filter_match_flags_from_mcdi(encap, mcdi_flags);
		if (rc < 0) {
			netif_dbg(efx, probe, efx->net_dev,
				  "%s: fw flags %#x pri %u not supported in driver\n",
				  __func__, mcdi_flags, pd_match_pri);
		} else {
			netif_dbg(efx, probe, efx->net_dev,
				  "%s: fw flags %#x pri %u supported as driver flags %#x pri %u\n",
				  __func__, mcdi_flags, pd_match_pri,
				  rc, table->rx_match_count);
			table->rx_match_mcdi_flags[table->rx_match_count] = mcdi_flags;
			table->rx_match_count++;
		}
	}

	return 0;
}

int efx_mcdi_filter_table_probe(struct efx_nic *efx, bool multicast_chaining)
{
	struct net_device *net_dev = efx->net_dev;
	struct efx_mcdi_filter_table *table;
	int rc;

	if (!efx_rwsem_assert_write_locked(&efx->filter_sem))
		return -EINVAL;

	if (efx->filter_state) /* already probed */
		return 0;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	table->mc_chaining = multicast_chaining;
	table->rx_match_count = 0;
	rc = efx_mcdi_filter_table_probe_matches(efx, table, false);
	if (rc)
		goto fail;
	if (efx_has_cap(efx, VXLAN_NVGRE))
		rc = efx_mcdi_filter_table_probe_matches(efx, table, true);
	if (rc)
		goto fail;
	if ((efx_supported_features(efx) & NETIF_F_HW_VLAN_CTAG_FILTER) &&
	    !(efx_mcdi_filter_match_supported(table, false,
		(EFX_FILTER_MATCH_OUTER_VID | EFX_FILTER_MATCH_LOC_MAC)) &&
	      efx_mcdi_filter_match_supported(table, false,
		(EFX_FILTER_MATCH_OUTER_VID | EFX_FILTER_MATCH_LOC_MAC_IG)))) {
		netif_info(efx, probe, net_dev,
			   "VLAN filters are not supported in this firmware variant\n");
		net_dev->features &= ~NETIF_F_HW_VLAN_CTAG_FILTER;
		efx->fixed_features &= ~NETIF_F_HW_VLAN_CTAG_FILTER;
		net_dev->hw_features &= ~NETIF_F_HW_VLAN_CTAG_FILTER;
	}

	table->entry = vzalloc(array_size(EFX_MCDI_FILTER_TBL_ROWS,
					  sizeof(*table->entry)));
	if (!table->entry) {
		rc = -ENOMEM;
		goto fail;
	}

	table->mc_promisc_last = false;
	table->vlan_filter =
		!!(efx->net_dev->features & NETIF_F_HW_VLAN_CTAG_FILTER);
	INIT_LIST_HEAD(&table->vlan_list);
	init_rwsem(&table->lock);

	efx->filter_state = table;

	return 0;
fail:
	kfree(table);
	return rc;
}

void efx_mcdi_filter_table_reset_mc_allocations(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;

	if (table) {
		table->must_restore_filters = true;
		table->must_restore_rss_contexts = true;
	}
}

/*
 * Caller must hold efx->filter_sem for read if race against
 * efx_mcdi_filter_table_remove() is possible
 */
void efx_mcdi_filter_table_restore(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	unsigned int invalid_filters = 0, failed = 0;
	struct efx_mcdi_filter_vlan *vlan;
	struct efx_rss_context_priv *ctx;
	struct efx_filter_spec *spec;
	unsigned int filter_idx;
	u32 mcdi_flags;
	int match_pri;
	int rc, i;

	WARN_ON(!rwsem_is_locked(&efx->filter_sem));

	if (!table || !table->must_restore_filters)
		return;

	down_write(&table->lock);
	mutex_lock(&efx->net_dev->ethtool->rss_lock);

	for (filter_idx = 0; filter_idx < EFX_MCDI_FILTER_TBL_ROWS; filter_idx++) {
		spec = efx_mcdi_filter_entry_spec(table, filter_idx);
		if (!spec)
			continue;

		mcdi_flags = efx_mcdi_filter_mcdi_flags_from_spec(spec);
		match_pri = 0;
		while (match_pri < table->rx_match_count &&
		       table->rx_match_mcdi_flags[match_pri] != mcdi_flags)
			++match_pri;
		if (match_pri >= table->rx_match_count) {
			invalid_filters++;
			goto not_restored;
		}
		if (spec->rss_context)
			ctx = efx_find_rss_context_entry(efx, spec->rss_context);
		else
			ctx = &efx->rss_context.priv;
		if (spec->flags & EFX_FILTER_FLAG_RX_RSS) {
			if (!ctx) {
				netif_warn(efx, drv, efx->net_dev,
					   "Warning: unable to restore a filter with nonexistent RSS context %u.\n",
					   spec->rss_context);
				invalid_filters++;
				goto not_restored;
			}
			if (ctx->context_id == EFX_MCDI_RSS_CONTEXT_INVALID) {
				netif_warn(efx, drv, efx->net_dev,
					   "Warning: unable to restore a filter with RSS context %u as it was not created.\n",
					   spec->rss_context);
				invalid_filters++;
				goto not_restored;
			}
		}

		rc = efx_mcdi_filter_push(efx, spec,
					  &table->entry[filter_idx].handle,
					  ctx, false);
		if (rc)
			failed++;

		if (rc) {
not_restored:
			list_for_each_entry(vlan, &table->vlan_list, list)
				for (i = 0; i < EFX_EF10_NUM_DEFAULT_FILTERS; ++i)
					if (vlan->default_filters[i] == filter_idx)
						vlan->default_filters[i] =
							EFX_EF10_FILTER_ID_INVALID;

			kfree(spec);
			efx_mcdi_filter_set_entry(table, filter_idx, NULL, 0);
		}
	}

	mutex_unlock(&efx->net_dev->ethtool->rss_lock);
	up_write(&table->lock);

	/*
	 * This can happen validly if the MC's capabilities have changed, so
	 * is not an error.
	 */
	if (invalid_filters)
		netif_dbg(efx, drv, efx->net_dev,
			  "Did not restore %u filters that are now unsupported.\n",
			  invalid_filters);

	if (failed)
		netif_err(efx, hw, efx->net_dev,
			  "unable to restore %u filters\n", failed);
	else
		table->must_restore_filters = false;
}

void efx_mcdi_filter_table_down(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FILTER_OP_EXT_IN_LEN);
	struct efx_filter_spec *spec;
	unsigned int filter_idx;
	int rc;

	if (!table)
		return;

	efx_mcdi_filter_cleanup_vlans(efx);

	for (filter_idx = 0; filter_idx < EFX_MCDI_FILTER_TBL_ROWS; filter_idx++) {
		spec = efx_mcdi_filter_entry_spec(table, filter_idx);
		if (!spec)
			continue;

		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
			       efx_mcdi_filter_is_exclusive(spec) ?
			       MC_CMD_FILTER_OP_IN_OP_REMOVE :
			       MC_CMD_FILTER_OP_IN_OP_UNSUBSCRIBE);
		MCDI_SET_QWORD(inbuf, FILTER_OP_IN_HANDLE,
			       table->entry[filter_idx].handle);
		rc = efx_mcdi_rpc_quiet(efx, MC_CMD_FILTER_OP, inbuf,
					sizeof(inbuf), NULL, 0, NULL);
		if (rc)
			netif_info(efx, drv, efx->net_dev,
				   "%s: filter %04x remove failed\n",
				   __func__, filter_idx);
		kfree(spec);
	}
}

void efx_mcdi_filter_table_remove(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;

	efx_mcdi_filter_table_down(efx);

	efx->filter_state = NULL;
	/*
	 * If we were called without locking, then it's not safe to free
	 * the table as others might be using it.  So we just WARN, leak
	 * the memory, and potentially get an inconsistent filter table
	 * state.
	 * This should never actually happen.
	 */
	if (!efx_rwsem_assert_write_locked(&efx->filter_sem))
		return;

	if (!table)
		return;

	vfree(table->entry);
	kfree(table);
}

static void efx_mcdi_filter_mark_one_old(struct efx_nic *efx, uint16_t *id)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	unsigned int filter_idx;

	efx_rwsem_assert_write_locked(&table->lock);

	if (*id != EFX_EF10_FILTER_ID_INVALID) {
		filter_idx = efx_mcdi_filter_get_unsafe_id(*id);
		if (!table->entry[filter_idx].spec)
			netif_dbg(efx, drv, efx->net_dev,
				  "marked null spec old %04x:%04x\n", *id,
				  filter_idx);
		table->entry[filter_idx].spec |= EFX_EF10_FILTER_FLAG_AUTO_OLD;
		*id = EFX_EF10_FILTER_ID_INVALID;
	}
}

/* Mark old per-VLAN filters that may need to be removed */
static void _efx_mcdi_filter_vlan_mark_old(struct efx_nic *efx,
					   struct efx_mcdi_filter_vlan *vlan)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	unsigned int i;

	for (i = 0; i < table->dev_uc_count; i++)
		efx_mcdi_filter_mark_one_old(efx, &vlan->uc[i]);
	for (i = 0; i < table->dev_mc_count; i++)
		efx_mcdi_filter_mark_one_old(efx, &vlan->mc[i]);
	for (i = 0; i < EFX_EF10_NUM_DEFAULT_FILTERS; i++)
		efx_mcdi_filter_mark_one_old(efx, &vlan->default_filters[i]);
}

/*
 * Mark old filters that may need to be removed.
 * Caller must hold efx->filter_sem for read if race against
 * efx_mcdi_filter_table_remove() is possible
 */
static void efx_mcdi_filter_mark_old(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	struct efx_mcdi_filter_vlan *vlan;

	down_write(&table->lock);
	list_for_each_entry(vlan, &table->vlan_list, list)
		_efx_mcdi_filter_vlan_mark_old(efx, vlan);
	up_write(&table->lock);
}

int efx_mcdi_filter_add_vlan(struct efx_nic *efx, u16 vid)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	struct efx_mcdi_filter_vlan *vlan;
	unsigned int i;

	if (!efx_rwsem_assert_write_locked(&efx->filter_sem))
		return -EINVAL;

	vlan = efx_mcdi_filter_find_vlan(efx, vid);
	if (WARN_ON(vlan)) {
		netif_err(efx, drv, efx->net_dev,
			  "VLAN %u already added\n", vid);
		return -EALREADY;
	}

	vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan)
		return -ENOMEM;

	vlan->vid = vid;

	for (i = 0; i < ARRAY_SIZE(vlan->uc); i++)
		vlan->uc[i] = EFX_EF10_FILTER_ID_INVALID;
	for (i = 0; i < ARRAY_SIZE(vlan->mc); i++)
		vlan->mc[i] = EFX_EF10_FILTER_ID_INVALID;
	for (i = 0; i < EFX_EF10_NUM_DEFAULT_FILTERS; i++)
		vlan->default_filters[i] = EFX_EF10_FILTER_ID_INVALID;

	list_add_tail(&vlan->list, &table->vlan_list);

	if (efx_dev_registered(efx))
		efx_mcdi_filter_vlan_sync_rx_mode(efx, vlan);

	return 0;
}

static void efx_mcdi_filter_del_vlan_internal(struct efx_nic *efx,
					      struct efx_mcdi_filter_vlan *vlan)
{
	unsigned int i;

	/* See comment in efx_mcdi_filter_table_remove() */
	if (!efx_rwsem_assert_write_locked(&efx->filter_sem))
		return;

	list_del(&vlan->list);

	for (i = 0; i < ARRAY_SIZE(vlan->uc); i++)
		efx_mcdi_filter_remove_unsafe(efx, EFX_FILTER_PRI_AUTO,
					      vlan->uc[i]);
	for (i = 0; i < ARRAY_SIZE(vlan->mc); i++)
		efx_mcdi_filter_remove_unsafe(efx, EFX_FILTER_PRI_AUTO,
					      vlan->mc[i]);
	for (i = 0; i < EFX_EF10_NUM_DEFAULT_FILTERS; i++)
		if (vlan->default_filters[i] != EFX_EF10_FILTER_ID_INVALID)
			efx_mcdi_filter_remove_unsafe(efx, EFX_FILTER_PRI_AUTO,
						      vlan->default_filters[i]);

	kfree(vlan);
}

void efx_mcdi_filter_del_vlan(struct efx_nic *efx, u16 vid)
{
	struct efx_mcdi_filter_vlan *vlan;

	/* See comment in efx_mcdi_filter_table_remove() */
	if (!efx_rwsem_assert_write_locked(&efx->filter_sem))
		return;

	vlan = efx_mcdi_filter_find_vlan(efx, vid);
	if (!vlan) {
		netif_err(efx, drv, efx->net_dev,
			  "VLAN %u not found in filter state\n", vid);
		return;
	}

	efx_mcdi_filter_del_vlan_internal(efx, vlan);
}

struct efx_mcdi_filter_vlan *efx_mcdi_filter_find_vlan(struct efx_nic *efx,
						       u16 vid)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	struct efx_mcdi_filter_vlan *vlan;

	WARN_ON(!rwsem_is_locked(&efx->filter_sem));

	list_for_each_entry(vlan, &table->vlan_list, list) {
		if (vlan->vid == vid)
			return vlan;
	}

	return NULL;
}

void efx_mcdi_filter_cleanup_vlans(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	struct efx_mcdi_filter_vlan *vlan, *next_vlan;

	/* See comment in efx_mcdi_filter_table_remove() */
	if (!efx_rwsem_assert_write_locked(&efx->filter_sem))
		return;

	if (!table)
		return;

	list_for_each_entry_safe(vlan, next_vlan, &table->vlan_list, list)
		efx_mcdi_filter_del_vlan_internal(efx, vlan);
}

static void efx_mcdi_filter_uc_addr_list(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	struct net_device *net_dev = efx->net_dev;
	struct netdev_hw_addr *uc;
	unsigned int i;

	table->uc_promisc = !!(net_dev->flags & IFF_PROMISC);
	ether_addr_copy(table->dev_uc_list[0].addr, net_dev->dev_addr);
	i = 1;
	netdev_for_each_uc_addr(uc, net_dev) {
		if (i >= EFX_EF10_FILTER_DEV_UC_MAX) {
			table->uc_promisc = true;
			break;
		}
		ether_addr_copy(table->dev_uc_list[i].addr, uc->addr);
		i++;
	}

	table->dev_uc_count = i;
}

static void efx_mcdi_filter_mc_addr_list(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	struct net_device *net_dev = efx->net_dev;
	struct netdev_hw_addr *mc;
	unsigned int i;

	table->mc_overflow = false;
	table->mc_promisc = !!(net_dev->flags & (IFF_PROMISC | IFF_ALLMULTI));

	i = 0;
	netdev_for_each_mc_addr(mc, net_dev) {
		if (i >= EFX_EF10_FILTER_DEV_MC_MAX) {
			table->mc_promisc = true;
			table->mc_overflow = true;
			break;
		}
		ether_addr_copy(table->dev_mc_list[i].addr, mc->addr);
		i++;
	}

	table->dev_mc_count = i;
}

/*
 * Caller must hold efx->filter_sem for read if race against
 * efx_mcdi_filter_table_remove() is possible
 */
void efx_mcdi_filter_sync_rx_mode(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	struct net_device *net_dev = efx->net_dev;
	struct efx_mcdi_filter_vlan *vlan;
	bool vlan_filter;

	if (!efx_dev_registered(efx))
		return;

	if (!table)
		return;

	efx_mcdi_filter_mark_old(efx);

	/*
	 * Copy/convert the address lists; add the primary station
	 * address and broadcast address
	 */
	netif_addr_lock_bh(net_dev);
	efx_mcdi_filter_uc_addr_list(efx);
	efx_mcdi_filter_mc_addr_list(efx);
	netif_addr_unlock_bh(net_dev);

	/*
	 * If VLAN filtering changes, all old filters are finally removed.
	 * Do it in advance to avoid conflicts for unicast untagged and
	 * VLAN 0 tagged filters.
	 */
	vlan_filter = !!(net_dev->features & NETIF_F_HW_VLAN_CTAG_FILTER);
	if (table->vlan_filter != vlan_filter) {
		table->vlan_filter = vlan_filter;
		efx_mcdi_filter_remove_old(efx);
	}

	list_for_each_entry(vlan, &table->vlan_list, list)
		efx_mcdi_filter_vlan_sync_rx_mode(efx, vlan);

	efx_mcdi_filter_remove_old(efx);
	table->mc_promisc_last = table->mc_promisc;
}

#ifdef CONFIG_RFS_ACCEL

bool efx_mcdi_filter_rfs_expire_one(struct efx_nic *efx, u32 flow_id,
				    unsigned int filter_idx)
{
	struct efx_filter_spec *spec, saved_spec;
	struct efx_mcdi_filter_table *table;
	struct efx_arfs_rule *rule = NULL;
	bool ret = true, force = false;
	u16 arfs_id;

	down_read(&efx->filter_sem);
	table = efx->filter_state;
	down_write(&table->lock);
	spec = efx_mcdi_filter_entry_spec(table, filter_idx);

	if (!spec || spec->priority != EFX_FILTER_PRI_HINT)
		goto out_unlock;

	spin_lock_bh(&efx->rps_hash_lock);
	if (!efx->rps_hash_table) {
		/* In the absence of the table, we always return 0 to ARFS. */
		arfs_id = 0;
	} else {
		rule = efx_rps_hash_find(efx, spec);
		if (!rule)
			/* ARFS table doesn't know of this filter, so remove it */
			goto expire;
		arfs_id = rule->arfs_id;
		ret = efx_rps_check_rule(rule, filter_idx, &force);
		if (force)
			goto expire;
		if (!ret) {
			spin_unlock_bh(&efx->rps_hash_lock);
			goto out_unlock;
		}
	}
	if (!rps_may_expire_flow(efx->net_dev, spec->dmaq_id, flow_id, arfs_id))
		ret = false;
	else if (rule)
		rule->filter_id = EFX_ARFS_FILTER_ID_REMOVING;
expire:
	saved_spec = *spec; /* remove operation will kfree spec */
	spin_unlock_bh(&efx->rps_hash_lock);
	/*
	 * At this point (since we dropped the lock), another thread might queue
	 * up a fresh insertion request (but the actual insertion will be held
	 * up by our possession of the filter table lock).  In that case, it
	 * will set rule->filter_id to EFX_ARFS_FILTER_ID_PENDING, meaning that
	 * the rule is not removed by efx_rps_hash_del() below.
	 */
	if (ret)
		ret = efx_mcdi_filter_remove_internal(efx, 1U << spec->priority,
						      filter_idx, true) == 0;
	/*
	 * While we can't safely dereference rule (we dropped the lock), we can
	 * still test it for NULL.
	 */
	if (ret && rule) {
		/* Expiring, so remove entry from ARFS table */
		spin_lock_bh(&efx->rps_hash_lock);
		efx_rps_hash_del(efx, &saved_spec);
		spin_unlock_bh(&efx->rps_hash_lock);
	}
out_unlock:
	up_write(&table->lock);
	up_read(&efx->filter_sem);
	return ret;
}

#endif /* CONFIG_RFS_ACCEL */

#define RSS_MODE_HASH_ADDRS	(1 << RSS_MODE_HASH_SRC_ADDR_LBN |\
				 1 << RSS_MODE_HASH_DST_ADDR_LBN)
#define RSS_MODE_HASH_PORTS	(1 << RSS_MODE_HASH_SRC_PORT_LBN |\
				 1 << RSS_MODE_HASH_DST_PORT_LBN)
#define RSS_CONTEXT_FLAGS_DEFAULT	(1 << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_IPV4_EN_LBN |\
					 1 << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_TCPV4_EN_LBN |\
					 1 << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_IPV6_EN_LBN |\
					 1 << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_TCPV6_EN_LBN |\
					 (RSS_MODE_HASH_ADDRS | RSS_MODE_HASH_PORTS) << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TCP_IPV4_RSS_MODE_LBN |\
					 RSS_MODE_HASH_ADDRS << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_UDP_IPV4_RSS_MODE_LBN |\
					 RSS_MODE_HASH_ADDRS << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_OTHER_IPV4_RSS_MODE_LBN |\
					 (RSS_MODE_HASH_ADDRS | RSS_MODE_HASH_PORTS) << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TCP_IPV6_RSS_MODE_LBN |\
					 RSS_MODE_HASH_ADDRS << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_UDP_IPV6_RSS_MODE_LBN |\
					 RSS_MODE_HASH_ADDRS << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_OTHER_IPV6_RSS_MODE_LBN)

static int efx_mcdi_get_rss_context_flags(struct efx_nic *efx, u32 context,
					  u32 *flags)
{
	/*
	 * Firmware had a bug (sfc bug 61952) where it would not actually
	 * fill in the flags field in the response to MC_CMD_RSS_CONTEXT_GET_FLAGS.
	 * This meant that it would always contain whatever was previously
	 * in the MCDI buffer.  Fortunately, all firmware versions with
	 * this bug have the same default flags value for a newly-allocated
	 * RSS context, and the only time we want to get the flags is just
	 * after allocating.  Moreover, the response has a 32-bit hole
	 * where the context ID would be in the request, so we can use an
	 * overlength buffer in the request and pre-fill the flags field
	 * with what we believe the default to be.  Thus if the firmware
	 * has the bug, it will leave our pre-filled value in the flags
	 * field of the response, and we will get the right answer.
	 *
	 * However, this does mean that this function should NOT be used if
	 * the RSS context flags might not be their defaults - it is ONLY
	 * reliably correct for a newly-allocated RSS context.
	 */
	MCDI_DECLARE_BUF(inbuf, MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_LEN);
	size_t outlen;
	int rc;

	/* Check we have a hole for the context ID */
	BUILD_BUG_ON(MC_CMD_RSS_CONTEXT_GET_FLAGS_IN_LEN != MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_FLAGS_OFST);
	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_GET_FLAGS_IN_RSS_CONTEXT_ID, context);
	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_GET_FLAGS_OUT_FLAGS,
		       RSS_CONTEXT_FLAGS_DEFAULT);
	rc = efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_GET_FLAGS, inbuf,
			  sizeof(inbuf), outbuf, sizeof(outbuf), &outlen);
	if (rc == 0) {
		if (outlen < MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_LEN)
			rc = -EIO;
		else
			*flags = MCDI_DWORD(outbuf, RSS_CONTEXT_GET_FLAGS_OUT_FLAGS);
	}
	return rc;
}

/*
 * Attempt to enable 4-tuple UDP hashing on the specified RSS context.
 * If we fail, we just leave the RSS context at its default hash settings,
 * which is safe but may slightly reduce performance.
 * Defaults are 4-tuple for TCP and 2-tuple for UDP and other-IP, so we
 * just need to set the UDP ports flags (for both IP versions).
 */
static void efx_mcdi_set_rss_context_flags(struct efx_nic *efx,
					   struct efx_rss_context_priv *ctx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_LEN);
	u32 flags;

	BUILD_BUG_ON(MC_CMD_RSS_CONTEXT_SET_FLAGS_OUT_LEN != 0);

	if (efx_mcdi_get_rss_context_flags(efx, ctx->context_id, &flags) != 0)
		return;
	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_SET_FLAGS_IN_RSS_CONTEXT_ID,
		       ctx->context_id);
	flags |= RSS_MODE_HASH_PORTS << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_UDP_IPV4_RSS_MODE_LBN;
	flags |= RSS_MODE_HASH_PORTS << MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_UDP_IPV6_RSS_MODE_LBN;
	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_SET_FLAGS_IN_FLAGS, flags);
	if (!efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_SET_FLAGS, inbuf, sizeof(inbuf),
			  NULL, 0, NULL))
		/* Succeeded, so UDP 4-tuple is now enabled */
		ctx->rx_hash_udp_4tuple = true;
}

static int efx_mcdi_filter_alloc_rss_context(struct efx_nic *efx, bool exclusive,
					     struct efx_rss_context_priv *ctx,
					     unsigned *context_size)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_RSS_CONTEXT_ALLOC_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_RSS_CONTEXT_ALLOC_OUT_LEN);
	size_t outlen;
	int rc;
	u32 alloc_type = exclusive ?
				MC_CMD_RSS_CONTEXT_ALLOC_IN_TYPE_EXCLUSIVE :
				MC_CMD_RSS_CONTEXT_ALLOC_IN_TYPE_SHARED;
	unsigned rss_spread = exclusive ?
				efx->rss_spread :
				min(rounddown_pow_of_two(efx->rss_spread),
				    EFX_EF10_MAX_SHARED_RSS_CONTEXT_SIZE);

	if (!exclusive && rss_spread == 1) {
		ctx->context_id = EFX_MCDI_RSS_CONTEXT_INVALID;
		if (context_size)
			*context_size = 1;
		return 0;
	}

	if (efx_has_cap(efx, RX_RSS_LIMITED))
		return -EOPNOTSUPP;

	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_ALLOC_IN_UPSTREAM_PORT_ID,
		       efx->vport_id);
	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_ALLOC_IN_TYPE, alloc_type);
	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_ALLOC_IN_NUM_QUEUES, rss_spread);

	rc = efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_ALLOC, inbuf, sizeof(inbuf),
		outbuf, sizeof(outbuf), &outlen);
	if (rc != 0)
		return rc;

	if (outlen < MC_CMD_RSS_CONTEXT_ALLOC_OUT_LEN)
		return -EIO;

	ctx->context_id = MCDI_DWORD(outbuf, RSS_CONTEXT_ALLOC_OUT_RSS_CONTEXT_ID);

	if (context_size)
		*context_size = rss_spread;

	if (efx_has_cap(efx, ADDITIONAL_RSS_MODES))
		efx_mcdi_set_rss_context_flags(efx, ctx);

	return 0;
}

static int efx_mcdi_filter_free_rss_context(struct efx_nic *efx, u32 context)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_RSS_CONTEXT_FREE_IN_LEN);

	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_FREE_IN_RSS_CONTEXT_ID,
		       context);
	return efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_FREE, inbuf, sizeof(inbuf),
			    NULL, 0, NULL);
}

static int efx_mcdi_filter_populate_rss_table(struct efx_nic *efx, u32 context,
				       const u32 *rx_indir_table, const u8 *key)
{
	MCDI_DECLARE_BUF(tablebuf, MC_CMD_RSS_CONTEXT_SET_TABLE_IN_LEN);
	MCDI_DECLARE_BUF(keybuf, MC_CMD_RSS_CONTEXT_SET_KEY_IN_LEN);
	int i, rc;

	MCDI_SET_DWORD(tablebuf, RSS_CONTEXT_SET_TABLE_IN_RSS_CONTEXT_ID,
		       context);
	BUILD_BUG_ON(ARRAY_SIZE(efx->rss_context.rx_indir_table) !=
		     MC_CMD_RSS_CONTEXT_SET_TABLE_IN_INDIRECTION_TABLE_LEN);

	/* This iterates over the length of efx->rss_context.rx_indir_table, but
	 * copies bytes from rx_indir_table.  That's because the latter is a
	 * pointer rather than an array, but should have the same length.
	 * The efx->rss_context.rx_hash_key loop below is similar.
	 */
	for (i = 0; i < ARRAY_SIZE(efx->rss_context.rx_indir_table); ++i)
		MCDI_PTR(tablebuf,
			 RSS_CONTEXT_SET_TABLE_IN_INDIRECTION_TABLE)[i] =
				(u8) rx_indir_table[i];

	rc = efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_SET_TABLE, tablebuf,
			  sizeof(tablebuf), NULL, 0, NULL);
	if (rc != 0)
		return rc;

	MCDI_SET_DWORD(keybuf, RSS_CONTEXT_SET_KEY_IN_RSS_CONTEXT_ID,
		       context);
	BUILD_BUG_ON(ARRAY_SIZE(efx->rss_context.rx_hash_key) !=
		     MC_CMD_RSS_CONTEXT_SET_KEY_IN_TOEPLITZ_KEY_LEN);
	for (i = 0; i < ARRAY_SIZE(efx->rss_context.rx_hash_key); ++i)
		MCDI_PTR(keybuf, RSS_CONTEXT_SET_KEY_IN_TOEPLITZ_KEY)[i] = key[i];

	return efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_SET_KEY, keybuf,
			    sizeof(keybuf), NULL, 0, NULL);
}

void efx_mcdi_rx_free_indir_table(struct efx_nic *efx)
{
	int rc;

	if (efx->rss_context.priv.context_id != EFX_MCDI_RSS_CONTEXT_INVALID) {
		rc = efx_mcdi_filter_free_rss_context(efx, efx->rss_context.priv.context_id);
		WARN_ON(rc != 0);
	}
	efx->rss_context.priv.context_id = EFX_MCDI_RSS_CONTEXT_INVALID;
}

static int efx_mcdi_filter_rx_push_shared_rss_config(struct efx_nic *efx,
					      unsigned *context_size)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	int rc = efx_mcdi_filter_alloc_rss_context(efx, false,
						   &efx->rss_context.priv,
						   context_size);

	if (rc != 0)
		return rc;

	table->rx_rss_context_exclusive = false;
	efx_set_default_rx_indir_table(efx, efx->rss_context.rx_indir_table);
	return 0;
}

static int efx_mcdi_filter_rx_push_exclusive_rss_config(struct efx_nic *efx,
						 const u32 *rx_indir_table,
						 const u8 *key)
{
	u32 old_rx_rss_context = efx->rss_context.priv.context_id;
	struct efx_mcdi_filter_table *table = efx->filter_state;
	int rc;

	if (efx->rss_context.priv.context_id == EFX_MCDI_RSS_CONTEXT_INVALID ||
	    !table->rx_rss_context_exclusive) {
		rc = efx_mcdi_filter_alloc_rss_context(efx, true,
						       &efx->rss_context.priv,
						       NULL);
		if (rc == -EOPNOTSUPP)
			return rc;
		else if (rc != 0)
			goto fail1;
	}

	rc = efx_mcdi_filter_populate_rss_table(efx, efx->rss_context.priv.context_id,
						rx_indir_table, key);
	if (rc != 0)
		goto fail2;

	if (efx->rss_context.priv.context_id != old_rx_rss_context &&
	    old_rx_rss_context != EFX_MCDI_RSS_CONTEXT_INVALID)
		WARN_ON(efx_mcdi_filter_free_rss_context(efx, old_rx_rss_context) != 0);
	table->rx_rss_context_exclusive = true;
	if (rx_indir_table != efx->rss_context.rx_indir_table)
		memcpy(efx->rss_context.rx_indir_table, rx_indir_table,
		       sizeof(efx->rss_context.rx_indir_table));
	if (key != efx->rss_context.rx_hash_key)
		memcpy(efx->rss_context.rx_hash_key, key,
		       efx->type->rx_hash_key_size);

	return 0;

fail2:
	if (old_rx_rss_context != efx->rss_context.priv.context_id) {
		WARN_ON(efx_mcdi_filter_free_rss_context(efx, efx->rss_context.priv.context_id) != 0);
		efx->rss_context.priv.context_id = old_rx_rss_context;
	}
fail1:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

int efx_mcdi_rx_push_rss_context_config(struct efx_nic *efx,
					struct efx_rss_context_priv *ctx,
					const u32 *rx_indir_table,
					const u8 *key, bool delete)
{
	int rc;

	WARN_ON(!mutex_is_locked(&efx->net_dev->ethtool->rss_lock));

	if (ctx->context_id == EFX_MCDI_RSS_CONTEXT_INVALID) {
		if (delete)
			/* already wasn't in HW, nothing to do */
			return 0;
		rc = efx_mcdi_filter_alloc_rss_context(efx, true, ctx, NULL);
		if (rc)
			return rc;
	}

	if (delete) /* Delete this context */
		return efx_mcdi_filter_free_rss_context(efx, ctx->context_id);

	return efx_mcdi_filter_populate_rss_table(efx, ctx->context_id,
						  rx_indir_table, key);
}

int efx_mcdi_rx_pull_rss_context_config(struct efx_nic *efx,
					struct efx_rss_context *ctx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_RSS_CONTEXT_GET_TABLE_IN_LEN);
	MCDI_DECLARE_BUF(tablebuf, MC_CMD_RSS_CONTEXT_GET_TABLE_OUT_LEN);
	MCDI_DECLARE_BUF(keybuf, MC_CMD_RSS_CONTEXT_GET_KEY_OUT_LEN);
	size_t outlen;
	int rc, i;

	WARN_ON(!mutex_is_locked(&efx->net_dev->ethtool->rss_lock));

	BUILD_BUG_ON(MC_CMD_RSS_CONTEXT_GET_TABLE_IN_LEN !=
		     MC_CMD_RSS_CONTEXT_GET_KEY_IN_LEN);

	if (ctx->priv.context_id == EFX_MCDI_RSS_CONTEXT_INVALID)
		return -ENOENT;

	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_GET_TABLE_IN_RSS_CONTEXT_ID,
		       ctx->priv.context_id);
	BUILD_BUG_ON(ARRAY_SIZE(ctx->rx_indir_table) !=
		     MC_CMD_RSS_CONTEXT_GET_TABLE_OUT_INDIRECTION_TABLE_LEN);
	rc = efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_GET_TABLE, inbuf, sizeof(inbuf),
			  tablebuf, sizeof(tablebuf), &outlen);
	if (rc != 0)
		return rc;

	if (WARN_ON(outlen != MC_CMD_RSS_CONTEXT_GET_TABLE_OUT_LEN))
		return -EIO;

	for (i = 0; i < ARRAY_SIZE(ctx->rx_indir_table); i++)
		ctx->rx_indir_table[i] = MCDI_PTR(tablebuf,
				RSS_CONTEXT_GET_TABLE_OUT_INDIRECTION_TABLE)[i];

	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_GET_KEY_IN_RSS_CONTEXT_ID,
		       ctx->priv.context_id);
	BUILD_BUG_ON(ARRAY_SIZE(ctx->rx_hash_key) !=
		     MC_CMD_RSS_CONTEXT_SET_KEY_IN_TOEPLITZ_KEY_LEN);
	rc = efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_GET_KEY, inbuf, sizeof(inbuf),
			  keybuf, sizeof(keybuf), &outlen);
	if (rc != 0)
		return rc;

	if (WARN_ON(outlen != MC_CMD_RSS_CONTEXT_GET_KEY_OUT_LEN))
		return -EIO;

	for (i = 0; i < ARRAY_SIZE(ctx->rx_hash_key); ++i)
		ctx->rx_hash_key[i] = MCDI_PTR(
				keybuf, RSS_CONTEXT_GET_KEY_OUT_TOEPLITZ_KEY)[i];

	return 0;
}

int efx_mcdi_rx_pull_rss_config(struct efx_nic *efx)
{
	int rc;

	mutex_lock(&efx->net_dev->ethtool->rss_lock);
	rc = efx_mcdi_rx_pull_rss_context_config(efx, &efx->rss_context);
	mutex_unlock(&efx->net_dev->ethtool->rss_lock);
	return rc;
}

void efx_mcdi_rx_restore_rss_contexts(struct efx_nic *efx)
{
	struct efx_mcdi_filter_table *table = efx->filter_state;
	struct ethtool_rxfh_context *ctx;
	unsigned long context;
	int rc;

	WARN_ON(!mutex_is_locked(&efx->net_dev->ethtool->rss_lock));

	if (!table->must_restore_rss_contexts)
		return;

	xa_for_each(&efx->net_dev->ethtool->rss_ctx, context, ctx) {
		struct efx_rss_context_priv *priv;
		u32 *indir;
		u8 *key;

		priv = ethtool_rxfh_context_priv(ctx);
		/* previous NIC RSS context is gone */
		priv->context_id = EFX_MCDI_RSS_CONTEXT_INVALID;
		/* so try to allocate a new one */
		indir = ethtool_rxfh_context_indir(ctx);
		key = ethtool_rxfh_context_key(ctx);
		rc = efx_mcdi_rx_push_rss_context_config(efx, priv, indir, key,
							 false);
		if (rc)
			netif_warn(efx, probe, efx->net_dev,
				   "failed to restore RSS context %lu, rc=%d"
				   "; RSS filters may fail to be applied\n",
				   context, rc);
	}
	table->must_restore_rss_contexts = false;
}

int efx_mcdi_pf_rx_push_rss_config(struct efx_nic *efx, bool user,
				   const u32 *rx_indir_table,
				   const u8 *key)
{
	int rc;

	if (efx->rss_spread == 1)
		return 0;

	if (!key)
		key = efx->rss_context.rx_hash_key;

	rc = efx_mcdi_filter_rx_push_exclusive_rss_config(efx, rx_indir_table, key);

	if (rc == -ENOBUFS && !user) {
		unsigned context_size;
		bool mismatch = false;
		size_t i;

		for (i = 0;
		     i < ARRAY_SIZE(efx->rss_context.rx_indir_table) && !mismatch;
		     i++)
			mismatch = rx_indir_table[i] !=
				ethtool_rxfh_indir_default(i, efx->rss_spread);

		rc = efx_mcdi_filter_rx_push_shared_rss_config(efx, &context_size);
		if (rc == 0) {
			if (context_size != efx->rss_spread)
				netif_warn(efx, probe, efx->net_dev,
					   "Could not allocate an exclusive RSS"
					   " context; allocated a shared one of"
					   " different size."
					   " Wanted %u, got %u.\n",
					   efx->rss_spread, context_size);
			else if (mismatch)
				netif_warn(efx, probe, efx->net_dev,
					   "Could not allocate an exclusive RSS"
					   " context; allocated a shared one but"
					   " could not apply custom"
					   " indirection.\n");
			else
				netif_info(efx, probe, efx->net_dev,
					   "Could not allocate an exclusive RSS"
					   " context; allocated a shared one.\n");
		}
	}
	return rc;
}

int efx_mcdi_vf_rx_push_rss_config(struct efx_nic *efx, bool user,
				   const u32 *rx_indir_table
				   __attribute__ ((unused)),
				   const u8 *key
				   __attribute__ ((unused)))
{
	if (user)
		return -EOPNOTSUPP;
	if (efx->rss_context.priv.context_id != EFX_MCDI_RSS_CONTEXT_INVALID)
		return 0;
	return efx_mcdi_filter_rx_push_shared_rss_config(efx, NULL);
}

int efx_mcdi_push_default_indir_table(struct efx_nic *efx,
				      unsigned int rss_spread)
{
	int rc = 0;

	if (efx->rss_spread == rss_spread)
		return 0;

	efx->rss_spread = rss_spread;
	if (!efx->filter_state)
		return 0;

	efx_mcdi_rx_free_indir_table(efx);
	if (rss_spread > 1) {
		efx_set_default_rx_indir_table(efx, efx->rss_context.rx_indir_table);
		rc = efx->type->rx_push_rss_config(efx, false,
				   efx->rss_context.rx_indir_table, NULL);
	}
	return rc;
}
