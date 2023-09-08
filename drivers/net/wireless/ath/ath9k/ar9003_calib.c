/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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
#include "ar9003_phy.h"
#include "ar9003_rtt.h"
#include "ar9003_mci.h"

#define MAX_MEASUREMENT	MAX_IQCAL_MEASUREMENT
#define MAX_MAG_DELTA	11
#define MAX_PHS_DELTA	10
#define MAXIQCAL        3

struct coeff {
	int mag_coeff[AR9300_MAX_CHAINS][MAX_MEASUREMENT][MAXIQCAL];
	int phs_coeff[AR9300_MAX_CHAINS][MAX_MEASUREMENT][MAXIQCAL];
	int iqc_coeff[2];
};

enum ar9003_cal_types {
	IQ_MISMATCH_CAL = BIT(0),
};

static void ar9003_hw_setup_calibration(struct ath_hw *ah,
					struct ath9k_cal_list *currCal)
{
	struct ath_common *common = ath9k_hw_common(ah);

	/* Select calibration to run */
	switch (currCal->calData->calType) {
	case IQ_MISMATCH_CAL:
		/*
		 * Start calibration with
		 * 2^(INIT_IQCAL_LOG_COUNT_MAX+1) samples
		 */
		REG_RMW_FIELD(ah, AR_PHY_TIMING4,
			      AR_PHY_TIMING4_IQCAL_LOG_COUNT_MAX,
			      currCal->calData->calCountMax);
		REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_IQ);

		ath_dbg(common, CALIBRATE,
			"starting IQ Mismatch Calibration\n");

		/* Kick-off cal */
		REG_SET_BIT(ah, AR_PHY_TIMING4, AR_PHY_TIMING4_DO_CAL);
		break;
	default:
		ath_err(common, "Invalid calibration type\n");
		break;
	}
}

/*
 * Generic calibration routine.
 * Recalibrate the lower PHY chips to account for temperature/environment
 * changes.
 */
static bool ar9003_hw_per_calibration(struct ath_hw *ah,
				      struct ath9k_channel *ichan,
				      u8 rxchainmask,
				      struct ath9k_cal_list *currCal)
{
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	const struct ath9k_percal_data *cur_caldata = currCal->calData;

	/* Calibration in progress. */
	if (currCal->calState == CAL_RUNNING) {
		/* Check to see if it has finished. */
		if (REG_READ(ah, AR_PHY_TIMING4) & AR_PHY_TIMING4_DO_CAL)
			return false;

		/*
		* Accumulate cal measures for active chains
		*/
		cur_caldata->calCollect(ah);
		ah->cal_samples++;

		if (ah->cal_samples >= cur_caldata->calNumSamples) {
			unsigned int i, numChains = 0;
			for (i = 0; i < AR9300_MAX_CHAINS; i++) {
				if (rxchainmask & (1 << i))
					numChains++;
			}

			/*
			* Process accumulated data
			*/
			cur_caldata->calPostProc(ah, numChains);

			/* Calibration has finished. */
			caldata->CalValid |= cur_caldata->calType;
			currCal->calState = CAL_DONE;
			return true;
		} else {
			/*
			 * Set-up collection of another sub-sample until we
			 * get desired number
			 */
			ar9003_hw_setup_calibration(ah, currCal);
		}
	} else if (!(caldata->CalValid & cur_caldata->calType)) {
		/* If current cal is marked invalid in channel, kick it off */
		ath9k_hw_reset_calibration(ah, currCal);
	}

	return false;
}

static int ar9003_hw_calibrate(struct ath_hw *ah, struct ath9k_channel *chan,
			       u8 rxchainmask, bool longcal)
{
	bool iscaldone = true;
	struct ath9k_cal_list *currCal = ah->cal_list_curr;
	int ret;

	/*
	 * For given calibration:
	 * 1. Call generic cal routine
	 * 2. When this cal is done (isCalDone) if we have more cals waiting
	 *    (eg after reset), mask this to upper layers by not propagating
	 *    isCalDone if it is set to TRUE.
	 *    Instead, change isCalDone to FALSE and setup the waiting cal(s)
	 *    to be run.
	 */
	if (currCal &&
	    (currCal->calState == CAL_RUNNING ||
	     currCal->calState == CAL_WAITING)) {
		iscaldone = ar9003_hw_per_calibration(ah, chan,
						      rxchainmask, currCal);
		if (iscaldone) {
			ah->cal_list_curr = currCal = currCal->calNext;

			if (currCal->calState == CAL_WAITING) {
				iscaldone = false;
				ath9k_hw_reset_calibration(ah, currCal);
			}
		}
	}

	/*
	 * Do NF cal only at longer intervals. Get the value from
	 * the previous NF cal and update history buffer.
	 */
	if (longcal && ath9k_hw_getnf(ah, chan)) {
		/*
		 * Load the NF from history buffer of the current channel.
		 * NF is slow time-variant, so it is OK to use a historical
		 * value.
		 */
		ret = ath9k_hw_loadnf(ah, ah->curchan);
		if (ret < 0)
			return ret;

		/* start NF calibration, without updating BB NF register */
		ath9k_hw_start_nfcal(ah, false);
	}

	return iscaldone;
}

static void ar9003_hw_iqcal_collect(struct ath_hw *ah)
{
	int i;

	/* Accumulate IQ cal measures for active chains */
	for (i = 0; i < AR9300_MAX_CHAINS; i++) {
		if (ah->txchainmask & BIT(i)) {
			ah->totalPowerMeasI[i] +=
				REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
			ah->totalPowerMeasQ[i] +=
				REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
			ah->totalIqCorrMeas[i] +=
				(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
			ath_dbg(ath9k_hw_common(ah), CALIBRATE,
				"%d: Chn %d pmi=0x%08x;pmq=0x%08x;iqcm=0x%08x;\n",
				ah->cal_samples, i, ah->totalPowerMeasI[i],
				ah->totalPowerMeasQ[i],
				ah->totalIqCorrMeas[i]);
		}
	}
}

static void ar9003_hw_iqcalibrate(struct ath_hw *ah, u8 numChains)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 powerMeasQ, powerMeasI, iqCorrMeas;
	u32 qCoffDenom, iCoffDenom;
	int32_t qCoff, iCoff;
	int iqCorrNeg, i;
	static const u_int32_t offset_array[3] = {
		AR_PHY_RX_IQCAL_CORR_B0,
		AR_PHY_RX_IQCAL_CORR_B1,
		AR_PHY_RX_IQCAL_CORR_B2,
	};

	for (i = 0; i < numChains; i++) {
		powerMeasI = ah->totalPowerMeasI[i];
		powerMeasQ = ah->totalPowerMeasQ[i];
		iqCorrMeas = ah->totalIqCorrMeas[i];

		ath_dbg(common, CALIBRATE,
			"Starting IQ Cal and Correction for Chain %d\n", i);

		ath_dbg(common, CALIBRATE,
			"Original: Chn %d iq_corr_meas = 0x%08x\n",
			i, ah->totalIqCorrMeas[i]);

		iqCorrNeg = 0;

		if (iqCorrMeas > 0x80000000) {
			iqCorrMeas = (0xffffffff - iqCorrMeas) + 1;
			iqCorrNeg = 1;
		}

		ath_dbg(common, CALIBRATE, "Chn %d pwr_meas_i = 0x%08x\n",
			i, powerMeasI);
		ath_dbg(common, CALIBRATE, "Chn %d pwr_meas_q = 0x%08x\n",
			i, powerMeasQ);
		ath_dbg(common, CALIBRATE, "iqCorrNeg is 0x%08x\n", iqCorrNeg);

		iCoffDenom = (powerMeasI / 2 + powerMeasQ / 2) / 256;
		qCoffDenom = powerMeasQ / 64;

		if ((iCoffDenom != 0) && (qCoffDenom != 0)) {
			iCoff = iqCorrMeas / iCoffDenom;
			qCoff = powerMeasI / qCoffDenom - 64;
			ath_dbg(common, CALIBRATE, "Chn %d iCoff = 0x%08x\n",
				i, iCoff);
			ath_dbg(common, CALIBRATE, "Chn %d qCoff = 0x%08x\n",
				i, qCoff);

			/* Force bounds on iCoff */
			if (iCoff >= 63)
				iCoff = 63;
			else if (iCoff <= -63)
				iCoff = -63;

			/* Negate iCoff if iqCorrNeg == 0 */
			if (iqCorrNeg == 0x0)
				iCoff = -iCoff;

			/* Force bounds on qCoff */
			if (qCoff >= 63)
				qCoff = 63;
			else if (qCoff <= -63)
				qCoff = -63;

			iCoff = iCoff & 0x7f;
			qCoff = qCoff & 0x7f;

			ath_dbg(common, CALIBRATE,
				"Chn %d : iCoff = 0x%x  qCoff = 0x%x\n",
				i, iCoff, qCoff);
			ath_dbg(common, CALIBRATE,
				"Register offset (0x%04x) before update = 0x%x\n",
				offset_array[i],
				REG_READ(ah, offset_array[i]));

			if (AR_SREV_9565(ah) &&
			    (iCoff == 63 || qCoff == 63 ||
			     iCoff == -63 || qCoff == -63))
				return;

			REG_RMW_FIELD(ah, offset_array[i],
				      AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF,
				      iCoff);
			REG_RMW_FIELD(ah, offset_array[i],
				      AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF,
				      qCoff);
			ath_dbg(common, CALIBRATE,
				"Register offset (0x%04x) QI COFF (bitfields 0x%08x) after update = 0x%x\n",
				offset_array[i],
				AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF,
				REG_READ(ah, offset_array[i]));
			ath_dbg(common, CALIBRATE,
				"Register offset (0x%04x) QQ COFF (bitfields 0x%08x) after update = 0x%x\n",
				offset_array[i],
				AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF,
				REG_READ(ah, offset_array[i]));

			ath_dbg(common, CALIBRATE,
				"IQ Cal and Correction done for Chain %d\n", i);
		}
	}

	REG_SET_BIT(ah, AR_PHY_RX_IQCAL_CORR_B0,
		    AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE);
	ath_dbg(common, CALIBRATE,
		"IQ Cal and Correction (offset 0x%04x) enabled (bit position 0x%08x). New Value 0x%08x\n",
		(unsigned) (AR_PHY_RX_IQCAL_CORR_B0),
		AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE,
		REG_READ(ah, AR_PHY_RX_IQCAL_CORR_B0));
}

