/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include <net/mac80211.h>

#include <asm/div64.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-sta.h"
#include "iwl-calib.h"


/******************************************************************************
 *
 * module boiler plate
 *
 ******************************************************************************/

/*
 * module name, copyright, version, etc.
 * NOTE: DRV_NAME is defined in iwlwifi.h for use by iwl-debug.h and printk
 */

#define DRV_DESCRIPTION	"Intel(R) Wireless WiFi Link AGN driver for Linux"

#ifdef CONFIG_IWLWIFI_DEBUG
#define VD "d"
#else
#define VD
#endif

#ifdef CONFIG_IWLAGN_SPECTRUM_MEASUREMENT
#define VS "s"
#else
#define VS
#endif

#define DRV_VERSION     IWLWIFI_VERSION VD VS


MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");
MODULE_ALIAS("iwl4965");

/*************** STATION TABLE MANAGEMENT ****
 * mac80211 should be examined to determine if sta_info is duplicating
 * the functionality provided here
 */

/**************************************************************/



static void iwl4965_set_rxon_hwcrypto(struct iwl_priv *priv, int hw_decrypt)
{
	struct iwl_rxon_cmd *rxon = &priv->staging_rxon;

	if (hw_decrypt)
		rxon->filter_flags &= ~RXON_FILTER_DIS_DECRYPT_MSK;
	else
		rxon->filter_flags |= RXON_FILTER_DIS_DECRYPT_MSK;

}

/**
 * iwl4965_check_rxon_cmd - validate RXON structure is valid
 *
 * NOTE:  This is really only useful during development and can eventually
 * be #ifdef'd out once the driver is stable and folks aren't actively
 * making changes
 */
static int iwl4965_check_rxon_cmd(struct iwl_rxon_cmd *rxon)
{
	int error = 0;
	int counter = 1;

	if (rxon->flags & RXON_FLG_BAND_24G_MSK) {
		error |= le32_to_cpu(rxon->flags &
				(RXON_FLG_TGJ_NARROW_BAND_MSK |
				 RXON_FLG_RADAR_DETECT_MSK));
		if (error)
			IWL_WARNING("check 24G fields %d | %d\n",
				    counter++, error);
	} else {
		error |= (rxon->flags & RXON_FLG_SHORT_SLOT_MSK) ?
				0 : le32_to_cpu(RXON_FLG_SHORT_SLOT_MSK);
		if (error)
			IWL_WARNING("check 52 fields %d | %d\n",
				    counter++, error);
		error |= le32_to_cpu(rxon->flags & RXON_FLG_CCK_MSK);
		if (error)
			IWL_WARNING("check 52 CCK %d | %d\n",
				    counter++, error);
	}
	error |= (rxon->node_addr[0] | rxon->bssid_addr[0]) & 0x1;
	if (error)
		IWL_WARNING("check mac addr %d | %d\n", counter++, error);

	/* make sure basic rates 6Mbps and 1Mbps are supported */
	error |= (((rxon->ofdm_basic_rates & IWL_RATE_6M_MASK) == 0) &&
		  ((rxon->cck_basic_rates & IWL_RATE_1M_MASK) == 0));
	if (error)
		IWL_WARNING("check basic rate %d | %d\n", counter++, error);

	error |= (le16_to_cpu(rxon->assoc_id) > 2007);
	if (error)
		IWL_WARNING("check assoc id %d | %d\n", counter++, error);

	error |= ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK))
			== (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK));
	if (error)
		IWL_WARNING("check CCK and short slot %d | %d\n",
			    counter++, error);

	error |= ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK))
			== (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK));
	if (error)
		IWL_WARNING("check CCK & auto detect %d | %d\n",
			    counter++, error);

	error |= ((rxon->flags & (RXON_FLG_AUTO_DETECT_MSK |
			RXON_FLG_TGG_PROTECT_MSK)) == RXON_FLG_TGG_PROTECT_MSK);
	if (error)
		IWL_WARNING("check TGG and auto detect %d | %d\n",
			    counter++, error);

	if (error)
		IWL_WARNING("Tuning to channel %d\n",
			    le16_to_cpu(rxon->channel));

	if (error) {
		IWL_ERROR("Not a valid iwl4965_rxon_assoc_cmd field values\n");
		return -1;
	}
	return 0;
}

/**
 * iwl4965_full_rxon_required - check if full RXON (vs RXON_ASSOC) cmd is needed
 * @priv: staging_rxon is compared to active_rxon
 *
 * If the RXON structure is changing enough to require a new tune,
 * or is clearing the RXON_FILTER_ASSOC_MSK, then return 1 to indicate that
 * a new tune (full RXON command, rather than RXON_ASSOC cmd) is required.
 */
static int iwl4965_full_rxon_required(struct iwl_priv *priv)
{

	/* These items are only settable from the full RXON command */
	if (!(iwl_is_associated(priv)) ||
	    compare_ether_addr(priv->staging_rxon.bssid_addr,
			       priv->active_rxon.bssid_addr) ||
	    compare_ether_addr(priv->staging_rxon.node_addr,
			       priv->active_rxon.node_addr) ||
	    compare_ether_addr(priv->staging_rxon.wlap_bssid_addr,
			       priv->active_rxon.wlap_bssid_addr) ||
	    (priv->staging_rxon.dev_type != priv->active_rxon.dev_type) ||
	    (priv->staging_rxon.channel != priv->active_rxon.channel) ||
	    (priv->staging_rxon.air_propagation !=
	     priv->active_rxon.air_propagation) ||
	    (priv->staging_rxon.ofdm_ht_single_stream_basic_rates !=
	     priv->active_rxon.ofdm_ht_single_stream_basic_rates) ||
	    (priv->staging_rxon.ofdm_ht_dual_stream_basic_rates !=
	     priv->active_rxon.ofdm_ht_dual_stream_basic_rates) ||
	    (priv->staging_rxon.rx_chain != priv->active_rxon.rx_chain) ||
	    (priv->staging_rxon.assoc_id != priv->active_rxon.assoc_id))
		return 1;

	/* flags, filter_flags, ofdm_basic_rates, and cck_basic_rates can
	 * be updated with the RXON_ASSOC command -- however only some
	 * flag transitions are allowed using RXON_ASSOC */

	/* Check if we are not switching bands */
	if ((priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK) !=
	    (priv->active_rxon.flags & RXON_FLG_BAND_24G_MSK))
		return 1;

	/* Check if we are switching association toggle */
	if ((priv->staging_rxon.filter_flags & RXON_FILTER_ASSOC_MSK) !=
		(priv->active_rxon.filter_flags & RXON_FILTER_ASSOC_MSK))
		return 1;

	return 0;
}

/**
 * iwl4965_commit_rxon - commit staging_rxon to hardware
 *
 * The RXON command in staging_rxon is committed to the hardware and
 * the active_rxon structure is updated with the new data.  This
 * function correctly transitions out of the RXON_ASSOC_MSK state if
 * a HW tune is required based on the RXON structure changes.
 */
static int iwl4965_commit_rxon(struct iwl_priv *priv)
{
	/* cast away the const for active_rxon in this function */
	struct iwl_rxon_cmd *active_rxon = (void *)&priv->active_rxon;
	DECLARE_MAC_BUF(mac);
	int ret;
	bool new_assoc =
		!!(priv->staging_rxon.filter_flags & RXON_FILTER_ASSOC_MSK);

	if (!iwl_is_alive(priv))
		return -EBUSY;

	/* always get timestamp with Rx frame */
	priv->staging_rxon.flags |= RXON_FLG_TSF2HOST_MSK;
	/* allow CTS-to-self if possible. this is relevant only for
	 * 5000, but will not damage 4965 */
	priv->staging_rxon.flags |= RXON_FLG_SELF_CTS_EN;

	ret = iwl4965_check_rxon_cmd(&priv->staging_rxon);
	if (ret) {
		IWL_ERROR("Invalid RXON configuration.  Not committing.\n");
		return -EINVAL;
	}

	/* If we don't need to send a full RXON, we can use
	 * iwl4965_rxon_assoc_cmd which is used to reconfigure filter
	 * and other flags for the current radio configuration. */
	if (!iwl4965_full_rxon_required(priv)) {
		ret = iwl_send_rxon_assoc(priv);
		if (ret) {
			IWL_ERROR("Error setting RXON_ASSOC (%d)\n", ret);
			return ret;
		}

		memcpy(active_rxon, &priv->staging_rxon, sizeof(*active_rxon));
		return 0;
	}

	/* station table will be cleared */
	priv->assoc_station_added = 0;

	/* If we are currently associated and the new config requires
	 * an RXON_ASSOC and the new config wants the associated mask enabled,
	 * we must clear the associated from the active configuration
	 * before we apply the new config */
	if (iwl_is_associated(priv) && new_assoc) {
		IWL_DEBUG_INFO("Toggling associated bit on current RXON\n");
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;

		ret = iwl_send_cmd_pdu(priv, REPLY_RXON,
				      sizeof(struct iwl_rxon_cmd),
				      &priv->active_rxon);

		/* If the mask clearing failed then we set
		 * active_rxon back to what it was previously */
		if (ret) {
			active_rxon->filter_flags |= RXON_FILTER_ASSOC_MSK;
			IWL_ERROR("Error clearing ASSOC_MSK (%d)\n", ret);
			return ret;
		}
	}

	IWL_DEBUG_INFO("Sending RXON\n"
		       "* with%s RXON_FILTER_ASSOC_MSK\n"
		       "* channel = %d\n"
		       "* bssid = %s\n",
		       (new_assoc ? "" : "out"),
		       le16_to_cpu(priv->staging_rxon.channel),
		       print_mac(mac, priv->staging_rxon.bssid_addr));

	iwl4965_set_rxon_hwcrypto(priv, !priv->hw_params.sw_crypto);

	/* Apply the new configuration
	 * RXON unassoc clears the station table in uCode, send it before
	 * we add the bcast station. If assoc bit is set, we will send RXON
	 * after having added the bcast and bssid station.
	 */
	if (!new_assoc) {
		ret = iwl_send_cmd_pdu(priv, REPLY_RXON,
			      sizeof(struct iwl_rxon_cmd), &priv->staging_rxon);
		if (ret) {
			IWL_ERROR("Error setting new RXON (%d)\n", ret);
			return ret;
		}
		memcpy(active_rxon, &priv->staging_rxon, sizeof(*active_rxon));
	}

	iwl_clear_stations_table(priv);

	if (!priv->error_recovering)
		priv->start_calib = 0;

	/* Add the broadcast address so we can send broadcast frames */
	if (iwl_rxon_add_station(priv, iwl_bcast_addr, 0) ==
						IWL_INVALID_STATION) {
		IWL_ERROR("Error adding BROADCAST address for transmit.\n");
		return -EIO;
	}

	/* If we have set the ASSOC_MSK and we are in BSS mode then
	 * add the IWL_AP_ID to the station rate table */
	if (new_assoc) {
		if (priv->iw_mode == IEEE80211_IF_TYPE_STA) {
			ret = iwl_rxon_add_station(priv,
					   priv->active_rxon.bssid_addr, 1);
			if (ret == IWL_INVALID_STATION) {
				IWL_ERROR("Error adding AP address for TX.\n");
				return -EIO;
			}
			priv->assoc_station_added = 1;
			if (priv->default_wep_key &&
			    iwl_send_static_wepkey_cmd(priv, 0))
				IWL_ERROR("Could not send WEP static key.\n");
		}

		/* Apply the new configuration
		 * RXON assoc doesn't clear the station table in uCode,
		 */
		ret = iwl_send_cmd_pdu(priv, REPLY_RXON,
			      sizeof(struct iwl_rxon_cmd), &priv->staging_rxon);
		if (ret) {
			IWL_ERROR("Error setting new RXON (%d)\n", ret);
			return ret;
		}
		memcpy(active_rxon, &priv->staging_rxon, sizeof(*active_rxon));
	}

	iwl_init_sensitivity(priv);

	/* If we issue a new RXON command which required a tune then we must
	 * send a new TXPOWER command or we won't be able to Tx any frames */
	ret = iwl_set_tx_power(priv, priv->tx_power_user_lmt, true);
	if (ret) {
		IWL_ERROR("Error sending TX power (%d)\n", ret);
		return ret;
	}

	return 0;
}

void iwl4965_update_chain_flags(struct iwl_priv *priv)
{

	iwl_set_rxon_chain(priv);
	iwl4965_commit_rxon(priv);
}

static int iwl4965_send_bt_config(struct iwl_priv *priv)
{
	struct iwl4965_bt_cmd bt_cmd = {
		.flags = 3,
		.lead_time = 0xAA,
		.max_kill = 1,
		.kill_ack_mask = 0,
		.kill_cts_mask = 0,
	};

	return iwl_send_cmd_pdu(priv, REPLY_BT_CONFIG,
				sizeof(struct iwl4965_bt_cmd), &bt_cmd);
}

static void iwl_clear_free_frames(struct iwl_priv *priv)
{
	struct list_head *element;

	IWL_DEBUG_INFO("%d frames on pre-allocated heap on clear.\n",
		       priv->frames_count);

	while (!list_empty(&priv->free_frames)) {
		element = priv->free_frames.next;
		list_del(element);
		kfree(list_entry(element, struct iwl_frame, list));
		priv->frames_count--;
	}

	if (priv->frames_count) {
		IWL_WARNING("%d frames still in use.  Did we lose one?\n",
			    priv->frames_count);
		priv->frames_count = 0;
	}
}

static struct iwl_frame *iwl_get_free_frame(struct iwl_priv *priv)
{
	struct iwl_frame *frame;
	struct list_head *element;
	if (list_empty(&priv->free_frames)) {
		frame = kzalloc(sizeof(*frame), GFP_KERNEL);
		if (!frame) {
			IWL_ERROR("Could not allocate frame!\n");
			return NULL;
		}

		priv->frames_count++;
		return frame;
	}

	element = priv->free_frames.next;
	list_del(element);
	return list_entry(element, struct iwl_frame, list);
}

static void iwl_free_frame(struct iwl_priv *priv, struct iwl_frame *frame)
{
	memset(frame, 0, sizeof(*frame));
	list_add(&frame->list, &priv->free_frames);
}

static unsigned int iwl_fill_beacon_frame(struct iwl_priv *priv,
					  struct ieee80211_hdr *hdr,
					  const u8 *dest, int left)
{
	if (!iwl_is_associated(priv) || !priv->ibss_beacon ||
	    ((priv->iw_mode != IEEE80211_IF_TYPE_IBSS) &&
	     (priv->iw_mode != IEEE80211_IF_TYPE_AP)))
		return 0;

	if (priv->ibss_beacon->len > left)
		return 0;

	memcpy(hdr, priv->ibss_beacon->data, priv->ibss_beacon->len);

	return priv->ibss_beacon->len;
}

static u8 iwl4965_rate_get_lowest_plcp(struct iwl_priv *priv)
{
	int i;
	int rate_mask;

	/* Set rate mask*/
	if (priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK)
		rate_mask = priv->active_rate_basic & 0xF;
	else
		rate_mask = priv->active_rate_basic & 0xFF0;

	/* Find lowest valid rate */
	for (i = IWL_RATE_1M_INDEX; i != IWL_RATE_INVALID;
					i = iwl_rates[i].next_ieee) {
		if (rate_mask & (1 << i))
			return iwl_rates[i].plcp;
	}

	/* No valid rate was found. Assign the lowest one */
	if (priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK)
		return IWL_RATE_1M_PLCP;
	else
		return IWL_RATE_6M_PLCP;
}

unsigned int iwl4965_hw_get_beacon_cmd(struct iwl_priv *priv,
				       struct iwl_frame *frame, u8 rate)
{
	struct iwl_tx_beacon_cmd *tx_beacon_cmd;
	unsigned int frame_size;

	tx_beacon_cmd = &frame->u.beacon;
	memset(tx_beacon_cmd, 0, sizeof(*tx_beacon_cmd));

	tx_beacon_cmd->tx.sta_id = priv->hw_params.bcast_sta_id;
	tx_beacon_cmd->tx.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;

	frame_size = iwl_fill_beacon_frame(priv, tx_beacon_cmd->frame,
				iwl_bcast_addr,
				sizeof(frame->u) - sizeof(*tx_beacon_cmd));

	BUG_ON(frame_size > MAX_MPDU_SIZE);
	tx_beacon_cmd->tx.len = cpu_to_le16((u16)frame_size);

	if ((rate == IWL_RATE_1M_PLCP) || (rate >= IWL_RATE_2M_PLCP))
		tx_beacon_cmd->tx.rate_n_flags =
			iwl_hw_set_rate_n_flags(rate, RATE_MCS_CCK_MSK);
	else
		tx_beacon_cmd->tx.rate_n_flags =
			iwl_hw_set_rate_n_flags(rate, 0);

	tx_beacon_cmd->tx.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK |
				     TX_CMD_FLG_TSF_MSK |
				     TX_CMD_FLG_STA_RATE_MSK;

	return sizeof(*tx_beacon_cmd) + frame_size;
}
static int iwl4965_send_beacon_cmd(struct iwl_priv *priv)
{
	struct iwl_frame *frame;
	unsigned int frame_size;
	int rc;
	u8 rate;

	frame = iwl_get_free_frame(priv);

	if (!frame) {
		IWL_ERROR("Could not obtain free frame buffer for beacon "
			  "command.\n");
		return -ENOMEM;
	}

	rate = iwl4965_rate_get_lowest_plcp(priv);

	frame_size = iwl4965_hw_get_beacon_cmd(priv, frame, rate);

	rc = iwl_send_cmd_pdu(priv, REPLY_TX_BEACON, frame_size,
			      &frame->u.cmd[0]);

	iwl_free_frame(priv, frame);

	return rc;
}

/******************************************************************************
 *
 * Misc. internal state and helper functions
 *
 ******************************************************************************/

static void iwl4965_ht_conf(struct iwl_priv *priv,
			    struct ieee80211_bss_conf *bss_conf)
{
	struct ieee80211_ht_info *ht_conf = bss_conf->ht_conf;
	struct ieee80211_ht_bss_info *ht_bss_conf = bss_conf->ht_bss_conf;
	struct iwl_ht_info *iwl_conf = &priv->current_ht_config;

	IWL_DEBUG_MAC80211("enter: \n");

	iwl_conf->is_ht = bss_conf->assoc_ht;

	if (!iwl_conf->is_ht)
		return;

