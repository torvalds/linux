/*
 * Copyright (c) 2008-2010 Atheros Communications Inc.
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
#include "hw.h"
#include "hw-ops.h"

struct ani_ofdm_level_entry {
	int spur_immunity_level;
	int fir_step_level;
	int ofdm_weak_signal_on;
};

/* values here are relative to the INI */

/*
 * Legend:
 *
 * SI: Spur immunity
 * FS: FIR Step
 * WS: OFDM / CCK Weak Signal detection
 * MRC-CCK: Maximal Ratio Combining for CCK
 */

static const struct ani_ofdm_level_entry ofdm_level_table[] = {
	/* SI  FS  WS */
	{  0,  0,  1  }, /* lvl 0 */
	{  1,  1,  1  }, /* lvl 1 */
	{  2,  2,  1  }, /* lvl 2 */
	{  3,  2,  1  }, /* lvl 3  (default) */
	{  4,  3,  1  }, /* lvl 4 */
	{  5,  4,  1  }, /* lvl 5 */
	{  6,  5,  1  }, /* lvl 6 */
	{  7,  6,  1  }, /* lvl 7 */
	{  7,  7,  1  }, /* lvl 8 */
	{  7,  8,  0  }  /* lvl 9 */
};
#define ATH9K_ANI_OFDM_NUM_LEVEL \
	ARRAY_SIZE(ofdm_level_table)
#define ATH9K_ANI_OFDM_MAX_LEVEL \
	(ATH9K_ANI_OFDM_NUM_LEVEL-1)
#define ATH9K_ANI_OFDM_DEF_LEVEL \
	3 /* default level - matches the INI settings */

/*
 * MRC (Maximal Ratio Combining) has always been used with multi-antenna ofdm.
 * With OFDM for single stream you just add up all antenna inputs, you're
 * only interested in what you get after FFT. Signal aligment is also not
 * required for OFDM because any phase difference adds up in the frequency
 * domain.
 *
 * MRC requires extra work for use with CCK. You need to align the antenna
 * signals from the different antenna before you can add the signals together.
 * You need aligment of signals as CCK is in time domain, so addition can cancel
 * your signal completely if phase is 180 degrees (think of adding sine waves).
 * You also need to remove noise before the addition and this is where ANI
 * MRC CCK comes into play. One of the antenna inputs may be stronger but
 * lower SNR, so just adding after alignment can be dangerous.
 *
 * Regardless of alignment in time, the antenna signals add constructively after
 * FFT and improve your reception. For more information:
 *
 * http://en.wikipedia.org/wiki/Maximal-ratio_combining
 */

struct ani_cck_level_entry {
	int fir_step_level;
	int mrc_cck_on;
};

static const struct ani_cck_level_entry cck_level_table[] = {
	/* FS  MRC-CCK  */
	{  0,  1  }, /* lvl 0 */
	{  1,  1  }, /* lvl 1 */
	{  2,  1  }, /* lvl 2  (default) */
	{  3,  1  }, /* lvl 3 */
	{  4,  0  }, /* lvl 4 */
	{  5,  0  }, /* lvl 5 */
	{  6,  0  }, /* lvl 6 */
	{  7,  0  }, /* lvl 7 (only for high rssi) */
	{  8,  0  }  /* lvl 8 (only for high rssi) */
};

#define ATH9K_ANI_CCK_NUM_LEVEL \
	ARRAY_SIZE(cck_level_table)
#define ATH9K_ANI_CCK_MAX_LEVEL \
	(ATH9K_ANI_CCK_NUM_LEVEL-1)
#define ATH9K_ANI_CCK_MAX_LEVEL_LOW_RSSI \
	(ATH9K_ANI_CCK_NUM_LEVEL-3)
#define ATH9K_ANI_CCK_DEF_LEVEL \
	2 /* default level - matches the INI settings */

static bool use_new_ani(struct ath_hw *ah)
{
	return AR_SREV_9300_20_OR_LATER(ah) || modparam_force_new_ani;
}

static void ath9k_hw_update_mibstats(struct ath_hw *ah,
				     struct ath9k_mib_stats *stats)
{
	stats->ackrcv_bad += REG_READ(ah, AR_ACK_FAIL);
	stats->rts_bad += REG_READ(ah, AR_RTS_FAIL);
	stats->fcs_bad += REG_READ(ah, AR_FCS_FAIL);
	stats->rts_good += REG_READ(ah, AR_RTS_OK);
	stats->beacons += REG_READ(ah, AR_BEACON_CNT);
}