static const struct ath9k_percal_data iq_cal_single_sample = {
	IQ_MISMATCH_CAL,
	MIN_CAL_SAMPLES,
	PER_MAX_LOG_COUNT,
	ar9003_hw_iqcal_collect,
	ar9003_hw_iqcalibrate
};

static void ar9003_hw_init_cal_settings(struct ath_hw *ah)
{
	ah->iq_caldata.calData = &iq_cal_single_sample;

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		ah->enabled_cals |= TX_IQ_CAL;
		if (AR_SREV_9485_OR_LATER(ah) && !AR_SREV_9340(ah))
			ah->enabled_cals |= TX_IQ_ON_AGC_CAL;
	}

	ah->supp_cals = IQ_MISMATCH_CAL;
}

#define OFF_UPPER_LT 24
#define OFF_LOWER_LT 7

static bool ar9003_hw_dynamic_osdac_selection(struct ath_hw *ah,
					      bool txiqcal_done)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int ch0_done, osdac_ch0, dc_off_ch0_i1, dc_off_ch0_q1, dc_off_ch0_i2,
		dc_off_ch0_q2, dc_off_ch0_i3, dc_off_ch0_q3;
	int ch1_done, osdac_ch1, dc_off_ch1_i1, dc_off_ch1_q1, dc_off_ch1_i2,
		dc_off_ch1_q2, dc_off_ch1_i3, dc_off_ch1_q3;
	int ch2_done, osdac_ch2, dc_off_ch2_i1, dc_off_ch2_q1, dc_off_ch2_i2,
		dc_off_ch2_q2, dc_off_ch2_i3, dc_off_ch2_q3;
	bool status;
	u32 temp, val;

	/*
	 * Clear offset and IQ calibration, run AGC cal.
	 */
	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL(ah),
		    AR_PHY_AGC_CONTROL_OFFSET_CAL);
	REG_CLR_BIT(ah, AR_PHY_TX_IQCAL_CONTROL_0(ah),
		    AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL);
	REG_WRITE(ah, AR_PHY_AGC_CONTROL(ah),
		  REG_READ(ah, AR_PHY_AGC_CONTROL(ah)) | AR_PHY_AGC_CONTROL_CAL);

	status = ath9k_hw_wait(ah, AR_PHY_AGC_CONTROL(ah),
			       AR_PHY_AGC_CONTROL_CAL,
			       0, AH_WAIT_TIMEOUT);
	if (!status) {
		ath_dbg(common, CALIBRATE,
			"AGC cal without offset cal failed to complete in 1ms");
		return false;
	}

	/*
	 * Allow only offset calibration and disable the others
	 * (Carrier Leak calibration, TX Filter calibration and
	 *  Peak Detector offset calibration).
	 */
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL(ah),
		    AR_PHY_AGC_CONTROL_OFFSET_CAL);
	REG_CLR_BIT(ah, AR_PHY_CL_CAL_CTL,
		    AR_PHY_CL_CAL_ENABLE);
	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL(ah),
		    AR_PHY_AGC_CONTROL_FLTR_CAL);
	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL(ah),
		    AR_PHY_AGC_CONTROL_PKDET_CAL);

	ch0_done = 0;
	ch1_done = 0;
	ch2_done = 0;

	while ((ch0_done == 0) || (ch1_done == 0) || (ch2_done == 0)) {
		osdac_ch0 = (REG_READ(ah, AR_PHY_65NM_CH0_BB1) >> 30) & 0x3;
		osdac_ch1 = (REG_READ(ah, AR_PHY_65NM_CH1_BB1) >> 30) & 0x3;
		osdac_ch2 = (REG_READ(ah, AR_PHY_65NM_CH2_BB1) >> 30) & 0x3;

		REG_SET_BIT(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

		REG_WRITE(ah, AR_PHY_AGC_CONTROL(ah),
			  REG_READ(ah, AR_PHY_AGC_CONTROL(ah)) | AR_PHY_AGC_CONTROL_CAL);

		status = ath9k_hw_wait(ah, AR_PHY_AGC_CONTROL(ah),
				       AR_PHY_AGC_CONTROL_CAL,
				       0, AH_WAIT_TIMEOUT);
		if (!status) {
			ath_dbg(common, CALIBRATE,
				"DC offset cal failed to complete in 1ms");
			return false;
		}

		REG_CLR_BIT(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

		/*
		 * High gain.
		 */
		REG_WRITE(ah, AR_PHY_65NM_CH0_BB3,
			  ((REG_READ(ah, AR_PHY_65NM_CH0_BB3) & 0xfffffcff) | (1 << 8)));
		REG_WRITE(ah, AR_PHY_65NM_CH1_BB3,
			  ((REG_READ(ah, AR_PHY_65NM_CH1_BB3) & 0xfffffcff) | (1 << 8)));
		REG_WRITE(ah, AR_PHY_65NM_CH2_BB3,
			  ((REG_READ(ah, AR_PHY_65NM_CH2_BB3) & 0xfffffcff) | (1 << 8)));

		temp = REG_READ(ah, AR_PHY_65NM_CH0_BB3);
		dc_off_ch0_i1 = (temp >> 26) & 0x1f;
		dc_off_ch0_q1 = (temp >> 21) & 0x1f;

		temp = REG_READ(ah, AR_PHY_65NM_CH1_BB3);
		dc_off_ch1_i1 = (temp >> 26) & 0x1f;
		dc_off_ch1_q1 = (temp >> 21) & 0x1f;

		temp = REG_READ(ah, AR_PHY_65NM_CH2_BB3);
		dc_off_ch2_i1 = (temp >> 26) & 0x1f;
		dc_off_ch2_q1 = (temp >> 21) & 0x1f;

		/*
		 * Low gain.
		 */
		REG_WRITE(ah, AR_PHY_65NM_CH0_BB3,
			  ((REG_READ(ah, AR_PHY_65NM_CH0_BB3) & 0xfffffcff) | (2 << 8)));
		REG_WRITE(ah, AR_PHY_65NM_CH1_BB3,
			  ((REG_READ(ah, AR_PHY_65NM_CH1_BB3) & 0xfffffcff) | (2 << 8)));
		REG_WRITE(ah, AR_PHY_65NM_CH2_BB3,
			  ((REG_READ(ah, AR_PHY_65NM_CH2_BB3) & 0xfffffcff) | (2 << 8)));

		temp = REG_READ(ah, AR_PHY_65NM_CH0_BB3);
		dc_off_ch0_i2 = (temp >> 26) & 0x1f;
		dc_off_ch0_q2 = (temp >> 21) & 0x1f;

		temp = REG_READ(ah, AR_PHY_65NM_CH1_BB3);
		dc_off_ch1_i2 = (temp >> 26) & 0x1f;
		dc_off_ch1_q2 = (temp >> 21) & 0x1f;

		temp = REG_READ(ah, AR_PHY_65NM_CH2_BB3);
		dc_off_ch2_i2 = (temp >> 26) & 0x1f;
		dc_off_ch2_q2 = (temp >> 21) & 0x1f;

		/*
		 * Loopback.
		 */
		REG_WRITE(ah, AR_PHY_65NM_CH0_BB3,
			  ((REG_READ(ah, AR_PHY_65NM_CH0_BB3) & 0xfffffcff) | (3 << 8)));
		REG_WRITE(ah, AR_PHY_65NM_CH1_BB3,
			  ((REG_READ(ah, AR_PHY_65NM_CH1_BB3) & 0xfffffcff) | (3 << 8)));
		REG_WRITE(ah, AR_PHY_65NM_CH2_BB3,
			  ((REG_READ(ah, AR_PHY_65NM_CH2_BB3) & 0xfffffcff) | (3 << 8)));

		temp = REG_READ(ah, AR_PHY_65NM_CH0_BB3);
		dc_off_ch0_i3 = (temp >> 26) & 0x1f;
		dc_off_ch0_q3 = (temp >> 21) & 0x1f;

		temp = REG_READ(ah, AR_PHY_65NM_CH1_BB3);
		dc_off_ch1_i3 = (temp >> 26) & 0x1f;
		dc_off_ch1_q3 = (temp >> 21) & 0x1f;

		temp = REG_READ(ah, AR_PHY_65NM_CH2_BB3);
		dc_off_ch2_i3 = (temp >> 26) & 0x1f;
		dc_off_ch2_q3 = (temp >> 21) & 0x1f;

		if ((dc_off_ch0_i1 > OFF_UPPER_LT) || (dc_off_ch0_i1 < OFF_LOWER_LT) ||
		    (dc_off_ch0_i2 > OFF_UPPER_LT) || (dc_off_ch0_i2 < OFF_LOWER_LT) ||
		    (dc_off_ch0_i3 > OFF_UPPER_LT) || (dc_off_ch0_i3 < OFF_LOWER_LT) ||
		    (dc_off_ch0_q1 > OFF_UPPER_LT) || (dc_off_ch0_q1 < OFF_LOWER_LT) ||
		    (dc_off_ch0_q2 > OFF_UPPER_LT) || (dc_off_ch0_q2 < OFF_LOWER_LT) ||
		    (dc_off_ch0_q3 > OFF_UPPER_LT) || (dc_off_ch0_q3 < OFF_LOWER_LT)) {
			if (osdac_ch0 == 3) {
				ch0_done = 1;
			} else {
				osdac_ch0++;

				val = REG_READ(ah, AR_PHY_65NM_CH0_BB1) & 0x3fffffff;
				val |= (osdac_ch0 << 30);
				REG_WRITE(ah, AR_PHY_65NM_CH0_BB1, val);

				ch0_done = 0;
			}
		} else {
			ch0_done = 1;
		}

		if ((dc_off_ch1_i1 > OFF_UPPER_LT) || (dc_off_ch1_i1 < OFF_LOWER_LT) ||
		    (dc_off_ch1_i2 > OFF_UPPER_LT) || (dc_off_ch1_i2 < OFF_LOWER_LT) ||
		    (dc_off_ch1_i3 > OFF_UPPER_LT) || (dc_off_ch1_i3 < OFF_LOWER_LT) ||
		    (dc_off_ch1_q1 > OFF_UPPER_LT) || (dc_off_ch1_q1 < OFF_LOWER_LT) ||
		    (dc_off_ch1_q2 > OFF_UPPER_LT) || (dc_off_ch1_q2 < OFF_LOWER_LT) ||
		    (dc_off_ch1_q3 > OFF_UPPER_LT) || (dc_off_ch1_q3 < OFF_LOWER_LT)) {
			if (osdac_ch1 == 3) {
				ch1_done = 1;
			} else {
				osdac_ch1++;

				val = REG_READ(ah, AR_PHY_65NM_CH1_BB1) & 0x3fffffff;
				val |= (osdac_ch1 << 30);
				REG_WRITE(ah, AR_PHY_65NM_CH1_BB1, val);

				ch1_done = 0;
			}
		} else {
			ch1_done = 1;
		}

		if ((dc_off_ch2_i1 > OFF_UPPER_LT) || (dc_off_ch2_i1 < OFF_LOWER_LT) ||
		    (dc_off_ch2_i2 > OFF_UPPER_LT) || (dc_off_ch2_i2 < OFF_LOWER_LT) ||
		    (dc_off_ch2_i3 > OFF_UPPER_LT) || (dc_off_ch2_i3 < OFF_LOWER_LT) ||
		    (dc_off_ch2_q1 > OFF_UPPER_LT) || (dc_off_ch2_q1 < OFF_LOWER_LT) ||
		    (dc_off_ch2_q2 > OFF_UPPER_LT) || (dc_off_ch2_q2 < OFF_LOWER_LT) ||
		    (dc_off_ch2_q3 > OFF_UPPER_LT) || (dc_off_ch2_q3 < OFF_LOWER_LT)) {
			if (osdac_ch2 == 3) {
				ch2_done = 1;
			} else {
				osdac_ch2++;

				val = REG_READ(ah, AR_PHY_65NM_CH2_BB1) & 0x3fffffff;
				val |= (osdac_ch2 << 30);
				REG_WRITE(ah, AR_PHY_65NM_CH2_BB1, val);

				ch2_done = 0;
			}
		} else {
			ch2_done = 1;
		}
	}

	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL(ah),
		    AR_PHY_AGC_CONTROL_OFFSET_CAL);
	REG_SET_BIT(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

	/*
	 * We don't need to check txiqcal_done here since it is always
	 * set for AR9550.
	 */
	REG_SET_BIT(ah, AR_PHY_TX_IQCAL_CONTROL_0(ah),
		    AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL);

	return true;
}

/*
 * solve 4x4 linear equation used in loopback iq cal.
 */
static bool ar9003_hw_solve_iq_cal(struct ath_hw *ah,
				   s32 sin_2phi_1,
				   s32 cos_2phi_1,
				   s32 sin_2phi_2,
				   s32 cos_2phi_2,
				   s32 mag_a0_d0,
				   s32 phs_a0_d0,
				   s32 mag_a1_d0,
				   s32 phs_a1_d0,
				   s32 solved_eq[])
{
	s32 f1 = cos_2phi_1 - cos_2phi_2,
	    f3 = sin_2phi_1 - sin_2phi_2,
	    f2;
	s32 mag_tx, phs_tx, mag_rx, phs_rx;
	const s32 result_shift = 1 << 15;
	struct ath_common *common = ath9k_hw_common(ah);

	f2 = ((f1 >> 3) * (f1 >> 3) + (f3 >> 3) * (f3 >> 3)) >> 9;

	if (!f2) {
		ath_dbg(common, CALIBRATE, "Divide by 0\n");
		return false;
	}

	/* mag mismatch, tx */
	mag_tx = f1 * (mag_a0_d0  - mag_a1_d0) + f3 * (phs_a0_d0 - phs_a1_d0);
	/* phs mismatch, tx */
	phs_tx = f3 * (-mag_a0_d0 + mag_a1_d0) + f1 * (phs_a0_d0 - phs_a1_d0);

	mag_tx = (mag_tx / f2);
	phs_tx = (phs_tx / f2);

	/* mag mismatch, rx */
	mag_rx = mag_a0_d0 - (cos_2phi_1 * mag_tx + sin_2phi_1 * phs_tx) /
		 result_shift;
	/* phs mismatch, rx */
	phs_rx = phs_a0_d0 + (sin_2phi_1 * mag_tx - cos_2phi_1 * phs_tx) /
		 result_shift;

	solved_eq[0] = mag_tx;
	solved_eq[1] = phs_tx;
	solved_eq[2] = mag_rx;
	solved_eq[3] = phs_rx;

	return true;
}

static s32 ar9003_hw_find_mag_approx(struct ath_hw *ah, s32 in_re, s32 in_im)
{
	s32 abs_i = abs(in_re),
	    abs_q = abs(in_im),
	    max_abs, min_abs;

	if (abs_i > abs_q) {
		max_abs = abs_i;
		min_abs = abs_q;
	} else {
		max_abs = abs_q;
		min_abs = abs_i;
	}

	return max_abs - (max_abs / 32) + (min_abs / 8) + (min_abs / 4);
}

#define DELPT 32

static bool ar9003_hw_calc_iq_corr(struct ath_hw *ah,
				   s32 chain_idx,
				   const s32 iq_res[],
				   s32 iqc_coeff[])
{
	s32 i2_m_q2_a0_d0, i2_p_q2_a0_d0, iq_corr_a0_d0,
	    i2_m_q2_a0_d1, i2_p_q2_a0_d1, iq_corr_a0_d1,
	    i2_m_q2_a1_d0, i2_p_q2_a1_d0, iq_corr_a1_d0,
	    i2_m_q2_a1_d1, i2_p_q2_a1_d1, iq_corr_a1_d1;
	s32 mag_a0_d0, mag_a1_d0, mag_a0_d1, mag_a1_d1,
	    phs_a0_d0, phs_a1_d0, phs_a0_d1, phs_a1_d1,
	    sin_2phi_1, cos_2phi_1,
	    sin_2phi_2, cos_2phi_2;
	s32 mag_tx, phs_tx, mag_rx, phs_rx;
	s32 solved_eq[4], mag_corr_tx, phs_corr_tx, mag_corr_rx, phs_corr_rx,
	    q_q_coff, q_i_coff;
	const s32 res_scale = 1 << 15;
	const s32 delpt_shift = 1 << 8;
	s32 mag1, mag2;
	struct ath_common *common = ath9k_hw_common(ah);

	i2_m_q2_a0_d0 = iq_res[0] & 0xfff;
	i2_p_q2_a0_d0 = (iq_res[0] >> 12) & 0xfff;
	iq_corr_a0_d0 = ((iq_res[0] >> 24) & 0xff) + ((iq_res[1] & 0xf) << 8);

	if (i2_m_q2_a0_d0 > 0x800)
		i2_m_q2_a0_d0 = -((0xfff - i2_m_q2_a0_d0) + 1);

	if (i2_p_q2_a0_d0 > 0x800)
		i2_p_q2_a0_d0 = -((0xfff - i2_p_q2_a0_d0) + 1);

	if (iq_corr_a0_d0 > 0x800)
		iq_corr_a0_d0 = -((0xfff - iq_corr_a0_d0) + 1);

	i2_m_q2_a0_d1 = (iq_res[1] >> 4) & 0xfff;
	i2_p_q2_a0_d1 = (iq_res[2] & 0xfff);
	iq_corr_a0_d1 = (iq_res[2] >> 12) & 0xfff;

	if (i2_m_q2_a0_d1 > 0x800)
		i2_m_q2_a0_d1 = -((0xfff - i2_m_q2_a0_d1) + 1);

	if (iq_corr_a0_d1 > 0x800)
		iq_corr_a0_d1 = -((0xfff - iq_corr_a0_d1) + 1);

	i2_m_q2_a1_d0 = ((iq_res[2] >> 24) & 0xff) + ((iq_res[3] & 0xf) << 8);
	i2_p_q2_a1_d0 = (iq_res[3] >> 4) & 0xfff;
	iq_corr_a1_d0 = iq_res[4] & 0xfff;

	if (i2_m_q2_a1_d0 > 0x800)
		i2_m_q2_a1_d0 = -((0xfff - i2_m_q2_a1_d0) + 1);

	if (i2_p_q2_a1_d0 > 0x800)
		i2_p_q2_a1_d0 = -((0xfff - i2_p_q2_a1_d0) + 1);

	if (iq_corr_a1_d0 > 0x800)
		iq_corr_a1_d0 = -((0xfff - iq_corr_a1_d0) + 1);

	i2_m_q2_a1_d1 = (iq_res[4] >> 12) & 0xfff;
	i2_p_q2_a1_d1 = ((iq_res[4] >> 24) & 0xff) + ((iq_res[5] & 0xf) << 8);
	iq_corr_a1_d1 = (iq_res[5] >> 4) & 0xfff;

	if (i2_m_q2_a1_d1 > 0x800)
		i2_m_q2_a1_d1 = -((0xfff - i2_m_q2_a1_d1) + 1);

	if (i2_p_q2_a1_d1 > 0x800)
		i2_p_q2_a1_d1 = -((0xfff - i2_p_q2_a1_d1) + 1);

	if (iq_corr_a1_d1 > 0x800)
		iq_corr_a1_d1 = -((0xfff - iq_corr_a1_d1) + 1);

	if ((i2_p_q2_a0_d0 == 0) || (i2_p_q2_a0_d1 == 0) ||
	    (i2_p_q2_a1_d0 == 0) || (i2_p_q2_a1_d1 == 0)) {
		ath_dbg(common, CALIBRATE,
			"Divide by 0:\n"
			"a0_d0=%d\n"
			"a0_d1=%d\n"
			"a2_d0=%d\n"
			"a1_d1=%d\n",
			i2_p_q2_a0_d0, i2_p_q2_a0_d1,
			i2_p_q2_a1_d0, i2_p_q2_a1_d1);
		return false;
	}

	if ((i2_p_q2_a0_d0 < 1024) || (i2_p_q2_a0_d0 > 2047) ||
            (i2_p_q2_a1_d0 < 0) || (i2_p_q2_a1_d1 < 0) ||
            (i2_p_q2_a0_d0 <= i2_m_q2_a0_d0) ||
            (i2_p_q2_a0_d0 <= iq_corr_a0_d0) ||
            (i2_p_q2_a0_d1 <= i2_m_q2_a0_d1) ||
            (i2_p_q2_a0_d1 <= iq_corr_a0_d1) ||
            (i2_p_q2_a1_d0 <= i2_m_q2_a1_d0) ||
            (i2_p_q2_a1_d0 <= iq_corr_a1_d0) ||
            (i2_p_q2_a1_d1 <= i2_m_q2_a1_d1) ||
            (i2_p_q2_a1_d1 <= iq_corr_a1_d1)) {
		return false;
	}

	mag_a0_d0 = (i2_m_q2_a0_d0 * res_scale) / i2_p_q2_a0_d0;
	phs_a0_d0 = (iq_corr_a0_d0 * res_scale) / i2_p_q2_a0_d0;

	mag_a0_d1 = (i2_m_q2_a0_d1 * res_scale) / i2_p_q2_a0_d1;
	phs_a0_d1 = (iq_corr_a0_d1 * res_scale) / i2_p_q2_a0_d1;

	mag_a1_d0 = (i2_m_q2_a1_d0 * res_scale) / i2_p_q2_a1_d0;
	phs_a1_d0 = (iq_corr_a1_d0 * res_scale) / i2_p_q2_a1_d0;

	mag_a1_d1 = (i2_m_q2_a1_d1 * res_scale) / i2_p_q2_a1_d1;
	phs_a1_d1 = (iq_corr_a1_d1 * res_scale) / i2_p_q2_a1_d1;

	/* w/o analog phase shift */
	sin_2phi_1 = (((mag_a0_d0 - mag_a0_d1) * delpt_shift) / DELPT);
	/* w/o analog phase shift */
	cos_2phi_1 = (((phs_a0_d1 - phs_a0_d0) * delpt_shift) / DELPT);
	/* w/  analog phase shift */
	sin_2phi_2 = (((mag_a1_d0 - mag_a1_d1) * delpt_shift) / DELPT);
	/* w/  analog phase shift */
	cos_2phi_2 = (((phs_a1_d1 - phs_a1_d0) * delpt_shift) / DELPT);

	/*
	 * force sin^2 + cos^2 = 1;
	 * find magnitude by approximation
	 */
	mag1 = ar9003_hw_find_mag_approx(ah, cos_2phi_1, sin_2phi_1);
	mag2 = ar9003_hw_find_mag_approx(ah, cos_2phi_2, sin_2phi_2);

	if ((mag1 == 0) || (mag2 == 0)) {
		ath_dbg(common, CALIBRATE, "Divide by 0: mag1=%d, mag2=%d\n",
			mag1, mag2);
		return false;
	}

	/* normalization sin and cos by mag */
	sin_2phi_1 = (sin_2phi_1 * res_scale / mag1);
	cos_2phi_1 = (cos_2phi_1 * res_scale / mag1);
	sin_2phi_2 = (sin_2phi_2 * res_scale / mag2);
	cos_2phi_2 = (cos_2phi_2 * res_scale / mag2);

	/* calculate IQ mismatch */
	if (!ar9003_hw_solve_iq_cal(ah,
			     sin_2phi_1, cos_2phi_1,
			     sin_2phi_2, cos_2phi_2,
			     mag_a0_d0, phs_a0_d0,
			     mag_a1_d0,
			     phs_a1_d0, solved_eq)) {
		ath_dbg(common, CALIBRATE,
			"Call to ar9003_hw_solve_iq_cal() failed\n");
		return false;
	}

	mag_tx = solved_eq[0];
	phs_tx = solved_eq[1];
	mag_rx = solved_eq[2];
	phs_rx = solved_eq[3];

	ath_dbg(common, CALIBRATE,
		"chain %d: mag mismatch=%d phase mismatch=%d\n",
		chain_idx, mag_tx/res_scale, phs_tx/res_scale);

	if (res_scale == mag_tx) {
		ath_dbg(common, CALIBRATE,
			"Divide by 0: mag_tx=%d, res_scale=%d\n",
			mag_tx, res_scale);
		return false;
	}

	/* calculate and quantize Tx IQ correction factor */
	mag_corr_tx = (mag_tx * res_scale) / (res_scale - mag_tx);
	phs_corr_tx = -phs_tx;

	q_q_coff = (mag_corr_tx * 128 / res_scale);
	q_i_coff = (phs_corr_tx * 256 / res_scale);

	ath_dbg(common, CALIBRATE, "tx chain %d: mag corr=%d  phase corr=%d\n",
		chain_idx, q_q_coff, q_i_coff);

	if (q_i_coff < -63)
		q_i_coff = -63;
	if (q_i_coff > 63)
		q_i_coff = 63;
	if (q_q_coff < -63)
		q_q_coff = -63;
	if (q_q_coff > 63)
		q_q_coff = 63;

	iqc_coeff[0] = (q_q_coff * 128) + (0x7f & q_i_coff);

	ath_dbg(common, CALIBRATE, "tx chain %d: iq corr coeff=%x\n",
		chain_idx, iqc_coeff[0]);

	if (-mag_rx == res_scale) {
		ath_dbg(common, CALIBRATE,
			"Divide by 0: mag_rx=%d, res_scale=%d\n",
			mag_rx, res_scale);
		return false;
	}

	/* calculate and quantize Rx IQ correction factors */
	mag_corr_rx = (-mag_rx * res_scale) / (res_scale + mag_rx);
	phs_corr_rx = -phs_rx;

	q_q_coff = (mag_corr_rx * 128 / res_scale);
	q_i_coff = (phs_corr_rx * 256 / res_scale);

	ath_dbg(common, CALIBRATE, "rx chain %d: mag corr=%d  phase corr=%d\n",
		chain_idx, q_q_coff, q_i_coff);

	if (q_i_coff < -63)
		q_i_coff = -63;
	if (q_i_coff > 63)
		q_i_coff = 63;
	if (q_q_coff < -63)
		q_q_coff = -63;
	if (q_q_coff > 63)
		q_q_coff = 63;

	iqc_coeff[1] = (q_q_coff * 128) + (0x7f & q_i_coff);

	ath_dbg(common, CALIBRATE, "rx chain %d: iq corr coeff=%x\n",
		chain_idx, iqc_coeff[1]);

	return true;
}

static void ar9003_hw_detect_outlier(int mp_coeff[][MAXIQCAL],
				     int nmeasurement,
				     int max_delta)
{
	int mp_max = -64, max_idx = 0;
	int mp_min = 63, min_idx = 0;
	int mp_avg = 0, i, outlier_idx = 0, mp_count = 0;

	/* find min/max mismatch across all calibrated gains */
	for (i = 0; i < nmeasurement; i++) {
		if (mp_coeff[i][0] > mp_max) {
			mp_max = mp_coeff[i][0];
			max_idx = i;
		} else if (mp_coeff[i][0] < mp_min) {
			mp_min = mp_coeff[i][0];
			min_idx = i;
		}
	}

	/* find average (exclude max abs value) */
	for (i = 0; i < nmeasurement; i++) {
		if ((abs(mp_coeff[i][0]) < abs(mp_max)) ||
		    (abs(mp_coeff[i][0]) < abs(mp_min))) {
			mp_avg += mp_coeff[i][0];
			mp_count++;
		}
	}

	/*
	 * finding mean magnitude/phase if possible, otherwise
	 * just use the last value as the mean
	 */
	if (mp_count)
		mp_avg /= mp_count;
	else
		mp_avg = mp_coeff[nmeasurement - 1][0];

	/* detect outlier */
	if (abs(mp_max - mp_min) > max_delta) {
		if (abs(mp_max - mp_avg) > abs(mp_min - mp_avg))
			outlier_idx = max_idx;
		else
			outlier_idx = min_idx;

		mp_coeff[outlier_idx][0] = mp_avg;
	}
}

static void ar9003_hw_tx_iq_cal_outlier_detection(struct ath_hw *ah,
						  struct coeff *coeff,
						  bool is_reusable)
{
	int i, im, nmeasurement;
	int magnitude, phase;
	u32 tx_corr_coeff[MAX_MEASUREMENT][AR9300_MAX_CHAINS];
	struct ath9k_hw_cal_data *caldata = ah->caldata;

	memset(tx_corr_coeff, 0, sizeof(tx_corr_coeff));
	for (i = 0; i < MAX_MEASUREMENT / 2; i++) {
		tx_corr_coeff[i * 2][0] = tx_corr_coeff[(i * 2) + 1][0] =
					AR_PHY_TX_IQCAL_CORR_COEFF_B0(ah, i);
		if (!AR_SREV_9485(ah)) {
			tx_corr_coeff[i * 2][1] =
			tx_corr_coeff[(i * 2) + 1][1] =
					AR_PHY_TX_IQCAL_CORR_COEFF_B1(i);

			tx_corr_coeff[i * 2][2] =
			tx_corr_coeff[(i * 2) + 1][2] =
					AR_PHY_TX_IQCAL_CORR_COEFF_B2(i);
		}
	}

	/* Load the average of 2 passes */
	for (i = 0; i < AR9300_MAX_CHAINS; i++) {
		if (!(ah->txchainmask & (1 << i)))
			continue;
		nmeasurement = REG_READ_FIELD(ah,
				AR_PHY_TX_IQCAL_STATUS_B0(ah),
				AR_PHY_CALIBRATED_GAINS_0);

		if (nmeasurement > MAX_MEASUREMENT)
			nmeasurement = MAX_MEASUREMENT;

		/*
		 * Skip normal outlier detection for AR9550.
		 */
		if (!AR_SREV_9550(ah)) {
			/* detect outlier only if nmeasurement > 1 */
			if (nmeasurement > 1) {
				/* Detect magnitude outlier */
				ar9003_hw_detect_outlier(coeff->mag_coeff[i],
							 nmeasurement,
							 MAX_MAG_DELTA);

				/* Detect phase outlier */
				ar9003_hw_detect_outlier(coeff->phs_coeff[i],
							 nmeasurement,
							 MAX_PHS_DELTA);
			}
		}

		for (im = 0; im < nmeasurement; im++) {
			magnitude = coeff->mag_coeff[i][im][0];
			phase = coeff->phs_coeff[i][im][0];

			coeff->iqc_coeff[0] =
				(phase & 0x7f) | ((magnitude & 0x7f) << 7);

			if ((im % 2) == 0)
				REG_RMW_FIELD(ah, tx_corr_coeff[im][i],
					AR_PHY_TX_IQCAL_CORR_COEFF_00_COEFF_TABLE,
					coeff->iqc_coeff[0]);
			else
				REG_RMW_FIELD(ah, tx_corr_coeff[im][i],
					AR_PHY_TX_IQCAL_CORR_COEFF_01_COEFF_TABLE,
					coeff->iqc_coeff[0]);

			if (caldata)
				caldata->tx_corr_coeff[im][i] =
					coeff->iqc_coeff[0];
		}
		if (caldata)
			caldata->num_measures[i] = nmeasurement;
	}

	REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_3,
		      AR_PHY_TX_IQCAL_CONTROL_3_IQCORR_EN, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_RX_IQCAL_CORR_B0,
		      AR_PHY_RX_IQCAL_CORR_B0_LOOPBACK_IQCORR_EN, 0x1);

	if (caldata) {
		if (is_reusable)
			set_bit(TXIQCAL_DONE, &caldata->cal_flags);
		else
			clear_bit(TXIQCAL_DONE, &caldata->cal_flags);
	}

	return;
}

