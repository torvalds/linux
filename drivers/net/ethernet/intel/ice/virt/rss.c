// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022, Intel Corporation. */

#include "ice_vf_lib_private.h"
#include "ice.h"

#define FIELD_SELECTOR(proto_hdr_field) \
		BIT((proto_hdr_field) & PROTO_HDR_FIELD_MASK)

struct ice_vc_hdr_match_type {
	u32 vc_hdr;	/* virtchnl headers (VIRTCHNL_PROTO_HDR_XXX) */
	u32 ice_hdr;	/* ice headers (ICE_FLOW_SEG_HDR_XXX) */
};

static const struct ice_vc_hdr_match_type ice_vc_hdr_list[] = {
	{VIRTCHNL_PROTO_HDR_NONE,	ICE_FLOW_SEG_HDR_NONE},
	{VIRTCHNL_PROTO_HDR_ETH,	ICE_FLOW_SEG_HDR_ETH},
	{VIRTCHNL_PROTO_HDR_S_VLAN,	ICE_FLOW_SEG_HDR_VLAN},
	{VIRTCHNL_PROTO_HDR_C_VLAN,	ICE_FLOW_SEG_HDR_VLAN},
	{VIRTCHNL_PROTO_HDR_IPV4,	ICE_FLOW_SEG_HDR_IPV4 |
					ICE_FLOW_SEG_HDR_IPV_OTHER},
	{VIRTCHNL_PROTO_HDR_IPV6,	ICE_FLOW_SEG_HDR_IPV6 |
					ICE_FLOW_SEG_HDR_IPV_OTHER},
	{VIRTCHNL_PROTO_HDR_TCP,	ICE_FLOW_SEG_HDR_TCP},
	{VIRTCHNL_PROTO_HDR_UDP,	ICE_FLOW_SEG_HDR_UDP},
	{VIRTCHNL_PROTO_HDR_SCTP,	ICE_FLOW_SEG_HDR_SCTP},
	{VIRTCHNL_PROTO_HDR_PPPOE,	ICE_FLOW_SEG_HDR_PPPOE},
	{VIRTCHNL_PROTO_HDR_GTPU_IP,	ICE_FLOW_SEG_HDR_GTPU_IP},
	{VIRTCHNL_PROTO_HDR_GTPU_EH,	ICE_FLOW_SEG_HDR_GTPU_EH},
	{VIRTCHNL_PROTO_HDR_GTPU_EH_PDU_DWN,
					ICE_FLOW_SEG_HDR_GTPU_DWN},
	{VIRTCHNL_PROTO_HDR_GTPU_EH_PDU_UP,
					ICE_FLOW_SEG_HDR_GTPU_UP},
	{VIRTCHNL_PROTO_HDR_L2TPV3,	ICE_FLOW_SEG_HDR_L2TPV3},
	{VIRTCHNL_PROTO_HDR_ESP,	ICE_FLOW_SEG_HDR_ESP},
	{VIRTCHNL_PROTO_HDR_AH,		ICE_FLOW_SEG_HDR_AH},
	{VIRTCHNL_PROTO_HDR_PFCP,	ICE_FLOW_SEG_HDR_PFCP_SESSION},
};

struct ice_vc_hash_field_match_type {
	u32 vc_hdr;		/* virtchnl headers
				 * (VIRTCHNL_PROTO_HDR_XXX)
				 */
	u32 vc_hash_field;	/* virtchnl hash fields selector
				 * FIELD_SELECTOR((VIRTCHNL_PROTO_HDR_ETH_XXX))
				 */
	u64 ice_hash_field;	/* ice hash fields
				 * (BIT_ULL(ICE_FLOW_FIELD_IDX_XXX))
				 */
};

static const struct
ice_vc_hash_field_match_type ice_vc_hash_field_list[] = {
	{VIRTCHNL_PROTO_HDR_ETH, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ETH_SRC),
		BIT_ULL(ICE_FLOW_FIELD_IDX_ETH_SA)},
	{VIRTCHNL_PROTO_HDR_ETH, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ETH_DST),
		BIT_ULL(ICE_FLOW_FIELD_IDX_ETH_DA)},
	{VIRTCHNL_PROTO_HDR_ETH, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ETH_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ETH_DST),
		ICE_FLOW_HASH_ETH},
	{VIRTCHNL_PROTO_HDR_ETH,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ETH_ETHERTYPE),
		BIT_ULL(ICE_FLOW_FIELD_IDX_ETH_TYPE)},
	{VIRTCHNL_PROTO_HDR_S_VLAN,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_S_VLAN_ID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_S_VLAN)},
	{VIRTCHNL_PROTO_HDR_C_VLAN,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_C_VLAN_ID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_C_VLAN)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST),
		ICE_FLOW_HASH_IPV4},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		ICE_FLOW_HASH_IPV4 | BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV4, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV4_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_SRC),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_SA)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_DST),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_DA)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_DST),
		ICE_FLOW_HASH_IPV6},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_SA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_DA) |
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_SRC) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_DST) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		ICE_FLOW_HASH_IPV6 | BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_IPV6, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_IPV6_PROT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_PROT)},
	{VIRTCHNL_PROTO_HDR_TCP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_SRC_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_SRC_PORT)},
	{VIRTCHNL_PROTO_HDR_TCP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_DST_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_DST_PORT)},
	{VIRTCHNL_PROTO_HDR_TCP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_TCP_DST_PORT),
		ICE_FLOW_HASH_TCP_PORT},
	{VIRTCHNL_PROTO_HDR_UDP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_SRC_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_SRC_PORT)},
	{VIRTCHNL_PROTO_HDR_UDP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_DST_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_DST_PORT)},
	{VIRTCHNL_PROTO_HDR_UDP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_UDP_DST_PORT),
		ICE_FLOW_HASH_UDP_PORT},
	{VIRTCHNL_PROTO_HDR_SCTP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_SRC_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT)},
	{VIRTCHNL_PROTO_HDR_SCTP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_DST_PORT),
		BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_DST_PORT)},
	{VIRTCHNL_PROTO_HDR_SCTP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_SRC_PORT) |
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_SCTP_DST_PORT),
		ICE_FLOW_HASH_SCTP_PORT},
	{VIRTCHNL_PROTO_HDR_PPPOE,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_PPPOE_SESS_ID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_PPPOE_SESS_ID)},
	{VIRTCHNL_PROTO_HDR_GTPU_IP,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_GTPU_IP_TEID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_GTPU_IP_TEID)},
	{VIRTCHNL_PROTO_HDR_L2TPV3,
		FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_L2TPV3_SESS_ID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_L2TPV3_SESS_ID)},
	{VIRTCHNL_PROTO_HDR_ESP, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_ESP_SPI),
		BIT_ULL(ICE_FLOW_FIELD_IDX_ESP_SPI)},
	{VIRTCHNL_PROTO_HDR_AH, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_AH_SPI),
		BIT_ULL(ICE_FLOW_FIELD_IDX_AH_SPI)},
	{VIRTCHNL_PROTO_HDR_PFCP, FIELD_SELECTOR(VIRTCHNL_PROTO_HDR_PFCP_SEID),
		BIT_ULL(ICE_FLOW_FIELD_IDX_PFCP_SEID)},
};

/**
 * ice_vc_validate_pattern
 * @vf: pointer to the VF info
 * @proto: virtchnl protocol headers
 *
 * validate the pattern is supported or not.
 *
 * Return: true on success, false on error.
 */
bool
ice_vc_validate_pattern(struct ice_vf *vf, struct virtchnl_proto_hdrs *proto)
{
	bool is_ipv4 = false;
	bool is_ipv6 = false;
	bool is_udp = false;
	u16 ptype = -1;
	int i = 0;

	while (i < proto->count &&
	       proto->proto_hdr[i].type != VIRTCHNL_PROTO_HDR_NONE) {
		switch (proto->proto_hdr[i].type) {
		case VIRTCHNL_PROTO_HDR_ETH:
			ptype = ICE_PTYPE_MAC_PAY;
			break;
		case VIRTCHNL_PROTO_HDR_IPV4:
			ptype = ICE_PTYPE_IPV4_PAY;
			is_ipv4 = true;
			break;
		case VIRTCHNL_PROTO_HDR_IPV6:
			ptype = ICE_PTYPE_IPV6_PAY;
			is_ipv6 = true;
			break;
		case VIRTCHNL_PROTO_HDR_UDP:
			if (is_ipv4)
				ptype = ICE_PTYPE_IPV4_UDP_PAY;
			else if (is_ipv6)
				ptype = ICE_PTYPE_IPV6_UDP_PAY;
			is_udp = true;
			break;
		case VIRTCHNL_PROTO_HDR_TCP:
			if (is_ipv4)
				ptype = ICE_PTYPE_IPV4_TCP_PAY;
			else if (is_ipv6)
				ptype = ICE_PTYPE_IPV6_TCP_PAY;
			break;
		case VIRTCHNL_PROTO_HDR_SCTP:
			if (is_ipv4)
				ptype = ICE_PTYPE_IPV4_SCTP_PAY;
			else if (is_ipv6)
				ptype = ICE_PTYPE_IPV6_SCTP_PAY;
			break;
		case VIRTCHNL_PROTO_HDR_GTPU_IP:
		case VIRTCHNL_PROTO_HDR_GTPU_EH:
			if (is_ipv4)
				ptype = ICE_MAC_IPV4_GTPU;
			else if (is_ipv6)
				ptype = ICE_MAC_IPV6_GTPU;
			goto out;
		case VIRTCHNL_PROTO_HDR_L2TPV3:
			if (is_ipv4)
				ptype = ICE_MAC_IPV4_L2TPV3;
			else if (is_ipv6)
				ptype = ICE_MAC_IPV6_L2TPV3;
			goto out;
		case VIRTCHNL_PROTO_HDR_ESP:
			if (is_ipv4)
				ptype = is_udp ? ICE_MAC_IPV4_NAT_T_ESP :
						ICE_MAC_IPV4_ESP;
			else if (is_ipv6)
				ptype = is_udp ? ICE_MAC_IPV6_NAT_T_ESP :
						ICE_MAC_IPV6_ESP;
			goto out;
		case VIRTCHNL_PROTO_HDR_AH:
			if (is_ipv4)
				ptype = ICE_MAC_IPV4_AH;
			else if (is_ipv6)
				ptype = ICE_MAC_IPV6_AH;
			goto out;
		case VIRTCHNL_PROTO_HDR_PFCP:
			if (is_ipv4)
				ptype = ICE_MAC_IPV4_PFCP_SESSION;
			else if (is_ipv6)
				ptype = ICE_MAC_IPV6_PFCP_SESSION;
			goto out;
		default:
			break;
		}
		i++;
	}

out:
	return ice_hw_ptype_ena(&vf->pf->hw, ptype);
}

