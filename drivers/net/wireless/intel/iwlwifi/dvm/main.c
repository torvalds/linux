/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 Intel Deutschland GmbH
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include <net/mac80211.h>

#include <asm/div64.h>

#include "iwl-eeprom-read.h"
#include "iwl-eeprom-parse.h"
#include "iwl-io.h"
#include "iwl-trans.h"
#include "iwl-op-mode.h"
#include "iwl-drv.h"
#include "iwl-modparams.h"
#include "iwl-prph.h"

#include "dev.h"
#include "calib.h"
#include "agn.h"


/******************************************************************************
 *
 * module boiler plate
 *
 ******************************************************************************/

#define DRV_DESCRIPTION	"Intel(R) Wireless WiFi Link AGN driver for Linux"
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");

/* Please keep this array *SORTED* by hex value.
 * Access is done through binary search.
 * A warning will be triggered on violation.
 */
static const struct iwl_hcmd_names iwl_dvm_cmd_names[] = {
	HCMD_NAME(REPLY_ALIVE),
	HCMD_NAME(REPLY_ERROR),
	HCMD_NAME(REPLY_ECHO),
	HCMD_NAME(REPLY_RXON),
	HCMD_NAME(REPLY_RXON_ASSOC),
	HCMD_NAME(REPLY_QOS_PARAM),
	HCMD_NAME(REPLY_RXON_TIMING),
	HCMD_NAME(REPLY_ADD_STA),
	HCMD_NAME(REPLY_REMOVE_STA),
	HCMD_NAME(REPLY_REMOVE_ALL_STA),
	HCMD_NAME(REPLY_TX),
	HCMD_NAME(REPLY_TXFIFO_FLUSH),
	HCMD_NAME(REPLY_WEPKEY),
	HCMD_NAME(REPLY_LEDS_CMD),
	HCMD_NAME(REPLY_TX_LINK_QUALITY_CMD),
	HCMD_NAME(COEX_PRIORITY_TABLE_CMD),
	HCMD_NAME(COEX_MEDIUM_NOTIFICATION),
	HCMD_NAME(COEX_EVENT_CMD),
	HCMD_NAME(TEMPERATURE_NOTIFICATION),
	HCMD_NAME(CALIBRATION_CFG_CMD),
	HCMD_NAME(CALIBRATION_RES_NOTIFICATION),
	HCMD_NAME(CALIBRATION_COMPLETE_NOTIFICATION),
	HCMD_NAME(REPLY_QUIET_CMD),
	HCMD_NAME(REPLY_CHANNEL_SWITCH),
	HCMD_NAME(CHANNEL_SWITCH_NOTIFICATION),
	HCMD_NAME(REPLY_SPECTRUM_MEASUREMENT_CMD),
	HCMD_NAME(SPECTRUM_MEASURE_NOTIFICATION),
	HCMD_NAME(POWER_TABLE_CMD),
	HCMD_NAME(PM_SLEEP_NOTIFICATION),
	HCMD_NAME(PM_DEBUG_STATISTIC_NOTIFIC),
	HCMD_NAME(REPLY_SCAN_CMD),
	HCMD_NAME(REPLY_SCAN_ABORT_CMD),
	HCMD_NAME(SCAN_START_NOTIFICATION),
	HCMD_NAME(SCAN_RESULTS_NOTIFICATION),
	HCMD_NAME(SCAN_COMPLETE_NOTIFICATION),
	HCMD_NAME(BEACON_NOTIFICATION),
	HCMD_NAME(REPLY_TX_BEACON),
	HCMD_NAME(WHO_IS_AWAKE_NOTIFICATION),
	HCMD_NAME(REPLY_TX_POWER_DBM_CMD),
	HCMD_NAME(QUIET_NOTIFICATION),
	HCMD_NAME(REPLY_TX_PWR_TABLE_CMD),
	HCMD_NAME(REPLY_TX_POWER_DBM_CMD_V1),
	HCMD_NAME(TX_ANT_CONFIGURATION_CMD),
	HCMD_NAME(MEASURE_ABORT_NOTIFICATION),
	HCMD_NAME(REPLY_BT_CONFIG),
	HCMD_NAME(REPLY_STATISTICS_CMD),
	HCMD_NAME(STATISTICS_NOTIFICATION),
	HCMD_NAME(REPLY_CARD_STATE_CMD),
	HCMD_NAME(CARD_STATE_NOTIFICATION),
	HCMD_NAME(MISSED_BEACONS_NOTIFICATION),
	HCMD_NAME(REPLY_CT_KILL_CONFIG_CMD),
	HCMD_NAME(SENSITIVITY_CMD),
	HCMD_NAME(REPLY_PHY_CALIBRATION_CMD),
	HCMD_NAME(REPLY_WIPAN_PARAMS),
	HCMD_NAME(REPLY_WIPAN_RXON),
	HCMD_NAME(REPLY_WIPAN_RXON_TIMING),
	HCMD_NAME(REPLY_WIPAN_RXON_ASSOC),
	HCMD_NAME(REPLY_WIPAN_QOS_PARAM),
	HCMD_NAME(REPLY_WIPAN_WEPKEY),
	HCMD_NAME(REPLY_WIPAN_P2P_CHANNEL_SWITCH),
	HCMD_NAME(REPLY_WIPAN_NOA_NOTIFICATION),
	HCMD_NAME(REPLY_WIPAN_DEACTIVATION_COMPLETE),
	HCMD_NAME(REPLY_RX_PHY_CMD),
	HCMD_NAME(REPLY_RX_MPDU_CMD),
	HCMD_NAME(REPLY_RX),
	HCMD_NAME(REPLY_COMPRESSED_BA),
	HCMD_NAME(REPLY_BT_COEX_PRIO_TABLE),
	HCMD_NAME(REPLY_BT_COEX_PROT_ENV),
	HCMD_NAME(REPLY_BT_COEX_PROFILE_NOTIF),
	HCMD_NAME(REPLY_D3_CONFIG),
	HCMD_NAME(REPLY_WOWLAN_PATTERNS),
	HCMD_NAME(REPLY_WOWLAN_WAKEUP_FILTER),
	HCMD_NAME(REPLY_WOWLAN_TSC_RSC_PARAMS),
	HCMD_NAME(REPLY_WOWLAN_TKIP_PARAMS),
	HCMD_NAME(REPLY_WOWLAN_KEK_KCK_MATERIAL),
	HCMD_NAME(REPLY_WOWLAN_GET_STATUS),
};

static const struct iwl_hcmd_arr iwl_dvm_groups[] = {
	[0x0] = HCMD_ARR(iwl_dvm_cmd_names),
};

static const struct iwl_op_mode_ops iwl_dvm_ops;

void iwl_update_chain_flags(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;

	for_each_context(priv, ctx) {
		iwlagn_set_rxon_chain(priv, ctx);
		if (ctx->active.rx_chain != ctx->staging.rx_chain)
			iwlagn_commit_rxon(priv, ctx);
	}
}

/* Parse the beacon frame to find the TIM element and set tim_idx & tim_size */
static void iwl_set_beacon_tim(struct iwl_priv *priv,
			       struct iwl_tx_beacon_cmd *tx_beacon_cmd,
			       u8 *beacon, u32 frame_size)
{
	u16 tim_idx;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)beacon;

	/*
	 * The index is relative to frame start but we start looking at the
	 * variable-length part of the beacon.
	 */
	tim_idx = mgmt->u.beacon.variable - beacon;

	/* Parse variable-length elements of beacon to find WLAN_EID_TIM */
	while ((tim_idx < (frame_size - 2)) &&
			(beacon[tim_idx] != WLAN_EID_TIM))
		tim_idx += beacon[tim_idx+1] + 2;

	/* If TIM field was found, set variables */
	if ((tim_idx < (frame_size - 1)) && (beacon[tim_idx] == WLAN_EID_TIM)) {
		tx_beacon_cmd->tim_idx = cpu_to_le16(tim_idx);
		tx_beacon_cmd->tim_size = beacon[tim_idx+1];
	} else
		IWL_WARN(priv, "Unable to find TIM Element in beacon\n");
}

int iwlagn_send_beacon_cmd(struct iwl_priv *priv)
{
	struct iwl_tx_beacon_cmd *tx_beacon_cmd;
	struct iwl_host_cmd cmd = {
		.id = REPLY_TX_BEACON,
	};
	struct ieee80211_tx_info *info;
	u32 frame_size;
	u32 rate_flags;
	u32 rate;

	/*
	 * We have to set up the TX command, the TX Beacon command, and the
	 * beacon contents.
	 */

	lockdep_assert_held(&priv->mutex);

	if (!priv->beacon_ctx) {
		IWL_ERR(priv, "trying to build beacon w/o beacon context!\n");
		return 0;
	}

	if (WARN_ON(!priv->beacon_skb))
		return -EINVAL;

	/* Allocate beacon command */
	if (!priv->beacon_cmd)
		priv->beacon_cmd = kzalloc(sizeof(*tx_beacon_cmd), GFP_KERNEL);
	tx_beacon_cmd = priv->beacon_cmd;
	if (!tx_beacon_cmd)
		return -ENOMEM;

	frame_size = priv->beacon_skb->len;

	/* Set up TX command fields */
	tx_beacon_cmd->tx.len = cpu_to_le16((u16)frame_size);
	tx_beacon_cmd->tx.sta_id = priv->beacon_ctx->bcast_sta_id;
	tx_beacon_cmd->tx.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;
	tx_beacon_cmd->tx.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK |
		TX_CMD_FLG_TSF_MSK | TX_CMD_FLG_STA_RATE_MSK;

	/* Set up TX beacon command fields */
	iwl_set_beacon_tim(priv, tx_beacon_cmd, priv->beacon_skb->data,
			   frame_size);

	/* Set up packet rate and flags */
	info = IEEE80211_SKB_CB(priv->beacon_skb);

	/*
	 * Let's set up the rate at least somewhat correctly;
	 * it will currently not actually be used by the uCode,
	 * it uses the broadcast station's rate instead.
	 */
	if (info->control.rates[0].idx < 0 ||
	    info->control.rates[0].flags & IEEE80211_TX_RC_MCS)
		rate = 0;
	else
		rate = info->control.rates[0].idx;

	priv->mgmt_tx_ant = iwl_toggle_tx_ant(priv, priv->mgmt_tx_ant,
					      priv->nvm_data->valid_tx_ant);
	rate_flags = iwl_ant_idx_to_flags(priv->mgmt_tx_ant);

	/* In mac80211, rates for 5 GHz start at 0 */
	if (info->band == NL80211_BAND_5GHZ)
		rate += IWL_FIRST_OFDM_RATE;
	else if (rate >= IWL_FIRST_CCK_RATE && rate <= IWL_LAST_CCK_RATE)
		rate_flags |= RATE_MCS_CCK_MSK;

	tx_beacon_cmd->tx.rate_n_flags =
			iwl_hw_set_rate_n_flags(rate, rate_flags);

	/* Submit command */
	cmd.len[0] = sizeof(*tx_beacon_cmd);
	cmd.data[0] = tx_beacon_cmd;
	cmd.dataflags[0] = IWL_HCMD_DFL_NOCOPY;
	cmd.len[1] = frame_size;
	cmd.data[1] = priv->beacon_skb->data;
	cmd.dataflags[1] = IWL_HCMD_DFL_NOCOPY;

	return iwl_dvm_send_cmd(priv, &cmd);
}

