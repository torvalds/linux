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

#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <linux/sched.h>
#include <linux/lockdep.h>
#include <linux/export.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-sta.h"

/* il->sta_lock must be held */
static void il_sta_ucode_activate(struct il_priv *il, u8 sta_id)
{

	if (!(il->stations[sta_id].used & IL_STA_DRIVER_ACTIVE))
		IL_ERR(
			"ACTIVATE a non DRIVER active station id %u addr %pM\n",
			sta_id, il->stations[sta_id].sta.sta.addr);

	if (il->stations[sta_id].used & IL_STA_UCODE_ACTIVE) {
		D_ASSOC(
			"STA id %u addr %pM already present"
			" in uCode (according to driver)\n",
			sta_id, il->stations[sta_id].sta.sta.addr);
	} else {
		il->stations[sta_id].used |= IL_STA_UCODE_ACTIVE;
		D_ASSOC("Added STA id %u addr %pM to uCode\n",
				sta_id, il->stations[sta_id].sta.sta.addr);
	}
}

static int il_process_add_sta_resp(struct il_priv *il,
				    struct il_addsta_cmd *addsta,
				    struct il_rx_pkt *pkt,
				    bool sync)
{
	u8 sta_id = addsta->sta.sta_id;
	unsigned long flags;
	int ret = -EIO;

	if (pkt->hdr.flags & IL_CMD_FAILED_MSK) {
		IL_ERR("Bad return from C_ADD_STA (0x%08X)\n",
			pkt->hdr.flags);
		return ret;
	}

	D_INFO("Processing response for adding station %u\n",
		       sta_id);

	spin_lock_irqsave(&il->sta_lock, flags);

	switch (pkt->u.add_sta.status) {
	case ADD_STA_SUCCESS_MSK:
		D_INFO("C_ADD_STA PASSED\n");
		il_sta_ucode_activate(il, sta_id);
		ret = 0;
		break;
	case ADD_STA_NO_ROOM_IN_TBL:
		IL_ERR("Adding station %d failed, no room in table.\n",
			sta_id);
		break;
	case ADD_STA_NO_BLOCK_ACK_RESOURCE:
		IL_ERR(
			"Adding station %d failed, no block ack resource.\n",
			sta_id);
		break;
	case ADD_STA_MODIFY_NON_EXIST_STA:
		IL_ERR("Attempting to modify non-existing station %d\n",
			sta_id);
		break;
	default:
		D_ASSOC("Received C_ADD_STA:(0x%08X)\n",
				pkt->u.add_sta.status);
		break;
	}

	D_INFO("%s station id %u addr %pM\n",
		       il->stations[sta_id].sta.mode ==
		       STA_CONTROL_MODIFY_MSK ?  "Modified" : "Added",
		       sta_id, il->stations[sta_id].sta.sta.addr);

	/*
	 * XXX: The MAC address in the command buffer is often changed from
	 * the original sent to the device. That is, the MAC address
	 * written to the command buffer often is not the same MAC address
	 * read from the command buffer when the command returns. This
	 * issue has not yet been resolved and this debugging is left to
	 * observe the problem.
	 */
	D_INFO("%s station according to cmd buffer %pM\n",
		       il->stations[sta_id].sta.mode ==
		       STA_CONTROL_MODIFY_MSK ? "Modified" : "Added",
		       addsta->sta.addr);
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return ret;
}

static void il_add_sta_callback(struct il_priv *il,
				 struct il_device_cmd *cmd,
				 struct il_rx_pkt *pkt)
{
	struct il_addsta_cmd *addsta =
		(struct il_addsta_cmd *)cmd->cmd.payload;

	il_process_add_sta_resp(il, addsta, pkt, false);

}

int il_send_add_sta(struct il_priv *il,
		     struct il_addsta_cmd *sta, u8 flags)
{
	struct il_rx_pkt *pkt = NULL;
	int ret = 0;
	u8 data[sizeof(*sta)];
	struct il_host_cmd cmd = {
		.id = C_ADD_STA,
		.flags = flags,
		.data = data,
	};
	u8 sta_id __maybe_unused = sta->sta.sta_id;

	D_INFO("Adding sta %u (%pM) %ssynchronously\n",
		       sta_id, sta->sta.addr, flags & CMD_ASYNC ?  "a" : "");

