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

struct coeff {
	int mag_coeff[AR9300_MAX_CHAINS][MAX_MEASUREMENT];
	int phs_coeff[AR9300_MAX_CHAINS][MAX_MEASUREMENT];
	int iqc_coeff[2];
};

enum ar9003_cal_types {
	IQ_MISMATCH_CAL = BIT(0),
	TEMP_COMP_CAL = BIT(1),
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
	case TEMP_COMP_CAL:
		REG_RMW_FIELD(ah, AR_PHY_65NM_CH0_THERM,
			      AR_PHY_65NM_CH0_THERM_LOCAL, 1);
		REG_RMW_FIELD(ah, AR_PHY_65NM_CH0_THERM,
			      AR_PHY_65NM_CH0_THERM_START, 1);

		ath_dbg(common, CALIBRATE,
			"starting Temperature Compensation Calibration\n");
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
	/* Cal is assumed not done until explicitly set below */
	bool iscaldone = false;

	/* Calibration in progress. */
	if (currCal->calState == CAL_RUNNING) {
		/* Check to see if it has finished. */
		if (!(REG_READ(ah, AR_PHY_TIMING4) & AR_PHY_TIMING4_DO_CAL)) {
			/*
			* Accumulate cal measures for active chains
			*/
			currCal->calData->calCollect(ah);
			ah->cal_samples++;

			if (ah->cal_samples >=
			    currCal->calData->calNumSamples) {
				unsigned int i, numChains = 0;
				for (i = 0; i < AR9300_MAX_CHAINS; i++) {
					if (rxchainmask & (1 << i))
						numChains++;
				}

				/*
				* Process accumulated data
				*/
				currCal->calData->calPostProc(ah, numChains);

				/* Calibration has finished. */
				caldata->CalValid |= currCal->calData->calType;
				currCal->calState = CAL_DONE;
				iscaldone = true;
			} else {
			/*
			 * Set-up collection of another sub-sample until we
			 * get desired number
			 */
			ar9003_hw_setup_calibration(ah, currCal);
			}
		}
	} else if (!(caldata->CalValid & currCal->calData->calType)) {
		/* If current cal is marked invalid in channel, kick it off */
		ath9k_hw_reset_calibration(ah, currCal);
	}

	return iscaldone;
}

static bool ar9003_hw_calibrate(struct ath_hw *ah,
				struct ath9k_channel *chan,
				u8 rxchainmask,
				bool longcal)
{
	bool iscaldone = true;
	struct ath9k_cal_list *currCal = ah->cal_list_curr;

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

	/* Do NF cal only at longer intervals */
	if (longcal) {
		/*
		 * Get the value from the previous NF cal and update
		 * history buffer.
		 */
		ath9k_hw_getnf(ah, chan);

		/*
		 * Load the NF from history buffer of the current channel.
		 * NF is slow time-variant, so it is OK to use a historical
		 * value.
		 */
		ath9k_hw_loadnf(ah, ah->curchan);

		/* start NF calibration, without updating BB NF register */
		ath9k_hw_start_nfcal(ah, false);
	}

	return iscaldone;
}

static void ar9003_hw_iqcal_collect(struct ath_hw *ah)
{
	int i;

	/* Accumulate IQ cal measures for active chains */
	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
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

	f2 = (f1 * f1 + f3 * f3) / result_shift;

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

	if (i2_p_q2_a0_d1 > 0x800)
		i2_p_q2_a0_d1 = -((0xfff - i2_p_q2_a0_d1) + 1);

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

	iqc_coeff[0] = (q_q_coff * 128) + q_i_coff;

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

	iqc_coeff[1] = (q_q_coff * 128) + q_i_coff;

	ath_dbg(common, CALIBRATE, "rx chain %d: iq corr coeff=%x\n",
		chain_idx, iqc_coeff[1]);

	return true;
}