static void iwl_bg_beacon_update(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, beacon_update);
	struct sk_buff *beacon;

	mutex_lock(&priv->mutex);
	if (!priv->beacon_ctx) {
		IWL_ERR(priv, "updating beacon w/o beacon context!\n");
		goto out;
	}

	if (priv->beacon_ctx->vif->type != NL80211_IFTYPE_AP) {
		/*
		 * The ucode will send beacon notifications even in
		 * IBSS mode, but we don't want to process them. But
		 * we need to defer the type check to here due to
		 * requiring locking around the beacon_ctx access.
		 */
		goto out;
	}

	/* Pull updated AP beacon from mac80211. will fail if not in AP mode */
	beacon = ieee80211_beacon_get(priv->hw, priv->beacon_ctx->vif);
	if (!beacon) {
		IWL_ERR(priv, "update beacon failed -- keeping old\n");
		goto out;
	}

	/* new beacon skb is allocated every time; dispose previous.*/
	dev_kfree_skb(priv->beacon_skb);

	priv->beacon_skb = beacon;

	iwlagn_send_beacon_cmd(priv);
 out:
	mutex_unlock(&priv->mutex);
}

static void iwl_bg_bt_runtime_config(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, bt_runtime_config);

	mutex_lock(&priv->mutex);
	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		goto out;

	/* dont send host command if rf-kill is on */
	if (!iwl_is_ready_rf(priv))
		goto out;

	iwlagn_send_advance_bt_config(priv);
out:
	mutex_unlock(&priv->mutex);
}

static void iwl_bg_bt_full_concurrency(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, bt_full_concurrency);
	struct iwl_rxon_context *ctx;

	mutex_lock(&priv->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		goto out;

	/* dont send host command if rf-kill is on */
	if (!iwl_is_ready_rf(priv))
		goto out;

	IWL_DEBUG_INFO(priv, "BT coex in %s mode\n",
		       priv->bt_full_concurrent ?
		       "full concurrency" : "3-wire");

	/*
	 * LQ & RXON updated cmds must be sent before BT Config cmd
	 * to avoid 3-wire collisions
	 */
	for_each_context(priv, ctx) {
		iwlagn_set_rxon_chain(priv, ctx);
		iwlagn_commit_rxon(priv, ctx);
	}

	iwlagn_send_advance_bt_config(priv);
out:
	mutex_unlock(&priv->mutex);
}

int iwl_send_statistics_request(struct iwl_priv *priv, u8 flags, bool clear)
{
	struct iwl_statistics_cmd statistics_cmd = {
		.configuration_flags =
			clear ? IWL_STATS_CONF_CLEAR_STATS : 0,
	};

	if (flags & CMD_ASYNC)
		return iwl_dvm_send_cmd_pdu(priv, REPLY_STATISTICS_CMD,
					CMD_ASYNC,
					sizeof(struct iwl_statistics_cmd),
					&statistics_cmd);
	else
		return iwl_dvm_send_cmd_pdu(priv, REPLY_STATISTICS_CMD, 0,
					sizeof(struct iwl_statistics_cmd),
					&statistics_cmd);
}

/**
 * iwl_bg_statistics_periodic - Timer callback to queue statistics
 *
 * This callback is provided in order to send a statistics request.
 *
 * This timer function is continually reset to execute within
 * REG_RECALIB_PERIOD seconds since the last STATISTICS_NOTIFICATION
 * was received.  We need to ensure we receive the statistics in order
 * to update the temperature used for calibrating the TXPOWER.
 */
static void iwl_bg_statistics_periodic(struct timer_list *t)
{
	struct iwl_priv *priv = from_timer(priv, t, statistics_periodic);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* dont send host command if rf-kill is on */
	if (!iwl_is_ready_rf(priv))
		return;

	iwl_send_statistics_request(priv, CMD_ASYNC, false);
}


static void iwl_print_cont_event_trace(struct iwl_priv *priv, u32 base,
					u32 start_idx, u32 num_events,
					u32 capacity, u32 mode)
{
	u32 i;
	u32 ptr;        /* SRAM byte address of log data */
	u32 ev, time, data; /* event log data */
	unsigned long reg_flags;

	if (mode == 0)
		ptr = base + (4 * sizeof(u32)) + (start_idx * 2 * sizeof(u32));
	else
		ptr = base + (4 * sizeof(u32)) + (start_idx * 3 * sizeof(u32));

	/* Make sure device is powered up for SRAM reads */
	if (!iwl_trans_grab_nic_access(priv->trans, &reg_flags))
		return;

	/* Set starting address; reads will auto-increment */
	iwl_write32(priv->trans, HBUS_TARG_MEM_RADDR, ptr);

	/*
	 * Refuse to read more than would have fit into the log from
	 * the current start_idx. This used to happen due to the race
	 * described below, but now WARN because the code below should
	 * prevent it from happening here.
	 */
	if (WARN_ON(num_events > capacity - start_idx))
		num_events = capacity - start_idx;

	/*
	 * "time" is actually "data" for mode 0 (no timestamp).
	 * place event id # at far right for easier visual parsing.
	 */
	for (i = 0; i < num_events; i++) {
		ev = iwl_read32(priv->trans, HBUS_TARG_MEM_RDAT);
		time = iwl_read32(priv->trans, HBUS_TARG_MEM_RDAT);
		if (mode == 0) {
			trace_iwlwifi_dev_ucode_cont_event(
					priv->trans->dev, 0, time, ev);
		} else {
			data = iwl_read32(priv->trans, HBUS_TARG_MEM_RDAT);
			trace_iwlwifi_dev_ucode_cont_event(
					priv->trans->dev, time, data, ev);
		}
	}
	/* Allow device to power down */
	iwl_trans_release_nic_access(priv->trans, &reg_flags);
}

static void iwl_continuous_event_trace(struct iwl_priv *priv)
{
	u32 capacity;   /* event log capacity in # entries */
	struct {
		u32 capacity;
		u32 mode;
		u32 wrap_counter;
		u32 write_counter;
	} __packed read;
	u32 base;       /* SRAM byte address of event log header */
	u32 mode;       /* 0 - no timestamp, 1 - timestamp recorded */
	u32 num_wraps;  /* # times uCode wrapped to top of log */
	u32 next_entry; /* index of next entry to be written by uCode */

	base = priv->device_pointers.log_event_table;
	if (iwlagn_hw_valid_rtc_data_addr(base)) {
		iwl_trans_read_mem_bytes(priv->trans, base,
					 &read, sizeof(read));
		capacity = read.capacity;
		mode = read.mode;
		num_wraps = read.wrap_counter;
		next_entry = read.write_counter;
	} else
		return;

	/*
	 * Unfortunately, the uCode doesn't use temporary variables.
	 * Therefore, it can happen that we read next_entry == capacity,
	 * which really means next_entry == 0.
	 */
	if (unlikely(next_entry == capacity))
		next_entry = 0;
	/*
	 * Additionally, the uCode increases the write pointer before
	 * the wraps counter, so if the write pointer is smaller than
	 * the old write pointer (wrap occurred) but we read that no
	 * wrap occurred, we actually read between the next_entry and
	 * num_wraps update (this does happen in practice!!) -- take
	 * that into account by increasing num_wraps.
	 */
	if (unlikely(next_entry < priv->event_log.next_entry &&
		     num_wraps == priv->event_log.num_wraps))
		num_wraps++;

	if (num_wraps == priv->event_log.num_wraps) {
		iwl_print_cont_event_trace(
			priv, base, priv->event_log.next_entry,
			next_entry - priv->event_log.next_entry,
			capacity, mode);

		priv->event_log.non_wraps_count++;
	} else {
		if (num_wraps - priv->event_log.num_wraps > 1)
			priv->event_log.wraps_more_count++;
		else
			priv->event_log.wraps_once_count++;

		trace_iwlwifi_dev_ucode_wrap_event(priv->trans->dev,
				num_wraps - priv->event_log.num_wraps,
				next_entry, priv->event_log.next_entry);

		if (next_entry < priv->event_log.next_entry) {
			iwl_print_cont_event_trace(
				priv, base, priv->event_log.next_entry,
				capacity - priv->event_log.next_entry,
				capacity, mode);

			iwl_print_cont_event_trace(
				priv, base, 0, next_entry, capacity, mode);
		} else {
			iwl_print_cont_event_trace(
				priv, base, next_entry,
				capacity - next_entry,
				capacity, mode);

			iwl_print_cont_event_trace(
				priv, base, 0, next_entry, capacity, mode);
		}
	}

	priv->event_log.num_wraps = num_wraps;
	priv->event_log.next_entry = next_entry;
}

/**
 * iwl_bg_ucode_trace - Timer callback to log ucode event
 *
 * The timer is continually set to execute every
 * UCODE_TRACE_PERIOD milliseconds after the last timer expired
 * this function is to perform continuous uCode event logging operation
 * if enabled
 */
static void iwl_bg_ucode_trace(struct timer_list *t)
{
	struct iwl_priv *priv = from_timer(priv, t, ucode_trace);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (priv->event_log.ucode_trace) {
		iwl_continuous_event_trace(priv);
		/* Reschedule the timer to occur in UCODE_TRACE_PERIOD */
		mod_timer(&priv->ucode_trace,
			 jiffies + msecs_to_jiffies(UCODE_TRACE_PERIOD));
	}
}

