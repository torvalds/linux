/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
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

#include <linux/kernel.h>
#include <linux/export.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include "regd.h"
#include "regd_common.h"

/*
 * This is a set of common rules used by our world regulatory domains.
 * We have 12 world regulatory domains. To save space we consolidate
 * the regulatory domains in 5 structures by frequency and change
 * the flags on our reg_notifier() on a case by case basis.
 */

/* Only these channels all allow active scan on all world regulatory domains */
#define ATH9K_2GHZ_CH01_11	REG_RULE(2412-10, 2462+10, 40, 0, 20, 0)

/* We enable active scan on these a case by case basis by regulatory domain */
#define ATH9K_2GHZ_CH12_13	REG_RULE(2467-10, 2472+10, 40, 0, 20,\
					NL80211_RRF_PASSIVE_SCAN)
#define ATH9K_2GHZ_CH14		REG_RULE(2484-10, 2484+10, 40, 0, 20,\
				NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_OFDM)

/* We allow IBSS on these on a case by case basis by regulatory domain */
#define ATH9K_5GHZ_5150_5350	REG_RULE(5150-10, 5350+10, 40, 0, 30,\
				NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_IBSS)
#define ATH9K_5GHZ_5470_5850	REG_RULE(5470-10, 5850+10, 40, 0, 30,\
				NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_IBSS)
#define ATH9K_5GHZ_5725_5850	REG_RULE(5725-10, 5850+10, 40, 0, 30,\
				NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_IBSS)

#define ATH9K_2GHZ_ALL		ATH9K_2GHZ_CH01_11, \
				ATH9K_2GHZ_CH12_13, \
				ATH9K_2GHZ_CH14

#define ATH9K_5GHZ_ALL		ATH9K_5GHZ_5150_5350, \
				ATH9K_5GHZ_5470_5850

/* This one skips what we call "mid band" */
#define ATH9K_5GHZ_NO_MIDBAND	ATH9K_5GHZ_5150_5350, \
				ATH9K_5GHZ_5725_5850

/* Can be used for:
 * 0x60, 0x61, 0x62 */
static const struct ieee80211_regdomain ath_world_regdom_60_61_62 = {
	.n_reg_rules = 5,
	.alpha2 =  "99",
	.reg_rules = {
		ATH9K_2GHZ_ALL,
		ATH9K_5GHZ_ALL,
	}
};

/* Can be used by 0x63 and 0x65 */
static const struct ieee80211_regdomain ath_world_regdom_63_65 = {
	.n_reg_rules = 4,
	.alpha2 =  "99",
	.reg_rules = {
		ATH9K_2GHZ_CH01_11,
		ATH9K_2GHZ_CH12_13,
		ATH9K_5GHZ_NO_MIDBAND,
	}
};

/* Can be used by 0x64 only */
static const struct ieee80211_regdomain ath_world_regdom_64 = {
	.n_reg_rules = 3,
	.alpha2 =  "99",
	.reg_rules = {
		ATH9K_2GHZ_CH01_11,
		ATH9K_5GHZ_NO_MIDBAND,
	}
};

/* Can be used by 0x66 and 0x69 */
static const struct ieee80211_regdomain ath_world_regdom_66_69 = {
	.n_reg_rules = 3,
	.alpha2 =  "99",
	.reg_rules = {
		ATH9K_2GHZ_CH01_11,
		ATH9K_5GHZ_ALL,
	}
};

/* Can be used by 0x67, 0x68, 0x6A and 0x6C */
static const struct ieee80211_regdomain ath_world_regdom_67_68_6A_6C = {
	.n_reg_rules = 4,
	.alpha2 =  "99",
	.reg_rules = {
		ATH9K_2GHZ_CH01_11,
		ATH9K_2GHZ_CH12_13,
		ATH9K_5GHZ_ALL,
	}
};

static inline bool is_wwr_sku(u16 regd)
{
	return ((regd & COUNTRY_ERD_FLAG) != COUNTRY_ERD_FLAG) &&
		(((regd & WORLD_SKU_MASK) == WORLD_SKU_PREFIX) ||
		(regd == WORLD));
}

static u16 ath_regd_get_eepromRD(struct ath_regulatory *reg)
{
	return reg->current_rd & ~WORLDWIDE_ROAMING_FLAG;
}

bool ath_is_world_regd(struct ath_regulatory *reg)
{
	return is_wwr_sku(ath_regd_get_eepromRD(reg));
}
EXPORT_SYMBOL(ath_is_world_regd);

static const struct ieee80211_regdomain *ath_default_world_regdomain(void)
{
	/* this is the most restrictive */
	return &ath_world_regdom_64;
}