static void ath9k_ani_restart(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;
	struct ath_common *common = ath9k_hw_common(ah);
	u32 ofdm_base = 0, cck_base = 0;

	if (!DO_ANI(ah))
		return;

	aniState = &ah->curchan->ani;
	aniState->listenTime = 0;

	if (!use_new_ani(ah)) {
		ofdm_base = AR_PHY_COUNTMAX - ah->config.ofdm_trig_high;
		cck_base = AR_PHY_COUNTMAX - ah->config.cck_trig_high;
	}

	ath_dbg(common, ATH_DBG_ANI,
		"Writing ofdmbase=%u   cckbase=%u\n", ofdm_base, cck_base);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_PHY_ERR_1, ofdm_base);
	REG_WRITE(ah, AR_PHY_ERR_2, cck_base);
	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	REGWRITE_BUFFER_FLUSH(ah);

	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);

	aniState->ofdmPhyErrCount = 0;
	aniState->cckPhyErrCount = 0;
}

static void ath9k_hw_ani_ofdm_err_trigger_old(struct ath_hw *ah)
{
	struct ieee80211_conf *conf = &ath9k_hw_common(ah)->hw->conf;
	struct ar5416AniState *aniState;
	int32_t rssi;

	aniState = &ah->curchan->ani;

	if (aniState->noiseImmunityLevel < HAL_NOISE_IMMUNE_MAX) {
		if (ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL,
					 aniState->noiseImmunityLevel + 1)) {
			return;
		}
	}

	if (aniState->spurImmunityLevel < HAL_SPUR_IMMUNE_MAX) {
		if (ath9k_hw_ani_control(ah, ATH9K_ANI_SPUR_IMMUNITY_LEVEL,
					 aniState->spurImmunityLevel + 1)) {
			return;
		}
	}

	if (ah->opmode == NL80211_IFTYPE_AP) {
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX) {
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
		}
		return;
	}
	rssi = BEACON_RSSI(ah);
	if (rssi > aniState->rssiThrHigh) {
		if (!aniState->ofdmWeakSigDetectOff) {
			if (ath9k_hw_ani_control(ah,
					 ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
					 false)) {
				ath9k_hw_ani_control(ah,
					ATH9K_ANI_SPUR_IMMUNITY_LEVEL, 0);
				return;
			}
		}
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX) {
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
			return;
		}
	} else if (rssi > aniState->rssiThrLow) {
		if (aniState->ofdmWeakSigDetectOff)
			ath9k_hw_ani_control(ah,
				     ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				     true);
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX)
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
		return;
	} else {
		if ((conf->channel->band == IEEE80211_BAND_2GHZ) &&
		    !conf_is_ht(conf)) {
			if (!aniState->ofdmWeakSigDetectOff)
				ath9k_hw_ani_control(ah,
				     ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				     false);
			if (aniState->firstepLevel > 0)
				ath9k_hw_ani_control(ah,
					     ATH9K_ANI_FIRSTEP_LEVEL, 0);
			return;
		}
	}
}

static void ath9k_hw_ani_cck_err_trigger_old(struct ath_hw *ah)
{
	struct ieee80211_conf *conf = &ath9k_hw_common(ah)->hw->conf;
	struct ar5416AniState *aniState;
	int32_t rssi;

	aniState = &ah->curchan->ani;
	if (aniState->noiseImmunityLevel < HAL_NOISE_IMMUNE_MAX) {
		if (ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL,
					 aniState->noiseImmunityLevel + 1)) {
			return;
		}
	}
	if (ah->opmode == NL80211_IFTYPE_AP) {
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX) {
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
		}
		return;
	}
	rssi = BEACON_RSSI(ah);
	if (rssi > aniState->rssiThrLow) {
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX)
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
	} else {
		if ((conf->channel->band == IEEE80211_BAND_2GHZ) &&
		    !conf_is_ht(conf)) {
			if (aniState->firstepLevel > 0)
				ath9k_hw_ani_control(ah,
					     ATH9K_ANI_FIRSTEP_LEVEL, 0);
		}
	}
}