static void iwl_bg_tx_flush(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, tx_flush);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* do nothing if rf-kill is on */
	if (!iwl_is_ready_rf(priv))
		return;

	IWL_DEBUG_INFO(priv, "device request: flush all tx frames\n");
	iwlagn_dev_txfifo_flush(priv);
}

/*
 * queue/FIFO/AC mapping definitions
 */

static const u8 iwlagn_bss_ac_to_fifo[] = {
	IWL_TX_FIFO_VO,
	IWL_TX_FIFO_VI,
	IWL_TX_FIFO_BE,
	IWL_TX_FIFO_BK,
};

static const u8 iwlagn_bss_ac_to_queue[] = {
	0, 1, 2, 3,
};

static const u8 iwlagn_pan_ac_to_fifo[] = {
	IWL_TX_FIFO_VO_IPAN,
	IWL_TX_FIFO_VI_IPAN,
	IWL_TX_FIFO_BE_IPAN,
	IWL_TX_FIFO_BK_IPAN,
};

static const u8 iwlagn_pan_ac_to_queue[] = {
	7, 6, 5, 4,
};

static void iwl_init_context(struct iwl_priv *priv, u32 ucode_flags)
{
	int i;

	/*
	 * The default context is always valid,
	 * the PAN context depends on uCode.
	 */
	priv->valid_contexts = BIT(IWL_RXON_CTX_BSS);
	if (ucode_flags & IWL_UCODE_TLV_FLAGS_PAN)
		priv->valid_contexts |= BIT(IWL_RXON_CTX_PAN);

	for (i = 0; i < NUM_IWL_RXON_CTX; i++)
		priv->contexts[i].ctxid = i;

	priv->contexts[IWL_RXON_CTX_BSS].always_active = true;
	priv->contexts[IWL_RXON_CTX_BSS].is_active = true;
	priv->contexts[IWL_RXON_CTX_BSS].rxon_cmd = REPLY_RXON;
	priv->contexts[IWL_RXON_CTX_BSS].rxon_timing_cmd = REPLY_RXON_TIMING;
	priv->contexts[IWL_RXON_CTX_BSS].rxon_assoc_cmd = REPLY_RXON_ASSOC;
	priv->contexts[IWL_RXON_CTX_BSS].qos_cmd = REPLY_QOS_PARAM;
	priv->contexts[IWL_RXON_CTX_BSS].ap_sta_id = IWL_AP_ID;
	priv->contexts[IWL_RXON_CTX_BSS].wep_key_cmd = REPLY_WEPKEY;
	priv->contexts[IWL_RXON_CTX_BSS].bcast_sta_id = IWLAGN_BROADCAST_ID;
	priv->contexts[IWL_RXON_CTX_BSS].exclusive_interface_modes =
		BIT(NL80211_IFTYPE_ADHOC) | BIT(NL80211_IFTYPE_MONITOR);
	priv->contexts[IWL_RXON_CTX_BSS].interface_modes =
		BIT(NL80211_IFTYPE_STATION);
	priv->contexts[IWL_RXON_CTX_BSS].ap_devtype = RXON_DEV_TYPE_AP;
	priv->contexts[IWL_RXON_CTX_BSS].ibss_devtype = RXON_DEV_TYPE_IBSS;
	priv->contexts[IWL_RXON_CTX_BSS].station_devtype = RXON_DEV_TYPE_ESS;
	priv->contexts[IWL_RXON_CTX_BSS].unused_devtype = RXON_DEV_TYPE_ESS;
	memcpy(priv->contexts[IWL_RXON_CTX_BSS].ac_to_queue,
	       iwlagn_bss_ac_to_queue, sizeof(iwlagn_bss_ac_to_queue));
	memcpy(priv->contexts[IWL_RXON_CTX_BSS].ac_to_fifo,
	       iwlagn_bss_ac_to_fifo, sizeof(iwlagn_bss_ac_to_fifo));

	priv->contexts[IWL_RXON_CTX_PAN].rxon_cmd = REPLY_WIPAN_RXON;
	priv->contexts[IWL_RXON_CTX_PAN].rxon_timing_cmd =
		REPLY_WIPAN_RXON_TIMING;
	priv->contexts[IWL_RXON_CTX_PAN].rxon_assoc_cmd =
		REPLY_WIPAN_RXON_ASSOC;
	priv->contexts[IWL_RXON_CTX_PAN].qos_cmd = REPLY_WIPAN_QOS_PARAM;
	priv->contexts[IWL_RXON_CTX_PAN].ap_sta_id = IWL_AP_ID_PAN;
	priv->contexts[IWL_RXON_CTX_PAN].wep_key_cmd = REPLY_WIPAN_WEPKEY;
	priv->contexts[IWL_RXON_CTX_PAN].bcast_sta_id = IWLAGN_PAN_BCAST_ID;
	priv->contexts[IWL_RXON_CTX_PAN].station_flags = STA_FLG_PAN_STATION;
	priv->contexts[IWL_RXON_CTX_PAN].interface_modes =
		BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP);

	priv->contexts[IWL_RXON_CTX_PAN].ap_devtype = RXON_DEV_TYPE_CP;
	priv->contexts[IWL_RXON_CTX_PAN].station_devtype = RXON_DEV_TYPE_2STA;
	priv->contexts[IWL_RXON_CTX_PAN].unused_devtype = RXON_DEV_TYPE_P2P;
	memcpy(priv->contexts[IWL_RXON_CTX_PAN].ac_to_queue,
	       iwlagn_pan_ac_to_queue, sizeof(iwlagn_pan_ac_to_queue));
	memcpy(priv->contexts[IWL_RXON_CTX_PAN].ac_to_fifo,
	       iwlagn_pan_ac_to_fifo, sizeof(iwlagn_pan_ac_to_fifo));
	priv->contexts[IWL_RXON_CTX_PAN].mcast_queue = IWL_IPAN_MCAST_QUEUE;

	BUILD_BUG_ON(NUM_IWL_RXON_CTX != 2);
}

static void iwl_rf_kill_ct_config(struct iwl_priv *priv)
{
	struct iwl_ct_kill_config cmd;
	struct iwl_ct_kill_throttling_config adv_cmd;
	int ret = 0;

	iwl_write32(priv->trans, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);

	priv->thermal_throttle.ct_kill_toggle = false;

	if (priv->lib->support_ct_kill_exit) {
		adv_cmd.critical_temperature_enter =
			cpu_to_le32(priv->hw_params.ct_kill_threshold);
		adv_cmd.critical_temperature_exit =
			cpu_to_le32(priv->hw_params.ct_kill_exit_threshold);

		ret = iwl_dvm_send_cmd_pdu(priv,
				       REPLY_CT_KILL_CONFIG_CMD,
				       0, sizeof(adv_cmd), &adv_cmd);
		if (ret)
			IWL_ERR(priv, "REPLY_CT_KILL_CONFIG_CMD failed\n");
		else
			IWL_DEBUG_INFO(priv, "REPLY_CT_KILL_CONFIG_CMD "
				"succeeded, critical temperature enter is %d,"
				"exit is %d\n",
				priv->hw_params.ct_kill_threshold,
				priv->hw_params.ct_kill_exit_threshold);
	} else {
		cmd.critical_temperature_R =
			cpu_to_le32(priv->hw_params.ct_kill_threshold);

		ret = iwl_dvm_send_cmd_pdu(priv,
				       REPLY_CT_KILL_CONFIG_CMD,
				       0, sizeof(cmd), &cmd);
		if (ret)
			IWL_ERR(priv, "REPLY_CT_KILL_CONFIG_CMD failed\n");
		else
			IWL_DEBUG_INFO(priv, "REPLY_CT_KILL_CONFIG_CMD "
				"succeeded, "
				"critical temperature is %d\n",
				priv->hw_params.ct_kill_threshold);
	}
}

static int iwlagn_send_calib_cfg_rt(struct iwl_priv *priv, u32 cfg)
{
	struct iwl_calib_cfg_cmd calib_cfg_cmd;
	struct iwl_host_cmd cmd = {
		.id = CALIBRATION_CFG_CMD,
		.len = { sizeof(struct iwl_calib_cfg_cmd), },
		.data = { &calib_cfg_cmd, },
	};

	memset(&calib_cfg_cmd, 0, sizeof(calib_cfg_cmd));
	calib_cfg_cmd.ucd_calib_cfg.once.is_enable = IWL_CALIB_RT_CFG_ALL;
	calib_cfg_cmd.ucd_calib_cfg.once.start = cpu_to_le32(cfg);

	return iwl_dvm_send_cmd(priv, &cmd);
}


static int iwlagn_send_tx_ant_config(struct iwl_priv *priv, u8 valid_tx_ant)
{
	struct iwl_tx_ant_config_cmd tx_ant_cmd = {
	  .valid = cpu_to_le32(valid_tx_ant),
	};

	if (IWL_UCODE_API(priv->fw->ucode_ver) > 1) {
		IWL_DEBUG_HC(priv, "select valid tx ant: %u\n", valid_tx_ant);
		return iwl_dvm_send_cmd_pdu(priv, TX_ANT_CONFIGURATION_CMD, 0,
					sizeof(struct iwl_tx_ant_config_cmd),
					&tx_ant_cmd);
	} else {
		IWL_DEBUG_HC(priv, "TX_ANT_CONFIGURATION_CMD not supported\n");
		return -EOPNOTSUPP;
	}
}

static void iwl_send_bt_config(struct iwl_priv *priv)
{
	struct iwl_bt_cmd bt_cmd = {
		.lead_time = BT_LEAD_TIME_DEF,
		.max_kill = BT_MAX_KILL_DEF,
		.kill_ack_mask = 0,
		.kill_cts_mask = 0,
	};

	if (!iwlwifi_mod_params.bt_coex_active)
		bt_cmd.flags = BT_COEX_DISABLE;
	else
		bt_cmd.flags = BT_COEX_ENABLE;

	priv->bt_enable_flag = bt_cmd.flags;
	IWL_DEBUG_INFO(priv, "BT coex %s\n",
		(bt_cmd.flags == BT_COEX_DISABLE) ? "disable" : "active");

	if (iwl_dvm_send_cmd_pdu(priv, REPLY_BT_CONFIG,
			     0, sizeof(struct iwl_bt_cmd), &bt_cmd))
		IWL_ERR(priv, "failed to send BT Coex Config\n");
}

