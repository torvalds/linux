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

#include "hw.h"
#include "hw-ops.h"
#include <linux/export.h>

/* Common calibration code */


static int16_t ath9k_hw_get_nf_hist_mid(int16_t *nfCalBuffer)
{
	int16_t nfval;
	int16_t sort[ATH9K_NF_CAL_HIST_MAX];
	int i, j;

	for (i = 0; i < ATH9K_NF_CAL_HIST_MAX; i++)
		sort[i] = nfCalBuffer[i];

	for (i = 0; i < ATH9K_NF_CAL_HIST_MAX - 1; i++) {
		for (j = 1; j < ATH9K_NF_CAL_HIST_MAX - i; j++) {
			if (sort[j] > sort[j - 1]) {
				nfval = sort[j];
				sort[j] = sort[j - 1];
				sort[j - 1] = nfval;
			}
		}
	}
	nfval = sort[(ATH9K_NF_CAL_HIST_MAX - 1) >> 1];

	return nfval;
}

static struct ath_nf_limits *ath9k_hw_get_nf_limits(struct ath_hw *ah,
						    struct ath9k_channel *chan)
{
	struct ath_nf_limits *limit;

	if (!chan || IS_CHAN_2GHZ(chan))
		limit = &ah->nf_2g;
	else
		limit = &ah->nf_5g;

	return limit;
}

static s16 ath9k_hw_get_default_nf(struct ath_hw *ah,
				   struct ath9k_channel *chan)
{
	return ath9k_hw_get_nf_limits(ah, chan)->nominal;
}

s16 ath9k_hw_getchan_noise(struct ath_hw *ah, struct ath9k_channel *chan)
{
	s8 noise = ATH_DEFAULT_NOISE_FLOOR;

	if (chan && chan->noisefloor) {
		s8 delta = chan->noisefloor -
			   ath9k_hw_get_default_nf(ah, chan);
		if (delta > 0)
			noise += delta;
	}
	return noise;
}
EXPORT_SYMBOL(ath9k_hw_getchan_noise);

static void ath9k_hw_update_nfcal_hist_buffer(struct ath_hw *ah,
					      struct ath9k_hw_cal_data *cal,
					      int16_t *nfarray)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_nf_limits *limit;
	struct ath9k_nfcal_hist *h;
	bool high_nf_mid = false;
	u8 chainmask = (ah->rxchainmask << 3) | ah->rxchainmask;
	int i;

	h = cal->nfCalHist;
	limit = ath9k_hw_get_nf_limits(ah, ah->curchan);

	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (!(chainmask & (1 << i)) ||
		    ((i >= AR5416_MAX_CHAINS) && !IS_CHAN_HT40(ah->curchan)))
			continue;

		h[i].nfCalBuffer[h[i].currIndex] = nfarray[i];

		if (++h[i].currIndex >= ATH9K_NF_CAL_HIST_MAX)
			h[i].currIndex = 0;

		if (h[i].invalidNFcount > 0) {
			h[i].invalidNFcount--;
			h[i].privNF = nfarray[i];
		} else {
			h[i].privNF =
				ath9k_hw_get_nf_hist_mid(h[i].nfCalBuffer);
		}

		if (!h[i].privNF)
			continue;

		if (h[i].privNF > limit->max) {
			high_nf_mid = true;

			ath_dbg(common, CALIBRATE,
				"NFmid[%d] (%d) > MAX (%d), %s\n",
				i, h[i].privNF, limit->max,
				(cal->nfcal_interference ?
				 "not corrected (due to interference)" :
				 "correcting to MAX"));

			/*
			 * Normally we limit the average noise floor by the
			 * hardware specific maximum here. However if we have
			 * encountered stuck beacons because of interference,
			 * we bypass this limit here in order to better deal
			 * with our environment.
			 */
			if (!cal->nfcal_interference)
				h[i].privNF = limit->max;
		}
	}

	/*
	 * If the noise floor seems normal for all chains, assume that
	 * there is no significant interference in the environment anymore.
	 * Re-enable the enforcement of the NF maximum again.
	 */
	if (!high_nf_mid)
		cal->nfcal_interference = false;
}

static bool ath9k_hw_get_nf_thresh(struct ath_hw *ah,
				   enum ieee80211_band band,
				   int16_t *nft)
{
	switch (band) {
	case IEEE80211_BAND_5GHZ:
		*nft = (int8_t)ah->eep_ops->get_eeprom(ah, EEP_NFTHRESH_5);
		break;
	case IEEE80211_BAND_2GHZ:
		*nft = (int8_t)ah->eep_ops->get_eeprom(ah, EEP_NFTHRESH_2);
		break;
	default:
		BUG_ON(1);
		return false;
	}

	return true;
}