/**
 * ice_vc_parse_rss_cfg - parses hash fields and headers from
 * a specific virtchnl RSS cfg
 * @hw: pointer to the hardware
 * @rss_cfg: pointer to the virtchnl RSS cfg
 * @hash_cfg: pointer to the HW hash configuration
 *
 * Return true if all the protocol header and hash fields in the RSS cfg could
 * be parsed, else return false
 *
 * This function parses the virtchnl RSS cfg to be the intended
 * hash fields and the intended header for RSS configuration
 */
static bool ice_vc_parse_rss_cfg(struct ice_hw *hw,
				 struct virtchnl_rss_cfg *rss_cfg,
				 struct ice_rss_hash_cfg *hash_cfg)
{
	const struct ice_vc_hash_field_match_type *hf_list;
	const struct ice_vc_hdr_match_type *hdr_list;
	int i, hf_list_len, hdr_list_len;
	u32 *addl_hdrs = &hash_cfg->addl_hdrs;
	u64 *hash_flds = &hash_cfg->hash_flds;

	/* set outer layer RSS as default */
	hash_cfg->hdr_type = ICE_RSS_OUTER_HEADERS;

	if (rss_cfg->rss_algorithm == VIRTCHNL_RSS_ALG_TOEPLITZ_SYMMETRIC)
		hash_cfg->symm = true;
	else
		hash_cfg->symm = false;

	hf_list = ice_vc_hash_field_list;
	hf_list_len = ARRAY_SIZE(ice_vc_hash_field_list);
	hdr_list = ice_vc_hdr_list;
	hdr_list_len = ARRAY_SIZE(ice_vc_hdr_list);

	for (i = 0; i < rss_cfg->proto_hdrs.count; i++) {
		struct virtchnl_proto_hdr *proto_hdr =
					&rss_cfg->proto_hdrs.proto_hdr[i];
		bool hdr_found = false;
		int j;

		/* Find matched ice headers according to virtchnl headers. */
		for (j = 0; j < hdr_list_len; j++) {
			struct ice_vc_hdr_match_type hdr_map = hdr_list[j];

			if (proto_hdr->type == hdr_map.vc_hdr) {
				*addl_hdrs |= hdr_map.ice_hdr;
				hdr_found = true;
			}
		}

		if (!hdr_found)
			return false;

		/* Find matched ice hash fields according to
		 * virtchnl hash fields.
		 */
		for (j = 0; j < hf_list_len; j++) {
			struct ice_vc_hash_field_match_type hf_map = hf_list[j];

			if (proto_hdr->type == hf_map.vc_hdr &&
			    proto_hdr->field_selector == hf_map.vc_hash_field) {
				*hash_flds |= hf_map.ice_hash_field;
				break;
			}
		}
	}

	return true;
}

/**
 * ice_vf_adv_rss_offload_ena - determine if capabilities support advanced
 * RSS offloads
 * @caps: VF driver negotiated capabilities
 *
 * Return true if VIRTCHNL_VF_OFFLOAD_ADV_RSS_PF capability is set,
 * else return false
 */
static bool ice_vf_adv_rss_offload_ena(u32 caps)
{
	return !!(caps & VIRTCHNL_VF_OFFLOAD_ADV_RSS_PF);
}

/**
 * ice_vc_handle_rss_cfg
 * @vf: pointer to the VF info
 * @msg: pointer to the message buffer
 * @add: add a RSS config if true, otherwise delete a RSS config
 *
 * This function adds/deletes a RSS config
 */
static int ice_vc_handle_rss_cfg(struct ice_vf *vf, u8 *msg, bool add)
{
	u32 v_opcode = add ? VIRTCHNL_OP_ADD_RSS_CFG : VIRTCHNL_OP_DEL_RSS_CFG;
	struct virtchnl_rss_cfg *rss_cfg = (struct virtchnl_rss_cfg *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_hw *hw = &vf->pf->hw;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		dev_dbg(dev, "VF %d attempting to configure RSS, but RSS is not supported by the PF\n",
			vf->vf_id);
		v_ret = VIRTCHNL_STATUS_ERR_NOT_SUPPORTED;
		goto error_param;
	}

	if (!ice_vf_adv_rss_offload_ena(vf->driver_caps)) {
		dev_dbg(dev, "VF %d attempting to configure RSS, but Advanced RSS offload is not supported\n",
			vf->vf_id);
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (rss_cfg->proto_hdrs.count > VIRTCHNL_MAX_NUM_PROTO_HDRS ||
	    rss_cfg->rss_algorithm < VIRTCHNL_RSS_ALG_TOEPLITZ_ASYMMETRIC ||
	    rss_cfg->rss_algorithm > VIRTCHNL_RSS_ALG_XOR_SYMMETRIC) {
		dev_dbg(dev, "VF %d attempting to configure RSS, but RSS configuration is not valid\n",
			vf->vf_id);
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_validate_pattern(vf, &rss_cfg->proto_hdrs)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (rss_cfg->rss_algorithm == VIRTCHNL_RSS_ALG_R_ASYMMETRIC) {
		struct ice_vsi_ctx *ctx;
		u8 lut_type, hash_type;
		int status;

		lut_type = ICE_AQ_VSI_Q_OPT_RSS_LUT_VSI;
		hash_type = add ? ICE_AQ_VSI_Q_OPT_RSS_HASH_XOR :
				ICE_AQ_VSI_Q_OPT_RSS_HASH_TPLZ;

		ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
		if (!ctx) {
			v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
			goto error_param;
		}

		ctx->info.q_opt_rss =
			FIELD_PREP(ICE_AQ_VSI_Q_OPT_RSS_LUT_M, lut_type) |
			FIELD_PREP(ICE_AQ_VSI_Q_OPT_RSS_HASH_M, hash_type);

		/* Preserve existing queueing option setting */
		ctx->info.q_opt_rss |= (vsi->info.q_opt_rss &
					  ICE_AQ_VSI_Q_OPT_RSS_GBL_LUT_M);
		ctx->info.q_opt_tc = vsi->info.q_opt_tc;
		ctx->info.q_opt_flags = vsi->info.q_opt_rss;

		ctx->info.valid_sections =
				cpu_to_le16(ICE_AQ_VSI_PROP_Q_OPT_VALID);

		status = ice_update_vsi(hw, vsi->idx, ctx, NULL);
		if (status) {
			dev_err(dev, "update VSI for RSS failed, err %d aq_err %s\n",
				status, libie_aq_str(hw->adminq.sq_last_status));
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		} else {
			vsi->info.q_opt_rss = ctx->info.q_opt_rss;
		}

		kfree(ctx);
	} else {
		struct ice_rss_hash_cfg cfg;

		/* Only check for none raw pattern case */
		if (!ice_vc_validate_pattern(vf, &rss_cfg->proto_hdrs)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}
		cfg.addl_hdrs = ICE_FLOW_SEG_HDR_NONE;
		cfg.hash_flds = ICE_HASH_INVALID;
		cfg.hdr_type = ICE_RSS_ANY_HEADERS;

		if (!ice_vc_parse_rss_cfg(hw, rss_cfg, &cfg)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto error_param;
		}

		if (add) {
			if (ice_add_rss_cfg(hw, vsi, &cfg)) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				dev_err(dev, "ice_add_rss_cfg failed for vsi = %d, v_ret = %d\n",
					vsi->vsi_num, v_ret);
			}
		} else {
			int status;

			status = ice_rem_rss_cfg(hw, vsi->idx, &cfg);
			/* We just ignore -ENOENT, because if two configurations
			 * share the same profile remove one of them actually
			 * removes both, since the profile is deleted.
			 */
			if (status && status != -ENOENT) {
				v_ret = VIRTCHNL_STATUS_ERR_PARAM;
				dev_err(dev, "ice_rem_rss_cfg failed for VF ID:%d, error:%d\n",
					vf->vf_id, status);
			}
		}
	}

error_param:
	return ice_vc_send_msg_to_vf(vf, v_opcode, v_ret, NULL, 0);
}

/**
 * ice_vc_config_rss_key
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Configure the VF's RSS key
 */
static int ice_vc_config_rss_key(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_rss_key *vrk =
		(struct virtchnl_rss_key *)msg;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vrk->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vrk->key_len != ICE_VSIQF_HKEY_ARRAY_SIZE) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (ice_set_rss_key(vsi, vrk->key))
		v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_RSS_KEY, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_config_rss_lut
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Configure the VF's RSS LUT
 */
static int ice_vc_config_rss_lut(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_rss_lut *vrl = (struct virtchnl_rss_lut *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vrl->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vrl->lut_entries != ICE_LUT_VSI_SIZE) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (ice_set_rss_lut(vsi, vrl->lut, ICE_LUT_VSI_SIZE))
		v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_RSS_LUT, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_config_rss_hfunc
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Configure the VF's RSS Hash function
 */
static int ice_vc_config_rss_hfunc(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_rss_hfunc *vrh = (struct virtchnl_rss_hfunc *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	u8 hfunc = ICE_AQ_VSI_Q_OPT_RSS_HASH_TPLZ;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vrh->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto error_param;
	}

	if (vrh->rss_algorithm == VIRTCHNL_RSS_ALG_TOEPLITZ_SYMMETRIC)
		hfunc = ICE_AQ_VSI_Q_OPT_RSS_HASH_SYM_TPLZ;

	if (ice_set_rss_hfunc(vsi, hfunc))
		v_ret = VIRTCHNL_STATUS_ERR_ADMIN_QUEUE_ERROR;
error_param:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_RSS_HFUNC, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_get_rss_hashcfg - return the RSS Hash configuration
 * @vf: pointer to the VF info
 */
static int ice_vc_get_rss_hashcfg(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_rss_hashcfg *vrh = NULL;
	int len = 0, ret;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, vf->pf->flags)) {
		dev_err(ice_pf_to_dev(vf->pf), "RSS not supported by PF\n");
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	len = sizeof(struct virtchnl_rss_hashcfg);
	vrh = kzalloc(len, GFP_KERNEL);
	if (!vrh) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		len = 0;
		goto err;
	}

	vrh->hashcfg = ICE_DEFAULT_RSS_HASHCFG;
err:
	/* send the response back to the VF */
	ret = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_RSS_HASHCFG_CAPS, v_ret,
				    (u8 *)vrh, len);
	kfree(vrh);
	return ret;
}