	if (flags & CMD_ASYNC)
		cmd.callback = il_add_sta_callback;
	else {
		cmd.flags |= CMD_WANT_SKB;
		might_sleep();
	}

	cmd.len = il->cfg->ops->utils->build_addsta_hcmd(sta, data);
	ret = il_send_cmd(il, &cmd);

	if (ret || (flags & CMD_ASYNC))
		return ret;

	if (ret == 0) {
		pkt = (struct il_rx_pkt *)cmd.reply_page;
		ret = il_process_add_sta_resp(il, sta, pkt, true);
	}
	il_free_pages(il, cmd.reply_page);

	return ret;
}
EXPORT_SYMBOL(il_send_add_sta);

static void il_set_ht_add_station(struct il_priv *il, u8 idx,
				   struct ieee80211_sta *sta,
				   struct il_rxon_context *ctx)
{
	struct ieee80211_sta_ht_cap *sta_ht_inf = &sta->ht_cap;
	__le32 sta_flags;
	u8 mimo_ps_mode;

	if (!sta || !sta_ht_inf->ht_supported)
		goto done;

	mimo_ps_mode = (sta_ht_inf->cap & IEEE80211_HT_CAP_SM_PS) >> 2;
	D_ASSOC("spatial multiplexing power save mode: %s\n",
			(mimo_ps_mode == WLAN_HT_CAP_SM_PS_STATIC) ?
			"static" :
			(mimo_ps_mode == WLAN_HT_CAP_SM_PS_DYNAMIC) ?
			"dynamic" : "disabled");

	sta_flags = il->stations[idx].sta.station_flags;

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
		IL_WARN("Invalid MIMO PS mode %d\n", mimo_ps_mode);
		break;
	}

	sta_flags |= cpu_to_le32(
	      (u32)sta_ht_inf->ampdu_factor << STA_FLG_MAX_AGG_SIZE_POS);

	sta_flags |= cpu_to_le32(
	      (u32)sta_ht_inf->ampdu_density << STA_FLG_AGG_MPDU_DENSITY_POS);

	if (il_is_ht40_tx_allowed(il, ctx, &sta->ht_cap))
		sta_flags |= STA_FLG_HT40_EN_MSK;
	else
		sta_flags &= ~STA_FLG_HT40_EN_MSK;

	il->stations[idx].sta.station_flags = sta_flags;
 done:
	return;
}

/**
 * il_prep_station - Prepare station information for addition
 *
 * should be called with sta_lock held
 */
u8 il_prep_station(struct il_priv *il, struct il_rxon_context *ctx,
		    const u8 *addr, bool is_ap, struct ieee80211_sta *sta)
{
	struct il_station_entry *station;
	int i;
	u8 sta_id = IL_INVALID_STATION;
	u16 rate;

	if (is_ap)
		sta_id = ctx->ap_sta_id;
	else if (is_broadcast_ether_addr(addr))
		sta_id = ctx->bcast_sta_id;
	else
		for (i = IL_STA_ID; i < il->hw_params.max_stations; i++) {
			if (!compare_ether_addr(il->stations[i].sta.sta.addr,
						addr)) {
				sta_id = i;
				break;
			}

			if (!il->stations[i].used &&
			    sta_id == IL_INVALID_STATION)
				sta_id = i;
		}

	/*
	 * These two conditions have the same outcome, but keep them
	 * separate
	 */
	if (unlikely(sta_id == IL_INVALID_STATION))
		return sta_id;

	/*
	 * uCode is not able to deal with multiple requests to add a
	 * station. Keep track if one is in progress so that we do not send
	 * another.
	 */
	if (il->stations[sta_id].used & IL_STA_UCODE_INPROGRESS) {
		D_INFO(
				"STA %d already in process of being added.\n",
				sta_id);
		return sta_id;
	}

	if ((il->stations[sta_id].used & IL_STA_DRIVER_ACTIVE) &&
	    (il->stations[sta_id].used & IL_STA_UCODE_ACTIVE) &&
	    !compare_ether_addr(il->stations[sta_id].sta.sta.addr, addr)) {
		D_ASSOC(
				"STA %d (%pM) already added, not adding again.\n",
				sta_id, addr);
		return sta_id;
	}

	station = &il->stations[sta_id];
	station->used = IL_STA_DRIVER_ACTIVE;
	D_ASSOC("Add STA to driver ID %d: %pM\n",
			sta_id, addr);
	il->num_stations++;

	/* Set up the C_ADD_STA command to send to device */
	memset(&station->sta, 0, sizeof(struct il_addsta_cmd));
	memcpy(station->sta.sta.addr, addr, ETH_ALEN);
	station->sta.mode = 0;
	station->sta.sta.sta_id = sta_id;
	station->sta.station_flags = ctx->station_flags;
	station->ctxid = ctx->ctxid;

	if (sta) {
		struct il_station_priv_common *sta_priv;

		sta_priv = (void *)sta->drv_priv;
		sta_priv->ctx = ctx;
	}

	/*
	 * OK to call unconditionally, since local stations (IBSS BSSID
	 * STA and broadcast STA) pass in a NULL sta, and mac80211
	 * doesn't allow HT IBSS.
	 */
	il_set_ht_add_station(il, sta_id, sta, ctx);

	/* 3945 only */
	rate = (il->band == IEEE80211_BAND_5GHZ) ?
		RATE_6M_PLCP : RATE_1M_PLCP;
	/* Turn on both antennas for the station... */
	station->sta.rate_n_flags = cpu_to_le16(rate | RATE_MCS_ANT_AB_MSK);

	return sta_id;

}
EXPORT_SYMBOL_GPL(il_prep_station);

