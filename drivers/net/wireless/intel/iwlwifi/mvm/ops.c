/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014, 2018 - 2020 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014, 2018 - 2020 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <net/mac80211.h>

#include "fw/notif-wait.h"
#include "iwl-trans.h"
#include "iwl-op-mode.h"
#include "fw/img.h"
#include "iwl-debug.h"
#include "iwl-drv.h"
#include "iwl-modparams.h"
#include "mvm.h"
#include "iwl-phy-db.h"
#include "iwl-eeprom-parse.h"
#include "iwl-csr.h"
#include "iwl-io.h"
#include "iwl-prph.h"
#include "rs.h"
#include "fw/api/scan.h"
#include "time-event.h"
#include "fw-api.h"
#include "fw/acpi.h"

#define DRV_DESCRIPTION	"The new Intel(R) wireless AGN driver for Linux"
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_LICENSE("GPL");

static const struct iwl_op_mode_ops iwl_mvm_ops;
static const struct iwl_op_mode_ops iwl_mvm_ops_mq;

struct iwl_mvm_mod_params iwlmvm_mod_params = {
	.power_scheme = IWL_POWER_SCHEME_BPS,
	/* rest of fields are 0 by default */
};

module_param_named(init_dbg, iwlmvm_mod_params.init_dbg, bool, 0444);
MODULE_PARM_DESC(init_dbg,
		 "set to true to debug an ASSERT in INIT fw (default: false");
module_param_named(power_scheme, iwlmvm_mod_params.power_scheme, int, 0444);
MODULE_PARM_DESC(power_scheme,
		 "power management scheme: 1-active, 2-balanced, 3-low power, default: 2");

/*
 * module init and exit functions
 */
static int __init iwl_mvm_init(void)
{
	int ret;

	ret = iwl_mvm_rate_control_register();
	if (ret) {
		pr_err("Unable to register rate control algorithm: %d\n", ret);
		return ret;
	}

	ret = iwl_opmode_register("iwlmvm", &iwl_mvm_ops);
	if (ret)
		pr_err("Unable to register MVM op_mode: %d\n", ret);

	return ret;
}
module_init(iwl_mvm_init);

static void __exit iwl_mvm_exit(void)
{
	iwl_opmode_deregister("iwlmvm");
	iwl_mvm_rate_control_unregister();
}
module_exit(iwl_mvm_exit);

static void iwl_mvm_nic_config(struct iwl_op_mode *op_mode)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	struct iwl_trans_debug *dbg = &mvm->trans->dbg;
	u8 radio_cfg_type, radio_cfg_step, radio_cfg_dash;
	u32 reg_val = 0;
	u32 phy_config = iwl_mvm_get_phy_config(mvm);

	radio_cfg_type = (phy_config & FW_PHY_CFG_RADIO_TYPE) >>
			 FW_PHY_CFG_RADIO_TYPE_POS;
	radio_cfg_step = (phy_config & FW_PHY_CFG_RADIO_STEP) >>
			 FW_PHY_CFG_RADIO_STEP_POS;
	radio_cfg_dash = (phy_config & FW_PHY_CFG_RADIO_DASH) >>
			 FW_PHY_CFG_RADIO_DASH_POS;

	/* SKU control */
	reg_val |= CSR_HW_REV_STEP(mvm->trans->hw_rev) <<
				CSR_HW_IF_CONFIG_REG_POS_MAC_STEP;
	reg_val |= CSR_HW_REV_DASH(mvm->trans->hw_rev) <<
				CSR_HW_IF_CONFIG_REG_POS_MAC_DASH;

	/* radio configuration */
	reg_val |= radio_cfg_type << CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE;
	reg_val |= radio_cfg_step << CSR_HW_IF_CONFIG_REG_POS_PHY_STEP;
	reg_val |= radio_cfg_dash << CSR_HW_IF_CONFIG_REG_POS_PHY_DASH;

	WARN_ON((radio_cfg_type << CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE) &
		 ~CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE);

	/*
	 * TODO: Bits 7-8 of CSR in 8000 HW family and higher set the ADC
	 * sampling, and shouldn't be set to any non-zero value.
	 * The same is supposed to be true of the other HW, but unsetting
	 * them (such as the 7260) causes automatic tests to fail on seemingly
	 * unrelated errors. Need to further investigate this, but for now
	 * we'll separate cases.
	 */
	if (mvm->trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_8000)
		reg_val |= CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI;

	if (iwl_fw_dbg_is_d3_debug_enabled(&mvm->fwrt) ||
	    (iwl_trans_dbg_ini_valid(mvm->trans) &&
	     dbg->fw_mon_cfg[IWL_FW_INI_ALLOCATION_ID_INTERNAL].buf_location)
	    )
		reg_val |= CSR_HW_IF_CONFIG_REG_D3_DEBUG;

	iwl_trans_set_bits_mask(mvm->trans, CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_MSK_MAC_DASH |
				CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP |
				CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE |
				CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP |
				CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH |
				CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
				CSR_HW_IF_CONFIG_REG_BIT_MAC_SI   |
				CSR_HW_IF_CONFIG_REG_D3_DEBUG,
				reg_val);

	IWL_DEBUG_INFO(mvm, "Radio type=0x%x-0x%x-0x%x\n", radio_cfg_type,
		       radio_cfg_step, radio_cfg_dash);

	/*
	 * W/A : NIC is stuck in a reset state after Early PCIe power off
	 * (PCIe power is lost before PERST# is asserted), causing ME FW
	 * to lose ownership and not being able to obtain it back.
	 */
	if (!mvm->trans->cfg->apmg_not_supported)
		iwl_set_bits_mask_prph(mvm->trans, APMG_PS_CTRL_REG,
				       APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS,
				       ~APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS);
}

/**
 * enum iwl_rx_handler_context context for Rx handler
 * @RX_HANDLER_SYNC : this means that it will be called in the Rx path
 *	which can't acquire mvm->mutex.
 * @RX_HANDLER_ASYNC_LOCKED : If the handler needs to hold mvm->mutex
 *	(and only in this case!), it should be set as ASYNC. In that case,
 *	it will be called from a worker with mvm->mutex held.
 * @RX_HANDLER_ASYNC_UNLOCKED : in case the handler needs to lock the
 *	mutex itself, it will be called from a worker without mvm->mutex held.
 */
enum iwl_rx_handler_context {
	RX_HANDLER_SYNC,
	RX_HANDLER_ASYNC_LOCKED,
	RX_HANDLER_ASYNC_UNLOCKED,
};

/**
 * struct iwl_rx_handlers handler for FW notification
 * @cmd_id: command id
 * @context: see &iwl_rx_handler_context
 * @fn: the function is called when notification is received
 */