/**
 * iwl_alive_start - called after REPLY_ALIVE notification received
 *                   from protocol/runtime uCode (initialization uCode's
 *                   Alive gets handled by iwl_init_alive_start()).
 */
int iwl_alive_start(struct iwl_priv *priv)
{
	int ret = 0;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	IWL_DEBUG_INFO(priv, "Runtime Alive received.\n");

	/* After the ALIVE response, we can send host commands to the uCode */
	set_bit(STATUS_ALIVE, &priv->status);

	if (iwl_is_rfkill(priv))
		return -ERFKILL;

	if (priv->event_log.ucode_trace) {
		/* start collecting data now */
		mod_timer(&priv->ucode_trace, jiffies);
	}

	/* download priority table before any calibration request */
	if (priv->lib->bt_params &&
	    priv->lib->bt_params->advanced_bt_coexist) {
		/* Configure Bluetooth device coexistence support */
		if (priv->lib->bt_params->bt_sco_disable)
			priv->bt_enable_pspoll = false;
		else
			priv->bt_enable_pspoll = true;

		priv->bt_valid = IWLAGN_BT_ALL_VALID_MSK;
		priv->kill_ack_mask = IWLAGN_BT_KILL_ACK_MASK_DEFAULT;
		priv->kill_cts_mask = IWLAGN_BT_KILL_CTS_MASK_DEFAULT;
		iwlagn_send_advance_bt_config(priv);
		priv->bt_valid = IWLAGN_BT_VALID_ENABLE_FLAGS;
		priv->cur_rssi_ctx = NULL;

		iwl_send_prio_tbl(priv);

		/* FIXME: w/a to force change uCode BT state machine */
		ret = iwl_send_bt_env(priv, IWL_BT_COEX_ENV_OPEN,
					 BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
		if (ret)
			return ret;
		ret = iwl_send_bt_env(priv, IWL_BT_COEX_ENV_CLOSE,
					 BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
		if (ret)
			return ret;
	} else if (priv->lib->bt_params) {
		/*
		 * default is 2-wire BT coexexistence support
		 */
		iwl_send_bt_config(priv);
	}

	/*
	 * Perform runtime calibrations, including DC calibration.
	 */
	iwlagn_send_calib_cfg_rt(priv, IWL_CALIB_CFG_DC_IDX);

	ieee80211_wake_queues(priv->hw);

	/* Configure Tx antenna selection based on H/W config */
	iwlagn_send_tx_ant_config(priv, priv->nvm_data->valid_tx_ant);

	if (iwl_is_associated_ctx(ctx) && !priv->wowlan) {
		struct iwl_rxon_cmd *active_rxon =
				(struct iwl_rxon_cmd *)&ctx->active;
		/* apply any changes in staging */
		ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	} else {
		struct iwl_rxon_context *tmp;
		/* Initialize our rx_config data */
		for_each_context(priv, tmp)
			iwl_connection_init_rx_config(priv, tmp);

		iwlagn_set_rxon_chain(priv, ctx);
	}

	if (!priv->wowlan) {
		/* WoWLAN ucode will not reply in the same way, skip it */
		iwl_reset_run_time_calib(priv);
	}

	set_bit(STATUS_READY, &priv->status);

	/* Configure the adapter for unassociated operation */
	ret = iwlagn_commit_rxon(priv, ctx);
	if (ret)
		return ret;

	/* At this point, the NIC is initialized and operational */
	iwl_rf_kill_ct_config(priv);

	IWL_DEBUG_INFO(priv, "ALIVE processing complete.\n");

	return iwl_power_update_mode(priv, true);
}

/**
 * iwl_clear_driver_stations - clear knowledge of all stations from driver
 * @priv: iwl priv struct
 *
 * This is called during iwl_down() to make sure that in the case
 * we're coming there from a hardware restart mac80211 will be
 * able to reconfigure stations -- if we're getting there in the
 * normal down flow then the stations will already be cleared.
 */
static void iwl_clear_driver_stations(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;

	spin_lock_bh(&priv->sta_lock);
	memset(priv->stations, 0, sizeof(priv->stations));
	priv->num_stations = 0;

	priv->ucode_key_table = 0;

	for_each_context(priv, ctx) {
		/*
		 * Remove all key information that is not stored as part
		 * of station information since mac80211 may not have had
		 * a chance to remove all the keys. When device is
		 * reconfigured by mac80211 after an error all keys will
		 * be reconfigured.
		 */
		memset(ctx->wep_keys, 0, sizeof(ctx->wep_keys));
		ctx->key_mapping_keys = 0;
	}

	spin_unlock_bh(&priv->sta_lock);
}

void iwl_down(struct iwl_priv *priv)
{
	int exit_pending;

	IWL_DEBUG_INFO(priv, DRV_NAME " is going down\n");

	lockdep_assert_held(&priv->mutex);

	iwl_scan_cancel_timeout(priv, 200);

	exit_pending =
		test_and_set_bit(STATUS_EXIT_PENDING, &priv->status);

	iwl_clear_ucode_stations(priv, NULL);
	iwl_dealloc_bcast_stations(priv);
	iwl_clear_driver_stations(priv);

	/* reset BT coex data */
	priv->bt_status = 0;
	priv->cur_rssi_ctx = NULL;
	priv->bt_is_sco = 0;
	if (priv->lib->bt_params)
		priv->bt_traffic_load =
			 priv->lib->bt_params->bt_init_traffic_load;
	else
		priv->bt_traffic_load = 0;
	priv->bt_full_concurrent = false;
	priv->bt_ci_compliance = 0;

	/* Wipe out the EXIT_PENDING status bit if we are not actually
	 * exiting the module */
	if (!exit_pending)
		clear_bit(STATUS_EXIT_PENDING, &priv->status);

	if (priv->mac80211_registered)
		ieee80211_stop_queues(priv->hw);

	priv->ucode_loaded = false;
	iwl_trans_stop_device(priv->trans);

	/* Set num_aux_in_flight must be done after the transport is stopped */
	atomic_set(&priv->num_aux_in_flight, 0);

	/* Clear out all status bits but a few that are stable across reset */
	priv->status &= test_bit(STATUS_RF_KILL_HW, &priv->status) <<
				STATUS_RF_KILL_HW |
			test_bit(STATUS_FW_ERROR, &priv->status) <<
				STATUS_FW_ERROR |
			test_bit(STATUS_EXIT_PENDING, &priv->status) <<
				STATUS_EXIT_PENDING;

	dev_kfree_skb(priv->beacon_skb);
	priv->beacon_skb = NULL;
}

/*****************************************************************************
 *
 * Workqueue callbacks
 *
 *****************************************************************************/

static void iwl_bg_run_time_calib_work(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv,
			run_time_calib_work);

	mutex_lock(&priv->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status) ||
	    test_bit(STATUS_SCANNING, &priv->status)) {
		mutex_unlock(&priv->mutex);
		return;
	}

	if (priv->start_calib) {
		iwl_chain_noise_calibration(priv);
		iwl_sensitivity_calibration(priv);
	}

	mutex_unlock(&priv->mutex);
}

void iwlagn_prepare_restart(struct iwl_priv *priv)
{
	bool bt_full_concurrent;
	u8 bt_ci_compliance;
	u8 bt_load;
	u8 bt_status;
	bool bt_is_sco;
	int i;

	lockdep_assert_held(&priv->mutex);

	priv->is_open = 0;

	/*
	 * __iwl_down() will clear the BT status variables,
	 * which is correct, but when we restart we really
	 * want to keep them so restore them afterwards.
	 *
	 * The restart process will later pick them up and
	 * re-configure the hw when we reconfigure the BT
	 * command.
	 */
	bt_full_concurrent = priv->bt_full_concurrent;
	bt_ci_compliance = priv->bt_ci_compliance;
	bt_load = priv->bt_traffic_load;
	bt_status = priv->bt_status;
	bt_is_sco = priv->bt_is_sco;

	iwl_down(priv);

	priv->bt_full_concurrent = bt_full_concurrent;
	priv->bt_ci_compliance = bt_ci_compliance;
	priv->bt_traffic_load = bt_load;
	priv->bt_status = bt_status;
	priv->bt_is_sco = bt_is_sco;

	/* reset aggregation queues */
	for (i = IWLAGN_FIRST_AMPDU_QUEUE; i < IWL_MAX_HW_QUEUES; i++)
		priv->queue_to_mac80211[i] = IWL_INVALID_MAC80211_QUEUE;
	/* and stop counts */
	for (i = 0; i < IWL_MAX_HW_QUEUES; i++)
		atomic_set(&priv->queue_stop_count[i], 0);

	memset(priv->agg_q_alloc, 0, sizeof(priv->agg_q_alloc));
}

static void iwl_bg_restart(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, restart);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (test_and_clear_bit(STATUS_FW_ERROR, &priv->status)) {
		mutex_lock(&priv->mutex);
		iwlagn_prepare_restart(priv);
		mutex_unlock(&priv->mutex);
		iwl_cancel_deferred_work(priv);
		if (priv->mac80211_registered)
			ieee80211_restart_hw(priv->hw);
		else
			IWL_ERR(priv,
				"Cannot request restart before registrating with mac80211\n");
	} else {
		WARN_ON(1);
	}
}

/*****************************************************************************
 *
 * driver setup and teardown
 *
 *****************************************************************************/

static void iwl_setup_deferred_work(struct iwl_priv *priv)
{
	priv->workqueue = alloc_ordered_workqueue(DRV_NAME, 0);

	INIT_WORK(&priv->restart, iwl_bg_restart);
	INIT_WORK(&priv->beacon_update, iwl_bg_beacon_update);
	INIT_WORK(&priv->run_time_calib_work, iwl_bg_run_time_calib_work);
	INIT_WORK(&priv->tx_flush, iwl_bg_tx_flush);
	INIT_WORK(&priv->bt_full_concurrency, iwl_bg_bt_full_concurrency);
	INIT_WORK(&priv->bt_runtime_config, iwl_bg_bt_runtime_config);

	iwl_setup_scan_deferred_work(priv);

	if (priv->lib->bt_params)
		iwlagn_bt_setup_deferred_work(priv);

	timer_setup(&priv->statistics_periodic, iwl_bg_statistics_periodic, 0);

	timer_setup(&priv->ucode_trace, iwl_bg_ucode_trace, 0);
}