	priv->ps_mode = (u8)((ht_conf->cap & IEEE80211_HT_CAP_MIMO_PS) >> 2);

	if (ht_conf->cap & IEEE80211_HT_CAP_SGI_20)
		iwl_conf->sgf |= HT_SHORT_GI_20MHZ;
	if (ht_conf->cap & IEEE80211_HT_CAP_SGI_40)
		iwl_conf->sgf |= HT_SHORT_GI_40MHZ;

	iwl_conf->is_green_field = !!(ht_conf->cap & IEEE80211_HT_CAP_GRN_FLD);
	iwl_conf->max_amsdu_size =
		!!(ht_conf->cap & IEEE80211_HT_CAP_MAX_AMSDU);

	iwl_conf->supported_chan_width =
		!!(ht_conf->cap & IEEE80211_HT_CAP_SUP_WIDTH);
	iwl_conf->extension_chan_offset =
		ht_bss_conf->bss_cap & IEEE80211_HT_IE_CHA_SEC_OFFSET;
	/* If no above or below channel supplied disable FAT channel */
	if (iwl_conf->extension_chan_offset != IEEE80211_HT_IE_CHA_SEC_ABOVE &&
	    iwl_conf->extension_chan_offset != IEEE80211_HT_IE_CHA_SEC_BELOW) {
		iwl_conf->extension_chan_offset = IEEE80211_HT_IE_CHA_SEC_NONE;
		iwl_conf->supported_chan_width = 0;
	}

	iwl_conf->tx_mimo_ps_mode =
		(u8)((ht_conf->cap & IEEE80211_HT_CAP_MIMO_PS) >> 2);
	memcpy(iwl_conf->supp_mcs_set, ht_conf->supp_mcs_set, 16);

	iwl_conf->control_channel = ht_bss_conf->primary_channel;
	iwl_conf->tx_chan_width =
		!!(ht_bss_conf->bss_cap & IEEE80211_HT_IE_CHA_WIDTH);
	iwl_conf->ht_protection =
		ht_bss_conf->bss_op_mode & IEEE80211_HT_IE_HT_PROTECTION;
	iwl_conf->non_GF_STA_present =
		!!(ht_bss_conf->bss_op_mode & IEEE80211_HT_IE_NON_GF_STA_PRSNT);

	IWL_DEBUG_MAC80211("control channel %d\n", iwl_conf->control_channel);
	IWL_DEBUG_MAC80211("leave\n");
}

/*
 * QoS  support
*/
static void iwl_activate_qos(struct iwl_priv *priv, u8 force)
{
	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (!priv->qos_data.qos_enable)
		return;

	priv->qos_data.def_qos_parm.qos_flags = 0;

	if (priv->qos_data.qos_cap.q_AP.queue_request &&
	    !priv->qos_data.qos_cap.q_AP.txop_request)
		priv->qos_data.def_qos_parm.qos_flags |=
			QOS_PARAM_FLG_TXOP_TYPE_MSK;
	if (priv->qos_data.qos_active)
		priv->qos_data.def_qos_parm.qos_flags |=
			QOS_PARAM_FLG_UPDATE_EDCA_MSK;

	if (priv->current_ht_config.is_ht)
		priv->qos_data.def_qos_parm.qos_flags |= QOS_PARAM_FLG_TGN_MSK;

	if (force || iwl_is_associated(priv)) {
		IWL_DEBUG_QOS("send QoS cmd with Qos active=%d FLAGS=0x%X\n",
				priv->qos_data.qos_active,
				priv->qos_data.def_qos_parm.qos_flags);

		iwl_send_cmd_pdu_async(priv, REPLY_QOS_PARAM,
				       sizeof(struct iwl_qosparam_cmd),
				       &priv->qos_data.def_qos_parm, NULL);
	}
}

#define MAX_UCODE_BEACON_INTERVAL	4096

static __le16 iwl4965_adjust_beacon_interval(u16 beacon_val)
{
	u16 new_val = 0;
	u16 beacon_factor = 0;

	beacon_factor =
	    (beacon_val + MAX_UCODE_BEACON_INTERVAL)
		/ MAX_UCODE_BEACON_INTERVAL;
	new_val = beacon_val / beacon_factor;

	return cpu_to_le16(new_val);
}

static void iwl4965_setup_rxon_timing(struct iwl_priv *priv)
{
	u64 interval_tm_unit;
	u64 tsf, result;
	unsigned long flags;
	struct ieee80211_conf *conf = NULL;
	u16 beacon_int = 0;

	conf = ieee80211_get_hw_conf(priv->hw);

	spin_lock_irqsave(&priv->lock, flags);
	priv->rxon_timing.timestamp.dw[1] = cpu_to_le32(priv->timestamp >> 32);
	priv->rxon_timing.timestamp.dw[0] =
				cpu_to_le32(priv->timestamp & 0xFFFFFFFF);

	priv->rxon_timing.listen_interval = cpu_to_le16(conf->listen_interval);

	tsf = priv->timestamp;

	beacon_int = priv->beacon_int;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (priv->iw_mode == IEEE80211_IF_TYPE_STA) {
		if (beacon_int == 0) {
			priv->rxon_timing.beacon_interval = cpu_to_le16(100);
			priv->rxon_timing.beacon_init_val = cpu_to_le32(102400);
		} else {
			priv->rxon_timing.beacon_interval =
				cpu_to_le16(beacon_int);
			priv->rxon_timing.beacon_interval =
			    iwl4965_adjust_beacon_interval(
				le16_to_cpu(priv->rxon_timing.beacon_interval));
		}

		priv->rxon_timing.atim_window = 0;
	} else {
		priv->rxon_timing.beacon_interval =
			iwl4965_adjust_beacon_interval(conf->beacon_int);
		/* TODO: we need to get atim_window from upper stack
		 * for now we set to 0 */
		priv->rxon_timing.atim_window = 0;
	}

	interval_tm_unit =
		(le16_to_cpu(priv->rxon_timing.beacon_interval) * 1024);
	result = do_div(tsf, interval_tm_unit);
	priv->rxon_timing.beacon_init_val =
	    cpu_to_le32((u32) ((u64) interval_tm_unit - result));

	IWL_DEBUG_ASSOC
	    ("beacon interval %d beacon timer %d beacon tim %d\n",
		le16_to_cpu(priv->rxon_timing.beacon_interval),
		le32_to_cpu(priv->rxon_timing.beacon_init_val),
		le16_to_cpu(priv->rxon_timing.atim_window));
}

static void iwl_set_flags_for_band(struct iwl_priv *priv,
				   enum ieee80211_band band)
{
	if (band == IEEE80211_BAND_5GHZ) {
		priv->staging_rxon.flags &=
		    ~(RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK
		      | RXON_FLG_CCK_MSK);
		priv->staging_rxon.flags |= RXON_FLG_SHORT_SLOT_MSK;
	} else {
		/* Copied from iwl4965_post_associate() */
		if (priv->assoc_capability & WLAN_CAPABILITY_SHORT_SLOT_TIME)
			priv->staging_rxon.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS)
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		priv->staging_rxon.flags |= RXON_FLG_BAND_24G_MSK;
		priv->staging_rxon.flags |= RXON_FLG_AUTO_DETECT_MSK;
		priv->staging_rxon.flags &= ~RXON_FLG_CCK_MSK;
	}
}

/*
 * initialize rxon structure with default values from eeprom
 */
static void iwl4965_connection_init_rx_config(struct iwl_priv *priv)
{
	const struct iwl_channel_info *ch_info;

	memset(&priv->staging_rxon, 0, sizeof(priv->staging_rxon));

	switch (priv->iw_mode) {
	case IEEE80211_IF_TYPE_AP:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_AP;
		break;

	case IEEE80211_IF_TYPE_STA:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_ESS;
		priv->staging_rxon.filter_flags = RXON_FILTER_ACCEPT_GRP_MSK;
		break;

	case IEEE80211_IF_TYPE_IBSS:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_IBSS;
		priv->staging_rxon.flags = RXON_FLG_SHORT_PREAMBLE_MSK;
		priv->staging_rxon.filter_flags = RXON_FILTER_BCON_AWARE_MSK |
						  RXON_FILTER_ACCEPT_GRP_MSK;
		break;

	case IEEE80211_IF_TYPE_MNTR:
		priv->staging_rxon.dev_type = RXON_DEV_TYPE_SNIFFER;
		priv->staging_rxon.filter_flags = RXON_FILTER_PROMISC_MSK |
		    RXON_FILTER_CTL2HOST_MSK | RXON_FILTER_ACCEPT_GRP_MSK;
		break;
	default:
		IWL_ERROR("Unsupported interface type %d\n", priv->iw_mode);
		break;
	}

#if 0
	/* TODO:  Figure out when short_preamble would be set and cache from
	 * that */
	if (!hw_to_local(priv->hw)->short_preamble)
		priv->staging_rxon.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		priv->staging_rxon.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
#endif

	ch_info = iwl_get_channel_info(priv, priv->band,
				       le16_to_cpu(priv->active_rxon.channel));

	if (!ch_info)
		ch_info = &priv->channel_info[0];

	/*
	 * in some case A channels are all non IBSS
	 * in this case force B/G channel
	 */
	if ((priv->iw_mode == IEEE80211_IF_TYPE_IBSS) &&
	    !(is_channel_ibss(ch_info)))
		ch_info = &priv->channel_info[0];

	priv->staging_rxon.channel = cpu_to_le16(ch_info->channel);
	priv->band = ch_info->band;

	iwl_set_flags_for_band(priv, priv->band);

	priv->staging_rxon.ofdm_basic_rates =
	    (IWL_OFDM_RATES_MASK >> IWL_FIRST_OFDM_RATE) & 0xFF;
	priv->staging_rxon.cck_basic_rates =
	    (IWL_CCK_RATES_MASK >> IWL_FIRST_CCK_RATE) & 0xF;

	priv->staging_rxon.flags &= ~(RXON_FLG_CHANNEL_MODE_MIXED_MSK |
					RXON_FLG_CHANNEL_MODE_PURE_40_MSK);
	memcpy(priv->staging_rxon.node_addr, priv->mac_addr, ETH_ALEN);
	memcpy(priv->staging_rxon.wlap_bssid_addr, priv->mac_addr, ETH_ALEN);
	priv->staging_rxon.ofdm_ht_single_stream_basic_rates = 0xff;
	priv->staging_rxon.ofdm_ht_dual_stream_basic_rates = 0xff;
	iwl_set_rxon_chain(priv);
}

static int iwl4965_set_mode(struct iwl_priv *priv, int mode)
{
	priv->iw_mode = mode;

	iwl4965_connection_init_rx_config(priv);
	memcpy(priv->staging_rxon.node_addr, priv->mac_addr, ETH_ALEN);

	iwl_clear_stations_table(priv);

	/* dont commit rxon if rf-kill is on*/
	if (!iwl_is_ready_rf(priv))
		return -EAGAIN;

	cancel_delayed_work(&priv->scan_check);
	if (iwl_scan_cancel_timeout(priv, 100)) {
		IWL_WARNING("Aborted scan still in progress after 100ms\n");
		IWL_DEBUG_MAC80211("leaving - scan abort failed.\n");
		return -EAGAIN;
	}

	iwl4965_commit_rxon(priv);

	return 0;
}

static void iwl4965_set_rate(struct iwl_priv *priv)
{
	const struct ieee80211_supported_band *hw = NULL;
	struct ieee80211_rate *rate;
	int i;

	hw = iwl_get_hw_mode(priv, priv->band);
	if (!hw) {
		IWL_ERROR("Failed to set rate: unable to get hw mode\n");
		return;
	}

	priv->active_rate = 0;
	priv->active_rate_basic = 0;

	for (i = 0; i < hw->n_bitrates; i++) {
		rate = &(hw->bitrates[i]);
		if (rate->hw_value < IWL_RATE_COUNT)
			priv->active_rate |= (1 << rate->hw_value);
	}

	IWL_DEBUG_RATE("Set active_rate = %0x, active_rate_basic = %0x\n",
		       priv->active_rate, priv->active_rate_basic);

	/*
	 * If a basic rate is configured, then use it (adding IWL_RATE_1M_MASK)
	 * otherwise set it to the default of all CCK rates and 6, 12, 24 for
	 * OFDM
	 */
	if (priv->active_rate_basic & IWL_CCK_BASIC_RATES_MASK)
		priv->staging_rxon.cck_basic_rates =
		    ((priv->active_rate_basic &
		      IWL_CCK_RATES_MASK) >> IWL_FIRST_CCK_RATE) & 0xF;
	else
		priv->staging_rxon.cck_basic_rates =
		    (IWL_CCK_BASIC_RATES_MASK >> IWL_FIRST_CCK_RATE) & 0xF;

	if (priv->active_rate_basic & IWL_OFDM_BASIC_RATES_MASK)
		priv->staging_rxon.ofdm_basic_rates =
		    ((priv->active_rate_basic &
		      (IWL_OFDM_BASIC_RATES_MASK | IWL_RATE_6M_MASK)) >>
		      IWL_FIRST_OFDM_RATE) & 0xFF;
	else
		priv->staging_rxon.ofdm_basic_rates =
		   (IWL_OFDM_BASIC_RATES_MASK >> IWL_FIRST_OFDM_RATE) & 0xFF;
}

#ifdef CONFIG_IWLAGN_SPECTRUM_MEASUREMENT

#include "iwl-spectrum.h"

#define BEACON_TIME_MASK_LOW	0x00FFFFFF
#define BEACON_TIME_MASK_HIGH	0xFF000000
#define TIME_UNIT		1024

/*
 * extended beacon time format
 * time in usec will be changed into a 32-bit value in 8:24 format
 * the high 1 byte is the beacon counts
 * the lower 3 bytes is the time in usec within one beacon interval
 */

static u32 iwl4965_usecs_to_beacons(u32 usec, u32 beacon_interval)
{
	u32 quot;
	u32 rem;
	u32 interval = beacon_interval * 1024;

	if (!interval || !usec)
		return 0;

	quot = (usec / interval) & (BEACON_TIME_MASK_HIGH >> 24);
	rem = (usec % interval) & BEACON_TIME_MASK_LOW;

	return (quot << 24) + rem;
}

/* base is usually what we get from ucode with each received frame,
 * the same as HW timer counter counting down
 */

static __le32 iwl4965_add_beacon_time(u32 base, u32 addon, u32 beacon_interval)
{
	u32 base_low = base & BEACON_TIME_MASK_LOW;
	u32 addon_low = addon & BEACON_TIME_MASK_LOW;
	u32 interval = beacon_interval * TIME_UNIT;
	u32 res = (base & BEACON_TIME_MASK_HIGH) +
	    (addon & BEACON_TIME_MASK_HIGH);

	if (base_low > addon_low)
		res += base_low - addon_low;
	else if (base_low < addon_low) {
		res += interval + base_low - addon_low;
		res += (1 << 24);
	} else
		res += (1 << 24);

	return cpu_to_le32(res);
}

static int iwl4965_get_measurement(struct iwl_priv *priv,
			       struct ieee80211_measurement_params *params,
			       u8 type)
{
	struct iwl4965_spectrum_cmd spectrum;
	struct iwl_rx_packet *res;
	struct iwl_host_cmd cmd = {
		.id = REPLY_SPECTRUM_MEASUREMENT_CMD,
		.data = (void *)&spectrum,
		.meta.flags = CMD_WANT_SKB,
	};
	u32 add_time = le64_to_cpu(params->start_time);
	int rc;
	int spectrum_resp_status;
	int duration = le16_to_cpu(params->duration);

	if (iwl_is_associated(priv))
		add_time =
		    iwl4965_usecs_to_beacons(
			le64_to_cpu(params->start_time) - priv->last_tsf,
			le16_to_cpu(priv->rxon_timing.beacon_interval));

	memset(&spectrum, 0, sizeof(spectrum));

	spectrum.channel_count = cpu_to_le16(1);
	spectrum.flags =
	    RXON_FLG_TSF2HOST_MSK | RXON_FLG_ANT_A_MSK | RXON_FLG_DIS_DIV_MSK;
	spectrum.filter_flags = MEASUREMENT_FILTER_FLAG;
	cmd.len = sizeof(spectrum);
	spectrum.len = cpu_to_le16(cmd.len - sizeof(spectrum.len));

	if (iwl_is_associated(priv))
		spectrum.start_time =
		    iwl4965_add_beacon_time(priv->last_beacon_time,
				add_time,
				le16_to_cpu(priv->rxon_timing.beacon_interval));
	else
		spectrum.start_time = 0;

	spectrum.channels[0].duration = cpu_to_le32(duration * TIME_UNIT);
	spectrum.channels[0].channel = params->channel;
	spectrum.channels[0].type = type;
	if (priv->active_rxon.flags & RXON_FLG_BAND_24G_MSK)
		spectrum.flags |= RXON_FLG_BAND_24G_MSK |
		    RXON_FLG_AUTO_DETECT_MSK | RXON_FLG_TGG_PROTECT_MSK;

	rc = iwl_send_cmd_sync(priv, &cmd);
	if (rc)
		return rc;

	res = (struct iwl_rx_packet *)cmd.meta.u.skb->data;
	if (res->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERROR("Bad return from REPLY_RX_ON_ASSOC command\n");
		rc = -EIO;
	}

	spectrum_resp_status = le16_to_cpu(res->u.spectrum.status);
	switch (spectrum_resp_status) {
	case 0:		/* Command will be handled */
		if (res->u.spectrum.id != 0xff) {
			IWL_DEBUG_INFO
			    ("Replaced existing measurement: %d\n",
			     res->u.spectrum.id);
			priv->measurement_status &= ~MEASUREMENT_READY;
		}
		priv->measurement_status |= MEASUREMENT_ACTIVE;
		rc = 0;
		break;

	case 1:		/* Command will not be handled */
		rc = -EAGAIN;
		break;
	}

	dev_kfree_skb_any(cmd.meta.u.skb);

	return rc;
}
#endif

/******************************************************************************
 *
 * Generic RX handler implementations
 *
 ******************************************************************************/
