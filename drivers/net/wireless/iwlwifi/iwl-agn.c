/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include <net/mac80211.h>

#include <asm/div64.h>

#include "iwl-eeprom.h"
#include "iwl-wifi.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-agn-calib.h"
#include "iwl-agn.h"
#include "iwl-shared.h"
#include "iwl-bus.h"
#include "iwl-trans.h"

/******************************************************************************
 *
 * module boiler plate
 *
 ******************************************************************************/

/*
 * module name, copyright, version, etc.
 */
#define DRV_DESCRIPTION	"Intel(R) Wireless WiFi Link AGN driver for Linux"

#ifdef CONFIG_IWLWIFI_DEBUG
#define VD "d"
#else
#define VD
#endif

#define DRV_VERSION     IWLWIFI_VERSION VD


MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_ALIAS("iwlagn");

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
		.flags = CMD_SYNC,
	};
	struct ieee80211_tx_info *info;
	u32 frame_size;
	u32 rate_flags;
	u32 rate;

	/*
	 * We have to set up the TX command, the TX Beacon command, and the
	 * beacon contents.
	 */

	lockdep_assert_held(&priv->shrd->mutex);

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
					      hw_params(priv).valid_tx_ant);
	rate_flags = iwl_ant_idx_to_flags(priv->mgmt_tx_ant);

	/* In mac80211, rates for 5 GHz start at 0 */
	if (info->band == IEEE80211_BAND_5GHZ)
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

	return iwl_trans_send_cmd(trans(priv), &cmd);
}

static void iwl_bg_beacon_update(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, beacon_update);
	struct sk_buff *beacon;

	mutex_lock(&priv->shrd->mutex);
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
	mutex_unlock(&priv->shrd->mutex);
}

static void iwl_bg_bt_runtime_config(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, bt_runtime_config);

	if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status))
		return;

	/* dont send host command if rf-kill is on */
	if (!iwl_is_ready_rf(priv->shrd))
		return;
	iwlagn_send_advance_bt_config(priv);
}

static void iwl_bg_bt_full_concurrency(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, bt_full_concurrency);
	struct iwl_rxon_context *ctx;

	mutex_lock(&priv->shrd->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status))
		goto out;

	/* dont send host command if rf-kill is on */
	if (!iwl_is_ready_rf(priv->shrd))
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
	mutex_unlock(&priv->shrd->mutex);
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
static void iwl_bg_statistics_periodic(unsigned long data)
{
	struct iwl_priv *priv = (struct iwl_priv *)data;

	if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status))
		return;

	/* dont send host command if rf-kill is on */
	if (!iwl_is_ready_rf(priv->shrd))
		return;

	iwl_send_statistics_request(priv, CMD_ASYNC, false);
}


static void iwl_print_cont_event_trace(struct iwl_priv *priv, u32 base,
					u32 start_idx, u32 num_events,
					u32 mode)
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
	spin_lock_irqsave(&bus(priv)->reg_lock, reg_flags);
	if (iwl_grab_nic_access(bus(priv))) {
		spin_unlock_irqrestore(&bus(priv)->reg_lock, reg_flags);
		return;
	}

	/* Set starting address; reads will auto-increment */
	iwl_write32(bus(priv), HBUS_TARG_MEM_RADDR, ptr);
	rmb();

	/*
	 * "time" is actually "data" for mode 0 (no timestamp).
	 * place event id # at far right for easier visual parsing.
	 */
	for (i = 0; i < num_events; i++) {
		ev = iwl_read32(bus(priv), HBUS_TARG_MEM_RDAT);
		time = iwl_read32(bus(priv), HBUS_TARG_MEM_RDAT);
		if (mode == 0) {
			trace_iwlwifi_dev_ucode_cont_event(priv,
							0, time, ev);
		} else {
			data = iwl_read32(bus(priv), HBUS_TARG_MEM_RDAT);
			trace_iwlwifi_dev_ucode_cont_event(priv,
						time, data, ev);
		}
	}
	/* Allow device to power down */
	iwl_release_nic_access(bus(priv));
	spin_unlock_irqrestore(&bus(priv)->reg_lock, reg_flags);
}

static void iwl_continuous_event_trace(struct iwl_priv *priv)
{
	u32 capacity;   /* event log capacity in # entries */
	u32 base;       /* SRAM byte address of event log header */
	u32 mode;       /* 0 - no timestamp, 1 - timestamp recorded */
	u32 num_wraps;  /* # times uCode wrapped to top of log */
	u32 next_entry; /* index of next entry to be written by uCode */

	base = priv->shrd->device_pointers.error_event_table;
	if (iwlagn_hw_valid_rtc_data_addr(base)) {
		capacity = iwl_read_targ_mem(bus(priv), base);
		num_wraps = iwl_read_targ_mem(bus(priv),
						base + (2 * sizeof(u32)));
		mode = iwl_read_targ_mem(bus(priv), base + (1 * sizeof(u32)));
		next_entry = iwl_read_targ_mem(bus(priv),
						base + (3 * sizeof(u32)));
	} else
		return;

	if (num_wraps == priv->event_log.num_wraps) {
		iwl_print_cont_event_trace(priv,
				       base, priv->event_log.next_entry,
				       next_entry - priv->event_log.next_entry,
				       mode);
		priv->event_log.non_wraps_count++;
	} else {
		if ((num_wraps - priv->event_log.num_wraps) > 1)
			priv->event_log.wraps_more_count++;
		else
			priv->event_log.wraps_once_count++;
		trace_iwlwifi_dev_ucode_wrap_event(priv,
				num_wraps - priv->event_log.num_wraps,
				next_entry, priv->event_log.next_entry);
		if (next_entry < priv->event_log.next_entry) {
			iwl_print_cont_event_trace(priv, base,
			       priv->event_log.next_entry,
			       capacity - priv->event_log.next_entry,
			       mode);

			iwl_print_cont_event_trace(priv, base, 0,
				next_entry, mode);
		} else {
			iwl_print_cont_event_trace(priv, base,
			       next_entry, capacity - next_entry,
			       mode);

			iwl_print_cont_event_trace(priv, base, 0,
				next_entry, mode);
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
static void iwl_bg_ucode_trace(unsigned long data)
{
	struct iwl_priv *priv = (struct iwl_priv *)data;

	if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status))
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

	if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status))
		return;

	/* do nothing if rf-kill is on */
	if (!iwl_is_ready_rf(priv->shrd))
		return;

	IWL_DEBUG_INFO(priv, "device request: flush all tx frames\n");
	iwlagn_dev_txfifo_flush(priv, IWL_DROP_ALL);
}

static void iwl_init_context(struct iwl_priv *priv, u32 ucode_flags)
{
	int i;

	/*
	 * The default context is always valid,
	 * the PAN context depends on uCode.
	 */
	priv->shrd->valid_contexts = BIT(IWL_RXON_CTX_BSS);
	if (ucode_flags & IWL_UCODE_TLV_FLAGS_PAN)
		priv->shrd->valid_contexts |= BIT(IWL_RXON_CTX_PAN);

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
	priv->contexts[IWL_RXON_CTX_BSS].exclusive_interface_modes =
		BIT(NL80211_IFTYPE_ADHOC);
	priv->contexts[IWL_RXON_CTX_BSS].interface_modes =
		BIT(NL80211_IFTYPE_STATION);
	priv->contexts[IWL_RXON_CTX_BSS].ap_devtype = RXON_DEV_TYPE_AP;
	priv->contexts[IWL_RXON_CTX_BSS].ibss_devtype = RXON_DEV_TYPE_IBSS;
	priv->contexts[IWL_RXON_CTX_BSS].station_devtype = RXON_DEV_TYPE_ESS;
	priv->contexts[IWL_RXON_CTX_BSS].unused_devtype = RXON_DEV_TYPE_ESS;

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

	if (ucode_flags & IWL_UCODE_TLV_FLAGS_P2P)
		priv->contexts[IWL_RXON_CTX_PAN].interface_modes |=
			BIT(NL80211_IFTYPE_P2P_CLIENT) |
			BIT(NL80211_IFTYPE_P2P_GO);

	priv->contexts[IWL_RXON_CTX_PAN].ap_devtype = RXON_DEV_TYPE_CP;
	priv->contexts[IWL_RXON_CTX_PAN].station_devtype = RXON_DEV_TYPE_2STA;
	priv->contexts[IWL_RXON_CTX_PAN].unused_devtype = RXON_DEV_TYPE_P2P;

	BUILD_BUG_ON(NUM_IWL_RXON_CTX != 2);
}

