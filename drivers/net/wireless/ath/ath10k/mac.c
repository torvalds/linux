/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

#include "mac.h"

#include <net/mac80211.h>
#include <linux/etherdevice.h>

#include "hif.h"
#include "core.h"
#include "debug.h"
#include "wmi.h"
#include "htt.h"
#include "txrx.h"
#include "testmode.h"

/**********/
/* Crypto */
/**********/

static int ath10k_send_key(struct ath10k_vif *arvif,
			   struct ieee80211_key_conf *key,
			   enum set_key_cmd cmd,
			   const u8 *macaddr)
{
	struct ath10k *ar = arvif->ar;
	struct wmi_vdev_install_key_arg arg = {
		.vdev_id = arvif->vdev_id,
		.key_idx = key->keyidx,
		.key_len = key->keylen,
		.key_data = key->key,
		.macaddr = macaddr,
	};

	lockdep_assert_held(&arvif->ar->conf_mutex);

	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
		arg.key_flags = WMI_KEY_PAIRWISE;
	else
		arg.key_flags = WMI_KEY_GROUP;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		arg.key_cipher = WMI_CIPHER_AES_CCM;
		if (arvif->vdev_type == WMI_VDEV_TYPE_AP)
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV_MGMT;
		else
			key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		arg.key_cipher = WMI_CIPHER_TKIP;
		arg.key_txmic_len = 8;
		arg.key_rxmic_len = 8;
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		arg.key_cipher = WMI_CIPHER_WEP;
		/* AP/IBSS mode requires self-key to be groupwise
		 * Otherwise pairwise key must be set */
		if (memcmp(macaddr, arvif->vif->addr, ETH_ALEN))
			arg.key_flags = WMI_KEY_PAIRWISE;
		break;
	default:
		ath10k_warn(ar, "cipher %d is not supported\n", key->cipher);
		return -EOPNOTSUPP;
	}

	if (cmd == DISABLE_KEY) {
		arg.key_cipher = WMI_CIPHER_NONE;
		arg.key_data = NULL;
	}

	return ath10k_wmi_vdev_install_key(arvif->ar, &arg);
}

static int ath10k_install_key(struct ath10k_vif *arvif,
			      struct ieee80211_key_conf *key,
			      enum set_key_cmd cmd,
			      const u8 *macaddr)
{
	struct ath10k *ar = arvif->ar;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	reinit_completion(&ar->install_key_done);

	ret = ath10k_send_key(arvif, key, cmd, macaddr);
	if (ret)
		return ret;

	ret = wait_for_completion_timeout(&ar->install_key_done, 3*HZ);
	if (ret == 0)
		return -ETIMEDOUT;

	return 0;
}

static int ath10k_install_peer_wep_keys(struct ath10k_vif *arvif,
					const u8 *addr)
{
	struct ath10k *ar = arvif->ar;
	struct ath10k_peer *peer;
	int ret;
	int i;

	lockdep_assert_held(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find(ar, arvif->vdev_id, addr);
	spin_unlock_bh(&ar->data_lock);

	if (!peer)
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(arvif->wep_keys); i++) {
		if (arvif->wep_keys[i] == NULL)
			continue;

		ret = ath10k_install_key(arvif, arvif->wep_keys[i], SET_KEY,
					 addr);
		if (ret)
			return ret;

		peer->keys[i] = arvif->wep_keys[i];
	}

	return 0;
}

static int ath10k_clear_peer_keys(struct ath10k_vif *arvif,
				  const u8 *addr)
{
	struct ath10k *ar = arvif->ar;
	struct ath10k_peer *peer;
	int first_errno = 0;
	int ret;
	int i;

	lockdep_assert_held(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find(ar, arvif->vdev_id, addr);
	spin_unlock_bh(&ar->data_lock);

	if (!peer)
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(peer->keys); i++) {
		if (peer->keys[i] == NULL)
			continue;

		ret = ath10k_install_key(arvif, peer->keys[i],
					 DISABLE_KEY, addr);
		if (ret && first_errno == 0)
			first_errno = ret;

		if (ret)
			ath10k_warn(ar, "failed to remove peer wep key %d: %d\n",
				    i, ret);

		peer->keys[i] = NULL;
	}

	return first_errno;
}

static int ath10k_clear_vdev_key(struct ath10k_vif *arvif,
				 struct ieee80211_key_conf *key)
{
	struct ath10k *ar = arvif->ar;
	struct ath10k_peer *peer;
	u8 addr[ETH_ALEN];
	int first_errno = 0;
	int ret;
	int i;

	lockdep_assert_held(&ar->conf_mutex);

	for (;;) {
		/* since ath10k_install_key we can't hold data_lock all the
		 * time, so we try to remove the keys incrementally */
		spin_lock_bh(&ar->data_lock);
		i = 0;
		list_for_each_entry(peer, &ar->peers, list) {
			for (i = 0; i < ARRAY_SIZE(peer->keys); i++) {
				if (peer->keys[i] == key) {
					ether_addr_copy(addr, peer->addr);
					peer->keys[i] = NULL;
					break;
				}
			}

			if (i < ARRAY_SIZE(peer->keys))
				break;
		}
		spin_unlock_bh(&ar->data_lock);

		if (i == ARRAY_SIZE(peer->keys))
			break;

		ret = ath10k_install_key(arvif, key, DISABLE_KEY, addr);
		if (ret && first_errno == 0)
			first_errno = ret;

		if (ret)
			ath10k_warn(ar, "failed to remove key for %pM: %d\n",
				    addr, ret);
	}

	return first_errno;
}

/*********************/
/* General utilities */
/*********************/

static inline enum wmi_phy_mode
chan_to_phymode(const struct cfg80211_chan_def *chandef)
{
	enum wmi_phy_mode phymode = MODE_UNKNOWN;

	switch (chandef->chan->band) {
	case IEEE80211_BAND_2GHZ:
		switch (chandef->width) {
		case NL80211_CHAN_WIDTH_20_NOHT:
			phymode = MODE_11G;
			break;
		case NL80211_CHAN_WIDTH_20:
			phymode = MODE_11NG_HT20;
			break;
		case NL80211_CHAN_WIDTH_40:
			phymode = MODE_11NG_HT40;
			break;
		case NL80211_CHAN_WIDTH_5:
		case NL80211_CHAN_WIDTH_10:
		case NL80211_CHAN_WIDTH_80:
		case NL80211_CHAN_WIDTH_80P80:
		case NL80211_CHAN_WIDTH_160:
			phymode = MODE_UNKNOWN;
			break;
		}
		break;
	case IEEE80211_BAND_5GHZ:
		switch (chandef->width) {
		case NL80211_CHAN_WIDTH_20_NOHT:
			phymode = MODE_11A;
			break;
		case NL80211_CHAN_WIDTH_20:
			phymode = MODE_11NA_HT20;
			break;
		case NL80211_CHAN_WIDTH_40:
			phymode = MODE_11NA_HT40;
			break;
		case NL80211_CHAN_WIDTH_80:
			phymode = MODE_11AC_VHT80;
			break;
		case NL80211_CHAN_WIDTH_5:
		case NL80211_CHAN_WIDTH_10:
		case NL80211_CHAN_WIDTH_80P80:
		case NL80211_CHAN_WIDTH_160:
			phymode = MODE_UNKNOWN;
			break;
		}
		break;
	default:
		break;
	}

	WARN_ON(phymode == MODE_UNKNOWN);
	return phymode;
}

static u8 ath10k_parse_mpdudensity(u8 mpdudensity)
{
/*
 * 802.11n D2.0 defined values for "Minimum MPDU Start Spacing":
 *   0 for no restriction
 *   1 for 1/4 us
 *   2 for 1/2 us
 *   3 for 1 us
 *   4 for 2 us
 *   5 for 4 us
 *   6 for 8 us
 *   7 for 16 us
 */
	switch (mpdudensity) {
	case 0:
		return 0;
	case 1:
	case 2:
	case 3:
	/* Our lower layer calculations limit our precision to
	   1 microsecond */
		return 1;
	case 4:
		return 2;
	case 5:
		return 4;
	case 6:
		return 8;
	case 7:
		return 16;
	default:
		return 0;
	}
}

static int ath10k_peer_create(struct ath10k *ar, u32 vdev_id, const u8 *addr)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_wmi_peer_create(ar, vdev_id, addr);
	if (ret) {
		ath10k_warn(ar, "failed to create wmi peer %pM on vdev %i: %i\n",
			    addr, vdev_id, ret);
		return ret;
	}

	ret = ath10k_wait_for_peer_created(ar, vdev_id, addr);
	if (ret) {
		ath10k_warn(ar, "failed to wait for created wmi peer %pM on vdev %i: %i\n",
			    addr, vdev_id, ret);
		return ret;
	}
	spin_lock_bh(&ar->data_lock);
	ar->num_peers++;
	spin_unlock_bh(&ar->data_lock);

	return 0;
}

static int ath10k_mac_set_kickout(struct ath10k_vif *arvif)
{
	struct ath10k *ar = arvif->ar;
	u32 param;
	int ret;

	param = ar->wmi.pdev_param->sta_kickout_th;
	ret = ath10k_wmi_pdev_set_param(ar, param,
					ATH10K_KICKOUT_THRESHOLD);
	if (ret) {
		ath10k_warn(ar, "failed to set kickout threshold on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	param = ar->wmi.vdev_param->ap_keepalive_min_idle_inactive_time_secs;
	ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, param,
					ATH10K_KEEPALIVE_MIN_IDLE);
	if (ret) {
		ath10k_warn(ar, "failed to set keepalive minimum idle time on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	param = ar->wmi.vdev_param->ap_keepalive_max_idle_inactive_time_secs;
	ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, param,
					ATH10K_KEEPALIVE_MAX_IDLE);
	if (ret) {
		ath10k_warn(ar, "failed to set keepalive maximum idle time on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	param = ar->wmi.vdev_param->ap_keepalive_max_unresponsive_time_secs;
	ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, param,
					ATH10K_KEEPALIVE_MAX_UNRESPONSIVE);
	if (ret) {
		ath10k_warn(ar, "failed to set keepalive maximum unresponsive time on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}

static int  ath10k_mac_set_rts(struct ath10k_vif *arvif, u32 value)
{
	struct ath10k *ar = arvif->ar;
	u32 vdev_param;

	if (value != 0xFFFFFFFF)
		value = min_t(u32, arvif->ar->hw->wiphy->rts_threshold,
			      ATH10K_RTS_MAX);

	vdev_param = ar->wmi.vdev_param->rts_threshold;
	return ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param, value);
}

static int ath10k_mac_set_frag(struct ath10k_vif *arvif, u32 value)
{
	struct ath10k *ar = arvif->ar;
	u32 vdev_param;

	if (value != 0xFFFFFFFF)
		value = clamp_t(u32, arvif->ar->hw->wiphy->frag_threshold,
				ATH10K_FRAGMT_THRESHOLD_MIN,
				ATH10K_FRAGMT_THRESHOLD_MAX);

	vdev_param = ar->wmi.vdev_param->fragmentation_threshold;
	return ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param, value);
}

static int ath10k_peer_delete(struct ath10k *ar, u32 vdev_id, const u8 *addr)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_wmi_peer_delete(ar, vdev_id, addr);
	if (ret)
		return ret;

	ret = ath10k_wait_for_peer_deleted(ar, vdev_id, addr);
	if (ret)
		return ret;

	spin_lock_bh(&ar->data_lock);
	ar->num_peers--;
	spin_unlock_bh(&ar->data_lock);

	return 0;
}

static void ath10k_peer_cleanup(struct ath10k *ar, u32 vdev_id)
{
	struct ath10k_peer *peer, *tmp;

	lockdep_assert_held(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	list_for_each_entry_safe(peer, tmp, &ar->peers, list) {
		if (peer->vdev_id != vdev_id)
			continue;

		ath10k_warn(ar, "removing stale peer %pM from vdev_id %d\n",
			    peer->addr, vdev_id);

		list_del(&peer->list);
		kfree(peer);
		ar->num_peers--;
	}
	spin_unlock_bh(&ar->data_lock);
}

static void ath10k_peer_cleanup_all(struct ath10k *ar)
{
	struct ath10k_peer *peer, *tmp;

	lockdep_assert_held(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	list_for_each_entry_safe(peer, tmp, &ar->peers, list) {
		list_del(&peer->list);
		kfree(peer);
	}
	ar->num_peers = 0;
	spin_unlock_bh(&ar->data_lock);
}

/************************/
/* Interface management */
/************************/

void ath10k_mac_vif_beacon_free(struct ath10k_vif *arvif)
{
	struct ath10k *ar = arvif->ar;

	lockdep_assert_held(&ar->data_lock);

	if (!arvif->beacon)
		return;

	if (!arvif->beacon_buf)
		dma_unmap_single(ar->dev, ATH10K_SKB_CB(arvif->beacon)->paddr,
				 arvif->beacon->len, DMA_TO_DEVICE);

	dev_kfree_skb_any(arvif->beacon);

	arvif->beacon = NULL;
	arvif->beacon_sent = false;
}

static void ath10k_mac_vif_beacon_cleanup(struct ath10k_vif *arvif)
{
	struct ath10k *ar = arvif->ar;

	lockdep_assert_held(&ar->data_lock);

	ath10k_mac_vif_beacon_free(arvif);

	if (arvif->beacon_buf) {
		dma_free_coherent(ar->dev, IEEE80211_MAX_FRAME_LEN,
				  arvif->beacon_buf, arvif->beacon_paddr);
		arvif->beacon_buf = NULL;
	}
}

static inline int ath10k_vdev_setup_sync(struct ath10k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = wait_for_completion_timeout(&ar->vdev_setup_done,
					  ATH10K_VDEV_SETUP_TIMEOUT_HZ);
	if (ret == 0)
		return -ETIMEDOUT;

	return 0;
}

static int ath10k_monitor_vdev_start(struct ath10k *ar, int vdev_id)
{
	struct cfg80211_chan_def *chandef = &ar->chandef;
	struct ieee80211_channel *channel = chandef->chan;
	struct wmi_vdev_start_request_arg arg = {};
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	arg.vdev_id = vdev_id;
	arg.channel.freq = channel->center_freq;
	arg.channel.band_center_freq1 = chandef->center_freq1;

	/* TODO setup this dynamically, what in case we
	   don't have any vifs? */
	arg.channel.mode = chan_to_phymode(chandef);
	arg.channel.chan_radar =
			!!(channel->flags & IEEE80211_CHAN_RADAR);

	arg.channel.min_power = 0;
	arg.channel.max_power = channel->max_power * 2;
	arg.channel.max_reg_power = channel->max_reg_power * 2;
	arg.channel.max_antenna_gain = channel->max_antenna_gain * 2;

	ret = ath10k_wmi_vdev_start(ar, &arg);
	if (ret) {
		ath10k_warn(ar, "failed to request monitor vdev %i start: %d\n",
			    vdev_id, ret);
		return ret;
	}

	ret = ath10k_vdev_setup_sync(ar);
	if (ret) {
		ath10k_warn(ar, "failed to synchronize setup for monitor vdev %i: %d\n",
			    vdev_id, ret);
		return ret;
	}

	ret = ath10k_wmi_vdev_up(ar, vdev_id, 0, ar->mac_addr);
	if (ret) {
		ath10k_warn(ar, "failed to put up monitor vdev %i: %d\n",
			    vdev_id, ret);
		goto vdev_stop;
	}

	ar->monitor_vdev_id = vdev_id;

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor vdev %i started\n",
		   ar->monitor_vdev_id);
	return 0;

vdev_stop:
	ret = ath10k_wmi_vdev_stop(ar, ar->monitor_vdev_id);
	if (ret)
		ath10k_warn(ar, "failed to stop monitor vdev %i after start failure: %d\n",
			    ar->monitor_vdev_id, ret);

	return ret;
}

static int ath10k_monitor_vdev_stop(struct ath10k *ar)
{
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_wmi_vdev_down(ar, ar->monitor_vdev_id);
	if (ret)
		ath10k_warn(ar, "failed to put down monitor vdev %i: %d\n",
			    ar->monitor_vdev_id, ret);

	ret = ath10k_wmi_vdev_stop(ar, ar->monitor_vdev_id);
	if (ret)
		ath10k_warn(ar, "failed to to request monitor vdev %i stop: %d\n",
			    ar->monitor_vdev_id, ret);

	ret = ath10k_vdev_setup_sync(ar);
	if (ret)
		ath10k_warn(ar, "failed to synchronise monitor vdev %i: %d\n",
			    ar->monitor_vdev_id, ret);

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor vdev %i stopped\n",
		   ar->monitor_vdev_id);
	return ret;
}

static int ath10k_monitor_vdev_create(struct ath10k *ar)
{
	int bit, ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	if (ar->free_vdev_map == 0) {
		ath10k_warn(ar, "failed to find free vdev id for monitor vdev\n");
		return -ENOMEM;
	}

	bit = ffs(ar->free_vdev_map);

	ar->monitor_vdev_id = bit - 1;

	ret = ath10k_wmi_vdev_create(ar, ar->monitor_vdev_id,
				     WMI_VDEV_TYPE_MONITOR,
				     0, ar->mac_addr);
	if (ret) {
		ath10k_warn(ar, "failed to request monitor vdev %i creation: %d\n",
			    ar->monitor_vdev_id, ret);
		return ret;
	}

	ar->free_vdev_map &= ~(1 << ar->monitor_vdev_id);
	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor vdev %d created\n",
		   ar->monitor_vdev_id);

	return 0;
}

static int ath10k_monitor_vdev_delete(struct ath10k *ar)
{
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_wmi_vdev_delete(ar, ar->monitor_vdev_id);
	if (ret) {
		ath10k_warn(ar, "failed to request wmi monitor vdev %i removal: %d\n",
			    ar->monitor_vdev_id, ret);
		return ret;
	}

	ar->free_vdev_map |= 1 << ar->monitor_vdev_id;

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor vdev %d deleted\n",
		   ar->monitor_vdev_id);
	return ret;
}

static int ath10k_monitor_start(struct ath10k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_monitor_vdev_create(ar);
	if (ret) {
		ath10k_warn(ar, "failed to create monitor vdev: %d\n", ret);
		return ret;
	}

	ret = ath10k_monitor_vdev_start(ar, ar->monitor_vdev_id);
	if (ret) {
		ath10k_warn(ar, "failed to start monitor vdev: %d\n", ret);
		ath10k_monitor_vdev_delete(ar);
		return ret;
	}

	ar->monitor_started = true;
	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor started\n");

	return 0;
}

static int ath10k_monitor_stop(struct ath10k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_monitor_vdev_stop(ar);
	if (ret) {
		ath10k_warn(ar, "failed to stop monitor vdev: %d\n", ret);
		return ret;
	}

	ret = ath10k_monitor_vdev_delete(ar);
	if (ret) {
		ath10k_warn(ar, "failed to delete monitor vdev: %d\n", ret);
		return ret;
	}

	ar->monitor_started = false;
	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor stopped\n");

	return 0;
}

static int ath10k_monitor_recalc(struct ath10k *ar)
{
	bool should_start;

	lockdep_assert_held(&ar->conf_mutex);

	should_start = ar->monitor ||
		       ar->filter_flags & FIF_PROMISC_IN_BSS ||
		       test_bit(ATH10K_CAC_RUNNING, &ar->dev_flags);

	ath10k_dbg(ar, ATH10K_DBG_MAC,
		   "mac monitor recalc started? %d should? %d\n",
		   ar->monitor_started, should_start);

	if (should_start == ar->monitor_started)
		return 0;

	if (should_start)
		return ath10k_monitor_start(ar);

	return ath10k_monitor_stop(ar);
}

static int ath10k_recalc_rtscts_prot(struct ath10k_vif *arvif)
{
	struct ath10k *ar = arvif->ar;
	u32 vdev_param, rts_cts = 0;

	lockdep_assert_held(&ar->conf_mutex);

	vdev_param = ar->wmi.vdev_param->enable_rtscts;

	if (arvif->use_cts_prot || arvif->num_legacy_stations > 0)
		rts_cts |= SM(WMI_RTSCTS_ENABLED, WMI_RTSCTS_SET);

	if (arvif->num_legacy_stations > 0)
		rts_cts |= SM(WMI_RTSCTS_ACROSS_SW_RETRIES,
			      WMI_RTSCTS_PROFILE);

	return ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
					 rts_cts);
}

