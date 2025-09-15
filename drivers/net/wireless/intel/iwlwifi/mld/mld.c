// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include <linux/rtnetlink.h>
#include <net/mac80211.h>

#include "fw/api/rx.h"
#include "fw/api/datapath.h"
#include "fw/api/commands.h"
#include "fw/api/offload.h"
#include "fw/api/coex.h"
#include "fw/dbg.h"
#include "fw/uefi.h"

#include "mld.h"
#include "mlo.h"
#include "mac80211.h"
#include "led.h"
#include "scan.h"
#include "tx.h"
#include "sta.h"
#include "regulatory.h"
#include "thermal.h"
#include "low_latency.h"
#include "hcmd.h"
#include "fw/api/location.h"

#include "iwl-nvm-parse.h"

#define DRV_DESCRIPTION "Intel(R) MLD wireless driver for Linux"
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IWLWIFI");

static const struct iwl_op_mode_ops iwl_mld_ops;

static int __init iwl_mld_init(void)
{
	int ret = iwl_opmode_register("iwlmld", &iwl_mld_ops);

	if (ret)
		pr_err("Unable to register MLD op_mode: %d\n", ret);

	return ret;
}
module_init(iwl_mld_init);

static void __exit iwl_mld_exit(void)
{
	iwl_opmode_deregister("iwlmld");
}
module_exit(iwl_mld_exit);

static void iwl_mld_hw_set_regulatory(struct iwl_mld *mld)
{
	struct wiphy *wiphy = mld->wiphy;

	wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED;
	wiphy->regulatory_flags |= REGULATORY_ENABLE_RELAX_NO_IR;
}

VISIBLE_IF_IWLWIFI_KUNIT
void iwl_construct_mld(struct iwl_mld *mld, struct iwl_trans *trans,
		       const struct iwl_rf_cfg *cfg, const struct iwl_fw *fw,
		       struct ieee80211_hw *hw, struct dentry *dbgfs_dir)
{
	mld->dev = trans->dev;
	mld->trans = trans;
	mld->cfg = cfg;
	mld->fw = fw;
	mld->hw = hw;
	mld->wiphy = hw->wiphy;
	mld->debugfs_dir = dbgfs_dir;

	iwl_notification_wait_init(&mld->notif_wait);

	/* Setup async RX handling */
	spin_lock_init(&mld->async_handlers_lock);
	INIT_LIST_HEAD(&mld->async_handlers_list);
	wiphy_work_init(&mld->async_handlers_wk,
			iwl_mld_async_handlers_wk);

	/* Dynamic Queue Allocation */
	spin_lock_init(&mld->add_txqs_lock);
	INIT_LIST_HEAD(&mld->txqs_to_add);
	wiphy_work_init(&mld->add_txqs_wk, iwl_mld_add_txqs_wk);

	/* Setup RX queues sync wait queue */
	init_waitqueue_head(&mld->rxq_sync.waitq);
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_construct_mld);

static void __acquires(&mld->wiphy->mtx)
iwl_mld_fwrt_dump_start(void *ctx)
{
	struct iwl_mld *mld = ctx;

	wiphy_lock(mld->wiphy);
}

static void __releases(&mld->wiphy->mtx)
iwl_mld_fwrt_dump_end(void *ctx)
{
	struct iwl_mld *mld = ctx;

	wiphy_unlock(mld->wiphy);
}

static bool iwl_mld_d3_debug_enable(void *ctx)
{
	return IWL_MLD_D3_DEBUG;
}

static int iwl_mld_fwrt_send_hcmd(void *ctx, struct iwl_host_cmd *host_cmd)
{
	struct iwl_mld *mld = (struct iwl_mld *)ctx;
	int ret;

	wiphy_lock(mld->wiphy);
	ret = iwl_mld_send_cmd(mld, host_cmd);
	wiphy_unlock(mld->wiphy);

	return ret;
}

static const struct iwl_fw_runtime_ops iwl_mld_fwrt_ops = {
	.dump_start = iwl_mld_fwrt_dump_start,
	.dump_end = iwl_mld_fwrt_dump_end,
	.send_hcmd = iwl_mld_fwrt_send_hcmd,
	.d3_debug_enable = iwl_mld_d3_debug_enable,
};