void ath9k_hw_reset_calibration(struct ath_hw *ah,
				struct ath9k_cal_list *currCal)
{
	int i;

	ath9k_hw_setup_calibration(ah, currCal);

	currCal->calState = CAL_RUNNING;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		ah->meas0.sign[i] = 0;
		ah->meas1.sign[i] = 0;
		ah->meas2.sign[i] = 0;
		ah->meas3.sign[i] = 0;
	}

	ah->cal_samples = 0;
}

/* This is done for the currently configured channel */
bool ath9k_hw_reset_calvalid(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_conf *conf = &common->hw->conf;
	struct ath9k_cal_list *currCal = ah->cal_list_curr;

	if (!ah->caldata)
		return true;

	if (!AR_SREV_9100(ah) && !AR_SREV_9160_10_OR_LATER(ah))
		return true;

	if (currCal == NULL)
		return true;

	if (currCal->calState != CAL_DONE) {
		ath_dbg(common, CALIBRATE, "Calibration state incorrect, %d\n",
			currCal->calState);
		return true;
	}

	if (!(ah->supp_cals & currCal->calData->calType))
		return true;

	ath_dbg(common, CALIBRATE, "Resetting Cal %d state for channel %u\n",
		currCal->calData->calType, conf->channel->center_freq);

	ah->caldata->CalValid &= ~currCal->calData->calType;
	currCal->calState = CAL_WAITING;

	return false;
}
EXPORT_SYMBOL(ath9k_hw_reset_calvalid);

void ath9k_hw_start_nfcal(struct ath_hw *ah, bool update)
{
	if (ah->caldata)
		ah->caldata->nfcal_pending = true;

	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_ENABLE_NF);

	if (update)
		REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	else
		REG_SET_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);

	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
}

void ath9k_hw_loadnf(struct ath_hw *ah, struct ath9k_channel *chan)
{
	struct ath9k_nfcal_hist *h = NULL;
	unsigned i, j;
	int32_t val;
	u8 chainmask = (ah->rxchainmask << 3) | ah->rxchainmask;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_conf *conf = &common->hw->conf;
	s16 default_nf = ath9k_hw_get_default_nf(ah, chan);

	if (ah->caldata)
		h = ah->caldata->nfCalHist;

	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (chainmask & (1 << i)) {
			s16 nfval;

			if ((i >= AR5416_MAX_CHAINS) && !conf_is_ht40(conf))
				continue;

			if (h)
				nfval = h[i].privNF;
			else
				nfval = default_nf;

			val = REG_READ(ah, ah->nf_regs[i]);
			val &= 0xFFFFFE00;
			val |= (((u32) nfval << 1) & 0x1ff);
			REG_WRITE(ah, ah->nf_regs[i], val);
		}
	}

	/*
	 * Load software filtered NF value into baseband internal minCCApwr
	 * variable.
	 */
	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_ENABLE_NF);
	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);

	/*
	 * Wait for load to complete, should be fast, a few 10s of us.
	 * The max delay was changed from an original 250us to 10000us
	 * since 250us often results in NF load timeout and causes deaf
	 * condition during stress testing 12/12/2009
	 */
	for (j = 0; j < 10000; j++) {
		if ((REG_READ(ah, AR_PHY_AGC_CONTROL) &
		     AR_PHY_AGC_CONTROL_NF) == 0)
			break;
		udelay(10);
	}

	/*
	 * We timed out waiting for the noisefloor to load, probably due to an
	 * in-progress rx. Simply return here and allow the load plenty of time
	 * to complete before the next calibration interval.  We need to avoid
	 * trying to load -50 (which happens below) while the previous load is
	 * still in progress as this can cause rx deafness. Instead by returning
	 * here, the baseband nf cal will just be capped by our present
	 * noisefloor until the next calibration timer.
	 */
	if (j == 10000) {
		ath_dbg(common, ANY,
			"Timeout while waiting for nf to load: AR_PHY_AGC_CONTROL=0x%x\n",
			REG_READ(ah, AR_PHY_AGC_CONTROL));
		return;
	}

	/*
	 * Restore maxCCAPower register parameter again so that we're not capped
	 * by the median we just loaded.  This will be initial (and max) value
	 * of next noise floor calibration the baseband does.
	 */
	ENABLE_REGWRITE_BUFFER(ah);
	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (chainmask & (1 << i)) {
			if ((i >= AR5416_MAX_CHAINS) && !conf_is_ht40(conf))
				continue;

			val = REG_READ(ah, ah->nf_regs[i]);
			val &= 0xFFFFFE00;
			val |= (((u32) (-50) << 1) & 0x1ff);
			REG_WRITE(ah, ah->nf_regs[i], val);
		}
	}
	REGWRITE_BUFFER_FLUSH(ah);
}