static bool ar9003_hw_tx_iq_cal_run(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u8 tx_gain_forced;

	tx_gain_forced = REG_READ_FIELD(ah, AR_PHY_TX_FORCED_GAIN,
					AR_PHY_TXGAIN_FORCE);
	if (tx_gain_forced)
		REG_RMW_FIELD(ah, AR_PHY_TX_FORCED_GAIN,
			      AR_PHY_TXGAIN_FORCE, 0);

	REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_START(ah),
		      AR_PHY_TX_IQCAL_START_DO_CAL, 1);

	if (!ath9k_hw_wait(ah, AR_PHY_TX_IQCAL_START(ah),
			AR_PHY_TX_IQCAL_START_DO_CAL, 0,
			AH_WAIT_TIMEOUT)) {
		ath_dbg(common, CALIBRATE, "Tx IQ Cal is not completed\n");
		return false;
	}
	return true;
}

static void __ar955x_tx_iq_cal_sort(struct ath_hw *ah,
				    struct coeff *coeff,
				    int i, int nmeasurement)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int im, ix, iy;

	for (im = 0; im < nmeasurement; im++) {
		for (ix = 0; ix < MAXIQCAL - 1; ix++) {
			for (iy = ix + 1; iy <= MAXIQCAL - 1; iy++) {
				if (coeff->mag_coeff[i][im][iy] <
				    coeff->mag_coeff[i][im][ix]) {
					swap(coeff->mag_coeff[i][im][ix],
					     coeff->mag_coeff[i][im][iy]);
				}
				if (coeff->phs_coeff[i][im][iy] <
				    coeff->phs_coeff[i][im][ix]) {
					swap(coeff->phs_coeff[i][im][ix],
					     coeff->phs_coeff[i][im][iy]);
				}
			}
		}
		coeff->mag_coeff[i][im][0] = coeff->mag_coeff[i][im][MAXIQCAL / 2];
		coeff->phs_coeff[i][im][0] = coeff->phs_coeff[i][im][MAXIQCAL / 2];

		ath_dbg(common, CALIBRATE,
			"IQCAL: Median [ch%d][gain%d]: mag = %d phase = %d\n",
			i, im,
			coeff->mag_coeff[i][im][0],
			coeff->phs_coeff[i][im][0]);
	}
}