/**
 * ice_vc_set_rss_hashcfg - set RSS Hash configuration bits for the VF
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 */
static int ice_vc_set_rss_hashcfg(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_rss_hashcfg *vrh = (struct virtchnl_rss_hashcfg *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vsi;
	struct device *dev;
	int status;

	dev = ice_pf_to_dev(pf);

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (!test_bit(ICE_FLAG_RSS_ENA, pf->flags)) {
		dev_err(dev, "RSS not supported by PF\n");
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	/* clear all previously programmed RSS configuration to allow VF drivers
	 * the ability to customize the RSS configuration and/or completely
	 * disable RSS
	 */
	status = ice_rem_vsi_rss_cfg(&pf->hw, vsi->idx);
	if (status && !vrh->hashcfg) {
		/* only report failure to clear the current RSS configuration if
		 * that was clearly the VF's intention (i.e. vrh->hashcfg = 0)
		 */
		v_ret = ice_err_to_virt_err(status);
		goto err;
	} else if (status) {
		/* allow the VF to update the RSS configuration even on failure
		 * to clear the current RSS confguration in an attempt to keep
		 * RSS in a working state
		 */
		dev_warn(dev, "Failed to clear the RSS configuration for VF %u\n",
			 vf->vf_id);
	}

	if (vrh->hashcfg) {
		status = ice_add_avf_rss_cfg(&pf->hw, vsi, vrh->hashcfg);
		v_ret = ice_err_to_virt_err(status);
	}

	/* save the requested VF configuration */
	if (!v_ret)
		vf->rss_hashcfg = vrh->hashcfg;

	/* send the response to the VF */
err:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_SET_RSS_HASHCFG, v_ret,
				     NULL, 0);
}

/**
 * ice_vc_query_rxdid - query RXDID supported by DDP package
 * @vf: pointer to VF info
 *
 * Called from VF to query a bitmap of supported flexible
 * descriptor RXDIDs of a DDP package.
 */
static int ice_vc_query_rxdid(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_pf *pf = vf->pf;
	u64 rxdid;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	if (!(vf->driver_caps & VIRTCHNL_VF_OFFLOAD_RX_FLEX_DESC)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto err;
	}

	rxdid = pf->supported_rxdids;

err:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_SUPPORTED_RXDIDS,
				     v_ret, (u8 *)&rxdid, sizeof(rxdid));
}

/**
 * ice_vf_init_vlan_stripping - enable/disable VLAN stripping on initialization
 * @vf: VF to enable/disable VLAN stripping for on initialization
 *
 * Set the default for VLAN stripping based on whether a port VLAN is configured
 * and the current VLAN mode of the device.
 */
static int ice_vf_init_vlan_stripping(struct ice_vf *vf)
{
	struct ice_vsi *vsi = ice_get_vf_vsi(vf);

	vf->vlan_strip_ena = 0;

	if (!vsi)
		return -EINVAL;

	/* don't modify stripping if port VLAN is configured in SVM since the
	 * port VLAN is based on the inner/single VLAN in SVM
	 */
	if (ice_vf_is_port_vlan_ena(vf) && !ice_is_dvm_ena(&vsi->back->hw))
		return 0;

	if (ice_vf_vlan_offload_ena(vf->driver_caps)) {
		int err;

		err = vsi->inner_vlan_ops.ena_stripping(vsi, ETH_P_8021Q);
		if (!err)
			vf->vlan_strip_ena |= ICE_INNER_VLAN_STRIP_ENA;
		return err;
	}

	return vsi->inner_vlan_ops.dis_stripping(vsi);
}

static u16 ice_vc_get_max_vlan_fltrs(struct ice_vf *vf)
{
	if (vf->trusted)
		return VLAN_N_VID;
	else
		return ICE_MAX_VLAN_PER_VF;
}

/**
 * ice_vf_outer_vlan_not_allowed - check if outer VLAN can be used
 * @vf: VF that being checked for
 *
 * When the device is in double VLAN mode, check whether or not the outer VLAN
 * is allowed.
 */
static bool ice_vf_outer_vlan_not_allowed(struct ice_vf *vf)
{
	if (ice_vf_is_port_vlan_ena(vf))
		return true;

	return false;
}

/**
 * ice_vc_set_dvm_caps - set VLAN capabilities when the device is in DVM
 * @vf: VF that capabilities are being set for
 * @caps: VLAN capabilities to populate
 *
 * Determine VLAN capabilities support based on whether a port VLAN is
 * configured. If a port VLAN is configured then the VF should use the inner
 * filtering/offload capabilities since the port VLAN is using the outer VLAN
 * capabilies.
 */
static void
ice_vc_set_dvm_caps(struct ice_vf *vf, struct virtchnl_vlan_caps *caps)
{
	struct virtchnl_vlan_supported_caps *supported_caps;

	if (ice_vf_outer_vlan_not_allowed(vf)) {
		/* until support for inner VLAN filtering is added when a port
		 * VLAN is configured, only support software offloaded inner
		 * VLANs when a port VLAN is confgured in DVM
		 */
		supported_caps = &caps->filtering.filtering_support;
		supported_caps->inner = VIRTCHNL_VLAN_UNSUPPORTED;

		supported_caps = &caps->offloads.stripping_support;
		supported_caps->inner = VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		supported_caps = &caps->offloads.insertion_support;
		supported_caps->inner = VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		caps->offloads.ethertype_init = VIRTCHNL_VLAN_ETHERTYPE_8100;
		caps->offloads.ethertype_match =
			VIRTCHNL_ETHERTYPE_STRIPPING_MATCHES_INSERTION;
	} else {
		supported_caps = &caps->filtering.filtering_support;
		supported_caps->inner = VIRTCHNL_VLAN_UNSUPPORTED;
		supported_caps->outer = VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_ETHERTYPE_88A8 |
					VIRTCHNL_VLAN_ETHERTYPE_9100 |
					VIRTCHNL_VLAN_ETHERTYPE_AND;
		caps->filtering.ethertype_init = VIRTCHNL_VLAN_ETHERTYPE_8100 |
						 VIRTCHNL_VLAN_ETHERTYPE_88A8 |
						 VIRTCHNL_VLAN_ETHERTYPE_9100;

		supported_caps = &caps->offloads.stripping_support;
		supported_caps->inner = VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_ETHERTYPE_88A8 |
					VIRTCHNL_VLAN_ETHERTYPE_9100 |
					VIRTCHNL_VLAN_ETHERTYPE_XOR |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG2_2;

		supported_caps = &caps->offloads.insertion_support;
		supported_caps->inner = VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_ETHERTYPE_88A8 |
					VIRTCHNL_VLAN_ETHERTYPE_9100 |
					VIRTCHNL_VLAN_ETHERTYPE_XOR |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG2;

		caps->offloads.ethertype_init = VIRTCHNL_VLAN_ETHERTYPE_8100;

		caps->offloads.ethertype_match =
			VIRTCHNL_ETHERTYPE_STRIPPING_MATCHES_INSERTION;
	}

	caps->filtering.max_filters = ice_vc_get_max_vlan_fltrs(vf);
}

