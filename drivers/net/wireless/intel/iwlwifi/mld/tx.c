// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024 - 2025 Intel Corporation
 */
#include <net/ip.h>

#include "tx.h"
#include "sta.h"
#include "hcmd.h"
#include "iwl-utils.h"
#include "iface.h"

#include "fw/dbg.h"

#include "fw/api/tx.h"
#include "fw/api/rs.h"
#include "fw/api/txq.h"
#include "fw/api/datapath.h"
#include "fw/api/time-event.h"

#define MAX_ANT_NUM 2

/* Toggles between TX antennas. Receives the bitmask of valid TX antennas and
 * the *index* used for the last TX, and returns the next valid *index* to use.
 * In order to set it in the tx_cmd, must do BIT(idx).
 */
static u8 iwl_mld_next_ant(u8 valid, u8 last_idx)
{
	u8 index = last_idx;

	for (int i = 0; i < MAX_ANT_NUM; i++) {
		index = (index + 1) % MAX_ANT_NUM;
		if (valid & BIT(index))
			return index;
	}

	WARN_ONCE(1, "Failed to toggle between antennas 0x%x", valid);

	return last_idx;
}

void iwl_mld_toggle_tx_ant(struct iwl_mld *mld, u8 *ant)
{
	*ant = iwl_mld_next_ant(iwl_mld_get_valid_tx_ant(mld), *ant);
}

static int
iwl_mld_get_queue_size(struct iwl_mld *mld, struct ieee80211_txq *txq)
{
	struct ieee80211_sta *sta = txq->sta;
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;
	int max_size = IWL_DEFAULT_QUEUE_SIZE;

	lockdep_assert_wiphy(mld->wiphy);

	for_each_sta_active_link(txq->vif, sta, link_sta, link_id) {
		if (link_sta->eht_cap.has_eht) {
			max_size = IWL_DEFAULT_QUEUE_SIZE_EHT;
			break;
		}

		if (link_sta->he_cap.has_he)
			max_size = IWL_DEFAULT_QUEUE_SIZE_HE;
	}

	return max_size;
}

static int iwl_mld_allocate_txq(struct iwl_mld *mld, struct ieee80211_txq *txq)
{
	u8 tid = txq->tid == IEEE80211_NUM_TIDS ? IWL_MGMT_TID : txq->tid;
	u32 fw_sta_mask = iwl_mld_fw_sta_id_mask(mld, txq->sta);
	/* We can't know when the station is asleep or awake, so we
	 * must disable the queue hang detection.
	 */
	unsigned int watchdog_timeout = txq->vif->type == NL80211_IFTYPE_AP ?
				IWL_WATCHDOG_DISABLED :
				mld->trans->trans_cfg->base_params->wd_timeout;
	int queue, size;

	lockdep_assert_wiphy(mld->wiphy);

	if (tid == IWL_MGMT_TID)
		size = max_t(u32, IWL_MGMT_QUEUE_SIZE,
			     mld->trans->cfg->min_txq_size);
	else
		size = iwl_mld_get_queue_size(mld, txq);

	queue = iwl_trans_txq_alloc(mld->trans, 0, fw_sta_mask, tid, size,
				    watchdog_timeout);

	if (queue >= 0)
		IWL_DEBUG_TX_QUEUES(mld,
				    "Enabling TXQ #%d for sta mask 0x%x tid %d\n",
				    queue, fw_sta_mask, tid);
	return queue;
}

static int iwl_mld_add_txq(struct iwl_mld *mld, struct ieee80211_txq *txq)
{
	struct iwl_mld_txq *mld_txq = iwl_mld_txq_from_mac80211(txq);
	int id;

	lockdep_assert_wiphy(mld->wiphy);

	/* This will alse send the SCD_QUEUE_CONFIG_CMD */
	id = iwl_mld_allocate_txq(mld, txq);
	if (id < 0)
		return id;

	mld_txq->fw_id = id;
	mld_txq->status.allocated = true;

	rcu_assign_pointer(mld->fw_id_to_txq[id], txq);

	return 0;
}

void iwl_mld_add_txq_list(struct iwl_mld *mld)
{
	lockdep_assert_wiphy(mld->wiphy);

	while (!list_empty(&mld->txqs_to_add)) {
		struct ieee80211_txq *txq;
		struct iwl_mld_txq *mld_txq =
			list_first_entry(&mld->txqs_to_add, struct iwl_mld_txq,
					 list);
		int failed;

		txq = container_of((void *)mld_txq, struct ieee80211_txq,
				   drv_priv);

		failed = iwl_mld_add_txq(mld, txq);

		local_bh_disable();
		spin_lock(&mld->add_txqs_lock);
		list_del_init(&mld_txq->list);
		spin_unlock(&mld->add_txqs_lock);
		/* If the queue allocation failed, we can't transmit. Leave the
		 * frames on the txq, maybe the attempt to allocate the queue
		 * will succeed.
		 */
		if (!failed)
			iwl_mld_tx_from_txq(mld, txq);
		local_bh_enable();
	}
}

void iwl_mld_add_txqs_wk(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct iwl_mld *mld = container_of(wk, struct iwl_mld,
					   add_txqs_wk);

	/* will reschedule to run after restart */
	if (mld->fw_status.in_hw_restart)
		return;

	iwl_mld_add_txq_list(mld);
}

void
iwl_mld_free_txq(struct iwl_mld *mld, u32 fw_sta_mask, u32 tid, u32 queue_id)
{
	struct iwl_scd_queue_cfg_cmd remove_cmd = {
		.operation = cpu_to_le32(IWL_SCD_QUEUE_REMOVE),
		.u.remove.tid = cpu_to_le32(tid),
		.u.remove.sta_mask = cpu_to_le32(fw_sta_mask),
	};

	iwl_mld_send_cmd_pdu(mld,
			     WIDE_ID(DATA_PATH_GROUP, SCD_QUEUE_CONFIG_CMD),
			     &remove_cmd);

	iwl_trans_txq_free(mld->trans, queue_id);
}

