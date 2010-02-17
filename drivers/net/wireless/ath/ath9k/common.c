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
	 *
	 * The rx_stats->rs_status will not be set until the end of the
	 * chained descriptors so it can be ignored if rs_more is set. The
	 * rs_more will be false at the last element of the chained
	 * descriptors.
	 */
	if (!rx_stats->rs_more && rx_stats->rs_status != 0) {
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

static u8 ath9k_process_rate(struct ath_common *common,
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
		return rx_stats->rs_rate & 0x7f;
	}

	for (i = 0; i < sband->n_bitrates; i++) {
		if (sband->bitrates[i].hw_value == rx_stats->rs_rate)
			return i;
		if (sband->bitrates[i].hw_value_short == rx_stats->rs_rate) {
			rxs->flag |= RX_FLAG_SHORTPRE;
			return i;
		}
	}

	/* No valid hardware bitrate found -- we should not get here */
	ath_print(common, ATH_DBG_XMIT, "unsupported hw bitrate detected "
		  "0x%02x using 1 Mbit\n", rx_stats->rs_rate);
	if ((common->debug_mask & ATH_DBG_XMIT))
		print_hex_dump_bytes("", DUMP_PREFIX_NONE, skb->data, skb->len);

        return 0;
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
	if (!ath9k_rx_accept(common, skb, rx_status, rx_stats, decrypt_error))
		return -EINVAL;

	ath9k_process_rssi(common, hw, skb, rx_stats);

	rx_status->rate_idx = ath9k_process_rate(common, hw,
						 rx_stats, rx_status, skb);
	rx_status->mactime = ath9k_hw_extend_tsf(ah, rx_stats->rs_tstamp);
	rx_status->band = hw->conf.channel->band;
	rx_status->freq = hw->conf.channel->center_freq;
	rx_status->noise = common->ani.noise_floor;
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

	if (!(keyix == ATH9K_RXKEYIX_INVALID) && !decrypt_error) {
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
