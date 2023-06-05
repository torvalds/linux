// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 * Copyright 2020-2022 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/rhashtable.h>
#include "ef100_nic.h"
#include "mae.h"
#include "mcdi.h"
#include "mcdi_pcol.h"
#include "mcdi_pcol_mae.h"

int efx_mae_allocate_mport(struct efx_nic *efx, u32 *id, u32 *label)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_MPORT_ALLOC_ALIAS_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_MPORT_ALLOC_ALIAS_IN_LEN);
	size_t outlen;
	int rc;

	if (WARN_ON_ONCE(!id))
		return -EINVAL;
	if (WARN_ON_ONCE(!label))
		return -EINVAL;

	MCDI_SET_DWORD(inbuf, MAE_MPORT_ALLOC_ALIAS_IN_TYPE,
		       MC_CMD_MAE_MPORT_ALLOC_ALIAS_IN_MPORT_TYPE_ALIAS);
	MCDI_SET_DWORD(inbuf, MAE_MPORT_ALLOC_ALIAS_IN_DELIVER_MPORT,
		       MAE_MPORT_SELECTOR_ASSIGNED);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_MPORT_ALLOC, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	*id = MCDI_DWORD(outbuf, MAE_MPORT_ALLOC_ALIAS_OUT_MPORT_ID);
	*label = MCDI_DWORD(outbuf, MAE_MPORT_ALLOC_ALIAS_OUT_LABEL);
	return 0;
}

int efx_mae_free_mport(struct efx_nic *efx, u32 id)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_MPORT_FREE_IN_LEN);

	BUILD_BUG_ON(MC_CMD_MAE_MPORT_FREE_OUT_LEN);
	MCDI_SET_DWORD(inbuf, MAE_MPORT_FREE_IN_MPORT_ID, id);
	return efx_mcdi_rpc(efx, MC_CMD_MAE_MPORT_FREE, inbuf, sizeof(inbuf),
			    NULL, 0, NULL);
}

void efx_mae_mport_wire(struct efx_nic *efx, u32 *out)
{
	efx_dword_t mport;

	EFX_POPULATE_DWORD_2(mport,
			     MAE_MPORT_SELECTOR_TYPE, MAE_MPORT_SELECTOR_TYPE_PPORT,
			     MAE_MPORT_SELECTOR_PPORT_ID, efx->port_num);
	*out = EFX_DWORD_VAL(mport);
}

void efx_mae_mport_uplink(struct efx_nic *efx __always_unused, u32 *out)
{
	efx_dword_t mport;

	EFX_POPULATE_DWORD_3(mport,
			     MAE_MPORT_SELECTOR_TYPE, MAE_MPORT_SELECTOR_TYPE_FUNC,
			     MAE_MPORT_SELECTOR_FUNC_PF_ID, MAE_MPORT_SELECTOR_FUNC_PF_ID_CALLER,
			     MAE_MPORT_SELECTOR_FUNC_VF_ID, MAE_MPORT_SELECTOR_FUNC_VF_ID_NULL);
	*out = EFX_DWORD_VAL(mport);
}

void efx_mae_mport_vf(struct efx_nic *efx __always_unused, u32 vf_id, u32 *out)
{
	efx_dword_t mport;

	EFX_POPULATE_DWORD_3(mport,
			     MAE_MPORT_SELECTOR_TYPE, MAE_MPORT_SELECTOR_TYPE_FUNC,
			     MAE_MPORT_SELECTOR_FUNC_PF_ID, MAE_MPORT_SELECTOR_FUNC_PF_ID_CALLER,
			     MAE_MPORT_SELECTOR_FUNC_VF_ID, vf_id);
	*out = EFX_DWORD_VAL(mport);
}

/* Constructs an mport selector from an mport ID, because they're not the same */
void efx_mae_mport_mport(struct efx_nic *efx __always_unused, u32 mport_id, u32 *out)
{
	efx_dword_t mport;

	EFX_POPULATE_DWORD_2(mport,
			     MAE_MPORT_SELECTOR_TYPE, MAE_MPORT_SELECTOR_TYPE_MPORT_ID,
			     MAE_MPORT_SELECTOR_MPORT_ID, mport_id);
	*out = EFX_DWORD_VAL(mport);
}

/* id is really only 24 bits wide */
int efx_mae_fw_lookup_mport(struct efx_nic *efx, u32 selector, u32 *id)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_MPORT_LOOKUP_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_MPORT_LOOKUP_IN_LEN);
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, MAE_MPORT_LOOKUP_IN_MPORT_SELECTOR, selector);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_MPORT_LOOKUP, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	*id = MCDI_DWORD(outbuf, MAE_MPORT_LOOKUP_OUT_MPORT_ID);
	return 0;
}

int efx_mae_start_counters(struct efx_nic *efx, struct efx_rx_queue *rx_queue)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_COUNTERS_STREAM_START_V2_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_COUNTERS_STREAM_START_OUT_LEN);
	u32 out_flags;
	size_t outlen;
	int rc;

	MCDI_SET_WORD(inbuf, MAE_COUNTERS_STREAM_START_V2_IN_QID,
		      efx_rx_queue_index(rx_queue));
	MCDI_SET_WORD(inbuf, MAE_COUNTERS_STREAM_START_V2_IN_PACKET_SIZE,
		      efx->net_dev->mtu);
	MCDI_SET_DWORD(inbuf, MAE_COUNTERS_STREAM_START_V2_IN_COUNTER_TYPES_MASK,
		       BIT(MAE_COUNTER_TYPE_AR) | BIT(MAE_COUNTER_TYPE_CT) |
		       BIT(MAE_COUNTER_TYPE_OR));
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_COUNTERS_STREAM_START,
			  inbuf, sizeof(inbuf), outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	out_flags = MCDI_DWORD(outbuf, MAE_COUNTERS_STREAM_START_OUT_FLAGS);
	if (out_flags & BIT(MC_CMD_MAE_COUNTERS_STREAM_START_OUT_USES_CREDITS_OFST)) {
		netif_dbg(efx, drv, efx->net_dev,
			  "MAE counter stream uses credits\n");
		rx_queue->grant_credits = true;
		out_flags &= ~BIT(MC_CMD_MAE_COUNTERS_STREAM_START_OUT_USES_CREDITS_OFST);
	}
	if (out_flags) {
		netif_err(efx, drv, efx->net_dev,
			  "MAE counter stream start: unrecognised flags %x\n",
			  out_flags);
		goto out_stop;
	}
	return 0;
out_stop:
	efx_mae_stop_counters(efx, rx_queue);
	return -EOPNOTSUPP;
}

static bool efx_mae_counters_flushed(u32 *flush_gen, u32 *seen_gen)
{
	int i;

	for (i = 0; i < EFX_TC_COUNTER_TYPE_MAX; i++)
		if ((s32)(flush_gen[i] - seen_gen[i]) > 0)
			return false;
	return true;
}

