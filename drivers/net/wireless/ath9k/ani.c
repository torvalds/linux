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

#include "core.h"
#include "hw.h"
#include "reg.h"
#include "phy.h"

static int ath9k_hw_get_ani_channel_idx(struct ath_hal *ah,
					struct ath9k_channel *chan)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	for (i = 0; i < ARRAY_SIZE(ahp->ah_ani); i++) {
		if (ahp->ah_ani[i].c.channel == chan->channel)
			return i;
		if (ahp->ah_ani[i].c.channel == 0) {
			ahp->ah_ani[i].c.channel = chan->channel;
			ahp->ah_ani[i].c.channelFlags = chan->channelFlags;
			return i;
		}
	}

	DPRINTF(ah->ah_sc, ATH_DBG_ANI,
		"No more channel states left. Using channel 0\n");

	return 0;
}

static bool ath9k_hw_ani_control(struct ath_hal *ah,
				 enum ath9k_ani_cmd cmd, int param)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState = ahp->ah_curani;

	switch (cmd & ahp->ah_ani_function) {
	case ATH9K_ANI_NOISE_IMMUNITY_LEVEL:{
		u32 level = param;

		if (level >= ARRAY_SIZE(ahp->ah_totalSizeDesired)) {
			DPRINTF(ah->ah_sc, ATH_DBG_ANI,
				"level out of range (%u > %u)\n",
				level,
				(unsigned)ARRAY_SIZE(ahp->ah_totalSizeDesired));
			return false;
		}

		REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ,
			      AR_PHY_DESIRED_SZ_TOT_DES,
			      ahp->ah_totalSizeDesired[level]);
		REG_RMW_FIELD(ah, AR_PHY_AGC_CTL1,
			      AR_PHY_AGC_CTL1_COARSE_LOW,
			      ahp->ah_coarseLow[level]);
		REG_RMW_FIELD(ah, AR_PHY_AGC_CTL1,
			      AR_PHY_AGC_CTL1_COARSE_HIGH,
			      ahp->ah_coarseHigh[level]);
		REG_RMW_FIELD(ah, AR_PHY_FIND_SIG,
			      AR_PHY_FIND_SIG_FIRPWR,
			      ahp->ah_firpwr[level]);

		if (level > aniState->noiseImmunityLevel)
			ahp->ah_stats.ast_ani_niup++;
		else if (level < aniState->noiseImmunityLevel)
			ahp->ah_stats.ast_ani_nidown++;
		aniState->noiseImmunityLevel = level;
		break;
	}
	case ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION:{
		const int m1ThreshLow[] = { 127, 50 };
		const int m2ThreshLow[] = { 127, 40 };
		const int m1Thresh[] = { 127, 0x4d };
		const int m2Thresh[] = { 127, 0x40 };
		const int m2CountThr[] = { 31, 16 };
		const int m2CountThrLow[] = { 63, 48 };
		u32 on = param ? 1 : 0;

		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M1_THRESH_LOW,
			      m1ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M2_THRESH_LOW,
			      m2ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M1_THRESH,
			      m1Thresh[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M2_THRESH,
			      m2Thresh[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M2COUNT_THR,
			      m2CountThr[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW,
			      m2CountThrLow[on]);

		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M1_THRESH_LOW,
			      m1ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M2_THRESH_LOW,
			      m2ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M1_THRESH,
			      m1Thresh[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M2_THRESH,
			      m2Thresh[on]);

		if (on)
			REG_SET_BIT(ah, AR_PHY_SFCORR_LOW,
				    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
		else
			REG_CLR_BIT(ah, AR_PHY_SFCORR_LOW,
				    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);

		if (!on != aniState->ofdmWeakSigDetectOff) {
			if (on)
				ahp->ah_stats.ast_ani_ofdmon++;
			else
				ahp->ah_stats.ast_ani_ofdmoff++;
			aniState->ofdmWeakSigDetectOff = !on;
		}
		break;
	}
	case ATH9K_ANI_CCK_WEAK_SIGNAL_THR:{
		const int weakSigThrCck[] = { 8, 6 };
		u32 high = param ? 1 : 0;

		REG_RMW_FIELD(ah, AR_PHY_CCK_DETECT,
			      AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK,
			      weakSigThrCck[high]);
		if (high != aniState->cckWeakSigThreshold) {
			if (high)
				ahp->ah_stats.ast_ani_cckhigh++;
			else
				ahp->ah_stats.ast_ani_ccklow++;
			aniState->cckWeakSigThreshold = high;
		}
		break;
	}
	case ATH9K_ANI_FIRSTEP_LEVEL:{
		const int firstep[] = { 0, 4, 8 };
		u32 level = param;

		if (level >= ARRAY_SIZE(firstep)) {
			DPRINTF(ah->ah_sc, ATH_DBG_ANI,
				"level out of range (%u > %u)\n",
				level,
				(unsigned) ARRAY_SIZE(firstep));
			return false;
		}
		REG_RMW_FIELD(ah, AR_PHY_FIND_SIG,
			      AR_PHY_FIND_SIG_FIRSTEP,
			      firstep[level]);
		if (level > aniState->firstepLevel)
			ahp->ah_stats.ast_ani_stepup++;
		else if (level < aniState->firstepLevel)
			ahp->ah_stats.ast_ani_stepdown++;
		aniState->firstepLevel = level;
		break;
	}
	case ATH9K_ANI_SPUR_IMMUNITY_LEVEL:{
		const int cycpwrThr1[] =
			{ 2, 4, 6, 8, 10, 12, 14, 16 };
		u32 level = param;

		if (level >= ARRAY_SIZE(cycpwrThr1)) {
			DPRINTF(ah->ah_sc, ATH_DBG_ANI,
				"level out of range (%u > %u)\n",
				level,
				(unsigned)
				ARRAY_SIZE(cycpwrThr1));
			return false;
		}
		REG_RMW_FIELD(ah, AR_PHY_TIMING5,
			      AR_PHY_TIMING5_CYCPWR_THR1,
			      cycpwrThr1[level]);
		if (level > aniState->spurImmunityLevel)
			ahp->ah_stats.ast_ani_spurup++;
		else if (level < aniState->spurImmunityLevel)
			ahp->ah_stats.ast_ani_spurdown++;
		aniState->spurImmunityLevel = level;
		break;
	}
	case ATH9K_ANI_PRESENT:
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_ANI,
			"invalid cmd %u\n", cmd);
		return false;
	}

	DPRINTF(ah->ah_sc, ATH_DBG_ANI, "ANI parameters:\n");
	DPRINTF(ah->ah_sc, ATH_DBG_ANI,
		"noiseImmunityLevel=%d, spurImmunityLevel=%d, "
		"ofdmWeakSigDetectOff=%d\n",
		aniState->noiseImmunityLevel, aniState->spurImmunityLevel,
		!aniState->ofdmWeakSigDetectOff);
	DPRINTF(ah->ah_sc, ATH_DBG_ANI,
		"cckWeakSigThreshold=%d, "
		"firstepLevel=%d, listenTime=%d\n",
		aniState->cckWeakSigThreshold, aniState->firstepLevel,
		aniState->listenTime);
	DPRINTF(ah->ah_sc, ATH_DBG_ANI,
		"cycleCount=%d, ofdmPhyErrCount=%d, cckPhyErrCount=%d\n\n",
		aniState->cycleCount, aniState->ofdmPhyErrCount,
		aniState->cckPhyErrCount);

	return true;
}

