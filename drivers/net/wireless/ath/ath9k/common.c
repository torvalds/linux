/*
 * Copyright (c) 2009 Atheros Communications Inc.
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

/*
 * Module for common driver code between ath9k and ath9k_htc
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "common.h"

MODULE_AUTHOR("Atheros Communications");
MODULE_DESCRIPTION("Shared library for Atheros wireless 802.11n LAN cards.");
MODULE_LICENSE("Dual BSD/GPL");

/* Common RX processing */

/* Assumes you've already done the endian to CPU conversion */
static bool ath9k_rx_accept(struct ath_common *common,
			    struct sk_buff *skb,
			    struct ieee80211_rx_status *rxs,
			    struct ath_rx_status *rx_stats,
			    bool *decrypt_error)
{
	struct ath_hw *ah = common->ah;
	struct ieee80211_hdr *hdr;
	__le16 fc;

	hdr = (struct ieee80211_hdr *) skb->data;
	fc = hdr->frame_control;

	if (!rx_stats->rs_datalen)
		return false;
        /*
         * rs_status follows rs_datalen so if rs_datalen is too large
         * we can take a hint that hardware corrupted it, so ignore
         * those frames.
         */
	if (rx_stats->rs_datalen > common->rx_bufsize)
		return false;

	/*
	 * rs_more indicates chained descriptors which can be used
	 * to link buffers together for a sort of scatter-gather
	 * operation.
	 * reject the frame, we don't support scatter-gather yet and
	 * the frame is probably corrupt anyway
	 */
	if (rx_stats->rs_more)
		return false;

	/*
	 * The rx_stats->rs_status will not be set until the end of the
	 * chained descriptors so it can be ignored if rs_more is set. The
	 * rs_more will be false at the last element of the chained
	 * descriptors.
	 */
	if (rx_stats->rs_status != 0) {
		if (rx_stats->rs_status & ATH9K_RXERR_CRC)
			rxs->flag |= RX_FLAG_FAILED_FCS_CRC;
		if (rx_stats->rs_status & ATH9K_RXERR_PHY)
			return false;

		if (rx_stats->rs_status & ATH9K_RXERR_DECRYPT) {
			*decrypt_error = true;
		} else if (rx_stats->rs_status & ATH9K_RXERR_MIC) {
			if (ieee80211_is_ctl(fc))
				/*
				 * Sometimes, we get invalid
				 * MIC failures on valid control frames.
				 * Remove these mic errors.
				 */
				rx_stats->rs_status &= ~ATH9K_RXERR_MIC;
			else
				rxs->flag |= RX_FLAG_MMIC_ERROR;
		}
		/*
		 * Reject error frames with the exception of
		 * decryption and MIC failures. For monitor mode,
		 * we also ignore the CRC error.
		 */
		if (ah->opmode == NL80211_IFTYPE_MONITOR) {
			if (rx_stats->rs_status &
			    ~(ATH9K_RXERR_DECRYPT | ATH9K_RXERR_MIC |
			      ATH9K_RXERR_CRC))
				return false;
		} else {
			if (rx_stats->rs_status &
			    ~(ATH9K_RXERR_DECRYPT | ATH9K_RXERR_MIC)) {
				return false;
			}
		}
	}
	return true;
}

static int ath9k_process_rate(struct ath_common *common,
			      struct ieee80211_hw *hw,
			      struct ath_rx_status *rx_stats,
			      struct ieee80211_rx_status *rxs,
			      struct sk_buff *skb)
{
	struct ieee80211_supported_band *sband;
	enum ieee80211_band band;
	unsigned int i = 0;

	band = hw->conf.channel->band;
	sband = hw->wiphy->bands[band];

	if (rx_stats->rs_rate & 0x80) {
		/* HT rate */
		rxs->flag |= RX_FLAG_HT;
		if (rx_stats->rs_flags & ATH9K_RX_2040)
			rxs->flag |= RX_FLAG_40MHZ;
		if (rx_stats->rs_flags & ATH9K_RX_GI)
			rxs->flag |= RX_FLAG_SHORT_GI;
		rxs->rate_idx = rx_stats->rs_rate & 0x7f;
		return 0;
	}

	for (i = 0; i < sband->n_bitrates; i++) {
		if (sband->bitrates[i].hw_value == rx_stats->rs_rate) {
			rxs->rate_idx = i;
			return 0;
		}
		if (sband->bitrates[i].hw_value_short == rx_stats->rs_rate) {
			rxs->flag |= RX_FLAG_SHORTPRE;
			rxs->rate_idx = i;
			return 0;
		}
	}

