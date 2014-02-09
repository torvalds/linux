/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/types.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/regulatory.h>

#include <defs.h>
#include "pub.h"
#include "phy/phy_hal.h"
#include "main.h"
#include "stf.h"
#include "channel.h"
#include "mac80211_if.h"
#include "debug.h"

/* QDB() macro takes a dB value and converts to a quarter dB value */
#define QDB(n) ((n) * BRCMS_TXPWR_DB_FACTOR)

#define LOCALE_MIMO_IDX_bn		0
#define LOCALE_MIMO_IDX_11n		0

/* max of BAND_5G_PWR_LVLS and 14 for 2.4 GHz */
#define BRCMS_MAXPWR_MIMO_TBL_SIZE	14

/* maxpwr mapping to 5GHz band channels:
 * maxpwr[0] - channels [34-48]
 * maxpwr[1] - channels [52-60]
 * maxpwr[2] - channels [62-64]
 * maxpwr[3] - channels [100-140]
 * maxpwr[4] - channels [149-165]
 */
#define BAND_5G_PWR_LVLS	5	/* 5 power levels for 5G */

#define LC(id)	LOCALE_MIMO_IDX_ ## id

#define LOCALES(mimo2, mimo5) \
		{LC(mimo2), LC(mimo5)}

/* macro to get 5 GHz channel group index for tx power */
#define CHANNEL_POWER_IDX_5G(c) (((c) < 52) ? 0 : \
				 (((c) < 62) ? 1 : \
				 (((c) < 100) ? 2 : \
				 (((c) < 149) ? 3 : 4))))

#define BRCM_2GHZ_2412_2462	REG_RULE(2412-10, 2462+10, 40, 0, 19, 0)
#define BRCM_2GHZ_2467_2472	REG_RULE(2467-10, 2472+10, 20, 0, 19, \
					 NL80211_RRF_NO_IR)

#define BRCM_5GHZ_5180_5240	REG_RULE(5180-10, 5240+10, 40, 0, 21, \
					 NL80211_RRF_NO_IR)
#define BRCM_5GHZ_5260_5320	REG_RULE(5260-10, 5320+10, 40, 0, 21, \
					 NL80211_RRF_DFS | \
					 NL80211_RRF_NO_IR)
#define BRCM_5GHZ_5500_5700	REG_RULE(5500-10, 5700+10, 40, 0, 21, \
					 NL80211_RRF_DFS | \
					 NL80211_RRF_NO_IR)
#define BRCM_5GHZ_5745_5825	REG_RULE(5745-10, 5825+10, 40, 0, 21, \
					 NL80211_RRF_NO_IR)

static const struct ieee80211_regdomain brcms_regdom_x2 = {
	.n_reg_rules = 6,
	.alpha2 = "X2",
	.reg_rules = {
		BRCM_2GHZ_2412_2462,
		BRCM_2GHZ_2467_2472,
		BRCM_5GHZ_5180_5240,
		BRCM_5GHZ_5260_5320,
		BRCM_5GHZ_5500_5700,
		BRCM_5GHZ_5745_5825,
	}
};

 /* locale per-channel tx power limits for MIMO frames
  * maxpwr arrays are index by channel for 2.4 GHz limits, and
  * by sub-band for 5 GHz limits using CHANNEL_POWER_IDX_5G(channel)
  */
struct locale_mimo_info {
	/* tx 20 MHz power limits, qdBm units */
	s8 maxpwr20[BRCMS_MAXPWR_MIMO_TBL_SIZE];
	/* tx 40 MHz power limits, qdBm units */
	s8 maxpwr40[BRCMS_MAXPWR_MIMO_TBL_SIZE];
};

/* Country names and abbreviations with locale defined from ISO 3166 */
struct country_info {
	const u8 locale_mimo_2G;	/* 2.4G mimo info */
	const u8 locale_mimo_5G;	/* 5G mimo info */
};