static void iwl_rx_reply_alive(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl_alive_resp *palive;
	struct delayed_work *pwork;

	palive = &pkt->u.alive_frame;

	IWL_DEBUG_INFO("Alive ucode status 0x%08X revision "
		       "0x%01X 0x%01X\n",
		       palive->is_valid, palive->ver_type,
		       palive->ver_subtype);

	if (palive->ver_subtype == INITIALIZE_SUBTYPE) {
		IWL_DEBUG_INFO("Initialization Alive received.\n");
		memcpy(&priv->card_alive_init,
		       &pkt->u.alive_frame,
		       sizeof(struct iwl_init_alive_resp));
		pwork = &priv->init_alive_start;
	} else {
		IWL_DEBUG_INFO("Runtime Alive received.\n");
		memcpy(&priv->card_alive, &pkt->u.alive_frame,
		       sizeof(struct iwl_alive_resp));
		pwork = &priv->alive_start;
	}

	/* We delay the ALIVE response by 5ms to
	 * give the HW RF Kill time to activate... */
	if (palive->is_valid == UCODE_VALID_OK)
		queue_delayed_work(priv->workqueue, pwork,
				   msecs_to_jiffies(5));
	else
		IWL_WARNING("uCode did not respond OK.\n");
}

static void iwl4965_rx_reply_error(struct iwl_priv *priv,
				   struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;

	IWL_ERROR("Error Reply type 0x%08X cmd %s (0x%02X) "
		"seq 0x%04X ser 0x%08X\n",
		le32_to_cpu(pkt->u.err_resp.error_type),
		get_cmd_string(pkt->u.err_resp.cmd_id),
		pkt->u.err_resp.cmd_id,
		le16_to_cpu(pkt->u.err_resp.bad_cmd_seq_num),
		le32_to_cpu(pkt->u.err_resp.error_info));
}

#define TX_STATUS_ENTRY(x) case TX_STATUS_FAIL_ ## x: return #x

static void iwl4965_rx_csa(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl_rxon_cmd *rxon = (void *)&priv->active_rxon;
	struct iwl4965_csa_notification *csa = &(pkt->u.csa_notif);
	IWL_DEBUG_11H("CSA notif: channel %d, status %d\n",
		      le16_to_cpu(csa->channel), le32_to_cpu(csa->status));
	rxon->channel = csa->channel;
	priv->staging_rxon.channel = csa->channel;
}

static void iwl4965_rx_spectrum_measure_notif(struct iwl_priv *priv,
					  struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLAGN_SPECTRUM_MEASUREMENT
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl4965_spectrum_notification *report = &(pkt->u.spectrum_notif);

	if (!report->state) {
		IWL_DEBUG(IWL_DL_11H,
			"Spectrum Measure Notification: Start\n");
		return;
	}

	memcpy(&priv->measure_report, report, sizeof(*report));
	priv->measurement_status |= MEASUREMENT_READY;
#endif
}

static void iwl4965_rx_pm_sleep_notif(struct iwl_priv *priv,
				      struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl4965_sleep_notification *sleep = &(pkt->u.sleep_notif);
	IWL_DEBUG_RX("sleep mode: %d, src: %d\n",
		     sleep->pm_sleep_mode, sleep->pm_wakeup_src);
#endif
}

static void iwl4965_rx_pm_debug_statistics_notif(struct iwl_priv *priv,
					     struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	IWL_DEBUG_RADIO("Dumping %d bytes of unhandled "
			"notification for %s:\n",
			le32_to_cpu(pkt->len), get_cmd_string(pkt->hdr.cmd));
	iwl_print_hex_dump(priv, IWL_DL_RADIO, pkt->u.raw, le32_to_cpu(pkt->len));
}

static void iwl4965_bg_beacon_update(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, beacon_update);
	struct sk_buff *beacon;

	/* Pull updated AP beacon from mac80211. will fail if not in AP mode */
	beacon = ieee80211_beacon_get(priv->hw, priv->vif);

	if (!beacon) {
		IWL_ERROR("update beacon failed\n");
		return;
	}

	mutex_lock(&priv->mutex);
	/* new beacon skb is allocated every time; dispose previous.*/
	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = beacon;
	mutex_unlock(&priv->mutex);

	iwl4965_send_beacon_cmd(priv);
}

/**
 * iwl4965_bg_statistics_periodic - Timer callback to queue statistics
 *
 * This callback is provided in order to send a statistics request.
 *
 * This timer function is continually reset to execute within
 * REG_RECALIB_PERIOD seconds since the last STATISTICS_NOTIFICATION
 * was received.  We need to ensure we receive the statistics in order
 * to update the temperature used for calibrating the TXPOWER.
 */
static void iwl4965_bg_statistics_periodic(unsigned long data)
{
	struct iwl_priv *priv = (struct iwl_priv *)data;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	iwl_send_statistics_request(priv, CMD_ASYNC);
}

static void iwl4965_rx_beacon_notif(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl4965_beacon_notif *beacon = &(pkt->u.beacon_status);
	u8 rate = iwl_hw_get_rate(beacon->beacon_notify_hdr.rate_n_flags);

	IWL_DEBUG_RX("beacon status %x retries %d iss %d "
		"tsf %d %d rate %d\n",
		le32_to_cpu(beacon->beacon_notify_hdr.u.status) & TX_STATUS_MSK,
		beacon->beacon_notify_hdr.failure_frame,
		le32_to_cpu(beacon->ibss_mgr_status),
		le32_to_cpu(beacon->high_tsf),
		le32_to_cpu(beacon->low_tsf), rate);
#endif

	if ((priv->iw_mode == IEEE80211_IF_TYPE_AP) &&
	    (!test_bit(STATUS_EXIT_PENDING, &priv->status)))
		queue_work(priv->workqueue, &priv->beacon_update);
}

/* Handle notification from uCode that card's power state is changing
 * due to software, hardware, or critical temperature RFKILL */
static void iwl4965_rx_card_state_notif(struct iwl_priv *priv,
				    struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	u32 flags = le32_to_cpu(pkt->u.card_state_notif.flags);
	unsigned long status = priv->status;

	IWL_DEBUG_RF_KILL("Card state received: HW:%s SW:%s\n",
			  (flags & HW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & SW_CARD_DISABLED) ? "Kill" : "On");

	if (flags & (SW_CARD_DISABLED | HW_CARD_DISABLED |
		     RF_CARD_DISABLED)) {

		iwl_write32(priv, CSR_UCODE_DRV_GP1_SET,
			    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

		if (!iwl_grab_nic_access(priv)) {
			iwl_write_direct32(
				priv, HBUS_TARG_MBX_C,
				HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED);

			iwl_release_nic_access(priv);
		}

		if (!(flags & RXON_CARD_DISABLED)) {
			iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
				    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);
			if (!iwl_grab_nic_access(priv)) {
				iwl_write_direct32(
					priv, HBUS_TARG_MBX_C,
					HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED);

				iwl_release_nic_access(priv);
			}
		}

		if (flags & RF_CARD_DISABLED) {
			iwl_write32(priv, CSR_UCODE_DRV_GP1_SET,
				    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
			iwl_read32(priv, CSR_UCODE_DRV_GP1);
			if (!iwl_grab_nic_access(priv))
				iwl_release_nic_access(priv);
		}
	}

	if (flags & HW_CARD_DISABLED)
		set_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		clear_bit(STATUS_RF_KILL_HW, &priv->status);


	if (flags & SW_CARD_DISABLED)
		set_bit(STATUS_RF_KILL_SW, &priv->status);
	else
		clear_bit(STATUS_RF_KILL_SW, &priv->status);

	if (!(flags & RXON_CARD_DISABLED))
		iwl_scan_cancel(priv);

	if ((test_bit(STATUS_RF_KILL_HW, &status) !=
	     test_bit(STATUS_RF_KILL_HW, &priv->status)) ||
	    (test_bit(STATUS_RF_KILL_SW, &status) !=
	     test_bit(STATUS_RF_KILL_SW, &priv->status)))
		queue_work(priv->workqueue, &priv->rf_kill);
	else
		wake_up_interruptible(&priv->wait_command_queue);
}

int iwl4965_set_pwr_src(struct iwl_priv *priv, enum iwl_pwr_src src)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	ret = iwl_grab_nic_access(priv);
	if (ret)
		goto err;

	if (src == IWL_PWR_SRC_VAUX) {
		u32 val;
		ret = pci_read_config_dword(priv->pci_dev, PCI_POWER_SOURCE,
					    &val);

		if (val & PCI_CFG_PMC_PME_FROM_D3COLD_SUPPORT)
			iwl_set_bits_mask_prph(priv, APMG_PS_CTRL_REG,
					       APMG_PS_CTRL_VAL_PWR_SRC_VAUX,
					       ~APMG_PS_CTRL_MSK_PWR_SRC);
	} else {
		iwl_set_bits_mask_prph(priv, APMG_PS_CTRL_REG,
				       APMG_PS_CTRL_VAL_PWR_SRC_VMAIN,
				       ~APMG_PS_CTRL_MSK_PWR_SRC);
	}

	iwl_release_nic_access(priv);
err:
	spin_unlock_irqrestore(&priv->lock, flags);
	return ret;
}

/**
 * iwl4965_setup_rx_handlers - Initialize Rx handler callbacks
 *
 * Setup the RX handlers for each of the reply types sent from the uCode
 * to the host.
 *
 * This function chains into the hardware specific files for them to setup
 * any hardware specific handlers as well.
 */
static void iwl_setup_rx_handlers(struct iwl_priv *priv)
{
	priv->rx_handlers[REPLY_ALIVE] = iwl_rx_reply_alive;
	priv->rx_handlers[REPLY_ERROR] = iwl4965_rx_reply_error;
	priv->rx_handlers[CHANNEL_SWITCH_NOTIFICATION] = iwl4965_rx_csa;
	priv->rx_handlers[SPECTRUM_MEASURE_NOTIFICATION] =
	    iwl4965_rx_spectrum_measure_notif;
	priv->rx_handlers[PM_SLEEP_NOTIFICATION] = iwl4965_rx_pm_sleep_notif;
	priv->rx_handlers[PM_DEBUG_STATISTIC_NOTIFIC] =
	    iwl4965_rx_pm_debug_statistics_notif;
	priv->rx_handlers[BEACON_NOTIFICATION] = iwl4965_rx_beacon_notif;

	/*
	 * The same handler is used for both the REPLY to a discrete
	 * statistics request from the host as well as for the periodic
	 * statistics notifications (after received beacons) from the uCode.
	 */
	priv->rx_handlers[REPLY_STATISTICS_CMD] = iwl_rx_statistics;
	priv->rx_handlers[STATISTICS_NOTIFICATION] = iwl_rx_statistics;

	iwl_setup_rx_scan_handlers(priv);

	/* status change handler */
	priv->rx_handlers[CARD_STATE_NOTIFICATION] = iwl4965_rx_card_state_notif;

	priv->rx_handlers[MISSED_BEACONS_NOTIFICATION] =
	    iwl_rx_missed_beacon_notif;
	/* Rx handlers */
	priv->rx_handlers[REPLY_RX_PHY_CMD] = iwl_rx_reply_rx_phy;
	priv->rx_handlers[REPLY_RX_MPDU_CMD] = iwl_rx_reply_rx;
	/* block ack */
	priv->rx_handlers[REPLY_COMPRESSED_BA] = iwl_rx_reply_compressed_ba;
	/* Set up hardware specific Rx handlers */
	priv->cfg->ops->lib->rx_handler_setup(priv);
}

/*
 * this should be called while priv->lock is locked
*/
static void __iwl_rx_replenish(struct iwl_priv *priv)
{
	iwl_rx_allocate(priv);
	iwl_rx_queue_restock(priv);
}


/**
 * iwl_rx_handle - Main entry function for receiving responses from uCode
 *
 * Uses the priv->rx_handlers callback function array to invoke
 * the appropriate handlers, including command responses,
 * frame-received notifications, and other notifications.
 */
void iwl_rx_handle(struct iwl_priv *priv)
{
	struct iwl_rx_mem_buffer *rxb;
	struct iwl_rx_packet *pkt;
	struct iwl_rx_queue *rxq = &priv->rxq;
	u32 r, i;
	int reclaim;
	unsigned long flags;
	u8 fill_rx = 0;
	u32 count = 8;

	/* uCode's read index (stored in shared DRAM) indicates the last Rx
	 * buffer that the driver may process (last buffer filled by ucode). */
	r = priv->cfg->ops->lib->shared_mem_rx_idx(priv);
	i = rxq->read;

	/* Rx interrupt, but nothing sent from uCode */
	if (i == r)
		IWL_DEBUG(IWL_DL_RX, "r = %d, i = %d\n", r, i);

	if (iwl_rx_queue_space(rxq) > (RX_QUEUE_SIZE / 2))
		fill_rx = 1;

	while (i != r) {
		rxb = rxq->queue[i];

		/* If an RXB doesn't have a Rx queue slot associated with it,
		 * then a bug has been introduced in the queue refilling
		 * routines -- catch it here */
		BUG_ON(rxb == NULL);

		rxq->queue[i] = NULL;

		pci_dma_sync_single_for_cpu(priv->pci_dev, rxb->dma_addr,
					    priv->hw_params.rx_buf_size,
					    PCI_DMA_FROMDEVICE);
		pkt = (struct iwl_rx_packet *)rxb->skb->data;

		/* Reclaim a command buffer only if this packet is a response
		 *   to a (driver-originated) command.
		 * If the packet (e.g. Rx frame) originated from uCode,
		 *   there is no command buffer to reclaim.
		 * Ucode should set SEQ_RX_FRAME bit if ucode-originated,
		 *   but apparently a few don't get set; catch them here. */
		reclaim = !(pkt->hdr.sequence & SEQ_RX_FRAME) &&
			(pkt->hdr.cmd != REPLY_RX_PHY_CMD) &&
			(pkt->hdr.cmd != REPLY_RX) &&
			(pkt->hdr.cmd != REPLY_COMPRESSED_BA) &&
			(pkt->hdr.cmd != STATISTICS_NOTIFICATION) &&
			(pkt->hdr.cmd != REPLY_TX);

		/* Based on type of command response or notification,
		 *   handle those that need handling via function in
		 *   rx_handlers table.  See iwl4965_setup_rx_handlers() */
		if (priv->rx_handlers[pkt->hdr.cmd]) {
			IWL_DEBUG(IWL_DL_RX, "r = %d, i = %d, %s, 0x%02x\n", r,
				i, get_cmd_string(pkt->hdr.cmd), pkt->hdr.cmd);
			priv->rx_handlers[pkt->hdr.cmd] (priv, rxb);
		} else {
			/* No handling needed */
			IWL_DEBUG(IWL_DL_RX,
				"r %d i %d No handler needed for %s, 0x%02x\n",
				r, i, get_cmd_string(pkt->hdr.cmd),
				pkt->hdr.cmd);
		}

		if (reclaim) {
			/* Invoke any callbacks, transfer the skb to caller, and
			 * fire off the (possibly) blocking iwl_send_cmd()
			 * as we reclaim the driver command queue */
			if (rxb && rxb->skb)
				iwl_tx_cmd_complete(priv, rxb);
			else
				IWL_WARNING("Claim null rxb?\n");
		}

		/* For now we just don't re-use anything.  We can tweak this
		 * later to try and re-use notification packets and SKBs that
		 * fail to Rx correctly */
		if (rxb->skb != NULL) {
			priv->alloc_rxb_skb--;
			dev_kfree_skb_any(rxb->skb);
			rxb->skb = NULL;
		}

		pci_unmap_single(priv->pci_dev, rxb->dma_addr,
				 priv->hw_params.rx_buf_size,
				 PCI_DMA_FROMDEVICE);
		spin_lock_irqsave(&rxq->lock, flags);
		list_add_tail(&rxb->list, &priv->rxq.rx_used);
		spin_unlock_irqrestore(&rxq->lock, flags);
		i = (i + 1) & RX_QUEUE_MASK;
		/* If there are a lot of unused frames,
		 * restock the Rx queue so ucode wont assert. */
		if (fill_rx) {
			count++;
			if (count >= 8) {
				priv->rxq.read = i;
				__iwl_rx_replenish(priv);
				count = 0;
			}
		}
	}

	/* Backtrack one entry */
	priv->rxq.read = i;
	iwl_rx_queue_restock(priv);
}

#ifdef CONFIG_IWLWIFI_DEBUG
static void iwl4965_print_rx_config_cmd(struct iwl_priv *priv)
{
	struct iwl_rxon_cmd *rxon = &priv->staging_rxon;
	DECLARE_MAC_BUF(mac);

	IWL_DEBUG_RADIO("RX CONFIG:\n");
	iwl_print_hex_dump(priv, IWL_DL_RADIO, (u8 *) rxon, sizeof(*rxon));
	IWL_DEBUG_RADIO("u16 channel: 0x%x\n", le16_to_cpu(rxon->channel));
	IWL_DEBUG_RADIO("u32 flags: 0x%08X\n", le32_to_cpu(rxon->flags));
	IWL_DEBUG_RADIO("u32 filter_flags: 0x%08x\n",
			le32_to_cpu(rxon->filter_flags));
	IWL_DEBUG_RADIO("u8 dev_type: 0x%x\n", rxon->dev_type);
	IWL_DEBUG_RADIO("u8 ofdm_basic_rates: 0x%02x\n",
			rxon->ofdm_basic_rates);
	IWL_DEBUG_RADIO("u8 cck_basic_rates: 0x%02x\n", rxon->cck_basic_rates);
	IWL_DEBUG_RADIO("u8[6] node_addr: %s\n",
			print_mac(mac, rxon->node_addr));
	IWL_DEBUG_RADIO("u8[6] bssid_addr: %s\n",
			print_mac(mac, rxon->bssid_addr));
	IWL_DEBUG_RADIO("u16 assoc_id: 0x%x\n", le16_to_cpu(rxon->assoc_id));
}
#endif

static void iwl4965_enable_interrupts(struct iwl_priv *priv)
{
	IWL_DEBUG_ISR("Enabling interrupts\n");
	set_bit(STATUS_INT_ENABLED, &priv->status);
	iwl_write32(priv, CSR_INT_MASK, CSR_INI_SET_MASK);
}

/* call this function to flush any scheduled tasklet */
static inline void iwl_synchronize_irq(struct iwl_priv *priv)
{
	/* wait to make sure we flush pedding tasklet*/
	synchronize_irq(priv->pci_dev->irq);
	tasklet_kill(&priv->irq_tasklet);
}

static inline void iwl4965_disable_interrupts(struct iwl_priv *priv)
{
	clear_bit(STATUS_INT_ENABLED, &priv->status);

	/* disable interrupts from uCode/NIC to host */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);

	/* acknowledge/clear/reset any interrupts still pending
	 * from uCode or flow handler (Rx/Tx DMA) */
	iwl_write32(priv, CSR_INT, 0xffffffff);
	iwl_write32(priv, CSR_FH_INT_STATUS, 0xffffffff);
	IWL_DEBUG_ISR("Disabled interrupts\n");
}


