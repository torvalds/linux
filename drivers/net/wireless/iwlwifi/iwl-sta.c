/******************************************************************************
 *
 * Copyright(c) 2003 - 2010 Intel Corporation. All rights reserved.
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

#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <linux/sched.h>
#include <linux/lockdep.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-sta.h"

/* priv->sta_lock must be held */
static void iwl_sta_ucode_activate(struct iwl_priv *priv, u8 sta_id)
{

	if (!(priv->stations[sta_id].used & IWL_STA_DRIVER_ACTIVE))
		IWL_ERR(priv, "ACTIVATE a non DRIVER active station id %u addr %pM\n",
			sta_id, priv->stations[sta_id].sta.sta.addr);

	if (priv->stations[sta_id].used & IWL_STA_UCODE_ACTIVE) {
		IWL_DEBUG_ASSOC(priv,
				"STA id %u addr %pM already present in uCode (according to driver)\n",
				sta_id, priv->stations[sta_id].sta.sta.addr);
	} else {
		priv->stations[sta_id].used |= IWL_STA_UCODE_ACTIVE;
		IWL_DEBUG_ASSOC(priv, "Added STA id %u addr %pM to uCode\n",
				sta_id, priv->stations[sta_id].sta.sta.addr);
	}
}

static int iwl_process_add_sta_resp(struct iwl_priv *priv,
				    struct iwl_addsta_cmd *addsta,
				    struct iwl_rx_packet *pkt,
				    bool sync)
{
	u8 sta_id = addsta->sta.sta_id;
	unsigned long flags;
	int ret = -EIO;

	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from REPLY_ADD_STA (0x%08X)\n",
			pkt->hdr.flags);
		return ret;
	}

	IWL_DEBUG_INFO(priv, "Processing response for adding station %u\n",
		       sta_id);

	spin_lock_irqsave(&priv->sta_lock, flags);

	switch (pkt->u.add_sta.status) {
	case ADD_STA_SUCCESS_MSK:
		IWL_DEBUG_INFO(priv, "REPLY_ADD_STA PASSED\n");
		iwl_sta_ucode_activate(priv, sta_id);
		ret = 0;
		break;
	case ADD_STA_NO_ROOM_IN_TABLE:
		IWL_ERR(priv, "Adding station %d failed, no room in table.\n",
			sta_id);
		break;
	case ADD_STA_NO_BLOCK_ACK_RESOURCE:
		IWL_ERR(priv, "Adding station %d failed, no block ack resource.\n",
			sta_id);
		break;
	case ADD_STA_MODIFY_NON_EXIST_STA:
		IWL_ERR(priv, "Attempting to modify non-existing station %d\n",
			sta_id);
		break;
	default:
		IWL_DEBUG_ASSOC(priv, "Received REPLY_ADD_STA:(0x%08X)\n",
				pkt->u.add_sta.status);
		break;
	}

	IWL_DEBUG_INFO(priv, "%s station id %u addr %pM\n",
		       priv->stations[sta_id].sta.mode ==
		       STA_CONTROL_MODIFY_MSK ?  "Modified" : "Added",
		       sta_id, priv->stations[sta_id].sta.sta.addr);

	/*
	 * XXX: The MAC address in the command buffer is often changed from
	 * the original sent to the device. That is, the MAC address
	 * written to the command buffer often is not the same MAC adress
	 * read from the command buffer when the command returns. This
	 * issue has not yet been resolved and this debugging is left to
	 * observe the problem.
	 */
	IWL_DEBUG_INFO(priv, "%s station according to cmd buffer %pM\n",
		       priv->stations[sta_id].sta.mode ==
		       STA_CONTROL_MODIFY_MSK ? "Modified" : "Added",
		       addsta->sta.addr);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}

static void iwl_add_sta_callback(struct iwl_priv *priv,
				 struct iwl_device_cmd *cmd,
				 struct iwl_rx_packet *pkt)
{
	struct iwl_addsta_cmd *addsta =
		(struct iwl_addsta_cmd *)cmd->cmd.payload;