struct iwl_rx_handlers {
	u16 cmd_id;
	enum iwl_rx_handler_context context;
	void (*fn)(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
};

#define RX_HANDLER(_cmd_id, _fn, _context)	\
	{ .cmd_id = _cmd_id, .fn = _fn, .context = _context }
#define RX_HANDLER_GRP(_grp, _cmd, _fn, _context)	\
	{ .cmd_id = WIDE_ID(_grp, _cmd), .fn = _fn, .context = _context }

/*
 * Handlers for fw notifications
 * Convention: RX_HANDLER(CMD_NAME, iwl_mvm_rx_CMD_NAME
 * This list should be in order of frequency for performance purposes.
 *
 * The handler can be one from three contexts, see &iwl_rx_handler_context
 */
static const struct iwl_rx_handlers iwl_mvm_rx_handlers[] = {
	RX_HANDLER(TX_CMD, iwl_mvm_rx_tx_cmd, RX_HANDLER_SYNC),
	RX_HANDLER(BA_NOTIF, iwl_mvm_rx_ba_notif, RX_HANDLER_SYNC),

	RX_HANDLER_GRP(DATA_PATH_GROUP, TLC_MNG_UPDATE_NOTIF,
		       iwl_mvm_tlc_update_notif, RX_HANDLER_SYNC),

	RX_HANDLER(BT_PROFILE_NOTIFICATION, iwl_mvm_rx_bt_coex_notif,
		   RX_HANDLER_ASYNC_LOCKED),
	RX_HANDLER(BEACON_NOTIFICATION, iwl_mvm_rx_beacon_notif,
		   RX_HANDLER_ASYNC_LOCKED),
	RX_HANDLER(STATISTICS_NOTIFICATION, iwl_mvm_rx_statistics,
		   RX_HANDLER_ASYNC_LOCKED),

	RX_HANDLER(BA_WINDOW_STATUS_NOTIFICATION_ID,
		   iwl_mvm_window_status_notif, RX_HANDLER_SYNC),

	RX_HANDLER(TIME_EVENT_NOTIFICATION, iwl_mvm_rx_time_event_notif,
		   RX_HANDLER_SYNC),
	RX_HANDLER_GRP(MAC_CONF_GROUP, SESSION_PROTECTION_NOTIF,
		       iwl_mvm_rx_session_protect_notif, RX_HANDLER_SYNC),
	RX_HANDLER(MCC_CHUB_UPDATE_CMD, iwl_mvm_rx_chub_update_mcc,
		   RX_HANDLER_ASYNC_LOCKED),

	RX_HANDLER(EOSP_NOTIFICATION, iwl_mvm_rx_eosp_notif, RX_HANDLER_SYNC),

	RX_HANDLER(SCAN_ITERATION_COMPLETE,
		   iwl_mvm_rx_lmac_scan_iter_complete_notif, RX_HANDLER_SYNC),
	RX_HANDLER(SCAN_OFFLOAD_COMPLETE,
		   iwl_mvm_rx_lmac_scan_complete_notif,
		   RX_HANDLER_ASYNC_LOCKED),
	RX_HANDLER(MATCH_FOUND_NOTIFICATION, iwl_mvm_rx_scan_match_found,
		   RX_HANDLER_SYNC),
	RX_HANDLER(SCAN_COMPLETE_UMAC, iwl_mvm_rx_umac_scan_complete_notif,
		   RX_HANDLER_ASYNC_LOCKED),
	RX_HANDLER(SCAN_ITERATION_COMPLETE_UMAC,
		   iwl_mvm_rx_umac_scan_iter_complete_notif, RX_HANDLER_SYNC),

	RX_HANDLER(CARD_STATE_NOTIFICATION, iwl_mvm_rx_card_state_notif,
		   RX_HANDLER_SYNC),

	RX_HANDLER(MISSED_BEACONS_NOTIFICATION, iwl_mvm_rx_missed_beacons_notif,
		   RX_HANDLER_SYNC),

	RX_HANDLER(REPLY_ERROR, iwl_mvm_rx_fw_error, RX_HANDLER_SYNC),
	RX_HANDLER(PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION,
		   iwl_mvm_power_uapsd_misbehaving_ap_notif, RX_HANDLER_SYNC),
	RX_HANDLER(DTS_MEASUREMENT_NOTIFICATION, iwl_mvm_temp_notif,
		   RX_HANDLER_ASYNC_LOCKED),
	RX_HANDLER_GRP(PHY_OPS_GROUP, DTS_MEASUREMENT_NOTIF_WIDE,
		       iwl_mvm_temp_notif, RX_HANDLER_ASYNC_UNLOCKED),
	RX_HANDLER_GRP(PHY_OPS_GROUP, CT_KILL_NOTIFICATION,
		       iwl_mvm_ct_kill_notif, RX_HANDLER_SYNC),

	RX_HANDLER(TDLS_CHANNEL_SWITCH_NOTIFICATION, iwl_mvm_rx_tdls_notif,
		   RX_HANDLER_ASYNC_LOCKED),
	RX_HANDLER(MFUART_LOAD_NOTIFICATION, iwl_mvm_rx_mfuart_notif,
		   RX_HANDLER_SYNC),
	RX_HANDLER_GRP(LOCATION_GROUP, TOF_RESPONDER_STATS,
		       iwl_mvm_ftm_responder_stats, RX_HANDLER_ASYNC_LOCKED),

	RX_HANDLER_GRP(LOCATION_GROUP, TOF_RANGE_RESPONSE_NOTIF,
		       iwl_mvm_ftm_range_resp, RX_HANDLER_ASYNC_LOCKED),
	RX_HANDLER_GRP(LOCATION_GROUP, TOF_LC_NOTIF,
		       iwl_mvm_ftm_lc_notif, RX_HANDLER_ASYNC_LOCKED),

	RX_HANDLER_GRP(DEBUG_GROUP, MFU_ASSERT_DUMP_NTF,
		       iwl_mvm_mfu_assert_dump_notif, RX_HANDLER_SYNC),
	RX_HANDLER_GRP(PROT_OFFLOAD_GROUP, STORED_BEACON_NTF,
		       iwl_mvm_rx_stored_beacon_notif, RX_HANDLER_SYNC),
	RX_HANDLER_GRP(DATA_PATH_GROUP, MU_GROUP_MGMT_NOTIF,
		       iwl_mvm_mu_mimo_grp_notif, RX_HANDLER_SYNC),
	RX_HANDLER_GRP(DATA_PATH_GROUP, STA_PM_NOTIF,
		       iwl_mvm_sta_pm_notif, RX_HANDLER_SYNC),
};
#undef RX_HANDLER
#undef RX_HANDLER_GRP

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_legacy_names[] = {
	HCMD_NAME(UCODE_ALIVE_NTFY),
	HCMD_NAME(REPLY_ERROR),
	HCMD_NAME(ECHO_CMD),
	HCMD_NAME(INIT_COMPLETE_NOTIF),
	HCMD_NAME(PHY_CONTEXT_CMD),
	HCMD_NAME(DBG_CFG),
	HCMD_NAME(SCAN_CFG_CMD),
	HCMD_NAME(SCAN_REQ_UMAC),
	HCMD_NAME(SCAN_ABORT_UMAC),
	HCMD_NAME(SCAN_COMPLETE_UMAC),
	HCMD_NAME(BA_WINDOW_STATUS_NOTIFICATION_ID),
	HCMD_NAME(ADD_STA_KEY),
	HCMD_NAME(ADD_STA),
	HCMD_NAME(REMOVE_STA),
	HCMD_NAME(FW_GET_ITEM_CMD),
	HCMD_NAME(TX_CMD),
	HCMD_NAME(SCD_QUEUE_CFG),
	HCMD_NAME(TXPATH_FLUSH),
	HCMD_NAME(MGMT_MCAST_KEY),
	HCMD_NAME(WEP_KEY),
	HCMD_NAME(SHARED_MEM_CFG),
	HCMD_NAME(TDLS_CHANNEL_SWITCH_CMD),
	HCMD_NAME(MAC_CONTEXT_CMD),
	HCMD_NAME(TIME_EVENT_CMD),
	HCMD_NAME(TIME_EVENT_NOTIFICATION),
	HCMD_NAME(BINDING_CONTEXT_CMD),
	HCMD_NAME(TIME_QUOTA_CMD),
	HCMD_NAME(NON_QOS_TX_COUNTER_CMD),
	HCMD_NAME(LEDS_CMD),
	HCMD_NAME(LQ_CMD),
	HCMD_NAME(FW_PAGING_BLOCK_CMD),
	HCMD_NAME(SCAN_OFFLOAD_REQUEST_CMD),
	HCMD_NAME(SCAN_OFFLOAD_ABORT_CMD),
	HCMD_NAME(HOT_SPOT_CMD),
	HCMD_NAME(SCAN_OFFLOAD_PROFILES_QUERY_CMD),
	HCMD_NAME(BT_COEX_UPDATE_REDUCED_TXP),
	HCMD_NAME(BT_COEX_CI),
	HCMD_NAME(PHY_CONFIGURATION_CMD),
	HCMD_NAME(CALIB_RES_NOTIF_PHY_DB),
	HCMD_NAME(PHY_DB_CMD),
	HCMD_NAME(SCAN_OFFLOAD_COMPLETE),
	HCMD_NAME(SCAN_OFFLOAD_UPDATE_PROFILES_CMD),
	HCMD_NAME(POWER_TABLE_CMD),
	HCMD_NAME(PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION),
	HCMD_NAME(REPLY_THERMAL_MNG_BACKOFF),
	HCMD_NAME(DC2DC_CONFIG_CMD),
	HCMD_NAME(NVM_ACCESS_CMD),
	HCMD_NAME(BEACON_NOTIFICATION),
	HCMD_NAME(BEACON_TEMPLATE_CMD),
	HCMD_NAME(TX_ANT_CONFIGURATION_CMD),
	HCMD_NAME(BT_CONFIG),
	HCMD_NAME(STATISTICS_CMD),
	HCMD_NAME(STATISTICS_NOTIFICATION),
	HCMD_NAME(EOSP_NOTIFICATION),
	HCMD_NAME(REDUCE_TX_POWER_CMD),
	HCMD_NAME(CARD_STATE_NOTIFICATION),
	HCMD_NAME(MISSED_BEACONS_NOTIFICATION),
	HCMD_NAME(TDLS_CONFIG_CMD),
	HCMD_NAME(MAC_PM_POWER_TABLE),
	HCMD_NAME(TDLS_CHANNEL_SWITCH_NOTIFICATION),
	HCMD_NAME(MFUART_LOAD_NOTIFICATION),
	HCMD_NAME(RSS_CONFIG_CMD),
	HCMD_NAME(SCAN_ITERATION_COMPLETE_UMAC),
	HCMD_NAME(REPLY_RX_PHY_CMD),
	HCMD_NAME(REPLY_RX_MPDU_CMD),
	HCMD_NAME(BAR_FRAME_RELEASE),
	HCMD_NAME(FRAME_RELEASE),
	HCMD_NAME(BA_NOTIF),
	HCMD_NAME(MCC_UPDATE_CMD),
	HCMD_NAME(MCC_CHUB_UPDATE_CMD),
	HCMD_NAME(MARKER_CMD),
	HCMD_NAME(BT_PROFILE_NOTIFICATION),
	HCMD_NAME(BCAST_FILTER_CMD),
	HCMD_NAME(MCAST_FILTER_CMD),
	HCMD_NAME(REPLY_SF_CFG_CMD),
	HCMD_NAME(REPLY_BEACON_FILTERING_CMD),
	HCMD_NAME(D3_CONFIG_CMD),
	HCMD_NAME(PROT_OFFLOAD_CONFIG_CMD),
	HCMD_NAME(OFFLOADS_QUERY_CMD),
	HCMD_NAME(REMOTE_WAKE_CONFIG_CMD),
	HCMD_NAME(MATCH_FOUND_NOTIFICATION),
	HCMD_NAME(DTS_MEASUREMENT_NOTIFICATION),
	HCMD_NAME(WOWLAN_PATTERNS),
	HCMD_NAME(WOWLAN_CONFIGURATION),
	HCMD_NAME(WOWLAN_TSC_RSC_PARAM),
	HCMD_NAME(WOWLAN_TKIP_PARAM),
	HCMD_NAME(WOWLAN_KEK_KCK_MATERIAL),
	HCMD_NAME(WOWLAN_GET_STATUSES),
	HCMD_NAME(SCAN_ITERATION_COMPLETE),
	HCMD_NAME(D0I3_END_CMD),
	HCMD_NAME(LTR_CONFIG),
	HCMD_NAME(LDBG_CONFIG_CMD),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_system_names[] = {
	HCMD_NAME(SHARED_MEM_CFG_CMD),
	HCMD_NAME(INIT_EXTENDED_CFG_CMD),
	HCMD_NAME(FW_ERROR_RECOVERY_CMD),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_mac_conf_names[] = {
	HCMD_NAME(CHANNEL_SWITCH_TIME_EVENT_CMD),
	HCMD_NAME(SESSION_PROTECTION_CMD),
	HCMD_NAME(SESSION_PROTECTION_NOTIF),
	HCMD_NAME(CHANNEL_SWITCH_NOA_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_phy_names[] = {
	HCMD_NAME(CMD_DTS_MEASUREMENT_TRIGGER_WIDE),
	HCMD_NAME(CTDP_CONFIG_CMD),
	HCMD_NAME(TEMP_REPORTING_THRESHOLDS_CMD),
	HCMD_NAME(GEO_TX_POWER_LIMIT),
	HCMD_NAME(CT_KILL_NOTIFICATION),
	HCMD_NAME(DTS_MEASUREMENT_NOTIF_WIDE),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_data_path_names[] = {
	HCMD_NAME(DQA_ENABLE_CMD),
	HCMD_NAME(UPDATE_MU_GROUPS_CMD),
	HCMD_NAME(TRIGGER_RX_QUEUES_NOTIF_CMD),
	HCMD_NAME(STA_HE_CTXT_CMD),
	HCMD_NAME(RFH_QUEUE_CONFIG_CMD),
	HCMD_NAME(TLC_MNG_CONFIG_CMD),
	HCMD_NAME(CHEST_COLLECTOR_FILTER_CONFIG_CMD),
	HCMD_NAME(STA_PM_NOTIF),
	HCMD_NAME(MU_GROUP_MGMT_NOTIF),
	HCMD_NAME(RX_QUEUES_NOTIFICATION),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_debug_names[] = {
	HCMD_NAME(DBGC_SUSPEND_RESUME),
	HCMD_NAME(BUFFER_ALLOCATION),
	HCMD_NAME(MFU_ASSERT_DUMP_NTF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_location_names[] = {
	HCMD_NAME(TOF_RANGE_REQ_CMD),
	HCMD_NAME(TOF_CONFIG_CMD),
	HCMD_NAME(TOF_RANGE_ABORT_CMD),
	HCMD_NAME(TOF_RANGE_REQ_EXT_CMD),
	HCMD_NAME(TOF_RESPONDER_CONFIG_CMD),
	HCMD_NAME(TOF_RESPONDER_DYN_CONFIG_CMD),
	HCMD_NAME(TOF_LC_NOTIF),
	HCMD_NAME(TOF_RESPONDER_STATS),
	HCMD_NAME(TOF_MCSI_DEBUG_NOTIF),
	HCMD_NAME(TOF_RANGE_RESPONSE_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_prot_offload_names[] = {
	HCMD_NAME(STORED_BEACON_NTF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_regulatory_and_nvm_names[] = {
	HCMD_NAME(NVM_ACCESS_COMPLETE),
	HCMD_NAME(NVM_GET_INFO),
	HCMD_NAME(TAS_CONFIG),
};

static const struct iwl_hcmd_arr iwl_mvm_groups[] = {
	[LEGACY_GROUP] = HCMD_ARR(iwl_mvm_legacy_names),
	[LONG_GROUP] = HCMD_ARR(iwl_mvm_legacy_names),
	[SYSTEM_GROUP] = HCMD_ARR(iwl_mvm_system_names),
	[MAC_CONF_GROUP] = HCMD_ARR(iwl_mvm_mac_conf_names),
	[PHY_OPS_GROUP] = HCMD_ARR(iwl_mvm_phy_names),
	[DATA_PATH_GROUP] = HCMD_ARR(iwl_mvm_data_path_names),
	[LOCATION_GROUP] = HCMD_ARR(iwl_mvm_location_names),
	[PROT_OFFLOAD_GROUP] = HCMD_ARR(iwl_mvm_prot_offload_names),
	[REGULATORY_AND_NVM_GROUP] =
		HCMD_ARR(iwl_mvm_regulatory_and_nvm_names),
};

/* this forward declaration can avoid to export the function */
static void iwl_mvm_async_handlers_wk(struct work_struct *wk);

static u32 iwl_mvm_min_backoff(struct iwl_mvm *mvm)
{
	const struct iwl_pwr_tx_backoff *backoff = mvm->cfg->pwr_tx_backoffs;
	u64 dflt_pwr_limit;

	if (!backoff)
		return 0;

	dflt_pwr_limit = iwl_acpi_get_pwr_limit(mvm->dev);

	while (backoff->pwr) {
		if (dflt_pwr_limit >= backoff->pwr)
			return backoff->backoff;

		backoff++;
	}

	return 0;
}

static void iwl_mvm_tx_unblock_dwork(struct work_struct *work)
{
	struct iwl_mvm *mvm =
		container_of(work, struct iwl_mvm, cs_tx_unblock_dwork.work);
	struct ieee80211_vif *tx_blocked_vif;
	struct iwl_mvm_vif *mvmvif;

	mutex_lock(&mvm->mutex);

	tx_blocked_vif =
		rcu_dereference_protected(mvm->csa_tx_blocked_vif,
					  lockdep_is_held(&mvm->mutex));

	if (!tx_blocked_vif)
		goto unlock;

	mvmvif = iwl_mvm_vif_from_mac80211(tx_blocked_vif);
	iwl_mvm_modify_all_sta_disable_tx(mvm, mvmvif, false);
	RCU_INIT_POINTER(mvm->csa_tx_blocked_vif, NULL);
unlock:
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_fwrt_dump_start(void *ctx)
{
	struct iwl_mvm *mvm = ctx;

	mutex_lock(&mvm->mutex);

	return 0;
}

static void iwl_mvm_fwrt_dump_end(void *ctx)
{
	struct iwl_mvm *mvm = ctx;

	mutex_unlock(&mvm->mutex);
}

static bool iwl_mvm_fwrt_fw_running(void *ctx)
{
	return iwl_mvm_firmware_running(ctx);
}

static int iwl_mvm_fwrt_send_hcmd(void *ctx, struct iwl_host_cmd *host_cmd)
{
	struct iwl_mvm *mvm = (struct iwl_mvm *)ctx;
	int ret;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd(mvm, host_cmd);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static bool iwl_mvm_d3_debug_enable(void *ctx)
{
	return IWL_MVM_D3_DEBUG;
}

static const struct iwl_fw_runtime_ops iwl_mvm_fwrt_ops = {
	.dump_start = iwl_mvm_fwrt_dump_start,
	.dump_end = iwl_mvm_fwrt_dump_end,
	.fw_running = iwl_mvm_fwrt_fw_running,
	.send_hcmd = iwl_mvm_fwrt_send_hcmd,
	.d3_debug_enable = iwl_mvm_d3_debug_enable,
};

static struct iwl_op_mode *
iwl_op_mode_mvm_start(struct iwl_trans *trans, const struct iwl_cfg *cfg,
		      const struct iwl_fw *fw, struct dentry *dbgfs_dir)
{
	struct ieee80211_hw *hw;
	struct iwl_op_mode *op_mode;
	struct iwl_mvm *mvm;
	struct iwl_trans_config trans_cfg = {};
	static const u8 no_reclaim_cmds[] = {
		TX_CMD,
	};
	int err, scan_size;
	u32 min_backoff;
	enum iwl_amsdu_size rb_size_default;

	/*
	 * We use IWL_MVM_STATION_COUNT to check the validity of the station
	 * index all over the driver - check that its value corresponds to the
	 * array size.
	 */
	BUILD_BUG_ON(ARRAY_SIZE(mvm->fw_id_to_mac_id) != IWL_MVM_STATION_COUNT);

	/********************************
	 * 1. Allocating and configuring HW data
	 ********************************/
	hw = ieee80211_alloc_hw(sizeof(struct iwl_op_mode) +
				sizeof(struct iwl_mvm),
				&iwl_mvm_hw_ops);
	if (!hw)
		return NULL;

	hw->max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF;

	if (cfg->max_tx_agg_size)
		hw->max_tx_aggregation_subframes = cfg->max_tx_agg_size;
	else
		hw->max_tx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF;

	op_mode = hw->priv;

	mvm = IWL_OP_MODE_GET_MVM(op_mode);
	mvm->dev = trans->dev;
	mvm->trans = trans;
	mvm->cfg = cfg;
	mvm->fw = fw;
	mvm->hw = hw;

	iwl_fw_runtime_init(&mvm->fwrt, trans, fw, &iwl_mvm_fwrt_ops, mvm,
			    dbgfs_dir);

	mvm->init_status = 0;

	if (iwl_mvm_has_new_rx_api(mvm)) {
		op_mode->ops = &iwl_mvm_ops_mq;
		trans->rx_mpdu_cmd_hdr_size =
			(trans->trans_cfg->device_family >=
			 IWL_DEVICE_FAMILY_AX210) ?
			sizeof(struct iwl_rx_mpdu_desc) :
			IWL_RX_DESC_SIZE_V1;
	} else {
		op_mode->ops = &iwl_mvm_ops;
		trans->rx_mpdu_cmd_hdr_size =
			sizeof(struct iwl_rx_mpdu_res_start);

		if (WARN_ON(trans->num_rx_queues > 1))
			goto out_free;
	}

	mvm->fw_restart = iwlwifi_mod_params.fw_restart ? -1 : 0;

	mvm->aux_queue = IWL_MVM_DQA_AUX_QUEUE;
	mvm->snif_queue = IWL_MVM_DQA_INJECT_MONITOR_QUEUE;
	mvm->probe_queue = IWL_MVM_DQA_AP_PROBE_RESP_QUEUE;
	mvm->p2p_dev_queue = IWL_MVM_DQA_P2P_DEVICE_QUEUE;

	mvm->sf_state = SF_UNINIT;
	if (iwl_mvm_has_unified_ucode(mvm))
		iwl_fw_set_current_image(&mvm->fwrt, IWL_UCODE_REGULAR);
	else
		iwl_fw_set_current_image(&mvm->fwrt, IWL_UCODE_INIT);
	mvm->drop_bcn_ap_mode = true;

	mutex_init(&mvm->mutex);
	spin_lock_init(&mvm->async_handlers_lock);
	INIT_LIST_HEAD(&mvm->time_event_list);
	INIT_LIST_HEAD(&mvm->aux_roc_te_list);
	INIT_LIST_HEAD(&mvm->async_handlers_list);
	spin_lock_init(&mvm->time_event_lock);
	INIT_LIST_HEAD(&mvm->ftm_initiator.loc_list);
	INIT_LIST_HEAD(&mvm->ftm_initiator.pasn_list);
	INIT_LIST_HEAD(&mvm->resp_pasn_list);

	INIT_WORK(&mvm->async_handlers_wk, iwl_mvm_async_handlers_wk);
	INIT_WORK(&mvm->roc_done_wk, iwl_mvm_roc_done_wk);
	INIT_DELAYED_WORK(&mvm->tdls_cs.dwork, iwl_mvm_tdls_ch_switch_work);
	INIT_DELAYED_WORK(&mvm->scan_timeout_dwork, iwl_mvm_scan_timeout_wk);
	INIT_WORK(&mvm->add_stream_wk, iwl_mvm_add_new_dqa_stream_wk);
	INIT_LIST_HEAD(&mvm->add_stream_txqs);

	init_waitqueue_head(&mvm->rx_sync_waitq);

	atomic_set(&mvm->queue_sync_counter, 0);

	SET_IEEE80211_DEV(mvm->hw, mvm->trans->dev);

	spin_lock_init(&mvm->tcm.lock);
	INIT_DELAYED_WORK(&mvm->tcm.work, iwl_mvm_tcm_work);
	mvm->tcm.ts = jiffies;
	mvm->tcm.ll_ts = jiffies;
	mvm->tcm.uapsd_nonagg_ts = jiffies;

	INIT_DELAYED_WORK(&mvm->cs_tx_unblock_dwork, iwl_mvm_tx_unblock_dwork);

	mvm->cmd_ver.d0i3_resp =
		iwl_fw_lookup_notif_ver(mvm->fw, LEGACY_GROUP, D0I3_END_CMD,
					0);
	/* we only support version 1 */
	if (WARN_ON_ONCE(mvm->cmd_ver.d0i3_resp > 1))
		goto out_free;

	mvm->cmd_ver.range_resp =
		iwl_fw_lookup_notif_ver(mvm->fw, LOCATION_GROUP,
					TOF_RANGE_RESPONSE_NOTIF, 5);
	/* we only support up to version 8 */
	if (WARN_ON_ONCE(mvm->cmd_ver.range_resp > 8))
		goto out_free;

	/*
	 * Populate the state variables that the transport layer needs
	 * to know about.
	 */
	trans_cfg.op_mode = op_mode;
	trans_cfg.no_reclaim_cmds = no_reclaim_cmds;
	trans_cfg.n_no_reclaim_cmds = ARRAY_SIZE(no_reclaim_cmds);

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210)
		rb_size_default = IWL_AMSDU_2K;
	else
		rb_size_default = IWL_AMSDU_4K;

	switch (iwlwifi_mod_params.amsdu_size) {
	case IWL_AMSDU_DEF:
		trans_cfg.rx_buf_size = rb_size_default;
		break;
	case IWL_AMSDU_4K:
		trans_cfg.rx_buf_size = IWL_AMSDU_4K;
		break;
	case IWL_AMSDU_8K:
		trans_cfg.rx_buf_size = IWL_AMSDU_8K;
		break;
	case IWL_AMSDU_12K:
		trans_cfg.rx_buf_size = IWL_AMSDU_12K;
		break;
	default:
		pr_err("%s: Unsupported amsdu_size: %d\n", KBUILD_MODNAME,
		       iwlwifi_mod_params.amsdu_size);
		trans_cfg.rx_buf_size = rb_size_default;
	}

	trans_cfg.bc_table_dword =
		mvm->trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210;

	trans_cfg.command_groups = iwl_mvm_groups;
	trans_cfg.command_groups_size = ARRAY_SIZE(iwl_mvm_groups);

	trans_cfg.cmd_queue = IWL_MVM_DQA_CMD_QUEUE;
	trans_cfg.cmd_fifo = IWL_MVM_TX_FIFO_CMD;
	trans_cfg.scd_set_active = true;

	trans_cfg.cb_data_offs = offsetof(struct ieee80211_tx_info,
					  driver_data[2]);

	trans_cfg.sw_csum_tx = IWL_MVM_SW_TX_CSUM_OFFLOAD;

	/* Set a short watchdog for the command queue */
	trans_cfg.cmd_q_wdg_timeout =
		iwl_mvm_get_wd_timeout(mvm, NULL, false, true);

	snprintf(mvm->hw->wiphy->fw_version,
		 sizeof(mvm->hw->wiphy->fw_version),
		 "%s", fw->fw_version);

	/* Configure transport layer */
	iwl_trans_configure(mvm->trans, &trans_cfg);

	trans->rx_mpdu_cmd = REPLY_RX_MPDU_CMD;
	trans->dbg.dest_tlv = mvm->fw->dbg.dest_tlv;
	trans->dbg.n_dest_reg = mvm->fw->dbg.n_dest_reg;
	memcpy(trans->dbg.conf_tlv, mvm->fw->dbg.conf_tlv,
	       sizeof(trans->dbg.conf_tlv));
	trans->dbg.trigger_tlv = mvm->fw->dbg.trigger_tlv;

	trans->iml = mvm->fw->iml;
	trans->iml_len = mvm->fw->iml_len;

	/* set up notification wait support */
	iwl_notification_wait_init(&mvm->notif_wait);

	/* Init phy db */
	mvm->phy_db = iwl_phy_db_init(trans);
	if (!mvm->phy_db) {
		IWL_ERR(mvm, "Cannot init phy_db\n");
		goto out_free;
	}

	IWL_INFO(mvm, "Detected %s, REV=0x%X\n",
		 mvm->trans->name, mvm->trans->hw_rev);

	if (iwlwifi_mod_params.nvm_file)
		mvm->nvm_file_name = iwlwifi_mod_params.nvm_file;
	else
		IWL_DEBUG_EEPROM(mvm->trans->dev,
				 "working without external nvm file\n");

	err = iwl_trans_start_hw(mvm->trans);
	if (err)
		goto out_free;

	mutex_lock(&mvm->mutex);
	err = iwl_run_init_mvm_ucode(mvm, true);
	if (err && err != -ERFKILL)
		iwl_fw_dbg_error_collect(&mvm->fwrt, FW_DBG_TRIGGER_DRIVER);
	if (!iwlmvm_mod_params.init_dbg || !err)
		iwl_mvm_stop_device(mvm);
	mutex_unlock(&mvm->mutex);
	if (err < 0) {
		IWL_ERR(mvm, "Failed to run INIT ucode: %d\n", err);
		goto out_free;
	}

	scan_size = iwl_mvm_scan_size(mvm);

	mvm->scan_cmd = kmalloc(scan_size, GFP_KERNEL);
	if (!mvm->scan_cmd)
		goto out_free;

	/* Set EBS as successful as long as not stated otherwise by the FW. */
	mvm->last_ebs_successful = true;

	err = iwl_mvm_mac_setup_register(mvm);
	if (err)
		goto out_free;
	mvm->hw_registered = true;

	min_backoff = iwl_mvm_min_backoff(mvm);
	iwl_mvm_thermal_initialize(mvm, min_backoff);

	iwl_mvm_dbgfs_register(mvm, dbgfs_dir);

	if (!iwl_mvm_has_new_rx_stats_api(mvm))
		memset(&mvm->rx_stats_v3, 0,
		       sizeof(struct mvm_statistics_rx_v3));
	else
		memset(&mvm->rx_stats, 0, sizeof(struct mvm_statistics_rx));

	iwl_mvm_toggle_tx_ant(mvm, &mvm->mgmt_last_antenna_idx);

	return op_mode;

 out_free:
	iwl_fw_flush_dumps(&mvm->fwrt);
	iwl_fw_runtime_free(&mvm->fwrt);

	if (iwlmvm_mod_params.init_dbg)
		return op_mode;
	iwl_phy_db_free(mvm->phy_db);
	kfree(mvm->scan_cmd);
	iwl_trans_op_mode_leave(trans);

	ieee80211_free_hw(mvm->hw);
	return NULL;
}

static void iwl_op_mode_mvm_stop(struct iwl_op_mode *op_mode)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	int i;

	iwl_mvm_leds_exit(mvm);

	iwl_mvm_thermal_exit(mvm);

	ieee80211_unregister_hw(mvm->hw);

	kfree(mvm->scan_cmd);
	kfree(mvm->mcast_filter_cmd);
	mvm->mcast_filter_cmd = NULL;

	kfree(mvm->error_recovery_buf);
	mvm->error_recovery_buf = NULL;

	iwl_trans_op_mode_leave(mvm->trans);

	iwl_phy_db_free(mvm->phy_db);
	mvm->phy_db = NULL;

	kfree(mvm->nvm_data);
	for (i = 0; i < NVM_MAX_NUM_SECTIONS; i++)
		kfree(mvm->nvm_sections[i].data);

	cancel_delayed_work_sync(&mvm->tcm.work);

	iwl_fw_runtime_free(&mvm->fwrt);
	mutex_destroy(&mvm->mutex);

	ieee80211_free_hw(mvm->hw);
}

struct iwl_async_handler_entry {
	struct list_head list;
	struct iwl_rx_cmd_buffer rxb;
	enum iwl_rx_handler_context context;
	void (*fn)(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
};

void iwl_mvm_async_handlers_purge(struct iwl_mvm *mvm)
{
	struct iwl_async_handler_entry *entry, *tmp;

	spin_lock_bh(&mvm->async_handlers_lock);
	list_for_each_entry_safe(entry, tmp, &mvm->async_handlers_list, list) {
		iwl_free_rxb(&entry->rxb);
		list_del(&entry->list);
		kfree(entry);
	}
	spin_unlock_bh(&mvm->async_handlers_lock);
}

static void iwl_mvm_async_handlers_wk(struct work_struct *wk)
{
	struct iwl_mvm *mvm =
		container_of(wk, struct iwl_mvm, async_handlers_wk);
	struct iwl_async_handler_entry *entry, *tmp;
	LIST_HEAD(local_list);

	/* Ensure that we are not in stop flow (check iwl_mvm_mac_stop) */

	/*
	 * Sync with Rx path with a lock. Remove all the entries from this list,
	 * add them to a local one (lock free), and then handle them.
	 */
	spin_lock_bh(&mvm->async_handlers_lock);
	list_splice_init(&mvm->async_handlers_list, &local_list);
	spin_unlock_bh(&mvm->async_handlers_lock);

	list_for_each_entry_safe(entry, tmp, &local_list, list) {
		if (entry->context == RX_HANDLER_ASYNC_LOCKED)
			mutex_lock(&mvm->mutex);
		entry->fn(mvm, &entry->rxb);
		iwl_free_rxb(&entry->rxb);
		list_del(&entry->list);
		if (entry->context == RX_HANDLER_ASYNC_LOCKED)
			mutex_unlock(&mvm->mutex);
		kfree(entry);
	}
}

static inline void iwl_mvm_rx_check_trigger(struct iwl_mvm *mvm,
					    struct iwl_rx_packet *pkt)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_cmd *cmds_trig;
	int i;

	trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, NULL,
				     FW_DBG_TRIGGER_FW_NOTIF);
	if (!trig)
		return;

	cmds_trig = (void *)trig->data;

	for (i = 0; i < ARRAY_SIZE(cmds_trig->cmds); i++) {
		/* don't collect on CMD 0 */
		if (!cmds_trig->cmds[i].cmd_id)
			break;

		if (cmds_trig->cmds[i].cmd_id != pkt->hdr.cmd ||
		    cmds_trig->cmds[i].group_id != pkt->hdr.group_id)
			continue;

		iwl_fw_dbg_collect_trig(&mvm->fwrt, trig,
					"CMD 0x%02x.%02x received",
					pkt->hdr.group_id, pkt->hdr.cmd);
		break;
	}
}

static void iwl_mvm_rx_common(struct iwl_mvm *mvm,
			      struct iwl_rx_cmd_buffer *rxb,
			      struct iwl_rx_packet *pkt)
{
	int i;
	union iwl_dbg_tlv_tp_data tp_data = { .fw_pkt = pkt };

	iwl_dbg_tlv_time_point(&mvm->fwrt,
			       IWL_FW_INI_TIME_POINT_FW_RSP_OR_NOTIF, &tp_data);
	iwl_mvm_rx_check_trigger(mvm, pkt);

	/*
	 * Do the notification wait before RX handlers so
	 * even if the RX handler consumes the RXB we have
	 * access to it in the notification wait entry.
	 */
	iwl_notification_wait_notify(&mvm->notif_wait, pkt);

	for (i = 0; i < ARRAY_SIZE(iwl_mvm_rx_handlers); i++) {
		const struct iwl_rx_handlers *rx_h = &iwl_mvm_rx_handlers[i];
		struct iwl_async_handler_entry *entry;

		if (rx_h->cmd_id != WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd))
			continue;

		if (rx_h->context == RX_HANDLER_SYNC) {
			rx_h->fn(mvm, rxb);
			return;
		}

		entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
		/* we can't do much... */
		if (!entry)
			return;

		entry->rxb._page = rxb_steal_page(rxb);
		entry->rxb._offset = rxb->_offset;
		entry->rxb._rx_page_order = rxb->_rx_page_order;
		entry->fn = rx_h->fn;
		entry->context = rx_h->context;
		spin_lock(&mvm->async_handlers_lock);
		list_add_tail(&entry->list, &mvm->async_handlers_list);
		spin_unlock(&mvm->async_handlers_lock);
		schedule_work(&mvm->async_handlers_wk);
		break;
	}
}

static void iwl_mvm_rx(struct iwl_op_mode *op_mode,
		       struct napi_struct *napi,
		       struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	u16 cmd = WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd);

	if (likely(cmd == WIDE_ID(LEGACY_GROUP, REPLY_RX_MPDU_CMD)))
		iwl_mvm_rx_rx_mpdu(mvm, napi, rxb);
	else if (cmd == WIDE_ID(LEGACY_GROUP, REPLY_RX_PHY_CMD))
		iwl_mvm_rx_rx_phy_cmd(mvm, rxb);
	else
		iwl_mvm_rx_common(mvm, rxb, pkt);
}

static void iwl_mvm_rx_mq(struct iwl_op_mode *op_mode,
			  struct napi_struct *napi,
			  struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	u16 cmd = WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd);

	if (likely(cmd == WIDE_ID(LEGACY_GROUP, REPLY_RX_MPDU_CMD)))
		iwl_mvm_rx_mpdu_mq(mvm, napi, rxb, 0);
	else if (unlikely(cmd == WIDE_ID(DATA_PATH_GROUP,
					 RX_QUEUES_NOTIFICATION)))
		iwl_mvm_rx_queue_notif(mvm, napi, rxb, 0);
	else if (cmd == WIDE_ID(LEGACY_GROUP, FRAME_RELEASE))
		iwl_mvm_rx_frame_release(mvm, napi, rxb, 0);
	else if (cmd == WIDE_ID(LEGACY_GROUP, BAR_FRAME_RELEASE))
		iwl_mvm_rx_bar_frame_release(mvm, napi, rxb, 0);
	else if (cmd == WIDE_ID(DATA_PATH_GROUP, RX_NO_DATA_NOTIF))
		iwl_mvm_rx_monitor_no_data(mvm, napi, rxb, 0);
	else
		iwl_mvm_rx_common(mvm, rxb, pkt);
}

static void iwl_mvm_async_cb(struct iwl_op_mode *op_mode,
			     const struct iwl_device_cmd *cmd)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	/*
	 * For now, we only set the CMD_WANT_ASYNC_CALLBACK for ADD_STA
	 * commands that need to block the Tx queues.
	 */
	iwl_trans_block_txq_ptrs(mvm->trans, false);
}

static int iwl_mvm_is_static_queue(struct iwl_mvm *mvm, int queue)
{
	return queue == mvm->aux_queue || queue == mvm->probe_queue ||
		queue == mvm->p2p_dev_queue || queue == mvm->snif_queue;
}

static void iwl_mvm_queue_state_change(struct iwl_op_mode *op_mode,
				       int hw_queue, bool start)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	struct ieee80211_sta *sta;
	struct ieee80211_txq *txq;
	struct iwl_mvm_txq *mvmtxq;
	int i;
	unsigned long tid_bitmap;
	struct iwl_mvm_sta *mvmsta;
	u8 sta_id;

	sta_id = iwl_mvm_has_new_tx_api(mvm) ?
		mvm->tvqm_info[hw_queue].sta_id :
		mvm->queue_info[hw_queue].ra_sta_id;

	if (WARN_ON_ONCE(sta_id >= ARRAY_SIZE(mvm->fw_id_to_mac_id)))
		return;

	rcu_read_lock();

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);
	if (IS_ERR_OR_NULL(sta))
		goto out;
	mvmsta = iwl_mvm_sta_from_mac80211(sta);

	if (iwl_mvm_is_static_queue(mvm, hw_queue)) {
		if (!start)
			ieee80211_stop_queues(mvm->hw);
		else if (mvmsta->sta_state != IEEE80211_STA_NOTEXIST)
			ieee80211_wake_queues(mvm->hw);

		goto out;
	}

	if (iwl_mvm_has_new_tx_api(mvm)) {
		int tid = mvm->tvqm_info[hw_queue].txq_tid;

		tid_bitmap = BIT(tid);
	} else {
		tid_bitmap = mvm->queue_info[hw_queue].tid_bitmap;
	}

	for_each_set_bit(i, &tid_bitmap, IWL_MAX_TID_COUNT + 1) {
		int tid = i;

		if (tid == IWL_MAX_TID_COUNT)
			tid = IEEE80211_NUM_TIDS;

		txq = sta->txq[tid];
		mvmtxq = iwl_mvm_txq_from_mac80211(txq);
		mvmtxq->stopped = !start;

		if (start && mvmsta->sta_state != IEEE80211_STA_NOTEXIST)
			iwl_mvm_mac_itxq_xmit(mvm->hw, txq);
	}

out:
	rcu_read_unlock();
}

static void iwl_mvm_stop_sw_queue(struct iwl_op_mode *op_mode, int hw_queue)
{
	iwl_mvm_queue_state_change(op_mode, hw_queue, false);
}

static void iwl_mvm_wake_sw_queue(struct iwl_op_mode *op_mode, int hw_queue)
{
	iwl_mvm_queue_state_change(op_mode, hw_queue, true);
}

static void iwl_mvm_set_rfkill_state(struct iwl_mvm *mvm)
{
	bool state = iwl_mvm_is_radio_killed(mvm);

	if (state)
		wake_up(&mvm->rx_sync_waitq);

	wiphy_rfkill_set_hw_state(mvm->hw->wiphy, state);
}

void iwl_mvm_set_hw_ctkill_state(struct iwl_mvm *mvm, bool state)
{
	if (state)
		set_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status);
	else
		clear_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status);

	iwl_mvm_set_rfkill_state(mvm);
}

static bool iwl_mvm_set_hw_rfkill_state(struct iwl_op_mode *op_mode, bool state)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	bool rfkill_safe_init_done = READ_ONCE(mvm->rfkill_safe_init_done);
	bool unified = iwl_mvm_has_unified_ucode(mvm);

	if (state)
		set_bit(IWL_MVM_STATUS_HW_RFKILL, &mvm->status);
	else
		clear_bit(IWL_MVM_STATUS_HW_RFKILL, &mvm->status);

	iwl_mvm_set_rfkill_state(mvm);

	 /* iwl_run_init_mvm_ucode is waiting for results, abort it. */
	if (rfkill_safe_init_done)
		iwl_abort_notification_waits(&mvm->notif_wait);

	/*
	 * Don't ask the transport to stop the firmware. We'll do it
	 * after cfg80211 takes us down.
	 */
	if (unified)
		return false;

	/*
	 * Stop the device if we run OPERATIONAL firmware or if we are in the
	 * middle of the calibrations.
	 */
	return state && rfkill_safe_init_done;
}

static void iwl_mvm_free_skb(struct iwl_op_mode *op_mode, struct sk_buff *skb)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	struct ieee80211_tx_info *info;

	info = IEEE80211_SKB_CB(skb);
	iwl_trans_free_tx_cmd(mvm->trans, info->driver_data[1]);
	ieee80211_free_txskb(mvm->hw, skb);
}

struct iwl_mvm_reprobe {
	struct device *dev;
	struct work_struct work;
};

static void iwl_mvm_reprobe_wk(struct work_struct *wk)
{
	struct iwl_mvm_reprobe *reprobe;

	reprobe = container_of(wk, struct iwl_mvm_reprobe, work);
	if (device_reprobe(reprobe->dev))
		dev_err(reprobe->dev, "reprobe failed!\n");
	kfree(reprobe);
	module_put(THIS_MODULE);
}

void iwl_mvm_nic_restart(struct iwl_mvm *mvm, bool fw_error)
{
	iwl_abort_notification_waits(&mvm->notif_wait);
	iwl_dbg_tlv_del_timers(mvm->trans);

	/*
	 * This is a bit racy, but worst case we tell mac80211 about
	 * a stopped/aborted scan when that was already done which
	 * is not a problem. It is necessary to abort any os scan
	 * here because mac80211 requires having the scan cleared
	 * before restarting.
	 * We'll reset the scan_status to NONE in restart cleanup in
	 * the next start() call from mac80211. If restart isn't called
	 * (no fw restart) scan status will stay busy.
	 */
	iwl_mvm_report_scan_aborted(mvm);

	/*
	 * If we're restarting already, don't cycle restarts.
	 * If INIT fw asserted, it will likely fail again.
	 * If WoWLAN fw asserted, don't restart either, mac80211
	 * can't recover this since we're already half suspended.
	 */
	if (!mvm->fw_restart && fw_error) {
		iwl_fw_error_collect(&mvm->fwrt);
	} else if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		struct iwl_mvm_reprobe *reprobe;

		IWL_ERR(mvm,
			"Firmware error during reconfiguration - reprobe!\n");

		/*
		 * get a module reference to avoid doing this while unloading
		 * anyway and to avoid scheduling a work with code that's
		 * being removed.
		 */
		if (!try_module_get(THIS_MODULE)) {
			IWL_ERR(mvm, "Module is being unloaded - abort\n");
			return;
		}

		reprobe = kzalloc(sizeof(*reprobe), GFP_ATOMIC);
		if (!reprobe) {
			module_put(THIS_MODULE);
			return;
		}
		reprobe->dev = mvm->trans->dev;
		INIT_WORK(&reprobe->work, iwl_mvm_reprobe_wk);
		schedule_work(&reprobe->work);
	} else if (test_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED,
			    &mvm->status)) {
		IWL_ERR(mvm, "HW restart already requested, but not started\n");
	} else if (mvm->fwrt.cur_fw_img == IWL_UCODE_REGULAR &&
		   mvm->hw_registered &&
		   !test_bit(STATUS_TRANS_DEAD, &mvm->trans->status)) {
		if (mvm->fw->ucode_capa.error_log_size) {
			u32 src_size = mvm->fw->ucode_capa.error_log_size;
			u32 src_addr = mvm->fw->ucode_capa.error_log_addr;
			u8 *recover_buf = kzalloc(src_size, GFP_ATOMIC);

			if (recover_buf) {
				mvm->error_recovery_buf = recover_buf;
				iwl_trans_read_mem_bytes(mvm->trans,
							 src_addr,
							 recover_buf,
							 src_size);
			}
		}

		iwl_fw_error_collect(&mvm->fwrt);

		if (fw_error && mvm->fw_restart > 0)
			mvm->fw_restart--;
		set_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED, &mvm->status);
		ieee80211_restart_hw(mvm->hw);
	}
}