/**
 * iwl4965_irq_handle_error - called for HW or SW error interrupt from card
 */
static void iwl4965_irq_handle_error(struct iwl_priv *priv)
{
	/* Set the FW error flag -- cleared on iwl4965_down */
	set_bit(STATUS_FW_ERROR, &priv->status);

	/* Cancel currently queued command. */
	clear_bit(STATUS_HCMD_ACTIVE, &priv->status);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (priv->debug_level & IWL_DL_FW_ERRORS) {
		iwl_dump_nic_error_log(priv);
		iwl_dump_nic_event_log(priv);
		iwl4965_print_rx_config_cmd(priv);
	}
#endif

	wake_up_interruptible(&priv->wait_command_queue);

	/* Keep the restart process from trying to send host
	 * commands by clearing the INIT status bit */
	clear_bit(STATUS_READY, &priv->status);

	if (!test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_DEBUG(IWL_DL_FW_ERRORS,
			  "Restarting adapter due to uCode error.\n");

		if (iwl_is_associated(priv)) {
			memcpy(&priv->recovery_rxon, &priv->active_rxon,
			       sizeof(priv->recovery_rxon));
			priv->error_recovering = 1;
		}
		if (priv->cfg->mod_params->restart_fw)
			queue_work(priv->workqueue, &priv->restart);
	}
}

static void iwl4965_error_recovery(struct iwl_priv *priv)
{
	unsigned long flags;

	memcpy(&priv->staging_rxon, &priv->recovery_rxon,
	       sizeof(priv->staging_rxon));
	priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwl4965_commit_rxon(priv);

	iwl_rxon_add_station(priv, priv->bssid, 1);

	spin_lock_irqsave(&priv->lock, flags);
	priv->assoc_id = le16_to_cpu(priv->staging_rxon.assoc_id);
	priv->error_recovering = 0;
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void iwl4965_irq_tasklet(struct iwl_priv *priv)
{
	u32 inta, handled = 0;
	u32 inta_fh;
	unsigned long flags;
#ifdef CONFIG_IWLWIFI_DEBUG
	u32 inta_mask;
#endif

	spin_lock_irqsave(&priv->lock, flags);

	/* Ack/clear/reset pending uCode interrupts.
	 * Note:  Some bits in CSR_INT are "OR" of bits in CSR_FH_INT_STATUS,
	 *  and will clear only when CSR_FH_INT_STATUS gets cleared. */
	inta = iwl_read32(priv, CSR_INT);
	iwl_write32(priv, CSR_INT, inta);

	/* Ack/clear/reset pending flow-handler (DMA) interrupts.
	 * Any new interrupts that happen after this, either while we're
	 * in this tasklet, or later, will show up in next ISR/tasklet. */
	inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);
	iwl_write32(priv, CSR_FH_INT_STATUS, inta_fh);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (priv->debug_level & IWL_DL_ISR) {
		/* just for debug */
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		IWL_DEBUG_ISR("inta 0x%08x, enabled 0x%08x, fh 0x%08x\n",
			      inta, inta_mask, inta_fh);
	}
#endif

	/* Since CSR_INT and CSR_FH_INT_STATUS reads and clears are not
	 * atomic, make sure that inta covers all the interrupts that
	 * we've discovered, even if FH interrupt came in just after
	 * reading CSR_INT. */
	if (inta_fh & CSR49_FH_INT_RX_MASK)
		inta |= CSR_INT_BIT_FH_RX;
	if (inta_fh & CSR49_FH_INT_TX_MASK)
		inta |= CSR_INT_BIT_FH_TX;

	/* Now service all interrupt bits discovered above. */
	if (inta & CSR_INT_BIT_HW_ERR) {
		IWL_ERROR("Microcode HW error detected.  Restarting.\n");

		/* Tell the device to stop sending interrupts */
		iwl4965_disable_interrupts(priv);

		iwl4965_irq_handle_error(priv);

		handled |= CSR_INT_BIT_HW_ERR;

		spin_unlock_irqrestore(&priv->lock, flags);

		return;
	}

#ifdef CONFIG_IWLWIFI_DEBUG
	if (priv->debug_level & (IWL_DL_ISR)) {
		/* NIC fires this, but we don't use it, redundant with WAKEUP */
		if (inta & CSR_INT_BIT_SCD)
			IWL_DEBUG_ISR("Scheduler finished to transmit "
				      "the frame/frames.\n");

		/* Alive notification via Rx interrupt will do the real work */
		if (inta & CSR_INT_BIT_ALIVE)
			IWL_DEBUG_ISR("Alive interrupt\n");
	}
#endif
	/* Safely ignore these bits for debug checks below */
	inta &= ~(CSR_INT_BIT_SCD | CSR_INT_BIT_ALIVE);

	/* HW RF KILL switch toggled */
	if (inta & CSR_INT_BIT_RF_KILL) {
		int hw_rf_kill = 0;
		if (!(iwl_read32(priv, CSR_GP_CNTRL) &
				CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW))
			hw_rf_kill = 1;

		IWL_DEBUG(IWL_DL_RF_KILL, "RF_KILL bit toggled to %s.\n",
				hw_rf_kill ? "disable radio":"enable radio");

		/* driver only loads ucode once setting the interface up.
		 * the driver as well won't allow loading if RFKILL is set
		 * therefore no need to restart the driver from this handler
		 */
		if (!hw_rf_kill && !test_bit(STATUS_ALIVE, &priv->status))
			clear_bit(STATUS_RF_KILL_HW, &priv->status);

		handled |= CSR_INT_BIT_RF_KILL;
	}

	/* Chip got too hot and stopped itself */
	if (inta & CSR_INT_BIT_CT_KILL) {
		IWL_ERROR("Microcode CT kill error detected.\n");
		handled |= CSR_INT_BIT_CT_KILL;
	}

	/* Error detected by uCode */
	if (inta & CSR_INT_BIT_SW_ERR) {
		IWL_ERROR("Microcode SW error detected.  Restarting 0x%X.\n",
			  inta);
		iwl4965_irq_handle_error(priv);
		handled |= CSR_INT_BIT_SW_ERR;
	}

	/* uCode wakes up after power-down sleep */
	if (inta & CSR_INT_BIT_WAKEUP) {
		IWL_DEBUG_ISR("Wakeup interrupt\n");
		iwl_rx_queue_update_write_ptr(priv, &priv->rxq);
		iwl_txq_update_write_ptr(priv, &priv->txq[0]);
		iwl_txq_update_write_ptr(priv, &priv->txq[1]);
		iwl_txq_update_write_ptr(priv, &priv->txq[2]);
		iwl_txq_update_write_ptr(priv, &priv->txq[3]);
		iwl_txq_update_write_ptr(priv, &priv->txq[4]);
		iwl_txq_update_write_ptr(priv, &priv->txq[5]);

		handled |= CSR_INT_BIT_WAKEUP;
	}

	/* All uCode command responses, including Tx command responses,
	 * Rx "responses" (frame-received notification), and other
	 * notifications from uCode come through here*/
	if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX)) {
		iwl_rx_handle(priv);
		handled |= (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX);
	}

	if (inta & CSR_INT_BIT_FH_TX) {
		IWL_DEBUG_ISR("Tx interrupt\n");
		handled |= CSR_INT_BIT_FH_TX;
		/* FH finished to write, send event */
		priv->ucode_write_complete = 1;
		wake_up_interruptible(&priv->wait_command_queue);
	}

	if (inta & ~handled)
		IWL_ERROR("Unhandled INTA bits 0x%08x\n", inta & ~handled);

	if (inta & ~CSR_INI_SET_MASK) {
		IWL_WARNING("Disabled INTA bits 0x%08x were pending\n",
			 inta & ~CSR_INI_SET_MASK);
		IWL_WARNING("   with FH_INT = 0x%08x\n", inta_fh);
	}

	/* Re-enable all interrupts */
	/* only Re-enable if diabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl4965_enable_interrupts(priv);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (priv->debug_level & (IWL_DL_ISR)) {
		inta = iwl_read32(priv, CSR_INT);
		inta_mask = iwl_read32(priv, CSR_INT_MASK);
		inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);
		IWL_DEBUG_ISR("End inta 0x%08x, enabled 0x%08x, fh 0x%08x, "
			"flags 0x%08lx\n", inta, inta_mask, inta_fh, flags);
	}
#endif
	spin_unlock_irqrestore(&priv->lock, flags);
}

static irqreturn_t iwl4965_isr(int irq, void *data)
{
	struct iwl_priv *priv = data;
	u32 inta, inta_mask;
	u32 inta_fh;
	if (!priv)
		return IRQ_NONE;

	spin_lock(&priv->lock);

	/* Disable (but don't clear!) interrupts here to avoid
	 *    back-to-back ISRs and sporadic interrupts from our NIC.
	 * If we have something to service, the tasklet will re-enable ints.
	 * If we *don't* have something, we'll re-enable before leaving here. */
	inta_mask = iwl_read32(priv, CSR_INT_MASK);  /* just for debug */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);

	/* Discover which interrupts are active/pending */
	inta = iwl_read32(priv, CSR_INT);
	inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);

	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	if (!inta && !inta_fh) {
		IWL_DEBUG_ISR("Ignore interrupt, inta == 0, inta_fh == 0\n");
		goto none;
	}

	if ((inta == 0xFFFFFFFF) || ((inta & 0xFFFFFFF0) == 0xa5a5a5a0)) {
		/* Hardware disappeared. It might have already raised
		 * an interrupt */
		IWL_WARNING("HARDWARE GONE?? INTA == 0x%080x\n", inta);
		goto unplugged;
	}

	IWL_DEBUG_ISR("ISR inta 0x%08x, enabled 0x%08x, fh 0x%08x\n",
		      inta, inta_mask, inta_fh);

	inta &= ~CSR_INT_BIT_SCD;

	/* iwl4965_irq_tasklet() will service interrupts and re-enable them */
	if (likely(inta || inta_fh))
		tasklet_schedule(&priv->irq_tasklet);

 unplugged:
	spin_unlock(&priv->lock);
	return IRQ_HANDLED;

 none:
	/* re-enable interrupts here since we don't have anything to service. */
	/* only Re-enable if diabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl4965_enable_interrupts(priv);
	spin_unlock(&priv->lock);
	return IRQ_NONE;
}

/******************************************************************************
 *
 * uCode download functions
 *
 ******************************************************************************/

static void iwl4965_dealloc_ucode_pci(struct iwl_priv *priv)
{
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_code);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_data);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_data_backup);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_init);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_init_data);
	iwl_free_fw_desc(priv->pci_dev, &priv->ucode_boot);
}

static void iwl4965_nic_start(struct iwl_priv *priv)
{
	/* Remove all resets to allow NIC to operate */
	iwl_write32(priv, CSR_RESET, 0);
}


/**
 * iwl4965_read_ucode - Read uCode images from disk file.
 *
 * Copy into buffers for card to fetch via bus-mastering
 */
static int iwl4965_read_ucode(struct iwl_priv *priv)
{
	struct iwl_ucode *ucode;
	int ret;
	const struct firmware *ucode_raw;
	const char *name = priv->cfg->fw_name;
	u8 *src;
	size_t len;
	u32 ver, inst_size, data_size, init_size, init_data_size, boot_size;

	/* Ask kernel firmware_class module to get the boot firmware off disk.
	 * request_firmware() is synchronous, file is in memory on return. */
	ret = request_firmware(&ucode_raw, name, &priv->pci_dev->dev);
	if (ret < 0) {
		IWL_ERROR("%s firmware file req failed: Reason %d\n",
					name, ret);
		goto error;
	}

	IWL_DEBUG_INFO("Got firmware '%s' file (%zd bytes) from disk\n",
		       name, ucode_raw->size);

	/* Make sure that we got at least our header! */
	if (ucode_raw->size < sizeof(*ucode)) {
		IWL_ERROR("File size way too small!\n");
		ret = -EINVAL;
		goto err_release;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (void *)ucode_raw->data;

	ver = le32_to_cpu(ucode->ver);
	inst_size = le32_to_cpu(ucode->inst_size);
	data_size = le32_to_cpu(ucode->data_size);
	init_size = le32_to_cpu(ucode->init_size);
	init_data_size = le32_to_cpu(ucode->init_data_size);
	boot_size = le32_to_cpu(ucode->boot_size);

	IWL_DEBUG_INFO("f/w package hdr ucode version = 0x%x\n", ver);
	IWL_DEBUG_INFO("f/w package hdr runtime inst size = %u\n",
		       inst_size);
	IWL_DEBUG_INFO("f/w package hdr runtime data size = %u\n",
		       data_size);
	IWL_DEBUG_INFO("f/w package hdr init inst size = %u\n",
		       init_size);
	IWL_DEBUG_INFO("f/w package hdr init data size = %u\n",
		       init_data_size);
	IWL_DEBUG_INFO("f/w package hdr boot inst size = %u\n",
		       boot_size);

	/* Verify size of file vs. image size info in file's header */
	if (ucode_raw->size < sizeof(*ucode) +
		inst_size + data_size + init_size +
		init_data_size + boot_size) {

		IWL_DEBUG_INFO("uCode file size %d too small\n",
			       (int)ucode_raw->size);
		ret = -EINVAL;
		goto err_release;
	}

	/* Verify that uCode images will fit in card's SRAM */
	if (inst_size > priv->hw_params.max_inst_size) {
		IWL_DEBUG_INFO("uCode instr len %d too large to fit in\n",
			       inst_size);
		ret = -EINVAL;
		goto err_release;
	}

	if (data_size > priv->hw_params.max_data_size) {
		IWL_DEBUG_INFO("uCode data len %d too large to fit in\n",
				data_size);
		ret = -EINVAL;
		goto err_release;
	}
	if (init_size > priv->hw_params.max_inst_size) {
		IWL_DEBUG_INFO
		    ("uCode init instr len %d too large to fit in\n",
		      init_size);
		ret = -EINVAL;
		goto err_release;
	}
	if (init_data_size > priv->hw_params.max_data_size) {
		IWL_DEBUG_INFO
		    ("uCode init data len %d too large to fit in\n",
		      init_data_size);
		ret = -EINVAL;
		goto err_release;
	}
	if (boot_size > priv->hw_params.max_bsm_size) {
		IWL_DEBUG_INFO
		    ("uCode boot instr len %d too large to fit in\n",
		      boot_size);
		ret = -EINVAL;
		goto err_release;
	}

	/* Allocate ucode buffers for card's bus-master loading ... */

	/* Runtime instructions and 2 copies of data:
	 * 1) unmodified from disk
	 * 2) backup cache for save/restore during power-downs */
	priv->ucode_code.len = inst_size;
	iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_code);

	priv->ucode_data.len = data_size;
	iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_data);

	priv->ucode_data_backup.len = data_size;
	iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_data_backup);

	/* Initialization instructions and data */
	if (init_size && init_data_size) {
		priv->ucode_init.len = init_size;
		iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_init);

		priv->ucode_init_data.len = init_data_size;
		iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_init_data);

		if (!priv->ucode_init.v_addr || !priv->ucode_init_data.v_addr)
			goto err_pci_alloc;
	}

	/* Bootstrap (instructions only, no data) */
	if (boot_size) {
		priv->ucode_boot.len = boot_size;
		iwl_alloc_fw_desc(priv->pci_dev, &priv->ucode_boot);

		if (!priv->ucode_boot.v_addr)
			goto err_pci_alloc;
	}

	/* Copy images into buffers for card's bus-master reads ... */

	/* Runtime instructions (first block of data in file) */
	src = &ucode->data[0];
	len = priv->ucode_code.len;
	IWL_DEBUG_INFO("Copying (but not loading) uCode instr len %Zd\n", len);
	memcpy(priv->ucode_code.v_addr, src, len);
	IWL_DEBUG_INFO("uCode instr buf vaddr = 0x%p, paddr = 0x%08x\n",
		priv->ucode_code.v_addr, (u32)priv->ucode_code.p_addr);

	/* Runtime data (2nd block)
	 * NOTE:  Copy into backup buffer will be done in iwl4965_up()  */
	src = &ucode->data[inst_size];
	len = priv->ucode_data.len;
	IWL_DEBUG_INFO("Copying (but not loading) uCode data len %Zd\n", len);
	memcpy(priv->ucode_data.v_addr, src, len);
	memcpy(priv->ucode_data_backup.v_addr, src, len);

	/* Initialization instructions (3rd block) */
	if (init_size) {
		src = &ucode->data[inst_size + data_size];
		len = priv->ucode_init.len;
		IWL_DEBUG_INFO("Copying (but not loading) init instr len %Zd\n",
				len);
		memcpy(priv->ucode_init.v_addr, src, len);
	}

	/* Initialization data (4th block) */
	if (init_data_size) {
		src = &ucode->data[inst_size + data_size + init_size];
		len = priv->ucode_init_data.len;
		IWL_DEBUG_INFO("Copying (but not loading) init data len %Zd\n",
			       len);
		memcpy(priv->ucode_init_data.v_addr, src, len);
	}

	/* Bootstrap instructions (5th block) */
	src = &ucode->data[inst_size + data_size + init_size + init_data_size];
	len = priv->ucode_boot.len;
	IWL_DEBUG_INFO("Copying (but not loading) boot instr len %Zd\n", len);
	memcpy(priv->ucode_boot.v_addr, src, len);

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);
	return 0;

 err_pci_alloc:
	IWL_ERROR("failed to allocate pci memory\n");
	ret = -ENOMEM;
	iwl4965_dealloc_ucode_pci(priv);

 err_release:
	release_firmware(ucode_raw);

 error:
	return ret;
}

/**
 * iwl_alive_start - called after REPLY_ALIVE notification received
 *                   from protocol/runtime uCode (initialization uCode's
 *                   Alive gets handled by iwl_init_alive_start()).
 */