static void ath9k_hw_nf_sanitize(struct ath_hw *ah, s16 *nf)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_nf_limits *limit;
	int i;

	if (IS_CHAN_2GHZ(ah->curchan))
		limit = &ah->nf_2g;
	else
		limit = &ah->nf_5g;

	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (!nf[i])
			continue;

		ath_dbg(common, CALIBRATE,
			"NF calibrated [%s] [chain %d] is %d\n",
			(i >= 3 ? "ext" : "ctl"), i % 3, nf[i]);

		if (nf[i] > limit->max) {
			ath_dbg(common, CALIBRATE,
				"NF[%d] (%d) > MAX (%d), correcting to MAX\n",
				i, nf[i], limit->max);
			nf[i] = limit->max;
		} else if (nf[i] < limit->min) {
			ath_dbg(common, CALIBRATE,
				"NF[%d] (%d) < MIN (%d), correcting to NOM\n",
				i, nf[i], limit->min);
			nf[i] = limit->nominal;
		}
	}
}

bool ath9k_hw_getnf(struct ath_hw *ah, struct ath9k_channel *chan)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int16_t nf, nfThresh;
	int16_t nfarray[NUM_NF_READINGS] = { 0 };
	struct ath9k_nfcal_hist *h;
	struct ieee80211_channel *c = chan->chan;
	struct ath9k_hw_cal_data *caldata = ah->caldata;

	chan->channelFlags &= (~CHANNEL_CW_INT);
	if (REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) {
		ath_dbg(common, CALIBRATE,
			"NF did not complete in calibration window\n");
		return false;
	}

	ath9k_hw_do_getnf(ah, nfarray);
	ath9k_hw_nf_sanitize(ah, nfarray);
	nf = nfarray[0];
	if (ath9k_hw_get_nf_thresh(ah, c->band, &nfThresh)
	    && nf > nfThresh) {
		ath_dbg(common, CALIBRATE,
			"noise floor failed detected; detected %d, threshold %d\n",
			nf, nfThresh);
		chan->channelFlags |= CHANNEL_CW_INT;
	}

	if (!caldata) {
		chan->noisefloor = nf;
		ah->noise = ath9k_hw_getchan_noise(ah, chan);
		return false;
	}

	h = caldata->nfCalHist;
	caldata->nfcal_pending = false;
	ath9k_hw_update_nfcal_hist_buffer(ah, caldata, nfarray);
	chan->noisefloor = h[0].privNF;
	ah->noise = ath9k_hw_getchan_noise(ah, chan);
	return true;
}
EXPORT_SYMBOL(ath9k_hw_getnf);

void ath9k_init_nfcal_hist_buffer(struct ath_hw *ah,
				  struct ath9k_channel *chan)
{
	struct ath9k_nfcal_hist *h;
	s16 default_nf;
	int i, j;

	ah->caldata->channel = chan->channel;
	ah->caldata->channelFlags = chan->channelFlags & ~CHANNEL_CW_INT;
	h = ah->caldata->nfCalHist;
	default_nf = ath9k_hw_get_default_nf(ah, chan);
	for (i = 0; i < NUM_NF_READINGS; i++) {
		h[i].currIndex = 0;
		h[i].privNF = default_nf;
		h[i].invalidNFcount = AR_PHY_CCA_FILTERWINDOW_LENGTH;
		for (j = 0; j < ATH9K_NF_CAL_HIST_MAX; j++) {
			h[i].nfCalBuffer[j] = default_nf;
		}
	}
}


void ath9k_hw_bstuck_nfcal(struct ath_hw *ah)
{
	struct ath9k_hw_cal_data *caldata = ah->caldata;

	if (unlikely(!caldata))
		return;

	/*
	 * If beacons are stuck, the most likely cause is interference.
	 * Triggering a noise floor calibration at this point helps the
	 * hardware adapt to a noisy environment much faster.
	 * To ensure that we recover from stuck beacons quickly, let
	 * the baseband update the internal NF value itself, similar to
	 * what is being done after a full reset.
	 */
	if (!caldata->nfcal_pending)
		ath9k_hw_start_nfcal(ah, true);
	else if (!(REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF))
		ath9k_hw_getnf(ah, ah->curchan);

	caldata->nfcal_interference = true;
}
EXPORT_SYMBOL(ath9k_hw_bstuck_nfcal);