/**
 * ice_vc_set_svm_caps - set VLAN capabilities when the device is in SVM
 * @vf: VF that capabilities are being set for
 * @caps: VLAN capabilities to populate
 *
 * Determine VLAN capabilities support based on whether a port VLAN is
 * configured. If a port VLAN is configured then the VF does not have any VLAN
 * filtering or offload capabilities since the port VLAN is using the inner VLAN
 * capabilities in single VLAN mode (SVM). Otherwise allow the VF to use inner
 * VLAN fitlering and offload capabilities.
 */
static void
ice_vc_set_svm_caps(struct ice_vf *vf, struct virtchnl_vlan_caps *caps)
{
	struct virtchnl_vlan_supported_caps *supported_caps;

	if (ice_vf_is_port_vlan_ena(vf)) {
		supported_caps = &caps->filtering.filtering_support;
		supported_caps->inner = VIRTCHNL_VLAN_UNSUPPORTED;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		supported_caps = &caps->offloads.stripping_support;
		supported_caps->inner = VIRTCHNL_VLAN_UNSUPPORTED;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		supported_caps = &caps->offloads.insertion_support;
		supported_caps->inner = VIRTCHNL_VLAN_UNSUPPORTED;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		caps->offloads.ethertype_init = VIRTCHNL_VLAN_UNSUPPORTED;
		caps->offloads.ethertype_match = VIRTCHNL_VLAN_UNSUPPORTED;
		caps->filtering.max_filters = 0;
	} else {
		supported_caps = &caps->filtering.filtering_support;
		supported_caps->inner = VIRTCHNL_VLAN_ETHERTYPE_8100;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;
		caps->filtering.ethertype_init = VIRTCHNL_VLAN_ETHERTYPE_8100;

		supported_caps = &caps->offloads.stripping_support;
		supported_caps->inner = VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		supported_caps = &caps->offloads.insertion_support;
		supported_caps->inner = VIRTCHNL_VLAN_ETHERTYPE_8100 |
					VIRTCHNL_VLAN_TOGGLE |
					VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1;
		supported_caps->outer = VIRTCHNL_VLAN_UNSUPPORTED;

		caps->offloads.ethertype_init = VIRTCHNL_VLAN_ETHERTYPE_8100;
		caps->offloads.ethertype_match =
			VIRTCHNL_ETHERTYPE_STRIPPING_MATCHES_INSERTION;
		caps->filtering.max_filters = ice_vc_get_max_vlan_fltrs(vf);
	}
}

/**
 * ice_vc_get_offload_vlan_v2_caps - determine VF's VLAN capabilities
 * @vf: VF to determine VLAN capabilities for
 *
 * This will only be called if the VF and PF successfully negotiated
 * VIRTCHNL_VF_OFFLOAD_VLAN_V2.
 *
 * Set VLAN capabilities based on the current VLAN mode and whether a port VLAN
 * is configured or not.
 */
static int ice_vc_get_offload_vlan_v2_caps(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_caps *caps = NULL;
	int err, len = 0;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	caps = kzalloc(sizeof(*caps), GFP_KERNEL);
	if (!caps) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		goto out;
	}
	len = sizeof(*caps);

	if (ice_is_dvm_ena(&vf->pf->hw))
		ice_vc_set_dvm_caps(vf, caps);
	else
		ice_vc_set_svm_caps(vf, caps);

	/* store negotiated caps to prevent invalid VF messages */
	memcpy(&vf->vlan_v2_caps, caps, sizeof(*caps));

out:
	err = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS,
				    v_ret, (u8 *)caps, len);
	kfree(caps);
	return err;
}

/**
 * ice_vc_validate_vlan_tpid - validate VLAN TPID
 * @filtering_caps: negotiated/supported VLAN filtering capabilities
 * @tpid: VLAN TPID used for validation
 *
 * Convert the VLAN TPID to a VIRTCHNL_VLAN_ETHERTYPE_* and then compare against
 * the negotiated/supported filtering caps to see if the VLAN TPID is valid.
 */
static bool ice_vc_validate_vlan_tpid(u16 filtering_caps, u16 tpid)
{
	enum virtchnl_vlan_support vlan_ethertype = VIRTCHNL_VLAN_UNSUPPORTED;

	switch (tpid) {
	case ETH_P_8021Q:
		vlan_ethertype = VIRTCHNL_VLAN_ETHERTYPE_8100;
		break;
	case ETH_P_8021AD:
		vlan_ethertype = VIRTCHNL_VLAN_ETHERTYPE_88A8;
		break;
	case ETH_P_QINQ1:
		vlan_ethertype = VIRTCHNL_VLAN_ETHERTYPE_9100;
		break;
	}

	if (!(filtering_caps & vlan_ethertype))
		return false;

	return true;
}

/**
 * ice_vc_is_valid_vlan - validate the virtchnl_vlan
 * @vc_vlan: virtchnl_vlan to validate
 *
 * If the VLAN TCI and VLAN TPID are 0, then this filter is invalid, so return
 * false. Otherwise return true.
 */
static bool ice_vc_is_valid_vlan(struct virtchnl_vlan *vc_vlan)
{
	if (!vc_vlan->tci || !vc_vlan->tpid)
		return false;

	return true;
}

/**
 * ice_vc_validate_vlan_filter_list - validate the filter list from the VF
 * @vfc: negotiated/supported VLAN filtering capabilities
 * @vfl: VLAN filter list from VF to validate
 *
 * Validate all of the filters in the VLAN filter list from the VF. If any of
 * the checks fail then return false. Otherwise return true.
 */
static bool
ice_vc_validate_vlan_filter_list(struct virtchnl_vlan_filtering_caps *vfc,
				 struct virtchnl_vlan_filter_list_v2 *vfl)
{
	u16 i;

	if (!vfl->num_elements)
		return false;

	for (i = 0; i < vfl->num_elements; i++) {
		struct virtchnl_vlan_supported_caps *filtering_support =
			&vfc->filtering_support;
		struct virtchnl_vlan_filter *vlan_fltr = &vfl->filters[i];
		struct virtchnl_vlan *outer = &vlan_fltr->outer;
		struct virtchnl_vlan *inner = &vlan_fltr->inner;

		if ((ice_vc_is_valid_vlan(outer) &&
		     filtering_support->outer == VIRTCHNL_VLAN_UNSUPPORTED) ||
		    (ice_vc_is_valid_vlan(inner) &&
		     filtering_support->inner == VIRTCHNL_VLAN_UNSUPPORTED))
			return false;

		if ((outer->tci_mask &&
		     !(filtering_support->outer & VIRTCHNL_VLAN_FILTER_MASK)) ||
		    (inner->tci_mask &&
		     !(filtering_support->inner & VIRTCHNL_VLAN_FILTER_MASK)))
			return false;

		if (((outer->tci & VLAN_PRIO_MASK) &&
		     !(filtering_support->outer & VIRTCHNL_VLAN_PRIO)) ||
		    ((inner->tci & VLAN_PRIO_MASK) &&
		     !(filtering_support->inner & VIRTCHNL_VLAN_PRIO)))
			return false;

		if ((ice_vc_is_valid_vlan(outer) &&
		     !ice_vc_validate_vlan_tpid(filtering_support->outer,
						outer->tpid)) ||
		    (ice_vc_is_valid_vlan(inner) &&
		     !ice_vc_validate_vlan_tpid(filtering_support->inner,
						inner->tpid)))
			return false;
	}

	return true;
}

/**
 * ice_vc_to_vlan - transform from struct virtchnl_vlan to struct ice_vlan
 * @vc_vlan: struct virtchnl_vlan to transform
 */
static struct ice_vlan ice_vc_to_vlan(struct virtchnl_vlan *vc_vlan)
{
	struct ice_vlan vlan = { 0 };

	vlan.prio = FIELD_GET(VLAN_PRIO_MASK, vc_vlan->tci);
	vlan.vid = vc_vlan->tci & VLAN_VID_MASK;
	vlan.tpid = vc_vlan->tpid;

	return vlan;
}

/**
 * ice_vc_vlan_action - action to perform on the virthcnl_vlan
 * @vsi: VF's VSI used to perform the action
 * @vlan_action: function to perform the action with (i.e. add/del)
 * @vlan: VLAN filter to perform the action with
 */
static int
ice_vc_vlan_action(struct ice_vsi *vsi,
		   int (*vlan_action)(struct ice_vsi *, struct ice_vlan *),
		   struct ice_vlan *vlan)
{
	int err;