static int ath10k_start_cac(struct ath10k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	set_bit(ATH10K_CAC_RUNNING, &ar->dev_flags);

	ret = ath10k_monitor_recalc(ar);
	if (ret) {
		ath10k_warn(ar, "failed to start monitor (cac): %d\n", ret);
		clear_bit(ATH10K_CAC_RUNNING, &ar->dev_flags);
		return ret;
	}

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac cac start monitor vdev %d\n",
		   ar->monitor_vdev_id);

	return 0;
}

static int ath10k_stop_cac(struct ath10k *ar)
{
	lockdep_assert_held(&ar->conf_mutex);

	/* CAC is not running - do nothing */
	if (!test_bit(ATH10K_CAC_RUNNING, &ar->dev_flags))
		return 0;

	clear_bit(ATH10K_CAC_RUNNING, &ar->dev_flags);
	ath10k_monitor_stop(ar);

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac cac finished\n");

	return 0;
}

static void ath10k_recalc_radar_detection(struct ath10k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ath10k_stop_cac(ar);

	if (!ar->radar_enabled)
		return;

	if (ar->num_started_vdevs > 0)
		return;

	ret = ath10k_start_cac(ar);
	if (ret) {
		/*
		 * Not possible to start CAC on current channel so starting
		 * radiation is not allowed, make this channel DFS_UNAVAILABLE
		 * by indicating that radar was detected.
		 */
		ath10k_warn(ar, "failed to start CAC: %d\n", ret);
		ieee80211_radar_detected(ar->hw);
	}
}

static int ath10k_vdev_start_restart(struct ath10k_vif *arvif, bool restart)
{
	struct ath10k *ar = arvif->ar;
	struct cfg80211_chan_def *chandef = &ar->chandef;
	struct wmi_vdev_start_request_arg arg = {};
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	reinit_completion(&ar->vdev_setup_done);

	arg.vdev_id = arvif->vdev_id;
	arg.dtim_period = arvif->dtim_period;
	arg.bcn_intval = arvif->beacon_interval;

	arg.channel.freq = chandef->chan->center_freq;
	arg.channel.band_center_freq1 = chandef->center_freq1;
	arg.channel.mode = chan_to_phymode(chandef);

	arg.channel.min_power = 0;
	arg.channel.max_power = chandef->chan->max_power * 2;
	arg.channel.max_reg_power = chandef->chan->max_reg_power * 2;
	arg.channel.max_antenna_gain = chandef->chan->max_antenna_gain * 2;

	if (arvif->vdev_type == WMI_VDEV_TYPE_AP) {
		arg.ssid = arvif->u.ap.ssid;
		arg.ssid_len = arvif->u.ap.ssid_len;
		arg.hidden_ssid = arvif->u.ap.hidden_ssid;

		/* For now allow DFS for AP mode */
		arg.channel.chan_radar =
			!!(chandef->chan->flags & IEEE80211_CHAN_RADAR);
	} else if (arvif->vdev_type == WMI_VDEV_TYPE_IBSS) {
		arg.ssid = arvif->vif->bss_conf.ssid;
		arg.ssid_len = arvif->vif->bss_conf.ssid_len;
	}

	ath10k_dbg(ar, ATH10K_DBG_MAC,
		   "mac vdev %d start center_freq %d phymode %s\n",
		   arg.vdev_id, arg.channel.freq,
		   ath10k_wmi_phymode_str(arg.channel.mode));

	if (restart)
		ret = ath10k_wmi_vdev_restart(ar, &arg);
	else
		ret = ath10k_wmi_vdev_start(ar, &arg);

	if (ret) {
		ath10k_warn(ar, "failed to start WMI vdev %i: %d\n",
			    arg.vdev_id, ret);
		return ret;
	}

	ret = ath10k_vdev_setup_sync(ar);
	if (ret) {
		ath10k_warn(ar, "failed to synchronise setup for vdev %i: %d\n",
			    arg.vdev_id, ret);
		return ret;
	}

	ar->num_started_vdevs++;
	ath10k_recalc_radar_detection(ar);

	return ret;
}

static int ath10k_vdev_start(struct ath10k_vif *arvif)
{
	return ath10k_vdev_start_restart(arvif, false);
}

static int ath10k_vdev_restart(struct ath10k_vif *arvif)
{
	return ath10k_vdev_start_restart(arvif, true);
}