static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context);

#define UCODE_EXPERIMENTAL_INDEX	100
#define UCODE_EXPERIMENTAL_TAG		"exp"

static int __must_check iwl_request_firmware(struct iwl_priv *priv, bool first)
{
	const char *name_pre = cfg(priv)->fw_name_pre;
	char tag[8];

	if (first) {
#ifdef CONFIG_IWLWIFI_DEBUG_EXPERIMENTAL_UCODE
		priv->fw_index = UCODE_EXPERIMENTAL_INDEX;
		strcpy(tag, UCODE_EXPERIMENTAL_TAG);
	} else if (priv->fw_index == UCODE_EXPERIMENTAL_INDEX) {
#endif
		priv->fw_index = cfg(priv)->ucode_api_max;
		sprintf(tag, "%d", priv->fw_index);
	} else {
		priv->fw_index--;
		sprintf(tag, "%d", priv->fw_index);
	}

	if (priv->fw_index < cfg(priv)->ucode_api_min) {
		IWL_ERR(priv, "no suitable firmware found!\n");
		return -ENOENT;
	}

	sprintf(priv->firmware_name, "%s%s%s", name_pre, tag, ".ucode");

	IWL_DEBUG_INFO(priv, "attempting to load firmware %s'%s'\n",
		       (priv->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? "EXPERIMENTAL " : "",
		       priv->firmware_name);

	return request_firmware_nowait(THIS_MODULE, 1, priv->firmware_name,
				       bus(priv)->dev,
				       GFP_KERNEL, priv, iwl_ucode_callback);
}

struct iwlagn_firmware_pieces {
	const void *inst, *data, *init, *init_data, *wowlan_inst, *wowlan_data;
	size_t inst_size, data_size, init_size, init_data_size,
	       wowlan_inst_size, wowlan_data_size;

	u32 build;

	u32 init_evtlog_ptr, init_evtlog_size, init_errlog_ptr;
	u32 inst_evtlog_ptr, inst_evtlog_size, inst_errlog_ptr;
};

static int iwlagn_load_legacy_firmware(struct iwl_priv *priv,
				       const struct firmware *ucode_raw,
				       struct iwlagn_firmware_pieces *pieces)
{
	struct iwl_ucode_header *ucode = (void *)ucode_raw->data;
	u32 api_ver, hdr_size;
	const u8 *src;

	priv->ucode_ver = le32_to_cpu(ucode->ver);
	api_ver = IWL_UCODE_API(priv->ucode_ver);

	switch (api_ver) {
	default:
		hdr_size = 28;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(priv, "File size too small!\n");
			return -EINVAL;
		}
		pieces->build = le32_to_cpu(ucode->u.v2.build);
		pieces->inst_size = le32_to_cpu(ucode->u.v2.inst_size);
		pieces->data_size = le32_to_cpu(ucode->u.v2.data_size);
		pieces->init_size = le32_to_cpu(ucode->u.v2.init_size);
		pieces->init_data_size = le32_to_cpu(ucode->u.v2.init_data_size);
		src = ucode->u.v2.data;
		break;
	case 0:
	case 1:
	case 2:
		hdr_size = 24;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(priv, "File size too small!\n");
			return -EINVAL;
		}
		pieces->build = 0;
		pieces->inst_size = le32_to_cpu(ucode->u.v1.inst_size);
		pieces->data_size = le32_to_cpu(ucode->u.v1.data_size);
		pieces->init_size = le32_to_cpu(ucode->u.v1.init_size);
		pieces->init_data_size = le32_to_cpu(ucode->u.v1.init_data_size);
		src = ucode->u.v1.data;
		break;
	}

	/* Verify size of file vs. image size info in file's header */
	if (ucode_raw->size != hdr_size + pieces->inst_size +
				pieces->data_size + pieces->init_size +
				pieces->init_data_size) {

		IWL_ERR(priv,
			"uCode file size %d does not match expected size\n",
			(int)ucode_raw->size);
		return -EINVAL;
	}

	pieces->inst = src;
	src += pieces->inst_size;
	pieces->data = src;
	src += pieces->data_size;
	pieces->init = src;
	src += pieces->init_size;
	pieces->init_data = src;
	src += pieces->init_data_size;

	return 0;
}

static int iwlagn_load_firmware(struct iwl_priv *priv,
				const struct firmware *ucode_raw,
				struct iwlagn_firmware_pieces *pieces,
				struct iwlagn_ucode_capabilities *capa)
{
	struct iwl_tlv_ucode_header *ucode = (void *)ucode_raw->data;
	struct iwl_ucode_tlv *tlv;
	size_t len = ucode_raw->size;
	const u8 *data;
	int wanted_alternative = iwlagn_mod_params.wanted_ucode_alternative;
	int tmp;
	u64 alternatives;
	u32 tlv_len;
	enum iwl_ucode_tlv_type tlv_type;
	const u8 *tlv_data;

	if (len < sizeof(*ucode)) {
		IWL_ERR(priv, "uCode has invalid length: %zd\n", len);
		return -EINVAL;
	}

	if (ucode->magic != cpu_to_le32(IWL_TLV_UCODE_MAGIC)) {
		IWL_ERR(priv, "invalid uCode magic: 0X%x\n",
			le32_to_cpu(ucode->magic));
		return -EINVAL;
	}

	/*
	 * Check which alternatives are present, and "downgrade"
	 * when the chosen alternative is not present, warning
	 * the user when that happens. Some files may not have
	 * any alternatives, so don't warn in that case.
	 */
	alternatives = le64_to_cpu(ucode->alternatives);
	tmp = wanted_alternative;
	if (wanted_alternative > 63)
		wanted_alternative = 63;
	while (wanted_alternative && !(alternatives & BIT(wanted_alternative)))
		wanted_alternative--;
	if (wanted_alternative && wanted_alternative != tmp)
		IWL_WARN(priv,
			 "uCode alternative %d not available, choosing %d\n",
			 tmp, wanted_alternative);

	priv->ucode_ver = le32_to_cpu(ucode->ver);
	pieces->build = le32_to_cpu(ucode->build);
	data = ucode->data;

	len -= sizeof(*ucode);