	iwl_process_add_sta_resp(priv, addsta, pkt, false);

}

int iwl_send_add_sta(struct iwl_priv *priv,
		     struct iwl_addsta_cmd *sta, u8 flags)
{
	struct iwl_rx_packet *pkt = NULL;
	int ret = 0;
	u8 data[sizeof(*sta)];
	struct iwl_host_cmd cmd = {
		.id = REPLY_ADD_STA,
		.flags = flags,
		.data = data,
	};
	u8 sta_id __maybe_unused = sta->sta.sta_id;

	IWL_DEBUG_INFO(priv, "Adding sta %u (%pM) %ssynchronously\n",
		       sta_id, sta->sta.addr, flags & CMD_ASYNC ?  "a" : "");

	if (flags & CMD_ASYNC)
		cmd.callback = iwl_add_sta_callback;
	else {
		cmd.flags |= CMD_WANT_SKB;
		might_sleep();
	}

	cmd.len = priv->cfg->ops->utils->build_addsta_hcmd(sta, data);
	ret = iwl_send_cmd(priv, &cmd);

	if (ret || (flags & CMD_ASYNC))
		return ret;

	if (ret == 0) {
		pkt = (struct iwl_rx_packet *)cmd.reply_page;
		ret = iwl_process_add_sta_resp(priv, sta, pkt, true);
	}
	iwl_free_pages(priv, cmd.reply_page);

	return ret;
}
EXPORT_SYMBOL(iwl_send_add_sta);

static void iwl_set_ht_add_station(struct iwl_priv *priv, u8 index,
				   struct ieee80211_sta *sta,
				   struct iwl_rxon_context *ctx)
{
	struct ieee80211_sta_ht_cap *sta_ht_inf = &sta->ht_cap;
	__le32 sta_flags;
	u8 mimo_ps_mode;

	if (!sta || !sta_ht_inf->ht_supported)
		goto done;

	mimo_ps_mode = (sta_ht_inf->cap & IEEE80211_HT_CAP_SM_PS) >> 2;
	IWL_DEBUG_ASSOC(priv, "spatial multiplexing power save mode: %s\n",
			(mimo_ps_mode == WLAN_HT_CAP_SM_PS_STATIC) ?
			"static" :
			(mimo_ps_mode == WLAN_HT_CAP_SM_PS_DYNAMIC) ?
			"dynamic" : "disabled");

	sta_flags = priv->stations[index].sta.station_flags;

	sta_flags &= ~(STA_FLG_RTS_MIMO_PROT_MSK | STA_FLG_MIMO_DIS_MSK);

	switch (mimo_ps_mode) {
	case WLAN_HT_CAP_SM_PS_STATIC:
		sta_flags |= STA_FLG_MIMO_DIS_MSK;
		break;
	case WLAN_HT_CAP_SM_PS_DYNAMIC:
		sta_flags |= STA_FLG_RTS_MIMO_PROT_MSK;
		break;
	case WLAN_HT_CAP_SM_PS_DISABLED:
		break;
	default:
		IWL_WARN(priv, "Invalid MIMO PS mode %d\n", mimo_ps_mode);
		break;
	}

	sta_flags |= cpu_to_le32(
	      (u32)sta_ht_inf->ampdu_factor << STA_FLG_MAX_AGG_SIZE_POS);

	sta_flags |= cpu_to_le32(
	      (u32)sta_ht_inf->ampdu_density << STA_FLG_AGG_MPDU_DENSITY_POS);

	if (iwl_is_ht40_tx_allowed(priv, ctx, &sta->ht_cap))
		sta_flags |= STA_FLG_HT40_EN_MSK;
	else
		sta_flags &= ~STA_FLG_HT40_EN_MSK;

	priv->stations[index].sta.station_flags = sta_flags;
 done:
	return;
}