static const struct
ieee80211_regdomain *ath_world_regdomain(struct ath_regulatory *reg)
{
	switch (reg->regpair->regDmnEnum) {
	case 0x60:
	case 0x61:
	case 0x62:
		return &ath_world_regdom_60_61_62;
	case 0x63:
	case 0x65:
		return &ath_world_regdom_63_65;
	case 0x64:
		return &ath_world_regdom_64;
	case 0x66:
	case 0x69:
		return &ath_world_regdom_66_69;
	case 0x67:
	case 0x68:
	case 0x6A:
	case 0x6C:
		return &ath_world_regdom_67_68_6A_6C;
	default:
		WARN_ON(1);
		return ath_default_world_regdomain();
	}
}

bool ath_is_49ghz_allowed(u16 regdomain)
{
	/* possibly more */
	return regdomain == MKK9_MKKC;
}
EXPORT_SYMBOL(ath_is_49ghz_allowed);

/* Frequency is one where radar detection is required */
static bool ath_is_radar_freq(u16 center_freq)
{
	return (center_freq >= 5260 && center_freq <= 5700);
}

/*
 * N.B: These exception rules do not apply radar freqs.
 *
 * - We enable adhoc (or beaconing) if allowed by 11d
 * - We enable active scan if the channel is allowed by 11d
 * - If no country IE has been processed and a we determine we have
 *   received a beacon on a channel we can enable active scan and
 *   adhoc (or beaconing).
 */
static void
ath_reg_apply_beaconing_flags(struct wiphy *wiphy,
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

			if (ath_is_radar_freq(ch->center_freq) ||
			    (ch->flags & IEEE80211_CHAN_RADAR))
				continue;

			if (initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE) {
				r = freq_reg_info(wiphy,
						  ch->center_freq,
						  bandwidth,
						  &reg_rule);
				if (r)
					continue;
				/*
				 * If 11d had a rule for this channel ensure
				 * we enable adhoc/beaconing if it allows us to
				 * use it. Note that we would have disabled it
				 * by applying our static world regdomain by
				 * default during init, prior to calling our
				 * regulatory_hint().
				 */
				if (!(reg_rule->flags &
				    NL80211_RRF_NO_IBSS))
					ch->flags &=
					  ~IEEE80211_CHAN_NO_IBSS;
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
static void
ath_reg_apply_active_scan_flags(struct wiphy *wiphy,
				enum nl80211_reg_initiator initiator)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	const struct ieee80211_reg_rule *reg_rule;
	u32 bandwidth = 0;
	int r;

	sband = wiphy->bands[IEEE80211_BAND_2GHZ];

	/*
	 * If no country IE has been received always enable active scan
	 * on these channels. This is only done for specific regulatory SKUs
	 */
	if (initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE) {
		ch = &sband->channels[11]; /* CH 12 */
		if (ch->flags & IEEE80211_CHAN_PASSIVE_SCAN)
			ch->flags &= ~IEEE80211_CHAN_PASSIVE_SCAN;
		ch = &sband->channels[12]; /* CH 13 */
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

	ch = &sband->channels[11]; /* CH 12 */
	r = freq_reg_info(wiphy, ch->center_freq, bandwidth, &reg_rule);
	if (!r) {
		if (!(reg_rule->flags & NL80211_RRF_PASSIVE_SCAN))
			if (ch->flags & IEEE80211_CHAN_PASSIVE_SCAN)
				ch->flags &= ~IEEE80211_CHAN_PASSIVE_SCAN;
	}

	ch = &sband->channels[12]; /* CH 13 */
	r = freq_reg_info(wiphy, ch->center_freq, bandwidth, &reg_rule);
	if (!r) {
		if (!(reg_rule->flags & NL80211_RRF_PASSIVE_SCAN))
			if (ch->flags & IEEE80211_CHAN_PASSIVE_SCAN)
				ch->flags &= ~IEEE80211_CHAN_PASSIVE_SCAN;
	}
}

/* Always apply Radar/DFS rules on freq range 5260 MHz - 5700 MHz */
static void ath_reg_apply_radar_flags(struct wiphy *wiphy)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	unsigned int i;

	if (!wiphy->bands[IEEE80211_BAND_5GHZ])
		return;

	sband = wiphy->bands[IEEE80211_BAND_5GHZ];

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		if (!ath_is_radar_freq(ch->center_freq))
			continue;
		/* We always enable radar detection/DFS on this
		 * frequency range. Additionally we also apply on
		 * this frequency range:
		 * - If STA mode does not yet have DFS supports disable
		 *   active scanning
		 * - If adhoc mode does not support DFS yet then
		 *   disable adhoc in the frequency.
		 * - If AP mode does not yet support radar detection/DFS
		 *   do not allow AP mode
		 */
		if (!(ch->flags & IEEE80211_CHAN_DISABLED))
			ch->flags |= IEEE80211_CHAN_RADAR |
				     IEEE80211_CHAN_NO_IBSS |
				     IEEE80211_CHAN_PASSIVE_SCAN;
	}
}