void iwl_cancel_deferred_work(struct iwl_priv *priv)
{
	if (priv->lib->bt_params)
		iwlagn_bt_cancel_deferred_work(priv);

	cancel_work_sync(&priv->run_time_calib_work);
	cancel_work_sync(&priv->beacon_update);

	iwl_cancel_scan_deferred_work(priv);

	cancel_work_sync(&priv->bt_full_concurrency);
	cancel_work_sync(&priv->bt_runtime_config);

	del_timer_sync(&priv->statistics_periodic);
	del_timer_sync(&priv->ucode_trace);
}

static int iwl_init_drv(struct iwl_priv *priv)
{
	spin_lock_init(&priv->sta_lock);

	mutex_init(&priv->mutex);

	INIT_LIST_HEAD(&priv->calib_results);

	priv->band = NL80211_BAND_2GHZ;

	priv->plcp_delta_threshold = priv->lib->plcp_delta_threshold;

	priv->iw_mode = NL80211_IFTYPE_STATION;
	priv->current_ht_config.smps = IEEE80211_SMPS_STATIC;
	priv->missed_beacon_threshold = IWL_MISSED_BEACON_THRESHOLD_DEF;
	priv->agg_tids_count = 0;

	priv->rx_statistics_jiffies = jiffies;

	/* Choose which receivers/antennas to use */
	iwlagn_set_rxon_chain(priv, &priv->contexts[IWL_RXON_CTX_BSS]);

	iwl_init_scan_params(priv);

	/* init bt coex */
	if (priv->lib->bt_params &&
	    priv->lib->bt_params->advanced_bt_coexist) {
		priv->kill_ack_mask = IWLAGN_BT_KILL_ACK_MASK_DEFAULT;
		priv->kill_cts_mask = IWLAGN_BT_KILL_CTS_MASK_DEFAULT;
		priv->bt_valid = IWLAGN_BT_ALL_VALID_MSK;
		priv->bt_on_thresh = BT_ON_THRESHOLD_DEF;
		priv->bt_duration = BT_DURATION_LIMIT_DEF;
		priv->dynamic_frag_thresh = BT_FRAG_THRESHOLD_DEF;
	}

	return 0;
}

static void iwl_uninit_drv(struct iwl_priv *priv)
{
	kfree(priv->scan_cmd);
	kfree(priv->beacon_cmd);
	kfree(rcu_dereference_raw(priv->noa_data));
	iwl_calib_free_results(priv);
#ifdef CONFIG_IWLWIFI_DEBUGFS
	kfree(priv->wowlan_sram);
#endif
}

static void iwl_set_hw_params(struct iwl_priv *priv)
{
	if (priv->cfg->ht_params)
		priv->hw_params.use_rts_for_aggregation =
			priv->cfg->ht_params->use_rts_for_aggregation;

	/* Device-specific setup */
	priv->lib->set_hw_params(priv);
}



/* show what optional capabilities we have */
static void iwl_option_config(struct iwl_priv *priv)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	IWL_INFO(priv, "CONFIG_IWLWIFI_DEBUG enabled\n");
#else
	IWL_INFO(priv, "CONFIG_IWLWIFI_DEBUG disabled\n");
#endif

#ifdef CONFIG_IWLWIFI_DEBUGFS
	IWL_INFO(priv, "CONFIG_IWLWIFI_DEBUGFS enabled\n");
#else
	IWL_INFO(priv, "CONFIG_IWLWIFI_DEBUGFS disabled\n");
#endif

#ifdef CONFIG_IWLWIFI_DEVICE_TRACING
	IWL_INFO(priv, "CONFIG_IWLWIFI_DEVICE_TRACING enabled\n");
#else
	IWL_INFO(priv, "CONFIG_IWLWIFI_DEVICE_TRACING disabled\n");
#endif
}

static int iwl_eeprom_init_hw_params(struct iwl_priv *priv)
{
	struct iwl_nvm_data *data = priv->nvm_data;

	if (data->sku_cap_11n_enable &&
	    !priv->cfg->ht_params) {
		IWL_ERR(priv, "Invalid 11n configuration\n");
		return -EINVAL;
	}

	if (!data->sku_cap_11n_enable && !data->sku_cap_band_24ghz_enable &&
	    !data->sku_cap_band_52ghz_enable) {
		IWL_ERR(priv, "Invalid device sku\n");
		return -EINVAL;
	}

	IWL_DEBUG_INFO(priv,
		       "Device SKU: 24GHz %s %s, 52GHz %s %s, 11.n %s %s\n",
		       data->sku_cap_band_24ghz_enable ? "" : "NOT", "enabled",
		       data->sku_cap_band_52ghz_enable ? "" : "NOT", "enabled",
		       data->sku_cap_11n_enable ? "" : "NOT", "enabled");

	priv->hw_params.tx_chains_num =
		num_of_ant(data->valid_tx_ant);
	if (priv->cfg->rx_with_siso_diversity)
		priv->hw_params.rx_chains_num = 1;
	else
		priv->hw_params.rx_chains_num =
			num_of_ant(data->valid_rx_ant);

	IWL_DEBUG_INFO(priv, "Valid Tx ant: 0x%X, Valid Rx ant: 0x%X\n",
		       data->valid_tx_ant,
		       data->valid_rx_ant);

	return 0;
}

static int iwl_nvm_check_version(struct iwl_nvm_data *data,
				 struct iwl_trans *trans)
{
	if (data->nvm_version >= trans->cfg->nvm_ver ||
	    data->calib_version >= trans->cfg->nvm_calib_ver) {
		IWL_DEBUG_INFO(trans, "device EEPROM VER=0x%x, CALIB=0x%x\n",
			       data->nvm_version, data->calib_version);
		return 0;
	}

	IWL_ERR(trans,
		"Unsupported (too old) EEPROM VER=0x%x < 0x%x CALIB=0x%x < 0x%x\n",
		data->nvm_version, trans->cfg->nvm_ver,
		data->calib_version,  trans->cfg->nvm_calib_ver);
	return -EINVAL;
}

static struct iwl_op_mode *iwl_op_mode_dvm_start(struct iwl_trans *trans,
						 const struct iwl_cfg *cfg,
						 const struct iwl_fw *fw,
						 struct dentry *dbgfs_dir)
{
	struct iwl_priv *priv;
	struct ieee80211_hw *hw;
	struct iwl_op_mode *op_mode;
	u16 num_mac;
	u32 ucode_flags;
	struct iwl_trans_config trans_cfg = {};
	static const u8 no_reclaim_cmds[] = {
		REPLY_RX_PHY_CMD,
		REPLY_RX_MPDU_CMD,
		REPLY_COMPRESSED_BA,
		STATISTICS_NOTIFICATION,
		REPLY_TX,
	};
	int i;

	/************************
	 * 1. Allocating HW data
	 ************************/
	hw = iwl_alloc_all();
	if (!hw) {
		pr_err("%s: Cannot allocate network device\n", cfg->name);
		goto out;
	}

	op_mode = hw->priv;
	op_mode->ops = &iwl_dvm_ops;
	priv = IWL_OP_MODE_GET_DVM(op_mode);
	priv->trans = trans;
	priv->dev = trans->dev;
	priv->cfg = cfg;
	priv->fw = fw;

	switch (priv->cfg->device_family) {
	case IWL_DEVICE_FAMILY_1000:
	case IWL_DEVICE_FAMILY_100:
		priv->lib = &iwl_dvm_1000_cfg;
		break;
	case IWL_DEVICE_FAMILY_2000:
		priv->lib = &iwl_dvm_2000_cfg;
		break;
	case IWL_DEVICE_FAMILY_105:
		priv->lib = &iwl_dvm_105_cfg;
		break;
	case IWL_DEVICE_FAMILY_2030:
	case IWL_DEVICE_FAMILY_135:
		priv->lib = &iwl_dvm_2030_cfg;
		break;
	case IWL_DEVICE_FAMILY_5000:
		priv->lib = &iwl_dvm_5000_cfg;
		break;
	case IWL_DEVICE_FAMILY_5150:
		priv->lib = &iwl_dvm_5150_cfg;
		break;
	case IWL_DEVICE_FAMILY_6000:
	case IWL_DEVICE_FAMILY_6000i:
		priv->lib = &iwl_dvm_6000_cfg;
		break;
	case IWL_DEVICE_FAMILY_6005:
		priv->lib = &iwl_dvm_6005_cfg;
		break;
	case IWL_DEVICE_FAMILY_6050:
	case IWL_DEVICE_FAMILY_6150:
		priv->lib = &iwl_dvm_6050_cfg;
		break;
	case IWL_DEVICE_FAMILY_6030:
		priv->lib = &iwl_dvm_6030_cfg;
		break;
	default:
		break;
	}

	if (WARN_ON(!priv->lib))
		goto out_free_hw;

	/*
	 * Populate the state variables that the transport layer needs
	 * to know about.
	 */
	trans_cfg.op_mode = op_mode;
	trans_cfg.no_reclaim_cmds = no_reclaim_cmds;
	trans_cfg.n_no_reclaim_cmds = ARRAY_SIZE(no_reclaim_cmds);

	switch (iwlwifi_mod_params.amsdu_size) {
	case IWL_AMSDU_DEF:
	case IWL_AMSDU_4K:
		trans_cfg.rx_buf_size = IWL_AMSDU_4K;
		break;
	case IWL_AMSDU_8K:
		trans_cfg.rx_buf_size = IWL_AMSDU_8K;
		break;
	case IWL_AMSDU_12K:
	default:
		trans_cfg.rx_buf_size = IWL_AMSDU_4K;
		pr_err("Unsupported amsdu_size: %d\n",
		       iwlwifi_mod_params.amsdu_size);
	}

	trans_cfg.cmd_q_wdg_timeout = IWL_WATCHDOG_DISABLED;

	trans_cfg.command_groups = iwl_dvm_groups;
	trans_cfg.command_groups_size = ARRAY_SIZE(iwl_dvm_groups);

	trans_cfg.cmd_fifo = IWLAGN_CMD_FIFO_NUM;
	trans_cfg.cb_data_offs = offsetof(struct ieee80211_tx_info,
					  driver_data[2]);

	WARN_ON(sizeof(priv->transport_queue_stop) * BITS_PER_BYTE <
		priv->cfg->base_params->num_of_queues);

