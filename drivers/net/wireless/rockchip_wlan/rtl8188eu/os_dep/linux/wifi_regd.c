/* SPDX-License-Identifier: GPL-2.0 */
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

#include <rtw_wifi_regd.h>

static struct country_code_to_enum_rd allCountries[] = {
	{COUNTRY_CODE_USER, "RD"},
};

/*
 * REG_RULE(freq start, freq end, bandwidth, max gain, eirp, reg_flags)
 */

/*
 *Only these channels all allow active
 *scan on all world regulatory domains
 */

/* 2G chan 01 - chan 11 */
#define RTW_2GHZ_CH01_11	\
	REG_RULE(2412-10, 2462+10, 40, 0, 20, 0)

/*
 *We enable active scan on these a case
 *by case basis by regulatory domain
 */

/* 2G chan 12 - chan 13, PASSIV SCAN */
#define RTW_2GHZ_CH12_13	\
	REG_RULE(2467-10, 2472+10, 40, 0, 20,	\
		 NL80211_RRF_PASSIVE_SCAN)

/* 2G chan 14, PASSIVS SCAN, NO OFDM (B only) */
#define RTW_2GHZ_CH14	\
	REG_RULE(2484-10, 2484+10, 40, 0, 20,	\
		 NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_OFDM)

/* 5G chan 36 - chan 64 */
#define RTW_5GHZ_5150_5350	\
	REG_RULE(5150-10, 5350+10, 40, 0, 30,	\
		 NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_IBSS)

/* 5G chan 100 - chan 165 */
#define RTW_5GHZ_5470_5850	\
	REG_RULE(5470-10, 5850+10, 40, 0, 30, \
		 NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_IBSS)

/* 5G chan 149 - chan 165 */
#define RTW_5GHZ_5725_5850	\
	REG_RULE(5725-10, 5850+10, 40, 0, 30, \
		 NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_IBSS)

/* 5G chan 36 - chan 165 */
#define RTW_5GHZ_5150_5850	\
	REG_RULE(5150-10, 5850+10, 40, 0, 30,	\
		 NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_IBSS)

static const struct ieee80211_regdomain rtw_regdom_rd = {
	.n_reg_rules = 3,
	.alpha2 = "99",
	.reg_rules = {
		RTW_2GHZ_CH01_11,
		RTW_2GHZ_CH12_13,
		RTW_5GHZ_5150_5850,
	}
};

static const struct ieee80211_regdomain rtw_regdom_11 = {
	.n_reg_rules = 1,
	.alpha2 = "99",
	.reg_rules = {
		RTW_2GHZ_CH01_11,
	}
};

static const struct ieee80211_regdomain rtw_regdom_12_13 = {
	.n_reg_rules = 2,
	.alpha2 = "99",
	.reg_rules = {
		RTW_2GHZ_CH01_11,
		RTW_2GHZ_CH12_13,
	}
};

static const struct ieee80211_regdomain rtw_regdom_no_midband = {
	.n_reg_rules = 3,
	.alpha2 = "99",
	.reg_rules = {
		RTW_2GHZ_CH01_11,
		RTW_5GHZ_5150_5350,
		RTW_5GHZ_5725_5850,
	}
};

static const struct ieee80211_regdomain rtw_regdom_60_64 = {
	.n_reg_rules = 3,
	.alpha2 = "99",
	.reg_rules = {
		RTW_2GHZ_CH01_11,
		RTW_2GHZ_CH12_13,
		RTW_5GHZ_5725_5850,
	}
};

static const struct ieee80211_regdomain rtw_regdom_14_60_64 = {
	.n_reg_rules = 4,
	.alpha2 = "99",
	.reg_rules = {
		RTW_2GHZ_CH01_11,
		RTW_2GHZ_CH12_13,
		RTW_2GHZ_CH14,
		RTW_5GHZ_5725_5850,
	}
};

static const struct ieee80211_regdomain rtw_regdom_14 = {
	.n_reg_rules = 3,
	.alpha2 = "99",
	.reg_rules = {
		RTW_2GHZ_CH01_11,
		RTW_2GHZ_CH12_13,
		RTW_2GHZ_CH14,
	}
};

#if 0
static struct rtw_regulatory *rtw_regd;
#endif

#if 0 /* not_yet */
static void _rtw_reg_apply_beaconing_flags(struct wiphy *wiphy,
		enum nl80211_reg_initiator initiator)
{
	enum nl80211_band band;
	struct ieee80211_supported_band *sband;
	const struct ieee80211_reg_rule *reg_rule;
	struct ieee80211_channel *ch;
	unsigned int i;
	u32 bandwidth = 0;
	int r;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {

		if (!wiphy->bands[band])
			continue;

		sband = wiphy->bands[band];

		for (i = 0; i < sband->n_channels; i++) {
			ch = &sband->channels[i];
			if (rtw_is_dfs_ch(ch->hw_value) ||
			    (ch->flags & IEEE80211_CHAN_RADAR))
				continue;
			if (initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE) {
				r = freq_reg_info(wiphy, ch->center_freq,
						  bandwidth, &reg_rule);
				if (r)
					continue;

				/*
				 *If 11d had a rule for this channel ensure
				 *we enable adhoc/beaconing if it allows us to
				 *use it. Note that we would have disabled it
				 *by applying our static world regdomain by
				 *default during init, prior to calling our
				 *regulatory_hint().
				 */

				if (!(reg_rule->flags & NL80211_RRF_NO_IBSS))
					ch->flags &= ~IEEE80211_CHAN_NO_IBSS;
				if (!
				    (reg_rule->flags &
				     NL80211_RRF_PASSIVE_SCAN))
					ch->flags &=
						~IEEE80211_CHAN_PASSIVE_SCAN;
			} else {
				if (ch->beacon_found)
					ch->flags &= ~(IEEE80211_CHAN_NO_IBSS |
						IEEE80211_CHAN_PASSIVE_SCAN);
			}
		}
	}
}