	while (len >= sizeof(*tlv)) {
		u16 tlv_alt;

		len -= sizeof(*tlv);
		tlv = (void *)data;

		tlv_len = le32_to_cpu(tlv->length);
		tlv_type = le16_to_cpu(tlv->type);
		tlv_alt = le16_to_cpu(tlv->alternative);
		tlv_data = tlv->data;

		if (len < tlv_len) {
			IWL_ERR(priv, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}
		len -= ALIGN(tlv_len, 4);
		data += sizeof(*tlv) + ALIGN(tlv_len, 4);

		/*
		 * Alternative 0 is always valid.
		 *
		 * Skip alternative TLVs that are not selected.
		 */
		if (tlv_alt != 0 && tlv_alt != wanted_alternative)
			continue;

		switch (tlv_type) {
		case IWL_UCODE_TLV_INST:
			pieces->inst = tlv_data;
			pieces->inst_size = tlv_len;
			break;
		case IWL_UCODE_TLV_DATA:
			pieces->data = tlv_data;
			pieces->data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_INIT:
			pieces->init = tlv_data;
			pieces->init_size = tlv_len;
			break;
		case IWL_UCODE_TLV_INIT_DATA:
			pieces->init_data = tlv_data;
			pieces->init_data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_BOOT:
			IWL_ERR(priv, "Found unexpected BOOT ucode\n");
			break;
		case IWL_UCODE_TLV_PROBE_MAX_LEN:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->max_probe_length =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_PAN:
			if (tlv_len)
				goto invalid_tlv_len;
			capa->flags |= IWL_UCODE_TLV_FLAGS_PAN;
			break;
		case IWL_UCODE_TLV_FLAGS:
			/* must be at least one u32 */
			if (tlv_len < sizeof(u32))
				goto invalid_tlv_len;
			/* and a proper number of u32s */
			if (tlv_len % sizeof(u32))
				goto invalid_tlv_len;
			/*
			 * This driver only reads the first u32 as
			 * right now no more features are defined,
			 * if that changes then either the driver
			 * will not work with the new firmware, or
			 * it'll not take advantage of new features.
			 */
			capa->flags = le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_errlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_errlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_ENHANCE_SENS_TBL:
			if (tlv_len)
				goto invalid_tlv_len;
			priv->enhance_sensitivity_table = true;
			break;
		case IWL_UCODE_TLV_WOWLAN_INST:
			pieces->wowlan_inst = tlv_data;
			pieces->wowlan_inst_size = tlv_len;
			break;
		case IWL_UCODE_TLV_WOWLAN_DATA:
			pieces->wowlan_data = tlv_data;
			pieces->wowlan_data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_PHY_CALIBRATION_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->standard_phy_calibration_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		default:
			IWL_DEBUG_INFO(priv, "unknown TLV: %d\n", tlv_type);
			break;
		}
	}

	if (len) {
		IWL_ERR(priv, "invalid TLV after parsing: %zd\n", len);
		iwl_print_hex_dump(priv, IWL_DL_FW, (u8 *)data, len);
		return -EINVAL;
	}

	return 0;

 invalid_tlv_len:
	IWL_ERR(priv, "TLV %d has invalid size: %u\n", tlv_type, tlv_len);
	iwl_print_hex_dump(priv, IWL_DL_FW, tlv_data, tlv_len);

	return -EINVAL;
}

/**
 * iwl_ucode_callback - callback when firmware was loaded
 *
 * If loaded successfully, copies the firmware into buffers
 * for the card to fetch (via DMA).
 */
static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context)
{
	struct iwl_priv *priv = context;
	struct iwl_ucode_header *ucode;
	int err;
	struct iwlagn_firmware_pieces pieces;
	const unsigned int api_max = cfg(priv)->ucode_api_max;
	unsigned int api_ok = cfg(priv)->ucode_api_ok;
	const unsigned int api_min = cfg(priv)->ucode_api_min;
	u32 api_ver;
	char buildstr[25];
	u32 build;
	struct iwlagn_ucode_capabilities ucode_capa = {
		.max_probe_length = 200,
		.standard_phy_calibration_size =
			IWL_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE,
	};

	if (!api_ok)
		api_ok = api_max;

	memset(&pieces, 0, sizeof(pieces));

	if (!ucode_raw) {
		if (priv->fw_index <= api_ok)
			IWL_ERR(priv,
				"request for firmware file '%s' failed.\n",
				priv->firmware_name);
		goto try_again;
	}

	IWL_DEBUG_INFO(priv, "Loaded firmware file '%s' (%zd bytes).\n",
		       priv->firmware_name, ucode_raw->size);

	/* Make sure that we got at least the API version number */
	if (ucode_raw->size < 4) {
		IWL_ERR(priv, "File size way too small!\n");
		goto try_again;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (struct iwl_ucode_header *)ucode_raw->data;

	if (ucode->ver)
		err = iwlagn_load_legacy_firmware(priv, ucode_raw, &pieces);
	else
		err = iwlagn_load_firmware(priv, ucode_raw, &pieces,
					   &ucode_capa);

	if (err)
		goto try_again;

	api_ver = IWL_UCODE_API(priv->ucode_ver);
	build = pieces.build;

	/*
	 * api_ver should match the api version forming part of the
	 * firmware filename ... but we don't check for that and only rely
	 * on the API version read from firmware header from here on forward
	 */
	/* no api version check required for experimental uCode */
	if (priv->fw_index != UCODE_EXPERIMENTAL_INDEX) {
		if (api_ver < api_min || api_ver > api_max) {
			IWL_ERR(priv,
				"Driver unable to support your firmware API. "
				"Driver supports v%u, firmware is v%u.\n",
				api_max, api_ver);
			goto try_again;
		}

		if (api_ver < api_ok) {
			if (api_ok != api_max)
				IWL_ERR(priv, "Firmware has old API version, "
					"expected v%u through v%u, got v%u.\n",
					api_ok, api_max, api_ver);
			else
				IWL_ERR(priv, "Firmware has old API version, "
					"expected v%u, got v%u.\n",
					api_max, api_ver);
			IWL_ERR(priv, "New firmware can be obtained from "
				      "http://www.intellinuxwireless.org/.\n");
		}
	}

	if (build)
		sprintf(buildstr, " build %u%s", build,
		       (priv->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? " (EXP)" : "");
	else
		buildstr[0] = '\0';

	IWL_INFO(priv, "loaded firmware version %u.%u.%u.%u%s\n",
		 IWL_UCODE_MAJOR(priv->ucode_ver),
		 IWL_UCODE_MINOR(priv->ucode_ver),
		 IWL_UCODE_API(priv->ucode_ver),
		 IWL_UCODE_SERIAL(priv->ucode_ver),
		 buildstr);

	snprintf(priv->hw->wiphy->fw_version,
		 sizeof(priv->hw->wiphy->fw_version),
		 "%u.%u.%u.%u%s",
		 IWL_UCODE_MAJOR(priv->ucode_ver),
		 IWL_UCODE_MINOR(priv->ucode_ver),
		 IWL_UCODE_API(priv->ucode_ver),
		 IWL_UCODE_SERIAL(priv->ucode_ver),
		 buildstr);

	/*
	 * For any of the failures below (before allocating pci memory)
	 * we will try to load a version with a smaller API -- maybe the
	 * user just got a corrupted version of the latest API.
	 */

	IWL_DEBUG_INFO(priv, "f/w package hdr ucode version raw = 0x%x\n",
		       priv->ucode_ver);
	IWL_DEBUG_INFO(priv, "f/w package hdr runtime inst size = %Zd\n",
		       pieces.inst_size);
	IWL_DEBUG_INFO(priv, "f/w package hdr runtime data size = %Zd\n",
		       pieces.data_size);
	IWL_DEBUG_INFO(priv, "f/w package hdr init inst size = %Zd\n",
		       pieces.init_size);
	IWL_DEBUG_INFO(priv, "f/w package hdr init data size = %Zd\n",
		       pieces.init_data_size);

	/* Verify that uCode images will fit in card's SRAM */
	if (pieces.inst_size > hw_params(priv).max_inst_size) {
		IWL_ERR(priv, "uCode instr len %Zd too large to fit in\n",
			pieces.inst_size);
		goto try_again;
	}

	if (pieces.data_size > hw_params(priv).max_data_size) {
		IWL_ERR(priv, "uCode data len %Zd too large to fit in\n",
			pieces.data_size);
		goto try_again;
	}

	if (pieces.init_size > hw_params(priv).max_inst_size) {
		IWL_ERR(priv, "uCode init instr len %Zd too large to fit in\n",
			pieces.init_size);
		goto try_again;
	}

	if (pieces.init_data_size > hw_params(priv).max_data_size) {
		IWL_ERR(priv, "uCode init data len %Zd too large to fit in\n",
			pieces.init_data_size);
		goto try_again;
	}

	/* Allocate ucode buffers for card's bus-master loading ... */

	/* Runtime instructions and 2 copies of data:
	 * 1) unmodified from disk
	 * 2) backup cache for save/restore during power-downs */
	if (iwl_alloc_fw_desc(bus(priv), &trans(priv)->ucode_rt.code,
			      pieces.inst, pieces.inst_size))
		goto err_pci_alloc;
	if (iwl_alloc_fw_desc(bus(priv), &trans(priv)->ucode_rt.data,
			      pieces.data, pieces.data_size))
		goto err_pci_alloc;

	/* Initialization instructions and data */
	if (pieces.init_size && pieces.init_data_size) {
		if (iwl_alloc_fw_desc(bus(priv), &trans(priv)->ucode_init.code,
				      pieces.init, pieces.init_size))
			goto err_pci_alloc;
		if (iwl_alloc_fw_desc(bus(priv), &trans(priv)->ucode_init.data,
				      pieces.init_data, pieces.init_data_size))
			goto err_pci_alloc;
	}

	/* WoWLAN instructions and data */
	if (pieces.wowlan_inst_size && pieces.wowlan_data_size) {
		if (iwl_alloc_fw_desc(bus(priv),
				      &trans(priv)->ucode_wowlan.code,
				      pieces.wowlan_inst,
				      pieces.wowlan_inst_size))
			goto err_pci_alloc;
		if (iwl_alloc_fw_desc(bus(priv),
				      &trans(priv)->ucode_wowlan.data,
				      pieces.wowlan_data,
				      pieces.wowlan_data_size))
			goto err_pci_alloc;
	}

	/* Now that we can no longer fail, copy information */

	/*
	 * The (size - 16) / 12 formula is based on the information recorded
	 * for each event, which is of mode 1 (including timestamp) for all
	 * new microcodes that include this information.
	 */
	priv->init_evtlog_ptr = pieces.init_evtlog_ptr;
	if (pieces.init_evtlog_size)
		priv->init_evtlog_size = (pieces.init_evtlog_size - 16)/12;
	else
		priv->init_evtlog_size =
			cfg(priv)->base_params->max_event_log_size;
	priv->init_errlog_ptr = pieces.init_errlog_ptr;
	priv->inst_evtlog_ptr = pieces.inst_evtlog_ptr;
	if (pieces.inst_evtlog_size)
		priv->inst_evtlog_size = (pieces.inst_evtlog_size - 16)/12;
	else
		priv->inst_evtlog_size =
			cfg(priv)->base_params->max_event_log_size;
	priv->inst_errlog_ptr = pieces.inst_errlog_ptr;
#ifndef CONFIG_IWLWIFI_P2P
	ucode_capa.flags &= ~IWL_UCODE_TLV_FLAGS_PAN;
#endif

	priv->new_scan_threshold_behaviour =
		!!(ucode_capa.flags & IWL_UCODE_TLV_FLAGS_NEWSCAN);

	if (!(cfg(priv)->sku & EEPROM_SKU_CAP_IPAN_ENABLE))
		ucode_capa.flags &= ~IWL_UCODE_TLV_FLAGS_PAN;

	/*
	 * if not PAN, then don't support P2P -- might be a uCode
	 * packaging bug or due to the eeprom check above
	 */
	if (!(ucode_capa.flags & IWL_UCODE_TLV_FLAGS_PAN))
		ucode_capa.flags &= ~IWL_UCODE_TLV_FLAGS_P2P;

	if (ucode_capa.flags & IWL_UCODE_TLV_FLAGS_PAN) {
		priv->sta_key_max_num = STA_KEY_MAX_NUM_PAN;
		priv->shrd->cmd_queue = IWL_IPAN_CMD_QUEUE_NUM;
	} else {
		priv->sta_key_max_num = STA_KEY_MAX_NUM;
		priv->shrd->cmd_queue = IWL_DEFAULT_CMD_QUEUE_NUM;
	}
	/*
	 * figure out the offset of chain noise reset and gain commands
	 * base on the size of standard phy calibration commands table size
	 */
	if (ucode_capa.standard_phy_calibration_size >
	    IWL_MAX_PHY_CALIBRATE_TBL_SIZE)
		ucode_capa.standard_phy_calibration_size =
			IWL_MAX_STANDARD_PHY_CALIBRATE_TBL_SIZE;

	priv->phy_calib_chain_noise_reset_cmd =
		ucode_capa.standard_phy_calibration_size;
	priv->phy_calib_chain_noise_gain_cmd =
		ucode_capa.standard_phy_calibration_size + 1;

	/* initialize all valid contexts */
	iwl_init_context(priv, ucode_capa.flags);

	/**************************************************
	 * This is still part of probe() in a sense...
	 *
	 * 9. Setup and register with mac80211 and debugfs
	 **************************************************/
	err = iwlagn_mac_setup_register(priv, &ucode_capa);
	if (err)
		goto out_unbind;

	err = iwl_dbgfs_register(priv, DRV_NAME);
	if (err)
		IWL_ERR(priv, "failed to create debugfs files. Ignoring error: %d\n", err);

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);
	complete(&priv->firmware_loading_complete);
	return;

 try_again:
	/* try next, if any */
	if (iwl_request_firmware(priv, false))
		goto out_unbind;
	release_firmware(ucode_raw);
	return;

 err_pci_alloc:
	IWL_ERR(priv, "failed to allocate pci memory\n");
	iwl_dealloc_ucode(trans(priv));
 out_unbind:
	complete(&priv->firmware_loading_complete);
	device_release_driver(bus(priv)->dev);
	release_firmware(ucode_raw);
}

static void iwl_rf_kill_ct_config(struct iwl_priv *priv)
{
	struct iwl_ct_kill_config cmd;
	struct iwl_ct_kill_throttling_config adv_cmd;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&priv->shrd->lock, flags);
	iwl_write32(bus(priv), CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
	spin_unlock_irqrestore(&priv->shrd->lock, flags);
	priv->thermal_throttle.ct_kill_toggle = false;

	if (cfg(priv)->base_params->support_ct_kill_exit) {
		adv_cmd.critical_temperature_enter =
			cpu_to_le32(hw_params(priv).ct_kill_threshold);
		adv_cmd.critical_temperature_exit =
			cpu_to_le32(hw_params(priv).ct_kill_exit_threshold);

		ret = iwl_trans_send_cmd_pdu(trans(priv),
				       REPLY_CT_KILL_CONFIG_CMD,
				       CMD_SYNC, sizeof(adv_cmd), &adv_cmd);
		if (ret)
			IWL_ERR(priv, "REPLY_CT_KILL_CONFIG_CMD failed\n");
		else
			IWL_DEBUG_INFO(priv, "REPLY_CT_KILL_CONFIG_CMD "
				"succeeded, critical temperature enter is %d,"
				"exit is %d\n",
				hw_params(priv).ct_kill_threshold,
				hw_params(priv).ct_kill_exit_threshold);
	} else {
		cmd.critical_temperature_R =
			cpu_to_le32(hw_params(priv).ct_kill_threshold);

		ret = iwl_trans_send_cmd_pdu(trans(priv),
				       REPLY_CT_KILL_CONFIG_CMD,
				       CMD_SYNC, sizeof(cmd), &cmd);
		if (ret)
			IWL_ERR(priv, "REPLY_CT_KILL_CONFIG_CMD failed\n");
		else
			IWL_DEBUG_INFO(priv, "REPLY_CT_KILL_CONFIG_CMD "
				"succeeded, "
				"critical temperature is %d\n",
				hw_params(priv).ct_kill_threshold);
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

	return iwl_trans_send_cmd(trans(priv), &cmd);
}


static int iwlagn_send_tx_ant_config(struct iwl_priv *priv, u8 valid_tx_ant)
{
	struct iwl_tx_ant_config_cmd tx_ant_cmd = {
	  .valid = cpu_to_le32(valid_tx_ant),
	};

	if (IWL_UCODE_API(priv->ucode_ver) > 1) {
		IWL_DEBUG_HC(priv, "select valid tx ant: %u\n", valid_tx_ant);
		return iwl_trans_send_cmd_pdu(trans(priv),
					TX_ANT_CONFIGURATION_CMD,
					CMD_SYNC,
					sizeof(struct iwl_tx_ant_config_cmd),
					&tx_ant_cmd);
	} else {
		IWL_DEBUG_HC(priv, "TX_ANT_CONFIGURATION_CMD not supported\n");
		return -EOPNOTSUPP;
	}
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

	/*TODO: this should go to the transport layer */
	iwl_reset_ict(trans(priv));

	IWL_DEBUG_INFO(priv, "Runtime Alive received.\n");

	/* After the ALIVE response, we can send host commands to the uCode */
	set_bit(STATUS_ALIVE, &priv->shrd->status);

	/* Enable watchdog to monitor the driver tx queues */
	iwl_setup_watchdog(priv);

	if (iwl_is_rfkill(priv->shrd))
		return -ERFKILL;

	/* download priority table before any calibration request */
	if (cfg(priv)->bt_params &&
	    cfg(priv)->bt_params->advanced_bt_coexist) {
		/* Configure Bluetooth device coexistence support */
		if (cfg(priv)->bt_params->bt_sco_disable)
			priv->bt_enable_pspoll = false;
		else
			priv->bt_enable_pspoll = true;

		priv->bt_valid = IWLAGN_BT_ALL_VALID_MSK;
		priv->kill_ack_mask = IWLAGN_BT_KILL_ACK_MASK_DEFAULT;
		priv->kill_cts_mask = IWLAGN_BT_KILL_CTS_MASK_DEFAULT;
		iwlagn_send_advance_bt_config(priv);
		priv->bt_valid = IWLAGN_BT_VALID_ENABLE_FLAGS;
		priv->cur_rssi_ctx = NULL;

		iwl_send_prio_tbl(trans(priv));

		/* FIXME: w/a to force change uCode BT state machine */
		ret = iwl_send_bt_env(trans(priv), IWL_BT_COEX_ENV_OPEN,
					 BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
		if (ret)
			return ret;
		ret = iwl_send_bt_env(trans(priv), IWL_BT_COEX_ENV_CLOSE,
					 BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
		if (ret)
			return ret;
	} else {
		/*
		 * default is 2-wire BT coexexistence support
		 */
		iwl_send_bt_config(priv);
	}

	if (hw_params(priv).calib_rt_cfg)
		iwlagn_send_calib_cfg_rt(priv,
					 hw_params(priv).calib_rt_cfg);

	ieee80211_wake_queues(priv->hw);

	priv->active_rate = IWL_RATES_MASK;

	/* Configure Tx antenna selection based on H/W config */
	iwlagn_send_tx_ant_config(priv, cfg(priv)->valid_tx_ant);

	if (iwl_is_associated_ctx(ctx) && !priv->shrd->wowlan) {
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

	if (!priv->shrd->wowlan) {
		/* WoWLAN ucode will not reply in the same way, skip it */
		iwl_reset_run_time_calib(priv);
	}

	set_bit(STATUS_READY, &priv->shrd->status);

	/* Configure the adapter for unassociated operation */
	ret = iwlagn_commit_rxon(priv, ctx);
	if (ret)
		return ret;

	/* At this point, the NIC is initialized and operational */
	iwl_rf_kill_ct_config(priv);

	IWL_DEBUG_INFO(priv, "ALIVE processing complete.\n");

	return iwl_power_update_mode(priv, true);
}

static void iwl_cancel_deferred_work(struct iwl_priv *priv);

void __iwl_down(struct iwl_priv *priv)
{
	int exit_pending;

	IWL_DEBUG_INFO(priv, DRV_NAME " is going down\n");

	iwl_scan_cancel_timeout(priv, 200);

	/*
	 * If active, scanning won't cancel it, so say it expired.
	 * No race since we hold the mutex here and a new one
	 * can't come in at this time.
	 */
	ieee80211_remain_on_channel_expired(priv->hw);

	exit_pending =
		test_and_set_bit(STATUS_EXIT_PENDING, &priv->shrd->status);

	/* Stop TX queues watchdog. We need to have STATUS_EXIT_PENDING bit set
	 * to prevent rearm timer */
	del_timer_sync(&priv->watchdog);

	iwl_clear_ucode_stations(priv, NULL);
	iwl_dealloc_bcast_stations(priv);
	iwl_clear_driver_stations(priv);

	/* reset BT coex data */
	priv->bt_status = 0;
	priv->cur_rssi_ctx = NULL;
	priv->bt_is_sco = 0;
	if (cfg(priv)->bt_params)
		priv->bt_traffic_load =
			 cfg(priv)->bt_params->bt_init_traffic_load;
	else
		priv->bt_traffic_load = 0;
	priv->bt_full_concurrent = false;
	priv->bt_ci_compliance = 0;

	/* Wipe out the EXIT_PENDING status bit if we are not actually
	 * exiting the module */
	if (!exit_pending)
		clear_bit(STATUS_EXIT_PENDING, &priv->shrd->status);

	if (priv->mac80211_registered)
		ieee80211_stop_queues(priv->hw);

	iwl_trans_stop_device(trans(priv));

	/* Clear out all status bits but a few that are stable across reset */
	priv->shrd->status &=
			test_bit(STATUS_RF_KILL_HW, &priv->shrd->status) <<
				STATUS_RF_KILL_HW |
			test_bit(STATUS_GEO_CONFIGURED, &priv->shrd->status) <<
				STATUS_GEO_CONFIGURED |
			test_bit(STATUS_FW_ERROR, &priv->shrd->status) <<
				STATUS_FW_ERROR |
			test_bit(STATUS_EXIT_PENDING, &priv->shrd->status) <<
				STATUS_EXIT_PENDING;

	dev_kfree_skb(priv->beacon_skb);
	priv->beacon_skb = NULL;
}

void iwl_down(struct iwl_priv *priv)
{
	mutex_lock(&priv->shrd->mutex);
	__iwl_down(priv);
	mutex_unlock(&priv->shrd->mutex);

	iwl_cancel_deferred_work(priv);
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

	mutex_lock(&priv->shrd->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status) ||
	    test_bit(STATUS_SCANNING, &priv->shrd->status)) {
		mutex_unlock(&priv->shrd->mutex);
		return;
	}

	if (priv->start_calib) {
		iwl_chain_noise_calibration(priv);
		iwl_sensitivity_calibration(priv);
	}

	mutex_unlock(&priv->shrd->mutex);
}

void iwlagn_prepare_restart(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;
	bool bt_full_concurrent;
	u8 bt_ci_compliance;
	u8 bt_load;
	u8 bt_status;
	bool bt_is_sco;

	lockdep_assert_held(&priv->shrd->mutex);

	for_each_context(priv, ctx)
		ctx->vif = NULL;
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

	__iwl_down(priv);

	priv->bt_full_concurrent = bt_full_concurrent;
	priv->bt_ci_compliance = bt_ci_compliance;
	priv->bt_traffic_load = bt_load;
	priv->bt_status = bt_status;
	priv->bt_is_sco = bt_is_sco;
}

static void iwl_bg_restart(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, restart);

	if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status))
		return;

	if (test_and_clear_bit(STATUS_FW_ERROR, &priv->shrd->status)) {
		mutex_lock(&priv->shrd->mutex);
		iwlagn_prepare_restart(priv);
		mutex_unlock(&priv->shrd->mutex);
		iwl_cancel_deferred_work(priv);
		ieee80211_restart_hw(priv->hw);
	} else {
		WARN_ON(1);
	}
}




void iwlagn_disable_roc(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_PAN];

	lockdep_assert_held(&priv->shrd->mutex);

	if (!priv->hw_roc_setup)
		return;

	ctx->staging.dev_type = RXON_DEV_TYPE_P2P;
	ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;

	priv->hw_roc_channel = NULL;

	memset(ctx->staging.node_addr, 0, ETH_ALEN);

	iwlagn_commit_rxon(priv, ctx);

	ctx->is_active = false;
	priv->hw_roc_setup = false;
}

