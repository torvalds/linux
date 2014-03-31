/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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
#include <net/mac80211.h>

#include "iwl-notif-wait.h"
#include "iwl-trans.h"
#include "iwl-op-mode.h"
#include "iwl-fw.h"
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
#include "fw-api-scan.h"
#include "time-event.h"

/*
 * module name, copyright, version, etc.
 */
#define DRV_DESCRIPTION	"The new Intel(R) wireless AGN driver for Linux"

#define DRV_VERSION     IWLWIFI_VERSION

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");

static const struct iwl_op_mode_ops iwl_mvm_ops;

struct iwl_mvm_mod_params iwlmvm_mod_params = {
	.power_scheme = IWL_POWER_SCHEME_BPS,
	/* rest of fields are 0 by default */
};

module_param_named(init_dbg, iwlmvm_mod_params.init_dbg, bool, S_IRUGO);
MODULE_PARM_DESC(init_dbg,
		 "set to true to debug an ASSERT in INIT fw (default: false");
module_param_named(power_scheme, iwlmvm_mod_params.power_scheme, int, S_IRUGO);
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

	if (ret) {
		pr_err("Unable to register MVM op_mode: %d\n", ret);
		iwl_mvm_rate_control_unregister();
	}

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
	u32 reg_val = 0;

	radio_cfg_type = (mvm->fw->phy_config & FW_PHY_CFG_RADIO_TYPE) >>
			  FW_PHY_CFG_RADIO_TYPE_POS;
	radio_cfg_step = (mvm->fw->phy_config & FW_PHY_CFG_RADIO_STEP) >>
			  FW_PHY_CFG_RADIO_STEP_POS;
	radio_cfg_dash = (mvm->fw->phy_config & FW_PHY_CFG_RADIO_DASH) >>
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

	/* silicon bits */
	reg_val |= CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI;

	iwl_trans_set_bits_mask(mvm->trans, CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_MSK_MAC_DASH |
				CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP |
				CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE |
				CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP |
				CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH |
				CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
				CSR_HW_IF_CONFIG_REG_BIT_MAC_SI,
				reg_val);

	IWL_DEBUG_INFO(mvm, "Radio type=0x%x-0x%x-0x%x\n", radio_cfg_type,
		       radio_cfg_step, radio_cfg_dash);

	/*
	 * W/A : NIC is stuck in a reset state after Early PCIe power off
	 * (PCIe power is lost before PERST# is asserted), causing ME FW
	 * to lose ownership and not being able to obtain it back.
	 */
	iwl_set_bits_mask_prph(mvm->trans, APMG_PS_CTRL_REG,
			       APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS,
			       ~APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS);
}

struct iwl_rx_handlers {
	u8 cmd_id;
	bool async;
	int (*fn)(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb,
		  struct iwl_device_cmd *cmd);
};

#define RX_HANDLER(_cmd_id, _fn, _async)	\
	{ .cmd_id = _cmd_id , .fn = _fn , .async = _async }

/*
 * Handlers for fw notifications
 * Convention: RX_HANDLER(CMD_NAME, iwl_mvm_rx_CMD_NAME
 * This list should be in order of frequency for performance purposes.
 *
 * The handler can be SYNC - this means that it will be called in the Rx path
 * which can't acquire mvm->mutex. If the handler needs to hold mvm->mutex (and
 * only in this case!), it should be set as ASYNC. In that case, it will be
 * called from a worker with mvm->mutex held.
 */
