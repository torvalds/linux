// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2023 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include <linux/tcp.h>
#include <net/gso.h>
#include <net/ip.h>
#include <net/ipv6.h>

#include "iwl-trans.h"
#include "iwl-eeprom-parse.h"
#include "mvm.h"
#include "sta.h"
#include "time-sync.h"

static void
iwl_mvm_bar_check_trigger(struct iwl_mvm *mvm, const u8 *addr,
			  u16 tid, u16 ssn)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_ba *ba_trig;

	trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, NULL, FW_DBG_TRIGGER_BA);
	if (!trig)
		return;

	ba_trig = (void *)trig->data;

	if (!(le16_to_cpu(ba_trig->tx_bar) & BIT(tid)))
		return;

	iwl_fw_dbg_collect_trig(&mvm->fwrt, trig,
				"BAR sent to %pM, tid %d, ssn %d",
				addr, tid, ssn);
}

#define OPT_HDR(type, skb, off) \
	(type *)(skb_network_header(skb) + (off))

static u32 iwl_mvm_tx_csum(struct iwl_mvm *mvm, struct sk_buff *skb,
			   struct ieee80211_tx_info *info,
			   bool amsdu)
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
	if (WARN_ONCE(!(mvm->hw->netdev_features & IWL_TX_CSUM_NETIF_FLAGS) ||
		      (skb->protocol != htons(ETH_P_IP) &&
		       skb->protocol != htons(ETH_P_IPV6)),
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

	/*
	 * Set offset to IP header (snap).
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
	/*
	 * mac header len should include IV, size is in words unless
	 * the IV is added by the firmware like in WEP.
	 * In new Tx API, the IV is always added by the firmware.
	 */
	if (!iwl_mvm_has_new_tx_api(mvm) && info->control.hw_key &&
	    info->control.hw_key->cipher != WLAN_CIPHER_SUITE_WEP40 &&
	    info->control.hw_key->cipher != WLAN_CIPHER_SUITE_WEP104)
		mh_len += info->control.hw_key->iv_len;
	mh_len /= 2;
	offload_assist |= mh_len << TX_CMD_OFFLD_MH_SIZE;

	if (amsdu)
		offload_assist |= BIT(TX_CMD_OFFLD_AMSDU);
	else if (ieee80211_hdrlen(hdr->frame_control) % 4)
		/* padding is inserted later in transport */
		offload_assist |= BIT(TX_CMD_OFFLD_PAD);

	return offload_assist;
}

/*
 * Sets most of the Tx cmd's fields
 */
void iwl_mvm_set_tx_cmd(struct iwl_mvm *mvm, struct sk_buff *skb,
			struct iwl_tx_cmd *tx_cmd,
			struct ieee80211_tx_info *info, u8 sta_id)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;
	u32 tx_flags = le32_to_cpu(tx_cmd->tx_flags);
	u32 len = skb->len + FCS_LEN;
	bool amsdu = false;
	u8 ac;

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK) ||
	    (ieee80211_is_probe_resp(fc) &&
	     !is_multicast_ether_addr(hdr->addr1)))
		tx_flags |= TX_CMD_FLG_ACK;
	else
		tx_flags &= ~TX_CMD_FLG_ACK;

	if (ieee80211_is_probe_resp(fc))
		tx_flags |= TX_CMD_FLG_TSF;

	if (ieee80211_has_morefrags(fc))
		tx_flags |= TX_CMD_FLG_MORE_FRAG;

	if (ieee80211_is_data_qos(fc)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		tx_cmd->tid_tspec = qc[0] & 0xf;
		tx_flags &= ~TX_CMD_FLG_SEQ_CTL;
		amsdu = *qc & IEEE80211_QOS_CTL_A_MSDU_PRESENT;
	} else if (ieee80211_is_back_req(fc)) {
		struct ieee80211_bar *bar = (void *)skb->data;
		u16 control = le16_to_cpu(bar->control);
		u16 ssn = le16_to_cpu(bar->start_seq_num);

		tx_flags |= TX_CMD_FLG_ACK | TX_CMD_FLG_BAR;
		tx_cmd->tid_tspec = (control &
				     IEEE80211_BAR_CTRL_TID_INFO_MASK) >>
			IEEE80211_BAR_CTRL_TID_INFO_SHIFT;
		WARN_ON_ONCE(tx_cmd->tid_tspec >= IWL_MAX_TID_COUNT);
		iwl_mvm_bar_check_trigger(mvm, bar->ra, tx_cmd->tid_tspec,
					  ssn);
	} else {
		if (ieee80211_is_data(fc))
			tx_cmd->tid_tspec = IWL_TID_NON_QOS;
		else
			tx_cmd->tid_tspec = IWL_MAX_TID_COUNT;

		if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ)
			tx_flags |= TX_CMD_FLG_SEQ_CTL;
		else
			tx_flags &= ~TX_CMD_FLG_SEQ_CTL;
	}

	/* Default to 0 (BE) when tid_spec is set to IWL_MAX_TID_COUNT */
	if (tx_cmd->tid_tspec < IWL_MAX_TID_COUNT)
		ac = tid_to_mac80211_ac[tx_cmd->tid_tspec];
	else
		ac = tid_to_mac80211_ac[0];

	tx_flags |= iwl_mvm_bt_coex_tx_prio(mvm, hdr, info, ac) <<
			TX_CMD_FLG_BT_PRIO_POS;

	if (ieee80211_is_mgmt(fc)) {
		if (ieee80211_is_assoc_req(fc) || ieee80211_is_reassoc_req(fc))
			tx_cmd->pm_frame_timeout = cpu_to_le16(PM_FRAME_ASSOC);
		else if (ieee80211_is_action(fc))
			tx_cmd->pm_frame_timeout = cpu_to_le16(PM_FRAME_NONE);
		else
			tx_cmd->pm_frame_timeout = cpu_to_le16(PM_FRAME_MGMT);

		/* The spec allows Action frames in A-MPDU, we don't support
		 * it
		 */
		WARN_ON_ONCE(info->flags & IEEE80211_TX_CTL_AMPDU);
	} else if (info->control.flags & IEEE80211_TX_CTRL_PORT_CTRL_PROTO) {
		tx_cmd->pm_frame_timeout = cpu_to_le16(PM_FRAME_MGMT);
	} else {
		tx_cmd->pm_frame_timeout = cpu_to_le16(PM_FRAME_NONE);
	}

	if (ieee80211_is_data(fc) && len > mvm->rts_threshold &&
	    !is_multicast_ether_addr(hdr->addr1))
		tx_flags |= TX_CMD_FLG_PROT_REQUIRE;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT) &&
	    ieee80211_action_contains_tpc(skb))
		tx_flags |= TX_CMD_FLG_WRITE_TX_POWER;

	tx_cmd->tx_flags = cpu_to_le32(tx_flags);
	/* Total # bytes to be transmitted - PCIe code will adjust for A-MSDU */
	tx_cmd->len = cpu_to_le16((u16)skb->len);
	tx_cmd->life_time = cpu_to_le32(TX_CMD_LIFE_TIME_INFINITE);
	tx_cmd->sta_id = sta_id;

	tx_cmd->offload_assist =
		cpu_to_le16(iwl_mvm_tx_csum(mvm, skb, info, amsdu));
}

static u32 iwl_mvm_get_tx_ant(struct iwl_mvm *mvm,
			      struct ieee80211_tx_info *info,
			      struct ieee80211_sta *sta, __le16 fc)
{
	if (info->band == NL80211_BAND_2GHZ &&
	    !iwl_mvm_bt_coex_is_shared_ant_avail(mvm))
		return mvm->cfg->non_shared_ant << RATE_MCS_ANT_POS;

	if (sta && ieee80211_is_data(fc)) {
		struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);

		return BIT(mvmsta->tx_ant) << RATE_MCS_ANT_POS;
	}

	return BIT(mvm->mgmt_last_antenna_idx) << RATE_MCS_ANT_POS;
}

static u32 iwl_mvm_convert_rate_idx(struct iwl_mvm *mvm,
				    struct ieee80211_tx_info *info,
				    int rate_idx)
{
	u32 rate_flags = 0;
	u8 rate_plcp;
	bool is_cck;

	/* if the rate isn't a well known legacy rate, take the lowest one */
	if (rate_idx < 0 || rate_idx >= IWL_RATE_COUNT_LEGACY)
		rate_idx = iwl_mvm_mac_ctxt_get_lowest_rate(mvm,
							    info,
							    info->control.vif);

	/* Get PLCP rate for tx_cmd->rate_n_flags */
	rate_plcp = iwl_mvm_mac80211_idx_to_hwrate(mvm->fw, rate_idx);
	is_cck = (rate_idx >= IWL_FIRST_CCK_RATE) &&
		 (rate_idx <= IWL_LAST_CCK_RATE);

	/* Set CCK or OFDM flag */
	if (iwl_fw_lookup_cmd_ver(mvm->fw, TX_CMD, 0) > 8) {
		if (!is_cck)
			rate_flags |= RATE_MCS_LEGACY_OFDM_MSK;
		else
			rate_flags |= RATE_MCS_CCK_MSK;
	} else if (is_cck) {
		rate_flags |= RATE_MCS_CCK_MSK_V1;
	}

	return (u32)rate_plcp | rate_flags;
}

static u32 iwl_mvm_get_inject_tx_rate(struct iwl_mvm *mvm,
				      struct ieee80211_tx_info *info,
				      struct ieee80211_sta *sta,
				      __le16 fc)
{
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	u32 result;

	/*
	 * we only care about legacy/HT/VHT so far, so we can
	 * build in v1 and use iwl_new_rate_from_v1()
	 */

	if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
		u8 mcs = ieee80211_rate_get_vht_mcs(rate);
		u8 nss = ieee80211_rate_get_vht_nss(rate);

		result = RATE_MCS_VHT_MSK_V1;
		result |= u32_encode_bits(mcs, RATE_VHT_MCS_RATE_CODE_MSK);
		result |= u32_encode_bits(nss, RATE_MCS_NSS_MSK);
		if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
			result |= RATE_MCS_SGI_MSK_V1;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			result |= u32_encode_bits(1, RATE_MCS_CHAN_WIDTH_MSK_V1);
		else if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			result |= u32_encode_bits(2, RATE_MCS_CHAN_WIDTH_MSK_V1);
		else if (rate->flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
			result |= u32_encode_bits(3, RATE_MCS_CHAN_WIDTH_MSK_V1);

		if (iwl_fw_lookup_notif_ver(mvm->fw, LONG_GROUP, TX_CMD, 0) > 6)
			result = iwl_new_rate_from_v1(result);
	} else if (rate->flags & IEEE80211_TX_RC_MCS) {
		result = RATE_MCS_HT_MSK_V1;
		result |= u32_encode_bits(rate->idx,
					  RATE_HT_MCS_RATE_CODE_MSK_V1 |
					  RATE_HT_MCS_NSS_MSK_V1);
		if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
			result |= RATE_MCS_SGI_MSK_V1;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			result |= u32_encode_bits(1, RATE_MCS_CHAN_WIDTH_MSK_V1);
		if (info->flags & IEEE80211_TX_CTL_LDPC)
			result |= RATE_MCS_LDPC_MSK_V1;
		if (u32_get_bits(info->flags, IEEE80211_TX_CTL_STBC))
			result |= RATE_MCS_STBC_MSK;

		if (iwl_fw_lookup_notif_ver(mvm->fw, LONG_GROUP, TX_CMD, 0) > 6)
			result = iwl_new_rate_from_v1(result);
	} else {
		int rate_idx = info->control.rates[0].idx;

		result = iwl_mvm_convert_rate_idx(mvm, info, rate_idx);
	}

	if (info->control.antennas)
		result |= u32_encode_bits(info->control.antennas,
					  RATE_MCS_ANT_AB_MSK);
	else
		result |= iwl_mvm_get_tx_ant(mvm, info, sta, fc);

	return result;
}

