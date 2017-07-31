/*
 * Copyright (c) 2009-2011 Atheros Communications Inc.
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

/* Assumes you've already done the endian to CPU conversion */
bool ath9k_cmn_rx_accept(struct ath_common *common,
			 struct ieee80211_hdr *hdr,
			 struct ieee80211_rx_status *rxs,
			 struct ath_rx_status *rx_stats,
			 bool *decrypt_error,
			 unsigned int rxfilter)
{
	struct ath_hw *ah = common->ah;
	bool is_mc, is_valid_tkip, strip_mic, mic_error;
	__le16 fc;

	fc = hdr->frame_control;

	is_mc = !!is_multicast_ether_addr(hdr->addr1);
	is_valid_tkip = rx_stats->rs_keyix != ATH9K_RXKEYIX_INVALID &&
		test_bit(rx_stats->rs_keyix, common->tkip_keymap);
	strip_mic = is_valid_tkip && ieee80211_is_data(fc) &&
		ieee80211_has_protected(fc) &&
		!(rx_stats->rs_status &
		(ATH9K_RXERR_DECRYPT | ATH9K_RXERR_CRC | ATH9K_RXERR_MIC |
		 ATH9K_RXERR_KEYMISS));

	/*
	 * Key miss events are only relevant for pairwise keys where the
	 * descriptor does contain a valid key index. This has been observed
	 * mostly with CCMP encryption.
	 */
	if (rx_stats->rs_keyix == ATH9K_RXKEYIX_INVALID ||
	    !test_bit(rx_stats->rs_keyix, common->ccmp_keymap))
		rx_stats->rs_status &= ~ATH9K_RXERR_KEYMISS;

	mic_error = is_valid_tkip && !ieee80211_is_ctl(fc) &&
		!ieee80211_has_morefrags(fc) &&
		!(le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG) &&
		(rx_stats->rs_status & ATH9K_RXERR_MIC);

	/*
	 * The rx_stats->rs_status will not be set until the end of the
	 * chained descriptors so it can be ignored if rs_more is set. The
	 * rs_more will be false at the last element of the chained
	 * descriptors.
	 */
	if (rx_stats->rs_status != 0) {
		u8 status_mask;

		if (rx_stats->rs_status & ATH9K_RXERR_CRC) {
			rxs->flag |= RX_FLAG_FAILED_FCS_CRC;
			mic_error = false;
		}

		if ((rx_stats->rs_status & ATH9K_RXERR_DECRYPT) ||
		    (!is_mc && (rx_stats->rs_status & ATH9K_RXERR_KEYMISS))) {
			*decrypt_error = true;
			mic_error = false;
		}


		/*
		 * Reject error frames with the exception of
		 * decryption and MIC failures. For monitor mode,
		 * we also ignore the CRC error.
		 */
		status_mask = ATH9K_RXERR_DECRYPT | ATH9K_RXERR_MIC |
			      ATH9K_RXERR_KEYMISS;

		if (ah->is_monitoring && (rxfilter & FIF_FCSFAIL))
			status_mask |= ATH9K_RXERR_CRC;

		if (rx_stats->rs_status & ~status_mask)
			return false;
	}

	/*
	 * For unicast frames the MIC error bit can have false positives,
	 * so all MIC error reports need to be validated in software.
	 * False negatives are not common, so skip software verification
	 * if the hardware considers the MIC valid.
	 */
	if (strip_mic)
		rxs->flag |= RX_FLAG_MMIC_STRIPPED;
	else if (is_mc && mic_error)
		rxs->flag |= RX_FLAG_MMIC_ERROR;

	return true;
}
EXPORT_SYMBOL(ath9k_cmn_rx_accept);

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
	padpos = ieee80211_hdrlen(fc);

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
	if (ah->sw_mgmt_crypto_rx &&
	    (rxs->flag & RX_FLAG_DECRYPTED) &&
	    ieee80211_is_mgmt(fc))
		/* Use software decrypt for management frames. */
		rxs->flag &= ~RX_FLAG_DECRYPTED;
}
EXPORT_SYMBOL(ath9k_cmn_rx_skb_postprocess);

