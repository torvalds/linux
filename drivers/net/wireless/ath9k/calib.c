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

static const int16_t NOISE_FLOOR[] = { -96, -93, -98, -96, -93, -96 };

/* We can tune this as we go by monitoring really low values */
#define ATH9K_NF_TOO_LOW	-60

/* AR5416 may return very high value (like -31 dBm), in those cases the nf
 * is incorrect and we should use the static NF value. Later we can try to
 * find out why they are reporting these values */

static bool ath9k_hw_nf_in_range(struct ath_hal *ah, s16 nf)
{
	if (nf > ATH9K_NF_TOO_LOW) {
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
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
	return;
}

static void ath9k_hw_do_getnf(struct ath_hal *ah,
			      int16_t nfarray[NUM_NF_READINGS])
{
	int16_t nf;

	if (AR_SREV_9280_10_OR_LATER(ah))
		nf = MS(REG_READ(ah, AR_PHY_CCA), AR9280_PHY_MINCCA_PWR);
	else
		nf = MS(REG_READ(ah, AR_PHY_CCA), AR_PHY_MINCCA_PWR);

	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
		"NF calibrated [ctl] [chain 0] is %d\n", nf);
	nfarray[0] = nf;

	if (AR_SREV_9280_10_OR_LATER(ah))
		nf = MS(REG_READ(ah, AR_PHY_CH1_CCA),
			AR9280_PHY_CH1_MINCCA_PWR);
	else
		nf = MS(REG_READ(ah, AR_PHY_CH1_CCA),
			AR_PHY_CH1_MINCCA_PWR);

	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
		"NF calibrated [ctl] [chain 1] is %d\n", nf);
	nfarray[1] = nf;

	if (!AR_SREV_9280(ah)) {
		nf = MS(REG_READ(ah, AR_PHY_CH2_CCA),
			AR_PHY_CH2_MINCCA_PWR);
		if (nf & 0x100)
			nf = 0 - ((nf ^ 0x1ff) + 1);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"NF calibrated [ctl] [chain 2] is %d\n", nf);
		nfarray[2] = nf;
	}

	if (AR_SREV_9280_10_OR_LATER(ah))
		nf = MS(REG_READ(ah, AR_PHY_EXT_CCA),
			AR9280_PHY_EXT_MINCCA_PWR);
	else
		nf = MS(REG_READ(ah, AR_PHY_EXT_CCA),
			AR_PHY_EXT_MINCCA_PWR);

	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
		"NF calibrated [ext] [chain 0] is %d\n", nf);
	nfarray[3] = nf;

	if (AR_SREV_9280_10_OR_LATER(ah))
		nf = MS(REG_READ(ah, AR_PHY_CH1_EXT_CCA),
			AR9280_PHY_CH1_EXT_MINCCA_PWR);
	else
		nf = MS(REG_READ(ah, AR_PHY_CH1_EXT_CCA),
			AR_PHY_CH1_EXT_MINCCA_PWR);

	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
		"NF calibrated [ext] [chain 1] is %d\n", nf);
	nfarray[4] = nf;

	if (!AR_SREV_9280(ah)) {
		nf = MS(REG_READ(ah, AR_PHY_CH2_EXT_CCA),
			AR_PHY_CH2_EXT_MINCCA_PWR);
		if (nf & 0x100)
			nf = 0 - ((nf ^ 0x1ff) + 1);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"NF calibrated [ext] [chain 2] is %d\n", nf);
		nfarray[5] = nf;
	}
}

static bool getNoiseFloorThresh(struct ath_hal *ah,
				const struct ath9k_channel *chan,
				int16_t *nft)
{
	switch (chan->chanmode) {
	case CHANNEL_A:
	case CHANNEL_A_HT20:
	case CHANNEL_A_HT40PLUS:
	case CHANNEL_A_HT40MINUS:
		*nft = (int8_t)ath9k_hw_get_eeprom(ah, EEP_NFTHRESH_5);
		break;
	case CHANNEL_B:
	case CHANNEL_G:
	case CHANNEL_G_HT20:
	case CHANNEL_G_HT40PLUS:
	case CHANNEL_G_HT40MINUS:
		*nft = (int8_t)ath9k_hw_get_eeprom(ah, EEP_NFTHRESH_2);
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			"invalid channel flags 0x%x\n", chan->channelFlags);
		return false;
	}

	return true;
}