int efx_mae_stop_counters(struct efx_nic *efx, struct efx_rx_queue *rx_queue)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_COUNTERS_STREAM_STOP_V2_OUT_LENMAX);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_COUNTERS_STREAM_STOP_IN_LEN);
	size_t outlen;
	int rc, i;

	MCDI_SET_WORD(inbuf, MAE_COUNTERS_STREAM_STOP_IN_QID,
		      efx_rx_queue_index(rx_queue));
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_COUNTERS_STREAM_STOP,
			  inbuf, sizeof(inbuf), outbuf, sizeof(outbuf), &outlen);

	if (rc)
		return rc;

	netif_dbg(efx, drv, efx->net_dev, "Draining counters:\n");
	/* Only process received generation counts */
	for (i = 0; (i < (outlen / 4)) && (i < EFX_TC_COUNTER_TYPE_MAX); i++) {
		efx->tc->flush_gen[i] = MCDI_ARRAY_DWORD(outbuf,
							 MAE_COUNTERS_STREAM_STOP_V2_OUT_GENERATION_COUNT,
							 i);
		netif_dbg(efx, drv, efx->net_dev,
			  "\ttype %u, awaiting gen %u\n", i,
			  efx->tc->flush_gen[i]);
	}

	efx->tc->flush_counters = true;

	/* Drain can take up to 2 seconds owing to FWRIVERHD-2884; whatever
	 * timeout we use, that delay is added to unload on nonresponsive
	 * hardware, so 2500ms seems like a reasonable compromise.
	 */
	if (!wait_event_timeout(efx->tc->flush_wq,
				efx_mae_counters_flushed(efx->tc->flush_gen,
							 efx->tc->seen_gen),
				msecs_to_jiffies(2500)))
		netif_warn(efx, drv, efx->net_dev,
			   "Failed to drain counters RXQ, FW may be unhappy\n");

	efx->tc->flush_counters = false;

	return rc;
}

void efx_mae_counters_grant_credits(struct work_struct *work)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_COUNTERS_STREAM_GIVE_CREDITS_IN_LEN);
	struct efx_rx_queue *rx_queue = container_of(work, struct efx_rx_queue,
						     grant_work);
	struct efx_nic *efx = rx_queue->efx;
	unsigned int credits;

	BUILD_BUG_ON(MC_CMD_MAE_COUNTERS_STREAM_GIVE_CREDITS_OUT_LEN);
	credits = READ_ONCE(rx_queue->notified_count) - rx_queue->granted_count;
	MCDI_SET_DWORD(inbuf, MAE_COUNTERS_STREAM_GIVE_CREDITS_IN_NUM_CREDITS,
		       credits);
	if (!efx_mcdi_rpc(efx, MC_CMD_MAE_COUNTERS_STREAM_GIVE_CREDITS,
			  inbuf, sizeof(inbuf), NULL, 0, NULL))
		rx_queue->granted_count += credits;
}

static int efx_mae_get_basic_caps(struct efx_nic *efx, struct mae_caps *caps)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_GET_CAPS_OUT_LEN);
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_MAE_GET_CAPS_IN_LEN);

	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_GET_CAPS, NULL, 0, outbuf,
			  sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	caps->match_field_count = MCDI_DWORD(outbuf, MAE_GET_CAPS_OUT_MATCH_FIELD_COUNT);
	caps->encap_types = MCDI_DWORD(outbuf, MAE_GET_CAPS_OUT_ENCAP_TYPES_SUPPORTED);
	caps->action_prios = MCDI_DWORD(outbuf, MAE_GET_CAPS_OUT_ACTION_PRIOS);
	return 0;
}

static int efx_mae_get_rule_fields(struct efx_nic *efx, u32 cmd,
				   u8 *field_support)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_GET_AR_CAPS_OUT_LEN(MAE_NUM_FIELDS));
	MCDI_DECLARE_STRUCT_PTR(caps);
	unsigned int count;
	size_t outlen;
	int rc, i;

	/* AR and OR caps MCDIs have identical layout, so we are using the
	 * same code for both.
	 */
	BUILD_BUG_ON(MC_CMD_MAE_GET_AR_CAPS_OUT_LEN(MAE_NUM_FIELDS) <
		     MC_CMD_MAE_GET_OR_CAPS_OUT_LEN(MAE_NUM_FIELDS));
	BUILD_BUG_ON(MC_CMD_MAE_GET_AR_CAPS_IN_LEN);
	BUILD_BUG_ON(MC_CMD_MAE_GET_OR_CAPS_IN_LEN);

	rc = efx_mcdi_rpc(efx, cmd, NULL, 0, outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	BUILD_BUG_ON(MC_CMD_MAE_GET_AR_CAPS_OUT_COUNT_OFST !=
		     MC_CMD_MAE_GET_OR_CAPS_OUT_COUNT_OFST);
	count = MCDI_DWORD(outbuf, MAE_GET_AR_CAPS_OUT_COUNT);
	memset(field_support, MAE_FIELD_UNSUPPORTED, MAE_NUM_FIELDS);
	BUILD_BUG_ON(MC_CMD_MAE_GET_AR_CAPS_OUT_FIELD_FLAGS_OFST !=
		     MC_CMD_MAE_GET_OR_CAPS_OUT_FIELD_FLAGS_OFST);
	caps = _MCDI_DWORD(outbuf, MAE_GET_AR_CAPS_OUT_FIELD_FLAGS);
	/* We're only interested in the support status enum, not any other
	 * flags, so just extract that from each entry.
	 */
	for (i = 0; i < count; i++)
		if (i * sizeof(*outbuf) + MC_CMD_MAE_GET_AR_CAPS_OUT_FIELD_FLAGS_OFST < outlen)
			field_support[i] = EFX_DWORD_FIELD(caps[i], MAE_FIELD_FLAGS_SUPPORT_STATUS);
	return 0;
}

int efx_mae_get_caps(struct efx_nic *efx, struct mae_caps *caps)
{
	int rc;

	rc = efx_mae_get_basic_caps(efx, caps);
	if (rc)
		return rc;
	rc = efx_mae_get_rule_fields(efx, MC_CMD_MAE_GET_AR_CAPS,
				     caps->action_rule_fields);
	if (rc)
		return rc;
	return efx_mae_get_rule_fields(efx, MC_CMD_MAE_GET_OR_CAPS,
				       caps->outer_rule_fields);
}

/* Bit twiddling:
 * Prefix: 1...110...0
 *      ~: 0...001...1
 *    + 1: 0...010...0 is power of two
 * so (~x) & ((~x) + 1) == 0.  Converse holds also.
 */
#define is_prefix_byte(_x)	!(((_x) ^ 0xff) & (((_x) ^ 0xff) + 1))

enum mask_type { MASK_ONES, MASK_ZEROES, MASK_PREFIX, MASK_OTHER };