/**
 * iwl_prep_station - Prepare station information for addition
 *
 * should be called with sta_lock held
 */
u8 iwl_prep_station(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
		    const u8 *addr, bool is_ap, struct ieee80211_sta *sta)
{
	struct iwl_station_entry *station;
	int i;
	u8 sta_id = IWL_INVALID_STATION;
	u16 rate;

	if (is_ap)
		sta_id = ctx->ap_sta_id;
	else if (is_broadcast_ether_addr(addr))
		sta_id = ctx->bcast_sta_id;
	else
		for (i = IWL_STA_ID; i < priv->hw_params.max_stations; i++) {
			if (!compare_ether_addr(priv->stations[i].sta.sta.addr,
						addr)) {
				sta_id = i;
				break;
			}

			if (!priv->stations[i].used &&
			    sta_id == IWL_INVALID_STATION)
				sta_id = i;
		}

	/*
	 * These two conditions have the same outcome, but keep them
	 * separate
	 */
	if (unlikely(sta_id == IWL_INVALID_STATION))
		return sta_id;

	/*
	 * uCode is not able to deal with multiple requests to add a
	 * station. Keep track if one is in progress so that we do not send
	 * another.
	 */
	if (priv->stations[sta_id].used & IWL_STA_UCODE_INPROGRESS) {
		IWL_DEBUG_INFO(priv, "STA %d already in process of being added.\n",
				sta_id);
		return sta_id;
	}

	if ((priv->stations[sta_id].used & IWL_STA_DRIVER_ACTIVE) &&
	    (priv->stations[sta_id].used & IWL_STA_UCODE_ACTIVE) &&
	    !compare_ether_addr(priv->stations[sta_id].sta.sta.addr, addr)) {
		IWL_DEBUG_ASSOC(priv, "STA %d (%pM) already added, not adding again.\n",
				sta_id, addr);
		return sta_id;
	}

	station = &priv->stations[sta_id];
	station->used = IWL_STA_DRIVER_ACTIVE;
	IWL_DEBUG_ASSOC(priv, "Add STA to driver ID %d: %pM\n",
			sta_id, addr);
	priv->num_stations++;

	/* Set up the REPLY_ADD_STA command to send to device */
	memset(&station->sta, 0, sizeof(struct iwl_addsta_cmd));
	memcpy(station->sta.sta.addr, addr, ETH_ALEN);
	station->sta.mode = 0;
	station->sta.sta.sta_id = sta_id;
	station->sta.station_flags = ctx->station_flags;
	station->ctxid = ctx->ctxid;

	if (sta) {
		struct iwl_station_priv_common *sta_priv;

		sta_priv = (void *)sta->drv_priv;
		sta_priv->ctx = ctx;
	}

	/*
	 * OK to call unconditionally, since local stations (IBSS BSSID
	 * STA and broadcast STA) pass in a NULL sta, and mac80211
	 * doesn't allow HT IBSS.
	 */
	iwl_set_ht_add_station(priv, sta_id, sta, ctx);

	/* 3945 only */
	rate = (priv->band == IEEE80211_BAND_5GHZ) ?
		IWL_RATE_6M_PLCP : IWL_RATE_1M_PLCP;
	/* Turn on both antennas for the station... */
	station->sta.rate_n_flags = cpu_to_le16(rate | RATE_MCS_ANT_AB_MSK);

	return sta_id;

}
EXPORT_SYMBOL_GPL(iwl_prep_station);

#define STA_WAIT_TIMEOUT (HZ/2)

/**
 * iwl_add_station_common -
 */
int iwl_add_station_common(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
			   const u8 *addr, bool is_ap,
			   struct ieee80211_sta *sta, u8 *sta_id_r)
{
	unsigned long flags_spin;
	int ret = 0;
	u8 sta_id;
	struct iwl_addsta_cmd sta_cmd;

