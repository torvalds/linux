/*
 * Implement cfg80211 ("iw") support.
 *
 * Copyright (C) 2009 M&N Solutions GmbH, 61191 Rosbach, Germany
 * Holger Schurig <hs4233@mail.mn-solutions.de>
 *
 */

#include <linux/slab.h>
#include <net/cfg80211.h>

#include "cfg.h"
#include "cmd.h"


#define CHAN2G(_channel, _freq, _flags) {        \
	.band             = IEEE80211_BAND_2GHZ, \
	.center_freq      = (_freq),             \
	.hw_value         = (_channel),          \
	.flags            = (_flags),            \
	.max_antenna_gain = 0,                   \
	.max_power        = 30,                  \
}

static struct ieee80211_channel lbs_2ghz_channels[] = {
	CHAN2G(1,  2412, 0),
	CHAN2G(2,  2417, 0),
	CHAN2G(3,  2422, 0),
	CHAN2G(4,  2427, 0),
	CHAN2G(5,  2432, 0),
	CHAN2G(6,  2437, 0),
	CHAN2G(7,  2442, 0),
	CHAN2G(8,  2447, 0),
	CHAN2G(9,  2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

#define RATETAB_ENT(_rate, _rateid, _flags) { \
	.bitrate  = (_rate),                  \
	.hw_value = (_rateid),                \
	.flags    = (_flags),                 \
}


static struct ieee80211_rate lbs_rates[] = {
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

static struct ieee80211_supported_band lbs_band_2ghz = {
	.channels = lbs_2ghz_channels,
	.n_channels = ARRAY_SIZE(lbs_2ghz_channels),
	.bitrates = lbs_rates,
	.n_bitrates = ARRAY_SIZE(lbs_rates),
};


static const u32 cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
};



static int lbs_cfg_set_channel(struct wiphy *wiphy,
	struct ieee80211_channel *chan,
	enum nl80211_channel_type channel_type)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	int ret = -ENOTSUPP;

	lbs_deb_enter_args(LBS_DEB_CFG80211, "freq %d, type %d", chan->center_freq, channel_type);

	if (channel_type != NL80211_CHAN_NO_HT)
		goto out;

	ret = lbs_set_channel(priv, chan->hw_value);

 out:
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}




static struct cfg80211_ops lbs_cfg80211_ops = {
	.set_channel = lbs_cfg_set_channel,
};


/*
 * At this time lbs_private *priv doesn't even exist, so we just allocate
 * memory and don't initialize the wiphy further. This is postponed until we
 * can talk to the firmware and happens at registration time in
 * lbs_cfg_wiphy_register().
 */
struct wireless_dev *lbs_cfg_alloc(struct device *dev)
{
	int ret = 0;
	struct wireless_dev *wdev;

	lbs_deb_enter(LBS_DEB_CFG80211);

	wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (!wdev) {
		dev_err(dev, "cannot allocate wireless device\n");
		return ERR_PTR(-ENOMEM);
	}

	wdev->wiphy = wiphy_new(&lbs_cfg80211_ops, sizeof(struct lbs_private));
	if (!wdev->wiphy) {
		dev_err(dev, "cannot allocate wiphy\n");
		ret = -ENOMEM;
		goto err_wiphy_new;
	}

	lbs_deb_leave(LBS_DEB_CFG80211);
	return wdev;

 err_wiphy_new:
	kfree(wdev);
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ERR_PTR(ret);
}


/*
 * This function get's called after lbs_setup_firmware() determined the
 * firmware capabities. So we can setup the wiphy according to our
 * hardware/firmware.
 */
int lbs_cfg_register(struct lbs_private *priv)
{
	struct wireless_dev *wdev = priv->wdev;
	int ret;

	lbs_deb_enter(LBS_DEB_CFG80211);

	wdev->wiphy->max_scan_ssids = 1;
	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	/* TODO: BIT(NL80211_IFTYPE_ADHOC); */
	wdev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);

	/* TODO: honor priv->regioncode */
	wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &lbs_band_2ghz;

	/*
	 * We could check priv->fwcapinfo && FW_CAPINFO_WPA, but I have
	 * never seen a firmware without WPA
	 */
	wdev->wiphy->cipher_suites = cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

	ret = wiphy_register(wdev->wiphy);
	if (ret < 0)
		lbs_pr_err("cannot register wiphy device\n");

	priv->wiphy_registered = true;

	ret = register_netdev(priv->dev);
	if (ret)
		lbs_pr_err("cannot register network device\n");

	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}


void lbs_cfg_free(struct lbs_private *priv)
{
	struct wireless_dev *wdev = priv->wdev;

	lbs_deb_enter(LBS_DEB_CFG80211);

	if (!wdev)
		return;

	if (priv->wiphy_registered)
		wiphy_unregister(wdev->wiphy);

	if (wdev->wiphy)
		wiphy_free(wdev->wiphy);

	kfree(wdev);
}
