/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "htc.h"

#define FUDGE 2

void ath9k_htc_beaconq_config(struct ath9k_htc_priv *priv)
{
	struct ath_hw *ah = priv->ah;
	struct ath9k_tx_queue_info qi, qi_be;

	memset(&qi, 0, sizeof(struct ath9k_tx_queue_info));
	memset(&qi_be, 0, sizeof(struct ath9k_tx_queue_info));

	ath9k_hw_get_txq_props(ah, priv->beacon.beaconq, &qi);

	if (priv->ah->opmode == NL80211_IFTYPE_AP ||
	    priv->ah->opmode == NL80211_IFTYPE_MESH_POINT) {
		qi.tqi_aifs = 1;
		qi.tqi_cwmin = 0;
		qi.tqi_cwmax = 0;
	} else if (priv->ah->opmode == NL80211_IFTYPE_ADHOC) {
		int qnum = priv->hwq_map[IEEE80211_AC_BE];

		ath9k_hw_get_txq_props(ah, qnum, &qi_be);

		qi.tqi_aifs = qi_be.tqi_aifs;

		/*
		 * For WIFI Beacon Distribution
		 * Long slot time  : 2x cwmin
		 * Short slot time : 4x cwmin
		 */
		if (ah->slottime == 20)
			qi.tqi_cwmin = 2*qi_be.tqi_cwmin;
		else
			qi.tqi_cwmin = 4*qi_be.tqi_cwmin;

		qi.tqi_cwmax = qi_be.tqi_cwmax;

	}

	if (!ath9k_hw_set_txq_props(ah, priv->beacon.beaconq, &qi)) {
		ath_err(ath9k_hw_common(ah),
			"Unable to update beacon queue %u!\n", priv->beacon.beaconq);
	} else {
		ath9k_hw_resettxqueue(ah, priv->beacon.beaconq);
	}
}

/*
 * Both nexttbtt and intval have to be in usecs.
 */
static void ath9k_htc_beacon_init(struct ath9k_htc_priv *priv,
				  struct ath_beacon_config *conf,
				  bool reset_tsf)
{
	struct ath_hw *ah = priv->ah;
	int ret __attribute__ ((unused));
	__be32 htc_imask = 0;
	u8 cmd_rsp;

	if (conf->intval >= TU_TO_USEC(DEFAULT_SWBA_RESPONSE))
		ah->config.sw_beacon_response_time = DEFAULT_SWBA_RESPONSE;
	else
		ah->config.sw_beacon_response_time = MIN_SWBA_RESPONSE;

	WMI_CMD(WMI_DISABLE_INTR_CMDID);
	if (reset_tsf)
		ath9k_hw_reset_tsf(ah);
	ath9k_htc_beaconq_config(priv);
	ath9k_hw_beaconinit(ah, conf->nexttbtt, conf->intval);
	priv->beacon.bmisscnt = 0;
	htc_imask = cpu_to_be32(ah->imask);
	WMI_CMD_BUF(WMI_ENABLE_INTR_CMDID, &htc_imask);
}

static void ath9k_htc_beacon_config_sta(struct ath9k_htc_priv *priv,
					struct ath_beacon_config *bss_conf)
{
	struct ath9k_beacon_state bs;
	enum ath9k_int imask = 0;
	__be32 htc_imask = 0;
	int ret __attribute__ ((unused));
	u8 cmd_rsp;

	if (ath9k_cmn_beacon_config_sta(priv->ah, bss_conf, &bs) == -EPERM)
		return;

	WMI_CMD(WMI_DISABLE_INTR_CMDID);
	ath9k_hw_set_sta_beacon_timers(priv->ah, &bs);
	imask |= ATH9K_INT_BMISS;
	htc_imask = cpu_to_be32(imask);
	WMI_CMD_BUF(WMI_ENABLE_INTR_CMDID, &htc_imask);
}

static void ath9k_htc_beacon_config_ap(struct ath9k_htc_priv *priv,
				       struct ath_beacon_config *conf)
{
	struct ath_hw *ah = priv->ah;
	ah->imask = 0;

	ath9k_cmn_beacon_config_ap(ah, conf, ATH9K_HTC_MAX_BCN_VIF);
	ath9k_htc_beacon_init(priv, conf, false);
}

static void ath9k_htc_beacon_config_adhoc(struct ath9k_htc_priv *priv,
					  struct ath_beacon_config *conf)
{
	struct ath_hw *ah = priv->ah;
	ah->imask = 0;