static u32 iwl_mvm_get_tx_rate(struct iwl_mvm *mvm,
			       struct ieee80211_tx_info *info,
			       struct ieee80211_sta *sta, __le16 fc)
{
	int rate_idx = -1;

	if (!ieee80211_hw_check(mvm->hw, HAS_RATE_CONTROL)) {
		/* info->control is only relevant for non HW rate control */

		/* HT rate doesn't make sense for a non data frame */
		WARN_ONCE(info->control.rates[0].flags & IEEE80211_TX_RC_MCS &&
			  !ieee80211_is_data(fc),
			  "Got a HT rate (flags:0x%x/mcs:%d/fc:0x%x/state:%d) for a non data frame\n",
			  info->control.rates[0].flags,
			  info->control.rates[0].idx,
			  le16_to_cpu(fc),
			  sta ? iwl_mvm_sta_from_mac80211(sta)->sta_state : -1);

		rate_idx = info->control.rates[0].idx;

		/* For non 2 GHZ band, remap mac80211 rate indices into driver
		 * indices.
		 */
		if (info->band != NL80211_BAND_2GHZ ||
		    (info->flags & IEEE80211_TX_CTL_NO_CCK_RATE))
			rate_idx += IWL_FIRST_OFDM_RATE;

		/* For 2.4 GHZ band, check that there is no need to remap */
		BUILD_BUG_ON(IWL_FIRST_CCK_RATE != 0);
	}

	return iwl_mvm_convert_rate_idx(mvm, info, rate_idx);
}

static u32 iwl_mvm_get_tx_rate_n_flags(struct iwl_mvm *mvm,
				       struct ieee80211_tx_info *info,
				       struct ieee80211_sta *sta, __le16 fc)
{
	if (unlikely(info->control.flags & IEEE80211_TX_CTRL_RATE_INJECT))
		return iwl_mvm_get_inject_tx_rate(mvm, info, sta, fc);

	return iwl_mvm_get_tx_rate(mvm, info, sta, fc) |
		iwl_mvm_get_tx_ant(mvm, info, sta, fc);
}

/*
 * Sets the fields in the Tx cmd that are rate related
 */
void iwl_mvm_set_tx_cmd_rate(struct iwl_mvm *mvm, struct iwl_tx_cmd *tx_cmd,
			    struct ieee80211_tx_info *info,
			    struct ieee80211_sta *sta, __le16 fc)
{
	/* Set retry limit on RTS packets */
	tx_cmd->rts_retry_limit = IWL_RTS_DFAULT_RETRY_LIMIT;

	/* Set retry limit on DATA packets and Probe Responses*/
	if (ieee80211_is_probe_resp(fc)) {
		tx_cmd->data_retry_limit = IWL_MGMT_DFAULT_RETRY_LIMIT;
		tx_cmd->rts_retry_limit =
			min(tx_cmd->data_retry_limit, tx_cmd->rts_retry_limit);
	} else if (ieee80211_is_back_req(fc)) {
		tx_cmd->data_retry_limit = IWL_BAR_DFAULT_RETRY_LIMIT;
	} else {
		tx_cmd->data_retry_limit = IWL_DEFAULT_TX_RETRY;
	}

	/*
	 * for data packets, rate info comes from the table inside the fw. This
	 * table is controlled by LINK_QUALITY commands
	 */

	if (likely(ieee80211_is_data(fc) && sta &&
		   !(info->control.flags & IEEE80211_TX_CTRL_RATE_INJECT))) {
		struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);

		if (mvmsta->sta_state >= IEEE80211_STA_AUTHORIZED) {
			tx_cmd->initial_rate_index = 0;
			tx_cmd->tx_flags |= cpu_to_le32(TX_CMD_FLG_STA_RATE);
			return;
		}
	} else if (ieee80211_is_back_req(fc)) {
		tx_cmd->tx_flags |=
			cpu_to_le32(TX_CMD_FLG_ACK | TX_CMD_FLG_BAR);
	}

	/* Set the rate in the TX cmd */
	tx_cmd->rate_n_flags =
		cpu_to_le32(iwl_mvm_get_tx_rate_n_flags(mvm, info, sta, fc));
}

static inline void iwl_mvm_set_tx_cmd_pn(struct ieee80211_tx_info *info,
					 u8 *crypto_hdr)
{
	struct ieee80211_key_conf *keyconf = info->control.hw_key;
	u64 pn;

	pn = atomic64_inc_return(&keyconf->tx_pn);
	crypto_hdr[0] = pn;
	crypto_hdr[2] = 0;
	crypto_hdr[3] = 0x20 | (keyconf->keyidx << 6);
	crypto_hdr[1] = pn >> 8;
	crypto_hdr[4] = pn >> 16;
	crypto_hdr[5] = pn >> 24;
	crypto_hdr[6] = pn >> 32;
	crypto_hdr[7] = pn >> 40;
}

/*
 * Sets the fields in the Tx cmd that are crypto related
 */
static void iwl_mvm_set_tx_cmd_crypto(struct iwl_mvm *mvm,
				      struct ieee80211_tx_info *info,
				      struct iwl_tx_cmd *tx_cmd,
				      struct sk_buff *skb_frag,
				      int hdrlen)
{
	struct ieee80211_key_conf *keyconf = info->control.hw_key;
	u8 *crypto_hdr = skb_frag->data + hdrlen;
	enum iwl_tx_cmd_sec_ctrl type = TX_CMD_SEC_CCM;
	u64 pn;

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		iwl_mvm_set_tx_cmd_ccmp(info, tx_cmd);
		iwl_mvm_set_tx_cmd_pn(info, crypto_hdr);
		break;

	case WLAN_CIPHER_SUITE_TKIP:
		tx_cmd->sec_ctl = TX_CMD_SEC_TKIP;
		pn = atomic64_inc_return(&keyconf->tx_pn);
		ieee80211_tkip_add_iv(crypto_hdr, keyconf, pn);
		ieee80211_get_tkip_p2k(keyconf, skb_frag, tx_cmd->key);
		break;

	case WLAN_CIPHER_SUITE_WEP104:
		tx_cmd->sec_ctl |= TX_CMD_SEC_KEY128;
		fallthrough;
	case WLAN_CIPHER_SUITE_WEP40:
		tx_cmd->sec_ctl |= TX_CMD_SEC_WEP |
			((keyconf->keyidx << TX_CMD_SEC_WEP_KEY_IDX_POS) &
			  TX_CMD_SEC_WEP_KEY_IDX_MSK);

		memcpy(&tx_cmd->key[3], keyconf->key, keyconf->keylen);
		break;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		type = TX_CMD_SEC_GCMP;
		fallthrough;
	case WLAN_CIPHER_SUITE_CCMP_256:
		/* TODO: Taking the key from the table might introduce a race
		 * when PTK rekeying is done, having an old packets with a PN
		 * based on the old key but the message encrypted with a new
		 * one.
		 * Need to handle this.
		 */
		tx_cmd->sec_ctl |= type | TX_CMD_SEC_KEY_FROM_TABLE;
		tx_cmd->key[0] = keyconf->hw_key_idx;
		iwl_mvm_set_tx_cmd_pn(info, crypto_hdr);
		break;
	default:
		tx_cmd->sec_ctl |= TX_CMD_SEC_EXT;
	}
}

/*
 * Allocates and sets the Tx cmd the driver data pointers in the skb
 */
static struct iwl_device_tx_cmd *
iwl_mvm_set_tx_params(struct iwl_mvm *mvm, struct sk_buff *skb,
		      struct ieee80211_tx_info *info, int hdrlen,
		      struct ieee80211_sta *sta, u8 sta_id)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct iwl_device_tx_cmd *dev_cmd;
	struct iwl_tx_cmd *tx_cmd;

	dev_cmd = iwl_trans_alloc_tx_cmd(mvm->trans);

	if (unlikely(!dev_cmd))
		return NULL;

	dev_cmd->hdr.cmd = TX_CMD;

	if (iwl_mvm_has_new_tx_api(mvm)) {
		u32 rate_n_flags = 0;
		u16 flags = 0;
		struct iwl_mvm_sta *mvmsta = sta ?
			iwl_mvm_sta_from_mac80211(sta) : NULL;
		bool amsdu = false;

		if (ieee80211_is_data_qos(hdr->frame_control)) {
			u8 *qc = ieee80211_get_qos_ctl(hdr);

			amsdu = *qc & IEEE80211_QOS_CTL_A_MSDU_PRESENT;
		}

		if (!info->control.hw_key)
			flags |= IWL_TX_FLAGS_ENCRYPT_DIS;

		/*
		 * For data and mgmt packets rate info comes from the fw. Only
		 * set rate/antenna for injected frames with fixed rate, or
		 * when no sta is given.
		 */
		if (unlikely(!sta ||
			     info->control.flags & IEEE80211_TX_CTRL_RATE_INJECT)) {
			flags |= IWL_TX_FLAGS_CMD_RATE;
			rate_n_flags =
				iwl_mvm_get_tx_rate_n_flags(mvm, info, sta,
							    hdr->frame_control);
		} else if (!ieee80211_is_data(hdr->frame_control) ||
			   mvmsta->sta_state < IEEE80211_STA_AUTHORIZED) {
			/* These are important frames */
			flags |= IWL_TX_FLAGS_HIGH_PRI;
		}

		if (mvm->trans->trans_cfg->device_family >=
		    IWL_DEVICE_FAMILY_AX210) {
			struct iwl_tx_cmd_gen3 *cmd = (void *)dev_cmd->payload;
			u32 offload_assist = iwl_mvm_tx_csum(mvm, skb,
							     info, amsdu);

			cmd->offload_assist = cpu_to_le32(offload_assist);

			/* Total # bytes to be transmitted */
			cmd->len = cpu_to_le16((u16)skb->len);

			/* Copy MAC header from skb into command buffer */
			memcpy(cmd->hdr, hdr, hdrlen);

			cmd->flags = cpu_to_le16(flags);
			cmd->rate_n_flags = cpu_to_le32(rate_n_flags);
		} else {
			struct iwl_tx_cmd_gen2 *cmd = (void *)dev_cmd->payload;
			u16 offload_assist = iwl_mvm_tx_csum(mvm, skb,
							     info, amsdu);

			cmd->offload_assist = cpu_to_le16(offload_assist);

			/* Total # bytes to be transmitted */
			cmd->len = cpu_to_le16((u16)skb->len);

			/* Copy MAC header from skb into command buffer */
			memcpy(cmd->hdr, hdr, hdrlen);

			cmd->flags = cpu_to_le32(flags);
			cmd->rate_n_flags = cpu_to_le32(rate_n_flags);
		}
		goto out;
	}

	tx_cmd = (struct iwl_tx_cmd *)dev_cmd->payload;

	if (info->control.hw_key)
		iwl_mvm_set_tx_cmd_crypto(mvm, info, tx_cmd, skb, hdrlen);

	iwl_mvm_set_tx_cmd(mvm, skb, tx_cmd, info, sta_id);

	iwl_mvm_set_tx_cmd_rate(mvm, tx_cmd, info, sta, hdr->frame_control);

	/* Copy MAC header from skb into command buffer */
	memcpy(tx_cmd->hdr, hdr, hdrlen);

