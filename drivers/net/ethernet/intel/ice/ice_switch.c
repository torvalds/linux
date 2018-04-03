// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice_switch.h"

#define ICE_ETH_DA_OFFSET		0
#define ICE_ETH_ETHTYPE_OFFSET		12
#define ICE_ETH_VLAN_TCI_OFFSET		14
#define ICE_MAX_VLAN_ID			0xFFF

/* Dummy ethernet header needed in the ice_aqc_sw_rules_elem
 * struct to configure any switch filter rules.
 * {DA (6 bytes), SA(6 bytes),
 * Ether type (2 bytes for header without VLAN tag) OR
 * VLAN tag (4 bytes for header with VLAN tag) }
 *
 * Word on Hardcoded values
 * byte 0 = 0x2: to identify it as locally administered DA MAC
 * byte 6 = 0x2: to identify it as locally administered SA MAC
 * byte 12 = 0x81 & byte 13 = 0x00:
 *	In case of VLAN filter first two bytes defines ether type (0x8100)
 *	and remaining two bytes are placeholder for programming a given VLAN id
 *	In case of Ether type filter it is treated as header without VLAN tag
 *	and byte 12 and 13 is used to program a given Ether type instead
 */
#define DUMMY_ETH_HDR_LEN		16
static const u8 dummy_eth_header[DUMMY_ETH_HDR_LEN] = { 0x2, 0, 0, 0, 0, 0,
							0x2, 0, 0, 0, 0, 0,
							0x81, 0, 0, 0};

#define ICE_SW_RULE_RX_TX_ETH_HDR_SIZE \
	(sizeof(struct ice_aqc_sw_rules_elem) - \
	 sizeof(((struct ice_aqc_sw_rules_elem *)0)->pdata) + \
	 sizeof(struct ice_sw_rule_lkup_rx_tx) + DUMMY_ETH_HDR_LEN - 1)
#define ICE_SW_RULE_RX_TX_NO_HDR_SIZE \
	(sizeof(struct ice_aqc_sw_rules_elem) - \
	 sizeof(((struct ice_aqc_sw_rules_elem *)0)->pdata) + \
	 sizeof(struct ice_sw_rule_lkup_rx_tx) - 1)
#define ICE_SW_RULE_LG_ACT_SIZE(n) \
	(sizeof(struct ice_aqc_sw_rules_elem) - \
	 sizeof(((struct ice_aqc_sw_rules_elem *)0)->pdata) + \
	 sizeof(struct ice_sw_rule_lg_act) - \
	 sizeof(((struct ice_sw_rule_lg_act *)0)->act) + \
	 ((n) * sizeof(((struct ice_sw_rule_lg_act *)0)->act)))
#define ICE_SW_RULE_VSI_LIST_SIZE(n) \
	(sizeof(struct ice_aqc_sw_rules_elem) - \
	 sizeof(((struct ice_aqc_sw_rules_elem *)0)->pdata) + \
	 sizeof(struct ice_sw_rule_vsi_list) - \
	 sizeof(((struct ice_sw_rule_vsi_list *)0)->vsi) + \
	 ((n) * sizeof(((struct ice_sw_rule_vsi_list *)0)->vsi)))

/**
 * ice_aq_alloc_free_res - command to allocate/free resources
 * @hw: pointer to the hw struct
 * @num_entries: number of resource entries in buffer
 * @buf: Indirect buffer to hold data parameters and response
 * @buf_size: size of buffer for indirect commands
 * @opc: pass in the command opcode
 * @cd: pointer to command details structure or NULL
 *
 * Helper function to allocate/free resources using the admin queue commands
 */
static enum ice_status
ice_aq_alloc_free_res(struct ice_hw *hw, u16 num_entries,
		      struct ice_aqc_alloc_free_res_elem *buf, u16 buf_size,
		      enum ice_adminq_opc opc, struct ice_sq_cd *cd)
{
	struct ice_aqc_alloc_free_res_cmd *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.sw_res_ctrl;

	if (!buf)
		return ICE_ERR_PARAM;

	if (buf_size < (num_entries * sizeof(buf->elem[0])))
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, opc);

	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	cmd->num_entries = cpu_to_le16(num_entries);

	return ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
}

/**
 * ice_aq_get_sw_cfg - get switch configuration
 * @hw: pointer to the hardware structure
 * @buf: pointer to the result buffer
 * @buf_size: length of the buffer available for response
 * @req_desc: pointer to requested descriptor
 * @num_elems: pointer to number of elements
 * @cd: pointer to command details structure or NULL
 *
 * Get switch configuration (0x0200) to be placed in 'buff'.
 * This admin command returns information such as initial VSI/port number
 * and switch ID it belongs to.
 *
 * NOTE: *req_desc is both an input/output parameter.
 * The caller of this function first calls this function with *request_desc set
 * to 0.  If the response from f/w has *req_desc set to 0, all the switch
 * configuration information has been returned; if non-zero (meaning not all
 * the information was returned), the caller should call this function again
 * with *req_desc set to the previous value returned by f/w to get the
 * next block of switch configuration information.
 *
 * *num_elems is output only parameter. This reflects the number of elements
 * in response buffer. The caller of this function to use *num_elems while
 * parsing the response buffer.
 */
static enum ice_status
ice_aq_get_sw_cfg(struct ice_hw *hw, struct ice_aqc_get_sw_cfg_resp *buf,
		  u16 buf_size, u16 *req_desc, u16 *num_elems,
		  struct ice_sq_cd *cd)
{
	struct ice_aqc_get_sw_cfg *cmd;
	enum ice_status status;
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_sw_cfg);
	cmd = &desc.params.get_sw_conf;
	cmd->element = cpu_to_le16(*req_desc);

	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status) {
		*req_desc = le16_to_cpu(cmd->element);
		*num_elems = le16_to_cpu(cmd->num_elems);
	}

	return status;
}

/**
 * ice_aq_add_vsi
 * @hw: pointer to the hw struct
 * @vsi_ctx: pointer to a VSI context struct
 * @cd: pointer to command details structure or NULL
 *
 * Add a VSI context to the hardware (0x0210)
 */
enum ice_status
ice_aq_add_vsi(struct ice_hw *hw, struct ice_vsi_ctx *vsi_ctx,
	       struct ice_sq_cd *cd)
{
	struct ice_aqc_add_update_free_vsi_resp *res;
	struct ice_aqc_add_get_update_free_vsi *cmd;
	enum ice_status status;
	struct ice_aq_desc desc;

	cmd = &desc.params.vsi_cmd;
	res = (struct ice_aqc_add_update_free_vsi_resp *)&desc.params.raw;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_add_vsi);

	if (!vsi_ctx->alloc_from_pool)
		cmd->vsi_num = cpu_to_le16(vsi_ctx->vsi_num |
					   ICE_AQ_VSI_IS_VALID);

	cmd->vsi_flags = cpu_to_le16(vsi_ctx->flags);

	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	status = ice_aq_send_cmd(hw, &desc, &vsi_ctx->info,
				 sizeof(vsi_ctx->info), cd);

	if (!status) {
		vsi_ctx->vsi_num = le16_to_cpu(res->vsi_num) & ICE_AQ_VSI_NUM_M;
		vsi_ctx->vsis_allocd = le16_to_cpu(res->vsi_used);
		vsi_ctx->vsis_unallocated = le16_to_cpu(res->vsi_free);
	}

	return status;
}

/**
 * ice_aq_update_vsi
 * @hw: pointer to the hw struct
 * @vsi_ctx: pointer to a VSI context struct
 * @cd: pointer to command details structure or NULL
 *
 * Update VSI context in the hardware (0x0211)
 */
enum ice_status
ice_aq_update_vsi(struct ice_hw *hw, struct ice_vsi_ctx *vsi_ctx,
		  struct ice_sq_cd *cd)
{
	struct ice_aqc_add_update_free_vsi_resp *resp;
	struct ice_aqc_add_get_update_free_vsi *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.vsi_cmd;
	resp = (struct ice_aqc_add_update_free_vsi_resp *)&desc.params.raw;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_update_vsi);

	cmd->vsi_num = cpu_to_le16(vsi_ctx->vsi_num | ICE_AQ_VSI_IS_VALID);

	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	status = ice_aq_send_cmd(hw, &desc, &vsi_ctx->info,
				 sizeof(vsi_ctx->info), cd);

	if (!status) {
		vsi_ctx->vsis_allocd = le16_to_cpu(resp->vsi_used);
		vsi_ctx->vsis_unallocated = le16_to_cpu(resp->vsi_free);
	}

	return status;
}

/**
 * ice_aq_free_vsi
 * @hw: pointer to the hw struct
 * @vsi_ctx: pointer to a VSI context struct
 * @keep_vsi_alloc: keep VSI allocation as part of this PF's resources
 * @cd: pointer to command details structure or NULL
 *
 * Get VSI context info from hardware (0x0213)
 */
enum ice_status
ice_aq_free_vsi(struct ice_hw *hw, struct ice_vsi_ctx *vsi_ctx,
		bool keep_vsi_alloc, struct ice_sq_cd *cd)
{
	struct ice_aqc_add_update_free_vsi_resp *resp;
	struct ice_aqc_add_get_update_free_vsi *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.vsi_cmd;
	resp = (struct ice_aqc_add_update_free_vsi_resp *)&desc.params.raw;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_free_vsi);

	cmd->vsi_num = cpu_to_le16(vsi_ctx->vsi_num | ICE_AQ_VSI_IS_VALID);
	if (keep_vsi_alloc)
		cmd->cmd_flags = cpu_to_le16(ICE_AQ_VSI_KEEP_ALLOC);

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
	if (!status) {
		vsi_ctx->vsis_allocd = le16_to_cpu(resp->vsi_used);
		vsi_ctx->vsis_unallocated = le16_to_cpu(resp->vsi_free);
	}

	return status;
}