static bool ar955x_tx_iq_cal_median(struct ath_hw *ah,
				    struct coeff *coeff,
				    int iqcal_idx,
				    int nmeasurement)
{
	int i;

	if ((iqcal_idx + 1) != MAXIQCAL)
		return false;

	for (i = 0; i < AR9300_MAX_CHAINS; i++) {
		__ar955x_tx_iq_cal_sort(ah, coeff, i, nmeasurement);
	}

	return true;
}

static void ar9003_hw_tx_iq_cal_post_proc(struct ath_hw *ah,
					  int iqcal_idx,
					  bool is_reusable)
{
	struct ath_common *common = ath9k_hw_common(ah);
	const u32 txiqcal_status[AR9300_MAX_CHAINS] = {
		AR_PHY_TX_IQCAL_STATUS_B0(ah),
		AR_PHY_TX_IQCAL_STATUS_B1,
		AR_PHY_TX_IQCAL_STATUS_B2,
	};
	const u_int32_t chan_info_tab[] = {
		AR_PHY_CHAN_INFO_TAB_0,
		AR_PHY_CHAN_INFO_TAB_1,
		AR_PHY_CHAN_INFO_TAB_2,
	};
	static struct coeff coeff;
	s32 iq_res[6];
	int i, im, j;
	int nmeasurement = 0;
	bool outlier_detect = true;

	for (i = 0; i < AR9300_MAX_CHAINS; i++) {
		if (!(ah->txchainmask & (1 << i)))
			continue;

		nmeasurement = REG_READ_FIELD(ah,
				AR_PHY_TX_IQCAL_STATUS_B0(ah),
				AR_PHY_CALIBRATED_GAINS_0);
		if (nmeasurement > MAX_MEASUREMENT)
			nmeasurement = MAX_MEASUREMENT;

		for (im = 0; im < nmeasurement; im++) {
			ath_dbg(common, CALIBRATE,
				"Doing Tx IQ Cal for chain %d\n", i);

			if (REG_READ(ah, txiqcal_status[i]) &
					AR_PHY_TX_IQCAL_STATUS_FAILED) {
				ath_dbg(common, CALIBRATE,
					"Tx IQ Cal failed for chain %d\n", i);
				goto tx_iqcal_fail;
			}

			for (j = 0; j < 3; j++) {
				u32 idx = 2 * j, offset = 4 * (3 * im + j);

				REG_RMW_FIELD(ah,
						AR_PHY_CHAN_INFO_MEMORY(ah),
						AR_PHY_CHAN_INFO_TAB_S2_READ,
						0);

				/* 32 bits */
				iq_res[idx] = REG_READ(ah,
						chan_info_tab[i] +
						offset);

				REG_RMW_FIELD(ah,
						AR_PHY_CHAN_INFO_MEMORY(ah),
						AR_PHY_CHAN_INFO_TAB_S2_READ,
						1);

				/* 16 bits */
				iq_res[idx + 1] = 0xffff & REG_READ(ah,
						chan_info_tab[i] + offset);

				ath_dbg(common, CALIBRATE,
					"IQ_RES[%d]=0x%x IQ_RES[%d]=0x%x\n",
					idx, iq_res[idx], idx + 1,
					iq_res[idx + 1]);
			}

			if (!ar9003_hw_calc_iq_corr(ah, i, iq_res,
						coeff.iqc_coeff)) {
				ath_dbg(common, CALIBRATE,
					"Failed in calculation of IQ correction\n");
				goto tx_iqcal_fail;
			}

			coeff.phs_coeff[i][im][iqcal_idx] =
				coeff.iqc_coeff[0] & 0x7f;
			coeff.mag_coeff[i][im][iqcal_idx] =
				(coeff.iqc_coeff[0] >> 7) & 0x7f;

			if (coeff.mag_coeff[i][im][iqcal_idx] > 63)
				coeff.mag_coeff[i][im][iqcal_idx] -= 128;
			if (coeff.phs_coeff[i][im][iqcal_idx] > 63)
				coeff.phs_coeff[i][im][iqcal_idx] -= 128;
		}
	}

	if (AR_SREV_9550(ah))
		outlier_detect = ar955x_tx_iq_cal_median(ah, &coeff,
							 iqcal_idx, nmeasurement);
	if (outlier_detect)
		ar9003_hw_tx_iq_cal_outlier_detection(ah, &coeff, is_reusable);

	return;

tx_iqcal_fail:
	ath_dbg(common, CALIBRATE, "Tx IQ Cal failed\n");
	return;
}