	ucode_flags = fw->ucode_capa.flags;

	if (ucode_flags & IWL_UCODE_TLV_FLAGS_PAN) {
		priv->sta_key_max_num = STA_KEY_MAX_NUM_PAN;
		trans_cfg.cmd_queue = IWL_IPAN_CMD_QUEUE_NUM;
	} else {
		priv->sta_key_max_num = STA_KEY_MAX_NUM;
		trans_cfg.cmd_queue = IWL_DEFAULT_CMD_QUEUE_NUM;
	}

	/* Configure transport layer */
	iwl_trans_configure(priv->trans, &trans_cfg);

	trans->rx_mpdu_cmd = REPLY_RX_MPDU_CMD;
	trans->rx_mpdu_cmd_hdr_size = sizeof(struct iwl_rx_mpdu_res_start);
	trans->command_groups = trans_cfg.command_groups;
	trans->command_groups_size = trans_cfg.command_groups_size;

	/* At this point both hw and priv are allocated. */

	SET_IEEE80211_DEV(priv->hw, priv->trans->dev);

	iwl_option_config(priv);

	IWL_DEBUG_INFO(priv, "*** LOAD DRIVER ***\n");

	/* is antenna coupling more than 35dB ? */
	priv->bt_ant_couple_ok =
		(iwlwifi_mod_params.antenna_coupling >
			IWL_BT_ANTENNA_COUPLING_THRESHOLD) ?
			true : false;

	/* bt channel inhibition enabled*/
	priv->bt_ch_announce = true;
	IWL_DEBUG_INFO(priv, "BT channel inhibition is %s\n",
		       (priv->bt_ch_announce) ? "On" : "Off");

	/* these spin locks will be used in apm_ops.init and EEPROM access
	 * we should init now
	 */
	spin_lock_init(&priv->statistics.lock);

	/***********************
	 * 2. Read REV register
	 ***********************/
	IWL_INFO(priv, "Detected %s, REV=0x%X\n",
		priv->cfg->name, priv->trans->hw_rev);

	if (iwl_trans_start_hw(priv->trans))
		goto out_free_hw;

	/* Read the EEPROM */
	if (iwl_read_eeprom(priv->trans, &priv->eeprom_blob,
			    &priv->eeprom_blob_size)) {
		IWL_ERR(priv, "Unable to init EEPROM\n");
		goto out_free_hw;
	}

	/* Reset chip to save power until we load uCode during "up". */
	iwl_trans_stop_device(priv->trans);

	priv->nvm_data = iwl_parse_eeprom_data(priv->trans->dev, priv->cfg,
						  priv->eeprom_blob,
						  priv->eeprom_blob_size);
	if (!priv->nvm_data)
		goto out_free_eeprom_blob;

	if (iwl_nvm_check_version(priv->nvm_data, priv->trans))
		goto out_free_eeprom;

	if (iwl_eeprom_init_hw_params(priv))
		goto out_free_eeprom;

	/* extract MAC Address */
	memcpy(priv->addresses[0].addr, priv->nvm_data->hw_addr, ETH_ALEN);
	IWL_DEBUG_INFO(priv, "MAC address: %pM\n", priv->addresses[0].addr);
	priv->hw->wiphy->addresses = priv->addresses;
	priv->hw->wiphy->n_addresses = 1;
	num_mac = priv->nvm_data->n_hw_addrs;
	if (num_mac > 1) {
		memcpy(priv->addresses[1].addr, priv->addresses[0].addr,
		       ETH_ALEN);
		priv->addresses[1].addr[5]++;
		priv->hw->wiphy->n_addresses++;
	}

	/************************
	 * 4. Setup HW constants
	 ************************/
	iwl_set_hw_params(priv);

	if (!(priv->nvm_data->sku_cap_ipan_enable)) {
		IWL_DEBUG_INFO(priv, "Your EEPROM disabled PAN\n");
		ucode_flags &= ~IWL_UCODE_TLV_FLAGS_PAN;
		/*
		 * if not PAN, then don't support P2P -- might be a uCode
		 * packaging bug or due to the eeprom check above
		 */
		priv->sta_key_max_num = STA_KEY_MAX_NUM;
		trans_cfg.cmd_queue = IWL_DEFAULT_CMD_QUEUE_NUM;

		/* Configure transport layer again*/
		iwl_trans_configure(priv->trans, &trans_cfg);
	}

	/*******************
	 * 5. Setup priv
	 *******************/
	for (i = 0; i < IWL_MAX_HW_QUEUES; i++) {
		priv->queue_to_mac80211[i] = IWL_INVALID_MAC80211_QUEUE;
		if (i < IWLAGN_FIRST_AMPDU_QUEUE &&
		    i != IWL_DEFAULT_CMD_QUEUE_NUM &&
		    i != IWL_IPAN_CMD_QUEUE_NUM)
			priv->queue_to_mac80211[i] = i;
		atomic_set(&priv->queue_stop_count[i], 0);
	}

	if (iwl_init_drv(priv))
		goto out_free_eeprom;

	/* At this point both hw and priv are initialized. */

	/********************
	 * 6. Setup services
	 ********************/
	iwl_setup_deferred_work(priv);
	iwl_setup_rx_handlers(priv);

	iwl_power_initialize(priv);
	iwl_tt_initialize(priv);

	snprintf(priv->hw->wiphy->fw_version,
		 sizeof(priv->hw->wiphy->fw_version),
		 "%s", fw->fw_version);

	priv->new_scan_threshold_behaviour =
		!!(ucode_flags & IWL_UCODE_TLV_FLAGS_NEWSCAN);

	priv->phy_calib_chain_noise_reset_cmd =
		fw->ucode_capa.standard_phy_calibration_size;
	priv->phy_calib_chain_noise_gain_cmd =
		fw->ucode_capa.standard_phy_calibration_size + 1;

	/* initialize all valid contexts */
	iwl_init_context(priv, ucode_flags);

	/**************************************************
	 * This is still part of probe() in a sense...
	 *
	 * 7. Setup and register with mac80211 and debugfs
	 **************************************************/
	if (iwlagn_mac_setup_register(priv, &fw->ucode_capa))
		goto out_destroy_workqueue;

	if (iwl_dbgfs_register(priv, dbgfs_dir))
		goto out_mac80211_unregister;

	return op_mode;

out_mac80211_unregister:
	iwlagn_mac_unregister(priv);
out_destroy_workqueue:
	iwl_tt_exit(priv);
	iwl_cancel_deferred_work(priv);
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;
	iwl_uninit_drv(priv);
out_free_eeprom_blob:
	kfree(priv->eeprom_blob);
out_free_eeprom:
	kfree(priv->nvm_data);
out_free_hw:
	ieee80211_free_hw(priv->hw);
out:
	op_mode = NULL;
	return op_mode;
}

static void iwl_op_mode_dvm_stop(struct iwl_op_mode *op_mode)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);

	IWL_DEBUG_INFO(priv, "*** UNLOAD DRIVER ***\n");

	iwlagn_mac_unregister(priv);

	iwl_tt_exit(priv);

	kfree(priv->eeprom_blob);
	kfree(priv->nvm_data);

	/*netif_stop_queue(dev); */
	flush_workqueue(priv->workqueue);

	/* ieee80211_unregister_hw calls iwlagn_mac_stop, which flushes
	 * priv->workqueue... so we can't take down the workqueue
	 * until now... */
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;

	iwl_uninit_drv(priv);

	dev_kfree_skb(priv->beacon_skb);

	iwl_trans_op_mode_leave(priv->trans);
	ieee80211_free_hw(priv->hw);
}

static const char * const desc_lookup_text[] = {
	"OK",
	"FAIL",
	"BAD_PARAM",
	"BAD_CHECKSUM",
	"NMI_INTERRUPT_WDG",
	"SYSASSERT",
	"FATAL_ERROR",
	"BAD_COMMAND",
	"HW_ERROR_TUNE_LOCK",
	"HW_ERROR_TEMPERATURE",
	"ILLEGAL_CHAN_FREQ",
	"VCC_NOT_STABLE",
	"FH_ERROR",
	"NMI_INTERRUPT_HOST",
	"NMI_INTERRUPT_ACTION_PT",
	"NMI_INTERRUPT_UNKNOWN",
	"UCODE_VERSION_MISMATCH",
	"HW_ERROR_ABS_LOCK",
	"HW_ERROR_CAL_LOCK_FAIL",
	"NMI_INTERRUPT_INST_ACTION_PT",
	"NMI_INTERRUPT_DATA_ACTION_PT",
	"NMI_TRM_HW_ER",
	"NMI_INTERRUPT_TRM",
	"NMI_INTERRUPT_BREAK_POINT",
	"DEBUG_0",
	"DEBUG_1",
	"DEBUG_2",
	"DEBUG_3",
};

static struct { char *name; u8 num; } advanced_lookup[] = {
	{ "NMI_INTERRUPT_WDG", 0x34 },
	{ "SYSASSERT", 0x35 },
	{ "UCODE_VERSION_MISMATCH", 0x37 },
	{ "BAD_COMMAND", 0x38 },
	{ "NMI_INTERRUPT_DATA_ACTION_PT", 0x3C },
	{ "FATAL_ERROR", 0x3D },
	{ "NMI_TRM_HW_ERR", 0x46 },
	{ "NMI_INTERRUPT_TRM", 0x4C },
	{ "NMI_INTERRUPT_BREAK_POINT", 0x54 },
	{ "NMI_INTERRUPT_WDG_RXF_FULL", 0x5C },
	{ "NMI_INTERRUPT_WDG_NO_RBD_RXF_FULL", 0x64 },
	{ "NMI_INTERRUPT_HOST", 0x66 },
	{ "NMI_INTERRUPT_ACTION_PT", 0x7C },
	{ "NMI_INTERRUPT_UNKNOWN", 0x84 },
	{ "NMI_INTERRUPT_INST_ACTION_PT", 0x86 },
	{ "ADVANCED_SYSASSERT", 0 },
};

static const char *desc_lookup(u32 num)
{
	int i;
	int max = ARRAY_SIZE(desc_lookup_text);

	if (num < max)
		return desc_lookup_text[num];

	max = ARRAY_SIZE(advanced_lookup) - 1;
	for (i = 0; i < max; i++) {
		if (advanced_lookup[i].num == num)
			break;
	}
	return advanced_lookup[i].name;
}