static const struct iwl_rx_handlers iwl_mvm_rx_handlers[] = {
	RX_HANDLER(REPLY_RX_MPDU_CMD, iwl_mvm_rx_rx_mpdu, false),
	RX_HANDLER(REPLY_RX_PHY_CMD, iwl_mvm_rx_rx_phy_cmd, false),
	RX_HANDLER(TX_CMD, iwl_mvm_rx_tx_cmd, false),
	RX_HANDLER(BA_NOTIF, iwl_mvm_rx_ba_notif, false),

	RX_HANDLER(BT_PROFILE_NOTIFICATION, iwl_mvm_rx_bt_coex_notif, true),
	RX_HANDLER(BEACON_NOTIFICATION, iwl_mvm_rx_beacon_notif, false),
	RX_HANDLER(STATISTICS_NOTIFICATION, iwl_mvm_rx_statistics, true),

	RX_HANDLER(TIME_EVENT_NOTIFICATION, iwl_mvm_rx_time_event_notif, false),

	RX_HANDLER(SCAN_REQUEST_CMD, iwl_mvm_rx_scan_response, false),
	RX_HANDLER(SCAN_COMPLETE_NOTIFICATION, iwl_mvm_rx_scan_complete, false),
	RX_HANDLER(SCAN_OFFLOAD_COMPLETE,
		   iwl_mvm_rx_scan_offload_complete_notif, false),
	RX_HANDLER(MATCH_FOUND_NOTIFICATION, iwl_mvm_rx_sched_scan_results,
		   false),

	RX_HANDLER(RADIO_VERSION_NOTIFICATION, iwl_mvm_rx_radio_ver, false),
	RX_HANDLER(CARD_STATE_NOTIFICATION, iwl_mvm_rx_card_state_notif, false),

	RX_HANDLER(MISSED_BEACONS_NOTIFICATION, iwl_mvm_rx_missed_beacons_notif,
		   false),

	RX_HANDLER(REPLY_ERROR, iwl_mvm_rx_fw_error, false),
	RX_HANDLER(PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION,
		   iwl_mvm_power_uapsd_misbehaving_ap_notif, false),
};
#undef RX_HANDLER
#define CMD(x) [x] = #x

static const char *iwl_mvm_cmd_strings[REPLY_MAX] = {
	CMD(MVM_ALIVE),
	CMD(REPLY_ERROR),
	CMD(INIT_COMPLETE_NOTIF),
	CMD(PHY_CONTEXT_CMD),
	CMD(MGMT_MCAST_KEY),
	CMD(TX_CMD),
	CMD(TXPATH_FLUSH),
	CMD(MAC_CONTEXT_CMD),
	CMD(TIME_EVENT_CMD),
	CMD(TIME_EVENT_NOTIFICATION),
	CMD(BINDING_CONTEXT_CMD),
	CMD(TIME_QUOTA_CMD),
	CMD(NON_QOS_TX_COUNTER_CMD),
	CMD(RADIO_VERSION_NOTIFICATION),
	CMD(SCAN_REQUEST_CMD),
	CMD(SCAN_ABORT_CMD),
	CMD(SCAN_START_NOTIFICATION),
	CMD(SCAN_RESULTS_NOTIFICATION),
	CMD(SCAN_COMPLETE_NOTIFICATION),
	CMD(NVM_ACCESS_CMD),
	CMD(PHY_CONFIGURATION_CMD),
	CMD(CALIB_RES_NOTIF_PHY_DB),
	CMD(SET_CALIB_DEFAULT_CMD),
	CMD(CALIBRATION_COMPLETE_NOTIFICATION),
	CMD(ADD_STA_KEY),
	CMD(ADD_STA),
	CMD(REMOVE_STA),
	CMD(LQ_CMD),
	CMD(SCAN_OFFLOAD_CONFIG_CMD),
	CMD(MATCH_FOUND_NOTIFICATION),
	CMD(SCAN_OFFLOAD_REQUEST_CMD),
	CMD(SCAN_OFFLOAD_ABORT_CMD),
	CMD(SCAN_OFFLOAD_COMPLETE),
	CMD(SCAN_OFFLOAD_UPDATE_PROFILES_CMD),
	CMD(POWER_TABLE_CMD),
	CMD(WEP_KEY),
	CMD(REPLY_RX_PHY_CMD),
	CMD(REPLY_RX_MPDU_CMD),
	CMD(BEACON_NOTIFICATION),
	CMD(BEACON_TEMPLATE_CMD),
	CMD(STATISTICS_NOTIFICATION),
	CMD(REDUCE_TX_POWER_CMD),
	CMD(TX_ANT_CONFIGURATION_CMD),
	CMD(D3_CONFIG_CMD),
	CMD(PROT_OFFLOAD_CONFIG_CMD),
	CMD(OFFLOADS_QUERY_CMD),
	CMD(REMOTE_WAKE_CONFIG_CMD),
	CMD(WOWLAN_PATTERNS),
	CMD(WOWLAN_CONFIGURATION),
	CMD(WOWLAN_TSC_RSC_PARAM),
	CMD(WOWLAN_TKIP_PARAM),
	CMD(WOWLAN_KEK_KCK_MATERIAL),
	CMD(WOWLAN_GET_STATUSES),
	CMD(WOWLAN_TX_POWER_PER_DB),
	CMD(NET_DETECT_CONFIG_CMD),
	CMD(NET_DETECT_PROFILES_QUERY_CMD),
	CMD(NET_DETECT_PROFILES_CMD),
	CMD(NET_DETECT_HOTSPOTS_CMD),
	CMD(NET_DETECT_HOTSPOTS_QUERY_CMD),
	CMD(CARD_STATE_NOTIFICATION),
	CMD(MISSED_BEACONS_NOTIFICATION),
	CMD(BT_COEX_PRIO_TABLE),
	CMD(BT_COEX_PROT_ENV),
	CMD(BT_PROFILE_NOTIFICATION),
	CMD(BT_CONFIG),
	CMD(MCAST_FILTER_CMD),
	CMD(REPLY_SF_CFG_CMD),
	CMD(REPLY_BEACON_FILTERING_CMD),
	CMD(REPLY_THERMAL_MNG_BACKOFF),
	CMD(MAC_PM_POWER_TABLE),
	CMD(BT_COEX_CI),
	CMD(PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION),
};
#undef CMD

