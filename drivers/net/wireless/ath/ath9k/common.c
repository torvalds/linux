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

static int ath_reserve_key_cache_slot(struct ath_common *common,
				      enum ieee80211_key_alg alg)
{
	int i;

	if (alg == ALG_TKIP)
		return ath_reserve_key_cache_slot_tkip(common);

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
	u8 gmac[ETH_ALEN];
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
		switch (vif->type) {
		case NL80211_IFTYPE_AP:
			memcpy(gmac, vif->addr, ETH_ALEN);
			gmac[0] |= 0x01;
			mac = gmac;
			idx = ath_reserve_key_cache_slot(common, key->alg);
			break;
		case NL80211_IFTYPE_ADHOC:
			if (!sta) {
				idx = key->keyidx;
				break;
			}
			memcpy(gmac, sta->addr, ETH_ALEN);
			gmac[0] |= 0x01;
			mac = gmac;
			idx = ath_reserve_key_cache_slot(common, key->alg);
			break;
		default:
			idx = key->keyidx;
			break;
		}
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

		idx = ath_reserve_key_cache_slot(common, key->alg);
	}

	if (idx < 0)
		return -ENOSPC; /* no free key cache entries */

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
		set_bit(idx, common->tkip_keymap);
		set_bit(idx + 64, common->tkip_keymap);
		if (common->splitmic) {
			set_bit(idx + 32, common->keymap);
			set_bit(idx + 64 + 32, common->keymap);
			set_bit(idx + 32, common->tkip_keymap);
			set_bit(idx + 64 + 32, common->tkip_keymap);
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

	clear_bit(key->hw_key_idx, common->tkip_keymap);
	clear_bit(key->hw_key_idx + 64, common->tkip_keymap);

	if (common->splitmic) {
		ath9k_hw_keyreset(ah, key->hw_key_idx + 32);
		clear_bit(key->hw_key_idx + 32, common->keymap);
		clear_bit(key->hw_key_idx + 64 + 32, common->keymap);

		clear_bit(key->hw_key_idx + 32, common->tkip_keymap);
		clear_bit(key->hw_key_idx + 64 + 32, common->tkip_keymap);
	}
}
EXPORT_SYMBOL(ath9k_cmn_key_delete);

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