	ath9k_cmn_beacon_config_adhoc(ah, conf);
	ath9k_htc_beacon_init(priv, conf, conf->ibss_creator);
}

void ath9k_htc_beaconep(void *drv_priv, struct sk_buff *skb,
			enum htc_endpoint_id ep_id, bool txok)
{
	dev_kfree_skb_any(skb);
}

static void ath9k_htc_send_buffered(struct ath9k_htc_priv *priv,
				    int slot)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ieee80211_vif *vif;
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	int padpos, padsize, ret, tx_slot;

	spin_lock_bh(&priv->beacon_lock);

	vif = priv->beacon.bslot[slot];

	skb = ieee80211_get_buffered_bc(priv->hw, vif);

	while(skb) {
		hdr = (struct ieee80211_hdr *) skb->data;

		padpos = ieee80211_hdrlen(hdr->frame_control);
		padsize = padpos & 3;
		if (padsize && skb->len > padpos) {
			if (skb_headroom(skb) < padsize) {
				dev_kfree_skb_any(skb);
				goto next;
			}
			skb_push(skb, padsize);
			memmove(skb->data, skb->data + padsize, padpos);
		}

		tx_slot = ath9k_htc_tx_get_slot(priv);
		if (tx_slot < 0) {
			ath_dbg(common, XMIT, "No free CAB slot\n");
			dev_kfree_skb_any(skb);
			goto next;
		}

		ret = ath9k_htc_tx_start(priv, NULL, skb, tx_slot, true);
		if (ret != 0) {
			ath9k_htc_tx_clear_slot(priv, tx_slot);
			dev_kfree_skb_any(skb);

			ath_dbg(common, XMIT, "Failed to send CAB frame\n");
		} else {
			spin_lock_bh(&priv->tx.tx_lock);
			priv->tx.queued_cnt++;
			spin_unlock_bh(&priv->tx.tx_lock);
		}
	next:
		skb = ieee80211_get_buffered_bc(priv->hw, vif);
	}

	spin_unlock_bh(&priv->beacon_lock);
}

static void ath9k_htc_send_beacon(struct ath9k_htc_priv *priv,
				  int slot)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ieee80211_vif *vif;
	struct ath9k_htc_vif *avp;
	struct tx_beacon_header beacon_hdr;
	struct ath9k_htc_tx_ctl *tx_ctl;
	struct ieee80211_tx_info *info;
	struct ieee80211_mgmt *mgmt;
	struct sk_buff *beacon;
	u8 *tx_fhdr;
	int ret;

	memset(&beacon_hdr, 0, sizeof(struct tx_beacon_header));

	spin_lock_bh(&priv->beacon_lock);

	vif = priv->beacon.bslot[slot];
	avp = (struct ath9k_htc_vif *)vif->drv_priv;

	if (unlikely(test_bit(ATH_OP_SCANNING, &common->op_flags))) {
		spin_unlock_bh(&priv->beacon_lock);
		return;
	}

	/* Get a new beacon */
	beacon = ieee80211_beacon_get(priv->hw, vif, 0);
	if (!beacon) {
		spin_unlock_bh(&priv->beacon_lock);
		return;
	}

	/*
	 * Update the TSF adjust value here, the HW will
	 * add this value for every beacon.
	 */
	mgmt = (struct ieee80211_mgmt *)beacon->data;
	mgmt->u.beacon.timestamp = avp->tsfadjust;

	info = IEEE80211_SKB_CB(beacon);
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		struct ieee80211_hdr *hdr =
			(struct ieee80211_hdr *) beacon->data;
		avp->seq_no += 0x10;
		hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(avp->seq_no);
	}

	tx_ctl = HTC_SKB_CB(beacon);
	memset(tx_ctl, 0, sizeof(*tx_ctl));

	tx_ctl->type = ATH9K_HTC_BEACON;
	tx_ctl->epid = priv->beacon_ep;

	beacon_hdr.vif_index = avp->index;
	tx_fhdr = skb_push(beacon, sizeof(beacon_hdr));
	memcpy(tx_fhdr, (u8 *) &beacon_hdr, sizeof(beacon_hdr));

	ret = htc_send(priv->htc, beacon);
	if (ret != 0) {
		if (ret == -ENOMEM) {
			ath_dbg(common, BSTUCK,
				"Failed to send beacon, no free TX buffer\n");
		}
		dev_kfree_skb_any(beacon);
	}

	spin_unlock_bh(&priv->beacon_lock);

	ath9k_htc_csa_is_finished(priv);
}

