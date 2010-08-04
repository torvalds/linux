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

#include "hw.h"
#include "hw-ops.h"

/* Common calibration code */

/* We can tune this as we go by monitoring really low values */
#define ATH9K_NF_TOO_LOW	-60

/* AR5416 may return very high value (like -31 dBm), in those cases the nf
 * is incorrect and we should use the static NF value. Later we can try to
 * find out why they are reporting these values */

static bool ath9k_hw_nf_in_range(struct ath_hw *ah, s16 nf)
{
	if (nf > ATH9K_NF_TOO_LOW) {
		ath_print(ath9k_hw_common(ah), ATH_DBG_CALIBRATE,
			  "noise floor value detected (%d) is "
			  "lower than what we think is a "
			  "reasonable value (%d)\n",
			  nf, ATH9K_NF_TOO_LOW);
		return false;
	}
	return true;
}

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

static void ath9k_hw_update_nfcal_hist_buffer(struct ath9k_nfcal_hist *h,
					      int16_t *nfarray)
{
	int i;

	for (i = 0; i < NUM_NF_READINGS; i++) {
		h[i].nfCalBuffer[h[i].currIndex] = nfarray[i];

		if (++h[i].currIndex >= ATH9K_NF_CAL_HIST_MAX)
			h[i].currIndex = 0;

		if (h[i].invalidNFcount > 0) {
			if (nfarray[i] < AR_PHY_CCA_MIN_BAD_VALUE ||
			    nfarray[i] > AR_PHY_CCA_MAX_HIGH_VALUE) {
				h[i].invalidNFcount = ATH9K_NF_CAL_HIST_MAX;
			} else {
				h[i].invalidNFcount--;
				h[i].privNF = nfarray[i];
			}
		} else {
			h[i].privNF =
				ath9k_hw_get_nf_hist_mid(h[i].nfCalBuffer);
		}
	}
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

	if (!ah->curchan)
		return true;

	if (!AR_SREV_9100(ah) && !AR_SREV_9160_10_OR_LATER(ah))
		return true;

	if (currCal == NULL)
		return true;

	if (currCal->calState != CAL_DONE) {
		ath_print(common, ATH_DBG_CALIBRATE,
			  "Calibration state incorrect, %d\n",
			  currCal->calState);
		return true;
	}

	if (!ath9k_hw_iscal_supported(ah, currCal->calData->calType))
		return true;

	ath_print(common, ATH_DBG_CALIBRATE,
		  "Resetting Cal %d state for channel %u\n",
		  currCal->calData->calType, conf->channel->center_freq);

	ah->curchan->CalValid &= ~currCal->calData->calType;
	currCal->calState = CAL_WAITING;

	return false;
}
EXPORT_SYMBOL(ath9k_hw_reset_calvalid);

void ath9k_hw_start_nfcal(struct ath_hw *ah)
{
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_ENABLE_NF);
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
}

int16_t ath9k_hw_getnf(struct ath_hw *ah,
		       struct ath9k_channel *chan)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int16_t nf, nfThresh;
	int16_t nfarray[NUM_NF_READINGS] = { 0 };
	struct ath9k_nfcal_hist *h;
	struct ieee80211_channel *c = chan->chan;

	chan->channelFlags &= (~CHANNEL_CW_INT);
	if (REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) {
		ath_print(common, ATH_DBG_CALIBRATE,
			  "NF did not complete in calibration window\n");
		nf = 0;
		chan->rawNoiseFloor = nf;
		return chan->rawNoiseFloor;
	} else {
		ath9k_hw_do_getnf(ah, nfarray);
		nf = nfarray[0];
		if (ath9k_hw_get_nf_thresh(ah, c->band, &nfThresh)
		    && nf > nfThresh) {
			ath_print(common, ATH_DBG_CALIBRATE,
				  "noise floor failed detected; "
				  "detected %d, threshold %d\n",
				  nf, nfThresh);
			chan->channelFlags |= CHANNEL_CW_INT;
		}
	}

	h = ah->nfCalHist;

	ath9k_hw_update_nfcal_hist_buffer(h, nfarray);
	chan->rawNoiseFloor = h[0].privNF;

	return chan->rawNoiseFloor;
}

void ath9k_init_nfcal_hist_buffer(struct ath_hw *ah)
{
	int i, j;
	s16 noise_floor;

	if (AR_SREV_9280(ah))
		noise_floor = AR_PHY_CCA_MAX_AR9280_GOOD_VALUE;
	else if (AR_SREV_9285(ah) || AR_SREV_9271(ah))
		noise_floor = AR_PHY_CCA_MAX_AR9285_GOOD_VALUE;
	else if (AR_SREV_9287(ah))
		noise_floor = AR_PHY_CCA_MAX_AR9287_GOOD_VALUE;
	else
		noise_floor = AR_PHY_CCA_MAX_AR5416_GOOD_VALUE;

	for (i = 0; i < NUM_NF_READINGS; i++) {
		ah->nfCalHist[i].currIndex = 0;
		ah->nfCalHist[i].privNF = noise_floor;
		ah->nfCalHist[i].invalidNFcount =
			AR_PHY_CCA_FILTERWINDOW_LENGTH;
		for (j = 0; j < ATH9K_NF_CAL_HIST_MAX; j++) {
			ah->nfCalHist[i].nfCalBuffer[j] = noise_floor;
		}
	}
}

s16 ath9k_hw_getchan_noise(struct ath_hw *ah, struct ath9k_channel *chan)
{
	s16 nf;

	if (chan->rawNoiseFloor == 0)
		nf = -96;
	else
		nf = chan->rawNoiseFloor;

	if (!ath9k_hw_nf_in_range(ah, nf))
		nf = ATH_DEFAULT_NOISE_FLOOR;

	return nf;
}
EXPORT_SYMBOL(ath9k_hw_getchan_noise);