static int ath10k_vdev_stop(struct ath10k_vif *arvif)
{
	struct ath10k *ar = arvif->ar;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	reinit_completion(&ar->vdev_setup_done);

	ret = ath10k_wmi_vdev_stop(ar, arvif->vdev_id);
	if (ret) {
		ath10k_warn(ar, "failed to stop WMI vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	ret = ath10k_vdev_setup_sync(ar);
	if (ret) {
		ath10k_warn(ar, "failed to syncronise setup for vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	WARN_ON(ar->num_started_vdevs == 0);

	if (ar->num_started_vdevs != 0) {
		ar->num_started_vdevs--;
		ath10k_recalc_radar_detection(ar);
	}

	return ret;
}

static void ath10k_control_beaconing(struct ath10k_vif *arvif,
				     struct ieee80211_bss_conf *info)
{
	struct ath10k *ar = arvif->ar;
	int ret = 0;

	lockdep_assert_held(&arvif->ar->conf_mutex);

	if (!info->enable_beacon) {
		ath10k_vdev_stop(arvif);

		arvif->is_started = false;
		arvif->is_up = false;

		spin_lock_bh(&arvif->ar->data_lock);
		ath10k_mac_vif_beacon_free(arvif);
		spin_unlock_bh(&arvif->ar->data_lock);

		return;
	}

	arvif->tx_seq_no = 0x1000;

	ret = ath10k_vdev_start(arvif);
	if (ret)
		return;

	arvif->aid = 0;
	ether_addr_copy(arvif->bssid, info->bssid);

	ret = ath10k_wmi_vdev_up(arvif->ar, arvif->vdev_id, arvif->aid,
				 arvif->bssid);
	if (ret) {
		ath10k_warn(ar, "failed to bring up vdev %d: %i\n",
			    arvif->vdev_id, ret);
		ath10k_vdev_stop(arvif);
		return;
	}

	arvif->is_started = true;
	arvif->is_up = true;

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d up\n", arvif->vdev_id);
}

static void ath10k_control_ibss(struct ath10k_vif *arvif,
				struct ieee80211_bss_conf *info,
				const u8 self_peer[ETH_ALEN])
{
	struct ath10k *ar = arvif->ar;
	u32 vdev_param;
	int ret = 0;

	lockdep_assert_held(&arvif->ar->conf_mutex);

	if (!info->ibss_joined) {
		ret = ath10k_peer_delete(arvif->ar, arvif->vdev_id, self_peer);
		if (ret)
			ath10k_warn(ar, "failed to delete IBSS self peer %pM for vdev %d: %d\n",
				    self_peer, arvif->vdev_id, ret);

		if (is_zero_ether_addr(arvif->bssid))
			return;

		ret = ath10k_peer_delete(arvif->ar, arvif->vdev_id,
					 arvif->bssid);
		if (ret) {
			ath10k_warn(ar, "failed to delete IBSS BSSID peer %pM for vdev %d: %d\n",
				    arvif->bssid, arvif->vdev_id, ret);
			return;
		}

		memset(arvif->bssid, 0, ETH_ALEN);

		return;
	}

	ret = ath10k_peer_create(arvif->ar, arvif->vdev_id, self_peer);
	if (ret) {
		ath10k_warn(ar, "failed to create IBSS self peer %pM for vdev %d: %d\n",
			    self_peer, arvif->vdev_id, ret);
		return;
	}

	vdev_param = arvif->ar->wmi.vdev_param->atim_window;
	ret = ath10k_wmi_vdev_set_param(arvif->ar, arvif->vdev_id, vdev_param,
					ATH10K_DEFAULT_ATIM);
	if (ret)
		ath10k_warn(ar, "failed to set IBSS ATIM for vdev %d: %d\n",
			    arvif->vdev_id, ret);
}

/*
 * Review this when mac80211 gains per-interface powersave support.
 */
static int ath10k_mac_vif_setup_ps(struct ath10k_vif *arvif)
{
	struct ath10k *ar = arvif->ar;
	struct ieee80211_conf *conf = &ar->hw->conf;
	enum wmi_sta_powersave_param param;
	enum wmi_sta_ps_mode psmode;
	int ret;

	lockdep_assert_held(&arvif->ar->conf_mutex);

	if (arvif->vif->type != NL80211_IFTYPE_STATION)
		return 0;

	if (conf->flags & IEEE80211_CONF_PS) {
		psmode = WMI_STA_PS_MODE_ENABLED;
		param = WMI_STA_PS_PARAM_INACTIVITY_TIME;

		ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id, param,
						  conf->dynamic_ps_timeout);
		if (ret) {
			ath10k_warn(ar, "failed to set inactivity time for vdev %d: %i\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	} else {
		psmode = WMI_STA_PS_MODE_DISABLED;
	}

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d psmode %s\n",
		   arvif->vdev_id, psmode ? "enable" : "disable");

	ret = ath10k_wmi_set_psmode(ar, arvif->vdev_id, psmode);
	if (ret) {
		ath10k_warn(ar, "failed to set PS Mode %d for vdev %d: %d\n",
			    psmode, arvif->vdev_id, ret);
		return ret;
	}

	return 0;
}

/**********************/
/* Station management */
/**********************/

static void ath10k_peer_assoc_h_basic(struct ath10k *ar,
				      struct ath10k_vif *arvif,
				      struct ieee80211_sta *sta,
				      struct ieee80211_bss_conf *bss_conf,
				      struct wmi_peer_assoc_complete_arg *arg)
{
	lockdep_assert_held(&ar->conf_mutex);

	ether_addr_copy(arg->addr, sta->addr);
	arg->vdev_id = arvif->vdev_id;
	arg->peer_aid = sta->aid;
	arg->peer_flags |= WMI_PEER_AUTH;

	if (arvif->vdev_type == WMI_VDEV_TYPE_STA)
		/*
		 * Seems FW have problems with Power Save in STA
		 * mode when we setup this parameter to high (eg. 5).
		 * Often we see that FW don't send NULL (with clean P flags)
		 * frame even there is info about buffered frames in beacons.
		 * Sometimes we have to wait more than 10 seconds before FW
		 * will wakeup. Often sending one ping from AP to our device
		 * just fail (more than 50%).
		 *
		 * Seems setting this FW parameter to 1 couse FW
		 * will check every beacon and will wakup immediately
		 * after detection buffered data.
		 */
		arg->peer_listen_intval = 1;
	else
		arg->peer_listen_intval = ar->hw->conf.listen_interval;

	arg->peer_num_spatial_streams = 1;

	/*
	 * The assoc capabilities are available only in managed mode.
	 */
	if (arvif->vdev_type == WMI_VDEV_TYPE_STA && bss_conf)
		arg->peer_caps = bss_conf->assoc_capability;
}

static void ath10k_peer_assoc_h_crypto(struct ath10k *ar,
				       struct ath10k_vif *arvif,
				       struct wmi_peer_assoc_complete_arg *arg)
{
	struct ieee80211_vif *vif = arvif->vif;
	struct ieee80211_bss_conf *info = &vif->bss_conf;
	struct cfg80211_bss *bss;
	const u8 *rsnie = NULL;
	const u8 *wpaie = NULL;

	lockdep_assert_held(&ar->conf_mutex);

	bss = cfg80211_get_bss(ar->hw->wiphy, ar->hw->conf.chandef.chan,
			       info->bssid, NULL, 0, 0, 0);
	if (bss) {
		const struct cfg80211_bss_ies *ies;

		rcu_read_lock();
		rsnie = ieee80211_bss_get_ie(bss, WLAN_EID_RSN);

		ies = rcu_dereference(bss->ies);

		wpaie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
						WLAN_OUI_TYPE_MICROSOFT_WPA,
						ies->data,
						ies->len);
		rcu_read_unlock();
		cfg80211_put_bss(ar->hw->wiphy, bss);
	}

	/* FIXME: base on RSN IE/WPA IE is a correct idea? */
	if (rsnie || wpaie) {
		ath10k_dbg(ar, ATH10K_DBG_WMI, "%s: rsn ie found\n", __func__);
		arg->peer_flags |= WMI_PEER_NEED_PTK_4_WAY;
	}

	if (wpaie) {
		ath10k_dbg(ar, ATH10K_DBG_WMI, "%s: wpa ie found\n", __func__);
		arg->peer_flags |= WMI_PEER_NEED_GTK_2_WAY;
	}
}

static void ath10k_peer_assoc_h_rates(struct ath10k *ar,
				      struct ieee80211_sta *sta,
				      struct wmi_peer_assoc_complete_arg *arg)
{
	struct wmi_rate_set_arg *rateset = &arg->peer_legacy_rates;
	const struct ieee80211_supported_band *sband;
	const struct ieee80211_rate *rates;
	u32 ratemask;
	int i;

	lockdep_assert_held(&ar->conf_mutex);

	sband = ar->hw->wiphy->bands[ar->hw->conf.chandef.chan->band];
	ratemask = sta->supp_rates[ar->hw->conf.chandef.chan->band];
	rates = sband->bitrates;

	rateset->num_rates = 0;

	for (i = 0; i < 32; i++, ratemask >>= 1, rates++) {
		if (!(ratemask & 1))
			continue;

		rateset->rates[rateset->num_rates] = rates->hw_value;
		rateset->num_rates++;
	}
}

static void ath10k_peer_assoc_h_ht(struct ath10k *ar,
				   struct ieee80211_sta *sta,
				   struct wmi_peer_assoc_complete_arg *arg)
{
	const struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
	int i, n;
	u32 stbc;

	lockdep_assert_held(&ar->conf_mutex);

	if (!ht_cap->ht_supported)
		return;

	arg->peer_flags |= WMI_PEER_HT;
	arg->peer_max_mpdu = (1 << (IEEE80211_HT_MAX_AMPDU_FACTOR +
				    ht_cap->ampdu_factor)) - 1;

	arg->peer_mpdu_density =
		ath10k_parse_mpdudensity(ht_cap->ampdu_density);

	arg->peer_ht_caps = ht_cap->cap;
	arg->peer_rate_caps |= WMI_RC_HT_FLAG;

	if (ht_cap->cap & IEEE80211_HT_CAP_LDPC_CODING)
		arg->peer_flags |= WMI_PEER_LDPC;

	if (sta->bandwidth >= IEEE80211_STA_RX_BW_40) {
		arg->peer_flags |= WMI_PEER_40MHZ;
		arg->peer_rate_caps |= WMI_RC_CW40_FLAG;
	}

	if (ht_cap->cap & IEEE80211_HT_CAP_SGI_20)
		arg->peer_rate_caps |= WMI_RC_SGI_FLAG;

	if (ht_cap->cap & IEEE80211_HT_CAP_SGI_40)
		arg->peer_rate_caps |= WMI_RC_SGI_FLAG;

	if (ht_cap->cap & IEEE80211_HT_CAP_TX_STBC) {
		arg->peer_rate_caps |= WMI_RC_TX_STBC_FLAG;
		arg->peer_flags |= WMI_PEER_STBC;
	}

	if (ht_cap->cap & IEEE80211_HT_CAP_RX_STBC) {
		stbc = ht_cap->cap & IEEE80211_HT_CAP_RX_STBC;
		stbc = stbc >> IEEE80211_HT_CAP_RX_STBC_SHIFT;
		stbc = stbc << WMI_RC_RX_STBC_FLAG_S;
		arg->peer_rate_caps |= stbc;
		arg->peer_flags |= WMI_PEER_STBC;
	}

	if (ht_cap->mcs.rx_mask[1] && ht_cap->mcs.rx_mask[2])
		arg->peer_rate_caps |= WMI_RC_TS_FLAG;
	else if (ht_cap->mcs.rx_mask[1])
		arg->peer_rate_caps |= WMI_RC_DS_FLAG;

	for (i = 0, n = 0; i < IEEE80211_HT_MCS_MASK_LEN*8; i++)
		if (ht_cap->mcs.rx_mask[i/8] & (1 << i%8))
			arg->peer_ht_rates.rates[n++] = i;

	/*
	 * This is a workaround for HT-enabled STAs which break the spec
	 * and have no HT capabilities RX mask (no HT RX MCS map).
	 *
	 * As per spec, in section 20.3.5 Modulation and coding scheme (MCS),
	 * MCS 0 through 7 are mandatory in 20MHz with 800 ns GI at all STAs.
	 *
	 * Firmware asserts if such situation occurs.
	 */
	if (n == 0) {
		arg->peer_ht_rates.num_rates = 8;
		for (i = 0; i < arg->peer_ht_rates.num_rates; i++)
			arg->peer_ht_rates.rates[i] = i;
	} else {
		arg->peer_ht_rates.num_rates = n;
		arg->peer_num_spatial_streams = sta->rx_nss;
	}

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac ht peer %pM mcs cnt %d nss %d\n",
		   arg->addr,
		   arg->peer_ht_rates.num_rates,
		   arg->peer_num_spatial_streams);
}

static int ath10k_peer_assoc_qos_ap(struct ath10k *ar,
				    struct ath10k_vif *arvif,
				    struct ieee80211_sta *sta)
{
	u32 uapsd = 0;
	u32 max_sp = 0;
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	if (sta->wme && sta->uapsd_queues) {
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac uapsd_queues 0x%x max_sp %d\n",
			   sta->uapsd_queues, sta->max_sp);

		if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)
			uapsd |= WMI_AP_PS_UAPSD_AC3_DELIVERY_EN |
				 WMI_AP_PS_UAPSD_AC3_TRIGGER_EN;
		if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VI)
			uapsd |= WMI_AP_PS_UAPSD_AC2_DELIVERY_EN |
				 WMI_AP_PS_UAPSD_AC2_TRIGGER_EN;
		if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BK)
			uapsd |= WMI_AP_PS_UAPSD_AC1_DELIVERY_EN |
				 WMI_AP_PS_UAPSD_AC1_TRIGGER_EN;
		if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BE)
			uapsd |= WMI_AP_PS_UAPSD_AC0_DELIVERY_EN |
				 WMI_AP_PS_UAPSD_AC0_TRIGGER_EN;

		if (sta->max_sp < MAX_WMI_AP_PS_PEER_PARAM_MAX_SP)
			max_sp = sta->max_sp;

		ret = ath10k_wmi_set_ap_ps_param(ar, arvif->vdev_id,
						 sta->addr,
						 WMI_AP_PS_PEER_PARAM_UAPSD,
						 uapsd);
		if (ret) {
			ath10k_warn(ar, "failed to set ap ps peer param uapsd for vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}

		ret = ath10k_wmi_set_ap_ps_param(ar, arvif->vdev_id,
						 sta->addr,
						 WMI_AP_PS_PEER_PARAM_MAX_SP,
						 max_sp);
		if (ret) {
			ath10k_warn(ar, "failed to set ap ps peer param max sp for vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}

		/* TODO setup this based on STA listen interval and
		   beacon interval. Currently we don't know
		   sta->listen_interval - mac80211 patch required.
		   Currently use 10 seconds */
		ret = ath10k_wmi_set_ap_ps_param(ar, arvif->vdev_id, sta->addr,
						 WMI_AP_PS_PEER_PARAM_AGEOUT_TIME,
						 10);
		if (ret) {
			ath10k_warn(ar, "failed to set ap ps peer param ageout time for vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static void ath10k_peer_assoc_h_vht(struct ath10k *ar,
				    struct ieee80211_sta *sta,
				    struct wmi_peer_assoc_complete_arg *arg)
{
	const struct ieee80211_sta_vht_cap *vht_cap = &sta->vht_cap;
	u8 ampdu_factor;

	if (!vht_cap->vht_supported)
		return;

	arg->peer_flags |= WMI_PEER_VHT;
	arg->peer_vht_caps = vht_cap->cap;

	ampdu_factor = (vht_cap->cap &
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK) >>
		       IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;

	/* Workaround: Some Netgear/Linksys 11ac APs set Rx A-MPDU factor to
	 * zero in VHT IE. Using it would result in degraded throughput.
	 * arg->peer_max_mpdu at this point contains HT max_mpdu so keep
	 * it if VHT max_mpdu is smaller. */
	arg->peer_max_mpdu = max(arg->peer_max_mpdu,
				 (1U << (IEEE80211_HT_MAX_AMPDU_FACTOR +
					ampdu_factor)) - 1);

	if (sta->bandwidth == IEEE80211_STA_RX_BW_80)
		arg->peer_flags |= WMI_PEER_80MHZ;

	arg->peer_vht_rates.rx_max_rate =
		__le16_to_cpu(vht_cap->vht_mcs.rx_highest);
	arg->peer_vht_rates.rx_mcs_set =
		__le16_to_cpu(vht_cap->vht_mcs.rx_mcs_map);
	arg->peer_vht_rates.tx_max_rate =
		__le16_to_cpu(vht_cap->vht_mcs.tx_highest);
	arg->peer_vht_rates.tx_mcs_set =
		__le16_to_cpu(vht_cap->vht_mcs.tx_mcs_map);

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vht peer %pM max_mpdu %d flags 0x%x\n",
		   sta->addr, arg->peer_max_mpdu, arg->peer_flags);
}

static void ath10k_peer_assoc_h_qos(struct ath10k *ar,
				    struct ath10k_vif *arvif,
				    struct ieee80211_sta *sta,
				    struct ieee80211_bss_conf *bss_conf,
				    struct wmi_peer_assoc_complete_arg *arg)
{
	switch (arvif->vdev_type) {
	case WMI_VDEV_TYPE_AP:
		if (sta->wme)
			arg->peer_flags |= WMI_PEER_QOS;

		if (sta->wme && sta->uapsd_queues) {
			arg->peer_flags |= WMI_PEER_APSD;
			arg->peer_rate_caps |= WMI_RC_UAPSD_FLAG;
		}
		break;
	case WMI_VDEV_TYPE_STA:
		if (bss_conf->qos)
			arg->peer_flags |= WMI_PEER_QOS;
		break;
	default:
		break;
	}
}

static void ath10k_peer_assoc_h_phymode(struct ath10k *ar,
					struct ath10k_vif *arvif,
					struct ieee80211_sta *sta,
					struct wmi_peer_assoc_complete_arg *arg)
{
	enum wmi_phy_mode phymode = MODE_UNKNOWN;

	switch (ar->hw->conf.chandef.chan->band) {
	case IEEE80211_BAND_2GHZ:
		if (sta->ht_cap.ht_supported) {
			if (sta->bandwidth == IEEE80211_STA_RX_BW_40)
				phymode = MODE_11NG_HT40;
			else
				phymode = MODE_11NG_HT20;
		} else {
			phymode = MODE_11G;
		}

		break;
	case IEEE80211_BAND_5GHZ:
		/*
		 * Check VHT first.
		 */
		if (sta->vht_cap.vht_supported) {
			if (sta->bandwidth == IEEE80211_STA_RX_BW_80)
				phymode = MODE_11AC_VHT80;
			else if (sta->bandwidth == IEEE80211_STA_RX_BW_40)
				phymode = MODE_11AC_VHT40;
			else if (sta->bandwidth == IEEE80211_STA_RX_BW_20)
				phymode = MODE_11AC_VHT20;
		} else if (sta->ht_cap.ht_supported) {
			if (sta->bandwidth == IEEE80211_STA_RX_BW_40)
				phymode = MODE_11NA_HT40;
			else
				phymode = MODE_11NA_HT20;
		} else {
			phymode = MODE_11A;
		}

		break;
	default:
		break;
	}

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac peer %pM phymode %s\n",
		   sta->addr, ath10k_wmi_phymode_str(phymode));

	arg->peer_phymode = phymode;
	WARN_ON(phymode == MODE_UNKNOWN);
}

static int ath10k_peer_assoc_prepare(struct ath10k *ar,
				     struct ath10k_vif *arvif,
				     struct ieee80211_sta *sta,
				     struct ieee80211_bss_conf *bss_conf,
				     struct wmi_peer_assoc_complete_arg *arg)
{
	lockdep_assert_held(&ar->conf_mutex);

	memset(arg, 0, sizeof(*arg));

	ath10k_peer_assoc_h_basic(ar, arvif, sta, bss_conf, arg);
	ath10k_peer_assoc_h_crypto(ar, arvif, arg);
	ath10k_peer_assoc_h_rates(ar, sta, arg);
	ath10k_peer_assoc_h_ht(ar, sta, arg);
	ath10k_peer_assoc_h_vht(ar, sta, arg);
	ath10k_peer_assoc_h_qos(ar, arvif, sta, bss_conf, arg);
	ath10k_peer_assoc_h_phymode(ar, arvif, sta, arg);

	return 0;
}

static const u32 ath10k_smps_map[] = {
	[WLAN_HT_CAP_SM_PS_STATIC] = WMI_PEER_SMPS_STATIC,
	[WLAN_HT_CAP_SM_PS_DYNAMIC] = WMI_PEER_SMPS_DYNAMIC,
	[WLAN_HT_CAP_SM_PS_INVALID] = WMI_PEER_SMPS_PS_NONE,
	[WLAN_HT_CAP_SM_PS_DISABLED] = WMI_PEER_SMPS_PS_NONE,
};

static int ath10k_setup_peer_smps(struct ath10k *ar, struct ath10k_vif *arvif,
				  const u8 *addr,
				  const struct ieee80211_sta_ht_cap *ht_cap)
{
	int smps;

	if (!ht_cap->ht_supported)
		return 0;

	smps = ht_cap->cap & IEEE80211_HT_CAP_SM_PS;
	smps >>= IEEE80211_HT_CAP_SM_PS_SHIFT;

	if (smps >= ARRAY_SIZE(ath10k_smps_map))
		return -EINVAL;

	return ath10k_wmi_peer_set_param(ar, arvif->vdev_id, addr,
					 WMI_PEER_SMPS_STATE,
					 ath10k_smps_map[smps]);
}

/* can be called only in mac80211 callbacks due to `key_count` usage */
static void ath10k_bss_assoc(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *bss_conf)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	struct ieee80211_sta_ht_cap ht_cap;
	struct wmi_peer_assoc_complete_arg peer_arg;
	struct ieee80211_sta *ap_sta;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	rcu_read_lock();

	ap_sta = ieee80211_find_sta(vif, bss_conf->bssid);
	if (!ap_sta) {
		ath10k_warn(ar, "failed to find station entry for bss %pM vdev %i\n",
			    bss_conf->bssid, arvif->vdev_id);
		rcu_read_unlock();
		return;
	}

	/* ap_sta must be accessed only within rcu section which must be left
	 * before calling ath10k_setup_peer_smps() which might sleep. */
	ht_cap = ap_sta->ht_cap;

	ret = ath10k_peer_assoc_prepare(ar, arvif, ap_sta,
					bss_conf, &peer_arg);
	if (ret) {
		ath10k_warn(ar, "failed to prepare peer assoc for %pM vdev %i: %d\n",
			    bss_conf->bssid, arvif->vdev_id, ret);
		rcu_read_unlock();
		return;
	}

	rcu_read_unlock();

	ret = ath10k_wmi_peer_assoc(ar, &peer_arg);
	if (ret) {
		ath10k_warn(ar, "failed to run peer assoc for %pM vdev %i: %d\n",
			    bss_conf->bssid, arvif->vdev_id, ret);
		return;
	}

	ret = ath10k_setup_peer_smps(ar, arvif, bss_conf->bssid, &ht_cap);
	if (ret) {
		ath10k_warn(ar, "failed to setup peer SMPS for vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return;
	}

	ath10k_dbg(ar, ATH10K_DBG_MAC,
		   "mac vdev %d up (associated) bssid %pM aid %d\n",
		   arvif->vdev_id, bss_conf->bssid, bss_conf->aid);

	arvif->aid = bss_conf->aid;
	ether_addr_copy(arvif->bssid, bss_conf->bssid);

	ret = ath10k_wmi_vdev_up(ar, arvif->vdev_id, arvif->aid, arvif->bssid);
	if (ret) {
		ath10k_warn(ar, "failed to set vdev %d up: %d\n",
			    arvif->vdev_id, ret);
		return;
	}

	arvif->is_up = true;
}

/*
 * FIXME: flush TIDs
 */
static void ath10k_bss_disassoc(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	/*
	 * For some reason, calling VDEV-DOWN before VDEV-STOP
	 * makes the FW to send frames via HTT after disassociation.
	 * No idea why this happens, even though VDEV-DOWN is supposed
	 * to be analogous to link down, so just stop the VDEV.
	 */
	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d stop (disassociated\n",
		   arvif->vdev_id);

	/* FIXME: check return value */
	ret = ath10k_vdev_stop(arvif);

	/*
	 * If we don't call VDEV-DOWN after VDEV-STOP FW will remain active and
	 * report beacons from previously associated network through HTT.
	 * This in turn would spam mac80211 WARN_ON if we bring down all
	 * interfaces as it expects there is no rx when no interface is
	 * running.
	 */
	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d down\n", arvif->vdev_id);

	/* FIXME: why don't we print error if wmi call fails? */
	ret = ath10k_wmi_vdev_down(ar, arvif->vdev_id);

	arvif->def_wep_key_idx = 0;

	arvif->is_started = false;
	arvif->is_up = false;
}

static int ath10k_station_assoc(struct ath10k *ar, struct ath10k_vif *arvif,
				struct ieee80211_sta *sta, bool reassoc)
{
	struct wmi_peer_assoc_complete_arg peer_arg;
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_peer_assoc_prepare(ar, arvif, sta, NULL, &peer_arg);
	if (ret) {
		ath10k_warn(ar, "failed to prepare WMI peer assoc for %pM vdev %i: %i\n",
			    sta->addr, arvif->vdev_id, ret);
		return ret;
	}

	peer_arg.peer_reassoc = reassoc;
	ret = ath10k_wmi_peer_assoc(ar, &peer_arg);
	if (ret) {
		ath10k_warn(ar, "failed to run peer assoc for STA %pM vdev %i: %d\n",
			    sta->addr, arvif->vdev_id, ret);
		return ret;
	}

	ret = ath10k_setup_peer_smps(ar, arvif, sta->addr, &sta->ht_cap);
	if (ret) {
		ath10k_warn(ar, "failed to setup peer SMPS for vdev %d: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	if (!sta->wme && !reassoc) {
		arvif->num_legacy_stations++;
		ret  = ath10k_recalc_rtscts_prot(arvif);
		if (ret) {
			ath10k_warn(ar, "failed to recalculate rts/cts prot for vdev %d: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	ret = ath10k_install_peer_wep_keys(arvif, sta->addr);
	if (ret) {
		ath10k_warn(ar, "failed to install peer wep keys for vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	ret = ath10k_peer_assoc_qos_ap(ar, arvif, sta);
	if (ret) {
		ath10k_warn(ar, "failed to set qos params for STA %pM for vdev %i: %d\n",
			    sta->addr, arvif->vdev_id, ret);
		return ret;
	}

	return ret;
}

static int ath10k_station_disassoc(struct ath10k *ar, struct ath10k_vif *arvif,
				   struct ieee80211_sta *sta)
{
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	if (!sta->wme) {
		arvif->num_legacy_stations--;
		ret = ath10k_recalc_rtscts_prot(arvif);
		if (ret) {
			ath10k_warn(ar, "failed to recalculate rts/cts prot for vdev %d: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	ret = ath10k_clear_peer_keys(arvif, sta->addr);
	if (ret) {
		ath10k_warn(ar, "failed to clear all peer wep keys for vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	}

	return ret;
}

/**************/
/* Regulatory */
/**************/

static int ath10k_update_channel_list(struct ath10k *ar)
{
	struct ieee80211_hw *hw = ar->hw;
	struct ieee80211_supported_band **bands;
	enum ieee80211_band band;
	struct ieee80211_channel *channel;
	struct wmi_scan_chan_list_arg arg = {0};
	struct wmi_channel_arg *ch;
	bool passive;
	int len;
	int ret;
	int i;

	lockdep_assert_held(&ar->conf_mutex);

	bands = hw->wiphy->bands;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (!bands[band])
			continue;

		for (i = 0; i < bands[band]->n_channels; i++) {
			if (bands[band]->channels[i].flags &
			    IEEE80211_CHAN_DISABLED)
				continue;

			arg.n_channels++;
		}
	}

	len = sizeof(struct wmi_channel_arg) * arg.n_channels;
	arg.channels = kzalloc(len, GFP_KERNEL);
	if (!arg.channels)
		return -ENOMEM;

	ch = arg.channels;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (!bands[band])
			continue;

		for (i = 0; i < bands[band]->n_channels; i++) {
			channel = &bands[band]->channels[i];

			if (channel->flags & IEEE80211_CHAN_DISABLED)
				continue;

			ch->allow_ht   = true;

			/* FIXME: when should we really allow VHT? */
			ch->allow_vht = true;

			ch->allow_ibss =
				!(channel->flags & IEEE80211_CHAN_NO_IR);

			ch->ht40plus =
				!(channel->flags & IEEE80211_CHAN_NO_HT40PLUS);

			ch->chan_radar =
				!!(channel->flags & IEEE80211_CHAN_RADAR);

			passive = channel->flags & IEEE80211_CHAN_NO_IR;
			ch->passive = passive;

			ch->freq = channel->center_freq;
			ch->min_power = 0;
			ch->max_power = channel->max_power * 2;
			ch->max_reg_power = channel->max_reg_power * 2;
			ch->max_antenna_gain = channel->max_antenna_gain * 2;
			ch->reg_class_id = 0; /* FIXME */

			/* FIXME: why use only legacy modes, why not any
			 * HT/VHT modes? Would that even make any
			 * difference? */
			if (channel->band == IEEE80211_BAND_2GHZ)
				ch->mode = MODE_11G;
			else
				ch->mode = MODE_11A;

			if (WARN_ON_ONCE(ch->mode == MODE_UNKNOWN))
				continue;

			ath10k_dbg(ar, ATH10K_DBG_WMI,
				   "mac channel [%zd/%d] freq %d maxpower %d regpower %d antenna %d mode %d\n",
				    ch - arg.channels, arg.n_channels,
				   ch->freq, ch->max_power, ch->max_reg_power,
				   ch->max_antenna_gain, ch->mode);

			ch++;
		}
	}

	ret = ath10k_wmi_scan_chan_list(ar, &arg);
	kfree(arg.channels);

	return ret;
}

static enum wmi_dfs_region
ath10k_mac_get_dfs_region(enum nl80211_dfs_regions dfs_region)
{
	switch (dfs_region) {
	case NL80211_DFS_UNSET:
		return WMI_UNINIT_DFS_DOMAIN;
	case NL80211_DFS_FCC:
		return WMI_FCC_DFS_DOMAIN;
	case NL80211_DFS_ETSI:
		return WMI_ETSI_DFS_DOMAIN;
	case NL80211_DFS_JP:
		return WMI_MKK4_DFS_DOMAIN;
	}
	return WMI_UNINIT_DFS_DOMAIN;
}

static void ath10k_regd_update(struct ath10k *ar)
{
	struct reg_dmn_pair_mapping *regpair;
	int ret;
	enum wmi_dfs_region wmi_dfs_reg;
	enum nl80211_dfs_regions nl_dfs_reg;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_update_channel_list(ar);
	if (ret)
		ath10k_warn(ar, "failed to update channel list: %d\n", ret);

	regpair = ar->ath_common.regulatory.regpair;

	if (config_enabled(CONFIG_ATH10K_DFS_CERTIFIED) && ar->dfs_detector) {
		nl_dfs_reg = ar->dfs_detector->region;
		wmi_dfs_reg = ath10k_mac_get_dfs_region(nl_dfs_reg);
	} else {
		wmi_dfs_reg = WMI_UNINIT_DFS_DOMAIN;
	}

	/* Target allows setting up per-band regdomain but ath_common provides
	 * a combined one only */
	ret = ath10k_wmi_pdev_set_regdomain(ar,
					    regpair->reg_domain,
					    regpair->reg_domain, /* 2ghz */
					    regpair->reg_domain, /* 5ghz */
					    regpair->reg_2ghz_ctl,
					    regpair->reg_5ghz_ctl,
					    wmi_dfs_reg);
	if (ret)
		ath10k_warn(ar, "failed to set pdev regdomain: %d\n", ret);
}

static void ath10k_reg_notifier(struct wiphy *wiphy,
				struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath10k *ar = hw->priv;
	bool result;

	ath_reg_notifier_apply(wiphy, request, &ar->ath_common.regulatory);

	if (config_enabled(CONFIG_ATH10K_DFS_CERTIFIED) && ar->dfs_detector) {
		ath10k_dbg(ar, ATH10K_DBG_REGULATORY, "dfs region 0x%x\n",
			   request->dfs_region);
		result = ar->dfs_detector->set_dfs_domain(ar->dfs_detector,
							  request->dfs_region);
		if (!result)
			ath10k_warn(ar, "DFS region 0x%X not supported, will trigger radar for every pulse\n",
				    request->dfs_region);
	}

	mutex_lock(&ar->conf_mutex);
	if (ar->state == ATH10K_STATE_ON)
		ath10k_regd_update(ar);
	mutex_unlock(&ar->conf_mutex);
}

/***************/
/* TX handlers */
/***************/

static u8 ath10k_tx_h_get_tid(struct ieee80211_hdr *hdr)
{
	if (ieee80211_is_mgmt(hdr->frame_control))
		return HTT_DATA_TX_EXT_TID_MGMT;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return HTT_DATA_TX_EXT_TID_NON_QOS_MCAST_BCAST;

	if (!is_unicast_ether_addr(ieee80211_get_DA(hdr)))
		return HTT_DATA_TX_EXT_TID_NON_QOS_MCAST_BCAST;

	return ieee80211_get_qos_ctl(hdr)[0] & IEEE80211_QOS_CTL_TID_MASK;
}

static u8 ath10k_tx_h_get_vdev_id(struct ath10k *ar, struct ieee80211_vif *vif)
{
	if (vif)
		return ath10k_vif_to_arvif(vif)->vdev_id;

	if (ar->monitor_started)
		return ar->monitor_vdev_id;

	ath10k_warn(ar, "failed to resolve vdev id\n");
	return 0;
}

/* HTT Tx uses Native Wifi tx mode which expects 802.11 frames without QoS
 * Control in the header.
 */
static void ath10k_tx_h_nwifi(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ath10k_skb_cb *cb = ATH10K_SKB_CB(skb);
	u8 *qos_ctl;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return;

	qos_ctl = ieee80211_get_qos_ctl(hdr);
	memmove(skb->data + IEEE80211_QOS_CTL_LEN,
		skb->data, (void *)qos_ctl - (void *)skb->data);
	skb_pull(skb, IEEE80211_QOS_CTL_LEN);

	/* Fw/Hw generates a corrupted QoS Control Field for QoS NullFunc
	 * frames. Powersave is handled by the fw/hw so QoS NyllFunc frames are
	 * used only for CQM purposes (e.g. hostapd station keepalive ping) so
	 * it is safe to downgrade to NullFunc.
	 */
	if (ieee80211_is_qos_nullfunc(hdr->frame_control)) {
		hdr->frame_control &= ~__cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
		cb->htt.tid = HTT_DATA_TX_EXT_TID_NON_QOS_MCAST_BCAST;
	}
}

static void ath10k_tx_wep_key_work(struct work_struct *work)
{
	struct ath10k_vif *arvif = container_of(work, struct ath10k_vif,
						wep_key_work);
	struct ath10k *ar = arvif->ar;
	int ret, keyidx = arvif->def_wep_key_newidx;

	mutex_lock(&arvif->ar->conf_mutex);

	if (arvif->ar->state != ATH10K_STATE_ON)
		goto unlock;

	if (arvif->def_wep_key_idx == keyidx)
		goto unlock;

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d set keyidx %d\n",
		   arvif->vdev_id, keyidx);

	ret = ath10k_wmi_vdev_set_param(arvif->ar,
					arvif->vdev_id,
					arvif->ar->wmi.vdev_param->def_keyid,
					keyidx);
	if (ret) {
		ath10k_warn(ar, "failed to update wep key index for vdev %d: %d\n",
			    arvif->vdev_id,
			    ret);
		goto unlock;
	}

	arvif->def_wep_key_idx = keyidx;

unlock:
	mutex_unlock(&arvif->ar->conf_mutex);
}

static void ath10k_tx_h_update_wep_key(struct ieee80211_vif *vif,
				       struct ieee80211_key_conf *key,
				       struct sk_buff *skb)
{
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	struct ath10k *ar = arvif->ar;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_has_protected(hdr->frame_control))
		return;

	if (!key)
		return;

	if (key->cipher != WLAN_CIPHER_SUITE_WEP40 &&
	    key->cipher != WLAN_CIPHER_SUITE_WEP104)
		return;

	if (key->keyidx == arvif->def_wep_key_idx)
		return;

	/* FIXME: Most likely a few frames will be TXed with an old key. Simply
	 * queueing frames until key index is updated is not an option because
	 * sk_buff may need more processing to be done, e.g. offchannel */
	arvif->def_wep_key_newidx = key->keyidx;
	ieee80211_queue_work(ar->hw, &arvif->wep_key_work);
}

static void ath10k_tx_h_add_p2p_noa_ie(struct ath10k *ar,
				       struct ieee80211_vif *vif,
				       struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);

	/* This is case only for P2P_GO */
	if (arvif->vdev_type != WMI_VDEV_TYPE_AP ||
	    arvif->vdev_subtype != WMI_VDEV_SUBTYPE_P2P_GO)
		return;

	if (unlikely(ieee80211_is_probe_resp(hdr->frame_control))) {
		spin_lock_bh(&ar->data_lock);
		if (arvif->u.ap.noa_data)
			if (!pskb_expand_head(skb, 0, arvif->u.ap.noa_len,
					      GFP_ATOMIC))
				memcpy(skb_put(skb, arvif->u.ap.noa_len),
				       arvif->u.ap.noa_data,
				       arvif->u.ap.noa_len);
		spin_unlock_bh(&ar->data_lock);
	}
}

static void ath10k_tx_htt(struct ath10k *ar, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	int ret = 0;

	if (ar->htt.target_version_major >= 3) {
		/* Since HTT 3.0 there is no separate mgmt tx command */
		ret = ath10k_htt_tx(&ar->htt, skb);
		goto exit;
	}

	if (ieee80211_is_mgmt(hdr->frame_control)) {
		if (test_bit(ATH10K_FW_FEATURE_HAS_WMI_MGMT_TX,
			     ar->fw_features)) {
			if (skb_queue_len(&ar->wmi_mgmt_tx_queue) >=
			    ATH10K_MAX_NUM_MGMT_PENDING) {
				ath10k_warn(ar, "reached WMI management transmit queue limit\n");
				ret = -EBUSY;
				goto exit;
			}

			skb_queue_tail(&ar->wmi_mgmt_tx_queue, skb);
			ieee80211_queue_work(ar->hw, &ar->wmi_mgmt_tx_work);
		} else {
			ret = ath10k_htt_mgmt_tx(&ar->htt, skb);
		}
	} else if (!test_bit(ATH10K_FW_FEATURE_HAS_WMI_MGMT_TX,
			     ar->fw_features) &&
		   ieee80211_is_nullfunc(hdr->frame_control)) {
		/* FW does not report tx status properly for NullFunc frames
		 * unless they are sent through mgmt tx path. mac80211 sends
		 * those frames when it detects link/beacon loss and depends
		 * on the tx status to be correct. */
		ret = ath10k_htt_mgmt_tx(&ar->htt, skb);
	} else {
		ret = ath10k_htt_tx(&ar->htt, skb);
	}

exit:
	if (ret) {
		ath10k_warn(ar, "failed to transmit packet, dropping: %d\n",
			    ret);
		ieee80211_free_txskb(ar->hw, skb);
	}
}

void ath10k_offchan_tx_purge(struct ath10k *ar)
{
	struct sk_buff *skb;

	for (;;) {
		skb = skb_dequeue(&ar->offchan_tx_queue);
		if (!skb)
			break;

		ieee80211_free_txskb(ar->hw, skb);
	}
}

void ath10k_offchan_tx_work(struct work_struct *work)
{
	struct ath10k *ar = container_of(work, struct ath10k, offchan_tx_work);
	struct ath10k_peer *peer;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb;
	const u8 *peer_addr;
	int vdev_id;
	int ret;

	/* FW requirement: We must create a peer before FW will send out
	 * an offchannel frame. Otherwise the frame will be stuck and
	 * never transmitted. We delete the peer upon tx completion.
	 * It is unlikely that a peer for offchannel tx will already be
	 * present. However it may be in some rare cases so account for that.
	 * Otherwise we might remove a legitimate peer and break stuff. */

	for (;;) {
		skb = skb_dequeue(&ar->offchan_tx_queue);
		if (!skb)
			break;

		mutex_lock(&ar->conf_mutex);

		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac offchannel skb %p\n",
			   skb);

		hdr = (struct ieee80211_hdr *)skb->data;
		peer_addr = ieee80211_get_DA(hdr);
		vdev_id = ATH10K_SKB_CB(skb)->vdev_id;

		spin_lock_bh(&ar->data_lock);
		peer = ath10k_peer_find(ar, vdev_id, peer_addr);
		spin_unlock_bh(&ar->data_lock);

		if (peer)
			/* FIXME: should this use ath10k_warn()? */
			ath10k_dbg(ar, ATH10K_DBG_MAC, "peer %pM on vdev %d already present\n",
				   peer_addr, vdev_id);

		if (!peer) {
			ret = ath10k_peer_create(ar, vdev_id, peer_addr);
			if (ret)
				ath10k_warn(ar, "failed to create peer %pM on vdev %d: %d\n",
					    peer_addr, vdev_id, ret);
		}

		spin_lock_bh(&ar->data_lock);
		reinit_completion(&ar->offchan_tx_completed);
		ar->offchan_tx_skb = skb;
		spin_unlock_bh(&ar->data_lock);

		ath10k_tx_htt(ar, skb);

		ret = wait_for_completion_timeout(&ar->offchan_tx_completed,
						  3 * HZ);
		if (ret <= 0)
			ath10k_warn(ar, "timed out waiting for offchannel skb %p\n",
				    skb);

		if (!peer) {
			ret = ath10k_peer_delete(ar, vdev_id, peer_addr);
			if (ret)
				ath10k_warn(ar, "failed to delete peer %pM on vdev %d: %d\n",
					    peer_addr, vdev_id, ret);
		}

		mutex_unlock(&ar->conf_mutex);
	}
}

void ath10k_mgmt_over_wmi_tx_purge(struct ath10k *ar)
{
	struct sk_buff *skb;

	for (;;) {
		skb = skb_dequeue(&ar->wmi_mgmt_tx_queue);
		if (!skb)
			break;

		ieee80211_free_txskb(ar->hw, skb);
	}
}

void ath10k_mgmt_over_wmi_tx_work(struct work_struct *work)
{
	struct ath10k *ar = container_of(work, struct ath10k, wmi_mgmt_tx_work);
	struct sk_buff *skb;
	int ret;

	for (;;) {
		skb = skb_dequeue(&ar->wmi_mgmt_tx_queue);
		if (!skb)
			break;

		ret = ath10k_wmi_mgmt_tx(ar, skb);
		if (ret) {
			ath10k_warn(ar, "failed to transmit management frame via WMI: %d\n",
				    ret);
			ieee80211_free_txskb(ar->hw, skb);
		}
	}
}

/************/
/* Scanning */
/************/

void __ath10k_scan_finish(struct ath10k *ar)
{
	lockdep_assert_held(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH10K_SCAN_IDLE:
		break;
	case ATH10K_SCAN_RUNNING:
	case ATH10K_SCAN_ABORTING:
		if (ar->scan.is_roc)
			ieee80211_remain_on_channel_expired(ar->hw);
		else
			ieee80211_scan_completed(ar->hw,
						 (ar->scan.state ==
						  ATH10K_SCAN_ABORTING));
		/* fall through */
	case ATH10K_SCAN_STARTING:
		ar->scan.state = ATH10K_SCAN_IDLE;
		ar->scan_channel = NULL;
		ath10k_offchan_tx_purge(ar);
		cancel_delayed_work(&ar->scan.timeout);
		complete_all(&ar->scan.completed);
		break;
	}
}

void ath10k_scan_finish(struct ath10k *ar)
{
	spin_lock_bh(&ar->data_lock);
	__ath10k_scan_finish(ar);
	spin_unlock_bh(&ar->data_lock);
}

static int ath10k_scan_stop(struct ath10k *ar)
{
	struct wmi_stop_scan_arg arg = {
		.req_id = 1, /* FIXME */
		.req_type = WMI_SCAN_STOP_ONE,
		.u.scan_id = ATH10K_SCAN_ID,
	};
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_wmi_stop_scan(ar, &arg);
	if (ret) {
		ath10k_warn(ar, "failed to stop wmi scan: %d\n", ret);
		goto out;
	}

	ret = wait_for_completion_timeout(&ar->scan.completed, 3*HZ);
	if (ret == 0) {
		ath10k_warn(ar, "failed to receive scan abortion completion: timed out\n");
		ret = -ETIMEDOUT;
	} else if (ret > 0) {
		ret = 0;
	}

out:
	/* Scan state should be updated upon scan completion but in case
	 * firmware fails to deliver the event (for whatever reason) it is
	 * desired to clean up scan state anyway. Firmware may have just
	 * dropped the scan completion event delivery due to transport pipe
	 * being overflown with data and/or it can recover on its own before
	 * next scan request is submitted.
	 */
	spin_lock_bh(&ar->data_lock);
	if (ar->scan.state != ATH10K_SCAN_IDLE)
		__ath10k_scan_finish(ar);
	spin_unlock_bh(&ar->data_lock);

	return ret;
}

static void ath10k_scan_abort(struct ath10k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);

	switch (ar->scan.state) {
	case ATH10K_SCAN_IDLE:
		/* This can happen if timeout worker kicked in and called
		 * abortion while scan completion was being processed.
		 */
		break;
	case ATH10K_SCAN_STARTING:
	case ATH10K_SCAN_ABORTING:
		ath10k_warn(ar, "refusing scan abortion due to invalid scan state: %s (%d)\n",
			    ath10k_scan_state_str(ar->scan.state),
			    ar->scan.state);
		break;
	case ATH10K_SCAN_RUNNING:
		ar->scan.state = ATH10K_SCAN_ABORTING;
		spin_unlock_bh(&ar->data_lock);

		ret = ath10k_scan_stop(ar);
		if (ret)
			ath10k_warn(ar, "failed to abort scan: %d\n", ret);

		spin_lock_bh(&ar->data_lock);
		break;
	}

	spin_unlock_bh(&ar->data_lock);
}

void ath10k_scan_timeout_work(struct work_struct *work)
{
	struct ath10k *ar = container_of(work, struct ath10k,
					 scan.timeout.work);

	mutex_lock(&ar->conf_mutex);
	ath10k_scan_abort(ar);
	mutex_unlock(&ar->conf_mutex);
}

static int ath10k_start_scan(struct ath10k *ar,
			     const struct wmi_start_scan_arg *arg)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_wmi_start_scan(ar, arg);
	if (ret)
		return ret;

	ret = wait_for_completion_timeout(&ar->scan.started, 1*HZ);
	if (ret == 0) {
		ret = ath10k_scan_stop(ar);
		if (ret)
			ath10k_warn(ar, "failed to stop scan: %d\n", ret);

		return -ETIMEDOUT;
	}

	/* Add a 200ms margin to account for event/command processing */
	ieee80211_queue_delayed_work(ar->hw, &ar->scan.timeout,
				     msecs_to_jiffies(arg->max_scan_time+200));
	return 0;
}

/**********************/
/* mac80211 callbacks */
/**********************/

static void ath10k_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct ath10k *ar = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	/* We should disable CCK RATE due to P2P */
	if (info->flags & IEEE80211_TX_CTL_NO_CCK_RATE)
		ath10k_dbg(ar, ATH10K_DBG_MAC, "IEEE80211_TX_CTL_NO_CCK_RATE\n");

	ATH10K_SKB_CB(skb)->htt.is_offchan = false;
	ATH10K_SKB_CB(skb)->htt.tid = ath10k_tx_h_get_tid(hdr);
	ATH10K_SKB_CB(skb)->vdev_id = ath10k_tx_h_get_vdev_id(ar, vif);

	/* it makes no sense to process injected frames like that */
	if (vif && vif->type != NL80211_IFTYPE_MONITOR) {
		ath10k_tx_h_nwifi(hw, skb);
		ath10k_tx_h_update_wep_key(vif, key, skb);
		ath10k_tx_h_add_p2p_noa_ie(ar, vif, skb);
		ath10k_tx_h_seq_no(vif, skb);
	}

	if (info->flags & IEEE80211_TX_CTL_TX_OFFCHAN) {
		spin_lock_bh(&ar->data_lock);
		ATH10K_SKB_CB(skb)->htt.is_offchan = true;
		ATH10K_SKB_CB(skb)->vdev_id = ar->scan.vdev_id;
		spin_unlock_bh(&ar->data_lock);

		ath10k_dbg(ar, ATH10K_DBG_MAC, "queued offchannel skb %p\n",
			   skb);

		skb_queue_tail(&ar->offchan_tx_queue, skb);
		ieee80211_queue_work(hw, &ar->offchan_tx_work);
		return;
	}

	ath10k_tx_htt(ar, skb);
}

/* Must not be called with conf_mutex held as workers can use that also. */
static void ath10k_drain_tx(struct ath10k *ar)
{
	/* make sure rcu-protected mac80211 tx path itself is drained */
	synchronize_net();

	ath10k_offchan_tx_purge(ar);
	ath10k_mgmt_over_wmi_tx_purge(ar);

	cancel_work_sync(&ar->offchan_tx_work);
	cancel_work_sync(&ar->wmi_mgmt_tx_work);
}

void ath10k_halt(struct ath10k *ar)
{
	struct ath10k_vif *arvif;

	lockdep_assert_held(&ar->conf_mutex);

	clear_bit(ATH10K_CAC_RUNNING, &ar->dev_flags);
	ar->filter_flags = 0;
	ar->monitor = false;

	if (ar->monitor_started)
		ath10k_monitor_stop(ar);

	ar->monitor_started = false;

	ath10k_scan_finish(ar);
	ath10k_peer_cleanup_all(ar);
	ath10k_core_stop(ar);
	ath10k_hif_power_down(ar);

	spin_lock_bh(&ar->data_lock);
	list_for_each_entry(arvif, &ar->arvifs, list)
		ath10k_mac_vif_beacon_cleanup(arvif);
	spin_unlock_bh(&ar->data_lock);
}

static int ath10k_get_antenna(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant)
{
	struct ath10k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);

	if (ar->cfg_tx_chainmask) {
		*tx_ant = ar->cfg_tx_chainmask;
		*rx_ant = ar->cfg_rx_chainmask;
	} else {
		*tx_ant = ar->supp_tx_chainmask;
		*rx_ant = ar->supp_rx_chainmask;
	}

	mutex_unlock(&ar->conf_mutex);

	return 0;
}

static int __ath10k_set_antenna(struct ath10k *ar, u32 tx_ant, u32 rx_ant)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ar->cfg_tx_chainmask = tx_ant;
	ar->cfg_rx_chainmask = rx_ant;

	if ((ar->state != ATH10K_STATE_ON) &&
	    (ar->state != ATH10K_STATE_RESTARTED))
		return 0;

	ret = ath10k_wmi_pdev_set_param(ar, ar->wmi.pdev_param->tx_chain_mask,
					tx_ant);
	if (ret) {
		ath10k_warn(ar, "failed to set tx-chainmask: %d, req 0x%x\n",
			    ret, tx_ant);
		return ret;
	}

	ret = ath10k_wmi_pdev_set_param(ar, ar->wmi.pdev_param->rx_chain_mask,
					rx_ant);
	if (ret) {
		ath10k_warn(ar, "failed to set rx-chainmask: %d, req 0x%x\n",
			    ret, rx_ant);
		return ret;
	}

	return 0;
}

static int ath10k_set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct ath10k *ar = hw->priv;
	int ret;

	mutex_lock(&ar->conf_mutex);
	ret = __ath10k_set_antenna(ar, tx_ant, rx_ant);
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath10k_start(struct ieee80211_hw *hw)
{
	struct ath10k *ar = hw->priv;
	int ret = 0;

	/*
	 * This makes sense only when restarting hw. It is harmless to call
	 * uncoditionally. This is necessary to make sure no HTT/WMI tx
	 * commands will be submitted while restarting.
	 */
	ath10k_drain_tx(ar);

	mutex_lock(&ar->conf_mutex);

	switch (ar->state) {
	case ATH10K_STATE_OFF:
		ar->state = ATH10K_STATE_ON;
		break;
	case ATH10K_STATE_RESTARTING:
		ath10k_halt(ar);
		ar->state = ATH10K_STATE_RESTARTED;
		break;
	case ATH10K_STATE_ON:
	case ATH10K_STATE_RESTARTED:
	case ATH10K_STATE_WEDGED:
		WARN_ON(1);
		ret = -EINVAL;
		goto err;
	case ATH10K_STATE_UTF:
		ret = -EBUSY;
		goto err;
	}

	ret = ath10k_hif_power_up(ar);
	if (ret) {
		ath10k_err(ar, "Could not init hif: %d\n", ret);
		goto err_off;
	}

	ret = ath10k_core_start(ar, ATH10K_FIRMWARE_MODE_NORMAL);
	if (ret) {
		ath10k_err(ar, "Could not init core: %d\n", ret);
		goto err_power_down;
	}

	ret = ath10k_wmi_pdev_set_param(ar, ar->wmi.pdev_param->pmf_qos, 1);
	if (ret) {
		ath10k_warn(ar, "failed to enable PMF QOS: %d\n", ret);
		goto err_core_stop;
	}

	ret = ath10k_wmi_pdev_set_param(ar, ar->wmi.pdev_param->dynamic_bw, 1);
	if (ret) {
		ath10k_warn(ar, "failed to enable dynamic BW: %d\n", ret);
		goto err_core_stop;
	}

	if (ar->cfg_tx_chainmask)
		__ath10k_set_antenna(ar, ar->cfg_tx_chainmask,
				     ar->cfg_rx_chainmask);

	/*
	 * By default FW set ARP frames ac to voice (6). In that case ARP
	 * exchange is not working properly for UAPSD enabled AP. ARP requests
	 * which arrives with access category 0 are processed by network stack
	 * and send back with access category 0, but FW changes access category
	 * to 6. Set ARP frames access category to best effort (0) solves
	 * this problem.
	 */

	ret = ath10k_wmi_pdev_set_param(ar,
					ar->wmi.pdev_param->arp_ac_override, 0);
	if (ret) {
		ath10k_warn(ar, "failed to set arp ac override parameter: %d\n",
			    ret);
		goto err_core_stop;
	}

	ar->num_started_vdevs = 0;
	ath10k_regd_update(ar);

	ath10k_spectral_start(ar);

	mutex_unlock(&ar->conf_mutex);
	return 0;

err_core_stop:
	ath10k_core_stop(ar);

err_power_down:
	ath10k_hif_power_down(ar);

err_off:
	ar->state = ATH10K_STATE_OFF;

err:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static void ath10k_stop(struct ieee80211_hw *hw)
{
	struct ath10k *ar = hw->priv;

	ath10k_drain_tx(ar);

	mutex_lock(&ar->conf_mutex);
	if (ar->state != ATH10K_STATE_OFF) {
		ath10k_halt(ar);
		ar->state = ATH10K_STATE_OFF;
	}
	mutex_unlock(&ar->conf_mutex);

	cancel_delayed_work_sync(&ar->scan.timeout);
	cancel_work_sync(&ar->restart_work);
}

static int ath10k_config_ps(struct ath10k *ar)
{
	struct ath10k_vif *arvif;
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ret = ath10k_mac_vif_setup_ps(arvif);
		if (ret) {
			ath10k_warn(ar, "failed to setup powersave: %d\n", ret);
			break;
		}
	}

	return ret;
}

static const char *chandef_get_width(enum nl80211_chan_width width)
{
	switch (width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		return "20 (noht)";
	case NL80211_CHAN_WIDTH_20:
		return "20";
	case NL80211_CHAN_WIDTH_40:
		return "40";
	case NL80211_CHAN_WIDTH_80:
		return "80";
	case NL80211_CHAN_WIDTH_80P80:
		return "80+80";
	case NL80211_CHAN_WIDTH_160:
		return "160";
	case NL80211_CHAN_WIDTH_5:
		return "5";
	case NL80211_CHAN_WIDTH_10:
		return "10";
	}
	return "?";
}

static void ath10k_config_chan(struct ath10k *ar)
{
	struct ath10k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ath10k_dbg(ar, ATH10K_DBG_MAC,
		   "mac config channel to %dMHz (cf1 %dMHz cf2 %dMHz width %s)\n",
		   ar->chandef.chan->center_freq,
		   ar->chandef.center_freq1,
		   ar->chandef.center_freq2,
		   chandef_get_width(ar->chandef.width));

	/* First stop monitor interface. Some FW versions crash if there's a
	 * lone monitor interface. */
	if (ar->monitor_started)
		ath10k_monitor_stop(ar);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (!arvif->is_started)
			continue;

		if (!arvif->is_up)
			continue;

		if (arvif->vdev_type == WMI_VDEV_TYPE_MONITOR)
			continue;

		ret = ath10k_wmi_vdev_down(ar, arvif->vdev_id);
		if (ret) {
			ath10k_warn(ar, "failed to down vdev %d: %d\n",
				    arvif->vdev_id, ret);
			continue;
		}
	}

	/* all vdevs are downed now - attempt to restart and re-up them */

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (!arvif->is_started)
			continue;

		if (arvif->vdev_type == WMI_VDEV_TYPE_MONITOR)
			continue;

		ret = ath10k_vdev_restart(arvif);
		if (ret) {
			ath10k_warn(ar, "failed to restart vdev %d: %d\n",
				    arvif->vdev_id, ret);
			continue;
		}

		if (!arvif->is_up)
			continue;

		ret = ath10k_wmi_vdev_up(arvif->ar, arvif->vdev_id, arvif->aid,
					 arvif->bssid);
		if (ret) {
			ath10k_warn(ar, "failed to bring vdev up %d: %d\n",
				    arvif->vdev_id, ret);
			continue;
		}
	}

	ath10k_monitor_recalc(ar);
}

static int ath10k_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ath10k *ar = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	int ret = 0;
	u32 param;

	mutex_lock(&ar->conf_mutex);

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ath10k_dbg(ar, ATH10K_DBG_MAC,
			   "mac config channel %dMHz flags 0x%x radar %d\n",
			   conf->chandef.chan->center_freq,
			   conf->chandef.chan->flags,
			   conf->radar_enabled);

		spin_lock_bh(&ar->data_lock);
		ar->rx_channel = conf->chandef.chan;
		spin_unlock_bh(&ar->data_lock);

		ar->radar_enabled = conf->radar_enabled;
		ath10k_recalc_radar_detection(ar);

		if (!cfg80211_chandef_identical(&ar->chandef, &conf->chandef)) {
			ar->chandef = conf->chandef;
			ath10k_config_chan(ar);
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac config power %d\n",
			   hw->conf.power_level);

		param = ar->wmi.pdev_param->txpower_limit2g;
		ret = ath10k_wmi_pdev_set_param(ar, param,
						hw->conf.power_level * 2);
		if (ret)
			ath10k_warn(ar, "failed to set 2g txpower %d: %d\n",
				    hw->conf.power_level, ret);

		param = ar->wmi.pdev_param->txpower_limit5g;
		ret = ath10k_wmi_pdev_set_param(ar, param,
						hw->conf.power_level * 2);
		if (ret)
			ath10k_warn(ar, "failed to set 5g txpower %d: %d\n",
				    hw->conf.power_level, ret);
	}

	if (changed & IEEE80211_CONF_CHANGE_PS)
		ath10k_config_ps(ar);

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		ar->monitor = conf->flags & IEEE80211_CONF_MONITOR;
		ret = ath10k_monitor_recalc(ar);
		if (ret)
			ath10k_warn(ar, "failed to recalc monitor: %d\n", ret);
	}

	mutex_unlock(&ar->conf_mutex);
	return ret;
}

