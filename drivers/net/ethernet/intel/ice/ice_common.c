// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice_common.h"
#include "ice_sched.h"
#include "ice_adminq_cmd.h"

#define ICE_PF_RESET_WAIT_COUNT	200

#define ICE_NIC_FLX_ENTRY(hw, mdid, idx) \
	wr32((hw), GLFLXP_RXDID_FLX_WRD_##idx(ICE_RXDID_FLEX_NIC), \
	     ((ICE_RX_OPC_MDID << \
	       GLFLXP_RXDID_FLX_WRD_##idx##_RXDID_OPCODE_S) & \
	      GLFLXP_RXDID_FLX_WRD_##idx##_RXDID_OPCODE_M) | \
	     (((mdid) << GLFLXP_RXDID_FLX_WRD_##idx##_PROT_MDID_S) & \
	      GLFLXP_RXDID_FLX_WRD_##idx##_PROT_MDID_M))

#define ICE_NIC_FLX_FLG_ENTRY(hw, flg_0, flg_1, flg_2, flg_3, idx) \
	wr32((hw), GLFLXP_RXDID_FLAGS(ICE_RXDID_FLEX_NIC, idx), \
	     (((flg_0) << GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_S) & \
	      GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_M) | \
	     (((flg_1) << GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_1_S) & \
	      GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_1_M) | \
	     (((flg_2) << GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_2_S) & \
	      GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_2_M) | \
	     (((flg_3) << GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_3_S) & \
	      GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_3_M))

/**
 * ice_set_mac_type - Sets MAC type
 * @hw: pointer to the HW structure
 *
 * This function sets the MAC type of the adapter based on the
 * vendor ID and device ID stored in the hw structure.
 */
static enum ice_status ice_set_mac_type(struct ice_hw *hw)
{
	if (hw->vendor_id != PCI_VENDOR_ID_INTEL)
		return ICE_ERR_DEVICE_NOT_SUPPORTED;

	hw->mac_type = ICE_MAC_GENERIC;
	return 0;
}

/**
 * ice_clear_pf_cfg - Clear PF configuration
 * @hw: pointer to the hardware structure
 *
 * Clears any existing PF configuration (VSIs, VSI lists, switch rules, port
 * configuration, flow director filters, etc.).
 */
enum ice_status ice_clear_pf_cfg(struct ice_hw *hw)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_clear_pf_cfg);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_aq_manage_mac_read - manage MAC address read command
 * @hw: pointer to the hw struct
 * @buf: a virtual buffer to hold the manage MAC read response
 * @buf_size: Size of the virtual buffer
 * @cd: pointer to command details structure or NULL
 *
 * This function is used to return per PF station MAC address (0x0107).
 * NOTE: Upon successful completion of this command, MAC address information
 * is returned in user specified buffer. Please interpret user specified
 * buffer as "manage_mac_read" response.
 * Response such as various MAC addresses are stored in HW struct (port.mac)
 * ice_aq_discover_caps is expected to be called before this function is called.
 */
static enum ice_status
ice_aq_manage_mac_read(struct ice_hw *hw, void *buf, u16 buf_size,
		       struct ice_sq_cd *cd)
{
	struct ice_aqc_manage_mac_read_resp *resp;
	struct ice_aqc_manage_mac_read *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;
	u16 flags;
	u8 i;

	cmd = &desc.params.mac_read;

	if (buf_size < sizeof(*resp))
		return ICE_ERR_BUF_TOO_SHORT;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_manage_mac_read);

	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (status)
		return status;

	resp = (struct ice_aqc_manage_mac_read_resp *)buf;
	flags = le16_to_cpu(cmd->flags) & ICE_AQC_MAN_MAC_READ_M;

	if (!(flags & ICE_AQC_MAN_MAC_LAN_ADDR_VALID)) {
		ice_debug(hw, ICE_DBG_LAN, "got invalid MAC address\n");
		return ICE_ERR_CFG;
	}

	/* A single port can report up to two (LAN and WoL) addresses */
	for (i = 0; i < cmd->num_addr; i++)
		if (resp[i].addr_type == ICE_AQC_MAN_MAC_ADDR_TYPE_LAN) {
			ether_addr_copy(hw->port_info->mac.lan_addr,
					resp[i].mac_addr);
			ether_addr_copy(hw->port_info->mac.perm_addr,
					resp[i].mac_addr);
			break;
		}

	return 0;
}

/**
 * ice_aq_get_phy_caps - returns PHY capabilities
 * @pi: port information structure
 * @qual_mods: report qualified modules
 * @report_mode: report mode capabilities
 * @pcaps: structure for PHY capabilities to be filled
 * @cd: pointer to command details structure or NULL
 *
 * Returns the various PHY capabilities supported on the Port (0x0600)
 */
static enum ice_status
ice_aq_get_phy_caps(struct ice_port_info *pi, bool qual_mods, u8 report_mode,
		    struct ice_aqc_get_phy_caps_data *pcaps,
		    struct ice_sq_cd *cd)
{
	struct ice_aqc_get_phy_caps *cmd;
	u16 pcaps_size = sizeof(*pcaps);
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.get_phy;

	if (!pcaps || (report_mode & ~ICE_AQC_REPORT_MODE_M) || !pi)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_phy_caps);

	if (qual_mods)
		cmd->param0 |= cpu_to_le16(ICE_AQC_GET_PHY_RQM);

	cmd->param0 |= cpu_to_le16(report_mode);
	status = ice_aq_send_cmd(pi->hw, &desc, pcaps, pcaps_size, cd);

	if (!status && report_mode == ICE_AQC_REPORT_TOPO_CAP)
		pi->phy.phy_type_low = le64_to_cpu(pcaps->phy_type_low);

	return status;
}

/**
 * ice_get_media_type - Gets media type
 * @pi: port information structure
 */
static enum ice_media_type ice_get_media_type(struct ice_port_info *pi)
{
	struct ice_link_status *hw_link_info;

	if (!pi)
		return ICE_MEDIA_UNKNOWN;

	hw_link_info = &pi->phy.link_info;

	if (hw_link_info->phy_type_low) {
		switch (hw_link_info->phy_type_low) {
		case ICE_PHY_TYPE_LOW_1000BASE_SX:
		case ICE_PHY_TYPE_LOW_1000BASE_LX:
		case ICE_PHY_TYPE_LOW_10GBASE_SR:
		case ICE_PHY_TYPE_LOW_10GBASE_LR:
		case ICE_PHY_TYPE_LOW_10G_SFI_C2C:
		case ICE_PHY_TYPE_LOW_25GBASE_SR:
		case ICE_PHY_TYPE_LOW_25GBASE_LR:
		case ICE_PHY_TYPE_LOW_25G_AUI_C2C:
		case ICE_PHY_TYPE_LOW_40GBASE_SR4:
		case ICE_PHY_TYPE_LOW_40GBASE_LR4:
			return ICE_MEDIA_FIBER;
		case ICE_PHY_TYPE_LOW_100BASE_TX:
		case ICE_PHY_TYPE_LOW_1000BASE_T:
		case ICE_PHY_TYPE_LOW_2500BASE_T:
		case ICE_PHY_TYPE_LOW_5GBASE_T:
		case ICE_PHY_TYPE_LOW_10GBASE_T:
		case ICE_PHY_TYPE_LOW_25GBASE_T:
			return ICE_MEDIA_BASET;
		case ICE_PHY_TYPE_LOW_10G_SFI_DA:
		case ICE_PHY_TYPE_LOW_25GBASE_CR:
		case ICE_PHY_TYPE_LOW_25GBASE_CR_S:
		case ICE_PHY_TYPE_LOW_25GBASE_CR1:
		case ICE_PHY_TYPE_LOW_40GBASE_CR4:
			return ICE_MEDIA_DA;
		case ICE_PHY_TYPE_LOW_1000BASE_KX:
		case ICE_PHY_TYPE_LOW_2500BASE_KX:
		case ICE_PHY_TYPE_LOW_2500BASE_X:
		case ICE_PHY_TYPE_LOW_5GBASE_KR:
		case ICE_PHY_TYPE_LOW_10GBASE_KR_CR1:
		case ICE_PHY_TYPE_LOW_25GBASE_KR:
		case ICE_PHY_TYPE_LOW_25GBASE_KR1:
		case ICE_PHY_TYPE_LOW_25GBASE_KR_S:
		case ICE_PHY_TYPE_LOW_40GBASE_KR4:
			return ICE_MEDIA_BACKPLANE;
		}
	}

	return ICE_MEDIA_UNKNOWN;
}

/**
 * ice_aq_get_link_info
 * @pi: port information structure
 * @ena_lse: enable/disable LinkStatusEvent reporting
 * @link: pointer to link status structure - optional
 * @cd: pointer to command details structure or NULL
 *
 * Get Link Status (0x607). Returns the link status of the adapter.
 */
enum ice_status
ice_aq_get_link_info(struct ice_port_info *pi, bool ena_lse,
		     struct ice_link_status *link, struct ice_sq_cd *cd)
{
	struct ice_link_status *hw_link_info_old, *hw_link_info;
	struct ice_aqc_get_link_status_data link_data = { 0 };
	struct ice_aqc_get_link_status *resp;
	enum ice_media_type *hw_media_type;
	struct ice_fc_info *hw_fc_info;
	bool tx_pause, rx_pause;
	struct ice_aq_desc desc;
	enum ice_status status;
	u16 cmd_flags;

	if (!pi)
		return ICE_ERR_PARAM;
	hw_link_info_old = &pi->phy.link_info_old;
	hw_media_type = &pi->phy.media_type;
	hw_link_info = &pi->phy.link_info;
	hw_fc_info = &pi->fc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_link_status);
	cmd_flags = (ena_lse) ? ICE_AQ_LSE_ENA : ICE_AQ_LSE_DIS;
	resp = &desc.params.get_link_status;
	resp->cmd_flags = cpu_to_le16(cmd_flags);
	resp->lport_num = pi->lport;

	status = ice_aq_send_cmd(pi->hw, &desc, &link_data, sizeof(link_data),
				 cd);

	if (status)
		return status;

	/* save off old link status information */
	*hw_link_info_old = *hw_link_info;

	/* update current link status information */
	hw_link_info->link_speed = le16_to_cpu(link_data.link_speed);
	hw_link_info->phy_type_low = le64_to_cpu(link_data.phy_type_low);
	*hw_media_type = ice_get_media_type(pi);
	hw_link_info->link_info = link_data.link_info;
	hw_link_info->an_info = link_data.an_info;
	hw_link_info->ext_info = link_data.ext_info;
	hw_link_info->max_frame_size = le16_to_cpu(link_data.max_frame_size);
	hw_link_info->pacing = link_data.cfg & ICE_AQ_CFG_PACING_M;

	/* update fc info */
	tx_pause = !!(link_data.an_info & ICE_AQ_LINK_PAUSE_TX);
	rx_pause = !!(link_data.an_info & ICE_AQ_LINK_PAUSE_RX);
	if (tx_pause && rx_pause)
		hw_fc_info->current_mode = ICE_FC_FULL;
	else if (tx_pause)
		hw_fc_info->current_mode = ICE_FC_TX_PAUSE;
	else if (rx_pause)
		hw_fc_info->current_mode = ICE_FC_RX_PAUSE;
	else
		hw_fc_info->current_mode = ICE_FC_NONE;

	hw_link_info->lse_ena =
		!!(resp->cmd_flags & cpu_to_le16(ICE_AQ_LSE_IS_ENABLED));

	/* save link status information */
	if (link)
		*link = *hw_link_info;

	/* flag cleared so calling functions don't call AQ again */
	pi->phy.get_link_info = false;

	return status;
}

/**
 * ice_init_flex_parser - initialize rx flex parser
 * @hw: pointer to the hardware structure
 *
 * Function to initialize flex descriptors
 */
static void ice_init_flex_parser(struct ice_hw *hw)
{
	u8 idx = 0;

	ICE_NIC_FLX_ENTRY(hw, ICE_RX_MDID_HASH_LOW, 0);
	ICE_NIC_FLX_ENTRY(hw, ICE_RX_MDID_HASH_HIGH, 1);
	ICE_NIC_FLX_ENTRY(hw, ICE_RX_MDID_FLOW_ID_LOWER, 2);
	ICE_NIC_FLX_ENTRY(hw, ICE_RX_MDID_FLOW_ID_HIGH, 3);
	ICE_NIC_FLX_FLG_ENTRY(hw, ICE_RXFLG_PKT_FRG, ICE_RXFLG_UDP_GRE,
			      ICE_RXFLG_PKT_DSI, ICE_RXFLG_FIN, idx++);
	ICE_NIC_FLX_FLG_ENTRY(hw, ICE_RXFLG_SYN, ICE_RXFLG_RST,
			      ICE_RXFLG_PKT_DSI, ICE_RXFLG_PKT_DSI, idx++);
	ICE_NIC_FLX_FLG_ENTRY(hw, ICE_RXFLG_PKT_DSI, ICE_RXFLG_PKT_DSI,
			      ICE_RXFLG_EVLAN_x8100, ICE_RXFLG_EVLAN_x9100,
			      idx++);
	ICE_NIC_FLX_FLG_ENTRY(hw, ICE_RXFLG_VLAN_x8100, ICE_RXFLG_TNL_VLAN,
			      ICE_RXFLG_TNL_MAC, ICE_RXFLG_TNL0, idx++);
	ICE_NIC_FLX_FLG_ENTRY(hw, ICE_RXFLG_TNL1, ICE_RXFLG_TNL2,
			      ICE_RXFLG_PKT_DSI, ICE_RXFLG_PKT_DSI, idx);
}

/**
 * ice_init_fltr_mgmt_struct - initializes filter management list and locks
 * @hw: pointer to the hw struct
 */
static enum ice_status ice_init_fltr_mgmt_struct(struct ice_hw *hw)
{
	struct ice_switch_info *sw;

	hw->switch_info = devm_kzalloc(ice_hw_to_dev(hw),
				       sizeof(*hw->switch_info), GFP_KERNEL);
	sw = hw->switch_info;

	if (!sw)
		return ICE_ERR_NO_MEMORY;

	INIT_LIST_HEAD(&sw->vsi_list_map_head);

	mutex_init(&sw->mac_list_lock);
	INIT_LIST_HEAD(&sw->mac_list_head);

	mutex_init(&sw->vlan_list_lock);
	INIT_LIST_HEAD(&sw->vlan_list_head);

	mutex_init(&sw->eth_m_list_lock);
	INIT_LIST_HEAD(&sw->eth_m_list_head);

	mutex_init(&sw->promisc_list_lock);
	INIT_LIST_HEAD(&sw->promisc_list_head);

	mutex_init(&sw->mac_vlan_list_lock);
	INIT_LIST_HEAD(&sw->mac_vlan_list_head);

	return 0;
}

/**
 * ice_cleanup_fltr_mgmt_struct - cleanup filter management list and locks
 * @hw: pointer to the hw struct
 */
static void ice_cleanup_fltr_mgmt_struct(struct ice_hw *hw)
{
	struct ice_switch_info *sw = hw->switch_info;
	struct ice_vsi_list_map_info *v_pos_map;
	struct ice_vsi_list_map_info *v_tmp_map;

	list_for_each_entry_safe(v_pos_map, v_tmp_map, &sw->vsi_list_map_head,
				 list_entry) {
		list_del(&v_pos_map->list_entry);
		devm_kfree(ice_hw_to_dev(hw), v_pos_map);
	}

	mutex_destroy(&sw->mac_list_lock);
	mutex_destroy(&sw->vlan_list_lock);
	mutex_destroy(&sw->eth_m_list_lock);
	mutex_destroy(&sw->promisc_list_lock);
	mutex_destroy(&sw->mac_vlan_list_lock);

	devm_kfree(ice_hw_to_dev(hw), sw);
}

/**
 * ice_init_hw - main hardware initialization routine
 * @hw: pointer to the hardware structure
 */
enum ice_status ice_init_hw(struct ice_hw *hw)
{
	struct ice_aqc_get_phy_caps_data *pcaps;
	enum ice_status status;
	u16 mac_buf_len;
	void *mac_buf;

	/* Set MAC type based on DeviceID */
	status = ice_set_mac_type(hw);
	if (status)
		return status;

	hw->pf_id = (u8)(rd32(hw, PF_FUNC_RID) &
			 PF_FUNC_RID_FUNC_NUM_M) >>
		PF_FUNC_RID_FUNC_NUM_S;

	status = ice_reset(hw, ICE_RESET_PFR);
	if (status)
		return status;

	/* set these values to minimum allowed */
	hw->itr_gran_200 = ICE_ITR_GRAN_MIN_200;
	hw->itr_gran_100 = ICE_ITR_GRAN_MIN_100;
	hw->itr_gran_50 = ICE_ITR_GRAN_MIN_50;
	hw->itr_gran_25 = ICE_ITR_GRAN_MIN_25;

	status = ice_init_all_ctrlq(hw);
	if (status)
		goto err_unroll_cqinit;

	status = ice_clear_pf_cfg(hw);
	if (status)
		goto err_unroll_cqinit;

	ice_clear_pxe_mode(hw);

	status = ice_init_nvm(hw);
	if (status)
		goto err_unroll_cqinit;

	status = ice_get_caps(hw);
	if (status)
		goto err_unroll_cqinit;

	hw->port_info = devm_kzalloc(ice_hw_to_dev(hw),
				     sizeof(*hw->port_info), GFP_KERNEL);
	if (!hw->port_info) {
		status = ICE_ERR_NO_MEMORY;
		goto err_unroll_cqinit;
	}

	/* set the back pointer to hw */
	hw->port_info->hw = hw;

	/* Initialize port_info struct with switch configuration data */
	status = ice_get_initial_sw_cfg(hw);
	if (status)
		goto err_unroll_alloc;

	hw->evb_veb = true;

	/* Query the allocated resources for tx scheduler */
	status = ice_sched_query_res_alloc(hw);
	if (status) {
		ice_debug(hw, ICE_DBG_SCHED,
			  "Failed to get scheduler allocated resources\n");
		goto err_unroll_alloc;
	}

	/* Initialize port_info struct with scheduler data */
	status = ice_sched_init_port(hw->port_info);
	if (status)
		goto err_unroll_sched;

	pcaps = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*pcaps), GFP_KERNEL);
	if (!pcaps) {
		status = ICE_ERR_NO_MEMORY;
		goto err_unroll_sched;
	}

	/* Initialize port_info struct with PHY capabilities */
	status = ice_aq_get_phy_caps(hw->port_info, false,
				     ICE_AQC_REPORT_TOPO_CAP, pcaps, NULL);
	devm_kfree(ice_hw_to_dev(hw), pcaps);
	if (status)
		goto err_unroll_sched;

	/* Initialize port_info struct with link information */
	status = ice_aq_get_link_info(hw->port_info, false, NULL, NULL);
	if (status)
		goto err_unroll_sched;

	status = ice_init_fltr_mgmt_struct(hw);
	if (status)
		goto err_unroll_sched;

	/* Get MAC information */
	/* A single port can report up to two (LAN and WoL) addresses */
	mac_buf = devm_kcalloc(ice_hw_to_dev(hw), 2,
			       sizeof(struct ice_aqc_manage_mac_read_resp),
			       GFP_KERNEL);
	mac_buf_len = 2 * sizeof(struct ice_aqc_manage_mac_read_resp);

	if (!mac_buf) {
		status = ICE_ERR_NO_MEMORY;
		goto err_unroll_fltr_mgmt_struct;
	}

	status = ice_aq_manage_mac_read(hw, mac_buf, mac_buf_len, NULL);
	devm_kfree(ice_hw_to_dev(hw), mac_buf);

	if (status)
		goto err_unroll_fltr_mgmt_struct;

	ice_init_flex_parser(hw);

	return 0;