static int ath9k_htc_choose_bslot(struct ath9k_htc_priv *priv,
				  struct wmi_event_swba *swba)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	u64 tsf;
	u32 tsftu;
	u16 intval;
	int slot;

	intval = priv->cur_beacon_conf.beacon_interval;

	tsf = be64_to_cpu(swba->tsf);
	tsftu = TSF_TO_TU(tsf >> 32, tsf);
	slot = ((tsftu % intval) * ATH9K_HTC_MAX_BCN_VIF) / intval;
	slot = ATH9K_HTC_MAX_BCN_VIF - slot - 1;

	ath_dbg(common, BEACON,
		"Choose slot: %d, tsf: %llu, tsftu: %u, intval: %u\n",
		slot, tsf, tsftu, intval);

	return slot;
}

void ath9k_htc_swba(struct ath9k_htc_priv *priv,
		    struct wmi_event_swba *swba)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	int slot;

	if (!priv->cur_beacon_conf.enable_beacon)
		return;

	if (swba->beacon_pending != 0) {
		priv->beacon.bmisscnt++;
		if (priv->beacon.bmisscnt > BSTUCK_THRESHOLD) {
			ath_dbg(common, BSTUCK, "Beacon stuck, HW reset\n");
			ieee80211_queue_work(priv->hw,
					     &priv->fatal_work);
		}
		return;
	}

	if (priv->beacon.bmisscnt) {
		ath_dbg(common, BSTUCK,
			"Resuming beacon xmit after %u misses\n",
			priv->beacon.bmisscnt);
		priv->beacon.bmisscnt = 0;
	}

	slot = ath9k_htc_choose_bslot(priv, swba);
	spin_lock_bh(&priv->beacon_lock);
	if (priv->beacon.bslot[slot] == NULL) {
		spin_unlock_bh(&priv->beacon_lock);
		return;
	}
	spin_unlock_bh(&priv->beacon_lock);

	ath9k_htc_send_buffered(priv, slot);
	ath9k_htc_send_beacon(priv, slot);
}

void ath9k_htc_assign_bslot(struct ath9k_htc_priv *priv,
			    struct ieee80211_vif *vif)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_vif *avp = (struct ath9k_htc_vif *)vif->drv_priv;
	int i = 0;

	spin_lock_bh(&priv->beacon_lock);
	for (i = 0; i < ATH9K_HTC_MAX_BCN_VIF; i++) {
		if (priv->beacon.bslot[i] == NULL) {
			avp->bslot = i;
			break;
		}
	}

	priv->beacon.bslot[avp->bslot] = vif;
	spin_unlock_bh(&priv->beacon_lock);

	ath_dbg(common, CONFIG, "Added interface at beacon slot: %d\n",
		avp->bslot);
}

void ath9k_htc_remove_bslot(struct ath9k_htc_priv *priv,
			    struct ieee80211_vif *vif)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_vif *avp = (struct ath9k_htc_vif *)vif->drv_priv;

	spin_lock_bh(&priv->beacon_lock);
	priv->beacon.bslot[avp->bslot] = NULL;
	spin_unlock_bh(&priv->beacon_lock);

	ath_dbg(common, CONFIG, "Removed interface at beacon slot: %d\n",
		avp->bslot);
}

/*
 * Calculate the TSF adjustment value for all slots
 * other than zero.
 */
void ath9k_htc_set_tsfadjust(struct ath9k_htc_priv *priv,
			     struct ieee80211_vif *vif)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_htc_vif *avp = (struct ath9k_htc_vif *)vif->drv_priv;
	struct ath_beacon_config *cur_conf = &priv->cur_beacon_conf;
	u64 tsfadjust;

	if (avp->bslot == 0)
		return;

	/*
	 * The beacon interval cannot be different for multi-AP mode,
	 * and we reach here only for VIF slots greater than zero,
	 * so beacon_interval is guaranteed to be set in cur_conf.
	 */
	tsfadjust = cur_conf->beacon_interval * avp->bslot / ATH9K_HTC_MAX_BCN_VIF;
	avp->tsfadjust = cpu_to_le64(TU_TO_USEC(tsfadjust));

	ath_dbg(common, CONFIG, "tsfadjust is: %llu for bslot: %d\n",
		(unsigned long long)tsfadjust, avp->bslot);
}