struct brcms_regd {
	struct country_info country;
	const struct ieee80211_regdomain *regdomain;
};

struct brcms_cm_info {
	struct brcms_pub *pub;
	struct brcms_c_info *wlc;
	const struct brcms_regd *world_regd;
};

/*
 * MIMO Locale Definitions - 2.4 GHz
 */
static const struct locale_mimo_info locale_bn = {
	{QDB(13), QDB(13), QDB(13), QDB(13), QDB(13),
	 QDB(13), QDB(13), QDB(13), QDB(13), QDB(13),
	 QDB(13), QDB(13), QDB(13)},
	{0, 0, QDB(13), QDB(13), QDB(13),
	 QDB(13), QDB(13), QDB(13), QDB(13), QDB(13),
	 QDB(13), 0, 0},
};

static const struct locale_mimo_info *g_mimo_2g_table[] = {
	&locale_bn
};

/*
 * MIMO Locale Definitions - 5 GHz
 */
static const struct locale_mimo_info locale_11n = {
	{ /* 12.5 dBm */ 50, 50, 50, QDB(15), QDB(15)},
	{QDB(14), QDB(15), QDB(15), QDB(15), QDB(15)},
};

static const struct locale_mimo_info *g_mimo_5g_table[] = {
	&locale_11n
};

static const struct brcms_regd cntry_locales[] = {
	/* Worldwide RoW 2, must always be at index 0 */
	{
		.country = LOCALES(bn, 11n),
		.regdomain = &brcms_regdom_x2,
	},
};

static const struct locale_mimo_info *brcms_c_get_mimo_2g(u8 locale_idx)
{
	if (locale_idx >= ARRAY_SIZE(g_mimo_2g_table))
		return NULL;

	return g_mimo_2g_table[locale_idx];
}

static const struct locale_mimo_info *brcms_c_get_mimo_5g(u8 locale_idx)
{
	if (locale_idx >= ARRAY_SIZE(g_mimo_5g_table))
		return NULL;

	return g_mimo_5g_table[locale_idx];
}

/*
 * Indicates whether the country provided is valid to pass
 * to cfg80211 or not.
 *
 * returns true if valid; false if not.
 */
static bool brcms_c_country_valid(const char *ccode)
{
	/*
	 * only allow ascii alpha uppercase for the first 2
	 * chars.
	 */
	if (!((0x80 & ccode[0]) == 0 && ccode[0] >= 0x41 && ccode[0] <= 0x5A &&
	      (0x80 & ccode[1]) == 0 && ccode[1] >= 0x41 && ccode[1] <= 0x5A))
		return false;

	/*
	 * do not match ISO 3166-1 user assigned country codes
	 * that may be in the driver table
	 */
	if (!strcmp("AA", ccode) ||        /* AA */
	    !strcmp("ZZ", ccode) ||        /* ZZ */
	    ccode[0] == 'X' ||             /* XA - XZ */
	    (ccode[0] == 'Q' &&            /* QM - QZ */
	     (ccode[1] >= 'M' && ccode[1] <= 'Z')))
		return false;

	if (!strcmp("NA", ccode))
		return false;

	return true;
}

static const struct brcms_regd *brcms_world_regd(const char *regdom, int len)
{
	const struct brcms_regd *regd = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(cntry_locales); i++) {
		if (!strncmp(regdom, cntry_locales[i].regdomain->alpha2, len)) {
			regd = &cntry_locales[i];
			break;
		}
	}

	return regd;
}

static const struct brcms_regd *brcms_default_world_regd(void)
{
	return &cntry_locales[0];
}

/* JP, J1 - J10 are Japan ccodes */
static bool brcms_c_japan_ccode(const char *ccode)
{
	return (ccode[0] == 'J' &&
		(ccode[1] == 'P' || (ccode[1] >= '1' && ccode[1] <= '9')));
}