static const char *mask_type_name(enum mask_type typ)
{
	switch (typ) {
	case MASK_ONES:
		return "all-1s";
	case MASK_ZEROES:
		return "all-0s";
	case MASK_PREFIX:
		return "prefix";
	case MASK_OTHER:
		return "arbitrary";
	default: /* can't happen */
		return "unknown";
	}
}

/* Checks a (big-endian) bytestring is a bit prefix */
static enum mask_type classify_mask(const u8 *mask, size_t len)
{
	bool zeroes = true; /* All bits seen so far are zeroes */
	bool ones = true; /* All bits seen so far are ones */
	bool prefix = true; /* Valid prefix so far */
	size_t i;

	for (i = 0; i < len; i++) {
		if (ones) {
			if (!is_prefix_byte(mask[i]))
				prefix = false;
		} else if (mask[i]) {
			prefix = false;
		}
		if (mask[i] != 0xff)
			ones = false;
		if (mask[i])
			zeroes = false;
	}
	if (ones)
		return MASK_ONES;
	if (zeroes)
		return MASK_ZEROES;
	if (prefix)
		return MASK_PREFIX;
	return MASK_OTHER;
}

static int efx_mae_match_check_cap_typ(u8 support, enum mask_type typ)
{
	switch (support) {
	case MAE_FIELD_UNSUPPORTED:
	case MAE_FIELD_SUPPORTED_MATCH_NEVER:
		if (typ == MASK_ZEROES)
			return 0;
		return -EOPNOTSUPP;
	case MAE_FIELD_SUPPORTED_MATCH_OPTIONAL:
		if (typ == MASK_ZEROES)
			return 0;
		fallthrough;
	case MAE_FIELD_SUPPORTED_MATCH_ALWAYS:
		if (typ == MASK_ONES)
			return 0;
		return -EINVAL;
	case MAE_FIELD_SUPPORTED_MATCH_PREFIX:
		if (typ == MASK_OTHER)
			return -EOPNOTSUPP;
		return 0;
	case MAE_FIELD_SUPPORTED_MATCH_MASK:
		return 0;
	default:
		return -EIO;
	}
}

/* Validate field mask against hardware capabilities.  Captures caller's 'rc' */
#define CHECK(_mcdi, _field)	({					       \
	enum mask_type typ = classify_mask((const u8 *)&mask->_field,	       \
					   sizeof(mask->_field));	       \
									       \
	rc = efx_mae_match_check_cap_typ(supported_fields[MAE_FIELD_ ## _mcdi],\
					 typ);				       \
	if (rc)								       \
		NL_SET_ERR_MSG_FMT_MOD(extack,				       \
				       "No support for %s mask in field %s",   \
				       mask_type_name(typ), #_field);	       \
	rc;								       \
})
/* Booleans need special handling */
#define CHECK_BIT(_mcdi, _field)	({				       \
	enum mask_type typ = mask->_field ? MASK_ONES : MASK_ZEROES;	       \
									       \
	rc = efx_mae_match_check_cap_typ(supported_fields[MAE_FIELD_ ## _mcdi],\
					 typ);				       \
	if (rc)								       \
		NL_SET_ERR_MSG_FMT_MOD(extack,				       \
				       "No support for %s mask in field %s",   \
				       mask_type_name(typ), #_field);	       \
	rc;								       \
})

int efx_mae_match_check_caps(struct efx_nic *efx,
			     const struct efx_tc_match_fields *mask,
			     struct netlink_ext_ack *extack)
{
	const u8 *supported_fields = efx->tc->caps->action_rule_fields;
	__be32 ingress_port = cpu_to_be32(mask->ingress_port);
	enum mask_type ingress_port_mask_type;
	int rc;

	/* Check for _PREFIX assumes big-endian, so we need to convert */
	ingress_port_mask_type = classify_mask((const u8 *)&ingress_port,
					       sizeof(ingress_port));
	rc = efx_mae_match_check_cap_typ(supported_fields[MAE_FIELD_INGRESS_PORT],
					 ingress_port_mask_type);
	if (rc) {
		NL_SET_ERR_MSG_FMT_MOD(extack, "No support for %s mask in field ingress_port",
				       mask_type_name(ingress_port_mask_type));
		return rc;
	}
	if (CHECK(ETHER_TYPE, eth_proto) ||
	    CHECK(VLAN0_TCI, vlan_tci[0]) ||
	    CHECK(VLAN0_PROTO, vlan_proto[0]) ||
	    CHECK(VLAN1_TCI, vlan_tci[1]) ||
	    CHECK(VLAN1_PROTO, vlan_proto[1]) ||
	    CHECK(ETH_SADDR, eth_saddr) ||
	    CHECK(ETH_DADDR, eth_daddr) ||
	    CHECK(IP_PROTO, ip_proto) ||
	    CHECK(IP_TOS, ip_tos) ||
	    CHECK(IP_TTL, ip_ttl) ||
	    CHECK(SRC_IP4, src_ip) ||
	    CHECK(DST_IP4, dst_ip) ||
#ifdef CONFIG_IPV6
	    CHECK(SRC_IP6, src_ip6) ||
	    CHECK(DST_IP6, dst_ip6) ||
#endif
	    CHECK(L4_SPORT, l4_sport) ||
	    CHECK(L4_DPORT, l4_dport) ||
	    CHECK(TCP_FLAGS, tcp_flags) ||
	    CHECK_BIT(IS_IP_FRAG, ip_frag) ||
	    CHECK_BIT(IP_FIRST_FRAG, ip_firstfrag) ||
	    CHECK(RECIRC_ID, recirc_id))
		return rc;
	/* Matches on outer fields are done in a separate hardware table,
	 * the Outer Rule table.  Thus the Action Rule merely does an
	 * exact match on Outer Rule ID if any outer field matches are
	 * present.  The exception is the VNI/VSID (enc_keyid), which is
	 * available to the Action Rule match iff the Outer Rule matched
	 * (and thus identified the encap protocol to use to extract it).
	 */
	if (efx_tc_match_is_encap(mask)) {
		rc = efx_mae_match_check_cap_typ(
				supported_fields[MAE_FIELD_OUTER_RULE_ID],
				MASK_ONES);
		if (rc) {
			NL_SET_ERR_MSG_MOD(extack, "No support for encap rule ID matches");
			return rc;
		}
		if (CHECK(ENC_VNET_ID, enc_keyid))
			return rc;
	} else if (mask->enc_keyid) {
		NL_SET_ERR_MSG_MOD(extack, "Match on enc_keyid requires other encap fields");
		return -EINVAL;
	}
	return 0;
}
#undef CHECK_BIT
#undef CHECK