static void ath9k_hw_setup_calibration(struct ath_hal *ah,
				       struct hal_cal_list *currCal)
{
	REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4(0),
		      AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX,
		      currCal->calData->calCountMax);

	switch (currCal->calData->calType) {
	case IQ_MISMATCH_CAL:
		REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_IQ);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"starting IQ Mismatch Calibration\n");
		break;
	case ADC_GAIN_CAL:
		REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_ADC_GAIN);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"starting ADC Gain Calibration\n");
		break;
	case ADC_DC_CAL:
		REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_ADC_DC_PER);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"starting ADC DC Calibration\n");
		break;
	case ADC_DC_INIT_CAL:
		REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_ADC_DC_INIT);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"starting Init ADC DC Calibration\n");
		break;
	}

	REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4(0),
		    AR_PHY_TIMING_CTRL4_DO_CAL);
}

static void ath9k_hw_reset_calibration(struct ath_hal *ah,
				       struct hal_cal_list *currCal)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	ath9k_hw_setup_calibration(ah, currCal);

	currCal->calState = CAL_RUNNING;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		ahp->ah_Meas0.sign[i] = 0;
		ahp->ah_Meas1.sign[i] = 0;
		ahp->ah_Meas2.sign[i] = 0;
		ahp->ah_Meas3.sign[i] = 0;
	}

	ahp->ah_CalSamples = 0;
}

static void ath9k_hw_per_calibration(struct ath_hal *ah,
				     struct ath9k_channel *ichan,
				     u8 rxchainmask,
				     struct hal_cal_list *currCal,
				     bool *isCalDone)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	*isCalDone = false;

	if (currCal->calState == CAL_RUNNING) {
		if (!(REG_READ(ah, AR_PHY_TIMING_CTRL4(0)) &
		      AR_PHY_TIMING_CTRL4_DO_CAL)) {

			currCal->calData->calCollect(ah);
			ahp->ah_CalSamples++;

			if (ahp->ah_CalSamples >= currCal->calData->calNumSamples) {
				int i, numChains = 0;
				for (i = 0; i < AR5416_MAX_CHAINS; i++) {
					if (rxchainmask & (1 << i))
						numChains++;
				}

				currCal->calData->calPostProc(ah, numChains);
				ichan->CalValid |= currCal->calData->calType;
				currCal->calState = CAL_DONE;
				*isCalDone = true;
			} else {
				ath9k_hw_setup_calibration(ah, currCal);
			}
		}
	} else if (!(ichan->CalValid & currCal->calData->calType)) {
		ath9k_hw_reset_calibration(ah, currCal);
	}
}

static bool ath9k_hw_iscal_supported(struct ath_hal *ah,
				     struct ath9k_channel *chan,
				     enum hal_cal_types calType)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	bool retval = false;

	switch (calType & ahp->ah_suppCals) {
	case IQ_MISMATCH_CAL:
		if (!IS_CHAN_B(chan))
			retval = true;
		break;
	case ADC_GAIN_CAL:
	case ADC_DC_CAL:
		if (!IS_CHAN_B(chan)
		    && !(IS_CHAN_2GHZ(chan) && IS_CHAN_HT20(chan)))
			retval = true;
		break;
	}

	return retval;
}

