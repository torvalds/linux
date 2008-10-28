/*
 * Copyright (c) 2008 Atheros Communications Inc.
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
#include <linux/slab.h>
#include "core.h"
#include "hw.h"
#include "regd.h"
#include "regd_common.h"

static int ath9k_regd_chansort(const void *a, const void *b)
{
	const struct ath9k_channel *ca = a;
	const struct ath9k_channel *cb = b;

	return (ca->channel == cb->channel) ?
	    (ca->channelFlags & CHAN_FLAGS) -
	    (cb->channelFlags & CHAN_FLAGS) : ca->channel - cb->channel;
}

static void
ath9k_regd_sort(void *a, u32 n, u32 size, ath_hal_cmp_t *cmp)
{
	u8 *aa = a;
	u8 *ai, *t;

	for (ai = aa + size; --n >= 1; ai += size)
		for (t = ai; t > aa; t -= size) {
			u8 *u = t - size;
			if (cmp(u, t) <= 0)
				break;
			swap(u, t, size);
		}
}

static u16 ath9k_regd_get_eepromRD(struct ath_hal *ah)
{
	return ah->ah_currentRD & ~WORLDWIDE_ROAMING_FLAG;
}

static bool ath9k_regd_is_chan_bm_zero(u64 *bitmask)
{
	int i;

	for (i = 0; i < BMLEN; i++) {
		if (bitmask[i] != 0)
			return false;
	}
	return true;
}

static bool ath9k_regd_is_eeprom_valid(struct ath_hal *ah)
{
	u16 rd = ath9k_regd_get_eepromRD(ah);
	int i;

	if (rd & COUNTRY_ERD_FLAG) {
		u16 cc = rd & ~COUNTRY_ERD_FLAG;
		for (i = 0; i < ARRAY_SIZE(allCountries); i++)
			if (allCountries[i].countryCode == cc)
				return true;
	} else {
		for (i = 0; i < ARRAY_SIZE(regDomainPairs); i++)
			if (regDomainPairs[i].regDmnEnum == rd)
				return true;
	}
	DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
		 "%s: invalid regulatory domain/country code 0x%x\n",
		 __func__, rd);
	return false;
}

static bool ath9k_regd_is_fcc_midband_supported(struct ath_hal *ah)
{
	u32 regcap;

	regcap = ah->ah_caps.reg_cap;

	if (regcap & AR_EEPROM_EEREGCAP_EN_FCC_MIDBAND)
		return true;
	else
		return false;
}

static bool ath9k_regd_is_ccode_valid(struct ath_hal *ah,
				      u16 cc)
{
	u16 rd;
	int i;

	if (cc == CTRY_DEFAULT)
		return true;
	if (cc == CTRY_DEBUG)
		return true;

	rd = ath9k_regd_get_eepromRD(ah);
	DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY, "%s: EEPROM regdomain 0x%x\n",
		 __func__, rd);

	if (rd & COUNTRY_ERD_FLAG) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: EEPROM setting is country code %u\n",
			__func__, rd & ~COUNTRY_ERD_FLAG);
		return cc == (rd & ~COUNTRY_ERD_FLAG);
	}

	for (i = 0; i < ARRAY_SIZE(allCountries); i++) {
		if (cc == allCountries[i].countryCode) {
#ifdef AH_SUPPORT_11D
			if ((rd & WORLD_SKU_MASK) == WORLD_SKU_PREFIX)
				return true;
#endif
			if (allCountries[i].regDmnEnum == rd ||
			    rd == DEBUG_REG_DMN || rd == NO_ENUMRD)
				return true;
		}
	}
	return false;
}

static void
ath9k_regd_get_wmodes_nreg(struct ath_hal *ah,
			   struct country_code_to_enum_rd *country,
			   struct regDomain *rd5GHz,
			   unsigned long *modes_allowed)
{
	bitmap_copy(modes_allowed, ah->ah_caps.wireless_modes, ATH9K_MODE_MAX);

	if (test_bit(ATH9K_MODE_11G, ah->ah_caps.wireless_modes) &&
	    (!country->allow11g))
		clear_bit(ATH9K_MODE_11G, modes_allowed);

	if (test_bit(ATH9K_MODE_11A, ah->ah_caps.wireless_modes) &&
	    (ath9k_regd_is_chan_bm_zero(rd5GHz->chan11a)))
		clear_bit(ATH9K_MODE_11A, modes_allowed);

	if (test_bit(ATH9K_MODE_11NG_HT20, ah->ah_caps.wireless_modes)
	    && (!country->allow11ng20))
		clear_bit(ATH9K_MODE_11NG_HT20, modes_allowed);

	if (test_bit(ATH9K_MODE_11NA_HT20, ah->ah_caps.wireless_modes)
	    && (!country->allow11na20))
		clear_bit(ATH9K_MODE_11NA_HT20, modes_allowed);

	if (test_bit(ATH9K_MODE_11NG_HT40PLUS, ah->ah_caps.wireless_modes) &&
	    (!country->allow11ng40))
		clear_bit(ATH9K_MODE_11NG_HT40PLUS, modes_allowed);

	if (test_bit(ATH9K_MODE_11NG_HT40MINUS, ah->ah_caps.wireless_modes) &&
	    (!country->allow11ng40))
		clear_bit(ATH9K_MODE_11NG_HT40MINUS, modes_allowed);

	if (test_bit(ATH9K_MODE_11NA_HT40PLUS, ah->ah_caps.wireless_modes) &&
	    (!country->allow11na40))
		clear_bit(ATH9K_MODE_11NA_HT40PLUS, modes_allowed);

	if (test_bit(ATH9K_MODE_11NA_HT40MINUS, ah->ah_caps.wireless_modes) &&
	    (!country->allow11na40))
		clear_bit(ATH9K_MODE_11NA_HT40MINUS, modes_allowed);
}

bool ath9k_regd_is_public_safety_sku(struct ath_hal *ah)
{
	u16 rd;

	rd = ath9k_regd_get_eepromRD(ah);

	switch (rd) {
	case FCC4_FCCA:
	case (CTRY_UNITED_STATES_FCC49 | COUNTRY_ERD_FLAG):
		return true;
	case DEBUG_REG_DMN:
	case NO_ENUMRD:
		if (ah->ah_countryCode == CTRY_UNITED_STATES_FCC49)
			return true;
		break;
	}
	return false;
}

static struct country_code_to_enum_rd*
ath9k_regd_find_country(u16 countryCode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(allCountries); i++) {
		if (allCountries[i].countryCode == countryCode)
			return &allCountries[i];
	}
	return NULL;
}

static u16 ath9k_regd_get_default_country(struct ath_hal *ah)
{
	u16 rd;
	int i;

	rd = ath9k_regd_get_eepromRD(ah);
	if (rd & COUNTRY_ERD_FLAG) {
		struct country_code_to_enum_rd *country = NULL;
		u16 cc = rd & ~COUNTRY_ERD_FLAG;

		country = ath9k_regd_find_country(cc);
		if (country != NULL)
			return cc;
	}

	for (i = 0; i < ARRAY_SIZE(regDomainPairs); i++)
		if (regDomainPairs[i].regDmnEnum == rd) {
			if (regDomainPairs[i].singleCC != 0)
				return regDomainPairs[i].singleCC;
			else
				i = ARRAY_SIZE(regDomainPairs);
		}
	return CTRY_DEFAULT;
}

static bool ath9k_regd_is_valid_reg_domain(int regDmn,
					   struct regDomain *rd)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(regDomains); i++) {
		if (regDomains[i].regDmnEnum == regDmn) {
			if (rd != NULL) {
				memcpy(rd, &regDomains[i],
				       sizeof(struct regDomain));
			}
			return true;
		}
	}
	return false;
}

static bool ath9k_regd_is_valid_reg_domainPair(int regDmnPair)
{
	int i;

	if (regDmnPair == NO_ENUMRD)
		return false;
	for (i = 0; i < ARRAY_SIZE(regDomainPairs); i++) {
		if (regDomainPairs[i].regDmnEnum == regDmnPair)
			return true;
	}
	return false;
}

static bool
ath9k_regd_get_wmode_regdomain(struct ath_hal *ah, int regDmn,
			       u16 channelFlag, struct regDomain *rd)
{
	int i, found;
	u64 flags = NO_REQ;
	struct reg_dmn_pair_mapping *regPair = NULL;
	int regOrg;

	regOrg = regDmn;
	if (regDmn == CTRY_DEFAULT) {
		u16 rdnum;
		rdnum = ath9k_regd_get_eepromRD(ah);

		if (!(rdnum & COUNTRY_ERD_FLAG)) {
			if (ath9k_regd_is_valid_reg_domain(rdnum, NULL) ||
			    ath9k_regd_is_valid_reg_domainPair(rdnum)) {
				regDmn = rdnum;
			}
		}
	}

	if ((regDmn & MULTI_DOMAIN_MASK) == 0) {
		for (i = 0, found = 0;
		     (i < ARRAY_SIZE(regDomainPairs)) && (!found); i++) {
			if (regDomainPairs[i].regDmnEnum == regDmn) {
				regPair = &regDomainPairs[i];
				found = 1;
			}
		}
		if (!found) {
			DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
				"%s: Failed to find reg domain pair %u\n",
				__func__, regDmn);
			return false;
		}
		if (!(channelFlag & CHANNEL_2GHZ)) {
			regDmn = regPair->regDmn5GHz;
			flags = regPair->flags5GHz;
		}
		if (channelFlag & CHANNEL_2GHZ) {
			regDmn = regPair->regDmn2GHz;
			flags = regPair->flags2GHz;
		}
	}

	found = ath9k_regd_is_valid_reg_domain(regDmn, rd);
	if (!found) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: Failed to find unitary reg domain %u\n",
			__func__, regDmn);
		return false;
	} else {
		rd->pscan &= regPair->pscanMask;
		if (((regOrg & MULTI_DOMAIN_MASK) == 0) &&
		    (flags != NO_REQ)) {
			rd->flags = flags;
		}

		rd->flags &= (channelFlag & CHANNEL_2GHZ) ?
		    REG_DOMAIN_2GHZ_MASK : REG_DOMAIN_5GHZ_MASK;
		return true;
	}
}

static bool ath9k_regd_is_bit_set(int bit, u64 *bitmask)
{
	int byteOffset, bitnum;
	u64 val;

	byteOffset = bit / 64;
	bitnum = bit - byteOffset * 64;
	val = ((u64) 1) << bitnum;
	if (bitmask[byteOffset] & val)
		return true;
	else
		return false;
}

static void
ath9k_regd_add_reg_classid(u8 *regclassids, u32 maxregids,
			   u32 *nregids, u8 regclassid)
{
	int i;

	if (regclassid == 0)
		return;

	for (i = 0; i < maxregids; i++) {
		if (regclassids[i] == regclassid)
			return;
		if (regclassids[i] == 0)
			break;
	}

	if (i == maxregids)
		return;
	else {
		regclassids[i] = regclassid;
		*nregids += 1;
	}

	return;
}

static bool
ath9k_regd_get_eeprom_reg_ext_bits(struct ath_hal *ah,
				   enum reg_ext_bitmap bit)
{
	return (ah->ah_currentRDExt & (1 << bit)) ? true : false;
}

#ifdef ATH_NF_PER_CHAN

static void ath9k_regd_init_rf_buffer(struct ath9k_channel *ichans,
				      int nchans)
{
	int i, j, next;

	for (next = 0; next < nchans; next++) {
		for (i = 0; i < NUM_NF_READINGS; i++) {
			ichans[next].nfCalHist[i].currIndex = 0;
			ichans[next].nfCalHist[i].privNF =
			    AR_PHY_CCA_MAX_GOOD_VALUE;
			ichans[next].nfCalHist[i].invalidNFcount =
			    AR_PHY_CCA_FILTERWINDOW_LENGTH;
			for (j = 0; j < ATH9K_NF_CAL_HIST_MAX; j++) {
				ichans[next].nfCalHist[i].nfCalBuffer[j] =
				    AR_PHY_CCA_MAX_GOOD_VALUE;
			}
		}
	}
}
#endif

static int ath9k_regd_is_chan_present(struct ath_hal *ah,
				      u16 c)
{
	int i;

	for (i = 0; i < 150; i++) {
		if (!ah->ah_channels[i].channel)
			return -1;
		else if (ah->ah_channels[i].channel == c)
			return i;
	}

	return -1;
}

static bool
ath9k_regd_add_channel(struct ath_hal *ah,
		       u16 c,
		       u16 c_lo,
		       u16 c_hi,
		       u16 maxChan,
		       u8 ctl,
		       int pos,
		       struct regDomain rd5GHz,
		       struct RegDmnFreqBand *fband,
		       struct regDomain *rd,
		       const struct cmode *cm,
		       struct ath9k_channel *ichans,
		       bool enableExtendedChannels)
{
	struct ath9k_channel *chan;
	int ret;
	u32 channelFlags = 0;
	u8 privFlags = 0;

	if (!(c_lo <= c && c <= c_hi)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: c %u out of range [%u..%u]\n",
			__func__, c, c_lo, c_hi);
		return false;
	}
	if ((fband->channelBW == CHANNEL_HALF_BW) &&
	    !(ah->ah_caps.hw_caps & ATH9K_HW_CAP_CHAN_HALFRATE)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: Skipping %u half rate channel\n",
			__func__, c);
		return false;
	}

	if ((fband->channelBW == CHANNEL_QUARTER_BW) &&
	    !(ah->ah_caps.hw_caps & ATH9K_HW_CAP_CHAN_QUARTERRATE)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: Skipping %u quarter rate channel\n",
			__func__, c);
		return false;
	}

	if (((c + fband->channelSep) / 2) > (maxChan + HALF_MAXCHANBW)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: c %u > maxChan %u\n",
			__func__, c, maxChan);
		return false;
	}

	if ((fband->usePassScan & IS_ECM_CHAN) && !enableExtendedChannels) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"Skipping ecm channel\n");
		return false;
	}

	if ((rd->flags & NO_HOSTAP) && (ah->ah_opmode == ATH9K_M_HOSTAP)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"Skipping HOSTAP channel\n");
		return false;
	}

	if (IS_HT40_MODE(cm->mode) &&
	    !(ath9k_regd_get_eeprom_reg_ext_bits(ah, REG_EXT_FCC_DFS_HT40)) &&
	    (fband->useDfs) &&
	    (rd->conformanceTestLimit != MKK)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"Skipping HT40 channel (en_fcc_dfs_ht40 = 0)\n");
		return false;
	}

	if (IS_HT40_MODE(cm->mode) &&
	    !(ath9k_regd_get_eeprom_reg_ext_bits(ah,
						 REG_EXT_JAPAN_NONDFS_HT40)) &&
	    !(fband->useDfs) && (rd->conformanceTestLimit == MKK)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"Skipping HT40 channel (en_jap_ht40 = 0)\n");
		return false;
	}

	if (IS_HT40_MODE(cm->mode) &&
	    !(ath9k_regd_get_eeprom_reg_ext_bits(ah, REG_EXT_JAPAN_DFS_HT40)) &&
	    (fband->useDfs) &&
	    (rd->conformanceTestLimit == MKK)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"Skipping HT40 channel (en_jap_dfs_ht40 = 0)\n");
		return false;
	}

	/* Calculate channel flags */

	channelFlags = cm->flags;

	switch (fband->channelBW) {
	case CHANNEL_HALF_BW:
		channelFlags |= CHANNEL_HALF;
		break;
	case CHANNEL_QUARTER_BW:
		channelFlags |= CHANNEL_QUARTER;
		break;
	}

	if (fband->usePassScan & rd->pscan)
		channelFlags |= CHANNEL_PASSIVE;
	else
		channelFlags &= ~CHANNEL_PASSIVE;
	if (fband->useDfs & rd->dfsMask)
		privFlags = CHANNEL_DFS;
	else
		privFlags = 0;
	if (rd->flags & LIMIT_FRAME_4MS)
		privFlags |= CHANNEL_4MS_LIMIT;
	if (privFlags & CHANNEL_DFS)
		privFlags |= CHANNEL_DISALLOW_ADHOC;
	if (rd->flags & ADHOC_PER_11D)
		privFlags |= CHANNEL_PER_11D_ADHOC;

	if (channelFlags & CHANNEL_PASSIVE) {
		if ((c < 2412) || (c > 2462)) {
			if (rd5GHz.regDmnEnum == MKK1 ||
			    rd5GHz.regDmnEnum == MKK2) {
				u32 regcap = ah->ah_caps.reg_cap;
				if (!(regcap &
				      (AR_EEPROM_EEREGCAP_EN_KK_U1_EVEN |
				       AR_EEPROM_EEREGCAP_EN_KK_U2 |
				       AR_EEPROM_EEREGCAP_EN_KK_MIDBAND)) &&
				    isUNII1OddChan(c)) {
					channelFlags &= ~CHANNEL_PASSIVE;
				} else {
					privFlags |= CHANNEL_DISALLOW_ADHOC;
				}
			} else {
				privFlags |= CHANNEL_DISALLOW_ADHOC;
			}
		}
	}

	if ((cm->mode == ATH9K_MODE_11A) ||
	    (cm->mode == ATH9K_MODE_11NA_HT20) ||
	    (cm->mode == ATH9K_MODE_11NA_HT40PLUS) ||
	    (cm->mode == ATH9K_MODE_11NA_HT40MINUS)) {
		if (rd->flags & (ADHOC_NO_11A | DISALLOW_ADHOC_11A))
			privFlags |= CHANNEL_DISALLOW_ADHOC;
	}

	/* Fill in channel details */

	ret = ath9k_regd_is_chan_present(ah, c);
	if (ret == -1) {
		chan = &ah->ah_channels[pos];
		chan->channel = c;
		chan->maxRegTxPower = fband->powerDfs;
		chan->antennaMax = fband->antennaMax;
		chan->regDmnFlags = rd->flags;
		chan->maxTxPower = AR5416_MAX_RATE_POWER;
		chan->minTxPower = AR5416_MAX_RATE_POWER;
		chan->channelFlags = channelFlags;
		chan->privFlags = privFlags;
	} else {
		chan = &ah->ah_channels[ret];
		chan->channelFlags |= channelFlags;
		chan->privFlags |= privFlags;
	}

	/* Set CTLs */

	if ((cm->flags & CHANNEL_ALL) == CHANNEL_A)
		chan->conformanceTestLimit[0] = ctl;
	else if ((cm->flags & CHANNEL_ALL) == CHANNEL_B)
		chan->conformanceTestLimit[1] = ctl;
	else if ((cm->flags & CHANNEL_ALL) == CHANNEL_G)
		chan->conformanceTestLimit[2] = ctl;

	return (ret == -1) ? true : false;
}