static void
brcms_c_channel_min_txpower_limits_with_local_constraint(
		struct brcms_cm_info *wlc_cm, struct txpwr_limits *txpwr,
		u8 local_constraint_qdbm)
{
	int j;

	/* CCK Rates */
	for (j = 0; j < WL_TX_POWER_CCK_NUM; j++)
		txpwr->cck[j] = min(txpwr->cck[j], local_constraint_qdbm);

	/* 20 MHz Legacy OFDM SISO */
	for (j = 0; j < WL_TX_POWER_OFDM_NUM; j++)
		txpwr->ofdm[j] = min(txpwr->ofdm[j], local_constraint_qdbm);

	/* 20 MHz Legacy OFDM CDD */
	for (j = 0; j < BRCMS_NUM_RATES_OFDM; j++)
		txpwr->ofdm_cdd[j] =
		    min(txpwr->ofdm_cdd[j], local_constraint_qdbm);

	/* 40 MHz Legacy OFDM SISO */
	for (j = 0; j < BRCMS_NUM_RATES_OFDM; j++)
		txpwr->ofdm_40_siso[j] =
		    min(txpwr->ofdm_40_siso[j], local_constraint_qdbm);

	/* 40 MHz Legacy OFDM CDD */
	for (j = 0; j < BRCMS_NUM_RATES_OFDM; j++)
		txpwr->ofdm_40_cdd[j] =
		    min(txpwr->ofdm_40_cdd[j], local_constraint_qdbm);

	/* 20MHz MCS 0-7 SISO */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_20_siso[j] =
		    min(txpwr->mcs_20_siso[j], local_constraint_qdbm);

	/* 20MHz MCS 0-7 CDD */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_20_cdd[j] =
		    min(txpwr->mcs_20_cdd[j], local_constraint_qdbm);

	/* 20MHz MCS 0-7 STBC */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_20_stbc[j] =
		    min(txpwr->mcs_20_stbc[j], local_constraint_qdbm);

	/* 20MHz MCS 8-15 MIMO */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_2_STREAM; j++)
		txpwr->mcs_20_mimo[j] =
		    min(txpwr->mcs_20_mimo[j], local_constraint_qdbm);

	/* 40MHz MCS 0-7 SISO */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_40_siso[j] =
		    min(txpwr->mcs_40_siso[j], local_constraint_qdbm);

	/* 40MHz MCS 0-7 CDD */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_40_cdd[j] =
		    min(txpwr->mcs_40_cdd[j], local_constraint_qdbm);

	/* 40MHz MCS 0-7 STBC */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_40_stbc[j] =
		    min(txpwr->mcs_40_stbc[j], local_constraint_qdbm);

	/* 40MHz MCS 8-15 MIMO */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_2_STREAM; j++)
		txpwr->mcs_40_mimo[j] =
		    min(txpwr->mcs_40_mimo[j], local_constraint_qdbm);

	/* 40MHz MCS 32 */
	txpwr->mcs32 = min(txpwr->mcs32, local_constraint_qdbm);

}

/*
 * set the driver's current country and regulatory information
 * using a country code as the source. Look up built in country
 * information found with the country code.
 */
static void
brcms_c_set_country(struct brcms_cm_info *wlc_cm,
		    const struct brcms_regd *regd)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;

	if ((wlc->pub->_n_enab & SUPPORT_11N) !=
	    wlc->protection->nmode_user)
		brcms_c_set_nmode(wlc);

	brcms_c_stf_ss_update(wlc, wlc->bandstate[BAND_2G_INDEX]);
	brcms_c_stf_ss_update(wlc, wlc->bandstate[BAND_5G_INDEX]);

	brcms_c_set_gmode(wlc, wlc->protection->gmode_user, false);

	return;
}

struct brcms_cm_info *brcms_c_channel_mgr_attach(struct brcms_c_info *wlc)
{
	struct brcms_cm_info *wlc_cm;
	struct brcms_pub *pub = wlc->pub;
	struct ssb_sprom *sprom = &wlc->hw->d11core->bus->sprom;
	const char *ccode = sprom->alpha2;
	int ccode_len = sizeof(sprom->alpha2);

