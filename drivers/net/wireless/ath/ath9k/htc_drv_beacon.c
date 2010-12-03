/*
 * Copyright (c) 2010 Atheros Communications Inc.
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

static void ath9k_htc_beacon_config_sta(struct ath9k_htc_priv *priv,
					struct htc_beacon_config *bss_conf)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath9k_beacon_state bs;
	enum ath9k_int imask = 0;
	int dtimperiod, dtimcount, sleepduration;
	int cfpperiod, cfpcount, bmiss_timeout;
	u32 nexttbtt = 0, intval, tsftu;
	__be32 htc_imask = 0;
	u64 tsf;
	int num_beacons, offset, dtim_dec_count, cfp_dec_count;
	int ret;
	u8 cmd_rsp;

	memset(&bs, 0, sizeof(bs));

	intval = bss_conf->beacon_interval & ATH9K_BEACON_PERIOD;
	bmiss_timeout = (ATH_DEFAULT_BMISS_LIMIT * bss_conf->beacon_interval);

	/*
	 * Setup dtim and cfp parameters according to
	 * last beacon we received (which may be none).
	 */
	dtimperiod = bss_conf->dtim_period;
	if (dtimperiod <= 0)		/* NB: 0 if not known */
		dtimperiod = 1;
	dtimcount = 1;
	if (dtimcount >= dtimperiod)	/* NB: sanity check */
		dtimcount = 0;
	cfpperiod = 1;			/* NB: no PCF support yet */
	cfpcount = 0;

	sleepduration = intval;
	if (sleepduration <= 0)
		sleepduration = intval;

	/*
	 * Pull nexttbtt forward to reflect the current
	 * TSF and calculate dtim+cfp state for the result.
	 */
	tsf = ath9k_hw_gettsf64(priv->ah);
	tsftu = TSF_TO_TU(tsf>>32, tsf) + FUDGE;

	num_beacons = tsftu / intval + 1;
	offset = tsftu % intval;
	nexttbtt = tsftu - offset;
	if (offset)
		nexttbtt += intval;

	/* DTIM Beacon every dtimperiod Beacon */
	dtim_dec_count = num_beacons % dtimperiod;
	/* CFP every cfpperiod DTIM Beacon */
	cfp_dec_count = (num_beacons / dtimperiod) % cfpperiod;
	if (dtim_dec_count)
		cfp_dec_count++;

	dtimcount -= dtim_dec_count;
	if (dtimcount < 0)
		dtimcount += dtimperiod;

	cfpcount -= cfp_dec_count;
	if (cfpcount < 0)
		cfpcount += cfpperiod;

	bs.bs_intval = intval;
	bs.bs_nexttbtt = nexttbtt;
	bs.bs_dtimperiod = dtimperiod*intval;
	bs.bs_nextdtim = bs.bs_nexttbtt + dtimcount*intval;
	bs.bs_cfpperiod = cfpperiod*bs.bs_dtimperiod;
	bs.bs_cfpnext = bs.bs_nextdtim + cfpcount*bs.bs_dtimperiod;
	bs.bs_cfpmaxduration = 0;

	/*
	 * Calculate the number of consecutive beacons to miss* before taking
	 * a BMISS interrupt. The configuration is specified in TU so we only
	 * need calculate based	on the beacon interval.  Note that we clamp the
	 * result to at most 15 beacons.
	 */
	if (sleepduration > intval) {
		bs.bs_bmissthreshold = ATH_DEFAULT_BMISS_LIMIT / 2;
	} else {
		bs.bs_bmissthreshold = DIV_ROUND_UP(bmiss_timeout, intval);
		if (bs.bs_bmissthreshold > 15)
			bs.bs_bmissthreshold = 15;
		else if (bs.bs_bmissthreshold <= 0)
			bs.bs_bmissthreshold = 1;
	}

	/*
	 * Calculate sleep duration. The configuration is given in ms.
	 * We ensure a multiple of the beacon period is used. Also, if the sleep
	 * duration is greater than the DTIM period then it makes senses
	 * to make it a multiple of that.
	 *
	 * XXX fixed at 100ms
	 */

	bs.bs_sleepduration = roundup(IEEE80211_MS_TO_TU(100), sleepduration);
	if (bs.bs_sleepduration > bs.bs_dtimperiod)
		bs.bs_sleepduration = bs.bs_dtimperiod;

	/* TSF out of range threshold fixed at 1 second */
	bs.bs_tsfoor_threshold = ATH9K_TSFOOR_THRESHOLD;

	ath_print(common, ATH_DBG_BEACON, "tsf: %llu tsftu: %u\n", tsf, tsftu);
	ath_print(common, ATH_DBG_BEACON,
		  "bmiss: %u sleep: %u cfp-period: %u maxdur: %u next: %u\n",
		  bs.bs_bmissthreshold, bs.bs_sleepduration,
		  bs.bs_cfpperiod, bs.bs_cfpmaxduration, bs.bs_cfpnext);

	/* Set the computed STA beacon timers */

	WMI_CMD(WMI_DISABLE_INTR_CMDID);
	ath9k_hw_set_sta_beacon_timers(priv->ah, &bs);
	imask |= ATH9K_INT_BMISS;
	htc_imask = cpu_to_be32(imask);
	WMI_CMD_BUF(WMI_ENABLE_INTR_CMDID, &htc_imask);
}

