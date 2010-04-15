/*
 * Copyright (c) 2010 Atheros Communications Inc.
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

static void ar9003_hw_setup_calibration(struct ath_hw *ah,
					struct ath9k_cal_list *currCal)
{
	/* TODO */
}

static bool ar9003_hw_calibrate(struct ath_hw *ah,
				struct ath9k_channel *chan,
				u8 rxchainmask,
				bool longcal)
{
	/* TODO */
	return false;
}

static bool ar9003_hw_init_cal(struct ath_hw *ah,
			       struct ath9k_channel *chan)
{
	/* TODO */
	return false;
}

static void ar9003_hw_iqcal_collect(struct ath_hw *ah)
{
	int i;

	/* Accumulate IQ cal measures for active chains */
	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		ah->totalPowerMeasI[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
		ah->totalPowerMeasQ[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
		ah->totalIqCorrMeas[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
		ath_print(ath9k_hw_common(ah), ATH_DBG_CALIBRATE,
			  "%d: Chn %d pmi=0x%08x;pmq=0x%08x;iqcm=0x%08x;\n",
			  ah->cal_samples, i, ah->totalPowerMeasI[i],
			  ah->totalPowerMeasQ[i],
			  ah->totalIqCorrMeas[i]);
	}
}

static void ar9003_hw_iqcalibrate(struct ath_hw *ah, u8 numChains)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 powerMeasQ, powerMeasI, iqCorrMeas;
	u32 qCoffDenom, iCoffDenom;
	int32_t qCoff, iCoff;
	int iqCorrNeg, i;
	const u_int32_t offset_array[3] = {
		AR_PHY_RX_IQCAL_CORR_B0,
		AR_PHY_RX_IQCAL_CORR_B1,
		AR_PHY_RX_IQCAL_CORR_B2,
	};

	for (i = 0; i < numChains; i++) {
		powerMeasI = ah->totalPowerMeasI[i];
		powerMeasQ = ah->totalPowerMeasQ[i];
		iqCorrMeas = ah->totalIqCorrMeas[i];

		ath_print(common, ATH_DBG_CALIBRATE,
			  "Starting IQ Cal and Correction for Chain %d\n",
			  i);

		ath_print(common, ATH_DBG_CALIBRATE,
			  "Orignal: Chn %diq_corr_meas = 0x%08x\n",
			  i, ah->totalIqCorrMeas[i]);

		iqCorrNeg = 0;

		if (iqCorrMeas > 0x80000000) {
			iqCorrMeas = (0xffffffff - iqCorrMeas) + 1;
			iqCorrNeg = 1;
		}

		ath_print(common, ATH_DBG_CALIBRATE,
			  "Chn %d pwr_meas_i = 0x%08x\n", i, powerMeasI);
		ath_print(common, ATH_DBG_CALIBRATE,
			  "Chn %d pwr_meas_q = 0x%08x\n", i, powerMeasQ);
		ath_print(common, ATH_DBG_CALIBRATE, "iqCorrNeg is 0x%08x\n",
			  iqCorrNeg);

		iCoffDenom = (powerMeasI / 2 + powerMeasQ / 2) / 256;
		qCoffDenom = powerMeasQ / 64;

		if ((iCoffDenom != 0) && (qCoffDenom != 0)) {
			iCoff = iqCorrMeas / iCoffDenom;
			qCoff = powerMeasI / qCoffDenom - 64;
			ath_print(common, ATH_DBG_CALIBRATE,
				  "Chn %d iCoff = 0x%08x\n", i, iCoff);
			ath_print(common, ATH_DBG_CALIBRATE,
				  "Chn %d qCoff = 0x%08x\n", i, qCoff);

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

			ath_print(common, ATH_DBG_CALIBRATE,
				  "Chn %d : iCoff = 0x%x  qCoff = 0x%x\n",
				  i, iCoff, qCoff);
			ath_print(common, ATH_DBG_CALIBRATE,
				  "Register offset (0x%04x) "
				  "before update = 0x%x\n",
				  offset_array[i],
				  REG_READ(ah, offset_array[i]));

			REG_RMW_FIELD(ah, offset_array[i],
				      AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF,
				      iCoff);
			REG_RMW_FIELD(ah, offset_array[i],
				      AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF,
				      qCoff);
			ath_print(common, ATH_DBG_CALIBRATE,
				  "Register offset (0x%04x) QI COFF "
				  "(bitfields 0x%08x) after update = 0x%x\n",
				  offset_array[i],
				  AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF,
				  REG_READ(ah, offset_array[i]));
			ath_print(common, ATH_DBG_CALIBRATE,
				  "Register offset (0x%04x) QQ COFF "
				  "(bitfields 0x%08x) after update = 0x%x\n",
				  offset_array[i],
				  AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF,
				  REG_READ(ah, offset_array[i]));

			ath_print(common, ATH_DBG_CALIBRATE,
				  "IQ Cal and Correction done for Chain %d\n",
				  i);
		}
	}

	REG_SET_BIT(ah, AR_PHY_RX_IQCAL_CORR_B0,
		    AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE);
	ath_print(common, ATH_DBG_CALIBRATE,
		  "IQ Cal and Correction (offset 0x%04x) enabled "
		  "(bit position 0x%08x). New Value 0x%08x\n",
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
	ah->supp_cals = IQ_MISMATCH_CAL;
}

static bool ar9003_hw_iscal_supported(struct ath_hw *ah,
				      enum ath9k_cal_types calType)
{
	/* TODO */
	return false;
}

static void ar9003_hw_loadnf(struct ath_hw *ah, struct ath9k_channel *chan)
{
	/* TODO */
}

void ar9003_hw_attach_calib_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);

	priv_ops->init_cal_settings = ar9003_hw_init_cal_settings;
	priv_ops->init_cal = ar9003_hw_init_cal;
	priv_ops->setup_calibration = ar9003_hw_setup_calibration;
	priv_ops->iscal_supported = ar9003_hw_iscal_supported;
	priv_ops->loadnf = ar9003_hw_loadnf;

	ops->calibrate = ar9003_hw_calibrate;
}