out:
	return dev_cmd;
}

static void iwl_mvm_skb_prepare_status(struct sk_buff *skb,
				       struct iwl_device_tx_cmd *cmd)
{
	struct ieee80211_tx_info *skb_info = IEEE80211_SKB_CB(skb);

	memset(&skb_info->status, 0, sizeof(skb_info->status));
	memset(skb_info->driver_data, 0, sizeof(skb_info->driver_data));

	skb_info->driver_data[1] = cmd;
}

static int iwl_mvm_get_ctrl_vif_queue(struct iwl_mvm *mvm,
				      struct iwl_mvm_vif_link_info *link,
				      struct ieee80211_tx_info *info,
				      struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;

	switch (info->control.vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		/*
		 * Non-bufferable frames use the broadcast station, thus they
		 * use the probe queue.
		 * Also take care of the case where we send a deauth to a
		 * station that we don't have, or similarly an association
		 * response (with non-success status) for a station we can't
		 * accept.
		 * Also, disassociate frames might happen, particular with
		 * reason 7 ("Class 3 frame received from nonassociated STA").
		 */
		if (ieee80211_is_mgmt(fc) &&
		    (!ieee80211_is_bufferable_mmpdu(skb) ||
		     ieee80211_is_deauth(fc) || ieee80211_is_disassoc(fc)))
			return link->mgmt_queue;

		if (!ieee80211_has_order(fc) && !ieee80211_is_probe_req(fc) &&
		    is_multicast_ether_addr(hdr->addr1))
			return link->cab_queue;

		WARN_ONCE(info->control.vif->type != NL80211_IFTYPE_ADHOC,
			  "fc=0x%02x", le16_to_cpu(fc));
		return link->mgmt_queue;
	case NL80211_IFTYPE_P2P_DEVICE:
		if (ieee80211_is_mgmt(fc))
			return mvm->p2p_dev_queue;

		WARN_ON_ONCE(1);
		return mvm->p2p_dev_queue;
	default:
		WARN_ONCE(1, "Not a ctrl vif, no available queue\n");
		return -1;
	}
}