	/*
	 * No valid hardware bitrate found -- we should not get here
	 * because hardware has already validated this frame as OK.
	 */
	ath_print(common, ATH_DBG_XMIT, "unsupported hw bitrate detected "
		  "0x%02x using 1 Mbit\n", rx_stats->rs_rate);
	if ((common->debug_mask & ATH_DBG_XMIT))
		print_hex_dump_bytes("", DUMP_PREFIX_NONE, skb->data, skb->len);

	return -EINVAL;
}

static void ath9k_process_rssi(struct ath_common *common,
			       struct ieee80211_hw *hw,
			       struct sk_buff *skb,
			       struct ath_rx_status *rx_stats)
{
	struct ath_hw *ah = common->ah;
	struct ieee80211_sta *sta;
	struct ieee80211_hdr *hdr;
	struct ath_node *an;
	int last_rssi = ATH_RSSI_DUMMY_MARKER;
	__le16 fc;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;

	rcu_read_lock();
	/*
	 * XXX: use ieee80211_find_sta! This requires quite a bit of work
	 * under the current ath9k virtual wiphy implementation as we have
	 * no way of tying a vif to wiphy. Typically vifs are attached to
	 * at least one sdata of a wiphy on mac80211 but with ath9k virtual
	 * wiphy you'd have to iterate over every wiphy and each sdata.
	 */
	sta = ieee80211_find_sta_by_hw(hw, hdr->addr2);
	if (sta) {
		an = (struct ath_node *) sta->drv_priv;
		if (rx_stats->rs_rssi != ATH9K_RSSI_BAD &&
		   !rx_stats->rs_moreaggr)
			ATH_RSSI_LPF(an->last_rssi, rx_stats->rs_rssi);
		last_rssi = an->last_rssi;
	}
	rcu_read_unlock();

	if (likely(last_rssi != ATH_RSSI_DUMMY_MARKER))
		rx_stats->rs_rssi = ATH_EP_RND(last_rssi,
					      ATH_RSSI_EP_MULTIPLIER);
	if (rx_stats->rs_rssi < 0)
		rx_stats->rs_rssi = 0;

	/* Update Beacon RSSI, this is used by ANI. */
	if (ieee80211_is_beacon(fc))
		ah->stats.avgbrssi = rx_stats->rs_rssi;
}

/*
 * For Decrypt or Demic errors, we only mark packet status here and always push
 * up the frame up to let mac80211 handle the actual error case, be it no
 * decryption key or real decryption error. This let us keep statistics there.
 */
int ath9k_cmn_rx_skb_preprocess(struct ath_common *common,
				struct ieee80211_hw *hw,
				struct sk_buff *skb,
				struct ath_rx_status *rx_stats,
				struct ieee80211_rx_status *rx_status,
				bool *decrypt_error)
{
	struct ath_hw *ah = common->ah;

	memset(rx_status, 0, sizeof(struct ieee80211_rx_status));

	/*
	 * everything but the rate is checked here, the rate check is done
	 * separately to avoid doing two lookups for a rate for each frame.
	 */
	if (!ath9k_rx_accept(common, skb, rx_status, rx_stats, decrypt_error))
		return -EINVAL;

	ath9k_process_rssi(common, hw, skb, rx_stats);

	if (ath9k_process_rate(common, hw, rx_stats, rx_status, skb))
		return -EINVAL;

	rx_status->mactime = ath9k_hw_extend_tsf(ah, rx_stats->rs_tstamp);
	rx_status->band = hw->conf.channel->band;
	rx_status->freq = hw->conf.channel->center_freq;
	rx_status->signal = ATH_DEFAULT_NOISE_FLOOR + rx_stats->rs_rssi;
	rx_status->antenna = rx_stats->rs_antenna;
	rx_status->flag |= RX_FLAG_TSFT;

	return 0;
}
EXPORT_SYMBOL(ath9k_cmn_rx_skb_preprocess);