/**
 * ice_aq_alloc_free_vsi_list
 * @hw: pointer to the hw struct
 * @vsi_list_id: VSI list id returned or used for lookup
 * @lkup_type: switch rule filter lookup type
 * @opc: switch rules population command type - pass in the command opcode
 *
 * allocates or free a VSI list resource
 */
static enum ice_status
ice_aq_alloc_free_vsi_list(struct ice_hw *hw, u16 *vsi_list_id,
			   enum ice_sw_lkup_type lkup_type,
			   enum ice_adminq_opc opc)
{
	struct ice_aqc_alloc_free_res_elem *sw_buf;
	struct ice_aqc_res_elem *vsi_ele;
	enum ice_status status;
	u16 buf_len;

	buf_len = sizeof(*sw_buf);
	sw_buf = devm_kzalloc(ice_hw_to_dev(hw), buf_len, GFP_KERNEL);
	if (!sw_buf)
		return ICE_ERR_NO_MEMORY;
	sw_buf->num_elems = cpu_to_le16(1);

	if (lkup_type == ICE_SW_LKUP_MAC ||
	    lkup_type == ICE_SW_LKUP_MAC_VLAN ||
	    lkup_type == ICE_SW_LKUP_ETHERTYPE ||
	    lkup_type == ICE_SW_LKUP_ETHERTYPE_MAC ||
	    lkup_type == ICE_SW_LKUP_PROMISC ||
	    lkup_type == ICE_SW_LKUP_PROMISC_VLAN) {
		sw_buf->res_type = cpu_to_le16(ICE_AQC_RES_TYPE_VSI_LIST_REP);
	} else if (lkup_type == ICE_SW_LKUP_VLAN) {
		sw_buf->res_type =
			cpu_to_le16(ICE_AQC_RES_TYPE_VSI_LIST_PRUNE);
	} else {
		status = ICE_ERR_PARAM;
		goto ice_aq_alloc_free_vsi_list_exit;
	}

	if (opc == ice_aqc_opc_free_res)
		sw_buf->elem[0].e.sw_resp = cpu_to_le16(*vsi_list_id);

	status = ice_aq_alloc_free_res(hw, 1, sw_buf, buf_len, opc, NULL);
	if (status)
		goto ice_aq_alloc_free_vsi_list_exit;

	if (opc == ice_aqc_opc_alloc_res) {
		vsi_ele = &sw_buf->elem[0];
		*vsi_list_id = le16_to_cpu(vsi_ele->e.sw_resp);
	}

ice_aq_alloc_free_vsi_list_exit:
	devm_kfree(ice_hw_to_dev(hw), sw_buf);
	return status;
}

/**
 * ice_aq_sw_rules - add/update/remove switch rules
 * @hw: pointer to the hw struct
 * @rule_list: pointer to switch rule population list
 * @rule_list_sz: total size of the rule list in bytes
 * @num_rules: number of switch rules in the rule_list
 * @opc: switch rules population command type - pass in the command opcode
 * @cd: pointer to command details structure or NULL
 *
 * Add(0x02a0)/Update(0x02a1)/Remove(0x02a2) switch rules commands to firmware
 */
static enum ice_status
ice_aq_sw_rules(struct ice_hw *hw, void *rule_list, u16 rule_list_sz,
		u8 num_rules, enum ice_adminq_opc opc, struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	if (opc != ice_aqc_opc_add_sw_rules &&
	    opc != ice_aqc_opc_update_sw_rules &&
	    opc != ice_aqc_opc_remove_sw_rules)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, opc);

	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);
	desc.params.sw_rules.num_rules_fltr_entry_index =
		cpu_to_le16(num_rules);
	return ice_aq_send_cmd(hw, &desc, rule_list, rule_list_sz, cd);
}

/* ice_init_port_info - Initialize port_info with switch configuration data
 * @pi: pointer to port_info
 * @vsi_port_num: VSI number or port number
 * @type: Type of switch element (port or VSI)
 * @swid: switch ID of the switch the element is attached to
 * @pf_vf_num: PF or VF number
 * @is_vf: true if the element is a VF, false otherwise
 */
static void
ice_init_port_info(struct ice_port_info *pi, u16 vsi_port_num, u8 type,
		   u16 swid, u16 pf_vf_num, bool is_vf)
{
	switch (type) {
	case ICE_AQC_GET_SW_CONF_RESP_PHYS_PORT:
		pi->lport = (u8)(vsi_port_num & ICE_LPORT_MASK);
		pi->sw_id = swid;
		pi->pf_vf_num = pf_vf_num;
		pi->is_vf = is_vf;
		pi->dflt_tx_vsi_num = ICE_DFLT_VSI_INVAL;
		pi->dflt_rx_vsi_num = ICE_DFLT_VSI_INVAL;
		break;
	default:
		ice_debug(pi->hw, ICE_DBG_SW,
			  "incorrect VSI/port type received\n");
		break;
	}
}

/* ice_get_initial_sw_cfg - Get initial port and default VSI data
 * @hw: pointer to the hardware structure
 */
enum ice_status ice_get_initial_sw_cfg(struct ice_hw *hw)
{
	struct ice_aqc_get_sw_cfg_resp *rbuf;
	enum ice_status status;
	u16 req_desc = 0;
	u16 num_elems;
	u16 i;

	rbuf = devm_kzalloc(ice_hw_to_dev(hw), ICE_SW_CFG_MAX_BUF_LEN,
			    GFP_KERNEL);

	if (!rbuf)
		return ICE_ERR_NO_MEMORY;

	/* Multiple calls to ice_aq_get_sw_cfg may be required
	 * to get all the switch configuration information. The need
	 * for additional calls is indicated by ice_aq_get_sw_cfg
	 * writing a non-zero value in req_desc
	 */
	do {
		status = ice_aq_get_sw_cfg(hw, rbuf, ICE_SW_CFG_MAX_BUF_LEN,
					   &req_desc, &num_elems, NULL);

		if (status)
			break;

		for (i = 0; i < num_elems; i++) {
			struct ice_aqc_get_sw_cfg_resp_elem *ele;
			u16 pf_vf_num, swid, vsi_port_num;
			bool is_vf = false;
			u8 type;

			ele = rbuf[i].elements;
			vsi_port_num = le16_to_cpu(ele->vsi_port_num) &
				ICE_AQC_GET_SW_CONF_RESP_VSI_PORT_NUM_M;

			pf_vf_num = le16_to_cpu(ele->pf_vf_num) &
				ICE_AQC_GET_SW_CONF_RESP_FUNC_NUM_M;

			swid = le16_to_cpu(ele->swid);

			if (le16_to_cpu(ele->pf_vf_num) &
			    ICE_AQC_GET_SW_CONF_RESP_IS_VF)
				is_vf = true;

			type = le16_to_cpu(ele->vsi_port_num) >>
				ICE_AQC_GET_SW_CONF_RESP_TYPE_S;

			if (type == ICE_AQC_GET_SW_CONF_RESP_VSI) {
				/* FW VSI is not needed. Just continue. */
				continue;
			}

			ice_init_port_info(hw->port_info, vsi_port_num,
					   type, swid, pf_vf_num, is_vf);
		}
	} while (req_desc && !status);

	devm_kfree(ice_hw_to_dev(hw), (void *)rbuf);
	return status;
}

/**
 * ice_fill_sw_info - Helper function to populate lb_en and lan_en
 * @hw: pointer to the hardware structure
 * @f_info: filter info structure to fill/update
 *
 * This helper function populates the lb_en and lan_en elements of the provided
 * ice_fltr_info struct using the switch's type and characteristics of the
 * switch rule being configured.
 */
static void ice_fill_sw_info(struct ice_hw *hw, struct ice_fltr_info *f_info)
{
	f_info->lb_en = false;
	f_info->lan_en = false;
	if ((f_info->flag & ICE_FLTR_TX) &&
	    (f_info->fltr_act == ICE_FWD_TO_VSI ||
	     f_info->fltr_act == ICE_FWD_TO_VSI_LIST ||
	     f_info->fltr_act == ICE_FWD_TO_Q ||
	     f_info->fltr_act == ICE_FWD_TO_QGRP)) {
		f_info->lb_en = true;
		if (!(hw->evb_veb && f_info->lkup_type == ICE_SW_LKUP_MAC &&
		      is_unicast_ether_addr(f_info->l_data.mac.mac_addr)))
			f_info->lan_en = true;
	}
}

/**
 * ice_fill_sw_rule - Helper function to fill switch rule structure
 * @hw: pointer to the hardware structure
 * @f_info: entry containing packet forwarding information
 * @s_rule: switch rule structure to be filled in based on mac_entry
 * @opc: switch rules population command type - pass in the command opcode
 */
static void
ice_fill_sw_rule(struct ice_hw *hw, struct ice_fltr_info *f_info,
		 struct ice_aqc_sw_rules_elem *s_rule, enum ice_adminq_opc opc)
{
	u16 vlan_id = ICE_MAX_VLAN_ID + 1;
	u8 eth_hdr[DUMMY_ETH_HDR_LEN];
	void *daddr = NULL;
	u32 act = 0;
	__be16 *off;

	if (opc == ice_aqc_opc_remove_sw_rules) {
		s_rule->pdata.lkup_tx_rx.act = 0;
		s_rule->pdata.lkup_tx_rx.index =
			cpu_to_le16(f_info->fltr_rule_id);
		s_rule->pdata.lkup_tx_rx.hdr_len = 0;
		return;
	}

	/* initialize the ether header with a dummy header */
	memcpy(eth_hdr, dummy_eth_header, sizeof(dummy_eth_header));
	ice_fill_sw_info(hw, f_info);

