/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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

	if (!ah->curchan)
		return;

	aniState = &ah->ani;
	aniState->listenTime = 0;

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_PHY_ERR_1, 0);
	REG_WRITE(ah, AR_PHY_ERR_2, 0);
	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	REGWRITE_BUFFER_FLUSH(ah);

	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);

	aniState->ofdmPhyErrCount = 0;
	aniState->cckPhyErrCount = 0;
}

/* Adjust the OFDM Noise Immunity Level */
static void ath9k_hw_set_ofdm_nil(struct ath_hw *ah, u8 immunityLevel,
				  bool scan)
{
	struct ar5416AniState *aniState = &ah->ani;
	struct ath_common *common = ath9k_hw_common(ah);
	const struct ani_ofdm_level_entry *entry_ofdm;
	const struct ani_cck_level_entry *entry_cck;
	bool weak_sig;

	ath_dbg(common, ANI, "**** ofdmlevel %d=>%d, rssi=%d[lo=%d hi=%d]\n",
		aniState->ofdmNoiseImmunityLevel,
		immunityLevel, BEACON_RSSI(ah),
		ATH9K_ANI_RSSI_THR_LOW,
		ATH9K_ANI_RSSI_THR_HIGH);

	if (AR_SREV_9100(ah) && immunityLevel < ATH9K_ANI_OFDM_DEF_LEVEL)
		immunityLevel = ATH9K_ANI_OFDM_DEF_LEVEL;

	if (!scan)
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

	weak_sig = entry_ofdm->ofdm_weak_signal_on;
	if (ah->opmode == NL80211_IFTYPE_STATION &&
	    BEACON_RSSI(ah) <= ATH9K_ANI_RSSI_THR_HIGH)
		weak_sig = true;
	/*
	 * Newer chipsets are better at dealing with high PHY error counts -
	 * keep weak signal detection enabled when no RSSI threshold is
	 * available to determine if it is needed (mode != STA)
	 */
	else if (AR_SREV_9300_20_OR_LATER(ah) &&
		 ah->opmode != NL80211_IFTYPE_STATION)
		weak_sig = true;

	/* Older chipsets are more sensitive to high PHY error counts */
	else if (!AR_SREV_9300_20_OR_LATER(ah) &&
		 aniState->ofdmNoiseImmunityLevel >= 8)
		weak_sig = false;

	if (aniState->ofdmWeakSigDetect != weak_sig)
		ath9k_hw_ani_control(ah, ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				     weak_sig);

	if (!AR_SREV_9300_20_OR_LATER(ah))
		return;

	if (aniState->ofdmNoiseImmunityLevel >= ATH9K_ANI_OFDM_DEF_LEVEL) {
		ah->config.ofdm_trig_high = ATH9K_ANI_OFDM_TRIG_HIGH;
		ah->config.ofdm_trig_low = ATH9K_ANI_OFDM_TRIG_LOW_ABOVE_INI;
	} else {
		ah->config.ofdm_trig_high = ATH9K_ANI_OFDM_TRIG_HIGH_BELOW_INI;
		ah->config.ofdm_trig_low = ATH9K_ANI_OFDM_TRIG_LOW;
	}
}

static void ath9k_hw_ani_ofdm_err_trigger(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;

	if (!ah->curchan)
		return;

	aniState = &ah->ani;

	if (aniState->ofdmNoiseImmunityLevel < ATH9K_ANI_OFDM_MAX_LEVEL)
		ath9k_hw_set_ofdm_nil(ah, aniState->ofdmNoiseImmunityLevel + 1, false);
}

/*
 * Set the ANI settings to match an CCK level.
 */
static void ath9k_hw_set_cck_nil(struct ath_hw *ah, u_int8_t immunityLevel,
				 bool scan)
{
	struct ar5416AniState *aniState = &ah->ani;
	struct ath_common *common = ath9k_hw_common(ah);
	const struct ani_ofdm_level_entry *entry_ofdm;
	const struct ani_cck_level_entry *entry_cck;

	ath_dbg(common, ANI, "**** ccklevel %d=>%d, rssi=%d[lo=%d hi=%d]\n",
		aniState->cckNoiseImmunityLevel, immunityLevel,
		BEACON_RSSI(ah), ATH9K_ANI_RSSI_THR_LOW,
		ATH9K_ANI_RSSI_THR_HIGH);

	if (AR_SREV_9100(ah) && immunityLevel < ATH9K_ANI_CCK_DEF_LEVEL)
		immunityLevel = ATH9K_ANI_CCK_DEF_LEVEL;

	if (ah->opmode == NL80211_IFTYPE_STATION &&
	    BEACON_RSSI(ah) <= ATH9K_ANI_RSSI_THR_LOW &&
	    immunityLevel > ATH9K_ANI_CCK_MAX_LEVEL_LOW_RSSI)
		immunityLevel = ATH9K_ANI_CCK_MAX_LEVEL_LOW_RSSI;

	if (!scan)
		aniState->cckNoiseImmunityLevel = immunityLevel;

	entry_ofdm = &ofdm_level_table[aniState->ofdmNoiseImmunityLevel];
	entry_cck = &cck_level_table[aniState->cckNoiseImmunityLevel];

	if (aniState->firstepLevel != entry_cck->fir_step_level &&
	    entry_cck->fir_step_level >= entry_ofdm->fir_step_level)
		ath9k_hw_ani_control(ah,
				     ATH9K_ANI_FIRSTEP_LEVEL,
				     entry_cck->fir_step_level);

	/* Skip MRC CCK for pre AR9003 families */
	if (!AR_SREV_9300_20_OR_LATER(ah) || AR_SREV_9485(ah) || AR_SREV_9565(ah))
		return;

	if (aniState->mrcCCK != entry_cck->mrc_cck_on)
		ath9k_hw_ani_control(ah,
				     ATH9K_ANI_MRC_CCK,
				     entry_cck->mrc_cck_on);
}