static void iwl_alive_start(struct iwl_priv *priv)
{
	int ret = 0;

	IWL_DEBUG_INFO("Runtime Alive received.\n");

	if (priv->card_alive.is_valid != UCODE_VALID_OK) {
		/* We had an error bringing up the hardware, so take it
		 * all the way back down so we can try again */
		IWL_DEBUG_INFO("Alive failed.\n");
		goto restart;
	}

	/* Initialize uCode has loaded Runtime uCode ... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "runtime" alive if code weren't properly loaded.  */
	if (iwl_verify_ucode(priv)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO("Bad runtime uCode load.\n");
		goto restart;
	}

	iwl_clear_stations_table(priv);
	ret = priv->cfg->ops->lib->alive_notify(priv);
	if (ret) {
		IWL_WARNING("Could not complete ALIVE transition [ntf]: %d\n",
			    ret);
		goto restart;
	}

	/* After the ALIVE response, we can send host commands to 4965 uCode */
	set_bit(STATUS_ALIVE, &priv->status);

	if (iwl_is_rfkill(priv))
		return;

	ieee80211_wake_queues(priv->hw);

	priv->active_rate = priv->rates_mask;
	priv->active_rate_basic = priv->rates_mask & IWL_BASIC_RATES_MASK;

	if (iwl_is_associated(priv)) {
		struct iwl_rxon_cmd *active_rxon =
				(struct iwl_rxon_cmd *)&priv->active_rxon;

		memcpy(&priv->staging_rxon, &priv->active_rxon,
		       sizeof(priv->staging_rxon));
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	} else {
		/* Initialize our rx_config data */
		iwl4965_connection_init_rx_config(priv);
		memcpy(priv->staging_rxon.node_addr, priv->mac_addr, ETH_ALEN);
	}

	/* Configure Bluetooth device coexistence support */
	iwl4965_send_bt_config(priv);

	iwl_reset_run_time_calib(priv);

	/* Configure the adapter for unassociated operation */
	iwl4965_commit_rxon(priv);

	/* At this point, the NIC is initialized and operational */
	iwl_rf_kill_ct_config(priv);

	iwl_leds_register(priv);

	IWL_DEBUG_INFO("ALIVE processing complete.\n");
	set_bit(STATUS_READY, &priv->status);
	wake_up_interruptible(&priv->wait_command_queue);

	if (priv->error_recovering)
		iwl4965_error_recovery(priv);

	iwl_power_update_mode(priv, 1);
	ieee80211_notify_mac(priv->hw, IEEE80211_NOTIFY_RE_ASSOC);

	if (test_and_clear_bit(STATUS_MODE_PENDING, &priv->status))
		iwl4965_set_mode(priv, priv->iw_mode);

	return;

 restart:
	queue_work(priv->workqueue, &priv->restart);
}

static void iwl_cancel_deferred_work(struct iwl_priv *priv);

static void __iwl4965_down(struct iwl_priv *priv)
{
	unsigned long flags;
	int exit_pending = test_bit(STATUS_EXIT_PENDING, &priv->status);

	IWL_DEBUG_INFO(DRV_NAME " is going down\n");

	if (!exit_pending)
		set_bit(STATUS_EXIT_PENDING, &priv->status);

	iwl_leds_unregister(priv);

	iwl_clear_stations_table(priv);

	/* Unblock any waiting calls */
	wake_up_interruptible_all(&priv->wait_command_queue);

	/* Wipe out the EXIT_PENDING status bit if we are not actually
	 * exiting the module */
	if (!exit_pending)
		clear_bit(STATUS_EXIT_PENDING, &priv->status);

	/* stop and reset the on-board processor */
	iwl_write32(priv, CSR_RESET, CSR_RESET_REG_FLAG_NEVO_RESET);

	/* tell the device to stop sending interrupts */
	spin_lock_irqsave(&priv->lock, flags);
	iwl4965_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
	iwl_synchronize_irq(priv);

	if (priv->mac80211_registered)
		ieee80211_stop_queues(priv->hw);

	/* If we have not previously called iwl4965_init() then
	 * clear all bits but the RF Kill and SUSPEND bits and return */
	if (!iwl_is_init(priv)) {
		priv->status = test_bit(STATUS_RF_KILL_HW, &priv->status) <<
					STATUS_RF_KILL_HW |
			       test_bit(STATUS_RF_KILL_SW, &priv->status) <<
					STATUS_RF_KILL_SW |
			       test_bit(STATUS_GEO_CONFIGURED, &priv->status) <<
					STATUS_GEO_CONFIGURED |
			       test_bit(STATUS_IN_SUSPEND, &priv->status) <<
					STATUS_IN_SUSPEND |
			       test_bit(STATUS_EXIT_PENDING, &priv->status) <<
					STATUS_EXIT_PENDING;
		goto exit;
	}

	/* ...otherwise clear out all the status bits but the RF Kill and
	 * SUSPEND bits and continue taking the NIC down. */
	priv->status &= test_bit(STATUS_RF_KILL_HW, &priv->status) <<
				STATUS_RF_KILL_HW |
			test_bit(STATUS_RF_KILL_SW, &priv->status) <<
				STATUS_RF_KILL_SW |
			test_bit(STATUS_GEO_CONFIGURED, &priv->status) <<
				STATUS_GEO_CONFIGURED |
			test_bit(STATUS_IN_SUSPEND, &priv->status) <<
				STATUS_IN_SUSPEND |
			test_bit(STATUS_FW_ERROR, &priv->status) <<
				STATUS_FW_ERROR |
		       test_bit(STATUS_EXIT_PENDING, &priv->status) <<
				STATUS_EXIT_PENDING;

	spin_lock_irqsave(&priv->lock, flags);
	iwl_clear_bit(priv, CSR_GP_CNTRL,
			 CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_txq_ctx_stop(priv);
	iwl_rxq_stop(priv);

	spin_lock_irqsave(&priv->lock, flags);
	if (!iwl_grab_nic_access(priv)) {
		iwl_write_prph(priv, APMG_CLK_DIS_REG,
					 APMG_CLK_VAL_DMA_CLK_RQT);
		iwl_release_nic_access(priv);
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	udelay(5);

	/* FIXME: apm_ops.suspend(priv) */
	priv->cfg->ops->lib->apm_ops.reset(priv);
	priv->cfg->ops->lib->free_shared_mem(priv);

 exit:
	memset(&priv->card_alive, 0, sizeof(struct iwl_alive_resp));

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);
	priv->ibss_beacon = NULL;

	/* clear out any free frames */
	iwl_clear_free_frames(priv);
}

static void iwl4965_down(struct iwl_priv *priv)
{
	mutex_lock(&priv->mutex);
	__iwl4965_down(priv);
	mutex_unlock(&priv->mutex);

	iwl_cancel_deferred_work(priv);
}

#define MAX_HW_RESTARTS 5

static int __iwl4965_up(struct iwl_priv *priv)
{
	int i;
	int ret;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_WARNING("Exit pending; will not bring the NIC up\n");
		return -EIO;
	}

	if (!priv->ucode_data_backup.v_addr || !priv->ucode_data.v_addr) {
		IWL_ERROR("ucode not available for device bringup\n");
		return -EIO;
	}

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (iwl_read32(priv, CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		set_bit(STATUS_RF_KILL_HW, &priv->status);

	if (iwl_is_rfkill(priv)) {
		iwl4965_enable_interrupts(priv);
		IWL_WARNING("Radio disabled by %s RF Kill switch\n",
		    test_bit(STATUS_RF_KILL_HW, &priv->status) ? "HW" : "SW");
		return 0;
	}

	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);

	ret = priv->cfg->ops->lib->alloc_shared_mem(priv);
	if (ret) {
		IWL_ERROR("Unable to allocate shared memory\n");
		return ret;
	}

	ret = iwl_hw_nic_init(priv);
	if (ret) {
		IWL_ERROR("Unable to init nic\n");
		return ret;
	}

	/* make sure rfkill handshake bits are cleared */
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable host interrupts */
	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);
	iwl4965_enable_interrupts(priv);

	/* really make sure rfkill handshake bits are cleared */
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);

	/* Copy original ucode data image from disk into backup cache.
	 * This will be used to initialize the on-board processor's
	 * data SRAM for a clean start when the runtime program first loads. */
	memcpy(priv->ucode_data_backup.v_addr, priv->ucode_data.v_addr,
	       priv->ucode_data.len);

	for (i = 0; i < MAX_HW_RESTARTS; i++) {

		iwl_clear_stations_table(priv);

		/* load bootstrap state machine,
		 * load bootstrap program into processor's memory,
		 * prepare to load the "initialize" uCode */
		ret = priv->cfg->ops->lib->load_ucode(priv);

		if (ret) {
			IWL_ERROR("Unable to set up bootstrap uCode: %d\n", ret);
			continue;
		}

		/* Clear out the uCode error bit if it is set */
		clear_bit(STATUS_FW_ERROR, &priv->status);

		/* start card; "initialize" will load runtime ucode */
		iwl4965_nic_start(priv);

		IWL_DEBUG_INFO(DRV_NAME " is coming up\n");

		return 0;
	}

	set_bit(STATUS_EXIT_PENDING, &priv->status);
	__iwl4965_down(priv);
	clear_bit(STATUS_EXIT_PENDING, &priv->status);

	/* tried to restart and config the device for as long as our
	 * patience could withstand */
	IWL_ERROR("Unable to initialize device after %d attempts.\n", i);
	return -EIO;
}


/*****************************************************************************
 *
 * Workqueue callbacks
 *
 *****************************************************************************/

static void iwl_bg_init_alive_start(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, init_alive_start.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	priv->cfg->ops->lib->init_alive_start(priv);
	mutex_unlock(&priv->mutex);
}

static void iwl_bg_alive_start(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, alive_start.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwl_alive_start(priv);
	mutex_unlock(&priv->mutex);
}

static void iwl4965_bg_rf_kill(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv, rf_kill);

	wake_up_interruptible(&priv->wait_command_queue);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);

	if (!iwl_is_rfkill(priv)) {
		IWL_DEBUG(IWL_DL_RF_KILL,
			  "HW and/or SW RF Kill no longer active, restarting "
			  "device\n");
		if (!test_bit(STATUS_EXIT_PENDING, &priv->status))
			queue_work(priv->workqueue, &priv->restart);
	} else {
		/* make sure mac80211 stop sending Tx frame */
		if (priv->mac80211_registered)
			ieee80211_stop_queues(priv->hw);

		if (!test_bit(STATUS_RF_KILL_HW, &priv->status))
			IWL_DEBUG_RF_KILL("Can not turn radio back on - "
					  "disabled by SW switch\n");
		else
			IWL_WARNING("Radio Frequency Kill Switch is On:\n"
				    "Kill switch must be turned off for "
				    "wireless networking to work.\n");
	}
	mutex_unlock(&priv->mutex);
	iwl_rfkill_set_hw_state(priv);
}

static void iwl4965_bg_set_monitor(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work,
				struct iwl_priv, set_monitor);
	int ret;

	IWL_DEBUG(IWL_DL_STATE, "setting monitor mode\n");

	mutex_lock(&priv->mutex);

	ret = iwl4965_set_mode(priv, IEEE80211_IF_TYPE_MNTR);

	if (ret) {
		if (ret == -EAGAIN)
			IWL_DEBUG(IWL_DL_STATE, "leave - not ready\n");
		else
			IWL_ERROR("iwl4965_set_mode() failed ret = %d\n", ret);
	}

	mutex_unlock(&priv->mutex);
}

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
		iwl_chain_noise_calibration(priv, &priv->statistics);

		iwl_sensitivity_calibration(priv, &priv->statistics);
	}

	mutex_unlock(&priv->mutex);
	return;
}

static void iwl4965_bg_up(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, up);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	__iwl4965_up(priv);
	mutex_unlock(&priv->mutex);
	iwl_rfkill_set_hw_state(priv);
}

static void iwl4965_bg_restart(struct work_struct *data)
{
	struct iwl_priv *priv = container_of(data, struct iwl_priv, restart);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	iwl4965_down(priv);
	queue_work(priv->workqueue, &priv->up);
}

static void iwl4965_bg_rx_replenish(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, rx_replenish);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	iwl_rx_replenish(priv);
	mutex_unlock(&priv->mutex);
}

#define IWL_DELAY_NEXT_SCAN (HZ*2)

static void iwl4965_post_associate(struct iwl_priv *priv)
{
	struct ieee80211_conf *conf = NULL;
	int ret = 0;
	DECLARE_MAC_BUF(mac);
	unsigned long flags;

	if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {
		IWL_ERROR("%s Should not be called in AP mode\n", __func__);
		return;
	}

	IWL_DEBUG_ASSOC("Associated as %d to: %s\n",
			priv->assoc_id,
			print_mac(mac, priv->active_rxon.bssid_addr));


	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;


	if (!priv->vif || !priv->is_open)
		return;

	iwl_scan_cancel_timeout(priv, 200);

	conf = ieee80211_get_hw_conf(priv->hw);

	priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwl4965_commit_rxon(priv);

	memset(&priv->rxon_timing, 0, sizeof(struct iwl4965_rxon_time_cmd));
	iwl4965_setup_rxon_timing(priv);
	ret = iwl_send_cmd_pdu(priv, REPLY_RXON_TIMING,
			      sizeof(priv->rxon_timing), &priv->rxon_timing);
	if (ret)
		IWL_WARNING("REPLY_RXON_TIMING failed - "
			    "Attempting to continue.\n");

	priv->staging_rxon.filter_flags |= RXON_FILTER_ASSOC_MSK;

	if (priv->current_ht_config.is_ht)
		iwl_set_rxon_ht(priv, &priv->current_ht_config);

	iwl_set_rxon_chain(priv);
	priv->staging_rxon.assoc_id = cpu_to_le16(priv->assoc_id);

	IWL_DEBUG_ASSOC("assoc id %d beacon interval %d\n",
			priv->assoc_id, priv->beacon_int);

	if (priv->assoc_capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		priv->staging_rxon.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		priv->staging_rxon.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;

	if (priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK) {
		if (priv->assoc_capability & WLAN_CAPABILITY_SHORT_SLOT_TIME)
			priv->staging_rxon.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS)
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

	}

	iwl4965_commit_rxon(priv);

	switch (priv->iw_mode) {
	case IEEE80211_IF_TYPE_STA:
		break;

	case IEEE80211_IF_TYPE_IBSS:

		/* assume default assoc id */
		priv->assoc_id = 1;

		iwl_rxon_add_station(priv, priv->bssid, 0);
		iwl4965_send_beacon_cmd(priv);

		break;

	default:
		IWL_ERROR("%s Should not be called in %d mode\n",
			  __func__, priv->iw_mode);
		break;
	}

	/* Enable Rx differential gain and sensitivity calibrations */
	iwl_chain_noise_reset(priv);
	priv->start_calib = 1;

	if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS)
		priv->assoc_station_added = 1;

	spin_lock_irqsave(&priv->lock, flags);
	iwl_activate_qos(priv, 0);
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_power_update_mode(priv, 0);
	/* we have just associated, don't start scan too early */
	priv->next_scan_jiffies = jiffies + IWL_DELAY_NEXT_SCAN;
}

static int iwl4965_mac_config(struct ieee80211_hw *hw, struct ieee80211_conf *conf);

static void iwl_bg_scan_completed(struct work_struct *work)
{
	struct iwl_priv *priv =
	    container_of(work, struct iwl_priv, scan_completed);

	IWL_DEBUG_SCAN("SCAN complete scan\n");

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (test_bit(STATUS_CONF_PENDING, &priv->status))
		iwl4965_mac_config(priv->hw, ieee80211_get_hw_conf(priv->hw));

	ieee80211_scan_completed(priv->hw);

	/* Since setting the TXPOWER may have been deferred while
	 * performing the scan, fire one off */
	mutex_lock(&priv->mutex);
	iwl_set_tx_power(priv, priv->tx_power_user_lmt, true);
	mutex_unlock(&priv->mutex);
}

/*****************************************************************************
 *
 * mac80211 entry point functions
 *
 *****************************************************************************/

#define UCODE_READY_TIMEOUT	(4 * HZ)

static int iwl4965_mac_start(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	int ret;

	IWL_DEBUG_MAC80211("enter\n");

	if (pci_enable_device(priv->pci_dev)) {
		IWL_ERROR("Fail to pci_enable_device\n");
		return -ENODEV;
	}
	pci_restore_state(priv->pci_dev);
	pci_enable_msi(priv->pci_dev);

	ret = request_irq(priv->pci_dev->irq, iwl4965_isr, IRQF_SHARED,
			  DRV_NAME, priv);
	if (ret) {
		IWL_ERROR("Error allocating IRQ %d\n", priv->pci_dev->irq);
		goto out_disable_msi;
	}

	/* we should be verifying the device is ready to be opened */
	mutex_lock(&priv->mutex);

	memset(&priv->staging_rxon, 0, sizeof(struct iwl_rxon_cmd));
	/* fetch ucode file from disk, alloc and copy to bus-master buffers ...
	 * ucode filename and max sizes are card-specific. */

	if (!priv->ucode_code.len) {
		ret = iwl4965_read_ucode(priv);
		if (ret) {
			IWL_ERROR("Could not read microcode: %d\n", ret);
			mutex_unlock(&priv->mutex);
			goto out_release_irq;
		}
	}

	ret = __iwl4965_up(priv);

	mutex_unlock(&priv->mutex);

	iwl_rfkill_set_hw_state(priv);

	if (ret)
		goto out_release_irq;

	if (iwl_is_rfkill(priv))
		goto out;

	IWL_DEBUG_INFO("Start UP work done.\n");

	if (test_bit(STATUS_IN_SUSPEND, &priv->status))
		return 0;

	/* Wait for START_ALIVE from Run Time ucode. Otherwise callbacks from
	 * mac80211 will not be run successfully. */
	ret = wait_event_interruptible_timeout(priv->wait_command_queue,
			test_bit(STATUS_READY, &priv->status),
			UCODE_READY_TIMEOUT);
	if (!ret) {
		if (!test_bit(STATUS_READY, &priv->status)) {
			IWL_ERROR("START_ALIVE timeout after %dms.\n",
				jiffies_to_msecs(UCODE_READY_TIMEOUT));
			ret = -ETIMEDOUT;
			goto out_release_irq;
		}
	}

out:
	priv->is_open = 1;
	IWL_DEBUG_MAC80211("leave\n");
	return 0;

out_release_irq:
	free_irq(priv->pci_dev->irq, priv);
out_disable_msi:
	pci_disable_msi(priv->pci_dev);
	pci_disable_device(priv->pci_dev);
	priv->is_open = 0;
	IWL_DEBUG_MAC80211("leave - failed\n");
	return ret;
}