/* this forward declaration can avoid to export the function */
static void iwl_mvm_async_handlers_wk(struct work_struct *wk);

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

	/********************************
	 * 1. Allocating and configuring HW data
	 ********************************/
	hw = ieee80211_alloc_hw(sizeof(struct iwl_op_mode) +
				sizeof(struct iwl_mvm),
				&iwl_mvm_hw_ops);
	if (!hw)
		return NULL;

	op_mode = hw->priv;
	op_mode->ops = &iwl_mvm_ops;

	mvm = IWL_OP_MODE_GET_MVM(op_mode);
	mvm->dev = trans->dev;
	mvm->trans = trans;
	mvm->cfg = cfg;
	mvm->fw = fw;
	mvm->hw = hw;

	mvm->restart_fw = iwlwifi_mod_params.restart_fw ? -1 : 0;

	mvm->aux_queue = 15;
	mvm->first_agg_queue = 16;
	mvm->last_agg_queue = mvm->cfg->base_params->num_of_queues - 1;
	if (mvm->cfg->base_params->num_of_queues == 16) {
		mvm->aux_queue = 11;
		mvm->first_agg_queue = 12;
	}
	mvm->sf_state = SF_UNINIT;

	mutex_init(&mvm->mutex);
	spin_lock_init(&mvm->async_handlers_lock);
	INIT_LIST_HEAD(&mvm->time_event_list);
	INIT_LIST_HEAD(&mvm->async_handlers_list);
	spin_lock_init(&mvm->time_event_lock);

	INIT_WORK(&mvm->async_handlers_wk, iwl_mvm_async_handlers_wk);
	INIT_WORK(&mvm->roc_done_wk, iwl_mvm_roc_done_wk);
	INIT_WORK(&mvm->sta_drained_wk, iwl_mvm_sta_drained_wk);

	SET_IEEE80211_DEV(mvm->hw, mvm->trans->dev);

	/*
	 * Populate the state variables that the transport layer needs
	 * to know about.
	 */
	trans_cfg.op_mode = op_mode;
	trans_cfg.no_reclaim_cmds = no_reclaim_cmds;
	trans_cfg.n_no_reclaim_cmds = ARRAY_SIZE(no_reclaim_cmds);
	trans_cfg.rx_buf_size_8k = iwlwifi_mod_params.amsdu_size_8K;

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_DW_BC_TABLE)
		trans_cfg.bc_table_dword = true;

	if (!iwlwifi_mod_params.wd_disable)
		trans_cfg.queue_watchdog_timeout = cfg->base_params->wd_timeout;
	else
		trans_cfg.queue_watchdog_timeout = IWL_WATCHDOG_DISABLED;

	trans_cfg.command_names = iwl_mvm_cmd_strings;

	trans_cfg.cmd_queue = IWL_MVM_CMD_QUEUE;
	trans_cfg.cmd_fifo = IWL_MVM_CMD_FIFO;

	snprintf(mvm->hw->wiphy->fw_version,
		 sizeof(mvm->hw->wiphy->fw_version),
		 "%s", fw->fw_version);

	/* Configure transport layer */
	iwl_trans_configure(mvm->trans, &trans_cfg);

	trans->rx_mpdu_cmd = REPLY_RX_MPDU_CMD;
	trans->rx_mpdu_cmd_hdr_size = sizeof(struct iwl_rx_mpdu_res_start);

	/* set up notification wait support */
	iwl_notification_wait_init(&mvm->notif_wait);

	/* Init phy db */
	mvm->phy_db = iwl_phy_db_init(trans);
	if (!mvm->phy_db) {
		IWL_ERR(mvm, "Cannot init phy_db\n");
		goto out_free;
	}

	IWL_INFO(mvm, "Detected %s, REV=0x%X\n",
		 mvm->cfg->name, mvm->trans->hw_rev);

	iwl_mvm_tt_initialize(mvm);

	/*
	 * If the NVM exists in an external file,
	 * there is no need to unnecessarily power up the NIC at driver load
	 */
	if (iwlwifi_mod_params.nvm_file) {
		err = iwl_nvm_init(mvm);
		if (err)
			goto out_free;
	} else {
		err = iwl_trans_start_hw(mvm->trans);
		if (err)
			goto out_free;

		mutex_lock(&mvm->mutex);
		err = iwl_run_init_mvm_ucode(mvm, true);
		iwl_trans_stop_device(trans);
		mutex_unlock(&mvm->mutex);
		/* returns 0 if successful, 1 if success but in rfkill */
		if (err < 0 && !iwlmvm_mod_params.init_dbg) {
			IWL_ERR(mvm, "Failed to run INIT ucode: %d\n", err);
			goto out_free;
		}
	}

	scan_size = sizeof(struct iwl_scan_cmd) +
		mvm->fw->ucode_capa.max_probe_length +
		(MAX_NUM_SCAN_CHANNELS * sizeof(struct iwl_scan_channel));
	mvm->scan_cmd = kmalloc(scan_size, GFP_KERNEL);
	if (!mvm->scan_cmd)
		goto out_free;

	err = iwl_mvm_mac_setup_register(mvm);
	if (err)
		goto out_free;

	err = iwl_mvm_dbgfs_register(mvm, dbgfs_dir);
	if (err)
		goto out_unregister;

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_PM_CMD_SUPPORT)
		mvm->pm_ops = &pm_mac_ops;
	else
		mvm->pm_ops = &pm_legacy_ops;

	memset(&mvm->rx_stats, 0, sizeof(struct mvm_statistics_rx));

	return op_mode;

 out_unregister:
	ieee80211_unregister_hw(mvm->hw);
	iwl_mvm_leds_exit(mvm);
 out_free:
	iwl_phy_db_free(mvm->phy_db);
	kfree(mvm->scan_cmd);
	if (!iwlwifi_mod_params.nvm_file)
		iwl_trans_op_mode_leave(trans);
	ieee80211_free_hw(mvm->hw);
	return NULL;
}