void ath9k_cmn_rx_skb_postprocess(struct ath_common *common,
				  struct sk_buff *skb,
				  struct ath_rx_status *rx_stats,
				  struct ieee80211_rx_status *rxs,
				  bool decrypt_error)
{
	struct ath_hw *ah = common->ah;
	struct ieee80211_hdr *hdr;
	int hdrlen, padpos, padsize;
	u8 keyix;
	__le16 fc;

	/* see if any padding is done by the hw and remove it */
	hdr = (struct ieee80211_hdr *) skb->data;
	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	fc = hdr->frame_control;
	padpos = ath9k_cmn_padpos(hdr->frame_control);

	/* The MAC header is padded to have 32-bit boundary if the
	 * packet payload is non-zero. The general calculation for
	 * padsize would take into account odd header lengths:
	 * padsize = (4 - padpos % 4) % 4; However, since only
	 * even-length headers are used, padding can only be 0 or 2
	 * bytes and we can optimize this a bit. In addition, we must
	 * not try to remove padding from short control frames that do
	 * not have payload. */
	padsize = padpos & 3;
	if (padsize && skb->len>=padpos+padsize+FCS_LEN) {
		memmove(skb->data + padsize, skb->data, padpos);
		skb_pull(skb, padsize);
	}

	keyix = rx_stats->rs_keyix;

	if (!(keyix == ATH9K_RXKEYIX_INVALID) && !decrypt_error &&
	    ieee80211_has_protected(fc)) {
		rxs->flag |= RX_FLAG_DECRYPTED;
	} else if (ieee80211_has_protected(fc)
		   && !decrypt_error && skb->len >= hdrlen + 4) {
		keyix = skb->data[hdrlen + 3] >> 6;

		if (test_bit(keyix, common->keymap))
			rxs->flag |= RX_FLAG_DECRYPTED;
	}
	if (ah->sw_mgmt_crypto &&
	    (rxs->flag & RX_FLAG_DECRYPTED) &&
	    ieee80211_is_mgmt(fc))
		/* Use software decrypt for management frames. */
		rxs->flag &= ~RX_FLAG_DECRYPTED;
}
EXPORT_SYMBOL(ath9k_cmn_rx_skb_postprocess);

int ath9k_cmn_padpos(__le16 frame_control)
{
	int padpos = 24;
	if (ieee80211_has_a4(frame_control)) {
		padpos += ETH_ALEN;
	}
	if (ieee80211_is_data_qos(frame_control)) {
		padpos += IEEE80211_QOS_CTL_LEN;
	}

	return padpos;
}
EXPORT_SYMBOL(ath9k_cmn_padpos);

int ath9k_cmn_get_hw_crypto_keytype(struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	if (tx_info->control.hw_key) {
		if (tx_info->control.hw_key->alg == ALG_WEP)
			return ATH9K_KEY_TYPE_WEP;
		else if (tx_info->control.hw_key->alg == ALG_TKIP)
			return ATH9K_KEY_TYPE_TKIP;
		else if (tx_info->control.hw_key->alg == ALG_CCMP)
			return ATH9K_KEY_TYPE_AES;
	}

	return ATH9K_KEY_TYPE_CLEAR;
}
EXPORT_SYMBOL(ath9k_cmn_get_hw_crypto_keytype);

static u32 ath9k_get_extchanmode(struct ieee80211_channel *chan,
				 enum nl80211_channel_type channel_type)
{
	u32 chanmode = 0;

	switch (chan->band) {
	case IEEE80211_BAND_2GHZ:
		switch (channel_type) {
		case NL80211_CHAN_NO_HT:
		case NL80211_CHAN_HT20:
			chanmode = CHANNEL_G_HT20;
			break;
		case NL80211_CHAN_HT40PLUS:
			chanmode = CHANNEL_G_HT40PLUS;
			break;
		case NL80211_CHAN_HT40MINUS:
			chanmode = CHANNEL_G_HT40MINUS;
			break;
		}
		break;
	case IEEE80211_BAND_5GHZ:
		switch (channel_type) {
		case NL80211_CHAN_NO_HT:
		case NL80211_CHAN_HT20:
			chanmode = CHANNEL_A_HT20;
			break;
		case NL80211_CHAN_HT40PLUS:
			chanmode = CHANNEL_A_HT40PLUS;
			break;
		case NL80211_CHAN_HT40MINUS:
			chanmode = CHANNEL_A_HT40MINUS;
			break;
		}
		break;
	default:
		break;
	}

	return chanmode;
}

/*
 * Update internal channel flags.
 */
void ath9k_cmn_update_ichannel(struct ieee80211_hw *hw,
			       struct ath9k_channel *ichan)
{
	struct ieee80211_channel *chan = hw->conf.channel;
	struct ieee80211_conf *conf = &hw->conf;

	ichan->channel = chan->center_freq;
	ichan->chan = chan;

	if (chan->band == IEEE80211_BAND_2GHZ) {
		ichan->chanmode = CHANNEL_G;
		ichan->channelFlags = CHANNEL_2GHZ | CHANNEL_OFDM | CHANNEL_G;
	} else {
		ichan->chanmode = CHANNEL_A;
		ichan->channelFlags = CHANNEL_5GHZ | CHANNEL_OFDM;
	}