static void ath9k_hw_iqcal_collect(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		ahp->ah_totalPowerMeasI[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
		ahp->ah_totalPowerMeasQ[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
		ahp->ah_totalIqCorrMeas[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"%d: Chn %d pmi=0x%08x;pmq=0x%08x;iqcm=0x%08x;\n",
			ahp->ah_CalSamples, i, ahp->ah_totalPowerMeasI[i],
			ahp->ah_totalPowerMeasQ[i],
			ahp->ah_totalIqCorrMeas[i]);
	}
}

static void ath9k_hw_adc_gaincal_collect(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		ahp->ah_totalAdcIOddPhase[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
		ahp->ah_totalAdcIEvenPhase[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
		ahp->ah_totalAdcQOddPhase[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
		ahp->ah_totalAdcQEvenPhase[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_3(i));

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"%d: Chn %d oddi=0x%08x; eveni=0x%08x; "
			"oddq=0x%08x; evenq=0x%08x;\n",
			ahp->ah_CalSamples, i,
			ahp->ah_totalAdcIOddPhase[i],
			ahp->ah_totalAdcIEvenPhase[i],
			ahp->ah_totalAdcQOddPhase[i],
			ahp->ah_totalAdcQEvenPhase[i]);
	}
}

static void ath9k_hw_adc_dccal_collect(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		ahp->ah_totalAdcDcOffsetIOddPhase[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
		ahp->ah_totalAdcDcOffsetIEvenPhase[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
		ahp->ah_totalAdcDcOffsetQOddPhase[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
		ahp->ah_totalAdcDcOffsetQEvenPhase[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_3(i));

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"%d: Chn %d oddi=0x%08x; eveni=0x%08x; "
			"oddq=0x%08x; evenq=0x%08x;\n",
			ahp->ah_CalSamples, i,
			ahp->ah_totalAdcDcOffsetIOddPhase[i],
			ahp->ah_totalAdcDcOffsetIEvenPhase[i],
			ahp->ah_totalAdcDcOffsetQOddPhase[i],
			ahp->ah_totalAdcDcOffsetQEvenPhase[i]);
	}
}

static void ath9k_hw_iqcalibrate(struct ath_hal *ah, u8 numChains)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 powerMeasQ, powerMeasI, iqCorrMeas;
	u32 qCoffDenom, iCoffDenom;
	int32_t qCoff, iCoff;
	int iqCorrNeg, i;

	for (i = 0; i < numChains; i++) {
		powerMeasI = ahp->ah_totalPowerMeasI[i];
		powerMeasQ = ahp->ah_totalPowerMeasQ[i];
		iqCorrMeas = ahp->ah_totalIqCorrMeas[i];

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Starting IQ Cal and Correction for Chain %d\n",
			i);

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Orignal: Chn %diq_corr_meas = 0x%08x\n",
			i, ahp->ah_totalIqCorrMeas[i]);

		iqCorrNeg = 0;

		if (iqCorrMeas > 0x80000000) {
			iqCorrMeas = (0xffffffff - iqCorrMeas) + 1;
			iqCorrNeg = 1;
		}

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d pwr_meas_i = 0x%08x\n", i, powerMeasI);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d pwr_meas_q = 0x%08x\n", i, powerMeasQ);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE, "iqCorrNeg is 0x%08x\n",
			iqCorrNeg);

		iCoffDenom = (powerMeasI / 2 + powerMeasQ / 2) / 128;
		qCoffDenom = powerMeasQ / 64;

		if (powerMeasQ != 0) {
			iCoff = iqCorrMeas / iCoffDenom;
			qCoff = powerMeasI / qCoffDenom - 64;
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"Chn %d iCoff = 0x%08x\n", i, iCoff);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"Chn %d qCoff = 0x%08x\n", i, qCoff);

			iCoff = iCoff & 0x3f;
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"New: Chn %d iCoff = 0x%08x\n", i, iCoff);
			if (iqCorrNeg == 0x0)
				iCoff = 0x40 - iCoff;

			if (qCoff > 15)
				qCoff = 15;
			else if (qCoff <= -16)
				qCoff = 16;

			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"Chn %d : iCoff = 0x%x  qCoff = 0x%x\n",
				i, iCoff, qCoff);

			REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4(i),
				      AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF,
				      iCoff);
			REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4(i),
				      AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF,
				      qCoff);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"IQ Cal and Correction done for Chain %d\n",
				i);
		}
	}

	REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4(0),
		    AR_PHY_TIMING_CTRL4_IQCORR_ENABLE);
}