int ath9k_cmn_process_rate(struct ath_common *common,
			   struct ieee80211_hw *hw,
			   struct ath_rx_status *rx_stats,
			   struct ieee80211_rx_status *rxs)
{
	struct ieee80211_supported_band *sband;
	enum nl80211_band band;
	unsigned int i = 0;
	struct ath_hw *ah = common->ah;

	band = ah->curchan->chan->band;
	sband = hw->wiphy->bands[band];

	if (IS_CHAN_QUARTER_RATE(ah->curchan))
		rxs->bw = RATE_INFO_BW_5;
	else if (IS_CHAN_HALF_RATE(ah->curchan))
		rxs->bw = RATE_INFO_BW_10;

	if (rx_stats->rs_rate & 0x80) {
		/* HT rate */
		rxs->encoding = RX_ENC_HT;
		rxs->enc_flags |= rx_stats->enc_flags;
		rxs->bw = rx_stats->bw;
		rxs->rate_idx = rx_stats->rs_rate & 0x7f;
		return 0;
	}

	for (i = 0; i < sband->n_bitrates; i++) {
		if (sband->bitrates[i].hw_value == rx_stats->rs_rate) {
			rxs->rate_idx = i;
			return 0;
		}
		if (sband->bitrates[i].hw_value_short == rx_stats->rs_rate) {
			rxs->enc_flags |= RX_ENC_FLAG_SHORTPRE;
			rxs->rate_idx = i;
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL(ath9k_cmn_process_rate);

void ath9k_cmn_process_rssi(struct ath_common *common,
			    struct ieee80211_hw *hw,
			    struct ath_rx_status *rx_stats,
			    struct ieee80211_rx_status *rxs)
{
	struct ath_hw *ah = common->ah;
	int last_rssi;
	int rssi = rx_stats->rs_rssi;
	int i, j;

	/*
	 * RSSI is not available for subframes in an A-MPDU.
	 */
	if (rx_stats->rs_moreaggr) {
		rxs->flag |= RX_FLAG_NO_SIGNAL_VAL;
		return;
	}

	/*
	 * Check if the RSSI for the last subframe in an A-MPDU
	 * or an unaggregated frame is valid.
	 */
	if (rx_stats->rs_rssi == ATH9K_RSSI_BAD) {
		rxs->flag |= RX_FLAG_NO_SIGNAL_VAL;
		return;
	}

	for (i = 0, j = 0; i < ARRAY_SIZE(rx_stats->rs_rssi_ctl); i++) {
		s8 rssi;

		if (!(ah->rxchainmask & BIT(i)))
			continue;

		rssi = rx_stats->rs_rssi_ctl[i];
		if (rssi != ATH9K_RSSI_BAD) {
		    rxs->chains |= BIT(j);
		    rxs->chain_signal[j] = ah->noise + rssi;
		}
		j++;
	}

	/*
	 * Update Beacon RSSI, this is used by ANI.
	 */
	if (rx_stats->is_mybeacon &&
	    ((ah->opmode == NL80211_IFTYPE_STATION) ||
	     (ah->opmode == NL80211_IFTYPE_ADHOC))) {
		ATH_RSSI_LPF(common->last_rssi, rx_stats->rs_rssi);
		last_rssi = common->last_rssi;

		if (likely(last_rssi != ATH_RSSI_DUMMY_MARKER))
			rssi = ATH_EP_RND(last_rssi, ATH_RSSI_EP_MULTIPLIER);
		if (rssi < 0)
			rssi = 0;

		ah->stats.avgbrssi = rssi;
	}

	rxs->signal = ah->noise + rx_stats->rs_rssi;
}
EXPORT_SYMBOL(ath9k_cmn_process_rssi);

int ath9k_cmn_get_hw_crypto_keytype(struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	if (tx_info->control.hw_key) {
		switch (tx_info->control.hw_key->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			return ATH9K_KEY_TYPE_WEP;
		case WLAN_CIPHER_SUITE_TKIP:
			return ATH9K_KEY_TYPE_TKIP;
		case WLAN_CIPHER_SUITE_CCMP:
			return ATH9K_KEY_TYPE_AES;
		default:
			break;
		}
	}

	return ATH9K_KEY_TYPE_CLEAR;
}
EXPORT_SYMBOL(ath9k_cmn_get_hw_crypto_keytype);

/*
 * Update internal channel flags.
 */
static void ath9k_cmn_update_ichannel(struct ath9k_channel *ichan,
				      struct cfg80211_chan_def *chandef)
{
	struct ieee80211_channel *chan = chandef->chan;
	u16 flags = 0;

	ichan->channel = chan->center_freq;
	ichan->chan = chan;

	if (chan->band == NL80211_BAND_5GHZ)
		flags |= CHANNEL_5GHZ;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_5:
		flags |= CHANNEL_QUARTER;
		break;
	case NL80211_CHAN_WIDTH_10:
		flags |= CHANNEL_HALF;
		break;
	case NL80211_CHAN_WIDTH_20_NOHT:
		break;
	case NL80211_CHAN_WIDTH_20:
		flags |= CHANNEL_HT;
		break;
	case NL80211_CHAN_WIDTH_40:
		if (chandef->center_freq1 > chandef->chan->center_freq)
			flags |= CHANNEL_HT40PLUS | CHANNEL_HT;
		else
			flags |= CHANNEL_HT40MINUS | CHANNEL_HT;
		break;
	default:
		WARN_ON(1);
	}

	ichan->channelFlags = flags;
}

/*
 * Get the internal channel reference.
 */
struct ath9k_channel *ath9k_cmn_get_channel(struct ieee80211_hw *hw,
					    struct ath_hw *ah,
					    struct cfg80211_chan_def *chandef)
{
	struct ieee80211_channel *curchan = chandef->chan;
	struct ath9k_channel *channel;

	channel = &ah->channels[curchan->hw_value];
	ath9k_cmn_update_ichannel(channel, chandef);

	return channel;
}
EXPORT_SYMBOL(ath9k_cmn_get_channel);

int ath9k_cmn_count_streams(unsigned int chainmask, int max)
{
	int streams = 0;

	do {
		if (++streams == max)
			break;
	} while ((chainmask = chainmask & (chainmask - 1)));

	return streams;
}
EXPORT_SYMBOL(ath9k_cmn_count_streams);

void ath9k_cmn_update_txpow(struct ath_hw *ah, u16 cur_txpow,
			    u16 new_txpow, u16 *txpower)
{
	struct ath_regulatory *reg = ath9k_hw_regulatory(ah);

	if (reg->power_limit != new_txpow)
		ath9k_hw_set_txpowerlimit(ah, new_txpow, false);

	/* read back in case value is clamped */
	*txpower = reg->max_power_level;
}
EXPORT_SYMBOL(ath9k_cmn_update_txpow);

void ath9k_cmn_init_crypto(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int i = 0;

	/* Get the hardware key cache size. */
	common->keymax = AR_KEYTABLE_SIZE;

	/*
	 * Check whether the separate key cache entries
	 * are required to handle both tx+rx MIC keys.
	 * With split mic keys the number of stations is limited
	 * to 27 otherwise 59.
	 */
	if (ah->misc_mode & AR_PCU_MIC_NEW_LOC_ENA)
		common->crypt_caps |= ATH_CRYPT_CAP_MIC_COMBINED;

	/*
	 * Reset the key cache since some parts do not
	 * reset the contents on initial power up.
	 */
	for (i = 0; i < common->keymax; i++)
		ath_hw_keyreset(common, (u16) i);
}
EXPORT_SYMBOL(ath9k_cmn_init_crypto);

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