#define CHECK(_mcdi)	({						       \
	rc = efx_mae_match_check_cap_typ(supported_fields[MAE_FIELD_ ## _mcdi],\
					 MASK_ONES);			       \
	if (rc)								       \
		NL_SET_ERR_MSG_FMT_MOD(extack,				       \
				       "No support for field %s", #_mcdi);     \
	rc;								       \
})
/* Checks that the fields needed for encap-rule matches are supported by the
 * MAE.  All the fields are exact-match.
 */
int efx_mae_check_encap_match_caps(struct efx_nic *efx, bool ipv6,
				   struct netlink_ext_ack *extack)
{
	u8 *supported_fields = efx->tc->caps->outer_rule_fields;
	int rc;

	if (CHECK(ENC_ETHER_TYPE))
		return rc;
	if (ipv6) {
		if (CHECK(ENC_SRC_IP6) ||
		    CHECK(ENC_DST_IP6))
			return rc;
	} else {
		if (CHECK(ENC_SRC_IP4) ||
		    CHECK(ENC_DST_IP4))
			return rc;
	}
	if (CHECK(ENC_L4_DPORT) ||
	    CHECK(ENC_IP_PROTO))
		return rc;
	return 0;
}
#undef CHECK

int efx_mae_check_encap_type_supported(struct efx_nic *efx, enum efx_encap_type typ)
{
	unsigned int bit;

	switch (typ & EFX_ENCAP_TYPES_MASK) {
	case EFX_ENCAP_TYPE_VXLAN:
		bit = MC_CMD_MAE_GET_CAPS_OUT_ENCAP_TYPE_VXLAN_LBN;
		break;
	case EFX_ENCAP_TYPE_GENEVE:
		bit = MC_CMD_MAE_GET_CAPS_OUT_ENCAP_TYPE_GENEVE_LBN;
		break;
	default:
		return -EOPNOTSUPP;
	}
	if (efx->tc->caps->encap_types & BIT(bit))
		return 0;
	return -EOPNOTSUPP;
}

int efx_mae_allocate_counter(struct efx_nic *efx, struct efx_tc_counter *cnt)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_COUNTER_ALLOC_OUT_LEN(1));
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_COUNTER_ALLOC_V2_IN_LEN);
	size_t outlen;
	int rc;

	if (!cnt)
		return -EINVAL;

	MCDI_SET_DWORD(inbuf, MAE_COUNTER_ALLOC_V2_IN_REQUESTED_COUNT, 1);
	MCDI_SET_DWORD(inbuf, MAE_COUNTER_ALLOC_V2_IN_COUNTER_TYPE, cnt->type);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_COUNTER_ALLOC, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	/* pcol says this can't happen, since count is 1 */
	if (outlen < sizeof(outbuf))
		return -EIO;
	cnt->fw_id = MCDI_DWORD(outbuf, MAE_COUNTER_ALLOC_OUT_COUNTER_ID);
	cnt->gen = MCDI_DWORD(outbuf, MAE_COUNTER_ALLOC_OUT_GENERATION_COUNT);
	return 0;
}

int efx_mae_free_counter(struct efx_nic *efx, struct efx_tc_counter *cnt)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_COUNTER_FREE_OUT_LEN(1));
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_COUNTER_FREE_V2_IN_LEN);
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, MAE_COUNTER_FREE_V2_IN_COUNTER_ID_COUNT, 1);
	MCDI_SET_DWORD(inbuf, MAE_COUNTER_FREE_V2_IN_FREE_COUNTER_ID, cnt->fw_id);
	MCDI_SET_DWORD(inbuf, MAE_COUNTER_FREE_V2_IN_COUNTER_TYPE, cnt->type);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_COUNTER_FREE, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	/* pcol says this can't happen, since count is 1 */
	if (outlen < sizeof(outbuf))
		return -EIO;
	/* FW freed a different ID than we asked for, should also never happen.
	 * Warn because it means we've now got a different idea to the FW of
	 * what counters exist, which could cause mayhem later.
	 */
	if (WARN_ON(MCDI_DWORD(outbuf, MAE_COUNTER_FREE_OUT_FREED_COUNTER_ID) !=
		    cnt->fw_id))
		return -EIO;
	return 0;
}

static int efx_mae_encap_type_to_mae_type(enum efx_encap_type type)
{
	switch (type & EFX_ENCAP_TYPES_MASK) {
	case EFX_ENCAP_TYPE_NONE:
		return MAE_MCDI_ENCAP_TYPE_NONE;
	case EFX_ENCAP_TYPE_VXLAN:
		return MAE_MCDI_ENCAP_TYPE_VXLAN;
	case EFX_ENCAP_TYPE_GENEVE:
		return MAE_MCDI_ENCAP_TYPE_GENEVE;
	default:
		return -EOPNOTSUPP;
	}
}

int efx_mae_lookup_mport(struct efx_nic *efx, u32 vf_idx, u32 *id)
{
	struct ef100_nic_data *nic_data = efx->nic_data;
	struct efx_mae *mae = efx->mae;
	struct rhashtable_iter walk;
	struct mae_mport_desc *m;
	int rc = -ENOENT;

	rhashtable_walk_enter(&mae->mports_ht, &walk);
	rhashtable_walk_start(&walk);
	while ((m = rhashtable_walk_next(&walk)) != NULL) {
		if (m->mport_type == MAE_MPORT_DESC_MPORT_TYPE_VNIC &&
		    m->interface_idx == nic_data->local_mae_intf &&
		    m->pf_idx == 0 &&
		    m->vf_idx == vf_idx) {
			*id = m->mport_id;
			rc = 0;
			break;
		}
	}
	rhashtable_walk_stop(&walk);
	rhashtable_walk_exit(&walk);
	return rc;
}

static bool efx_mae_asl_id(u32 id)
{
	return !!(id & BIT(31));
}

/* mport handling */
static const struct rhashtable_params efx_mae_mports_ht_params = {
	.key_len	= sizeof(u32),
	.key_offset	= offsetof(struct mae_mport_desc, mport_id),
	.head_offset	= offsetof(struct mae_mport_desc, linkage),
};

struct mae_mport_desc *efx_mae_get_mport(struct efx_nic *efx, u32 mport_id)
{
	return rhashtable_lookup_fast(&efx->mae->mports_ht, &mport_id,
				      efx_mae_mports_ht_params);
}

static int efx_mae_add_mport(struct efx_nic *efx, struct mae_mport_desc *desc)
{
	struct efx_mae *mae = efx->mae;
	int rc;

	rc = rhashtable_insert_fast(&mae->mports_ht, &desc->linkage,
				    efx_mae_mports_ht_params);

	if (rc) {
		pci_err(efx->pci_dev, "Failed to insert MPORT %08x, rc %d\n",
			desc->mport_id, rc);
		kfree(desc);
		return rc;
	}

	return rc;
}

void efx_mae_remove_mport(void *desc, void *arg)
{
	struct mae_mport_desc *mport = desc;

	synchronize_rcu();
	kfree(mport);
}