	*sta_id_r = 0;
	spin_lock_irqsave(&priv->sta_lock, flags_spin);
	sta_id = iwl_prep_station(priv, ctx, addr, is_ap, sta);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Unable to prepare station %pM for addition\n",
			addr);
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return -EINVAL;
	}

	/*
	 * uCode is not able to deal with multiple requests to add a
	 * station. Keep track if one is in progress so that we do not send
	 * another.
	 */
	if (priv->stations[sta_id].used & IWL_STA_UCODE_INPROGRESS) {
		IWL_DEBUG_INFO(priv, "STA %d already in process of being added.\n",
			       sta_id);
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return -EEXIST;
	}

	if ((priv->stations[sta_id].used & IWL_STA_DRIVER_ACTIVE) &&
	    (priv->stations[sta_id].used & IWL_STA_UCODE_ACTIVE)) {
		IWL_DEBUG_ASSOC(priv, "STA %d (%pM) already added, not adding again.\n",
				sta_id, addr);
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
		return -EEXIST;
	}

	priv->stations[sta_id].used |= IWL_STA_UCODE_INPROGRESS;
	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags_spin);

	/* Add station to device's station table */
	ret = iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
	if (ret) {
		spin_lock_irqsave(&priv->sta_lock, flags_spin);
		IWL_ERR(priv, "Adding station %pM failed.\n",
			priv->stations[sta_id].sta.sta.addr);
		priv->stations[sta_id].used &= ~IWL_STA_DRIVER_ACTIVE;
		priv->stations[sta_id].used &= ~IWL_STA_UCODE_INPROGRESS;
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
	}
	*sta_id_r = sta_id;
	return ret;
}
EXPORT_SYMBOL(iwl_add_station_common);

/**
 * iwl_sta_ucode_deactivate - deactivate ucode status for a station
 *
 * priv->sta_lock must be held
 */
static void iwl_sta_ucode_deactivate(struct iwl_priv *priv, u8 sta_id)
{
	/* Ucode must be active and driver must be non active */
	if ((priv->stations[sta_id].used &
	     (IWL_STA_UCODE_ACTIVE | IWL_STA_DRIVER_ACTIVE)) != IWL_STA_UCODE_ACTIVE)
		IWL_ERR(priv, "removed non active STA %u\n", sta_id);

	priv->stations[sta_id].used &= ~IWL_STA_UCODE_ACTIVE;

	memset(&priv->stations[sta_id], 0, sizeof(struct iwl_station_entry));
	IWL_DEBUG_ASSOC(priv, "Removed STA %u\n", sta_id);
}

static int iwl_send_remove_station(struct iwl_priv *priv,
				   const u8 *addr, int sta_id)
{
	struct iwl_rx_packet *pkt;
	int ret;

	unsigned long flags_spin;
	struct iwl_rem_sta_cmd rm_sta_cmd;

	struct iwl_host_cmd cmd = {
		.id = REPLY_REMOVE_STA,
		.len = sizeof(struct iwl_rem_sta_cmd),
		.flags = CMD_SYNC,
		.data = &rm_sta_cmd,
	};

	memset(&rm_sta_cmd, 0, sizeof(rm_sta_cmd));
	rm_sta_cmd.num_sta = 1;
	memcpy(&rm_sta_cmd.addr, addr, ETH_ALEN);

	cmd.flags |= CMD_WANT_SKB;

	ret = iwl_send_cmd(priv, &cmd);

	if (ret)
		return ret;

	pkt = (struct iwl_rx_packet *)cmd.reply_page;
	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from REPLY_REMOVE_STA (0x%08X)\n",
			  pkt->hdr.flags);
		ret = -EIO;
	}

	if (!ret) {
		switch (pkt->u.rem_sta.status) {
		case REM_STA_SUCCESS_MSK:
			spin_lock_irqsave(&priv->sta_lock, flags_spin);
			iwl_sta_ucode_deactivate(priv, sta_id);
			spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
			IWL_DEBUG_ASSOC(priv, "REPLY_REMOVE_STA PASSED\n");
			break;
		default:
			ret = -EIO;
			IWL_ERR(priv, "REPLY_REMOVE_STA failed\n");
			break;
		}
	}
	iwl_free_pages(priv, cmd.reply_page);

	return ret;
}