static void ath_reg_apply_world_flags(struct wiphy *wiphy,
				      enum nl80211_reg_initiator initiator,
				      struct ath_regulatory *reg)
{
	switch (reg->regpair->regDmnEnum) {
	case 0x60:
	case 0x63:
	case 0x66:
	case 0x67:
	case 0x6C:
		ath_reg_apply_beaconing_flags(wiphy, initiator);
		break;
	case 0x68:
		ath_reg_apply_beaconing_flags(wiphy, initiator);
		ath_reg_apply_active_scan_flags(wiphy, initiator);
		break;
	}
}

int ath_reg_notifier_apply(struct wiphy *wiphy,
			   struct regulatory_request *request,
			   struct ath_regulatory *reg)
{
	/* We always apply this */
	ath_reg_apply_radar_flags(wiphy);

	/*
	 * This would happen when we have sent a custom regulatory request
	 * a world regulatory domain and the scheduler hasn't yet processed
	 * any pending requests in the queue.
	 */
	if (!request)
		return 0;

	switch (request->initiator) {
	case NL80211_REGDOM_SET_BY_DRIVER:
	case NL80211_REGDOM_SET_BY_CORE:
	case NL80211_REGDOM_SET_BY_USER:
		break;
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		if (ath_is_world_regd(reg))
			ath_reg_apply_world_flags(wiphy, request->initiator,
						  reg);
		break;
	}

	return 0;
}
EXPORT_SYMBOL(ath_reg_notifier_apply);

static bool ath_regd_is_eeprom_valid(struct ath_regulatory *reg)
{
	u16 rd = ath_regd_get_eepromRD(reg);
	int i;

	if (rd & COUNTRY_ERD_FLAG) {
		/* EEPROM value is a country code */
		u16 cc = rd & ~COUNTRY_ERD_FLAG;
		printk(KERN_DEBUG
		       "ath: EEPROM indicates we should expect "
			"a country code\n");
		for (i = 0; i < ARRAY_SIZE(allCountries); i++)
			if (allCountries[i].countryCode == cc)
				return true;
	} else {
		/* EEPROM value is a regpair value */
		if (rd != CTRY_DEFAULT)
			printk(KERN_DEBUG "ath: EEPROM indicates we "
			       "should expect a direct regpair map\n");
		for (i = 0; i < ARRAY_SIZE(regDomainPairs); i++)
			if (regDomainPairs[i].regDmnEnum == rd)
				return true;
	}
	printk(KERN_DEBUG
		 "ath: invalid regulatory domain/country code 0x%x\n", rd);
	return false;
}

/* EEPROM country code to regpair mapping */
static struct country_code_to_enum_rd*
ath_regd_find_country(u16 countryCode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(allCountries); i++) {
		if (allCountries[i].countryCode == countryCode)
			return &allCountries[i];
	}
	return NULL;
}

/* EEPROM rd code to regpair mapping */
static struct country_code_to_enum_rd*
ath_regd_find_country_by_rd(int regdmn)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(allCountries); i++) {
		if (allCountries[i].regDmnEnum == regdmn)
			return &allCountries[i];
	}
	return NULL;
}

/* Returns the map of the EEPROM set RD to a country code */
static u16 ath_regd_get_default_country(u16 rd)
{
	if (rd & COUNTRY_ERD_FLAG) {
		struct country_code_to_enum_rd *country = NULL;
		u16 cc = rd & ~COUNTRY_ERD_FLAG;

		country = ath_regd_find_country(cc);
		if (country != NULL)
			return cc;
	}

	return CTRY_DEFAULT;
}

static struct reg_dmn_pair_mapping*
ath_get_regpair(int regdmn)
{
	int i;

	if (regdmn == NO_ENUMRD)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(regDomainPairs); i++) {
		if (regDomainPairs[i].regDmnEnum == regdmn)
			return &regDomainPairs[i];
	}
	return NULL;
}

static int
ath_regd_init_wiphy(struct ath_regulatory *reg,
		    struct wiphy *wiphy,
		    int (*reg_notifier)(struct wiphy *wiphy,
					struct regulatory_request *request))
{
	const struct ieee80211_regdomain *regd;

	wiphy->reg_notifier = reg_notifier;
	wiphy->flags |= WIPHY_FLAG_STRICT_REGULATORY;

