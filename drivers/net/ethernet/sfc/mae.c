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

#include "mae.h"
#include "mcdi.h"
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
int efx_mae_lookup_mport(struct efx_nic *efx, u32 selector, u32 *id)
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

	BUILD_BUG_ON(MC_CMD_MAE_GET_AR_CAPS_IN_LEN);

	rc = efx_mcdi_rpc(efx, cmd, NULL, 0, outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	count = MCDI_DWORD(outbuf, MAE_GET_AR_CAPS_OUT_COUNT);
	memset(field_support, MAE_FIELD_UNSUPPORTED, MAE_NUM_FIELDS);
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
	return efx_mae_get_rule_fields(efx, MC_CMD_MAE_GET_AR_CAPS,
				       caps->action_rule_fields);
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
		efx_tc_err(efx, "No support for %s mask in field ingress_port\n",
			   mask_type_name(ingress_port_mask_type));
		NL_SET_ERR_MSG_MOD(extack, "Unsupported mask type for ingress_port");
		return rc;
	}
	return 0;
}

static bool efx_mae_asl_id(u32 id)
{
	return !!(id & BIT(31));
}

int efx_mae_alloc_action_set(struct efx_nic *efx, struct efx_tc_action_set *act)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_ACTION_SET_ALLOC_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_ACTION_SET_ALLOC_IN_LEN);
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_SRC_MAC_ID,
		       MC_CMD_MAE_MAC_ADDR_ALLOC_OUT_MAC_ID_NULL);
	MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_DST_MAC_ID,
		       MC_CMD_MAE_MAC_ADDR_ALLOC_OUT_MAC_ID_NULL);
	MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_COUNTER_ID,
		       MC_CMD_MAE_COUNTER_ALLOC_OUT_COUNTER_ID_NULL);
	MCDI_SET_DWORD(inbuf, MAE_ACTION_SET_ALLOC_IN_COUNTER_LIST_ID,
		       MC_CMD_MAE_COUNTER_LIST_ALLOC_OUT_COUNTER_LIST_ID_NULL);
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
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_RECIRC_ID,
			     match->value.recirc_id);
	MCDI_STRUCT_SET_BYTE(match_crit, MAE_FIELD_MASK_VALUE_PAIRS_V2_RECIRC_ID_MASK,
			     match->mask.recirc_id);
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