	if (conf_is_ht(conf))
		ichan->chanmode = ath9k_get_extchanmode(chan,
							conf->channel_type);
}
EXPORT_SYMBOL(ath9k_cmn_update_ichannel);

/*
 * Get the internal channel reference.
 */
struct ath9k_channel *ath9k_cmn_get_curchannel(struct ieee80211_hw *hw,
					       struct ath_hw *ah)
{
	struct ieee80211_channel *curchan = hw->conf.channel;
	struct ath9k_channel *channel;
	u8 chan_idx;

	chan_idx = curchan->hw_value;
	channel = &ah->channels[chan_idx];
	ath9k_cmn_update_ichannel(hw, channel);

	return channel;
}
EXPORT_SYMBOL(ath9k_cmn_get_curchannel);

static int ath_setkey_tkip(struct ath_common *common, u16 keyix, const u8 *key,
			   struct ath9k_keyval *hk, const u8 *addr,
			   bool authenticator)
{
	struct ath_hw *ah = common->ah;
	const u8 *key_rxmic;
	const u8 *key_txmic;

	key_txmic = key + NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY;
	key_rxmic = key + NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY;

	if (addr == NULL) {
		/*
		 * Group key installation - only two key cache entries are used
		 * regardless of splitmic capability since group key is only
		 * used either for TX or RX.
		 */
		if (authenticator) {
			memcpy(hk->kv_mic, key_txmic, sizeof(hk->kv_mic));
			memcpy(hk->kv_txmic, key_txmic, sizeof(hk->kv_mic));
		} else {
			memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
			memcpy(hk->kv_txmic, key_rxmic, sizeof(hk->kv_mic));
		}
		return ath9k_hw_set_keycache_entry(ah, keyix, hk, addr);
	}
	if (!common->splitmic) {
		/* TX and RX keys share the same key cache entry. */
		memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
		memcpy(hk->kv_txmic, key_txmic, sizeof(hk->kv_txmic));
		return ath9k_hw_set_keycache_entry(ah, keyix, hk, addr);
	}

	/* Separate key cache entries for TX and RX */

	/* TX key goes at first index, RX key at +32. */
	memcpy(hk->kv_mic, key_txmic, sizeof(hk->kv_mic));
	if (!ath9k_hw_set_keycache_entry(ah, keyix, hk, NULL)) {
		/* TX MIC entry failed. No need to proceed further */
		ath_print(common, ATH_DBG_FATAL,
			  "Setting TX MIC Key Failed\n");
		return 0;
	}

	memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
	/* XXX delete tx key on failure? */
	return ath9k_hw_set_keycache_entry(ah, keyix + 32, hk, addr);
}

static int ath_reserve_key_cache_slot_tkip(struct ath_common *common)
{
	int i;

	for (i = IEEE80211_WEP_NKID; i < common->keymax / 2; i++) {
		if (test_bit(i, common->keymap) ||
		    test_bit(i + 64, common->keymap))
			continue; /* At least one part of TKIP key allocated */
		if (common->splitmic &&
		    (test_bit(i + 32, common->keymap) ||
		     test_bit(i + 64 + 32, common->keymap)))
			continue; /* At least one part of TKIP key allocated */

		/* Found a free slot for a TKIP key */
		return i;
	}
	return -1;
}

static int ath_reserve_key_cache_slot(struct ath_common *common)
{
	int i;

	/* First, try to find slots that would not be available for TKIP. */
	if (common->splitmic) {
		for (i = IEEE80211_WEP_NKID; i < common->keymax / 4; i++) {
			if (!test_bit(i, common->keymap) &&
			    (test_bit(i + 32, common->keymap) ||
			     test_bit(i + 64, common->keymap) ||
			     test_bit(i + 64 + 32, common->keymap)))
				return i;
			if (!test_bit(i + 32, common->keymap) &&
			    (test_bit(i, common->keymap) ||
			     test_bit(i + 64, common->keymap) ||
			     test_bit(i + 64 + 32, common->keymap)))
				return i + 32;
			if (!test_bit(i + 64, common->keymap) &&
			    (test_bit(i , common->keymap) ||
			     test_bit(i + 32, common->keymap) ||
			     test_bit(i + 64 + 32, common->keymap)))
				return i + 64;
			if (!test_bit(i + 64 + 32, common->keymap) &&
			    (test_bit(i, common->keymap) ||
			     test_bit(i + 32, common->keymap) ||
			     test_bit(i + 64, common->keymap)))
				return i + 64 + 32;
		}
	} else {
		for (i = IEEE80211_WEP_NKID; i < common->keymax / 2; i++) {
			if (!test_bit(i, common->keymap) &&
			    test_bit(i + 64, common->keymap))
				return i;
			if (test_bit(i, common->keymap) &&
			    !test_bit(i + 64, common->keymap))
				return i + 64;
		}
	}

	/* No partially used TKIP slots, pick any available slot */
	for (i = IEEE80211_WEP_NKID; i < common->keymax; i++) {
		/* Do not allow slots that could be needed for TKIP group keys
		 * to be used. This limitation could be removed if we know that
		 * TKIP will not be used. */
		if (i >= 64 && i < 64 + IEEE80211_WEP_NKID)
			continue;
		if (common->splitmic) {
			if (i >= 32 && i < 32 + IEEE80211_WEP_NKID)
				continue;
			if (i >= 64 + 32 && i < 64 + 32 + IEEE80211_WEP_NKID)
				continue;
		}

		if (!test_bit(i, common->keymap))
			return i; /* Found a free slot for a key */
	}

	/* No free slot found */
	return -1;
}