#define ERROR_START_OFFSET  (1 * sizeof(u32))
#define ERROR_ELEM_SIZE     (7 * sizeof(u32))

static void iwl_dump_nic_error_log(struct iwl_priv *priv)
{
	struct iwl_trans *trans = priv->trans;
	u32 base;
	struct iwl_error_event_table table;

	base = priv->device_pointers.error_event_table;
	if (priv->cur_ucode == IWL_UCODE_INIT) {
		if (!base)
			base = priv->fw->init_errlog_ptr;
	} else {
		if (!base)
			base = priv->fw->inst_errlog_ptr;
	}

	if (!iwlagn_hw_valid_rtc_data_addr(base)) {
		IWL_ERR(priv,
			"Not valid error log pointer 0x%08X for %s uCode\n",
			base,
			(priv->cur_ucode == IWL_UCODE_INIT)
					? "Init" : "RT");
		return;
	}

	/*TODO: Update dbgfs with ISR error stats obtained below */
	iwl_trans_read_mem_bytes(trans, base, &table, sizeof(table));

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		IWL_ERR(trans, "Start IWL Error Log Dump:\n");
		IWL_ERR(trans, "Status: 0x%08lX, count: %d\n",
			priv->status, table.valid);
	}

	trace_iwlwifi_dev_ucode_error(trans->dev, &table, 0, table.brd_ver);
	IWL_ERR(priv, "0x%08X | %-28s\n", table.error_id,
		desc_lookup(table.error_id));
	IWL_ERR(priv, "0x%08X | uPc\n", table.pc);
	IWL_ERR(priv, "0x%08X | branchlink1\n", table.blink1);
	IWL_ERR(priv, "0x%08X | branchlink2\n", table.blink2);
	IWL_ERR(priv, "0x%08X | interruptlink1\n", table.ilink1);
	IWL_ERR(priv, "0x%08X | interruptlink2\n", table.ilink2);
	IWL_ERR(priv, "0x%08X | data1\n", table.data1);
	IWL_ERR(priv, "0x%08X | data2\n", table.data2);
	IWL_ERR(priv, "0x%08X | line\n", table.line);
	IWL_ERR(priv, "0x%08X | beacon time\n", table.bcon_time);
	IWL_ERR(priv, "0x%08X | tsf low\n", table.tsf_low);
	IWL_ERR(priv, "0x%08X | tsf hi\n", table.tsf_hi);
	IWL_ERR(priv, "0x%08X | time gp1\n", table.gp1);
	IWL_ERR(priv, "0x%08X | time gp2\n", table.gp2);
	IWL_ERR(priv, "0x%08X | time gp3\n", table.gp3);
	IWL_ERR(priv, "0x%08X | uCode version\n", table.ucode_ver);
	IWL_ERR(priv, "0x%08X | hw version\n", table.hw_ver);
	IWL_ERR(priv, "0x%08X | board version\n", table.brd_ver);
	IWL_ERR(priv, "0x%08X | hcmd\n", table.hcmd);
	IWL_ERR(priv, "0x%08X | isr0\n", table.isr0);
	IWL_ERR(priv, "0x%08X | isr1\n", table.isr1);
	IWL_ERR(priv, "0x%08X | isr2\n", table.isr2);
	IWL_ERR(priv, "0x%08X | isr3\n", table.isr3);
	IWL_ERR(priv, "0x%08X | isr4\n", table.isr4);
	IWL_ERR(priv, "0x%08X | isr_pref\n", table.isr_pref);
	IWL_ERR(priv, "0x%08X | wait_event\n", table.wait_event);
	IWL_ERR(priv, "0x%08X | l2p_control\n", table.l2p_control);
	IWL_ERR(priv, "0x%08X | l2p_duration\n", table.l2p_duration);
	IWL_ERR(priv, "0x%08X | l2p_mhvalid\n", table.l2p_mhvalid);
	IWL_ERR(priv, "0x%08X | l2p_addr_match\n", table.l2p_addr_match);
	IWL_ERR(priv, "0x%08X | lmpm_pmg_sel\n", table.lmpm_pmg_sel);
	IWL_ERR(priv, "0x%08X | timestamp\n", table.u_timestamp);
	IWL_ERR(priv, "0x%08X | flow_handler\n", table.flow_handler);
}

#define EVENT_START_OFFSET  (4 * sizeof(u32))

/**
 * iwl_print_event_log - Dump error event log to syslog
 *
 */
static int iwl_print_event_log(struct iwl_priv *priv, u32 start_idx,
			       u32 num_events, u32 mode,
			       int pos, char **buf, size_t bufsz)
{
	u32 i;
	u32 base;       /* SRAM byte address of event log header */
	u32 event_size; /* 2 u32s, or 3 u32s if timestamp recorded */
	u32 ptr;        /* SRAM byte address of log data */
	u32 ev, time, data; /* event log data */
	unsigned long reg_flags;

	struct iwl_trans *trans = priv->trans;

	if (num_events == 0)
		return pos;

	base = priv->device_pointers.log_event_table;
	if (priv->cur_ucode == IWL_UCODE_INIT) {
		if (!base)
			base = priv->fw->init_evtlog_ptr;
	} else {
		if (!base)
			base = priv->fw->inst_evtlog_ptr;
	}

	if (mode == 0)
		event_size = 2 * sizeof(u32);
	else
		event_size = 3 * sizeof(u32);

	ptr = base + EVENT_START_OFFSET + (start_idx * event_size);

	/* Make sure device is powered up for SRAM reads */
	if (!iwl_trans_grab_nic_access(trans, &reg_flags))
		return pos;

	/* Set starting address; reads will auto-increment */
	iwl_write32(trans, HBUS_TARG_MEM_RADDR, ptr);

	/* "time" is actually "data" for mode 0 (no timestamp).
	* place event id # at far right for easier visual parsing. */
	for (i = 0; i < num_events; i++) {
		ev = iwl_read32(trans, HBUS_TARG_MEM_RDAT);
		time = iwl_read32(trans, HBUS_TARG_MEM_RDAT);
		if (mode == 0) {
			/* data, ev */
			if (bufsz) {
				pos += scnprintf(*buf + pos, bufsz - pos,
						"EVT_LOG:0x%08x:%04u\n",
						time, ev);
			} else {
				trace_iwlwifi_dev_ucode_event(trans->dev, 0,
					time, ev);
				IWL_ERR(priv, "EVT_LOG:0x%08x:%04u\n",
					time, ev);
			}
		} else {
			data = iwl_read32(trans, HBUS_TARG_MEM_RDAT);
			if (bufsz) {
				pos += scnprintf(*buf + pos, bufsz - pos,
						"EVT_LOGT:%010u:0x%08x:%04u\n",
						 time, data, ev);
			} else {
				IWL_ERR(priv, "EVT_LOGT:%010u:0x%08x:%04u\n",
					time, data, ev);
				trace_iwlwifi_dev_ucode_event(trans->dev, time,
					data, ev);
			}
		}
	}

	/* Allow device to power down */
	iwl_trans_release_nic_access(trans, &reg_flags);
	return pos;
}

/**
 * iwl_print_last_event_logs - Dump the newest # of event log to syslog
 */
static int iwl_print_last_event_logs(struct iwl_priv *priv, u32 capacity,
				    u32 num_wraps, u32 next_entry,
				    u32 size, u32 mode,
				    int pos, char **buf, size_t bufsz)
{
	/*
	 * display the newest DEFAULT_LOG_ENTRIES entries
	 * i.e the entries just before the next ont that uCode would fill.
	 */
	if (num_wraps) {
		if (next_entry < size) {
			pos = iwl_print_event_log(priv,
						capacity - (size - next_entry),
						size - next_entry, mode,
						pos, buf, bufsz);
			pos = iwl_print_event_log(priv, 0,
						  next_entry, mode,
						  pos, buf, bufsz);
		} else
			pos = iwl_print_event_log(priv, next_entry - size,
						  size, mode, pos, buf, bufsz);
	} else {
		if (next_entry < size) {
			pos = iwl_print_event_log(priv, 0, next_entry,
						  mode, pos, buf, bufsz);
		} else {
			pos = iwl_print_event_log(priv, next_entry - size,
						  size, mode, pos, buf, bufsz);
		}
	}
	return pos;
}

#define DEFAULT_DUMP_EVENT_LOG_ENTRIES (20)

int iwl_dump_nic_event_log(struct iwl_priv *priv, bool full_log,
			    char **buf)
{
	u32 base;       /* SRAM byte address of event log header */
	u32 capacity;   /* event log capacity in # entries */
	u32 mode;       /* 0 - no timestamp, 1 - timestamp recorded */
	u32 num_wraps;  /* # times uCode wrapped to top of log */
	u32 next_entry; /* index of next entry to be written by uCode */
	u32 size;       /* # entries that we'll print */
	u32 logsize;
	int pos = 0;
	size_t bufsz = 0;
	struct iwl_trans *trans = priv->trans;

	base = priv->device_pointers.log_event_table;
	if (priv->cur_ucode == IWL_UCODE_INIT) {
		logsize = priv->fw->init_evtlog_size;
		if (!base)
			base = priv->fw->init_evtlog_ptr;
	} else {
		logsize = priv->fw->inst_evtlog_size;
		if (!base)
			base = priv->fw->inst_evtlog_ptr;
	}

	if (!iwlagn_hw_valid_rtc_data_addr(base)) {
		IWL_ERR(priv,
			"Invalid event log pointer 0x%08X for %s uCode\n",
			base,
			(priv->cur_ucode == IWL_UCODE_INIT)
					? "Init" : "RT");
		return -EINVAL;
	}

	/* event log header */
	capacity = iwl_trans_read_mem32(trans, base);
	mode = iwl_trans_read_mem32(trans, base + (1 * sizeof(u32)));
	num_wraps = iwl_trans_read_mem32(trans, base + (2 * sizeof(u32)));
	next_entry = iwl_trans_read_mem32(trans, base + (3 * sizeof(u32)));

	if (capacity > logsize) {
		IWL_ERR(priv, "Log capacity %d is bogus, limit to %d "
			"entries\n", capacity, logsize);
		capacity = logsize;
	}

	if (next_entry > logsize) {
		IWL_ERR(priv, "Log write index %d is bogus, limit to %d\n",
			next_entry, logsize);
		next_entry = logsize;
	}

	size = num_wraps ? capacity : next_entry;

	/* bail out if nothing in log */
	if (size == 0) {
		IWL_ERR(trans, "Start IWL Event Log Dump: nothing in log\n");
		return pos;
	}

	if (!(iwl_have_debug_level(IWL_DL_FW_ERRORS)) && !full_log)
		size = (size > DEFAULT_DUMP_EVENT_LOG_ENTRIES)
			? DEFAULT_DUMP_EVENT_LOG_ENTRIES : size;
	IWL_ERR(priv, "Start IWL Event Log Dump: display last %u entries\n",
		size);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (buf) {
		if (full_log)
			bufsz = capacity * 48;
		else
			bufsz = size * 48;
		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;
	}
	if (iwl_have_debug_level(IWL_DL_FW_ERRORS) || full_log) {
		/*
		 * if uCode has wrapped back to top of log,
		 * start at the oldest entry,
		 * i.e the next one that uCode would fill.
		 */
		if (num_wraps)
			pos = iwl_print_event_log(priv, next_entry,
						capacity - next_entry, mode,
						pos, buf, bufsz);
		/* (then/else) start at top of log */
		pos = iwl_print_event_log(priv, 0,
					  next_entry, mode, pos, buf, bufsz);
	} else
		pos = iwl_print_last_event_logs(priv, capacity, num_wraps,
						next_entry, size, mode,
						pos, buf, bufsz);
#else
	pos = iwl_print_last_event_logs(priv, capacity, num_wraps,
					next_entry, size, mode,
					pos, buf, bufsz);
#endif
	return pos;
}