static void ath9k_hw_adc_gaincal_calibrate(struct ath_hal *ah, u8 numChains)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 iOddMeasOffset, iEvenMeasOffset, qOddMeasOffset, qEvenMeasOffset;
	u32 qGainMismatch, iGainMismatch, val, i;

	for (i = 0; i < numChains; i++) {
		iOddMeasOffset = ahp->ah_totalAdcIOddPhase[i];
		iEvenMeasOffset = ahp->ah_totalAdcIEvenPhase[i];
		qOddMeasOffset = ahp->ah_totalAdcQOddPhase[i];
		qEvenMeasOffset = ahp->ah_totalAdcQEvenPhase[i];

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Starting ADC Gain Cal for Chain %d\n", i);

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d pwr_meas_odd_i = 0x%08x\n", i,
			iOddMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d pwr_meas_even_i = 0x%08x\n", i,
			iEvenMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d pwr_meas_odd_q = 0x%08x\n", i,
			qOddMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d pwr_meas_even_q = 0x%08x\n", i,
			qEvenMeasOffset);

		if (iOddMeasOffset != 0 && qEvenMeasOffset != 0) {
			iGainMismatch =
				((iEvenMeasOffset * 32) /
				 iOddMeasOffset) & 0x3f;
			qGainMismatch =
				((qOddMeasOffset * 32) /
				 qEvenMeasOffset) & 0x3f;

			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"Chn %d gain_mismatch_i = 0x%08x\n", i,
				iGainMismatch);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"Chn %d gain_mismatch_q = 0x%08x\n", i,
				qGainMismatch);

			val = REG_READ(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i));
			val &= 0xfffff000;
			val |= (qGainMismatch) | (iGainMismatch << 6);
			REG_WRITE(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i), val);

			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"ADC Gain Cal done for Chain %d\n", i);
		}
	}

	REG_WRITE(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(0),
		  REG_READ(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(0)) |
		  AR_PHY_NEW_ADC_GAIN_CORR_ENABLE);
}

static void ath9k_hw_adc_dccal_calibrate(struct ath_hal *ah, u8 numChains)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 iOddMeasOffset, iEvenMeasOffset, val, i;
	int32_t qOddMeasOffset, qEvenMeasOffset, qDcMismatch, iDcMismatch;
	const struct hal_percal_data *calData =
		ahp->ah_cal_list_curr->calData;
	u32 numSamples =
		(1 << (calData->calCountMax + 5)) * calData->calNumSamples;

	for (i = 0; i < numChains; i++) {
		iOddMeasOffset = ahp->ah_totalAdcDcOffsetIOddPhase[i];
		iEvenMeasOffset = ahp->ah_totalAdcDcOffsetIEvenPhase[i];
		qOddMeasOffset = ahp->ah_totalAdcDcOffsetQOddPhase[i];
		qEvenMeasOffset = ahp->ah_totalAdcDcOffsetQEvenPhase[i];

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Starting ADC DC Offset Cal for Chain %d\n", i);

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d pwr_meas_odd_i = %d\n", i,
			iOddMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d pwr_meas_even_i = %d\n", i,
			iEvenMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d pwr_meas_odd_q = %d\n", i,
			qOddMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d pwr_meas_even_q = %d\n", i,
			qEvenMeasOffset);

		iDcMismatch = (((iEvenMeasOffset - iOddMeasOffset) * 2) /
			       numSamples) & 0x1ff;
		qDcMismatch = (((qOddMeasOffset - qEvenMeasOffset) * 2) /
			       numSamples) & 0x1ff;

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d dc_offset_mismatch_i = 0x%08x\n", i,
			iDcMismatch);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Chn %d dc_offset_mismatch_q = 0x%08x\n", i,
			qDcMismatch);

		val = REG_READ(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i));
		val &= 0xc0000fff;
		val |= (qDcMismatch << 12) | (iDcMismatch << 21);
		REG_WRITE(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i), val);

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"ADC DC Offset Cal done for Chain %d\n", i);
	}

	REG_WRITE(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(0),
		  REG_READ(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(0)) |
		  AR_PHY_NEW_ADC_DC_OFFSET_CORR_ENABLE);
}

