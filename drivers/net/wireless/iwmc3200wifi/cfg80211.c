/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>

#include "iwm.h"
#include "commands.h"
#include "cfg80211.h"
#include "debug.h"

#define RATETAB_ENT(_rate, _rateid, _flags) \
	{								\
		.bitrate	= (_rate),				\
		.hw_value	= (_rateid),				\
		.flags		= (_flags),				\
	}

#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq		= 5000 + (5 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

static struct ieee80211_rate iwm_rates[] = {
	RATETAB_ENT(10,  0x1,   0),
	RATETAB_ENT(20,  0x2,   0),
	RATETAB_ENT(55,  0x4,   0),
	RATETAB_ENT(110, 0x8,   0),
	RATETAB_ENT(60,  0x10,  0),
	RATETAB_ENT(90,  0x20,  0),
	RATETAB_ENT(120, 0x40,  0),
	RATETAB_ENT(180, 0x80,  0),
	RATETAB_ENT(240, 0x100, 0),
	RATETAB_ENT(360, 0x200, 0),
	RATETAB_ENT(480, 0x400, 0),
	RATETAB_ENT(540, 0x800, 0),
};

#define iwm_a_rates		(iwm_rates + 4)
#define iwm_a_rates_size	8
#define iwm_g_rates		(iwm_rates + 0)
#define iwm_g_rates_size	12

static struct ieee80211_channel iwm_2ghz_channels[] = {
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

static struct ieee80211_channel iwm_5ghz_a_channels[] = {
	CHAN5G(34, 0),		CHAN5G(36, 0),
	CHAN5G(38, 0),		CHAN5G(40, 0),
	CHAN5G(42, 0),		CHAN5G(44, 0),
	CHAN5G(46, 0),		CHAN5G(48, 0),
	CHAN5G(52, 0),		CHAN5G(56, 0),
	CHAN5G(60, 0),		CHAN5G(64, 0),
	CHAN5G(100, 0),		CHAN5G(104, 0),
	CHAN5G(108, 0),		CHAN5G(112, 0),
	CHAN5G(116, 0),		CHAN5G(120, 0),
	CHAN5G(124, 0),		CHAN5G(128, 0),
	CHAN5G(132, 0),		CHAN5G(136, 0),
	CHAN5G(140, 0),		CHAN5G(149, 0),
	CHAN5G(153, 0),		CHAN5G(157, 0),
	CHAN5G(161, 0),		CHAN5G(165, 0),
	CHAN5G(184, 0),		CHAN5G(188, 0),
	CHAN5G(192, 0),		CHAN5G(196, 0),
	CHAN5G(200, 0),		CHAN5G(204, 0),
	CHAN5G(208, 0),		CHAN5G(212, 0),
	CHAN5G(216, 0),
};

static struct ieee80211_supported_band iwm_band_2ghz = {
	.channels = iwm_2ghz_channels,
	.n_channels = ARRAY_SIZE(iwm_2ghz_channels),
	.bitrates = iwm_g_rates,
	.n_bitrates = iwm_g_rates_size,
};

static struct ieee80211_supported_band iwm_band_5ghz = {
	.channels = iwm_5ghz_a_channels,
	.n_channels = ARRAY_SIZE(iwm_5ghz_a_channels),
	.bitrates = iwm_a_rates,
	.n_bitrates = iwm_a_rates_size,
};

static int iwm_key_init(struct iwm_key *key, u8 key_index,
			const u8 *mac_addr, struct key_params *params)
{
	key->hdr.key_idx = key_index;
	if (!mac_addr || is_broadcast_ether_addr(mac_addr)) {
		key->hdr.multicast = 1;
		memset(key->hdr.mac, 0xff, ETH_ALEN);
	} else {
		key->hdr.multicast = 0;
		memcpy(key->hdr.mac, mac_addr, ETH_ALEN);
	}

	if (params) {
		if (params->key_len > WLAN_MAX_KEY_LEN ||
		    params->seq_len > IW_ENCODE_SEQ_MAX_SIZE)
			return -EINVAL;

		key->cipher = params->cipher;
		key->key_len = params->key_len;
		key->seq_len = params->seq_len;
		memcpy(key->key, params->key, key->key_len);
		memcpy(key->seq, params->seq, key->seq_len);
	}

	return 0;
}

static int iwm_reset_profile(struct iwm_priv *iwm)
{
	int ret;

	if (!iwm->umac_profile_active)
		return 0;

	/*
	 * If there is a current active profile, but no
	 * default key, it's not worth trying to associate again.
	 */
	if (iwm->default_key < 0)
		return 0;

	/*
	 * Here we have an active profile, but a key setting changed.
	 * We thus have to invalidate the current profile, and push the
	 * new one. Keys will be pushed when association takes place.
	 */
	ret = iwm_invalidate_mlme_profile(iwm);
	if (ret < 0) {
		IWM_ERR(iwm, "Couldn't invalidate profile\n");
		return ret;
	}

	return iwm_send_mlme_profile(iwm);
}

static int iwm_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev,
				u8 key_index, const u8 *mac_addr,
				struct key_params *params)
{
	struct iwm_priv *iwm = ndev_to_iwm(ndev);
	struct iwm_key *key = &iwm->keys[key_index];
	int ret;

