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
				   struct ieee80211_sta_ht_cap *sta_ht_inf)
{
	__le32 sta_flags;
	u8 mimo_ps_mode;

	if (!sta_ht_inf || !sta_ht_inf->ht_supported)
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

	if (iwl_is_ht40_tx_allowed(priv, sta_ht_inf))
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
static u8 iwl_prep_station(struct iwl_priv *priv, const u8 *addr,
			   bool is_ap,
			   struct ieee80211_sta_ht_cap *ht_info)
{
	struct iwl_station_entry *station;
	int i;
	u8 sta_id = IWL_INVALID_STATION;
	u16 rate;

	if (is_ap)
		sta_id = IWL_AP_ID;
	else if (is_broadcast_ether_addr(addr))
		sta_id = priv->hw_params.bcast_sta_id;
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
	station->sta.station_flags = 0;

	/*
	 * OK to call unconditionally, since local stations (IBSS BSSID
	 * STA and broadcast STA) pass in a NULL ht_info, and mac80211
	 * doesn't allow HT IBSS.
	 */
	iwl_set_ht_add_station(priv, sta_id, ht_info);

	/* 3945 only */
	rate = (priv->band == IEEE80211_BAND_5GHZ) ?
		IWL_RATE_6M_PLCP : IWL_RATE_1M_PLCP;
	/* Turn on both antennas for the station... */
	station->sta.rate_n_flags = cpu_to_le16(rate | RATE_MCS_ANT_AB_MSK);

	return sta_id;

}

#define STA_WAIT_TIMEOUT (HZ/2)

/**
 * iwl_add_station_common -
 */
int iwl_add_station_common(struct iwl_priv *priv, const u8 *addr,
				  bool is_ap,
				  struct ieee80211_sta_ht_cap *ht_info,
				  u8 *sta_id_r)
{
	unsigned long flags_spin;
	int ret = 0;
	u8 sta_id;
	struct iwl_addsta_cmd sta_cmd;

	*sta_id_r = 0;
	spin_lock_irqsave(&priv->sta_lock, flags_spin);
	sta_id = iwl_prep_station(priv, addr, is_ap, ht_info);
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

static struct iwl_link_quality_cmd *iwl_sta_alloc_lq(struct iwl_priv *priv,
						     u8 sta_id)
{
	int i, r;
	struct iwl_link_quality_cmd *link_cmd;
	u32 rate_flags;

	link_cmd = kzalloc(sizeof(struct iwl_link_quality_cmd), GFP_KERNEL);
	if (!link_cmd) {
		IWL_ERR(priv, "Unable to allocate memory for LQ cmd.\n");
		return NULL;
	}
	/* Set up the rate scaling to start at selected rate, fall back
	 * all the way down to 1M in IEEE order, and then spin on 1M */
	if (priv->band == IEEE80211_BAND_5GHZ)
		r = IWL_RATE_6M_INDEX;
	else
		r = IWL_RATE_1M_INDEX;

	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++) {
		rate_flags = 0;
		if (r >= IWL_FIRST_CCK_RATE && r <= IWL_LAST_CCK_RATE)
			rate_flags |= RATE_MCS_CCK_MSK;

		rate_flags |= first_antenna(priv->hw_params.valid_tx_ant) <<
				RATE_MCS_ANT_POS;

		link_cmd->rs_table[i].rate_n_flags =
			iwl_hw_set_rate_n_flags(iwl_rates[r].plcp, rate_flags);
		r = iwl_get_prev_ieee_rate(r);
	}

	link_cmd->general_params.single_stream_ant_msk =
				first_antenna(priv->hw_params.valid_tx_ant);

	link_cmd->general_params.dual_stream_ant_msk =
		priv->hw_params.valid_tx_ant &
		~first_antenna(priv->hw_params.valid_tx_ant);
	if (!link_cmd->general_params.dual_stream_ant_msk) {
		link_cmd->general_params.dual_stream_ant_msk = ANT_AB;
	} else if (num_of_ant(priv->hw_params.valid_tx_ant) == 2) {
		link_cmd->general_params.dual_stream_ant_msk =
			priv->hw_params.valid_tx_ant;
	}

	link_cmd->agg_params.agg_dis_start_th = LINK_QUAL_AGG_DISABLE_START_DEF;
	link_cmd->agg_params.agg_time_limit =
		cpu_to_le16(LINK_QUAL_AGG_TIME_LIMIT_DEF);

	link_cmd->sta_id = sta_id;

	return link_cmd;
}

/*
 * iwl_add_bssid_station - Add the special IBSS BSSID station
 *
 * Function sleeps.
 */
int iwl_add_bssid_station(struct iwl_priv *priv, const u8 *addr, bool init_rs,
			  u8 *sta_id_r)
{
	int ret;
	u8 sta_id;
	struct iwl_link_quality_cmd *link_cmd;
	unsigned long flags;

	if (sta_id_r)
		*sta_id_r = IWL_INVALID_STATION;

	ret = iwl_add_station_common(priv, addr, 0, NULL, &sta_id);
	if (ret) {
		IWL_ERR(priv, "Unable to add station %pM\n", addr);
		return ret;
	}

	if (sta_id_r)
		*sta_id_r = sta_id;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].used |= IWL_STA_LOCAL;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	if (init_rs) {
		/* Set up default rate scaling table in device's station table */
		link_cmd = iwl_sta_alloc_lq(priv, sta_id);
		if (!link_cmd) {
			IWL_ERR(priv, "Unable to initialize rate scaling for station %pM.\n",
				addr);
			return -ENOMEM;
		}

		ret = iwl_send_lq_cmd(priv, link_cmd, CMD_SYNC, true);
		if (ret)
			IWL_ERR(priv, "Link quality command failed (%d)\n", ret);

		spin_lock_irqsave(&priv->sta_lock, flags);
		priv->stations[sta_id].lq = link_cmd;
		spin_unlock_irqrestore(&priv->sta_lock, flags);
	}