/**
 * iwl_remove_station - Remove driver's knowledge of station.
 */
int iwl_remove_station(struct iwl_priv *priv, const u8 sta_id,
		       const u8 *addr)
{
	unsigned long flags;

	if (!iwl_is_ready(priv)) {
		IWL_DEBUG_INFO(priv,
			"Unable to remove station %pM, device not ready.\n",
			addr);
		/*
		 * It is typical for stations to be removed when we are
		 * going down. Return success since device will be down
		 * soon anyway
		 */
		return 0;
	}

	IWL_DEBUG_ASSOC(priv, "Removing STA from driver:%d  %pM\n",
			sta_id, addr);

	if (WARN_ON(sta_id == IWL_INVALID_STATION))
		return -EINVAL;

	spin_lock_irqsave(&priv->sta_lock, flags);

	if (!(priv->stations[sta_id].used & IWL_STA_DRIVER_ACTIVE)) {
		IWL_DEBUG_INFO(priv, "Removing %pM but non DRIVER active\n",
				addr);
		goto out_err;
	}

	if (!(priv->stations[sta_id].used & IWL_STA_UCODE_ACTIVE)) {
		IWL_DEBUG_INFO(priv, "Removing %pM but non UCODE active\n",
				addr);
		goto out_err;
	}

	if (priv->stations[sta_id].used & IWL_STA_LOCAL) {
		kfree(priv->stations[sta_id].lq);
		priv->stations[sta_id].lq = NULL;
	}

	priv->stations[sta_id].used &= ~IWL_STA_DRIVER_ACTIVE;

	priv->num_stations--;

	BUG_ON(priv->num_stations < 0);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_remove_station(priv, addr, sta_id);
out_err:
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(iwl_remove_station);

/**
 * iwl_clear_ucode_stations - clear ucode station table bits
 *
 * This function clears all the bits in the driver indicating
 * which stations are active in the ucode. Call when something
 * other than explicit station management would cause this in
 * the ucode, e.g. unassociated RXON.
 */
void iwl_clear_ucode_stations(struct iwl_priv *priv,
			      struct iwl_rxon_context *ctx)
{
	int i;
	unsigned long flags_spin;
	bool cleared = false;

	IWL_DEBUG_INFO(priv, "Clearing ucode stations in driver\n");

	spin_lock_irqsave(&priv->sta_lock, flags_spin);
	for (i = 0; i < priv->hw_params.max_stations; i++) {
		if (ctx && ctx->ctxid != priv->stations[i].ctxid)
			continue;

		if (priv->stations[i].used & IWL_STA_UCODE_ACTIVE) {
			IWL_DEBUG_INFO(priv, "Clearing ucode active for station %d\n", i);
			priv->stations[i].used &= ~IWL_STA_UCODE_ACTIVE;
			cleared = true;
		}
	}
	spin_unlock_irqrestore(&priv->sta_lock, flags_spin);

	if (!cleared)
		IWL_DEBUG_INFO(priv, "No active stations found to be cleared\n");
}
EXPORT_SYMBOL(iwl_clear_ucode_stations);

/**
 * iwl_restore_stations() - Restore driver known stations to device
 *
 * All stations considered active by driver, but not present in ucode, is
 * restored.
 *
 * Function sleeps.
 */
void iwl_restore_stations(struct iwl_priv *priv, struct iwl_rxon_context *ctx)
{
	struct iwl_addsta_cmd sta_cmd;
	struct iwl_link_quality_cmd lq;
	unsigned long flags_spin;
	int i;
	bool found = false;
	int ret;
	bool send_lq;

	if (!iwl_is_ready(priv)) {
		IWL_DEBUG_INFO(priv, "Not ready yet, not restoring any stations.\n");
		return;
	}

	IWL_DEBUG_ASSOC(priv, "Restoring all known stations ... start.\n");
	spin_lock_irqsave(&priv->sta_lock, flags_spin);
	for (i = 0; i < priv->hw_params.max_stations; i++) {
		if (ctx->ctxid != priv->stations[i].ctxid)
			continue;
		if ((priv->stations[i].used & IWL_STA_DRIVER_ACTIVE) &&
			    !(priv->stations[i].used & IWL_STA_UCODE_ACTIVE)) {
			IWL_DEBUG_ASSOC(priv, "Restoring sta %pM\n",
					priv->stations[i].sta.sta.addr);
			priv->stations[i].sta.mode = 0;
			priv->stations[i].used |= IWL_STA_UCODE_INPROGRESS;
			found = true;
		}
	}

	for (i = 0; i < priv->hw_params.max_stations; i++) {
		if ((priv->stations[i].used & IWL_STA_UCODE_INPROGRESS)) {
			memcpy(&sta_cmd, &priv->stations[i].sta,
			       sizeof(struct iwl_addsta_cmd));
			send_lq = false;
			if (priv->stations[i].lq) {
				memcpy(&lq, priv->stations[i].lq,
				       sizeof(struct iwl_link_quality_cmd));
				send_lq = true;
			}
			spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
			ret = iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
			if (ret) {
				spin_lock_irqsave(&priv->sta_lock, flags_spin);
				IWL_ERR(priv, "Adding station %pM failed.\n",
					priv->stations[i].sta.sta.addr);
				priv->stations[i].used &= ~IWL_STA_DRIVER_ACTIVE;
				priv->stations[i].used &= ~IWL_STA_UCODE_INPROGRESS;
				spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
			}
			/*
			 * Rate scaling has already been initialized, send
			 * current LQ command
			 */
			if (send_lq)
				iwl_send_lq_cmd(priv, ctx, &lq, CMD_SYNC, true);
			spin_lock_irqsave(&priv->sta_lock, flags_spin);
			priv->stations[i].used &= ~IWL_STA_UCODE_INPROGRESS;
		}
	}

	spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
	if (!found)
		IWL_DEBUG_INFO(priv, "Restoring all known stations .... no stations to be restored.\n");
	else
		IWL_DEBUG_INFO(priv, "Restoring all known stations .... complete.\n");
}
EXPORT_SYMBOL(iwl_restore_stations);

int iwl_get_free_ucode_key_index(struct iwl_priv *priv)
{
	int i;

	for (i = 0; i < priv->sta_key_max_num; i++)
		if (!test_and_set_bit(i, &priv->ucode_key_table))
			return i;

	return WEP_INVALID_OFFSET;
}
EXPORT_SYMBOL(iwl_get_free_ucode_key_index);

void iwl_dealloc_bcast_stations(struct iwl_priv *priv)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&priv->sta_lock, flags);
	for (i = 0; i < priv->hw_params.max_stations; i++) {
		if (!(priv->stations[i].used & IWL_STA_BCAST))
			continue;

		priv->stations[i].used &= ~IWL_STA_UCODE_ACTIVE;
		priv->num_stations--;
		BUG_ON(priv->num_stations < 0);
		kfree(priv->stations[i].lq);
		priv->stations[i].lq = NULL;
	}
	spin_unlock_irqrestore(&priv->sta_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_dealloc_bcast_stations);