static void iwl_mvm_nic_error(struct iwl_op_mode *op_mode)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	if (!test_bit(STATUS_TRANS_DEAD, &mvm->trans->status))
		iwl_mvm_dump_nic_error_log(mvm);

	iwl_mvm_nic_restart(mvm, true);
}

static void iwl_mvm_cmd_queue_full(struct iwl_op_mode *op_mode)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	WARN_ON(1);
	iwl_mvm_nic_restart(mvm, true);
}

#define IWL_MVM_COMMON_OPS					\
	/* these could be differentiated */			\
	.async_cb = iwl_mvm_async_cb,				\
	.queue_full = iwl_mvm_stop_sw_queue,			\
	.queue_not_full = iwl_mvm_wake_sw_queue,		\
	.hw_rf_kill = iwl_mvm_set_hw_rfkill_state,		\
	.free_skb = iwl_mvm_free_skb,				\
	.nic_error = iwl_mvm_nic_error,				\
	.cmd_queue_full = iwl_mvm_cmd_queue_full,		\
	.nic_config = iwl_mvm_nic_config,			\
	/* as we only register one, these MUST be common! */	\
	.start = iwl_op_mode_mvm_start,				\
	.stop = iwl_op_mode_mvm_stop

static const struct iwl_op_mode_ops iwl_mvm_ops = {
	IWL_MVM_COMMON_OPS,
	.rx = iwl_mvm_rx,
};

static void iwl_mvm_rx_mq_rss(struct iwl_op_mode *op_mode,
			      struct napi_struct *napi,
			      struct iwl_rx_cmd_buffer *rxb,
			      unsigned int queue)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u16 cmd = WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd);

	if (unlikely(cmd == WIDE_ID(LEGACY_GROUP, FRAME_RELEASE)))
		iwl_mvm_rx_frame_release(mvm, napi, rxb, queue);
	else if (unlikely(cmd == WIDE_ID(DATA_PATH_GROUP,
					 RX_QUEUES_NOTIFICATION)))
		iwl_mvm_rx_queue_notif(mvm, napi, rxb, queue);
	else if (likely(cmd == WIDE_ID(LEGACY_GROUP, REPLY_RX_MPDU_CMD)))
		iwl_mvm_rx_mpdu_mq(mvm, napi, rxb, queue);
}

static const struct iwl_op_mode_ops iwl_mvm_ops_mq = {
	IWL_MVM_COMMON_OPS,
	.rx = iwl_mvm_rx_mq,
	.rx_rss = iwl_mvm_rx_mq_rss,
};