static void
iwl_mld_construct_fw_runtime(struct iwl_mld *mld, struct iwl_trans *trans,
			     const struct iwl_fw *fw,
			     struct dentry *debugfs_dir)
{
	iwl_fw_runtime_init(&mld->fwrt, trans, fw, &iwl_mld_fwrt_ops, mld,
			    NULL, NULL, debugfs_dir);

	iwl_fw_set_current_image(&mld->fwrt, IWL_UCODE_REGULAR);
}

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_legacy_names[] = {
	HCMD_NAME(UCODE_ALIVE_NTFY),
	HCMD_NAME(INIT_COMPLETE_NOTIF),
	HCMD_NAME(PHY_CONTEXT_CMD),
	HCMD_NAME(SCAN_CFG_CMD),
	HCMD_NAME(SCAN_REQ_UMAC),
	HCMD_NAME(SCAN_ABORT_UMAC),
	HCMD_NAME(SCAN_COMPLETE_UMAC),
	HCMD_NAME(TX_CMD),
	HCMD_NAME(TXPATH_FLUSH),
	HCMD_NAME(LEDS_CMD),
	HCMD_NAME(WNM_80211V_TIMING_MEASUREMENT_NOTIFICATION),
	HCMD_NAME(WNM_80211V_TIMING_MEASUREMENT_CONFIRM_NOTIFICATION),
	HCMD_NAME(SCAN_OFFLOAD_UPDATE_PROFILES_CMD),
	HCMD_NAME(POWER_TABLE_CMD),
	HCMD_NAME(PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION),
	HCMD_NAME(BEACON_NOTIFICATION),
	HCMD_NAME(BEACON_TEMPLATE_CMD),
	HCMD_NAME(TX_ANT_CONFIGURATION_CMD),
	HCMD_NAME(REDUCE_TX_POWER_CMD),
	HCMD_NAME(MISSED_BEACONS_NOTIFICATION),
	HCMD_NAME(MAC_PM_POWER_TABLE),
	HCMD_NAME(MFUART_LOAD_NOTIFICATION),
	HCMD_NAME(RSS_CONFIG_CMD),
	HCMD_NAME(SCAN_ITERATION_COMPLETE_UMAC),
	HCMD_NAME(REPLY_RX_MPDU_CMD),
	HCMD_NAME(BA_NOTIF),
	HCMD_NAME(MCC_UPDATE_CMD),
	HCMD_NAME(MCC_CHUB_UPDATE_CMD),
	HCMD_NAME(MCAST_FILTER_CMD),
	HCMD_NAME(REPLY_BEACON_FILTERING_CMD),
	HCMD_NAME(PROT_OFFLOAD_CONFIG_CMD),
	HCMD_NAME(MATCH_FOUND_NOTIFICATION),
	HCMD_NAME(WOWLAN_PATTERNS),
	HCMD_NAME(WOWLAN_CONFIGURATION),
	HCMD_NAME(WOWLAN_TSC_RSC_PARAM),
	HCMD_NAME(WOWLAN_KEK_KCK_MATERIAL),
	HCMD_NAME(DEBUG_HOST_COMMAND),
	HCMD_NAME(LDBG_CONFIG_CMD),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_system_names[] = {
	HCMD_NAME(SHARED_MEM_CFG_CMD),
	HCMD_NAME(SOC_CONFIGURATION_CMD),
	HCMD_NAME(INIT_EXTENDED_CFG_CMD),
	HCMD_NAME(FW_ERROR_RECOVERY_CMD),
	HCMD_NAME(RFI_CONFIG_CMD),
	HCMD_NAME(RFI_GET_FREQ_TABLE_CMD),
	HCMD_NAME(SYSTEM_STATISTICS_CMD),
	HCMD_NAME(SYSTEM_STATISTICS_END_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_reg_and_nvm_names[] = {
	HCMD_NAME(LARI_CONFIG_CHANGE),
	HCMD_NAME(NVM_GET_INFO),
	HCMD_NAME(TAS_CONFIG),
	HCMD_NAME(SAR_OFFSET_MAPPING_TABLE_CMD),
	HCMD_NAME(MCC_ALLOWED_AP_TYPE_CMD),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_debug_names[] = {
	HCMD_NAME(HOST_EVENT_CFG),
	HCMD_NAME(DBGC_SUSPEND_RESUME),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_mac_conf_names[] = {
	HCMD_NAME(LOW_LATENCY_CMD),
	HCMD_NAME(SESSION_PROTECTION_CMD),
	HCMD_NAME(MAC_CONFIG_CMD),
	HCMD_NAME(LINK_CONFIG_CMD),
	HCMD_NAME(STA_CONFIG_CMD),
	HCMD_NAME(AUX_STA_CMD),
	HCMD_NAME(STA_REMOVE_CMD),
	HCMD_NAME(ROC_CMD),
	HCMD_NAME(MISSED_BEACONS_NOTIF),
	HCMD_NAME(EMLSR_TRANS_FAIL_NOTIF),
	HCMD_NAME(ROC_NOTIF),
	HCMD_NAME(CHANNEL_SWITCH_ERROR_NOTIF),
	HCMD_NAME(SESSION_PROTECTION_NOTIF),
	HCMD_NAME(PROBE_RESPONSE_DATA_NOTIF),
	HCMD_NAME(CHANNEL_SWITCH_START_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_data_path_names[] = {
	HCMD_NAME(TRIGGER_RX_QUEUES_NOTIF_CMD),
	HCMD_NAME(WNM_PLATFORM_PTM_REQUEST_CMD),
	HCMD_NAME(WNM_80211V_TIMING_MEASUREMENT_CONFIG_CMD),
	HCMD_NAME(RFH_QUEUE_CONFIG_CMD),
	HCMD_NAME(TLC_MNG_CONFIG_CMD),
	HCMD_NAME(RX_BAID_ALLOCATION_CONFIG_CMD),
	HCMD_NAME(SCD_QUEUE_CONFIG_CMD),
	HCMD_NAME(ESR_MODE_NOTIF),
	HCMD_NAME(MONITOR_NOTIF),
	HCMD_NAME(TLC_MNG_UPDATE_NOTIF),
	HCMD_NAME(BEACON_FILTER_IN_NOTIF),
	HCMD_NAME(MU_GROUP_MGMT_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_scan_names[] = {
	HCMD_NAME(CHANNEL_SURVEY_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_location_names[] = {
	HCMD_NAME(TOF_RANGE_REQ_CMD),
	HCMD_NAME(TOF_RANGE_RESPONSE_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_phy_names[] = {
	HCMD_NAME(CMD_DTS_MEASUREMENT_TRIGGER_WIDE),
	HCMD_NAME(CTDP_CONFIG_CMD),
	HCMD_NAME(TEMP_REPORTING_THRESHOLDS_CMD),
	HCMD_NAME(PER_CHAIN_LIMIT_OFFSET_CMD),
	HCMD_NAME(CT_KILL_NOTIFICATION),
	HCMD_NAME(DTS_MEASUREMENT_NOTIF_WIDE),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_statistics_names[] = {
	HCMD_NAME(STATISTICS_OPER_NOTIF),
	HCMD_NAME(STATISTICS_OPER_PART1_NOTIF),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_prot_offload_names[] = {
	HCMD_NAME(WOWLAN_WAKE_PKT_NOTIFICATION),
	HCMD_NAME(WOWLAN_INFO_NOTIFICATION),
	HCMD_NAME(D3_END_NOTIFICATION),
};

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search
 */
static const struct iwl_hcmd_names iwl_mld_coex_names[] = {
	HCMD_NAME(PROFILE_NOTIF),
};

VISIBLE_IF_IWLWIFI_KUNIT
const struct iwl_hcmd_arr iwl_mld_groups[] = {
	[LEGACY_GROUP] = HCMD_ARR(iwl_mld_legacy_names),
	[LONG_GROUP] = HCMD_ARR(iwl_mld_legacy_names),
	[SYSTEM_GROUP] = HCMD_ARR(iwl_mld_system_names),
	[MAC_CONF_GROUP] = HCMD_ARR(iwl_mld_mac_conf_names),
	[DATA_PATH_GROUP] = HCMD_ARR(iwl_mld_data_path_names),
	[SCAN_GROUP] = HCMD_ARR(iwl_mld_scan_names),
	[LOCATION_GROUP] = HCMD_ARR(iwl_mld_location_names),
	[REGULATORY_AND_NVM_GROUP] = HCMD_ARR(iwl_mld_reg_and_nvm_names),
	[DEBUG_GROUP] = HCMD_ARR(iwl_mld_debug_names),
	[PHY_OPS_GROUP] = HCMD_ARR(iwl_mld_phy_names),
	[STATISTICS_GROUP] = HCMD_ARR(iwl_mld_statistics_names),
	[PROT_OFFLOAD_GROUP] = HCMD_ARR(iwl_mld_prot_offload_names),
	[BT_COEX_GROUP] = HCMD_ARR(iwl_mld_coex_names),
};
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_groups);

#if IS_ENABLED(CONFIG_IWLWIFI_KUNIT_TESTS)
const unsigned int global_iwl_mld_goups_size = ARRAY_SIZE(iwl_mld_groups);
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(global_iwl_mld_goups_size);
#endif

static void
iwl_mld_configure_trans(struct iwl_op_mode *op_mode)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);
	static const u8 no_reclaim_cmds[] = {TX_CMD};
	struct iwl_trans *trans = mld->trans;
	u32 eckv_value;

	iwl_bios_setup_step(trans, &mld->fwrt);
	iwl_uefi_get_step_table(trans);

	if (iwl_bios_get_eckv(&mld->fwrt, &eckv_value))
		IWL_DEBUG_RADIO(mld, "ECKV table doesn't exist in BIOS\n");
	else
		trans->conf.ext_32khz_clock_valid = !!eckv_value;

	trans->conf.rx_buf_size = iwl_amsdu_size_to_rxb_size();
	trans->conf.command_groups = iwl_mld_groups;
	trans->conf.command_groups_size = ARRAY_SIZE(iwl_mld_groups);
	trans->conf.fw_reset_handshake = true;
	trans->conf.queue_alloc_cmd_ver =
		iwl_fw_lookup_cmd_ver(mld->fw, WIDE_ID(DATA_PATH_GROUP,
						       SCD_QUEUE_CONFIG_CMD),
				      0);
	trans->conf.cb_data_offs = offsetof(struct ieee80211_tx_info,
					    driver_data[2]);
	BUILD_BUG_ON(sizeof(no_reclaim_cmds) >
		     sizeof(trans->conf.no_reclaim_cmds));
	memcpy(trans->conf.no_reclaim_cmds, no_reclaim_cmds,
	       sizeof(no_reclaim_cmds));
	trans->conf.n_no_reclaim_cmds = ARRAY_SIZE(no_reclaim_cmds);

	trans->conf.rx_mpdu_cmd = REPLY_RX_MPDU_CMD;
	trans->conf.rx_mpdu_cmd_hdr_size = sizeof(struct iwl_rx_mpdu_desc);
	trans->conf.wide_cmd_header = true;

	iwl_trans_op_mode_enter(trans, op_mode);
}

/*
 *****************************************************
 * op mode ops functions
 *****************************************************
 */

#define NUM_FW_LOAD_RETRIES	3
static struct iwl_op_mode *
iwl_op_mode_mld_start(struct iwl_trans *trans, const struct iwl_rf_cfg *cfg,
		      const struct iwl_fw *fw, struct dentry *dbgfs_dir)
{
	struct ieee80211_hw *hw;
	struct iwl_op_mode *op_mode;
	struct iwl_mld *mld;
	int ret;

	/* Allocate and initialize a new hardware device */
	hw = ieee80211_alloc_hw(sizeof(struct iwl_op_mode) +
				sizeof(struct iwl_mld),
				&iwl_mld_hw_ops);
	if (!hw)
		return ERR_PTR(-ENOMEM);

	op_mode = hw->priv;

	op_mode->ops = &iwl_mld_ops;

	mld = IWL_OP_MODE_GET_MLD(op_mode);

	iwl_construct_mld(mld, trans, cfg, fw, hw, dbgfs_dir);

	/* we'll verify later it matches between commands */
	mld->fw_rates_ver_3 = iwl_fw_lookup_cmd_ver(mld->fw, TX_CMD, 0) >= 11;

	iwl_mld_construct_fw_runtime(mld, trans, fw, dbgfs_dir);

	iwl_mld_get_bios_tables(mld);
	iwl_uefi_get_sgom_table(trans, &mld->fwrt);
	mld->bios_enable_puncturing = iwl_uefi_get_puncturing(&mld->fwrt);

	iwl_mld_hw_set_regulatory(mld);

	/* Configure transport layer with the opmode specific params */
	iwl_mld_configure_trans(op_mode);

	/* needed for regulatory init */
	rtnl_lock();
	/* Needed for sending commands */
	wiphy_lock(mld->wiphy);

	for (int i = 0; i < NUM_FW_LOAD_RETRIES; i++) {
		ret = iwl_mld_load_fw(mld);
		if (!ret)
			break;
	}

	if (!ret) {
		mld->nvm_data = iwl_get_nvm(mld->trans, mld->fw, 0, 0);
		if (IS_ERR(mld->nvm_data)) {
			IWL_ERR(mld, "Failed to read NVM: %d\n", ret);
			ret = PTR_ERR(mld->nvm_data);
		}
	}

	if (ret) {
		wiphy_unlock(mld->wiphy);
		rtnl_unlock();
		goto err;
	}

	/* We are about to stop the FW. Notifications may require an
	 * operational FW, so handle them all here before we stop.
	 */
	wiphy_work_flush(mld->wiphy, &mld->async_handlers_wk);

	iwl_mld_stop_fw(mld);

	wiphy_unlock(mld->wiphy);
	rtnl_unlock();

	ret = iwl_mld_leds_init(mld);
	if (ret)
		goto free_nvm;

	ret = iwl_mld_alloc_scan_cmd(mld);
	if (ret)
		goto leds_exit;

	ret = iwl_mld_low_latency_init(mld);
	if (ret)
		goto free_scan_cmd;

	ret = iwl_mld_register_hw(mld);
	if (ret)
		goto low_latency_free;

	iwl_mld_toggle_tx_ant(mld, &mld->mgmt_tx_ant);

	iwl_mld_add_debugfs_files(mld, dbgfs_dir);
	iwl_mld_thermal_initialize(mld);

	iwl_mld_ptp_init(mld);

	return op_mode;

low_latency_free:
	iwl_mld_low_latency_free(mld);
free_scan_cmd:
	kfree(mld->scan.cmd);
leds_exit:
	iwl_mld_leds_exit(mld);
free_nvm:
	kfree(mld->nvm_data);
err:
	iwl_trans_op_mode_leave(mld->trans);
	ieee80211_free_hw(mld->hw);
	return ERR_PTR(ret);
}

static void
iwl_op_mode_mld_stop(struct iwl_op_mode *op_mode)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);

	iwl_mld_ptp_remove(mld);
	iwl_mld_leds_exit(mld);

	iwl_mld_thermal_exit(mld);

	wiphy_lock(mld->wiphy);
	iwl_mld_low_latency_stop(mld);
	iwl_mld_deinit_time_sync(mld);
	wiphy_unlock(mld->wiphy);

	ieee80211_unregister_hw(mld->hw);

	iwl_fw_runtime_free(&mld->fwrt);
	iwl_mld_low_latency_free(mld);

	iwl_trans_op_mode_leave(mld->trans);

	kfree(mld->nvm_data);
	kfree(mld->scan.cmd);
	kfree(mld->channel_survey);
	kfree(mld->error_recovery_buf);
	kfree(mld->mcast_filter_cmd);

	ieee80211_free_hw(mld->hw);
}

static void iwl_mld_queue_state_change(struct iwl_op_mode *op_mode,
				       int hw_queue, bool queue_full)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);
	struct ieee80211_txq *txq;
	struct iwl_mld_sta *mld_sta;
	struct iwl_mld_txq *mld_txq;

	rcu_read_lock();

	txq = rcu_dereference(mld->fw_id_to_txq[hw_queue]);
	if (!txq) {
		rcu_read_unlock();

		if (queue_full) {
			/* An internal queue is not expected to become full */
			IWL_WARN(mld,
				 "Internal hw_queue %d is full! stopping all queues\n",
				 hw_queue);
			/* Stop all queues, as an internal queue is not
			 * mapped to a mac80211 one
			 */
			ieee80211_stop_queues(mld->hw);
		} else {
			ieee80211_wake_queues(mld->hw);
		}

		return;
	}

	mld_txq = iwl_mld_txq_from_mac80211(txq);
	mld_sta = txq->sta ? iwl_mld_sta_from_mac80211(txq->sta) : NULL;

	mld_txq->status.stop_full = queue_full;

	if (!queue_full && mld_sta &&
	    mld_sta->sta_state != IEEE80211_STA_NOTEXIST) {
		local_bh_disable();
		iwl_mld_tx_from_txq(mld, txq);
		local_bh_enable();
	}

	rcu_read_unlock();
}

static void
iwl_mld_queue_full(struct iwl_op_mode *op_mode, int hw_queue)
{
	iwl_mld_queue_state_change(op_mode, hw_queue, true);
}

static void
iwl_mld_queue_not_full(struct iwl_op_mode *op_mode, int hw_queue)
{
	iwl_mld_queue_state_change(op_mode, hw_queue, false);
}

static bool
iwl_mld_set_hw_rfkill_state(struct iwl_op_mode *op_mode, bool state)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);

	iwl_mld_set_hwkill(mld, state);

	return false;
}

static void
iwl_mld_free_skb(struct iwl_op_mode *op_mode, struct sk_buff *skb)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	iwl_trans_free_tx_cmd(mld->trans, info->driver_data[1]);
	ieee80211_free_txskb(mld->hw, skb);
}

static void iwl_mld_read_error_recovery_buffer(struct iwl_mld *mld)
{
	u32 src_size = mld->fw->ucode_capa.error_log_size;
	u32 src_addr = mld->fw->ucode_capa.error_log_addr;
	u8 *recovery_buf;
	int ret;

	/* no recovery buffer size defined in a TLV */
	if (!src_size)
		return;

	recovery_buf = kzalloc(src_size, GFP_ATOMIC);
	if (!recovery_buf)
		return;

	ret = iwl_trans_read_mem_bytes(mld->trans, src_addr,
				       recovery_buf, src_size);
	if (ret) {
		IWL_ERR(mld, "Failed to read error recovery buffer (%d)\n",
			ret);
		kfree(recovery_buf);
		return;
	}

	mld->error_recovery_buf = recovery_buf;
}

static void iwl_mld_restart_nic(struct iwl_mld *mld)
{
	iwl_mld_read_error_recovery_buffer(mld);

	mld->fwrt.trans->dbg.restart_required = false;

	ieee80211_restart_hw(mld->hw);
}

static void
iwl_mld_nic_error(struct iwl_op_mode *op_mode,
		  enum iwl_fw_error_type type)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);
	bool trans_dead = iwl_trans_is_dead(mld->trans);

	if (type == IWL_ERR_TYPE_CMD_QUEUE_FULL)
		IWL_ERR(mld, "Command queue full!\n");
	else if (!trans_dead && !mld->fw_status.do_not_dump_once)
		iwl_fwrt_dump_error_logs(&mld->fwrt);

	mld->fw_status.do_not_dump_once = false;

	/* It is necessary to abort any os scan here because mac80211 requires
	 * having the scan cleared before restarting.
	 * We'll reset the scan_status to NONE in restart cleanup in
	 * the next drv_start() call from mac80211. If ieee80211_hw_restart
	 * isn't called scan status will stay busy.
	 */
	iwl_mld_report_scan_aborted(mld);

	/*
	 * This should be first thing before trying to collect any
	 * data to avoid endless loops if any HW error happens while
	 * collecting debug data.
	 * It might not actually be true that we'll restart, but the
	 * setting doesn't matter if we're going to be unbound either.
	 */
	if (type != IWL_ERR_TYPE_RESET_HS_TIMEOUT &&
	    mld->fw_status.running)
		mld->fw_status.in_hw_restart = true;
}

static void iwl_mld_dump_error(struct iwl_op_mode *op_mode,
			       struct iwl_fw_error_dump_mode *mode)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);

	/* if we come in from opmode we have the mutex held */
	if (mode->context == IWL_ERR_CONTEXT_FROM_OPMODE) {
		lockdep_assert_wiphy(mld->wiphy);
		iwl_fw_error_collect(&mld->fwrt);
	} else {
		wiphy_lock(mld->wiphy);
		if (mode->context != IWL_ERR_CONTEXT_ABORT)
			iwl_fw_error_collect(&mld->fwrt);
		wiphy_unlock(mld->wiphy);
	}
}

static bool iwl_mld_sw_reset(struct iwl_op_mode *op_mode,
			     enum iwl_fw_error_type type)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);

	/* SW reset can happen for TOP error w/o NIC error, so
	 * also abort scan here and set in_hw_restart, when we
	 * had a NIC error both were already done.
	 */
	iwl_mld_report_scan_aborted(mld);
	mld->fw_status.in_hw_restart = true;

	/* Do restart only in the following conditions are met:
	 * - we consider the FW as running
	 * - The trigger that brought us here is defined as one that requires
	 *   a restart (in the debug TLVs)
	 */
	if (!mld->fw_status.running || !mld->fwrt.trans->dbg.restart_required)
		return false;

	iwl_mld_restart_nic(mld);
	return true;
}