static bool ath9k_regd_japan_check(struct ath_hal *ah,
				   int b,
				   struct regDomain *rd5GHz)
{
	bool skipband = false;
	int i;
	u32 regcap;

	for (i = 0; i < ARRAY_SIZE(j_bandcheck); i++) {
		if (j_bandcheck[i].freqbandbit == b) {
			regcap = ah->ah_caps.reg_cap;
			if ((j_bandcheck[i].eepromflagtocheck & regcap) == 0) {
				skipband = true;
			} else if ((regcap & AR_EEPROM_EEREGCAP_EN_KK_U2) ||
				  (regcap & AR_EEPROM_EEREGCAP_EN_KK_MIDBAND)) {
				rd5GHz->dfsMask |= DFS_MKK4;
				rd5GHz->pscan |= PSCAN_MKK3;
			}
			break;
		}
	}

	DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
		"%s: Skipping %d freq band\n",
		__func__, j_bandcheck[i].freqbandbit);

	return skipband;
}

bool
ath9k_regd_init_channels(struct ath_hal *ah,
			 u32 maxchans,
			 u32 *nchans, u8 *regclassids,
			 u32 maxregids, u32 *nregids, u16 cc,
			 bool enableOutdoor,
			 bool enableExtendedChannels)
{
	u16 maxChan = 7000;
	struct country_code_to_enum_rd *country = NULL;
	struct regDomain rd5GHz, rd2GHz;
	const struct cmode *cm;
	struct ath9k_channel *ichans = &ah->ah_channels[0];
	int next = 0, b;
	u8 ctl;
	int regdmn;
	u16 chanSep;
	unsigned long *modes_avail;
	DECLARE_BITMAP(modes_allowed, ATH9K_MODE_MAX);

	DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY, "%s: cc %u %s %s\n",
		 __func__, cc,
		 enableOutdoor ? "Enable outdoor" : "",
		 enableExtendedChannels ? "Enable ecm" : "");

	if (!ath9k_regd_is_ccode_valid(ah, cc)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: invalid country code %d\n", __func__, cc);
		return false;
	}

	if (!ath9k_regd_is_eeprom_valid(ah)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: invalid EEPROM contents\n", __func__);
		return false;
	}

	ah->ah_countryCode = ath9k_regd_get_default_country(ah);

	if (ah->ah_countryCode == CTRY_DEFAULT) {
		ah->ah_countryCode = cc & COUNTRY_CODE_MASK;
		if ((ah->ah_countryCode == CTRY_DEFAULT) &&
		    (ath9k_regd_get_eepromRD(ah) == CTRY_DEFAULT)) {
			ah->ah_countryCode = CTRY_UNITED_STATES;
		}
	}