	switch (f_info->fltr_act) {
	case ICE_FWD_TO_VSI:
		act |= (f_info->fwd_id.vsi_id << ICE_SINGLE_ACT_VSI_ID_S) &
			ICE_SINGLE_ACT_VSI_ID_M;
		if (f_info->lkup_type != ICE_SW_LKUP_VLAN)
			act |= ICE_SINGLE_ACT_VSI_FORWARDING |
				ICE_SINGLE_ACT_VALID_BIT;
		break;
	case ICE_FWD_TO_VSI_LIST:
		act |= ICE_SINGLE_ACT_VSI_LIST;
		act |= (f_info->fwd_id.vsi_list_id <<
			ICE_SINGLE_ACT_VSI_LIST_ID_S) &
			ICE_SINGLE_ACT_VSI_LIST_ID_M;
		if (f_info->lkup_type != ICE_SW_LKUP_VLAN)
			act |= ICE_SINGLE_ACT_VSI_FORWARDING |
				ICE_SINGLE_ACT_VALID_BIT;
		break;
	case ICE_FWD_TO_Q:
		act |= ICE_SINGLE_ACT_TO_Q;
		act |= (f_info->fwd_id.q_id << ICE_SINGLE_ACT_Q_INDEX_S) &
			ICE_SINGLE_ACT_Q_INDEX_M;
		break;
	case ICE_FWD_TO_QGRP:
		act |= ICE_SINGLE_ACT_TO_Q;
		act |= (f_info->qgrp_size << ICE_SINGLE_ACT_Q_REGION_S) &
			ICE_SINGLE_ACT_Q_REGION_M;
		break;
	case ICE_DROP_PACKET:
		act |= ICE_SINGLE_ACT_VSI_FORWARDING | ICE_SINGLE_ACT_DROP;
		break;
	default:
		return;
	}

	if (f_info->lb_en)
		act |= ICE_SINGLE_ACT_LB_ENABLE;
	if (f_info->lan_en)
		act |= ICE_SINGLE_ACT_LAN_ENABLE;

	switch (f_info->lkup_type) {
	case ICE_SW_LKUP_MAC:
		daddr = f_info->l_data.mac.mac_addr;
		break;
	case ICE_SW_LKUP_VLAN:
		vlan_id = f_info->l_data.vlan.vlan_id;
		if (f_info->fltr_act == ICE_FWD_TO_VSI ||
		    f_info->fltr_act == ICE_FWD_TO_VSI_LIST) {
			act |= ICE_SINGLE_ACT_PRUNE;
			act |= ICE_SINGLE_ACT_EGRESS | ICE_SINGLE_ACT_INGRESS;
		}
		break;
	case ICE_SW_LKUP_ETHERTYPE_MAC:
		daddr = f_info->l_data.ethertype_mac.mac_addr;
		/* fall-through */
	case ICE_SW_LKUP_ETHERTYPE:
		off = (__be16 *)&eth_hdr[ICE_ETH_ETHTYPE_OFFSET];
		*off = cpu_to_be16(f_info->l_data.ethertype_mac.ethertype);
		break;
	case ICE_SW_LKUP_MAC_VLAN:
		daddr = f_info->l_data.mac_vlan.mac_addr;
		vlan_id = f_info->l_data.mac_vlan.vlan_id;
		break;
	case ICE_SW_LKUP_PROMISC_VLAN:
		vlan_id = f_info->l_data.mac_vlan.vlan_id;
		/* fall-through */
	case ICE_SW_LKUP_PROMISC:
		daddr = f_info->l_data.mac_vlan.mac_addr;
		break;
	default:
		break;
	}

	s_rule->type = (f_info->flag & ICE_FLTR_RX) ?
		cpu_to_le16(ICE_AQC_SW_RULES_T_LKUP_RX) :
		cpu_to_le16(ICE_AQC_SW_RULES_T_LKUP_TX);

	/* Recipe set depending on lookup type */
	s_rule->pdata.lkup_tx_rx.recipe_id = cpu_to_le16(f_info->lkup_type);
	s_rule->pdata.lkup_tx_rx.src = cpu_to_le16(f_info->src);
	s_rule->pdata.lkup_tx_rx.act = cpu_to_le32(act);

	if (daddr)
		ether_addr_copy(&eth_hdr[ICE_ETH_DA_OFFSET], daddr);

	if (!(vlan_id > ICE_MAX_VLAN_ID)) {
		off = (__be16 *)&eth_hdr[ICE_ETH_VLAN_TCI_OFFSET];
		*off = cpu_to_be16(vlan_id);
	}

	/* Create the switch rule with the final dummy Ethernet header */
	if (opc != ice_aqc_opc_update_sw_rules)
		s_rule->pdata.lkup_tx_rx.hdr_len = cpu_to_le16(sizeof(eth_hdr));

	memcpy(s_rule->pdata.lkup_tx_rx.hdr, eth_hdr, sizeof(eth_hdr));
}

/**
 * ice_add_marker_act
 * @hw: pointer to the hardware structure
 * @m_ent: the management entry for which sw marker needs to be added
 * @sw_marker: sw marker to tag the Rx descriptor with
 * @l_id: large action resource id
 *
 * Create a large action to hold software marker and update the switch rule
 * entry pointed by m_ent with newly created large action
 */
static enum ice_status
ice_add_marker_act(struct ice_hw *hw, struct ice_fltr_mgmt_list_entry *m_ent,
		   u16 sw_marker, u16 l_id)
{
	struct ice_aqc_sw_rules_elem *lg_act, *rx_tx;
	/* For software marker we need 3 large actions
	 * 1. FWD action: FWD TO VSI or VSI LIST
	 * 2. GENERIC VALUE action to hold the profile id
	 * 3. GENERIC VALUE action to hold the software marker id
	 */
	const u16 num_lg_acts = 3;
	enum ice_status status;
	u16 lg_act_size;
	u16 rules_size;
	u16 vsi_info;
	u32 act;

	if (m_ent->fltr_info.lkup_type != ICE_SW_LKUP_MAC)
		return ICE_ERR_PARAM;

	/* Create two back-to-back switch rules and submit them to the HW using
	 * one memory buffer:
	 *    1. Large Action
	 *    2. Look up tx rx
	 */
	lg_act_size = (u16)ICE_SW_RULE_LG_ACT_SIZE(num_lg_acts);
	rules_size = lg_act_size + ICE_SW_RULE_RX_TX_ETH_HDR_SIZE;
	lg_act = devm_kzalloc(ice_hw_to_dev(hw), rules_size, GFP_KERNEL);
	if (!lg_act)
		return ICE_ERR_NO_MEMORY;

	rx_tx = (struct ice_aqc_sw_rules_elem *)((u8 *)lg_act + lg_act_size);

	/* Fill in the first switch rule i.e. large action */
	lg_act->type = cpu_to_le16(ICE_AQC_SW_RULES_T_LG_ACT);
	lg_act->pdata.lg_act.index = cpu_to_le16(l_id);
	lg_act->pdata.lg_act.size = cpu_to_le16(num_lg_acts);

	/* First action VSI forwarding or VSI list forwarding depending on how
	 * many VSIs
	 */
	vsi_info = (m_ent->vsi_count > 1) ?
		m_ent->fltr_info.fwd_id.vsi_list_id :
		m_ent->fltr_info.fwd_id.vsi_id;

	act = ICE_LG_ACT_VSI_FORWARDING | ICE_LG_ACT_VALID_BIT;
	act |= (vsi_info << ICE_LG_ACT_VSI_LIST_ID_S) &
		ICE_LG_ACT_VSI_LIST_ID_M;
	if (m_ent->vsi_count > 1)
		act |= ICE_LG_ACT_VSI_LIST;
	lg_act->pdata.lg_act.act[0] = cpu_to_le32(act);

	/* Second action descriptor type */
	act = ICE_LG_ACT_GENERIC;

	act |= (1 << ICE_LG_ACT_GENERIC_VALUE_S) & ICE_LG_ACT_GENERIC_VALUE_M;
	lg_act->pdata.lg_act.act[1] = cpu_to_le32(act);

	act = (7 << ICE_LG_ACT_GENERIC_OFFSET_S) & ICE_LG_ACT_GENERIC_VALUE_M;

	/* Third action Marker value */
	act |= ICE_LG_ACT_GENERIC;
	act |= (sw_marker << ICE_LG_ACT_GENERIC_VALUE_S) &
		ICE_LG_ACT_GENERIC_VALUE_M;

	act |= (0 << ICE_LG_ACT_GENERIC_OFFSET_S) & ICE_LG_ACT_GENERIC_VALUE_M;
	lg_act->pdata.lg_act.act[2] = cpu_to_le32(act);

	/* call the fill switch rule to fill the lookup tx rx structure */
	ice_fill_sw_rule(hw, &m_ent->fltr_info, rx_tx,
			 ice_aqc_opc_update_sw_rules);

	/* Update the action to point to the large action id */
	rx_tx->pdata.lkup_tx_rx.act =
		cpu_to_le32(ICE_SINGLE_ACT_PTR |
			    ((l_id << ICE_SINGLE_ACT_PTR_VAL_S) &
			     ICE_SINGLE_ACT_PTR_VAL_M));

	/* Use the filter rule id of the previously created rule with single
	 * act. Once the update happens, hardware will treat this as large
	 * action
	 */
	rx_tx->pdata.lkup_tx_rx.index =
		cpu_to_le16(m_ent->fltr_info.fltr_rule_id);

	status = ice_aq_sw_rules(hw, lg_act, rules_size, 2,
				 ice_aqc_opc_update_sw_rules, NULL);
	if (!status) {
		m_ent->lg_act_idx = l_id;
		m_ent->sw_marker_id = sw_marker;
	}

	devm_kfree(ice_hw_to_dev(hw), lg_act);
	return status;
}