err_unroll_fltr_mgmt_struct:
	ice_cleanup_fltr_mgmt_struct(hw);
err_unroll_sched:
	ice_sched_cleanup_all(hw);
err_unroll_alloc:
	devm_kfree(ice_hw_to_dev(hw), hw->port_info);
err_unroll_cqinit:
	ice_shutdown_all_ctrlq(hw);
	return status;
}

/**
 * ice_deinit_hw - unroll initialization operations done by ice_init_hw
 * @hw: pointer to the hardware structure
 */
void ice_deinit_hw(struct ice_hw *hw)
{
	ice_sched_cleanup_all(hw);
	ice_shutdown_all_ctrlq(hw);

	if (hw->port_info) {
		devm_kfree(ice_hw_to_dev(hw), hw->port_info);
		hw->port_info = NULL;
	}

	ice_cleanup_fltr_mgmt_struct(hw);
}

/**
 * ice_check_reset - Check to see if a global reset is complete
 * @hw: pointer to the hardware structure
 */
enum ice_status ice_check_reset(struct ice_hw *hw)
{
	u32 cnt, reg = 0, grst_delay;

	/* Poll for Device Active state in case a recent CORER, GLOBR,
	 * or EMPR has occurred. The grst delay value is in 100ms units.
	 * Add 1sec for outstanding AQ commands that can take a long time.
	 */
	grst_delay = ((rd32(hw, GLGEN_RSTCTL) & GLGEN_RSTCTL_GRSTDEL_M) >>
		      GLGEN_RSTCTL_GRSTDEL_S) + 10;