static void
iwl_mld_time_point(struct iwl_op_mode *op_mode,
		   enum iwl_fw_ini_time_point tp_id,
		   union iwl_dbg_tlv_tp_data *tp_data)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);

	iwl_dbg_tlv_time_point(&mld->fwrt, tp_id, tp_data);
}

#ifdef CONFIG_PM_SLEEP
static void iwl_mld_device_powered_off(struct iwl_op_mode *op_mode)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);

	wiphy_lock(mld->wiphy);
	iwl_mld_stop_fw(mld);
	mld->fw_status.in_d3 = false;
	wiphy_unlock(mld->wiphy);
}
#else
static void iwl_mld_device_powered_off(struct iwl_op_mode *op_mode)
{}
#endif

static void iwl_mld_dump(struct iwl_op_mode *op_mode)
{
	struct iwl_mld *mld = IWL_OP_MODE_GET_MLD(op_mode);
	struct iwl_fw_runtime *fwrt = &mld->fwrt;

	if (!iwl_trans_fw_running(fwrt->trans))
		return;

	iwl_dbg_tlv_time_point(fwrt, IWL_FW_INI_TIME_POINT_USER_TRIGGER, NULL);
}

static const struct iwl_op_mode_ops iwl_mld_ops = {
	.start = iwl_op_mode_mld_start,
	.stop = iwl_op_mode_mld_stop,
	.rx = iwl_mld_rx,
	.rx_rss = iwl_mld_rx_rss,
	.queue_full = iwl_mld_queue_full,
	.queue_not_full = iwl_mld_queue_not_full,
	.hw_rf_kill = iwl_mld_set_hw_rfkill_state,
	.free_skb = iwl_mld_free_skb,
	.nic_error = iwl_mld_nic_error,
	.dump_error = iwl_mld_dump_error,
	.sw_reset = iwl_mld_sw_reset,
	.time_point = iwl_mld_time_point,
	.device_powered_off = pm_sleep_ptr(iwl_mld_device_powered_off),
	.dump = iwl_mld_dump,
};

struct iwl_mld_mod_params iwlmld_mod_params = {
	.power_scheme = IWL_POWER_SCHEME_BPS,
};

module_param_named(power_scheme, iwlmld_mod_params.power_scheme, int, 0444);
MODULE_PARM_DESC(power_scheme,
		 "power management scheme: 1-active, 2-balanced, default: 2");