void ath9k_hw_reset_calvalid(struct ath_hal *ah, struct ath9k_channel *chan,
			     bool *isCalDone)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *ichan =
		ath9k_regd_check_channel(ah, chan);
	struct hal_cal_list *currCal = ahp->ah_cal_list_curr;

	*isCalDone = true;

	if (!AR_SREV_9100(ah) && !AR_SREV_9160_10_OR_LATER(ah))
		return;

	if (currCal == NULL)
		return;

	if (ichan == NULL) {
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"invalid channel %u/0x%x; no mapping\n",
			chan->channel, chan->channelFlags);
		return;
	}


	if (currCal->calState != CAL_DONE) {
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"Calibration state incorrect, %d\n",
			currCal->calState);
		return;
	}


	if (!ath9k_hw_iscal_supported(ah, chan, currCal->calData->calType))
		return;

	DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
		"Resetting Cal %d state for channel %u/0x%x\n",
		currCal->calData->calType, chan->channel,
		chan->channelFlags);

	ichan->CalValid &= ~currCal->calData->calType;
	currCal->calState = CAL_WAITING;

	*isCalDone = false;
}

void ath9k_hw_start_nfcal(struct ath_hal *ah)
{
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_ENABLE_NF);
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
}

void ath9k_hw_loadnf(struct ath_hal *ah, struct ath9k_channel *chan)
{
	struct ath9k_nfcal_hist *h;
	int i, j;
	int32_t val;
	const u32 ar5416_cca_regs[6] = {
		AR_PHY_CCA,
		AR_PHY_CH1_CCA,
		AR_PHY_CH2_CCA,
		AR_PHY_EXT_CCA,
		AR_PHY_CH1_EXT_CCA,
		AR_PHY_CH2_EXT_CCA
	};
	u8 chainmask;

	if (AR_SREV_9280(ah))
		chainmask = 0x1B;
	else
		chainmask = 0x3F;

#ifdef ATH_NF_PER_CHAN
	h = chan->nfCalHist;
#else
	h = ah->nfCalHist;
#endif

	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (chainmask & (1 << i)) {
			val = REG_READ(ah, ar5416_cca_regs[i]);
			val &= 0xFFFFFE00;
			val |= (((u32) (h[i].privNF) << 1) & 0x1ff);
			REG_WRITE(ah, ar5416_cca_regs[i], val);
		}
	}

	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_ENABLE_NF);
	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);

	for (j = 0; j < 1000; j++) {
		if ((REG_READ(ah, AR_PHY_AGC_CONTROL) &
		     AR_PHY_AGC_CONTROL_NF) == 0)
			break;
		udelay(10);
	}

	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (chainmask & (1 << i)) {
			val = REG_READ(ah, ar5416_cca_regs[i]);
			val &= 0xFFFFFE00;
			val |= (((u32) (-50) << 1) & 0x1ff);
			REG_WRITE(ah, ar5416_cca_regs[i], val);
		}
	}
}

int16_t ath9k_hw_getnf(struct ath_hal *ah,
		       struct ath9k_channel *chan)
{
	int16_t nf, nfThresh;
	int16_t nfarray[NUM_NF_READINGS] = { 0 };
	struct ath9k_nfcal_hist *h;
	u8 chainmask;

	if (AR_SREV_9280(ah))
		chainmask = 0x1B;
	else
		chainmask = 0x3F;

	chan->channelFlags &= (~CHANNEL_CW_INT);
	if (REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) {
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"NF did not complete in calibration window\n");
		nf = 0;
		chan->rawNoiseFloor = nf;
		return chan->rawNoiseFloor;
	} else {
		ath9k_hw_do_getnf(ah, nfarray);
		nf = nfarray[0];
		if (getNoiseFloorThresh(ah, chan, &nfThresh)
		    && nf > nfThresh) {
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"noise floor failed detected; "
				"detected %d, threshold %d\n",
				nf, nfThresh);
			chan->channelFlags |= CHANNEL_CW_INT;
		}
	}