	for (cnt = 0; cnt < grst_delay; cnt++) {
		mdelay(100);
		reg = rd32(hw, GLGEN_RSTAT);
		if (!(reg & GLGEN_RSTAT_DEVSTATE_M))
			break;
	}

	if (cnt == grst_delay) {
		ice_debug(hw, ICE_DBG_INIT,
			  "Global reset polling failed to complete.\n");
		return ICE_ERR_RESET_FAILED;
	}

#define ICE_RESET_DONE_MASK	(GLNVM_ULD_CORER_DONE_M | \
				 GLNVM_ULD_GLOBR_DONE_M)

	/* Device is Active; check Global Reset processes are done */
	for (cnt = 0; cnt < ICE_PF_RESET_WAIT_COUNT; cnt++) {
		reg = rd32(hw, GLNVM_ULD) & ICE_RESET_DONE_MASK;
		if (reg == ICE_RESET_DONE_MASK) {
			ice_debug(hw, ICE_DBG_INIT,
				  "Global reset processes done. %d\n", cnt);
			break;
		}
		mdelay(10);
	}

	if (cnt == ICE_PF_RESET_WAIT_COUNT) {
		ice_debug(hw, ICE_DBG_INIT,
			  "Wait for Reset Done timed out. GLNVM_ULD = 0x%x\n",
			  reg);
		return ICE_ERR_RESET_FAILED;
	}

	return 0;
}

/**
 * ice_pf_reset - Reset the PF
 * @hw: pointer to the hardware structure
 *
 * If a global reset has been triggered, this function checks
 * for its completion and then issues the PF reset
 */
static enum ice_status ice_pf_reset(struct ice_hw *hw)
{
	u32 cnt, reg;

	/* If at function entry a global reset was already in progress, i.e.
	 * state is not 'device active' or any of the reset done bits are not
	 * set in GLNVM_ULD, there is no need for a PF Reset; poll until the
	 * global reset is done.
	 */
	if ((rd32(hw, GLGEN_RSTAT) & GLGEN_RSTAT_DEVSTATE_M) ||
	    (rd32(hw, GLNVM_ULD) & ICE_RESET_DONE_MASK) ^ ICE_RESET_DONE_MASK) {
		/* poll on global reset currently in progress until done */
		if (ice_check_reset(hw))
			return ICE_ERR_RESET_FAILED;

		return 0;
	}

	/* Reset the PF */
	reg = rd32(hw, PFGEN_CTRL);

	wr32(hw, PFGEN_CTRL, (reg | PFGEN_CTRL_PFSWR_M));

	for (cnt = 0; cnt < ICE_PF_RESET_WAIT_COUNT; cnt++) {
		reg = rd32(hw, PFGEN_CTRL);
		if (!(reg & PFGEN_CTRL_PFSWR_M))
			break;

		mdelay(1);
	}

	if (cnt == ICE_PF_RESET_WAIT_COUNT) {
		ice_debug(hw, ICE_DBG_INIT,
			  "PF reset polling failed to complete.\n");
		return ICE_ERR_RESET_FAILED;
	}

	return 0;
}

/**
 * ice_reset - Perform different types of reset
 * @hw: pointer to the hardware structure
 * @req: reset request
 *
 * This function triggers a reset as specified by the req parameter.
 *
 * Note:
 * If anything other than a PF reset is triggered, PXE mode is restored.
 * This has to be cleared using ice_clear_pxe_mode again, once the AQ
 * interface has been restored in the rebuild flow.
 */
enum ice_status ice_reset(struct ice_hw *hw, enum ice_reset_req req)
{
	u32 val = 0;

	switch (req) {
	case ICE_RESET_PFR:
		return ice_pf_reset(hw);
	case ICE_RESET_CORER:
		ice_debug(hw, ICE_DBG_INIT, "CoreR requested\n");
		val = GLGEN_RTRIG_CORER_M;
		break;
	case ICE_RESET_GLOBR:
		ice_debug(hw, ICE_DBG_INIT, "GlobalR requested\n");
		val = GLGEN_RTRIG_GLOBR_M;
		break;
	}

	val |= rd32(hw, GLGEN_RTRIG);
	wr32(hw, GLGEN_RTRIG, val);
	ice_flush(hw);

	/* wait for the FW to be ready */
	return ice_check_reset(hw);
}

/**
 * ice_copy_rxq_ctx_to_hw
 * @hw: pointer to the hardware structure
 * @ice_rxq_ctx: pointer to the rxq context
 * @rxq_index: the index of the rx queue
 *
 * Copies rxq context from dense structure to hw register space
 */
static enum ice_status
ice_copy_rxq_ctx_to_hw(struct ice_hw *hw, u8 *ice_rxq_ctx, u32 rxq_index)
{
	u8 i;

	if (!ice_rxq_ctx)
		return ICE_ERR_BAD_PTR;

	if (rxq_index > QRX_CTRL_MAX_INDEX)
		return ICE_ERR_PARAM;

	/* Copy each dword separately to hw */
	for (i = 0; i < ICE_RXQ_CTX_SIZE_DWORDS; i++) {
		wr32(hw, QRX_CONTEXT(i, rxq_index),
		     *((u32 *)(ice_rxq_ctx + (i * sizeof(u32)))));

		ice_debug(hw, ICE_DBG_QCTX, "qrxdata[%d]: %08X\n", i,
			  *((u32 *)(ice_rxq_ctx + (i * sizeof(u32)))));
	}

	return 0;
}

/* LAN Rx Queue Context */
static const struct ice_ctx_ele ice_rlan_ctx_info[] = {
	/* Field		Width	LSB */
	ICE_CTX_STORE(ice_rlan_ctx, head,		13,	0),
	ICE_CTX_STORE(ice_rlan_ctx, cpuid,		8,	13),
	ICE_CTX_STORE(ice_rlan_ctx, base,		57,	32),
	ICE_CTX_STORE(ice_rlan_ctx, qlen,		13,	89),
	ICE_CTX_STORE(ice_rlan_ctx, dbuf,		7,	102),
	ICE_CTX_STORE(ice_rlan_ctx, hbuf,		5,	109),
	ICE_CTX_STORE(ice_rlan_ctx, dtype,		2,	114),
	ICE_CTX_STORE(ice_rlan_ctx, dsize,		1,	116),
	ICE_CTX_STORE(ice_rlan_ctx, crcstrip,		1,	117),
	ICE_CTX_STORE(ice_rlan_ctx, l2tsel,		1,	119),
	ICE_CTX_STORE(ice_rlan_ctx, hsplit_0,		4,	120),
	ICE_CTX_STORE(ice_rlan_ctx, hsplit_1,		2,	124),
	ICE_CTX_STORE(ice_rlan_ctx, showiv,		1,	127),
	ICE_CTX_STORE(ice_rlan_ctx, rxmax,		14,	174),
	ICE_CTX_STORE(ice_rlan_ctx, tphrdesc_ena,	1,	193),
	ICE_CTX_STORE(ice_rlan_ctx, tphwdesc_ena,	1,	194),
	ICE_CTX_STORE(ice_rlan_ctx, tphdata_ena,	1,	195),
	ICE_CTX_STORE(ice_rlan_ctx, tphhead_ena,	1,	196),
	ICE_CTX_STORE(ice_rlan_ctx, lrxqthresh,		3,	198),
	{ 0 }
};

/**
 * ice_write_rxq_ctx
 * @hw: pointer to the hardware structure
 * @rlan_ctx: pointer to the rxq context
 * @rxq_index: the index of the rx queue
 *
 * Converts rxq context from sparse to dense structure and then writes
 * it to hw register space
 */
enum ice_status
ice_write_rxq_ctx(struct ice_hw *hw, struct ice_rlan_ctx *rlan_ctx,
		  u32 rxq_index)
{
	u8 ctx_buf[ICE_RXQ_CTX_SZ] = { 0 };

	ice_set_ctx((u8 *)rlan_ctx, ctx_buf, ice_rlan_ctx_info);
	return ice_copy_rxq_ctx_to_hw(hw, ctx_buf, rxq_index);
}

/* LAN Tx Queue Context */
const struct ice_ctx_ele ice_tlan_ctx_info[] = {
				    /* Field			Width	LSB */
	ICE_CTX_STORE(ice_tlan_ctx, base,			57,	0),
	ICE_CTX_STORE(ice_tlan_ctx, port_num,			3,	57),
	ICE_CTX_STORE(ice_tlan_ctx, cgd_num,			5,	60),
	ICE_CTX_STORE(ice_tlan_ctx, pf_num,			3,	65),
	ICE_CTX_STORE(ice_tlan_ctx, vmvf_num,			10,	68),
	ICE_CTX_STORE(ice_tlan_ctx, vmvf_type,			2,	78),
	ICE_CTX_STORE(ice_tlan_ctx, src_vsi,			10,	80),
	ICE_CTX_STORE(ice_tlan_ctx, tsyn_ena,			1,	90),
	ICE_CTX_STORE(ice_tlan_ctx, alt_vlan,			1,	92),
	ICE_CTX_STORE(ice_tlan_ctx, cpuid,			8,	93),
	ICE_CTX_STORE(ice_tlan_ctx, wb_mode,			1,	101),
	ICE_CTX_STORE(ice_tlan_ctx, tphrd_desc,			1,	102),
	ICE_CTX_STORE(ice_tlan_ctx, tphrd,			1,	103),
	ICE_CTX_STORE(ice_tlan_ctx, tphwr_desc,			1,	104),
	ICE_CTX_STORE(ice_tlan_ctx, cmpq_id,			9,	105),
	ICE_CTX_STORE(ice_tlan_ctx, qnum_in_func,		14,	114),
	ICE_CTX_STORE(ice_tlan_ctx, itr_notification_mode,	1,	128),
	ICE_CTX_STORE(ice_tlan_ctx, adjust_prof_id,		6,	129),
	ICE_CTX_STORE(ice_tlan_ctx, qlen,			13,	135),
	ICE_CTX_STORE(ice_tlan_ctx, quanta_prof_idx,		4,	148),
	ICE_CTX_STORE(ice_tlan_ctx, tso_ena,			1,	152),
	ICE_CTX_STORE(ice_tlan_ctx, tso_qnum,			11,	153),
	ICE_CTX_STORE(ice_tlan_ctx, legacy_int,			1,	164),
	ICE_CTX_STORE(ice_tlan_ctx, drop_ena,			1,	165),
	ICE_CTX_STORE(ice_tlan_ctx, cache_prof_idx,		2,	166),
	ICE_CTX_STORE(ice_tlan_ctx, pkt_shaper_prof_idx,	3,	168),
	ICE_CTX_STORE(ice_tlan_ctx, int_q_state,		110,	171),
	{ 0 }
};

