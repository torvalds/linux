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
	(sizeof(ofdm_level_table)/sizeof(ofdm_level_table[0]))
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
	(sizeof(cck_level_table)/sizeof(cck_level_table[0]))
#define ATH9K_ANI_CCK_MAX_LEVEL \
	(ATH9K_ANI_CCK_NUM_LEVEL-1)
#define ATH9K_ANI_CCK_MAX_LEVEL_LOW_RSSI \
	(ATH9K_ANI_CCK_NUM_LEVEL-3)
#define ATH9K_ANI_CCK_DEF_LEVEL \
	2 /* default level - matches the INI settings */

/* Private to ani.c */
static void ath9k_hw_ani_lower_immunity(struct ath_hw *ah)
{
	ath9k_hw_private_ops(ah)->ani_lower_immunity(ah);
}

int ath9k_hw_get_ani_channel_idx(struct ath_hw *ah,
				 struct ath9k_channel *chan)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ah->ani); i++) {
		if (ah->ani[i].c &&
		    ah->ani[i].c->channel == chan->channel)
			return i;
		if (ah->ani[i].c == NULL) {
			ah->ani[i].c = chan;
			return i;
		}
	}

	ath_print(ath9k_hw_common(ah), ATH_DBG_ANI,
		  "No more channel states left. Using channel 0\n");

	return 0;
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

static void ath9k_ani_restart_old(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!DO_ANI(ah))
		return;

	aniState = ah->curani;
	aniState->listenTime = 0;

	if (aniState->ofdmTrigHigh > AR_PHY_COUNTMAX) {
		aniState->ofdmPhyErrBase = 0;
		ath_print(common, ATH_DBG_ANI,
			  "OFDM Trigger is too high for hw counters\n");
	} else {
		aniState->ofdmPhyErrBase =
			AR_PHY_COUNTMAX - aniState->ofdmTrigHigh;
	}
	if (aniState->cckTrigHigh > AR_PHY_COUNTMAX) {
		aniState->cckPhyErrBase = 0;
		ath_print(common, ATH_DBG_ANI,
			  "CCK Trigger is too high for hw counters\n");
	} else {
		aniState->cckPhyErrBase =
			AR_PHY_COUNTMAX - aniState->cckTrigHigh;
	}
	ath_print(common, ATH_DBG_ANI,
		  "Writing ofdmbase=%u   cckbase=%u\n",
		  aniState->ofdmPhyErrBase,
		  aniState->cckPhyErrBase);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_PHY_ERR_1, aniState->ofdmPhyErrBase);
	REG_WRITE(ah, AR_PHY_ERR_2, aniState->cckPhyErrBase);
	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);

	aniState->ofdmPhyErrCount = 0;
	aniState->cckPhyErrCount = 0;
}

static void ath9k_ani_restart_new(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!DO_ANI(ah))
		return;

	aniState = ah->curani;
	aniState->listenTime = 0;

	aniState->ofdmPhyErrBase = 0;
	aniState->cckPhyErrBase = 0;

	ath_print(common, ATH_DBG_ANI,
		  "Writing ofdmbase=%08x   cckbase=%08x\n",
		  aniState->ofdmPhyErrBase,
		  aniState->cckPhyErrBase);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_PHY_ERR_1, aniState->ofdmPhyErrBase);
	REG_WRITE(ah, AR_PHY_ERR_2, aniState->cckPhyErrBase);
	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);

	aniState->ofdmPhyErrCount = 0;
	aniState->cckPhyErrCount = 0;
}