void iwl_mld_remove_txq(struct iwl_mld *mld, struct ieee80211_txq *txq)
{
	struct iwl_mld_txq *mld_txq = iwl_mld_txq_from_mac80211(txq);
	u32 sta_msk, tid;

	lockdep_assert_wiphy(mld->wiphy);

	spin_lock_bh(&mld->add_txqs_lock);
	if (!list_empty(&mld_txq->list))
		list_del_init(&mld_txq->list);
	spin_unlock_bh(&mld->add_txqs_lock);

	if (!mld_txq->status.allocated ||
	    WARN_ON(mld_txq->fw_id >= ARRAY_SIZE(mld->fw_id_to_txq)))
		return;

	sta_msk = iwl_mld_fw_sta_id_mask(mld, txq->sta);

	tid = txq->tid == IEEE80211_NUM_TIDS ? IWL_MGMT_TID :
					       txq->tid;

	iwl_mld_free_txq(mld, sta_msk, tid, mld_txq->fw_id);

	RCU_INIT_POINTER(mld->fw_id_to_txq[mld_txq->fw_id], NULL);
	mld_txq->status.allocated = false;
}

#define OPT_HDR(type, skb, off) \
	(type *)(skb_network_header(skb) + (off))

static __le32
iwl_mld_get_offload_assist(struct sk_buff *skb, bool amsdu)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	u16 mh_len = ieee80211_hdrlen(hdr->frame_control);
	u16 offload_assist = 0;
#if IS_ENABLED(CONFIG_INET)
	u8 protocol = 0;

	/* Do not compute checksum if already computed */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		goto out;

	/* We do not expect to be requested to csum stuff we do not support */

	/* TBD: do we also need to check
	 * !(mvm->hw->netdev_features & IWL_TX_CSUM_NETIF_FLAGS) now that all
	 * the devices we support has this flags?
	 */
	if (WARN_ONCE(skb->protocol != htons(ETH_P_IP) &&
		      skb->protocol != htons(ETH_P_IPV6),
		      "No support for requested checksum\n")) {
		skb_checksum_help(skb);
		goto out;
	}

	if (skb->protocol == htons(ETH_P_IP)) {
		protocol = ip_hdr(skb)->protocol;
	} else {
#if IS_ENABLED(CONFIG_IPV6)
		struct ipv6hdr *ipv6h =
			(struct ipv6hdr *)skb_network_header(skb);
		unsigned int off = sizeof(*ipv6h);

		protocol = ipv6h->nexthdr;
		while (protocol != NEXTHDR_NONE && ipv6_ext_hdr(protocol)) {
			struct ipv6_opt_hdr *hp;

			/* only supported extension headers */
			if (protocol != NEXTHDR_ROUTING &&
			    protocol != NEXTHDR_HOP &&
			    protocol != NEXTHDR_DEST) {
				skb_checksum_help(skb);
				goto out;
			}

			hp = OPT_HDR(struct ipv6_opt_hdr, skb, off);
			protocol = hp->nexthdr;
			off += ipv6_optlen(hp);
		}
		/* if we get here - protocol now should be TCP/UDP */
#endif
	}

	if (protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) {
		WARN_ON_ONCE(1);
		skb_checksum_help(skb);
		goto out;
	}

	/* enable L4 csum */
	offload_assist |= BIT(TX_CMD_OFFLD_L4_EN);

	/* Set offset to IP header (snap).
	 * We don't support tunneling so no need to take care of inner header.
	 * Size is in words.
	 */
	offload_assist |= (4 << TX_CMD_OFFLD_IP_HDR);

	/* Do IPv4 csum for AMSDU only (no IP csum for Ipv6) */
	if (skb->protocol == htons(ETH_P_IP) && amsdu) {
		ip_hdr(skb)->check = 0;
		offload_assist |= BIT(TX_CMD_OFFLD_L3_EN);
	}

	/* reset UDP/TCP header csum */
	if (protocol == IPPROTO_TCP)
		tcp_hdr(skb)->check = 0;
	else
		udp_hdr(skb)->check = 0;

out:
#endif
	mh_len /= 2;
	offload_assist |= mh_len << TX_CMD_OFFLD_MH_SIZE;

	if (amsdu)
		offload_assist |= BIT(TX_CMD_OFFLD_AMSDU);
	else if (ieee80211_hdrlen(hdr->frame_control) % 4)
		/* padding is inserted later in transport */
		offload_assist |= BIT(TX_CMD_OFFLD_PAD);

	return cpu_to_le32(offload_assist);
}

static void iwl_mld_get_basic_rates_and_band(struct iwl_mld *mld,
					     struct ieee80211_vif *vif,
					     struct ieee80211_tx_info *info,
					     unsigned long *basic_rates,
					     u8 *band)
{
	u32 link_id = u32_get_bits(info->control.flags,
				   IEEE80211_TX_CTRL_MLO_LINK);

	*basic_rates = vif->bss_conf.basic_rates;
	*band = info->band;

	if (link_id == IEEE80211_LINK_UNSPECIFIED &&
	    ieee80211_vif_is_mld(vif)) {
		/* shouldn't do this when >1 link is active */
		WARN_ON(hweight16(vif->active_links) != 1);
		link_id = __ffs(vif->active_links);
	}

	if (link_id < IEEE80211_LINK_UNSPECIFIED) {
		struct ieee80211_bss_conf *link_conf;

		rcu_read_lock();
		link_conf = rcu_dereference(vif->link_conf[link_id]);
		if (link_conf) {
			*basic_rates = link_conf->basic_rates;
			if (link_conf->chanreq.oper.chan)
				*band = link_conf->chanreq.oper.chan->band;
		}
		rcu_read_unlock();
	}
}

u8 iwl_mld_get_lowest_rate(struct iwl_mld *mld,
			   struct ieee80211_tx_info *info,
			   struct ieee80211_vif *vif)
{
	struct ieee80211_supported_band *sband;
	u16 lowest_cck = IWL_RATE_COUNT, lowest_ofdm = IWL_RATE_COUNT;
	unsigned long basic_rates;
	u8 band, rate;
	u32 i;

	iwl_mld_get_basic_rates_and_band(mld, vif, info, &basic_rates, &band);

	sband = mld->hw->wiphy->bands[band];
	for_each_set_bit(i, &basic_rates, BITS_PER_LONG) {
		u16 hw = sband->bitrates[i].hw_value;

		if (hw >= IWL_FIRST_OFDM_RATE) {
			if (lowest_ofdm > hw)
				lowest_ofdm = hw;
		} else if (lowest_cck > hw) {
			lowest_cck = hw;
		}
	}