/**
 * ice_debug_cq
 * @hw: pointer to the hardware structure
 * @mask: debug mask
 * @desc: pointer to control queue descriptor
 * @buf: pointer to command buffer
 * @buf_len: max length of buf
 *
 * Dumps debug log about control command with descriptor contents.
 */
void ice_debug_cq(struct ice_hw *hw, u32 __maybe_unused mask, void *desc,
		  void *buf, u16 buf_len)
{
	struct ice_aq_desc *cq_desc = (struct ice_aq_desc *)desc;
	u16 len;

#ifndef CONFIG_DYNAMIC_DEBUG
	if (!(mask & hw->debug_mask))
		return;
#endif

	if (!desc)
		return;

	len = le16_to_cpu(cq_desc->datalen);

	ice_debug(hw, mask,
		  "CQ CMD: opcode 0x%04X, flags 0x%04X, datalen 0x%04X, retval 0x%04X\n",
		  le16_to_cpu(cq_desc->opcode),
		  le16_to_cpu(cq_desc->flags),
		  le16_to_cpu(cq_desc->datalen), le16_to_cpu(cq_desc->retval));
	ice_debug(hw, mask, "\tcookie (h,l) 0x%08X 0x%08X\n",
		  le32_to_cpu(cq_desc->cookie_high),
		  le32_to_cpu(cq_desc->cookie_low));
	ice_debug(hw, mask, "\tparam (0,1)  0x%08X 0x%08X\n",
		  le32_to_cpu(cq_desc->params.generic.param0),
		  le32_to_cpu(cq_desc->params.generic.param1));
	ice_debug(hw, mask, "\taddr (h,l)   0x%08X 0x%08X\n",
		  le32_to_cpu(cq_desc->params.generic.addr_high),
		  le32_to_cpu(cq_desc->params.generic.addr_low));
	if (buf && cq_desc->datalen != 0) {
		ice_debug(hw, mask, "Buffer:\n");
		if (buf_len < len)
			len = buf_len;

		ice_debug_array(hw, mask, 16, 1, (u8 *)buf, len);
	}
}

/* FW Admin Queue command wrappers */

/**
 * ice_aq_send_cmd - send FW Admin Queue command to FW Admin Queue
 * @hw: pointer to the hw struct
 * @desc: descriptor describing the command
 * @buf: buffer to use for indirect commands (NULL for direct commands)
 * @buf_size: size of buffer for indirect commands (0 for direct commands)
 * @cd: pointer to command details structure
 *
 * Helper function to send FW Admin Queue commands to the FW Admin Queue.
 */
enum ice_status
ice_aq_send_cmd(struct ice_hw *hw, struct ice_aq_desc *desc, void *buf,
		u16 buf_size, struct ice_sq_cd *cd)
{
	return ice_sq_send_cmd(hw, &hw->adminq, desc, buf, buf_size, cd);
}

/**
 * ice_aq_get_fw_ver
 * @hw: pointer to the hw struct
 * @cd: pointer to command details structure or NULL
 *
 * Get the firmware version (0x0001) from the admin queue commands
 */
enum ice_status ice_aq_get_fw_ver(struct ice_hw *hw, struct ice_sq_cd *cd)
{
	struct ice_aqc_get_ver *resp;
	struct ice_aq_desc desc;
	enum ice_status status;

	resp = &desc.params.get_ver;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_ver);

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);

	if (!status) {
		hw->fw_branch = resp->fw_branch;
		hw->fw_maj_ver = resp->fw_major;
		hw->fw_min_ver = resp->fw_minor;
		hw->fw_patch = resp->fw_patch;
		hw->fw_build = le32_to_cpu(resp->fw_build);
		hw->api_branch = resp->api_branch;
		hw->api_maj_ver = resp->api_major;
		hw->api_min_ver = resp->api_minor;
		hw->api_patch = resp->api_patch;
	}

	return status;
}

/**
 * ice_aq_q_shutdown
 * @hw: pointer to the hw struct
 * @unloading: is the driver unloading itself
 *
 * Tell the Firmware that we're shutting down the AdminQ and whether
 * or not the driver is unloading as well (0x0003).
 */
enum ice_status ice_aq_q_shutdown(struct ice_hw *hw, bool unloading)
{
	struct ice_aqc_q_shutdown *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.q_shutdown;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_q_shutdown);

	if (unloading)
		cmd->driver_unloading = cpu_to_le32(ICE_AQC_DRIVER_UNLOADING);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_aq_req_res
 * @hw: pointer to the hw struct
 * @res: resource id
 * @access: access type
 * @sdp_number: resource number
 * @timeout: the maximum time in ms that the driver may hold the resource
 * @cd: pointer to command details structure or NULL
 *
 * Requests common resource using the admin queue commands (0x0008).
 * When attempting to acquire the Global Config Lock, the driver can
 * learn of three states:
 *  1) ICE_SUCCESS -        acquired lock, and can perform download package
 *  2) ICE_ERR_AQ_ERROR -   did not get lock, driver should fail to load
 *  3) ICE_ERR_AQ_NO_WORK - did not get lock, but another driver has
 *                          successfully downloaded the package; the driver does
 *                          not have to download the package and can continue
 *                          loading
 *
 * Note that if the caller is in an acquire lock, perform action, release lock
 * phase of operation, it is possible that the FW may detect a timeout and issue
 * a CORER. In this case, the driver will receive a CORER interrupt and will
 * have to determine its cause. The calling thread that is handling this flow
 * will likely get an error propagated back to it indicating the Download
 * Package, Update Package or the Release Resource AQ commands timed out.
 */
static enum ice_status
ice_aq_req_res(struct ice_hw *hw, enum ice_aq_res_ids res,
	       enum ice_aq_res_access_type access, u8 sdp_number, u32 *timeout,
	       struct ice_sq_cd *cd)
{
	struct ice_aqc_req_res *cmd_resp;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd_resp = &desc.params.res_owner;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_req_res);

	cmd_resp->res_id = cpu_to_le16(res);
	cmd_resp->access_type = cpu_to_le16(access);
	cmd_resp->res_number = cpu_to_le32(sdp_number);
	cmd_resp->timeout = cpu_to_le32(*timeout);
	*timeout = 0;

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);

	/* The completion specifies the maximum time in ms that the driver
	 * may hold the resource in the Timeout field.
	 */

	/* Global config lock response utilizes an additional status field.
	 *
	 * If the Global config lock resource is held by some other driver, the
	 * command completes with ICE_AQ_RES_GLBL_IN_PROG in the status field
	 * and the timeout field indicates the maximum time the current owner
	 * of the resource has to free it.
	 */
	if (res == ICE_GLOBAL_CFG_LOCK_RES_ID) {
		if (le16_to_cpu(cmd_resp->status) == ICE_AQ_RES_GLBL_SUCCESS) {
			*timeout = le32_to_cpu(cmd_resp->timeout);
			return 0;
		} else if (le16_to_cpu(cmd_resp->status) ==
			   ICE_AQ_RES_GLBL_IN_PROG) {
			*timeout = le32_to_cpu(cmd_resp->timeout);
			return ICE_ERR_AQ_ERROR;
		} else if (le16_to_cpu(cmd_resp->status) ==
			   ICE_AQ_RES_GLBL_DONE) {
			return ICE_ERR_AQ_NO_WORK;
		}

		/* invalid FW response, force a timeout immediately */
		*timeout = 0;
		return ICE_ERR_AQ_ERROR;
	}

	/* If the resource is held by some other driver, the command completes
	 * with a busy return value and the timeout field indicates the maximum
	 * time the current owner of the resource has to free it.
	 */
	if (!status || hw->adminq.sq_last_status == ICE_AQ_RC_EBUSY)
		*timeout = le32_to_cpu(cmd_resp->timeout);

	return status;
}

/**
 * ice_aq_release_res
 * @hw: pointer to the hw struct
 * @res: resource id
 * @sdp_number: resource number
 * @cd: pointer to command details structure or NULL
 *
 * release common resource using the admin queue commands (0x0009)
 */
static enum ice_status
ice_aq_release_res(struct ice_hw *hw, enum ice_aq_res_ids res, u8 sdp_number,
		   struct ice_sq_cd *cd)
{
	struct ice_aqc_req_res *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.res_owner;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_release_res);

	cmd->res_id = cpu_to_le16(res);
	cmd->res_number = cpu_to_le32(sdp_number);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_acquire_res
 * @hw: pointer to the HW structure
 * @res: resource id
 * @access: access type (read or write)
 * @timeout: timeout in milliseconds
 *
 * This function will attempt to acquire the ownership of a resource.
 */
enum ice_status
ice_acquire_res(struct ice_hw *hw, enum ice_aq_res_ids res,
		enum ice_aq_res_access_type access, u32 timeout)
{
#define ICE_RES_POLLING_DELAY_MS	10
	u32 delay = ICE_RES_POLLING_DELAY_MS;
	u32 time_left = timeout;
	enum ice_status status;

	status = ice_aq_req_res(hw, res, access, 0, &time_left, NULL);

	/* A return code of ICE_ERR_AQ_NO_WORK means that another driver has
	 * previously acquired the resource and performed any necessary updates;
	 * in this case the caller does not obtain the resource and has no
	 * further work to do.
	 */
	if (status == ICE_ERR_AQ_NO_WORK)
		goto ice_acquire_res_exit;

	if (status)
		ice_debug(hw, ICE_DBG_RES,
			  "resource %d acquire type %d failed.\n", res, access);