/* Adjust the OFDM Noise Immunity Level */
static void ath9k_hw_set_ofdm_nil(struct ath_hw *ah, u8 immunityLevel)
{
	struct ar5416AniState *aniState = &ah->curchan->ani;
	struct ath_common *common = ath9k_hw_common(ah);
	const struct ani_ofdm_level_entry *entry_ofdm;
	const struct ani_cck_level_entry *entry_cck;

	aniState->noiseFloor = BEACON_RSSI(ah);

	ath_dbg(common, ATH_DBG_ANI,
		"**** ofdmlevel %d=>%d, rssi=%d[lo=%d hi=%d]\n",
		aniState->ofdmNoiseImmunityLevel,
		immunityLevel, aniState->noiseFloor,
		aniState->rssiThrLow, aniState->rssiThrHigh);

	aniState->ofdmNoiseImmunityLevel = immunityLevel;

	entry_ofdm = &ofdm_level_table[aniState->ofdmNoiseImmunityLevel];
	entry_cck = &cck_level_table[aniState->cckNoiseImmunityLevel];

	if (aniState->spurImmunityLevel != entry_ofdm->spur_immunity_level)
		ath9k_hw_ani_control(ah,
				     ATH9K_ANI_SPUR_IMMUNITY_LEVEL,
				     entry_ofdm->spur_immunity_level);

	if (aniState->firstepLevel != entry_ofdm->fir_step_level &&
	    entry_ofdm->fir_step_level >= entry_cck->fir_step_level)
		ath9k_hw_ani_control(ah,
				     ATH9K_ANI_FIRSTEP_LEVEL,
				     entry_ofdm->fir_step_level);

	if ((ah->opmode != NL80211_IFTYPE_STATION &&
	     ah->opmode != NL80211_IFTYPE_ADHOC) ||
	    aniState->noiseFloor <= aniState->rssiThrHigh) {
		if (aniState->ofdmWeakSigDetectOff)
			/* force on ofdm weak sig detect */
			ath9k_hw_ani_control(ah,
				ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
					     true);
		else if (aniState->ofdmWeakSigDetectOff ==
			 entry_ofdm->ofdm_weak_signal_on)
			ath9k_hw_ani_control(ah,
				ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				entry_ofdm->ofdm_weak_signal_on);
	}
}

static void ath9k_hw_ani_ofdm_err_trigger(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;

	if (!DO_ANI(ah))
		return;

	if (!use_new_ani(ah)) {
		ath9k_hw_ani_ofdm_err_trigger_old(ah);
		return;
	}

	aniState = &ah->curchan->ani;

	if (aniState->ofdmNoiseImmunityLevel < ATH9K_ANI_OFDM_MAX_LEVEL)
		ath9k_hw_set_ofdm_nil(ah, aniState->ofdmNoiseImmunityLevel + 1);
}

/*
 * Set the ANI settings to match an CCK level.
 */
static void ath9k_hw_set_cck_nil(struct ath_hw *ah, u_int8_t immunityLevel)
{
	struct ar5416AniState *aniState = &ah->curchan->ani;
	struct ath_common *common = ath9k_hw_common(ah);
	const struct ani_ofdm_level_entry *entry_ofdm;
	const struct ani_cck_level_entry *entry_cck;

	aniState->noiseFloor = BEACON_RSSI(ah);
	ath_dbg(common, ATH_DBG_ANI,
		"**** ccklevel %d=>%d, rssi=%d[lo=%d hi=%d]\n",
		aniState->cckNoiseImmunityLevel, immunityLevel,
		aniState->noiseFloor, aniState->rssiThrLow,
		aniState->rssiThrHigh);

	if ((ah->opmode == NL80211_IFTYPE_STATION ||
	     ah->opmode == NL80211_IFTYPE_ADHOC) &&
	    aniState->noiseFloor <= aniState->rssiThrLow &&
	    immunityLevel > ATH9K_ANI_CCK_MAX_LEVEL_LOW_RSSI)
		immunityLevel = ATH9K_ANI_CCK_MAX_LEVEL_LOW_RSSI;

	aniState->cckNoiseImmunityLevel = immunityLevel;

	entry_ofdm = &ofdm_level_table[aniState->ofdmNoiseImmunityLevel];
	entry_cck = &cck_level_table[aniState->cckNoiseImmunityLevel];

	if (aniState->firstepLevel != entry_cck->fir_step_level &&
	    entry_cck->fir_step_level >= entry_ofdm->fir_step_level)
		ath9k_hw_ani_control(ah,
				     ATH9K_ANI_FIRSTEP_LEVEL,
				     entry_cck->fir_step_level);

	/* Skip MRC CCK for pre AR9003 families */
	if (!AR_SREV_9300_20_OR_LATER(ah))
		return;

	if (aniState->mrcCCKOff == entry_cck->mrc_cck_on)
		ath9k_hw_ani_control(ah,
				     ATH9K_ANI_MRC_CCK,
				     entry_cck->mrc_cck_on);
}