	wlc_cm = kzalloc(sizeof(struct brcms_cm_info), GFP_ATOMIC);
	if (wlc_cm == NULL)
		return NULL;
	wlc_cm->pub = pub;
	wlc_cm->wlc = wlc;
	wlc->cmi = wlc_cm;

	/* store the country code for passing up as a regulatory hint */
	wlc_cm->world_regd = brcms_world_regd(ccode, ccode_len);
	if (brcms_c_country_valid(ccode))
		strncpy(wlc->pub->srom_ccode, ccode, ccode_len);

	/*
	 * If no custom world domain is found in the SROM, use the
	 * default "X2" domain.
	 */
	if (!wlc_cm->world_regd) {
		wlc_cm->world_regd = brcms_default_world_regd();
		ccode = wlc_cm->world_regd->regdomain->alpha2;
		ccode_len = BRCM_CNTRY_BUF_SZ - 1;
	}

	/* save default country for exiting 11d regulatory mode */
	strncpy(wlc->country_default, ccode, ccode_len);

	/* initialize autocountry_default to driver default */
	strncpy(wlc->autocountry_default, ccode, ccode_len);

	brcms_c_set_country(wlc_cm, wlc_cm->world_regd);

	return wlc_cm;
}

void brcms_c_channel_mgr_detach(struct brcms_cm_info *wlc_cm)
{
	kfree(wlc_cm);
}

void
brcms_c_channel_set_chanspec(struct brcms_cm_info *wlc_cm, u16 chanspec,
			 u8 local_constraint_qdbm)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;
	struct ieee80211_channel *ch = wlc->pub->ieee_hw->conf.chandef.chan;
	struct txpwr_limits txpwr;

	brcms_c_channel_reg_limits(wlc_cm, chanspec, &txpwr);

	brcms_c_channel_min_txpower_limits_with_local_constraint(
		wlc_cm, &txpwr, local_constraint_qdbm
	);

	/* set or restore gmode as required by regulatory */
	if (ch->flags & IEEE80211_CHAN_NO_OFDM)
		brcms_c_set_gmode(wlc, GMODE_LEGACY_B, false);
	else
		brcms_c_set_gmode(wlc, wlc->protection->gmode_user, false);

	brcms_b_set_chanspec(wlc->hw, chanspec,
			      !!(ch->flags & IEEE80211_CHAN_NO_IR),
			      &txpwr);
}