#define STA_WAIT_TIMEOUT (HZ/2)

/**
 * il_add_station_common -
 */
int
il_add_station_common(struct il_priv *il,
			struct il_rxon_context *ctx,
			   const u8 *addr, bool is_ap,
			   struct ieee80211_sta *sta, u8 *sta_id_r)
{
	unsigned long flags_spin;
	int ret = 0;
	u8 sta_id;
	struct il_addsta_cmd sta_cmd;

	*sta_id_r = 0;
	spin_lock_irqsave(&il->sta_lock, flags_spin);
	sta_id = il_prep_station(il, ctx, addr, is_ap, sta);
	if (sta_id == IL_INVALID_STATION) {
		IL_ERR("Unable to prepare station %pM for addition\n",
			addr);
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
		return -EINVAL;
	}

	/*
	 * uCode is not able to deal with multiple requests to add a
	 * station. Keep track if one is in progress so that we do not send
	 * another.
	 */
	if (il->stations[sta_id].used & IL_STA_UCODE_INPROGRESS) {
		D_INFO(
			"STA %d already in process of being added.\n",
		       sta_id);
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
		return -EEXIST;
	}

	if ((il->stations[sta_id].used & IL_STA_DRIVER_ACTIVE) &&
	    (il->stations[sta_id].used & IL_STA_UCODE_ACTIVE)) {
		D_ASSOC(
			"STA %d (%pM) already added, not adding again.\n",
			sta_id, addr);
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
		return -EEXIST;
	}

	il->stations[sta_id].used |= IL_STA_UCODE_INPROGRESS;
	memcpy(&sta_cmd, &il->stations[sta_id].sta,
				sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags_spin);

	/* Add station to device's station table */
	ret = il_send_add_sta(il, &sta_cmd, CMD_SYNC);
	if (ret) {
		spin_lock_irqsave(&il->sta_lock, flags_spin);
		IL_ERR("Adding station %pM failed.\n",
			il->stations[sta_id].sta.sta.addr);
		il->stations[sta_id].used &= ~IL_STA_DRIVER_ACTIVE;
		il->stations[sta_id].used &= ~IL_STA_UCODE_INPROGRESS;
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
	}
	*sta_id_r = sta_id;
	return ret;
}
EXPORT_SYMBOL(il_add_station_common);

/**
 * il_sta_ucode_deactivate - deactivate ucode status for a station
 *
 * il->sta_lock must be held
 */
static void il_sta_ucode_deactivate(struct il_priv *il, u8 sta_id)
{
	/* Ucode must be active and driver must be non active */
	if ((il->stations[sta_id].used &
	     (IL_STA_UCODE_ACTIVE | IL_STA_DRIVER_ACTIVE)) !=
						IL_STA_UCODE_ACTIVE)
		IL_ERR("removed non active STA %u\n", sta_id);

	il->stations[sta_id].used &= ~IL_STA_UCODE_ACTIVE;

	memset(&il->stations[sta_id], 0, sizeof(struct il_station_entry));
	D_ASSOC("Removed STA %u\n", sta_id);
}