/**
 * ice_create_vsi_list_map
 * @hw: pointer to the hardware structure
 * @vsi_array: array of VSIs to form a VSI list
 * @num_vsi: num VSI in the array
 * @vsi_list_id: VSI list id generated as part of allocate resource
 *
 * Helper function to create a new entry of VSI list id to VSI mapping
 * using the given VSI list id
 */
static struct ice_vsi_list_map_info *
ice_create_vsi_list_map(struct ice_hw *hw, u16 *vsi_array, u16 num_vsi,
			u16 vsi_list_id)
{
	struct ice_switch_info *sw = hw->switch_info;
	struct ice_vsi_list_map_info *v_map;
	int i;

	v_map = devm_kcalloc(ice_hw_to_dev(hw), 1, sizeof(*v_map), GFP_KERNEL);
	if (!v_map)
		return NULL;

	v_map->vsi_list_id = vsi_list_id;

	for (i = 0; i < num_vsi; i++)
		set_bit(vsi_array[i], v_map->vsi_map);

	list_add(&v_map->list_entry, &sw->vsi_list_map_head);
	return v_map;
}

/**
 * ice_update_vsi_list_rule
 * @hw: pointer to the hardware structure
 * @vsi_array: array of VSIs to form a VSI list
 * @num_vsi: num VSI in the array
 * @vsi_list_id: VSI list id generated as part of allocate resource
 * @remove: Boolean value to indicate if this is a remove action
 * @opc: switch rules population command type - pass in the command opcode
 * @lkup_type: lookup type of the filter
 *
 * Call AQ command to add a new switch rule or update existing switch rule
 * using the given VSI list id
 */
static enum ice_status
ice_update_vsi_list_rule(struct ice_hw *hw, u16 *vsi_array, u16 num_vsi,
			 u16 vsi_list_id, bool remove, enum ice_adminq_opc opc,
			 enum ice_sw_lkup_type lkup_type)
{
	struct ice_aqc_sw_rules_elem *s_rule;
	enum ice_status status;
	u16 s_rule_size;
	u16 type;
	int i;

	if (!num_vsi)
		return ICE_ERR_PARAM;

	if (lkup_type == ICE_SW_LKUP_MAC ||
	    lkup_type == ICE_SW_LKUP_MAC_VLAN ||
	    lkup_type == ICE_SW_LKUP_ETHERTYPE ||
	    lkup_type == ICE_SW_LKUP_ETHERTYPE_MAC ||
	    lkup_type == ICE_SW_LKUP_PROMISC ||
	    lkup_type == ICE_SW_LKUP_PROMISC_VLAN)
		type = remove ? ICE_AQC_SW_RULES_T_VSI_LIST_CLEAR :
				ICE_AQC_SW_RULES_T_VSI_LIST_SET;
	else if (lkup_type == ICE_SW_LKUP_VLAN)
		type = remove ? ICE_AQC_SW_RULES_T_PRUNE_LIST_CLEAR :
				ICE_AQC_SW_RULES_T_PRUNE_LIST_SET;
	else
		return ICE_ERR_PARAM;

	s_rule_size = (u16)ICE_SW_RULE_VSI_LIST_SIZE(num_vsi);
	s_rule = devm_kzalloc(ice_hw_to_dev(hw), s_rule_size, GFP_KERNEL);
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;

	for (i = 0; i < num_vsi; i++)
		s_rule->pdata.vsi_list.vsi[i] = cpu_to_le16(vsi_array[i]);

	s_rule->type = cpu_to_le16(type);
	s_rule->pdata.vsi_list.number_vsi = cpu_to_le16(num_vsi);
	s_rule->pdata.vsi_list.index = cpu_to_le16(vsi_list_id);

	status = ice_aq_sw_rules(hw, s_rule, s_rule_size, 1, opc, NULL);

	devm_kfree(ice_hw_to_dev(hw), s_rule);
	return status;
}

/**
 * ice_create_vsi_list_rule - Creates and populates a VSI list rule
 * @hw: pointer to the hw struct
 * @vsi_array: array of VSIs to form a VSI list
 * @num_vsi: number of VSIs in the array
 * @vsi_list_id: stores the ID of the VSI list to be created
 * @lkup_type: switch rule filter's lookup type
 */
static enum ice_status
ice_create_vsi_list_rule(struct ice_hw *hw, u16 *vsi_array, u16 num_vsi,
			 u16 *vsi_list_id, enum ice_sw_lkup_type lkup_type)
{
	enum ice_status status;
	int i;

	for (i = 0; i < num_vsi; i++)
		if (vsi_array[i] >= ICE_MAX_VSI)
			return ICE_ERR_OUT_OF_RANGE;

	status = ice_aq_alloc_free_vsi_list(hw, vsi_list_id, lkup_type,
					    ice_aqc_opc_alloc_res);
	if (status)
		return status;

	/* Update the newly created VSI list to include the specified VSIs */
	return ice_update_vsi_list_rule(hw, vsi_array, num_vsi, *vsi_list_id,
					false, ice_aqc_opc_add_sw_rules,
					lkup_type);
}

/**
 * ice_create_pkt_fwd_rule
 * @hw: pointer to the hardware structure
 * @f_entry: entry containing packet forwarding information
 *
 * Create switch rule with given filter information and add an entry
 * to the corresponding filter management list to track this switch rule
 * and VSI mapping
 */
static enum ice_status
ice_create_pkt_fwd_rule(struct ice_hw *hw,
			struct ice_fltr_list_entry *f_entry)
{
	struct ice_switch_info *sw = hw->switch_info;
	struct ice_fltr_mgmt_list_entry *fm_entry;
	struct ice_aqc_sw_rules_elem *s_rule;
	enum ice_sw_lkup_type l_type;
	enum ice_status status;

	s_rule = devm_kzalloc(ice_hw_to_dev(hw),
			      ICE_SW_RULE_RX_TX_ETH_HDR_SIZE, GFP_KERNEL);
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;
	fm_entry = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*fm_entry),
				GFP_KERNEL);
	if (!fm_entry) {
		status = ICE_ERR_NO_MEMORY;
		goto ice_create_pkt_fwd_rule_exit;
	}

	fm_entry->fltr_info = f_entry->fltr_info;

	/* Initialize all the fields for the management entry */
	fm_entry->vsi_count = 1;
	fm_entry->lg_act_idx = ICE_INVAL_LG_ACT_INDEX;
	fm_entry->sw_marker_id = ICE_INVAL_SW_MARKER_ID;
	fm_entry->counter_index = ICE_INVAL_COUNTER_ID;

	ice_fill_sw_rule(hw, &fm_entry->fltr_info, s_rule,
			 ice_aqc_opc_add_sw_rules);

	status = ice_aq_sw_rules(hw, s_rule, ICE_SW_RULE_RX_TX_ETH_HDR_SIZE, 1,
				 ice_aqc_opc_add_sw_rules, NULL);
	if (status) {
		devm_kfree(ice_hw_to_dev(hw), fm_entry);
		goto ice_create_pkt_fwd_rule_exit;
	}

	f_entry->fltr_info.fltr_rule_id =
		le16_to_cpu(s_rule->pdata.lkup_tx_rx.index);
	fm_entry->fltr_info.fltr_rule_id =
		le16_to_cpu(s_rule->pdata.lkup_tx_rx.index);

	/* The book keeping entries will get removed when base driver
	 * calls remove filter AQ command
	 */
	l_type = fm_entry->fltr_info.lkup_type;
	if (l_type == ICE_SW_LKUP_MAC) {
		mutex_lock(&sw->mac_list_lock);
		list_add(&fm_entry->list_entry, &sw->mac_list_head);
		mutex_unlock(&sw->mac_list_lock);
	} else if (l_type == ICE_SW_LKUP_VLAN) {
		mutex_lock(&sw->vlan_list_lock);
		list_add(&fm_entry->list_entry, &sw->vlan_list_head);
		mutex_unlock(&sw->vlan_list_lock);
	} else if (l_type == ICE_SW_LKUP_ETHERTYPE ||
		   l_type == ICE_SW_LKUP_ETHERTYPE_MAC) {
		mutex_lock(&sw->eth_m_list_lock);
		list_add(&fm_entry->list_entry, &sw->eth_m_list_head);
		mutex_unlock(&sw->eth_m_list_lock);
	} else if (l_type == ICE_SW_LKUP_PROMISC ||
		   l_type == ICE_SW_LKUP_PROMISC_VLAN) {
		mutex_lock(&sw->promisc_list_lock);
		list_add(&fm_entry->list_entry, &sw->promisc_list_head);
		mutex_unlock(&sw->promisc_list_lock);
	} else if (fm_entry->fltr_info.lkup_type == ICE_SW_LKUP_MAC_VLAN) {
		mutex_lock(&sw->mac_vlan_list_lock);
		list_add(&fm_entry->list_entry, &sw->mac_vlan_list_head);
		mutex_unlock(&sw->mac_vlan_list_lock);
	} else {
		status = ICE_ERR_NOT_IMPL;
	}
ice_create_pkt_fwd_rule_exit:
	devm_kfree(ice_hw_to_dev(hw), s_rule);
	return status;
}

/**
 * ice_update_pkt_fwd_rule
 * @hw: pointer to the hardware structure
 * @rule_id: rule of previously created switch rule to update
 * @vsi_list_id: VSI list id to be updated with
 * @f_info: ice_fltr_info to pull other information for switch rule
 *
 * Call AQ command to update a previously created switch rule with a
 * VSI list id
 */
static enum ice_status
ice_update_pkt_fwd_rule(struct ice_hw *hw, u16 rule_id, u16 vsi_list_id,
			struct ice_fltr_info f_info)
{
	struct ice_aqc_sw_rules_elem *s_rule;
	struct ice_fltr_info tmp_fltr;
	enum ice_status status;