	if (band == NL80211_BAND_2GHZ && !vif->p2p &&
	    vif->type != NL80211_IFTYPE_P2P_DEVICE &&
	    !(info->flags & IEEE80211_TX_CTL_NO_CCK_RATE)) {
		if (lowest_cck != IWL_RATE_COUNT)
			rate = lowest_cck;
		else if (lowest_ofdm != IWL_RATE_COUNT)
			rate = lowest_ofdm;
		else
			rate = IWL_FIRST_CCK_RATE;
	} else if (lowest_ofdm != IWL_RATE_COUNT) {
		rate = lowest_ofdm;
	} else {
		rate = IWL_FIRST_OFDM_RATE;
	}

	return rate;
}

static u32 iwl_mld_mac80211_rate_idx_to_fw(struct iwl_mld *mld,
					   struct ieee80211_tx_info *info,
					   int rate_idx)
{
	u32 rate_flags = 0;
	u8 rate_plcp;

	/* if the rate isn't a well known legacy rate, take the lowest one */
	if (rate_idx < 0 || rate_idx >= IWL_RATE_COUNT_LEGACY)
		rate_idx = iwl_mld_get_lowest_rate(mld, info,
						   info->control.vif);

	WARN_ON_ONCE(rate_idx < 0);

	/* Set CCK or OFDM flag */
	if (rate_idx <= IWL_LAST_CCK_RATE)
		rate_flags |= RATE_MCS_MOD_TYPE_CCK;
	else
		rate_flags |= RATE_MCS_MOD_TYPE_LEGACY_OFDM;

	/* Legacy rates are indexed:
	 * 0 - 3 for CCK and 0 - 7 for OFDM
	 */
	rate_plcp = (rate_idx >= IWL_FIRST_OFDM_RATE ?
		     rate_idx - IWL_FIRST_OFDM_RATE : rate_idx);

	return (u32)rate_plcp | rate_flags;
}

static u32 iwl_mld_get_tx_ant(struct iwl_mld *mld,
			      struct ieee80211_tx_info *info,
			      struct ieee80211_sta *sta, __le16 fc)
{
	if (sta && ieee80211_is_data(fc)) {
		struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);

		return BIT(mld_sta->data_tx_ant) << RATE_MCS_ANT_POS;
	}

	return BIT(mld->mgmt_tx_ant) << RATE_MCS_ANT_POS;
}

static u32 iwl_mld_get_inject_tx_rate(struct iwl_mld *mld,
				      struct ieee80211_tx_info *info,
				      struct ieee80211_sta *sta,
				      __le16 fc)
{
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	u32 result;

	if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
		u8 mcs = ieee80211_rate_get_vht_mcs(rate);
		u8 nss = ieee80211_rate_get_vht_nss(rate);

		result = RATE_MCS_MOD_TYPE_VHT;
		result |= u32_encode_bits(mcs, RATE_MCS_CODE_MSK);
		result |= u32_encode_bits(nss, RATE_MCS_NSS_MSK);

		if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
			result |= RATE_MCS_SGI_MSK;

		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			result |= RATE_MCS_CHAN_WIDTH_40;
		else if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			result |= RATE_MCS_CHAN_WIDTH_80;
		else if (rate->flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
			result |= RATE_MCS_CHAN_WIDTH_160;
	} else if (rate->flags & IEEE80211_TX_RC_MCS) {
		/* only MCS 0-15 are supported */
		u8 mcs = rate->idx & 7;
		u8 nss = rate->idx > 7;

		result = RATE_MCS_MOD_TYPE_HT;
		result |= u32_encode_bits(mcs, RATE_MCS_CODE_MSK);
		result |= u32_encode_bits(nss, RATE_MCS_NSS_MSK);

		if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
			result |= RATE_MCS_SGI_MSK;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			result |= RATE_MCS_CHAN_WIDTH_40;
		if (info->flags & IEEE80211_TX_CTL_LDPC)
			result |= RATE_MCS_LDPC_MSK;
		if (u32_get_bits(info->flags, IEEE80211_TX_CTL_STBC))
			result |= RATE_MCS_STBC_MSK;
	} else {
		result = iwl_mld_mac80211_rate_idx_to_fw(mld, info, rate->idx);
	}

	if (info->control.antennas)
		result |= u32_encode_bits(info->control.antennas,
					  RATE_MCS_ANT_AB_MSK);
	else
		result |= iwl_mld_get_tx_ant(mld, info, sta, fc);

	return result;
}

static __le32 iwl_mld_get_tx_rate_n_flags(struct iwl_mld *mld,
					  struct ieee80211_tx_info *info,
					  struct ieee80211_sta *sta, __le16 fc)
{
	u32 rate;

	if (unlikely(info->control.flags & IEEE80211_TX_CTRL_RATE_INJECT))
		rate = iwl_mld_get_inject_tx_rate(mld, info, sta, fc);
	else
		rate = iwl_mld_mac80211_rate_idx_to_fw(mld, info, -1) |
		       iwl_mld_get_tx_ant(mld, info, sta, fc);

	return iwl_v3_rate_to_v2_v3(rate, mld->fw_rates_ver_3);
}

static void
iwl_mld_fill_tx_cmd_hdr(struct iwl_tx_cmd_gen3 *tx_cmd,
			struct sk_buff *skb, bool amsdu)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ieee80211_vif *vif;

	/* Copy MAC header from skb into command buffer */
	memcpy(tx_cmd->hdr, hdr, ieee80211_hdrlen(hdr->frame_control));

	if (!amsdu || !skb_is_gso(skb))
		return;

	/* As described in IEEE sta 802.11-2020, table 9-30 (Address
	 * field contents), A-MSDU address 3 should contain the BSSID
	 * address.
	 *
	 * In TSO, the skb header address 3 contains the original address 3 to
	 * correctly create all the A-MSDU subframes headers from it.
	 * Override now the address 3 in the command header with the BSSID.
	 *
	 * Note: we fill in the MLD address, but the firmware will do the
	 * necessary translation to link address after encryption.
	 */
	vif = info->control.vif;
	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		ether_addr_copy(tx_cmd->hdr->addr3, vif->cfg.ap_addr);
		break;
	case NL80211_IFTYPE_AP:
		ether_addr_copy(tx_cmd->hdr->addr3, vif->addr);
		break;
	default:
		break;
	}
}