/* Allows active scan scan on Ch 12 and 13 */
static void _rtw_reg_apply_active_scan_flags(struct wiphy *wiphy,
		enum nl80211_reg_initiator
		initiator)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	const struct ieee80211_reg_rule *reg_rule;
	u32 bandwidth = 0;
	int r;

	if (!wiphy->bands[NL80211_BAND_2GHZ])
		return;
	sband = wiphy->bands[NL80211_BAND_2GHZ];

	/*
	 * If no country IE has been received always enable active scan
	 * on these channels. This is only done for specific regulatory SKUs
	 */
	if (initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE) {
		ch = &sband->channels[11];	/* CH 12 */
		if (ch->flags & IEEE80211_CHAN_PASSIVE_SCAN)
			ch->flags &= ~IEEE80211_CHAN_PASSIVE_SCAN;
		ch = &sband->channels[12];	/* CH 13 */
		if (ch->flags & IEEE80211_CHAN_PASSIVE_SCAN)
			ch->flags &= ~IEEE80211_CHAN_PASSIVE_SCAN;
		return;
	}

	/*
	 * If a country IE has been received check its rule for this
	 * channel first before enabling active scan. The passive scan
	 * would have been enforced by the initial processing of our
	 * custom regulatory domain.
	 */

	ch = &sband->channels[11];	/* CH 12 */
	r = freq_reg_info(wiphy, ch->center_freq, bandwidth, &reg_rule);
	if (!r) {
		if (!(reg_rule->flags & NL80211_RRF_PASSIVE_SCAN))
			if (ch->flags & IEEE80211_CHAN_PASSIVE_SCAN)
				ch->flags &= ~IEEE80211_CHAN_PASSIVE_SCAN;
	}

	ch = &sband->channels[12];	/* CH 13 */
	r = freq_reg_info(wiphy, ch->center_freq, bandwidth, &reg_rule);
	if (!r) {
		if (!(reg_rule->flags & NL80211_RRF_PASSIVE_SCAN))
			if (ch->flags & IEEE80211_CHAN_PASSIVE_SCAN)
				ch->flags &= ~IEEE80211_CHAN_PASSIVE_SCAN;
	}
}
#endif

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

		if (sband) {
			for (j = 0; j < sband->n_channels; j++) {
				ch = &sband->channels[j];

				if (ch)
					ch->flags = IEEE80211_CHAN_DISABLED;
			}
		}
	}

	/* channels apply by channel plans. */
	for (i = 0; i < max_chan_nums; i++) {
		channel = channel_set[i].ChannelNum;
		freq = rtw_ch2freq(channel);

		ch = ieee80211_get_channel(wiphy, freq);
		if (!ch)
			continue;

		if (channel_set[i].ScanType == SCAN_PASSIVE
			#if defined(CONFIG_DFS_MASTER)
			&& rtw_odm_dfs_domain_unknown(dvobj)
			#endif
		) {
			#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
			ch->flags = (IEEE80211_CHAN_NO_IBSS | IEEE80211_CHAN_PASSIVE_SCAN);
			#else
			ch->flags = IEEE80211_CHAN_NO_IR;
			#endif
		} else
			ch->flags = 0;

		#ifdef CONFIG_DFS
		if (rtw_is_dfs_ch(ch->hw_value)
			#if defined(CONFIG_DFS_MASTER)
			&& rtw_odm_dfs_domain_unknown(dvobj)
			#endif
		) {
			ch->flags |= IEEE80211_CHAN_RADAR;
			#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
			ch->flags |= (IEEE80211_CHAN_NO_IBSS | IEEE80211_CHAN_PASSIVE_SCAN);
			#else
			ch->flags |= IEEE80211_CHAN_NO_IR;
			#endif
		}
		#endif /* CONFIG_DFS */
	}
}

static const struct ieee80211_regdomain *_rtw_regdomain_select(struct
		rtw_regulatory
		*reg)
{
#if 0
	switch (reg->country_code) {
	case COUNTRY_CODE_USER:
	default:
		return &rtw_regdom_rd;
	}
#else
	return &rtw_regdom_rd;
#endif
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

static void _rtw_regd_init_wiphy(struct rtw_regulatory *reg, struct wiphy *wiphy)
{
	const struct ieee80211_regdomain *regd;

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

	regd = _rtw_regdomain_select(reg);
	wiphy_apply_custom_regulatory(wiphy, regd);

	rtw_regd_apply_flags(wiphy);
}

int rtw_regd_init(struct wiphy *wiphy)
{
#if 0
	if (rtw_regd == NULL) {
		rtw_regd = (struct rtw_regulatory *)
			   rtw_malloc(sizeof(struct rtw_regulatory));

		rtw_regd->alpha2[0] = '9';
		rtw_regd->alpha2[1] = '9';

		rtw_regd->country_code = COUNTRY_CODE_USER;
	}

	RTW_INFO("%s: Country alpha2 being used: %c%c\n",
		 __func__, rtw_regd->alpha2[0], rtw_regd->alpha2[1]);
#endif

	_rtw_regd_init_wiphy(NULL, wiphy);

	return 0;
}
#endif /* CONFIG_IOCTL_CFG80211 */