	s_rule = devm_kzalloc(ice_hw_to_dev(hw),
			      ICE_SW_RULE_RX_TX_ETH_HDR_SIZE, GFP_KERNEL);
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;

	tmp_fltr = f_info;
	tmp_fltr.fltr_act = ICE_FWD_TO_VSI_LIST;
	tmp_fltr.fwd_id.vsi_list_id = vsi_list_id;

	ice_fill_sw_rule(hw, &tmp_fltr, s_rule,
			 ice_aqc_opc_update_sw_rules);

	s_rule->pdata.lkup_tx_rx.index = cpu_to_le16(rule_id);

	/* Update switch rule with new rule set to forward VSI list */
	status = ice_aq_sw_rules(hw, s_rule, ICE_SW_RULE_RX_TX_ETH_HDR_SIZE, 1,
				 ice_aqc_opc_update_sw_rules, NULL);

	devm_kfree(ice_hw_to_dev(hw), s_rule);
	return status;
}

/**
 * ice_handle_vsi_list_mgmt
 * @hw: pointer to the hardware structure
 * @m_entry: pointer to current filter management list entry
 * @cur_fltr: filter information from the book keeping entry
 * @new_fltr: filter information with the new VSI to be added
 *
 * Call AQ command to add or update previously created VSI list with new VSI.
 *
 * Helper function to do book keeping associated with adding filter information
 * The algorithm to do the booking keeping is described below :
 * When a VSI needs to subscribe to a given filter( MAC/VLAN/Ethtype etc.)
 *	if only one VSI has been added till now
 *		Allocate a new VSI list and add two VSIs
 *		to this list using switch rule command
 *		Update the previously created switch rule with the
 *		newly created VSI list id
 *	if a VSI list was previously created
 *		Add the new VSI to the previously created VSI list set
 *		using the update switch rule command
 */
static enum ice_status
ice_handle_vsi_list_mgmt(struct ice_hw *hw,
			 struct ice_fltr_mgmt_list_entry *m_entry,
			 struct ice_fltr_info *cur_fltr,
			 struct ice_fltr_info *new_fltr)
{
	enum ice_status status = 0;
	u16 vsi_list_id = 0;

	if ((cur_fltr->fltr_act == ICE_FWD_TO_Q ||
	     cur_fltr->fltr_act == ICE_FWD_TO_QGRP))
		return ICE_ERR_NOT_IMPL;

	if ((new_fltr->fltr_act == ICE_FWD_TO_Q ||
	     new_fltr->fltr_act == ICE_FWD_TO_QGRP) &&
	    (cur_fltr->fltr_act == ICE_FWD_TO_VSI ||
	     cur_fltr->fltr_act == ICE_FWD_TO_VSI_LIST))
		return ICE_ERR_NOT_IMPL;

	if (m_entry->vsi_count < 2 && !m_entry->vsi_list_info) {
		/* Only one entry existed in the mapping and it was not already
		 * a part of a VSI list. So, create a VSI list with the old and
		 * new VSIs.
		 */
		u16 vsi_id_arr[2];
		u16 fltr_rule;

		/* A rule already exists with the new VSI being added */
		if (cur_fltr->fwd_id.vsi_id == new_fltr->fwd_id.vsi_id)
			return ICE_ERR_ALREADY_EXISTS;

		vsi_id_arr[0] = cur_fltr->fwd_id.vsi_id;
		vsi_id_arr[1] = new_fltr->fwd_id.vsi_id;
		status = ice_create_vsi_list_rule(hw, &vsi_id_arr[0], 2,
						  &vsi_list_id,
						  new_fltr->lkup_type);
		if (status)
			return status;

		fltr_rule = cur_fltr->fltr_rule_id;
		/* Update the previous switch rule of "MAC forward to VSI" to
		 * "MAC fwd to VSI list"
		 */
		status = ice_update_pkt_fwd_rule(hw, fltr_rule, vsi_list_id,
						 *new_fltr);
		if (status)
			return status;

		cur_fltr->fwd_id.vsi_list_id = vsi_list_id;
		cur_fltr->fltr_act = ICE_FWD_TO_VSI_LIST;
		m_entry->vsi_list_info =
			ice_create_vsi_list_map(hw, &vsi_id_arr[0], 2,
						vsi_list_id);

		/* If this entry was large action then the large action needs
		 * to be updated to point to FWD to VSI list
		 */
		if (m_entry->sw_marker_id != ICE_INVAL_SW_MARKER_ID)
			status =
			    ice_add_marker_act(hw, m_entry,
					       m_entry->sw_marker_id,
					       m_entry->lg_act_idx);
	} else {
		u16 vsi_id = new_fltr->fwd_id.vsi_id;
		enum ice_adminq_opc opcode;

		/* A rule already exists with the new VSI being added */
		if (test_bit(vsi_id, m_entry->vsi_list_info->vsi_map))
			return 0;

		/* Update the previously created VSI list set with
		 * the new VSI id passed in
		 */
		vsi_list_id = cur_fltr->fwd_id.vsi_list_id;
		opcode = ice_aqc_opc_update_sw_rules;

		status = ice_update_vsi_list_rule(hw, &vsi_id, 1, vsi_list_id,
						  false, opcode,
						  new_fltr->lkup_type);
		/* update VSI list mapping info with new VSI id */
		if (!status)
			set_bit(vsi_id, m_entry->vsi_list_info->vsi_map);
	}
	if (!status)
		m_entry->vsi_count++;
	return status;
}

/**
 * ice_find_mac_entry
 * @hw: pointer to the hardware structure
 * @mac_addr: MAC address to search for
 *
 * Helper function to search for a MAC entry using a given MAC address
 * Returns pointer to the entry if found.
 */
static struct ice_fltr_mgmt_list_entry *
ice_find_mac_entry(struct ice_hw *hw, u8 *mac_addr)
{
	struct ice_fltr_mgmt_list_entry *m_list_itr, *mac_ret = NULL;
	struct ice_switch_info *sw = hw->switch_info;

	mutex_lock(&sw->mac_list_lock);
	list_for_each_entry(m_list_itr, &sw->mac_list_head, list_entry) {
		u8 *buf = &m_list_itr->fltr_info.l_data.mac.mac_addr[0];

		if (ether_addr_equal(buf, mac_addr)) {
			mac_ret = m_list_itr;
			break;
		}
	}
	mutex_unlock(&sw->mac_list_lock);
	return mac_ret;
}

/**
 * ice_add_shared_mac - Add one MAC shared filter rule
 * @hw: pointer to the hardware structure
 * @f_entry: structure containing MAC forwarding information
 *
 * Adds or updates the book keeping list for the MAC addresses
 */
static enum ice_status
ice_add_shared_mac(struct ice_hw *hw, struct ice_fltr_list_entry *f_entry)
{
	struct ice_fltr_info *new_fltr, *cur_fltr;
	struct ice_fltr_mgmt_list_entry *m_entry;

	new_fltr = &f_entry->fltr_info;

	m_entry = ice_find_mac_entry(hw, &new_fltr->l_data.mac.mac_addr[0]);
	if (!m_entry)
		return ice_create_pkt_fwd_rule(hw, f_entry);

	cur_fltr = &m_entry->fltr_info;

	return ice_handle_vsi_list_mgmt(hw, m_entry, cur_fltr, new_fltr);
}

/**
 * ice_add_mac - Add a MAC address based filter rule
 * @hw: pointer to the hardware structure
 * @m_list: list of MAC addresses and forwarding information
 *
 * IMPORTANT: When the ucast_shared flag is set to false and m_list has
 * multiple unicast addresses, the function assumes that all the
 * addresses are unique in a given add_mac call. It doesn't
 * check for duplicates in this case, removing duplicates from a given
 * list should be taken care of in the caller of this function.
 */
