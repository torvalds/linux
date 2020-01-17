/* cfg80211 support
 *
 * See copyright yestice in main.c
 */
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include "hw.h"
#include "main.h"
#include "oriyesco.h"

#include "cfg.h"

/* Supported bitrates. Must agree with hw.c */
static struct ieee80211_rate oriyesco_rates[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20 },
	{ .bitrate = 55 },
	{ .bitrate = 110 },
};

static const void * const oriyesco_wiphy_privid = &oriyesco_wiphy_privid;

/* Called after oriyesco_private is allocated. */
void oriyesco_wiphy_init(struct wiphy *wiphy)
{
	struct oriyesco_private *priv = wiphy_priv(wiphy);

	wiphy->privid = oriyesco_wiphy_privid;

	set_wiphy_dev(wiphy, priv->dev);
}

/* Called after firmware is initialised */
int oriyesco_wiphy_register(struct wiphy *wiphy)
{
	struct oriyesco_private *priv = wiphy_priv(wiphy);
	int i, channels = 0;

	if (priv->firmware_type == FIRMWARE_TYPE_AGERE)
		wiphy->max_scan_ssids = 1;
	else
		wiphy->max_scan_ssids = 0;

	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);

	/* TODO: should we set if we only have demo ad-hoc?
	 *       (priv->has_port3)
	 */
	if (priv->has_ibss)
		wiphy->interface_modes |= BIT(NL80211_IFTYPE_ADHOC);

	if (!priv->broken_monitor || force_monitor)
		wiphy->interface_modes |= BIT(NL80211_IFTYPE_MONITOR);

	priv->band.bitrates = oriyesco_rates;
	priv->band.n_bitrates = ARRAY_SIZE(oriyesco_rates);

	/* Only support channels allowed by the card EEPROM */
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (priv->channel_mask & (1 << i)) {
			priv->channels[i].center_freq =
				ieee80211_channel_to_frequency(i + 1,
							   NL80211_BAND_2GHZ);
			channels++;
		}
	}
	priv->band.channels = priv->channels;
	priv->band.n_channels = channels;

	wiphy->bands[NL80211_BAND_2GHZ] = &priv->band;
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	i = 0;
	if (priv->has_wep) {
		priv->cipher_suites[i] = WLAN_CIPHER_SUITE_WEP40;
		i++;

		if (priv->has_big_wep) {
			priv->cipher_suites[i] = WLAN_CIPHER_SUITE_WEP104;
			i++;
		}
	}
	if (priv->has_wpa) {
		priv->cipher_suites[i] = WLAN_CIPHER_SUITE_TKIP;
		i++;
	}
	wiphy->cipher_suites = priv->cipher_suites;
	wiphy->n_cipher_suites = i;

	wiphy->rts_threshold = priv->rts_thresh;
	if (!priv->has_mwo)
		wiphy->frag_threshold = priv->frag_thresh + 1;
	wiphy->retry_short = priv->short_retry_limit;
	wiphy->retry_long = priv->long_retry_limit;

	return wiphy_register(wiphy);
}

static int oriyesco_change_vif(struct wiphy *wiphy, struct net_device *dev,
			      enum nl80211_iftype type,
			      struct vif_params *params)
{
	struct oriyesco_private *priv = wiphy_priv(wiphy);
	int err = 0;
	unsigned long lock;

	if (oriyesco_lock(priv, &lock) != 0)
		return -EBUSY;

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
		if (!priv->has_ibss && !priv->has_port3)
			err = -EINVAL;
		break;

	case NL80211_IFTYPE_STATION:
		break;

	case NL80211_IFTYPE_MONITOR:
		if (priv->broken_monitor && !force_monitor) {
			wiphy_warn(wiphy,
				   "Monitor mode support is buggy in this firmware, yest enabling\n");
			err = -EINVAL;
		}
		break;

	default:
		err = -EINVAL;
	}

	if (!err) {
		priv->iw_mode = type;
		set_port_type(priv);
		err = oriyesco_commit(priv);
	}

	oriyesco_unlock(priv, &lock);

	return err;
}

static int oriyesco_scan(struct wiphy *wiphy,
			struct cfg80211_scan_request *request)
{
	struct oriyesco_private *priv = wiphy_priv(wiphy);
	int err;