#ifdef AH_SUPPORT_11D
	if (ah->ah_countryCode == CTRY_DEFAULT) {
		regdmn = ath9k_regd_get_eepromRD(ah);
		country = NULL;
	} else {
#endif
		country = ath9k_regd_find_country(ah->ah_countryCode);
		if (country == NULL) {
			DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
				"Country is NULL!!!!, cc= %d\n",
				ah->ah_countryCode);
			return false;
		} else {
			regdmn = country->regDmnEnum;
#ifdef AH_SUPPORT_11D
			if (((ath9k_regd_get_eepromRD(ah) &
			      WORLD_SKU_MASK) == WORLD_SKU_PREFIX) &&
			    (cc == CTRY_UNITED_STATES)) {
				if (!isWwrSKU_NoMidband(ah)
				    && ath9k_regd_is_fcc_midband_supported(ah))
					regdmn = FCC3_FCCA;
				else
					regdmn = FCC1_FCCA;
			}
#endif
		}
#ifdef AH_SUPPORT_11D
	}
#endif
	if (!ath9k_regd_get_wmode_regdomain(ah,
					    regdmn,
					    ~CHANNEL_2GHZ,
					    &rd5GHz)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: couldn't find unitary "
			"5GHz reg domain for country %u\n",
			__func__, ah->ah_countryCode);
		return false;
	}
	if (!ath9k_regd_get_wmode_regdomain(ah,
					    regdmn,
					    CHANNEL_2GHZ,
					    &rd2GHz)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: couldn't find unitary 2GHz "
			"reg domain for country %u\n",
			__func__, ah->ah_countryCode);
		return false;
	}

	if (!isWwrSKU(ah) && ((rd5GHz.regDmnEnum == FCC1) ||
			      (rd5GHz.regDmnEnum == FCC2))) {
		if (ath9k_regd_is_fcc_midband_supported(ah)) {
			if (!ath9k_regd_get_wmode_regdomain(ah,
							    FCC3_FCCA,
							    ~CHANNEL_2GHZ,
							    &rd5GHz)) {
				DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
					"%s: couldn't find unitary 5GHz "
					"reg domain for country %u\n",
					__func__, ah->ah_countryCode);
				return false;
			}
		}
	}

	if (country == NULL) {
		modes_avail = ah->ah_caps.wireless_modes;
	} else {
		ath9k_regd_get_wmodes_nreg(ah, country, &rd5GHz, modes_allowed);
		modes_avail = modes_allowed;

		if (!enableOutdoor)
			maxChan = country->outdoorChanStart;
	}

	next = 0;

	if (maxchans > ARRAY_SIZE(ah->ah_channels))
		maxchans = ARRAY_SIZE(ah->ah_channels);

	for (cm = modes; cm < &modes[ARRAY_SIZE(modes)]; cm++) {
		u16 c, c_hi, c_lo;
		u64 *channelBM = NULL;
		struct regDomain *rd = NULL;
		struct RegDmnFreqBand *fband = NULL, *freqs;
		int8_t low_adj = 0, hi_adj = 0;

		if (!test_bit(cm->mode, modes_avail)) {
			DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
				"%s: !avail mode %d flags 0x%x\n",
				__func__, cm->mode, cm->flags);
			continue;
		}
		if (!ath9k_get_channel_edges(ah, cm->flags, &c_lo, &c_hi)) {
			DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
				"%s: channels 0x%x not supported "
				"by hardware\n",
				__func__, cm->flags);
			continue;
		}

		switch (cm->mode) {
		case ATH9K_MODE_11A:
		case ATH9K_MODE_11NA_HT20:
		case ATH9K_MODE_11NA_HT40PLUS:
		case ATH9K_MODE_11NA_HT40MINUS:
			rd = &rd5GHz;
			channelBM = rd->chan11a;
			freqs = &regDmn5GhzFreq[0];
			ctl = rd->conformanceTestLimit;
			break;
		case ATH9K_MODE_11B:
			rd = &rd2GHz;
			channelBM = rd->chan11b;
			freqs = &regDmn2GhzFreq[0];
			ctl = rd->conformanceTestLimit | CTL_11B;
			break;
		case ATH9K_MODE_11G:
		case ATH9K_MODE_11NG_HT20:
		case ATH9K_MODE_11NG_HT40PLUS:
		case ATH9K_MODE_11NG_HT40MINUS:
			rd = &rd2GHz;
			channelBM = rd->chan11g;
			freqs = &regDmn2Ghz11gFreq[0];
			ctl = rd->conformanceTestLimit | CTL_11G;
			break;
		default:
			DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
				"%s: Unknown HAL mode 0x%x\n", __func__,
				cm->mode);
			continue;
		}

		if (ath9k_regd_is_chan_bm_zero(channelBM))
			continue;

		if ((cm->mode == ATH9K_MODE_11NA_HT40PLUS) ||
		    (cm->mode == ATH9K_MODE_11NG_HT40PLUS)) {
			hi_adj = -20;
		}

		if ((cm->mode == ATH9K_MODE_11NA_HT40MINUS) ||
		    (cm->mode == ATH9K_MODE_11NG_HT40MINUS)) {
			low_adj = 20;
		}

		/* XXX: Add a helper here instead */
		for (b = 0; b < 64 * BMLEN; b++) {
			if (ath9k_regd_is_bit_set(b, channelBM)) {
				fband = &freqs[b];
				if (rd5GHz.regDmnEnum == MKK1
				    || rd5GHz.regDmnEnum == MKK2) {
					if (ath9k_regd_japan_check(ah,
								   b,
								   &rd5GHz))
						continue;
				}

				ath9k_regd_add_reg_classid(regclassids,
							   maxregids,
							   nregids,
							   fband->
							   regClassId);

				if (IS_HT40_MODE(cm->mode) && (rd == &rd5GHz)) {
					chanSep = 40;
					if (fband->lowChannel == 5280)
						low_adj += 20;

					if (fband->lowChannel == 5170)
						continue;
				} else
					chanSep = fband->channelSep;

				for (c = fband->lowChannel + low_adj;
				     ((c <= (fband->highChannel + hi_adj)) &&
				      (c >= (fband->lowChannel + low_adj)));
				     c += chanSep) {
					if (next >= maxchans) {
						DPRINTF(ah->ah_sc,
							ATH_DBG_REGULATORY,
							"%s: too many channels "
							"for channel table\n",
							__func__);
						goto done;
					}
					if (ath9k_regd_add_channel(ah,
						   c, c_lo, c_hi,
						   maxChan, ctl,
						   next,
						   rd5GHz,
						   fband, rd, cm,
						   ichans,
						   enableExtendedChannels))
						next++;
				}
				if (IS_HT40_MODE(cm->mode) &&
				    (fband->lowChannel == 5280)) {
					low_adj -= 20;
				}
			}
		}
	}