enum ice_status
ice_add_mac(struct ice_hw *hw, struct list_head *m_list)
{
	struct ice_aqc_sw_rules_elem *s_rule, *r_iter;
	struct ice_fltr_list_entry *m_list_itr;
	u16 elem_sent, total_elem_left;
	enum ice_status status = 0;
	u16 num_unicast = 0;
	u16 s_rule_size;

	if (!m_list || !hw)
		return ICE_ERR_PARAM;

	list_for_each_entry(m_list_itr, m_list, list_entry) {
		u8 *add = &m_list_itr->fltr_info.l_data.mac.mac_addr[0];

		if (m_list_itr->fltr_info.lkup_type != ICE_SW_LKUP_MAC)
			return ICE_ERR_PARAM;
		if (is_zero_ether_addr(add))
			return ICE_ERR_PARAM;
		if (is_unicast_ether_addr(add) && !hw->ucast_shared) {
			/* Don't overwrite the unicast address */
			if (ice_find_mac_entry(hw, add))
				return ICE_ERR_ALREADY_EXISTS;
			num_unicast++;
		} else if (is_multicast_ether_addr(add) ||
			   (is_unicast_ether_addr(add) && hw->ucast_shared)) {
			status = ice_add_shared_mac(hw, m_list_itr);
			if (status) {
				m_list_itr->status = ICE_FLTR_STATUS_FW_FAIL;
				return status;
			}
			m_list_itr->status = ICE_FLTR_STATUS_FW_SUCCESS;
		}
	}

	/* Exit if no suitable entries were found for adding bulk switch rule */
	if (!num_unicast)
		return 0;

	/* Allocate switch rule buffer for the bulk update for unicast */
	s_rule_size = ICE_SW_RULE_RX_TX_ETH_HDR_SIZE;
	s_rule = devm_kcalloc(ice_hw_to_dev(hw), num_unicast, s_rule_size,
			      GFP_KERNEL);
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;

	r_iter = s_rule;
	list_for_each_entry(m_list_itr, m_list, list_entry) {
		struct ice_fltr_info *f_info = &m_list_itr->fltr_info;
		u8 *addr = &f_info->l_data.mac.mac_addr[0];

		if (is_unicast_ether_addr(addr)) {
			ice_fill_sw_rule(hw, &m_list_itr->fltr_info,
					 r_iter, ice_aqc_opc_add_sw_rules);
			r_iter = (struct ice_aqc_sw_rules_elem *)
				((u8 *)r_iter + s_rule_size);
		}
	}

	/* Call AQ bulk switch rule update for all unicast addresses */
	r_iter = s_rule;
	/* Call AQ switch rule in AQ_MAX chunk */
	for (total_elem_left = num_unicast; total_elem_left > 0;
	     total_elem_left -= elem_sent) {
		struct ice_aqc_sw_rules_elem *entry = r_iter;

		elem_sent = min(total_elem_left,
				(u16)(ICE_AQ_MAX_BUF_LEN / s_rule_size));
		status = ice_aq_sw_rules(hw, entry, elem_sent * s_rule_size,
					 elem_sent, ice_aqc_opc_add_sw_rules,
					 NULL);
		if (status)
			goto ice_add_mac_exit;
		r_iter = (struct ice_aqc_sw_rules_elem *)
			((u8 *)r_iter + (elem_sent * s_rule_size));
	}

	/* Fill up rule id based on the value returned from FW */
	r_iter = s_rule;
	list_for_each_entry(m_list_itr, m_list, list_entry) {
		struct ice_fltr_info *f_info = &m_list_itr->fltr_info;
		u8 *addr = &f_info->l_data.mac.mac_addr[0];
		struct ice_switch_info *sw = hw->switch_info;
		struct ice_fltr_mgmt_list_entry *fm_entry;

		if (is_unicast_ether_addr(addr)) {
			f_info->fltr_rule_id =
				le16_to_cpu(r_iter->pdata.lkup_tx_rx.index);
			f_info->fltr_act = ICE_FWD_TO_VSI;
			/* Create an entry to track this MAC address */
			fm_entry = devm_kzalloc(ice_hw_to_dev(hw),
						sizeof(*fm_entry), GFP_KERNEL);
			if (!fm_entry) {
				status = ICE_ERR_NO_MEMORY;
				goto ice_add_mac_exit;
			}
			fm_entry->fltr_info = *f_info;
			fm_entry->vsi_count = 1;
			/* The book keeping entries will get removed when
			 * base driver calls remove filter AQ command
			 */
			mutex_lock(&sw->mac_list_lock);
			list_add(&fm_entry->list_entry, &sw->mac_list_head);
			mutex_unlock(&sw->mac_list_lock);

			r_iter = (struct ice_aqc_sw_rules_elem *)
				((u8 *)r_iter + s_rule_size);
		}
	}

ice_add_mac_exit:
	devm_kfree(ice_hw_to_dev(hw), s_rule);
	return status;
}

/**
 * ice_find_vlan_entry
 * @hw: pointer to the hardware structure
 * @vlan_id: VLAN id to search for
 *
 * Helper function to search for a VLAN entry using a given VLAN id
 * Returns pointer to the entry if found.
 */
static struct ice_fltr_mgmt_list_entry *
ice_find_vlan_entry(struct ice_hw *hw, u16 vlan_id)
{
	struct ice_fltr_mgmt_list_entry *vlan_list_itr, *vlan_ret = NULL;
	struct ice_switch_info *sw = hw->switch_info;

	mutex_lock(&sw->vlan_list_lock);
	list_for_each_entry(vlan_list_itr, &sw->vlan_list_head, list_entry)
		if (vlan_list_itr->fltr_info.l_data.vlan.vlan_id == vlan_id) {
			vlan_ret = vlan_list_itr;
			break;
		}

	mutex_unlock(&sw->vlan_list_lock);
	return vlan_ret;
}

/**
 * ice_add_vlan_internal - Add one VLAN based filter rule
 * @hw: pointer to the hardware structure
 * @f_entry: filter entry containing one VLAN information
 */
static enum ice_status
ice_add_vlan_internal(struct ice_hw *hw, struct ice_fltr_list_entry *f_entry)
{
	struct ice_fltr_info *new_fltr, *cur_fltr;
	struct ice_fltr_mgmt_list_entry *v_list_itr;
	u16 vlan_id;

	new_fltr = &f_entry->fltr_info;
	/* VLAN id should only be 12 bits */
	if (new_fltr->l_data.vlan.vlan_id > ICE_MAX_VLAN_ID)
		return ICE_ERR_PARAM;

	vlan_id = new_fltr->l_data.vlan.vlan_id;
	v_list_itr = ice_find_vlan_entry(hw, vlan_id);
	if (!v_list_itr) {
		u16 vsi_id = ICE_VSI_INVAL_ID;
		enum ice_status status;
		u16 vsi_list_id = 0;

		if (new_fltr->fltr_act == ICE_FWD_TO_VSI) {
			enum ice_sw_lkup_type lkup_type = new_fltr->lkup_type;

			/* All VLAN pruning rules use a VSI list.
			 * Convert the action to forwarding to a VSI list.
			 */
			vsi_id = new_fltr->fwd_id.vsi_id;
			status = ice_create_vsi_list_rule(hw, &vsi_id, 1,
							  &vsi_list_id,
							  lkup_type);
			if (status)
				return status;
			new_fltr->fltr_act = ICE_FWD_TO_VSI_LIST;
			new_fltr->fwd_id.vsi_list_id = vsi_list_id;
		}

		status = ice_create_pkt_fwd_rule(hw, f_entry);
		if (!status && vsi_id != ICE_VSI_INVAL_ID) {
			v_list_itr = ice_find_vlan_entry(hw, vlan_id);
			if (!v_list_itr)
				return ICE_ERR_DOES_NOT_EXIST;
			v_list_itr->vsi_list_info =
				ice_create_vsi_list_map(hw, &vsi_id, 1,
							vsi_list_id);
		}

		return status;
	}

	cur_fltr = &v_list_itr->fltr_info;
	return ice_handle_vsi_list_mgmt(hw, v_list_itr, cur_fltr, new_fltr);
}

/**
 * ice_add_vlan - Add VLAN based filter rule
 * @hw: pointer to the hardware structure
 * @v_list: list of VLAN entries and forwarding information
 */
enum ice_status
ice_add_vlan(struct ice_hw *hw, struct list_head *v_list)
{
	struct ice_fltr_list_entry *v_list_itr;

	if (!v_list || !hw)
		return ICE_ERR_PARAM;

	list_for_each_entry(v_list_itr, v_list, list_entry) {
		enum ice_status status;

		if (v_list_itr->fltr_info.lkup_type != ICE_SW_LKUP_VLAN)
			return ICE_ERR_PARAM;

		status = ice_add_vlan_internal(hw, v_list_itr);
		if (status) {
			v_list_itr->status = ICE_FLTR_STATUS_FW_FAIL;
			return status;
		}
		v_list_itr->status = ICE_FLTR_STATUS_FW_SUCCESS;
	}
	return 0;
}

/**
 * ice_remove_vsi_list_rule
 * @hw: pointer to the hardware structure
 * @vsi_list_id: VSI list id generated as part of allocate resource
 * @lkup_type: switch rule filter lookup type
 */
static enum ice_status
ice_remove_vsi_list_rule(struct ice_hw *hw, u16 vsi_list_id,
			 enum ice_sw_lkup_type lkup_type)
{
	struct ice_aqc_sw_rules_elem *s_rule;
	enum ice_status status;
	u16 s_rule_size;

	s_rule_size = (u16)ICE_SW_RULE_VSI_LIST_SIZE(0);
	s_rule = devm_kzalloc(ice_hw_to_dev(hw), s_rule_size, GFP_KERNEL);
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;

	s_rule->type = cpu_to_le16(ICE_AQC_SW_RULES_T_VSI_LIST_CLEAR);
	s_rule->pdata.vsi_list.index = cpu_to_le16(vsi_list_id);
	/* FW expects number of VSIs in vsi_list resource to be 0 for clear
	 * command. Since memory is zero'ed out during initialization, it's not
	 * necessary to explicitly initialize the variable to 0.
	 */

	status = ice_aq_sw_rules(hw, s_rule, s_rule_size, 1,
				 ice_aqc_opc_remove_sw_rules, NULL);
	if (!status)
		/* Free the vsi_list resource that we allocated */
		status = ice_aq_alloc_free_vsi_list(hw, &vsi_list_id, lkup_type,
						    ice_aqc_opc_free_res);

	devm_kfree(ice_hw_to_dev(hw), s_rule);
	return status;
}

/**
 * ice_handle_rem_vsi_list_mgmt
 * @hw: pointer to the hardware structure
 * @vsi_id: ID of the VSI to remove
 * @fm_list_itr: filter management entry for which the VSI list management
 * needs to be done
 */
static enum ice_status
ice_handle_rem_vsi_list_mgmt(struct ice_hw *hw, u16 vsi_id,
			     struct ice_fltr_mgmt_list_entry *fm_list_itr)
{
	struct ice_switch_info *sw = hw->switch_info;
	enum ice_status status = 0;
	enum ice_sw_lkup_type lkup_type;
	bool is_last_elem = true;
	bool conv_list = false;
	bool del_list = false;
	u16 vsi_list_id;

	lkup_type = fm_list_itr->fltr_info.lkup_type;
	vsi_list_id = fm_list_itr->fltr_info.fwd_id.vsi_list_id;

	if (fm_list_itr->vsi_count > 1) {
		status = ice_update_vsi_list_rule(hw, &vsi_id, 1, vsi_list_id,
						  true,
						  ice_aqc_opc_update_sw_rules,
						  lkup_type);
		if (status)
			return status;
		fm_list_itr->vsi_count--;
		is_last_elem = false;
		clear_bit(vsi_id, fm_list_itr->vsi_list_info->vsi_map);
	}