static void ar9003_hw_tx_iq_cal_reload(struct ath_hw *ah)
{
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	u32 tx_corr_coeff[MAX_MEASUREMENT][AR9300_MAX_CHAINS];
	int i, im;

	memset(tx_corr_coeff, 0, sizeof(tx_corr_coeff));
	for (i = 0; i < MAX_MEASUREMENT / 2; i++) {
		tx_corr_coeff[i * 2][0] = tx_corr_coeff[(i * 2) + 1][0] =
					AR_PHY_TX_IQCAL_CORR_COEFF_B0(ah, i);
		if (!AR_SREV_9485(ah)) {
			tx_corr_coeff[i * 2][1] =
			tx_corr_coeff[(i * 2) + 1][1] =
					AR_PHY_TX_IQCAL_CORR_COEFF_B1(i);

			tx_corr_coeff[i * 2][2] =
			tx_corr_coeff[(i * 2) + 1][2] =
					AR_PHY_TX_IQCAL_CORR_COEFF_B2(i);
		}
	}

	for (i = 0; i < AR9300_MAX_CHAINS; i++) {
		if (!(ah->txchainmask & (1 << i)))
			continue;

		for (im = 0; im < caldata->num_measures[i]; im++) {
			if ((im % 2) == 0)
				REG_RMW_FIELD(ah, tx_corr_coeff[im][i],
				     AR_PHY_TX_IQCAL_CORR_COEFF_00_COEFF_TABLE,
				     caldata->tx_corr_coeff[im][i]);
			else
				REG_RMW_FIELD(ah, tx_corr_coeff[im][i],
				     AR_PHY_TX_IQCAL_CORR_COEFF_01_COEFF_TABLE,
				     caldata->tx_corr_coeff[im][i]);
		}
	}

	REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_3,
		      AR_PHY_TX_IQCAL_CONTROL_3_IQCORR_EN, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_RX_IQCAL_CORR_B0,
		      AR_PHY_RX_IQCAL_CORR_B0_LOOPBACK_IQCORR_EN, 0x1);
}