done:
	if (next != 0) {
		int i;

		if (next > ARRAY_SIZE(ah->ah_channels)) {
			DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
				"%s: too many channels %u; truncating to %u\n",
				__func__, next,
				(int) ARRAY_SIZE(ah->ah_channels));
			next = ARRAY_SIZE(ah->ah_channels);
		}
#ifdef ATH_NF_PER_CHAN
		ath9k_regd_init_rf_buffer(ichans, next);
#endif
		ath9k_regd_sort(ichans, next,
				sizeof(struct ath9k_channel),
				ath9k_regd_chansort);

		ah->ah_nchan = next;

		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY, "Channel list:\n");
		for (i = 0; i < next; i++) {
			DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
				"chan: %d flags: 0x%x\n",
				ah->ah_channels[i].channel,
				ah->ah_channels[i].channelFlags);
		}
	}
	*nchans = next;

	ah->ah_countryCode = ah->ah_countryCode;

	ah->ah_currentRDInUse = regdmn;
	ah->ah_currentRD5G = rd5GHz.regDmnEnum;
	ah->ah_currentRD2G = rd2GHz.regDmnEnum;
	if (country == NULL) {
		ah->ah_iso[0] = 0;
		ah->ah_iso[1] = 0;
	} else {
		ah->ah_iso[0] = country->isoName[0];
		ah->ah_iso[1] = country->isoName[1];
	}

	return next != 0;
}