static void ath9k_hw_ani_ofdm_err_trigger_old(struct ath_hw *ah)
{
	struct ieee80211_conf *conf = &ath9k_hw_common(ah)->hw->conf;
	struct ar5416AniState *aniState;
	int32_t rssi;

	if (!DO_ANI(ah))
		return;

	aniState = ah->curani;

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

	if (!DO_ANI(ah))
		return;

	aniState = ah->curani;
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
	struct ar5416AniState *aniState = ah->curani;
	struct ath_common *common = ath9k_hw_common(ah);
	const struct ani_ofdm_level_entry *entry_ofdm;
	const struct ani_cck_level_entry *entry_cck;

	aniState->noiseFloor = BEACON_RSSI(ah);

	ath_print(common, ATH_DBG_ANI,
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

static void ath9k_hw_ani_ofdm_err_trigger_new(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;

	if (!DO_ANI(ah))
		return;

	aniState = ah->curani;

	if (aniState->ofdmNoiseImmunityLevel < ATH9K_ANI_OFDM_MAX_LEVEL)
		ath9k_hw_set_ofdm_nil(ah, aniState->ofdmNoiseImmunityLevel + 1);
}

/*
 * Set the ANI settings to match an CCK level.
 */
static void ath9k_hw_set_cck_nil(struct ath_hw *ah, u_int8_t immunityLevel)
{
	struct ar5416AniState *aniState = ah->curani;
	struct ath_common *common = ath9k_hw_common(ah);
	const struct ani_ofdm_level_entry *entry_ofdm;
	const struct ani_cck_level_entry *entry_cck;

	aniState->noiseFloor = BEACON_RSSI(ah);
	ath_print(common, ATH_DBG_ANI,
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

static void ath9k_hw_ani_cck_err_trigger_new(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;

	if (!DO_ANI(ah))
		return;

	aniState = ah->curani;

	if (aniState->cckNoiseImmunityLevel < ATH9K_ANI_CCK_MAX_LEVEL)
		ath9k_hw_set_cck_nil(ah, aniState->cckNoiseImmunityLevel + 1);
}

static void ath9k_hw_ani_lower_immunity_old(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;
	int32_t rssi;

	aniState = ah->curani;

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
static void ath9k_hw_ani_lower_immunity_new(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;

	aniState = ah->curani;

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

static u8 ath9k_hw_chan_2_clockrate_mhz(struct ath_hw *ah)
{
	struct ath9k_channel *chan = ah->curchan;
	struct ieee80211_conf *conf = &ath9k_hw_common(ah)->hw->conf;
	u8 clockrate; /* in MHz */

	if (!ah->curchan) /* should really check for CCK instead */
		clockrate = ATH9K_CLOCK_RATE_CCK;
	else if (conf->channel->band == IEEE80211_BAND_2GHZ)
		clockrate = ATH9K_CLOCK_RATE_2GHZ_OFDM;
	else if (IS_CHAN_A_FAST_CLOCK(ah, chan))
		clockrate = ATH9K_CLOCK_FAST_RATE_5GHZ_OFDM;
	else
		clockrate = ATH9K_CLOCK_RATE_5GHZ_OFDM;

	if (conf_is_ht40(conf))
		return clockrate * 2;

	return clockrate;
}

static int32_t ath9k_hw_ani_get_listen_time(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;
	struct ath_common *common = ath9k_hw_common(ah);
	u32 txFrameCount, rxFrameCount, cycleCount;
	int32_t listenTime;

	txFrameCount = REG_READ(ah, AR_TFCNT);
	rxFrameCount = REG_READ(ah, AR_RFCNT);
	cycleCount = REG_READ(ah, AR_CCCNT);

	aniState = ah->curani;
	if (aniState->cycleCount == 0 || aniState->cycleCount > cycleCount) {
		listenTime = 0;
		ah->stats.ast_ani_lzero++;
		ath_print(common, ATH_DBG_ANI,
			  "1st call: aniState->cycleCount=%d\n",
			  aniState->cycleCount);
	} else {
		int32_t ccdelta = cycleCount - aniState->cycleCount;
		int32_t rfdelta = rxFrameCount - aniState->rxFrameCount;
		int32_t tfdelta = txFrameCount - aniState->txFrameCount;
		int32_t clock_rate;

		/*
		 * convert HW counter values to ms using mode
		 * specifix clock rate
		 */
		clock_rate = ath9k_hw_chan_2_clockrate_mhz(ah) * 1000;;

		listenTime = (ccdelta - rfdelta - tfdelta) / clock_rate;

		ath_print(common, ATH_DBG_ANI,
			  "cyclecount=%d, rfcount=%d, "
			  "tfcount=%d, listenTime=%d CLOCK_RATE=%d\n",
			  ccdelta, rfdelta, tfdelta, listenTime, clock_rate);
	}

	aniState->cycleCount = cycleCount;
	aniState->txFrameCount = txFrameCount;
	aniState->rxFrameCount = rxFrameCount;

	return listenTime;
}

static void ath9k_ani_reset_old(struct ath_hw *ah, bool is_scanning)
{
	struct ar5416AniState *aniState;
	struct ath9k_channel *chan = ah->curchan;
	struct ath_common *common = ath9k_hw_common(ah);
	int index;

	if (!DO_ANI(ah))
		return;

	index = ath9k_hw_get_ani_channel_idx(ah, chan);
	aniState = &ah->ani[index];
	ah->curani = aniState;

	if (DO_ANI(ah) && ah->opmode != NL80211_IFTYPE_STATION
	    && ah->opmode != NL80211_IFTYPE_ADHOC) {
		ath_print(common, ATH_DBG_ANI,
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

		if (ah->opmode == NL80211_IFTYPE_AP) {
			ah->curani->ofdmTrigHigh =
				ah->config.ofdm_trig_high;
			ah->curani->ofdmTrigLow =
				ah->config.ofdm_trig_low;
			ah->curani->cckTrigHigh =
				ah->config.cck_trig_high;
			ah->curani->cckTrigLow =
				ah->config.cck_trig_low;
		}
		ath9k_ani_restart_old(ah);
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
	ath9k_ani_restart_old(ah);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);
}

/*
 * Restore the ANI parameters in the HAL and reset the statistics.
 * This routine should be called for every hardware reset and for
 * every channel change.
 */
static void ath9k_ani_reset_new(struct ath_hw *ah, bool is_scanning)
{
	struct ar5416AniState *aniState = ah->curani;
	struct ath9k_channel *chan = ah->curchan;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!DO_ANI(ah))
		return;

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
			ath_print(common, ATH_DBG_ANI,
				  "Restore defaults: opmode %u "
				  "chan %d Mhz/0x%x is_scanning=%d "
				  "ofdm:%d cck:%d\n",
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
		ath_print(common, ATH_DBG_ANI,
			  "Restore history: opmode %u "
			  "chan %d Mhz/0x%x is_scanning=%d "
			  "ofdm:%d cck:%d\n",
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
	ath9k_ani_restart_new(ah);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);
}

static void ath9k_hw_ani_monitor_old(struct ath_hw *ah,
				     struct ath9k_channel *chan)
{
	struct ar5416AniState *aniState;
	struct ath_common *common = ath9k_hw_common(ah);
	int32_t listenTime;
	u32 phyCnt1, phyCnt2;
	u32 ofdmPhyErrCnt, cckPhyErrCnt;

	if (!DO_ANI(ah))
		return;

	aniState = ah->curani;

	listenTime = ath9k_hw_ani_get_listen_time(ah);
	if (listenTime < 0) {
		ah->stats.ast_ani_lneg++;
		ath9k_ani_restart_old(ah);
		return;
	}

	aniState->listenTime += listenTime;

	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);

	phyCnt1 = REG_READ(ah, AR_PHY_ERR_1);
	phyCnt2 = REG_READ(ah, AR_PHY_ERR_2);

	if (phyCnt1 < aniState->ofdmPhyErrBase ||
	    phyCnt2 < aniState->cckPhyErrBase) {
		if (phyCnt1 < aniState->ofdmPhyErrBase) {
			ath_print(common, ATH_DBG_ANI,
				  "phyCnt1 0x%x, resetting "
				  "counter value to 0x%x\n",
				  phyCnt1,
				  aniState->ofdmPhyErrBase);
			REG_WRITE(ah, AR_PHY_ERR_1,
				  aniState->ofdmPhyErrBase);
			REG_WRITE(ah, AR_PHY_ERR_MASK_1,
				  AR_PHY_ERR_OFDM_TIMING);
		}
		if (phyCnt2 < aniState->cckPhyErrBase) {
			ath_print(common, ATH_DBG_ANI,
				  "phyCnt2 0x%x, resetting "
				  "counter value to 0x%x\n",
				  phyCnt2,
				  aniState->cckPhyErrBase);
			REG_WRITE(ah, AR_PHY_ERR_2,
				  aniState->cckPhyErrBase);
			REG_WRITE(ah, AR_PHY_ERR_MASK_2,
				  AR_PHY_ERR_CCK_TIMING);
		}
		return;
	}

	ofdmPhyErrCnt = phyCnt1 - aniState->ofdmPhyErrBase;
	ah->stats.ast_ani_ofdmerrs +=
		ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
	aniState->ofdmPhyErrCount = ofdmPhyErrCnt;

	cckPhyErrCnt = phyCnt2 - aniState->cckPhyErrBase;
	ah->stats.ast_ani_cckerrs +=
		cckPhyErrCnt - aniState->cckPhyErrCount;
	aniState->cckPhyErrCount = cckPhyErrCnt;

	if (aniState->listenTime > 5 * ah->aniperiod) {
		if (aniState->ofdmPhyErrCount <= aniState->listenTime *
		    aniState->ofdmTrigLow / 1000 &&
		    aniState->cckPhyErrCount <= aniState->listenTime *
		    aniState->cckTrigLow / 1000)
			ath9k_hw_ani_lower_immunity(ah);
		ath9k_ani_restart_old(ah);
	} else if (aniState->listenTime > ah->aniperiod) {
		if (aniState->ofdmPhyErrCount > aniState->listenTime *
		    aniState->ofdmTrigHigh / 1000) {
			ath9k_hw_ani_ofdm_err_trigger_old(ah);
			ath9k_ani_restart_old(ah);
		} else if (aniState->cckPhyErrCount >
			   aniState->listenTime * aniState->cckTrigHigh /
			   1000) {
			ath9k_hw_ani_cck_err_trigger_old(ah);
			ath9k_ani_restart_old(ah);
		}
	}
}

static void ath9k_hw_ani_monitor_new(struct ath_hw *ah,
				     struct ath9k_channel *chan)
{
	struct ar5416AniState *aniState;
	struct ath_common *common = ath9k_hw_common(ah);
	int32_t listenTime;
	u32 phyCnt1, phyCnt2;
	u32 ofdmPhyErrCnt, cckPhyErrCnt;
	u32 ofdmPhyErrRate, cckPhyErrRate;

	if (!DO_ANI(ah))
		return;

	aniState = ah->curani;
	if (WARN_ON(!aniState))
		return;

	listenTime = ath9k_hw_ani_get_listen_time(ah);
	if (listenTime <= 0) {
		ah->stats.ast_ani_lneg++;
		/* restart ANI period if listenTime is invalid */
		ath_print(common, ATH_DBG_ANI,
			  "listenTime=%d - on new ani monitor\n",
			  listenTime);
		ath9k_ani_restart_new(ah);
		return;
	}

	aniState->listenTime += listenTime;

	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);

	phyCnt1 = REG_READ(ah, AR_PHY_ERR_1);
	phyCnt2 = REG_READ(ah, AR_PHY_ERR_2);

	if (phyCnt1 < aniState->ofdmPhyErrBase ||
	    phyCnt2 < aniState->cckPhyErrBase) {
		if (phyCnt1 < aniState->ofdmPhyErrBase) {
			ath_print(common, ATH_DBG_ANI,
				  "phyCnt1 0x%x, resetting "
				  "counter value to 0x%x\n",
				  phyCnt1,
				  aniState->ofdmPhyErrBase);
			REG_WRITE(ah, AR_PHY_ERR_1,
				  aniState->ofdmPhyErrBase);
			REG_WRITE(ah, AR_PHY_ERR_MASK_1,
				  AR_PHY_ERR_OFDM_TIMING);
		}
		if (phyCnt2 < aniState->cckPhyErrBase) {
			ath_print(common, ATH_DBG_ANI,
				  "phyCnt2 0x%x, resetting "
				  "counter value to 0x%x\n",
				  phyCnt2,
				  aniState->cckPhyErrBase);
			REG_WRITE(ah, AR_PHY_ERR_2,
				  aniState->cckPhyErrBase);
			REG_WRITE(ah, AR_PHY_ERR_MASK_2,
				  AR_PHY_ERR_CCK_TIMING);
		}
		return;
	}

	ofdmPhyErrCnt = phyCnt1 - aniState->ofdmPhyErrBase;
	ah->stats.ast_ani_ofdmerrs +=
		ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
	aniState->ofdmPhyErrCount = ofdmPhyErrCnt;

	cckPhyErrCnt = phyCnt2 - aniState->cckPhyErrBase;
	ah->stats.ast_ani_cckerrs +=
		cckPhyErrCnt - aniState->cckPhyErrCount;
	aniState->cckPhyErrCount = cckPhyErrCnt;

	ath_print(common, ATH_DBG_ANI,
		  "Errors: OFDM=0x%08x-0x%08x=%d   "
		  "CCK=0x%08x-0x%08x=%d\n",
		  phyCnt1,
		  aniState->ofdmPhyErrBase,
		  ofdmPhyErrCnt,
		  phyCnt2,
		  aniState->cckPhyErrBase,
		  cckPhyErrCnt);

	ofdmPhyErrRate = aniState->ofdmPhyErrCount * 1000 /
			 aniState->listenTime;
	cckPhyErrRate =  aniState->cckPhyErrCount * 1000 /
			 aniState->listenTime;

	ath_print(common, ATH_DBG_ANI,
		  "listenTime=%d OFDM:%d errs=%d/s CCK:%d "
		  "errs=%d/s ofdm_turn=%d\n",
		  listenTime, aniState->ofdmNoiseImmunityLevel,
		  ofdmPhyErrRate, aniState->cckNoiseImmunityLevel,
		  cckPhyErrRate, aniState->ofdmsTurn);

	if (aniState->listenTime > 5 * ah->aniperiod) {
		if (ofdmPhyErrRate <= aniState->ofdmTrigLow &&
		    cckPhyErrRate <= aniState->cckTrigLow) {
			ath_print(common, ATH_DBG_ANI,
				  "1. listenTime=%d OFDM:%d errs=%d/s(<%d)  "
				  "CCK:%d errs=%d/s(<%d) -> "
				  "ath9k_hw_ani_lower_immunity()\n",
				  aniState->listenTime,
				  aniState->ofdmNoiseImmunityLevel,
				  ofdmPhyErrRate,
				  aniState->ofdmTrigLow,
				  aniState->cckNoiseImmunityLevel,
				  cckPhyErrRate,
				  aniState->cckTrigLow);
			ath9k_hw_ani_lower_immunity(ah);
			aniState->ofdmsTurn = !aniState->ofdmsTurn;
		}
		ath_print(common, ATH_DBG_ANI,
			  "1 listenTime=%d ofdm=%d/s cck=%d/s - "
			  "calling ath9k_ani_restart_new()\n",
			  aniState->listenTime, ofdmPhyErrRate, cckPhyErrRate);
		ath9k_ani_restart_new(ah);
	} else if (aniState->listenTime > ah->aniperiod) {
		/* check to see if need to raise immunity */
		if (ofdmPhyErrRate > aniState->ofdmTrigHigh &&
		    (cckPhyErrRate <= aniState->cckTrigHigh ||
		     aniState->ofdmsTurn)) {
			ath_print(common, ATH_DBG_ANI,
				  "2 listenTime=%d OFDM:%d errs=%d/s(>%d) -> "
				  "ath9k_hw_ani_ofdm_err_trigger_new()\n",
				  aniState->listenTime,
				  aniState->ofdmNoiseImmunityLevel,
				  ofdmPhyErrRate,
				  aniState->ofdmTrigHigh);
			ath9k_hw_ani_ofdm_err_trigger_new(ah);
			ath9k_ani_restart_new(ah);
			aniState->ofdmsTurn = false;
		} else if (cckPhyErrRate > aniState->cckTrigHigh) {
			ath_print(common, ATH_DBG_ANI,
				 "3 listenTime=%d CCK:%d errs=%d/s(>%d) -> "
				 "ath9k_hw_ani_cck_err_trigger_new()\n",
				 aniState->listenTime,
				 aniState->cckNoiseImmunityLevel,
				 cckPhyErrRate,
				 aniState->cckTrigHigh);
			ath9k_hw_ani_cck_err_trigger_new(ah);
			ath9k_ani_restart_new(ah);
			aniState->ofdmsTurn = true;
		}
	}
}

void ath9k_enable_mib_counters(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	ath_print(common, ATH_DBG_ANI, "Enable MIB counters\n");

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
	DISABLE_REGWRITE_BUFFER(ah);
}

/* Freeze the MIB counters, get the stats and then clear them */
void ath9k_hw_disable_mib_counters(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	ath_print(common, ATH_DBG_ANI, "Disable MIB counters\n");

	REG_WRITE(ah, AR_MIBC, AR_MIBC_FMC);
	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);
	REG_WRITE(ah, AR_MIBC, AR_MIBC_CMC);
	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
}
EXPORT_SYMBOL(ath9k_hw_disable_mib_counters);

u32 ath9k_hw_GetMibCycleCountsPct(struct ath_hw *ah,
				  u32 *rxc_pcnt,
				  u32 *rxf_pcnt,
				  u32 *txf_pcnt)
{
	struct ath_common *common = ath9k_hw_common(ah);
	static u32 cycles, rx_clear, rx_frame, tx_frame;
	u32 good = 1;

	u32 rc = REG_READ(ah, AR_RCCNT);
	u32 rf = REG_READ(ah, AR_RFCNT);
	u32 tf = REG_READ(ah, AR_TFCNT);
	u32 cc = REG_READ(ah, AR_CCCNT);

	if (cycles == 0 || cycles > cc) {
		ath_print(common, ATH_DBG_ANI,
			  "cycle counter wrap. ExtBusy = 0\n");
		good = 0;
	} else {
		u32 cc_d = cc - cycles;
		u32 rc_d = rc - rx_clear;
		u32 rf_d = rf - rx_frame;
		u32 tf_d = tf - tx_frame;

		if (cc_d != 0) {
			*rxc_pcnt = rc_d * 100 / cc_d;
			*rxf_pcnt = rf_d * 100 / cc_d;
			*txf_pcnt = tf_d * 100 / cc_d;
		} else {
			good = 0;
		}
	}

	cycles = cc;
	rx_frame = rf;
	rx_clear = rc;
	tx_frame = tf;

	return good;
}

/*
 * Process a MIB interrupt.  We may potentially be invoked because
 * any of the MIB counters overflow/trigger so don't assume we're
 * here because a PHY error counter triggered.
 */
static void ath9k_hw_proc_mib_event_old(struct ath_hw *ah)
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
		struct ar5416AniState *aniState = ah->curani;
		u32 ofdmPhyErrCnt, cckPhyErrCnt;

		/* NB: only use ast_ani_*errs with AH_PRIVATE_DIAG */
		ofdmPhyErrCnt = phyCnt1 - aniState->ofdmPhyErrBase;
		ah->stats.ast_ani_ofdmerrs +=
			ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
		aniState->ofdmPhyErrCount = ofdmPhyErrCnt;

		cckPhyErrCnt = phyCnt2 - aniState->cckPhyErrBase;
		ah->stats.ast_ani_cckerrs +=
			cckPhyErrCnt - aniState->cckPhyErrCount;
		aniState->cckPhyErrCount = cckPhyErrCnt;

		/*
		 * NB: figure out which counter triggered.  If both
		 * trigger we'll only deal with one as the processing
		 * clobbers the error counter so the trigger threshold
		 * check will never be true.
		 */
		if (aniState->ofdmPhyErrCount > aniState->ofdmTrigHigh)
			ath9k_hw_ani_ofdm_err_trigger_new(ah);
		if (aniState->cckPhyErrCount > aniState->cckTrigHigh)
			ath9k_hw_ani_cck_err_trigger_old(ah);
		/* NB: always restart to insure the h/w counters are reset */
		ath9k_ani_restart_old(ah);
	}
}

/*
 * Process a MIB interrupt.  We may potentially be invoked because
 * any of the MIB counters overflow/trigger so don't assume we're
 * here because a PHY error counter triggered.
 */
static void ath9k_hw_proc_mib_event_new(struct ath_hw *ah)
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

	/* NB: always restart to insure the h/w counters are reset */
	if (((phyCnt1 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK) ||
	    ((phyCnt2 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK))
		ath9k_ani_restart_new(ah);
}

void ath9k_hw_ani_setup(struct ath_hw *ah)
{
	int i;

	const int totalSizeDesired[] = { -55, -55, -55, -55, -62 };
	const int coarseHigh[] = { -14, -14, -14, -14, -12 };
	const int coarseLow[] = { -64, -64, -64, -64, -70 };
	const int firpwr[] = { -78, -78, -78, -78, -80 };

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

	ath_print(common, ATH_DBG_ANI, "Initialize ANI\n");

	memset(ah->ani, 0, sizeof(ah->ani));
	for (i = 0; i < ARRAY_SIZE(ah->ani); i++) {
		if (AR_SREV_9300_20_OR_LATER(ah) || modparam_force_new_ani) {
			ah->ani[i].ofdmTrigHigh = ATH9K_ANI_OFDM_TRIG_HIGH_NEW;
			ah->ani[i].ofdmTrigLow = ATH9K_ANI_OFDM_TRIG_LOW_NEW;

			ah->ani[i].cckTrigHigh = ATH9K_ANI_CCK_TRIG_HIGH_NEW;
			ah->ani[i].cckTrigLow = ATH9K_ANI_CCK_TRIG_LOW_NEW;

			ah->ani[i].spurImmunityLevel =
				ATH9K_ANI_SPUR_IMMUNE_LVL_NEW;

			ah->ani[i].firstepLevel = ATH9K_ANI_FIRSTEP_LVL_NEW;

			ah->ani[i].ofdmPhyErrBase = 0;
			ah->ani[i].cckPhyErrBase = 0;

			if (AR_SREV_9300_20_OR_LATER(ah))
				ah->ani[i].mrcCCKOff =
					!ATH9K_ANI_ENABLE_MRC_CCK;
			else
				ah->ani[i].mrcCCKOff = true;

			ah->ani[i].ofdmsTurn = true;
		} else {
			ah->ani[i].ofdmTrigHigh = ATH9K_ANI_OFDM_TRIG_HIGH_OLD;
			ah->ani[i].ofdmTrigLow = ATH9K_ANI_OFDM_TRIG_LOW_OLD;

			ah->ani[i].cckTrigHigh = ATH9K_ANI_CCK_TRIG_HIGH_OLD;
			ah->ani[i].cckTrigLow = ATH9K_ANI_CCK_TRIG_LOW_OLD;

			ah->ani[i].spurImmunityLevel =
				ATH9K_ANI_SPUR_IMMUNE_LVL_OLD;
			ah->ani[i].firstepLevel = ATH9K_ANI_FIRSTEP_LVL_OLD;

			ah->ani[i].ofdmPhyErrBase =
				AR_PHY_COUNTMAX - ATH9K_ANI_OFDM_TRIG_HIGH_OLD;
			ah->ani[i].cckPhyErrBase =
				AR_PHY_COUNTMAX - ATH9K_ANI_CCK_TRIG_HIGH_OLD;
			ah->ani[i].cckWeakSigThreshold =
				ATH9K_ANI_CCK_WEAK_SIG_THR;
		}

		ah->ani[i].rssiThrHigh = ATH9K_ANI_RSSI_THR_HIGH;
		ah->ani[i].rssiThrLow = ATH9K_ANI_RSSI_THR_LOW;
		ah->ani[i].ofdmWeakSigDetectOff =
			!ATH9K_ANI_USE_OFDM_WEAK_SIG;
		ah->ani[i].cckNoiseImmunityLevel = ATH9K_ANI_CCK_DEF_LEVEL;
	}

	/*
	 * since we expect some ongoing maintenance on the tables, let's sanity
	 * check here default level should not modify INI setting.
	 */
	if (AR_SREV_9300_20_OR_LATER(ah) || modparam_force_new_ani) {
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

	ath_print(common, ATH_DBG_ANI,
		  "Setting OfdmErrBase = 0x%08x\n",
		  ah->ani[0].ofdmPhyErrBase);
	ath_print(common, ATH_DBG_ANI, "Setting cckErrBase = 0x%08x\n",
		  ah->ani[0].cckPhyErrBase);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_PHY_ERR_1, ah->ani[0].ofdmPhyErrBase);
	REG_WRITE(ah, AR_PHY_ERR_2, ah->ani[0].cckPhyErrBase);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	ath9k_enable_mib_counters(ah);

	if (ah->config.enable_ani)
		ah->proc_phyerr |= HAL_PROCESS_ANI;
}

void ath9k_hw_attach_ani_ops_old(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);

	priv_ops->ani_reset = ath9k_ani_reset_old;
	priv_ops->ani_lower_immunity = ath9k_hw_ani_lower_immunity_old;

	ops->ani_proc_mib_event = ath9k_hw_proc_mib_event_old;
	ops->ani_monitor = ath9k_hw_ani_monitor_old;

	ath_print(ath9k_hw_common(ah), ATH_DBG_ANY, "Using ANI v1\n");
}

void ath9k_hw_attach_ani_ops_new(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);

	priv_ops->ani_reset = ath9k_ani_reset_new;
	priv_ops->ani_lower_immunity = ath9k_hw_ani_lower_immunity_new;

	ops->ani_proc_mib_event = ath9k_hw_proc_mib_event_new;
	ops->ani_monitor = ath9k_hw_ani_monitor_new;

	ath_print(ath9k_hw_common(ah), ATH_DBG_ANY, "Using ANI v2\n");
}