static void
iwl_mld_fill_tx_cmd(struct iwl_mld *mld, struct sk_buff *skb,
		    struct iwl_device_tx_cmd *dev_tx_cmd,
		    struct ieee80211_sta *sta)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct iwl_mld_sta *mld_sta = sta ? iwl_mld_sta_from_mac80211(sta) :
					    NULL;
	struct iwl_tx_cmd_gen3 *tx_cmd;
	bool amsdu = ieee80211_is_data_qos(hdr->frame_control) &&
		     (*ieee80211_get_qos_ctl(hdr) &
		      IEEE80211_QOS_CTL_A_MSDU_PRESENT);
	__le32 rate_n_flags = 0;
	u16 flags = 0;

	dev_tx_cmd->hdr.cmd = TX_CMD;

	if (!info->control.hw_key)
		flags |= IWL_TX_FLAGS_ENCRYPT_DIS;

	/* For data and mgmt packets rate info comes from the fw.
	 * Only set rate/antenna for injected frames with fixed rate, or
	 * when no sta is given.
	 */
	if (unlikely(!sta ||
		     info->control.flags & IEEE80211_TX_CTRL_RATE_INJECT)) {
		flags |= IWL_TX_FLAGS_CMD_RATE;
		rate_n_flags = iwl_mld_get_tx_rate_n_flags(mld, info, sta,
							   hdr->frame_control);
	} else if (!ieee80211_is_data(hdr->frame_control) ||
		   (mld_sta &&
		    mld_sta->sta_state < IEEE80211_STA_AUTHORIZED)) {
		/* These are important frames */
		flags |= IWL_TX_FLAGS_HIGH_PRI;
	}

	tx_cmd = (void *)dev_tx_cmd->payload;

	iwl_mld_fill_tx_cmd_hdr(tx_cmd, skb, amsdu);

	tx_cmd->offload_assist = iwl_mld_get_offload_assist(skb, amsdu);

	/* Total # bytes to be transmitted */
	tx_cmd->len = cpu_to_le16((u16)skb->len);

	tx_cmd->flags = cpu_to_le16(flags);

	tx_cmd->rate_n_flags = rate_n_flags;
}

/* Caller of this need to check that info->control.vif is not NULL */
static struct iwl_mld_link *
iwl_mld_get_link_from_tx_info(struct ieee80211_tx_info *info)
{
	struct iwl_mld_vif *mld_vif =
		iwl_mld_vif_from_mac80211(info->control.vif);
	u32 link_id = u32_get_bits(info->control.flags,
				   IEEE80211_TX_CTRL_MLO_LINK);

	if (link_id == IEEE80211_LINK_UNSPECIFIED) {
		if (info->control.vif->active_links)
			link_id = ffs(info->control.vif->active_links) - 1;
		else
			link_id = 0;
	}

	return rcu_dereference(mld_vif->link[link_id]);
}

static int
iwl_mld_get_tx_queue_id(struct iwl_mld *mld, struct ieee80211_txq *txq,
			struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;
	struct iwl_mld_vif *mld_vif;
	struct iwl_mld_link *link;

	if (txq && txq->sta)
		return iwl_mld_txq_from_mac80211(txq)->fw_id;

	if (!info->control.vif)
		return IWL_MLD_INVALID_QUEUE;

	switch (info->control.vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		link = iwl_mld_get_link_from_tx_info(info);

		if (WARN_ON(!link))
			break;

		/* ucast disassociate/deauth frames without a station might
		 * happen, especially with reason 7 ("Class 3 frame received
		 * from nonassociated STA").
		 */
		if (ieee80211_is_mgmt(fc) &&
		    (!ieee80211_is_bufferable_mmpdu(skb) ||
		     ieee80211_is_deauth(fc) || ieee80211_is_disassoc(fc)))
			return link->bcast_sta.queue_id;

		if (is_multicast_ether_addr(hdr->addr1) &&
		    !ieee80211_has_order(fc))
			return link->mcast_sta.queue_id;

		WARN_ONCE(info->control.vif->type != NL80211_IFTYPE_ADHOC,
			  "Couldn't find a TXQ. fc=0x%02x", le16_to_cpu(fc));
		return link->bcast_sta.queue_id;
	case NL80211_IFTYPE_P2P_DEVICE:
		mld_vif = iwl_mld_vif_from_mac80211(info->control.vif);

		if (mld_vif->roc_activity == ROC_NUM_ACTIVITIES) {
			IWL_DEBUG_DROP(mld, "Drop tx outside ROC\n");
			return IWL_MLD_INVALID_DROP_TX;
		}

		WARN_ON(!ieee80211_is_mgmt(fc));

		return mld_vif->deflink.aux_sta.queue_id;
	case NL80211_IFTYPE_MONITOR:
		mld_vif = iwl_mld_vif_from_mac80211(info->control.vif);
		return mld_vif->deflink.mon_sta.queue_id;
	default:
		WARN_ONCE(1, "Unsupported vif type\n");
		break;
	}

	return IWL_MLD_INVALID_QUEUE;
}

static void iwl_mld_probe_resp_set_noa(struct iwl_mld *mld,
				       struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct iwl_mld_link *mld_link =
		&iwl_mld_vif_from_mac80211(info->control.vif)->deflink;
	struct iwl_probe_resp_data *resp_data;
	u8 *pos;

	if (!info->control.vif->p2p)
		return;

	rcu_read_lock();

	resp_data = rcu_dereference(mld_link->probe_resp_data);
	if (!resp_data)
		goto out;

	if (!resp_data->notif.noa_active)
		goto out;

	if (skb_tailroom(skb) < resp_data->noa_len) {
		if (pskb_expand_head(skb, 0, resp_data->noa_len, GFP_ATOMIC)) {
			IWL_ERR(mld,
				"Failed to reallocate probe resp\n");
			goto out;
		}
	}

	pos = skb_put(skb, resp_data->noa_len);

	*pos++ = WLAN_EID_VENDOR_SPECIFIC;
	/* Set length of IE body (not including ID and length itself) */
	*pos++ = resp_data->noa_len - 2;
	*pos++ = (WLAN_OUI_WFA >> 16) & 0xff;
	*pos++ = (WLAN_OUI_WFA >> 8) & 0xff;
	*pos++ = WLAN_OUI_WFA & 0xff;
	*pos++ = WLAN_OUI_TYPE_WFA_P2P;

	memcpy(pos, &resp_data->notif.noa_attr,
	       resp_data->noa_len - sizeof(struct ieee80211_vendor_ie));

out:
	rcu_read_unlock();
}