struct ath9k_channel*
ath9k_regd_check_channel(struct ath_hal *ah,
			 const struct ath9k_channel *c)
{
	struct ath9k_channel *base, *cc;

	int flags = c->channelFlags & CHAN_FLAGS;
	int n, lim;

	DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
		"%s: channel %u/0x%x (0x%x) requested\n", __func__,
		c->channel, c->channelFlags, flags);

	cc = ah->ah_curchan;
	if (cc != NULL && cc->channel == c->channel &&
	    (cc->channelFlags & CHAN_FLAGS) == flags) {
		if ((cc->privFlags & CHANNEL_INTERFERENCE) &&
		    (cc->privFlags & CHANNEL_DFS))
			return NULL;
		else
			return cc;
	}

	base = ah->ah_channels;
	n = ah->ah_nchan;

	for (lim = n; lim != 0; lim >>= 1) {
		int d;
		cc = &base[lim >> 1];
		d = c->channel - cc->channel;
		if (d == 0) {
			if ((cc->channelFlags & CHAN_FLAGS) == flags) {
				if ((cc->privFlags & CHANNEL_INTERFERENCE) &&
				    (cc->privFlags & CHANNEL_DFS))
					return NULL;
				else
					return cc;
			}
			d = flags - (cc->channelFlags & CHAN_FLAGS);
		}
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"%s: channel %u/0x%x d %d\n", __func__,
			cc->channel, cc->channelFlags, d);
		if (d > 0) {
			base = cc + 1;
			lim--;
		}
	}
	DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY, "%s: no match for %u/0x%x\n",
		__func__, c->channel, c->channelFlags);
	return NULL;
}