static void ath9k_htc_beacon_config_adhoc(struct ath9k_htc_priv *priv,
					  struct htc_beacon_config *bss_conf)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	enum ath9k_int imask = 0;
	u32 nexttbtt, intval;
	__be32 htc_imask = 0;
	int ret;
	u8 cmd_rsp;

	intval = bss_conf->beacon_interval & ATH9K_BEACON_PERIOD;
	nexttbtt = intval;
	intval |= ATH9K_BEACON_ENA;
	if (priv->op_flags & OP_ENABLE_BEACON)
		imask |= ATH9K_INT_SWBA;

	ath_print(common, ATH_DBG_BEACON,
		  "IBSS Beacon config, intval: %d, imask: 0x%x\n",
		  bss_conf->beacon_interval, imask);

	WMI_CMD(WMI_DISABLE_INTR_CMDID);
	ath9k_hw_beaconinit(priv->ah, nexttbtt, intval);
	priv->bmiss_cnt = 0;
	htc_imask = cpu_to_be32(imask);
	WMI_CMD_BUF(WMI_ENABLE_INTR_CMDID, &htc_imask);
}

void ath9k_htc_beaconep(void *drv_priv, struct sk_buff *skb,
			enum htc_endpoint_id ep_id, bool txok)
{
	dev_kfree_skb_any(skb);
}

void ath9k_htc_swba(struct ath9k_htc_priv *priv, u8 beacon_pending)
{
	struct ath9k_htc_vif *avp = (void *)priv->vif->drv_priv;
	struct tx_beacon_header beacon_hdr;
	struct ath9k_htc_tx_ctl tx_ctl;
	struct ieee80211_tx_info *info;
	struct sk_buff *beacon;
	u8 *tx_fhdr;

	memset(&beacon_hdr, 0, sizeof(struct tx_beacon_header));
	memset(&tx_ctl, 0, sizeof(struct ath9k_htc_tx_ctl));

	/* FIXME: Handle BMISS */
	if (beacon_pending != 0) {
		priv->bmiss_cnt++;
		return;
	}

	spin_lock_bh(&priv->beacon_lock);

	if (unlikely(priv->op_flags & OP_SCANNING)) {
		spin_unlock_bh(&priv->beacon_lock);
		return;
	}

	/* Get a new beacon */
	beacon = ieee80211_beacon_get(priv->hw, priv->vif);
	if (!beacon) {
		spin_unlock_bh(&priv->beacon_lock);
		return;
	}

	info = IEEE80211_SKB_CB(beacon);
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		struct ieee80211_hdr *hdr =
			(struct ieee80211_hdr *) beacon->data;
		priv->seq_no += 0x10;
		hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(priv->seq_no);
	}

	tx_ctl.type = ATH9K_HTC_NORMAL;
	beacon_hdr.vif_index = avp->index;
	tx_fhdr = skb_push(beacon, sizeof(beacon_hdr));
	memcpy(tx_fhdr, (u8 *) &beacon_hdr, sizeof(beacon_hdr));

	htc_send(priv->htc, beacon, priv->beacon_ep, &tx_ctl);

	spin_unlock_bh(&priv->beacon_lock);
}

/* Currently, only for IBSS */
void ath9k_htc_beaconq_config(struct ath9k_htc_priv *priv)
{
	struct ath_hw *ah = priv->ah;
	struct ath9k_tx_queue_info qi, qi_be;
	int qnum = priv->hwq_map[WME_AC_BE];

	memset(&qi, 0, sizeof(struct ath9k_tx_queue_info));
	memset(&qi_be, 0, sizeof(struct ath9k_tx_queue_info));

	ath9k_hw_get_txq_props(ah, qnum, &qi_be);

	qi.tqi_aifs = qi_be.tqi_aifs;
	/* For WIFI Beacon Distribution
	 * Long slot time  : 2x cwmin
	 * Short slot time : 4x cwmin
	 */
	if (ah->slottime == ATH9K_SLOT_TIME_20)
		qi.tqi_cwmin = 2*qi_be.tqi_cwmin;
	else
		qi.tqi_cwmin = 4*qi_be.tqi_cwmin;
	qi.tqi_cwmax = qi_be.tqi_cwmax;

	if (!ath9k_hw_set_txq_props(ah, priv->beaconq, &qi)) {
		ath_err(ath9k_hw_common(ah),
			"Unable to update beacon queue %u!\n", qnum);
	} else {
		ath9k_hw_resettxqueue(ah, priv->beaconq);
	}
}

void ath9k_htc_beacon_config(struct ath9k_htc_priv *priv,
			     struct ieee80211_vif *vif)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct htc_beacon_config *cur_conf = &priv->cur_beacon_conf;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

	cur_conf->beacon_interval = bss_conf->beacon_int;
	if (cur_conf->beacon_interval == 0)
		cur_conf->beacon_interval = 100;

	cur_conf->dtim_period = bss_conf->dtim_period;
	cur_conf->listen_interval = 1;
	cur_conf->dtim_count = 1;
	cur_conf->bmiss_timeout =
		ATH_DEFAULT_BMISS_LIMIT * cur_conf->beacon_interval;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		ath9k_htc_beacon_config_sta(priv, cur_conf);
		break;
	case NL80211_IFTYPE_ADHOC:
		ath9k_htc_beacon_config_adhoc(priv, cur_conf);
		break;
	default:
		ath_print(common, ATH_DBG_CONFIG,
			  "Unsupported beaconing mode\n");
		return;
	}
}