static void ath9k_hw_update_mibstats(struct ath_hal *ah,
				     struct ath9k_mib_stats *stats)
{
	stats->ackrcv_bad += REG_READ(ah, AR_ACK_FAIL);
	stats->rts_bad += REG_READ(ah, AR_RTS_FAIL);
	stats->fcs_bad += REG_READ(ah, AR_FCS_FAIL);
	stats->rts_good += REG_READ(ah, AR_RTS_OK);
	stats->beacons += REG_READ(ah, AR_BEACON_CNT);
}

static void ath9k_ani_restart(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState;

	if (!DO_ANI(ah))
		return;

	aniState = ahp->ah_curani;

	aniState->listenTime = 0;
	if (ahp->ah_hasHwPhyCounters) {
		if (aniState->ofdmTrigHigh > AR_PHY_COUNTMAX) {
			aniState->ofdmPhyErrBase = 0;
			DPRINTF(ah->ah_sc, ATH_DBG_ANI,
				"OFDM Trigger is too high for hw counters\n");
		} else {
			aniState->ofdmPhyErrBase =
				AR_PHY_COUNTMAX - aniState->ofdmTrigHigh;
		}
		if (aniState->cckTrigHigh > AR_PHY_COUNTMAX) {
			aniState->cckPhyErrBase = 0;
			DPRINTF(ah->ah_sc, ATH_DBG_ANI,
				"CCK Trigger is too high for hw counters\n");
		} else {
			aniState->cckPhyErrBase =
				AR_PHY_COUNTMAX - aniState->cckTrigHigh;
		}
		DPRINTF(ah->ah_sc, ATH_DBG_ANI,
			"Writing ofdmbase=%u   cckbase=%u\n",
			aniState->ofdmPhyErrBase,
			aniState->cckPhyErrBase);
		REG_WRITE(ah, AR_PHY_ERR_1, aniState->ofdmPhyErrBase);
		REG_WRITE(ah, AR_PHY_ERR_2, aniState->cckPhyErrBase);
		REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
		REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

		ath9k_hw_update_mibstats(ah, &ahp->ah_mibStats);
	}
	aniState->ofdmPhyErrCount = 0;
	aniState->cckPhyErrCount = 0;
}