/* This function must be called with BHs disabled */
static int iwl_mld_tx_mpdu(struct iwl_mld *mld, struct sk_buff *skb,
			   struct ieee80211_txq *txq)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_sta *sta = txq ? txq->sta : NULL;
	struct iwl_device_tx_cmd *dev_tx_cmd;
	int queue = iwl_mld_get_tx_queue_id(mld, txq, skb);
	u8 tid = IWL_MAX_TID_COUNT;

	if (WARN_ONCE(queue == IWL_MLD_INVALID_QUEUE, "Invalid TX Queue id") ||
	    queue == IWL_MLD_INVALID_DROP_TX)
		return -1;

	if (unlikely(ieee80211_is_any_nullfunc(hdr->frame_control)))
		return -1;

	dev_tx_cmd = iwl_trans_alloc_tx_cmd(mld->trans);
	if (unlikely(!dev_tx_cmd))
		return -1;

	if (unlikely(ieee80211_is_probe_resp(hdr->frame_control))) {
		if (IWL_MLD_NON_TRANSMITTING_AP)
			return -1;

		iwl_mld_probe_resp_set_noa(mld, skb);
	}

	iwl_mld_fill_tx_cmd(mld, skb, dev_tx_cmd, sta);

	if (ieee80211_is_data(hdr->frame_control)) {
		if (ieee80211_is_data_qos(hdr->frame_control))
			tid = ieee80211_get_tid(hdr);
		else
			tid = IWL_TID_NON_QOS;
	}

	IWL_DEBUG_TX(mld, "TX TID:%d from Q:%d len %d\n",
		     tid, queue, skb->len);

	/* From now on, we cannot access info->control */
	memset(&info->status, 0, sizeof(info->status));
	memset(info->driver_data, 0, sizeof(info->driver_data));

	info->driver_data[1] = dev_tx_cmd;

	if (iwl_trans_tx(mld->trans, skb, dev_tx_cmd, queue))
		goto err;

	/* Update low-latency counter when a packet is queued instead
	 * of after TX, it makes sense for early low-latency detection
	 */
	if (sta)
		iwl_mld_low_latency_update_counters(mld, hdr, sta, 0);

	return 0;

err:
	iwl_trans_free_tx_cmd(mld->trans, dev_tx_cmd);
	IWL_DEBUG_TX(mld, "TX from Q:%d dropped\n", queue);
	return -1;
}

#ifdef CONFIG_INET

/* This function handles the segmentation of a large TSO packet into multiple
 * MPDUs, ensuring that the resulting segments conform to AMSDU limits and
 * constraints.
 */
static int iwl_mld_tx_tso_segment(struct iwl_mld *mld, struct sk_buff *skb,
				  struct ieee80211_sta *sta,
				  struct sk_buff_head *mpdus_skbs)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	netdev_features_t netdev_flags = NETIF_F_CSUM_MASK | NETIF_F_SG;
	unsigned int mss = skb_shinfo(skb)->gso_size;
	unsigned int num_subframes, tcp_payload_len, subf_len;
	u16 snap_ip_tcp, pad, max_tid_amsdu_len;
	u8 tid;

	snap_ip_tcp = 8 + skb_network_header_len(skb) + tcp_hdrlen(skb);

	if (!ieee80211_is_data_qos(hdr->frame_control) ||
	    !sta->cur->max_rc_amsdu_len)
		return iwl_tx_tso_segment(skb, 1, netdev_flags, mpdus_skbs);

	/* Do not build AMSDU for IPv6 with extension headers.
	 * Ask stack to segment and checksum the generated MPDUs for us.
	 */
	if (skb->protocol == htons(ETH_P_IPV6) &&
	    ((struct ipv6hdr *)skb_network_header(skb))->nexthdr !=
	    IPPROTO_TCP) {
		netdev_flags &= ~NETIF_F_CSUM_MASK;
		return iwl_tx_tso_segment(skb, 1, netdev_flags, mpdus_skbs);
	}

	tid = ieee80211_get_tid(hdr);
	if (WARN_ON_ONCE(tid >= IWL_MAX_TID_COUNT))
		return -EINVAL;

	max_tid_amsdu_len = sta->cur->max_tid_amsdu_len[tid];
	if (!max_tid_amsdu_len)
		return iwl_tx_tso_segment(skb, 1, netdev_flags, mpdus_skbs);

	/* Sub frame header + SNAP + IP header + TCP header + MSS */
	subf_len = sizeof(struct ethhdr) + snap_ip_tcp + mss;
	pad = (4 - subf_len) & 0x3;

	/* If we have N subframes in the A-MSDU, then the A-MSDU's size is
	 * N * subf_len + (N - 1) * pad.
	 */
	num_subframes = (max_tid_amsdu_len + pad) / (subf_len + pad);

	if (sta->max_amsdu_subframes &&
	    num_subframes > sta->max_amsdu_subframes)
		num_subframes = sta->max_amsdu_subframes;

	tcp_payload_len = skb_tail_pointer(skb) - skb_transport_header(skb) -
		tcp_hdrlen(skb) + skb->data_len;

	/* Make sure we have enough TBs for the A-MSDU:
	 *	2 for each subframe
	 *	1 more for each fragment
	 *	1 more for the potential data in the header
	 */
	if ((num_subframes * 2 + skb_shinfo(skb)->nr_frags + 1) >
	    mld->trans->info.max_skb_frags)
		num_subframes = 1;

	if (num_subframes > 1)
		*ieee80211_get_qos_ctl(hdr) |= IEEE80211_QOS_CTL_A_MSDU_PRESENT;

	/* This skb fits in one single A-MSDU */
	if (tcp_payload_len <= num_subframes * mss) {
		__skb_queue_tail(mpdus_skbs, skb);
		return 0;
	}

	/* Trick the segmentation function to make it create SKBs that can fit
	 * into one A-MSDU.
	 */
	return iwl_tx_tso_segment(skb, num_subframes, netdev_flags, mpdus_skbs);
}

/* Manages TSO (TCP Segmentation Offload) packet transmission by segmenting
 * large packets when necessary and transmitting each segment as MPDU.
 */
