// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2025 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <linux/module.h>
#include <linux/rtnetlink.h>
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
#include "iwl-nvm-utils.h"
#include "iwl-csr.h"
#include "iwl-io.h"
#include "iwl-prph.h"
#include "rs.h"
#include "fw/api/scan.h"
#include "fw/api/rfi.h"
#include "time-event.h"
#include "fw-api.h"
#include "fw/acpi.h"
#include "fw/uefi.h"
#include "time-sync.h"

#define DRV_DESCRIPTION	"The new Intel(R) wireless AGN driver for Linux"
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IWLWIFI");

static const struct iwl_op_mode_ops iwl_mvm_ops;
static const struct iwl_op_mode_ops iwl_mvm_ops_mq;

struct iwl_mvm_mod_params iwlmvm_mod_params = {
	.power_scheme = IWL_POWER_SCHEME_BPS,
};

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
	u8 radio_cfg_type, radio_cfg_step, radio_cfg_dash;
	u32 reg_val;
	u32 phy_config = iwl_mvm_get_phy_config(mvm);

	radio_cfg_type = (phy_config & FW_PHY_CFG_RADIO_TYPE) >>
			 FW_PHY_CFG_RADIO_TYPE_POS;
	radio_cfg_step = (phy_config & FW_PHY_CFG_RADIO_STEP) >>
			 FW_PHY_CFG_RADIO_STEP_POS;
	radio_cfg_dash = (phy_config & FW_PHY_CFG_RADIO_DASH) >>
			 FW_PHY_CFG_RADIO_DASH_POS;

	IWL_DEBUG_INFO(mvm, "Radio type=0x%x-0x%x-0x%x\n", radio_cfg_type,
		       radio_cfg_step, radio_cfg_dash);

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210)
		return;

	/* SKU control */
	reg_val = CSR_HW_REV_STEP_DASH(mvm->trans->info.hw_rev);

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

	if (iwl_fw_dbg_is_d3_debug_enabled(&mvm->fwrt))
		reg_val |= CSR_HW_IF_CONFIG_REG_D3_DEBUG;

	iwl_trans_set_bits_mask(mvm->trans, CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP_DASH |
				CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE |
				CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP |
				CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH |
				CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
				CSR_HW_IF_CONFIG_REG_BIT_MAC_SI   |
				CSR_HW_IF_CONFIG_REG_D3_DEBUG,
				reg_val);

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

static void iwl_mvm_rx_esr_mode_notif(struct iwl_mvm *mvm,
				      struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_esr_mode_notif *notif = (void *)pkt->data;
	struct ieee80211_vif *vif = iwl_mvm_get_bss_vif(mvm);

	/* FW recommendations is only for entering EMLSR */
	if (IS_ERR_OR_NULL(vif) || iwl_mvm_vif_from_mac80211(vif)->esr_active)
		return;

	if (le32_to_cpu(notif->action) == ESR_RECOMMEND_ENTER)
		iwl_mvm_unblock_esr(mvm, vif, IWL_MVM_ESR_BLOCKED_FW);
	else
		iwl_mvm_block_esr(mvm, vif, IWL_MVM_ESR_BLOCKED_FW,
				  iwl_mvm_get_primary_link(vif));
}

static void iwl_mvm_rx_esr_trans_fail_notif(struct iwl_mvm *mvm,
					    struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_esr_trans_fail_notif *notif = (void *)pkt->data;
	struct ieee80211_vif *vif = iwl_mvm_get_bss_vif(mvm);
	u8 fw_link_id = le32_to_cpu(notif->link_id);
	struct ieee80211_bss_conf *bss_conf;

	if (IS_ERR_OR_NULL(vif))
		return;

	IWL_DEBUG_INFO(mvm, "Failed to %s eSR on link %d, reason %d\n",
		       le32_to_cpu(notif->activation) ? "enter" : "exit",
		       le32_to_cpu(notif->link_id),
		       le32_to_cpu(notif->err_code));

	/* we couldn't go back to single link, disconnect */
	if (!le32_to_cpu(notif->activation)) {
		iwl_mvm_connection_loss(mvm, vif, "emlsr exit failed");
		return;
	}

	bss_conf = iwl_mvm_rcu_fw_link_id_to_link_conf(mvm, fw_link_id, false);
	if (IWL_FW_CHECK(mvm, !bss_conf,
			 "FW reported failure to activate EMLSR on a non-existing link: %d\n",
			 fw_link_id))
		return;

	/*
	 * We failed to activate the second link and enter EMLSR, we need to go
	 * back to single link.
	 */
	iwl_mvm_exit_esr(mvm, vif, IWL_MVM_ESR_EXIT_FAIL_ENTRY,
			 bss_conf->link_id);
}

static void iwl_mvm_rx_monitor_notif(struct iwl_mvm *mvm,
				     struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_datapath_monitor_notif *notif = (void *)pkt->data;
	struct ieee80211_supported_band *sband;
	const struct ieee80211_sta_he_cap *he_cap;
	struct ieee80211_vif *vif;

	if (notif->type != cpu_to_le32(IWL_DP_MON_NOTIF_TYPE_EXT_CCA))
		return;

	/* FIXME: should fetch the link and not the vif */
	vif = iwl_mvm_get_vif_by_macid(mvm, notif->link_id);
	if (!vif || vif->type != NL80211_IFTYPE_STATION)
		return;

	if (!vif->bss_conf.chanreq.oper.chan ||
	    vif->bss_conf.chanreq.oper.chan->band != NL80211_BAND_2GHZ ||
	    vif->bss_conf.chanreq.oper.width < NL80211_CHAN_WIDTH_40)
		return;

	if (!vif->cfg.assoc)
		return;

	/* this shouldn't happen *again*, ignore it */
	if (mvm->cca_40mhz_workaround)
		return;

	/*
	 * We'll decrement this on disconnect - so set to 2 since we'll
	 * still have to disconnect from the current AP first.
	 */
	mvm->cca_40mhz_workaround = 2;

	/*
	 * This capability manipulation isn't really ideal, but it's the
	 * easiest choice - otherwise we'd have to do some major changes
	 * in mac80211 to support this, which isn't worth it. This does
	 * mean that userspace may have outdated information, but that's
	 * actually not an issue at all.
	 */
	sband = mvm->hw->wiphy->bands[NL80211_BAND_2GHZ];

	WARN_ON(!sband->ht_cap.ht_supported);
	WARN_ON(!(sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40));
	sband->ht_cap.cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;

	he_cap = ieee80211_get_he_iftype_cap_vif(sband, vif);

	if (he_cap) {
		/* we know that ours is writable */
		struct ieee80211_sta_he_cap *he = (void *)(uintptr_t)he_cap;

		WARN_ON(!he->has_he);
		WARN_ON(!(he->he_cap_elem.phy_cap_info[0] &
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G));
		he->he_cap_elem.phy_cap_info[0] &=
			~IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
	}

	ieee80211_disconnect(vif, true);
}

void iwl_mvm_update_link_smps(struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	enum ieee80211_smps_mode mode = IEEE80211_SMPS_AUTOMATIC;

	if (!link_conf)
		return;

	if (mvm->fw_static_smps_request &&
	    link_conf->chanreq.oper.width == NL80211_CHAN_WIDTH_160 &&
	    link_conf->he_support)
		mode = IEEE80211_SMPS_STATIC;

	iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_FW, mode,
			    link_conf->link_id);
}

static void iwl_mvm_intf_dual_chain_req(void *data, u8 *mac,
					struct ieee80211_vif *vif)
{
	struct ieee80211_bss_conf *link_conf;
	unsigned int link_id;

	rcu_read_lock();

	for_each_vif_active_link(vif, link_conf, link_id)
		iwl_mvm_update_link_smps(vif, link_conf);

	rcu_read_unlock();
}

static void iwl_mvm_rx_thermal_dual_chain_req(struct iwl_mvm *mvm,
					      struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_thermal_dual_chain_request *req = (void *)pkt->data;

	/* firmware is expected to handle that in RLC offload mode */
	if (IWL_FW_CHECK(mvm, iwl_mvm_has_rlc_offload(mvm),
			 "Got THERMAL_DUAL_CHAIN_REQUEST (0x%x) in RLC offload mode\n",
			 req->event))
		return;

	/*
	 * We could pass it to the iterator data, but also need to remember
	 * it for new interfaces that are added while in this state.
	 */
	mvm->fw_static_smps_request =
		req->event == cpu_to_le32(THERMAL_DUAL_CHAIN_REQ_DISABLE);
	ieee80211_iterate_interfaces(mvm->hw,
				     IEEE80211_IFACE_SKIP_SDATA_NOT_IN_DRIVER,
				     iwl_mvm_intf_dual_chain_req, NULL);
}