	err = vlan_action(vsi, vlan);
	if (err)
		return err;

	return 0;
}

/**
 * ice_vc_del_vlans - delete VLAN(s) from the virtchnl filter list
 * @vf: VF used to delete the VLAN(s)
 * @vsi: VF's VSI used to delete the VLAN(s)
 * @vfl: virthchnl filter list used to delete the filters
 */
static int
ice_vc_del_vlans(struct ice_vf *vf, struct ice_vsi *vsi,
		 struct virtchnl_vlan_filter_list_v2 *vfl)
{
	bool vlan_promisc = ice_is_vlan_promisc_allowed(vf);
	int err;
	u16 i;

	for (i = 0; i < vfl->num_elements; i++) {
		struct virtchnl_vlan_filter *vlan_fltr = &vfl->filters[i];
		struct virtchnl_vlan *vc_vlan;

		vc_vlan = &vlan_fltr->outer;
		if (ice_vc_is_valid_vlan(vc_vlan)) {
			struct ice_vlan vlan = ice_vc_to_vlan(vc_vlan);

			err = ice_vc_vlan_action(vsi,
						 vsi->outer_vlan_ops.del_vlan,
						 &vlan);
			if (err)
				return err;

			if (vlan_promisc)
				ice_vf_dis_vlan_promisc(vsi, &vlan);

			/* Disable VLAN filtering when only VLAN 0 is left */
			if (!ice_vsi_has_non_zero_vlans(vsi) && ice_is_dvm_ena(&vsi->back->hw)) {
				err = vsi->outer_vlan_ops.dis_tx_filtering(vsi);
				if (err)
					return err;
			}
		}

		vc_vlan = &vlan_fltr->inner;
		if (ice_vc_is_valid_vlan(vc_vlan)) {
			struct ice_vlan vlan = ice_vc_to_vlan(vc_vlan);

			err = ice_vc_vlan_action(vsi,
						 vsi->inner_vlan_ops.del_vlan,
						 &vlan);
			if (err)
				return err;

			/* no support for VLAN promiscuous on inner VLAN unless
			 * we are in Single VLAN Mode (SVM)
			 */
			if (!ice_is_dvm_ena(&vsi->back->hw)) {
				if (vlan_promisc)
					ice_vf_dis_vlan_promisc(vsi, &vlan);

				/* Disable VLAN filtering when only VLAN 0 is left */
				if (!ice_vsi_has_non_zero_vlans(vsi)) {
					err = vsi->inner_vlan_ops.dis_tx_filtering(vsi);
					if (err)
						return err;
				}
			}
		}
	}

	return 0;
}

/**
 * ice_vc_remove_vlan_v2_msg - virtchnl handler for VIRTCHNL_OP_DEL_VLAN_V2
 * @vf: VF the message was received from
 * @msg: message received from the VF
 */
static int ice_vc_remove_vlan_v2_msg(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_vlan_filter_list_v2 *vfl =
		(struct virtchnl_vlan_filter_list_v2 *)msg;
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct ice_vsi *vsi;

	if (!ice_vc_validate_vlan_filter_list(&vf->vlan_v2_caps.filtering,
					      vfl)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vfl->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (ice_vc_del_vlans(vf, vsi, vfl))
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DEL_VLAN_V2, v_ret, NULL,
				     0);
}

/**
 * ice_vc_add_vlans - add VLAN(s) from the virtchnl filter list
 * @vf: VF used to add the VLAN(s)
 * @vsi: VF's VSI used to add the VLAN(s)
 * @vfl: virthchnl filter list used to add the filters
 */
static int
ice_vc_add_vlans(struct ice_vf *vf, struct ice_vsi *vsi,
		 struct virtchnl_vlan_filter_list_v2 *vfl)
{
	bool vlan_promisc = ice_is_vlan_promisc_allowed(vf);
	int err;
	u16 i;

	for (i = 0; i < vfl->num_elements; i++) {
		struct virtchnl_vlan_filter *vlan_fltr = &vfl->filters[i];
		struct virtchnl_vlan *vc_vlan;

		vc_vlan = &vlan_fltr->outer;
		if (ice_vc_is_valid_vlan(vc_vlan)) {
			struct ice_vlan vlan = ice_vc_to_vlan(vc_vlan);

			err = ice_vc_vlan_action(vsi,
						 vsi->outer_vlan_ops.add_vlan,
						 &vlan);
			if (err)
				return err;

			if (vlan_promisc) {
				err = ice_vf_ena_vlan_promisc(vf, vsi, &vlan);
				if (err)
					return err;
			}

			/* Enable VLAN filtering on first non-zero VLAN */
			if (vf->spoofchk && vlan.vid && ice_is_dvm_ena(&vsi->back->hw)) {
				err = vsi->outer_vlan_ops.ena_tx_filtering(vsi);
				if (err)
					return err;
			}
		}

		vc_vlan = &vlan_fltr->inner;
		if (ice_vc_is_valid_vlan(vc_vlan)) {
			struct ice_vlan vlan = ice_vc_to_vlan(vc_vlan);

			err = ice_vc_vlan_action(vsi,
						 vsi->inner_vlan_ops.add_vlan,
						 &vlan);
			if (err)
				return err;

			/* no support for VLAN promiscuous on inner VLAN unless
			 * we are in Single VLAN Mode (SVM)
			 */
			if (!ice_is_dvm_ena(&vsi->back->hw)) {
				if (vlan_promisc) {
					err = ice_vf_ena_vlan_promisc(vf, vsi,
								      &vlan);
					if (err)
						return err;
				}

				/* Enable VLAN filtering on first non-zero VLAN */
				if (vf->spoofchk && vlan.vid) {
					err = vsi->inner_vlan_ops.ena_tx_filtering(vsi);
					if (err)
						return err;
				}
			}
		}
	}

	return 0;
}

/**
 * ice_vc_validate_add_vlan_filter_list - validate add filter list from the VF
 * @vsi: VF VSI used to get number of existing VLAN filters
 * @vfc: negotiated/supported VLAN filtering capabilities
 * @vfl: VLAN filter list from VF to validate
 *
 * Validate all of the filters in the VLAN filter list from the VF during the
 * VIRTCHNL_OP_ADD_VLAN_V2 opcode. If any of the checks fail then return false.
 * Otherwise return true.
 */
static bool
ice_vc_validate_add_vlan_filter_list(struct ice_vsi *vsi,
				     struct virtchnl_vlan_filtering_caps *vfc,
				     struct virtchnl_vlan_filter_list_v2 *vfl)
{
	u16 num_requested_filters = ice_vsi_num_non_zero_vlans(vsi) +
		vfl->num_elements;

	if (num_requested_filters > vfc->max_filters)
		return false;

	return ice_vc_validate_vlan_filter_list(vfc, vfl);
}

/**
 * ice_vc_add_vlan_v2_msg - virtchnl handler for VIRTCHNL_OP_ADD_VLAN_V2
 * @vf: VF the message was received from
 * @msg: message received from the VF
 */
static int ice_vc_add_vlan_v2_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_filter_list_v2 *vfl =
		(struct virtchnl_vlan_filter_list_v2 *)msg;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, vfl->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_validate_add_vlan_filter_list(vsi,
						  &vf->vlan_v2_caps.filtering,
						  vfl)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (ice_vc_add_vlans(vf, vsi, vfl))
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ADD_VLAN_V2, v_ret, NULL,
				     0);
}

/**
 * ice_vc_valid_vlan_setting - validate VLAN setting
 * @negotiated_settings: negotiated VLAN settings during VF init
 * @ethertype_setting: ethertype(s) requested for the VLAN setting
 */
static bool
ice_vc_valid_vlan_setting(u32 negotiated_settings, u32 ethertype_setting)
{
	if (ethertype_setting && !(negotiated_settings & ethertype_setting))
		return false;

	/* only allow a single VIRTCHNL_VLAN_ETHERTYPE if
	 * VIRTHCNL_VLAN_ETHERTYPE_AND is not negotiated/supported
	 */
	if (!(negotiated_settings & VIRTCHNL_VLAN_ETHERTYPE_AND) &&
	    hweight32(ethertype_setting) > 1)
		return false;

	/* ability to modify the VLAN setting was not negotiated */
	if (!(negotiated_settings & VIRTCHNL_VLAN_TOGGLE))
		return false;

	return true;
}

/**
 * ice_vc_valid_vlan_setting_msg - validate the VLAN setting message
 * @caps: negotiated VLAN settings during VF init
 * @msg: message to validate
 *
 * Used to validate any VLAN virtchnl message sent as a
 * virtchnl_vlan_setting structure. Validates the message against the
 * negotiated/supported caps during VF driver init.
 */
static bool
ice_vc_valid_vlan_setting_msg(struct virtchnl_vlan_supported_caps *caps,
			      struct virtchnl_vlan_setting *msg)
{
	if ((!msg->outer_ethertype_setting &&
	     !msg->inner_ethertype_setting) ||
	    (!caps->outer && !caps->inner))
		return false;

	if (msg->outer_ethertype_setting &&
	    !ice_vc_valid_vlan_setting(caps->outer,
				       msg->outer_ethertype_setting))
		return false;