static void iwl_op_mode_mvm_stop(struct iwl_op_mode *op_mode)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	int i;

	iwl_mvm_leds_exit(mvm);

	iwl_mvm_tt_exit(mvm);

	ieee80211_unregister_hw(mvm->hw);

	kfree(mvm->scan_cmd);
	kfree(mvm->mcast_filter_cmd);
	mvm->mcast_filter_cmd = NULL;

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_IWLWIFI_DEBUGFS)
	kfree(mvm->d3_resume_sram);
#endif

	iwl_trans_op_mode_leave(mvm->trans);

	iwl_phy_db_free(mvm->phy_db);
	mvm->phy_db = NULL;

	iwl_free_nvm_data(mvm->nvm_data);
	for (i = 0; i < NVM_NUM_OF_SECTIONS; i++)
		kfree(mvm->nvm_sections[i].data);

	ieee80211_free_hw(mvm->hw);
}

struct iwl_async_handler_entry {
	struct list_head list;
	struct iwl_rx_cmd_buffer rxb;
	int (*fn)(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb,
		  struct iwl_device_cmd *cmd);
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
	struct list_head local_list;

	INIT_LIST_HEAD(&local_list);

	/* Ensure that we are not in stop flow (check iwl_mvm_mac_stop) */
	mutex_lock(&mvm->mutex);