/**
 * enum iwl_rx_handler_context: context for Rx handler
 * @RX_HANDLER_SYNC : this means that it will be called in the Rx path
 *	which can't acquire mvm->mutex.
 * @RX_HANDLER_ASYNC_LOCKED : If the handler needs to hold mvm->mutex
 *	(and only in this case!), it should be set as ASYNC. In that case,
 *	it will be called from a worker with mvm->mutex held.
 * @RX_HANDLER_ASYNC_UNLOCKED : in case the handler needs to lock the
 *	mutex itself, it will be called from a worker without mvm->mutex held.
 * @RX_HANDLER_ASYNC_LOCKED_WIPHY: If the handler needs to hold the wiphy lock
 *	and mvm->mutex. Will be handled with the wiphy_work queue infra
 *	instead of regular work queue.
 */
enum iwl_rx_handler_context {
	RX_HANDLER_SYNC,
	RX_HANDLER_ASYNC_LOCKED,
	RX_HANDLER_ASYNC_UNLOCKED,
	RX_HANDLER_ASYNC_LOCKED_WIPHY,
};

/**
 * struct iwl_rx_handlers: handler for FW notification
 * @cmd_id: command id
 * @min_size: minimum size to expect for the notification
 * @context: see &iwl_rx_handler_context
 * @fn: the function is called when notification is received
 */
struct iwl_rx_handlers {
	u16 cmd_id, min_size;
	enum iwl_rx_handler_context context;
	void (*fn)(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb);
};

#define RX_HANDLER_NO_SIZE(_cmd_id, _fn, _context)		\
	{ .cmd_id = _cmd_id, .fn = _fn, .context = _context, }
#define RX_HANDLER_GRP_NO_SIZE(_grp, _cmd, _fn, _context)	\
	{ .cmd_id = WIDE_ID(_grp, _cmd), .fn = _fn, .context = _context, }
#define RX_HANDLER(_cmd_id, _fn, _context, _struct)		\
	{ .cmd_id = _cmd_id, .fn = _fn,				\
	  .context = _context, .min_size = sizeof(_struct), }
#define RX_HANDLER_GRP(_grp, _cmd, _fn, _context, _struct)	\
	{ .cmd_id = WIDE_ID(_grp, _cmd), .fn = _fn,		\
	  .context = _context, .min_size = sizeof(_struct), }

/*
 * Handlers for fw notifications
 * Convention: RX_HANDLER(CMD_NAME, iwl_mvm_rx_CMD_NAME
 * This list should be in order of frequency for performance purposes.
 *
 * The handler can be one from three contexts, see &iwl_rx_handler_context
 */