static void iwlagn_disable_roc_work(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv,
					     hw_roc_disable_work.work);

	mutex_lock(&priv->shrd->mutex);
	iwlagn_disable_roc(priv);
	mutex_unlock(&priv->shrd->mutex);
}

/*****************************************************************************
 *
 * driver setup and teardown
 *
 *****************************************************************************/

static void iwl_setup_deferred_work(struct iwl_priv *priv)
{
	priv->shrd->workqueue = create_singlethread_workqueue(DRV_NAME);

	init_waitqueue_head(&priv->shrd->wait_command_queue);

	INIT_WORK(&priv->restart, iwl_bg_restart);
	INIT_WORK(&priv->beacon_update, iwl_bg_beacon_update);
	INIT_WORK(&priv->run_time_calib_work, iwl_bg_run_time_calib_work);
	INIT_WORK(&priv->tx_flush, iwl_bg_tx_flush);
	INIT_WORK(&priv->bt_full_concurrency, iwl_bg_bt_full_concurrency);
	INIT_WORK(&priv->bt_runtime_config, iwl_bg_bt_runtime_config);
	INIT_DELAYED_WORK(&priv->hw_roc_disable_work,
			  iwlagn_disable_roc_work);

	iwl_setup_scan_deferred_work(priv);

	if (cfg(priv)->lib->bt_setup_deferred_work)
		cfg(priv)->lib->bt_setup_deferred_work(priv);

	init_timer(&priv->statistics_periodic);
	priv->statistics_periodic.data = (unsigned long)priv;
	priv->statistics_periodic.function = iwl_bg_statistics_periodic;

	init_timer(&priv->ucode_trace);
	priv->ucode_trace.data = (unsigned long)priv;
	priv->ucode_trace.function = iwl_bg_ucode_trace;

	init_timer(&priv->watchdog);
	priv->watchdog.data = (unsigned long)priv;
	priv->watchdog.function = iwl_bg_watchdog;
}