static void ath9k_hw_ani_cck_err_trigger(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;

	if (!DO_ANI(ah))
		return;

	if (!use_new_ani(ah)) {
		ath9k_hw_ani_cck_err_trigger_old(ah);
		return;
	}

	aniState = &ah->curchan->ani;

	if (aniState->cckNoiseImmunityLevel < ATH9K_ANI_CCK_MAX_LEVEL)
		ath9k_hw_set_cck_nil(ah, aniState->cckNoiseImmunityLevel + 1);
}

static void ath9k_hw_ani_lower_immunity_old(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;
	int32_t rssi;

	aniState = &ah->curchan->ani;

	if (ah->opmode == NL80211_IFTYPE_AP) {
		if (aniState->firstepLevel > 0) {
			if (ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
						 aniState->firstepLevel - 1))
				return;
		}
	} else {
		rssi = BEACON_RSSI(ah);
		if (rssi > aniState->rssiThrHigh) {
			/* XXX: Handle me */
		} else if (rssi > aniState->rssiThrLow) {
			if (aniState->ofdmWeakSigDetectOff) {
				if (ath9k_hw_ani_control(ah,
					 ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
					 true) == true)
					return;
			}
			if (aniState->firstepLevel > 0) {
				if (ath9k_hw_ani_control(ah,
					 ATH9K_ANI_FIRSTEP_LEVEL,
					 aniState->firstepLevel - 1) == true)
					return;
			}
		} else {
			if (aniState->firstepLevel > 0) {
				if (ath9k_hw_ani_control(ah,
					 ATH9K_ANI_FIRSTEP_LEVEL,
					 aniState->firstepLevel - 1) == true)
					return;
			}
		}
	}

	if (aniState->spurImmunityLevel > 0) {
		if (ath9k_hw_ani_control(ah, ATH9K_ANI_SPUR_IMMUNITY_LEVEL,
					 aniState->spurImmunityLevel - 1))
			return;
	}

	if (aniState->noiseImmunityLevel > 0) {
		ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL,
				     aniState->noiseImmunityLevel - 1);
		return;
	}
}

/*
 * only lower either OFDM or CCK errors per turn
 * we lower the other one next time
 */
static void ath9k_hw_ani_lower_immunity(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;

	aniState = &ah->curchan->ani;

	if (!use_new_ani(ah)) {
		ath9k_hw_ani_lower_immunity_old(ah);
		return;
	}

	/* lower OFDM noise immunity */
	if (aniState->ofdmNoiseImmunityLevel > 0 &&
	    (aniState->ofdmsTurn || aniState->cckNoiseImmunityLevel == 0)) {
		ath9k_hw_set_ofdm_nil(ah, aniState->ofdmNoiseImmunityLevel - 1);
		return;
	}

	/* lower CCK noise immunity */
	if (aniState->cckNoiseImmunityLevel > 0)
		ath9k_hw_set_cck_nil(ah, aniState->cckNoiseImmunityLevel - 1);
}