	/* If necessary, poll until the current lock owner timeouts */
	timeout = time_left;
	while (status && timeout && time_left) {
		mdelay(delay);
		timeout = (timeout > delay) ? timeout - delay : 0;
		status = ice_aq_req_res(hw, res, access, 0, &time_left, NULL);

		if (status == ICE_ERR_AQ_NO_WORK)
			/* lock free, but no work to do */
			break;

		if (!status)
			/* lock acquired */
			break;
	}
	if (status && status != ICE_ERR_AQ_NO_WORK)
		ice_debug(hw, ICE_DBG_RES, "resource acquire timed out.\n");

ice_acquire_res_exit:
	if (status == ICE_ERR_AQ_NO_WORK) {
		if (access == ICE_RES_WRITE)
			ice_debug(hw, ICE_DBG_RES,
				  "resource indicates no work to do.\n");
		else
			ice_debug(hw, ICE_DBG_RES,
				  "Warning: ICE_ERR_AQ_NO_WORK not expected\n");
	}
	return status;
}

/**
 * ice_release_res
 * @hw: pointer to the HW structure
 * @res: resource id
 *
 * This function will release a resource using the proper Admin Command.
 */
void ice_release_res(struct ice_hw *hw, enum ice_aq_res_ids res)
{
	enum ice_status status;
	u32 total_delay = 0;

	status = ice_aq_release_res(hw, res, 0, NULL);

	/* there are some rare cases when trying to release the resource
	 * results in an admin Q timeout, so handle them correctly
	 */
	while ((status == ICE_ERR_AQ_TIMEOUT) &&
	       (total_delay < hw->adminq.sq_cmd_timeout)) {
		mdelay(1);
		status = ice_aq_release_res(hw, res, 0, NULL);
		total_delay++;
	}
}

/**
 * ice_parse_caps - parse function/device capabilities
 * @hw: pointer to the hw struct
 * @buf: pointer to a buffer containing function/device capability records
 * @cap_count: number of capability records in the list
 * @opc: type of capabilities list to parse
 *
 * Helper function to parse function(0x000a)/device(0x000b) capabilities list.
 */
static void
ice_parse_caps(struct ice_hw *hw, void *buf, u32 cap_count,
	       enum ice_adminq_opc opc)
{
	struct ice_aqc_list_caps_elem *cap_resp;
	struct ice_hw_func_caps *func_p = NULL;
	struct ice_hw_dev_caps *dev_p = NULL;
	struct ice_hw_common_caps *caps;
	u32 i;

	if (!buf)
		return;

	cap_resp = (struct ice_aqc_list_caps_elem *)buf;

	if (opc == ice_aqc_opc_list_dev_caps) {
		dev_p = &hw->dev_caps;
		caps = &dev_p->common_cap;
	} else if (opc == ice_aqc_opc_list_func_caps) {
		func_p = &hw->func_caps;
		caps = &func_p->common_cap;
	} else {
		ice_debug(hw, ICE_DBG_INIT, "wrong opcode\n");
		return;
	}

	for (i = 0; caps && i < cap_count; i++, cap_resp++) {
		u32 logical_id = le32_to_cpu(cap_resp->logical_id);
		u32 phys_id = le32_to_cpu(cap_resp->phys_id);
		u32 number = le32_to_cpu(cap_resp->number);
		u16 cap = le16_to_cpu(cap_resp->cap);

		switch (cap) {
		case ICE_AQC_CAPS_VSI:
			if (dev_p) {
				dev_p->num_vsi_allocd_to_host = number;
				ice_debug(hw, ICE_DBG_INIT,
					  "HW caps: Dev.VSI cnt = %d\n",
					  dev_p->num_vsi_allocd_to_host);
			} else if (func_p) {
				func_p->guaranteed_num_vsi = number;
				ice_debug(hw, ICE_DBG_INIT,
					  "HW caps: Func.VSI cnt = %d\n",
					  func_p->guaranteed_num_vsi);
			}
			break;
		case ICE_AQC_CAPS_RSS:
			caps->rss_table_size = number;
			caps->rss_table_entry_width = logical_id;
			ice_debug(hw, ICE_DBG_INIT,
				  "HW caps: RSS table size = %d\n",
				  caps->rss_table_size);
			ice_debug(hw, ICE_DBG_INIT,
				  "HW caps: RSS table width = %d\n",
				  caps->rss_table_entry_width);
			break;
		case ICE_AQC_CAPS_RXQS:
			caps->num_rxq = number;
			caps->rxq_first_id = phys_id;
			ice_debug(hw, ICE_DBG_INIT,
				  "HW caps: Num Rx Qs = %d\n", caps->num_rxq);
			ice_debug(hw, ICE_DBG_INIT,
				  "HW caps: Rx first queue ID = %d\n",
				  caps->rxq_first_id);
			break;
		case ICE_AQC_CAPS_TXQS:
			caps->num_txq = number;
			caps->txq_first_id = phys_id;
			ice_debug(hw, ICE_DBG_INIT,
				  "HW caps: Num Tx Qs = %d\n", caps->num_txq);
			ice_debug(hw, ICE_DBG_INIT,
				  "HW caps: Tx first queue ID = %d\n",
				  caps->txq_first_id);
			break;
		case ICE_AQC_CAPS_MSIX:
			caps->num_msix_vectors = number;
			caps->msix_vector_first_id = phys_id;
			ice_debug(hw, ICE_DBG_INIT,
				  "HW caps: MSIX vector count = %d\n",
				  caps->num_msix_vectors);
			ice_debug(hw, ICE_DBG_INIT,
				  "HW caps: MSIX first vector index = %d\n",
				  caps->msix_vector_first_id);
			break;
		case ICE_AQC_CAPS_MAX_MTU:
			caps->max_mtu = number;
			if (dev_p)
				ice_debug(hw, ICE_DBG_INIT,
					  "HW caps: Dev.MaxMTU = %d\n",
					  caps->max_mtu);
			else if (func_p)
				ice_debug(hw, ICE_DBG_INIT,
					  "HW caps: func.MaxMTU = %d\n",
					  caps->max_mtu);
			break;
		default:
			ice_debug(hw, ICE_DBG_INIT,
				  "HW caps: Unknown capability[%d]: 0x%x\n", i,
				  cap);
			break;
		}
	}
}

/**
 * ice_aq_discover_caps - query function/device capabilities
 * @hw: pointer to the hw struct
 * @buf: a virtual buffer to hold the capabilities
 * @buf_size: Size of the virtual buffer
 * @data_size: Size of the returned data, or buf size needed if AQ err==ENOMEM
 * @opc: capabilities type to discover - pass in the command opcode
 * @cd: pointer to command details structure or NULL
 *
 * Get the function(0x000a)/device(0x000b) capabilities description from
 * the firmware.
 */
static enum ice_status
ice_aq_discover_caps(struct ice_hw *hw, void *buf, u16 buf_size, u16 *data_size,
		     enum ice_adminq_opc opc, struct ice_sq_cd *cd)
{
	struct ice_aqc_list_caps *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.get_cap;

	if (opc != ice_aqc_opc_list_func_caps &&
	    opc != ice_aqc_opc_list_dev_caps)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, opc);

	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status)
		ice_parse_caps(hw, buf, le32_to_cpu(cmd->count), opc);
	*data_size = le16_to_cpu(desc.datalen);

	return status;
}

/**
 * ice_get_caps - get info about the HW
 * @hw: pointer to the hardware structure
 */
enum ice_status ice_get_caps(struct ice_hw *hw)
{
	enum ice_status status;
	u16 data_size = 0;
	u16 cbuf_len;
	u8 retries;

	/* The driver doesn't know how many capabilities the device will return
	 * so the buffer size required isn't known ahead of time. The driver
	 * starts with cbuf_len and if this turns out to be insufficient, the
	 * device returns ICE_AQ_RC_ENOMEM and also the buffer size it needs.
	 * The driver then allocates the buffer of this size and retries the
	 * operation. So it follows that the retry count is 2.
	 */
#define ICE_GET_CAP_BUF_COUNT	40
#define ICE_GET_CAP_RETRY_COUNT	2

	cbuf_len = ICE_GET_CAP_BUF_COUNT *
		sizeof(struct ice_aqc_list_caps_elem);

	retries = ICE_GET_CAP_RETRY_COUNT;

	do {
		void *cbuf;

		cbuf = devm_kzalloc(ice_hw_to_dev(hw), cbuf_len, GFP_KERNEL);
		if (!cbuf)
			return ICE_ERR_NO_MEMORY;

		status = ice_aq_discover_caps(hw, cbuf, cbuf_len, &data_size,
					      ice_aqc_opc_list_func_caps, NULL);
		devm_kfree(ice_hw_to_dev(hw), cbuf);

		if (!status || hw->adminq.sq_last_status != ICE_AQ_RC_ENOMEM)
			break;

		/* If ENOMEM is returned, try again with bigger buffer */
		cbuf_len = data_size;
	} while (--retries);

	return status;
}

/**
 * ice_aq_manage_mac_write - manage MAC address write command
 * @hw: pointer to the hw struct
 * @mac_addr: MAC address to be written as LAA/LAA+WoL/Port address
 * @flags: flags to control write behavior
 * @cd: pointer to command details structure or NULL
 *
 * This function is used to write MAC address to the NVM (0x0108).
 */
enum ice_status
ice_aq_manage_mac_write(struct ice_hw *hw, u8 *mac_addr, u8 flags,
			struct ice_sq_cd *cd)
{
	struct ice_aqc_manage_mac_write *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.mac_write;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_manage_mac_write);

	cmd->flags = flags;

	/* Prep values for flags, sah, sal */
	cmd->sah = htons(*((u16 *)mac_addr));
	cmd->sal = htonl(*((u32 *)(mac_addr + 2)));

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_aq_clear_pxe_mode
 * @hw: pointer to the hw struct
 *
 * Tell the firmware that the driver is taking over from PXE (0x0110).
 */
static enum ice_status ice_aq_clear_pxe_mode(struct ice_hw *hw)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_clear_pxe_mode);
	desc.params.clear_pxe.rx_cnt = ICE_AQC_CLEAR_PXE_RX_CNT;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
}

/**
 * ice_clear_pxe_mode - clear pxe operations mode
 * @hw: pointer to the hw struct
 *
 * Make sure all PXE mode settings are cleared, including things
 * like descriptor fetch/write-back mode.
 */