static void ath9k_hw_ani_ofdm_err_trigger(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *chan = ah->ah_curchan;
	struct ar5416AniState *aniState;
	enum wireless_mode mode;
	int32_t rssi;

	if (!DO_ANI(ah))
		return;

	aniState = ahp->ah_curani;

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

	if (ah->ah_opmode == NL80211_IFTYPE_AP) {
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX) {
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
		}
		return;
	}
	rssi = BEACON_RSSI(ahp);
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
		mode = ath9k_hw_chan2wmode(ah, chan);
		if (mode == ATH9K_MODE_11G || mode == ATH9K_MODE_11B) {
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

static void ath9k_hw_ani_cck_err_trigger(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *chan = ah->ah_curchan;
	struct ar5416AniState *aniState;
	enum wireless_mode mode;
	int32_t rssi;

	if (!DO_ANI(ah))
		return;

	aniState = ahp->ah_curani;
	if (aniState->noiseImmunityLevel < HAL_NOISE_IMMUNE_MAX) {
		if (ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL,
					 aniState->noiseImmunityLevel + 1)) {
			return;
		}
	}
	if (ah->ah_opmode == NL80211_IFTYPE_AP) {
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX) {
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
		}
		return;
	}
	rssi = BEACON_RSSI(ahp);
	if (rssi > aniState->rssiThrLow) {
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX)
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
	} else {
		mode = ath9k_hw_chan2wmode(ah, chan);
		if (mode == ATH9K_MODE_11G || mode == ATH9K_MODE_11B) {
			if (aniState->firstepLevel > 0)
				ath9k_hw_ani_control(ah,
					     ATH9K_ANI_FIRSTEP_LEVEL, 0);
		}
	}
}