static void ath9k_ani_reset_old(struct ath_hw *ah, bool is_scanning)
{
	struct ar5416AniState *aniState;
	struct ath9k_channel *chan = ah->curchan;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!DO_ANI(ah))
		return;

	aniState = &ah->curchan->ani;

	if (ah->opmode != NL80211_IFTYPE_STATION
	    && ah->opmode != NL80211_IFTYPE_ADHOC) {
		ath_dbg(common, ATH_DBG_ANI,
			"Reset ANI state opmode %u\n", ah->opmode);
		ah->stats.ast_ani_reset++;

		if (ah->opmode == NL80211_IFTYPE_AP) {
			/*
			 * ath9k_hw_ani_control() will only process items set on
			 * ah->ani_function
			 */
			if (IS_CHAN_2GHZ(chan))
				ah->ani_function = (ATH9K_ANI_SPUR_IMMUNITY_LEVEL |
						    ATH9K_ANI_FIRSTEP_LEVEL);
			else
				ah->ani_function = 0;
		}

		ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL, 0);
		ath9k_hw_ani_control(ah, ATH9K_ANI_SPUR_IMMUNITY_LEVEL, 0);
		ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL, 0);
		ath9k_hw_ani_control(ah, ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				     !ATH9K_ANI_USE_OFDM_WEAK_SIG);
		ath9k_hw_ani_control(ah, ATH9K_ANI_CCK_WEAK_SIGNAL_THR,
				     ATH9K_ANI_CCK_WEAK_SIG_THR);

		ath9k_hw_setrxfilter(ah, ath9k_hw_getrxfilter(ah) |
				     ATH9K_RX_FILTER_PHYERR);

		ath9k_ani_restart(ah);
		return;
	}

	if (aniState->noiseImmunityLevel != 0)
		ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL,
				     aniState->noiseImmunityLevel);
	if (aniState->spurImmunityLevel != 0)
		ath9k_hw_ani_control(ah, ATH9K_ANI_SPUR_IMMUNITY_LEVEL,
				     aniState->spurImmunityLevel);
	if (aniState->ofdmWeakSigDetectOff)
		ath9k_hw_ani_control(ah, ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				     !aniState->ofdmWeakSigDetectOff);
	if (aniState->cckWeakSigThreshold)
		ath9k_hw_ani_control(ah, ATH9K_ANI_CCK_WEAK_SIGNAL_THR,
				     aniState->cckWeakSigThreshold);
	if (aniState->firstepLevel != 0)
		ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
				     aniState->firstepLevel);

	ath9k_hw_setrxfilter(ah, ath9k_hw_getrxfilter(ah) &
			     ~ATH9K_RX_FILTER_PHYERR);
	ath9k_ani_restart(ah);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	REGWRITE_BUFFER_FLUSH(ah);
}

/*
 * Restore the ANI parameters in the HAL and reset the statistics.
 * This routine should be called for every hardware reset and for
 * every channel change.
 */
void ath9k_ani_reset(struct ath_hw *ah, bool is_scanning)
{
	struct ar5416AniState *aniState = &ah->curchan->ani;
	struct ath9k_channel *chan = ah->curchan;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!DO_ANI(ah))
		return;

	if (!use_new_ani(ah))
		return ath9k_ani_reset_old(ah, is_scanning);

	BUG_ON(aniState == NULL);
	ah->stats.ast_ani_reset++;

	/* only allow a subset of functions in AP mode */
	if (ah->opmode == NL80211_IFTYPE_AP) {
		if (IS_CHAN_2GHZ(chan)) {
			ah->ani_function = (ATH9K_ANI_SPUR_IMMUNITY_LEVEL |
					    ATH9K_ANI_FIRSTEP_LEVEL);
			if (AR_SREV_9300_20_OR_LATER(ah))
				ah->ani_function |= ATH9K_ANI_MRC_CCK;
		} else
			ah->ani_function = 0;
	}

	/* always allow mode (on/off) to be controlled */
	ah->ani_function |= ATH9K_ANI_MODE;

	if (is_scanning ||
	    (ah->opmode != NL80211_IFTYPE_STATION &&
	     ah->opmode != NL80211_IFTYPE_ADHOC)) {
		/*
		 * If we're scanning or in AP mode, the defaults (ini)
		 * should be in place. For an AP we assume the historical
		 * levels for this channel are probably outdated so start
		 * from defaults instead.
		 */
		if (aniState->ofdmNoiseImmunityLevel !=
		    ATH9K_ANI_OFDM_DEF_LEVEL ||
		    aniState->cckNoiseImmunityLevel !=
		    ATH9K_ANI_CCK_DEF_LEVEL) {
			ath_dbg(common, ATH_DBG_ANI,
				"Restore defaults: opmode %u chan %d Mhz/0x%x is_scanning=%d ofdm:%d cck:%d\n",
				ah->opmode,
				chan->channel,
				chan->channelFlags,
				is_scanning,
				aniState->ofdmNoiseImmunityLevel,
				aniState->cckNoiseImmunityLevel);

			ath9k_hw_set_ofdm_nil(ah, ATH9K_ANI_OFDM_DEF_LEVEL);
			ath9k_hw_set_cck_nil(ah, ATH9K_ANI_CCK_DEF_LEVEL);
		}
	} else {
		/*
		 * restore historical levels for this channel
		 */
		ath_dbg(common, ATH_DBG_ANI,
			"Restore history: opmode %u chan %d Mhz/0x%x is_scanning=%d ofdm:%d cck:%d\n",
			ah->opmode,
			chan->channel,
			chan->channelFlags,
			is_scanning,
			aniState->ofdmNoiseImmunityLevel,
			aniState->cckNoiseImmunityLevel);

			ath9k_hw_set_ofdm_nil(ah,
					      aniState->ofdmNoiseImmunityLevel);
			ath9k_hw_set_cck_nil(ah,
					     aniState->cckNoiseImmunityLevel);
	}

	/*
	 * enable phy counters if hw supports or if not, enable phy
	 * interrupts (so we can count each one)
	 */
	ath9k_ani_restart(ah);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	REGWRITE_BUFFER_FLUSH(ah);
}