static int il_send_remove_station(struct il_priv *il,
				   const u8 *addr, int sta_id,
				   bool temporary)
{
	struct il_rx_pkt *pkt;
	int ret;

	unsigned long flags_spin;
	struct il_rem_sta_cmd rm_sta_cmd;

	struct il_host_cmd cmd = {
		.id = C_REM_STA,
		.len = sizeof(struct il_rem_sta_cmd),
		.flags = CMD_SYNC,
		.data = &rm_sta_cmd,
	};

	memset(&rm_sta_cmd, 0, sizeof(rm_sta_cmd));
	rm_sta_cmd.num_sta = 1;
	memcpy(&rm_sta_cmd.addr, addr, ETH_ALEN);

	cmd.flags |= CMD_WANT_SKB;

	ret = il_send_cmd(il, &cmd);

	if (ret)
		return ret;

	pkt = (struct il_rx_pkt *)cmd.reply_page;
	if (pkt->hdr.flags & IL_CMD_FAILED_MSK) {
		IL_ERR("Bad return from C_REM_STA (0x%08X)\n",
			  pkt->hdr.flags);
		ret = -EIO;
	}

	if (!ret) {
		switch (pkt->u.rem_sta.status) {
		case REM_STA_SUCCESS_MSK:
			if (!temporary) {
				spin_lock_irqsave(&il->sta_lock, flags_spin);
				il_sta_ucode_deactivate(il, sta_id);
				spin_unlock_irqrestore(&il->sta_lock,
								flags_spin);
			}
			D_ASSOC("C_REM_STA PASSED\n");
			break;
		default:
			ret = -EIO;
			IL_ERR("C_REM_STA failed\n");
			break;
		}
	}
	il_free_pages(il, cmd.reply_page);

	return ret;
}

/**
 * il_remove_station - Remove driver's knowledge of station.
 */