static int efx_mae_process_mport(struct efx_nic *efx,
				 struct mae_mport_desc *desc)
{
	struct ef100_nic_data *nic_data = efx->nic_data;
	struct mae_mport_desc *mport;

	mport = efx_mae_get_mport(efx, desc->mport_id);
	if (!IS_ERR_OR_NULL(mport)) {
		netif_err(efx, drv, efx->net_dev,
			  "mport with id %u does exist!!!\n", desc->mport_id);
		return -EEXIST;
	}

	if (nic_data->have_own_mport &&
	    desc->mport_id == nic_data->own_mport) {
		WARN_ON(desc->mport_type != MAE_MPORT_DESC_MPORT_TYPE_VNIC);
		WARN_ON(desc->vnic_client_type !=
			MAE_MPORT_DESC_VNIC_CLIENT_TYPE_FUNCTION);
		nic_data->local_mae_intf = desc->interface_idx;
		nic_data->have_local_intf = true;
		pci_dbg(efx->pci_dev, "MAE interface_idx is %u\n",
			nic_data->local_mae_intf);
	}

	return efx_mae_add_mport(efx, desc);
}

#define MCDI_MPORT_JOURNAL_LEN \
	ALIGN(MC_CMD_MAE_MPORT_READ_JOURNAL_OUT_LENMAX_MCDI2, 4)

int efx_mae_enumerate_mports(struct efx_nic *efx)
{
	efx_dword_t *outbuf = kzalloc(MCDI_MPORT_JOURNAL_LEN, GFP_KERNEL);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_MPORT_READ_JOURNAL_IN_LEN);
	MCDI_DECLARE_STRUCT_PTR(desc);
	size_t outlen, stride, count;
	int rc = 0, i;

	if (!outbuf)
		return -ENOMEM;
	do {
		rc = efx_mcdi_rpc(efx, MC_CMD_MAE_MPORT_READ_JOURNAL, inbuf,
				  sizeof(inbuf), outbuf,
				  MCDI_MPORT_JOURNAL_LEN, &outlen);
		if (rc)
			goto fail;
		if (outlen < MC_CMD_MAE_MPORT_READ_JOURNAL_OUT_MPORT_DESC_DATA_OFST) {
			rc = -EIO;
			goto fail;
		}
		count = MCDI_DWORD(outbuf, MAE_MPORT_READ_JOURNAL_OUT_MPORT_DESC_COUNT);
		if (!count)
			continue; /* not break; we want to look at MORE flag */
		stride = MCDI_DWORD(outbuf, MAE_MPORT_READ_JOURNAL_OUT_SIZEOF_MPORT_DESC);
		if (stride < MAE_MPORT_DESC_LEN) {
			rc = -EIO;
			goto fail;
		}
		if (outlen < MC_CMD_MAE_MPORT_READ_JOURNAL_OUT_LEN(count * stride)) {
			rc = -EIO;
			goto fail;
		}

		for (i = 0; i < count; i++) {
			struct mae_mport_desc *d;

			d = kzalloc(sizeof(*d), GFP_KERNEL);
			if (!d) {
				rc = -ENOMEM;
				goto fail;
			}

			desc = (efx_dword_t *)
				_MCDI_PTR(outbuf, MC_CMD_MAE_MPORT_READ_JOURNAL_OUT_MPORT_DESC_DATA_OFST +
					  i * stride);
			d->mport_id = MCDI_STRUCT_DWORD(desc, MAE_MPORT_DESC_MPORT_ID);
			d->flags = MCDI_STRUCT_DWORD(desc, MAE_MPORT_DESC_FLAGS);
			d->caller_flags = MCDI_STRUCT_DWORD(desc,
							    MAE_MPORT_DESC_CALLER_FLAGS);
			d->mport_type = MCDI_STRUCT_DWORD(desc,
							  MAE_MPORT_DESC_MPORT_TYPE);
			switch (d->mport_type) {
			case MAE_MPORT_DESC_MPORT_TYPE_NET_PORT:
				d->port_idx = MCDI_STRUCT_DWORD(desc,
								MAE_MPORT_DESC_NET_PORT_IDX);
				break;
			case MAE_MPORT_DESC_MPORT_TYPE_ALIAS:
				d->alias_mport_id = MCDI_STRUCT_DWORD(desc,
								      MAE_MPORT_DESC_ALIAS_DELIVER_MPORT_ID);
				break;
			case MAE_MPORT_DESC_MPORT_TYPE_VNIC:
				d->vnic_client_type = MCDI_STRUCT_DWORD(desc,
									MAE_MPORT_DESC_VNIC_CLIENT_TYPE);
				d->interface_idx = MCDI_STRUCT_DWORD(desc,
								     MAE_MPORT_DESC_VNIC_FUNCTION_INTERFACE);
				d->pf_idx = MCDI_STRUCT_WORD(desc,
							     MAE_MPORT_DESC_VNIC_FUNCTION_PF_IDX);
				d->vf_idx = MCDI_STRUCT_WORD(desc,
							     MAE_MPORT_DESC_VNIC_FUNCTION_VF_IDX);
				break;
			default:
				/* Unknown mport_type, just accept it */
				break;
			}
			rc = efx_mae_process_mport(efx, d);
			/* Any failure will be due to memory allocation faiure,
			 * so there is no point to try subsequent entries.
			 */
			if (rc)
				goto fail;
		}
	} while (MCDI_FIELD(outbuf, MAE_MPORT_READ_JOURNAL_OUT, MORE) &&
		 !WARN_ON(!count));
fail:
	kfree(outbuf);
	return rc;
}