static bool ath9k_hw_ani_read_counters(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ar5416AniState *aniState = &ah->curchan->ani;
	u32 ofdm_base = 0;
	u32 cck_base = 0;
	u32 ofdmPhyErrCnt, cckPhyErrCnt;
	u32 phyCnt1, phyCnt2;
	int32_t listenTime;

	ath_hw_cycle_counters_update(common);
	listenTime = ath_hw_get_listen_time(common);

	if (listenTime <= 0) {
		ah->stats.ast_ani_lneg++;
		ath9k_ani_restart(ah);
		return false;
	}

	if (!use_new_ani(ah)) {
		ofdm_base = AR_PHY_COUNTMAX - ah->config.ofdm_trig_high;
		cck_base = AR_PHY_COUNTMAX - ah->config.cck_trig_high;
	}

	aniState->listenTime += listenTime;

	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);

	phyCnt1 = REG_READ(ah, AR_PHY_ERR_1);
	phyCnt2 = REG_READ(ah, AR_PHY_ERR_2);

	if (!use_new_ani(ah) && (phyCnt1 < ofdm_base || phyCnt2 < cck_base)) {
		if (phyCnt1 < ofdm_base) {
			ath_dbg(common, ATH_DBG_ANI,
				"phyCnt1 0x%x, resetting counter value to 0x%x\n",
				phyCnt1, ofdm_base);
			REG_WRITE(ah, AR_PHY_ERR_1, ofdm_base);
			REG_WRITE(ah, AR_PHY_ERR_MASK_1,
				  AR_PHY_ERR_OFDM_TIMING);
		}
		if (phyCnt2 < cck_base) {
			ath_dbg(common, ATH_DBG_ANI,
				"phyCnt2 0x%x, resetting counter value to 0x%x\n",
				phyCnt2, cck_base);
			REG_WRITE(ah, AR_PHY_ERR_2, cck_base);
			REG_WRITE(ah, AR_PHY_ERR_MASK_2,
				  AR_PHY_ERR_CCK_TIMING);
		}
		return false;
	}

	ofdmPhyErrCnt = phyCnt1 - ofdm_base;
	ah->stats.ast_ani_ofdmerrs +=
		ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
	aniState->ofdmPhyErrCount = ofdmPhyErrCnt;

	cckPhyErrCnt = phyCnt2 - cck_base;
	ah->stats.ast_ani_cckerrs +=
		cckPhyErrCnt - aniState->cckPhyErrCount;
	aniState->cckPhyErrCount = cckPhyErrCnt;
	return true;
}