#ifdef ATH_NF_PER_CHAN
	h = chan->nfCalHist;
#else
	h = ah->nfCalHist;
#endif

	ath9k_hw_update_nfcal_hist_buffer(h, nfarray);
	chan->rawNoiseFloor = h[0].privNF;

	return chan->rawNoiseFloor;
}

void ath9k_init_nfcal_hist_buffer(struct ath_hal *ah)
{
	int i, j;

	for (i = 0; i < NUM_NF_READINGS; i++) {
		ah->nfCalHist[i].currIndex = 0;
		ah->nfCalHist[i].privNF = AR_PHY_CCA_MAX_GOOD_VALUE;
		ah->nfCalHist[i].invalidNFcount =
			AR_PHY_CCA_FILTERWINDOW_LENGTH;
		for (j = 0; j < ATH9K_NF_CAL_HIST_MAX; j++) {
			ah->nfCalHist[i].nfCalBuffer[j] =
				AR_PHY_CCA_MAX_GOOD_VALUE;
		}
	}
	return;
}

s16 ath9k_hw_getchan_noise(struct ath_hal *ah, struct ath9k_channel *chan)
{
	struct ath9k_channel *ichan;
	s16 nf;

	ichan = ath9k_regd_check_channel(ah, chan);
	if (ichan == NULL) {
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"invalid channel %u/0x%x; no mapping\n",
			chan->channel, chan->channelFlags);
		return ATH_DEFAULT_NOISE_FLOOR;
	}
	if (ichan->rawNoiseFloor == 0) {
		enum wireless_mode mode = ath9k_hw_chan2wmode(ah, chan);
		nf = NOISE_FLOOR[mode];
	} else
		nf = ichan->rawNoiseFloor;

	if (!ath9k_hw_nf_in_range(ah, nf))
		nf = ATH_DEFAULT_NOISE_FLOOR;

	return nf;
}

bool ath9k_hw_calibrate(struct ath_hal *ah, struct ath9k_channel *chan,
			u8 rxchainmask, bool longcal,
			bool *isCalDone)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct hal_cal_list *currCal = ahp->ah_cal_list_curr;
	struct ath9k_channel *ichan = ath9k_regd_check_channel(ah, chan);

	*isCalDone = true;

	if (ichan == NULL) {
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			"invalid channel %u/0x%x; no mapping\n",
			chan->channel, chan->channelFlags);
		return false;
	}

	if (currCal &&
	    (currCal->calState == CAL_RUNNING ||
	     currCal->calState == CAL_WAITING)) {
		ath9k_hw_per_calibration(ah, ichan, rxchainmask, currCal,
					 isCalDone);
		if (*isCalDone) {
			ahp->ah_cal_list_curr = currCal = currCal->calNext;

			if (currCal->calState == CAL_WAITING) {
				*isCalDone = false;
				ath9k_hw_reset_calibration(ah, currCal);
			}
		}
	}

	if (longcal) {
		ath9k_hw_getnf(ah, ichan);
		ath9k_hw_loadnf(ah, ah->ah_curchan);
		ath9k_hw_start_nfcal(ah);

		if ((ichan->channelFlags & CHANNEL_CW_INT) != 0) {
			chan->channelFlags |= CHANNEL_CW_INT;
			ichan->channelFlags &= ~CHANNEL_CW_INT;
		}
	}

	return true;
}