static void iwl_mvm_probe_resp_set_noa(struct iwl_mvm *mvm,
				       struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct iwl_mvm_vif *mvmvif =
		iwl_mvm_vif_from_mac80211(info->control.vif);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	int base_len = (u8 *)mgmt->u.probe_resp.variable - (u8 *)mgmt;
	struct iwl_probe_resp_data *resp_data;
	const u8 *ie;
	u8 *pos;
	u8 match[] = {
		(WLAN_OUI_WFA >> 16) & 0xff,
		(WLAN_OUI_WFA >> 8) & 0xff,
		WLAN_OUI_WFA & 0xff,
		WLAN_OUI_TYPE_WFA_P2P,
	};

	rcu_read_lock();

	resp_data = rcu_dereference(mvmvif->deflink.probe_resp_data);
	if (!resp_data)
		goto out;

	if (!resp_data->notif.noa_active)
		goto out;

	ie = cfg80211_find_ie_match(WLAN_EID_VENDOR_SPECIFIC,
				    mgmt->u.probe_resp.variable,
				    skb->len - base_len,
				    match, 4, 2);
	if (!ie) {
		IWL_DEBUG_TX(mvm, "probe resp doesn't have P2P IE\n");
		goto out;
	}

	if (skb_tailroom(skb) < resp_data->noa_len) {
		if (pskb_expand_head(skb, 0, resp_data->noa_len, GFP_ATOMIC)) {
			IWL_ERR(mvm,
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

int iwl_mvm_tx_skb_non_sta(struct iwl_mvm *mvm, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info info;
	struct iwl_device_tx_cmd *dev_cmd;
	u8 sta_id;
	int hdrlen = ieee80211_hdrlen(hdr->frame_control);
	__le16 fc = hdr->frame_control;
	bool offchannel = IEEE80211_SKB_CB(skb)->flags &
		IEEE80211_TX_CTL_TX_OFFCHAN;
	int queue = -1;

	if (IWL_MVM_NON_TRANSMITTING_AP && ieee80211_is_probe_resp(fc))
		return -1;

	memcpy(&info, skb->cb, sizeof(info));

	if (WARN_ON_ONCE(skb->len > IEEE80211_MAX_DATA_LEN + hdrlen))
		return -1;

	if (WARN_ON_ONCE(info.flags & IEEE80211_TX_CTL_AMPDU))
		return -1;

	if (info.control.vif) {
		struct iwl_mvm_vif *mvmvif =
			iwl_mvm_vif_from_mac80211(info.control.vif);

		if (info.control.vif->type == NL80211_IFTYPE_P2P_DEVICE ||
		    info.control.vif->type == NL80211_IFTYPE_AP ||
		    info.control.vif->type == NL80211_IFTYPE_ADHOC) {
			u32 link_id = u32_get_bits(info.control.flags,
						   IEEE80211_TX_CTRL_MLO_LINK);
			struct iwl_mvm_vif_link_info *link;

			if (link_id == IEEE80211_LINK_UNSPECIFIED) {
				if (info.control.vif->active_links)
					link_id = ffs(info.control.vif->active_links) - 1;
				else
					link_id = 0;
			}

			link = mvmvif->link[link_id];
			if (WARN_ON(!link))
				return -1;

			if (!ieee80211_is_data(hdr->frame_control))
				sta_id = link->bcast_sta.sta_id;
			else
				sta_id = link->mcast_sta.sta_id;

			queue = iwl_mvm_get_ctrl_vif_queue(mvm, link, &info,
							   skb);
		} else if (info.control.vif->type == NL80211_IFTYPE_MONITOR) {
			queue = mvm->snif_queue;
			sta_id = mvm->snif_sta.sta_id;
		} else if (info.control.vif->type == NL80211_IFTYPE_STATION &&
			   offchannel) {
			/*
			 * IWL_MVM_OFFCHANNEL_QUEUE is used for ROC packets
			 * that can be used in 2 different types of vifs, P2P &
			 * STATION.
			 * P2P uses the offchannel queue.
			 * STATION (HS2.0) uses the auxiliary context of the FW,
			 * and hence needs to be sent on the aux queue.
			 */
			sta_id = mvm->aux_sta.sta_id;
			queue = mvm->aux_queue;
		}
	}

	if (queue < 0) {
		IWL_ERR(mvm, "No queue was found. Dropping TX\n");
		return -1;
	}

	if (unlikely(ieee80211_is_probe_resp(fc)))
		iwl_mvm_probe_resp_set_noa(mvm, skb);

	IWL_DEBUG_TX(mvm, "station Id %d, queue=%d\n", sta_id, queue);

	dev_cmd = iwl_mvm_set_tx_params(mvm, skb, &info, hdrlen, NULL, sta_id);
	if (!dev_cmd)
		return -1;

	/* From now on, we cannot access info->control */
	iwl_mvm_skb_prepare_status(skb, dev_cmd);

	if (iwl_trans_tx(mvm->trans, skb, dev_cmd, queue)) {
		iwl_trans_free_tx_cmd(mvm->trans, dev_cmd);
		return -1;
	}

	return 0;
}

unsigned int iwl_mvm_max_amsdu_size(struct iwl_mvm *mvm,
				    struct ieee80211_sta *sta, unsigned int tid)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	u8 ac = tid_to_mac80211_ac[tid];
	enum nl80211_band band;
	unsigned int txf;
	unsigned int val;
	int lmac;

	/* For HE redirect to trigger based fifos */
	if (sta->deflink.he_cap.has_he && !WARN_ON(!iwl_mvm_has_new_tx_api(mvm)))
		ac += 4;

	txf = iwl_mvm_mac_ac_to_tx_fifo(mvm, ac);

	/*
	 * Don't send an AMSDU that will be longer than the TXF.
	 * Add a security margin of 256 for the TX command + headers.
	 * We also want to have the start of the next packet inside the
	 * fifo to be able to send bursts.
	 */
	val = mvmsta->max_amsdu_len;

	if (hweight16(sta->valid_links) <= 1) {
		if (sta->valid_links) {
			struct ieee80211_bss_conf *link_conf;
			unsigned int link = ffs(sta->valid_links) - 1;

			rcu_read_lock();
			link_conf = rcu_dereference(mvmsta->vif->link_conf[link]);
			if (WARN_ON(!link_conf))
				band = NL80211_BAND_2GHZ;
			else
				band = link_conf->chandef.chan->band;
			rcu_read_unlock();
		} else {
			band = mvmsta->vif->bss_conf.chandef.chan->band;
		}

		lmac = iwl_mvm_get_lmac_id(mvm, band);
	} else if (fw_has_capa(&mvm->fw->ucode_capa,
			       IWL_UCODE_TLV_CAPA_CDB_SUPPORT)) {
		/* for real MLO restrict to both LMACs if they exist */
		lmac = IWL_LMAC_5G_INDEX;
		val = min_t(unsigned int, val,
			    mvm->fwrt.smem_cfg.lmac[lmac].txfifo_size[txf] - 256);
		lmac = IWL_LMAC_24G_INDEX;
	} else {
		lmac = IWL_LMAC_24G_INDEX;
	}

	return min_t(unsigned int, val,
		     mvm->fwrt.smem_cfg.lmac[lmac].txfifo_size[txf] - 256);
}

#ifdef CONFIG_INET

static int
iwl_mvm_tx_tso_segment(struct sk_buff *skb, unsigned int num_subframes,
		       netdev_features_t netdev_flags,
		       struct sk_buff_head *mpdus_skb)
{
	struct sk_buff *tmp, *next;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	char cb[sizeof(skb->cb)];
	u16 i = 0;
	unsigned int tcp_payload_len;
	unsigned int mss = skb_shinfo(skb)->gso_size;
	bool ipv4 = (skb->protocol == htons(ETH_P_IP));
	bool qos = ieee80211_is_data_qos(hdr->frame_control);
	u16 ip_base_id = ipv4 ? ntohs(ip_hdr(skb)->id) : 0;

	skb_shinfo(skb)->gso_size = num_subframes * mss;
	memcpy(cb, skb->cb, sizeof(cb));

	next = skb_gso_segment(skb, netdev_flags);
	skb_shinfo(skb)->gso_size = mss;
	skb_shinfo(skb)->gso_type = ipv4 ? SKB_GSO_TCPV4 : SKB_GSO_TCPV6;
	if (WARN_ON_ONCE(IS_ERR(next)))
		return -EINVAL;
	else if (next)
		consume_skb(skb);

	skb_list_walk_safe(next, tmp, next) {
		memcpy(tmp->cb, cb, sizeof(tmp->cb));
		/*
		 * Compute the length of all the data added for the A-MSDU.
		 * This will be used to compute the length to write in the TX
		 * command. We have: SNAP + IP + TCP for n -1 subframes and
		 * ETH header for n subframes.
		 */
		tcp_payload_len = skb_tail_pointer(tmp) -
			skb_transport_header(tmp) -
			tcp_hdrlen(tmp) + tmp->data_len;

		if (ipv4)
			ip_hdr(tmp)->id = htons(ip_base_id + i * num_subframes);

		if (tcp_payload_len > mss) {
			skb_shinfo(tmp)->gso_size = mss;
			skb_shinfo(tmp)->gso_type = ipv4 ? SKB_GSO_TCPV4 :
							   SKB_GSO_TCPV6;
		} else {
			if (qos) {
				u8 *qc;

				if (ipv4)
					ip_send_check(ip_hdr(tmp));

				qc = ieee80211_get_qos_ctl((void *)tmp->data);
				*qc &= ~IEEE80211_QOS_CTL_A_MSDU_PRESENT;
			}
			skb_shinfo(tmp)->gso_size = 0;
		}

		skb_mark_not_on_list(tmp);
		__skb_queue_tail(mpdus_skb, tmp);
		i++;
	}

	return 0;
}

static int iwl_mvm_tx_tso(struct iwl_mvm *mvm, struct sk_buff *skb,
			  struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff_head *mpdus_skb)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	unsigned int mss = skb_shinfo(skb)->gso_size;
	unsigned int num_subframes, tcp_payload_len, subf_len, max_amsdu_len;
	u16 snap_ip_tcp, pad;
	netdev_features_t netdev_flags = NETIF_F_CSUM_MASK | NETIF_F_SG;
	u8 tid;

	snap_ip_tcp = 8 + skb_transport_header(skb) - skb_network_header(skb) +
		tcp_hdrlen(skb);

	if (!mvmsta->max_amsdu_len ||
	    !ieee80211_is_data_qos(hdr->frame_control) ||
	    !mvmsta->amsdu_enabled)
		return iwl_mvm_tx_tso_segment(skb, 1, netdev_flags, mpdus_skb);

	/*
	 * Do not build AMSDU for IPv6 with extension headers.
	 * ask stack to segment and checkum the generated MPDUs for us.
	 */
	if (skb->protocol == htons(ETH_P_IPV6) &&
	    ((struct ipv6hdr *)skb_network_header(skb))->nexthdr !=
	    IPPROTO_TCP) {
		netdev_flags &= ~NETIF_F_CSUM_MASK;
		return iwl_mvm_tx_tso_segment(skb, 1, netdev_flags, mpdus_skb);
	}

	tid = ieee80211_get_tid(hdr);
	if (WARN_ON_ONCE(tid >= IWL_MAX_TID_COUNT))
		return -EINVAL;

	/*
	 * No need to lock amsdu_in_ampdu_allowed since it can't be modified
	 * during an BA session.
	 */
	if ((info->flags & IEEE80211_TX_CTL_AMPDU &&
	     !mvmsta->tid_data[tid].amsdu_in_ampdu_allowed) ||
	    !(mvmsta->amsdu_enabled & BIT(tid)))
		return iwl_mvm_tx_tso_segment(skb, 1, netdev_flags, mpdus_skb);

	/*
	 * Take the min of ieee80211 station and mvm station
	 */
	max_amsdu_len =
		min_t(unsigned int, sta->cur->max_amsdu_len,
		      iwl_mvm_max_amsdu_size(mvm, sta, tid));

	/*
	 * Limit A-MSDU in A-MPDU to 4095 bytes when VHT is not
	 * supported. This is a spec requirement (IEEE 802.11-2015
	 * section 8.7.3 NOTE 3).
	 */
	if (info->flags & IEEE80211_TX_CTL_AMPDU &&
	    !sta->deflink.vht_cap.vht_supported)
		max_amsdu_len = min_t(unsigned int, max_amsdu_len, 4095);

	/* Sub frame header + SNAP + IP header + TCP header + MSS */
	subf_len = sizeof(struct ethhdr) + snap_ip_tcp + mss;
	pad = (4 - subf_len) & 0x3;

	/*
	 * If we have N subframes in the A-MSDU, then the A-MSDU's size is
	 * N * subf_len + (N - 1) * pad.
	 */
	num_subframes = (max_amsdu_len + pad) / (subf_len + pad);

	if (sta->max_amsdu_subframes &&
	    num_subframes > sta->max_amsdu_subframes)
		num_subframes = sta->max_amsdu_subframes;

	tcp_payload_len = skb_tail_pointer(skb) - skb_transport_header(skb) -
		tcp_hdrlen(skb) + skb->data_len;

	/*
	 * Make sure we have enough TBs for the A-MSDU:
	 *	2 for each subframe
	 *	1 more for each fragment
	 *	1 more for the potential data in the header
	 */
	if ((num_subframes * 2 + skb_shinfo(skb)->nr_frags + 1) >
	    mvm->trans->max_skb_frags)
		num_subframes = 1;

	if (num_subframes > 1)
		*ieee80211_get_qos_ctl(hdr) |= IEEE80211_QOS_CTL_A_MSDU_PRESENT;

	/* This skb fits in one single A-MSDU */
	if (num_subframes * mss >= tcp_payload_len) {
		__skb_queue_tail(mpdus_skb, skb);
		return 0;
	}

	/*
	 * Trick the segmentation function to make it
	 * create SKBs that can fit into one A-MSDU.
	 */
	return iwl_mvm_tx_tso_segment(skb, num_subframes, netdev_flags,
				      mpdus_skb);
}
#else /* CONFIG_INET */
static int iwl_mvm_tx_tso(struct iwl_mvm *mvm, struct sk_buff *skb,
			  struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff_head *mpdus_skb)
{
	/* Impossible to get TSO with CONFIG_INET */
	WARN_ON(1);

	return -1;
}
#endif

/* Check if there are any timed-out TIDs on a given shared TXQ */
static bool iwl_mvm_txq_should_update(struct iwl_mvm *mvm, int txq_id)
{
	unsigned long queue_tid_bitmap = mvm->queue_info[txq_id].tid_bitmap;
	unsigned long now = jiffies;
	int tid;

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return false;

	for_each_set_bit(tid, &queue_tid_bitmap, IWL_MAX_TID_COUNT + 1) {
		if (time_before(mvm->queue_info[txq_id].last_frame_time[tid] +
				IWL_MVM_DQA_QUEUE_TIMEOUT, now))
			return true;
	}

	return false;
}

static void iwl_mvm_tx_airtime(struct iwl_mvm *mvm,
			       struct iwl_mvm_sta *mvmsta,
			       int airtime)
{
	int mac = mvmsta->mac_id_n_color & FW_CTXT_ID_MSK;
	struct iwl_mvm_tcm_mac *mdata;

	if (mac >= NUM_MAC_INDEX_DRIVER)
		return;

	mdata = &mvm->tcm.data[mac];

	if (mvm->tcm.paused)
		return;

	if (time_after(jiffies, mvm->tcm.ts + MVM_TCM_PERIOD))
		schedule_delayed_work(&mvm->tcm.work, 0);

	mdata->tx.airtime += airtime;
}

static int iwl_mvm_tx_pkt_queued(struct iwl_mvm *mvm,
				 struct iwl_mvm_sta *mvmsta, int tid)
{
	u32 ac = tid_to_mac80211_ac[tid];
	int mac = mvmsta->mac_id_n_color & FW_CTXT_ID_MSK;
	struct iwl_mvm_tcm_mac *mdata;

	if (mac >= NUM_MAC_INDEX_DRIVER)
		return -EINVAL;

	mdata = &mvm->tcm.data[mac];

	mdata->tx.pkts[ac]++;

	return 0;
}

/*
 * Sets the fields in the Tx cmd that are crypto related.
 *
 * This function must be called with BHs disabled.
 */
static int iwl_mvm_tx_mpdu(struct iwl_mvm *mvm, struct sk_buff *skb,
			   struct ieee80211_tx_info *info,
			   struct ieee80211_sta *sta)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct iwl_mvm_sta *mvmsta;
	struct iwl_device_tx_cmd *dev_cmd;
	__le16 fc;
	u16 seq_number = 0;
	u8 tid = IWL_MAX_TID_COUNT;
	u16 txq_id;
	bool is_ampdu = false;
	int hdrlen;

	mvmsta = iwl_mvm_sta_from_mac80211(sta);
	fc = hdr->frame_control;
	hdrlen = ieee80211_hdrlen(fc);

	if (IWL_MVM_NON_TRANSMITTING_AP && ieee80211_is_probe_resp(fc))
		return -1;

	if (WARN_ON_ONCE(!mvmsta))
		return -1;

	if (WARN_ON_ONCE(mvmsta->deflink.sta_id == IWL_MVM_INVALID_STA))
		return -1;

	if (unlikely(ieee80211_is_any_nullfunc(fc)) && sta->deflink.he_cap.has_he)
		return -1;

	if (unlikely(ieee80211_is_probe_resp(fc)))
		iwl_mvm_probe_resp_set_noa(mvm, skb);

	dev_cmd = iwl_mvm_set_tx_params(mvm, skb, info, hdrlen,
					sta, mvmsta->deflink.sta_id);
	if (!dev_cmd)
		goto drop;

	/*
	 * we handle that entirely ourselves -- for uAPSD the firmware
	 * will always send a notification, and for PS-Poll responses
	 * we'll notify mac80211 when getting frame status
	 */
	info->flags &= ~IEEE80211_TX_STATUS_EOSP;

	spin_lock(&mvmsta->lock);

	/* nullfunc frames should go to the MGMT queue regardless of QOS,
	 * the conditions of !ieee80211_is_qos_nullfunc(fc) and
	 * !ieee80211_is_data_qos(fc) keep the default assignment of MGMT TID
	 */
	if (ieee80211_is_data_qos(fc) && !ieee80211_is_qos_nullfunc(fc)) {
		tid = ieee80211_get_tid(hdr);
		if (WARN_ONCE(tid >= IWL_MAX_TID_COUNT, "Invalid TID %d", tid))
			goto drop_unlock_sta;

		is_ampdu = info->flags & IEEE80211_TX_CTL_AMPDU;
		if (WARN_ONCE(is_ampdu &&
			      mvmsta->tid_data[tid].state != IWL_AGG_ON,
			      "Invalid internal agg state %d for TID %d",
			       mvmsta->tid_data[tid].state, tid))
			goto drop_unlock_sta;

		seq_number = mvmsta->tid_data[tid].seq_number;
		seq_number &= IEEE80211_SCTL_SEQ;

		if (!iwl_mvm_has_new_tx_api(mvm)) {
			struct iwl_tx_cmd *tx_cmd = (void *)dev_cmd->payload;

			hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
			hdr->seq_ctrl |= cpu_to_le16(seq_number);
			/* update the tx_cmd hdr as it was already copied */
			tx_cmd->hdr->seq_ctrl = hdr->seq_ctrl;
		}
	} else if (ieee80211_is_data(fc) && !ieee80211_is_data_qos(fc) &&
		   !ieee80211_is_nullfunc(fc)) {
		tid = IWL_TID_NON_QOS;
	}

	txq_id = mvmsta->tid_data[tid].txq_id;

	WARN_ON_ONCE(info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM);

	if (WARN_ONCE(txq_id == IWL_MVM_INVALID_QUEUE, "Invalid TXQ id")) {
		iwl_trans_free_tx_cmd(mvm->trans, dev_cmd);
		spin_unlock(&mvmsta->lock);
		return -1;
	}

	if (!iwl_mvm_has_new_tx_api(mvm)) {
		/* Keep track of the time of the last frame for this RA/TID */
		mvm->queue_info[txq_id].last_frame_time[tid] = jiffies;

		/*
		 * If we have timed-out TIDs - schedule the worker that will
		 * reconfig the queues and update them
		 *
		 * Note that the no lock is taken here in order to not serialize
		 * the TX flow. This isn't dangerous because scheduling
		 * mvm->add_stream_wk can't ruin the state, and if we DON'T
		 * schedule it due to some race condition then next TX we get
		 * here we will.
		 */
		if (unlikely(mvm->queue_info[txq_id].status ==
			     IWL_MVM_QUEUE_SHARED &&
			     iwl_mvm_txq_should_update(mvm, txq_id)))
			schedule_work(&mvm->add_stream_wk);
	}

	IWL_DEBUG_TX(mvm, "TX to [%d|%d] Q:%d - seq: 0x%x len %d\n",
		     mvmsta->deflink.sta_id, tid, txq_id,
		     IEEE80211_SEQ_TO_SN(seq_number), skb->len);

	/* From now on, we cannot access info->control */
	iwl_mvm_skb_prepare_status(skb, dev_cmd);

	/*
	 * The IV is introduced by the HW for new tx api, and it is not present
	 * in the skb, hence, don't tell iwl_mvm_mei_tx_copy_to_csme about the
	 * IV for those devices.
	 */
	if (ieee80211_is_data(fc))
		iwl_mvm_mei_tx_copy_to_csme(mvm, skb,
					    info->control.hw_key &&
					    !iwl_mvm_has_new_tx_api(mvm) ?
					    info->control.hw_key->iv_len : 0);

	if (iwl_trans_tx(mvm->trans, skb, dev_cmd, txq_id))
		goto drop_unlock_sta;

	if (tid < IWL_MAX_TID_COUNT && !ieee80211_has_morefrags(fc))
		mvmsta->tid_data[tid].seq_number = seq_number + 0x10;

	spin_unlock(&mvmsta->lock);

	if (iwl_mvm_tx_pkt_queued(mvm, mvmsta,
				  tid == IWL_MAX_TID_COUNT ? 0 : tid))
		goto drop;

	return 0;

drop_unlock_sta:
	iwl_trans_free_tx_cmd(mvm->trans, dev_cmd);
	spin_unlock(&mvmsta->lock);
drop:
	IWL_DEBUG_TX(mvm, "TX to [%d|%d] dropped\n", mvmsta->deflink.sta_id,
		     tid);
	return -1;
}

int iwl_mvm_tx_skb_sta(struct iwl_mvm *mvm, struct sk_buff *skb,
		       struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct ieee80211_tx_info info;
	struct sk_buff_head mpdus_skbs;
	unsigned int payload_len;
	int ret;
	struct sk_buff *orig_skb = skb;

	if (WARN_ON_ONCE(!mvmsta))
		return -1;

	if (WARN_ON_ONCE(mvmsta->deflink.sta_id == IWL_MVM_INVALID_STA))
		return -1;

	memcpy(&info, skb->cb, sizeof(info));

	if (!skb_is_gso(skb))
		return iwl_mvm_tx_mpdu(mvm, skb, &info, sta);

	payload_len = skb_tail_pointer(skb) - skb_transport_header(skb) -
		tcp_hdrlen(skb) + skb->data_len;

	if (payload_len <= skb_shinfo(skb)->gso_size)
		return iwl_mvm_tx_mpdu(mvm, skb, &info, sta);

	__skb_queue_head_init(&mpdus_skbs);

	ret = iwl_mvm_tx_tso(mvm, skb, &info, sta, &mpdus_skbs);
	if (ret)
		return ret;

	WARN_ON(skb_queue_empty(&mpdus_skbs));

	while (!skb_queue_empty(&mpdus_skbs)) {
		skb = __skb_dequeue(&mpdus_skbs);

		ret = iwl_mvm_tx_mpdu(mvm, skb, &info, sta);
		if (ret) {
			/* Free skbs created as part of TSO logic that have not yet been dequeued */
			__skb_queue_purge(&mpdus_skbs);
			/* skb here is not necessarily same as skb that entered this method,
			 * so free it explicitly.
			 */
			if (skb == orig_skb)
				ieee80211_free_txskb(mvm->hw, skb);
			else
				kfree_skb(skb);
			/* there was error, but we consumed skb one way or another, so return 0 */
			return 0;
		}
	}

	return 0;
}

static void iwl_mvm_check_ratid_empty(struct iwl_mvm *mvm,
				      struct ieee80211_sta *sta, u8 tid)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_tid_data *tid_data = &mvmsta->tid_data[tid];
	struct ieee80211_vif *vif = mvmsta->vif;
	u16 normalized_ssn;

	lockdep_assert_held(&mvmsta->lock);

	if ((tid_data->state == IWL_AGG_ON ||
	     tid_data->state == IWL_EMPTYING_HW_QUEUE_DELBA) &&
	    iwl_mvm_tid_queued(mvm, tid_data) == 0) {
		/*
		 * Now that this aggregation or DQA queue is empty tell
		 * mac80211 so it knows we no longer have frames buffered for
		 * the station on this TID (for the TIM bitmap calculation.)
		 */
		ieee80211_sta_set_buffered(sta, tid, false);
	}

	/*
	 * In 22000 HW, the next_reclaimed index is only 8 bit, so we'll need
	 * to align the wrap around of ssn so we compare relevant values.
	 */
	normalized_ssn = tid_data->ssn;
	if (mvm->trans->trans_cfg->gen2)
		normalized_ssn &= 0xff;

	if (normalized_ssn != tid_data->next_reclaimed)
		return;

	switch (tid_data->state) {
	case IWL_EMPTYING_HW_QUEUE_ADDBA:
		IWL_DEBUG_TX_QUEUES(mvm,
				    "Can continue addBA flow ssn = next_recl = %d\n",
				    tid_data->next_reclaimed);
		tid_data->state = IWL_AGG_STARTING;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;

	case IWL_EMPTYING_HW_QUEUE_DELBA:
		IWL_DEBUG_TX_QUEUES(mvm,
				    "Can continue DELBA flow ssn = next_recl = %d\n",
				    tid_data->next_reclaimed);
		tid_data->state = IWL_AGG_OFF;
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;

	default:
		break;
	}
}