void ath9k_hw_ani_monitor(struct ath_hw *ah, struct ath9k_channel *chan)
{
	struct ar5416AniState *aniState;
	struct ath_common *common = ath9k_hw_common(ah);
	u32 ofdmPhyErrRate, cckPhyErrRate;

	if (!DO_ANI(ah))
		return;

	aniState = &ah->curchan->ani;
	if (WARN_ON(!aniState))
		return;

	if (!ath9k_hw_ani_read_counters(ah))
		return;

	ofdmPhyErrRate = aniState->ofdmPhyErrCount * 1000 /
			 aniState->listenTime;
	cckPhyErrRate =  aniState->cckPhyErrCount * 1000 /
			 aniState->listenTime;

	ath_dbg(common, ATH_DBG_ANI,
		"listenTime=%d OFDM:%d errs=%d/s CCK:%d errs=%d/s ofdm_turn=%d\n",
		aniState->listenTime,
		aniState->ofdmNoiseImmunityLevel,
		ofdmPhyErrRate, aniState->cckNoiseImmunityLevel,
		cckPhyErrRate, aniState->ofdmsTurn);

	if (aniState->listenTime > 5 * ah->aniperiod) {
		if (ofdmPhyErrRate <= ah->config.ofdm_trig_low &&
		    cckPhyErrRate <= ah->config.cck_trig_low) {
			ath9k_hw_ani_lower_immunity(ah);
			aniState->ofdmsTurn = !aniState->ofdmsTurn;
		}
		ath9k_ani_restart(ah);
	} else if (aniState->listenTime > ah->aniperiod) {
		/* check to see if need to raise immunity */
		if (ofdmPhyErrRate > ah->config.ofdm_trig_high &&
		    (cckPhyErrRate <= ah->config.cck_trig_high ||
		     aniState->ofdmsTurn)) {
			ath9k_hw_ani_ofdm_err_trigger(ah);
			ath9k_ani_restart(ah);
			aniState->ofdmsTurn = false;
		} else if (cckPhyErrRate > ah->config.cck_trig_high) {
			ath9k_hw_ani_cck_err_trigger(ah);
			ath9k_ani_restart(ah);
			aniState->ofdmsTurn = true;
		}
	}
}
EXPORT_SYMBOL(ath9k_hw_ani_monitor);

void ath9k_enable_mib_counters(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	ath_dbg(common, ATH_DBG_ANI, "Enable MIB counters\n");

	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
	REG_WRITE(ah, AR_MIBC,
		  ~(AR_MIBC_COW | AR_MIBC_FMC | AR_MIBC_CMC | AR_MIBC_MCS)
		  & 0x0f);
	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	REGWRITE_BUFFER_FLUSH(ah);
}

/* Freeze the MIB counters, get the stats and then clear them */
void ath9k_hw_disable_mib_counters(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	ath_dbg(common, ATH_DBG_ANI, "Disable MIB counters\n");

	REG_WRITE(ah, AR_MIBC, AR_MIBC_FMC);
	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);
	REG_WRITE(ah, AR_MIBC, AR_MIBC_CMC);
	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
}
EXPORT_SYMBOL(ath9k_hw_disable_mib_counters);

/*
 * Process a MIB interrupt.  We may potentially be invoked because
 * any of the MIB counters overflow/trigger so don't assume we're
 * here because a PHY error counter triggered.
 */