void
brcms_c_channel_reg_limits(struct brcms_cm_info *wlc_cm, u16 chanspec,
		       struct txpwr_limits *txpwr)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;
	struct ieee80211_channel *ch = wlc->pub->ieee_hw->conf.chandef.chan;
	uint i;
	uint chan;
	int maxpwr;
	int delta;
	const struct country_info *country;
	struct brcms_band *band;
	int conducted_max = BRCMS_TXPWR_MAX;
	const struct locale_mimo_info *li_mimo;
	int maxpwr20, maxpwr40;
	int maxpwr_idx;
	uint j;

	memset(txpwr, 0, sizeof(struct txpwr_limits));

	if (WARN_ON(!ch))
		return;

	country = &wlc_cm->world_regd->country;

	chan = CHSPEC_CHANNEL(chanspec);
	band = wlc->bandstate[chspec_bandunit(chanspec)];
	li_mimo = (band->bandtype == BRCM_BAND_5G) ?
	    brcms_c_get_mimo_5g(country->locale_mimo_5G) :
	    brcms_c_get_mimo_2g(country->locale_mimo_2G);

	delta = band->antgain;

	if (band->bandtype == BRCM_BAND_2G)
		conducted_max = QDB(22);

	maxpwr = QDB(ch->max_power) - delta;
	maxpwr = max(maxpwr, 0);
	maxpwr = min(maxpwr, conducted_max);

	/* CCK txpwr limits for 2.4G band */
	if (band->bandtype == BRCM_BAND_2G) {
		for (i = 0; i < BRCMS_NUM_RATES_CCK; i++)
			txpwr->cck[i] = (u8) maxpwr;
	}

	for (i = 0; i < BRCMS_NUM_RATES_OFDM; i++) {
		txpwr->ofdm[i] = (u8) maxpwr;

		/*
		 * OFDM 40 MHz SISO has the same power as the corresponding
		 * MCS0-7 rate unless overriden by the locale specific code.
		 * We set this value to 0 as a flag (presumably 0 dBm isn't
		 * a possibility) and then copy the MCS0-7 value to the 40 MHz
		 * value if it wasn't explicitly set.
		 */
		txpwr->ofdm_40_siso[i] = 0;

		txpwr->ofdm_cdd[i] = (u8) maxpwr;

		txpwr->ofdm_40_cdd[i] = 0;
	}

	delta = 0;
	if (band->antgain > QDB(6))
		delta = band->antgain - QDB(6);	/* Excess over 6 dB */

	if (band->bandtype == BRCM_BAND_2G)
		maxpwr_idx = (chan - 1);
	else
		maxpwr_idx = CHANNEL_POWER_IDX_5G(chan);

	maxpwr20 = li_mimo->maxpwr20[maxpwr_idx];
	maxpwr40 = li_mimo->maxpwr40[maxpwr_idx];

	maxpwr20 = maxpwr20 - delta;
	maxpwr20 = max(maxpwr20, 0);
	maxpwr40 = maxpwr40 - delta;
	maxpwr40 = max(maxpwr40, 0);

	/* Fill in the MCS 0-7 (SISO) rates */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {

		/*
		 * 20 MHz has the same power as the corresponding OFDM rate
		 * unless overriden by the locale specific code.
		 */
		txpwr->mcs_20_siso[i] = txpwr->ofdm[i];
		txpwr->mcs_40_siso[i] = 0;
	}

	/* Fill in the MCS 0-7 CDD rates */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {
		txpwr->mcs_20_cdd[i] = (u8) maxpwr20;
		txpwr->mcs_40_cdd[i] = (u8) maxpwr40;
	}

	/*
	 * These locales have SISO expressed in the
	 * table and override CDD later
	 */
	if (li_mimo == &locale_bn) {
		if (li_mimo == &locale_bn) {
			maxpwr20 = QDB(16);
			maxpwr40 = 0;

			if (chan >= 3 && chan <= 11)
				maxpwr40 = QDB(16);
		}

		for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {
			txpwr->mcs_20_siso[i] = (u8) maxpwr20;
			txpwr->mcs_40_siso[i] = (u8) maxpwr40;
		}
	}

	/* Fill in the MCS 0-7 STBC rates */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {
		txpwr->mcs_20_stbc[i] = 0;
		txpwr->mcs_40_stbc[i] = 0;
	}

	/* Fill in the MCS 8-15 SDM rates */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_2_STREAM; i++) {
		txpwr->mcs_20_mimo[i] = (u8) maxpwr20;
		txpwr->mcs_40_mimo[i] = (u8) maxpwr40;
	}

	/* Fill in MCS32 */
	txpwr->mcs32 = (u8) maxpwr40;

	for (i = 0, j = 0; i < BRCMS_NUM_RATES_OFDM; i++, j++) {
		if (txpwr->ofdm_40_cdd[i] == 0)
			txpwr->ofdm_40_cdd[i] = txpwr->mcs_40_cdd[j];
		if (i == 0) {
			i = i + 1;
			if (txpwr->ofdm_40_cdd[i] == 0)
				txpwr->ofdm_40_cdd[i] = txpwr->mcs_40_cdd[j];
		}
	}

	/*
	 * Copy the 40 MHZ MCS 0-7 CDD value to the 40 MHZ MCS 0-7 SISO
	 * value if it wasn't provided explicitly.
	 */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {
		if (txpwr->mcs_40_siso[i] == 0)
			txpwr->mcs_40_siso[i] = txpwr->mcs_40_cdd[i];
	}

	for (i = 0, j = 0; i < BRCMS_NUM_RATES_OFDM; i++, j++) {
		if (txpwr->ofdm_40_siso[i] == 0)
			txpwr->ofdm_40_siso[i] = txpwr->mcs_40_siso[j];
		if (i == 0) {
			i = i + 1;
			if (txpwr->ofdm_40_siso[i] == 0)
				txpwr->ofdm_40_siso[i] = txpwr->mcs_40_siso[j];
		}
	}

	/*
	 * Copy the 20 and 40 MHz MCS0-7 CDD values to the corresponding
	 * STBC values if they weren't provided explicitly.
	 */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {
		if (txpwr->mcs_20_stbc[i] == 0)
			txpwr->mcs_20_stbc[i] = txpwr->mcs_20_cdd[i];

		if (txpwr->mcs_40_stbc[i] == 0)
			txpwr->mcs_40_stbc[i] = txpwr->mcs_40_cdd[i];
	}

	return;
}