	/*
	 * Sync with Rx path with a lock. Remove all the entries from this list,
	 * add them to a local one (lock free), and then handle them.
	 */
	spin_lock_bh(&mvm->async_handlers_lock);
	list_splice_init(&mvm->async_handlers_list, &local_list);
	spin_unlock_bh(&mvm->async_handlers_lock);

	list_for_each_entry_safe(entry, tmp, &local_list, list) {
		if (entry->fn(mvm, &entry->rxb, NULL))
			IWL_WARN(mvm,
				 "returned value from ASYNC handlers are ignored\n");
		iwl_free_rxb(&entry->rxb);
		list_del(&entry->list);
		kfree(entry);
	}
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_rx_dispatch(struct iwl_op_mode *op_mode,
			       struct iwl_rx_cmd_buffer *rxb,
			       struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	u8 i;

	/*
	 * Do the notification wait before RX handlers so
	 * even if the RX handler consumes the RXB we have
	 * access to it in the notification wait entry.
	 */
	iwl_notification_wait_notify(&mvm->notif_wait, pkt);

	for (i = 0; i < ARRAY_SIZE(iwl_mvm_rx_handlers); i++) {
		const struct iwl_rx_handlers *rx_h = &iwl_mvm_rx_handlers[i];
		struct iwl_async_handler_entry *entry;

		if (rx_h->cmd_id != pkt->hdr.cmd)
			continue;

		if (!rx_h->async)
			return rx_h->fn(mvm, rxb, cmd);

		entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
		/* we can't do much... */
		if (!entry)
			return 0;

		entry->rxb._page = rxb_steal_page(rxb);
		entry->rxb._offset = rxb->_offset;
		entry->rxb._rx_page_order = rxb->_rx_page_order;
		entry->fn = rx_h->fn;
		spin_lock(&mvm->async_handlers_lock);
		list_add_tail(&entry->list, &mvm->async_handlers_list);
		spin_unlock(&mvm->async_handlers_lock);
		schedule_work(&mvm->async_handlers_wk);
		break;
	}

	return 0;
}

static void iwl_mvm_stop_sw_queue(struct iwl_op_mode *op_mode, int queue)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	int mq = mvm->queue_to_mac80211[queue];

	if (WARN_ON_ONCE(mq == IWL_INVALID_MAC80211_QUEUE))
		return;

	if (atomic_inc_return(&mvm->queue_stop_count[mq]) > 1) {
		IWL_DEBUG_TX_QUEUES(mvm,
				    "queue %d (mac80211 %d) already stopped\n",
				    queue, mq);
		return;
	}

	set_bit(mq, &mvm->transport_queue_stop);
	ieee80211_stop_queue(mvm->hw, mq);
}