#ifdef CONFIG_IWLWIFI_DEBUG
static void iwl_dump_lq_cmd(struct iwl_priv *priv,
			   struct iwl_link_quality_cmd *lq)
{
	int i;
	IWL_DEBUG_RATE(priv, "lq station id 0x%x\n", lq->sta_id);
	IWL_DEBUG_RATE(priv, "lq ant 0x%X 0x%X\n",
		       lq->general_params.single_stream_ant_msk,
		       lq->general_params.dual_stream_ant_msk);

	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		IWL_DEBUG_RATE(priv, "lq index %d 0x%X\n",
			       i, lq->rs_table[i].rate_n_flags);
}
#else
static inline void iwl_dump_lq_cmd(struct iwl_priv *priv,
				   struct iwl_link_quality_cmd *lq)
{
}
#endif

/**
 * is_lq_table_valid() - Test one aspect of LQ cmd for validity
 *
 * It sometimes happens when a HT rate has been in use and we
 * loose connectivity with AP then mac80211 will first tell us that the
 * current channel is not HT anymore before removing the station. In such a
 * scenario the RXON flags will be updated to indicate we are not
 * communicating HT anymore, but the LQ command may still contain HT rates.
 * Test for this to prevent driver from sending LQ command between the time
 * RXON flags are updated and when LQ command is updated.
 */