	if (msg->inner_ethertype_setting &&
	    !ice_vc_valid_vlan_setting(caps->inner,
				       msg->inner_ethertype_setting))
		return false;

	return true;
}

/**
 * ice_vc_get_tpid - transform from VIRTCHNL_VLAN_ETHERTYPE_* to VLAN TPID
 * @ethertype_setting: VIRTCHNL_VLAN_ETHERTYPE_* used to get VLAN TPID
 * @tpid: VLAN TPID to populate
 */
static int ice_vc_get_tpid(u32 ethertype_setting, u16 *tpid)
{
	switch (ethertype_setting) {
	case VIRTCHNL_VLAN_ETHERTYPE_8100:
		*tpid = ETH_P_8021Q;
		break;
	case VIRTCHNL_VLAN_ETHERTYPE_88A8:
		*tpid = ETH_P_8021AD;
		break;
	case VIRTCHNL_VLAN_ETHERTYPE_9100:
		*tpid = ETH_P_QINQ1;
		break;
	default:
		*tpid = 0;
		return -EINVAL;
	}

	return 0;
}

/**
 * ice_vc_ena_vlan_offload - enable VLAN offload based on the ethertype_setting
 * @vsi: VF's VSI used to enable the VLAN offload
 * @ena_offload: function used to enable the VLAN offload
 * @ethertype_setting: VIRTCHNL_VLAN_ETHERTYPE_* to enable offloads for
 */
static int
ice_vc_ena_vlan_offload(struct ice_vsi *vsi,
			int (*ena_offload)(struct ice_vsi *vsi, u16 tpid),
			u32 ethertype_setting)
{
	u16 tpid;
	int err;

	err = ice_vc_get_tpid(ethertype_setting, &tpid);
	if (err)
		return err;

	err = ena_offload(vsi, tpid);
	if (err)
		return err;

	return 0;
}

/**
 * ice_vc_ena_vlan_stripping_v2_msg
 * @vf: VF the message was received from
 * @msg: message received from the VF
 *
 * virthcnl handler for VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2
 */
static int ice_vc_ena_vlan_stripping_v2_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_supported_caps *stripping_support;
	struct virtchnl_vlan_setting *strip_msg =
		(struct virtchnl_vlan_setting *)msg;
	u32 ethertype_setting;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, strip_msg->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	stripping_support = &vf->vlan_v2_caps.offloads.stripping_support;
	if (!ice_vc_valid_vlan_setting_msg(stripping_support, strip_msg)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (ice_vsi_is_rxq_crc_strip_dis(vsi)) {
		v_ret = VIRTCHNL_STATUS_ERR_NOT_SUPPORTED;
		goto out;
	}

	ethertype_setting = strip_msg->outer_ethertype_setting;
	if (ethertype_setting) {
		if (ice_vc_ena_vlan_offload(vsi,
					    vsi->outer_vlan_ops.ena_stripping,
					    ethertype_setting)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto out;
		} else {
			enum ice_l2tsel l2tsel =
				ICE_L2TSEL_EXTRACT_FIRST_TAG_L2TAG2_2ND;

			/* PF tells the VF that the outer VLAN tag is always
			 * extracted to VIRTCHNL_VLAN_TAG_LOCATION_L2TAG2_2 and
			 * inner is always extracted to
			 * VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1. This is needed to
			 * support outer stripping so the first tag always ends
			 * up in L2TAG2_2ND and the second/inner tag, if
			 * enabled, is extracted in L2TAG1.
			 */
			ice_vsi_update_l2tsel(vsi, l2tsel);

			vf->vlan_strip_ena |= ICE_OUTER_VLAN_STRIP_ENA;
		}
	}

	ethertype_setting = strip_msg->inner_ethertype_setting;
	if (ethertype_setting &&
	    ice_vc_ena_vlan_offload(vsi, vsi->inner_vlan_ops.ena_stripping,
				    ethertype_setting)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (ethertype_setting)
		vf->vlan_strip_ena |= ICE_INNER_VLAN_STRIP_ENA;

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_dis_vlan_stripping_v2_msg
 * @vf: VF the message was received from
 * @msg: message received from the VF
 *
 * virthcnl handler for VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2
 */
static int ice_vc_dis_vlan_stripping_v2_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_supported_caps *stripping_support;
	struct virtchnl_vlan_setting *strip_msg =
		(struct virtchnl_vlan_setting *)msg;
	u32 ethertype_setting;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, strip_msg->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	stripping_support = &vf->vlan_v2_caps.offloads.stripping_support;
	if (!ice_vc_valid_vlan_setting_msg(stripping_support, strip_msg)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	ethertype_setting = strip_msg->outer_ethertype_setting;
	if (ethertype_setting) {
		if (vsi->outer_vlan_ops.dis_stripping(vsi)) {
			v_ret = VIRTCHNL_STATUS_ERR_PARAM;
			goto out;
		} else {
			enum ice_l2tsel l2tsel =
				ICE_L2TSEL_EXTRACT_FIRST_TAG_L2TAG1;

			/* PF tells the VF that the outer VLAN tag is always
			 * extracted to VIRTCHNL_VLAN_TAG_LOCATION_L2TAG2_2 and
			 * inner is always extracted to
			 * VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1. This is needed to
			 * support inner stripping while outer stripping is
			 * disabled so that the first and only tag is extracted
			 * in L2TAG1.
			 */
			ice_vsi_update_l2tsel(vsi, l2tsel);

			vf->vlan_strip_ena &= ~ICE_OUTER_VLAN_STRIP_ENA;
		}
	}

	ethertype_setting = strip_msg->inner_ethertype_setting;
	if (ethertype_setting && vsi->inner_vlan_ops.dis_stripping(vsi)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (ethertype_setting)
		vf->vlan_strip_ena &= ~ICE_INNER_VLAN_STRIP_ENA;

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_ena_vlan_insertion_v2_msg
 * @vf: VF the message was received from
 * @msg: message received from the VF
 *
 * virthcnl handler for VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2
 */
static int ice_vc_ena_vlan_insertion_v2_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_supported_caps *insertion_support;
	struct virtchnl_vlan_setting *insertion_msg =
		(struct virtchnl_vlan_setting *)msg;
	u32 ethertype_setting;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, insertion_msg->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	insertion_support = &vf->vlan_v2_caps.offloads.insertion_support;
	if (!ice_vc_valid_vlan_setting_msg(insertion_support, insertion_msg)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	ethertype_setting = insertion_msg->outer_ethertype_setting;
	if (ethertype_setting &&
	    ice_vc_ena_vlan_offload(vsi, vsi->outer_vlan_ops.ena_insertion,
				    ethertype_setting)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	ethertype_setting = insertion_msg->inner_ethertype_setting;
	if (ethertype_setting &&
	    ice_vc_ena_vlan_offload(vsi, vsi->inner_vlan_ops.ena_insertion,
				    ethertype_setting)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_dis_vlan_insertion_v2_msg
 * @vf: VF the message was received from
 * @msg: message received from the VF
 *
 * virthcnl handler for VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2
 */
static int ice_vc_dis_vlan_insertion_v2_msg(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_vlan_supported_caps *insertion_support;
	struct virtchnl_vlan_setting *insertion_msg =
		(struct virtchnl_vlan_setting *)msg;
	u32 ethertype_setting;
	struct ice_vsi *vsi;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	if (!ice_vc_isvalid_vsi_id(vf, insertion_msg->vport_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	insertion_support = &vf->vlan_v2_caps.offloads.insertion_support;
	if (!ice_vc_valid_vlan_setting_msg(insertion_support, insertion_msg)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	ethertype_setting = insertion_msg->outer_ethertype_setting;
	if (ethertype_setting && vsi->outer_vlan_ops.dis_insertion(vsi)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

	ethertype_setting = insertion_msg->inner_ethertype_setting;
	if (ethertype_setting && vsi->inner_vlan_ops.dis_insertion(vsi)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto out;
	}

out:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2,
				     v_ret, NULL, 0);
}

static int ice_vc_get_ptp_cap(struct ice_vf *vf,
			      const struct virtchnl_ptp_caps *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_ERR_PARAM;
	u32 caps = VIRTCHNL_1588_PTP_CAP_RX_TSTAMP |
		   VIRTCHNL_1588_PTP_CAP_READ_PHC;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
		goto err;

	v_ret = VIRTCHNL_STATUS_SUCCESS;

	if (msg->caps & caps)
		vf->ptp_caps = caps;

err:
	/* send the response back to the VF */
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_1588_PTP_GET_CAPS, v_ret,
				     (u8 *)&vf->ptp_caps,
				     sizeof(struct virtchnl_ptp_caps));
}

static int ice_vc_get_phc_time(struct ice_vf *vf)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_ERR_PARAM;
	struct virtchnl_phc_time *phc_time = NULL;
	struct ice_pf *pf = vf->pf;
	u32 len = 0;
	int ret;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
		goto err;

	v_ret = VIRTCHNL_STATUS_SUCCESS;

	phc_time = kzalloc(sizeof(*phc_time), GFP_KERNEL);
	if (!phc_time) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		goto err;
	}

	len = sizeof(*phc_time);

	phc_time->time = ice_ptp_read_src_clk_reg(pf, NULL);

err:
	/* send the response back to the VF */
	ret = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_1588_PTP_GET_TIME, v_ret,
				    (u8 *)phc_time, len);
	kfree(phc_time);
	return ret;
}

static const struct ice_virtchnl_ops ice_virtchnl_dflt_ops = {
	.get_ver_msg = ice_vc_get_ver_msg,
	.get_vf_res_msg = ice_vc_get_vf_res_msg,
	.reset_vf = ice_vc_reset_vf_msg,
	.add_mac_addr_msg = ice_vc_add_mac_addr_msg,
	.del_mac_addr_msg = ice_vc_del_mac_addr_msg,
	.cfg_qs_msg = ice_vc_cfg_qs_msg,
	.ena_qs_msg = ice_vc_ena_qs_msg,
	.dis_qs_msg = ice_vc_dis_qs_msg,
	.request_qs_msg = ice_vc_request_qs_msg,
	.cfg_irq_map_msg = ice_vc_cfg_irq_map_msg,
	.config_rss_key = ice_vc_config_rss_key,
	.config_rss_lut = ice_vc_config_rss_lut,
	.config_rss_hfunc = ice_vc_config_rss_hfunc,
	.get_stats_msg = ice_vc_get_stats_msg,
	.cfg_promiscuous_mode_msg = ice_vc_cfg_promiscuous_mode_msg,
	.add_vlan_msg = ice_vc_add_vlan_msg,
	.remove_vlan_msg = ice_vc_remove_vlan_msg,
	.query_rxdid = ice_vc_query_rxdid,
	.get_rss_hashcfg = ice_vc_get_rss_hashcfg,
	.set_rss_hashcfg = ice_vc_set_rss_hashcfg,
	.ena_vlan_stripping = ice_vc_ena_vlan_stripping,
	.dis_vlan_stripping = ice_vc_dis_vlan_stripping,
	.handle_rss_cfg_msg = ice_vc_handle_rss_cfg,
	.add_fdir_fltr_msg = ice_vc_add_fdir_fltr,
	.del_fdir_fltr_msg = ice_vc_del_fdir_fltr,
	.get_offload_vlan_v2_caps = ice_vc_get_offload_vlan_v2_caps,
	.add_vlan_v2_msg = ice_vc_add_vlan_v2_msg,
	.remove_vlan_v2_msg = ice_vc_remove_vlan_v2_msg,
	.ena_vlan_stripping_v2_msg = ice_vc_ena_vlan_stripping_v2_msg,
	.dis_vlan_stripping_v2_msg = ice_vc_dis_vlan_stripping_v2_msg,
	.ena_vlan_insertion_v2_msg = ice_vc_ena_vlan_insertion_v2_msg,
	.dis_vlan_insertion_v2_msg = ice_vc_dis_vlan_insertion_v2_msg,
	.get_qos_caps = ice_vc_get_qos_caps,
	.cfg_q_bw = ice_vc_cfg_q_bw,
	.cfg_q_quanta = ice_vc_cfg_q_quanta,
	.get_ptp_cap = ice_vc_get_ptp_cap,
	.get_phc_time = ice_vc_get_phc_time,
	/* If you add a new op here please make sure to add it to
	 * ice_virtchnl_repr_ops as well.
	 */
};

/**
 * ice_virtchnl_set_dflt_ops - Switch to default virtchnl ops
 * @vf: the VF to switch ops
 */
void ice_virtchnl_set_dflt_ops(struct ice_vf *vf)
{
	vf->virtchnl_ops = &ice_virtchnl_dflt_ops;
}

/**
 * ice_vc_repr_add_mac
 * @vf: pointer to VF
 * @msg: virtchannel message
 *
 * When port representors are created, we do not add MAC rule
 * to firmware, we store it so that PF could report same
 * MAC as VF.
 */
static int ice_vc_repr_add_mac(struct ice_vf *vf, u8 *msg)
{
	enum virtchnl_status_code v_ret = VIRTCHNL_STATUS_SUCCESS;
	struct virtchnl_ether_addr_list *al =
	    (struct virtchnl_ether_addr_list *)msg;
	struct ice_vsi *vsi;
	struct ice_pf *pf;
	int i;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states) ||
	    !ice_vc_isvalid_vsi_id(vf, al->vsi_id)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto handle_mac_exit;
	}

	pf = vf->pf;

	vsi = ice_get_vf_vsi(vf);
	if (!vsi) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		goto handle_mac_exit;
	}

	for (i = 0; i < al->num_elements; i++) {
		u8 *mac_addr = al->list[i].addr;

		if (!is_unicast_ether_addr(mac_addr) ||
		    ether_addr_equal(mac_addr, vf->hw_lan_addr))
			continue;

		if (vf->pf_set_mac) {
			dev_err(ice_pf_to_dev(pf), "VF attempting to override administratively set MAC address\n");
			v_ret = VIRTCHNL_STATUS_ERR_NOT_SUPPORTED;
			goto handle_mac_exit;
		}

		ice_vfhw_mac_add(vf, &al->list[i]);
		break;
	}

handle_mac_exit:
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ADD_ETH_ADDR,
				     v_ret, NULL, 0);
}

/**
 * ice_vc_repr_del_mac - response with success for deleting MAC
 * @vf: pointer to VF
 * @msg: virtchannel message
 *
 * Respond with success to not break normal VF flow.
 * For legacy VF driver try to update cached MAC address.
 */
static int
ice_vc_repr_del_mac(struct ice_vf __always_unused *vf, u8 __always_unused *msg)
{
	struct virtchnl_ether_addr_list *al =
		(struct virtchnl_ether_addr_list *)msg;

	ice_update_legacy_cached_mac(vf, &al->list[0]);

	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DEL_ETH_ADDR,
				     VIRTCHNL_STATUS_SUCCESS, NULL, 0);
}

static int
ice_vc_repr_cfg_promiscuous_mode(struct ice_vf *vf, u8 __always_unused *msg)
{
	dev_dbg(ice_pf_to_dev(vf->pf),
		"Can't config promiscuous mode in switchdev mode for VF %d\n",
		vf->vf_id);
	return ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE,
				     VIRTCHNL_STATUS_ERR_NOT_SUPPORTED,
				     NULL, 0);
}