static void ar9003_hw_manual_peak_cal(struct ath_hw *ah, u8 chain, bool is_2g)
{
	int offset[8] = {0}, total = 0, test;
	int agc_out, i, peak_detect_threshold = 0;

	if (AR_SREV_9550(ah) || AR_SREV_9531(ah))
		peak_detect_threshold = 8;
	else if (AR_SREV_9561(ah))
		peak_detect_threshold = 11;

	/*
	 * Turn off LNA/SW.
	 */
	REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_GAINSTAGES(chain),
		      AR_PHY_65NM_RXRF_GAINSTAGES_RX_OVERRIDE, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_GAINSTAGES(chain),
		      AR_PHY_65NM_RXRF_GAINSTAGES_LNAON_CALDC, 0x0);

	if (AR_SREV_9003_PCOEM(ah) || AR_SREV_9330_11(ah)) {
		if (is_2g)
			REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_GAINSTAGES(chain),
				      AR_PHY_65NM_RXRF_GAINSTAGES_LNA2G_GAIN_OVR, 0x0);
		else
			REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_GAINSTAGES(chain),
				      AR_PHY_65NM_RXRF_GAINSTAGES_LNA5G_GAIN_OVR, 0x0);
	}

	/*
	 * Turn off RXON.
	 */
	REG_RMW_FIELD(ah, AR_PHY_65NM_RXTX2(chain),
		      AR_PHY_65NM_RXTX2_RXON_OVR, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_65NM_RXTX2(chain),
		      AR_PHY_65NM_RXTX2_RXON, 0x0);

	/*
	 * Turn on AGC for cal.
	 */
	REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
		      AR_PHY_65NM_RXRF_AGC_AGC_OVERRIDE, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
		      AR_PHY_65NM_RXRF_AGC_AGC_ON_OVR, 0x1);
	REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
		      AR_PHY_65NM_RXRF_AGC_AGC_CAL_OVR, 0x1);

	if (AR_SREV_9330_11(ah))
		REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
			      AR_PHY_65NM_RXRF_AGC_AGC2G_CALDAC_OVR, 0x0);

	if (is_2g)
		REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
			      AR_PHY_65NM_RXRF_AGC_AGC2G_DBDAC_OVR,
			      peak_detect_threshold);
	else
		REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
			      AR_PHY_65NM_RXRF_AGC_AGC5G_DBDAC_OVR,
			      peak_detect_threshold);

	for (i = 6; i > 0; i--) {
		offset[i] = BIT(i - 1);
		test = total + offset[i];

		if (is_2g)
			REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
				      AR_PHY_65NM_RXRF_AGC_AGC2G_CALDAC_OVR,
				      test);
		else
			REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
				      AR_PHY_65NM_RXRF_AGC_AGC5G_CALDAC_OVR,
				      test);
		udelay(100);
		agc_out = REG_READ_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
					 AR_PHY_65NM_RXRF_AGC_AGC_OUT);
		offset[i] = (agc_out) ? 0 : 1;
		total += (offset[i] << (i - 1));
	}

	if (is_2g)
		REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
			      AR_PHY_65NM_RXRF_AGC_AGC2G_CALDAC_OVR, total);
	else
		REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
			      AR_PHY_65NM_RXRF_AGC_AGC5G_CALDAC_OVR, total);

	/*
	 * Turn on LNA.
	 */
	REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_GAINSTAGES(chain),
		      AR_PHY_65NM_RXRF_GAINSTAGES_RX_OVERRIDE, 0);
	/*
	 * Turn off RXON.
	 */
	REG_RMW_FIELD(ah, AR_PHY_65NM_RXTX2(chain),
		      AR_PHY_65NM_RXTX2_RXON_OVR, 0);
	/*
	 * Turn off peak detect calibration.
	 */
	REG_RMW_FIELD(ah, AR_PHY_65NM_RXRF_AGC(chain),
		      AR_PHY_65NM_RXRF_AGC_AGC_CAL_OVR, 0);
}