	return 0;
}
EXPORT_SYMBOL(iwl_add_bssid_station);

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
void iwl_clear_ucode_stations(struct iwl_priv *priv)
{
	int i;
	unsigned long flags_spin;
	bool cleared = false;

	IWL_DEBUG_INFO(priv, "Clearing ucode stations in driver\n");

	spin_lock_irqsave(&priv->sta_lock, flags_spin);
	for (i = 0; i < priv->hw_params.max_stations; i++) {
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
void iwl_restore_stations(struct iwl_priv *priv)
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
				iwl_send_lq_cmd(priv, &lq, CMD_SYNC, true);
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

	for (i = 0; i < STA_KEY_MAX_NUM; i++)
		if (!test_and_set_bit(i, &priv->ucode_key_table))
			return i;

	return WEP_INVALID_OFFSET;
}
EXPORT_SYMBOL(iwl_get_free_ucode_key_index);

static int iwl_send_static_wepkey_cmd(struct iwl_priv *priv, u8 send_if_empty)
{
	int i, not_empty = 0;
	u8 buff[sizeof(struct iwl_wep_cmd) +
		sizeof(struct iwl_wep_key) * WEP_KEYS_MAX];
	struct iwl_wep_cmd *wep_cmd = (struct iwl_wep_cmd *)buff;
	size_t cmd_size  = sizeof(struct iwl_wep_cmd);
	struct iwl_host_cmd cmd = {
		.id = REPLY_WEPKEY,
		.data = wep_cmd,
		.flags = CMD_SYNC,
	};

	might_sleep();

	memset(wep_cmd, 0, cmd_size +
			(sizeof(struct iwl_wep_key) * WEP_KEYS_MAX));

	for (i = 0; i < WEP_KEYS_MAX ; i++) {
		wep_cmd->key[i].key_index = i;
		if (priv->wep_keys[i].key_size) {
			wep_cmd->key[i].key_offset = i;
			not_empty = 1;
		} else {
			wep_cmd->key[i].key_offset = WEP_INVALID_OFFSET;
		}

		wep_cmd->key[i].key_size = priv->wep_keys[i].key_size;
		memcpy(&wep_cmd->key[i].key[3], priv->wep_keys[i].key,
				priv->wep_keys[i].key_size);
	}

	wep_cmd->global_key_type = WEP_KEY_WEP_TYPE;
	wep_cmd->num_keys = WEP_KEYS_MAX;

	cmd_size += sizeof(struct iwl_wep_key) * WEP_KEYS_MAX;

	cmd.len = cmd_size;

	if (not_empty || send_if_empty)
		return iwl_send_cmd(priv, &cmd);
	else
		return 0;
}

int iwl_restore_default_wep_keys(struct iwl_priv *priv)
{
	lockdep_assert_held(&priv->mutex);

	return iwl_send_static_wepkey_cmd(priv, 0);
}
EXPORT_SYMBOL(iwl_restore_default_wep_keys);

int iwl_remove_default_wep_key(struct iwl_priv *priv,
			       struct ieee80211_key_conf *keyconf)
{
	int ret;

	lockdep_assert_held(&priv->mutex);

	IWL_DEBUG_WEP(priv, "Removing default WEP key: idx=%d\n",
		      keyconf->keyidx);

	memset(&priv->wep_keys[keyconf->keyidx], 0, sizeof(priv->wep_keys[0]));
	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_WEP(priv, "Not sending REPLY_WEPKEY command due to RFKILL.\n");
		/* but keys in device are clear anyway so return success */
		return 0;
	}
	ret = iwl_send_static_wepkey_cmd(priv, 1);
	IWL_DEBUG_WEP(priv, "Remove default WEP key: idx=%d ret=%d\n",
		      keyconf->keyidx, ret);

	return ret;
}
EXPORT_SYMBOL(iwl_remove_default_wep_key);