u32
ath9k_regd_get_antenna_allowed(struct ath_hal *ah,
			       struct ath9k_channel *chan)
{
	struct ath9k_channel *ichan = NULL;

	ichan = ath9k_regd_check_channel(ah, chan);
	if (!ichan)
		return 0;

	return ichan->antennaMax;
}

u32 ath9k_regd_get_ctl(struct ath_hal *ah, struct ath9k_channel *chan)
{
	u32 ctl = NO_CTL;
	struct ath9k_channel *ichan;

	if (ah->ah_countryCode == CTRY_DEFAULT && isWwrSKU(ah)) {
		if (IS_CHAN_B(chan))
			ctl = SD_NO_CTL | CTL_11B;
		else if (IS_CHAN_G(chan))
			ctl = SD_NO_CTL | CTL_11G;
		else
			ctl = SD_NO_CTL | CTL_11A;
	} else {
		ichan = ath9k_regd_check_channel(ah, chan);
		if (ichan != NULL) {
			/* FIXME */
			if (IS_CHAN_A(ichan))
				ctl = ichan->conformanceTestLimit[0];
			else if (IS_CHAN_B(ichan))
				ctl = ichan->conformanceTestLimit[1];
			else if (IS_CHAN_G(ichan))
				ctl = ichan->conformanceTestLimit[2];

			if (IS_CHAN_G(chan) && (ctl & 0xf) == CTL_11B)
				ctl = (ctl & ~0xf) | CTL_11G;
		}
	}
	return ctl;
}

void ath9k_regd_get_current_country(struct ath_hal *ah,
				    struct ath9k_country_entry *ctry)
{
	u16 rd = ath9k_regd_get_eepromRD(ah);

	ctry->isMultidomain = false;
	if (rd == CTRY_DEFAULT)
		ctry->isMultidomain = true;
	else if (!(rd & COUNTRY_ERD_FLAG))
		ctry->isMultidomain = isWwrSKU(ah);

	ctry->countryCode = ah->ah_countryCode;
	ctry->regDmnEnum = ah->ah_currentRD;
	ctry->regDmn5G = ah->ah_currentRD5G;
	ctry->regDmn2G = ah->ah_currentRD2G;
	ctry->iso[0] = ah->ah_iso[0];
	ctry->iso[1] = ah->ah_iso[1];
	ctry->iso[2] = ah->ah_iso[2];
}