static void iwl_cancel_deferred_work(struct iwl_priv *priv)
{
	if (cfg(priv)->lib->cancel_deferred_work)
		cfg(priv)->lib->cancel_deferred_work(priv);

	cancel_work_sync(&priv->run_time_calib_work);
	cancel_work_sync(&priv->beacon_update);

	iwl_cancel_scan_deferred_work(priv);

	cancel_work_sync(&priv->bt_full_concurrency);
	cancel_work_sync(&priv->bt_runtime_config);
	cancel_delayed_work_sync(&priv->hw_roc_disable_work);

	del_timer_sync(&priv->statistics_periodic);
	del_timer_sync(&priv->ucode_trace);
}

static void iwl_init_hw_rates(struct iwl_priv *priv,
			      struct ieee80211_rate *rates)
{
	int i;

	for (i = 0; i < IWL_RATE_COUNT_LEGACY; i++) {
		rates[i].bitrate = iwl_rates[i].ieee * 5;
		rates[i].hw_value = i; /* Rate scaling will work on indexes */
		rates[i].hw_value_short = i;
		rates[i].flags = 0;
		if ((i >= IWL_FIRST_CCK_RATE) && (i <= IWL_LAST_CCK_RATE)) {
			/*
			 * If CCK != 1M then set short preamble rate flag.
			 */
			rates[i].flags |=
				(iwl_rates[i].plcp == IWL_RATE_1M_PLCP) ?
					0 : IEEE80211_RATE_SHORT_PREAMBLE;
		}
	}
}