	IWM_DBG_WEXT(iwm, DBG, "Adding key for %pM\n", mac_addr);

	memset(key, 0, sizeof(struct iwm_key));
	ret = iwm_key_init(key, key_index, mac_addr, params);
	if (ret < 0) {
		IWM_ERR(iwm, "Invalid key_params\n");
		return ret;
	}

	/*
	 * The WEP keys can be set before or after setting the essid.
	 * We need to handle both cases by simply pushing the keys after
	 * we send the profile.
	 * If the profile is not set yet (i.e. we're pushing keys before
	 * the essid), we set the cipher appropriately.
	 * If the profile is set, we havent associated yet because our
	 * cipher was incorrectly set. So we invalidate and send the
	 * profile again.
	 */
	if (key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	    key->cipher == WLAN_CIPHER_SUITE_WEP104) {
		u8 *ucast_cipher = &iwm->umac_profile->sec.ucast_cipher;
		u8 *mcast_cipher = &iwm->umac_profile->sec.mcast_cipher;

		IWM_DBG_WEXT(iwm, DBG, "WEP key\n");

		if (key->cipher == WLAN_CIPHER_SUITE_WEP40)
			*ucast_cipher = *mcast_cipher = UMAC_CIPHER_TYPE_WEP_40;
		if (key->cipher == WLAN_CIPHER_SUITE_WEP104)
			*ucast_cipher = *mcast_cipher =
				UMAC_CIPHER_TYPE_WEP_104;

		return iwm_reset_profile(iwm);
	}

	return iwm_set_key(iwm, 0, key);
}

static int iwm_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev,
				u8 key_index, const u8 *mac_addr, void *cookie,
				void (*callback)(void *cookie,
						 struct key_params*))
{
	struct iwm_priv *iwm = ndev_to_iwm(ndev);
	struct iwm_key *key = &iwm->keys[key_index];
	struct key_params params;

	IWM_DBG_WEXT(iwm, DBG, "Getting key %d\n", key_index);

	memset(&params, 0, sizeof(params));

	params.cipher = key->cipher;
	params.key_len = key->key_len;
	params.seq_len = key->seq_len;
	params.seq = key->seq;
	params.key = key->key;

	callback(cookie, &params);

	return key->key_len ? 0 : -ENOENT;
}


static int iwm_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev,
				u8 key_index, const u8 *mac_addr)
{
	struct iwm_priv *iwm = ndev_to_iwm(ndev);
	struct iwm_key *key = &iwm->keys[key_index];

	if (!iwm->keys[key_index].key_len) {
		IWM_DBG_WEXT(iwm, DBG, "Key %d not used\n", key_index);
		return 0;
	}

	if (key_index == iwm->default_key)
		iwm->default_key = -1;

	/* If the interface is down, we just cache this */
	if (!test_bit(IWM_STATUS_READY, &iwm->status))
		return 0;

	return iwm_set_key(iwm, 1, key);
}

static int iwm_cfg80211_set_default_key(struct wiphy *wiphy,
					struct net_device *ndev,
					u8 key_index)
{
	struct iwm_priv *iwm = ndev_to_iwm(ndev);
	int ret;

	IWM_DBG_WEXT(iwm, DBG, "Default key index is: %d\n", key_index);

	if (!iwm->keys[key_index].key_len) {
		IWM_ERR(iwm, "Key %d not used\n", key_index);
		return -EINVAL;
	}

	iwm->default_key = key_index;

	/* If the interface is down, we just cache this */
	if (!test_bit(IWM_STATUS_READY, &iwm->status))
		return 0;

	ret = iwm_set_tx_key(iwm, key_index);
	if (ret < 0)
		return ret;

	return iwm_reset_profile(iwm);
}


