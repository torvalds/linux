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

static inline u16 ath9k_hw_fbin2freq(u8 fbin, bool is2GHz)
{
	if (fbin == AR5416_BCHAN_UNUSED)
		return fbin;

	return (u16) ((is2GHz) ? (2300 + fbin) : (4800 + 5 * fbin));
}

void ath9k_hw_analog_shift_regwrite(struct ath_hw *ah, u32 reg, u32 val)
{
        REG_WRITE(ah, reg, val);

        if (ah->config.analog_shiftreg)
		udelay(100);
}

void ath9k_hw_analog_shift_rmw(struct ath_hw *ah, u32 reg, u32 mask,
			       u32 shift, u32 val)
{
	u32 regVal;

	regVal = REG_READ(ah, reg) & ~mask;
	regVal |= (val << shift) & mask;

	REG_WRITE(ah, reg, regVal);

	if (ah->config.analog_shiftreg)
		udelay(100);
}

int16_t ath9k_hw_interpolate(u16 target, u16 srcLeft, u16 srcRight,
			     int16_t targetLeft, int16_t targetRight)
{
	int16_t rv;

	if (srcRight == srcLeft) {
		rv = targetLeft;
	} else {
		rv = (int16_t) (((target - srcLeft) * targetRight +
				 (srcRight - target) * targetLeft) /
				(srcRight - srcLeft));
	}
	return rv;
}

bool ath9k_hw_get_lower_upper_index(u8 target, u8 *pList, u16 listSize,
				    u16 *indexL, u16 *indexR)
{
	u16 i;

	if (target <= pList[0]) {
		*indexL = *indexR = 0;
		return true;
	}
	if (target >= pList[listSize - 1]) {
		*indexL = *indexR = (u16) (listSize - 1);
		return true;
	}

	for (i = 0; i < listSize - 1; i++) {
		if (pList[i] == target) {
			*indexL = *indexR = i;
			return true;
		}
		if (target < pList[i + 1]) {
			*indexL = i;
			*indexR = (u16) (i + 1);
			return false;
		}
	}
	return false;
}

void ath9k_hw_usb_gen_fill_eeprom(struct ath_hw *ah, u16 *eep_data,
				  int eep_start_loc, int size)
{
	int i = 0, j, addr;
	u32 addrdata[8];
	u32 data[8];

	for (addr = 0; addr < size; addr++) {
		addrdata[i] = AR5416_EEPROM_OFFSET +
			((addr + eep_start_loc) << AR5416_EEPROM_S);
		i++;
		if (i == 8) {
			REG_READ_MULTI(ah, addrdata, data, i);

			for (j = 0; j < i; j++) {
				*eep_data = data[j];
				eep_data++;
			}
			i = 0;
		}
	}

	if (i != 0) {
		REG_READ_MULTI(ah, addrdata, data, i);

		for (j = 0; j < i; j++) {
			*eep_data = data[j];
			eep_data++;
		}
	}
}

bool ath9k_hw_nvram_read(struct ath_common *common, u32 off, u16 *data)
{
	return common->bus_ops->eeprom_read(common, off, data);
}

void ath9k_hw_fill_vpd_table(u8 pwrMin, u8 pwrMax, u8 *pPwrList,
			     u8 *pVpdList, u16 numIntercepts,
			     u8 *pRetVpdList)
{
	u16 i, k;
	u8 currPwr = pwrMin;
	u16 idxL = 0, idxR = 0;

	for (i = 0; i <= (pwrMax - pwrMin) / 2; i++) {
		ath9k_hw_get_lower_upper_index(currPwr, pPwrList,
					       numIntercepts, &(idxL),
					       &(idxR));
		if (idxR < 1)
			idxR = 1;
		if (idxL == numIntercepts - 1)
			idxL = (u16) (numIntercepts - 2);
		if (pPwrList[idxL] == pPwrList[idxR])
			k = pVpdList[idxL];
		else
			k = (u16)(((currPwr - pPwrList[idxL]) * pVpdList[idxR] +
				   (pPwrList[idxR] - currPwr) * pVpdList[idxL]) /
				  (pPwrList[idxR] - pPwrList[idxL]));
		pRetVpdList[i] = (u8) k;
		currPwr += 2;
	}
}