static void iwl4965_mac_stop(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211("enter\n");

	if (!priv->is_open) {
		IWL_DEBUG_MAC80211("leave - skip\n");
		return;
	}

	priv->is_open = 0;

	if (iwl_is_ready_rf(priv)) {
		/* stop mac, cancel any scan request and clear
		 * RXON_FILTER_ASSOC_MSK BIT
		 */
		mutex_lock(&priv->mutex);
		iwl_scan_cancel_timeout(priv, 100);
		mutex_unlock(&priv->mutex);
	}

	iwl4965_down(priv);

	flush_workqueue(priv->workqueue);
	free_irq(priv->pci_dev->irq, priv);
	pci_disable_msi(priv->pci_dev);
	pci_save_state(priv->pci_dev);
	pci_disable_device(priv->pci_dev);

	IWL_DEBUG_MAC80211("leave\n");
}

static int iwl4965_mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MACDUMP("enter\n");

	if (priv->iw_mode == IEEE80211_IF_TYPE_MNTR) {
		IWL_DEBUG_MAC80211("leave - monitor\n");
		dev_kfree_skb_any(skb);
		return 0;
	}

	IWL_DEBUG_TX("dev->xmit(%d bytes) at rate 0x%02x\n", skb->len,
		     ieee80211_get_tx_rate(hw, IEEE80211_SKB_CB(skb))->bitrate);

	if (iwl_tx_skb(priv, skb))
		dev_kfree_skb_any(skb);

	IWL_DEBUG_MACDUMP("leave\n");
	return 0;
}

static int iwl4965_mac_add_interface(struct ieee80211_hw *hw,
				 struct ieee80211_if_init_conf *conf)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	DECLARE_MAC_BUF(mac);

	IWL_DEBUG_MAC80211("enter: type %d\n", conf->type);

	if (priv->vif) {
		IWL_DEBUG_MAC80211("leave - vif != NULL\n");
		return -EOPNOTSUPP;
	}

	spin_lock_irqsave(&priv->lock, flags);
	priv->vif = conf->vif;

	spin_unlock_irqrestore(&priv->lock, flags);

	mutex_lock(&priv->mutex);

	if (conf->mac_addr) {
		IWL_DEBUG_MAC80211("Set %s\n", print_mac(mac, conf->mac_addr));
		memcpy(priv->mac_addr, conf->mac_addr, ETH_ALEN);
	}

	if (iwl4965_set_mode(priv, conf->type) == -EAGAIN)
		/* we are not ready, will run again when ready */
		set_bit(STATUS_MODE_PENDING, &priv->status);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211("leave\n");
	return 0;
}

/**
 * iwl4965_mac_config - mac80211 config callback
 *
 * We ignore conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME since it seems to
 * be set inappropriately and the driver currently sets the hardware up to
 * use it whenever needed.
 */
static int iwl4965_mac_config(struct ieee80211_hw *hw, struct ieee80211_conf *conf)
{
	struct iwl_priv *priv = hw->priv;
	const struct iwl_channel_info *ch_info;
	unsigned long flags;
	int ret = 0;
	u16 channel;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211("enter to channel %d\n", conf->channel->hw_value);

	if (conf->radio_enabled && iwl_radio_kill_sw_enable_radio(priv)) {
		IWL_DEBUG_MAC80211("leave - RF-KILL - waiting for uCode\n");
		goto out;
	}

	if (!conf->radio_enabled)
		iwl_radio_kill_sw_disable_radio(priv);

	if (!iwl_is_ready(priv)) {
		IWL_DEBUG_MAC80211("leave - not ready\n");
		ret = -EIO;
		goto out;
	}

	if (unlikely(!priv->cfg->mod_params->disable_hw_scan &&
		     test_bit(STATUS_SCANNING, &priv->status))) {
		IWL_DEBUG_MAC80211("leave - scanning\n");
		set_bit(STATUS_CONF_PENDING, &priv->status);
		mutex_unlock(&priv->mutex);
		return 0;
	}

	channel = ieee80211_frequency_to_channel(conf->channel->center_freq);
	ch_info = iwl_get_channel_info(priv, conf->channel->band, channel);
	if (!is_channel_valid(ch_info)) {
		IWL_DEBUG_MAC80211("leave - invalid channel\n");
		ret = -EINVAL;
		goto out;
	}

	if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS &&
	    !is_channel_ibss(ch_info)) {
		IWL_ERROR("channel %d in band %d not IBSS channel\n",
			conf->channel->hw_value, conf->channel->band);
		ret = -EINVAL;
		goto out;
	}

	spin_lock_irqsave(&priv->lock, flags);


	/* if we are switching from ht to 2.4 clear flags
	 * from any ht related info since 2.4 does not
	 * support ht */
	if ((le16_to_cpu(priv->staging_rxon.channel) != channel)
#ifdef IEEE80211_CONF_CHANNEL_SWITCH
	    && !(conf->flags & IEEE80211_CONF_CHANNEL_SWITCH)
#endif
	)
		priv->staging_rxon.flags = 0;

	iwl_set_rxon_channel(priv, conf->channel->band, channel);

	iwl_set_flags_for_band(priv, conf->channel->band);

	/* The list of supported rates and rate mask can be different
	 * for each band; since the band may have changed, reset
	 * the rate mask to what mac80211 lists */
	iwl4965_set_rate(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

#ifdef IEEE80211_CONF_CHANNEL_SWITCH
	if (conf->flags & IEEE80211_CONF_CHANNEL_SWITCH) {
		iwl4965_hw_channel_switch(priv, conf->channel);
		goto out;
	}
#endif

	if (!conf->radio_enabled) {
		IWL_DEBUG_MAC80211("leave - radio disabled\n");
		goto out;
	}

	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_MAC80211("leave - RF kill\n");
		ret = -EIO;
		goto out;
	}

	IWL_DEBUG_MAC80211("TX Power old=%d new=%d\n",
			   priv->tx_power_user_lmt, conf->power_level);

	iwl_set_tx_power(priv, conf->power_level, false);

	iwl4965_set_rate(priv);

	if (memcmp(&priv->active_rxon,
		   &priv->staging_rxon, sizeof(priv->staging_rxon)))
		iwl4965_commit_rxon(priv);
	else
		IWL_DEBUG_INFO("No re-sending same RXON configuration.\n");

	IWL_DEBUG_MAC80211("leave\n");

out:
	clear_bit(STATUS_CONF_PENDING, &priv->status);
	mutex_unlock(&priv->mutex);
	return ret;
}

static void iwl4965_config_ap(struct iwl_priv *priv)
{
	int ret = 0;
	unsigned long flags;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* The following should be done only at AP bring up */
	if (!(iwl_is_associated(priv))) {

		/* RXON - unassoc (to set timing command) */
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl4965_commit_rxon(priv);

		/* RXON Timing */
		memset(&priv->rxon_timing, 0, sizeof(struct iwl4965_rxon_time_cmd));
		iwl4965_setup_rxon_timing(priv);
		ret = iwl_send_cmd_pdu(priv, REPLY_RXON_TIMING,
				sizeof(priv->rxon_timing), &priv->rxon_timing);
		if (ret)
			IWL_WARNING("REPLY_RXON_TIMING failed - "
					"Attempting to continue.\n");

		iwl_set_rxon_chain(priv);

		/* FIXME: what should be the assoc_id for AP? */
		priv->staging_rxon.assoc_id = cpu_to_le16(priv->assoc_id);
		if (priv->assoc_capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
			priv->staging_rxon.flags |=
				RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			priv->staging_rxon.flags &=
				~RXON_FLG_SHORT_PREAMBLE_MSK;

		if (priv->staging_rxon.flags & RXON_FLG_BAND_24G_MSK) {
			if (priv->assoc_capability &
				WLAN_CAPABILITY_SHORT_SLOT_TIME)
				priv->staging_rxon.flags |=
					RXON_FLG_SHORT_SLOT_MSK;
			else
				priv->staging_rxon.flags &=
					~RXON_FLG_SHORT_SLOT_MSK;

			if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS)
				priv->staging_rxon.flags &=
					~RXON_FLG_SHORT_SLOT_MSK;
		}
		/* restore RXON assoc */
		priv->staging_rxon.filter_flags |= RXON_FILTER_ASSOC_MSK;
		iwl4965_commit_rxon(priv);
		spin_lock_irqsave(&priv->lock, flags);
		iwl_activate_qos(priv, 1);
		spin_unlock_irqrestore(&priv->lock, flags);
		iwl_rxon_add_station(priv, iwl_bcast_addr, 0);
	}
	iwl4965_send_beacon_cmd(priv);

	/* FIXME - we need to add code here to detect a totally new
	 * configuration, reset the AP, unassoc, rxon timing, assoc,
	 * clear sta table, add BCAST sta... */
}

/* temporary */
static int iwl4965_mac_beacon_update(struct ieee80211_hw *hw, struct sk_buff *skb);

static int iwl4965_mac_config_interface(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
				    struct ieee80211_if_conf *conf)
{
	struct iwl_priv *priv = hw->priv;
	DECLARE_MAC_BUF(mac);
	unsigned long flags;
	int rc;

	if (conf == NULL)
		return -EIO;

	if (priv->vif != vif) {
		IWL_DEBUG_MAC80211("leave - priv->vif != vif\n");
		return 0;
	}

	if (priv->iw_mode == IEEE80211_IF_TYPE_IBSS &&
	    conf->changed & IEEE80211_IFCC_BEACON) {
		struct sk_buff *beacon = ieee80211_beacon_get(hw, vif);
		if (!beacon)
			return -ENOMEM;
		rc = iwl4965_mac_beacon_update(hw, beacon);
		if (rc)
			return rc;
	}

	if ((priv->iw_mode == IEEE80211_IF_TYPE_AP) &&
	    (!conf->ssid_len)) {
		IWL_DEBUG_MAC80211
		    ("Leaving in AP mode because HostAPD is not ready.\n");
		return 0;
	}

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	mutex_lock(&priv->mutex);

	if (conf->bssid)
		IWL_DEBUG_MAC80211("bssid: %s\n",
				   print_mac(mac, conf->bssid));

/*
 * very dubious code was here; the probe filtering flag is never set:
 *
	if (unlikely(test_bit(STATUS_SCANNING, &priv->status)) &&
	    !(priv->hw->flags & IEEE80211_HW_NO_PROBE_FILTERING)) {
 */

	if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {
		if (!conf->bssid) {
			conf->bssid = priv->mac_addr;
			memcpy(priv->bssid, priv->mac_addr, ETH_ALEN);
			IWL_DEBUG_MAC80211("bssid was set to: %s\n",
					   print_mac(mac, conf->bssid));
		}
		if (priv->ibss_beacon)
			dev_kfree_skb(priv->ibss_beacon);

		priv->ibss_beacon = ieee80211_beacon_get(hw, vif);
	}

	if (iwl_is_rfkill(priv))
		goto done;

	if (conf->bssid && !is_zero_ether_addr(conf->bssid) &&
	    !is_multicast_ether_addr(conf->bssid)) {
		/* If there is currently a HW scan going on in the background
		 * then we need to cancel it else the RXON below will fail. */
		if (iwl_scan_cancel_timeout(priv, 100)) {
			IWL_WARNING("Aborted scan still in progress "
				    "after 100ms\n");
			IWL_DEBUG_MAC80211("leaving - scan abort failed.\n");
			mutex_unlock(&priv->mutex);
			return -EAGAIN;
		}
		memcpy(priv->staging_rxon.bssid_addr, conf->bssid, ETH_ALEN);

		/* TODO: Audit driver for usage of these members and see
		 * if mac80211 deprecates them (priv->bssid looks like it
		 * shouldn't be there, but I haven't scanned the IBSS code
		 * to verify) - jpk */
		memcpy(priv->bssid, conf->bssid, ETH_ALEN);

		if (priv->iw_mode == IEEE80211_IF_TYPE_AP)
			iwl4965_config_ap(priv);
		else {
			rc = iwl4965_commit_rxon(priv);
			if ((priv->iw_mode == IEEE80211_IF_TYPE_STA) && rc)
				iwl_rxon_add_station(
					priv, priv->active_rxon.bssid_addr, 1);
		}

	} else {
		iwl_scan_cancel_timeout(priv, 100);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl4965_commit_rxon(priv);
	}

 done:
	spin_lock_irqsave(&priv->lock, flags);
	if (!conf->ssid_len)
		memset(priv->essid, 0, IW_ESSID_MAX_SIZE);
	else
		memcpy(priv->essid, conf->ssid, conf->ssid_len);

	priv->essid_len = conf->ssid_len;
	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_MAC80211("leave\n");
	mutex_unlock(&priv->mutex);

	return 0;
}

static void iwl4965_configure_filter(struct ieee80211_hw *hw,
				 unsigned int changed_flags,
				 unsigned int *total_flags,
				 int mc_count, struct dev_addr_list *mc_list)
{
	struct iwl_priv *priv = hw->priv;

	if (changed_flags & (*total_flags) & FIF_OTHER_BSS) {
		IWL_DEBUG_MAC80211("Enter: type %d (0x%x, 0x%x)\n",
				   IEEE80211_IF_TYPE_MNTR,
				   changed_flags, *total_flags);
		/* queue work 'cuz mac80211 is holding a lock which
		 * prevents us from issuing (synchronous) f/w cmds */
		queue_work(priv->workqueue, &priv->set_monitor);
	}
	*total_flags &= FIF_OTHER_BSS | FIF_ALLMULTI |
			FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL;
}

static void iwl4965_mac_remove_interface(struct ieee80211_hw *hw,
				     struct ieee80211_if_init_conf *conf)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211("enter\n");

	mutex_lock(&priv->mutex);

	if (iwl_is_ready_rf(priv)) {
		iwl_scan_cancel_timeout(priv, 100);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl4965_commit_rxon(priv);
	}
	if (priv->vif == conf->vif) {
		priv->vif = NULL;
		memset(priv->bssid, 0, ETH_ALEN);
		memset(priv->essid, 0, IW_ESSID_MAX_SIZE);
		priv->essid_len = 0;
	}
	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211("leave\n");

}

#define IWL_DELAY_NEXT_SCAN_AFTER_ASSOC (HZ*6)
static void iwl4965_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u32 changes)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211("changes = 0x%X\n", changes);

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		IWL_DEBUG_MAC80211("ERP_PREAMBLE %d\n",
				   bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			priv->staging_rxon.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	}

	if (changes & BSS_CHANGED_ERP_CTS_PROT) {
		IWL_DEBUG_MAC80211("ERP_CTS %d\n", bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot && (priv->band != IEEE80211_BAND_5GHZ))
			priv->staging_rxon.flags |= RXON_FLG_TGG_PROTECT_MSK;
		else
			priv->staging_rxon.flags &= ~RXON_FLG_TGG_PROTECT_MSK;
	}

	if (changes & BSS_CHANGED_HT) {
		IWL_DEBUG_MAC80211("HT %d\n", bss_conf->assoc_ht);
		iwl4965_ht_conf(priv, bss_conf);
		iwl_set_rxon_chain(priv);
	}

	if (changes & BSS_CHANGED_ASSOC) {
		IWL_DEBUG_MAC80211("ASSOC %d\n", bss_conf->assoc);
		/* This should never happen as this function should
		 * never be called from interrupt context. */
		if (WARN_ON_ONCE(in_interrupt()))
			return;
		if (bss_conf->assoc) {
			priv->assoc_id = bss_conf->aid;
			priv->beacon_int = bss_conf->beacon_int;
			priv->power_data.dtim_period = bss_conf->dtim_period;
			priv->timestamp = bss_conf->timestamp;
			priv->assoc_capability = bss_conf->assoc_capability;
			priv->next_scan_jiffies = jiffies +
					IWL_DELAY_NEXT_SCAN_AFTER_ASSOC;
			mutex_lock(&priv->mutex);
			iwl4965_post_associate(priv);
			mutex_unlock(&priv->mutex);
		} else {
			priv->assoc_id = 0;
			IWL_DEBUG_MAC80211("DISASSOC %d\n", bss_conf->assoc);
		}
	} else if (changes && iwl_is_associated(priv) && priv->assoc_id) {
			IWL_DEBUG_MAC80211("Associated Changes %d\n", changes);
			iwl_send_rxon_assoc(priv);
	}

}

static int iwl_mac_hw_scan(struct ieee80211_hw *hw, u8 *ssid, size_t ssid_len)
{
	int ret;
	unsigned long flags;
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211("enter\n");

	mutex_lock(&priv->mutex);
	spin_lock_irqsave(&priv->lock, flags);

	if (!iwl_is_ready_rf(priv)) {
		ret = -EIO;
		IWL_DEBUG_MAC80211("leave - not ready or exit pending\n");
		goto out_unlock;
	}

	if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {	/* APs don't scan */
		ret = -EIO;
		IWL_ERROR("ERROR: APs don't scan\n");
		goto out_unlock;
	}

	/* we don't schedule scan within next_scan_jiffies period */
	if (priv->next_scan_jiffies &&
	    time_after(priv->next_scan_jiffies, jiffies)) {
		IWL_DEBUG_SCAN("scan rejected: within next scan period\n");
		ret = -EAGAIN;
		goto out_unlock;
	}
	/* if we just finished scan ask for delay */
	if (iwl_is_associated(priv) && priv->last_scan_jiffies &&
	    time_after(priv->last_scan_jiffies + IWL_DELAY_NEXT_SCAN, jiffies)) {
		IWL_DEBUG_SCAN("scan rejected: within previous scan period\n");
		ret = -EAGAIN;
		goto out_unlock;
	}
	if (ssid_len) {
		priv->one_direct_scan = 1;
		priv->direct_ssid_len =  min_t(u8, ssid_len, IW_ESSID_MAX_SIZE);
		memcpy(priv->direct_ssid, ssid, priv->direct_ssid_len);
	} else {
		priv->one_direct_scan = 0;
	}

	ret = iwl_scan_initiate(priv);

	IWL_DEBUG_MAC80211("leave\n");

out_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
	mutex_unlock(&priv->mutex);

	return ret;
}