static void iwl_mvm_wake_sw_queue(struct iwl_op_mode *op_mode, int queue)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);
	int mq = mvm->queue_to_mac80211[queue];

	if (WARN_ON_ONCE(mq == IWL_INVALID_MAC80211_QUEUE))
		return;

	if (atomic_dec_return(&mvm->queue_stop_count[mq]) > 0) {
		IWL_DEBUG_TX_QUEUES(mvm,
				    "queue %d (mac80211 %d) already awake\n",
				    queue, mq);
		return;
	}

	clear_bit(mq, &mvm->transport_queue_stop);

	ieee80211_wake_queue(mvm->hw, mq);
}

void iwl_mvm_set_hw_ctkill_state(struct iwl_mvm *mvm, bool state)
{
	if (state)
		set_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status);
	else
		clear_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status);

	wiphy_rfkill_set_hw_state(mvm->hw->wiphy, iwl_mvm_is_radio_killed(mvm));
}

static void iwl_mvm_set_hw_rfkill_state(struct iwl_op_mode *op_mode, bool state)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	if (state)
		set_bit(IWL_MVM_STATUS_HW_RFKILL, &mvm->status);
	else
		clear_bit(IWL_MVM_STATUS_HW_RFKILL, &mvm->status);

	if (state && mvm->cur_ucode != IWL_UCODE_INIT)
		iwl_trans_stop_device(mvm->trans);
	wiphy_rfkill_set_hw_state(mvm->hw->wiphy, iwl_mvm_is_radio_killed(mvm));
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

static void iwl_mvm_nic_restart(struct iwl_mvm *mvm)
{
	iwl_abort_notification_waits(&mvm->notif_wait);

	/*
	 * If we're restarting already, don't cycle restarts.
	 * If INIT fw asserted, it will likely fail again.
	 * If WoWLAN fw asserted, don't restart either, mac80211
	 * can't recover this since we're already half suspended.
	 */
	if (test_and_set_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
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
	} else if (mvm->cur_ucode == IWL_UCODE_REGULAR && mvm->restart_fw) {
		/*
		 * This is a bit racy, but worst case we tell mac80211 about
		 * a stopped/aborted (sched) scan when that was already done
		 * which is not a problem. It is necessary to abort any scan
		 * here because mac80211 requires having the scan cleared
		 * before restarting.
		 * We'll reset the scan_status to NONE in restart cleanup in
		 * the next start() call from mac80211.
		 */
		switch (mvm->scan_status) {
		case IWL_MVM_SCAN_NONE:
			break;
		case IWL_MVM_SCAN_OS:
			ieee80211_scan_completed(mvm->hw, true);
			break;
		case IWL_MVM_SCAN_SCHED:
			ieee80211_sched_scan_stopped(mvm->hw);
			break;
		}

		if (mvm->restart_fw > 0)
			mvm->restart_fw--;
		ieee80211_restart_hw(mvm->hw);
	}
}

static void iwl_mvm_nic_error(struct iwl_op_mode *op_mode)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	iwl_mvm_dump_nic_error_log(mvm);
	if (!mvm->restart_fw)
		iwl_mvm_dump_sram(mvm);

	iwl_mvm_nic_restart(mvm);
}

static void iwl_mvm_cmd_queue_full(struct iwl_op_mode *op_mode)
{
	struct iwl_mvm *mvm = IWL_OP_MODE_GET_MVM(op_mode);

	WARN_ON(1);
	iwl_mvm_nic_restart(mvm);
}

static const struct iwl_op_mode_ops iwl_mvm_ops = {
	.start = iwl_op_mode_mvm_start,
	.stop = iwl_op_mode_mvm_stop,
	.rx = iwl_mvm_rx_dispatch,
	.queue_full = iwl_mvm_stop_sw_queue,
	.queue_not_full = iwl_mvm_wake_sw_queue,
	.hw_rf_kill = iwl_mvm_set_hw_rfkill_state,
	.free_skb = iwl_mvm_free_skb,
	.nic_error = iwl_mvm_nic_error,
	.cmd_queue_full = iwl_mvm_cmd_queue_full,
	.nic_config = iwl_mvm_nic_config,
};