int efx_mae_alloc_action_set(struct efx_nic *efx, struct efx_tc_action_set *act)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_ACTION_SET_ALLOC_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_ACTION_SET_ALLOC_IN_LEN);
	size_t outlen;
	int rc;

	MCDI_POPULATE_DWORD_3(inbuf, MAE_ACTION_SET_ALLOC_IN_FLAGS,
			      MAE_ACTION_SET_ALLOC_IN_VLAN_PUSH, act->vlan_push,
			      MAE_ACTION_SET_ALLOC_IN_VLAN_POP, act->vlan_pop,
			      MAE_ACTION_SET_ALLOC_IN_DECAP, act->decap);

	MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_SRC_MAC_ID,
		       MC_CMD_MAE_MAC_ADDR_ALLOC_OUT_MAC_ID_NULL);
	MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_DST_MAC_ID,
		       MC_CMD_MAE_MAC_ADDR_ALLOC_OUT_MAC_ID_NULL);
	if (act->count && !WARN_ON(!act->count->cnt))
		MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_COUNTER_ID,
			       act->count->cnt->fw_id);
	else
		MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_COUNTER_ID,
			       MC_CMD_MAE_COUNTER_ALLOC_OUT_COUNTER_ID_NULL);
	MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_COUNTER_LIST_ID,
		       MC_CMD_MAE_COUNTER_LIST_ALLOC_OUT_COUNTER_LIST_ID_NULL);
	if (act->vlan_push) {
		MCDI_SET_WORD_BE(inbuf, MAE_ACTION_SET_ALLOC_IN_VLAN0_TCI_BE,
				 act->vlan_tci[0]);
		MCDI_SET_WORD_BE(inbuf, MAE_ACTION_SET_ALLOC_IN_VLAN0_PROTO_BE,
				 act->vlan_proto[0]);
	}
	if (act->vlan_push >= 2) {
		MCDI_SET_WORD_BE(inbuf, MAE_ACTION_SET_ALLOC_IN_VLAN1_TCI_BE,
				 act->vlan_tci[1]);
		MCDI_SET_WORD_BE(inbuf, MAE_ACTION_SET_ALLOC_IN_VLAN1_PROTO_BE,
				 act->vlan_proto[1]);
	}
	MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_ENCAP_HEADER_ID,
		       MC_CMD_MAE_ENCAP_HEADER_ALLOC_OUT_ENCAP_HEADER_ID_NULL);
	if (act->deliver)
		MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_DELIVER,
			       act->dest_mport);
	BUILD_BUG_ON(MAE_MPORT_SELECTOR_NULL);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_ACTION_SET_ALLOC, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	act->fw_id = MCDI_DWORD(outbuf, MAE_ACTION_SET_ALLOC_OUT_AS_ID);
	/* We rely on the high bit of AS IDs always being clear.
	 * The firmware API guarantees this, but let's check it ourselves.
	 */
	if (WARN_ON_ONCE(efx_mae_asl_id(act->fw_id))) {
		efx_mae_free_action_set(efx, act->fw_id);
		return -EIO;
	}
	return 0;
}

int efx_mae_free_action_set(struct efx_nic *efx, u32 fw_id)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_ACTION_SET_FREE_OUT_LEN(1));
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_ACTION_SET_FREE_IN_LEN(1));
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_FREE_IN_AS_ID, fw_id);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_ACTION_SET_FREE, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	/* FW freed a different ID than we asked for, should never happen.
	 * Warn because it means we've now got a different idea to the FW of
	 * what action-sets exist, which could cause mayhem later.
	 */
	if (WARN_ON(MCDI_DWORD(outbuf, MAE_ACTION_SET_FREE_OUT_FREED_AS_ID) != fw_id))
		return -EIO;
	return 0;
}

int efx_mae_alloc_action_set_list(struct efx_nic *efx,
				  struct efx_tc_action_set_list *acts)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_ACTION_SET_LIST_ALLOC_OUT_LEN);
	struct efx_tc_action_set *act;
	size_t inlen, outlen, i = 0;
	efx_dword_t *inbuf;
	int rc;

	list_for_each_entry(act, &acts->list, list)
		i++;
	if (i == 0)
		return -EINVAL;
	if (i == 1) {
		/* Don't wrap an ASL around a single AS, just use the AS_ID
		 * directly.  ASLs are a more limited resource.
		 */
		act = list_first_entry(&acts->list, struct efx_tc_action_set, list);
		acts->fw_id = act->fw_id;
		return 0;
	}
	if (i > MC_CMD_MAE_ACTION_SET_LIST_ALLOC_IN_AS_IDS_MAXNUM_MCDI2)
		return -EOPNOTSUPP; /* Too many actions */
	inlen = MC_CMD_MAE_ACTION_SET_LIST_ALLOC_IN_LEN(i);
	inbuf = kzalloc(inlen, GFP_KERNEL);
	if (!inbuf)
		return -ENOMEM;
	i = 0;
	list_for_each_entry(act, &acts->list, list) {
		MCDI_SET_ARRAY_DWORD(inbuf, MAE_ACTION_SET_LIST_ALLOC_IN_AS_IDS,
				     i, act->fw_id);
		i++;
	}
	MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_LIST_ALLOC_IN_COUNT, i);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_ACTION_SET_LIST_ALLOC, inbuf, inlen,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto out_free;
	if (outlen < sizeof(outbuf)) {
		rc = -EIO;
		goto out_free;
	}
	acts->fw_id = MCDI_DWORD(outbuf, MAE_ACTION_SET_LIST_ALLOC_OUT_ASL_ID);
	/* We rely on the high bit of ASL IDs always being set.
	 * The firmware API guarantees this, but let's check it ourselves.
	 */
	if (WARN_ON_ONCE(!efx_mae_asl_id(acts->fw_id))) {
		efx_mae_free_action_set_list(efx, acts);
		rc = -EIO;
	}
out_free:
	kfree(inbuf);
	return rc;
}

int efx_mae_free_action_set_list(struct efx_nic *efx,
				 struct efx_tc_action_set_list *acts)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_ACTION_SET_LIST_FREE_OUT_LEN(1));
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_ACTION_SET_LIST_FREE_IN_LEN(1));
	size_t outlen;
	int rc;

	/* If this is just an AS_ID with no ASL wrapper, then there is
	 * nothing for us to free.  (The AS will be freed later.)
	 */
	if (efx_mae_asl_id(acts->fw_id)) {
		MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_LIST_FREE_IN_ASL_ID,
			       acts->fw_id);
		rc = efx_mcdi_rpc(efx, MC_CMD_MAE_ACTION_SET_LIST_FREE, inbuf,
				  sizeof(inbuf), outbuf, sizeof(outbuf), &outlen);
		if (rc)
			return rc;
		if (outlen < sizeof(outbuf))
			return -EIO;
		/* FW freed a different ID than we asked for, should never happen.
		 * Warn because it means we've now got a different idea to the FW of
		 * what action-set-lists exist, which could cause mayhem later.
		 */
		if (WARN_ON(MCDI_DWORD(outbuf, MAE_ACTION_SET_LIST_FREE_OUT_FREED_ASL_ID) != acts->fw_id))
			return -EIO;
	}
	/* We're probably about to free @acts, but let's just make sure its
	 * fw_id is blatted so that it won't look valid if it leaks out.
	 */
	acts->fw_id = MC_CMD_MAE_ACTION_SET_LIST_ALLOC_OUT_ACTION_SET_LIST_ID_NULL;
	return 0;
}

int efx_mae_register_encap_match(struct efx_nic *efx,
				 struct efx_tc_encap_match *encap)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_OUTER_RULE_INSERT_IN_LEN(MAE_ENC_FIELD_PAIRS_LEN));
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_OUTER_RULE_INSERT_OUT_LEN);
	MCDI_DECLARE_STRUCT_PTR(match_crit);
	size_t outlen;
	int rc;

	rc = efx_mae_encap_type_to_mae_type(encap->tun_type);
	if (rc < 0)
		return rc;
	match_crit = _MCDI_DWORD(inbuf, MAE_OUTER_RULE_INSERT_IN_FIELD_MATCH_CRITERIA);
	/* The struct contains IP src and dst, and udp dport.
	 * So we actually need to filter on IP src and dst, L4 dport, and
	 * ipproto == udp.
	 */
	MCDI_SET_DWORD(inbuf, MAE_OUTER_RULE_INSERT_IN_ENCAP_TYPE, rc);