static const struct ice_virtchnl_ops ice_virtchnl_repr_ops = {
	.get_ver_msg = ice_vc_get_ver_msg,
	.get_vf_res_msg = ice_vc_get_vf_res_msg,
	.reset_vf = ice_vc_reset_vf_msg,
	.add_mac_addr_msg = ice_vc_repr_add_mac,
	.del_mac_addr_msg = ice_vc_repr_del_mac,
	.cfg_qs_msg = ice_vc_cfg_qs_msg,
	.ena_qs_msg = ice_vc_ena_qs_msg,
	.dis_qs_msg = ice_vc_dis_qs_msg,
	.request_qs_msg = ice_vc_request_qs_msg,
	.cfg_irq_map_msg = ice_vc_cfg_irq_map_msg,
	.config_rss_key = ice_vc_config_rss_key,
	.config_rss_lut = ice_vc_config_rss_lut,
	.config_rss_hfunc = ice_vc_config_rss_hfunc,
	.get_stats_msg = ice_vc_get_stats_msg,
	.cfg_promiscuous_mode_msg = ice_vc_repr_cfg_promiscuous_mode,
	.add_vlan_msg = ice_vc_add_vlan_msg,
	.remove_vlan_msg = ice_vc_remove_vlan_msg,
	.query_rxdid = ice_vc_query_rxdid,
	.get_rss_hashcfg = ice_vc_get_rss_hashcfg,
	.set_rss_hashcfg = ice_vc_set_rss_hashcfg,
	.ena_vlan_stripping = ice_vc_ena_vlan_stripping,
	.dis_vlan_stripping = ice_vc_dis_vlan_stripping,
	.handle_rss_cfg_msg = ice_vc_handle_rss_cfg,
	.add_fdir_fltr_msg = ice_vc_add_fdir_fltr,
	.del_fdir_fltr_msg = ice_vc_del_fdir_fltr,
	.get_offload_vlan_v2_caps = ice_vc_get_offload_vlan_v2_caps,
	.add_vlan_v2_msg = ice_vc_add_vlan_v2_msg,
	.remove_vlan_v2_msg = ice_vc_remove_vlan_v2_msg,
	.ena_vlan_stripping_v2_msg = ice_vc_ena_vlan_stripping_v2_msg,
	.dis_vlan_stripping_v2_msg = ice_vc_dis_vlan_stripping_v2_msg,
	.ena_vlan_insertion_v2_msg = ice_vc_ena_vlan_insertion_v2_msg,
	.dis_vlan_insertion_v2_msg = ice_vc_dis_vlan_insertion_v2_msg,
	.get_qos_caps = ice_vc_get_qos_caps,
	.cfg_q_bw = ice_vc_cfg_q_bw,
	.cfg_q_quanta = ice_vc_cfg_q_quanta,
	.get_ptp_cap = ice_vc_get_ptp_cap,
	.get_phc_time = ice_vc_get_phc_time,
};