#ifdef CONFIG_IWLWIFI_DEBUG
const char *iwl_mvm_get_tx_fail_reason(u32 status)
{
#define TX_STATUS_FAIL(x) case TX_STATUS_FAIL_ ## x: return #x
#define TX_STATUS_POSTPONE(x) case TX_STATUS_POSTPONE_ ## x: return #x

	switch (status & TX_STATUS_MSK) {
	case TX_STATUS_SUCCESS:
		return "SUCCESS";
	TX_STATUS_POSTPONE(DELAY);
	TX_STATUS_POSTPONE(FEW_BYTES);
	TX_STATUS_POSTPONE(BT_PRIO);
	TX_STATUS_POSTPONE(QUIET_PERIOD);
	TX_STATUS_POSTPONE(CALC_TTAK);
	TX_STATUS_FAIL(INTERNAL_CROSSED_RETRY);
	TX_STATUS_FAIL(SHORT_LIMIT);
	TX_STATUS_FAIL(LONG_LIMIT);
	TX_STATUS_FAIL(UNDERRUN);
	TX_STATUS_FAIL(DRAIN_FLOW);
	TX_STATUS_FAIL(RFKILL_FLUSH);
	TX_STATUS_FAIL(LIFE_EXPIRE);
	TX_STATUS_FAIL(DEST_PS);
	TX_STATUS_FAIL(HOST_ABORTED);
	TX_STATUS_FAIL(BT_RETRY);
	TX_STATUS_FAIL(STA_INVALID);
	TX_STATUS_FAIL(FRAG_DROPPED);
	TX_STATUS_FAIL(TID_DISABLE);
	TX_STATUS_FAIL(FIFO_FLUSHED);
	TX_STATUS_FAIL(SMALL_CF_POLL);
	TX_STATUS_FAIL(FW_DROP);
	TX_STATUS_FAIL(STA_COLOR_MISMATCH);
	}

	return "UNKNOWN";

#undef TX_STATUS_FAIL
#undef TX_STATUS_POSTPONE
}
#endif /* CONFIG_IWLWIFI_DEBUG */

static int iwl_mvm_get_hwrate_chan_width(u32 chan_width)
{
	switch (chan_width) {
	case RATE_MCS_CHAN_WIDTH_20:
		return 0;
	case RATE_MCS_CHAN_WIDTH_40:
		return IEEE80211_TX_RC_40_MHZ_WIDTH;
	case RATE_MCS_CHAN_WIDTH_80:
		return IEEE80211_TX_RC_80_MHZ_WIDTH;
	case RATE_MCS_CHAN_WIDTH_160:
		return IEEE80211_TX_RC_160_MHZ_WIDTH;
	default:
		return 0;
	}
}

void iwl_mvm_hwrate_to_tx_rate(u32 rate_n_flags,
			       enum nl80211_band band,
			       struct ieee80211_tx_rate *r)
{
	u32 format = rate_n_flags & RATE_MCS_MOD_TYPE_MSK;
	u32 rate = format == RATE_MCS_HT_MSK ?
		RATE_HT_MCS_INDEX(rate_n_flags) :
		rate_n_flags & RATE_MCS_CODE_MSK;

	r->flags |=
		iwl_mvm_get_hwrate_chan_width(rate_n_flags &
					      RATE_MCS_CHAN_WIDTH_MSK);

	if (rate_n_flags & RATE_MCS_SGI_MSK)
		r->flags |= IEEE80211_TX_RC_SHORT_GI;
	if (format ==  RATE_MCS_HT_MSK) {
		r->flags |= IEEE80211_TX_RC_MCS;
		r->idx = rate;
	} else if (format ==  RATE_MCS_VHT_MSK) {
		ieee80211_rate_set_vht(r, rate,
				       FIELD_GET(RATE_MCS_NSS_MSK,
						 rate_n_flags) + 1);
		r->flags |= IEEE80211_TX_RC_VHT_MCS;
	} else if (format == RATE_MCS_HE_MSK) {
		/* mac80211 cannot do this without ieee80211_tx_status_ext()
		 * but it only matters for radiotap */
		r->idx = 0;
	} else {
		r->idx = iwl_mvm_legacy_hw_idx_to_mac80211_idx(rate_n_flags,
							       band);
	}
}