/*
 * Verify the chanspec is using a legal set of parameters, i.e. that the
 * chanspec specified a band, bw, ctl_sb and channel and that the
 * combination could be legal given any set of circumstances.
 * RETURNS: true is the chanspec is malformed, false if it looks good.
 */
static bool brcms_c_chspec_malformed(u16 chanspec)
{
	/* must be 2G or 5G band */
	if (!CHSPEC_IS5G(chanspec) && !CHSPEC_IS2G(chanspec))
		return true;
	/* must be 20 or 40 bandwidth */
	if (!CHSPEC_IS40(chanspec) && !CHSPEC_IS20(chanspec))
		return true;

	/* 20MHZ b/w must have no ctl sb, 40 must have a ctl sb */
	if (CHSPEC_IS20(chanspec)) {
		if (!CHSPEC_SB_NONE(chanspec))
			return true;
	} else if (!CHSPEC_SB_UPPER(chanspec) && !CHSPEC_SB_LOWER(chanspec)) {
		return true;
	}

	return false;
}

/*
 * Validate the chanspec for this locale, for 40MHZ we need to also
 * check that the sidebands are valid 20MZH channels in this locale
 * and they are also a legal HT combination
 */
static bool
brcms_c_valid_chanspec_ext(struct brcms_cm_info *wlc_cm, u16 chspec)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;
	u8 channel = CHSPEC_CHANNEL(chspec);

	/* check the chanspec */
	if (brcms_c_chspec_malformed(chspec)) {
		brcms_err(wlc->hw->d11core, "wl%d: malformed chanspec 0x%x\n",
			  wlc->pub->unit, chspec);
		return false;
	}

	if (CHANNEL_BANDUNIT(wlc_cm->wlc, channel) !=
	    chspec_bandunit(chspec))
		return false;

	return true;
}

bool brcms_c_valid_chanspec_db(struct brcms_cm_info *wlc_cm, u16 chspec)
{
	return brcms_c_valid_chanspec_ext(wlc_cm, chspec);
}

static bool brcms_is_radar_freq(u16 center_freq)
{
	return center_freq >= 5260 && center_freq <= 5700;
}

static void brcms_reg_apply_radar_flags(struct wiphy *wiphy)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	int i;

	sband = wiphy->bands[IEEE80211_BAND_5GHZ];
	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];

		if (!brcms_is_radar_freq(ch->center_freq))
			continue;

		/*
		 * All channels in this range should be passive and have
		 * DFS enabled.
		 */
		if (!(ch->flags & IEEE80211_CHAN_DISABLED))
			ch->flags |= IEEE80211_CHAN_RADAR |
				     IEEE80211_CHAN_NO_IR |
				     IEEE80211_CHAN_NO_IR;
	}
}