/*
 * TODO:
 * Figure out how to handle WMI_VDEV_SUBTYPE_P2P_DEVICE,
 * because we will send mgmt frames without CCK. This requirement
 * for P2P_FIND/GO_NEG should be handled by checking CCK flag
 * in the TX packet.
 */
static int ath10k_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	enum wmi_sta_powersave_param param;
	int ret = 0;
	u32 value;
	int bit;
	u32 vdev_param;

	mutex_lock(&ar->conf_mutex);

	memset(arvif, 0, sizeof(*arvif));

	arvif->ar = ar;
	arvif->vif = vif;

	INIT_WORK(&arvif->wep_key_work, ath10k_tx_wep_key_work);
	INIT_LIST_HEAD(&arvif->list);

	if (ar->free_vdev_map == 0) {
		ath10k_warn(ar, "Free vdev map is empty, no more interfaces allowed.\n");
		ret = -EBUSY;
		goto err;
	}
	bit = ffs(ar->free_vdev_map);

	arvif->vdev_id = bit - 1;
	arvif->vdev_subtype = WMI_VDEV_SUBTYPE_NONE;

	if (ar->p2p)
		arvif->vdev_subtype = WMI_VDEV_SUBTYPE_P2P_DEVICE;

	switch (vif->type) {
	case NL80211_IFTYPE_UNSPECIFIED:
	case NL80211_IFTYPE_STATION:
		arvif->vdev_type = WMI_VDEV_TYPE_STA;
		if (vif->p2p)
			arvif->vdev_subtype = WMI_VDEV_SUBTYPE_P2P_CLIENT;
		break;
	case NL80211_IFTYPE_ADHOC:
		arvif->vdev_type = WMI_VDEV_TYPE_IBSS;
		break;
	case NL80211_IFTYPE_AP:
		arvif->vdev_type = WMI_VDEV_TYPE_AP;

		if (vif->p2p)
			arvif->vdev_subtype = WMI_VDEV_SUBTYPE_P2P_GO;
		break;
	case NL80211_IFTYPE_MONITOR:
		arvif->vdev_type = WMI_VDEV_TYPE_MONITOR;
		break;
	default:
		WARN_ON(1);
		break;
	}

	/* Some firmware revisions don't wait for beacon tx completion before
	 * sending another SWBA event. This could lead to hardware using old
	 * (freed) beacon data in some cases, e.g. tx credit starvation
	 * combined with missed TBTT. This is very very rare.
	 *
	 * On non-IOMMU-enabled hosts this could be a possible security issue
	 * because hw could beacon some random data on the air.  On
	 * IOMMU-enabled hosts DMAR faults would occur in most cases and target
	 * device would crash.
	 *
	 * Since there are no beacon tx completions (implicit nor explicit)
	 * propagated to host the only workaround for this is to allocate a
	 * DMA-coherent buffer for a lifetime of a vif and use it for all
	 * beacon tx commands. Worst case for this approach is some beacons may
	 * become corrupted, e.g. have garbled IEs or out-of-date TIM bitmap.
	 */
	if (vif->type == NL80211_IFTYPE_ADHOC ||
	    vif->type == NL80211_IFTYPE_AP) {
		arvif->beacon_buf = dma_zalloc_coherent(ar->dev,
							IEEE80211_MAX_FRAME_LEN,
							&arvif->beacon_paddr,
							GFP_KERNEL);
		if (!arvif->beacon_buf) {
			ret = -ENOMEM;
			ath10k_warn(ar, "failed to allocate beacon buffer: %d\n",
				    ret);
			goto err;
		}
	}

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev create %d (add interface) type %d subtype %d bcnmode %s\n",
		   arvif->vdev_id, arvif->vdev_type, arvif->vdev_subtype,
		   arvif->beacon_buf ? "single-buf" : "per-skb");

	ret = ath10k_wmi_vdev_create(ar, arvif->vdev_id, arvif->vdev_type,
				     arvif->vdev_subtype, vif->addr);
	if (ret) {
		ath10k_warn(ar, "failed to create WMI vdev %i: %d\n",
			    arvif->vdev_id, ret);
		goto err;
	}

	ar->free_vdev_map &= ~(1 << arvif->vdev_id);
	list_add(&arvif->list, &ar->arvifs);

	vdev_param = ar->wmi.vdev_param->def_keyid;
	ret = ath10k_wmi_vdev_set_param(ar, 0, vdev_param,
					arvif->def_wep_key_idx);
	if (ret) {
		ath10k_warn(ar, "failed to set vdev %i default key id: %d\n",
			    arvif->vdev_id, ret);
		goto err_vdev_delete;
	}

	vdev_param = ar->wmi.vdev_param->tx_encap_type;
	ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
					ATH10K_HW_TXRX_NATIVE_WIFI);
	/* 10.X firmware does not support this VDEV parameter. Do not warn */
	if (ret && ret != -EOPNOTSUPP) {
		ath10k_warn(ar, "failed to set vdev %i TX encapsulation: %d\n",
			    arvif->vdev_id, ret);
		goto err_vdev_delete;
	}

	if (arvif->vdev_type == WMI_VDEV_TYPE_AP) {
		ret = ath10k_peer_create(ar, arvif->vdev_id, vif->addr);
		if (ret) {
			ath10k_warn(ar, "failed to create vdev %i peer for AP: %d\n",
				    arvif->vdev_id, ret);
			goto err_vdev_delete;
		}

		ret = ath10k_mac_set_kickout(arvif);
		if (ret) {
			ath10k_warn(ar, "failed to set vdev %i kickout parameters: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_delete;
		}
	}

	if (arvif->vdev_type == WMI_VDEV_TYPE_STA) {
		param = WMI_STA_PS_PARAM_RX_WAKE_POLICY;
		value = WMI_STA_PS_RX_WAKE_POLICY_WAKE;
		ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
						  param, value);
		if (ret) {
			ath10k_warn(ar, "failed to set vdev %i RX wake policy: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_delete;
		}

		param = WMI_STA_PS_PARAM_TX_WAKE_THRESHOLD;
		value = WMI_STA_PS_TX_WAKE_THRESHOLD_ALWAYS;
		ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
						  param, value);
		if (ret) {
			ath10k_warn(ar, "failed to set vdev %i TX wake thresh: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_delete;
		}

		param = WMI_STA_PS_PARAM_PSPOLL_COUNT;
		value = WMI_STA_PS_PSPOLL_COUNT_NO_MAX;
		ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
						  param, value);
		if (ret) {
			ath10k_warn(ar, "failed to set vdev %i PSPOLL count: %d\n",
				    arvif->vdev_id, ret);
			goto err_peer_delete;
		}
	}

	ret = ath10k_mac_set_rts(arvif, ar->hw->wiphy->rts_threshold);
	if (ret) {
		ath10k_warn(ar, "failed to set rts threshold for vdev %d: %d\n",
			    arvif->vdev_id, ret);
		goto err_peer_delete;
	}

	ret = ath10k_mac_set_frag(arvif, ar->hw->wiphy->frag_threshold);
	if (ret) {
		ath10k_warn(ar, "failed to set frag threshold for vdev %d: %d\n",
			    arvif->vdev_id, ret);
		goto err_peer_delete;
	}

	mutex_unlock(&ar->conf_mutex);
	return 0;