static void ath9k_htc_beacon_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	bool *beacon_configured = data;
	struct ath9k_htc_vif *avp = (struct ath9k_htc_vif *) vif->drv_priv;

	if (vif->type == NL80211_IFTYPE_STATION &&
	    avp->beacon_configured)
		*beacon_configured = true;
}

static bool ath9k_htc_check_beacon_config(struct ath9k_htc_priv *priv,
					  struct ieee80211_vif *vif)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath_beacon_config *cur_conf = &priv->cur_beacon_conf;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;
	bool beacon_configured;

	/*
	 * Changing the beacon interval when multiple AP interfaces
	 * are configured will affect beacon transmission of all
	 * of them.
	 */
	if ((priv->ah->opmode == NL80211_IFTYPE_AP) &&
	    (priv->num_ap_vif > 1) &&
	    (vif->type == NL80211_IFTYPE_AP) &&
	    (cur_conf->beacon_interval != bss_conf->beacon_int)) {
		ath_dbg(common, CONFIG,
			"Changing beacon interval of multiple AP interfaces !\n");
		return false;
	}

	/*
	 * If the HW is operating in AP mode, any new station interfaces that
	 * are added cannot change the beacon parameters.
	 */
	if (priv->num_ap_vif &&
	    (vif->type != NL80211_IFTYPE_AP)) {
		ath_dbg(common, CONFIG,
			"HW in AP mode, cannot set STA beacon parameters\n");
		return false;
	}

	/*
	 * The beacon parameters are configured only for the first
	 * station interface.
	 */
	if ((priv->ah->opmode == NL80211_IFTYPE_STATION) &&
	    (priv->num_sta_vif > 1) &&
	    (vif->type == NL80211_IFTYPE_STATION)) {
		beacon_configured = false;
		ieee80211_iterate_active_interfaces_atomic(
			priv->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
			ath9k_htc_beacon_iter, &beacon_configured);

		if (beacon_configured) {
			ath_dbg(common, CONFIG,
				"Beacon already configured for a station interface\n");
			return false;
		}
	}

	return true;
}

void ath9k_htc_beacon_config(struct ath9k_htc_priv *priv,
			     struct ieee80211_vif *vif)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath_beacon_config *cur_conf = &priv->cur_beacon_conf;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;
	struct ath9k_htc_vif *avp = (struct ath9k_htc_vif *) vif->drv_priv;

	if (!ath9k_htc_check_beacon_config(priv, vif))
		return;

	cur_conf->beacon_interval = bss_conf->beacon_int;
	if (cur_conf->beacon_interval == 0)
		cur_conf->beacon_interval = 100;

	cur_conf->dtim_period = bss_conf->dtim_period;
	cur_conf->bmiss_timeout =
		ATH_DEFAULT_BMISS_LIMIT * cur_conf->beacon_interval;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		ath9k_htc_beacon_config_sta(priv, cur_conf);
		avp->beacon_configured = true;
		break;
	case NL80211_IFTYPE_ADHOC:
		ath9k_htc_beacon_config_adhoc(priv, cur_conf);
		break;
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		ath9k_htc_beacon_config_ap(priv, cur_conf);
		break;
	default:
		ath_dbg(common, CONFIG, "Unsupported beaconing mode\n");
		return;
	}
}

void ath9k_htc_beacon_reconfig(struct ath9k_htc_priv *priv)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath_beacon_config *cur_conf = &priv->cur_beacon_conf;

	switch (priv->ah->opmode) {
	case NL80211_IFTYPE_STATION:
		ath9k_htc_beacon_config_sta(priv, cur_conf);
		break;
	case NL80211_IFTYPE_ADHOC:
		ath9k_htc_beacon_config_adhoc(priv, cur_conf);
		break;
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		ath9k_htc_beacon_config_ap(priv, cur_conf);
		break;
	default:
		ath_dbg(common, CONFIG, "Unsupported beaconing mode\n");
		return;
	}
}

bool ath9k_htc_csa_is_finished(struct ath9k_htc_priv *priv)
{
	struct ieee80211_vif *vif;

	vif = priv->csa_vif;
	if (!vif || !vif->bss_conf.csa_active)
		return false;

	if (!ieee80211_beacon_cntdwn_is_complete(vif, 0))
		return false;

	ieee80211_csa_finish(vif, 0);

	priv->csa_vif = NULL;
	return true;
}