static const struct iwl_rx_handlers iwl_mvm_rx_handlers[] = {
	RX_HANDLER(TX_CMD, iwl_mvm_rx_tx_cmd, RX_HANDLER_SYNC,
		   struct iwl_tx_resp),
	RX_HANDLER(BA_NOTIF, iwl_mvm_rx_ba_notif, RX_HANDLER_SYNC,
		   struct iwl_mvm_ba_notif),

	RX_HANDLER_GRP(DATA_PATH_GROUP, TLC_MNG_UPDATE_NOTIF,
		       iwl_mvm_tlc_update_notif, RX_HANDLER_SYNC,
		       struct iwl_tlc_update_notif),

	RX_HANDLER(BT_PROFILE_NOTIFICATION, iwl_mvm_rx_bt_coex_old_notif,
		   RX_HANDLER_ASYNC_LOCKED_WIPHY,
		   struct iwl_bt_coex_prof_old_notif),
	RX_HANDLER_GRP(BT_COEX_GROUP, PROFILE_NOTIF, iwl_mvm_rx_bt_coex_notif,
		       RX_HANDLER_ASYNC_LOCKED_WIPHY,
		       struct iwl_bt_coex_profile_notif),
	RX_HANDLER_NO_SIZE(BEACON_NOTIFICATION, iwl_mvm_rx_beacon_notif,
			   RX_HANDLER_ASYNC_LOCKED),
	RX_HANDLER_NO_SIZE(STATISTICS_NOTIFICATION, iwl_mvm_rx_statistics,
			   RX_HANDLER_ASYNC_LOCKED),

	RX_HANDLER_GRP(STATISTICS_GROUP, STATISTICS_OPER_NOTIF,
		       iwl_mvm_handle_rx_system_oper_stats,
		       RX_HANDLER_ASYNC_LOCKED_WIPHY,
		       struct iwl_system_statistics_notif_oper),
	RX_HANDLER_GRP(STATISTICS_GROUP, STATISTICS_OPER_PART1_NOTIF,
		       iwl_mvm_handle_rx_system_oper_part1_stats,
		       RX_HANDLER_ASYNC_LOCKED,
		       struct iwl_system_statistics_part1_notif_oper),
	RX_HANDLER_GRP(SYSTEM_GROUP, SYSTEM_STATISTICS_END_NOTIF,
		       iwl_mvm_handle_rx_system_end_stats_notif,
		       RX_HANDLER_ASYNC_LOCKED,
		       struct iwl_system_statistics_end_notif),

	RX_HANDLER(BA_WINDOW_STATUS_NOTIFICATION_ID,
		   iwl_mvm_window_status_notif, RX_HANDLER_SYNC,
		   struct iwl_ba_window_status_notif),

	RX_HANDLER(TIME_EVENT_NOTIFICATION, iwl_mvm_rx_time_event_notif,
		   RX_HANDLER_SYNC, struct iwl_time_event_notif),
	RX_HANDLER_GRP(MAC_CONF_GROUP, SESSION_PROTECTION_NOTIF,
		       iwl_mvm_rx_session_protect_notif, RX_HANDLER_SYNC,
		       struct iwl_session_prot_notif),
	RX_HANDLER(MCC_CHUB_UPDATE_CMD, iwl_mvm_rx_chub_update_mcc,
		   RX_HANDLER_ASYNC_LOCKED, struct iwl_mcc_chub_notif),

	RX_HANDLER(EOSP_NOTIFICATION, iwl_mvm_rx_eosp_notif, RX_HANDLER_SYNC,
		   struct iwl_mvm_eosp_notification),

	RX_HANDLER(SCAN_ITERATION_COMPLETE,
		   iwl_mvm_rx_lmac_scan_iter_complete_notif, RX_HANDLER_SYNC,
		   struct iwl_lmac_scan_complete_notif),
	RX_HANDLER(SCAN_OFFLOAD_COMPLETE,
		   iwl_mvm_rx_lmac_scan_complete_notif,
		   RX_HANDLER_ASYNC_LOCKED, struct iwl_periodic_scan_complete),
	RX_HANDLER_NO_SIZE(MATCH_FOUND_NOTIFICATION,
			   iwl_mvm_rx_scan_match_found,
			   RX_HANDLER_SYNC),
	RX_HANDLER(SCAN_COMPLETE_UMAC, iwl_mvm_rx_umac_scan_complete_notif,
		   RX_HANDLER_ASYNC_LOCKED,
		   struct iwl_umac_scan_complete),
	RX_HANDLER(SCAN_ITERATION_COMPLETE_UMAC,
		   iwl_mvm_rx_umac_scan_iter_complete_notif, RX_HANDLER_SYNC,
		   struct iwl_umac_scan_iter_complete_notif),

	RX_HANDLER(MISSED_BEACONS_NOTIFICATION,
		   iwl_mvm_rx_missed_beacons_notif_legacy,
		   RX_HANDLER_ASYNC_LOCKED_WIPHY,
		   struct iwl_missed_beacons_notif_v4),

	RX_HANDLER_GRP(MAC_CONF_GROUP, MISSED_BEACONS_NOTIF,
		       iwl_mvm_rx_missed_beacons_notif,
		       RX_HANDLER_ASYNC_LOCKED_WIPHY,
		       struct iwl_missed_beacons_notif),
	RX_HANDLER(REPLY_ERROR, iwl_mvm_rx_fw_error, RX_HANDLER_SYNC,
		   struct iwl_error_resp),
	RX_HANDLER(PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION,
		   iwl_mvm_power_uapsd_misbehaving_ap_notif, RX_HANDLER_SYNC,
		   struct iwl_uapsd_misbehaving_ap_notif),
	RX_HANDLER_NO_SIZE(DTS_MEASUREMENT_NOTIFICATION, iwl_mvm_temp_notif,
			   RX_HANDLER_ASYNC_LOCKED),
	RX_HANDLER_GRP_NO_SIZE(PHY_OPS_GROUP, DTS_MEASUREMENT_NOTIF_WIDE,
			       iwl_mvm_temp_notif, RX_HANDLER_ASYNC_UNLOCKED),
	RX_HANDLER_GRP(PHY_OPS_GROUP, CT_KILL_NOTIFICATION,
		       iwl_mvm_ct_kill_notif, RX_HANDLER_SYNC,
		       struct ct_kill_notif),

	RX_HANDLER(TDLS_CHANNEL_SWITCH_NOTIFICATION, iwl_mvm_rx_tdls_notif,
		   RX_HANDLER_ASYNC_LOCKED,
		   struct iwl_tdls_channel_switch_notif),
	RX_HANDLER(MFUART_LOAD_NOTIFICATION, iwl_mvm_rx_mfuart_notif,
		   RX_HANDLER_SYNC, struct iwl_mfuart_load_notif_v1),
	RX_HANDLER_GRP(LOCATION_GROUP, TOF_RESPONDER_STATS,
		       iwl_mvm_ftm_responder_stats, RX_HANDLER_ASYNC_LOCKED,
		       struct iwl_ftm_responder_stats),

	RX_HANDLER_GRP_NO_SIZE(LOCATION_GROUP, TOF_RANGE_RESPONSE_NOTIF,
			       iwl_mvm_ftm_range_resp, RX_HANDLER_ASYNC_LOCKED),
	RX_HANDLER_GRP_NO_SIZE(LOCATION_GROUP, TOF_LC_NOTIF,
			       iwl_mvm_ftm_lc_notif, RX_HANDLER_ASYNC_LOCKED),

	RX_HANDLER_GRP(DEBUG_GROUP, MFU_ASSERT_DUMP_NTF,
		       iwl_mvm_mfu_assert_dump_notif, RX_HANDLER_SYNC,
		       struct iwl_mfu_assert_dump_notif),
	RX_HANDLER_GRP(PROT_OFFLOAD_GROUP, STORED_BEACON_NTF,
		       iwl_mvm_rx_stored_beacon_notif, RX_HANDLER_SYNC,
		       struct iwl_stored_beacon_notif_v2),
	RX_HANDLER_GRP(DATA_PATH_GROUP, MU_GROUP_MGMT_NOTIF,
		       iwl_mvm_mu_mimo_grp_notif, RX_HANDLER_SYNC,
		       struct iwl_mu_group_mgmt_notif),
	RX_HANDLER_GRP(DATA_PATH_GROUP, STA_PM_NOTIF,
		       iwl_mvm_sta_pm_notif, RX_HANDLER_SYNC,
		       struct iwl_mvm_pm_state_notification),
	RX_HANDLER_GRP(MAC_CONF_GROUP, PROBE_RESPONSE_DATA_NOTIF,
		       iwl_mvm_probe_resp_data_notif,
		       RX_HANDLER_ASYNC_LOCKED,
		       struct iwl_probe_resp_data_notif),
	RX_HANDLER_GRP(MAC_CONF_GROUP, CHANNEL_SWITCH_START_NOTIF,
		       iwl_mvm_channel_switch_start_notif,
		       RX_HANDLER_SYNC, struct iwl_channel_switch_start_notif),
	RX_HANDLER_GRP(MAC_CONF_GROUP, CHANNEL_SWITCH_ERROR_NOTIF,
		       iwl_mvm_channel_switch_error_notif,
		       RX_HANDLER_ASYNC_UNLOCKED,
		       struct iwl_channel_switch_error_notif),

	RX_HANDLER_GRP(DATA_PATH_GROUP, ESR_MODE_NOTIF,
		       iwl_mvm_rx_esr_mode_notif,
		       RX_HANDLER_ASYNC_LOCKED_WIPHY,
		       struct iwl_esr_mode_notif),

	RX_HANDLER_GRP(DATA_PATH_GROUP, MONITOR_NOTIF,
		       iwl_mvm_rx_monitor_notif, RX_HANDLER_ASYNC_LOCKED,
		       struct iwl_datapath_monitor_notif),

	RX_HANDLER_GRP(DATA_PATH_GROUP, THERMAL_DUAL_CHAIN_REQUEST,
		       iwl_mvm_rx_thermal_dual_chain_req,
		       RX_HANDLER_ASYNC_LOCKED,
		       struct iwl_thermal_dual_chain_request),

	RX_HANDLER_GRP(SYSTEM_GROUP, RFI_DEACTIVATE_NOTIF,
		       iwl_rfi_deactivate_notif_handler, RX_HANDLER_ASYNC_UNLOCKED,
		       struct iwl_rfi_deactivate_notif),

	RX_HANDLER_GRP(LEGACY_GROUP,
		       WNM_80211V_TIMING_MEASUREMENT_NOTIFICATION,
		       iwl_mvm_time_sync_msmt_event, RX_HANDLER_SYNC,
		       struct iwl_time_msmt_notify),
	RX_HANDLER_GRP(LEGACY_GROUP,
		       WNM_80211V_TIMING_MEASUREMENT_CONFIRM_NOTIFICATION,
		       iwl_mvm_time_sync_msmt_confirm_event, RX_HANDLER_SYNC,
		       struct iwl_time_msmt_cfm_notify),
	RX_HANDLER_GRP(MAC_CONF_GROUP, ROC_NOTIF,
		       iwl_mvm_rx_roc_notif, RX_HANDLER_ASYNC_LOCKED,
		       struct iwl_roc_notif),
	RX_HANDLER_GRP(SCAN_GROUP, CHANNEL_SURVEY_NOTIF,
		       iwl_mvm_rx_channel_survey_notif, RX_HANDLER_ASYNC_LOCKED,
		       struct iwl_umac_scan_channel_survey_notif),
	RX_HANDLER_GRP(MAC_CONF_GROUP, EMLSR_TRANS_FAIL_NOTIF,
		       iwl_mvm_rx_esr_trans_fail_notif,
		       RX_HANDLER_ASYNC_LOCKED_WIPHY,
		       struct iwl_esr_trans_fail_notif),
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
	HCMD_NAME(WNM_80211V_TIMING_MEASUREMENT_NOTIFICATION),
	HCMD_NAME(WNM_80211V_TIMING_MEASUREMENT_CONFIRM_NOTIFICATION),
	HCMD_NAME(PHY_CONFIGURATION_CMD),
	HCMD_NAME(CALIB_RES_NOTIF_PHY_DB),
	HCMD_NAME(PHY_DB_CMD),
	HCMD_NAME(SCAN_OFFLOAD_COMPLETE),
	HCMD_NAME(SCAN_OFFLOAD_UPDATE_PROFILES_CMD),
	HCMD_NAME(POWER_TABLE_CMD),
	HCMD_NAME(PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION),
	HCMD_NAME(REPLY_THERMAL_MNG_BACKOFF),
	HCMD_NAME(NVM_ACCESS_CMD),
	HCMD_NAME(BEACON_NOTIFICATION),
	HCMD_NAME(BEACON_TEMPLATE_CMD),
	HCMD_NAME(TX_ANT_CONFIGURATION_CMD),
	HCMD_NAME(BT_CONFIG),
	HCMD_NAME(STATISTICS_CMD),
	HCMD_NAME(STATISTICS_NOTIFICATION),
	HCMD_NAME(EOSP_NOTIFICATION),
	HCMD_NAME(REDUCE_TX_POWER_CMD),
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
	HCMD_NAME(MCAST_FILTER_CMD),
	HCMD_NAME(REPLY_SF_CFG_CMD),
	HCMD_NAME(REPLY_BEACON_FILTERING_CMD),
	HCMD_NAME(D3_CONFIG_CMD),
	HCMD_NAME(PROT_OFFLOAD_CONFIG_CMD),
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
	HCMD_NAME(DEBUG_LOG_MSG),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_system_names[] = {
	HCMD_NAME(SHARED_MEM_CFG_CMD),
	HCMD_NAME(SOC_CONFIGURATION_CMD),
	HCMD_NAME(INIT_EXTENDED_CFG_CMD),
	HCMD_NAME(FW_ERROR_RECOVERY_CMD),
	HCMD_NAME(RFI_CONFIG_CMD),
	HCMD_NAME(RFI_GET_FREQ_TABLE_CMD),
	HCMD_NAME(SYSTEM_FEATURES_CONTROL_CMD),
	HCMD_NAME(SYSTEM_STATISTICS_CMD),
	HCMD_NAME(SYSTEM_STATISTICS_END_NOTIF),
	HCMD_NAME(RFI_DEACTIVATE_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_mac_conf_names[] = {
	HCMD_NAME(LOW_LATENCY_CMD),
	HCMD_NAME(CHANNEL_SWITCH_TIME_EVENT_CMD),
	HCMD_NAME(SESSION_PROTECTION_CMD),
	HCMD_NAME(CANCEL_CHANNEL_SWITCH_CMD),
	HCMD_NAME(MAC_CONFIG_CMD),
	HCMD_NAME(LINK_CONFIG_CMD),
	HCMD_NAME(STA_CONFIG_CMD),
	HCMD_NAME(AUX_STA_CMD),
	HCMD_NAME(STA_REMOVE_CMD),
	HCMD_NAME(STA_DISABLE_TX_CMD),
	HCMD_NAME(ROC_CMD),
	HCMD_NAME(EMLSR_TRANS_FAIL_NOTIF),
	HCMD_NAME(ROC_NOTIF),
	HCMD_NAME(CHANNEL_SWITCH_ERROR_NOTIF),
	HCMD_NAME(MISSED_VAP_NOTIF),
	HCMD_NAME(SESSION_PROTECTION_NOTIF),
	HCMD_NAME(PROBE_RESPONSE_DATA_NOTIF),
	HCMD_NAME(CHANNEL_SWITCH_START_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_phy_names[] = {
	HCMD_NAME(CMD_DTS_MEASUREMENT_TRIGGER_WIDE),
	HCMD_NAME(CTDP_CONFIG_CMD),
	HCMD_NAME(TEMP_REPORTING_THRESHOLDS_CMD),
	HCMD_NAME(PER_CHAIN_LIMIT_OFFSET_CMD),
	HCMD_NAME(AP_TX_POWER_CONSTRAINTS_CMD),
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
	HCMD_NAME(WNM_PLATFORM_PTM_REQUEST_CMD),
	HCMD_NAME(WNM_80211V_TIMING_MEASUREMENT_CONFIG_CMD),
	HCMD_NAME(STA_HE_CTXT_CMD),
	HCMD_NAME(RLC_CONFIG_CMD),
	HCMD_NAME(RFH_QUEUE_CONFIG_CMD),
	HCMD_NAME(TLC_MNG_CONFIG_CMD),
	HCMD_NAME(CHEST_COLLECTOR_FILTER_CONFIG_CMD),
	HCMD_NAME(SCD_QUEUE_CONFIG_CMD),
	HCMD_NAME(SEC_KEY_CMD),
	HCMD_NAME(ESR_MODE_NOTIF),
	HCMD_NAME(MONITOR_NOTIF),
	HCMD_NAME(THERMAL_DUAL_CHAIN_REQUEST),
	HCMD_NAME(STA_PM_NOTIF),
	HCMD_NAME(MU_GROUP_MGMT_NOTIF),
	HCMD_NAME(RX_QUEUES_NOTIFICATION),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_statistics_names[] = {
	HCMD_NAME(STATISTICS_OPER_NOTIF),
	HCMD_NAME(STATISTICS_OPER_PART1_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_debug_names[] = {
	HCMD_NAME(LMAC_RD_WR),
	HCMD_NAME(UMAC_RD_WR),
	HCMD_NAME(HOST_EVENT_CFG),
	HCMD_NAME(DBGC_SUSPEND_RESUME),
	HCMD_NAME(BUFFER_ALLOCATION),
	HCMD_NAME(GET_TAS_STATUS),
	HCMD_NAME(FW_DUMP_COMPLETE_CMD),
	HCMD_NAME(FW_CLEAR_BUFFER),
	HCMD_NAME(MFU_ASSERT_DUMP_NTF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_scan_names[] = {
	HCMD_NAME(CHANNEL_SURVEY_NOTIF),
	HCMD_NAME(OFFLOAD_MATCH_INFO_NOTIF),
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
	HCMD_NAME(WOWLAN_WAKE_PKT_NOTIFICATION),
	HCMD_NAME(WOWLAN_INFO_NOTIFICATION),
	HCMD_NAME(D3_END_NOTIFICATION),
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

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mvm_bt_coex_names[] = {
	HCMD_NAME(PROFILE_NOTIF),
};

VISIBLE_IF_IWLWIFI_KUNIT
const struct iwl_hcmd_arr iwl_mvm_groups[] = {
	[LEGACY_GROUP] = HCMD_ARR(iwl_mvm_legacy_names),
	[LONG_GROUP] = HCMD_ARR(iwl_mvm_legacy_names),
	[SYSTEM_GROUP] = HCMD_ARR(iwl_mvm_system_names),
	[MAC_CONF_GROUP] = HCMD_ARR(iwl_mvm_mac_conf_names),
	[PHY_OPS_GROUP] = HCMD_ARR(iwl_mvm_phy_names),
	[DATA_PATH_GROUP] = HCMD_ARR(iwl_mvm_data_path_names),
	[SCAN_GROUP] = HCMD_ARR(iwl_mvm_scan_names),
	[LOCATION_GROUP] = HCMD_ARR(iwl_mvm_location_names),
	[BT_COEX_GROUP] = HCMD_ARR(iwl_mvm_bt_coex_names),
	[PROT_OFFLOAD_GROUP] = HCMD_ARR(iwl_mvm_prot_offload_names),
	[REGULATORY_AND_NVM_GROUP] =
		HCMD_ARR(iwl_mvm_regulatory_and_nvm_names),
	[DEBUG_GROUP] = HCMD_ARR(iwl_mvm_debug_names),
	[STATISTICS_GROUP] = HCMD_ARR(iwl_mvm_statistics_names),
};
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mvm_groups);
#if IS_ENABLED(CONFIG_IWLWIFI_KUNIT_TESTS)
const unsigned int iwl_mvm_groups_size = ARRAY_SIZE(iwl_mvm_groups);
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mvm_groups_size);
#endif

/* this forward declaration can avoid to export the function */
static void iwl_mvm_async_handlers_wk(struct work_struct *wk);
static void iwl_mvm_async_handlers_wiphy_wk(struct wiphy *wiphy,
					    struct wiphy_work *work);

static u32 iwl_mvm_min_backoff(struct iwl_mvm *mvm)
{
	const struct iwl_pwr_tx_backoff *backoff = mvm->cfg->pwr_tx_backoffs;
	u64 dflt_pwr_limit;

	if (!backoff)
		return 0;

	iwl_bios_get_pwr_limit(&mvm->fwrt, &dflt_pwr_limit);

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

	guard(mvm)(mvm);

	tx_blocked_vif =
		rcu_dereference_protected(mvm->csa_tx_blocked_vif,
					  lockdep_is_held(&mvm->mutex));

	if (!tx_blocked_vif)
		return;

	mvmvif = iwl_mvm_vif_from_mac80211(tx_blocked_vif);
	iwl_mvm_modify_all_sta_disable_tx(mvm, mvmvif, false);
	RCU_INIT_POINTER(mvm->csa_tx_blocked_vif, NULL);
}

static void iwl_mvm_fwrt_dump_start(void *ctx)
{
	struct iwl_mvm *mvm = ctx;

	mutex_lock(&mvm->mutex);
}

static void iwl_mvm_fwrt_dump_end(void *ctx)
{
	struct iwl_mvm *mvm = ctx;

	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_fwrt_send_hcmd(void *ctx, struct iwl_host_cmd *host_cmd)
{
	struct iwl_mvm *mvm = (struct iwl_mvm *)ctx;

	guard(mvm)(mvm);
	return iwl_mvm_send_cmd(mvm, host_cmd);
}

static bool iwl_mvm_d3_debug_enable(void *ctx)
{
	return IWL_MVM_D3_DEBUG;
}

static const struct iwl_fw_runtime_ops iwl_mvm_fwrt_ops = {
	.dump_start = iwl_mvm_fwrt_dump_start,
	.dump_end = iwl_mvm_fwrt_dump_end,
	.send_hcmd = iwl_mvm_fwrt_send_hcmd,
	.d3_debug_enable = iwl_mvm_d3_debug_enable,
};

static int iwl_mvm_start_get_nvm(struct iwl_mvm *mvm)
{
	struct iwl_trans *trans = mvm->trans;
	int ret;

	if (trans->csme_own) {
		if (WARN(!mvm->mei_registered,
			 "csme is owner, but we aren't registered to iwlmei\n"))
			goto get_nvm_from_fw;

		mvm->mei_nvm_data = iwl_mei_get_nvm();
		if (mvm->mei_nvm_data) {
			/*
			 * mvm->mei_nvm_data is set and because of that,
			 * we'll load the NVM from the FW when we'll get
			 * ownership.
			 */
			mvm->nvm_data =
				iwl_parse_mei_nvm_data(trans, trans->cfg,
						       mvm->mei_nvm_data,
						       mvm->fw,
						       mvm->set_tx_ant,
						       mvm->set_rx_ant);
			return 0;
		}

		IWL_ERR(mvm,
			"Got a NULL NVM from CSME, trying to get it from the device\n");
	}

get_nvm_from_fw:
	rtnl_lock();
	wiphy_lock(mvm->hw->wiphy);
	mutex_lock(&mvm->mutex);

	ret = iwl_trans_start_hw(mvm->trans);
	if (ret) {
		mutex_unlock(&mvm->mutex);
		wiphy_unlock(mvm->hw->wiphy);
		rtnl_unlock();
		return ret;
	}

	ret = iwl_run_init_mvm_ucode(mvm);
	if (ret && ret != -ERFKILL)
		iwl_fw_dbg_error_collect(&mvm->fwrt, FW_DBG_TRIGGER_DRIVER);
	if (!ret && iwl_mvm_is_lar_supported(mvm)) {
		mvm->hw->wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED;
		ret = iwl_mvm_init_mcc(mvm);
	}

	iwl_mvm_stop_device(mvm);

	mutex_unlock(&mvm->mutex);
	wiphy_unlock(mvm->hw->wiphy);
	rtnl_unlock();

	if (ret)
		IWL_ERR(mvm, "Failed to run INIT ucode: %d\n", ret);

	/* no longer need this regardless of failure or not */
	mvm->fw_product_reset = false;

	return ret;
}

static int iwl_mvm_start_post_nvm(struct iwl_mvm *mvm)
{
	struct iwl_mvm_csme_conn_info *csme_conn_info __maybe_unused;
	int ret;

	iwl_mvm_toggle_tx_ant(mvm, &mvm->mgmt_last_antenna_idx);

	ret = iwl_mvm_mac_setup_register(mvm);
	if (ret)
		return ret;

	mvm->hw_registered = true;

	iwl_mvm_dbgfs_register(mvm);

	wiphy_rfkill_set_hw_state_reason(mvm->hw->wiphy,
					 mvm->mei_rfkill_blocked,
					 RFKILL_HARD_BLOCK_NOT_OWNER);

	iwl_mvm_mei_set_sw_rfkill_state(mvm);

	return 0;
}

struct iwl_mvm_frob_txf_data {
	u8 *buf;
	size_t buflen;
};

static void iwl_mvm_frob_txf_key_iter(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      struct ieee80211_key_conf *key,
				      void *data)
{
	struct iwl_mvm_frob_txf_data *txf = data;
	u8 keylen, match, matchend;
	u8 *keydata;
	size_t i;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		keydata = key->key;
		keylen = key->keylen;
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	case WLAN_CIPHER_SUITE_TKIP:
		/*
		 * WEP has short keys which might show up in the payload,
		 * and then you can deduce the key, so in this case just
		 * remove all FIFO data.
		 * For TKIP, we don't know the phase 2 keys here, so same.
		 */
		memset(txf->buf, 0xBB, txf->buflen);
		return;
	default:
		return;
	}

	/* scan for key material and clear it out */
	match = 0;
	for (i = 0; i < txf->buflen; i++) {
		if (txf->buf[i] != keydata[match]) {
			match = 0;
			continue;
		}
		match++;
		if (match == keylen) {
			memset(txf->buf + i - keylen, 0xAA, keylen);
			match = 0;
		}
	}

	/* we're dealing with a FIFO, so check wrapped around data */
	matchend = match;
	for (i = 0; match && i < keylen - match; i++) {
		if (txf->buf[i] != keydata[match])
			break;
		match++;
		if (match == keylen) {
			memset(txf->buf, 0xAA, i + 1);
			memset(txf->buf + txf->buflen - matchend, 0xAA,
			       matchend);
			break;
		}
	}
}

static void iwl_mvm_frob_txf(void *ctx, void *buf, size_t buflen)
{
	struct iwl_mvm_frob_txf_data txf = {
		.buf = buf,
		.buflen = buflen,
	};
	struct iwl_mvm *mvm = ctx;

	/* embedded key material exists only on old API */
	if (iwl_mvm_has_new_tx_api(mvm))
		return;

	rcu_read_lock();
	ieee80211_iter_keys_rcu(mvm->hw, NULL, iwl_mvm_frob_txf_key_iter, &txf);
	rcu_read_unlock();
}

static void iwl_mvm_frob_hcmd(void *ctx, void *hcmd, size_t len)
{
	/* we only use wide headers for commands */
	struct iwl_cmd_header_wide *hdr = hcmd;
	unsigned int frob_start = sizeof(*hdr), frob_end = 0;

	if (len < sizeof(hdr))
		return;

	/* all the commands we care about are in LONG_GROUP */
	if (hdr->group_id != LONG_GROUP)
		return;

	switch (hdr->cmd) {
	case WEP_KEY:
	case WOWLAN_TKIP_PARAM:
	case WOWLAN_KEK_KCK_MATERIAL:
	case ADD_STA_KEY:
		/*
		 * blank out everything here, easier than dealing
		 * with the various versions of the command
		 */
		frob_end = INT_MAX;
		break;
	case MGMT_MCAST_KEY:
		frob_start = offsetof(struct iwl_mvm_mgmt_mcast_key_cmd, igtk);
		BUILD_BUG_ON(offsetof(struct iwl_mvm_mgmt_mcast_key_cmd, igtk) !=
			     offsetof(struct iwl_mvm_mgmt_mcast_key_cmd_v1, igtk));

		frob_end = offsetofend(struct iwl_mvm_mgmt_mcast_key_cmd, igtk);
		BUILD_BUG_ON(offsetof(struct iwl_mvm_mgmt_mcast_key_cmd, igtk) <
			     offsetof(struct iwl_mvm_mgmt_mcast_key_cmd_v1, igtk));
		break;
	}

	if (frob_start >= frob_end)
		return;

	if (frob_end > len)
		frob_end = len;

	memset((u8 *)hcmd + frob_start, 0xAA, frob_end - frob_start);
}

static void iwl_mvm_frob_mem(void *ctx, u32 mem_addr, void *mem, size_t buflen)
{
	const struct iwl_dump_exclude *excl;
	struct iwl_mvm *mvm = ctx;
	int i;

	switch (mvm->fwrt.cur_fw_img) {
	case IWL_UCODE_INIT:
	default:
		/* not relevant */
		return;
	case IWL_UCODE_REGULAR:
	case IWL_UCODE_REGULAR_USNIFFER:
		excl = mvm->fw->dump_excl;
		break;
	case IWL_UCODE_WOWLAN:
		excl = mvm->fw->dump_excl_wowlan;
		break;
	}

	BUILD_BUG_ON(sizeof(mvm->fw->dump_excl) !=
		     sizeof(mvm->fw->dump_excl_wowlan));

	for (i = 0; i < ARRAY_SIZE(mvm->fw->dump_excl); i++) {
		u32 start, end;

		if (!excl[i].addr || !excl[i].size)
			continue;

		start = excl[i].addr;
		end = start + excl[i].size;

		if (end <= mem_addr || start >= mem_addr + buflen)
			continue;

		if (start < mem_addr)
			start = mem_addr;

		if (end > mem_addr + buflen)
			end = mem_addr + buflen;

		memset((u8 *)mem + start - mem_addr, 0xAA, end - start);
	}
}

static const struct iwl_dump_sanitize_ops iwl_mvm_sanitize_ops = {
	.frob_txf = iwl_mvm_frob_txf,
	.frob_hcmd = iwl_mvm_frob_hcmd,
	.frob_mem = iwl_mvm_frob_mem,
};

static void iwl_mvm_me_conn_status(void *priv, const struct iwl_mei_conn_info *conn_info)
{
	struct iwl_mvm *mvm = priv;
	struct iwl_mvm_csme_conn_info *prev_conn_info, *curr_conn_info;

	/*
	 * This is protected by the guarantee that this function will not be
	 * called twice on two different threads
	 */
	prev_conn_info = rcu_dereference_protected(mvm->csme_conn_info, true);

	curr_conn_info = kzalloc(sizeof(*curr_conn_info), GFP_KERNEL);
	if (!curr_conn_info)
		return;

	curr_conn_info->conn_info = *conn_info;

	rcu_assign_pointer(mvm->csme_conn_info, curr_conn_info);

	if (prev_conn_info)
		kfree_rcu(prev_conn_info, rcu_head);
}

static void iwl_mvm_mei_rfkill(void *priv, bool blocked,
			       bool csme_taking_ownership)
{
	struct iwl_mvm *mvm = priv;

	if (blocked && !csme_taking_ownership)
		return;

	mvm->mei_rfkill_blocked = blocked;
	if (!mvm->hw_registered)
		return;

	wiphy_rfkill_set_hw_state_reason(mvm->hw->wiphy,
					 mvm->mei_rfkill_blocked,
					 RFKILL_HARD_BLOCK_NOT_OWNER);
}

static void iwl_mvm_mei_roaming_forbidden(void *priv, bool forbidden)
{
	struct iwl_mvm *mvm = priv;

	if (!mvm->hw_registered || !mvm->csme_vif)
		return;

	iwl_mvm_send_roaming_forbidden_event(mvm, mvm->csme_vif, forbidden);
}

static void iwl_mvm_sap_connected_wk(struct work_struct *wk)
{
	struct iwl_mvm *mvm =
		container_of(wk, struct iwl_mvm, sap_connected_wk);
	int ret;

	ret = iwl_mvm_start_get_nvm(mvm);
	if (ret)
		goto out_free;

	ret = iwl_mvm_start_post_nvm(mvm);
	if (ret)
		goto out_free;

	return;

out_free:
	IWL_ERR(mvm, "Couldn't get started...\n");
	iwl_mei_start_unregister();
	iwl_mei_unregister_complete();
	iwl_fw_flush_dumps(&mvm->fwrt);
	iwl_mvm_thermal_exit(mvm);
	iwl_fw_runtime_free(&mvm->fwrt);
	iwl_phy_db_free(mvm->phy_db);
	kfree(mvm->scan_cmd);
	iwl_trans_op_mode_leave(mvm->trans);
	kfree(mvm->nvm_data);
	kfree(mvm->mei_nvm_data);

	ieee80211_free_hw(mvm->hw);
}

static void iwl_mvm_mei_sap_connected(void *priv)
{
	struct iwl_mvm *mvm = priv;

	if (!mvm->hw_registered)
		schedule_work(&mvm->sap_connected_wk);
}

static void iwl_mvm_mei_nic_stolen(void *priv)
{
	struct iwl_mvm *mvm = priv;

	rtnl_lock();
	cfg80211_shutdown_all_interfaces(mvm->hw->wiphy);
	rtnl_unlock();
}

static const struct iwl_mei_ops mei_ops = {
	.me_conn_status = iwl_mvm_me_conn_status,
	.rfkill = iwl_mvm_mei_rfkill,
	.roaming_forbidden = iwl_mvm_mei_roaming_forbidden,
	.sap_connected = iwl_mvm_mei_sap_connected,
	.nic_stolen = iwl_mvm_mei_nic_stolen,
};

static void iwl_mvm_find_link_selection_vif(void *_data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (ieee80211_vif_is_mld(vif) && mvmvif->authorized)
		iwl_mvm_select_links(mvmvif->mvm, vif);
}

static void iwl_mvm_trig_link_selection(struct wiphy *wiphy,
					struct wiphy_work *wk)
{
	struct iwl_mvm *mvm =
		container_of(wk, struct iwl_mvm, trig_link_selection_wk);

	mutex_lock(&mvm->mutex);
	ieee80211_iterate_active_interfaces(mvm->hw,
					    IEEE80211_IFACE_ITER_NORMAL,
					    iwl_mvm_find_link_selection_vif,
					    NULL);
	mutex_unlock(&mvm->mutex);
}

static struct iwl_op_mode *
iwl_op_mode_mvm_start(struct iwl_trans *trans, const struct iwl_cfg *cfg,
		      const struct iwl_fw *fw, struct dentry *dbgfs_dir)
{
	struct ieee80211_hw *hw;
	struct iwl_op_mode *op_mode;
	struct iwl_mvm *mvm;
	static const u8 no_reclaim_cmds[] = {
		TX_CMD,
	};
	u32 max_agg;
	size_t scan_size;
	u32 min_backoff;
	struct iwl_mvm_csme_conn_info *csme_conn_info __maybe_unused;
	int ratecheck;
	int err;

	/*
	 * We use IWL_STATION_COUNT_MAX to check the validity of the station
	 * index all over the driver - check that its value corresponds to the
	 * array size.
	 */
	BUILD_BUG_ON(ARRAY_SIZE(mvm->fw_id_to_mac_id) !=
		     IWL_STATION_COUNT_MAX);

	/********************************
	 * 1. Allocating and configuring HW data
	 ********************************/
	hw = ieee80211_alloc_hw(sizeof(struct iwl_op_mode) +
				sizeof(struct iwl_mvm),
				iwl_mvm_has_mld_api(fw) ? &iwl_mvm_mld_hw_ops :
				&iwl_mvm_hw_ops);
	if (!hw)
		return ERR_PTR(-ENOMEM);

	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_BZ)
		max_agg = 512;
	else
		max_agg = IEEE80211_MAX_AMPDU_BUF_HE;

	hw->max_rx_aggregation_subframes = max_agg;

	op_mode = hw->priv;

	mvm = IWL_OP_MODE_GET_MVM(op_mode);
	mvm->dev = trans->dev;
	mvm->trans = trans;
	mvm->cfg = cfg;
	mvm->fw = fw;
	mvm->hw = hw;

	iwl_fw_runtime_init(&mvm->fwrt, trans, fw, &iwl_mvm_fwrt_ops, mvm,
			    &iwl_mvm_sanitize_ops, mvm, dbgfs_dir);

	iwl_mvm_get_bios_tables(mvm);
	iwl_uefi_get_sgom_table(trans, &mvm->fwrt);
	iwl_uefi_get_step_table(trans);
	iwl_bios_setup_step(trans, &mvm->fwrt);

	mvm->init_status = 0;

	/* start with v1 rates */
	mvm->fw_rates_ver = 1;

	/* check for rates version 2 */
	ratecheck =
		(iwl_fw_lookup_cmd_ver(mvm->fw, TX_CMD, 0) >= 8) +
		(iwl_fw_lookup_notif_ver(mvm->fw, DATA_PATH_GROUP,
					 TLC_MNG_UPDATE_NOTIF, 0) >= 3) +
		(iwl_fw_lookup_notif_ver(mvm->fw, LEGACY_GROUP,
					 REPLY_RX_MPDU_CMD, 0) >= 4) +
		(iwl_fw_lookup_notif_ver(mvm->fw, LONG_GROUP, TX_CMD, 0) >= 6);
	if (ratecheck != 0 && ratecheck != 4) {
		IWL_ERR(mvm, "Firmware has inconsistent rates\n");
		err = -EINVAL;
		goto out_free;
	}
	if (ratecheck == 4)
		mvm->fw_rates_ver = 2;

	/* check for rates version 3 */
	ratecheck =
		(iwl_fw_lookup_cmd_ver(mvm->fw, TX_CMD, 0) >= 11) +
		(iwl_fw_lookup_notif_ver(mvm->fw, DATA_PATH_GROUP,
					 TLC_MNG_UPDATE_NOTIF, 0) >= 4) +
		(iwl_fw_lookup_notif_ver(mvm->fw, LEGACY_GROUP,
					 REPLY_RX_MPDU_CMD, 0) >= 6) +
		(iwl_fw_lookup_notif_ver(mvm->fw, DATA_PATH_GROUP,
					 RX_NO_DATA_NOTIF, 0) >= 4) +
		(iwl_fw_lookup_notif_ver(mvm->fw, LONG_GROUP, TX_CMD, 0) >= 9);
	if (ratecheck != 0 && ratecheck != 5) {
		IWL_ERR(mvm, "Firmware has inconsistent rates\n");
		err = -EINVAL;
		goto out_free;
	}
	if (ratecheck == 5)
		mvm->fw_rates_ver = 3;

	trans->conf.rx_mpdu_cmd = REPLY_RX_MPDU_CMD;

	if (iwl_mvm_has_new_rx_api(mvm)) {
		op_mode->ops = &iwl_mvm_ops_mq;
		trans->conf.rx_mpdu_cmd_hdr_size =
			(trans->trans_cfg->device_family >=
			 IWL_DEVICE_FAMILY_AX210) ?
			sizeof(struct iwl_rx_mpdu_desc) :
			IWL_RX_DESC_SIZE_V1;
	} else {
		op_mode->ops = &iwl_mvm_ops;
		trans->conf.rx_mpdu_cmd_hdr_size =
			sizeof(struct iwl_rx_mpdu_res_start);

		if (WARN_ON(trans->info.num_rxqs > 1)) {
			err = -EINVAL;
			goto out_free;
		}
	}

	mvm->bios_enable_puncturing = iwl_uefi_get_puncturing(&mvm->fwrt);

	if (iwl_mvm_has_new_tx_api(mvm)) {
		/*
		 * If we have the new TX/queue allocation API initialize them
		 * all to invalid numbers. We'll rewrite the ones that we need
		 * later, but that doesn't happen for all of them all of the
		 * time (e.g. P2P Device is optional), and if a dynamic queue
		 * ends up getting number 2 (IWL_MVM_DQA_P2P_DEVICE_QUEUE) then
		 * iwl_mvm_is_static_queue() erroneously returns true, and we
		 * might have things getting stuck.
		 */
		mvm->aux_queue = IWL_MVM_INVALID_QUEUE;
		mvm->snif_queue = IWL_MVM_INVALID_QUEUE;
		mvm->probe_queue = IWL_MVM_INVALID_QUEUE;
		mvm->p2p_dev_queue = IWL_MVM_INVALID_QUEUE;
	} else {
		mvm->aux_queue = IWL_MVM_DQA_AUX_QUEUE;
		mvm->snif_queue = IWL_MVM_DQA_INJECT_MONITOR_QUEUE;
		mvm->probe_queue = IWL_MVM_DQA_AP_PROBE_RESP_QUEUE;
		mvm->p2p_dev_queue = IWL_MVM_DQA_P2P_DEVICE_QUEUE;
	}

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
	INIT_WORK(&mvm->sap_connected_wk, iwl_mvm_sap_connected_wk);
	INIT_DELAYED_WORK(&mvm->tdls_cs.dwork, iwl_mvm_tdls_ch_switch_work);
	INIT_DELAYED_WORK(&mvm->scan_timeout_dwork, iwl_mvm_scan_timeout_wk);
	INIT_WORK(&mvm->add_stream_wk, iwl_mvm_add_new_dqa_stream_wk);
	INIT_LIST_HEAD(&mvm->add_stream_txqs);
	spin_lock_init(&mvm->add_stream_lock);

	wiphy_work_init(&mvm->async_handlers_wiphy_wk,
			iwl_mvm_async_handlers_wiphy_wk);

	wiphy_work_init(&mvm->trig_link_selection_wk,
			iwl_mvm_trig_link_selection);

	init_waitqueue_head(&mvm->rx_sync_waitq);

	mvm->queue_sync_state = 0;

	SET_IEEE80211_DEV(mvm->hw, mvm->trans->dev);

	spin_lock_init(&mvm->tcm.lock);
	INIT_DELAYED_WORK(&mvm->tcm.work, iwl_mvm_tcm_work);
	mvm->tcm.ts = jiffies;
	mvm->tcm.ll_ts = jiffies;
	mvm->tcm.uapsd_nonagg_ts = jiffies;

	INIT_DELAYED_WORK(&mvm->cs_tx_unblock_dwork, iwl_mvm_tx_unblock_dwork);

	mvm->cmd_ver.range_resp =
		iwl_fw_lookup_notif_ver(mvm->fw, LOCATION_GROUP,
					TOF_RANGE_RESPONSE_NOTIF, 5);
	/* we only support up to version 9 */
	if (WARN_ON_ONCE(mvm->cmd_ver.range_resp > 9)) {
		err = -EINVAL;
		goto out_free;
	}

	/*
	 * Populate the state variables that the transport layer needs
	 * to know about.
	 */
	BUILD_BUG_ON(sizeof(no_reclaim_cmds) >
		     sizeof(trans->conf.no_reclaim_cmds));
	memcpy(trans->conf.no_reclaim_cmds, no_reclaim_cmds,
	       sizeof(no_reclaim_cmds));
	trans->conf.n_no_reclaim_cmds = ARRAY_SIZE(no_reclaim_cmds);

	trans->conf.rx_buf_size = iwl_amsdu_size_to_rxb_size();

	trans->conf.wide_cmd_header = true;

	trans->conf.command_groups = iwl_mvm_groups;
	trans->conf.command_groups_size = ARRAY_SIZE(iwl_mvm_groups);

	trans->conf.cmd_queue = IWL_MVM_DQA_CMD_QUEUE;
	trans->conf.cmd_fifo = IWL_MVM_TX_FIFO_CMD;
	trans->conf.scd_set_active = true;

	trans->conf.cb_data_offs = offsetof(struct ieee80211_tx_info,
					    driver_data[2]);

	snprintf(mvm->hw->wiphy->fw_version,
		 sizeof(mvm->hw->wiphy->fw_version),
		 "%.31s", fw->fw_version);

	trans->conf.fw_reset_handshake =
		fw_has_capa(&mvm->fw->ucode_capa,
			    IWL_UCODE_TLV_CAPA_FW_RESET_HANDSHAKE);

	trans->conf.queue_alloc_cmd_ver =
		iwl_fw_lookup_cmd_ver(mvm->fw,
				      WIDE_ID(DATA_PATH_GROUP,
					      SCD_QUEUE_CONFIG_CMD),
				      0);
	mvm->sta_remove_requires_queue_remove =
		trans->conf.queue_alloc_cmd_ver > 0;

	mvm->mld_api_is_used = iwl_mvm_has_mld_api(mvm->fw);

	/* Configure transport layer */
	iwl_trans_op_mode_enter(mvm->trans, op_mode);

	trans->dbg.dest_tlv = mvm->fw->dbg.dest_tlv;
	trans->dbg.n_dest_reg = mvm->fw->dbg.n_dest_reg;

	/* set up notification wait support */
	iwl_notification_wait_init(&mvm->notif_wait);

	/* Init phy db */
	mvm->phy_db = iwl_phy_db_init(trans);
	if (!mvm->phy_db) {
		IWL_ERR(mvm, "Cannot init phy_db\n");
		err = -ENOMEM;
		goto out_free;
	}

	if (iwlwifi_mod_params.nvm_file)
		mvm->nvm_file_name = iwlwifi_mod_params.nvm_file;
	else
		IWL_DEBUG_EEPROM(mvm->trans->dev,
				 "working without external nvm file\n");

	scan_size = iwl_mvm_scan_size(mvm);

	mvm->scan_cmd = kmalloc(scan_size, GFP_KERNEL);
	if (!mvm->scan_cmd) {
		err = -ENOMEM;
		goto out_free;
	}
	mvm->scan_cmd_size = scan_size;

	/* invalidate ids to prevent accidental removal of sta_id 0 */
	mvm->aux_sta.sta_id = IWL_INVALID_STA;
	mvm->snif_sta.sta_id = IWL_INVALID_STA;

	/* Set EBS as successful as long as not stated otherwise by the FW. */
	mvm->last_ebs_successful = true;

	min_backoff = iwl_mvm_min_backoff(mvm);
	iwl_mvm_thermal_initialize(mvm, min_backoff);

	if (!iwl_mvm_has_new_rx_stats_api(mvm))
		memset(&mvm->rx_stats_v3, 0,
		       sizeof(struct mvm_statistics_rx_v3));
	else
		memset(&mvm->rx_stats, 0, sizeof(struct mvm_statistics_rx));

	iwl_mvm_ftm_initiator_smooth_config(mvm);

	iwl_mvm_init_time_sync(&mvm->time_sync);

	mvm->debugfs_dir = dbgfs_dir;

	mvm->mei_registered = !iwl_mei_register(mvm, &mei_ops);

	iwl_mvm_mei_scan_filter_init(&mvm->mei_scan_filter);

	err = iwl_mvm_start_get_nvm(mvm);
	if (err) {
		/*
		 * Getting NVM failed while CSME is the owner, but we are
		 * registered to MEI, we'll get the NVM later when it'll be
		 * possible to get it from CSME.
		 */
		if (trans->csme_own && mvm->mei_registered)
			return op_mode;

		goto out_thermal_exit;
	}


	err = iwl_mvm_start_post_nvm(mvm);
	if (err)
		goto out_thermal_exit;

	return op_mode;

 out_thermal_exit:
	iwl_mvm_thermal_exit(mvm);
	if (mvm->mei_registered) {
		iwl_mei_start_unregister();
		iwl_mei_unregister_complete();
	}
 out_free:
	iwl_fw_flush_dumps(&mvm->fwrt);
	iwl_fw_runtime_free(&mvm->fwrt);

	iwl_phy_db_free(mvm->phy_db);
	kfree(mvm->scan_cmd);
	iwl_trans_op_mode_leave(trans);

	ieee80211_free_hw(mvm->hw);
	return ERR_PTR(err);
}

void iwl_mvm_stop_device(struct iwl_mvm *mvm)
{
	lockdep_assert_held(&mvm->mutex);

	iwl_fw_cancel_timestamp(&mvm->fwrt);

	clear_bit(IWL_MVM_STATUS_FIRMWARE_RUNNING, &mvm->status);

	iwl_mvm_pause_tcm(mvm, false);

	iwl_fw_dbg_stop_sync(&mvm->fwrt);
	iwl_trans_stop_device(mvm->trans);
	iwl_free_fw_paging(&mvm->fwrt);
	iwl_fw_dump_conf_clear(&mvm->fwrt);
	iwl_mvm_mei_device_state(mvm, false);
}

static void iwl_op_mode_mvm_stop(struct iwl_op_mode *op_mode)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	int i;

	if (mvm->mei_registered) {
		rtnl_lock();
		iwl_mei_set_netdev(NULL);
		rtnl_unlock();
		iwl_mei_start_unregister();
	}

	/*
	 * After we unregister from mei, the worker can't be scheduled
	 * anymore.
	 */
	cancel_work_sync(&mvm->sap_connected_wk);

	iwl_mvm_leds_exit(mvm);

	iwl_mvm_thermal_exit(mvm);

	/*
	 * If we couldn't get ownership on the device and we couldn't
	 * get the NVM from CSME, we haven't registered to mac80211.
	 * In that case, we didn't fail op_mode_start, because we are
	 * waiting for CSME to allow us to get the NVM to register to
	 * mac80211. If that didn't happen, we haven't registered to
	 * mac80211, hence the if below.
	 */
	if (mvm->hw_registered)
		ieee80211_unregister_hw(mvm->hw);

	kfree(mvm->scan_cmd);
	kfree(mvm->mcast_filter_cmd);
	mvm->mcast_filter_cmd = NULL;

	kfree(mvm->error_recovery_buf);
	mvm->error_recovery_buf = NULL;

	iwl_mvm_ptp_remove(mvm);

	iwl_trans_op_mode_leave(mvm->trans);

	iwl_phy_db_free(mvm->phy_db);
	mvm->phy_db = NULL;

	kfree(mvm->nvm_data);
	kfree(mvm->mei_nvm_data);
	kfree(rcu_access_pointer(mvm->csme_conn_info));
	kfree(mvm->temp_nvm_data);
	for (i = 0; i < NVM_MAX_NUM_SECTIONS; i++)
		kfree(mvm->nvm_sections[i].data);
	kfree(mvm->acs_survey);

	cancel_delayed_work_sync(&mvm->tcm.work);

	iwl_fw_runtime_free(&mvm->fwrt);
	mutex_destroy(&mvm->mutex);

	if (mvm->mei_registered)
		iwl_mei_unregister_complete();

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

/*
 * This function receives a bitmap of rx async handler contexts
 * (&iwl_rx_handler_context) to handle, and runs only them
 */
static void iwl_mvm_async_handlers_by_context(struct iwl_mvm *mvm,
					      u8 contexts)
{
	struct iwl_async_handler_entry *entry, *tmp;
	LIST_HEAD(local_list);

	/*
	 * Sync with Rx path with a lock. Remove all the entries of the
	 * wanted contexts from this list, add them to a local one (lock free),
	 * and then handle them.
	 */
	spin_lock_bh(&mvm->async_handlers_lock);
	list_for_each_entry_safe(entry, tmp, &mvm->async_handlers_list, list) {
		if (!(BIT(entry->context) & contexts))
			continue;
		list_del(&entry->list);
		list_add_tail(&entry->list, &local_list);
	}
	spin_unlock_bh(&mvm->async_handlers_lock);

	list_for_each_entry_safe(entry, tmp, &local_list, list) {
		if (entry->context != RX_HANDLER_ASYNC_UNLOCKED)
			mutex_lock(&mvm->mutex);
		entry->fn(mvm, &entry->rxb);
		iwl_free_rxb(&entry->rxb);
		list_del(&entry->list);
		if (entry->context != RX_HANDLER_ASYNC_UNLOCKED)
			mutex_unlock(&mvm->mutex);
		kfree(entry);
	}
}

static void iwl_mvm_async_handlers_wiphy_wk(struct wiphy *wiphy,
					    struct wiphy_work *wk)
{
	struct iwl_mvm *mvm =
		container_of(wk, struct iwl_mvm, async_handlers_wiphy_wk);
	u8 contexts = BIT(RX_HANDLER_ASYNC_LOCKED_WIPHY);

	iwl_mvm_async_handlers_by_context(mvm, contexts);
}

static void iwl_mvm_async_handlers_wk(struct work_struct *wk)
{
	struct iwl_mvm *mvm =
		container_of(wk, struct iwl_mvm, async_handlers_wk);
	u8 contexts = BIT(RX_HANDLER_ASYNC_LOCKED) |
		      BIT(RX_HANDLER_ASYNC_UNLOCKED);

	iwl_mvm_async_handlers_by_context(mvm, contexts);
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
	unsigned int pkt_len = iwl_rx_packet_payload_len(pkt);
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

		if (IWL_FW_CHECK(mvm, pkt_len < rx_h->min_size,
				 "unexpected notification 0x%04x size %d, need %d\n",
				 rx_h->cmd_id, pkt_len, rx_h->min_size))
			return;

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
		if (rx_h->context == RX_HANDLER_ASYNC_LOCKED_WIPHY)
			wiphy_work_queue(mvm->hw->wiphy,
					 &mvm->async_handlers_wiphy_wk);
		else
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

void iwl_mvm_rx_mq(struct iwl_op_mode *op_mode,
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

	if (WARN_ON_ONCE(sta_id >= mvm->fw->ucode_capa.num_stations))
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
		if (start)
			clear_bit(IWL_MVM_TXQ_STATE_STOP_FULL, &mvmtxq->state);
		else
			set_bit(IWL_MVM_TXQ_STATE_STOP_FULL, &mvmtxq->state);

		if (start && mvmsta->sta_state != IEEE80211_STA_NOTEXIST) {
			local_bh_disable();
			iwl_mvm_mac_itxq_xmit(mvm->hw, txq);
			local_bh_enable();
		}
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
	wiphy_rfkill_set_hw_state(mvm->hw->wiphy,
				  iwl_mvm_is_radio_killed(mvm));
}

void iwl_mvm_set_hw_ctkill_state(struct iwl_mvm *mvm, bool state)
{
	if (state)
		set_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status);
	else
		clear_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status);

	iwl_mvm_set_rfkill_state(mvm);
}

struct iwl_mvm_csme_conn_info *iwl_mvm_get_csme_conn_info(struct iwl_mvm *mvm)
{
	return rcu_dereference_protected(mvm->csme_conn_info,
					 lockdep_is_held(&mvm->mutex));
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

static void iwl_mvm_nic_error(struct iwl_op_mode *op_mode,
			      enum iwl_fw_error_type type)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	iwl_abort_notification_waits(&mvm->notif_wait);
	iwl_dbg_tlv_del_timers(mvm->trans);

	if (type == IWL_ERR_TYPE_CMD_QUEUE_FULL)
		IWL_ERR(mvm, "Command queue full!\n");
	else if (!test_bit(STATUS_TRANS_DEAD, &mvm->trans->status) &&
		 !test_and_clear_bit(IWL_MVM_STATUS_SUPPRESS_ERROR_LOG_ONCE,
				     &mvm->status))
		iwl_mvm_dump_nic_error_log(mvm);

	/*
	 * This should be first thing before trying to collect any
	 * data to avoid endless loops if any HW error happens while
	 * collecting debug data.
	 * It might not actually be true that we'll restart, but the
	 * setting of the bit doesn't matter if we're going to be
	 * unbound either.
	 */
	if (type != IWL_ERR_TYPE_RESET_HS_TIMEOUT)
		set_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED, &mvm->status);
}

static void iwl_mvm_dump_error(struct iwl_op_mode *op_mode,
			       struct iwl_fw_error_dump_mode *mode)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	/* if we come in from opmode we have the mutex held */
	if (mode->context == IWL_ERR_CONTEXT_FROM_OPMODE) {
		lockdep_assert_held(&mvm->mutex);
		iwl_fw_error_collect(&mvm->fwrt);
	} else {
		mutex_lock(&mvm->mutex);
		if (mode->context != IWL_ERR_CONTEXT_ABORT)
			iwl_fw_error_collect(&mvm->fwrt);
		mutex_unlock(&mvm->mutex);
	}
}

static bool iwl_mvm_sw_reset(struct iwl_op_mode *op_mode,
			     enum iwl_fw_error_type type)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	/*
	 * If the firmware crashes while we're already considering it
	 * to be dead then don't ask for a restart, that cannot do
	 * anything useful anyway.
	 */
	if (!test_bit(IWL_MVM_STATUS_FIRMWARE_RUNNING, &mvm->status))
		return false;

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
	 * If INIT fw asserted, it will likely fail again.
	 * If WoWLAN fw asserted, don't restart either, mac80211
	 * can't recover this since we're already half suspended.
	 */
	if (mvm->fwrt.cur_fw_img == IWL_UCODE_REGULAR && mvm->hw_registered) {
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

		if (mvm->fwrt.trans->dbg.restart_required) {
			IWL_DEBUG_INFO(mvm, "FW restart requested after debug collection\n");
			mvm->fwrt.trans->dbg.restart_required = false;
			ieee80211_restart_hw(mvm->hw);
			return true;
		} else if (mvm->trans->trans_cfg->device_family <= IWL_DEVICE_FAMILY_8000) {
			ieee80211_restart_hw(mvm->hw);
			return true;
		}
	}

	return false;
}

static void iwl_op_mode_mvm_time_point(struct iwl_op_mode *op_mode,
				       enum iwl_fw_ini_time_point tp_id,
				       union iwl_dbg_tlv_tp_data *tp_data)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	iwl_dbg_tlv_time_point(&mvm->fwrt, tp_id, tp_data);
}

#ifdef CONFIG_PM_SLEEP
static void iwl_op_mode_mvm_device_powered_off(struct iwl_op_mode *op_mode)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	mutex_lock(&mvm->mutex);
	clear_bit(IWL_MVM_STATUS_IN_D3, &mvm->status);
	iwl_mvm_stop_device(mvm);
	mvm->fast_resume = false;
	mutex_unlock(&mvm->mutex);
}
#else
static void iwl_op_mode_mvm_device_powered_off(struct iwl_op_mode *op_mode)
{}
#endif

#define IWL_MVM_COMMON_OPS					\
	/* these could be differentiated */			\
	.queue_full = iwl_mvm_stop_sw_queue,			\
	.queue_not_full = iwl_mvm_wake_sw_queue,		\
	.hw_rf_kill = iwl_mvm_set_hw_rfkill_state,		\
	.free_skb = iwl_mvm_free_skb,				\
	.nic_error = iwl_mvm_nic_error,				\
	.dump_error = iwl_mvm_dump_error,			\
	.sw_reset = iwl_mvm_sw_reset,				\
	.nic_config = iwl_mvm_nic_config,			\
	/* as we only register one, these MUST be common! */	\
	.start = iwl_op_mode_mvm_start,				\
	.stop = iwl_op_mode_mvm_stop,				\
	.time_point = iwl_op_mode_mvm_time_point,		\
	.device_powered_off = iwl_op_mode_mvm_device_powered_off

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

	if (unlikely(queue >= mvm->trans->info.num_rxqs))
		return;

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