int iwl_set_default_wep_key(struct iwl_priv *priv,
			    struct ieee80211_key_conf *keyconf)
{
	int ret;

	lockdep_assert_held(&priv->mutex);

	if (keyconf->keylen != WEP_KEY_LEN_128 &&
	    keyconf->keylen != WEP_KEY_LEN_64) {
		IWL_DEBUG_WEP(priv, "Bad WEP key length %d\n", keyconf->keylen);
		return -EINVAL;
	}

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->hw_key_idx = HW_KEY_DEFAULT;
	priv->stations[IWL_AP_ID].keyinfo.alg = ALG_WEP;

	priv->wep_keys[keyconf->keyidx].key_size = keyconf->keylen;
	memcpy(&priv->wep_keys[keyconf->keyidx].key, &keyconf->key,
							keyconf->keylen);

	ret = iwl_send_static_wepkey_cmd(priv, 0);
	IWL_DEBUG_WEP(priv, "Set default WEP key: len=%d idx=%d ret=%d\n",
		keyconf->keylen, keyconf->keyidx, ret);

	return ret;
}
EXPORT_SYMBOL(iwl_set_default_wep_key);

static int iwl_set_wep_dynamic_key_info(struct iwl_priv *priv,
				struct ieee80211_key_conf *keyconf,
				u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;

	key_flags |= (STA_KEY_FLG_WEP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (keyconf->keylen == WEP_KEY_LEN_128)
		key_flags |= STA_KEY_FLG_KEY_SIZE_MSK;

	if (sta_id == priv->hw_params.bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].keyinfo.alg = keyconf->alg;
	priv->stations[sta_id].keyinfo.keylen = keyconf->keylen;
	priv->stations[sta_id].keyinfo.keyidx = keyconf->keyidx;

	memcpy(priv->stations[sta_id].keyinfo.key,
				keyconf->key, keyconf->keylen);

	memcpy(&priv->stations[sta_id].sta.key.key[3],
				keyconf->key, keyconf->keylen);

	if ((priv->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		priv->stations[sta_id].sta.key.key_offset =
				 iwl_get_free_ucode_key_index(priv);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	priv->stations[sta_id].sta.key.key_flags = key_flags;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}

static int iwl_set_ccmp_dynamic_key_info(struct iwl_priv *priv,
				   struct ieee80211_key_conf *keyconf,
				   u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	key_flags |= (STA_KEY_FLG_CCMP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == priv->hw_params.bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].keyinfo.alg = keyconf->alg;
	priv->stations[sta_id].keyinfo.keylen = keyconf->keylen;

	memcpy(priv->stations[sta_id].keyinfo.key, keyconf->key,
	       keyconf->keylen);

	memcpy(priv->stations[sta_id].sta.key.key, keyconf->key,
	       keyconf->keylen);

	if ((priv->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		priv->stations[sta_id].sta.key.key_offset =
				 iwl_get_free_ucode_key_index(priv);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	priv->stations[sta_id].sta.key.key_flags = key_flags;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}

static int iwl_set_tkip_dynamic_key_info(struct iwl_priv *priv,
				   struct ieee80211_key_conf *keyconf,
				   u8 sta_id)
{
	unsigned long flags;
	int ret = 0;
	__le16 key_flags = 0;

	key_flags |= (STA_KEY_FLG_TKIP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (sta_id == priv->hw_params.bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].keyinfo.alg = keyconf->alg;
	priv->stations[sta_id].keyinfo.keylen = 16;

	if ((priv->stations[sta_id].sta.key.key_flags & STA_KEY_FLG_ENCRYPT_MSK)
			== STA_KEY_FLG_NO_ENC)
		priv->stations[sta_id].sta.key.key_offset =
				 iwl_get_free_ucode_key_index(priv);
	/* else, we are overriding an existing key => no need to allocated room
	 * in uCode. */

	WARN(priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET,
		"no space for a new key");

	priv->stations[sta_id].sta.key.key_flags = key_flags;


	/* This copy is acutally not needed: we get the key with each TX */
	memcpy(priv->stations[sta_id].keyinfo.key, keyconf->key, 16);

	memcpy(priv->stations[sta_id].sta.key.key, keyconf->key, 16);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}

void iwl_update_tkip_key(struct iwl_priv *priv,
			struct ieee80211_key_conf *keyconf,
			struct ieee80211_sta *sta, u32 iv32, u16 *phase1key)
{
	u8 sta_id;
	unsigned long flags;
	int i;

	if (iwl_scan_cancel(priv)) {
		/* cancel scan failed, just live w/ bad key and rely
		   briefly on SW decryption */
		return;
	}

	sta_id = iwl_sta_id_or_broadcast(priv, sta);
	if (sta_id == IWL_INVALID_STATION)
		return;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].sta.key.tkip_rx_tsc_byte2 = (u8) iv32;

	for (i = 0; i < 5; i++)
		priv->stations[sta_id].sta.key.tkip_rx_ttak[i] =
			cpu_to_le16(phase1key[i]);

	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

}
EXPORT_SYMBOL(iwl_update_tkip_key);

int iwl_remove_dynamic_key(struct iwl_priv *priv,
				struct ieee80211_key_conf *keyconf,
				u8 sta_id)
{
	unsigned long flags;
	u16 key_flags;
	u8 keyidx;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	priv->key_mapping_key--;

	spin_lock_irqsave(&priv->sta_lock, flags);
	key_flags = le16_to_cpu(priv->stations[sta_id].sta.key.key_flags);
	keyidx = (key_flags >> STA_KEY_FLG_KEYID_POS) & 0x3;

	IWL_DEBUG_WEP(priv, "Remove dynamic key: idx=%d sta=%d\n",
		      keyconf->keyidx, sta_id);

	if (keyconf->keyidx != keyidx) {
		/* We need to remove a key with index different that the one
		 * in the uCode. This means that the key we need to remove has
		 * been replaced by another one with different index.
		 * Don't do anything and return ok
		 */
		spin_unlock_irqrestore(&priv->sta_lock, flags);
		return 0;
	}

	if (priv->stations[sta_id].sta.key.key_offset == WEP_INVALID_OFFSET) {
		IWL_WARN(priv, "Removing wrong key %d 0x%x\n",
			    keyconf->keyidx, key_flags);
		spin_unlock_irqrestore(&priv->sta_lock, flags);
		return 0;
	}

	if (!test_and_clear_bit(priv->stations[sta_id].sta.key.key_offset,
		&priv->ucode_key_table))
		IWL_ERR(priv, "index %d not used in uCode key table.\n",
			priv->stations[sta_id].sta.key.key_offset);
	memset(&priv->stations[sta_id].keyinfo, 0,
					sizeof(struct iwl_hw_key));
	memset(&priv->stations[sta_id].sta.key, 0,
					sizeof(struct iwl4965_keyinfo));
	priv->stations[sta_id].sta.key.key_flags =
			STA_KEY_FLG_NO_ENC | STA_KEY_FLG_INVALID;
	priv->stations[sta_id].sta.key.key_offset = WEP_INVALID_OFFSET;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_WEP(priv, "Not sending REPLY_ADD_STA command because RFKILL enabled.\n");
		spin_unlock_irqrestore(&priv->sta_lock, flags);
		return 0;
	}
	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}
EXPORT_SYMBOL(iwl_remove_dynamic_key);

int iwl_set_dynamic_key(struct iwl_priv *priv,
				struct ieee80211_key_conf *keyconf, u8 sta_id)
{
	int ret;

	lockdep_assert_held(&priv->mutex);

	priv->key_mapping_key++;
	keyconf->hw_key_idx = HW_KEY_DYNAMIC;

	switch (keyconf->alg) {
	case ALG_CCMP:
		ret = iwl_set_ccmp_dynamic_key_info(priv, keyconf, sta_id);
		break;
	case ALG_TKIP:
		ret = iwl_set_tkip_dynamic_key_info(priv, keyconf, sta_id);
		break;
	case ALG_WEP:
		ret = iwl_set_wep_dynamic_key_info(priv, keyconf, sta_id);
		break;
	default:
		IWL_ERR(priv,
			"Unknown alg: %s alg = %d\n", __func__, keyconf->alg);
		ret = -EINVAL;
	}

	IWL_DEBUG_WEP(priv, "Set dynamic key: alg= %d len=%d idx=%d sta=%d ret=%d\n",
		      keyconf->alg, keyconf->keylen, keyconf->keyidx,
		      sta_id, ret);

	return ret;
}
EXPORT_SYMBOL(iwl_set_dynamic_key);

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
			      struct iwl_link_quality_cmd *lq)
{
	int i;
	struct iwl_ht_config *ht_conf = &priv->current_ht_config;

	if (ht_conf->is_ht)
		return true;

	IWL_DEBUG_INFO(priv, "Channel %u is not an HT channel\n",
		       priv->active_rxon.channel);
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
int iwl_send_lq_cmd(struct iwl_priv *priv,
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

	if (is_lq_table_valid(priv, lq))
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

/**
 * iwl_alloc_bcast_station - add broadcast station into driver's station table.
 *
 * This adds the broadcast station into the driver's station table
 * and marks it driver active, so that it will be restored to the
 * device at the next best time.
 */
int iwl_alloc_bcast_station(struct iwl_priv *priv, bool init_lq)
{
	struct iwl_link_quality_cmd *link_cmd;
	unsigned long flags;
	u8 sta_id;

	spin_lock_irqsave(&priv->sta_lock, flags);
	sta_id = iwl_prep_station(priv, iwl_bcast_addr, false, NULL);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Unable to prepare broadcast station\n");
		spin_unlock_irqrestore(&priv->sta_lock, flags);

		return -EINVAL;
	}

	priv->stations[sta_id].used |= IWL_STA_DRIVER_ACTIVE;
	priv->stations[sta_id].used |= IWL_STA_BCAST;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	if (init_lq) {
		link_cmd = iwl_sta_alloc_lq(priv, sta_id);
		if (!link_cmd) {
			IWL_ERR(priv,
				"Unable to initialize rate scaling for bcast station.\n");
			return -ENOMEM;
		}

		spin_lock_irqsave(&priv->sta_lock, flags);
		priv->stations[sta_id].lq = link_cmd;
		spin_unlock_irqrestore(&priv->sta_lock, flags);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iwl_alloc_bcast_station);

/**
 * iwl_update_bcast_station - update broadcast station's LQ command
 *
 * Only used by iwlagn. Placed here to have all bcast station management
 * code together.
 */
int iwl_update_bcast_station(struct iwl_priv *priv)
{
	unsigned long flags;
	struct iwl_link_quality_cmd *link_cmd;
	u8 sta_id = priv->hw_params.bcast_sta_id;

	link_cmd = iwl_sta_alloc_lq(priv, sta_id);
	if (!link_cmd) {
		IWL_ERR(priv, "Unable to initialize rate scaling for bcast station.\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&priv->sta_lock, flags);
	if (priv->stations[sta_id].lq)
		kfree(priv->stations[sta_id].lq);
	else
		IWL_DEBUG_INFO(priv, "Bcast station rate scaling has not been initialized yet.\n");
	priv->stations[sta_id].lq = link_cmd;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(iwl_update_bcast_station);

void iwl_dealloc_bcast_station(struct iwl_priv *priv)
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
EXPORT_SYMBOL_GPL(iwl_dealloc_bcast_station);

/**
 * iwl_sta_tx_modify_enable_tid - Enable Tx for this TID in station table
 */
int iwl_sta_tx_modify_enable_tid(struct iwl_priv *priv, int sta_id, int tid)
{
	unsigned long flags;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	/* Remove "disable" flag, to enable Tx for this TID */
	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_TID_DISABLE_TX;
	priv->stations[sta_id].sta.tid_disable_tx &= cpu_to_le16(~(1 << tid));
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}
EXPORT_SYMBOL(iwl_sta_tx_modify_enable_tid);

int iwl_sta_rx_agg_start(struct iwl_priv *priv, struct ieee80211_sta *sta,
			 int tid, u16 ssn)
{
	unsigned long flags;
	int sta_id;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	sta_id = iwl_sta_id(sta);
	if (sta_id == IWL_INVALID_STATION)
		return -ENXIO;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags_msk = 0;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_ADDBA_TID_MSK;
	priv->stations[sta_id].sta.add_immediate_ba_tid = (u8)tid;
	priv->stations[sta_id].sta.add_immediate_ba_ssn = cpu_to_le16(ssn);
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}
EXPORT_SYMBOL(iwl_sta_rx_agg_start);

int iwl_sta_rx_agg_stop(struct iwl_priv *priv, struct ieee80211_sta *sta,
			int tid)
{
	unsigned long flags;
	int sta_id;
	struct iwl_addsta_cmd sta_cmd;

	lockdep_assert_held(&priv->mutex);

	sta_id = iwl_sta_id(sta);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Invalid station for AGG tid %d\n", tid);
		return -ENXIO;
	}

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags_msk = 0;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_DELBA_TID_MSK;
	priv->stations[sta_id].sta.remove_immediate_ba_tid = (u8)tid;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	memcpy(&sta_cmd, &priv->stations[sta_id].sta, sizeof(struct iwl_addsta_cmd));
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &sta_cmd, CMD_SYNC);
}
EXPORT_SYMBOL(iwl_sta_rx_agg_stop);

void iwl_sta_modify_ps_wake(struct iwl_priv *priv, int sta_id)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags &= ~STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.station_flags_msk = STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.sta.modify_mask = 0;
	priv->stations[sta_id].sta.sleep_tx_count = 0;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

}
EXPORT_SYMBOL(iwl_sta_modify_ps_wake);

void iwl_sta_modify_sleep_tx_count(struct iwl_priv *priv, int sta_id, int cnt)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags |= STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.station_flags_msk = STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.sta.modify_mask =
					STA_MODIFY_SLEEP_TX_COUNT_MSK;
	priv->stations[sta_id].sta.sleep_tx_count = cpu_to_le16(cnt);
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

}
EXPORT_SYMBOL(iwl_sta_modify_sleep_tx_count);

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