static inline void ath9k_hw_9285_pa_cal(struct ath_hal *ah)
{

	u32 regVal;
	int i, offset, offs_6_1, offs_0;
	u32 ccomp_org, reg_field;
	u32 regList[][2] = {
		{ 0x786c, 0 },
		{ 0x7854, 0 },
		{ 0x7820, 0 },
		{ 0x7824, 0 },
		{ 0x7868, 0 },
		{ 0x783c, 0 },
		{ 0x7838, 0 },
	};

	if (AR_SREV_9285_11(ah)) {
		REG_WRITE(ah, AR9285_AN_TOP4, (AR9285_AN_TOP4_DEFAULT | 0x14));
		udelay(10);
	}

	for (i = 0; i < ARRAY_SIZE(regList); i++)
		regList[i][1] = REG_READ(ah, regList[i][0]);

	regVal = REG_READ(ah, 0x7834);
	regVal &= (~(0x1));
	REG_WRITE(ah, 0x7834, regVal);
	regVal = REG_READ(ah, 0x9808);
	regVal |= (0x1 << 27);
	REG_WRITE(ah, 0x9808, regVal);

	REG_RMW_FIELD(ah, AR9285_AN_TOP3, AR9285_AN_TOP3_PWDDAC, 1);
	REG_RMW_FIELD(ah, AR9285_AN_RXTXBB1, AR9285_AN_RXTXBB1_PDRXTXBB1, 1);
	REG_RMW_FIELD(ah, AR9285_AN_RXTXBB1, AR9285_AN_RXTXBB1_PDV2I, 1);
	REG_RMW_FIELD(ah, AR9285_AN_RXTXBB1, AR9285_AN_RXTXBB1_PDDACIF, 1);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G2, AR9285_AN_RF2G2_OFFCAL, 0);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G7, AR9285_AN_RF2G7_PWDDB, 0);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_ENPACAL, 0);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_PDPADRV1, 1);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_PDPADRV2, 0);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_PDPAOUT, 0);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G8, AR9285_AN_RF2G8_PADRVGN2TAB0, 7);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G7, AR9285_AN_RF2G7_PADRVGN2TAB0, 0);
	ccomp_org = MS(REG_READ(ah, AR9285_AN_RF2G6), AR9285_AN_RF2G6_CCOMP);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_CCOMP, 7);

	REG_WRITE(ah, AR9285_AN_TOP2, 0xca0358a0);
	udelay(30);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_OFFS, 0);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, 0);

	for (i = 6; i > 0; i--) {
		regVal = REG_READ(ah, 0x7834);
		regVal |= (1 << (19 + i));
		REG_WRITE(ah, 0x7834, regVal);
		udelay(1);
		regVal = REG_READ(ah, 0x7834);
		regVal &= (~(0x1 << (19 + i)));
		reg_field = MS(REG_READ(ah, 0x7840), AR9285_AN_RXTXBB1_SPARE9);
		regVal |= (reg_field << (19 + i));
		REG_WRITE(ah, 0x7834, regVal);
	}

	REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, 1);
	udelay(1);
	reg_field = MS(REG_READ(ah, AR9285_AN_RF2G9), AR9285_AN_RXTXBB1_SPARE9);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, reg_field);
	offs_6_1 = MS(REG_READ(ah, AR9285_AN_RF2G6), AR9285_AN_RF2G6_OFFS);
	offs_0   = MS(REG_READ(ah, AR9285_AN_RF2G3), AR9285_AN_RF2G3_PDVCCOMP);

	offset = (offs_6_1<<1) | offs_0;
	offset = offset - 0;
	offs_6_1 = offset>>1;
	offs_0 = offset & 1;

	REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_OFFS, offs_6_1);
	REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, offs_0);

	regVal = REG_READ(ah, 0x7834);
	regVal |= 0x1;
	REG_WRITE(ah, 0x7834, regVal);
	regVal = REG_READ(ah, 0x9808);
	regVal &= (~(0x1 << 27));
	REG_WRITE(ah, 0x9808, regVal);

	for (i = 0; i < ARRAY_SIZE(regList); i++)
		REG_WRITE(ah, regList[i][0], regList[i][1]);

	REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_CCOMP, ccomp_org);

	if (AR_SREV_9285_11(ah))
		REG_WRITE(ah, AR9285_AN_TOP4, AR9285_AN_TOP4_DEFAULT);

}