	/* For non-VLAN rules that forward packets to a VSI list, convert them
	 * to forwarding packets to a VSI if there is only one VSI left in the
	 * list.  Unused lists are then removed.
	 * VLAN rules need to use VSI lists even with only one VSI.
	 */
	if (fm_list_itr->fltr_info.fltr_act == ICE_FWD_TO_VSI_LIST) {
		if (lkup_type == ICE_SW_LKUP_VLAN) {
			del_list = is_last_elem;
		} else if (fm_list_itr->vsi_count == 1) {
			conv_list = true;
			del_list = true;
		}
	}

	if (del_list) {
		/* Remove the VSI list since it is no longer used */
		struct ice_vsi_list_map_info *vsi_list_info =
			fm_list_itr->vsi_list_info;

		status = ice_remove_vsi_list_rule(hw, vsi_list_id, lkup_type);
		if (status)
			return status;

		if (conv_list) {
			u16 rem_vsi_id;

			rem_vsi_id = find_first_bit(vsi_list_info->vsi_map,
						    ICE_MAX_VSI);

			/* Error out when the expected last element is not in
			 * the VSI list map
			 */
			if (rem_vsi_id == ICE_MAX_VSI)
				return ICE_ERR_OUT_OF_RANGE;

			/* Change the list entry action from VSI_LIST to VSI */
			fm_list_itr->fltr_info.fltr_act = ICE_FWD_TO_VSI;
			fm_list_itr->fltr_info.fwd_id.vsi_id = rem_vsi_id;
		}

		list_del(&vsi_list_info->list_entry);
		devm_kfree(ice_hw_to_dev(hw), vsi_list_info);
		fm_list_itr->vsi_list_info = NULL;
	}

	if (conv_list) {
		/* Convert the rule's forward action to forwarding packets to
		 * a VSI
		 */
		struct ice_aqc_sw_rules_elem *s_rule;

		s_rule = devm_kzalloc(ice_hw_to_dev(hw),
				      ICE_SW_RULE_RX_TX_ETH_HDR_SIZE,
				      GFP_KERNEL);
		if (!s_rule)
			return ICE_ERR_NO_MEMORY;

		ice_fill_sw_rule(hw, &fm_list_itr->fltr_info, s_rule,
				 ice_aqc_opc_update_sw_rules);

		s_rule->pdata.lkup_tx_rx.index =
			cpu_to_le16(fm_list_itr->fltr_info.fltr_rule_id);

		status = ice_aq_sw_rules(hw, s_rule,
					 ICE_SW_RULE_RX_TX_ETH_HDR_SIZE, 1,
					 ice_aqc_opc_update_sw_rules, NULL);
		devm_kfree(ice_hw_to_dev(hw), s_rule);
		if (status)
			return status;
	}

	if (is_last_elem) {
		/* Remove the lookup rule */
		struct ice_aqc_sw_rules_elem *s_rule;

		s_rule = devm_kzalloc(ice_hw_to_dev(hw),
				      ICE_SW_RULE_RX_TX_NO_HDR_SIZE,
				      GFP_KERNEL);
		if (!s_rule)
			return ICE_ERR_NO_MEMORY;

		ice_fill_sw_rule(hw, &fm_list_itr->fltr_info, s_rule,
				 ice_aqc_opc_remove_sw_rules);

		status = ice_aq_sw_rules(hw, s_rule,
					 ICE_SW_RULE_RX_TX_NO_HDR_SIZE, 1,
					 ice_aqc_opc_remove_sw_rules, NULL);
		if (status)
			return status;

		/* Remove a book keeping entry from the MAC address list */
		mutex_lock(&sw->mac_list_lock);
		list_del(&fm_list_itr->list_entry);
		mutex_unlock(&sw->mac_list_lock);
		devm_kfree(ice_hw_to_dev(hw), fm_list_itr);
		devm_kfree(ice_hw_to_dev(hw), s_rule);
	}
	return status;
}

/**
 * ice_remove_mac_entry
 * @hw: pointer to the hardware structure
 * @f_entry: structure containing MAC forwarding information
 */
static enum ice_status
ice_remove_mac_entry(struct ice_hw *hw, struct ice_fltr_list_entry *f_entry)
{
	struct ice_fltr_mgmt_list_entry *m_entry;
	u16 vsi_id;
	u8 *add;

	add = &f_entry->fltr_info.l_data.mac.mac_addr[0];

	m_entry = ice_find_mac_entry(hw, add);
	if (!m_entry)
		return ICE_ERR_PARAM;

	vsi_id = f_entry->fltr_info.fwd_id.vsi_id;
	return ice_handle_rem_vsi_list_mgmt(hw, vsi_id, m_entry);
}

/**
 * ice_remove_mac - remove a MAC address based filter rule
 * @hw: pointer to the hardware structure
 * @m_list: list of MAC addresses and forwarding information
 *
 * This function removes either a MAC filter rule or a specific VSI from a
 * VSI list for a multicast MAC address.
 *
 * Returns ICE_ERR_DOES_NOT_EXIST if a given entry was not added by
 * ice_add_mac. Caller should be aware that this call will only work if all
 * the entries passed into m_list were added previously. It will not attempt to
 * do a partial remove of entries that were found.
 */
enum ice_status
ice_remove_mac(struct ice_hw *hw, struct list_head *m_list)
{
	struct ice_aqc_sw_rules_elem *s_rule, *r_iter;
	u8 s_rule_size = ICE_SW_RULE_RX_TX_NO_HDR_SIZE;
	struct ice_switch_info *sw = hw->switch_info;
	struct ice_fltr_mgmt_list_entry *m_entry;
	struct ice_fltr_list_entry *m_list_itr;
	u16 elem_sent, total_elem_left;
	enum ice_status status = 0;
	u16 num_unicast = 0;

	if (!m_list)
		return ICE_ERR_PARAM;

	list_for_each_entry(m_list_itr, m_list, list_entry) {
		u8 *addr = m_list_itr->fltr_info.l_data.mac.mac_addr;

		if (is_unicast_ether_addr(addr) && !hw->ucast_shared)
			num_unicast++;
		else if (is_multicast_ether_addr(addr) ||
			 (is_unicast_ether_addr(addr) && hw->ucast_shared))
			ice_remove_mac_entry(hw, m_list_itr);
	}

	/* Exit if no unicast addresses found. Multicast switch rules
	 * were added individually
	 */
	if (!num_unicast)
		return 0;

	/* Allocate switch rule buffer for the bulk update for unicast */
	s_rule = devm_kcalloc(ice_hw_to_dev(hw), num_unicast, s_rule_size,
			      GFP_KERNEL);
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;

	r_iter = s_rule;
	list_for_each_entry(m_list_itr, m_list, list_entry) {
		u8 *addr = m_list_itr->fltr_info.l_data.mac.mac_addr;

		if (is_unicast_ether_addr(addr)) {
			m_entry = ice_find_mac_entry(hw, addr);
			if (!m_entry) {
				status = ICE_ERR_DOES_NOT_EXIST;
				goto ice_remove_mac_exit;
			}

			ice_fill_sw_rule(hw, &m_entry->fltr_info,
					 r_iter, ice_aqc_opc_remove_sw_rules);
			r_iter = (struct ice_aqc_sw_rules_elem *)
				((u8 *)r_iter + s_rule_size);
		}
	}

	/* Call AQ bulk switch rule update for all unicast addresses */
	r_iter = s_rule;
	/* Call AQ switch rule in AQ_MAX chunk */
	for (total_elem_left = num_unicast; total_elem_left > 0;
	     total_elem_left -= elem_sent) {
		struct ice_aqc_sw_rules_elem *entry = r_iter;

		elem_sent = min(total_elem_left,
				(u16)(ICE_AQ_MAX_BUF_LEN / s_rule_size));
		status = ice_aq_sw_rules(hw, entry, elem_sent * s_rule_size,
					 elem_sent, ice_aqc_opc_remove_sw_rules,
					 NULL);
		if (status)
			break;
		r_iter = (struct ice_aqc_sw_rules_elem *)
			((u8 *)r_iter + s_rule_size);
	}

	list_for_each_entry(m_list_itr, m_list, list_entry) {
		u8 *addr = m_list_itr->fltr_info.l_data.mac.mac_addr;

		if (is_unicast_ether_addr(addr)) {
			m_entry = ice_find_mac_entry(hw, addr);
			if (!m_entry)
				return ICE_ERR_OUT_OF_RANGE;
			mutex_lock(&sw->mac_list_lock);
			list_del(&m_entry->list_entry);
			mutex_unlock(&sw->mac_list_lock);
			devm_kfree(ice_hw_to_dev(hw), m_entry);
		}
	}

ice_remove_mac_exit:
	devm_kfree(ice_hw_to_dev(hw), s_rule);
	return status;
}

/**
 * ice_cfg_dflt_vsi - add filter rule to set/unset given VSI as default
 * VSI for the switch (represented by swid)
 * @hw: pointer to the hardware structure
 * @vsi_id: number of VSI to set as default
 * @set: true to add the above mentioned switch rule, false to remove it
 * @direction: ICE_FLTR_RX or ICE_FLTR_TX
 */