static void
brcms_reg_apply_beaconing_flags(struct wiphy *wiphy,
				enum nl80211_reg_initiator initiator)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	const struct ieee80211_reg_rule *rule;
	int band, i;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		sband = wiphy->bands[band];
		if (!sband)
			continue;

		for (i = 0; i < sband->n_channels; i++) {
			ch = &sband->channels[i];

			if (ch->flags &
			    (IEEE80211_CHAN_DISABLED | IEEE80211_CHAN_RADAR))
				continue;

			if (initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE) {
				rule = freq_reg_info(wiphy,
						     MHZ_TO_KHZ(ch->center_freq));
				if (IS_ERR(rule))
					continue;

				if (!(rule->flags & NL80211_RRF_NO_IR))
					ch->flags &= ~IEEE80211_CHAN_NO_IR;
			} else if (ch->beacon_found) {
				ch->flags &= ~IEEE80211_CHAN_NO_IR;
			}
		}
	}
}

static void brcms_reg_notifier(struct wiphy *wiphy,
			       struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct brcms_info *wl = hw->priv;
	struct brcms_c_info *wlc = wl->wlc;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	int band, i;
	bool ch_found = false;

	brcms_reg_apply_radar_flags(wiphy);

	if (request->initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE)
		brcms_reg_apply_beaconing_flags(wiphy, request->initiator);

	/* Disable radio if all channels disallowed by regulatory */
	for (band = 0; !ch_found && band < IEEE80211_NUM_BANDS; band++) {
		sband = wiphy->bands[band];
		if (!sband)
			continue;

		for (i = 0; !ch_found && i < sband->n_channels; i++) {
			ch = &sband->channels[i];

			if (!(ch->flags & IEEE80211_CHAN_DISABLED))
				ch_found = true;
		}
	}

	if (ch_found) {
		mboolclr(wlc->pub->radio_disabled, WL_RADIO_COUNTRY_DISABLE);
	} else {
		mboolset(wlc->pub->radio_disabled, WL_RADIO_COUNTRY_DISABLE);
		brcms_err(wlc->hw->d11core,
			  "wl%d: %s: no valid channel for \"%s\"\n",
			  wlc->pub->unit, __func__, request->alpha2);
	}

	if (wlc->pub->_nbands > 1 || wlc->band->bandtype == BRCM_BAND_2G)
		wlc_phy_chanspec_ch14_widefilter_set(wlc->band->pi,
					brcms_c_japan_ccode(request->alpha2));
}

void brcms_c_regd_init(struct brcms_c_info *wlc)
{
	struct wiphy *wiphy = wlc->wiphy;
	const struct brcms_regd *regd = wlc->cmi->world_regd;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	struct brcms_chanvec sup_chan;
	struct brcms_band *band;
	int band_idx, i;

	/* Disable any channels not supported by the phy */
	for (band_idx = 0; band_idx < wlc->pub->_nbands; band_idx++) {
		band = wlc->bandstate[band_idx];

		wlc_phy_chanspec_band_validch(band->pi, band->bandtype,
					      &sup_chan);

		if (band_idx == BAND_2G_INDEX)
			sband = wiphy->bands[IEEE80211_BAND_2GHZ];
		else
			sband = wiphy->bands[IEEE80211_BAND_5GHZ];

		for (i = 0; i < sband->n_channels; i++) {
			ch = &sband->channels[i];
			if (!isset(sup_chan.vec, ch->hw_value))
				ch->flags |= IEEE80211_CHAN_DISABLED;
		}
	}

	wlc->wiphy->reg_notifier = brcms_reg_notifier;
	wlc->wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG |
					REGULATORY_STRICT_REG;
	wiphy_apply_custom_regulatory(wlc->wiphy, regd->regdomain);
	brcms_reg_apply_beaconing_flags(wiphy, NL80211_REGDOM_SET_BY_DRIVER);
}