int il_remove_station(struct il_priv *il, const u8 sta_id,
		       const u8 *addr)
{
	unsigned long flags;

	if (!il_is_ready(il)) {
		D_INFO(
			"Unable to remove station %pM, device not ready.\n",
			addr);
		/*
		 * It is typical for stations to be removed when we are
		 * going down. Return success since device will be down
		 * soon anyway
		 */
		return 0;
	}

	D_ASSOC("Removing STA from driver:%d  %pM\n",
			sta_id, addr);

	if (WARN_ON(sta_id == IL_INVALID_STATION))
		return -EINVAL;

	spin_lock_irqsave(&il->sta_lock, flags);

	if (!(il->stations[sta_id].used & IL_STA_DRIVER_ACTIVE)) {
		D_INFO("Removing %pM but non DRIVER active\n",
				addr);
		goto out_err;
	}

	if (!(il->stations[sta_id].used & IL_STA_UCODE_ACTIVE)) {
		D_INFO("Removing %pM but non UCODE active\n",
				addr);
		goto out_err;
	}

	if (il->stations[sta_id].used & IL_STA_LOCAL) {
		kfree(il->stations[sta_id].lq);
		il->stations[sta_id].lq = NULL;
	}

	il->stations[sta_id].used &= ~IL_STA_DRIVER_ACTIVE;

	il->num_stations--;

	BUG_ON(il->num_stations < 0);

	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_remove_station(il, addr, sta_id, false);
out_err:
	spin_unlock_irqrestore(&il->sta_lock, flags);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(il_remove_station);

/**
 * il_clear_ucode_stations - clear ucode station table bits
 *
 * This function clears all the bits in the driver indicating
 * which stations are active in the ucode. Call when something
 * other than explicit station management would cause this in
 * the ucode, e.g. unassociated RXON.
 */
void il_clear_ucode_stations(struct il_priv *il,
			      struct il_rxon_context *ctx)
{
	int i;
	unsigned long flags_spin;
	bool cleared = false;

	D_INFO("Clearing ucode stations in driver\n");

	spin_lock_irqsave(&il->sta_lock, flags_spin);
	for (i = 0; i < il->hw_params.max_stations; i++) {
		if (ctx && ctx->ctxid != il->stations[i].ctxid)
			continue;

		if (il->stations[i].used & IL_STA_UCODE_ACTIVE) {
			D_INFO(
				"Clearing ucode active for station %d\n", i);
			il->stations[i].used &= ~IL_STA_UCODE_ACTIVE;
			cleared = true;
		}
	}
	spin_unlock_irqrestore(&il->sta_lock, flags_spin);

	if (!cleared)
		D_INFO(
			"No active stations found to be cleared\n");
}
EXPORT_SYMBOL(il_clear_ucode_stations);

/**
 * il_restore_stations() - Restore driver known stations to device
 *
 * All stations considered active by driver, but not present in ucode, is
 * restored.
 *
 * Function sleeps.
 */
void
il_restore_stations(struct il_priv *il, struct il_rxon_context *ctx)
{
	struct il_addsta_cmd sta_cmd;
	struct il_link_quality_cmd lq;
	unsigned long flags_spin;
	int i;
	bool found = false;
	int ret;
	bool send_lq;

	if (!il_is_ready(il)) {
		D_INFO(
			"Not ready yet, not restoring any stations.\n");
		return;
	}

	D_ASSOC("Restoring all known stations ... start.\n");
	spin_lock_irqsave(&il->sta_lock, flags_spin);
	for (i = 0; i < il->hw_params.max_stations; i++) {
		if (ctx->ctxid != il->stations[i].ctxid)
			continue;
		if ((il->stations[i].used & IL_STA_DRIVER_ACTIVE) &&
		    !(il->stations[i].used & IL_STA_UCODE_ACTIVE)) {
			D_ASSOC("Restoring sta %pM\n",
					il->stations[i].sta.sta.addr);
			il->stations[i].sta.mode = 0;
			il->stations[i].used |= IL_STA_UCODE_INPROGRESS;
			found = true;
		}
	}

	for (i = 0; i < il->hw_params.max_stations; i++) {
		if ((il->stations[i].used & IL_STA_UCODE_INPROGRESS)) {
			memcpy(&sta_cmd, &il->stations[i].sta,
			       sizeof(struct il_addsta_cmd));
			send_lq = false;
			if (il->stations[i].lq) {
				memcpy(&lq, il->stations[i].lq,
				       sizeof(struct il_link_quality_cmd));
				send_lq = true;
			}
			spin_unlock_irqrestore(&il->sta_lock, flags_spin);
			ret = il_send_add_sta(il, &sta_cmd, CMD_SYNC);
			if (ret) {
				spin_lock_irqsave(&il->sta_lock, flags_spin);
				IL_ERR("Adding station %pM failed.\n",
					il->stations[i].sta.sta.addr);
				il->stations[i].used &=
						~IL_STA_DRIVER_ACTIVE;
				il->stations[i].used &=
						~IL_STA_UCODE_INPROGRESS;
				spin_unlock_irqrestore(&il->sta_lock,
								flags_spin);
			}
			/*
			 * Rate scaling has already been initialized, send
			 * current LQ command
			 */
			if (send_lq)
				il_send_lq_cmd(il, ctx, &lq,
								CMD_SYNC, true);
			spin_lock_irqsave(&il->sta_lock, flags_spin);
			il->stations[i].used &= ~IL_STA_UCODE_INPROGRESS;
		}
	}

	spin_unlock_irqrestore(&il->sta_lock, flags_spin);
	if (!found)
		D_INFO("Restoring all known stations"
				" .... no stations to be restored.\n");
	else
		D_INFO("Restoring all known stations"
				" .... complete.\n");
}
EXPORT_SYMBOL(il_restore_stations);

int il_get_free_ucode_key_idx(struct il_priv *il)
{
	int i;

	for (i = 0; i < il->sta_key_max_num; i++)
		if (!test_and_set_bit(i, &il->ucode_key_table))
			return i;

	return WEP_INVALID_OFFSET;
}
EXPORT_SYMBOL(il_get_free_ucode_key_idx);

void il_dealloc_bcast_stations(struct il_priv *il)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&il->sta_lock, flags);
	for (i = 0; i < il->hw_params.max_stations; i++) {
		if (!(il->stations[i].used & IL_STA_BCAST))
			continue;

		il->stations[i].used &= ~IL_STA_UCODE_ACTIVE;
		il->num_stations--;
		BUG_ON(il->num_stations < 0);
		kfree(il->stations[i].lq);
		il->stations[i].lq = NULL;
	}
	spin_unlock_irqrestore(&il->sta_lock, flags);
}
EXPORT_SYMBOL_GPL(il_dealloc_bcast_stations);