#ifdef CONFIG_IPV6
	if (encap->src_ip | encap->dst_ip) {
#endif
		MCDI_STRUCT_SET_DWORD_BE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_SRC_IP4_BE,
					 encap->src_ip);
		MCDI_STRUCT_SET_DWORD_BE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_SRC_IP4_BE_MASK,
					 ~(__be32)0);
		MCDI_STRUCT_SET_DWORD_BE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_DST_IP4_BE,
					 encap->dst_ip);
		MCDI_STRUCT_SET_DWORD_BE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_DST_IP4_BE_MASK,
					 ~(__be32)0);
		MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_ETHER_TYPE_BE,
					htons(ETH_P_IP));
#ifdef CONFIG_IPV6
	} else {
		memcpy(MCDI_STRUCT_PTR(match_crit, MAE_ENC_FIELD_PAIRS_ENC_SRC_IP6_BE),
		       &encap->src_ip6, sizeof(encap->src_ip6));
		memset(MCDI_STRUCT_PTR(match_crit, MAE_ENC_FIELD_PAIRS_ENC_SRC_IP6_BE_MASK),
		       0xff, sizeof(encap->src_ip6));
		memcpy(MCDI_STRUCT_PTR(match_crit, MAE_ENC_FIELD_PAIRS_ENC_DST_IP6_BE),
		       &encap->dst_ip6, sizeof(encap->dst_ip6));
		memset(MCDI_STRUCT_PTR(match_crit, MAE_ENC_FIELD_PAIRS_ENC_DST_IP6_BE_MASK),
		       0xff, sizeof(encap->dst_ip6));
		MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_ETHER_TYPE_BE,
					htons(ETH_P_IPV6));
	}
#endif
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_ETHER_TYPE_BE_MASK,
				~(__be16)0);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_L4_DPORT_BE,
				encap->udp_dport);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_L4_DPORT_BE_MASK,
				~(__be16)0);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_IP_PROTO, IPPROTO_UDP);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_ENC_FIELD_PAIRS_ENC_IP_PROTO_MASK, ~0);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_OUTER_RULE_INSERT, inbuf,
			  sizeof(inbuf), outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	encap->fw_id = MCDI_DWORD(outbuf, MAE_OUTER_RULE_INSERT_OUT_OR_ID);
	return 0;
}

int efx_mae_unregister_encap_match(struct efx_nic *efx,
				   struct efx_tc_encap_match *encap)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_OUTER_RULE_REMOVE_OUT_LEN(1));
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_OUTER_RULE_REMOVE_IN_LEN(1));
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, MAE_OUTER_RULE_REMOVE_IN_OR_ID, encap->fw_id);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_OUTER_RULE_REMOVE, inbuf,
			  sizeof(inbuf), outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	/* FW freed a different ID than we asked for, should also never happen.
	 * Warn because it means we've now got a different idea to the FW of
	 * what encap_mds exist, which could cause mayhem later.
	 */
	if (WARN_ON(MCDI_DWORD(outbuf, MAE_OUTER_RULE_REMOVE_OUT_REMOVED_OR_ID) != encap->fw_id))
		return -EIO;
	/* We're probably about to free @encap, but let's just make sure its
	 * fw_id is blatted so that it won't look valid if it leaks out.
	 */
	encap->fw_id = MC_CMD_MAE_OUTER_RULE_INSERT_OUT_OUTER_RULE_ID_NULL;
	return 0;
}

static int efx_mae_populate_match_criteria(MCDI_DECLARE_STRUCT_PTR(match_crit),
					   const struct efx_tc_match *match)
{
	if (match->mask.ingress_port) {
		if (~match->mask.ingress_port)
			return -EOPNOTSUPP;
		MCDI_STRUCT_SET_DWORD(match_crit,
				      MAE_FIELD_MASK_VALUE_PAIRS_V2_INGRESS_MPORT_SELECTOR,
				      match->value.ingress_port);
	}
	MCDI_STRUCT_SET_DWORD(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_INGRESS_MPORT_SELECTOR_MASK,
			      match->mask.ingress_port);
	EFX_POPULATE_DWORD_2(*_MCDI_STRUCT_DWORD(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_FLAGS),
			     MAE_FIELD_MASK_VALUE_PAIRS_V2_IS_IP_FRAG,
			     match->value.ip_frag,
			     MAE_FIELD_MASK_VALUE_PAIRS_V2_IP_FIRST_FRAG,
			     match->value.ip_firstfrag);
	EFX_POPULATE_DWORD_2(*_MCDI_STRUCT_DWORD(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_FLAGS_MASK),
			     MAE_FIELD_MASK_VALUE_PAIRS_V2_IS_IP_FRAG,
			     match->mask.ip_frag,
			     MAE_FIELD_MASK_VALUE_PAIRS_V2_IP_FIRST_FRAG,
			     match->mask.ip_firstfrag);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_RECIRC_ID,
			     match->value.recirc_id);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_RECIRC_ID_MASK,
			     match->mask.recirc_id);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_ETHER_TYPE_BE,
				match->value.eth_proto);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_ETHER_TYPE_BE_MASK,
				match->mask.eth_proto);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_VLAN0_TCI_BE,
				match->value.vlan_tci[0]);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_VLAN0_TCI_BE_MASK,
				match->mask.vlan_tci[0]);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_VLAN0_PROTO_BE,
				match->value.vlan_proto[0]);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_VLAN0_PROTO_BE_MASK,
				match->mask.vlan_proto[0]);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_VLAN1_TCI_BE,
				match->value.vlan_tci[1]);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_VLAN1_TCI_BE_MASK,
				match->mask.vlan_tci[1]);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_VLAN1_PROTO_BE,
				match->value.vlan_proto[1]);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_VLAN1_PROTO_BE_MASK,
				match->mask.vlan_proto[1]);
	memcpy(MCDI_STRUCT_PTR(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_ETH_SADDR_BE),
	       match->value.eth_saddr, ETH_ALEN);
	memcpy(MCDI_STRUCT_PTR(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_ETH_SADDR_BE_MASK),
	       match->mask.eth_saddr, ETH_ALEN);
	memcpy(MCDI_STRUCT_PTR(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_ETH_DADDR_BE),
	       match->value.eth_daddr, ETH_ALEN);
	memcpy(MCDI_STRUCT_PTR(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_ETH_DADDR_BE_MASK),
	       match->mask.eth_daddr, ETH_ALEN);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_IP_PROTO,
			     match->value.ip_proto);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_IP_PROTO_MASK,
			     match->mask.ip_proto);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_IP_TOS,
			     match->value.ip_tos);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_IP_TOS_MASK,
			     match->mask.ip_tos);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_IP_TTL,
			     match->value.ip_ttl);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_IP_TTL_MASK,
			     match->mask.ip_ttl);
	MCDI_STRUCT_SET_DWORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP4_BE,
				 match->value.src_ip);
	MCDI_STRUCT_SET_DWORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP4_BE_MASK,
				 match->mask.src_ip);
	MCDI_STRUCT_SET_DWORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_DST_IP4_BE,
				 match->value.dst_ip);
	MCDI_STRUCT_SET_DWORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_DST_IP4_BE_MASK,
				 match->mask.dst_ip);
