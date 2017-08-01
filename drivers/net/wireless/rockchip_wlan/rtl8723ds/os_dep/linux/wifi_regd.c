/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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

static bool _rtw_is_radar_freq(u16 center_freq)
{
	return center_freq >= 5260 && center_freq <= 5700;
}

#if 0 /* not_yet */
static void _rtw_reg_apply_beaconing_flags(struct wiphy *wiphy,
		enum nl80211_reg_initiator initiator)
{
	enum ieee80211_band band;
	struct ieee80211_supported_band *sband;
	const struct ieee80211_reg_rule *reg_rule;
	struct ieee80211_channel *ch;
	unsigned int i;
	u32 bandwidth = 0;
	int r;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {

		if (!wiphy->bands[band])
			continue;

		sband = wiphy->bands[band];

		for (i = 0; i < sband->n_channels; i++) {
			ch = &sband->channels[i];
			if (_rtw_is_radar_freq(ch->center_freq) ||
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

	if (!wiphy->bands[IEEE80211_BAND_2GHZ])
		return;
	sband = wiphy->bands[IEEE80211_BAND_2GHZ];

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

/*
 * Always apply Radar/DFS rules on
 * freq range 5260 MHz - 5700 MHz
 */
static void _rtw_reg_apply_radar_flags(struct wiphy *wiphy)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	unsigned int i;

	if (!wiphy->bands[IEEE80211_BAND_5GHZ])
		return;

	sband = wiphy->bands[IEEE80211_BAND_5GHZ];

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		if (!_rtw_is_radar_freq(ch->center_freq))
			continue;
#ifdef CONFIG_DFS
		#if !defined(CONFIG_DFS_MASTER)
		if (!(ch->flags & IEEE80211_CHAN_DISABLED)) {
			ch->flags |= IEEE80211_CHAN_RADAR;
			#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
			ch->flags |= (IEEE80211_CHAN_NO_IBSS | IEEE80211_CHAN_PASSIVE_SCAN);
			#else
			ch->flags |= IEEE80211_CHAN_NO_IR;
			#endif
		}
		#endif
#endif /* CONFIG_DFS */

#if 0
		/*
		 * We always enable radar detection/DFS on this
		 * frequency range. Additionally we also apply on
		 * this frequency range:
		 * - If STA mode does not yet have DFS supports disable
		 *  active scanning
		 * - If adhoc mode does not support DFS yet then disable
		 *  adhoc in the frequency.
		 * - If AP mode does not yet support radar detection/DFS
		 *  do not allow AP mode
		 */
		if (!(ch->flags & IEEE80211_CHAN_DISABLED))
			ch->flags |= IEEE80211_CHAN_RADAR |
				     IEEE80211_CHAN_NO_IBSS |
				     IEEE80211_CHAN_PASSIVE_SCAN;
#endif
	}
}

static void _rtw_reg_apply_flags(struct wiphy *wiphy)
{
#if 1				/* by channel plan */
	_adapter *padapter = wiphy_to_adapter(wiphy);
	u8 channel_plan = padapter->mlmepriv.ChannelPlan;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	RT_CHANNEL_INFO *channel_set = pmlmeext->channel_set;
	u8 max_chan_nums = pmlmeext->max_chan_nums;

	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	unsigned int i, j;
	u16 channel;
	u32 freq;

	/* all channels disable */
	for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
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
		if (ch) {
			if (channel_set[i].ScanType == SCAN_PASSIVE) {
				#if defined(CONFIG_DFS_MASTER)
				ch->flags = 0;
				#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
				ch->flags = (IEEE80211_CHAN_NO_IBSS | IEEE80211_CHAN_PASSIVE_SCAN);
				#else
				ch->flags = IEEE80211_CHAN_NO_IR;
				#endif
			} else
				ch->flags = 0;
		}
	}

#else
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	unsigned int i, j;
	u16 channels[37] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56,
		60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
		149, 153,
		157, 161, 165
	};
	u16 channel;
	u32 freq;

	for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
		sband = wiphy->bands[i];