static void ath9k_hw_ani_cck_err_trigger(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;

	if (!ah->curchan)
		return;

	aniState = &ah->ani;

	if (aniState->cckNoiseImmunityLevel < ATH9K_ANI_CCK_MAX_LEVEL)
		ath9k_hw_set_cck_nil(ah, aniState->cckNoiseImmunityLevel + 1,
				     false);
}

/*
 * only lower either OFDM or CCK errors per turn
 * we lower the other one next time
 */
static void ath9k_hw_ani_lower_immunity(struct ath_hw *ah)
{
	struct ar5416AniState *aniState;

	aniState = &ah->ani;

	/* lower OFDM noise immunity */
	if (aniState->ofdmNoiseImmunityLevel > 0 &&
	    (aniState->ofdmsTurn || aniState->cckNoiseImmunityLevel == 0)) {
		ath9k_hw_set_ofdm_nil(ah, aniState->ofdmNoiseImmunityLevel - 1,
				      false);
		return;
	}

	/* lower CCK noise immunity */
	if (aniState->cckNoiseImmunityLevel > 0)
		ath9k_hw_set_cck_nil(ah, aniState->cckNoiseImmunityLevel - 1,
				     false);
}

/*
 * Restore the ANI parameters in the HAL and reset the statistics.
 * This routine should be called for every hardware reset and for
 * every channel change.
 */
void ath9k_ani_reset(struct ath_hw *ah, bool is_scanning)
{
	struct ar5416AniState *aniState = &ah->ani;
	struct ath9k_channel *chan = ah->curchan;
	struct ath_common *common = ath9k_hw_common(ah);
	int ofdm_nil, cck_nil;

	if (!ah->curchan)
		return;

	BUG_ON(aniState == NULL);
	ah->stats.ast_ani_reset++;

	ofdm_nil = max_t(int, ATH9K_ANI_OFDM_DEF_LEVEL,
			 aniState->ofdmNoiseImmunityLevel);
	cck_nil = max_t(int, ATH9K_ANI_CCK_DEF_LEVEL,
			 aniState->cckNoiseImmunityLevel);

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
			ath_dbg(common, ANI,
				"Restore defaults: opmode %u chan %d Mhz is_scanning=%d ofdm:%d cck:%d\n",
				ah->opmode,
				chan->channel,
				is_scanning,
				aniState->ofdmNoiseImmunityLevel,
				aniState->cckNoiseImmunityLevel);

			ofdm_nil = ATH9K_ANI_OFDM_DEF_LEVEL;
			cck_nil = ATH9K_ANI_CCK_DEF_LEVEL;
		}
	} else {
		/*
		 * restore historical levels for this channel
		 */
		ath_dbg(common, ANI,
			"Restore history: opmode %u chan %d Mhz is_scanning=%d ofdm:%d cck:%d\n",
			ah->opmode,
			chan->channel,
			is_scanning,
			aniState->ofdmNoiseImmunityLevel,
			aniState->cckNoiseImmunityLevel);
	}
	ath9k_hw_set_ofdm_nil(ah, ofdm_nil, is_scanning);
	ath9k_hw_set_cck_nil(ah, cck_nil, is_scanning);

	ath9k_ani_restart(ah);
}

static bool ath9k_hw_ani_read_counters(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ar5416AniState *aniState = &ah->ani;
	u32 phyCnt1, phyCnt2;
	int32_t listenTime;

	ath_hw_cycle_counters_update(common);
	listenTime = ath_hw_get_listen_time(common);

	if (listenTime <= 0) {
		ah->stats.ast_ani_lneg_or_lzero++;
		ath9k_ani_restart(ah);
		return false;
	}

	aniState->listenTime += listenTime;

	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);

	phyCnt1 = REG_READ(ah, AR_PHY_ERR_1);
	phyCnt2 = REG_READ(ah, AR_PHY_ERR_2);

	ah->stats.ast_ani_ofdmerrs += phyCnt1 - aniState->ofdmPhyErrCount;
	aniState->ofdmPhyErrCount = phyCnt1;

	ah->stats.ast_ani_cckerrs += phyCnt2 - aniState->cckPhyErrCount;
	aniState->cckPhyErrCount = phyCnt2;

	return true;
}

