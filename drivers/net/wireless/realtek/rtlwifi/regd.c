// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "wifi.h"
#include "regd.h"

static struct country_code_to_enum_rd all_countries[] = {
	{COUNTRY_CODE_FCC, "US"},
	{COUNTRY_CODE_IC, "US"},
	{COUNTRY_CODE_ETSI, "EC"},
	{COUNTRY_CODE_SPAIN, "EC"},
	{COUNTRY_CODE_FRANCE, "EC"},
	{COUNTRY_CODE_MKK, "JP"},
	{COUNTRY_CODE_MKK1, "JP"},
	{COUNTRY_CODE_ISRAEL, "EC"},
	{COUNTRY_CODE_TELEC, "JP"},
	{COUNTRY_CODE_MIC, "JP"},
	{COUNTRY_CODE_GLOBAL_DOMAIN, "JP"},
	{COUNTRY_CODE_WORLD_WIDE_13, "EC"},
	{COUNTRY_CODE_TELEC_NETGEAR, "EC"},
	{COUNTRY_CODE_WORLD_WIDE_13_5G_ALL, "US"},
};

/*
 *Only these channels all allow active
 *scan on all world regulatory domains
 */
#define RTL819x_2GHZ_CH01_11	\
	REG_RULE(2412-10, 2462+10, 40, 0, 20, 0)

/*
 *We enable active scan on these a case
 *by case basis by regulatory domain
 */
#define RTL819x_2GHZ_CH12_13	\
	REG_RULE(2467-10, 2472+10, 40, 0, 20,\
	NL80211_RRF_PASSIVE_SCAN)

#define RTL819x_2GHZ_CH14	\
	REG_RULE(2484-10, 2484+10, 40, 0, 20, \
	NL80211_RRF_PASSIVE_SCAN | \
	NL80211_RRF_NO_OFDM)

/* 5G chan 36 - chan 64*/
#define RTL819x_5GHZ_5150_5350	\
	REG_RULE(5150-10, 5350+10, 80, 0, 30, 0)
/* 5G chan 100 - chan 165*/
#define RTL819x_5GHZ_5470_5850	\
	REG_RULE(5470-10, 5850+10, 80, 0, 30, 0)
/* 5G chan 149 - chan 165*/
#define RTL819x_5GHZ_5725_5850	\
	REG_RULE(5725-10, 5850+10, 80, 0, 30, 0)

#define RTL819x_5GHZ_ALL	\
	(RTL819x_5GHZ_5150_5350, RTL819x_5GHZ_5470_5850)

static const struct ieee80211_regdomain rtl_regdom_11 = {
	.n_reg_rules = 1,
	.alpha2 = "99",
	.reg_rules = {
		      RTL819x_2GHZ_CH01_11,
		      }
};

static const struct ieee80211_regdomain rtl_regdom_12_13 = {
	.n_reg_rules = 2,
	.alpha2 = "99",
	.reg_rules = {
		      RTL819x_2GHZ_CH01_11,
			  RTL819x_2GHZ_CH12_13,
		      }
};

static const struct ieee80211_regdomain rtl_regdom_no_midband = {
	.n_reg_rules = 3,
	.alpha2 = "99",
	.reg_rules = {
		      RTL819x_2GHZ_CH01_11,
			  RTL819x_5GHZ_5150_5350,
			  RTL819x_5GHZ_5725_5850,
		      }
};

static const struct ieee80211_regdomain rtl_regdom_60_64 = {
	.n_reg_rules = 3,
	.alpha2 = "99",
	.reg_rules = {
		      RTL819x_2GHZ_CH01_11,
			  RTL819x_2GHZ_CH12_13,
			  RTL819x_5GHZ_5725_5850,
		      }
};

static const struct ieee80211_regdomain rtl_regdom_14_60_64 = {
	.n_reg_rules = 4,
	.alpha2 = "99",
	.reg_rules = {
		      RTL819x_2GHZ_CH01_11,
			  RTL819x_2GHZ_CH12_13,
			  RTL819x_2GHZ_CH14,
			  RTL819x_5GHZ_5725_5850,
		      }
};

static const struct ieee80211_regdomain rtl_regdom_12_13_5g_all = {
	.n_reg_rules = 4,
	.alpha2 = "99",
	.reg_rules = {
			RTL819x_2GHZ_CH01_11,
			RTL819x_2GHZ_CH12_13,
			RTL819x_5GHZ_5150_5350,
			RTL819x_5GHZ_5470_5850,
		}
};

static const struct ieee80211_regdomain rtl_regdom_14 = {
	.n_reg_rules = 3,
	.alpha2 = "99",
	.reg_rules = {
		      RTL819x_2GHZ_CH01_11,
			  RTL819x_2GHZ_CH12_13,
			  RTL819x_2GHZ_CH14,
		      }
};