void ath9k_hw_get_legacy_target_powers(struct ath_hw *ah,
				       struct ath9k_channel *chan,
				       struct cal_target_power_leg *powInfo,
				       u16 numChannels,
				       struct cal_target_power_leg *pNewPower,
				       u16 numRates, bool isExtTarget)
{
	struct chan_centers centers;
	u16 clo, chi;
	int i;
	int matchIndex = -1, lowIndex = -1;
	u16 freq;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = (isExtTarget) ? centers.ext_center : centers.ctl_center;

	if (freq <= ath9k_hw_fbin2freq(powInfo[0].bChannel,
				       IS_CHAN_2GHZ(chan))) {
		matchIndex = 0;
	} else {
		for (i = 0; (i < numChannels) &&
			     (powInfo[i].bChannel != AR5416_BCHAN_UNUSED); i++) {
			if (freq == ath9k_hw_fbin2freq(powInfo[i].bChannel,
						       IS_CHAN_2GHZ(chan))) {
				matchIndex = i;
				break;
			} else if (freq < ath9k_hw_fbin2freq(powInfo[i].bChannel,
						IS_CHAN_2GHZ(chan)) && i > 0 &&
				   freq > ath9k_hw_fbin2freq(powInfo[i - 1].bChannel,
						IS_CHAN_2GHZ(chan))) {
				lowIndex = i - 1;
				break;
			}
		}
		if ((matchIndex == -1) && (lowIndex == -1))
			matchIndex = i - 1;
	}

	if (matchIndex != -1) {
		*pNewPower = powInfo[matchIndex];
	} else {
		clo = ath9k_hw_fbin2freq(powInfo[lowIndex].bChannel,
					 IS_CHAN_2GHZ(chan));
		chi = ath9k_hw_fbin2freq(powInfo[lowIndex + 1].bChannel,
					 IS_CHAN_2GHZ(chan));

		for (i = 0; i < numRates; i++) {
			pNewPower->tPow2x[i] =
				(u8)ath9k_hw_interpolate(freq, clo, chi,
						powInfo[lowIndex].tPow2x[i],
						powInfo[lowIndex + 1].tPow2x[i]);
		}
	}
}

void ath9k_hw_get_target_powers(struct ath_hw *ah,
				struct ath9k_channel *chan,
				struct cal_target_power_ht *powInfo,
				u16 numChannels,
				struct cal_target_power_ht *pNewPower,
				u16 numRates, bool isHt40Target)
{
	struct chan_centers centers;
	u16 clo, chi;
	int i;
	int matchIndex = -1, lowIndex = -1;
	u16 freq;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = isHt40Target ? centers.synth_center : centers.ctl_center;

	if (freq <= ath9k_hw_fbin2freq(powInfo[0].bChannel, IS_CHAN_2GHZ(chan))) {
		matchIndex = 0;
	} else {
		for (i = 0; (i < numChannels) &&
			     (powInfo[i].bChannel != AR5416_BCHAN_UNUSED); i++) {
			if (freq == ath9k_hw_fbin2freq(powInfo[i].bChannel,
						       IS_CHAN_2GHZ(chan))) {
				matchIndex = i;
				break;
			} else
				if (freq < ath9k_hw_fbin2freq(powInfo[i].bChannel,
						IS_CHAN_2GHZ(chan)) && i > 0 &&
				    freq > ath9k_hw_fbin2freq(powInfo[i - 1].bChannel,
						IS_CHAN_2GHZ(chan))) {
					lowIndex = i - 1;
					break;
				}
		}
		if ((matchIndex == -1) && (lowIndex == -1))
			matchIndex = i - 1;
	}

	if (matchIndex != -1) {
		*pNewPower = powInfo[matchIndex];
	} else {
		clo = ath9k_hw_fbin2freq(powInfo[lowIndex].bChannel,
					 IS_CHAN_2GHZ(chan));
		chi = ath9k_hw_fbin2freq(powInfo[lowIndex + 1].bChannel,
					 IS_CHAN_2GHZ(chan));

		for (i = 0; i < numRates; i++) {
			pNewPower->tPow2x[i] = (u8)ath9k_hw_interpolate(freq,
						clo, chi,
						powInfo[lowIndex].tPow2x[i],
						powInfo[lowIndex + 1].tPow2x[i]);
		}
	}
}