void ice_clear_pxe_mode(struct ice_hw *hw)
{
	if (ice_check_sq_alive(hw, &hw->adminq))
		ice_aq_clear_pxe_mode(hw);
}

/**
 * ice_aq_set_phy_cfg
 * @hw: pointer to the hw struct
 * @lport: logical port number
 * @cfg: structure with PHY configuration data to be set
 * @cd: pointer to command details structure or NULL
 *
 * Set the various PHY configuration parameters supported on the Port.
 * One or more of the Set PHY config parameters may be ignored in an MFP
 * mode as the PF may not have the privilege to set some of the PHY Config
 * parameters. This status will be indicated by the command response (0x0601).
 */
static enum ice_status
ice_aq_set_phy_cfg(struct ice_hw *hw, u8 lport,
		   struct ice_aqc_set_phy_cfg_data *cfg, struct ice_sq_cd *cd)
{
	struct ice_aqc_set_phy_cfg *cmd;
	struct ice_aq_desc desc;

	if (!cfg)
		return ICE_ERR_PARAM;

	cmd = &desc.params.set_phy;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_phy_cfg);
	cmd->lport_num = lport;

	return ice_aq_send_cmd(hw, &desc, cfg, sizeof(*cfg), cd);
}

/**
 * ice_update_link_info - update status of the HW network link
 * @pi: port info structure of the interested logical port
 */
static enum ice_status
ice_update_link_info(struct ice_port_info *pi)
{
	struct ice_aqc_get_phy_caps_data *pcaps;
	struct ice_phy_info *phy_info;
	enum ice_status status;
	struct ice_hw *hw;

	if (!pi)
		return ICE_ERR_PARAM;

	hw = pi->hw;

	pcaps = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*pcaps), GFP_KERNEL);
	if (!pcaps)
		return ICE_ERR_NO_MEMORY;

	phy_info = &pi->phy;
	status = ice_aq_get_link_info(pi, true, NULL, NULL);
	if (status)
		goto out;

	if (phy_info->link_info.link_info & ICE_AQ_MEDIA_AVAILABLE) {
		status = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_SW_CFG,
					     pcaps, NULL);
		if (status)
			goto out;

		memcpy(phy_info->link_info.module_type, &pcaps->module_type,
		       sizeof(phy_info->link_info.module_type));
	}
out:
	devm_kfree(ice_hw_to_dev(hw), pcaps);
	return status;
}

/**
 * ice_set_fc
 * @pi: port information structure
 * @aq_failures: pointer to status code, specific to ice_set_fc routine
 * @atomic_restart: enable automatic link update
 *
 * Set the requested flow control mode.
 */
enum ice_status
ice_set_fc(struct ice_port_info *pi, u8 *aq_failures, bool atomic_restart)
{
	struct ice_aqc_set_phy_cfg_data cfg = { 0 };
	struct ice_aqc_get_phy_caps_data *pcaps;
	enum ice_status status;
	u8 pause_mask = 0x0;
	struct ice_hw *hw;

	if (!pi)
		return ICE_ERR_PARAM;
	hw = pi->hw;
	*aq_failures = ICE_SET_FC_AQ_FAIL_NONE;

	switch (pi->fc.req_mode) {
	case ICE_FC_FULL:
		pause_mask |= ICE_AQC_PHY_EN_TX_LINK_PAUSE;
		pause_mask |= ICE_AQC_PHY_EN_RX_LINK_PAUSE;
		break;
	case ICE_FC_RX_PAUSE:
		pause_mask |= ICE_AQC_PHY_EN_RX_LINK_PAUSE;
		break;
	case ICE_FC_TX_PAUSE:
		pause_mask |= ICE_AQC_PHY_EN_TX_LINK_PAUSE;
		break;
	default:
		break;
	}

	pcaps = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*pcaps), GFP_KERNEL);
	if (!pcaps)
		return ICE_ERR_NO_MEMORY;

	/* Get the current phy config */
	status = ice_aq_get_phy_caps(pi, false, ICE_AQC_REPORT_SW_CFG, pcaps,
				     NULL);
	if (status) {
		*aq_failures = ICE_SET_FC_AQ_FAIL_GET;
		goto out;
	}

	/* clear the old pause settings */
	cfg.caps = pcaps->caps & ~(ICE_AQC_PHY_EN_TX_LINK_PAUSE |
				   ICE_AQC_PHY_EN_RX_LINK_PAUSE);
	/* set the new capabilities */
	cfg.caps |= pause_mask;
	/* If the capabilities have changed, then set the new config */
	if (cfg.caps != pcaps->caps) {
		int retry_count, retry_max = 10;

		/* Auto restart link so settings take effect */
		if (atomic_restart)
			cfg.caps |= ICE_AQ_PHY_ENA_ATOMIC_LINK;
		/* Copy over all the old settings */
		cfg.phy_type_low = pcaps->phy_type_low;
		cfg.low_power_ctrl = pcaps->low_power_ctrl;
		cfg.eee_cap = pcaps->eee_cap;
		cfg.eeer_value = pcaps->eeer_value;
		cfg.link_fec_opt = pcaps->link_fec_options;

		status = ice_aq_set_phy_cfg(hw, pi->lport, &cfg, NULL);
		if (status) {
			*aq_failures = ICE_SET_FC_AQ_FAIL_SET;
			goto out;
		}

		/* Update the link info
		 * It sometimes takes a really long time for link to
		 * come back from the atomic reset. Thus, we wait a
		 * little bit.
		 */
		for (retry_count = 0; retry_count < retry_max; retry_count++) {
			status = ice_update_link_info(pi);

			if (!status)
				break;

			mdelay(100);
		}

		if (status)
			*aq_failures = ICE_SET_FC_AQ_FAIL_UPDATE;
	}

out:
	devm_kfree(ice_hw_to_dev(hw), pcaps);
	return status;
}

/**
 * ice_get_link_status - get status of the HW network link
 * @pi: port information structure
 * @link_up: pointer to bool (true/false = linkup/linkdown)
 *
 * Variable link_up is true if link is up, false if link is down.
 * The variable link_up is invalid if status is non zero. As a
 * result of this call, link status reporting becomes enabled
 */
enum ice_status ice_get_link_status(struct ice_port_info *pi, bool *link_up)
{
	struct ice_phy_info *phy_info;
	enum ice_status status = 0;

	if (!pi || !link_up)
		return ICE_ERR_PARAM;

	phy_info = &pi->phy;

	if (phy_info->get_link_info) {
		status = ice_update_link_info(pi);

		if (status)
			ice_debug(pi->hw, ICE_DBG_LINK,
				  "get link status error, status = %d\n",
				  status);
	}

	*link_up = phy_info->link_info.link_info & ICE_AQ_LINK_UP;

	return status;
}

/**
 * ice_aq_set_link_restart_an
 * @pi: pointer to the port information structure
 * @ena_link: if true: enable link, if false: disable link
 * @cd: pointer to command details structure or NULL
 *
 * Sets up the link and restarts the Auto-Negotiation over the link.
 */
enum ice_status
ice_aq_set_link_restart_an(struct ice_port_info *pi, bool ena_link,
			   struct ice_sq_cd *cd)
{
	struct ice_aqc_restart_an *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.restart_an;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_restart_an);

	cmd->cmd_flags = ICE_AQC_RESTART_AN_LINK_RESTART;
	cmd->lport_num = pi->lport;
	if (ena_link)
		cmd->cmd_flags |= ICE_AQC_RESTART_AN_LINK_ENABLE;
	else
		cmd->cmd_flags &= ~ICE_AQC_RESTART_AN_LINK_ENABLE;

	return ice_aq_send_cmd(pi->hw, &desc, NULL, 0, cd);
}

/**
 * ice_aq_set_event_mask
 * @hw: pointer to the hw struct
 * @port_num: port number of the physical function
 * @mask: event mask to be set
 * @cd: pointer to command details structure or NULL
 *
 * Set event mask (0x0613)
 */
enum ice_status
ice_aq_set_event_mask(struct ice_hw *hw, u8 port_num, u16 mask,
		      struct ice_sq_cd *cd)
{
	struct ice_aqc_set_event_mask *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.set_event_mask;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_event_mask);

	cmd->lport_num = port_num;

	cmd->event_mask = cpu_to_le16(mask);

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * __ice_aq_get_set_rss_lut
 * @hw: pointer to the hardware structure
 * @vsi_id: VSI FW index
 * @lut_type: LUT table type
 * @lut: pointer to the LUT buffer provided by the caller
 * @lut_size: size of the LUT buffer
 * @glob_lut_idx: global LUT index
 * @set: set true to set the table, false to get the table
 *
 * Internal function to get (0x0B05) or set (0x0B03) RSS look up table
 */
static enum ice_status
__ice_aq_get_set_rss_lut(struct ice_hw *hw, u16 vsi_id, u8 lut_type, u8 *lut,
			 u16 lut_size, u8 glob_lut_idx, bool set)
{
	struct ice_aqc_get_set_rss_lut *cmd_resp;
	struct ice_aq_desc desc;
	enum ice_status status;
	u16 flags = 0;

	cmd_resp = &desc.params.get_set_rss_lut;

	if (set) {
		ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_rss_lut);
		desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);
	} else {
		ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_rss_lut);
	}

	cmd_resp->vsi_id = cpu_to_le16(((vsi_id <<
					 ICE_AQC_GSET_RSS_LUT_VSI_ID_S) &
					ICE_AQC_GSET_RSS_LUT_VSI_ID_M) |
				       ICE_AQC_GSET_RSS_LUT_VSI_VALID);

	switch (lut_type) {
	case ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_VSI:
	case ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_PF:
	case ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_GLOBAL:
		flags |= ((lut_type << ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_S) &
			  ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_M);
		break;
	default:
		status = ICE_ERR_PARAM;
		goto ice_aq_get_set_rss_lut_exit;
	}

	if (lut_type == ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_GLOBAL) {
		flags |= ((glob_lut_idx << ICE_AQC_GSET_RSS_LUT_GLOBAL_IDX_S) &
			  ICE_AQC_GSET_RSS_LUT_GLOBAL_IDX_M);

		if (!set)
			goto ice_aq_get_set_rss_lut_send;
	} else if (lut_type == ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_PF) {
		if (!set)
			goto ice_aq_get_set_rss_lut_send;
	} else {
		goto ice_aq_get_set_rss_lut_send;
	}

	/* LUT size is only valid for Global and PF table types */
	switch (lut_size) {
	case ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_128:
		break;
	case ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_512:
		flags |= (ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_512_FLAG <<
			  ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_S) &
			 ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_M;
		break;
	case ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_2K:
		if (lut_type == ICE_AQC_GSET_RSS_LUT_TABLE_TYPE_PF) {
			flags |= (ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_2K_FLAG <<
				  ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_S) &
				 ICE_AQC_GSET_RSS_LUT_TABLE_SIZE_M;
			break;
		}
		/* fall-through */
	default:
		status = ICE_ERR_PARAM;
		goto ice_aq_get_set_rss_lut_exit;
	}