static int iwl_mld_tx_tso(struct iwl_mld *mld, struct sk_buff *skb,
			  struct ieee80211_txq *txq)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct sk_buff *orig_skb = skb;
	struct sk_buff_head mpdus_skbs;
	unsigned int payload_len;
	int ret;

	if (WARN_ON(!txq || !txq->sta))
		return -1;

	payload_len = skb_tail_pointer(skb) - skb_transport_header(skb) -
		tcp_hdrlen(skb) + skb->data_len;

	if (payload_len <= skb_shinfo(skb)->gso_size)
		return iwl_mld_tx_mpdu(mld, skb, txq);

	if (!info->control.vif)
		return -1;

	__skb_queue_head_init(&mpdus_skbs);

	ret = iwl_mld_tx_tso_segment(mld, skb, txq->sta, &mpdus_skbs);
	if (ret)
		return ret;

	WARN_ON(skb_queue_empty(&mpdus_skbs));

	while (!skb_queue_empty(&mpdus_skbs)) {
		skb = __skb_dequeue(&mpdus_skbs);

		ret = iwl_mld_tx_mpdu(mld, skb, txq);
		if (!ret)
			continue;

		/* Free skbs created as part of TSO logic that have not yet
		 * been dequeued
		 */
		__skb_queue_purge(&mpdus_skbs);

		/* skb here is not necessarily same as skb that entered
		 * this method, so free it explicitly.
		 */
		if (skb == orig_skb)
			ieee80211_free_txskb(mld->hw, skb);
		else
			kfree_skb(skb);

		/* there was error, but we consumed skb one way or
		 * another, so return 0
		 */
		return 0;
	}

	return 0;
}
#else
static int iwl_mld_tx_tso(struct iwl_mld *mld, struct sk_buff *skb,
			  struct ieee80211_txq *txq)
{
	/* Impossible to get TSO without CONFIG_INET */
	WARN_ON(1);

	return -1;
}
#endif /* CONFIG_INET */

void iwl_mld_tx_skb(struct iwl_mld *mld, struct sk_buff *skb,
		    struct ieee80211_txq *txq)
{
	if (skb_is_gso(skb)) {
		if (!iwl_mld_tx_tso(mld, skb, txq))
			return;
		goto err;
	}

	if (likely(!iwl_mld_tx_mpdu(mld, skb, txq)))
		return;

err:
	ieee80211_free_txskb(mld->hw, skb);
}

void iwl_mld_tx_from_txq(struct iwl_mld *mld, struct ieee80211_txq *txq)
{
	struct iwl_mld_txq *mld_txq = iwl_mld_txq_from_mac80211(txq);
	struct sk_buff *skb = NULL;
	u8 zero_addr[ETH_ALEN] = {};

	/*
	 * No need for threads to be pending here, they can leave the first
	 * taker all the work.
	 *
	 * mld_txq->tx_request logic:
	 *
	 * If 0, no one is currently TXing, set to 1 to indicate current thread
	 * will now start TX and other threads should quit.
	 *
	 * If 1, another thread is currently TXing, set to 2 to indicate to
	 * that thread that there was another request. Since that request may
	 * have raced with the check whether the queue is empty, the TXing
	 * thread should check the queue's status one more time before leaving.
	 * This check is done in order to not leave any TX hanging in the queue
	 * until the next TX invocation (which may not even happen).
	 *
	 * If 2, another thread is currently TXing, and it will already double
	 * check the queue, so do nothing.
	 */
	if (atomic_fetch_add_unless(&mld_txq->tx_request, 1, 2))
		return;

	rcu_read_lock();
	do {
		while (likely(!mld_txq->status.stop_full) &&
		       (skb = ieee80211_tx_dequeue(mld->hw, txq)))
			iwl_mld_tx_skb(mld, skb, txq);
	} while (atomic_dec_return(&mld_txq->tx_request));

	IWL_DEBUG_TX(mld, "TXQ of sta %pM tid %d is now empty\n",
		     txq->sta ? txq->sta->addr : zero_addr, txq->tid);

	rcu_read_unlock();
}

static void iwl_mld_hwrate_to_tx_rate(struct iwl_mld *mld,
				      __le32 rate_n_flags_fw,
				      struct ieee80211_tx_info *info)
{
	enum nl80211_band band = info->band;
	struct ieee80211_tx_rate *tx_rate = &info->status.rates[0];
	u32 rate_n_flags = iwl_v3_rate_from_v2_v3(rate_n_flags_fw,
						  mld->fw_rates_ver_3);
	u32 sgi = rate_n_flags & RATE_MCS_SGI_MSK;
	u32 chan_width = rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK;
	u32 format = rate_n_flags & RATE_MCS_MOD_TYPE_MSK;

	if (sgi)
		tx_rate->flags |= IEEE80211_TX_RC_SHORT_GI;

	switch (chan_width) {
	case RATE_MCS_CHAN_WIDTH_20:
		break;
	case RATE_MCS_CHAN_WIDTH_40:
		tx_rate->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
		break;
	case RATE_MCS_CHAN_WIDTH_80:
		tx_rate->flags |= IEEE80211_TX_RC_80_MHZ_WIDTH;
		break;
	case RATE_MCS_CHAN_WIDTH_160:
		tx_rate->flags |= IEEE80211_TX_RC_160_MHZ_WIDTH;
		break;
	default:
		break;
	}

	switch (format) {
	case RATE_MCS_MOD_TYPE_HT:
		tx_rate->flags |= IEEE80211_TX_RC_MCS;
		tx_rate->idx = RATE_HT_MCS_INDEX(rate_n_flags);
		break;
	case RATE_MCS_MOD_TYPE_VHT:
		ieee80211_rate_set_vht(tx_rate,
				       rate_n_flags & RATE_MCS_CODE_MSK,
				       u32_get_bits(rate_n_flags,
						    RATE_MCS_NSS_MSK) + 1);
		tx_rate->flags |= IEEE80211_TX_RC_VHT_MCS;
		break;
	case RATE_MCS_MOD_TYPE_HE:
	case RATE_MCS_MOD_TYPE_EHT:
		/* mac80211 cannot do this without ieee80211_tx_status_ext()
		 * but it only matters for radiotap
		 */
		tx_rate->idx = 0;
		break;
	default:
		tx_rate->idx =
			iwl_mld_legacy_hw_idx_to_mac80211_idx(rate_n_flags,
							      band);
		break;
	}
}