static int iwl_init_drv(struct iwl_priv *priv)
{
	int ret;

	spin_lock_init(&priv->shrd->sta_lock);

	mutex_init(&priv->shrd->mutex);

	INIT_LIST_HEAD(&trans(priv)->calib_results);

	priv->ieee_channels = NULL;
	priv->ieee_rates = NULL;
	priv->band = IEEE80211_BAND_2GHZ;

	priv->iw_mode = NL80211_IFTYPE_STATION;
	priv->current_ht_config.smps = IEEE80211_SMPS_STATIC;
	priv->missed_beacon_threshold = IWL_MISSED_BEACON_THRESHOLD_DEF;
	priv->agg_tids_count = 0;

	/* initialize force reset */
	priv->force_reset[IWL_RF_RESET].reset_duration =
		IWL_DELAY_NEXT_FORCE_RF_RESET;
	priv->force_reset[IWL_FW_RESET].reset_duration =
		IWL_DELAY_NEXT_FORCE_FW_RELOAD;

	priv->rx_statistics_jiffies = jiffies;

	/* Choose which receivers/antennas to use */
	iwlagn_set_rxon_chain(priv, &priv->contexts[IWL_RXON_CTX_BSS]);

	iwl_init_scan_params(priv);

	/* init bt coex */
	if (cfg(priv)->bt_params &&
	    cfg(priv)->bt_params->advanced_bt_coexist) {
		priv->kill_ack_mask = IWLAGN_BT_KILL_ACK_MASK_DEFAULT;
		priv->kill_cts_mask = IWLAGN_BT_KILL_CTS_MASK_DEFAULT;
		priv->bt_valid = IWLAGN_BT_ALL_VALID_MSK;
		priv->bt_on_thresh = BT_ON_THRESHOLD_DEF;
		priv->bt_duration = BT_DURATION_LIMIT_DEF;
		priv->dynamic_frag_thresh = BT_FRAG_THRESHOLD_DEF;
	}

	ret = iwl_init_channel_map(priv);
	if (ret) {
		IWL_ERR(priv, "initializing regulatory failed: %d\n", ret);
		goto err;
	}

	ret = iwl_init_geos(priv);
	if (ret) {
		IWL_ERR(priv, "initializing geos failed: %d\n", ret);
		goto err_free_channel_map;
	}
	iwl_init_hw_rates(priv, priv->ieee_rates);

	return 0;

err_free_channel_map:
	iwl_free_channel_map(priv);
err:
	return ret;
}

static void iwl_uninit_drv(struct iwl_priv *priv)
{
	iwl_free_geos(priv);
	iwl_free_channel_map(priv);
	if (priv->tx_cmd_pool)
		kmem_cache_destroy(priv->tx_cmd_pool);
	kfree(priv->scan_cmd);
	kfree(priv->beacon_cmd);
	kfree(rcu_dereference_raw(priv->noa_data));
#ifdef CONFIG_IWLWIFI_DEBUGFS
	kfree(priv->wowlan_sram);
#endif
}