static void ar9003_hw_do_pcoem_manual_peak_cal(struct ath_hw *ah,
					       struct ath9k_channel *chan,
					       bool run_rtt_cal)
{
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	int i;

	if ((ah->caps.hw_caps & ATH9K_HW_CAP_RTT) && !run_rtt_cal)
		return;

	for (i = 0; i < AR9300_MAX_CHAINS; i++) {
		if (!(ah->rxchainmask & (1 << i)))
			continue;
		ar9003_hw_manual_peak_cal(ah, i, IS_CHAN_2GHZ(chan));
	}

	if (caldata)
		set_bit(SW_PKDET_DONE, &caldata->cal_flags);

	if ((ah->caps.hw_caps & ATH9K_HW_CAP_RTT) && caldata) {
		if (IS_CHAN_2GHZ(chan)){
			caldata->caldac[0] = REG_READ_FIELD(ah,
						    AR_PHY_65NM_RXRF_AGC(0),
						    AR_PHY_65NM_RXRF_AGC_AGC2G_CALDAC_OVR);
			caldata->caldac[1] = REG_READ_FIELD(ah,
						    AR_PHY_65NM_RXRF_AGC(1),
						    AR_PHY_65NM_RXRF_AGC_AGC2G_CALDAC_OVR);
		} else {
			caldata->caldac[0] = REG_READ_FIELD(ah,
						    AR_PHY_65NM_RXRF_AGC(0),
						    AR_PHY_65NM_RXRF_AGC_AGC5G_CALDAC_OVR);
			caldata->caldac[1] = REG_READ_FIELD(ah,
						    AR_PHY_65NM_RXRF_AGC(1),
						    AR_PHY_65NM_RXRF_AGC_AGC5G_CALDAC_OVR);
		}
	}
}

static void ar9003_hw_cl_cal_post_proc(struct ath_hw *ah, bool is_reusable)
{
	u32 cl_idx[AR9300_MAX_CHAINS] = { AR_PHY_CL_TAB_0,
					  AR_PHY_CL_TAB_1,
					  AR_PHY_CL_TAB_2 };
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	bool txclcal_done = false;
	int i, j;

	if (!caldata || !(ah->enabled_cals & TX_CL_CAL))
		return;

	txclcal_done = !!(REG_READ(ah, AR_PHY_AGC_CONTROL(ah)) &
			  AR_PHY_AGC_CONTROL_CLC_SUCCESS);

	if (test_bit(TXCLCAL_DONE, &caldata->cal_flags)) {
		for (i = 0; i < AR9300_MAX_CHAINS; i++) {
			if (!(ah->txchainmask & (1 << i)))
				continue;
			for (j = 0; j < MAX_CL_TAB_ENTRY; j++)
				REG_WRITE(ah, CL_TAB_ENTRY(cl_idx[i]),
					  caldata->tx_clcal[i][j]);
		}
	} else if (is_reusable && txclcal_done) {
		for (i = 0; i < AR9300_MAX_CHAINS; i++) {
			if (!(ah->txchainmask & (1 << i)))
				continue;
			for (j = 0; j < MAX_CL_TAB_ENTRY; j++)
				caldata->tx_clcal[i][j] =
					REG_READ(ah, CL_TAB_ENTRY(cl_idx[i]));
		}
		set_bit(TXCLCAL_DONE, &caldata->cal_flags);
	}
}

static void ar9003_hw_init_cal_common(struct ath_hw *ah)
{
	struct ath9k_hw_cal_data *caldata = ah->caldata;

	/* Initialize list pointers */
	ah->cal_list = ah->cal_list_last = ah->cal_list_curr = NULL;

	INIT_CAL(&ah->iq_caldata);
	INSERT_CAL(ah, &ah->iq_caldata);

	/* Initialize current pointer to first element in list */
	ah->cal_list_curr = ah->cal_list;

	if (ah->cal_list_curr)
		ath9k_hw_reset_calibration(ah, ah->cal_list_curr);

	if (caldata)
		caldata->CalValid = 0;
}

static bool ar9003_hw_init_cal_pcoem(struct ath_hw *ah,
				     struct ath9k_channel *chan)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	bool txiqcal_done = false;
	bool is_reusable = true, status = true;
	bool run_rtt_cal = false, run_agc_cal;
	bool rtt = !!(ah->caps.hw_caps & ATH9K_HW_CAP_RTT);
	u32 rx_delay = 0;
	u32 agc_ctrl = 0, agc_supp_cals = AR_PHY_AGC_CONTROL_OFFSET_CAL |
					  AR_PHY_AGC_CONTROL_FLTR_CAL   |
					  AR_PHY_AGC_CONTROL_PKDET_CAL;

	/* Use chip chainmask only for calibration */
	ar9003_hw_set_chain_masks(ah, ah->caps.rx_chainmask, ah->caps.tx_chainmask);

	if (rtt) {
		if (!ar9003_hw_rtt_restore(ah, chan))
			run_rtt_cal = true;

		if (run_rtt_cal)
			ath_dbg(common, CALIBRATE, "RTT calibration to be done\n");
	}

	run_agc_cal = run_rtt_cal;

	if (run_rtt_cal) {
		ar9003_hw_rtt_enable(ah);
		ar9003_hw_rtt_set_mask(ah, 0x00);
		ar9003_hw_rtt_clear_hist(ah);
	}

	if (rtt) {
		if (!run_rtt_cal) {
			agc_ctrl = REG_READ(ah, AR_PHY_AGC_CONTROL(ah));
			agc_supp_cals &= agc_ctrl;
			agc_ctrl &= ~(AR_PHY_AGC_CONTROL_OFFSET_CAL |
				      AR_PHY_AGC_CONTROL_FLTR_CAL |
				      AR_PHY_AGC_CONTROL_PKDET_CAL);
			REG_WRITE(ah, AR_PHY_AGC_CONTROL(ah), agc_ctrl);
		} else {
			if (ah->ah_flags & AH_FASTCC)
				run_agc_cal = true;
		}
	}

	if (ah->enabled_cals & TX_CL_CAL) {
		if (caldata && test_bit(TXCLCAL_DONE, &caldata->cal_flags))
			REG_CLR_BIT(ah, AR_PHY_CL_CAL_CTL,
				    AR_PHY_CL_CAL_ENABLE);
		else {
			REG_SET_BIT(ah, AR_PHY_CL_CAL_CTL,
				    AR_PHY_CL_CAL_ENABLE);
			run_agc_cal = true;
		}
	}

	if ((IS_CHAN_HALF_RATE(chan) || IS_CHAN_QUARTER_RATE(chan)) ||
	    !(ah->enabled_cals & TX_IQ_CAL))
		goto skip_tx_iqcal;

	/* Do Tx IQ Calibration */
	REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_1(ah),
		      AR_PHY_TX_IQCAL_CONTROL_1_IQCORR_I_Q_COFF_DELPT,
		      DELPT);

	/*
	 * For AR9485 or later chips, TxIQ cal runs as part of
	 * AGC calibration
	 */
	if (ah->enabled_cals & TX_IQ_ON_AGC_CAL) {
		if (caldata && !test_bit(TXIQCAL_DONE, &caldata->cal_flags))
			REG_SET_BIT(ah, AR_PHY_TX_IQCAL_CONTROL_0(ah),
				    AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL);
		else
			REG_CLR_BIT(ah, AR_PHY_TX_IQCAL_CONTROL_0(ah),
				    AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL);
		txiqcal_done = run_agc_cal = true;
	}