enum ice_status
ice_cfg_dflt_vsi(struct ice_hw *hw, u16 vsi_id, bool set, u8 direction)
{
	struct ice_aqc_sw_rules_elem *s_rule;
	struct ice_fltr_info f_info;
	enum ice_adminq_opc opcode;
	enum ice_status status;
	u16 s_rule_size;

	s_rule_size = set ? ICE_SW_RULE_RX_TX_ETH_HDR_SIZE :
			    ICE_SW_RULE_RX_TX_NO_HDR_SIZE;
	s_rule = devm_kzalloc(ice_hw_to_dev(hw), s_rule_size, GFP_KERNEL);
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;

	memset(&f_info, 0, sizeof(f_info));

	f_info.lkup_type = ICE_SW_LKUP_DFLT;
	f_info.flag = direction;
	f_info.fltr_act = ICE_FWD_TO_VSI;
	f_info.fwd_id.vsi_id = vsi_id;

	if (f_info.flag & ICE_FLTR_RX) {
		f_info.src = hw->port_info->lport;
		if (!set)
			f_info.fltr_rule_id =
				hw->port_info->dflt_rx_vsi_rule_id;
	} else if (f_info.flag & ICE_FLTR_TX) {
		f_info.src = vsi_id;
		if (!set)
			f_info.fltr_rule_id =
				hw->port_info->dflt_tx_vsi_rule_id;
	}

	if (set)
		opcode = ice_aqc_opc_add_sw_rules;
	else
		opcode = ice_aqc_opc_remove_sw_rules;

	ice_fill_sw_rule(hw, &f_info, s_rule, opcode);

	status = ice_aq_sw_rules(hw, s_rule, s_rule_size, 1, opcode, NULL);
	if (status || !(f_info.flag & ICE_FLTR_TX_RX))
		goto out;
	if (set) {
		u16 index = le16_to_cpu(s_rule->pdata.lkup_tx_rx.index);

		if (f_info.flag & ICE_FLTR_TX) {
			hw->port_info->dflt_tx_vsi_num = vsi_id;
			hw->port_info->dflt_tx_vsi_rule_id = index;
		} else if (f_info.flag & ICE_FLTR_RX) {
			hw->port_info->dflt_rx_vsi_num = vsi_id;
			hw->port_info->dflt_rx_vsi_rule_id = index;
		}
	} else {
		if (f_info.flag & ICE_FLTR_TX) {
			hw->port_info->dflt_tx_vsi_num = ICE_DFLT_VSI_INVAL;
			hw->port_info->dflt_tx_vsi_rule_id = ICE_INVAL_ACT;
		} else if (f_info.flag & ICE_FLTR_RX) {
			hw->port_info->dflt_rx_vsi_num = ICE_DFLT_VSI_INVAL;
			hw->port_info->dflt_rx_vsi_rule_id = ICE_INVAL_ACT;
		}
	}

out:
	devm_kfree(ice_hw_to_dev(hw), s_rule);
	return status;
}

/**
 * ice_remove_vlan_internal - Remove one VLAN based filter rule
 * @hw: pointer to the hardware structure
 * @f_entry: filter entry containing one VLAN information
 */
static enum ice_status
ice_remove_vlan_internal(struct ice_hw *hw,
			 struct ice_fltr_list_entry *f_entry)
{
	struct ice_fltr_info *new_fltr;
	struct ice_fltr_mgmt_list_entry *v_list_elem;
	u16 vsi_id;

	new_fltr = &f_entry->fltr_info;

	v_list_elem = ice_find_vlan_entry(hw, new_fltr->l_data.vlan.vlan_id);
	if (!v_list_elem)
		return ICE_ERR_PARAM;

	vsi_id = f_entry->fltr_info.fwd_id.vsi_id;
	return ice_handle_rem_vsi_list_mgmt(hw, vsi_id, v_list_elem);
}

/**
 * ice_remove_vlan - Remove VLAN based filter rule
 * @hw: pointer to the hardware structure
 * @v_list: list of VLAN entries and forwarding information
 */
enum ice_status
ice_remove_vlan(struct ice_hw *hw, struct list_head *v_list)
{
	struct ice_fltr_list_entry *v_list_itr;
	enum ice_status status = 0;

	if (!v_list || !hw)
		return ICE_ERR_PARAM;

	list_for_each_entry(v_list_itr, v_list, list_entry) {
		status = ice_remove_vlan_internal(hw, v_list_itr);
		if (status) {
			v_list_itr->status = ICE_FLTR_STATUS_FW_FAIL;
			return status;
		}
		v_list_itr->status = ICE_FLTR_STATUS_FW_SUCCESS;
	}
	return status;
}

/**
 * ice_add_to_vsi_fltr_list - Add VSI filters to the list
 * @hw: pointer to the hardware structure
 * @vsi_id: ID of VSI to remove filters from
 * @lkup_list_head: pointer to the list that has certain lookup type filters
 * @vsi_list_head: pointer to the list pertaining to VSI with vsi_id
 */
static enum ice_status
ice_add_to_vsi_fltr_list(struct ice_hw *hw, u16 vsi_id,
			 struct list_head *lkup_list_head,
			 struct list_head *vsi_list_head)
{
	struct ice_fltr_mgmt_list_entry *fm_entry;

	/* check to make sure VSI id is valid and within boundary */
	if (vsi_id >=
	    (sizeof(fm_entry->vsi_list_info->vsi_map) * BITS_PER_BYTE - 1))
		return ICE_ERR_PARAM;

	list_for_each_entry(fm_entry, lkup_list_head, list_entry) {
		struct ice_fltr_info *fi;

		fi = &fm_entry->fltr_info;
		if ((fi->fltr_act == ICE_FWD_TO_VSI &&
		     fi->fwd_id.vsi_id == vsi_id) ||
		    (fi->fltr_act == ICE_FWD_TO_VSI_LIST &&
		     (test_bit(vsi_id, fm_entry->vsi_list_info->vsi_map)))) {
			struct ice_fltr_list_entry *tmp;

			/* this memory is freed up in the caller function
			 * ice_remove_vsi_lkup_fltr() once filters for
			 * this VSI are removed
			 */
			tmp = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*tmp),
					   GFP_KERNEL);
			if (!tmp)
				return ICE_ERR_NO_MEMORY;

			memcpy(&tmp->fltr_info, fi, sizeof(*fi));

			/* Expected below fields to be set to ICE_FWD_TO_VSI and
			 * the particular VSI id since we are only removing this
			 * one VSI
			 */
			if (fi->fltr_act == ICE_FWD_TO_VSI_LIST) {
				tmp->fltr_info.fltr_act = ICE_FWD_TO_VSI;
				tmp->fltr_info.fwd_id.vsi_id = vsi_id;
			}

			list_add(&tmp->list_entry, vsi_list_head);
		}
	}
	return 0;
}

/**
 * ice_remove_vsi_lkup_fltr - Remove lookup type filters for a VSI
 * @hw: pointer to the hardware structure
 * @vsi_id: ID of VSI to remove filters from
 * @lkup: switch rule filter lookup type
 */
static void
ice_remove_vsi_lkup_fltr(struct ice_hw *hw, u16 vsi_id,
			 enum ice_sw_lkup_type lkup)
{
	struct ice_switch_info *sw = hw->switch_info;
	struct ice_fltr_list_entry *fm_entry;
	struct list_head remove_list_head;
	struct ice_fltr_list_entry *tmp;
	enum ice_status status;

	INIT_LIST_HEAD(&remove_list_head);
	switch (lkup) {
	case ICE_SW_LKUP_MAC:
		mutex_lock(&sw->mac_list_lock);
		status = ice_add_to_vsi_fltr_list(hw, vsi_id,
						  &sw->mac_list_head,
						  &remove_list_head);
		mutex_unlock(&sw->mac_list_lock);
		if (!status) {
			ice_remove_mac(hw, &remove_list_head);
			goto free_fltr_list;
		}
		break;
	case ICE_SW_LKUP_VLAN:
		mutex_lock(&sw->vlan_list_lock);
		status = ice_add_to_vsi_fltr_list(hw, vsi_id,
						  &sw->vlan_list_head,
						  &remove_list_head);
		mutex_unlock(&sw->vlan_list_lock);
		if (!status) {
			ice_remove_vlan(hw, &remove_list_head);
			goto free_fltr_list;
		}
		break;
	case ICE_SW_LKUP_MAC_VLAN:
	case ICE_SW_LKUP_ETHERTYPE:
	case ICE_SW_LKUP_ETHERTYPE_MAC:
	case ICE_SW_LKUP_PROMISC:
	case ICE_SW_LKUP_PROMISC_VLAN:
	case ICE_SW_LKUP_DFLT:
		ice_debug(hw, ICE_DBG_SW,
			  "Remove filters for this lookup type hasn't been implemented yet\n");
		break;
	}

	return;
free_fltr_list:
	list_for_each_entry_safe(fm_entry, tmp, &remove_list_head, list_entry) {
		list_del(&fm_entry->list_entry);
		devm_kfree(ice_hw_to_dev(hw), fm_entry);
	}
}

/**
 * ice_remove_vsi_fltr - Remove all filters for a VSI
 * @hw: pointer to the hardware structure
 * @vsi_id: ID of VSI to remove filters from
 */
void ice_remove_vsi_fltr(struct ice_hw *hw, u16 vsi_id)
{
	ice_remove_vsi_lkup_fltr(hw, vsi_id, ICE_SW_LKUP_MAC);
	ice_remove_vsi_lkup_fltr(hw, vsi_id, ICE_SW_LKUP_MAC_VLAN);
	ice_remove_vsi_lkup_fltr(hw, vsi_id, ICE_SW_LKUP_PROMISC);
	ice_remove_vsi_lkup_fltr(hw, vsi_id, ICE_SW_LKUP_VLAN);
	ice_remove_vsi_lkup_fltr(hw, vsi_id, ICE_SW_LKUP_DFLT);
	ice_remove_vsi_lkup_fltr(hw, vsi_id, ICE_SW_LKUP_ETHERTYPE);
	ice_remove_vsi_lkup_fltr(hw, vsi_id, ICE_SW_LKUP_ETHERTYPE_MAC);
	ice_remove_vsi_lkup_fltr(hw, vsi_id, ICE_SW_LKUP_PROMISC_VLAN);
}
