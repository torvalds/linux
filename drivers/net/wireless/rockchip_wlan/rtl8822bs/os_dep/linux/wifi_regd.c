/******************************************************************************
 *
 * Copyright(c) 2009-2010 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#include <drv_types.h>

#ifdef CONFIG_IOCTL_CFG80211
void rtw_regd_apply_flags(struct wiphy *wiphy)
{
	struct dvobj_priv *dvobj = wiphy_to_dvobj(wiphy);
	struct rf_ctl_t *rfctl = dvobj_to_rfctl(dvobj);
	RT_CHANNEL_INFO *channel_set = rfctl->channel_set;
	u8 max_chan_nums = rfctl->max_chan_nums;

	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	unsigned int i, j;
	u16 channel;
	u32 freq;

	/* all channels disable */
	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		sband = wiphy->bands[i];
		if (!sband)
			continue;
		for (j = 0; j < sband->n_channels; j++) {
			ch = &sband->channels[j];
			if (!ch)
				continue;
			ch->flags = IEEE80211_CHAN_DISABLED;
		}
	}

	/* channels apply by channel plans. */
	for (i = 0; i < max_chan_nums; i++) {
		channel = channel_set[i].ChannelNum;
		freq = rtw_ch2freq(channel);
		ch = ieee80211_get_channel(wiphy, freq);
		if (!ch)
			continue;

		/* enable */
		ch->flags = 0;

		if (channel_set[i].dfs) {
			/*
			* before integrating with nl80211 flow
			* bypass IEEE80211_CHAN_RADAR when configured with radar detection
			* to prevent from hostapd blocking DFS channels
			*/
			if (rtw_rfctl_dfs_domain_unknown(rfctl))
				ch->flags |= IEEE80211_CHAN_RADAR;
		}

		if (channel_set[i].ScanType == SCAN_PASSIVE) {
			#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
			ch->flags |= IEEE80211_CHAN_NO_IBSS | IEEE80211_CHAN_PASSIVE_SCAN;
			#else
			ch->flags |= IEEE80211_CHAN_NO_IR;
			#endif
		}
	}
}

static void rtw_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	switch (request->initiator) {
	case NL80211_REGDOM_SET_BY_DRIVER:
		RTW_INFO("%s: %s\n", __func__, "NL80211_REGDOM_SET_BY_DRIVER");
		break;
	case NL80211_REGDOM_SET_BY_CORE:
		RTW_INFO("%s: %s\n", __func__, "NL80211_REGDOM_SET_BY_CORE");
		break;
	case NL80211_REGDOM_SET_BY_USER:
		RTW_INFO("%s: %s alpha2:%c%c\n", __func__, "NL80211_REGDOM_SET_BY_USER"
			, request->alpha2[0], request->alpha2[1]);
		rtw_set_country(wiphy_to_adapter(wiphy), request->alpha2);
		break;
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		RTW_INFO("%s: %s\n", __func__, "NL80211_REGDOM_SET_BY_COUNTRY_IE");
		break;
	}

	rtw_regd_apply_flags(wiphy);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
static int rtw_reg_notifier_return(struct wiphy *wiphy, struct regulatory_request *request)
{
	rtw_reg_notifier(wiphy, request);
	return 0;
}
#endif

int rtw_regd_init(struct wiphy *wiphy)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
	wiphy->reg_notifier = rtw_reg_notifier_return;
#else
	wiphy->reg_notifier = rtw_reg_notifier;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
	wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
	wiphy->flags &= ~WIPHY_FLAG_STRICT_REGULATORY;
	wiphy->flags &= ~WIPHY_FLAG_DISABLE_BEACON_HINTS;
#else
	wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG;
	wiphy->regulatory_flags &= ~REGULATORY_STRICT_REG;
	wiphy->regulatory_flags &= ~REGULATORY_DISABLE_BEACON_HINTS;
#endif

	rtw_regd_apply_flags(wiphy);

	return 0;
}
#endif /* CONFIG_IOCTL_CFG80211 */