skip_tx_iqcal:
	if (ath9k_hw_mci_is_enabled(ah) && IS_CHAN_2GHZ(chan) && run_agc_cal)
		ar9003_mci_init_cal_req(ah, &is_reusable);

	if (REG_READ(ah, AR_PHY_CL_CAL_CTL) & AR_PHY_CL_CAL_ENABLE) {
		rx_delay = REG_READ(ah, AR_PHY_RX_DELAY);
		/* Disable BB_active */
		REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
		udelay(5);
		REG_WRITE(ah, AR_PHY_RX_DELAY, AR_PHY_RX_DELAY_DELAY);
		REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);
	}

	if (run_agc_cal || !(ah->ah_flags & AH_FASTCC)) {
		/* Calibrate the AGC */
		REG_WRITE(ah, AR_PHY_AGC_CONTROL(ah),
			  REG_READ(ah, AR_PHY_AGC_CONTROL(ah)) |
			  AR_PHY_AGC_CONTROL_CAL);

		/* Poll for offset calibration complete */
		status = ath9k_hw_wait(ah, AR_PHY_AGC_CONTROL(ah),
				       AR_PHY_AGC_CONTROL_CAL,
				       0, AH_WAIT_TIMEOUT);

		ar9003_hw_do_pcoem_manual_peak_cal(ah, chan, run_rtt_cal);
	}

	if (REG_READ(ah, AR_PHY_CL_CAL_CTL) & AR_PHY_CL_CAL_ENABLE) {
		REG_WRITE(ah, AR_PHY_RX_DELAY, rx_delay);
		udelay(5);
	}

	if (ath9k_hw_mci_is_enabled(ah) && IS_CHAN_2GHZ(chan) && run_agc_cal)
		ar9003_mci_init_cal_done(ah);

	if (rtt && !run_rtt_cal) {
		agc_ctrl |= agc_supp_cals;
		REG_WRITE(ah, AR_PHY_AGC_CONTROL(ah), agc_ctrl);
	}

	if (!status) {
		if (run_rtt_cal)
			ar9003_hw_rtt_disable(ah);

		ath_dbg(common, CALIBRATE,
			"offset calibration failed to complete in %d ms; noisy environment?\n",
			AH_WAIT_TIMEOUT / 1000);
		return false;
	}

	if (txiqcal_done)
		ar9003_hw_tx_iq_cal_post_proc(ah, 0, is_reusable);
	else if (caldata && test_bit(TXIQCAL_DONE, &caldata->cal_flags))
		ar9003_hw_tx_iq_cal_reload(ah);

	ar9003_hw_cl_cal_post_proc(ah, is_reusable);

	if (run_rtt_cal && caldata) {
		if (is_reusable) {
			if (!ath9k_hw_rfbus_req(ah)) {
				ath_err(ath9k_hw_common(ah),
					"Could not stop baseband\n");
			} else {
				ar9003_hw_rtt_fill_hist(ah);

				if (test_bit(SW_PKDET_DONE, &caldata->cal_flags))
					ar9003_hw_rtt_load_hist(ah);
			}

			ath9k_hw_rfbus_done(ah);
		}

		ar9003_hw_rtt_disable(ah);
	}

	/* Revert chainmask to runtime parameters */
	ar9003_hw_set_chain_masks(ah, ah->rxchainmask, ah->txchainmask);

	ar9003_hw_init_cal_common(ah);

	return true;
}

static bool do_ar9003_agc_cal(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	bool status;

	REG_WRITE(ah, AR_PHY_AGC_CONTROL(ah),
		  REG_READ(ah, AR_PHY_AGC_CONTROL(ah)) |
		  AR_PHY_AGC_CONTROL_CAL);

	status = ath9k_hw_wait(ah, AR_PHY_AGC_CONTROL(ah),
			       AR_PHY_AGC_CONTROL_CAL,
			       0, AH_WAIT_TIMEOUT);
	if (!status) {
		ath_dbg(common, CALIBRATE,
			"offset calibration failed to complete in %d ms,"
			"noisy environment?\n",
			AH_WAIT_TIMEOUT / 1000);
		return false;
	}

	return true;
}

static bool ar9003_hw_init_cal_soc(struct ath_hw *ah,
				   struct ath9k_channel *chan)
{
	bool txiqcal_done = false;
	bool status = true;
	bool run_agc_cal = false, sep_iq_cal = false;
	int i = 0;

	/* Use chip chainmask only for calibration */
	ar9003_hw_set_chain_masks(ah, ah->caps.rx_chainmask, ah->caps.tx_chainmask);

	if (ah->enabled_cals & TX_CL_CAL) {
		REG_SET_BIT(ah, AR_PHY_CL_CAL_CTL, AR_PHY_CL_CAL_ENABLE);
		run_agc_cal = true;
	}

	if (IS_CHAN_HALF_RATE(chan) || IS_CHAN_QUARTER_RATE(chan))
		goto skip_tx_iqcal;

	/* Do Tx IQ Calibration */
	REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_1(ah),
		      AR_PHY_TX_IQCAL_CONTROL_1_IQCORR_I_Q_COFF_DELPT,
		      DELPT);

	/*
	 * For AR9485 or later chips, TxIQ cal runs as part of
	 * AGC calibration. Specifically, AR9550 in SoC chips.
	 */
	if (ah->enabled_cals & TX_IQ_ON_AGC_CAL) {
		if (REG_READ_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_0(ah),
				   AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL)) {
				txiqcal_done = true;
		} else {
			txiqcal_done = false;
		}
		run_agc_cal = true;
	} else {
		sep_iq_cal = true;
		run_agc_cal = true;
	}

	/*
	 * In the SoC family, this will run for AR9300, AR9331 and AR9340.
	 */
	if (sep_iq_cal) {
		txiqcal_done = ar9003_hw_tx_iq_cal_run(ah);
		REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
		udelay(5);
		REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);
	}

	if (AR_SREV_9550(ah) && IS_CHAN_2GHZ(chan)) {
		if (!ar9003_hw_dynamic_osdac_selection(ah, txiqcal_done))
			return false;
	}

skip_tx_iqcal:
	if (run_agc_cal || !(ah->ah_flags & AH_FASTCC)) {
		for (i = 0; i < AR9300_MAX_CHAINS; i++) {
			if (!(ah->rxchainmask & (1 << i)))
				continue;

			ar9003_hw_manual_peak_cal(ah, i,
						  IS_CHAN_2GHZ(chan));
		}

		/*
		 * For non-AR9550 chips, we just trigger AGC calibration
		 * in the HW, poll for completion and then process
		 * the results.
		 *
		 * For AR955x, we run it multiple times and use
		 * median IQ correction.
		 */
		if (!AR_SREV_9550(ah)) {
			status = do_ar9003_agc_cal(ah);
			if (!status)
				return false;

			if (txiqcal_done)
				ar9003_hw_tx_iq_cal_post_proc(ah, 0, false);
		} else {
			if (!txiqcal_done) {
				status = do_ar9003_agc_cal(ah);
				if (!status)
					return false;
			} else {
				for (i = 0; i < MAXIQCAL; i++) {
					status = do_ar9003_agc_cal(ah);
					if (!status)
						return false;
					ar9003_hw_tx_iq_cal_post_proc(ah, i, false);
				}
			}
		}
	}

	/* Revert chainmask to runtime parameters */
	ar9003_hw_set_chain_masks(ah, ah->rxchainmask, ah->txchainmask);

	ar9003_hw_init_cal_common(ah);

	return true;
}

void ar9003_hw_attach_calib_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);

	if (AR_SREV_9003_PCOEM(ah))
		priv_ops->init_cal = ar9003_hw_init_cal_pcoem;
	else
		priv_ops->init_cal = ar9003_hw_init_cal_soc;

	priv_ops->init_cal_settings = ar9003_hw_init_cal_settings;
	priv_ops->setup_calibration = ar9003_hw_setup_calibration;

	ops->calibrate = ar9003_hw_calibrate;
}