err_peer_delete:
	if (arvif->vdev_type == WMI_VDEV_TYPE_AP)
		ath10k_wmi_peer_delete(ar, arvif->vdev_id, vif->addr);

err_vdev_delete:
	ath10k_wmi_vdev_delete(ar, arvif->vdev_id);
	ar->free_vdev_map |= 1 << arvif->vdev_id;
	list_del(&arvif->list);

err:
	if (arvif->beacon_buf) {
		dma_free_coherent(ar->dev, IEEE80211_MAX_FRAME_LEN,
				  arvif->beacon_buf, arvif->beacon_paddr);
		arvif->beacon_buf = NULL;
	}

	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static void ath10k_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	int ret;

	mutex_lock(&ar->conf_mutex);

	cancel_work_sync(&arvif->wep_key_work);

	spin_lock_bh(&ar->data_lock);
	ath10k_mac_vif_beacon_cleanup(arvif);
	spin_unlock_bh(&ar->data_lock);

	ret = ath10k_spectral_vif_stop(arvif);
	if (ret)
		ath10k_warn(ar, "failed to stop spectral for vdev %i: %d\n",
			    arvif->vdev_id, ret);

	ar->free_vdev_map |= 1 << arvif->vdev_id;
	list_del(&arvif->list);

	if (arvif->vdev_type == WMI_VDEV_TYPE_AP) {
		ret = ath10k_peer_delete(arvif->ar, arvif->vdev_id, vif->addr);
		if (ret)
			ath10k_warn(ar, "failed to remove peer for AP vdev %i: %d\n",
				    arvif->vdev_id, ret);

		kfree(arvif->u.ap.noa_data);
	}

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %i delete (remove interface)\n",
		   arvif->vdev_id);

	ret = ath10k_wmi_vdev_delete(ar, arvif->vdev_id);
	if (ret)
		ath10k_warn(ar, "failed to delete WMI vdev %i: %d\n",
			    arvif->vdev_id, ret);

	ath10k_peer_cleanup(ar, arvif->vdev_id);

	mutex_unlock(&ar->conf_mutex);
}

/*
 * FIXME: Has to be verified.
 */
#define SUPPORTED_FILTERS			\
	(FIF_PROMISC_IN_BSS |			\
	FIF_ALLMULTI |				\
	FIF_CONTROL |				\
	FIF_PSPOLL |				\
	FIF_OTHER_BSS |				\
	FIF_BCN_PRBRESP_PROMISC |		\
	FIF_PROBE_REQ |				\
	FIF_FCSFAIL)

static void ath10k_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	struct ath10k *ar = hw->priv;
	int ret;

	mutex_lock(&ar->conf_mutex);

	changed_flags &= SUPPORTED_FILTERS;
	*total_flags &= SUPPORTED_FILTERS;
	ar->filter_flags = *total_flags;

	ret = ath10k_monitor_recalc(ar);
	if (ret)
		ath10k_warn(ar, "failed to recalc montior: %d\n", ret);

	mutex_unlock(&ar->conf_mutex);
}