static void ath9k_hw_ani_lower_immunity(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState;
	int32_t rssi;

	aniState = ahp->ah_curani;

	if (ah->ah_opmode == NL80211_IFTYPE_AP) {
		if (aniState->firstepLevel > 0) {
			if (ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
						 aniState->firstepLevel - 1))
				return;
		}
	} else {
		rssi = BEACON_RSSI(ahp);
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

static int32_t ath9k_hw_ani_get_listen_time(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState;
	u32 txFrameCount, rxFrameCount, cycleCount;
	int32_t listenTime;

	txFrameCount = REG_READ(ah, AR_TFCNT);
	rxFrameCount = REG_READ(ah, AR_RFCNT);
	cycleCount = REG_READ(ah, AR_CCCNT);

	aniState = ahp->ah_curani;
	if (aniState->cycleCount == 0 || aniState->cycleCount > cycleCount) {

		listenTime = 0;
		ahp->ah_stats.ast_ani_lzero++;
	} else {
		int32_t ccdelta = cycleCount - aniState->cycleCount;
		int32_t rfdelta = rxFrameCount - aniState->rxFrameCount;
		int32_t tfdelta = txFrameCount - aniState->txFrameCount;
		listenTime = (ccdelta - rfdelta - tfdelta) / 44000;
	}
	aniState->cycleCount = cycleCount;
	aniState->txFrameCount = txFrameCount;
	aniState->rxFrameCount = rxFrameCount;

	return listenTime;
}

void ath9k_ani_reset(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState;
	struct ath9k_channel *chan = ah->ah_curchan;
	int index;

	if (!DO_ANI(ah))
		return;

	index = ath9k_hw_get_ani_channel_idx(ah, chan);
	aniState = &ahp->ah_ani[index];
	ahp->ah_curani = aniState;

	if (DO_ANI(ah) && ah->ah_opmode != NL80211_IFTYPE_STATION
	    && ah->ah_opmode != NL80211_IFTYPE_ADHOC) {
		DPRINTF(ah->ah_sc, ATH_DBG_ANI,
			"Reset ANI state opmode %u\n", ah->ah_opmode);
		ahp->ah_stats.ast_ani_reset++;

		ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL, 0);
		ath9k_hw_ani_control(ah, ATH9K_ANI_SPUR_IMMUNITY_LEVEL, 0);
		ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL, 0);
		ath9k_hw_ani_control(ah, ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				     !ATH9K_ANI_USE_OFDM_WEAK_SIG);
		ath9k_hw_ani_control(ah, ATH9K_ANI_CCK_WEAK_SIGNAL_THR,
				     ATH9K_ANI_CCK_WEAK_SIG_THR);

		ath9k_hw_setrxfilter(ah, ath9k_hw_getrxfilter(ah) |
				     ATH9K_RX_FILTER_PHYERR);

		if (ah->ah_opmode == NL80211_IFTYPE_AP) {
			ahp->ah_curani->ofdmTrigHigh =
				ah->ah_config.ofdm_trig_high;
			ahp->ah_curani->ofdmTrigLow =
				ah->ah_config.ofdm_trig_low;
			ahp->ah_curani->cckTrigHigh =
				ah->ah_config.cck_trig_high;
			ahp->ah_curani->cckTrigLow =
				ah->ah_config.cck_trig_low;
		}
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
	if (ahp->ah_hasHwPhyCounters) {
		ath9k_hw_setrxfilter(ah, ath9k_hw_getrxfilter(ah) &
				     ~ATH9K_RX_FILTER_PHYERR);
		ath9k_ani_restart(ah);
		REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
		REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	} else {
		ath9k_ani_restart(ah);
		ath9k_hw_setrxfilter(ah, ath9k_hw_getrxfilter(ah) |
				     ATH9K_RX_FILTER_PHYERR);
	}
}