void ath9k_hw_ani_monitor(struct ath_hw *ah, struct ath9k_channel *chan)
{
	struct ar5416AniState *aniState;
	struct ath_common *common = ath9k_hw_common(ah);
	u32 ofdmPhyErrRate, cckPhyErrRate;

	if (!ah->curchan)
		return;

	aniState = &ah->ani;
	if (!ath9k_hw_ani_read_counters(ah))
		return;

	ofdmPhyErrRate = aniState->ofdmPhyErrCount * 1000 /
			 aniState->listenTime;
	cckPhyErrRate =  aniState->cckPhyErrCount * 1000 /
			 aniState->listenTime;

	ath_dbg(common, ANI,
		"listenTime=%d OFDM:%d errs=%d/s CCK:%d errs=%d/s ofdm_turn=%d\n",
		aniState->listenTime,
		aniState->ofdmNoiseImmunityLevel,
		ofdmPhyErrRate, aniState->cckNoiseImmunityLevel,
		cckPhyErrRate, aniState->ofdmsTurn);

	if (aniState->listenTime > ah->aniperiod) {
		if (cckPhyErrRate < ah->config.cck_trig_low &&
		    ofdmPhyErrRate < ah->config.ofdm_trig_low) {
			ath9k_hw_ani_lower_immunity(ah);
			aniState->ofdmsTurn = !aniState->ofdmsTurn;
		} else if (ofdmPhyErrRate > ah->config.ofdm_trig_high) {
			ath9k_hw_ani_ofdm_err_trigger(ah);
			aniState->ofdmsTurn = false;
		} else if (cckPhyErrRate > ah->config.cck_trig_high) {
			ath9k_hw_ani_cck_err_trigger(ah);
			aniState->ofdmsTurn = true;
		}
		ath9k_ani_restart(ah);
	}
}
EXPORT_SYMBOL(ath9k_hw_ani_monitor);

void ath9k_enable_mib_counters(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	ath_dbg(common, ANI, "Enable MIB counters\n");

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

	ath_dbg(common, ANI, "Disable MIB counters\n");

	REG_WRITE(ah, AR_MIBC, AR_MIBC_FMC);
	ath9k_hw_update_mibstats(ah, &ah->ah_mibStats);
	REG_WRITE(ah, AR_MIBC, AR_MIBC_CMC);
	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
}
EXPORT_SYMBOL(ath9k_hw_disable_mib_counters);

void ath9k_hw_ani_init(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ar5416AniState *ani = &ah->ani;

	ath_dbg(common, ANI, "Initialize ANI\n");

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		ah->config.ofdm_trig_high = ATH9K_ANI_OFDM_TRIG_HIGH;
		ah->config.ofdm_trig_low = ATH9K_ANI_OFDM_TRIG_LOW;
		ah->config.cck_trig_high = ATH9K_ANI_CCK_TRIG_HIGH;
		ah->config.cck_trig_low = ATH9K_ANI_CCK_TRIG_LOW;
	} else {
		ah->config.ofdm_trig_high = ATH9K_ANI_OFDM_TRIG_HIGH_OLD;
		ah->config.ofdm_trig_low = ATH9K_ANI_OFDM_TRIG_LOW_OLD;
		ah->config.cck_trig_high = ATH9K_ANI_CCK_TRIG_HIGH_OLD;
		ah->config.cck_trig_low = ATH9K_ANI_CCK_TRIG_LOW_OLD;
	}

	ani->spurImmunityLevel = ATH9K_ANI_SPUR_IMMUNE_LVL;
	ani->firstepLevel = ATH9K_ANI_FIRSTEP_LVL;
	ani->mrcCCK = AR_SREV_9300_20_OR_LATER(ah) ? true : false;
	ani->ofdmsTurn = true;
	ani->ofdmWeakSigDetect = true;
	ani->cckNoiseImmunityLevel = ATH9K_ANI_CCK_DEF_LEVEL;
	ani->ofdmNoiseImmunityLevel = ATH9K_ANI_OFDM_DEF_LEVEL;

	/*
	 * since we expect some ongoing maintenance on the tables, let's sanity
	 * check here default level should not modify INI setting.
	 */
	ah->aniperiod = ATH9K_ANI_PERIOD;
	ah->config.ani_poll_interval = ATH9K_ANI_POLLINTERVAL;

	ath9k_ani_restart(ah);
	ath9k_enable_mib_counters(ah);
}