static u32 iwl_hw_detect(struct iwl_priv *priv)
{
	return iwl_read32(bus(priv), CSR_HW_REV);
}

/* Size of one Rx buffer in host DRAM */
#define IWL_RX_BUF_SIZE_4K (4 * 1024)
#define IWL_RX_BUF_SIZE_8K (8 * 1024)

static int iwl_set_hw_params(struct iwl_priv *priv)
{
	if (iwlagn_mod_params.amsdu_size_8K)
		hw_params(priv).rx_page_order =
			get_order(IWL_RX_BUF_SIZE_8K);
	else
		hw_params(priv).rx_page_order =
			get_order(IWL_RX_BUF_SIZE_4K);

	if (iwlagn_mod_params.disable_11n)
		cfg(priv)->sku &= ~EEPROM_SKU_CAP_11N_ENABLE;

	hw_params(priv).num_ampdu_queues =
		cfg(priv)->base_params->num_of_ampdu_queues;
	hw_params(priv).shadow_reg_enable =
		cfg(priv)->base_params->shadow_reg_enable;
	hw_params(priv).sku = cfg(priv)->sku;
	hw_params(priv).wd_timeout = cfg(priv)->base_params->wd_timeout;

	/* Device-specific setup */
	return cfg(priv)->lib->set_hw_params(priv);
}



static void iwl_debug_config(struct iwl_priv *priv)
{
	dev_printk(KERN_INFO, bus(priv)->dev, "CONFIG_IWLWIFI_DEBUG "
#ifdef CONFIG_IWLWIFI_DEBUG
		"enabled\n");
#else
		"disabled\n");
#endif
	dev_printk(KERN_INFO, bus(priv)->dev, "CONFIG_IWLWIFI_DEBUGFS "
#ifdef CONFIG_IWLWIFI_DEBUGFS
		"enabled\n");
#else
		"disabled\n");
#endif
	dev_printk(KERN_INFO, bus(priv)->dev, "CONFIG_IWLWIFI_DEVICE_TRACING "
#ifdef CONFIG_IWLWIFI_DEVICE_TRACING
		"enabled\n");
#else
		"disabled\n");
#endif

	dev_printk(KERN_INFO, bus(priv)->dev, "CONFIG_IWLWIFI_DEVICE_TESTMODE "
#ifdef CONFIG_IWLWIFI_DEVICE_TESTMODE
		"enabled\n");
#else
		"disabled\n");
#endif
	dev_printk(KERN_INFO, bus(priv)->dev, "CONFIG_IWLWIFI_P2P "
#ifdef CONFIG_IWLWIFI_P2P
		"enabled\n");
#else
		"disabled\n");
#endif
}

int iwl_probe(struct iwl_bus *bus, const struct iwl_trans_ops *trans_ops,
		struct iwl_cfg *cfg)
{
	int err = 0;
	struct iwl_priv *priv;
	struct ieee80211_hw *hw;
	u16 num_mac;
	u32 hw_rev;

	/************************
	 * 1. Allocating HW data
	 ************************/
	hw = iwl_alloc_all();
	if (!hw) {
		pr_err("%s: Cannot allocate network device\n", cfg->name);
		err = -ENOMEM;
		goto out;
	}

	priv = hw->priv;
	priv->shrd = &priv->_shrd;
	bus->shrd = priv->shrd;
	priv->shrd->bus = bus;
	priv->shrd->priv = priv;

	priv->shrd->trans = trans_ops->alloc(priv->shrd);
	if (priv->shrd->trans == NULL) {
		err = -ENOMEM;
		goto out_free_traffic_mem;
	}

	/* At this point both hw and priv are allocated. */

	SET_IEEE80211_DEV(hw, bus(priv)->dev);

	/* what debugging capabilities we have */
	iwl_debug_config(priv);

	IWL_DEBUG_INFO(priv, "*** LOAD DRIVER ***\n");
	cfg(priv) = cfg;

	/* is antenna coupling more than 35dB ? */
	priv->bt_ant_couple_ok =
		(iwlagn_mod_params.ant_coupling >
			IWL_BT_ANTENNA_COUPLING_THRESHOLD) ?
			true : false;

	/* enable/disable bt channel inhibition */
	priv->bt_ch_announce = iwlagn_mod_params.bt_ch_announce;
	IWL_DEBUG_INFO(priv, "BT channel inhibition is %s\n",
		       (priv->bt_ch_announce) ? "On" : "Off");

	if (iwl_alloc_traffic_mem(priv))
		IWL_ERR(priv, "Not enough memory to generate traffic log\n");

	/* these spin locks will be used in apm_ops.init and EEPROM access
	 * we should init now
	 */
	spin_lock_init(&bus(priv)->reg_lock);
	spin_lock_init(&priv->shrd->lock);

	/*
	 * stop and reset the on-board processor just in case it is in a
	 * strange state ... like being left stranded by a primary kernel
	 * and this is now the kdump kernel trying to start up
	 */
	iwl_write32(bus(priv), CSR_RESET, CSR_RESET_REG_FLAG_NEVO_RESET);

	/***********************
	 * 3. Read REV register
	 ***********************/
	hw_rev = iwl_hw_detect(priv);
	IWL_INFO(priv, "Detected %s, REV=0x%X\n",
		cfg(priv)->name, hw_rev);

	err = iwl_trans_request_irq(trans(priv));
	if (err)
		goto out_free_trans;

	if (iwl_trans_prepare_card_hw(trans(priv))) {
		err = -EIO;
		IWL_WARN(priv, "Failed, HW not ready\n");
		goto out_free_trans;
	}

	/*****************
	 * 4. Read EEPROM
	 *****************/
	/* Read the EEPROM */
	err = iwl_eeprom_init(priv, hw_rev);
	if (err) {
		IWL_ERR(priv, "Unable to init EEPROM\n");
		goto out_free_trans;
	}
	err = iwl_eeprom_check_version(priv);
	if (err)
		goto out_free_eeprom;

	err = iwl_eeprom_check_sku(priv);
	if (err)
		goto out_free_eeprom;

	/* extract MAC Address */
	iwl_eeprom_get_mac(priv->shrd, priv->addresses[0].addr);
	IWL_DEBUG_INFO(priv, "MAC address: %pM\n", priv->addresses[0].addr);
	priv->hw->wiphy->addresses = priv->addresses;
	priv->hw->wiphy->n_addresses = 1;
	num_mac = iwl_eeprom_query16(priv->shrd, EEPROM_NUM_MAC_ADDRESS);
	if (num_mac > 1) {
		memcpy(priv->addresses[1].addr, priv->addresses[0].addr,
		       ETH_ALEN);
		priv->addresses[1].addr[5]++;
		priv->hw->wiphy->n_addresses++;
	}

	/************************
	 * 5. Setup HW constants
	 ************************/
	if (iwl_set_hw_params(priv)) {
		err = -ENOENT;
		IWL_ERR(priv, "failed to set hw parameters\n");
		goto out_free_eeprom;
	}

	/*******************
	 * 6. Setup priv
	 *******************/

	err = iwl_init_drv(priv);
	if (err)
		goto out_free_eeprom;
	/* At this point both hw and priv are initialized. */

	/********************
	 * 7. Setup services
	 ********************/
	iwl_setup_deferred_work(priv);
	iwl_setup_rx_handlers(priv);
	iwl_testmode_init(priv);

	/*********************************************
	 * 8. Enable interrupts
	 *********************************************/