int iwm_cfg80211_inform_bss(struct iwm_priv *iwm)
{
	struct wiphy *wiphy = iwm_to_wiphy(iwm);
	struct iwm_bss_info *bss, *next;
	struct iwm_umac_notif_bss_info *umac_bss;
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_channel *channel;
	struct ieee80211_supported_band *band;
	s32 signal;
	int freq;

	list_for_each_entry_safe(bss, next, &iwm->bss_list, node) {
		umac_bss = bss->bss;
		mgmt = (struct ieee80211_mgmt *)(umac_bss->frame_buf);

		if (umac_bss->band == UMAC_BAND_2GHZ)
			band = wiphy->bands[IEEE80211_BAND_2GHZ];
		else if (umac_bss->band == UMAC_BAND_5GHZ)
			band = wiphy->bands[IEEE80211_BAND_5GHZ];
		else {
			IWM_ERR(iwm, "Invalid band: %d\n", umac_bss->band);
			return -EINVAL;
		}

		freq = ieee80211_channel_to_frequency(umac_bss->channel);
		channel = ieee80211_get_channel(wiphy, freq);
		signal = umac_bss->rssi * 100;

		if (!cfg80211_inform_bss_frame(wiphy, channel, mgmt,
					       le16_to_cpu(umac_bss->frame_len),
					       signal, GFP_KERNEL))
			return -EINVAL;
	}

	return 0;
}

static int iwm_cfg80211_change_iface(struct wiphy *wiphy,
				     struct net_device *ndev,
				     enum nl80211_iftype type, u32 *flags,
				     struct vif_params *params)
{
	struct wireless_dev *wdev;
	struct iwm_priv *iwm;
	u32 old_mode;

	wdev = ndev->ieee80211_ptr;
	iwm = ndev_to_iwm(ndev);
	old_mode = iwm->conf.mode;

	switch (type) {
	case NL80211_IFTYPE_STATION:
		iwm->conf.mode = UMAC_MODE_BSS;
		break;
	case NL80211_IFTYPE_ADHOC:
		iwm->conf.mode = UMAC_MODE_IBSS;
		break;
	default:
		return -EOPNOTSUPP;
	}

	wdev->iftype = type;

	if ((old_mode == iwm->conf.mode) || !iwm->umac_profile)
		return 0;

	iwm->umac_profile->mode = cpu_to_le32(iwm->conf.mode);

	if (iwm->umac_profile_active) {
		int ret = iwm_invalidate_mlme_profile(iwm);
		if (ret < 0)
			IWM_ERR(iwm, "Couldn't invalidate profile\n");
	}

	return 0;
}

static int iwm_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
			     struct cfg80211_scan_request *request)
{
	struct iwm_priv *iwm = ndev_to_iwm(ndev);
	int ret;

	if (!test_bit(IWM_STATUS_READY, &iwm->status)) {
		IWM_ERR(iwm, "Scan while device is not ready\n");
		return -EIO;
	}

	if (test_bit(IWM_STATUS_SCANNING, &iwm->status)) {
		IWM_ERR(iwm, "Scanning already\n");
		return -EAGAIN;
	}

	if (test_bit(IWM_STATUS_SCAN_ABORTING, &iwm->status)) {
		IWM_ERR(iwm, "Scanning being aborted\n");
		return -EAGAIN;
	}

	set_bit(IWM_STATUS_SCANNING, &iwm->status);

	ret = iwm_scan_ssids(iwm, request->ssids, request->n_ssids);
	if (ret) {
		clear_bit(IWM_STATUS_SCANNING, &iwm->status);
		return ret;
	}

	iwm->scan_request = request;
	return 0;
}

static int iwm_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct iwm_priv *iwm = wiphy_to_iwm(wiphy);

	if (changed & WIPHY_PARAM_RTS_THRESHOLD &&
	    (iwm->conf.rts_threshold != wiphy->rts_threshold)) {
		int ret;

		iwm->conf.rts_threshold = wiphy->rts_threshold;

		ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
					     CFG_RTS_THRESHOLD,
					     iwm->conf.rts_threshold);
		if (ret < 0)
			return ret;
	}

	if (changed & WIPHY_PARAM_FRAG_THRESHOLD &&
	    (iwm->conf.frag_threshold != wiphy->frag_threshold)) {
		int ret;

		iwm->conf.frag_threshold = wiphy->frag_threshold;

		ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_FA_CFG_FIX,
					     CFG_FRAG_THRESHOLD,
					     iwm->conf.frag_threshold);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int iwm_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
				  struct cfg80211_ibss_params *params)
{
	struct iwm_priv *iwm = wiphy_to_iwm(wiphy);
	struct ieee80211_channel *chan = params->channel;
	struct cfg80211_bss *bss;