bool ath9k_hw_init_cal(struct ath_hal *ah,
		       struct ath9k_channel *chan)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *ichan = ath9k_regd_check_channel(ah, chan);

	REG_WRITE(ah, AR_PHY_AGC_CONTROL,
		  REG_READ(ah, AR_PHY_AGC_CONTROL) |
		  AR_PHY_AGC_CONTROL_CAL);

	if (!ath9k_hw_wait(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL, 0)) {
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"offset calibration failed to complete in 1ms; "
			"noisy environment?\n");
		return false;
	}

	if (AR_SREV_9285(ah) && AR_SREV_9285_11_OR_LATER(ah))
		ath9k_hw_9285_pa_cal(ah);

	REG_WRITE(ah, AR_PHY_AGC_CONTROL,
		  REG_READ(ah, AR_PHY_AGC_CONTROL) |
		  AR_PHY_AGC_CONTROL_NF);

	ahp->ah_cal_list = ahp->ah_cal_list_last = ahp->ah_cal_list_curr = NULL;

	if (AR_SREV_9100(ah) || AR_SREV_9160_10_OR_LATER(ah)) {
		if (ath9k_hw_iscal_supported(ah, chan, ADC_GAIN_CAL)) {
			INIT_CAL(&ahp->ah_adcGainCalData);
			INSERT_CAL(ahp, &ahp->ah_adcGainCalData);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"enabling ADC Gain Calibration.\n");
		}
		if (ath9k_hw_iscal_supported(ah, chan, ADC_DC_CAL)) {
			INIT_CAL(&ahp->ah_adcDcCalData);
			INSERT_CAL(ahp, &ahp->ah_adcDcCalData);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"enabling ADC DC Calibration.\n");
		}
		if (ath9k_hw_iscal_supported(ah, chan, IQ_MISMATCH_CAL)) {
			INIT_CAL(&ahp->ah_iqCalData);
			INSERT_CAL(ahp, &ahp->ah_iqCalData);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"enabling IQ Calibration.\n");
		}

		ahp->ah_cal_list_curr = ahp->ah_cal_list;

		if (ahp->ah_cal_list_curr)
			ath9k_hw_reset_calibration(ah, ahp->ah_cal_list_curr);
	}

	ichan->CalValid = 0;

	return true;
}

const struct hal_percal_data iq_cal_multi_sample = {
	IQ_MISMATCH_CAL,
	MAX_CAL_SAMPLES,
	PER_MIN_LOG_COUNT,
	ath9k_hw_iqcal_collect,
	ath9k_hw_iqcalibrate
};
const struct hal_percal_data iq_cal_single_sample = {
	IQ_MISMATCH_CAL,
	MIN_CAL_SAMPLES,
	PER_MAX_LOG_COUNT,
	ath9k_hw_iqcal_collect,
	ath9k_hw_iqcalibrate
};
const struct hal_percal_data adc_gain_cal_multi_sample = {
	ADC_GAIN_CAL,
	MAX_CAL_SAMPLES,
	PER_MIN_LOG_COUNT,
	ath9k_hw_adc_gaincal_collect,
	ath9k_hw_adc_gaincal_calibrate
};
const struct hal_percal_data adc_gain_cal_single_sample = {
	ADC_GAIN_CAL,
	MIN_CAL_SAMPLES,
	PER_MAX_LOG_COUNT,
	ath9k_hw_adc_gaincal_collect,
	ath9k_hw_adc_gaincal_calibrate
};
const struct hal_percal_data adc_dc_cal_multi_sample = {
	ADC_DC_CAL,
	MAX_CAL_SAMPLES,
	PER_MIN_LOG_COUNT,
	ath9k_hw_adc_dccal_collect,
	ath9k_hw_adc_dccal_calibrate
};
const struct hal_percal_data adc_dc_cal_single_sample = {
	ADC_DC_CAL,
	MIN_CAL_SAMPLES,
	PER_MAX_LOG_COUNT,
	ath9k_hw_adc_dccal_collect,
	ath9k_hw_adc_dccal_calibrate
};
const struct hal_percal_data adc_init_dc_cal = {
	ADC_DC_INIT_CAL,
	MIN_CAL_SAMPLES,
	INIT_LOG_COUNT,
	ath9k_hw_adc_dccal_collect,
	ath9k_hw_adc_dccal_calibrate
};