static void ath10k_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
				    u32 changed)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	int ret = 0;
	u32 vdev_param, pdev_param, slottime, preamble;

	mutex_lock(&ar->conf_mutex);

	if (changed & BSS_CHANGED_IBSS)
		ath10k_control_ibss(arvif, info, vif->addr);

	if (changed & BSS_CHANGED_BEACON_INT) {
		arvif->beacon_interval = info->beacon_int;
		vdev_param = ar->wmi.vdev_param->beacon_interval;
		ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
						arvif->beacon_interval);
		ath10k_dbg(ar, ATH10K_DBG_MAC,
			   "mac vdev %d beacon_interval %d\n",
			   arvif->vdev_id, arvif->beacon_interval);

		if (ret)
			ath10k_warn(ar, "failed to set beacon interval for vdev %d: %i\n",
				    arvif->vdev_id, ret);
	}

	if (changed & BSS_CHANGED_BEACON) {
		ath10k_dbg(ar, ATH10K_DBG_MAC,
			   "vdev %d set beacon tx mode to staggered\n",
			   arvif->vdev_id);

		pdev_param = ar->wmi.pdev_param->beacon_tx_mode;
		ret = ath10k_wmi_pdev_set_param(ar, pdev_param,
						WMI_BEACON_STAGGERED_MODE);
		if (ret)
			ath10k_warn(ar, "failed to set beacon mode for vdev %d: %i\n",
				    arvif->vdev_id, ret);
	}

	if (changed & BSS_CHANGED_BEACON_INFO) {
		arvif->dtim_period = info->dtim_period;

		ath10k_dbg(ar, ATH10K_DBG_MAC,
			   "mac vdev %d dtim_period %d\n",
			   arvif->vdev_id, arvif->dtim_period);

		vdev_param = ar->wmi.vdev_param->dtim_period;
		ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
						arvif->dtim_period);
		if (ret)
			ath10k_warn(ar, "failed to set dtim period for vdev %d: %i\n",
				    arvif->vdev_id, ret);
	}

	if (changed & BSS_CHANGED_SSID &&
	    vif->type == NL80211_IFTYPE_AP) {
		arvif->u.ap.ssid_len = info->ssid_len;
		if (info->ssid_len)
			memcpy(arvif->u.ap.ssid, info->ssid, info->ssid_len);
		arvif->u.ap.hidden_ssid = info->hidden_ssid;
	}

	/*
	 * Firmware manages AP self-peer internally so make sure to not create
	 * it in driver. Otherwise AP self-peer deletion may timeout later.
	 */
	if (changed & BSS_CHANGED_BSSID &&
	    vif->type != NL80211_IFTYPE_AP) {
		if (!is_zero_ether_addr(info->bssid)) {
			ath10k_dbg(ar, ATH10K_DBG_MAC,
				   "mac vdev %d create peer %pM\n",
				   arvif->vdev_id, info->bssid);

			ret = ath10k_peer_create(ar, arvif->vdev_id,
						 info->bssid);
			if (ret)
				ath10k_warn(ar, "failed to add peer %pM for vdev %d when changing bssid: %i\n",
					    info->bssid, arvif->vdev_id, ret);

			if (vif->type == NL80211_IFTYPE_STATION) {
				/*
				 * this is never erased as we it for crypto key
				 * clearing; this is FW requirement
				 */
				ether_addr_copy(arvif->bssid, info->bssid);

				ath10k_dbg(ar, ATH10K_DBG_MAC,
					   "mac vdev %d start %pM\n",
					   arvif->vdev_id, info->bssid);

				ret = ath10k_vdev_start(arvif);
				if (ret) {
					ath10k_warn(ar, "failed to start vdev %i: %d\n",
						    arvif->vdev_id, ret);
					goto exit;
				}

				arvif->is_started = true;
			}

			/*
			 * Mac80211 does not keep IBSS bssid when leaving IBSS,
			 * so driver need to store it. It is needed when leaving
			 * IBSS in order to remove BSSID peer.
			 */
			if (vif->type == NL80211_IFTYPE_ADHOC)
				memcpy(arvif->bssid, info->bssid,
				       ETH_ALEN);
		}
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED)
		ath10k_control_beaconing(arvif, info);

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		arvif->use_cts_prot = info->use_cts_prot;
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d cts_prot %d\n",
			   arvif->vdev_id, info->use_cts_prot);

		ret = ath10k_recalc_rtscts_prot(arvif);
		if (ret)
			ath10k_warn(ar, "failed to recalculate rts/cts prot for vdev %d: %d\n",
				    arvif->vdev_id, ret);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (info->use_short_slot)
			slottime = WMI_VDEV_SLOT_TIME_SHORT; /* 9us */

		else
			slottime = WMI_VDEV_SLOT_TIME_LONG; /* 20us */

		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d slot_time %d\n",
			   arvif->vdev_id, slottime);

		vdev_param = ar->wmi.vdev_param->slot_time;
		ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
						slottime);
		if (ret)
			ath10k_warn(ar, "failed to set erp slot for vdev %d: %i\n",
				    arvif->vdev_id, ret);
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		if (info->use_short_preamble)
			preamble = WMI_VDEV_PREAMBLE_SHORT;
		else
			preamble = WMI_VDEV_PREAMBLE_LONG;

		ath10k_dbg(ar, ATH10K_DBG_MAC,
			   "mac vdev %d preamble %dn",
			   arvif->vdev_id, preamble);

		vdev_param = ar->wmi.vdev_param->preamble;
		ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
						preamble);
		if (ret)
			ath10k_warn(ar, "failed to set preamble for vdev %d: %i\n",
				    arvif->vdev_id, ret);
	}

	if (changed & BSS_CHANGED_ASSOC) {
		if (info->assoc) {
			/* Workaround: Make sure monitor vdev is not running
			 * when associating to prevent some firmware revisions
			 * (e.g. 10.1 and 10.2) from crashing.
			 */
			if (ar->monitor_started)
				ath10k_monitor_stop(ar);
			ath10k_bss_assoc(hw, vif, info);
			ath10k_monitor_recalc(ar);
		}
	}

exit:
	mutex_unlock(&ar->conf_mutex);
}

static int ath10k_hw_scan(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  struct ieee80211_scan_request *hw_req)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	struct cfg80211_scan_request *req = &hw_req->req;
	struct wmi_start_scan_arg arg;
	int ret = 0;
	int i;

	mutex_lock(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	switch (ar->scan.state) {
	case ATH10K_SCAN_IDLE:
		reinit_completion(&ar->scan.started);
		reinit_completion(&ar->scan.completed);
		ar->scan.state = ATH10K_SCAN_STARTING;
		ar->scan.is_roc = false;
		ar->scan.vdev_id = arvif->vdev_id;
		ret = 0;
		break;
	case ATH10K_SCAN_STARTING:
	case ATH10K_SCAN_RUNNING:
	case ATH10K_SCAN_ABORTING:
		ret = -EBUSY;
		break;
	}
	spin_unlock_bh(&ar->data_lock);

	if (ret)
		goto exit;

	memset(&arg, 0, sizeof(arg));
	ath10k_wmi_start_scan_init(ar, &arg);
	arg.vdev_id = arvif->vdev_id;
	arg.scan_id = ATH10K_SCAN_ID;

	if (!req->no_cck)
		arg.scan_ctrl_flags |= WMI_SCAN_ADD_CCK_RATES;

	if (req->ie_len) {
		arg.ie_len = req->ie_len;
		memcpy(arg.ie, req->ie, arg.ie_len);
	}

	if (req->n_ssids) {
		arg.n_ssids = req->n_ssids;
		for (i = 0; i < arg.n_ssids; i++) {
			arg.ssids[i].len  = req->ssids[i].ssid_len;
			arg.ssids[i].ssid = req->ssids[i].ssid;
		}
	} else {
		arg.scan_ctrl_flags |= WMI_SCAN_FLAG_PASSIVE;
	}

	if (req->n_channels) {
		arg.n_channels = req->n_channels;
		for (i = 0; i < arg.n_channels; i++)
			arg.channels[i] = req->channels[i]->center_freq;
	}

	ret = ath10k_start_scan(ar, &arg);
	if (ret) {
		ath10k_warn(ar, "failed to start hw scan: %d\n", ret);
		spin_lock_bh(&ar->data_lock);
		ar->scan.state = ATH10K_SCAN_IDLE;
		spin_unlock_bh(&ar->data_lock);
	}

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static void ath10k_cancel_hw_scan(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif)
{
	struct ath10k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);
	cancel_delayed_work_sync(&ar->scan.timeout);
	ath10k_scan_abort(ar);
	mutex_unlock(&ar->conf_mutex);
}

static void ath10k_set_key_h_def_keyidx(struct ath10k *ar,
					struct ath10k_vif *arvif,
					enum set_key_cmd cmd,
					struct ieee80211_key_conf *key)
{
	u32 vdev_param = arvif->ar->wmi.vdev_param->def_keyid;
	int ret;

	/* 10.1 firmware branch requires default key index to be set to group
	 * key index after installing it. Otherwise FW/HW Txes corrupted
	 * frames with multi-vif APs. This is not required for main firmware
	 * branch (e.g. 636).
	 *
	 * FIXME: This has been tested only in AP. It remains unknown if this
	 * is required for multi-vif STA interfaces on 10.1 */

	if (arvif->vdev_type != WMI_VDEV_TYPE_AP)
		return;

	if (key->cipher == WLAN_CIPHER_SUITE_WEP40)
		return;

	if (key->cipher == WLAN_CIPHER_SUITE_WEP104)
		return;

	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
		return;

	if (cmd != SET_KEY)
		return;

	ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
					key->keyidx);
	if (ret)
		ath10k_warn(ar, "failed to set vdev %i group key as default key: %d\n",
			    arvif->vdev_id, ret);
}

static int ath10k_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	struct ath10k_peer *peer;
	const u8 *peer_addr;
	bool is_wep = key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
		      key->cipher == WLAN_CIPHER_SUITE_WEP104;
	int ret = 0;

	if (key->keyidx > WMI_MAX_KEY_INDEX)
		return -ENOSPC;

	mutex_lock(&ar->conf_mutex);

	if (sta)
		peer_addr = sta->addr;
	else if (arvif->vdev_type == WMI_VDEV_TYPE_STA)
		peer_addr = vif->bss_conf.bssid;
	else
		peer_addr = vif->addr;

	key->hw_key_idx = key->keyidx;

	/* the peer should not disappear in mid-way (unless FW goes awry) since
	 * we already hold conf_mutex. we just make sure its there now. */
	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find(ar, arvif->vdev_id, peer_addr);
	spin_unlock_bh(&ar->data_lock);

	if (!peer) {
		if (cmd == SET_KEY) {
			ath10k_warn(ar, "failed to install key for non-existent peer %pM\n",
				    peer_addr);
			ret = -EOPNOTSUPP;
			goto exit;
		} else {
			/* if the peer doesn't exist there is no key to disable
			 * anymore */
			goto exit;
		}
	}

	if (is_wep) {
		if (cmd == SET_KEY)
			arvif->wep_keys[key->keyidx] = key;
		else
			arvif->wep_keys[key->keyidx] = NULL;

		if (cmd == DISABLE_KEY)
			ath10k_clear_vdev_key(arvif, key);
	}

	ret = ath10k_install_key(arvif, key, cmd, peer_addr);
	if (ret) {
		ath10k_warn(ar, "failed to install key for vdev %i peer %pM: %d\n",
			    arvif->vdev_id, peer_addr, ret);
		goto exit;
	}

	ath10k_set_key_h_def_keyidx(ar, arvif, cmd, key);

	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find(ar, arvif->vdev_id, peer_addr);
	if (peer && cmd == SET_KEY)
		peer->keys[key->keyidx] = key;
	else if (peer && cmd == DISABLE_KEY)
		peer->keys[key->keyidx] = NULL;
	else if (peer == NULL)
		/* impossible unless FW goes crazy */
		ath10k_warn(ar, "Peer %pM disappeared!\n", peer_addr);
	spin_unlock_bh(&ar->data_lock);

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static void ath10k_sta_rc_update_wk(struct work_struct *wk)
{
	struct ath10k *ar;
	struct ath10k_vif *arvif;
	struct ath10k_sta *arsta;
	struct ieee80211_sta *sta;
	u32 changed, bw, nss, smps;
	int err;

	arsta = container_of(wk, struct ath10k_sta, update_wk);
	sta = container_of((void *)arsta, struct ieee80211_sta, drv_priv);
	arvif = arsta->arvif;
	ar = arvif->ar;

	spin_lock_bh(&ar->data_lock);

	changed = arsta->changed;
	arsta->changed = 0;

	bw = arsta->bw;
	nss = arsta->nss;
	smps = arsta->smps;

	spin_unlock_bh(&ar->data_lock);

	mutex_lock(&ar->conf_mutex);

	if (changed & IEEE80211_RC_BW_CHANGED) {
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac update sta %pM peer bw %d\n",
			   sta->addr, bw);

		err = ath10k_wmi_peer_set_param(ar, arvif->vdev_id, sta->addr,
						WMI_PEER_CHAN_WIDTH, bw);
		if (err)
			ath10k_warn(ar, "failed to update STA %pM peer bw %d: %d\n",
				    sta->addr, bw, err);
	}

	if (changed & IEEE80211_RC_NSS_CHANGED) {
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac update sta %pM nss %d\n",
			   sta->addr, nss);

		err = ath10k_wmi_peer_set_param(ar, arvif->vdev_id, sta->addr,
						WMI_PEER_NSS, nss);
		if (err)
			ath10k_warn(ar, "failed to update STA %pM nss %d: %d\n",
				    sta->addr, nss, err);
	}

	if (changed & IEEE80211_RC_SMPS_CHANGED) {
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac update sta %pM smps %d\n",
			   sta->addr, smps);

		err = ath10k_wmi_peer_set_param(ar, arvif->vdev_id, sta->addr,
						WMI_PEER_SMPS_STATE, smps);
		if (err)
			ath10k_warn(ar, "failed to update STA %pM smps %d: %d\n",
				    sta->addr, smps, err);
	}

	if (changed & IEEE80211_RC_SUPP_RATES_CHANGED) {
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac update sta %pM supp rates\n",
			   sta->addr);

		err = ath10k_station_assoc(ar, arvif, sta, true);
		if (err)
			ath10k_warn(ar, "failed to reassociate station: %pM\n",
				    sta->addr);
	}

	mutex_unlock(&ar->conf_mutex);
}

static int ath10k_sta_state(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta,
			    enum ieee80211_sta_state old_state,
			    enum ieee80211_sta_state new_state)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	int max_num_peers;
	int ret = 0;

	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE) {
		memset(arsta, 0, sizeof(*arsta));
		arsta->arvif = arvif;
		INIT_WORK(&arsta->update_wk, ath10k_sta_rc_update_wk);
	}

	/* cancel must be done outside the mutex to avoid deadlock */
	if ((old_state == IEEE80211_STA_NONE &&
	     new_state == IEEE80211_STA_NOTEXIST))
		cancel_work_sync(&arsta->update_wk);

	mutex_lock(&ar->conf_mutex);

	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE &&
	    vif->type != NL80211_IFTYPE_STATION) {
		/*
		 * New station addition.
		 */
		if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features))
			max_num_peers = TARGET_10X_NUM_PEERS_MAX - 1;
		else
			max_num_peers = TARGET_NUM_PEERS;

		if (ar->num_peers >= max_num_peers) {
			ath10k_warn(ar, "number of peers exceeded: peers number %d (max peers %d)\n",
				    ar->num_peers, max_num_peers);
			ret = -ENOBUFS;
			goto exit;
		}

		ath10k_dbg(ar, ATH10K_DBG_MAC,
			   "mac vdev %d peer create %pM (new sta) num_peers %d\n",
			   arvif->vdev_id, sta->addr, ar->num_peers);

		ret = ath10k_peer_create(ar, arvif->vdev_id, sta->addr);
		if (ret)
			ath10k_warn(ar, "failed to add peer %pM for vdev %d when adding a new sta: %i\n",
				    sta->addr, arvif->vdev_id, ret);
	} else if ((old_state == IEEE80211_STA_NONE &&
		    new_state == IEEE80211_STA_NOTEXIST)) {
		/*
		 * Existing station deletion.
		 */
		ath10k_dbg(ar, ATH10K_DBG_MAC,
			   "mac vdev %d peer delete %pM (sta gone)\n",
			   arvif->vdev_id, sta->addr);
		ret = ath10k_peer_delete(ar, arvif->vdev_id, sta->addr);
		if (ret)
			ath10k_warn(ar, "failed to delete peer %pM for vdev %d: %i\n",
				    sta->addr, arvif->vdev_id, ret);

		if (vif->type == NL80211_IFTYPE_STATION)
			ath10k_bss_disassoc(hw, vif);
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_ASSOC &&
		   (vif->type == NL80211_IFTYPE_AP ||
		    vif->type == NL80211_IFTYPE_ADHOC)) {
		/*
		 * New association.
		 */
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac sta %pM associated\n",
			   sta->addr);

		ret = ath10k_station_assoc(ar, arvif, sta, false);
		if (ret)
			ath10k_warn(ar, "failed to associate station %pM for vdev %i: %i\n",
				    sta->addr, arvif->vdev_id, ret);
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTH &&
		   (vif->type == NL80211_IFTYPE_AP ||
		    vif->type == NL80211_IFTYPE_ADHOC)) {
		/*
		 * Disassociation.
		 */
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac sta %pM disassociated\n",
			   sta->addr);

		ret = ath10k_station_disassoc(ar, arvif, sta);
		if (ret)
			ath10k_warn(ar, "failed to disassociate station: %pM vdev %i: %i\n",
				    sta->addr, arvif->vdev_id, ret);
	}
exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath10k_conf_tx_uapsd(struct ath10k *ar, struct ieee80211_vif *vif,
				u16 ac, bool enable)
{
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	u32 value = 0;
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	if (arvif->vdev_type != WMI_VDEV_TYPE_STA)
		return 0;

	switch (ac) {
	case IEEE80211_AC_VO:
		value = WMI_STA_PS_UAPSD_AC3_DELIVERY_EN |
			WMI_STA_PS_UAPSD_AC3_TRIGGER_EN;
		break;
	case IEEE80211_AC_VI:
		value = WMI_STA_PS_UAPSD_AC2_DELIVERY_EN |
			WMI_STA_PS_UAPSD_AC2_TRIGGER_EN;
		break;
	case IEEE80211_AC_BE:
		value = WMI_STA_PS_UAPSD_AC1_DELIVERY_EN |
			WMI_STA_PS_UAPSD_AC1_TRIGGER_EN;
		break;
	case IEEE80211_AC_BK:
		value = WMI_STA_PS_UAPSD_AC0_DELIVERY_EN |
			WMI_STA_PS_UAPSD_AC0_TRIGGER_EN;
		break;
	}

	if (enable)
		arvif->u.sta.uapsd |= value;
	else
		arvif->u.sta.uapsd &= ~value;

	ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
					  WMI_STA_PS_PARAM_UAPSD,
					  arvif->u.sta.uapsd);
	if (ret) {
		ath10k_warn(ar, "failed to set uapsd params: %d\n", ret);
		goto exit;
	}

	if (arvif->u.sta.uapsd)
		value = WMI_STA_PS_RX_WAKE_POLICY_POLL_UAPSD;
	else
		value = WMI_STA_PS_RX_WAKE_POLICY_WAKE;

	ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
					  WMI_STA_PS_PARAM_RX_WAKE_POLICY,
					  value);
	if (ret)
		ath10k_warn(ar, "failed to set rx wake param: %d\n", ret);

exit:
	return ret;
}