static void iwl4965_mac_update_tkip_key(struct ieee80211_hw *hw,
			struct ieee80211_key_conf *keyconf, const u8 *addr,
			u32 iv32, u16 *phase1key)
{
	struct iwl_priv *priv = hw->priv;
	u8 sta_id = IWL_INVALID_STATION;
	unsigned long flags;
	__le16 key_flags = 0;
	int i;
	DECLARE_MAC_BUF(mac);

	IWL_DEBUG_MAC80211("enter\n");

	sta_id = iwl_find_station(priv, addr);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_DEBUG_MAC80211("leave - %s not in station map.\n",
				   print_mac(mac, addr));
		return;
	}

	iwl_scan_cancel_timeout(priv, 100);

	key_flags |= (STA_KEY_FLG_TKIP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == priv->hw_params.bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].sta.key.key_flags = key_flags;
	priv->stations[sta_id].sta.key.tkip_rx_tsc_byte2 = (u8) iv32;

	for (i = 0; i < 5; i++)
		priv->stations[sta_id].sta.key.tkip_rx_ttak[i] =
			cpu_to_le16(phase1key[i]);

	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	IWL_DEBUG_MAC80211("leave\n");
}

static int iwl4965_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			   const u8 *local_addr, const u8 *addr,
			   struct ieee80211_key_conf *key)
{
	struct iwl_priv *priv = hw->priv;
	DECLARE_MAC_BUF(mac);
	int ret = 0;
	u8 sta_id = IWL_INVALID_STATION;
	u8 is_default_wep_key = 0;

	IWL_DEBUG_MAC80211("enter\n");

	if (priv->hw_params.sw_crypto) {
		IWL_DEBUG_MAC80211("leave - hwcrypto disabled\n");
		return -EOPNOTSUPP;
	}

	if (is_zero_ether_addr(addr))
		/* only support pairwise keys */
		return -EOPNOTSUPP;

	sta_id = iwl_find_station(priv, addr);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_DEBUG_MAC80211("leave - %s not in station map.\n",
				   print_mac(mac, addr));
		return -EINVAL;

	}

	mutex_lock(&priv->mutex);
	iwl_scan_cancel_timeout(priv, 100);
	mutex_unlock(&priv->mutex);

	/* If we are getting WEP group key and we didn't receive any key mapping
	 * so far, we are in legacy wep mode (group key only), otherwise we are
	 * in 1X mode.
	 * In legacy wep mode, we use another host command to the uCode */
	if (key->alg == ALG_WEP && sta_id == priv->hw_params.bcast_sta_id &&
		priv->iw_mode != IEEE80211_IF_TYPE_AP) {
		if (cmd == SET_KEY)
			is_default_wep_key = !priv->key_mapping_key;
		else
			is_default_wep_key =
					(key->hw_key_idx == HW_KEY_DEFAULT);
	}

	switch (cmd) {
	case SET_KEY:
		if (is_default_wep_key)
			ret = iwl_set_default_wep_key(priv, key);
		else
			ret = iwl_set_dynamic_key(priv, key, sta_id);

		IWL_DEBUG_MAC80211("enable hwcrypto key\n");
		break;
	case DISABLE_KEY:
		if (is_default_wep_key)
			ret = iwl_remove_default_wep_key(priv, key);
		else
			ret = iwl_remove_dynamic_key(priv, key, sta_id);

		IWL_DEBUG_MAC80211("disable hwcrypto key\n");
		break;
	default:
		ret = -EINVAL;
	}

	IWL_DEBUG_MAC80211("leave\n");

	return ret;
}

static int iwl4965_mac_conf_tx(struct ieee80211_hw *hw, u16 queue,
			   const struct ieee80211_tx_queue_params *params)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	int q;

	IWL_DEBUG_MAC80211("enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211("leave - RF not ready\n");
		return -EIO;
	}

	if (queue >= AC_NUM) {
		IWL_DEBUG_MAC80211("leave - queue >= AC_NUM %d\n", queue);
		return 0;
	}

	if (!priv->qos_data.qos_enable) {
		priv->qos_data.qos_active = 0;
		IWL_DEBUG_MAC80211("leave - qos not enabled\n");
		return 0;
	}
	q = AC_NUM - 1 - queue;

	spin_lock_irqsave(&priv->lock, flags);

	priv->qos_data.def_qos_parm.ac[q].cw_min = cpu_to_le16(params->cw_min);
	priv->qos_data.def_qos_parm.ac[q].cw_max = cpu_to_le16(params->cw_max);
	priv->qos_data.def_qos_parm.ac[q].aifsn = params->aifs;
	priv->qos_data.def_qos_parm.ac[q].edca_txop =
			cpu_to_le16((params->txop * 32));

	priv->qos_data.def_qos_parm.ac[q].reserved1 = 0;
	priv->qos_data.qos_active = 1;

	if (priv->iw_mode == IEEE80211_IF_TYPE_AP)
		iwl_activate_qos(priv, 1);
	else if (priv->assoc_id && iwl_is_associated(priv))
		iwl_activate_qos(priv, 0);

	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_MAC80211("leave\n");
	return 0;
}

static int iwl4965_mac_ampdu_action(struct ieee80211_hw *hw,
			     enum ieee80211_ampdu_mlme_action action,
			     const u8 *addr, u16 tid, u16 *ssn)
{
	struct iwl_priv *priv = hw->priv;
	DECLARE_MAC_BUF(mac);

	IWL_DEBUG_HT("A-MPDU action on addr %s tid %d\n",
		     print_mac(mac, addr), tid);

	if (!(priv->cfg->sku & IWL_SKU_N))
		return -EACCES;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		IWL_DEBUG_HT("start Rx\n");
		return iwl_rx_agg_start(priv, addr, tid, *ssn);
	case IEEE80211_AMPDU_RX_STOP:
		IWL_DEBUG_HT("stop Rx\n");
		return iwl_rx_agg_stop(priv, addr, tid);
	case IEEE80211_AMPDU_TX_START:
		IWL_DEBUG_HT("start Tx\n");
		return iwl_tx_agg_start(priv, addr, tid, ssn);
	case IEEE80211_AMPDU_TX_STOP:
		IWL_DEBUG_HT("stop Tx\n");
		return iwl_tx_agg_stop(priv, addr, tid);
	default:
		IWL_DEBUG_HT("unknown\n");
		return -EINVAL;
		break;
	}
	return 0;
}
static int iwl4965_mac_get_tx_stats(struct ieee80211_hw *hw,
				struct ieee80211_tx_queue_stats *stats)
{
	struct iwl_priv *priv = hw->priv;
	int i, avail;
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;
	unsigned long flags;

	IWL_DEBUG_MAC80211("enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211("leave - RF not ready\n");
		return -EIO;
	}

	spin_lock_irqsave(&priv->lock, flags);

	for (i = 0; i < AC_NUM; i++) {
		txq = &priv->txq[i];
		q = &txq->q;
		avail = iwl_queue_space(q);

		stats[i].len = q->n_window - avail;
		stats[i].limit = q->n_window - q->high_mark;
		stats[i].count = q->n_window;

	}
	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_MAC80211("leave\n");

	return 0;
}

static int iwl4965_mac_get_stats(struct ieee80211_hw *hw,
			     struct ieee80211_low_level_stats *stats)
{
	struct iwl_priv *priv = hw->priv;

	priv = hw->priv;
	IWL_DEBUG_MAC80211("enter\n");
	IWL_DEBUG_MAC80211("leave\n");

	return 0;
}

static void iwl4965_mac_reset_tsf(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211("enter\n");

	spin_lock_irqsave(&priv->lock, flags);
	memset(&priv->current_ht_config, 0, sizeof(struct iwl_ht_info));
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_reset_qos(priv);

	spin_lock_irqsave(&priv->lock, flags);
	priv->assoc_id = 0;
	priv->assoc_capability = 0;
	priv->assoc_station_added = 0;

	/* new association get rid of ibss beacon skb */
	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = NULL;

	priv->beacon_int = priv->hw->conf.beacon_int;
	priv->timestamp = 0;
	if ((priv->iw_mode == IEEE80211_IF_TYPE_STA))
		priv->beacon_int = 0;

	spin_unlock_irqrestore(&priv->lock, flags);

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211("leave - not ready\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	/* we are restarting association process
	 * clear RXON_FILTER_ASSOC_MSK bit
	 */
	if (priv->iw_mode != IEEE80211_IF_TYPE_AP) {
		iwl_scan_cancel_timeout(priv, 100);
		priv->staging_rxon.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl4965_commit_rxon(priv);
	}

	iwl_power_update_mode(priv, 0);

	/* Per mac80211.h: This is only used in IBSS mode... */
	if (priv->iw_mode != IEEE80211_IF_TYPE_IBSS) {

		IWL_DEBUG_MAC80211("leave - not in IBSS\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	iwl4965_set_rate(priv);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211("leave\n");
}

static int iwl4965_mac_beacon_update(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	__le64 timestamp;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211("enter\n");

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211("leave - RF not ready\n");
		mutex_unlock(&priv->mutex);
		return -EIO;
	}

	if (priv->iw_mode != IEEE80211_IF_TYPE_IBSS) {
		IWL_DEBUG_MAC80211("leave - not IBSS\n");
		mutex_unlock(&priv->mutex);
		return -EIO;
	}

	spin_lock_irqsave(&priv->lock, flags);

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	priv->ibss_beacon = skb;

	priv->assoc_id = 0;
	timestamp = ((struct ieee80211_mgmt *)skb->data)->u.beacon.timestamp;
	priv->timestamp = le64_to_cpu(timestamp) +  (priv->beacon_int * 1000);

	IWL_DEBUG_MAC80211("leave\n");
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_reset_qos(priv);

	iwl4965_post_associate(priv);

	mutex_unlock(&priv->mutex);

	return 0;
}

/*****************************************************************************
 *
 * sysfs attributes
 *
 *****************************************************************************/

#ifdef CONFIG_IWLWIFI_DEBUG

/*
 * The following adds a new attribute to the sysfs representation
 * of this device driver (i.e. a new file in /sys/bus/pci/drivers/iwl/)
 * used for controlling the debug level.
 *
 * See the level definitions in iwl for details.
 */

static ssize_t show_debug_level(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = d->driver_data;

	return sprintf(buf, "0x%08X\n", priv->debug_level);
}
static ssize_t store_debug_level(struct device *d,
				struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct iwl_priv *priv = d->driver_data;
	char *p = (char *)buf;
	u32 val;

	val = simple_strtoul(p, &p, 0);
	if (p == buf)
		printk(KERN_INFO DRV_NAME
		       ": %s is not in hex or decimal form.\n", buf);
	else
		priv->debug_level = val;

	return strnlen(buf, count);
}

static DEVICE_ATTR(debug_level, S_IWUSR | S_IRUGO,
			show_debug_level, store_debug_level);


#endif /* CONFIG_IWLWIFI_DEBUG */


static ssize_t show_version(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = d->driver_data;
	struct iwl_alive_resp *palive = &priv->card_alive;
	ssize_t pos = 0;
	u16 eeprom_ver;

	if (palive->is_valid)
		pos += sprintf(buf + pos,
				"fw version: 0x%01X.0x%01X.0x%01X.0x%01X\n"
				"fw type: 0x%01X 0x%01X\n",
				palive->ucode_major, palive->ucode_minor,
				palive->sw_rev[0], palive->sw_rev[1],
				palive->ver_type, palive->ver_subtype);
	else
		pos += sprintf(buf + pos, "fw not loaded\n");

	if (priv->eeprom) {
		eeprom_ver = iwl_eeprom_query16(priv, EEPROM_VERSION);
		pos += sprintf(buf + pos, "EEPROM version: 0x%x\n",
				 eeprom_ver);
	} else {
		pos += sprintf(buf + pos, "EEPROM not initialzed\n");
	}

	return pos;
}

static DEVICE_ATTR(version, S_IWUSR | S_IRUGO, show_version, NULL);

static ssize_t show_temperature(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	return sprintf(buf, "%d\n", priv->temperature);
}

static DEVICE_ATTR(temperature, S_IRUGO, show_temperature, NULL);

static ssize_t show_tx_power(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	return sprintf(buf, "%d\n", priv->tx_power_user_lmt);
}

static ssize_t store_tx_power(struct device *d,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	char *p = (char *)buf;
	u32 val;

	val = simple_strtoul(p, &p, 10);
	if (p == buf)
		printk(KERN_INFO DRV_NAME
		       ": %s is not in decimal form.\n", buf);
	else
		iwl_set_tx_power(priv, val, false);

	return count;
}

static DEVICE_ATTR(tx_power, S_IWUSR | S_IRUGO, show_tx_power, store_tx_power);

static ssize_t show_flags(struct device *d,
			  struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;

	return sprintf(buf, "0x%04X\n", priv->active_rxon.flags);
}

static ssize_t store_flags(struct device *d,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	u32 flags = simple_strtoul(buf, NULL, 0);

	mutex_lock(&priv->mutex);
	if (le32_to_cpu(priv->staging_rxon.flags) != flags) {
		/* Cancel any currently running scans... */
		if (iwl_scan_cancel_timeout(priv, 100))
			IWL_WARNING("Could not cancel scan.\n");
		else {
			IWL_DEBUG_INFO("Committing rxon.flags = 0x%04X\n",
				       flags);
			priv->staging_rxon.flags = cpu_to_le32(flags);
			iwl4965_commit_rxon(priv);
		}
	}
	mutex_unlock(&priv->mutex);

	return count;
}

static DEVICE_ATTR(flags, S_IWUSR | S_IRUGO, show_flags, store_flags);

static ssize_t show_filter_flags(struct device *d,
				 struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;

	return sprintf(buf, "0x%04X\n",
		le32_to_cpu(priv->active_rxon.filter_flags));
}

static ssize_t store_filter_flags(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	u32 filter_flags = simple_strtoul(buf, NULL, 0);

	mutex_lock(&priv->mutex);
	if (le32_to_cpu(priv->staging_rxon.filter_flags) != filter_flags) {
		/* Cancel any currently running scans... */
		if (iwl_scan_cancel_timeout(priv, 100))
			IWL_WARNING("Could not cancel scan.\n");
		else {
			IWL_DEBUG_INFO("Committing rxon.filter_flags = "
				       "0x%04X\n", filter_flags);
			priv->staging_rxon.filter_flags =
				cpu_to_le32(filter_flags);
			iwl4965_commit_rxon(priv);
		}
	}
	mutex_unlock(&priv->mutex);

	return count;
}

static DEVICE_ATTR(filter_flags, S_IWUSR | S_IRUGO, show_filter_flags,
		   store_filter_flags);

#ifdef CONFIG_IWLAGN_SPECTRUM_MEASUREMENT

static ssize_t show_measurement(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	struct iwl4965_spectrum_notification measure_report;
	u32 size = sizeof(measure_report), len = 0, ofs = 0;
	u8 *data = (u8 *)&measure_report;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (!(priv->measurement_status & MEASUREMENT_READY)) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return 0;
	}
	memcpy(&measure_report, &priv->measure_report, size);
	priv->measurement_status = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	while (size && (PAGE_SIZE - len)) {
		hex_dump_to_buffer(data + ofs, size, 16, 1, buf + len,
				   PAGE_SIZE - len, 1);
		len = strlen(buf);
		if (PAGE_SIZE - len)
			buf[len++] = '\n';

		ofs += 16;
		size -= min(size, 16U);
	}

	return len;
}

static ssize_t store_measurement(struct device *d,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	struct ieee80211_measurement_params params = {
		.channel = le16_to_cpu(priv->active_rxon.channel),
		.start_time = cpu_to_le64(priv->last_tsf),
		.duration = cpu_to_le16(1),
	};
	u8 type = IWL_MEASURE_BASIC;
	u8 buffer[32];
	u8 channel;

	if (count) {
		char *p = buffer;
		strncpy(buffer, buf, min(sizeof(buffer), count));
		channel = simple_strtoul(p, NULL, 0);
		if (channel)
			params.channel = channel;

		p = buffer;
		while (*p && *p != ' ')
			p++;
		if (*p)
			type = simple_strtoul(p + 1, NULL, 0);
	}

	IWL_DEBUG_INFO("Invoking measurement of type %d on "
		       "channel %d (for '%s')\n", type, params.channel, buf);
	iwl4965_get_measurement(priv, &params, type);

	return count;
}

static DEVICE_ATTR(measurement, S_IRUSR | S_IWUSR,
		   show_measurement, store_measurement);
#endif /* CONFIG_IWLAGN_SPECTRUM_MEASUREMENT */

static ssize_t store_retry_rate(struct device *d,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);

	priv->retry_rate = simple_strtoul(buf, NULL, 0);
	if (priv->retry_rate <= 0)
		priv->retry_rate = 1;

	return count;
}

static ssize_t show_retry_rate(struct device *d,
			       struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	return sprintf(buf, "%d", priv->retry_rate);
}

static DEVICE_ATTR(retry_rate, S_IWUSR | S_IRUSR, show_retry_rate,
		   store_retry_rate);