	if (!test_bit(IWM_STATUS_READY, &iwm->status))
		return -EIO;

	/* UMAC doesn't support creating IBSS network with specified bssid.
	 * This should be removed after we have join only mode supported. */
	if (params->bssid)
		return -EOPNOTSUPP;

	bss = cfg80211_get_ibss(iwm_to_wiphy(iwm), NULL,
				params->ssid, params->ssid_len);
	if (!bss) {
		iwm_scan_one_ssid(iwm, params->ssid, params->ssid_len);
		schedule_timeout_interruptible(2 * HZ);
		bss = cfg80211_get_ibss(iwm_to_wiphy(iwm), NULL,
					params->ssid, params->ssid_len);
	}
	/* IBSS join only mode is not supported by UMAC ATM */
	if (bss) {
		cfg80211_put_bss(bss);
		return -EOPNOTSUPP;
	}

	iwm->channel = ieee80211_frequency_to_channel(chan->center_freq);
	iwm->umac_profile->ibss.band = chan->band;
	iwm->umac_profile->ibss.channel = iwm->channel;
	iwm->umac_profile->ssid.ssid_len = params->ssid_len;
	memcpy(iwm->umac_profile->ssid.ssid, params->ssid, params->ssid_len);

	if (params->bssid)
		memcpy(&iwm->umac_profile->bssid[0], params->bssid, ETH_ALEN);

	return iwm_send_mlme_profile(iwm);
}

static int iwm_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	struct iwm_priv *iwm = wiphy_to_iwm(wiphy);

	if (iwm->umac_profile_active)
		return iwm_invalidate_mlme_profile(iwm);

	return 0;
}

static struct cfg80211_ops iwm_cfg80211_ops = {
	.change_virtual_intf = iwm_cfg80211_change_iface,
	.add_key = iwm_cfg80211_add_key,
	.get_key = iwm_cfg80211_get_key,
	.del_key = iwm_cfg80211_del_key,
	.set_default_key = iwm_cfg80211_set_default_key,
	.scan = iwm_cfg80211_scan,
	.set_wiphy_params = iwm_cfg80211_set_wiphy_params,
	.join_ibss = iwm_cfg80211_join_ibss,
	.leave_ibss = iwm_cfg80211_leave_ibss,
};

struct wireless_dev *iwm_wdev_alloc(int sizeof_bus, struct device *dev)
{
	int ret = 0;
	struct wireless_dev *wdev;

	/*
	 * We're trying to have the following memory
	 * layout:
	 *
	 * +-------------------------+
	 * | struct wiphy	     |
	 * +-------------------------+
	 * | struct iwm_priv         |
	 * +-------------------------+
	 * | bus private data        |
	 * | (e.g. iwm_priv_sdio)    |
	 * +-------------------------+
	 *
	 */

	wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (!wdev) {
		dev_err(dev, "Couldn't allocate wireless device\n");
		return ERR_PTR(-ENOMEM);
	}

	wdev->wiphy = wiphy_new(&iwm_cfg80211_ops,
				sizeof(struct iwm_priv) + sizeof_bus);
	if (!wdev->wiphy) {
		dev_err(dev, "Couldn't allocate wiphy device\n");
		ret = -ENOMEM;
		goto out_err_new;
	}

	set_wiphy_dev(wdev->wiphy, dev);
	wdev->wiphy->max_scan_ssids = UMAC_WIFI_IF_PROBE_OPTION_MAX;
	wdev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				       BIT(NL80211_IFTYPE_ADHOC);
	wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &iwm_band_2ghz;
	wdev->wiphy->bands[IEEE80211_BAND_5GHZ] = &iwm_band_5ghz;
	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	ret = wiphy_register(wdev->wiphy);
	if (ret < 0) {
		dev_err(dev, "Couldn't register wiphy device\n");
		goto out_err_register;
	}

	return wdev;

 out_err_register:
	wiphy_free(wdev->wiphy);

 out_err_new:
	kfree(wdev);

	return ERR_PTR(ret);
}

void iwm_wdev_free(struct iwm_priv *iwm)
{
	struct wireless_dev *wdev = iwm_to_wdev(iwm);

	if (!wdev)
		return;

	wiphy_unregister(wdev->wiphy);
	wiphy_free(wdev->wiphy);
	kfree(wdev);
}