#ifdef CONFIG_IPV6
	memcpy(MCDI_STRUCT_PTR(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP6_BE),
	       &match->value.src_ip6, sizeof(struct in6_addr));
	memcpy(MCDI_STRUCT_PTR(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP6_BE_MASK),
	       &match->mask.src_ip6, sizeof(struct in6_addr));
	memcpy(MCDI_STRUCT_PTR(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_DST_IP6_BE),
	       &match->value.dst_ip6, sizeof(struct in6_addr));
	memcpy(MCDI_STRUCT_PTR(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_DST_IP6_BE_MASK),
	       &match->mask.dst_ip6, sizeof(struct in6_addr));
#endif
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_L4_SPORT_BE,
				match->value.l4_sport);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_L4_SPORT_BE_MASK,
				match->mask.l4_sport);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_L4_DPORT_BE,
				match->value.l4_dport);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_L4_DPORT_BE_MASK,
				match->mask.l4_dport);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_TCP_FLAGS_BE,
				match->value.tcp_flags);
	MCDI_STRUCT_SET_WORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_TCP_FLAGS_BE_MASK,
				match->mask.tcp_flags);
	/* enc-keys are handled indirectly, through encap_match ID */
	if (match->encap) {
		MCDI_STRUCT_SET_DWORD(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_OUTER_RULE_ID,
				      match->encap->fw_id);
		MCDI_STRUCT_SET_DWORD(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_OUTER_RULE_ID_MASK,
				      U32_MAX);
		/* enc_keyid (VNI/VSID) is not part of the encap_match */
		MCDI_STRUCT_SET_DWORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_ENC_VNET_ID_BE,
					 match->value.enc_keyid);
		MCDI_STRUCT_SET_DWORD_BE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_ENC_VNET_ID_BE_MASK,
					 match->mask.enc_keyid);
	} else if (WARN_ON_ONCE(match->mask.enc_src_ip) ||
		   WARN_ON_ONCE(match->mask.enc_dst_ip) ||
		   WARN_ON_ONCE(!ipv6_addr_any(&match->mask.enc_src_ip6)) ||
		   WARN_ON_ONCE(!ipv6_addr_any(&match->mask.enc_dst_ip6)) ||
		   WARN_ON_ONCE(match->mask.enc_ip_tos) ||
		   WARN_ON_ONCE(match->mask.enc_ip_ttl) ||
		   WARN_ON_ONCE(match->mask.enc_sport) ||
		   WARN_ON_ONCE(match->mask.enc_dport) ||
		   WARN_ON_ONCE(match->mask.enc_keyid)) {
		/* No enc-keys should appear in a rule without an encap_match */
		return -EOPNOTSUPP;
	}
	return 0;
}

int efx_mae_insert_rule(struct efx_nic *efx, const struct efx_tc_match *match,
			u32 prio, u32 acts_id, u32 *id)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_ACTION_RULE_INSERT_IN_LEN(MAE_FIELD_MASK_VALUE_PAIRS_V2_LEN));
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_ACTION_RULE_INSERT_OUT_LEN);
	MCDI_DECLARE_STRUCT_PTR(match_crit);
	MCDI_DECLARE_STRUCT_PTR(response);
	size_t outlen;
	int rc;

	if (!id)
		return -EINVAL;

	match_crit = _MCDI_DWORD(inbuf, MAE_ACTION_RULE_INSERT_IN_MATCH_CRITERIA);
	response = _MCDI_DWORD(inbuf, MAE_ACTION_RULE_INSERT_IN_RESPONSE);
	if (efx_mae_asl_id(acts_id)) {
		MCDI_STRUCT_SET_DWORD(response, MAE_ACTION_RULE_RESPONSE_ASL_ID, acts_id);
		MCDI_STRUCT_SET_DWORD(response, MAE_ACTION_RULE_RESPONSE_AS_ID,
				      MC_CMD_MAE_ACTION_SET_ALLOC_OUT_ACTION_SET_ID_NULL);
	} else {
		/* We only had one AS, so we didn't wrap it in an ASL */
		MCDI_STRUCT_SET_DWORD(response, MAE_ACTION_RULE_RESPONSE_ASL_ID,
				      MC_CMD_MAE_ACTION_SET_LIST_ALLOC_OUT_ACTION_SET_LIST_ID_NULL);
		MCDI_STRUCT_SET_DWORD(response, MAE_ACTION_RULE_RESPONSE_AS_ID, acts_id);
	}
	MCDI_SET_DWORD(inbuf, MAE_ACTION_RULE_INSERT_IN_PRIO, prio);
	rc = efx_mae_populate_match_criteria(match_crit, match);
	if (rc)
		return rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_ACTION_RULE_INSERT, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	*id = MCDI_DWORD(outbuf, MAE_ACTION_RULE_INSERT_OUT_AR_ID);
	return 0;
}

int efx_mae_delete_rule(struct efx_nic *efx, u32 id)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_ACTION_RULE_DELETE_OUT_LEN(1));
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_ACTION_RULE_DELETE_IN_LEN(1));
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, MAE_ACTION_RULE_DELETE_IN_AR_ID, id);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_ACTION_RULE_DELETE, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	/* FW freed a different ID than we asked for, should also never happen.
	 * Warn because it means we've now got a different idea to the FW of
	 * what rules exist, which could cause mayhem later.
	 */
	if (WARN_ON(MCDI_DWORD(outbuf, MAE_ACTION_RULE_DELETE_OUT_DELETED_AR_ID) != id))
		return -EIO;
	return 0;
}

int efx_init_mae(struct efx_nic *efx)
{
	struct ef100_nic_data *nic_data = efx->nic_data;
	struct efx_mae *mae;
	int rc;

	if (!nic_data->have_mport)
		return -EINVAL;

	mae = kmalloc(sizeof(*mae), GFP_KERNEL);
	if (!mae)
		return -ENOMEM;

	rc = rhashtable_init(&mae->mports_ht, &efx_mae_mports_ht_params);
	if (rc < 0) {
		kfree(mae);
		return rc;
	}
	efx->mae = mae;
	mae->efx = efx;
	return 0;
}

void efx_fini_mae(struct efx_nic *efx)
{
	struct efx_mae *mae = efx->mae;

	kfree(mae);
	efx->mae = NULL;
}