/*
 * Configure encryption in the HW.
 */
int ath9k_cmn_key_config(struct ath_common *common,
			 struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta,
			 struct ieee80211_key_conf *key)
{
	struct ath_hw *ah = common->ah;
	struct ath9k_keyval hk;
	const u8 *mac = NULL;
	int ret = 0;
	int idx;

	memset(&hk, 0, sizeof(hk));

	switch (key->alg) {
	case ALG_WEP:
		hk.kv_type = ATH9K_CIPHER_WEP;
		break;
	case ALG_TKIP:
		hk.kv_type = ATH9K_CIPHER_TKIP;
		break;
	case ALG_CCMP:
		hk.kv_type = ATH9K_CIPHER_AES_CCM;
		break;
	default:
		return -EOPNOTSUPP;
	}

	hk.kv_len = key->keylen;
	memcpy(hk.kv_val, key->key, key->keylen);

	if (!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
		/* For now, use the default keys for broadcast keys. This may
		 * need to change with virtual interfaces. */
		idx = key->keyidx;
	} else if (key->keyidx) {
		if (WARN_ON(!sta))
			return -EOPNOTSUPP;
		mac = sta->addr;

		if (vif->type != NL80211_IFTYPE_AP) {
			/* Only keyidx 0 should be used with unicast key, but
			 * allow this for client mode for now. */
			idx = key->keyidx;
		} else
			return -EIO;
	} else {
		if (WARN_ON(!sta))
			return -EOPNOTSUPP;
		mac = sta->addr;

		if (key->alg == ALG_TKIP)
			idx = ath_reserve_key_cache_slot_tkip(common);
		else
			idx = ath_reserve_key_cache_slot(common);
		if (idx < 0)
			return -ENOSPC; /* no free key cache entries */
	}

	if (key->alg == ALG_TKIP)
		ret = ath_setkey_tkip(common, idx, key->key, &hk, mac,
				      vif->type == NL80211_IFTYPE_AP);
	else
		ret = ath9k_hw_set_keycache_entry(ah, idx, &hk, mac);

	if (!ret)
		return -EIO;

	set_bit(idx, common->keymap);
	if (key->alg == ALG_TKIP) {
		set_bit(idx + 64, common->keymap);
		if (common->splitmic) {
			set_bit(idx + 32, common->keymap);
			set_bit(idx + 64 + 32, common->keymap);
		}
	}

	return idx;
}
EXPORT_SYMBOL(ath9k_cmn_key_config);

/*
 * Delete Key.
 */
void ath9k_cmn_key_delete(struct ath_common *common,
			  struct ieee80211_key_conf *key)
{
	struct ath_hw *ah = common->ah;

	ath9k_hw_keyreset(ah, key->hw_key_idx);
	if (key->hw_key_idx < IEEE80211_WEP_NKID)
		return;

	clear_bit(key->hw_key_idx, common->keymap);
	if (key->alg != ALG_TKIP)
		return;

	clear_bit(key->hw_key_idx + 64, common->keymap);
	if (common->splitmic) {
		ath9k_hw_keyreset(ah, key->hw_key_idx + 32);
		clear_bit(key->hw_key_idx + 32, common->keymap);
		clear_bit(key->hw_key_idx + 64 + 32, common->keymap);
	}
}
EXPORT_SYMBOL(ath9k_cmn_key_delete);

static int __init ath9k_cmn_init(void)
{
	return 0;
}
module_init(ath9k_cmn_init);

static void __exit ath9k_cmn_exit(void)
{
	return;
}
module_exit(ath9k_cmn_exit);