static bool _rtl_is_radar_freq(u16 center_freq)
{
	return center_freq >= 5260 && center_freq <= 5700;
}

static void _rtl_reg_apply_beaconing_flags(struct wiphy *wiphy,
					   enum nl80211_reg_initiator initiator)
{
	enum nl80211_band band;
	struct ieee80211_supported_band *sband;
	const struct ieee80211_reg_rule *reg_rule;
	struct ieee80211_channel *ch;
	unsigned int i;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {

		if (!wiphy->bands[band])
			continue;

		sband = wiphy->bands[band];

		for (i = 0; i < sband->n_channels; i++) {
			ch = &sband->channels[i];
			if (_rtl_is_radar_freq(ch->center_freq) ||
			    (ch->flags & IEEE80211_CHAN_RADAR))
				continue;
			if (initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE) {
				reg_rule = freq_reg_info(wiphy,
							 ch->center_freq);
				if (IS_ERR(reg_rule))
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
				if (!(reg_rule->flags &
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
static void _rtl_reg_apply_active_scan_flags(struct wiphy *wiphy,
					     enum nl80211_reg_initiator
					     initiator)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	const struct ieee80211_reg_rule *reg_rule;

	if (!wiphy->bands[NL80211_BAND_2GHZ])
		return;
	sband = wiphy->bands[NL80211_BAND_2GHZ];

	/*
	 *If no country IE has been received always enable active scan
	 *on these channels. This is only done for specific regulatory SKUs
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
	 *If a country IE has been recieved check its rule for this
	 *channel first before enabling active scan. The passive scan
	 *would have been enforced by the initial processing of our
	 *custom regulatory domain.
	 */

	ch = &sband->channels[11];	/* CH 12 */
	reg_rule = freq_reg_info(wiphy, ch->center_freq);
	if (!IS_ERR(reg_rule)) {
		if (!(reg_rule->flags & NL80211_RRF_PASSIVE_SCAN))
			if (ch->flags & IEEE80211_CHAN_PASSIVE_SCAN)
				ch->flags &= ~IEEE80211_CHAN_PASSIVE_SCAN;
	}

	ch = &sband->channels[12];	/* CH 13 */
	reg_rule = freq_reg_info(wiphy, ch->center_freq);
	if (!IS_ERR(reg_rule)) {
		if (!(reg_rule->flags & NL80211_RRF_PASSIVE_SCAN))
			if (ch->flags & IEEE80211_CHAN_PASSIVE_SCAN)
				ch->flags &= ~IEEE80211_CHAN_PASSIVE_SCAN;
	}
}

/*
 *Always apply Radar/DFS rules on
 *freq range 5260 MHz - 5700 MHz
 */
static void _rtl_reg_apply_radar_flags(struct wiphy *wiphy)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	unsigned int i;

	if (!wiphy->bands[NL80211_BAND_5GHZ])
		return;

	sband = wiphy->bands[NL80211_BAND_5GHZ];

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		if (!_rtl_is_radar_freq(ch->center_freq))
			continue;

		/*
		 *We always enable radar detection/DFS on this
		 *frequency range. Additionally we also apply on
		 *this frequency range:
		 *- If STA mode does not yet have DFS supports disable
		 * active scanning
		 *- If adhoc mode does not support DFS yet then disable
		 * adhoc in the frequency.
		 *- If AP mode does not yet support radar detection/DFS
		 *do not allow AP mode
		 */
		if (!(ch->flags & IEEE80211_CHAN_DISABLED))
			ch->flags |= IEEE80211_CHAN_RADAR |
			    IEEE80211_CHAN_NO_IBSS |
			    IEEE80211_CHAN_PASSIVE_SCAN;
	}
}

static void _rtl_reg_apply_world_flags(struct wiphy *wiphy,
				       enum nl80211_reg_initiator initiator,
				       struct rtl_regulatory *reg)
{
	_rtl_reg_apply_beaconing_flags(wiphy, initiator);
	_rtl_reg_apply_active_scan_flags(wiphy, initiator);
	return;
}

static int _rtl_reg_notifier_apply(struct wiphy *wiphy,
				   struct regulatory_request *request,
				   struct rtl_regulatory *reg)
{
	/* We always apply this */
	_rtl_reg_apply_radar_flags(wiphy);

	switch (request->initiator) {
	case NL80211_REGDOM_SET_BY_DRIVER:
	case NL80211_REGDOM_SET_BY_CORE:
	case NL80211_REGDOM_SET_BY_USER:
		break;
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		_rtl_reg_apply_world_flags(wiphy, request->initiator, reg);
		break;
	}

	return 0;
}

static const struct ieee80211_regdomain *_rtl_regdomain_select(
						struct rtl_regulatory *reg)
{
	switch (reg->country_code) {
	case COUNTRY_CODE_FCC:
		return &rtl_regdom_no_midband;
	case COUNTRY_CODE_IC:
		return &rtl_regdom_11;
	case COUNTRY_CODE_TELEC_NETGEAR:
		return &rtl_regdom_60_64;
	case COUNTRY_CODE_ETSI:
	case COUNTRY_CODE_SPAIN:
	case COUNTRY_CODE_FRANCE:
	case COUNTRY_CODE_ISRAEL:
		return &rtl_regdom_12_13;
	case COUNTRY_CODE_MKK:
	case COUNTRY_CODE_MKK1:
	case COUNTRY_CODE_TELEC:
	case COUNTRY_CODE_MIC:
		return &rtl_regdom_14_60_64;
	case COUNTRY_CODE_GLOBAL_DOMAIN:
		return &rtl_regdom_14;
	case COUNTRY_CODE_WORLD_WIDE_13:
	case COUNTRY_CODE_WORLD_WIDE_13_5G_ALL:
		return &rtl_regdom_12_13_5g_all;
	default:
		return &rtl_regdom_no_midband;
	}
}

static int _rtl_regd_init_wiphy(struct rtl_regulatory *reg,
				struct wiphy *wiphy,
				void (*reg_notifier)(struct wiphy *wiphy,
						     struct regulatory_request *
						     request))
{
	const struct ieee80211_regdomain *regd;

	wiphy->reg_notifier = reg_notifier;

	wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG;
	wiphy->regulatory_flags &= ~REGULATORY_STRICT_REG;
	wiphy->regulatory_flags &= ~REGULATORY_DISABLE_BEACON_HINTS;
	regd = _rtl_regdomain_select(reg);
	wiphy_apply_custom_regulatory(wiphy, regd);
	_rtl_reg_apply_radar_flags(wiphy);
	_rtl_reg_apply_world_flags(wiphy, NL80211_REGDOM_SET_BY_DRIVER, reg);
	return 0;
}

static struct country_code_to_enum_rd *_rtl_regd_find_country(u16 countrycode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(all_countries); i++) {
		if (all_countries[i].countrycode == countrycode)
			return &all_countries[i];
	}
	return NULL;
}

static u8 channel_plan_to_country_code(u8 channelplan)
{
	switch (channelplan) {
	case 0x20:
	case 0x21:
		return COUNTRY_CODE_WORLD_WIDE_13;
	case 0x22:
		return COUNTRY_CODE_IC;
	case 0x25:
		return COUNTRY_CODE_ETSI;
	case 0x32:
		return COUNTRY_CODE_TELEC_NETGEAR;
	case 0x41:
		return COUNTRY_CODE_GLOBAL_DOMAIN;
	case 0x7f:
		return COUNTRY_CODE_WORLD_WIDE_13_5G_ALL;
	default:
		return COUNTRY_CODE_MAX; /*Error*/
	}
}

int rtl_regd_init(struct ieee80211_hw *hw,
		  void (*reg_notifier)(struct wiphy *wiphy,
				       struct regulatory_request *request))
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct wiphy *wiphy = hw->wiphy;
	struct country_code_to_enum_rd *country = NULL;

	if (wiphy == NULL || &rtlpriv->regd == NULL)
		return -EINVAL;

	/* init country_code from efuse channel plan */
	rtlpriv->regd.country_code =
		channel_plan_to_country_code(rtlpriv->efuse.channel_plan);

	RT_TRACE(rtlpriv, COMP_REGD, DBG_DMESG,
		 "rtl: EEPROM regdomain: 0x%0x country code: %d\n",
		 rtlpriv->efuse.channel_plan, rtlpriv->regd.country_code);

	if (rtlpriv->regd.country_code >= COUNTRY_CODE_MAX) {
		RT_TRACE(rtlpriv, COMP_REGD, DBG_DMESG,
			 "rtl: EEPROM indicates invalid country code, world wide 13 should be used\n");

		rtlpriv->regd.country_code = COUNTRY_CODE_WORLD_WIDE_13;
	}

	country = _rtl_regd_find_country(rtlpriv->regd.country_code);

	if (country) {
		rtlpriv->regd.alpha2[0] = country->iso_name[0];
		rtlpriv->regd.alpha2[1] = country->iso_name[1];
	} else {
		rtlpriv->regd.alpha2[0] = '0';
		rtlpriv->regd.alpha2[1] = '0';
	}

	RT_TRACE(rtlpriv, COMP_REGD, DBG_TRACE,
		 "rtl: Country alpha2 being used: %c%c\n",
		  rtlpriv->regd.alpha2[0], rtlpriv->regd.alpha2[1]);

	_rtl_regd_init_wiphy(&rtlpriv->regd, wiphy, reg_notifier);

	return 0;
}

void rtl_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(rtlpriv, COMP_REGD, DBG_LOUD, "\n");

	_rtl_reg_notifier_apply(wiphy, request, &rtlpriv->regd);
}
