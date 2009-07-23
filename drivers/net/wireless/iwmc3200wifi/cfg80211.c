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

static int iwm_cfg80211_change_iface(struct wiphy *wiphy, int ifindex,
				     enum nl80211_iftype type, u32 *flags,
				     struct vif_params *params)
{
	struct net_device *ndev;
	struct wireless_dev *wdev;
	struct iwm_priv *iwm;
	u32 old_mode;

	/* we're under RTNL */
	ndev = __dev_get_by_index(&init_net, ifindex);
	if (!ndev)
		return -ENODEV;

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