void iwl_mvm_hwrate_to_tx_rate_v1(u32 rate_n_flags,
				  enum nl80211_band band,
				  struct ieee80211_tx_rate *r)
{
	if (rate_n_flags & RATE_HT_MCS_GF_MSK)
		r->flags |= IEEE80211_TX_RC_GREEN_FIELD;

	r->flags |=
		iwl_mvm_get_hwrate_chan_width(rate_n_flags &
					      RATE_MCS_CHAN_WIDTH_MSK_V1);

	if (rate_n_flags & RATE_MCS_SGI_MSK_V1)
		r->flags |= IEEE80211_TX_RC_SHORT_GI;
	if (rate_n_flags & RATE_MCS_HT_MSK_V1) {
		r->flags |= IEEE80211_TX_RC_MCS;
		r->idx = rate_n_flags & RATE_HT_MCS_INDEX_MSK_V1;
	} else if (rate_n_flags & RATE_MCS_VHT_MSK_V1) {
		ieee80211_rate_set_vht(
			r, rate_n_flags & RATE_VHT_MCS_RATE_CODE_MSK,
			FIELD_GET(RATE_MCS_NSS_MSK, rate_n_flags) + 1);
		r->flags |= IEEE80211_TX_RC_VHT_MCS;
	} else {
		r->idx = iwl_mvm_legacy_rate_to_mac80211_idx(rate_n_flags,
							     band);
	}
}

/*
 * translate ucode response to mac80211 tx status control values
 */
static void iwl_mvm_hwrate_to_tx_status(const struct iwl_fw *fw,
					u32 rate_n_flags,
					struct ieee80211_tx_info *info)
{
	struct ieee80211_tx_rate *r = &info->status.rates[0];

	if (iwl_fw_lookup_notif_ver(fw, LONG_GROUP,
				    TX_CMD, 0) <= 6)
		rate_n_flags = iwl_new_rate_from_v1(rate_n_flags);

	info->status.antenna =
		((rate_n_flags & RATE_MCS_ANT_AB_MSK) >> RATE_MCS_ANT_POS);
	iwl_mvm_hwrate_to_tx_rate(rate_n_flags,
				  info->band, r);
}

static void iwl_mvm_tx_status_check_trigger(struct iwl_mvm *mvm,
					    u32 status, __le16 frame_control)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_tx_status *status_trig;
	int i;

	if ((status & TX_STATUS_MSK) != TX_STATUS_SUCCESS) {
		enum iwl_fw_ini_time_point tp =
			IWL_FW_INI_TIME_POINT_TX_FAILED;

		if (ieee80211_is_action(frame_control))
			tp = IWL_FW_INI_TIME_POINT_TX_WFD_ACTION_FRAME_FAILED;

		iwl_dbg_tlv_time_point(&mvm->fwrt,
				       tp, NULL);
		return;
	}

	trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, NULL,
				     FW_DBG_TRIGGER_TX_STATUS);
	if (!trig)
		return;

	status_trig = (void *)trig->data;

	for (i = 0; i < ARRAY_SIZE(status_trig->statuses); i++) {
		/* don't collect on status 0 */
		if (!status_trig->statuses[i].status)
			break;

		if (status_trig->statuses[i].status != (status & TX_STATUS_MSK))
			continue;

		iwl_fw_dbg_collect_trig(&mvm->fwrt, trig,
					"Tx status %d was received",
					status & TX_STATUS_MSK);
		break;
	}
}

/*
 * iwl_mvm_get_scd_ssn - returns the SSN of the SCD
 * @tx_resp: the Tx response from the fw (agg or non-agg)
 *
 * When the fw sends an AMPDU, it fetches the MPDUs one after the other. Since
 * it can't know that everything will go well until the end of the AMPDU, it
 * can't know in advance the number of MPDUs that will be sent in the current
 * batch. This is why it writes the agg Tx response while it fetches the MPDUs.
 * Hence, it can't know in advance what the SSN of the SCD will be at the end
 * of the batch. This is why the SSN of the SCD is written at the end of the
 * whole struct at a variable offset. This function knows how to cope with the
 * variable offset and returns the SSN of the SCD.
 */
static inline u32 iwl_mvm_get_scd_ssn(struct iwl_mvm *mvm,
				      struct iwl_mvm_tx_resp *tx_resp)
{
	return le32_to_cpup((__le32 *)iwl_mvm_get_agg_status(mvm, tx_resp) +
			    tx_resp->frame_count) & 0xfff;
}

static void iwl_mvm_rx_tx_cmd_single(struct iwl_mvm *mvm,
				     struct iwl_rx_packet *pkt)
{
	struct ieee80211_sta *sta;
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	/* struct iwl_mvm_tx_resp_v3 is almost the same */
	struct iwl_mvm_tx_resp *tx_resp = (void *)pkt->data;
	int sta_id = IWL_MVM_TX_RES_GET_RA(tx_resp->ra_tid);
	int tid = IWL_MVM_TX_RES_GET_TID(tx_resp->ra_tid);
	struct agg_tx_status *agg_status =
		iwl_mvm_get_agg_status(mvm, tx_resp);
	u32 status = le16_to_cpu(agg_status->status);
	u16 ssn = iwl_mvm_get_scd_ssn(mvm, tx_resp);
	struct sk_buff_head skbs;
	u8 skb_freed = 0;
	u8 lq_color;
	u16 next_reclaimed, seq_ctl;
	bool is_ndp = false;

	__skb_queue_head_init(&skbs);

	if (iwl_mvm_has_new_tx_api(mvm))
		txq_id = le16_to_cpu(tx_resp->tx_queue);

	seq_ctl = le16_to_cpu(tx_resp->seq_ctl);

	/* we can free until ssn % q.n_bd not inclusive */
	iwl_trans_reclaim(mvm->trans, txq_id, ssn, &skbs, false);

	while (!skb_queue_empty(&skbs)) {
		struct sk_buff *skb = __skb_dequeue(&skbs);
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		struct ieee80211_hdr *hdr = (void *)skb->data;
		bool flushed = false;

		skb_freed++;

		iwl_trans_free_tx_cmd(mvm->trans, info->driver_data[1]);

		memset(&info->status, 0, sizeof(info->status));
		info->flags &= ~(IEEE80211_TX_STAT_ACK | IEEE80211_TX_STAT_TX_FILTERED);

		/* inform mac80211 about what happened with the frame */
		switch (status & TX_STATUS_MSK) {
		case TX_STATUS_SUCCESS:
		case TX_STATUS_DIRECT_DONE:
			info->flags |= IEEE80211_TX_STAT_ACK;
			break;
		case TX_STATUS_FAIL_FIFO_FLUSHED:
		case TX_STATUS_FAIL_DRAIN_FLOW:
			flushed = true;
			break;
		case TX_STATUS_FAIL_DEST_PS:
			/* the FW should have stopped the queue and not
			 * return this status
			 */
			IWL_ERR_LIMIT(mvm,
				      "FW reported TX filtered, status=0x%x, FC=0x%x\n",
				      status, le16_to_cpu(hdr->frame_control));
			info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
			break;
		default:
			break;
		}

		if ((status & TX_STATUS_MSK) != TX_STATUS_SUCCESS &&
		    ieee80211_is_mgmt(hdr->frame_control))
			iwl_mvm_toggle_tx_ant(mvm, &mvm->mgmt_last_antenna_idx);

		/*
		 * If we are freeing multiple frames, mark all the frames
		 * but the first one as acked, since they were acknowledged
		 * before
		 * */
		if (skb_freed > 1)
			info->flags |= IEEE80211_TX_STAT_ACK;

		iwl_mvm_tx_status_check_trigger(mvm, status, hdr->frame_control);

		info->status.rates[0].count = tx_resp->failure_frame + 1;

		iwl_mvm_hwrate_to_tx_status(mvm->fw,
					    le32_to_cpu(tx_resp->initial_rate),
					    info);

		/* Don't assign the converted initial_rate, because driver
		 * TLC uses this and doesn't support the new FW rate
		 */
		info->status.status_driver_data[1] =
			(void *)(uintptr_t)le32_to_cpu(tx_resp->initial_rate);

		/* Single frame failure in an AMPDU queue => send BAR */
		if (info->flags & IEEE80211_TX_CTL_AMPDU &&
		    !(info->flags & IEEE80211_TX_STAT_ACK) &&
		    !(info->flags & IEEE80211_TX_STAT_TX_FILTERED) && !flushed)
			info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;

		/* W/A FW bug: seq_ctl is wrong upon failure / BAR frame */
		if (ieee80211_is_back_req(hdr->frame_control))
			seq_ctl = 0;
		else if (status != TX_STATUS_SUCCESS)
			seq_ctl = le16_to_cpu(hdr->seq_ctrl);

		if (unlikely(!seq_ctl)) {
			/*
			 * If it is an NDP, we can't update next_reclaim since
			 * its sequence control is 0. Note that for that same
			 * reason, NDPs are never sent to A-MPDU'able queues
			 * so that we can never have more than one freed frame
			 * for a single Tx resonse (see WARN_ON below).
			 */
			if (ieee80211_is_qos_nullfunc(hdr->frame_control))
				is_ndp = true;
		}

		/*
		 * TODO: this is not accurate if we are freeing more than one
		 * packet.
		 */
		info->status.tx_time =
			le16_to_cpu(tx_resp->wireless_media_time);
		BUILD_BUG_ON(ARRAY_SIZE(info->status.status_driver_data) < 1);
		lq_color = TX_RES_RATE_TABLE_COL_GET(tx_resp->tlc_info);
		info->status.status_driver_data[0] =
			RS_DRV_DATA_PACK(lq_color, tx_resp->reduced_tpc);

		if (likely(!iwl_mvm_time_sync_frame(mvm, skb, hdr->addr1)))
			ieee80211_tx_status_skb(mvm->hw, skb);
	}

	/* This is an aggregation queue or might become one, so we use
	 * the ssn since: ssn = wifi seq_num % 256.
	 * The seq_ctl is the sequence control of the packet to which
	 * this Tx response relates. But if there is a hole in the
	 * bitmap of the BA we received, this Tx response may allow to
	 * reclaim the hole and all the subsequent packets that were
	 * already acked. In that case, seq_ctl != ssn, and the next
	 * packet to be reclaimed will be ssn and not seq_ctl. In that
	 * case, several packets will be reclaimed even if
	 * frame_count = 1.
	 *
	 * The ssn is the index (% 256) of the latest packet that has
	 * treated (acked / dropped) + 1.
	 */
	next_reclaimed = ssn;

	IWL_DEBUG_TX_REPLY(mvm,
			   "TXQ %d status %s (0x%08x)\n",
			   txq_id, iwl_mvm_get_tx_fail_reason(status), status);

	IWL_DEBUG_TX_REPLY(mvm,
			   "\t\t\t\tinitial_rate 0x%x retries %d, idx=%d ssn=%d next_reclaimed=0x%x seq_ctl=0x%x\n",
			   le32_to_cpu(tx_resp->initial_rate),
			   tx_resp->failure_frame, SEQ_TO_INDEX(sequence),
			   ssn, next_reclaimed, seq_ctl);

