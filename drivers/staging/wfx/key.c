// SPDX-License-Identifier: GPL-2.0-only
/*
 * Key management related functions.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <net/mac80211.h>

#include "key.h"
#include "wfx.h"
#include "hif_tx_mib.h"

static int wfx_alloc_key(struct wfx_dev *wdev)
{
	int idx;

	idx = ffs(~wdev->key_map) - 1;
	if (idx < 0 || idx >= MAX_KEY_ENTRIES)
		return -1;

	wdev->key_map |= BIT(idx);
	wdev->keys[idx].entry_index = idx;
	return idx;
}

static void wfx_free_key(struct wfx_dev *wdev, int idx)
{
	WARN(!(wdev->key_map & BIT(idx)), "inconsistent key allocation");
	memset(&wdev->keys[idx], 0, sizeof(wdev->keys[idx]));
	wdev->key_map &= ~BIT(idx);
}

static uint8_t fill_wep_pair(struct hif_wep_pairwise_key *msg,
			     struct ieee80211_key_conf *key, u8 *peer_addr)
{
	WARN(key->keylen > sizeof(msg->key_data), "inconsistent data");
	msg->key_length = key->keylen;
	memcpy(msg->key_data, key->key, key->keylen);
	ether_addr_copy(msg->peer_address, peer_addr);
	return HIF_KEY_TYPE_WEP_PAIRWISE;
}

static uint8_t fill_wep_group(struct hif_wep_group_key *msg,
			      struct ieee80211_key_conf *key)
{
	WARN(key->keylen > sizeof(msg->key_data), "inconsistent data");
	msg->key_id = key->keyidx;
	msg->key_length = key->keylen;
	memcpy(msg->key_data, key->key, key->keylen);
	return HIF_KEY_TYPE_WEP_DEFAULT;
}

static uint8_t fill_tkip_pair(struct hif_tkip_pairwise_key *msg,
			      struct ieee80211_key_conf *key, u8 *peer_addr)
{
	uint8_t *keybuf = key->key;

	WARN(key->keylen != sizeof(msg->tkip_key_data)
			    + sizeof(msg->tx_mic_key)
			    + sizeof(msg->rx_mic_key), "inconsistent data");
	memcpy(msg->tkip_key_data, keybuf, sizeof(msg->tkip_key_data));
	keybuf += sizeof(msg->tkip_key_data);
	memcpy(msg->tx_mic_key, keybuf, sizeof(msg->tx_mic_key));
	keybuf += sizeof(msg->tx_mic_key);
	memcpy(msg->rx_mic_key, keybuf, sizeof(msg->rx_mic_key));
	ether_addr_copy(msg->peer_address, peer_addr);
	return HIF_KEY_TYPE_TKIP_PAIRWISE;
}

static uint8_t fill_tkip_group(struct hif_tkip_group_key *msg,
			       struct ieee80211_key_conf *key,
			       struct ieee80211_key_seq *seq,
			       enum nl80211_iftype iftype)
{
	uint8_t *keybuf = key->key;

	WARN(key->keylen != sizeof(msg->tkip_key_data)
			    + 2 * sizeof(msg->rx_mic_key), "inconsistent data");
	msg->key_id = key->keyidx;
	memcpy(msg->rx_sequence_counter, &seq->tkip.iv16, sizeof(seq->tkip.iv16));
	memcpy(msg->rx_sequence_counter + sizeof(uint16_t), &seq->tkip.iv32, sizeof(seq->tkip.iv32));
	memcpy(msg->tkip_key_data, keybuf, sizeof(msg->tkip_key_data));
	keybuf += sizeof(msg->tkip_key_data);
	if (iftype == NL80211_IFTYPE_AP)
		// Use Tx MIC Key
		memcpy(msg->rx_mic_key, keybuf + 0, sizeof(msg->rx_mic_key));
	else
		// Use Rx MIC Key
		memcpy(msg->rx_mic_key, keybuf + 8, sizeof(msg->rx_mic_key));
	return HIF_KEY_TYPE_TKIP_GROUP;
}

static uint8_t fill_ccmp_pair(struct hif_aes_pairwise_key *msg,
			      struct ieee80211_key_conf *key, u8 *peer_addr)
{
	WARN(key->keylen != sizeof(msg->aes_key_data), "inconsistent data");
	ether_addr_copy(msg->peer_address, peer_addr);
	memcpy(msg->aes_key_data, key->key, key->keylen);
	return HIF_KEY_TYPE_AES_PAIRWISE;
}

static uint8_t fill_ccmp_group(struct hif_aes_group_key *msg,
			       struct ieee80211_key_conf *key,
			       struct ieee80211_key_seq *seq)
{
	WARN(key->keylen != sizeof(msg->aes_key_data), "inconsistent data");
	memcpy(msg->aes_key_data, key->key, key->keylen);
	memcpy(msg->rx_sequence_counter, seq->ccmp.pn, sizeof(seq->ccmp.pn));
	memreverse(msg->rx_sequence_counter, sizeof(seq->ccmp.pn));
	msg->key_id = key->keyidx;
	return HIF_KEY_TYPE_AES_GROUP;
}

static uint8_t fill_sms4_pair(struct hif_wapi_pairwise_key *msg,
			      struct ieee80211_key_conf *key, u8 *peer_addr)
{
	uint8_t *keybuf = key->key;

	WARN(key->keylen != sizeof(msg->wapi_key_data)
			    + sizeof(msg->mic_key_data), "inconsistent data");
	ether_addr_copy(msg->peer_address, peer_addr);
	memcpy(msg->wapi_key_data, keybuf, sizeof(msg->wapi_key_data));
	keybuf += sizeof(msg->wapi_key_data);
	memcpy(msg->mic_key_data, keybuf, sizeof(msg->mic_key_data));
	msg->key_id = key->keyidx;
	return HIF_KEY_TYPE_WAPI_PAIRWISE;
}

static uint8_t fill_sms4_group(struct hif_wapi_group_key *msg,
			       struct ieee80211_key_conf *key)
{
	uint8_t *keybuf = key->key;

	WARN(key->keylen != sizeof(msg->wapi_key_data)
			    + sizeof(msg->mic_key_data), "inconsistent data");
	memcpy(msg->wapi_key_data, keybuf, sizeof(msg->wapi_key_data));
	keybuf += sizeof(msg->wapi_key_data);
	memcpy(msg->mic_key_data, keybuf, sizeof(msg->mic_key_data));
	msg->key_id = key->keyidx;
	return HIF_KEY_TYPE_WAPI_GROUP;
}

static uint8_t fill_aes_cmac_group(struct hif_igtk_group_key *msg,
				   struct ieee80211_key_conf *key,
				   struct ieee80211_key_seq *seq)
{
	WARN(key->keylen != sizeof(msg->igtk_key_data), "inconsistent data");
	memcpy(msg->igtk_key_data, key->key, key->keylen);
	memcpy(msg->ipn, seq->aes_cmac.pn, sizeof(seq->aes_cmac.pn));
	memreverse(msg->ipn, sizeof(seq->aes_cmac.pn));
	msg->key_id = key->keyidx;
	return HIF_KEY_TYPE_IGTK_GROUP;
}

static int wfx_add_key(struct wfx_vif *wvif, struct ieee80211_sta *sta,
		       struct ieee80211_key_conf *key)
{
	int ret;
	struct hif_req_add_key *k;
	struct ieee80211_key_seq seq;
	struct wfx_dev *wdev = wvif->wdev;
	int idx = wfx_alloc_key(wvif->wdev);
	bool pairwise = key->flags & IEEE80211_KEY_FLAG_PAIRWISE;

	WARN(key->flags & IEEE80211_KEY_FLAG_PAIRWISE && !sta, "inconsistent data");
	ieee80211_get_key_rx_seq(key, 0, &seq);
	if (idx < 0)
		return -EINVAL;
	k = &wdev->keys[idx];
	k->int_id = wvif->id;
	if (key->cipher == WLAN_CIPHER_SUITE_WEP40 || key->cipher ==  WLAN_CIPHER_SUITE_WEP104) {
		if (pairwise)
			k->type = fill_wep_pair(&k->key.wep_pairwise_key, key, sta->addr);
		else
			k->type = fill_wep_group(&k->key.wep_group_key, key);
	} else if (key->cipher == WLAN_CIPHER_SUITE_TKIP) {
		if (pairwise)
			k->type = fill_tkip_pair(&k->key.tkip_pairwise_key, key, sta->addr);
		else
			k->type = fill_tkip_group(&k->key.tkip_group_key, key, &seq, wvif->vif->type);
	} else if (key->cipher == WLAN_CIPHER_SUITE_CCMP) {
		if (pairwise)
			k->type = fill_ccmp_pair(&k->key.aes_pairwise_key, key, sta->addr);
		else
			k->type = fill_ccmp_group(&k->key.aes_group_key, key, &seq);
	} else if (key->cipher ==  WLAN_CIPHER_SUITE_SMS4) {
		if (pairwise)
			k->type = fill_sms4_pair(&k->key.wapi_pairwise_key, key, sta->addr);
		else
			k->type = fill_sms4_group(&k->key.wapi_group_key, key);
	} else if (key->cipher ==  WLAN_CIPHER_SUITE_AES_CMAC) {
		k->type = fill_aes_cmac_group(&k->key.igtk_group_key, key, &seq);
	} else {
		dev_warn(wdev->dev, "unsupported key type %d\n", key->cipher);
		wfx_free_key(wdev, idx);
		return -EOPNOTSUPP;
	}
	ret = hif_add_key(wdev, k);
	if (ret) {
		wfx_free_key(wdev, idx);
		return -EOPNOTSUPP;
	}
	key->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE |
		      IEEE80211_KEY_FLAG_RESERVE_TAILROOM;
	key->hw_key_idx = idx;
	return 0;
}

static int wfx_remove_key(struct wfx_vif *wvif, struct ieee80211_key_conf *key)
{
	WARN(key->hw_key_idx >= MAX_KEY_ENTRIES, "corrupted hw_key_idx");
	wfx_free_key(wvif->wdev, key->hw_key_idx);
	return hif_remove_key(wvif->wdev, key->hw_key_idx);
}

int wfx_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		struct ieee80211_key_conf *key)
{
	int ret = -EOPNOTSUPP;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;

	mutex_lock(&wvif->wdev->conf_mutex);
	if (cmd == SET_KEY)
		ret = wfx_add_key(wvif, sta, key);
	if (cmd == DISABLE_KEY)
		ret = wfx_remove_key(wvif, key);
	mutex_unlock(&wvif->wdev->conf_mutex);
	return ret;
}

int wfx_upload_keys(struct wfx_vif *wvif)
{
	int i;
	struct hif_req_add_key *key;
	struct wfx_dev *wdev = wvif->wdev;

	for (i = 0; i < ARRAY_SIZE(wdev->keys); i++) {
		if (wdev->key_map & BIT(i)) {
			key = &wdev->keys[i];
			if (key->int_id == wvif->id)
				hif_add_key(wdev, key);
		}
	}
	return 0;
}

void wfx_wep_key_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, wep_key_work);

	wfx_tx_flush(wvif->wdev);
	hif_wep_default_key_id(wvif, wvif->wep_default_key_id);
	wfx_pending_requeue(wvif->wdev, wvif->wep_pending_skb);
	wvif->wep_pending_skb = NULL;
	wfx_tx_unlock(wvif->wdev);
}