	if (!request)
		return -EINVAL;

	if (priv->scan_request && priv->scan_request != request)
		return -EBUSY;

	priv->scan_request = request;

	err = oriyesco_hw_trigger_scan(priv, request->ssids);
	/* On error the we aren't processing the request */
	if (err)
		priv->scan_request = NULL;

	return err;
}

static int oriyesco_set_monitor_channel(struct wiphy *wiphy,
				       struct cfg80211_chan_def *chandef)
{
	struct oriyesco_private *priv = wiphy_priv(wiphy);
	int err = 0;
	unsigned long flags;
	int channel;

	if (!chandef->chan)
		return -EINVAL;

	if (cfg80211_get_chandef_type(chandef) != NL80211_CHAN_NO_HT)
		return -EINVAL;

	if (chandef->chan->band != NL80211_BAND_2GHZ)
		return -EINVAL;

	channel = ieee80211_frequency_to_channel(chandef->chan->center_freq);

	if ((channel < 1) || (channel > NUM_CHANNELS) ||
	     !(priv->channel_mask & (1 << (channel - 1))))
		return -EINVAL;

	if (oriyesco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->channel = channel;
	if (priv->iw_mode == NL80211_IFTYPE_MONITOR) {
		/* Fast channel change - yes commit if successful */
		struct hermes *hw = &priv->hw;
		err = hw->ops->cmd_wait(hw, HERMES_CMD_TEST |
					    HERMES_TEST_SET_CHANNEL,
					channel, NULL);
	}
	oriyesco_unlock(priv, &flags);

	return err;
}

static int oriyesco_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct oriyesco_private *priv = wiphy_priv(wiphy);
	int frag_value = -1;
	int rts_value = -1;
	int err = 0;

	if (changed & WIPHY_PARAM_RETRY_SHORT) {
		/* Setting short retry yest supported */
		err = -EINVAL;
	}

	if (changed & WIPHY_PARAM_RETRY_LONG) {
		/* Setting long retry yest supported */
		err = -EINVAL;
	}

	if (changed & WIPHY_PARAM_FRAG_THRESHOLD) {
		/* Set fragmentation */
		if (priv->has_mwo) {
			if (wiphy->frag_threshold == -1)
				frag_value = 0;
			else {
				printk(KERN_WARNING "%s: Fixed fragmentation "
				       "is yest supported on this firmware. "
				       "Using MWO robust instead.\n",
				       priv->ndev->name);
				frag_value = 1;
			}
		} else {
			if (wiphy->frag_threshold == -1)
				frag_value = 2346;
			else if ((wiphy->frag_threshold < 257) ||
				 (wiphy->frag_threshold > 2347))
				err = -EINVAL;
			else
				/* cfg80211 value is 257-2347 (odd only)
				 * oriyesco rid has range 256-2346 (even only) */
				frag_value = wiphy->frag_threshold & ~0x1;
		}
	}

	if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
		/* Set RTS.
		 *
		 * Prism documentation suggests default of 2432,
		 * and a range of 0-3000.
		 *
		 * Current implementation uses 2347 as the default and
		 * the upper limit.
		 */

		if (wiphy->rts_threshold == -1)
			rts_value = 2347;
		else if (wiphy->rts_threshold > 2347)
			err = -EINVAL;
		else
			rts_value = wiphy->rts_threshold;
	}

	if (!err) {
		unsigned long flags;

		if (oriyesco_lock(priv, &flags) != 0)
			return -EBUSY;

		if (frag_value >= 0) {
			if (priv->has_mwo)
				priv->mwo_robust = frag_value;
			else
				priv->frag_thresh = frag_value;
		}
		if (rts_value >= 0)
			priv->rts_thresh = rts_value;

		err = oriyesco_commit(priv);

		oriyesco_unlock(priv, &flags);
	}

	return err;
}

const struct cfg80211_ops oriyesco_cfg_ops = {
	.change_virtual_intf = oriyesco_change_vif,
	.set_monitor_channel = oriyesco_set_monitor_channel,
	.scan = oriyesco_scan,
	.set_wiphy_params = oriyesco_set_wiphy_params,
};