u16 ath9k_hw_get_max_edge_power(u16 freq, struct cal_ctl_edges *pRdEdgesPower,
				bool is2GHz, int num_band_edges)
{
	u16 twiceMaxEdgePower = MAX_RATE_POWER;
	int i;

	for (i = 0; (i < num_band_edges) &&
		     (pRdEdgesPower[i].bChannel != AR5416_BCHAN_UNUSED); i++) {
		if (freq == ath9k_hw_fbin2freq(pRdEdgesPower[i].bChannel, is2GHz)) {
			twiceMaxEdgePower = CTL_EDGE_TPOWER(pRdEdgesPower[i].ctl);
			break;
		} else if ((i > 0) &&
			   (freq < ath9k_hw_fbin2freq(pRdEdgesPower[i].bChannel,
						      is2GHz))) {
			if (ath9k_hw_fbin2freq(pRdEdgesPower[i - 1].bChannel,
					       is2GHz) < freq &&
			    CTL_EDGE_FLAGS(pRdEdgesPower[i - 1].ctl)) {
				twiceMaxEdgePower =
					CTL_EDGE_TPOWER(pRdEdgesPower[i - 1].ctl);
			}
			break;
		}
	}

	return twiceMaxEdgePower;
}

void ath9k_hw_update_regulatory_maxpower(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);

	switch (ar5416_get_ntxchains(ah->txchainmask)) {
	case 1:
		break;
	case 2:
		regulatory->max_power_level += INCREASE_MAXPOW_BY_TWO_CHAIN;
		break;
	case 3:
		regulatory->max_power_level += INCREASE_MAXPOW_BY_THREE_CHAIN;
		break;
	default:
		ath_dbg(common, ATH_DBG_EEPROM,
			"Invalid chainmask configuration\n");
		break;
	}
}