static void iwlagn_fw_error(struct iwl_priv *priv, bool ondemand)
{
	unsigned int reload_msec;
	unsigned long reload_jiffies;

	if (iwl_have_debug_level(IWL_DL_FW_ERRORS))
		iwl_print_rx_config_cmd(priv, IWL_RXON_CTX_BSS);

	/* uCode is no longer loaded. */
	priv->ucode_loaded = false;

	/* Set the FW error flag -- cleared on iwl_down */
	set_bit(STATUS_FW_ERROR, &priv->status);

	iwl_abort_notification_waits(&priv->notif_wait);

	/* Keep the restart process from trying to send host
	 * commands by clearing the ready bit */
	clear_bit(STATUS_READY, &priv->status);

	if (!ondemand) {
		/*
		 * If firmware keep reloading, then it indicate something
		 * serious wrong and firmware having problem to recover
		 * from it. Instead of keep trying which will fill the syslog
		 * and hang the system, let's just stop it
		 */
		reload_jiffies = jiffies;
		reload_msec = jiffies_to_msecs((long) reload_jiffies -
					(long) priv->reload_jiffies);
		priv->reload_jiffies = reload_jiffies;
		if (reload_msec <= IWL_MIN_RELOAD_DURATION) {
			priv->reload_count++;
			if (priv->reload_count >= IWL_MAX_CONTINUE_RELOAD_CNT) {
				IWL_ERR(priv, "BUG_ON, Stop restarting\n");
				return;
			}
		} else
			priv->reload_count = 0;
	}

	if (!test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		if (iwlwifi_mod_params.fw_restart) {
			IWL_DEBUG_FW_ERRORS(priv,
				  "Restarting adapter due to uCode error.\n");
			queue_work(priv->workqueue, &priv->restart);
		} else
			IWL_DEBUG_FW_ERRORS(priv,
				  "Detected FW error, but not restarting\n");
	}
}

static void iwl_nic_error(struct iwl_op_mode *op_mode)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);

	IWL_ERR(priv, "Loaded firmware version: %s\n",
		priv->fw->fw_version);

	iwl_dump_nic_error_log(priv);
	iwl_dump_nic_event_log(priv, false, NULL);

	iwlagn_fw_error(priv, false);
}

static void iwl_cmd_queue_full(struct iwl_op_mode *op_mode)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);

	if (!iwl_check_for_ct_kill(priv)) {
		IWL_ERR(priv, "Restarting adapter queue is full\n");
		iwlagn_fw_error(priv, false);
	}
}

#define EEPROM_RF_CONFIG_TYPE_MAX      0x3

static void iwl_nic_config(struct iwl_op_mode *op_mode)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);

	/* SKU Control */
	iwl_trans_set_bits_mask(priv->trans, CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_MSK_MAC_DASH |
				CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP,
				(CSR_HW_REV_STEP(priv->trans->hw_rev) <<
					CSR_HW_IF_CONFIG_REG_POS_MAC_STEP) |
				(CSR_HW_REV_DASH(priv->trans->hw_rev) <<
					CSR_HW_IF_CONFIG_REG_POS_MAC_DASH));

	/* write radio config values to register */
	if (priv->nvm_data->radio_cfg_type <= EEPROM_RF_CONFIG_TYPE_MAX) {
		u32 reg_val =
			priv->nvm_data->radio_cfg_type <<
				CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE |
			priv->nvm_data->radio_cfg_step <<
				CSR_HW_IF_CONFIG_REG_POS_PHY_STEP |
			priv->nvm_data->radio_cfg_dash <<
				CSR_HW_IF_CONFIG_REG_POS_PHY_DASH;

		iwl_trans_set_bits_mask(priv->trans, CSR_HW_IF_CONFIG_REG,
					CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE |
					CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP |
					CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH,
					reg_val);

		IWL_INFO(priv, "Radio type=0x%x-0x%x-0x%x\n",
			 priv->nvm_data->radio_cfg_type,
			 priv->nvm_data->radio_cfg_step,
			 priv->nvm_data->radio_cfg_dash);
	} else {
		WARN_ON(1);
	}

	/* set CSR_HW_CONFIG_REG for uCode use */
	iwl_set_bit(priv->trans, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
		    CSR_HW_IF_CONFIG_REG_BIT_MAC_SI);

	/* W/A : NIC is stuck in a reset state after Early PCIe power off
	 * (PCIe power is lost before PERST# is asserted),
	 * causing ME FW to lose ownership and not being able to obtain it back.
	 */
	iwl_set_bits_mask_prph(priv->trans, APMG_PS_CTRL_REG,
			       APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS,
			       ~APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS);

	if (priv->lib->nic_config)
		priv->lib->nic_config(priv);
}

static void iwl_wimax_active(struct iwl_op_mode *op_mode)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);

	clear_bit(STATUS_READY, &priv->status);
	IWL_ERR(priv, "RF is used by WiMAX\n");
}

static void iwl_stop_sw_queue(struct iwl_op_mode *op_mode, int queue)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);
	int mq = priv->queue_to_mac80211[queue];

	if (WARN_ON_ONCE(mq == IWL_INVALID_MAC80211_QUEUE))
		return;

	if (atomic_inc_return(&priv->queue_stop_count[mq]) > 1) {
		IWL_DEBUG_TX_QUEUES(priv,
			"queue %d (mac80211 %d) already stopped\n",
			queue, mq);
		return;
	}

	set_bit(mq, &priv->transport_queue_stop);
	ieee80211_stop_queue(priv->hw, mq);
}

static void iwl_wake_sw_queue(struct iwl_op_mode *op_mode, int queue)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);
	int mq = priv->queue_to_mac80211[queue];

	if (WARN_ON_ONCE(mq == IWL_INVALID_MAC80211_QUEUE))
		return;

	if (atomic_dec_return(&priv->queue_stop_count[mq]) > 0) {
		IWL_DEBUG_TX_QUEUES(priv,
			"queue %d (mac80211 %d) already awake\n",
			queue, mq);
		return;
	}

	clear_bit(mq, &priv->transport_queue_stop);

	if (!priv->passive_no_rx)
		ieee80211_wake_queue(priv->hw, mq);
}

void iwlagn_lift_passive_no_rx(struct iwl_priv *priv)
{
	int mq;

	if (!priv->passive_no_rx)
		return;

	for (mq = 0; mq < IWLAGN_FIRST_AMPDU_QUEUE; mq++) {
		if (!test_bit(mq, &priv->transport_queue_stop)) {
			IWL_DEBUG_TX_QUEUES(priv, "Wake queue %d\n", mq);
			ieee80211_wake_queue(priv->hw, mq);
		} else {
			IWL_DEBUG_TX_QUEUES(priv, "Don't wake queue %d\n", mq);
		}
	}

	priv->passive_no_rx = false;
}

static void iwl_free_skb(struct iwl_op_mode *op_mode, struct sk_buff *skb)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);
	struct ieee80211_tx_info *info;

	info = IEEE80211_SKB_CB(skb);
	iwl_trans_free_tx_cmd(priv->trans, info->driver_data[1]);
	ieee80211_free_txskb(priv->hw, skb);
}

static bool iwl_set_hw_rfkill_state(struct iwl_op_mode *op_mode, bool state)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);

	if (state)
		set_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		clear_bit(STATUS_RF_KILL_HW, &priv->status);

	wiphy_rfkill_set_hw_state(priv->hw->wiphy, state);

	return false;
}

static const struct iwl_op_mode_ops iwl_dvm_ops = {
	.start = iwl_op_mode_dvm_start,
	.stop = iwl_op_mode_dvm_stop,
	.rx = iwl_rx_dispatch,
	.queue_full = iwl_stop_sw_queue,
	.queue_not_full = iwl_wake_sw_queue,
	.hw_rf_kill = iwl_set_hw_rfkill_state,
	.free_skb = iwl_free_skb,
	.nic_error = iwl_nic_error,
	.cmd_queue_full = iwl_cmd_queue_full,
	.nic_config = iwl_nic_config,
	.wimax_active = iwl_wimax_active,
};

/*****************************************************************************
 *
 * driver and module entry point
 *
 *****************************************************************************/
static int __init iwl_init(void)
{

	int ret;

	ret = iwlagn_rate_control_register();
	if (ret) {
		pr_err("Unable to register rate control algorithm: %d\n", ret);
		return ret;
	}

	ret = iwl_opmode_register("iwldvm", &iwl_dvm_ops);
	if (ret) {
		pr_err("Unable to register op_mode: %d\n", ret);
		iwlagn_rate_control_unregister();
	}

	return ret;
}
module_init(iwl_init);

static void __exit iwl_exit(void)
{
	iwl_opmode_deregister("iwldvm");
	iwlagn_rate_control_unregister();
}
module_exit(iwl_exit);