static bool is_lq_table_valid(struct iwl_priv *priv,
			      struct iwl_rxon_context *ctx,
			      struct iwl_link_quality_cmd *lq)
{
	int i;

	if (ctx->ht.enabled)
		return true;

	IWL_DEBUG_INFO(priv, "Channel %u is not an HT channel\n",
		       ctx->active.channel);
	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++) {
		if (le32_to_cpu(lq->rs_table[i].rate_n_flags) & RATE_MCS_HT_MSK) {
			IWL_DEBUG_INFO(priv,
				       "index %d of LQ expects HT channel\n",
				       i);
			return false;
		}
	}
	return true;
}

/**
 * iwl_send_lq_cmd() - Send link quality command
 * @init: This command is sent as part of station initialization right
 *        after station has been added.
 *
 * The link quality command is sent as the last step of station creation.
 * This is the special case in which init is set and we call a callback in
 * this case to clear the state indicating that station creation is in
 * progress.
 */
int iwl_send_lq_cmd(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
		    struct iwl_link_quality_cmd *lq, u8 flags, bool init)
{
	int ret = 0;
	unsigned long flags_spin;

	struct iwl_host_cmd cmd = {
		.id = REPLY_TX_LINK_QUALITY_CMD,
		.len = sizeof(struct iwl_link_quality_cmd),
		.flags = flags,
		.data = lq,
	};

	if (WARN_ON(lq->sta_id == IWL_INVALID_STATION))
		return -EINVAL;

	iwl_dump_lq_cmd(priv, lq);
	BUG_ON(init && (cmd.flags & CMD_ASYNC));

	if (is_lq_table_valid(priv, ctx, lq))
		ret = iwl_send_cmd(priv, &cmd);
	else
		ret = -EINVAL;

	if (cmd.flags & CMD_ASYNC)
		return ret;

	if (init) {
		IWL_DEBUG_INFO(priv, "init LQ command complete, clearing sta addition status for sta %d\n",
			       lq->sta_id);
		spin_lock_irqsave(&priv->sta_lock, flags_spin);
		priv->stations[lq->sta_id].used &= ~IWL_STA_UCODE_INPROGRESS;
		spin_unlock_irqrestore(&priv->sta_lock, flags_spin);
	}
	return ret;
}
EXPORT_SYMBOL(iwl_send_lq_cmd);

int iwl_mac_sta_remove(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_station_priv_common *sta_common = (void *)sta->drv_priv;
	int ret;

	IWL_DEBUG_INFO(priv, "received request to remove station %pM\n",
			sta->addr);
	mutex_lock(&priv->mutex);
	IWL_DEBUG_INFO(priv, "proceeding to remove station %pM\n",
			sta->addr);
	ret = iwl_remove_station(priv, sta_common->sta_id, sta->addr);
	if (ret)
		IWL_ERR(priv, "Error removing station %pM\n",
			sta->addr);
	mutex_unlock(&priv->mutex);
	return ret;
}
EXPORT_SYMBOL(iwl_mac_sta_remove);