void ath9k_hw_proc_mib_event(struct ath_hw *ah)
{
	u32 phyCnt1, phyCnt2;

	/* Reset these counters regardless */
	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
	if (!(REG_READ(ah, AR_SLP_MIB_CTRL) & AR_SLP_MIB_PENDING))
		REG_WRITE(ah, AR_SLP_MIB_CTRL, AR_SLP_MIB_CLEAR);

	/* Clear the mib counters and save them in the stats */
	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);

	if (!DO_ANI(ah)) {
		/*
		 * We must always clear the interrupt cause by
		 * resetting the phy error regs.
		 */
		REG_WRITE(ah, AR_PHY_ERR_1, 0);
		REG_WRITE(ah, AR_PHY_ERR_2, 0);
		return;
	}

	/* NB: these are not reset-on-read */
	phyCnt1 = REG_READ(ah, AR_PHY_ERR_1);
	phyCnt2 = REG_READ(ah, AR_PHY_ERR_2);
	if (((phyCnt1 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK) ||
	    ((phyCnt2 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK)) {

		if (!use_new_ani(ah))
			ath9k_hw_ani_read_counters(ah);

		/* NB: always restart to insure the h/w counters are reset */
		ath9k_ani_restart(ah);
	}
}
EXPORT_SYMBOL(ath9k_hw_proc_mib_event);

void ath9k_hw_ani_setup(struct ath_hw *ah)
{
	int i;

	static const int totalSizeDesired[] = { -55, -55, -55, -55, -62 };
	static const int coarseHigh[] = { -14, -14, -14, -14, -12 };
	static const int coarseLow[] = { -64, -64, -64, -64, -70 };
	static const int firpwr[] = { -78, -78, -78, -78, -80 };

	for (i = 0; i < 5; i++) {
		ah->totalSizeDesired[i] = totalSizeDesired[i];
		ah->coarse_high[i] = coarseHigh[i];
		ah->coarse_low[i] = coarseLow[i];
		ah->firpwr[i] = firpwr[i];
	}
}

void ath9k_hw_ani_init(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int i;

	ath_dbg(common, ATH_DBG_ANI, "Initialize ANI\n");

	if (use_new_ani(ah)) {
		ah->config.ofdm_trig_high = ATH9K_ANI_OFDM_TRIG_HIGH_NEW;
		ah->config.ofdm_trig_low = ATH9K_ANI_OFDM_TRIG_LOW_NEW;

		ah->config.cck_trig_high = ATH9K_ANI_CCK_TRIG_HIGH_NEW;
		ah->config.cck_trig_low = ATH9K_ANI_CCK_TRIG_LOW_NEW;
	} else {
		ah->config.ofdm_trig_high = ATH9K_ANI_OFDM_TRIG_HIGH_OLD;
		ah->config.ofdm_trig_low = ATH9K_ANI_OFDM_TRIG_LOW_OLD;

		ah->config.cck_trig_high = ATH9K_ANI_CCK_TRIG_HIGH_OLD;
		ah->config.cck_trig_low = ATH9K_ANI_CCK_TRIG_LOW_OLD;
	}

	for (i = 0; i < ARRAY_SIZE(ah->channels); i++) {
		struct ath9k_channel *chan = &ah->channels[i];
		struct ar5416AniState *ani = &chan->ani;

		if (use_new_ani(ah)) {
			ani->spurImmunityLevel =
				ATH9K_ANI_SPUR_IMMUNE_LVL_NEW;

			ani->firstepLevel = ATH9K_ANI_FIRSTEP_LVL_NEW;

			if (AR_SREV_9300_20_OR_LATER(ah))
				ani->mrcCCKOff =
					!ATH9K_ANI_ENABLE_MRC_CCK;
			else
				ani->mrcCCKOff = true;

			ani->ofdmsTurn = true;
		} else {
			ani->spurImmunityLevel =
				ATH9K_ANI_SPUR_IMMUNE_LVL_OLD;
			ani->firstepLevel = ATH9K_ANI_FIRSTEP_LVL_OLD;

			ani->cckWeakSigThreshold =
				ATH9K_ANI_CCK_WEAK_SIG_THR;
		}

		ani->rssiThrHigh = ATH9K_ANI_RSSI_THR_HIGH;
		ani->rssiThrLow = ATH9K_ANI_RSSI_THR_LOW;
		ani->ofdmWeakSigDetectOff =
			!ATH9K_ANI_USE_OFDM_WEAK_SIG;
		ani->cckNoiseImmunityLevel = ATH9K_ANI_CCK_DEF_LEVEL;
	}

	/*
	 * since we expect some ongoing maintenance on the tables, let's sanity
	 * check here default level should not modify INI setting.
	 */
	if (use_new_ani(ah)) {
		const struct ani_ofdm_level_entry *entry_ofdm;
		const struct ani_cck_level_entry *entry_cck;

		entry_ofdm = &ofdm_level_table[ATH9K_ANI_OFDM_DEF_LEVEL];
		entry_cck = &cck_level_table[ATH9K_ANI_CCK_DEF_LEVEL];

		ah->aniperiod = ATH9K_ANI_PERIOD_NEW;
		ah->config.ani_poll_interval = ATH9K_ANI_POLLINTERVAL_NEW;
	} else {
		ah->aniperiod = ATH9K_ANI_PERIOD_OLD;
		ah->config.ani_poll_interval = ATH9K_ANI_POLLINTERVAL_OLD;
	}

	if (ah->config.enable_ani)
		ah->proc_phyerr |= HAL_PROCESS_ANI;

	ath9k_ani_restart(ah);
	ath9k_enable_mib_counters(ah);
}