void ath9k_hw_ani_monitor(struct ath_hal *ah,
			  const struct ath9k_node_stats *stats,
			  struct ath9k_channel *chan)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState;
	int32_t listenTime;

	aniState = ahp->ah_curani;
	ahp->ah_stats.ast_nodestats = *stats;

	listenTime = ath9k_hw_ani_get_listen_time(ah);
	if (listenTime < 0) {
		ahp->ah_stats.ast_ani_lneg++;
		ath9k_ani_restart(ah);
		return;
	}

	aniState->listenTime += listenTime;

	if (ahp->ah_hasHwPhyCounters) {
		u32 phyCnt1, phyCnt2;
		u32 ofdmPhyErrCnt, cckPhyErrCnt;

		ath9k_hw_update_mibstats(ah, &ahp->ah_mibStats);

		phyCnt1 = REG_READ(ah, AR_PHY_ERR_1);
		phyCnt2 = REG_READ(ah, AR_PHY_ERR_2);

		if (phyCnt1 < aniState->ofdmPhyErrBase ||
		    phyCnt2 < aniState->cckPhyErrBase) {
			if (phyCnt1 < aniState->ofdmPhyErrBase) {
				DPRINTF(ah->ah_sc, ATH_DBG_ANI,
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
				DPRINTF(ah->ah_sc, ATH_DBG_ANI,
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
		ahp->ah_stats.ast_ani_ofdmerrs +=
			ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
		aniState->ofdmPhyErrCount = ofdmPhyErrCnt;

		cckPhyErrCnt = phyCnt2 - aniState->cckPhyErrBase;
		ahp->ah_stats.ast_ani_cckerrs +=
			cckPhyErrCnt - aniState->cckPhyErrCount;
		aniState->cckPhyErrCount = cckPhyErrCnt;
	}

	if (!DO_ANI(ah))
		return;

	if (aniState->listenTime > 5 * ahp->ah_aniPeriod) {
		if (aniState->ofdmPhyErrCount <= aniState->listenTime *
		    aniState->ofdmTrigLow / 1000 &&
		    aniState->cckPhyErrCount <= aniState->listenTime *
		    aniState->cckTrigLow / 1000)
			ath9k_hw_ani_lower_immunity(ah);
		ath9k_ani_restart(ah);
	} else if (aniState->listenTime > ahp->ah_aniPeriod) {
		if (aniState->ofdmPhyErrCount > aniState->listenTime *
		    aniState->ofdmTrigHigh / 1000) {
			ath9k_hw_ani_ofdm_err_trigger(ah);
			ath9k_ani_restart(ah);
		} else if (aniState->cckPhyErrCount >
			   aniState->listenTime * aniState->cckTrigHigh /
			   1000) {
			ath9k_hw_ani_cck_err_trigger(ah);
			ath9k_ani_restart(ah);
		}
	}
}

bool ath9k_hw_phycounters(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	return ahp->ah_hasHwPhyCounters ? true : false;
}

void ath9k_enable_mib_counters(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	DPRINTF(ah->ah_sc, ATH_DBG_ANI, "Enable MIB counters\n");

	ath9k_hw_update_mibstats(ah, &ahp->ah_mibStats);

	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
	REG_WRITE(ah, AR_MIBC,
		  ~(AR_MIBC_COW | AR_MIBC_FMC | AR_MIBC_CMC | AR_MIBC_MCS)
		  & 0x0f);
	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);
}

void ath9k_hw_disable_mib_counters(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	DPRINTF(ah->ah_sc, ATH_DBG_ANI, "Disable MIB counters\n");

	REG_WRITE(ah, AR_MIBC, AR_MIBC_FMC | AR_MIBC_CMC);

	ath9k_hw_update_mibstats(ah, &ahp->ah_mibStats);

	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
}

u32 ath9k_hw_GetMibCycleCountsPct(struct ath_hal *ah,
				  u32 *rxc_pcnt,
				  u32 *rxf_pcnt,
				  u32 *txf_pcnt)
{
	static u32 cycles, rx_clear, rx_frame, tx_frame;
	u32 good = 1;

	u32 rc = REG_READ(ah, AR_RCCNT);
	u32 rf = REG_READ(ah, AR_RFCNT);
	u32 tf = REG_READ(ah, AR_TFCNT);
	u32 cc = REG_READ(ah, AR_CCCNT);

	if (cycles == 0 || cycles > cc) {
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
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
void ath9k_hw_procmibevent(struct ath_hal *ah,
			   const struct ath9k_node_stats *stats)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 phyCnt1, phyCnt2;

	/* Reset these counters regardless */
	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
	if (!(REG_READ(ah, AR_SLP_MIB_CTRL) & AR_SLP_MIB_PENDING))
		REG_WRITE(ah, AR_SLP_MIB_CTRL, AR_SLP_MIB_CLEAR);

	/* Clear the mib counters and save them in the stats */
	ath9k_hw_update_mibstats(ah, &ahp->ah_mibStats);
	ahp->ah_stats.ast_nodestats = *stats;

	if (!DO_ANI(ah))
		return;

	/* NB: these are not reset-on-read */
	phyCnt1 = REG_READ(ah, AR_PHY_ERR_1);
	phyCnt2 = REG_READ(ah, AR_PHY_ERR_2);
	if (((phyCnt1 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK) ||
	    ((phyCnt2 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK)) {
		struct ar5416AniState *aniState = ahp->ah_curani;
		u32 ofdmPhyErrCnt, cckPhyErrCnt;

		/* NB: only use ast_ani_*errs with AH_PRIVATE_DIAG */
		ofdmPhyErrCnt = phyCnt1 - aniState->ofdmPhyErrBase;
		ahp->ah_stats.ast_ani_ofdmerrs +=
			ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
		aniState->ofdmPhyErrCount = ofdmPhyErrCnt;

		cckPhyErrCnt = phyCnt2 - aniState->cckPhyErrBase;
		ahp->ah_stats.ast_ani_cckerrs +=
			cckPhyErrCnt - aniState->cckPhyErrCount;
		aniState->cckPhyErrCount = cckPhyErrCnt;

		/*
		 * NB: figure out which counter triggered.  If both
		 * trigger we'll only deal with one as the processing
		 * clobbers the error counter so the trigger threshold
		 * check will never be true.
		 */
		if (aniState->ofdmPhyErrCount > aniState->ofdmTrigHigh)
			ath9k_hw_ani_ofdm_err_trigger(ah);
		if (aniState->cckPhyErrCount > aniState->cckTrigHigh)
			ath9k_hw_ani_cck_err_trigger(ah);
		/* NB: always restart to insure the h/w counters are reset */
		ath9k_ani_restart(ah);
	}
}

void ath9k_hw_ani_setup(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	const int totalSizeDesired[] = { -55, -55, -55, -55, -62 };
	const int coarseHigh[] = { -14, -14, -14, -14, -12 };
	const int coarseLow[] = { -64, -64, -64, -64, -70 };
	const int firpwr[] = { -78, -78, -78, -78, -80 };

	for (i = 0; i < 5; i++) {
		ahp->ah_totalSizeDesired[i] = totalSizeDesired[i];
		ahp->ah_coarseHigh[i] = coarseHigh[i];
		ahp->ah_coarseLow[i] = coarseLow[i];
		ahp->ah_firpwr[i] = firpwr[i];
	}
}

void ath9k_hw_ani_attach(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	DPRINTF(ah->ah_sc, ATH_DBG_ANI, "Attach ANI\n");

	ahp->ah_hasHwPhyCounters = 1;

	memset(ahp->ah_ani, 0, sizeof(ahp->ah_ani));
	for (i = 0; i < ARRAY_SIZE(ahp->ah_ani); i++) {
		ahp->ah_ani[i].ofdmTrigHigh = ATH9K_ANI_OFDM_TRIG_HIGH;
		ahp->ah_ani[i].ofdmTrigLow = ATH9K_ANI_OFDM_TRIG_LOW;
		ahp->ah_ani[i].cckTrigHigh = ATH9K_ANI_CCK_TRIG_HIGH;
		ahp->ah_ani[i].cckTrigLow = ATH9K_ANI_CCK_TRIG_LOW;
		ahp->ah_ani[i].rssiThrHigh = ATH9K_ANI_RSSI_THR_HIGH;
		ahp->ah_ani[i].rssiThrLow = ATH9K_ANI_RSSI_THR_LOW;
		ahp->ah_ani[i].ofdmWeakSigDetectOff =
			!ATH9K_ANI_USE_OFDM_WEAK_SIG;
		ahp->ah_ani[i].cckWeakSigThreshold =
			ATH9K_ANI_CCK_WEAK_SIG_THR;
		ahp->ah_ani[i].spurImmunityLevel = ATH9K_ANI_SPUR_IMMUNE_LVL;
		ahp->ah_ani[i].firstepLevel = ATH9K_ANI_FIRSTEP_LVL;
		if (ahp->ah_hasHwPhyCounters) {
			ahp->ah_ani[i].ofdmPhyErrBase =
				AR_PHY_COUNTMAX - ATH9K_ANI_OFDM_TRIG_HIGH;
			ahp->ah_ani[i].cckPhyErrBase =
				AR_PHY_COUNTMAX - ATH9K_ANI_CCK_TRIG_HIGH;
		}
	}
	if (ahp->ah_hasHwPhyCounters) {
		DPRINTF(ah->ah_sc, ATH_DBG_ANI,
			"Setting OfdmErrBase = 0x%08x\n",
			ahp->ah_ani[0].ofdmPhyErrBase);
		DPRINTF(ah->ah_sc, ATH_DBG_ANI, "Setting cckErrBase = 0x%08x\n",
			ahp->ah_ani[0].cckPhyErrBase);

		REG_WRITE(ah, AR_PHY_ERR_1, ahp->ah_ani[0].ofdmPhyErrBase);
		REG_WRITE(ah, AR_PHY_ERR_2, ahp->ah_ani[0].cckPhyErrBase);
		ath9k_enable_mib_counters(ah);
	}
	ahp->ah_aniPeriod = ATH9K_ANI_PERIOD;
	if (ah->ah_config.enable_ani)
		ahp->ah_procPhyErr |= HAL_PROCESS_ANI;
}

void ath9k_hw_ani_detach(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	DPRINTF(ah->ah_sc, ATH_DBG_ANI, "Detach ANI\n");

	if (ahp->ah_hasHwPhyCounters) {
		ath9k_hw_disable_mib_counters(ah);
		REG_WRITE(ah, AR_PHY_ERR_1, 0);
		REG_WRITE(ah, AR_PHY_ERR_2, 0);
	}
}