static void ar9003_hw_detect_outlier(int *mp_coeff, int nmeasurement,
				     int max_delta)
{
	int mp_max = -64, max_idx = 0;
	int mp_min = 63, min_idx = 0;
	int mp_avg = 0, i, outlier_idx = 0, mp_count = 0;

	/* find min/max mismatch across all calibrated gains */
	for (i = 0; i < nmeasurement; i++) {
		if (mp_coeff[i] > mp_max) {
			mp_max = mp_coeff[i];
			max_idx = i;
		} else if (mp_coeff[i] < mp_min) {
			mp_min = mp_coeff[i];
			min_idx = i;
		}
	}

	/* find average (exclude max abs value) */
	for (i = 0; i < nmeasurement; i++) {
		if ((abs(mp_coeff[i]) < abs(mp_max)) ||
		    (abs(mp_coeff[i]) < abs(mp_min))) {
			mp_avg += mp_coeff[i];
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
		mp_avg = mp_coeff[nmeasurement - 1];

	/* detect outlier */
	if (abs(mp_max - mp_min) > max_delta) {
		if (abs(mp_max - mp_avg) > abs(mp_min - mp_avg))
			outlier_idx = max_idx;
		else
			outlier_idx = min_idx;

		mp_coeff[outlier_idx] = mp_avg;
	}
}

static void ar9003_hw_tx_iqcal_load_avg_2_passes(struct ath_hw *ah,
						 u8 num_chains,
						 struct coeff *coeff,
						 bool is_reusable)
{
	int i, im, nmeasurement;
	u32 tx_corr_coeff[MAX_MEASUREMENT][AR9300_MAX_CHAINS];
	struct ath9k_hw_cal_data *caldata = ah->caldata;

	memset(tx_corr_coeff, 0, sizeof(tx_corr_coeff));
	for (i = 0; i < MAX_MEASUREMENT / 2; i++) {
		tx_corr_coeff[i * 2][0] = tx_corr_coeff[(i * 2) + 1][0] =
					AR_PHY_TX_IQCAL_CORR_COEFF_B0(i);
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
	for (i = 0; i < num_chains; i++) {
		nmeasurement = REG_READ_FIELD(ah,
				AR_PHY_TX_IQCAL_STATUS_B0,
				AR_PHY_CALIBRATED_GAINS_0);

		if (nmeasurement > MAX_MEASUREMENT)
			nmeasurement = MAX_MEASUREMENT;

		/* detect outlier only if nmeasurement > 1 */
		if (nmeasurement > 1) {
			/* Detect magnitude outlier */
			ar9003_hw_detect_outlier(coeff->mag_coeff[i],
					nmeasurement, MAX_MAG_DELTA);

			/* Detect phase outlier */
			ar9003_hw_detect_outlier(coeff->phs_coeff[i],
					nmeasurement, MAX_PHS_DELTA);
		}

		for (im = 0; im < nmeasurement; im++) {

			coeff->iqc_coeff[0] = (coeff->mag_coeff[i][im] & 0x7f) |
				((coeff->phs_coeff[i][im] & 0x7f) << 7);

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

	if (caldata)
		caldata->done_txiqcal_once = is_reusable;

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

	REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_START,
		      AR_PHY_TX_IQCAL_START_DO_CAL, 1);

	if (!ath9k_hw_wait(ah, AR_PHY_TX_IQCAL_START,
			AR_PHY_TX_IQCAL_START_DO_CAL, 0,
			AH_WAIT_TIMEOUT)) {
		ath_dbg(common, CALIBRATE, "Tx IQ Cal is not completed\n");
		return false;
	}
	return true;
}

static void ar9003_hw_tx_iq_cal_post_proc(struct ath_hw *ah, bool is_reusable)
{
	struct ath_common *common = ath9k_hw_common(ah);
	const u32 txiqcal_status[AR9300_MAX_CHAINS] = {
		AR_PHY_TX_IQCAL_STATUS_B0,
		AR_PHY_TX_IQCAL_STATUS_B1,
		AR_PHY_TX_IQCAL_STATUS_B2,
	};
	const u_int32_t chan_info_tab[] = {
		AR_PHY_CHAN_INFO_TAB_0,
		AR_PHY_CHAN_INFO_TAB_1,
		AR_PHY_CHAN_INFO_TAB_2,
	};
	struct coeff coeff;
	s32 iq_res[6];
	u8 num_chains = 0;
	int i, im, j;
	int nmeasurement;

	for (i = 0; i < AR9300_MAX_CHAINS; i++) {
		if (ah->txchainmask & (1 << i))
			num_chains++;
	}

	for (i = 0; i < num_chains; i++) {
		nmeasurement = REG_READ_FIELD(ah,
				AR_PHY_TX_IQCAL_STATUS_B0,
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
						AR_PHY_CHAN_INFO_MEMORY,
						AR_PHY_CHAN_INFO_TAB_S2_READ,
						0);

				/* 32 bits */
				iq_res[idx] = REG_READ(ah,
						chan_info_tab[i] +
						offset);

				REG_RMW_FIELD(ah,
						AR_PHY_CHAN_INFO_MEMORY,
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

			coeff.mag_coeff[i][im] = coeff.iqc_coeff[0] & 0x7f;
			coeff.phs_coeff[i][im] =
				(coeff.iqc_coeff[0] >> 7) & 0x7f;

			if (coeff.mag_coeff[i][im] > 63)
				coeff.mag_coeff[i][im] -= 128;
			if (coeff.phs_coeff[i][im] > 63)
				coeff.phs_coeff[i][im] -= 128;
		}
	}
	ar9003_hw_tx_iqcal_load_avg_2_passes(ah, num_chains,
					     &coeff, is_reusable);

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
					AR_PHY_TX_IQCAL_CORR_COEFF_B0(i);
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

static bool ar9003_hw_rtt_restore(struct ath_hw *ah, struct ath9k_channel *chan)
{
	struct ath9k_rtt_hist *hist;
	u32 *table;
	int i;
	bool restore;

	if (!ah->caldata)
		return false;

	hist = &ah->caldata->rtt_hist;
	if (!hist->num_readings)
		return false;

	ar9003_hw_rtt_enable(ah);
	ar9003_hw_rtt_set_mask(ah, 0x00);
	for (i = 0; i < AR9300_MAX_CHAINS; i++) {
		if (!(ah->rxchainmask & (1 << i)))
			continue;
		table = &hist->table[i][hist->num_readings][0];
		ar9003_hw_rtt_load_hist(ah, i, table);
	}
	restore = ar9003_hw_rtt_force_restore(ah);
	ar9003_hw_rtt_disable(ah);

	return restore;
}

static bool ar9003_hw_init_cal(struct ath_hw *ah,
			       struct ath9k_channel *chan)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	bool txiqcal_done = false, txclcal_done = false;
	bool is_reusable = true, status = true;
	bool run_rtt_cal = false, run_agc_cal;
	bool rtt = !!(ah->caps.hw_caps & ATH9K_HW_CAP_RTT);
	bool mci = !!(ah->caps.hw_caps & ATH9K_HW_CAP_MCI);
	u32 agc_ctrl = 0, agc_supp_cals = AR_PHY_AGC_CONTROL_OFFSET_CAL |
					  AR_PHY_AGC_CONTROL_FLTR_CAL   |
					  AR_PHY_AGC_CONTROL_PKDET_CAL;
	int i, j;
	u32 cl_idx[AR9300_MAX_CHAINS] = { AR_PHY_CL_TAB_0,
					  AR_PHY_CL_TAB_1,
					  AR_PHY_CL_TAB_2 };

	if (rtt) {
		if (!ar9003_hw_rtt_restore(ah, chan))
			run_rtt_cal = true;

		ath_dbg(common, CALIBRATE, "RTT restore %s\n",
			run_rtt_cal ? "failed" : "succeed");
	}
	run_agc_cal = run_rtt_cal;

	if (run_rtt_cal) {
		ar9003_hw_rtt_enable(ah);
		ar9003_hw_rtt_set_mask(ah, 0x00);
		ar9003_hw_rtt_clear_hist(ah);
	}

	if (rtt && !run_rtt_cal) {
		agc_ctrl = REG_READ(ah, AR_PHY_AGC_CONTROL);
		agc_supp_cals &= agc_ctrl;
		agc_ctrl &= ~(AR_PHY_AGC_CONTROL_OFFSET_CAL |
			     AR_PHY_AGC_CONTROL_FLTR_CAL |
			     AR_PHY_AGC_CONTROL_PKDET_CAL);
		REG_WRITE(ah, AR_PHY_AGC_CONTROL, agc_ctrl);
	}

	if (ah->enabled_cals & TX_CL_CAL) {
		if (caldata && caldata->done_txclcal_once)
			REG_CLR_BIT(ah, AR_PHY_CL_CAL_CTL,
				    AR_PHY_CL_CAL_ENABLE);
		else {
			REG_SET_BIT(ah, AR_PHY_CL_CAL_CTL,
				    AR_PHY_CL_CAL_ENABLE);
			run_agc_cal = true;
		}
	}

	if (!(ah->enabled_cals & TX_IQ_CAL))
		goto skip_tx_iqcal;

	/* Do Tx IQ Calibration */
	REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_1,
		      AR_PHY_TX_IQCAL_CONTROL_1_IQCORR_I_Q_COFF_DELPT,
		      DELPT);

	/*
	 * For AR9485 or later chips, TxIQ cal runs as part of
	 * AGC calibration
	 */
	if (ah->enabled_cals & TX_IQ_ON_AGC_CAL) {
		if (caldata && !caldata->done_txiqcal_once)
			REG_SET_BIT(ah, AR_PHY_TX_IQCAL_CONTROL_0,
				    AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL);
		else
			REG_CLR_BIT(ah, AR_PHY_TX_IQCAL_CONTROL_0,
				    AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL);
		txiqcal_done = run_agc_cal = true;
		goto skip_tx_iqcal;
	} else if (caldata && !caldata->done_txiqcal_once)
		run_agc_cal = true;

	if (mci && IS_CHAN_2GHZ(chan) && run_agc_cal)
		ar9003_mci_init_cal_req(ah, &is_reusable);

	txiqcal_done = ar9003_hw_tx_iq_cal_run(ah);
	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
	udelay(5);
	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

skip_tx_iqcal:
	if (run_agc_cal || !(ah->ah_flags & AH_FASTCC)) {
		/* Calibrate the AGC */
		REG_WRITE(ah, AR_PHY_AGC_CONTROL,
			  REG_READ(ah, AR_PHY_AGC_CONTROL) |
			  AR_PHY_AGC_CONTROL_CAL);

		/* Poll for offset calibration complete */
		status = ath9k_hw_wait(ah, AR_PHY_AGC_CONTROL,
				       AR_PHY_AGC_CONTROL_CAL,
				       0, AH_WAIT_TIMEOUT);
	}

	if (mci && IS_CHAN_2GHZ(chan) && run_agc_cal)
		ar9003_mci_init_cal_done(ah);

	if (rtt && !run_rtt_cal) {
		agc_ctrl |= agc_supp_cals;
		REG_WRITE(ah, AR_PHY_AGC_CONTROL, agc_ctrl);
	}

	if (!status) {
		if (run_rtt_cal)
			ar9003_hw_rtt_disable(ah);

		ath_dbg(common, CALIBRATE,
			"offset calibration failed to complete in 1ms; noisy environment?\n");
		return false;
	}

	if (txiqcal_done)
		ar9003_hw_tx_iq_cal_post_proc(ah, is_reusable);
	else if (caldata && caldata->done_txiqcal_once)
		ar9003_hw_tx_iq_cal_reload(ah);

#define CL_TAB_ENTRY(reg_base)	(reg_base + (4 * j))
	if (caldata && (ah->enabled_cals & TX_CL_CAL)) {
		txclcal_done = !!(REG_READ(ah, AR_PHY_AGC_CONTROL) &
					   AR_PHY_AGC_CONTROL_CLC_SUCCESS);
		if (caldata->done_txclcal_once) {
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
						REG_READ(ah,
						  CL_TAB_ENTRY(cl_idx[i]));
			}
			caldata->done_txclcal_once = true;
		}
	}
#undef CL_TAB_ENTRY

	if (run_rtt_cal && caldata) {
		struct ath9k_rtt_hist *hist = &caldata->rtt_hist;
		if (is_reusable && (hist->num_readings < RTT_HIST_MAX)) {
			u32 *table;

			hist->num_readings++;
			for (i = 0; i < AR9300_MAX_CHAINS; i++) {
				if (!(ah->rxchainmask & (1 << i)))
					continue;
				table = &hist->table[i][hist->num_readings][0];
				ar9003_hw_rtt_fill_hist(ah, i, table);
			}
		}

		ar9003_hw_rtt_disable(ah);
	}

	/* Initialize list pointers */
	ah->cal_list = ah->cal_list_last = ah->cal_list_curr = NULL;
	ah->supp_cals = IQ_MISMATCH_CAL;

	if (ah->supp_cals & IQ_MISMATCH_CAL) {
		INIT_CAL(&ah->iq_caldata);
		INSERT_CAL(ah, &ah->iq_caldata);
		ath_dbg(common, CALIBRATE, "enabling IQ Calibration\n");
	}

	if (ah->supp_cals & TEMP_COMP_CAL) {
		INIT_CAL(&ah->tempCompCalData);
		INSERT_CAL(ah, &ah->tempCompCalData);
		ath_dbg(common, CALIBRATE,
			"enabling Temperature Compensation Calibration\n");
	}

	/* Initialize current pointer to first element in list */
	ah->cal_list_curr = ah->cal_list;

	if (ah->cal_list_curr)
		ath9k_hw_reset_calibration(ah, ah->cal_list_curr);

	if (caldata)
		caldata->CalValid = 0;

	return true;
}

void ar9003_hw_attach_calib_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);

	priv_ops->init_cal_settings = ar9003_hw_init_cal_settings;
	priv_ops->init_cal = ar9003_hw_init_cal;
	priv_ops->setup_calibration = ar9003_hw_setup_calibration;

	ops->calibrate = ar9003_hw_calibrate;
}