#ifdef CONFIG_IWLEGACY_DEBUG
static void il_dump_lq_cmd(struct il_priv *il,
			   struct il_link_quality_cmd *lq)
{
	int i;
	D_RATE("lq station id 0x%x\n", lq->sta_id);
	D_RATE("lq ant 0x%X 0x%X\n",
		       lq->general_params.single_stream_ant_msk,
		       lq->general_params.dual_stream_ant_msk);

	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		D_RATE("lq idx %d 0x%X\n",
			       i, lq->rs_table[i].rate_n_flags);
}
#else
static inline void il_dump_lq_cmd(struct il_priv *il,
				   struct il_link_quality_cmd *lq)
{
}
#endif

/**
 * il_is_lq_table_valid() - Test one aspect of LQ cmd for validity
 *
 * It sometimes happens when a HT rate has been in use and we
 * loose connectivity with AP then mac80211 will first tell us that the
 * current channel is not HT anymore before removing the station. In such a
 * scenario the RXON flags will be updated to indicate we are not
 * communicating HT anymore, but the LQ command may still contain HT rates.
 * Test for this to prevent driver from sending LQ command between the time
 * RXON flags are updated and when LQ command is updated.
 */
static bool il_is_lq_table_valid(struct il_priv *il,
			      struct il_rxon_context *ctx,
			      struct il_link_quality_cmd *lq)
{
	int i;

	if (ctx->ht.enabled)
		return true;

	D_INFO("Channel %u is not an HT channel\n",
		       ctx->active.channel);
	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++) {
		if (le32_to_cpu(lq->rs_table[i].rate_n_flags) &
						RATE_MCS_HT_MSK) {
			D_INFO(
				       "idx %d of LQ expects HT channel\n",
				       i);
			return false;
		}
	}
	return true;
}

/**
 * il_send_lq_cmd() - Send link quality command
 * @init: This command is sent as part of station initialization right
 *        after station has been added.
 *
 * The link quality command is sent as the last step of station creation.
 * This is the special case in which init is set and we call a callback in
 * this case to clear the state indicating that station creation is in
 * progress.
 */
int il_send_lq_cmd(struct il_priv *il, struct il_rxon_context *ctx,
		    struct il_link_quality_cmd *lq, u8 flags, bool init)
{
	int ret = 0;
	unsigned long flags_spin;

	struct il_host_cmd cmd = {
		.id = C_TX_LINK_QUALITY_CMD,
		.len = sizeof(struct il_link_quality_cmd),
		.flags = flags,
		.data = lq,
	};

	if (WARN_ON(lq->sta_id == IL_INVALID_STATION))
		return -EINVAL;


	spin_lock_irqsave(&il->sta_lock, flags_spin);
	if (!(il->stations[lq->sta_id].used & IL_STA_DRIVER_ACTIVE)) {
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&il->sta_lock, flags_spin);

	il_dump_lq_cmd(il, lq);
	BUG_ON(init && (cmd.flags & CMD_ASYNC));

	if (il_is_lq_table_valid(il, ctx, lq))
		ret = il_send_cmd(il, &cmd);
	else
		ret = -EINVAL;

	if (cmd.flags & CMD_ASYNC)
		return ret;

	if (init) {
		D_INFO("init LQ command complete,"
				" clearing sta addition status for sta %d\n",
			       lq->sta_id);
		spin_lock_irqsave(&il->sta_lock, flags_spin);
		il->stations[lq->sta_id].used &= ~IL_STA_UCODE_INPROGRESS;
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
	}
	return ret;
}
EXPORT_SYMBOL(il_send_lq_cmd);

int il_mac_sta_remove(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct il_priv *il = hw->priv;
	struct il_station_priv_common *sta_common = (void *)sta->drv_priv;
	int ret;

	D_INFO("received request to remove station %pM\n",
			sta->addr);
	mutex_lock(&il->mutex);
	D_INFO("proceeding to remove station %pM\n",
			sta->addr);
	ret = il_remove_station(il, sta_common->sta_id, sta->addr);
	if (ret)
		IL_ERR("Error removing station %pM\n",
			sta->addr);
	mutex_unlock(&il->mutex);
	return ret;
}
EXPORT_SYMBOL(il_mac_sta_remove);