void iwl_mld_handle_tx_resp_notif(struct iwl_mld *mld,
				  struct iwl_rx_packet *pkt)
{
	struct iwl_tx_resp *tx_resp = (void *)pkt->data;
	int txq_id = le16_to_cpu(tx_resp->tx_queue);
	struct agg_tx_status *agg_status = &tx_resp->status;
	u32 status = le16_to_cpu(agg_status->status);
	u32 pkt_len = iwl_rx_packet_payload_len(pkt);
	size_t notif_size = sizeof(*tx_resp) + sizeof(u32);
	int sta_id = IWL_TX_RES_GET_RA(tx_resp->ra_tid);
	int tid = IWL_TX_RES_GET_TID(tx_resp->ra_tid);
	struct ieee80211_link_sta *link_sta;
	struct iwl_mld_sta *mld_sta;
	u16 ssn;
	struct sk_buff_head skbs;
	u8 skb_freed = 0;
	bool mgmt = false;
	bool tx_failure = (status & TX_STATUS_MSK) != TX_STATUS_SUCCESS;

	if (IWL_FW_CHECK(mld, tx_resp->frame_count != 1,
			 "Invalid tx_resp notif frame_count (%d)\n",
			 tx_resp->frame_count))
		return;

	/* validate the size of the variable part of the notif */
	if (IWL_FW_CHECK(mld, notif_size != pkt_len,
			 "Invalid tx_resp notif size (expected=%zu got=%u)\n",
			 notif_size, pkt_len))
		return;

	ssn = le32_to_cpup((__le32 *)agg_status +
			   tx_resp->frame_count) & 0xFFFF;

	__skb_queue_head_init(&skbs);

	/* we can free until ssn % q.n_bd not inclusive */
	iwl_trans_reclaim(mld->trans, txq_id, ssn, &skbs, false);

	while (!skb_queue_empty(&skbs)) {
		struct sk_buff *skb = __skb_dequeue(&skbs);
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		struct ieee80211_hdr *hdr = (void *)skb->data;

		skb_freed++;

		iwl_trans_free_tx_cmd(mld->trans, info->driver_data[1]);

		memset(&info->status, 0, sizeof(info->status));

		info->flags &= ~(IEEE80211_TX_STAT_ACK | IEEE80211_TX_STAT_TX_FILTERED);

		/* inform mac80211 about what happened with the frame */
		switch (status & TX_STATUS_MSK) {
		case TX_STATUS_SUCCESS:
		case TX_STATUS_DIRECT_DONE:
			info->flags |= IEEE80211_TX_STAT_ACK;
			break;
		default:
			break;
		}

		/* If we are freeing multiple frames, mark all the frames
		 * but the first one as acked, since they were acknowledged
		 * before
		 */
		if (skb_freed > 1)
			info->flags |= IEEE80211_TX_STAT_ACK;

		if (tx_failure) {
			enum iwl_fw_ini_time_point tp =
				IWL_FW_INI_TIME_POINT_TX_FAILED;

			if (ieee80211_is_action(hdr->frame_control))
				tp = IWL_FW_INI_TIME_POINT_TX_WFD_ACTION_FRAME_FAILED;
			else if (ieee80211_is_mgmt(hdr->frame_control))
				mgmt = true;

			iwl_dbg_tlv_time_point(&mld->fwrt, tp, NULL);
		}

		iwl_mld_hwrate_to_tx_rate(mld, tx_resp->initial_rate, info);

		if (likely(!iwl_mld_time_sync_frame(mld, skb, hdr->addr1)))
			ieee80211_tx_status_skb(mld->hw, skb);
	}

	IWL_DEBUG_TX_REPLY(mld,
			   "TXQ %d status 0x%08x ssn=%d initial_rate 0x%x retries %d\n",
			   txq_id, status, ssn, le32_to_cpu(tx_resp->initial_rate),
			   tx_resp->failure_frame);

	if (tx_failure && mgmt)
		iwl_mld_toggle_tx_ant(mld, &mld->mgmt_tx_ant);

	if (IWL_FW_CHECK(mld, sta_id >= mld->fw->ucode_capa.num_stations,
			 "Got invalid sta_id (%d)\n", sta_id))
		return;

	rcu_read_lock();

	link_sta = rcu_dereference(mld->fw_id_to_link_sta[sta_id]);
	if (!link_sta) {
		/* This can happen if the TX cmd was sent before pre_rcu_remove
		 * but the TX response was received after
		 */
		IWL_DEBUG_TX_REPLY(mld,
				   "Got valid sta_id (%d) but sta is NULL\n",
				   sta_id);
		goto out;
	}

	if (IS_ERR(link_sta))
		goto out;

	mld_sta = iwl_mld_sta_from_mac80211(link_sta->sta);

	if (tx_failure && mld_sta->sta_state < IEEE80211_STA_AUTHORIZED)
		iwl_mld_toggle_tx_ant(mld, &mld_sta->data_tx_ant);

	if (tid < IWL_MAX_TID_COUNT)
		iwl_mld_count_mpdu_tx(link_sta, 1);

out:
	rcu_read_unlock();
}

static void iwl_mld_tx_reclaim_txq(struct iwl_mld *mld, int txq, int index,
				   bool in_flush)
{
	struct sk_buff_head reclaimed_skbs;

	__skb_queue_head_init(&reclaimed_skbs);

	iwl_trans_reclaim(mld->trans, txq, index, &reclaimed_skbs, in_flush);

	while (!skb_queue_empty(&reclaimed_skbs)) {
		struct sk_buff *skb = __skb_dequeue(&reclaimed_skbs);
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		iwl_trans_free_tx_cmd(mld->trans, info->driver_data[1]);

		memset(&info->status, 0, sizeof(info->status));

		/* Packet was transmitted successfully, failures come as single
		 * frames because before failing a frame the firmware transmits
		 * it without aggregation at least once.
		 */
		if (!in_flush)
			info->flags |= IEEE80211_TX_STAT_ACK;
		else
			info->flags &= ~IEEE80211_TX_STAT_ACK;

		ieee80211_tx_status_skb(mld->hw, skb);
	}
}