	iwl_enable_rfkill_int(priv);

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (iwl_read32(bus(priv),
			CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &priv->shrd->status);
	else
		set_bit(STATUS_RF_KILL_HW, &priv->shrd->status);

	wiphy_rfkill_set_hw_state(priv->hw->wiphy,
		test_bit(STATUS_RF_KILL_HW, &priv->shrd->status));

	iwl_power_initialize(priv);
	iwl_tt_initialize(priv);

	init_completion(&priv->firmware_loading_complete);

	err = iwl_request_firmware(priv, true);
	if (err)
		goto out_destroy_workqueue;

	return 0;

out_destroy_workqueue:
	destroy_workqueue(priv->shrd->workqueue);
	priv->shrd->workqueue = NULL;
	iwl_uninit_drv(priv);
out_free_eeprom:
	iwl_eeprom_free(priv->shrd);
out_free_trans:
	iwl_trans_free(trans(priv));
out_free_traffic_mem:
	iwl_free_traffic_mem(priv);
	ieee80211_free_hw(priv->hw);
out:
	return err;
}

void __devexit iwl_remove(struct iwl_priv * priv)
{
	wait_for_completion(&priv->firmware_loading_complete);

	IWL_DEBUG_INFO(priv, "*** UNLOAD DRIVER ***\n");

	iwl_dbgfs_unregister(priv);

	/* ieee80211_unregister_hw call wil cause iwlagn_mac_stop to
	 * to be called and iwl_down since we are removing the device
	 * we need to set STATUS_EXIT_PENDING bit.
	 */
	set_bit(STATUS_EXIT_PENDING, &priv->shrd->status);

	iwl_testmode_cleanup(priv);
	iwlagn_mac_unregister(priv);

	iwl_tt_exit(priv);

	/*This will stop the queues, move the device to low power state */
	iwl_trans_stop_device(trans(priv));

	iwl_dealloc_ucode(trans(priv));

	iwl_eeprom_free(priv->shrd);

	/*netif_stop_queue(dev); */
	flush_workqueue(priv->shrd->workqueue);

	/* ieee80211_unregister_hw calls iwlagn_mac_stop, which flushes
	 * priv->shrd->workqueue... so we can't take down the workqueue
	 * until now... */
	destroy_workqueue(priv->shrd->workqueue);
	priv->shrd->workqueue = NULL;
	iwl_free_traffic_mem(priv);

	iwl_trans_free(trans(priv));

	iwl_uninit_drv(priv);

	dev_kfree_skb(priv->beacon_skb);

	ieee80211_free_hw(priv->hw);
}


/*****************************************************************************
 *
 * driver and module entry point
 *
 *****************************************************************************/
static int __init iwl_init(void)
{

	int ret;
	pr_info(DRV_DESCRIPTION ", " DRV_VERSION "\n");
	pr_info(DRV_COPYRIGHT "\n");

	ret = iwlagn_rate_control_register();
	if (ret) {
		pr_err("Unable to register rate control algorithm: %d\n", ret);
		return ret;
	}

	ret = iwl_pci_register_driver();

	if (ret)
		goto error_register;
	return ret;

error_register:
	iwlagn_rate_control_unregister();
	return ret;
}

static void __exit iwl_exit(void)
{
	iwl_pci_unregister_driver();
	iwlagn_rate_control_unregister();
}

module_exit(iwl_exit);
module_init(iwl_init);

#ifdef CONFIG_IWLWIFI_DEBUG
module_param_named(debug, iwlagn_mod_params.debug_level, uint,
		   S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "debug output mask");
#endif

module_param_named(swcrypto, iwlagn_mod_params.sw_crypto, int, S_IRUGO);
MODULE_PARM_DESC(swcrypto, "using crypto in software (default 0 [hardware])");
module_param_named(queues_num, iwlagn_mod_params.num_of_queues, int, S_IRUGO);
MODULE_PARM_DESC(queues_num, "number of hw queues.");
module_param_named(11n_disable, iwlagn_mod_params.disable_11n, int, S_IRUGO);
MODULE_PARM_DESC(11n_disable, "disable 11n functionality");
module_param_named(amsdu_size_8K, iwlagn_mod_params.amsdu_size_8K,
		   int, S_IRUGO);
MODULE_PARM_DESC(amsdu_size_8K, "enable 8K amsdu size");
module_param_named(fw_restart, iwlagn_mod_params.restart_fw, int, S_IRUGO);
MODULE_PARM_DESC(fw_restart, "restart firmware in case of error");

module_param_named(ucode_alternative,
		   iwlagn_mod_params.wanted_ucode_alternative,
		   int, S_IRUGO);
MODULE_PARM_DESC(ucode_alternative,
		 "specify ucode alternative to use from ucode file");

module_param_named(antenna_coupling, iwlagn_mod_params.ant_coupling,
		   int, S_IRUGO);
MODULE_PARM_DESC(antenna_coupling,
		 "specify antenna coupling in dB (defualt: 0 dB)");

module_param_named(bt_ch_inhibition, iwlagn_mod_params.bt_ch_announce,
		   bool, S_IRUGO);
MODULE_PARM_DESC(bt_ch_inhibition,
		 "Enable BT channel inhibition (default: enable)");

module_param_named(plcp_check, iwlagn_mod_params.plcp_check, bool, S_IRUGO);
MODULE_PARM_DESC(plcp_check, "Check plcp health (default: 1 [enabled])");

module_param_named(ack_check, iwlagn_mod_params.ack_check, bool, S_IRUGO);
MODULE_PARM_DESC(ack_check, "Check ack health (default: 0 [disabled])");

module_param_named(wd_disable, iwlagn_mod_params.wd_disable, int, S_IRUGO);
MODULE_PARM_DESC(wd_disable,
		"Disable stuck queue watchdog timer 0=system default, "
		"1=disable, 2=enable (default: 0)");

/*
 * set bt_coex_active to true, uCode will do kill/defer
 * every time the priority line is asserted (BT is sending signals on the
 * priority line in the PCIx).
 * set bt_coex_active to false, uCode will ignore the BT activity and
 * perform the normal operation
 *
 * User might experience transmit issue on some platform due to WiFi/BT
 * co-exist problem. The possible behaviors are:
 *   Able to scan and finding all the available AP
 *   Not able to associate with any AP
 * On those platforms, WiFi communication can be restored by set
 * "bt_coex_active" module parameter to "false"
 *
 * default: bt_coex_active = true (BT_COEX_ENABLE)
 */
module_param_named(bt_coex_active, iwlagn_mod_params.bt_coex_active,
		bool, S_IRUGO);
MODULE_PARM_DESC(bt_coex_active, "enable wifi/bt co-exist (default: enable)");

module_param_named(led_mode, iwlagn_mod_params.led_mode, int, S_IRUGO);
MODULE_PARM_DESC(led_mode, "0=system default, "
		"1=On(RF On)/Off(RF Off), 2=blinking (default: 0)");

module_param_named(power_save, iwlagn_mod_params.power_save,
		bool, S_IRUGO);
MODULE_PARM_DESC(power_save,
		 "enable WiFi power management (default: disable)");

module_param_named(power_level, iwlagn_mod_params.power_level,
		int, S_IRUGO);
MODULE_PARM_DESC(power_level,
		 "default power save level (range from 1 - 5, default: 1)");

module_param_named(auto_agg, iwlagn_mod_params.auto_agg,
		bool, S_IRUGO);
MODULE_PARM_DESC(auto_agg,
		 "enable agg w/o check traffic load (default: enable)");

/*
 * For now, keep using power level 1 instead of automatically
 * adjusting ...
 */
module_param_named(no_sleep_autoadjust, iwlagn_mod_params.no_sleep_autoadjust,
		bool, S_IRUGO);
MODULE_PARM_DESC(no_sleep_autoadjust,
		 "don't automatically adjust sleep level "
		 "according to maximum network latency (default: true)");