/**
 * ice_virtchnl_set_repr_ops - Switch to representor virtchnl ops
 * @vf: the VF to switch ops
 */
void ice_virtchnl_set_repr_ops(struct ice_vf *vf)
{
	vf->virtchnl_ops = &ice_virtchnl_repr_ops;
}

/**
 * ice_is_malicious_vf - check if this vf might be overflowing mailbox
 * @vf: the VF to check
 * @mbxdata: data about the state of the mailbox
 *
 * Detect if a given VF might be malicious and attempting to overflow the PF
 * mailbox. If so, log a warning message and ignore this event.
 */
static bool
ice_is_malicious_vf(struct ice_vf *vf, struct ice_mbx_data *mbxdata)
{
	bool report_malvf = false;
	struct device *dev;
	struct ice_pf *pf;
	int status;

	pf = vf->pf;
	dev = ice_pf_to_dev(pf);

	if (test_bit(ICE_VF_STATE_DIS, vf->vf_states))
		return vf->mbx_info.malicious;

	/* check to see if we have a newly malicious VF */
	status = ice_mbx_vf_state_handler(&pf->hw, mbxdata, &vf->mbx_info,
					  &report_malvf);
	if (status)
		dev_warn_ratelimited(dev, "Unable to check status of mailbox overflow for VF %u MAC %pM, status %d\n",
				     vf->vf_id, vf->dev_lan_addr, status);

	if (report_malvf) {
		struct ice_vsi *pf_vsi = ice_get_main_vsi(pf);
		u8 zero_addr[ETH_ALEN] = {};

		dev_warn(dev, "VF MAC %pM on PF MAC %pM is generating asynchronous messages and may be overflowing the PF message queue. Please see the Adapter User Guide for more information\n",
			 vf->dev_lan_addr,
			 pf_vsi ? pf_vsi->netdev->dev_addr : zero_addr);
	}

	return vf->mbx_info.malicious;
}

/**
 * ice_vc_process_vf_msg - Process request from VF
 * @pf: pointer to the PF structure
 * @event: pointer to the AQ event
 * @mbxdata: information used to detect VF attempting mailbox overflow
 *
 * Called from the common asq/arq handler to process request from VF. When this
 * flow is used for devices with hardware VF to PF message queue overflow
 * support (ICE_F_MBX_LIMIT) mbxdata is set to NULL and ice_is_malicious_vf
 * check is skipped.
 */
void ice_vc_process_vf_msg(struct ice_pf *pf, struct ice_rq_event_info *event,
			   struct ice_mbx_data *mbxdata)
{
	u32 v_opcode = le32_to_cpu(event->desc.cookie_high);
	s16 vf_id = le16_to_cpu(event->desc.retval);
	const struct ice_virtchnl_ops *ops;
	u16 msglen = event->msg_len;
	u8 *msg = event->msg_buf;
	struct ice_vf *vf = NULL;
	struct device *dev;
	int err = 0;

	dev = ice_pf_to_dev(pf);

	vf = ice_get_vf_by_id(pf, vf_id);
	if (!vf) {
		dev_err(dev, "Unable to locate VF for message from VF ID %d, opcode %d, len %d\n",
			vf_id, v_opcode, msglen);
		return;
	}

	mutex_lock(&vf->cfg_lock);

	/* Check if the VF is trying to overflow the mailbox */
	if (mbxdata && ice_is_malicious_vf(vf, mbxdata))
		goto finish;

	/* Check if VF is disabled. */
	if (test_bit(ICE_VF_STATE_DIS, vf->vf_states)) {
		err = -EPERM;
		goto error_handler;
	}

	ops = vf->virtchnl_ops;

	/* Perform basic checks on the msg */
	err = virtchnl_vc_validate_vf_msg(&vf->vf_ver, v_opcode, msg, msglen);
	if (err) {
		if (err == VIRTCHNL_STATUS_ERR_PARAM)
			err = -EPERM;
		else
			err = -EINVAL;
	}

error_handler:
	if (err) {
		ice_vc_send_msg_to_vf(vf, v_opcode, VIRTCHNL_STATUS_ERR_PARAM,
				      NULL, 0);
		dev_err(dev, "Invalid message from VF %d, opcode %d, len %d, error %d\n",
			vf_id, v_opcode, msglen, err);
		goto finish;
	}

	if (!ice_vc_is_opcode_allowed(vf, v_opcode)) {
		ice_vc_send_msg_to_vf(vf, v_opcode,
				      VIRTCHNL_STATUS_ERR_NOT_SUPPORTED, NULL,
				      0);
		goto finish;
	}

	switch (v_opcode) {
	case VIRTCHNL_OP_VERSION:
		err = ops->get_ver_msg(vf, msg);
		break;
	case VIRTCHNL_OP_GET_VF_RESOURCES:
		err = ops->get_vf_res_msg(vf, msg);
		if (ice_vf_init_vlan_stripping(vf))
			dev_dbg(dev, "Failed to initialize VLAN stripping for VF %d\n",
				vf->vf_id);
		ice_vc_notify_vf_link_state(vf);
		break;
	case VIRTCHNL_OP_RESET_VF:
		ops->reset_vf(vf);
		break;
	case VIRTCHNL_OP_ADD_ETH_ADDR:
		err = ops->add_mac_addr_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DEL_ETH_ADDR:
		err = ops->del_mac_addr_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_VSI_QUEUES:
		err = ops->cfg_qs_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ENABLE_QUEUES:
		err = ops->ena_qs_msg(vf, msg);
		ice_vc_notify_vf_link_state(vf);
		break;
	case VIRTCHNL_OP_DISABLE_QUEUES:
		err = ops->dis_qs_msg(vf, msg);
		break;
	case VIRTCHNL_OP_REQUEST_QUEUES:
		err = ops->request_qs_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_IRQ_MAP:
		err = ops->cfg_irq_map_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_RSS_KEY:
		err = ops->config_rss_key(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_RSS_LUT:
		err = ops->config_rss_lut(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_RSS_HFUNC:
		err = ops->config_rss_hfunc(vf, msg);
		break;
	case VIRTCHNL_OP_GET_STATS:
		err = ops->get_stats_msg(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE:
		err = ops->cfg_promiscuous_mode_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ADD_VLAN:
		err = ops->add_vlan_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DEL_VLAN:
		err = ops->remove_vlan_msg(vf, msg);
		break;
	case VIRTCHNL_OP_GET_SUPPORTED_RXDIDS:
		err = ops->query_rxdid(vf);
		break;
	case VIRTCHNL_OP_GET_RSS_HASHCFG_CAPS:
		err = ops->get_rss_hashcfg(vf);
		break;
	case VIRTCHNL_OP_SET_RSS_HASHCFG:
		err = ops->set_rss_hashcfg(vf, msg);
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING:
		err = ops->ena_vlan_stripping(vf);
		break;
	case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING:
		err = ops->dis_vlan_stripping(vf);
		break;
	case VIRTCHNL_OP_ADD_FDIR_FILTER:
		err = ops->add_fdir_fltr_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DEL_FDIR_FILTER:
		err = ops->del_fdir_fltr_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ADD_RSS_CFG:
		err = ops->handle_rss_cfg_msg(vf, msg, true);
		break;
	case VIRTCHNL_OP_DEL_RSS_CFG:
		err = ops->handle_rss_cfg_msg(vf, msg, false);
		break;
	case VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS:
		err = ops->get_offload_vlan_v2_caps(vf);
		break;
	case VIRTCHNL_OP_ADD_VLAN_V2:
		err = ops->add_vlan_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DEL_VLAN_V2:
		err = ops->remove_vlan_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2:
		err = ops->ena_vlan_stripping_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2:
		err = ops->dis_vlan_stripping_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2:
		err = ops->ena_vlan_insertion_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2:
		err = ops->dis_vlan_insertion_v2_msg(vf, msg);
		break;
	case VIRTCHNL_OP_GET_QOS_CAPS:
		err = ops->get_qos_caps(vf);
		break;
	case VIRTCHNL_OP_CONFIG_QUEUE_BW:
		err = ops->cfg_q_bw(vf, msg);
		break;
	case VIRTCHNL_OP_CONFIG_QUANTA:
		err = ops->cfg_q_quanta(vf, msg);
		break;
	case VIRTCHNL_OP_1588_PTP_GET_CAPS:
		err = ops->get_ptp_cap(vf, (const void *)msg);
		break;
	case VIRTCHNL_OP_1588_PTP_GET_TIME:
		err = ops->get_phc_time(vf);
		break;
	case VIRTCHNL_OP_UNKNOWN:
	default:
		dev_err(dev, "Unsupported opcode %d from VF %d\n", v_opcode,
			vf_id);
		err = ice_vc_send_msg_to_vf(vf, v_opcode,
					    VIRTCHNL_STATUS_ERR_NOT_SUPPORTED,
					    NULL, 0);
		break;
	}
	if (err) {
		/* Helper function cares less about error return values here
		 * as it is busy with pending work.
		 */
		dev_info(dev, "PF failed to honor VF %d, opcode %d, error %d\n",
			 vf_id, v_opcode, err);
	}

finish:
	mutex_unlock(&vf->cfg_lock);
	ice_put_vf(vf);
}