ice_aq_get_set_rss_lut_send:
	cmd_resp->flags = cpu_to_le16(flags);
	status = ice_aq_send_cmd(hw, &desc, lut, lut_size, NULL);

ice_aq_get_set_rss_lut_exit:
	return status;
}

/**
 * ice_aq_get_rss_lut
 * @hw: pointer to the hardware structure
 * @vsi_id: VSI FW index
 * @lut_type: LUT table type
 * @lut: pointer to the LUT buffer provided by the caller
 * @lut_size: size of the LUT buffer
 *
 * get the RSS lookup table, PF or VSI type
 */
enum ice_status
ice_aq_get_rss_lut(struct ice_hw *hw, u16 vsi_id, u8 lut_type, u8 *lut,
		   u16 lut_size)
{
	return __ice_aq_get_set_rss_lut(hw, vsi_id, lut_type, lut, lut_size, 0,
					false);
}

/**
 * ice_aq_set_rss_lut
 * @hw: pointer to the hardware structure
 * @vsi_id: VSI FW index
 * @lut_type: LUT table type
 * @lut: pointer to the LUT buffer provided by the caller
 * @lut_size: size of the LUT buffer
 *
 * set the RSS lookup table, PF or VSI type
 */
enum ice_status
ice_aq_set_rss_lut(struct ice_hw *hw, u16 vsi_id, u8 lut_type, u8 *lut,
		   u16 lut_size)
{
	return __ice_aq_get_set_rss_lut(hw, vsi_id, lut_type, lut, lut_size, 0,
					true);
}

/**
 * __ice_aq_get_set_rss_key
 * @hw: pointer to the hw struct
 * @vsi_id: VSI FW index
 * @key: pointer to key info struct
 * @set: set true to set the key, false to get the key
 *
 * get (0x0B04) or set (0x0B02) the RSS key per VSI
 */
static enum
ice_status __ice_aq_get_set_rss_key(struct ice_hw *hw, u16 vsi_id,
				    struct ice_aqc_get_set_rss_keys *key,
				    bool set)
{
	struct ice_aqc_get_set_rss_key *cmd_resp;
	u16 key_size = sizeof(*key);
	struct ice_aq_desc desc;

	cmd_resp = &desc.params.get_set_rss_key;

	if (set) {
		ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_rss_key);
		desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);
	} else {
		ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_rss_key);
	}

	cmd_resp->vsi_id = cpu_to_le16(((vsi_id <<
					 ICE_AQC_GSET_RSS_KEY_VSI_ID_S) &
					ICE_AQC_GSET_RSS_KEY_VSI_ID_M) |
				       ICE_AQC_GSET_RSS_KEY_VSI_VALID);

	return ice_aq_send_cmd(hw, &desc, key, key_size, NULL);
}

/**
 * ice_aq_get_rss_key
 * @hw: pointer to the hw struct
 * @vsi_id: VSI FW index
 * @key: pointer to key info struct
 *
 * get the RSS key per VSI
 */
enum ice_status
ice_aq_get_rss_key(struct ice_hw *hw, u16 vsi_id,
		   struct ice_aqc_get_set_rss_keys *key)
{
	return __ice_aq_get_set_rss_key(hw, vsi_id, key, false);
}

/**
 * ice_aq_set_rss_key
 * @hw: pointer to the hw struct
 * @vsi_id: VSI FW index
 * @keys: pointer to key info struct
 *
 * set the RSS key per VSI
 */
enum ice_status
ice_aq_set_rss_key(struct ice_hw *hw, u16 vsi_id,
		   struct ice_aqc_get_set_rss_keys *keys)
{
	return __ice_aq_get_set_rss_key(hw, vsi_id, keys, true);
}

/**
 * ice_aq_add_lan_txq
 * @hw: pointer to the hardware structure
 * @num_qgrps: Number of added queue groups
 * @qg_list: list of queue groups to be added
 * @buf_size: size of buffer for indirect command
 * @cd: pointer to command details structure or NULL
 *
 * Add Tx LAN queue (0x0C30)
 *
 * NOTE:
 * Prior to calling add Tx LAN queue:
 * Initialize the following as part of the Tx queue context:
 * Completion queue ID if the queue uses Completion queue, Quanta profile,
 * Cache profile and Packet shaper profile.
 *
 * After add Tx LAN queue AQ command is completed:
 * Interrupts should be associated with specific queues,
 * Association of Tx queue to Doorbell queue is not part of Add LAN Tx queue
 * flow.
 */
static enum ice_status
ice_aq_add_lan_txq(struct ice_hw *hw, u8 num_qgrps,
		   struct ice_aqc_add_tx_qgrp *qg_list, u16 buf_size,
		   struct ice_sq_cd *cd)
{
	u16 i, sum_header_size, sum_q_size = 0;
	struct ice_aqc_add_tx_qgrp *list;
	struct ice_aqc_add_txqs *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.add_txqs;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_add_txqs);

	if (!qg_list)
		return ICE_ERR_PARAM;

	if (num_qgrps > ICE_LAN_TXQ_MAX_QGRPS)
		return ICE_ERR_PARAM;

	sum_header_size = num_qgrps *
		(sizeof(*qg_list) - sizeof(*qg_list->txqs));

	list = qg_list;
	for (i = 0; i < num_qgrps; i++) {
		struct ice_aqc_add_txqs_perq *q = list->txqs;

		sum_q_size += list->num_txqs * sizeof(*q);
		list = (struct ice_aqc_add_tx_qgrp *)(q + list->num_txqs);
	}

	if (buf_size != (sum_header_size + sum_q_size))
		return ICE_ERR_PARAM;

	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	cmd->num_qgrps = num_qgrps;

	return ice_aq_send_cmd(hw, &desc, qg_list, buf_size, cd);
}

/**
 * ice_aq_dis_lan_txq
 * @hw: pointer to the hardware structure
 * @num_qgrps: number of groups in the list
 * @qg_list: the list of groups to disable
 * @buf_size: the total size of the qg_list buffer in bytes
 * @cd: pointer to command details structure or NULL
 *
 * Disable LAN Tx queue (0x0C31)
 */
static enum ice_status
ice_aq_dis_lan_txq(struct ice_hw *hw, u8 num_qgrps,
		   struct ice_aqc_dis_txq_item *qg_list, u16 buf_size,
		   struct ice_sq_cd *cd)
{
	struct ice_aqc_dis_txqs *cmd;
	struct ice_aq_desc desc;
	u16 i, sz = 0;

	cmd = &desc.params.dis_txqs;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_dis_txqs);

	if (!qg_list)
		return ICE_ERR_PARAM;

	if (num_qgrps > ICE_LAN_TXQ_MAX_QGRPS)
		return ICE_ERR_PARAM;
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);
	cmd->num_entries = num_qgrps;

	for (i = 0; i < num_qgrps; ++i) {
		/* Calculate the size taken up by the queue IDs in this group */
		sz += qg_list[i].num_qs * sizeof(qg_list[i].q_id);

		/* Add the size of the group header */
		sz += sizeof(qg_list[i]) - sizeof(qg_list[i].q_id);

		/* If the num of queues is even, add 2 bytes of padding */
		if ((qg_list[i].num_qs % 2) == 0)
			sz += 2;
	}

	if (buf_size != sz)
		return ICE_ERR_PARAM;

	return ice_aq_send_cmd(hw, &desc, qg_list, buf_size, cd);
}

/* End of FW Admin Queue command wrappers */

/**
 * ice_write_byte - write a byte to a packed context structure
 * @src_ctx:  the context structure to read from
 * @dest_ctx: the context to be written to
 * @ce_info:  a description of the struct to be filled
 */
static void ice_write_byte(u8 *src_ctx, u8 *dest_ctx,
			   const struct ice_ctx_ele *ce_info)
{
	u8 src_byte, dest_byte, mask;
	u8 *from, *dest;
	u16 shift_width;

	/* copy from the next struct field */
	from = src_ctx + ce_info->offset;

	/* prepare the bits and mask */
	shift_width = ce_info->lsb % 8;
	mask = (u8)(BIT(ce_info->width) - 1);

	src_byte = *from;
	src_byte &= mask;

	/* shift to correct alignment */
	mask <<= shift_width;
	src_byte <<= shift_width;

	/* get the current bits from the target bit string */
	dest = dest_ctx + (ce_info->lsb / 8);

	memcpy(&dest_byte, dest, sizeof(dest_byte));

	dest_byte &= ~mask;	/* get the bits not changing */
	dest_byte |= src_byte;	/* add in the new bits */

	/* put it all back */
	memcpy(dest, &dest_byte, sizeof(dest_byte));
}

/**
 * ice_write_word - write a word to a packed context structure
 * @src_ctx:  the context structure to read from
 * @dest_ctx: the context to be written to
 * @ce_info:  a description of the struct to be filled
 */
static void ice_write_word(u8 *src_ctx, u8 *dest_ctx,
			   const struct ice_ctx_ele *ce_info)
{
	u16 src_word, mask;
	__le16 dest_word;
	u8 *from, *dest;
	u16 shift_width;

	/* copy from the next struct field */
	from = src_ctx + ce_info->offset;

	/* prepare the bits and mask */
	shift_width = ce_info->lsb % 8;
	mask = BIT(ce_info->width) - 1;

	/* don't swizzle the bits until after the mask because the mask bits
	 * will be in a different bit position on big endian machines
	 */
	src_word = *(u16 *)from;
	src_word &= mask;

	/* shift to correct alignment */
	mask <<= shift_width;
	src_word <<= shift_width;

	/* get the current bits from the target bit string */
	dest = dest_ctx + (ce_info->lsb / 8);

	memcpy(&dest_word, dest, sizeof(dest_word));

	dest_word &= ~(cpu_to_le16(mask));	/* get the bits not changing */
	dest_word |= cpu_to_le16(src_word);	/* add in the new bits */

	/* put it all back */
	memcpy(dest, &dest_word, sizeof(dest_word));
}