static ssize_t store_power_level(struct device *d,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	int ret;
	int mode;

	mode = simple_strtoul(buf, NULL, 0);
	mutex_lock(&priv->mutex);

	if (!iwl_is_ready(priv)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = iwl_power_set_user_mode(priv, mode);
	if (ret) {
		IWL_DEBUG_MAC80211("failed setting power mode.\n");
		goto out;
	}
	ret = count;

 out:
	mutex_unlock(&priv->mutex);
	return ret;
}

static ssize_t show_power_level(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	int mode = priv->power_data.user_power_setting;
	int system = priv->power_data.system_power_setting;
	int level = priv->power_data.power_mode;
	char *p = buf;

	switch (system) {
	case IWL_POWER_SYS_AUTO:
		p += sprintf(p, "SYSTEM:auto");
		break;
	case IWL_POWER_SYS_AC:
		p += sprintf(p, "SYSTEM:ac");
		break;
	case IWL_POWER_SYS_BATTERY:
		p += sprintf(p, "SYSTEM:battery");
		break;
	}

	p += sprintf(p, "\tMODE:%s", (mode < IWL_POWER_AUTO)?"fixed":"auto");
	p += sprintf(p, "\tINDEX:%d", level);
	p += sprintf(p, "\n");
	return p - buf + 1;
}

static DEVICE_ATTR(power_level, S_IWUSR | S_IRUSR, show_power_level,
		   store_power_level);

static ssize_t show_channels(struct device *d,
			     struct device_attribute *attr, char *buf)
{

	struct iwl_priv *priv = dev_get_drvdata(d);
	struct ieee80211_channel *channels = NULL;
	const struct ieee80211_supported_band *supp_band = NULL;
	int len = 0, i;
	int count = 0;

	if (!test_bit(STATUS_GEO_CONFIGURED, &priv->status))
		return -EAGAIN;

	supp_band = iwl_get_hw_mode(priv, IEEE80211_BAND_2GHZ);
	channels = supp_band->channels;
	count = supp_band->n_channels;

	len += sprintf(&buf[len],
			"Displaying %d channels in 2.4GHz band "
			"(802.11bg):\n", count);

	for (i = 0; i < count; i++)
		len += sprintf(&buf[len], "%d: %ddBm: BSS%s%s, %s.\n",
				ieee80211_frequency_to_channel(
				channels[i].center_freq),
				channels[i].max_power,
				channels[i].flags & IEEE80211_CHAN_RADAR ?
				" (IEEE 802.11h required)" : "",
				(!(channels[i].flags & IEEE80211_CHAN_NO_IBSS)
				|| (channels[i].flags &
				IEEE80211_CHAN_RADAR)) ? "" :
				", IBSS",
				channels[i].flags &
				IEEE80211_CHAN_PASSIVE_SCAN ?
				"passive only" : "active/passive");

	supp_band = iwl_get_hw_mode(priv, IEEE80211_BAND_5GHZ);
	channels = supp_band->channels;
	count = supp_band->n_channels;

	len += sprintf(&buf[len], "Displaying %d channels in 5.2GHz band "
			"(802.11a):\n", count);

	for (i = 0; i < count; i++)
		len += sprintf(&buf[len], "%d: %ddBm: BSS%s%s, %s.\n",
				ieee80211_frequency_to_channel(
				channels[i].center_freq),
				channels[i].max_power,
				channels[i].flags & IEEE80211_CHAN_RADAR ?
				" (IEEE 802.11h required)" : "",
				((channels[i].flags & IEEE80211_CHAN_NO_IBSS)
				|| (channels[i].flags &
				IEEE80211_CHAN_RADAR)) ? "" :
				", IBSS",
				channels[i].flags &
				IEEE80211_CHAN_PASSIVE_SCAN ?
				"passive only" : "active/passive");

	return len;
}

static DEVICE_ATTR(channels, S_IRUSR, show_channels, NULL);

static ssize_t show_statistics(struct device *d,
			       struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = dev_get_drvdata(d);
	u32 size = sizeof(struct iwl_notif_statistics);
	u32 len = 0, ofs = 0;
	u8 *data = (u8 *)&priv->statistics;
	int rc = 0;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	mutex_lock(&priv->mutex);
	rc = iwl_send_statistics_request(priv, 0);
	mutex_unlock(&priv->mutex);

	if (rc) {
		len = sprintf(buf,
			      "Error sending statistics request: 0x%08X\n", rc);
		return len;
	}

	while (size && (PAGE_SIZE - len)) {
		hex_dump_to_buffer(data + ofs, size, 16, 1, buf + len,
				   PAGE_SIZE - len, 1);
		len = strlen(buf);
		if (PAGE_SIZE - len)
			buf[len++] = '\n';

		ofs += 16;
		size -= min(size, 16U);
	}

	return len;
}

static DEVICE_ATTR(statistics, S_IRUGO, show_statistics, NULL);

static ssize_t show_status(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct iwl_priv *priv = (struct iwl_priv *)d->driver_data;
	if (!iwl_is_alive(priv))
		return -EAGAIN;
	return sprintf(buf, "0x%08x\n", (int)priv->status);
}

static DEVICE_ATTR(status, S_IRUGO, show_status, NULL);

/*****************************************************************************
 *
 * driver setup and teardown
 *
 *****************************************************************************/

static void iwl_setup_deferred_work(struct iwl_priv *priv)
{
	priv->workqueue = create_workqueue(DRV_NAME);

	init_waitqueue_head(&priv->wait_command_queue);

	INIT_WORK(&priv->up, iwl4965_bg_up);
	INIT_WORK(&priv->restart, iwl4965_bg_restart);
	INIT_WORK(&priv->rx_replenish, iwl4965_bg_rx_replenish);
	INIT_WORK(&priv->rf_kill, iwl4965_bg_rf_kill);
	INIT_WORK(&priv->beacon_update, iwl4965_bg_beacon_update);
	INIT_WORK(&priv->set_monitor, iwl4965_bg_set_monitor);
	INIT_WORK(&priv->run_time_calib_work, iwl_bg_run_time_calib_work);
	INIT_DELAYED_WORK(&priv->init_alive_start, iwl_bg_init_alive_start);
	INIT_DELAYED_WORK(&priv->alive_start, iwl_bg_alive_start);

	/* FIXME : remove when resolved PENDING */
	INIT_WORK(&priv->scan_completed, iwl_bg_scan_completed);
	iwl_setup_scan_deferred_work(priv);

	if (priv->cfg->ops->lib->setup_deferred_work)
		priv->cfg->ops->lib->setup_deferred_work(priv);

	init_timer(&priv->statistics_periodic);
	priv->statistics_periodic.data = (unsigned long)priv;
	priv->statistics_periodic.function = iwl4965_bg_statistics_periodic;

	tasklet_init(&priv->irq_tasklet, (void (*)(unsigned long))
		     iwl4965_irq_tasklet, (unsigned long)priv);
}

static void iwl_cancel_deferred_work(struct iwl_priv *priv)
{
	if (priv->cfg->ops->lib->cancel_deferred_work)
		priv->cfg->ops->lib->cancel_deferred_work(priv);

	cancel_delayed_work_sync(&priv->init_alive_start);
	cancel_delayed_work(&priv->scan_check);
	cancel_delayed_work(&priv->alive_start);
	cancel_work_sync(&priv->beacon_update);
	del_timer_sync(&priv->statistics_periodic);
}

static struct attribute *iwl4965_sysfs_entries[] = {
	&dev_attr_channels.attr,
	&dev_attr_flags.attr,
	&dev_attr_filter_flags.attr,
#ifdef CONFIG_IWLAGN_SPECTRUM_MEASUREMENT
	&dev_attr_measurement.attr,
#endif
	&dev_attr_power_level.attr,
	&dev_attr_retry_rate.attr,
	&dev_attr_statistics.attr,
	&dev_attr_status.attr,
	&dev_attr_temperature.attr,
	&dev_attr_tx_power.attr,
#ifdef CONFIG_IWLWIFI_DEBUG
	&dev_attr_debug_level.attr,
#endif
	&dev_attr_version.attr,

	NULL
};

static struct attribute_group iwl4965_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = iwl4965_sysfs_entries,
};

static struct ieee80211_ops iwl4965_hw_ops = {
	.tx = iwl4965_mac_tx,
	.start = iwl4965_mac_start,
	.stop = iwl4965_mac_stop,
	.add_interface = iwl4965_mac_add_interface,
	.remove_interface = iwl4965_mac_remove_interface,
	.config = iwl4965_mac_config,
	.config_interface = iwl4965_mac_config_interface,
	.configure_filter = iwl4965_configure_filter,
	.set_key = iwl4965_mac_set_key,
	.update_tkip_key = iwl4965_mac_update_tkip_key,
	.get_stats = iwl4965_mac_get_stats,
	.get_tx_stats = iwl4965_mac_get_tx_stats,
	.conf_tx = iwl4965_mac_conf_tx,
	.reset_tsf = iwl4965_mac_reset_tsf,
	.bss_info_changed = iwl4965_bss_info_changed,
	.ampdu_action = iwl4965_mac_ampdu_action,
	.hw_scan = iwl_mac_hw_scan
};

static int iwl4965_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = 0;
	struct iwl_priv *priv;
	struct ieee80211_hw *hw;
	struct iwl_cfg *cfg = (struct iwl_cfg *)(ent->driver_data);
	unsigned long flags;
	DECLARE_MAC_BUF(mac);

	/************************
	 * 1. Allocating HW data
	 ************************/

	/* Disabling hardware scan means that mac80211 will perform scans
	 * "the hard way", rather than using device's scan. */
	if (cfg->mod_params->disable_hw_scan) {
		if (cfg->mod_params->debug & IWL_DL_INFO)
			dev_printk(KERN_DEBUG, &(pdev->dev),
				   "Disabling hw_scan\n");
		iwl4965_hw_ops.hw_scan = NULL;
	}

	hw = iwl_alloc_all(cfg, &iwl4965_hw_ops);
	if (!hw) {
		err = -ENOMEM;
		goto out;
	}
	priv = hw->priv;
	/* At this point both hw and priv are allocated. */

	SET_IEEE80211_DEV(hw, &pdev->dev);

	IWL_DEBUG_INFO("*** LOAD DRIVER ***\n");
	priv->cfg = cfg;
	priv->pci_dev = pdev;

#ifdef CONFIG_IWLWIFI_DEBUG
	priv->debug_level = priv->cfg->mod_params->debug;
	atomic_set(&priv->restrict_refcnt, 0);
#endif

	/**************************
	 * 2. Initializing PCI bus
	 **************************/
	if (pci_enable_device(pdev)) {
		err = -ENODEV;
		goto out_ieee80211_free_hw;
	}

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_64BIT_MASK);
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (!err)
			err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
		/* both attempts failed: */
		if (err) {
			printk(KERN_WARNING "%s: No suitable DMA available.\n",
				DRV_NAME);
			goto out_pci_disable_device;
		}
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err)
		goto out_pci_disable_device;

	pci_set_drvdata(pdev, priv);

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config_byte(pdev, 0x41, 0x00);

	/***********************
	 * 3. Read REV register
	 ***********************/
	priv->hw_base = pci_iomap(pdev, 0, 0);
	if (!priv->hw_base) {
		err = -ENODEV;
		goto out_pci_release_regions;
	}

	IWL_DEBUG_INFO("pci_resource_len = 0x%08llx\n",
		(unsigned long long) pci_resource_len(pdev, 0));
	IWL_DEBUG_INFO("pci_resource_base = %p\n", priv->hw_base);

	iwl_hw_detect(priv);
	printk(KERN_INFO DRV_NAME
		": Detected Intel Wireless WiFi Link %s REV=0x%X\n",
		priv->cfg->name, priv->hw_rev);

	/* amp init */
	err = priv->cfg->ops->lib->apm_ops.init(priv);
	if (err < 0) {
		IWL_DEBUG_INFO("Failed to init APMG\n");
		goto out_iounmap;
	}
	/*****************
	 * 4. Read EEPROM
	 *****************/
	/* Read the EEPROM */
	err = iwl_eeprom_init(priv);
	if (err) {
		IWL_ERROR("Unable to init EEPROM\n");
		goto out_iounmap;
	}
	err = iwl_eeprom_check_version(priv);
	if (err)
		goto out_iounmap;

	/* extract MAC Address */
	iwl_eeprom_get_mac(priv, priv->mac_addr);
	IWL_DEBUG_INFO("MAC address: %s\n", print_mac(mac, priv->mac_addr));
	SET_IEEE80211_PERM_ADDR(priv->hw, priv->mac_addr);

	/************************
	 * 5. Setup HW constants
	 ************************/
	if (iwl_set_hw_params(priv)) {
		IWL_ERROR("failed to set hw parameters\n");
		goto out_free_eeprom;
	}

	/*******************
	 * 6. Setup priv
	 *******************/

	err = iwl_init_drv(priv);
	if (err)
		goto out_free_eeprom;
	/* At this point both hw and priv are initialized. */

	/**********************************
	 * 7. Initialize module parameters
	 **********************************/

	/* Disable radio (SW RF KILL) via parameter when loading driver */
	if (priv->cfg->mod_params->disable) {
		set_bit(STATUS_RF_KILL_SW, &priv->status);
		IWL_DEBUG_INFO("Radio disabled.\n");
	}

	/********************
	 * 8. Setup services
	 ********************/
	spin_lock_irqsave(&priv->lock, flags);
	iwl4965_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	err = sysfs_create_group(&pdev->dev.kobj, &iwl4965_attribute_group);
	if (err) {
		IWL_ERROR("failed to create sysfs device attributes\n");
		goto out_uninit_drv;
	}


	iwl_setup_deferred_work(priv);
	iwl_setup_rx_handlers(priv);

	/********************
	 * 9. Conclude
	 ********************/
	pci_save_state(pdev);
	pci_disable_device(pdev);

	/**********************************
	 * 10. Setup and register mac80211
	 **********************************/

	err = iwl_setup_mac(priv);
	if (err)
		goto out_remove_sysfs;

	err = iwl_dbgfs_register(priv, DRV_NAME);
	if (err)
		IWL_ERROR("failed to create debugfs files\n");

	err = iwl_rfkill_init(priv);
	if (err)
		IWL_ERROR("Unable to initialize RFKILL system. "
				  "Ignoring error: %d\n", err);
	iwl_power_initialize(priv);
	return 0;

 out_remove_sysfs:
	sysfs_remove_group(&pdev->dev.kobj, &iwl4965_attribute_group);
 out_uninit_drv:
	iwl_uninit_drv(priv);
 out_free_eeprom:
	iwl_eeprom_free(priv);
 out_iounmap:
	pci_iounmap(pdev, priv->hw_base);
 out_pci_release_regions:
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
 out_pci_disable_device:
	pci_disable_device(pdev);
 out_ieee80211_free_hw:
	ieee80211_free_hw(priv->hw);
 out:
	return err;
}

static void __devexit iwl4965_pci_remove(struct pci_dev *pdev)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);
	unsigned long flags;

	if (!priv)
		return;

	IWL_DEBUG_INFO("*** UNLOAD DRIVER ***\n");

	iwl_dbgfs_unregister(priv);
	sysfs_remove_group(&pdev->dev.kobj, &iwl4965_attribute_group);

	if (priv->mac80211_registered) {
		ieee80211_unregister_hw(priv->hw);
		priv->mac80211_registered = 0;
	}

	set_bit(STATUS_EXIT_PENDING, &priv->status);

	iwl4965_down(priv);

	/* make sure we flush any pending irq or
	 * tasklet for the driver
	 */
	spin_lock_irqsave(&priv->lock, flags);
	iwl4965_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_synchronize_irq(priv);

	iwl_rfkill_unregister(priv);
	iwl4965_dealloc_ucode_pci(priv);

	if (priv->rxq.bd)
		iwl_rx_queue_free(priv, &priv->rxq);
	iwl_hw_txq_ctx_free(priv);

	iwl_clear_stations_table(priv);
	iwl_eeprom_free(priv);


	/*netif_stop_queue(dev); */
	flush_workqueue(priv->workqueue);

	/* ieee80211_unregister_hw calls iwl4965_mac_stop, which flushes
	 * priv->workqueue... so we can't take down the workqueue
	 * until now... */
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;

	pci_iounmap(pdev, priv->hw_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	iwl_uninit_drv(priv);

	if (priv->ibss_beacon)
		dev_kfree_skb(priv->ibss_beacon);

	ieee80211_free_hw(priv->hw);
}

#ifdef CONFIG_PM

static int iwl4965_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);

	if (priv->is_open) {
		set_bit(STATUS_IN_SUSPEND, &priv->status);
		iwl4965_mac_stop(priv->hw);
		priv->is_open = 1;
	}

	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int iwl4965_pci_resume(struct pci_dev *pdev)
{
	struct iwl_priv *priv = pci_get_drvdata(pdev);

	pci_set_power_state(pdev, PCI_D0);

	if (priv->is_open)
		iwl4965_mac_start(priv->hw);

	clear_bit(STATUS_IN_SUSPEND, &priv->status);
	return 0;
}

#endif /* CONFIG_PM */

/*****************************************************************************
 *
 * driver and module entry point
 *
 *****************************************************************************/

/* Hardware specific file defines the PCI IDs table for that hardware module */
static struct pci_device_id iwl_hw_card_ids[] = {
#ifdef CONFIG_IWL4965
	{IWL_PCI_DEVICE(0x4229, PCI_ANY_ID, iwl4965_agn_cfg)},
	{IWL_PCI_DEVICE(0x4230, PCI_ANY_ID, iwl4965_agn_cfg)},
#endif /* CONFIG_IWL4965 */
#ifdef CONFIG_IWL5000
	{IWL_PCI_DEVICE(0x4232, 0x1205, iwl5100_bg_cfg)},
	{IWL_PCI_DEVICE(0x4232, 0x1305, iwl5100_bg_cfg)},
	{IWL_PCI_DEVICE(0x4232, 0x1206, iwl5100_abg_cfg)},
	{IWL_PCI_DEVICE(0x4232, 0x1306, iwl5100_abg_cfg)},
	{IWL_PCI_DEVICE(0x4232, 0x1326, iwl5100_abg_cfg)},
	{IWL_PCI_DEVICE(0x4237, 0x1216, iwl5100_abg_cfg)},
	{IWL_PCI_DEVICE(0x4232, PCI_ANY_ID, iwl5100_agn_cfg)},
	{IWL_PCI_DEVICE(0x4235, PCI_ANY_ID, iwl5300_agn_cfg)},
	{IWL_PCI_DEVICE(0x4236, PCI_ANY_ID, iwl5300_agn_cfg)},
	{IWL_PCI_DEVICE(0x4237, PCI_ANY_ID, iwl5100_agn_cfg)},
	{IWL_PCI_DEVICE(0x423A, PCI_ANY_ID, iwl5350_agn_cfg)},
#endif /* CONFIG_IWL5000 */
	{0}
};
MODULE_DEVICE_TABLE(pci, iwl_hw_card_ids);

static struct pci_driver iwl_driver = {
	.name = DRV_NAME,
	.id_table = iwl_hw_card_ids,
	.probe = iwl4965_pci_probe,
	.remove = __devexit_p(iwl4965_pci_remove),
#ifdef CONFIG_PM
	.suspend = iwl4965_pci_suspend,
	.resume = iwl4965_pci_resume,
#endif
};

static int __init iwl4965_init(void)
{

	int ret;
	printk(KERN_INFO DRV_NAME ": " DRV_DESCRIPTION ", " DRV_VERSION "\n");
	printk(KERN_INFO DRV_NAME ": " DRV_COPYRIGHT "\n");

	ret = iwlagn_rate_control_register();
	if (ret) {
		IWL_ERROR("Unable to register rate control algorithm: %d\n", ret);
		return ret;
	}

	ret = pci_register_driver(&iwl_driver);
	if (ret) {
		IWL_ERROR("Unable to initialize PCI module\n");
		goto error_register;
	}

	return ret;

error_register:
	iwlagn_rate_control_unregister();
	return ret;
}

static void __exit iwl4965_exit(void)
{
	pci_unregister_driver(&iwl_driver);
	iwlagn_rate_control_unregister();
}

module_exit(iwl4965_exit);
module_init(iwl4965_init);