void ath9k_hw_get_gain_boundaries_pdadcs(struct ath_hw *ah,
				struct ath9k_channel *chan,
				void *pRawDataSet,
				u8 *bChans, u16 availPiers,
				u16 tPdGainOverlap,
				u16 *pPdGainBoundaries, u8 *pPDADCValues,
				u16 numXpdGains)
{
	int i, j, k;
	int16_t ss;
	u16 idxL = 0, idxR = 0, numPiers;
	static u8 vpdTableL[AR5416_NUM_PD_GAINS]
		[AR5416_MAX_PWR_RANGE_IN_HALF_DB];
	static u8 vpdTableR[AR5416_NUM_PD_GAINS]
		[AR5416_MAX_PWR_RANGE_IN_HALF_DB];
	static u8 vpdTableI[AR5416_NUM_PD_GAINS]
		[AR5416_MAX_PWR_RANGE_IN_HALF_DB];

	u8 *pVpdL, *pVpdR, *pPwrL, *pPwrR;
	u8 minPwrT4[AR5416_NUM_PD_GAINS];
	u8 maxPwrT4[AR5416_NUM_PD_GAINS];
	int16_t vpdStep;
	int16_t tmpVal;
	u16 sizeCurrVpdTable, maxIndex, tgtIndex;
	bool match;
	int16_t minDelta = 0;
	struct chan_centers centers;
	int pdgain_boundary_default;
	struct cal_data_per_freq *data_def = pRawDataSet;
	struct cal_data_per_freq_4k *data_4k = pRawDataSet;
	struct cal_data_per_freq_ar9287 *data_9287 = pRawDataSet;
	bool eeprom_4k = AR_SREV_9285(ah) || AR_SREV_9271(ah);
	int intercepts;

	if (AR_SREV_9287(ah))
		intercepts = AR9287_PD_GAIN_ICEPTS;
	else
		intercepts = AR5416_PD_GAIN_ICEPTS;

	memset(&minPwrT4, 0, AR5416_NUM_PD_GAINS);
	ath9k_hw_get_channel_centers(ah, chan, &centers);

	for (numPiers = 0; numPiers < availPiers; numPiers++) {
		if (bChans[numPiers] == AR5416_BCHAN_UNUSED)
			break;
	}

	match = ath9k_hw_get_lower_upper_index((u8)FREQ2FBIN(centers.synth_center,
							     IS_CHAN_2GHZ(chan)),
					       bChans, numPiers, &idxL, &idxR);

	if (match) {
		if (AR_SREV_9287(ah)) {
			/* FIXME: array overrun? */
			for (i = 0; i < numXpdGains; i++) {
				minPwrT4[i] = data_9287[idxL].pwrPdg[i][0];
				maxPwrT4[i] = data_9287[idxL].pwrPdg[i][4];
				ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
						data_9287[idxL].pwrPdg[i],
						data_9287[idxL].vpdPdg[i],
						intercepts,
						vpdTableI[i]);
			}
		} else if (eeprom_4k) {
			for (i = 0; i < numXpdGains; i++) {
				minPwrT4[i] = data_4k[idxL].pwrPdg[i][0];
				maxPwrT4[i] = data_4k[idxL].pwrPdg[i][4];
				ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
						data_4k[idxL].pwrPdg[i],
						data_4k[idxL].vpdPdg[i],
						intercepts,
						vpdTableI[i]);
			}
		} else {
			for (i = 0; i < numXpdGains; i++) {
				minPwrT4[i] = data_def[idxL].pwrPdg[i][0];
				maxPwrT4[i] = data_def[idxL].pwrPdg[i][4];
				ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
						data_def[idxL].pwrPdg[i],
						data_def[idxL].vpdPdg[i],
						intercepts,
						vpdTableI[i]);
			}
		}
	} else {
		for (i = 0; i < numXpdGains; i++) {
			if (AR_SREV_9287(ah)) {
				pVpdL = data_9287[idxL].vpdPdg[i];
				pPwrL = data_9287[idxL].pwrPdg[i];
				pVpdR = data_9287[idxR].vpdPdg[i];
				pPwrR = data_9287[idxR].pwrPdg[i];
			} else if (eeprom_4k) {
				pVpdL = data_4k[idxL].vpdPdg[i];
				pPwrL = data_4k[idxL].pwrPdg[i];
				pVpdR = data_4k[idxR].vpdPdg[i];
				pPwrR = data_4k[idxR].pwrPdg[i];
			} else {
				pVpdL = data_def[idxL].vpdPdg[i];
				pPwrL = data_def[idxL].pwrPdg[i];
				pVpdR = data_def[idxR].vpdPdg[i];
				pPwrR = data_def[idxR].pwrPdg[i];
			}

			minPwrT4[i] = max(pPwrL[0], pPwrR[0]);

			maxPwrT4[i] =
				min(pPwrL[intercepts - 1],
				    pPwrR[intercepts - 1]);


			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
						pPwrL, pVpdL,
						intercepts,
						vpdTableL[i]);
			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
						pPwrR, pVpdR,
						intercepts,
						vpdTableR[i]);

			for (j = 0; j <= (maxPwrT4[i] - minPwrT4[i]) / 2; j++) {
				vpdTableI[i][j] =
					(u8)(ath9k_hw_interpolate((u16)
					     FREQ2FBIN(centers.
						       synth_center,
						       IS_CHAN_2GHZ
						       (chan)),
					     bChans[idxL], bChans[idxR],
					     vpdTableL[i][j], vpdTableR[i][j]));
			}
		}
	}

	k = 0;

	for (i = 0; i < numXpdGains; i++) {
		if (i == (numXpdGains - 1))
			pPdGainBoundaries[i] =
				(u16)(maxPwrT4[i] / 2);
		else
			pPdGainBoundaries[i] =
				(u16)((maxPwrT4[i] + minPwrT4[i + 1]) / 4);

		pPdGainBoundaries[i] =
			min((u16)MAX_RATE_POWER, pPdGainBoundaries[i]);

		minDelta = 0;

		if (i == 0) {
			if (AR_SREV_9280_20_OR_LATER(ah))
				ss = (int16_t)(0 - (minPwrT4[i] / 2));
			else
				ss = 0;
		} else {
			ss = (int16_t)((pPdGainBoundaries[i - 1] -
					(minPwrT4[i] / 2)) -
				       tPdGainOverlap + 1 + minDelta);
		}
		vpdStep = (int16_t)(vpdTableI[i][1] - vpdTableI[i][0]);
		vpdStep = (int16_t)((vpdStep < 1) ? 1 : vpdStep);

		while ((ss < 0) && (k < (AR5416_NUM_PDADC_VALUES - 1))) {
			tmpVal = (int16_t)(vpdTableI[i][0] + ss * vpdStep);
			pPDADCValues[k++] = (u8)((tmpVal < 0) ? 0 : tmpVal);
			ss++;
		}

		sizeCurrVpdTable = (u8) ((maxPwrT4[i] - minPwrT4[i]) / 2 + 1);
		tgtIndex = (u8)(pPdGainBoundaries[i] + tPdGainOverlap -
				(minPwrT4[i] / 2));
		maxIndex = (tgtIndex < sizeCurrVpdTable) ?
			tgtIndex : sizeCurrVpdTable;

		while ((ss < maxIndex) && (k < (AR5416_NUM_PDADC_VALUES - 1))) {
			pPDADCValues[k++] = vpdTableI[i][ss++];
		}

		vpdStep = (int16_t)(vpdTableI[i][sizeCurrVpdTable - 1] -
				    vpdTableI[i][sizeCurrVpdTable - 2]);
		vpdStep = (int16_t)((vpdStep < 1) ? 1 : vpdStep);

		if (tgtIndex >= maxIndex) {
			while ((ss <= tgtIndex) &&
			       (k < (AR5416_NUM_PDADC_VALUES - 1))) {
				tmpVal = (int16_t)((vpdTableI[i][sizeCurrVpdTable - 1] +
						    (ss - maxIndex + 1) * vpdStep));
				pPDADCValues[k++] = (u8)((tmpVal > 255) ?
							 255 : tmpVal);
				ss++;
			}
		}
	}

	if (eeprom_4k)
		pdgain_boundary_default = 58;
	else
		pdgain_boundary_default = pPdGainBoundaries[i - 1];

	while (i < AR5416_PD_GAINS_IN_MASK) {
		pPdGainBoundaries[i] = pdgain_boundary_default;
		i++;
	}

	while (k < AR5416_NUM_PDADC_VALUES) {
		pPDADCValues[k] = pPDADCValues[k - 1];
		k++;
	}
}

int ath9k_hw_eeprom_init(struct ath_hw *ah)
{
	int status;

	if (AR_SREV_9300_20_OR_LATER(ah))
		ah->eep_ops = &eep_ar9300_ops;
	else if (AR_SREV_9287(ah)) {
		ah->eep_ops = &eep_ar9287_ops;
	} else if (AR_SREV_9285(ah) || AR_SREV_9271(ah)) {
		ah->eep_ops = &eep_4k_ops;
	} else {
		ah->eep_ops = &eep_def_ops;
	}

	if (!ah->eep_ops->fill_eeprom(ah))
		return -EIO;

	status = ah->eep_ops->check_eeprom(ah);

	return status;
}