	if (ath_is_world_regd(reg)) {
		/*
		 * Anything applied here (prior to wiphy registration) gets
		 * saved on the wiphy orig_* parameters
		 */
		regd = ath_world_regdomain(reg);
		wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
	} else {
		/*
		 * This gets applied in the case of the absence of CRDA,
		 * it's our own custom world regulatory domain, similar to
		 * cfg80211's but we enable passive scanning.
		 */
		regd = ath_default_world_regdomain();
	}
	wiphy_apply_custom_regulatory(wiphy, regd);
	ath_reg_apply_radar_flags(wiphy);
	ath_reg_apply_world_flags(wiphy, NL80211_REGDOM_SET_BY_DRIVER, reg);
	return 0;
}

/*
 * Some users have reported their EEPROM programmed with
 * 0x8000 set, this is not a supported regulatory domain
 * but since we have more than one user with it we need
 * a solution for them. We default to 0x64, which is the
 * default Atheros world regulatory domain.
 */
static void ath_regd_sanitize(struct ath_regulatory *reg)
{
	if (reg->current_rd != COUNTRY_ERD_FLAG)
		return;
	printk(KERN_DEBUG "ath: EEPROM regdomain sanitized\n");
	reg->current_rd = 0x64;
}

int
ath_regd_init(struct ath_regulatory *reg,
	      struct wiphy *wiphy,
	      int (*reg_notifier)(struct wiphy *wiphy,
				  struct regulatory_request *request))
{
	struct country_code_to_enum_rd *country = NULL;
	u16 regdmn;

	if (!reg)
		return -EINVAL;

	ath_regd_sanitize(reg);

	printk(KERN_DEBUG "ath: EEPROM regdomain: 0x%0x\n", reg->current_rd);

	if (!ath_regd_is_eeprom_valid(reg)) {
		printk(KERN_ERR "ath: Invalid EEPROM contents\n");
		return -EINVAL;
	}

	regdmn = ath_regd_get_eepromRD(reg);
	reg->country_code = ath_regd_get_default_country(regdmn);

	if (reg->country_code == CTRY_DEFAULT &&
	    regdmn == CTRY_DEFAULT) {
		printk(KERN_DEBUG "ath: EEPROM indicates default "
		       "country code should be used\n");
		reg->country_code = CTRY_UNITED_STATES;
	}

	if (reg->country_code == CTRY_DEFAULT) {
		country = NULL;
	} else {
		printk(KERN_DEBUG "ath: doing EEPROM country->regdmn "
		       "map search\n");
		country = ath_regd_find_country(reg->country_code);
		if (country == NULL) {
			printk(KERN_DEBUG
				"ath: no valid country maps found for "
				"country code: 0x%0x\n",
				reg->country_code);
			return -EINVAL;
		} else {
			regdmn = country->regDmnEnum;
			printk(KERN_DEBUG "ath: country maps to "
			       "regdmn code: 0x%0x\n",
			       regdmn);
		}
	}

	reg->regpair = ath_get_regpair(regdmn);

	if (!reg->regpair) {
		printk(KERN_DEBUG "ath: "
			"No regulatory domain pair found, cannot continue\n");
		return -EINVAL;
	}

	if (!country)
		country = ath_regd_find_country_by_rd(regdmn);

	if (country) {
		reg->alpha2[0] = country->isoName[0];
		reg->alpha2[1] = country->isoName[1];
	} else {
		reg->alpha2[0] = '0';
		reg->alpha2[1] = '0';
	}

	printk(KERN_DEBUG "ath: Country alpha2 being used: %c%c\n",
		reg->alpha2[0], reg->alpha2[1]);
	printk(KERN_DEBUG "ath: Regpair used: 0x%0x\n",
		reg->regpair->regDmnEnum);

	ath_regd_init_wiphy(reg, wiphy, reg_notifier);
	return 0;
}
EXPORT_SYMBOL(ath_regd_init);

u32 ath_regd_get_band_ctl(struct ath_regulatory *reg,
			  enum ieee80211_band band)
{
	if (!reg->regpair ||
	    (reg->country_code == CTRY_DEFAULT &&
	     is_wwr_sku(ath_regd_get_eepromRD(reg)))) {
		return SD_NO_CTL;
	}

	switch (band) {
	case IEEE80211_BAND_2GHZ:
		return reg->regpair->reg_2ghz_ctl;
	case IEEE80211_BAND_5GHZ:
		return reg->regpair->reg_5ghz_ctl;
	default:
		return NO_CTL;
	}
}
EXPORT_SYMBOL(ath_regd_get_band_ctl);