int iwl_mld_flush_link_sta_txqs(struct iwl_mld *mld, u32 fw_sta_id)
{
	struct iwl_tx_path_flush_cmd_rsp *rsp;
	struct iwl_tx_path_flush_cmd flush_cmd = {
		.sta_id = cpu_to_le32(fw_sta_id),
		.tid_mask = cpu_to_le16(0xffff),
	};
	struct iwl_host_cmd cmd = {
		.id = TXPATH_FLUSH,
		.len = { sizeof(flush_cmd), },
		.data = { &flush_cmd, },
		.flags = CMD_WANT_SKB,
	};
	int ret, num_flushed_queues;
	u32 resp_len;

	IWL_DEBUG_TX_QUEUES(mld, "flush for sta id %d tid mask 0x%x\n",
			    fw_sta_id, 0xffff);

	ret = iwl_mld_send_cmd(mld, &cmd);
	if (ret) {
		IWL_ERR(mld, "Failed to send flush command (%d)\n", ret);
		return ret;
	}

	resp_len = iwl_rx_packet_payload_len(cmd.resp_pkt);
	if (IWL_FW_CHECK(mld, resp_len != sizeof(*rsp),
			 "Invalid TXPATH_FLUSH response len: %d\n",
			 resp_len)) {
		ret = -EIO;
		goto free_rsp;
	}

	rsp = (void *)cmd.resp_pkt->data;

	if (IWL_FW_CHECK(mld, le16_to_cpu(rsp->sta_id) != fw_sta_id,
			 "sta_id %d != rsp_sta_id %d\n", fw_sta_id,
			 le16_to_cpu(rsp->sta_id))) {
		ret = -EIO;
		goto free_rsp;
	}

	num_flushed_queues = le16_to_cpu(rsp->num_flushed_queues);
	if (IWL_FW_CHECK(mld, num_flushed_queues > IWL_TX_FLUSH_QUEUE_RSP,
			 "num_flushed_queues %d\n", num_flushed_queues)) {
		ret = -EIO;
		goto free_rsp;
	}

	for (int i = 0; i < num_flushed_queues; i++) {
		struct iwl_flush_queue_info *queue_info = &rsp->queues[i];
		int read_after = le16_to_cpu(queue_info->read_after_flush);
		int txq_id = le16_to_cpu(queue_info->queue_num);

		if (IWL_FW_CHECK(mld,
				 txq_id >= ARRAY_SIZE(mld->fw_id_to_txq),
				 "Invalid txq id %d\n", txq_id))
			continue;

		IWL_DEBUG_TX_QUEUES(mld,
				    "tid %d txq_id %d read-before %d read-after %d\n",
				    le16_to_cpu(queue_info->tid), txq_id,
				    le16_to_cpu(queue_info->read_before_flush),
				    read_after);

		iwl_mld_tx_reclaim_txq(mld, txq_id, read_after, true);
	}

free_rsp:
	iwl_free_resp(&cmd);
	return ret;
}

int iwl_mld_ensure_queue(struct iwl_mld *mld, struct ieee80211_txq *txq)
{
	struct iwl_mld_txq *mld_txq = iwl_mld_txq_from_mac80211(txq);
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (likely(mld_txq->status.allocated))
		return 0;

	ret = iwl_mld_add_txq(mld, txq);

	spin_lock_bh(&mld->add_txqs_lock);
	if (!list_empty(&mld_txq->list))
		list_del_init(&mld_txq->list);
	spin_unlock_bh(&mld->add_txqs_lock);

	return ret;
}

int iwl_mld_update_sta_txqs(struct iwl_mld *mld,
			    struct ieee80211_sta *sta,
			    u32 old_sta_mask, u32 new_sta_mask)
{
	struct iwl_scd_queue_cfg_cmd cmd = {
		.operation = cpu_to_le32(IWL_SCD_QUEUE_MODIFY),
		.u.modify.old_sta_mask = cpu_to_le32(old_sta_mask),
		.u.modify.new_sta_mask = cpu_to_le32(new_sta_mask),
	};

	lockdep_assert_wiphy(mld->wiphy);

	for (int tid = 0; tid <= IWL_MAX_TID_COUNT; tid++) {
		struct ieee80211_txq *txq =
			sta->txq[tid != IWL_MAX_TID_COUNT ?
					tid : IEEE80211_NUM_TIDS];
		struct iwl_mld_txq *mld_txq =
			iwl_mld_txq_from_mac80211(txq);
		int ret;

		if (!mld_txq->status.allocated)
			continue;

		if (tid == IWL_MAX_TID_COUNT)
			cmd.u.modify.tid = cpu_to_le32(IWL_MGMT_TID);
		else
			cmd.u.modify.tid = cpu_to_le32(tid);

		ret = iwl_mld_send_cmd_pdu(mld,
					   WIDE_ID(DATA_PATH_GROUP,
						   SCD_QUEUE_CONFIG_CMD),
					   &cmd);
		if (ret)
			return ret;
	}

	return 0;
}

void iwl_mld_handle_compressed_ba_notif(struct iwl_mld *mld,
					struct iwl_rx_packet *pkt)
{
	struct iwl_compressed_ba_notif *ba_res = (void *)pkt->data;
	u32 pkt_len = iwl_rx_packet_payload_len(pkt);
	u16 tfd_cnt = le16_to_cpu(ba_res->tfd_cnt);
	u8 sta_id = ba_res->sta_id;
	struct ieee80211_link_sta *link_sta;

	if (!tfd_cnt)
		return;

	if (IWL_FW_CHECK(mld, struct_size(ba_res, tfd, tfd_cnt) > pkt_len,
			 "Short BA notif (tfd_cnt=%d, size:0x%x)\n",
			 tfd_cnt, pkt_len))
		return;

	IWL_DEBUG_TX_REPLY(mld,
			   "BA notif received from sta_id=%d, flags=0x%x, sent:%d, acked:%d\n",
			   sta_id, le32_to_cpu(ba_res->flags),
			   le16_to_cpu(ba_res->txed),
			   le16_to_cpu(ba_res->done));

	for (int i = 0; i < tfd_cnt; i++) {
		struct iwl_compressed_ba_tfd *ba_tfd = &ba_res->tfd[i];
		int txq_id = le16_to_cpu(ba_tfd->q_num);
		int index = le16_to_cpu(ba_tfd->tfd_index);

		if (IWL_FW_CHECK(mld,
				 txq_id >= ARRAY_SIZE(mld->fw_id_to_txq),
				 "Invalid txq id %d\n", txq_id))
			continue;

		iwl_mld_tx_reclaim_txq(mld, txq_id, index, false);
	}

	if (IWL_FW_CHECK(mld, sta_id >= mld->fw->ucode_capa.num_stations,
			 "Got invalid sta_id (%d)\n", sta_id))
		return;

	rcu_read_lock();

	link_sta = rcu_dereference(mld->fw_id_to_link_sta[sta_id]);
	if (IWL_FW_CHECK(mld, IS_ERR_OR_NULL(link_sta),
			 "Got valid sta_id (%d) but link_sta is NULL\n",
			 sta_id))
		goto out;

	iwl_mld_count_mpdu_tx(link_sta, le16_to_cpu(ba_res->txed));
out:
	rcu_read_unlock();
}