		if (sband)
			for (j = 0; j < sband->n_channels; j++) {
				ch = &sband->channels[j];

				if (ch)
					ch->flags = IEEE80211_CHAN_DISABLED;
			}
	}

	for (i = 0; i < 37; i++) {
		channel = channels[i];
		freq = rtw_ch2freq(channel);

		ch = ieee80211_get_channel(wiphy, freq);
		if (ch) {
			if (channel <= 11)
				ch->flags = 0;
			else
				ch->flags = 0;	/* IEEE80211_CHAN_PASSIVE_SCAN; */
		}
		/* printk("%s: freq %d(%d) flag 0x%02X\n", __func__, freq, channel, ch->flags); */
	}
#endif
}

static void _rtw_reg_apply_world_flags(struct wiphy *wiphy,
				       enum nl80211_reg_initiator initiator,
				       struct rtw_regulatory *reg)
{
	/* _rtw_reg_apply_beaconing_flags(wiphy, initiator); */
	/* _rtw_reg_apply_active_scan_flags(wiphy, initiator); */
	return;
}

static int _rtw_reg_notifier_apply(struct wiphy *wiphy,
				   struct regulatory_request *request,
				   struct rtw_regulatory *reg)
{

	/* Hard code flags */
	_rtw_reg_apply_flags(wiphy);

	/* We always apply this */
	_rtw_reg_apply_radar_flags(wiphy);

	switch (request->initiator) {
	case NL80211_REGDOM_SET_BY_DRIVER:
		RTW_INFO("%s: %s\n", __func__, "NL80211_REGDOM_SET_BY_DRIVER");
		_rtw_reg_apply_world_flags(wiphy, NL80211_REGDOM_SET_BY_DRIVER,
					   reg);
		break;
	case NL80211_REGDOM_SET_BY_CORE:
		RTW_INFO("%s: %s\n", __func__,
			 "NL80211_REGDOM_SET_BY_CORE to DRV");
		_rtw_reg_apply_world_flags(wiphy, NL80211_REGDOM_SET_BY_DRIVER,
					   reg);
		break;
	case NL80211_REGDOM_SET_BY_USER:
		RTW_INFO("%s: %s\n", __func__,
			 "NL80211_REGDOM_SET_BY_USER to DRV");
		_rtw_reg_apply_world_flags(wiphy, NL80211_REGDOM_SET_BY_DRIVER,
					   reg);
		break;
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		RTW_INFO("%s: %s\n", __func__,
			 "NL80211_REGDOM_SET_BY_COUNTRY_IE");
		_rtw_reg_apply_world_flags(wiphy, request->initiator, reg);
		break;
	}

	return 0;
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

void _rtw_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct rtw_regulatory *reg = NULL;

	RTW_INFO("%s\n", __func__);

	_rtw_reg_notifier_apply(wiphy, request, reg);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
int rtw_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
#else
void rtw_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
#endif
{
	_rtw_reg_notifier(wiphy, request);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
	return 0;
#endif
}

void rtw_reg_notify_by_driver(_adapter *adapter)
{
	if ((adapter->rtw_wdev != NULL) && (adapter->rtw_wdev->wiphy)) {
		struct regulatory_request request;
		request.initiator = NL80211_REGDOM_SET_BY_DRIVER;
		rtw_reg_notifier(adapter->rtw_wdev->wiphy, &request);
	}
}

static void _rtw_regd_init_wiphy(struct rtw_regulatory *reg, struct wiphy *wiphy)
{
	const struct ieee80211_regdomain *regd;

	wiphy->reg_notifier = rtw_reg_notifier;

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

	/* Hard code flags */
	_rtw_reg_apply_flags(wiphy);
	_rtw_reg_apply_radar_flags(wiphy);
	_rtw_reg_apply_world_flags(wiphy, NL80211_REGDOM_SET_BY_DRIVER, reg);
}

static struct country_code_to_enum_rd *_rtw_regd_find_country(u16 countrycode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(allCountries); i++) {
		if (allCountries[i].countrycode == countrycode)
			return &allCountries[i];
	}
	return NULL;
}

int rtw_regd_init(_adapter *padapter)
{
	struct wiphy *wiphy = padapter->rtw_wdev->wiphy;

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