	rcu_read_lock();

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);
	/*
	 * sta can't be NULL otherwise it'd mean that the sta has been freed in
	 * the firmware while we still have packets for it in the Tx queues.
	 */
	if (WARN_ON_ONCE(!sta))
		goto out;

	if (!IS_ERR(sta)) {
		struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);

		iwl_mvm_tx_airtime(mvm, mvmsta,
				   le16_to_cpu(tx_resp->wireless_media_time));

		if ((status & TX_STATUS_MSK) != TX_STATUS_SUCCESS &&
		    mvmsta->sta_state < IEEE80211_STA_AUTHORIZED)
			iwl_mvm_toggle_tx_ant(mvm, &mvmsta->tx_ant);

		if (sta->wme && tid != IWL_MGMT_TID) {
			struct iwl_mvm_tid_data *tid_data =
				&mvmsta->tid_data[tid];
			bool send_eosp_ndp = false;

			spin_lock_bh(&mvmsta->lock);

			if (!is_ndp) {
				tid_data->next_reclaimed = next_reclaimed;
				IWL_DEBUG_TX_REPLY(mvm,
						   "Next reclaimed packet:%d\n",
						   next_reclaimed);
			} else {
				IWL_DEBUG_TX_REPLY(mvm,
						   "NDP - don't update next_reclaimed\n");
			}

			iwl_mvm_check_ratid_empty(mvm, sta, tid);

			if (mvmsta->sleep_tx_count) {
				mvmsta->sleep_tx_count--;
				if (mvmsta->sleep_tx_count &&
				    !iwl_mvm_tid_queued(mvm, tid_data)) {
					/*
					 * The number of frames in the queue
					 * dropped to 0 even if we sent less
					 * frames than we thought we had on the
					 * Tx queue.
					 * This means we had holes in the BA
					 * window that we just filled, ask
					 * mac80211 to send EOSP since the
					 * firmware won't know how to do that.
					 * Send NDP and the firmware will send
					 * EOSP notification that will trigger
					 * a call to ieee80211_sta_eosp().
					 */
					send_eosp_ndp = true;
				}
			}

			spin_unlock_bh(&mvmsta->lock);
			if (send_eosp_ndp) {
				iwl_mvm_sta_modify_sleep_tx_count(mvm, sta,
					IEEE80211_FRAME_RELEASE_UAPSD,
					1, tid, false, false);
				mvmsta->sleep_tx_count = 0;
				ieee80211_send_eosp_nullfunc(sta, tid);
			}
		}

		if (mvmsta->next_status_eosp) {
			mvmsta->next_status_eosp = false;
			ieee80211_sta_eosp(sta);
		}
	}
out:
	rcu_read_unlock();
}

#ifdef CONFIG_IWLWIFI_DEBUG
#define AGG_TX_STATE_(x) case AGG_TX_STATE_ ## x: return #x
static const char *iwl_get_agg_tx_status(u16 status)
{
	switch (status & AGG_TX_STATE_STATUS_MSK) {
	AGG_TX_STATE_(TRANSMITTED);
	AGG_TX_STATE_(UNDERRUN);
	AGG_TX_STATE_(BT_PRIO);
	AGG_TX_STATE_(FEW_BYTES);
	AGG_TX_STATE_(ABORT);
	AGG_TX_STATE_(TX_ON_AIR_DROP);
	AGG_TX_STATE_(LAST_SENT_TRY_CNT);
	AGG_TX_STATE_(LAST_SENT_BT_KILL);
	AGG_TX_STATE_(SCD_QUERY);
	AGG_TX_STATE_(TEST_BAD_CRC32);
	AGG_TX_STATE_(RESPONSE);
	AGG_TX_STATE_(DUMP_TX);
	AGG_TX_STATE_(DELAY_TX);
	}

	return "UNKNOWN";
}

static void iwl_mvm_rx_tx_cmd_agg_dbg(struct iwl_mvm *mvm,
				      struct iwl_rx_packet *pkt)
{
	struct iwl_mvm_tx_resp *tx_resp = (void *)pkt->data;
	struct agg_tx_status *frame_status =
		iwl_mvm_get_agg_status(mvm, tx_resp);
	int i;
	bool tirgger_timepoint = false;

	for (i = 0; i < tx_resp->frame_count; i++) {
		u16 fstatus = le16_to_cpu(frame_status[i].status);
		/* In case one frame wasn't transmitted trigger time point */
		tirgger_timepoint |= ((fstatus & AGG_TX_STATE_STATUS_MSK) !=
				      AGG_TX_STATE_TRANSMITTED);
		IWL_DEBUG_TX_REPLY(mvm,
				   "status %s (0x%04x), try-count (%d) seq (0x%x)\n",
				   iwl_get_agg_tx_status(fstatus),
				   fstatus & AGG_TX_STATE_STATUS_MSK,
				   (fstatus & AGG_TX_STATE_TRY_CNT_MSK) >>
					AGG_TX_STATE_TRY_CNT_POS,
				   le16_to_cpu(frame_status[i].sequence));
	}

	if (tirgger_timepoint)
		iwl_dbg_tlv_time_point(&mvm->fwrt,
				       IWL_FW_INI_TIME_POINT_TX_FAILED, NULL);

}
#else
static void iwl_mvm_rx_tx_cmd_agg_dbg(struct iwl_mvm *mvm,
				      struct iwl_rx_packet *pkt)
{}
#endif /* CONFIG_IWLWIFI_DEBUG */

static void iwl_mvm_rx_tx_cmd_agg(struct iwl_mvm *mvm,
				  struct iwl_rx_packet *pkt)
{
	struct iwl_mvm_tx_resp *tx_resp = (void *)pkt->data;
	int sta_id = IWL_MVM_TX_RES_GET_RA(tx_resp->ra_tid);
	int tid = IWL_MVM_TX_RES_GET_TID(tx_resp->ra_tid);
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	struct iwl_mvm_sta *mvmsta;
	int queue = SEQ_TO_QUEUE(sequence);
	struct ieee80211_sta *sta;

	if (WARN_ON_ONCE(queue < IWL_MVM_DQA_MIN_DATA_QUEUE &&
			 (queue != IWL_MVM_DQA_BSS_CLIENT_QUEUE)))
		return;

	iwl_mvm_rx_tx_cmd_agg_dbg(mvm, pkt);

	rcu_read_lock();

	mvmsta = iwl_mvm_sta_from_staid_rcu(mvm, sta_id);

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);
	if (WARN_ON_ONCE(IS_ERR_OR_NULL(sta) || !sta->wme)) {
		rcu_read_unlock();
		return;
	}

	if (!WARN_ON_ONCE(!mvmsta)) {
		mvmsta->tid_data[tid].rate_n_flags =
			le32_to_cpu(tx_resp->initial_rate);
		mvmsta->tid_data[tid].tx_time =
			le16_to_cpu(tx_resp->wireless_media_time);
		mvmsta->tid_data[tid].lq_color =
			TX_RES_RATE_TABLE_COL_GET(tx_resp->tlc_info);
		iwl_mvm_tx_airtime(mvm, mvmsta,
				   le16_to_cpu(tx_resp->wireless_media_time));
	}

	rcu_read_unlock();
}

void iwl_mvm_rx_tx_cmd(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mvm_tx_resp *tx_resp = (void *)pkt->data;

	if (tx_resp->frame_count == 1)
		iwl_mvm_rx_tx_cmd_single(mvm, pkt);
	else
		iwl_mvm_rx_tx_cmd_agg(mvm, pkt);
}

static void iwl_mvm_tx_reclaim(struct iwl_mvm *mvm, int sta_id, int tid,
			       int txq, int index,
			       struct ieee80211_tx_info *tx_info, u32 rate,
			       bool is_flush)
{
	struct sk_buff_head reclaimed_skbs;
	struct iwl_mvm_tid_data *tid_data = NULL;
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta = NULL;
	struct sk_buff *skb;
	int freed;

	if (WARN_ONCE(sta_id >= mvm->fw->ucode_capa.num_stations ||
		      tid > IWL_MAX_TID_COUNT,
		      "sta_id %d tid %d", sta_id, tid))
		return;

	rcu_read_lock();

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);

	/* Reclaiming frames for a station that has been deleted ? */
	if (WARN_ON_ONCE(!sta)) {
		rcu_read_unlock();
		return;
	}

	__skb_queue_head_init(&reclaimed_skbs);

	/*
	 * Release all TFDs before the SSN, i.e. all TFDs in front of
	 * block-ack window (we assume that they've been successfully
	 * transmitted ... if not, it's too late anyway).
	 */
	iwl_trans_reclaim(mvm->trans, txq, index, &reclaimed_skbs, is_flush);

	skb_queue_walk(&reclaimed_skbs, skb) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		iwl_trans_free_tx_cmd(mvm->trans, info->driver_data[1]);

		memset(&info->status, 0, sizeof(info->status));
		/* Packet was transmitted successfully, failures come as single
		 * frames because before failing a frame the firmware transmits
		 * it without aggregation at least once.
		 */
		if (!is_flush)
			info->flags |= IEEE80211_TX_STAT_ACK;
		else
			info->flags &= ~IEEE80211_TX_STAT_ACK;
	}

	/*
	 * It's possible to get a BA response after invalidating the rcu (rcu is
	 * invalidated in order to prevent new Tx from being sent, but there may
	 * be some frames already in-flight).
	 * In this case we just want to reclaim, and could skip all the
	 * sta-dependent stuff since it's in the middle of being removed
	 * anyways.
	 */
	if (IS_ERR(sta))
		goto out;

	mvmsta = iwl_mvm_sta_from_mac80211(sta);
	tid_data = &mvmsta->tid_data[tid];

	if (tid_data->txq_id != txq) {
		IWL_ERR(mvm,
			"invalid reclaim request: Q %d, tid %d\n",
			tid_data->txq_id, tid);
		rcu_read_unlock();
		return;
	}

	spin_lock_bh(&mvmsta->lock);

	tid_data->next_reclaimed = index;

	iwl_mvm_check_ratid_empty(mvm, sta, tid);

	freed = 0;

	/* pack lq color from tid_data along the reduced txp */
	tx_info->status.status_driver_data[0] =
		RS_DRV_DATA_PACK(tid_data->lq_color,
				 tx_info->status.status_driver_data[0]);
	tx_info->status.status_driver_data[1] = (void *)(uintptr_t)rate;

	skb_queue_walk(&reclaimed_skbs, skb) {
		struct ieee80211_hdr *hdr = (void *)skb->data;
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (!is_flush) {
			if (ieee80211_is_data_qos(hdr->frame_control))
				freed++;
			else
				WARN_ON_ONCE(tid != IWL_MAX_TID_COUNT);
		}

		/* this is the first skb we deliver in this batch */
		/* put the rate scaling data there */
		if (freed == 1) {
			info->flags |= IEEE80211_TX_STAT_AMPDU;
			memcpy(&info->status, &tx_info->status,
			       sizeof(tx_info->status));
			iwl_mvm_hwrate_to_tx_status(mvm->fw, rate, info);
		}
	}

	spin_unlock_bh(&mvmsta->lock);

	/* We got a BA notif with 0 acked or scd_ssn didn't progress which is
	 * possible (i.e. first MPDU in the aggregation wasn't acked)
	 * Still it's important to update RS about sent vs. acked.
	 */
	if (!is_flush && skb_queue_empty(&reclaimed_skbs) &&
	    !iwl_mvm_has_tlc_offload(mvm)) {
		struct ieee80211_chanctx_conf *chanctx_conf = NULL;

		/* no TLC offload, so non-MLD mode */
		if (mvmsta->vif)
			chanctx_conf =
				rcu_dereference(mvmsta->vif->bss_conf.chanctx_conf);

		if (WARN_ON_ONCE(!chanctx_conf))
			goto out;

		tx_info->band = chanctx_conf->def.chan->band;
		iwl_mvm_hwrate_to_tx_status(mvm->fw, rate, tx_info);

		IWL_DEBUG_TX_REPLY(mvm, "No reclaim. Update rs directly\n");
		iwl_mvm_rs_tx_status(mvm, sta, tid, tx_info, false);
	}