/**
 * ice_write_dword - write a dword to a packed context structure
 * @src_ctx:  the context structure to read from
 * @dest_ctx: the context to be written to
 * @ce_info:  a description of the struct to be filled
 */
static void ice_write_dword(u8 *src_ctx, u8 *dest_ctx,
			    const struct ice_ctx_ele *ce_info)
{
	u32 src_dword, mask;
	__le32 dest_dword;
	u8 *from, *dest;
	u16 shift_width;

	/* copy from the next struct field */
	from = src_ctx + ce_info->offset;

	/* prepare the bits and mask */
	shift_width = ce_info->lsb % 8;

	/* if the field width is exactly 32 on an x86 machine, then the shift
	 * operation will not work because the SHL instructions count is masked
	 * to 5 bits so the shift will do nothing
	 */
	if (ce_info->width < 32)
		mask = BIT(ce_info->width) - 1;
	else
		mask = (u32)~0;

	/* don't swizzle the bits until after the mask because the mask bits
	 * will be in a different bit position on big endian machines
	 */
	src_dword = *(u32 *)from;
	src_dword &= mask;

	/* shift to correct alignment */
	mask <<= shift_width;
	src_dword <<= shift_width;

	/* get the current bits from the target bit string */
	dest = dest_ctx + (ce_info->lsb / 8);

	memcpy(&dest_dword, dest, sizeof(dest_dword));

	dest_dword &= ~(cpu_to_le32(mask));	/* get the bits not changing */
	dest_dword |= cpu_to_le32(src_dword);	/* add in the new bits */

	/* put it all back */
	memcpy(dest, &dest_dword, sizeof(dest_dword));
}

/**
 * ice_write_qword - write a qword to a packed context structure
 * @src_ctx:  the context structure to read from
 * @dest_ctx: the context to be written to
 * @ce_info:  a description of the struct to be filled
 */
static void ice_write_qword(u8 *src_ctx, u8 *dest_ctx,
			    const struct ice_ctx_ele *ce_info)
{
	u64 src_qword, mask;
	__le64 dest_qword;
	u8 *from, *dest;
	u16 shift_width;

	/* copy from the next struct field */
	from = src_ctx + ce_info->offset;

	/* prepare the bits and mask */
	shift_width = ce_info->lsb % 8;

	/* if the field width is exactly 64 on an x86 machine, then the shift
	 * operation will not work because the SHL instructions count is masked
	 * to 6 bits so the shift will do nothing
	 */
	if (ce_info->width < 64)
		mask = BIT_ULL(ce_info->width) - 1;
	else
		mask = (u64)~0;

	/* don't swizzle the bits until after the mask because the mask bits
	 * will be in a different bit position on big endian machines
	 */
	src_qword = *(u64 *)from;
	src_qword &= mask;

	/* shift to correct alignment */
	mask <<= shift_width;
	src_qword <<= shift_width;

	/* get the current bits from the target bit string */
	dest = dest_ctx + (ce_info->lsb / 8);

	memcpy(&dest_qword, dest, sizeof(dest_qword));

	dest_qword &= ~(cpu_to_le64(mask));	/* get the bits not changing */
	dest_qword |= cpu_to_le64(src_qword);	/* add in the new bits */

	/* put it all back */
	memcpy(dest, &dest_qword, sizeof(dest_qword));
}

/**
 * ice_set_ctx - set context bits in packed structure
 * @src_ctx:  pointer to a generic non-packed context structure
 * @dest_ctx: pointer to memory for the packed structure
 * @ce_info:  a description of the structure to be transformed
 */
enum ice_status
ice_set_ctx(u8 *src_ctx, u8 *dest_ctx, const struct ice_ctx_ele *ce_info)
{
	int f;

	for (f = 0; ce_info[f].width; f++) {
		/* We have to deal with each element of the FW response
		 * using the correct size so that we are correct regardless
		 * of the endianness of the machine.
		 */
		switch (ce_info[f].size_of) {
		case sizeof(u8):
			ice_write_byte(src_ctx, dest_ctx, &ce_info[f]);
			break;
		case sizeof(u16):
			ice_write_word(src_ctx, dest_ctx, &ce_info[f]);
			break;
		case sizeof(u32):
			ice_write_dword(src_ctx, dest_ctx, &ce_info[f]);
			break;
		case sizeof(u64):
			ice_write_qword(src_ctx, dest_ctx, &ce_info[f]);
			break;
		default:
			return ICE_ERR_INVAL_SIZE;
		}
	}

	return 0;
}

/**
 * ice_ena_vsi_txq
 * @pi: port information structure
 * @vsi_id: VSI id
 * @tc: tc number
 * @num_qgrps: Number of added queue groups
 * @buf: list of queue groups to be added
 * @buf_size: size of buffer for indirect command
 * @cd: pointer to command details structure or NULL
 *
 * This function adds one lan q
 */
enum ice_status
ice_ena_vsi_txq(struct ice_port_info *pi, u16 vsi_id, u8 tc, u8 num_qgrps,
		struct ice_aqc_add_tx_qgrp *buf, u16 buf_size,
		struct ice_sq_cd *cd)
{
	struct ice_aqc_txsched_elem_data node = { 0 };
	struct ice_sched_node *parent;
	enum ice_status status;
	struct ice_hw *hw;

	if (!pi || pi->port_state != ICE_SCHED_PORT_STATE_READY)
		return ICE_ERR_CFG;

	if (num_qgrps > 1 || buf->num_txqs > 1)
		return ICE_ERR_MAX_LIMIT;

	hw = pi->hw;

	mutex_lock(&pi->sched_lock);

	/* find a parent node */
	parent = ice_sched_get_free_qparent(pi, vsi_id, tc,
					    ICE_SCHED_NODE_OWNER_LAN);
	if (!parent) {
		status = ICE_ERR_PARAM;
		goto ena_txq_exit;
	}
	buf->parent_teid = parent->info.node_teid;
	node.parent_teid = parent->info.node_teid;
	/* Mark that the values in the "generic" section as valid. The default
	 * value in the "generic" section is zero. This means that :
	 * - Scheduling mode is Bytes Per Second (BPS), indicated by Bit 0.
	 * - 0 priority among siblings, indicated by Bit 1-3.
	 * - WFQ, indicated by Bit 4.
	 * - 0 Adjustment value is used in PSM credit update flow, indicated by
	 * Bit 5-6.
	 * - Bit 7 is reserved.
	 * Without setting the generic section as valid in valid_sections, the
	 * Admin Q command will fail with error code ICE_AQ_RC_EINVAL.
	 */
	buf->txqs[0].info.valid_sections = ICE_AQC_ELEM_VALID_GENERIC;

	/* add the lan q */
	status = ice_aq_add_lan_txq(hw, num_qgrps, buf, buf_size, cd);
	if (status)
		goto ena_txq_exit;

	node.node_teid = buf->txqs[0].q_teid;
	node.data.elem_type = ICE_AQC_ELEM_TYPE_LEAF;

	/* add a leaf node into schduler tree q layer */
	status = ice_sched_add_node(pi, hw->num_tx_sched_layers - 1, &node);

ena_txq_exit:
	mutex_unlock(&pi->sched_lock);
	return status;
}

/**
 * ice_dis_vsi_txq
 * @pi: port information structure
 * @num_queues: number of queues
 * @q_ids: pointer to the q_id array
 * @q_teids: pointer to queue node teids
 * @cd: pointer to command details structure or NULL
 *
 * This function removes queues and their corresponding nodes in SW DB
 */
enum ice_status
ice_dis_vsi_txq(struct ice_port_info *pi, u8 num_queues, u16 *q_ids,
		u32 *q_teids, struct ice_sq_cd *cd)
{
	enum ice_status status = ICE_ERR_DOES_NOT_EXIST;
	struct ice_aqc_dis_txq_item qg_list;
	u16 i;

	if (!pi || pi->port_state != ICE_SCHED_PORT_STATE_READY)
		return ICE_ERR_CFG;

	mutex_lock(&pi->sched_lock);

	for (i = 0; i < num_queues; i++) {
		struct ice_sched_node *node;

		node = ice_sched_find_node_by_teid(pi->root, q_teids[i]);
		if (!node)
			continue;
		qg_list.parent_teid = node->info.parent_teid;
		qg_list.num_qs = 1;
		qg_list.q_id[0] = cpu_to_le16(q_ids[i]);
		status = ice_aq_dis_lan_txq(pi->hw, 1, &qg_list,
					    sizeof(qg_list), cd);

		if (status)
			break;
		ice_free_sched_node(pi, node);
	}
	mutex_unlock(&pi->sched_lock);
	return status;
}

/**
 * ice_cfg_vsi_qs - configure the new/exisiting VSI queues
 * @pi: port information structure
 * @vsi_id: VSI Id
 * @tc_bitmap: TC bitmap
 * @maxqs: max queues array per TC
 * @owner: lan or rdma
 *
 * This function adds/updates the VSI queues per TC.
 */
static enum ice_status
ice_cfg_vsi_qs(struct ice_port_info *pi, u16 vsi_id, u8 tc_bitmap,
	       u16 *maxqs, u8 owner)
{
	enum ice_status status = 0;
	u8 i;

	if (!pi || pi->port_state != ICE_SCHED_PORT_STATE_READY)
		return ICE_ERR_CFG;

	mutex_lock(&pi->sched_lock);

	for (i = 0; i < ICE_MAX_TRAFFIC_CLASS; i++) {
		/* configuration is possible only if TC node is present */
		if (!ice_sched_get_tc_node(pi, i))
			continue;

		status = ice_sched_cfg_vsi(pi, vsi_id, i, maxqs[i], owner,
					   ice_is_tc_ena(tc_bitmap, i));
		if (status)
			break;
	}

	mutex_unlock(&pi->sched_lock);
	return status;
}

/**
 * ice_cfg_vsi_lan - configure VSI lan queues
 * @pi: port information structure
 * @vsi_id: VSI Id
 * @tc_bitmap: TC bitmap
 * @max_lanqs: max lan queues array per TC
 *
 * This function adds/updates the VSI lan queues per TC.
 */
enum ice_status
ice_cfg_vsi_lan(struct ice_port_info *pi, u16 vsi_id, u8 tc_bitmap,
		u16 *max_lanqs)
{
	return ice_cfg_vsi_qs(pi, vsi_id, tc_bitmap, max_lanqs,
			      ICE_SCHED_NODE_OWNER_LAN);
}