static int ath10k_conf_tx(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif, u16 ac,
			  const struct ieee80211_tx_queue_params *params)
{
	struct ath10k *ar = hw->priv;
	struct wmi_wmm_params_arg *p = NULL;
	int ret;

	mutex_lock(&ar->conf_mutex);

	switch (ac) {
	case IEEE80211_AC_VO:
		p = &ar->wmm_params.ac_vo;
		break;
	case IEEE80211_AC_VI:
		p = &ar->wmm_params.ac_vi;
		break;
	case IEEE80211_AC_BE:
		p = &ar->wmm_params.ac_be;
		break;
	case IEEE80211_AC_BK:
		p = &ar->wmm_params.ac_bk;
		break;
	}

	if (WARN_ON(!p)) {
		ret = -EINVAL;
		goto exit;
	}

	p->cwmin = params->cw_min;
	p->cwmax = params->cw_max;
	p->aifs = params->aifs;

	/*
	 * The channel time duration programmed in the HW is in absolute
	 * microseconds, while mac80211 gives the txop in units of
	 * 32 microseconds.
	 */
	p->txop = params->txop * 32;

	/* FIXME: FW accepts wmm params per hw, not per vif */
	ret = ath10k_wmi_pdev_set_wmm_params(ar, &ar->wmm_params);
	if (ret) {
		ath10k_warn(ar, "failed to set wmm params: %d\n", ret);
		goto exit;
	}

	ret = ath10k_conf_tx_uapsd(ar, vif, ac, params->uapsd);
	if (ret)
		ath10k_warn(ar, "failed to set sta uapsd: %d\n", ret);

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

#define ATH10K_ROC_TIMEOUT_HZ (2*HZ)

static int ath10k_remain_on_channel(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_channel *chan,
				    int duration,
				    enum ieee80211_roc_type type)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	struct wmi_start_scan_arg arg;
	int ret = 0;

	mutex_lock(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);
	switch (ar->scan.state) {
	case ATH10K_SCAN_IDLE:
		reinit_completion(&ar->scan.started);
		reinit_completion(&ar->scan.completed);
		reinit_completion(&ar->scan.on_channel);
		ar->scan.state = ATH10K_SCAN_STARTING;
		ar->scan.is_roc = true;
		ar->scan.vdev_id = arvif->vdev_id;
		ar->scan.roc_freq = chan->center_freq;
		ret = 0;
		break;
	case ATH10K_SCAN_STARTING:
	case ATH10K_SCAN_RUNNING:
	case ATH10K_SCAN_ABORTING:
		ret = -EBUSY;
		break;
	}
	spin_unlock_bh(&ar->data_lock);

	if (ret)
		goto exit;

	memset(&arg, 0, sizeof(arg));
	ath10k_wmi_start_scan_init(ar, &arg);
	arg.vdev_id = arvif->vdev_id;
	arg.scan_id = ATH10K_SCAN_ID;
	arg.n_channels = 1;
	arg.channels[0] = chan->center_freq;
	arg.dwell_time_active = duration;
	arg.dwell_time_passive = duration;
	arg.max_scan_time = 2 * duration;
	arg.scan_ctrl_flags |= WMI_SCAN_FLAG_PASSIVE;
	arg.scan_ctrl_flags |= WMI_SCAN_FILTER_PROBE_REQ;

	ret = ath10k_start_scan(ar, &arg);
	if (ret) {
		ath10k_warn(ar, "failed to start roc scan: %d\n", ret);
		spin_lock_bh(&ar->data_lock);
		ar->scan.state = ATH10K_SCAN_IDLE;
		spin_unlock_bh(&ar->data_lock);
		goto exit;
	}

	ret = wait_for_completion_timeout(&ar->scan.on_channel, 3*HZ);
	if (ret == 0) {
		ath10k_warn(ar, "failed to switch to channel for roc scan\n");

		ret = ath10k_scan_stop(ar);
		if (ret)
			ath10k_warn(ar, "failed to stop scan: %d\n", ret);

		ret = -ETIMEDOUT;
		goto exit;
	}

	ret = 0;
exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath10k_cancel_remain_on_channel(struct ieee80211_hw *hw)
{
	struct ath10k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);
	cancel_delayed_work_sync(&ar->scan.timeout);
	ath10k_scan_abort(ar);
	mutex_unlock(&ar->conf_mutex);

	return 0;
}

/*
 * Both RTS and Fragmentation threshold are interface-specific
 * in ath10k, but device-specific in mac80211.
 */

static int ath10k_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif;
	int ret = 0;

	mutex_lock(&ar->conf_mutex);
	list_for_each_entry(arvif, &ar->arvifs, list) {
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d rts threshold %d\n",
			   arvif->vdev_id, value);

		ret = ath10k_mac_set_rts(arvif, value);
		if (ret) {
			ath10k_warn(ar, "failed to set rts threshold for vdev %d: %d\n",
				    arvif->vdev_id, ret);
			break;
		}
	}
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static int ath10k_set_frag_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif;
	int ret = 0;

	mutex_lock(&ar->conf_mutex);
	list_for_each_entry(arvif, &ar->arvifs, list) {
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d fragmentation threshold %d\n",
			   arvif->vdev_id, value);

		ret = ath10k_mac_set_rts(arvif, value);
		if (ret) {
			ath10k_warn(ar, "failed to set fragmentation threshold for vdev %d: %d\n",
				    arvif->vdev_id, ret);
			break;
		}
	}
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static void ath10k_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 u32 queues, bool drop)
{
	struct ath10k *ar = hw->priv;
	bool skip;
	int ret;

	/* mac80211 doesn't care if we really xmit queued frames or not
	 * we'll collect those frames either way if we stop/delete vdevs */
	if (drop)
		return;

	mutex_lock(&ar->conf_mutex);

	if (ar->state == ATH10K_STATE_WEDGED)
		goto skip;

	ret = wait_event_timeout(ar->htt.empty_tx_wq, ({
			bool empty;

			spin_lock_bh(&ar->htt.tx_lock);
			empty = (ar->htt.num_pending_tx == 0);
			spin_unlock_bh(&ar->htt.tx_lock);

			skip = (ar->state == ATH10K_STATE_WEDGED);

			(empty || skip);
		}), ATH10K_FLUSH_TIMEOUT_HZ);

	if (ret <= 0 || skip)
		ath10k_warn(ar, "failed to flush transmit queue (skip %i ar-state %i): %i\n",
			    skip, ar->state, ret);

skip:
	mutex_unlock(&ar->conf_mutex);
}

/* TODO: Implement this function properly
 * For now it is needed to reply to Probe Requests in IBSS mode.
 * Propably we need this information from FW.
 */
static int ath10k_tx_last_beacon(struct ieee80211_hw *hw)
{
	return 1;
}

#ifdef CONFIG_PM
static int ath10k_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan)
{
	struct ath10k *ar = hw->priv;
	int ret;

	mutex_lock(&ar->conf_mutex);

	ret = ath10k_wait_for_suspend(ar, WMI_PDEV_SUSPEND);
	if (ret) {
		if (ret == -ETIMEDOUT)
			goto resume;
		ret = 1;
		goto exit;
	}

	ret = ath10k_hif_suspend(ar);
	if (ret) {
		ath10k_warn(ar, "failed to suspend hif: %d\n", ret);
		goto resume;
	}

	ret = 0;
	goto exit;
resume:
	ret = ath10k_wmi_pdev_resume_target(ar);
	if (ret)
		ath10k_warn(ar, "failed to resume target: %d\n", ret);

	ret = 1;
exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath10k_resume(struct ieee80211_hw *hw)
{
	struct ath10k *ar = hw->priv;
	int ret;

	mutex_lock(&ar->conf_mutex);

	ret = ath10k_hif_resume(ar);
	if (ret) {
		ath10k_warn(ar, "failed to resume hif: %d\n", ret);
		ret = 1;
		goto exit;
	}

	ret = ath10k_wmi_pdev_resume_target(ar);
	if (ret) {
		ath10k_warn(ar, "failed to resume target: %d\n", ret);
		ret = 1;
		goto exit;
	}

	ret = 0;
exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}
#endif

static void ath10k_restart_complete(struct ieee80211_hw *hw)
{
	struct ath10k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);

	/* If device failed to restart it will be in a different state, e.g.
	 * ATH10K_STATE_WEDGED */
	if (ar->state == ATH10K_STATE_RESTARTED) {
		ath10k_info(ar, "device successfully recovered\n");
		ar->state = ATH10K_STATE_ON;
	}

	mutex_unlock(&ar->conf_mutex);
}

static int ath10k_get_survey(struct ieee80211_hw *hw, int idx,
			     struct survey_info *survey)
{
	struct ath10k *ar = hw->priv;
	struct ieee80211_supported_band *sband;
	struct survey_info *ar_survey = &ar->survey[idx];
	int ret = 0;

	mutex_lock(&ar->conf_mutex);

	sband = hw->wiphy->bands[IEEE80211_BAND_2GHZ];
	if (sband && idx >= sband->n_channels) {
		idx -= sband->n_channels;
		sband = NULL;
	}

	if (!sband)
		sband = hw->wiphy->bands[IEEE80211_BAND_5GHZ];

	if (!sband || idx >= sband->n_channels) {
		ret = -ENOENT;
		goto exit;
	}

	spin_lock_bh(&ar->data_lock);
	memcpy(survey, ar_survey, sizeof(*survey));
	spin_unlock_bh(&ar->data_lock);

	survey->channel = &sband->channels[idx];

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

/* Helper table for legacy fixed_rate/bitrate_mask */
static const u8 cck_ofdm_rate[] = {
	/* CCK */
	3, /* 1Mbps */
	2, /* 2Mbps */
	1, /* 5.5Mbps */
	0, /* 11Mbps */
	/* OFDM */
	3, /* 6Mbps */
	7, /* 9Mbps */
	2, /* 12Mbps */
	6, /* 18Mbps */
	1, /* 24Mbps */
	5, /* 36Mbps */
	0, /* 48Mbps */
	4, /* 54Mbps */
};

/* Check if only one bit set */
static int ath10k_check_single_mask(u32 mask)
{
	int bit;

	bit = ffs(mask);
	if (!bit)
		return 0;

	mask &= ~BIT(bit - 1);
	if (mask)
		return 2;

	return 1;
}

static bool
ath10k_default_bitrate_mask(struct ath10k *ar,
			    enum ieee80211_band band,
			    const struct cfg80211_bitrate_mask *mask)
{
	u32 legacy = 0x00ff;
	u8 ht = 0xff, i;
	u16 vht = 0x3ff;

	switch (band) {
	case IEEE80211_BAND_2GHZ:
		legacy = 0x00fff;
		vht = 0;
		break;
	case IEEE80211_BAND_5GHZ:
		break;
	default:
		return false;
	}

	if (mask->control[band].legacy != legacy)
		return false;

	for (i = 0; i < ar->num_rf_chains; i++)
		if (mask->control[band].ht_mcs[i] != ht)
			return false;

	for (i = 0; i < ar->num_rf_chains; i++)
		if (mask->control[band].vht_mcs[i] != vht)
			return false;

	return true;
}

static bool
ath10k_bitrate_mask_nss(const struct cfg80211_bitrate_mask *mask,
			enum ieee80211_band band,
			u8 *fixed_nss)
{
	int ht_nss = 0, vht_nss = 0, i;

	/* check legacy */
	if (ath10k_check_single_mask(mask->control[band].legacy))
		return false;

	/* check HT */
	for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
		if (mask->control[band].ht_mcs[i] == 0xff)
			continue;
		else if (mask->control[band].ht_mcs[i] == 0x00)
			break;

		return false;
	}

	ht_nss = i;

	/* check VHT */
	for (i = 0; i < NL80211_VHT_NSS_MAX; i++) {
		if (mask->control[band].vht_mcs[i] == 0x03ff)
			continue;
		else if (mask->control[band].vht_mcs[i] == 0x0000)
			break;

		return false;
	}

	vht_nss = i;

	if (ht_nss > 0 && vht_nss > 0)
		return false;

	if (ht_nss)
		*fixed_nss = ht_nss;
	else if (vht_nss)
		*fixed_nss = vht_nss;
	else
		return false;

	return true;
}

static bool
ath10k_bitrate_mask_correct(const struct cfg80211_bitrate_mask *mask,
			    enum ieee80211_band band,
			    enum wmi_rate_preamble *preamble)
{
	int legacy = 0, ht = 0, vht = 0, i;

	*preamble = WMI_RATE_PREAMBLE_OFDM;

	/* check legacy */
	legacy = ath10k_check_single_mask(mask->control[band].legacy);
	if (legacy > 1)
		return false;

	/* check HT */
	for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++)
		ht += ath10k_check_single_mask(mask->control[band].ht_mcs[i]);
	if (ht > 1)
		return false;

	/* check VHT */
	for (i = 0; i < NL80211_VHT_NSS_MAX; i++)
		vht += ath10k_check_single_mask(mask->control[band].vht_mcs[i]);
	if (vht > 1)
		return false;

	/* Currently we support only one fixed_rate */
	if ((legacy + ht + vht) != 1)
		return false;

	if (ht)
		*preamble = WMI_RATE_PREAMBLE_HT;
	else if (vht)
		*preamble = WMI_RATE_PREAMBLE_VHT;

	return true;
}

static bool
ath10k_bitrate_mask_rate(struct ath10k *ar,
			 const struct cfg80211_bitrate_mask *mask,
			 enum ieee80211_band band,
			 u8 *fixed_rate,
			 u8 *fixed_nss)
{
	u8 rate = 0, pream = 0, nss = 0, i;
	enum wmi_rate_preamble preamble;

	/* Check if single rate correct */
	if (!ath10k_bitrate_mask_correct(mask, band, &preamble))
		return false;

	pream = preamble;

	switch (preamble) {
	case WMI_RATE_PREAMBLE_CCK:
	case WMI_RATE_PREAMBLE_OFDM:
		i = ffs(mask->control[band].legacy) - 1;

		if (band == IEEE80211_BAND_2GHZ && i < 4)
			pream = WMI_RATE_PREAMBLE_CCK;

		if (band == IEEE80211_BAND_5GHZ)
			i += 4;

		if (i >= ARRAY_SIZE(cck_ofdm_rate))
			return false;

		rate = cck_ofdm_rate[i];
		break;
	case WMI_RATE_PREAMBLE_HT:
		for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++)
			if (mask->control[band].ht_mcs[i])
				break;

		if (i == IEEE80211_HT_MCS_MASK_LEN)
			return false;

		rate = ffs(mask->control[band].ht_mcs[i]) - 1;
		nss = i;
		break;
	case WMI_RATE_PREAMBLE_VHT:
		for (i = 0; i < NL80211_VHT_NSS_MAX; i++)
			if (mask->control[band].vht_mcs[i])
				break;

		if (i == NL80211_VHT_NSS_MAX)
			return false;

		rate = ffs(mask->control[band].vht_mcs[i]) - 1;
		nss = i;
		break;
	}

	*fixed_nss = nss + 1;
	nss <<= 4;
	pream <<= 6;

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac fixed rate pream 0x%02x nss 0x%02x rate 0x%02x\n",
		   pream, nss, rate);

	*fixed_rate = pream | nss | rate;

	return true;
}

static bool ath10k_get_fixed_rate_nss(struct ath10k *ar,
				      const struct cfg80211_bitrate_mask *mask,
				      enum ieee80211_band band,
				      u8 *fixed_rate,
				      u8 *fixed_nss)
{
	/* First check full NSS mask, if we can simply limit NSS */
	if (ath10k_bitrate_mask_nss(mask, band, fixed_nss))
		return true;

	/* Next Check single rate is set */
	return ath10k_bitrate_mask_rate(ar, mask, band, fixed_rate, fixed_nss);
}

static int ath10k_set_fixed_rate_param(struct ath10k_vif *arvif,
				       u8 fixed_rate,
				       u8 fixed_nss,
				       u8 force_sgi)
{
	struct ath10k *ar = arvif->ar;
	u32 vdev_param;
	int ret = 0;

	mutex_lock(&ar->conf_mutex);

	if (arvif->fixed_rate == fixed_rate &&
	    arvif->fixed_nss == fixed_nss &&
	    arvif->force_sgi == force_sgi)
		goto exit;

	if (fixed_rate == WMI_FIXED_RATE_NONE)
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac disable fixed bitrate mask\n");

	if (force_sgi)
		ath10k_dbg(ar, ATH10K_DBG_MAC, "mac force sgi\n");

	vdev_param = ar->wmi.vdev_param->fixed_rate;
	ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id,
					vdev_param, fixed_rate);
	if (ret) {
		ath10k_warn(ar, "failed to set fixed rate param 0x%02x: %d\n",
			    fixed_rate, ret);
		ret = -EINVAL;
		goto exit;
	}

	arvif->fixed_rate = fixed_rate;

	vdev_param = ar->wmi.vdev_param->nss;
	ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id,
					vdev_param, fixed_nss);

	if (ret) {
		ath10k_warn(ar, "failed to set fixed nss param %d: %d\n",
			    fixed_nss, ret);
		ret = -EINVAL;
		goto exit;
	}

	arvif->fixed_nss = fixed_nss;

	vdev_param = ar->wmi.vdev_param->sgi;
	ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
					force_sgi);

	if (ret) {
		ath10k_warn(ar, "failed to set sgi param %d: %d\n",
			    force_sgi, ret);
		ret = -EINVAL;
		goto exit;
	}

	arvif->force_sgi = force_sgi;

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath10k_set_bitrate_mask(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   const struct cfg80211_bitrate_mask *mask)
{
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);
	struct ath10k *ar = arvif->ar;
	enum ieee80211_band band = ar->hw->conf.chandef.chan->band;
	u8 fixed_rate = WMI_FIXED_RATE_NONE;
	u8 fixed_nss = ar->num_rf_chains;
	u8 force_sgi;

	force_sgi = mask->control[band].gi;
	if (force_sgi == NL80211_TXRATE_FORCE_LGI)
		return -EINVAL;

	if (!ath10k_default_bitrate_mask(ar, band, mask)) {
		if (!ath10k_get_fixed_rate_nss(ar, mask, band,
					       &fixed_rate,
					       &fixed_nss))
			return -EINVAL;
	}

	if (fixed_rate == WMI_FIXED_RATE_NONE && force_sgi) {
		ath10k_warn(ar, "failed to force SGI usage for default rate settings\n");
		return -EINVAL;
	}

	return ath10k_set_fixed_rate_param(arvif, fixed_rate,
					   fixed_nss, force_sgi);
}

static void ath10k_sta_rc_update(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 u32 changed)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	u32 bw, smps;

	spin_lock_bh(&ar->data_lock);

	ath10k_dbg(ar, ATH10K_DBG_MAC,
		   "mac sta rc update for %pM changed %08x bw %d nss %d smps %d\n",
		   sta->addr, changed, sta->bandwidth, sta->rx_nss,
		   sta->smps_mode);

	if (changed & IEEE80211_RC_BW_CHANGED) {
		bw = WMI_PEER_CHWIDTH_20MHZ;

		switch (sta->bandwidth) {
		case IEEE80211_STA_RX_BW_20:
			bw = WMI_PEER_CHWIDTH_20MHZ;
			break;
		case IEEE80211_STA_RX_BW_40:
			bw = WMI_PEER_CHWIDTH_40MHZ;
			break;
		case IEEE80211_STA_RX_BW_80:
			bw = WMI_PEER_CHWIDTH_80MHZ;
			break;
		case IEEE80211_STA_RX_BW_160:
			ath10k_warn(ar, "Invalid bandwith %d in rc update for %pM\n",
				    sta->bandwidth, sta->addr);
			bw = WMI_PEER_CHWIDTH_20MHZ;
			break;
		}

		arsta->bw = bw;
	}

	if (changed & IEEE80211_RC_NSS_CHANGED)
		arsta->nss = sta->rx_nss;

	if (changed & IEEE80211_RC_SMPS_CHANGED) {
		smps = WMI_PEER_SMPS_PS_NONE;

		switch (sta->smps_mode) {
		case IEEE80211_SMPS_AUTOMATIC:
		case IEEE80211_SMPS_OFF:
			smps = WMI_PEER_SMPS_PS_NONE;
			break;
		case IEEE80211_SMPS_STATIC:
			smps = WMI_PEER_SMPS_STATIC;
			break;
		case IEEE80211_SMPS_DYNAMIC:
			smps = WMI_PEER_SMPS_DYNAMIC;
			break;
		case IEEE80211_SMPS_NUM_MODES:
			ath10k_warn(ar, "Invalid smps %d in sta rc update for %pM\n",
				    sta->smps_mode, sta->addr);
			smps = WMI_PEER_SMPS_PS_NONE;
			break;
		}

		arsta->smps = smps;
	}

	arsta->changed |= changed;

	spin_unlock_bh(&ar->data_lock);

	ieee80211_queue_work(hw, &arsta->update_wk);
}