out:
	rcu_read_unlock();

	while (!skb_queue_empty(&reclaimed_skbs)) {
		skb = __skb_dequeue(&reclaimed_skbs);
		ieee80211_tx_status_skb(mvm->hw, skb);
	}
}

void iwl_mvm_rx_ba_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	unsigned int pkt_len = iwl_rx_packet_payload_len(pkt);
	int sta_id, tid, txq, index;
	struct ieee80211_tx_info ba_info = {};
	struct iwl_mvm_ba_notif *ba_notif;
	struct iwl_mvm_tid_data *tid_data;
	struct iwl_mvm_sta *mvmsta;

	ba_info.flags = IEEE80211_TX_STAT_AMPDU;

	if (iwl_mvm_has_new_tx_api(mvm)) {
		struct iwl_mvm_compressed_ba_notif *ba_res =
			(void *)pkt->data;
		u8 lq_color = TX_RES_RATE_TABLE_COL_GET(ba_res->tlc_rate_info);
		u16 tfd_cnt;
		int i;

		if (IWL_FW_CHECK(mvm, sizeof(*ba_res) > pkt_len,
				 "short BA notification (%d)\n", pkt_len))
			return;

		sta_id = ba_res->sta_id;
		ba_info.status.ampdu_ack_len = (u8)le16_to_cpu(ba_res->done);
		ba_info.status.ampdu_len = (u8)le16_to_cpu(ba_res->txed);
		ba_info.status.tx_time =
			(u16)le32_to_cpu(ba_res->wireless_time);
		ba_info.status.status_driver_data[0] =
			(void *)(uintptr_t)ba_res->reduced_txp;

		tfd_cnt = le16_to_cpu(ba_res->tfd_cnt);
		if (!tfd_cnt)
			return;

		if (IWL_FW_CHECK(mvm,
				 struct_size(ba_res, tfd, tfd_cnt) > pkt_len,
				 "short BA notification (tfds:%d, size:%d)\n",
				 tfd_cnt, pkt_len))
			return;

		rcu_read_lock();

		mvmsta = iwl_mvm_sta_from_staid_rcu(mvm, sta_id);
		/*
		 * It's possible to get a BA response after invalidating the rcu
		 * (rcu is invalidated in order to prevent new Tx from being
		 * sent, but there may be some frames already in-flight).
		 * In this case we just want to reclaim, and could skip all the
		 * sta-dependent stuff since it's in the middle of being removed
		 * anyways.
		 */

		/* Free per TID */
		for (i = 0; i < tfd_cnt; i++) {
			struct iwl_mvm_compressed_ba_tfd *ba_tfd =
				&ba_res->tfd[i];

			tid = ba_tfd->tid;
			if (tid == IWL_MGMT_TID)
				tid = IWL_MAX_TID_COUNT;

			if (mvmsta)
				mvmsta->tid_data[i].lq_color = lq_color;

			iwl_mvm_tx_reclaim(mvm, sta_id, tid,
					   (int)(le16_to_cpu(ba_tfd->q_num)),
					   le16_to_cpu(ba_tfd->tfd_index),
					   &ba_info,
					   le32_to_cpu(ba_res->tx_rate), false);
		}

		if (mvmsta)
			iwl_mvm_tx_airtime(mvm, mvmsta,
					   le32_to_cpu(ba_res->wireless_time));
		rcu_read_unlock();

		IWL_DEBUG_TX_REPLY(mvm,
				   "BA_NOTIFICATION Received from sta_id = %d, flags %x, sent:%d, acked:%d\n",
				   sta_id, le32_to_cpu(ba_res->flags),
				   le16_to_cpu(ba_res->txed),
				   le16_to_cpu(ba_res->done));
		return;
	}

	ba_notif = (void *)pkt->data;
	sta_id = ba_notif->sta_id;
	tid = ba_notif->tid;
	/* "flow" corresponds to Tx queue */
	txq = le16_to_cpu(ba_notif->scd_flow);
	/* "ssn" is start of block-ack Tx window, corresponds to index
	 * (in Tx queue's circular buffer) of first TFD/frame in window */
	index = le16_to_cpu(ba_notif->scd_ssn);

	rcu_read_lock();
	mvmsta = iwl_mvm_sta_from_staid_rcu(mvm, sta_id);
	if (IWL_FW_CHECK(mvm, !mvmsta,
			 "invalid STA ID %d in BA notif\n",
			 sta_id)) {
		rcu_read_unlock();
		return;
	}

	tid_data = &mvmsta->tid_data[tid];

	ba_info.status.ampdu_ack_len = ba_notif->txed_2_done;
	ba_info.status.ampdu_len = ba_notif->txed;
	ba_info.status.tx_time = tid_data->tx_time;
	ba_info.status.status_driver_data[0] =
		(void *)(uintptr_t)ba_notif->reduced_txp;

	rcu_read_unlock();

	iwl_mvm_tx_reclaim(mvm, sta_id, tid, txq, index, &ba_info,
			   tid_data->rate_n_flags, false);

	IWL_DEBUG_TX_REPLY(mvm,
			   "BA_NOTIFICATION Received from %pM, sta_id = %d\n",
			   ba_notif->sta_addr, ba_notif->sta_id);

	IWL_DEBUG_TX_REPLY(mvm,
			   "TID = %d, SeqCtl = %d, bitmap = 0x%llx, scd_flow = %d, scd_ssn = %d sent:%d, acked:%d\n",
			   ba_notif->tid, le16_to_cpu(ba_notif->seq_ctl),
			   le64_to_cpu(ba_notif->bitmap), txq, index,
			   ba_notif->txed, ba_notif->txed_2_done);

	IWL_DEBUG_TX_REPLY(mvm, "reduced txp from ba notif %d\n",
			   ba_notif->reduced_txp);
}

/*
 * Note that there are transports that buffer frames before they reach
 * the firmware. This means that after flush_tx_path is called, the
 * queue might not be empty. The race-free way to handle this is to:
 * 1) set the station as draining
 * 2) flush the Tx path
 * 3) wait for the transport queues to be empty
 */
int iwl_mvm_flush_tx_path(struct iwl_mvm *mvm, u32 tfd_msk)
{
	int ret;
	struct iwl_tx_path_flush_cmd_v1 flush_cmd = {
		.queues_ctl = cpu_to_le32(tfd_msk),
		.flush_ctl = cpu_to_le16(DUMP_TX_FIFO_FLUSH),
	};

	WARN_ON(iwl_mvm_has_new_tx_api(mvm));
	ret = iwl_mvm_send_cmd_pdu(mvm, TXPATH_FLUSH, 0,
				   sizeof(flush_cmd), &flush_cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send flush command (%d)\n", ret);
	return ret;
}

int iwl_mvm_flush_sta_tids(struct iwl_mvm *mvm, u32 sta_id, u16 tids)
{
	int ret;
	struct iwl_tx_path_flush_cmd_rsp *rsp;
	struct iwl_tx_path_flush_cmd flush_cmd = {
		.sta_id = cpu_to_le32(sta_id),
		.tid_mask = cpu_to_le16(tids),
	};

	struct iwl_host_cmd cmd = {
		.id = TXPATH_FLUSH,
		.len = { sizeof(flush_cmd), },
		.data = { &flush_cmd, },
	};

	WARN_ON(!iwl_mvm_has_new_tx_api(mvm));

	if (iwl_fw_lookup_notif_ver(mvm->fw, LONG_GROUP, TXPATH_FLUSH, 0) > 0)
		cmd.flags |= CMD_WANT_SKB | CMD_SEND_IN_RFKILL;

	IWL_DEBUG_TX_QUEUES(mvm, "flush for sta id %d tid mask 0x%x\n",
			    sta_id, tids);

	ret = iwl_mvm_send_cmd(mvm, &cmd);

	if (ret) {
		IWL_ERR(mvm, "Failed to send flush command (%d)\n", ret);
		return ret;
	}

	if (cmd.flags & CMD_WANT_SKB) {
		int i;
		int num_flushed_queues;

		if (WARN_ON_ONCE(iwl_rx_packet_payload_len(cmd.resp_pkt) != sizeof(*rsp))) {
			ret = -EIO;
			goto free_rsp;
		}

		rsp = (void *)cmd.resp_pkt->data;

		if (WARN_ONCE(le16_to_cpu(rsp->sta_id) != sta_id,
			      "sta_id %d != rsp_sta_id %d",
			      sta_id, le16_to_cpu(rsp->sta_id))) {
			ret = -EIO;
			goto free_rsp;
		}

		num_flushed_queues = le16_to_cpu(rsp->num_flushed_queues);
		if (WARN_ONCE(num_flushed_queues > IWL_TX_FLUSH_QUEUE_RSP,
			      "num_flushed_queues %d", num_flushed_queues)) {
			ret = -EIO;
			goto free_rsp;
		}

		for (i = 0; i < num_flushed_queues; i++) {
			struct ieee80211_tx_info tx_info = {};
			struct iwl_flush_queue_info *queue_info = &rsp->queues[i];
			int tid = le16_to_cpu(queue_info->tid);
			int read_before = le16_to_cpu(queue_info->read_before_flush);
			int read_after = le16_to_cpu(queue_info->read_after_flush);
			int queue_num = le16_to_cpu(queue_info->queue_num);

			if (tid == IWL_MGMT_TID)
				tid = IWL_MAX_TID_COUNT;

			IWL_DEBUG_TX_QUEUES(mvm,
					    "tid %d queue_id %d read-before %d read-after %d\n",
					    tid, queue_num, read_before, read_after);

			iwl_mvm_tx_reclaim(mvm, sta_id, tid, queue_num, read_after,
					   &tx_info, 0, true);
		}
free_rsp:
		iwl_free_resp(&cmd);
	}
	return ret;
}

int iwl_mvm_flush_sta(struct iwl_mvm *mvm, u32 sta_id, u32 tfd_queue_mask)
{
	if (iwl_mvm_has_new_tx_api(mvm))
		return iwl_mvm_flush_sta_tids(mvm, sta_id, 0xffff);

	return iwl_mvm_flush_tx_path(mvm, tfd_queue_mask);
}