static u64 ath10k_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	/*
	 * FIXME: Return 0 for time being. Need to figure out whether FW
	 * has the API to fetch 64-bit local TSF
	 */

	return 0;
}

static int ath10k_ampdu_action(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       enum ieee80211_ampdu_mlme_action action,
			       struct ieee80211_sta *sta, u16 tid, u16 *ssn,
			       u8 buf_size)
{
	struct ath10k *ar = hw->priv;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);

	ath10k_dbg(ar, ATH10K_DBG_MAC, "mac ampdu vdev_id %i sta %pM tid %hu action %d\n",
		   arvif->vdev_id, sta->addr, tid, action);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		/* HTT AddBa/DelBa events trigger mac80211 Rx BA session
		 * creation/removal. Do we need to verify this?
		 */
		return 0;
	case IEEE80211_AMPDU_TX_START:
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		/* Firmware offloads Tx aggregation entirely so deny mac80211
		 * Tx aggregation requests.
		 */
		return -EOPNOTSUPP;
	}

	return -EINVAL;
}

static const struct ieee80211_ops ath10k_ops = {
	.tx				= ath10k_tx,
	.start				= ath10k_start,
	.stop				= ath10k_stop,
	.config				= ath10k_config,
	.add_interface			= ath10k_add_interface,
	.remove_interface		= ath10k_remove_interface,
	.configure_filter		= ath10k_configure_filter,
	.bss_info_changed		= ath10k_bss_info_changed,
	.hw_scan			= ath10k_hw_scan,
	.cancel_hw_scan			= ath10k_cancel_hw_scan,
	.set_key			= ath10k_set_key,
	.sta_state			= ath10k_sta_state,
	.conf_tx			= ath10k_conf_tx,
	.remain_on_channel		= ath10k_remain_on_channel,
	.cancel_remain_on_channel	= ath10k_cancel_remain_on_channel,
	.set_rts_threshold		= ath10k_set_rts_threshold,
	.set_frag_threshold		= ath10k_set_frag_threshold,
	.flush				= ath10k_flush,
	.tx_last_beacon			= ath10k_tx_last_beacon,
	.set_antenna			= ath10k_set_antenna,
	.get_antenna			= ath10k_get_antenna,
	.restart_complete		= ath10k_restart_complete,
	.get_survey			= ath10k_get_survey,
	.set_bitrate_mask		= ath10k_set_bitrate_mask,
	.sta_rc_update			= ath10k_sta_rc_update,
	.get_tsf			= ath10k_get_tsf,
	.ampdu_action			= ath10k_ampdu_action,

	CFG80211_TESTMODE_CMD(ath10k_tm_cmd)

#ifdef CONFIG_PM
	.suspend			= ath10k_suspend,
	.resume				= ath10k_resume,
#endif
};

#define RATETAB_ENT(_rate, _rateid, _flags) { \
	.bitrate		= (_rate), \
	.flags			= (_flags), \
	.hw_value		= (_rateid), \
}

#define CHAN2G(_channel, _freq, _flags) { \
	.band			= IEEE80211_BAND_2GHZ, \
	.hw_value		= (_channel), \
	.center_freq		= (_freq), \
	.flags			= (_flags), \
	.max_antenna_gain	= 0, \
	.max_power		= 30, \
}

#define CHAN5G(_channel, _freq, _flags) { \
	.band			= IEEE80211_BAND_5GHZ, \
	.hw_value		= (_channel), \
	.center_freq		= (_freq), \
	.flags			= (_flags), \
	.max_antenna_gain	= 0, \
	.max_power		= 30, \
}

static const struct ieee80211_channel ath10k_2ghz_channels[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

static const struct ieee80211_channel ath10k_5ghz_channels[] = {
	CHAN5G(36, 5180, 0),
	CHAN5G(40, 5200, 0),
	CHAN5G(44, 5220, 0),
	CHAN5G(48, 5240, 0),
	CHAN5G(52, 5260, 0),
	CHAN5G(56, 5280, 0),
	CHAN5G(60, 5300, 0),
	CHAN5G(64, 5320, 0),
	CHAN5G(100, 5500, 0),
	CHAN5G(104, 5520, 0),
	CHAN5G(108, 5540, 0),
	CHAN5G(112, 5560, 0),
	CHAN5G(116, 5580, 0),
	CHAN5G(120, 5600, 0),
	CHAN5G(124, 5620, 0),
	CHAN5G(128, 5640, 0),
	CHAN5G(132, 5660, 0),
	CHAN5G(136, 5680, 0),
	CHAN5G(140, 5700, 0),
	CHAN5G(149, 5745, 0),
	CHAN5G(153, 5765, 0),
	CHAN5G(157, 5785, 0),
	CHAN5G(161, 5805, 0),
	CHAN5G(165, 5825, 0),
};

static struct ieee80211_rate ath10k_rates[] = {
	/* CCK */
	RATETAB_ENT(10,  0x82, 0),
	RATETAB_ENT(20,  0x84, 0),
	RATETAB_ENT(55,  0x8b, 0),
	RATETAB_ENT(110, 0x96, 0),
	/* OFDM */
	RATETAB_ENT(60,  0x0c, 0),
	RATETAB_ENT(90,  0x12, 0),
	RATETAB_ENT(120, 0x18, 0),
	RATETAB_ENT(180, 0x24, 0),
	RATETAB_ENT(240, 0x30, 0),
	RATETAB_ENT(360, 0x48, 0),
	RATETAB_ENT(480, 0x60, 0),
	RATETAB_ENT(540, 0x6c, 0),
};

#define ath10k_a_rates (ath10k_rates + 4)
#define ath10k_a_rates_size (ARRAY_SIZE(ath10k_rates) - 4)
#define ath10k_g_rates (ath10k_rates + 0)
#define ath10k_g_rates_size (ARRAY_SIZE(ath10k_rates))

struct ath10k *ath10k_mac_create(size_t priv_size)
{
	struct ieee80211_hw *hw;
	struct ath10k *ar;

	hw = ieee80211_alloc_hw(sizeof(struct ath10k) + priv_size, &ath10k_ops);
	if (!hw)
		return NULL;

	ar = hw->priv;
	ar->hw = hw;

	return ar;
}

void ath10k_mac_destroy(struct ath10k *ar)
{
	ieee80211_free_hw(ar->hw);
}

static const struct ieee80211_iface_limit ath10k_if_limits[] = {
	{
	.max	= 8,
	.types	= BIT(NL80211_IFTYPE_STATION)
		| BIT(NL80211_IFTYPE_P2P_CLIENT)
	},
	{
	.max	= 3,
	.types	= BIT(NL80211_IFTYPE_P2P_GO)
	},
	{
	.max	= 7,
	.types	= BIT(NL80211_IFTYPE_AP)
	},
};

static const struct ieee80211_iface_limit ath10k_10x_if_limits[] = {
	{
	.max	= 8,
	.types	= BIT(NL80211_IFTYPE_AP)
	},
};

static const struct ieee80211_iface_combination ath10k_if_comb[] = {
	{
		.limits = ath10k_if_limits,
		.n_limits = ARRAY_SIZE(ath10k_if_limits),
		.max_interfaces = 8,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
	},
};

static const struct ieee80211_iface_combination ath10k_10x_if_comb[] = {
	{
		.limits = ath10k_10x_if_limits,
		.n_limits = ARRAY_SIZE(ath10k_10x_if_limits),
		.max_interfaces = 8,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
#ifdef CONFIG_ATH10K_DFS_CERTIFIED
		.radar_detect_widths =	BIT(NL80211_CHAN_WIDTH_20_NOHT) |
					BIT(NL80211_CHAN_WIDTH_20) |
					BIT(NL80211_CHAN_WIDTH_40) |
					BIT(NL80211_CHAN_WIDTH_80),
#endif
	},
};

static struct ieee80211_sta_vht_cap ath10k_create_vht_cap(struct ath10k *ar)
{
	struct ieee80211_sta_vht_cap vht_cap = {0};
	u16 mcs_map;
	int i;

	vht_cap.vht_supported = 1;
	vht_cap.cap = ar->vht_cap_info;

	mcs_map = 0;
	for (i = 0; i < 8; i++) {
		if (i < ar->num_rf_chains)
			mcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << (i*2);
		else
			mcs_map |= IEEE80211_VHT_MCS_NOT_SUPPORTED << (i*2);
	}

	vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(mcs_map);
	vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(mcs_map);

	return vht_cap;
}

static struct ieee80211_sta_ht_cap ath10k_get_ht_cap(struct ath10k *ar)
{
	int i;
	struct ieee80211_sta_ht_cap ht_cap = {0};

	if (!(ar->ht_cap_info & WMI_HT_CAP_ENABLED))
		return ht_cap;

	ht_cap.ht_supported = 1;
	ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_8;
	ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	ht_cap.cap |= IEEE80211_HT_CAP_DSSSCCK40;
	ht_cap.cap |= WLAN_HT_CAP_SM_PS_STATIC << IEEE80211_HT_CAP_SM_PS_SHIFT;

	if (ar->ht_cap_info & WMI_HT_CAP_HT20_SGI)
		ht_cap.cap |= IEEE80211_HT_CAP_SGI_20;

	if (ar->ht_cap_info & WMI_HT_CAP_HT40_SGI)
		ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;

	if (ar->ht_cap_info & WMI_HT_CAP_DYNAMIC_SMPS) {
		u32 smps;

		smps   = WLAN_HT_CAP_SM_PS_DYNAMIC;
		smps <<= IEEE80211_HT_CAP_SM_PS_SHIFT;

		ht_cap.cap |= smps;
	}

	if (ar->ht_cap_info & WMI_HT_CAP_TX_STBC)
		ht_cap.cap |= IEEE80211_HT_CAP_TX_STBC;

	if (ar->ht_cap_info & WMI_HT_CAP_RX_STBC) {
		u32 stbc;

		stbc   = ar->ht_cap_info;
		stbc  &= WMI_HT_CAP_RX_STBC;
		stbc >>= WMI_HT_CAP_RX_STBC_MASK_SHIFT;
		stbc <<= IEEE80211_HT_CAP_RX_STBC_SHIFT;
		stbc  &= IEEE80211_HT_CAP_RX_STBC;

		ht_cap.cap |= stbc;
	}

	if (ar->ht_cap_info & WMI_HT_CAP_LDPC)
		ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;

	if (ar->ht_cap_info & WMI_HT_CAP_L_SIG_TXOP_PROT)
		ht_cap.cap |= IEEE80211_HT_CAP_LSIG_TXOP_PROT;

	/* max AMSDU is implicitly taken from vht_cap_info */
	if (ar->vht_cap_info & WMI_VHT_CAP_MAX_MPDU_LEN_MASK)
		ht_cap.cap |= IEEE80211_HT_CAP_MAX_AMSDU;

	for (i = 0; i < ar->num_rf_chains; i++)
		ht_cap.mcs.rx_mask[i] = 0xFF;

	ht_cap.mcs.tx_params |= IEEE80211_HT_MCS_TX_DEFINED;

	return ht_cap;
}

static void ath10k_get_arvif_iter(void *data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	struct ath10k_vif_iter *arvif_iter = data;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);

	if (arvif->vdev_id == arvif_iter->vdev_id)
		arvif_iter->arvif = arvif;
}

struct ath10k_vif *ath10k_get_arvif(struct ath10k *ar, u32 vdev_id)
{
	struct ath10k_vif_iter arvif_iter;
	u32 flags;

	memset(&arvif_iter, 0, sizeof(struct ath10k_vif_iter));
	arvif_iter.vdev_id = vdev_id;

	flags = IEEE80211_IFACE_ITER_RESUME_ALL;
	ieee80211_iterate_active_interfaces_atomic(ar->hw,
						   flags,
						   ath10k_get_arvif_iter,
						   &arvif_iter);
	if (!arvif_iter.arvif) {
		ath10k_warn(ar, "No VIF found for vdev %d\n", vdev_id);
		return NULL;
	}

	return arvif_iter.arvif;
}

int ath10k_mac_register(struct ath10k *ar)
{
	struct ieee80211_supported_band *band;
	struct ieee80211_sta_vht_cap vht_cap;
	struct ieee80211_sta_ht_cap ht_cap;
	void *channels;
	int ret;

	SET_IEEE80211_PERM_ADDR(ar->hw, ar->mac_addr);

	SET_IEEE80211_DEV(ar->hw, ar->dev);

	ht_cap = ath10k_get_ht_cap(ar);
	vht_cap = ath10k_create_vht_cap(ar);

	if (ar->phy_capability & WHAL_WLAN_11G_CAPABILITY) {
		channels = kmemdup(ath10k_2ghz_channels,
				   sizeof(ath10k_2ghz_channels),
				   GFP_KERNEL);
		if (!channels) {
			ret = -ENOMEM;
			goto err_free;
		}

		band = &ar->mac.sbands[IEEE80211_BAND_2GHZ];
		band->n_channels = ARRAY_SIZE(ath10k_2ghz_channels);
		band->channels = channels;
		band->n_bitrates = ath10k_g_rates_size;
		band->bitrates = ath10k_g_rates;
		band->ht_cap = ht_cap;

		/* vht is not supported in 2.4 GHz */

		ar->hw->wiphy->bands[IEEE80211_BAND_2GHZ] = band;
	}

	if (ar->phy_capability & WHAL_WLAN_11A_CAPABILITY) {
		channels = kmemdup(ath10k_5ghz_channels,
				   sizeof(ath10k_5ghz_channels),
				   GFP_KERNEL);
		if (!channels) {
			ret = -ENOMEM;
			goto err_free;
		}

		band = &ar->mac.sbands[IEEE80211_BAND_5GHZ];
		band->n_channels = ARRAY_SIZE(ath10k_5ghz_channels);
		band->channels = channels;
		band->n_bitrates = ath10k_a_rates_size;
		band->bitrates = ath10k_a_rates;
		band->ht_cap = ht_cap;
		band->vht_cap = vht_cap;
		ar->hw->wiphy->bands[IEEE80211_BAND_5GHZ] = band;
	}

	ar->hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_AP);

	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features)) {
		/* TODO:  Have to deal with 2x2 chips if/when the come out. */
		ar->supp_tx_chainmask = TARGET_10X_TX_CHAIN_MASK;
		ar->supp_rx_chainmask = TARGET_10X_RX_CHAIN_MASK;
	} else {
		ar->supp_tx_chainmask = TARGET_TX_CHAIN_MASK;
		ar->supp_rx_chainmask = TARGET_RX_CHAIN_MASK;
	}

	ar->hw->wiphy->available_antennas_rx = ar->supp_rx_chainmask;
	ar->hw->wiphy->available_antennas_tx = ar->supp_tx_chainmask;

	if (!test_bit(ATH10K_FW_FEATURE_NO_P2P, ar->fw_features))
		ar->hw->wiphy->interface_modes |=
			BIT(NL80211_IFTYPE_P2P_CLIENT) |
			BIT(NL80211_IFTYPE_P2P_GO);

	ar->hw->flags = IEEE80211_HW_SIGNAL_DBM |
			IEEE80211_HW_SUPPORTS_PS |
			IEEE80211_HW_SUPPORTS_DYNAMIC_PS |
			IEEE80211_HW_SUPPORTS_UAPSD |
			IEEE80211_HW_MFP_CAPABLE |
			IEEE80211_HW_REPORTS_TX_ACK_STATUS |
			IEEE80211_HW_HAS_RATE_CONTROL |
			IEEE80211_HW_SUPPORTS_STATIC_SMPS |
			IEEE80211_HW_AP_LINK_PS |
			IEEE80211_HW_SPECTRUM_MGMT;

	/* MSDU can have HTT TX fragment pushed in front. The additional 4
	 * bytes is used for padding/alignment if necessary. */
	ar->hw->extra_tx_headroom += sizeof(struct htt_data_tx_desc_frag)*2 + 4;

	if (ar->ht_cap_info & WMI_HT_CAP_DYNAMIC_SMPS)
		ar->hw->flags |= IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS;

	if (ar->ht_cap_info & WMI_HT_CAP_ENABLED) {
		ar->hw->flags |= IEEE80211_HW_AMPDU_AGGREGATION;
		ar->hw->flags |= IEEE80211_HW_TX_AMPDU_SETUP_IN_HW;
	}

	ar->hw->wiphy->max_scan_ssids = WLAN_SCAN_PARAMS_MAX_SSID;
	ar->hw->wiphy->max_scan_ie_len = WLAN_SCAN_PARAMS_MAX_IE_LEN;

	ar->hw->vif_data_size = sizeof(struct ath10k_vif);
	ar->hw->sta_data_size = sizeof(struct ath10k_sta);

	ar->hw->max_listen_interval = ATH10K_MAX_HW_LISTEN_INTERVAL;

	ar->hw->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	ar->hw->wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	ar->hw->wiphy->max_remain_on_channel_duration = 5000;

	ar->hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	/*
	 * on LL hardware queues are managed entirely by the FW
	 * so we only advertise to mac we can do the queues thing
	 */
	ar->hw->queues = 4;

	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features)) {
		ar->hw->wiphy->iface_combinations = ath10k_10x_if_comb;
		ar->hw->wiphy->n_iface_combinations =
			ARRAY_SIZE(ath10k_10x_if_comb);
	} else {
		ar->hw->wiphy->iface_combinations = ath10k_if_comb;
		ar->hw->wiphy->n_iface_combinations =
			ARRAY_SIZE(ath10k_if_comb);

		ar->hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_ADHOC);
	}

	ar->hw->netdev_features = NETIF_F_HW_CSUM;

	if (config_enabled(CONFIG_ATH10K_DFS_CERTIFIED)) {
		/* Init ath dfs pattern detector */
		ar->ath_common.debug_mask = ATH_DBG_DFS;
		ar->dfs_detector = dfs_pattern_detector_init(&ar->ath_common,
							     NL80211_DFS_UNSET);

		if (!ar->dfs_detector)
			ath10k_warn(ar, "failed to initialise DFS pattern detector\n");
	}

	ret = ath_regd_init(&ar->ath_common.regulatory, ar->hw->wiphy,
			    ath10k_reg_notifier);
	if (ret) {
		ath10k_err(ar, "failed to initialise regulatory: %i\n", ret);
		goto err_free;
	}

	ret = ieee80211_register_hw(ar->hw);
	if (ret) {
		ath10k_err(ar, "failed to register ieee80211: %d\n", ret);
		goto err_free;
	}

	if (!ath_is_world_regd(&ar->ath_common.regulatory)) {
		ret = regulatory_hint(ar->hw->wiphy,
				      ar->ath_common.regulatory.alpha2);
		if (ret)
			goto err_unregister;
	}

	return 0;

err_unregister:
	ieee80211_unregister_hw(ar->hw);
err_free:
	kfree(ar->mac.sbands[IEEE80211_BAND_2GHZ].channels);
	kfree(ar->mac.sbands[IEEE80211_BAND_5GHZ].channels);

	return ret;
}

void ath10k_mac_unregister(struct ath10k *ar)
{
	ieee80211_unregister_hw(ar->hw);

	if (config_enabled(CONFIG_ATH10K_DFS_CERTIFIED) && ar->dfs_detector)
		ar->dfs_detector->exit(ar->dfs_detector);

	kfree(ar->mac.sbands[IEEE80211_BAND_2GHZ].channels);
	kfree(ar->mac.sbands[IEEE80211_BAND_5GHZ].channels);

	SET_IEEE80211_DEV(ar->hw, NULL);
}
