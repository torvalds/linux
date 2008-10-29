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

static void ath9k_hw_analog_shift_rmw(struct ath_hal *ah,
				      u32 reg, u32 mask,
				      u32 shift, u32 val)
{
	u32 regVal;

	regVal = REG_READ(ah, reg) & ~mask;
	regVal |= (val << shift) & mask;

	REG_WRITE(ah, reg, regVal);

	if (ah->ah_config.analog_shiftreg)
		udelay(100);

	return;
}

static inline u16 ath9k_hw_fbin2freq(u8 fbin, bool is2GHz)
{

	if (fbin == AR5416_BCHAN_UNUSED)
		return fbin;

	return (u16) ((is2GHz) ? (2300 + fbin) : (4800 + 5 * fbin));
}

static inline int16_t ath9k_hw_interpolate(u16 target,
					   u16 srcLeft, u16 srcRight,
					   int16_t targetLeft,
					   int16_t targetRight)
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

static inline bool ath9k_hw_get_lower_upper_index(u8 target, u8 *pList,
						  u16 listSize, u16 *indexL,
						  u16 *indexR)
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

static bool ath9k_hw_eeprom_read(struct ath_hal *ah, u32 off, u16 *data)
{
	(void)REG_READ(ah, AR5416_EEPROM_OFFSET + (off << AR5416_EEPROM_S));

	if (!ath9k_hw_wait(ah,
			   AR_EEPROM_STATUS_DATA,
			   AR_EEPROM_STATUS_DATA_BUSY |
			   AR_EEPROM_STATUS_DATA_PROT_ACCESS, 0)) {
		return false;
	}

	*data = MS(REG_READ(ah, AR_EEPROM_STATUS_DATA),
		   AR_EEPROM_STATUS_DATA_VAL);

	return true;
}

static int ath9k_hw_flash_map(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	ahp->ah_cal_mem = ioremap(AR5416_EEPROM_START_ADDR, AR5416_EEPROM_MAX);

	if (!ahp->ah_cal_mem) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			"%s: cannot remap eeprom region \n", __func__);
		return -EIO;
	}

	return 0;
}

static bool ath9k_hw_flash_read(struct ath_hal *ah, u32 off, u16 *data)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	*data = ioread16(ahp->ah_cal_mem + off);

	return true;
}

static inline bool ath9k_hw_nvram_read(struct ath_hal *ah, u32 off, u16 *data)
{
	if (ath9k_hw_use_flash(ah))
		return ath9k_hw_flash_read(ah, off, data);
	else
		return ath9k_hw_eeprom_read(ah, off, data);
}

static bool ath9k_hw_fill_eeprom(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	u16 *eep_data;
	int addr, ar5416_eep_start_loc = 0;

	if (!ath9k_hw_use_flash(ah)) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			"%s: Reading from EEPROM, not flash\n", __func__);
		ar5416_eep_start_loc = 256;
	}

	if (AR_SREV_9100(ah))
		ar5416_eep_start_loc = 256;

	eep_data = (u16 *)eep;

	for (addr = 0; addr < sizeof(struct ar5416_eeprom) / sizeof(u16); addr++) {
		if (!ath9k_hw_nvram_read(ah, addr + ar5416_eep_start_loc,
					 eep_data)) {
			DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
				"%s: Unable to read eeprom region \n",
				__func__);
			return false;
		}
		eep_data++;
	}
	return true;
}

static int ath9k_hw_check_eeprom(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep =
		(struct ar5416_eeprom *) &ahp->ah_eeprom;
	u16 *eepdata, temp, magic, magic2;
	u32 sum = 0, el;
	bool need_swap = false;
	int i, addr, size;

	if (!ath9k_hw_use_flash(ah)) {
		if (!ath9k_hw_nvram_read(ah, AR5416_EEPROM_MAGIC_OFFSET,
					 &magic)) {
			DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
				"%s: Reading Magic # failed\n", __func__);
			return false;
		}

		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM, "%s: Read Magic = 0x%04X\n",
			 __func__, magic);

		if (magic != AR5416_EEPROM_MAGIC) {
			magic2 = swab16(magic);

			if (magic2 == AR5416_EEPROM_MAGIC) {
				size = sizeof(struct ar5416_eeprom);
				need_swap = true;
				eepdata = (u16 *) (&ahp->ah_eeprom);

				for (addr = 0; addr < size / sizeof(u16); addr++) {
					temp = swab16(*eepdata);
					*eepdata = temp;
					eepdata++;

					DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
						"0x%04X  ", *eepdata);

					if (((addr + 1) % 6) == 0)
						DPRINTF(ah->ah_sc,
							ATH_DBG_EEPROM, "\n");
				}
			} else {
				DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
					"Invalid EEPROM Magic. "
					"endianness mismatch.\n");
				return -EINVAL;
			}
		}
	}

	DPRINTF(ah->ah_sc, ATH_DBG_EEPROM, "need_swap = %s.\n",
		need_swap ? "True" : "False");

	if (need_swap)
		el = swab16(ahp->ah_eeprom.baseEepHeader.length);
	else
		el = ahp->ah_eeprom.baseEepHeader.length;

	if (el > sizeof(struct ar5416_eeprom))
		el = sizeof(struct ar5416_eeprom) / sizeof(u16);
	else
		el = el / sizeof(u16);

	eepdata = (u16 *)(&ahp->ah_eeprom);

	for (i = 0; i < el; i++)
		sum ^= *eepdata++;

	if (need_swap) {
		u32 integer, j;
		u16 word;

		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			"EEPROM Endianness is not native.. Changing \n");

		word = swab16(eep->baseEepHeader.length);
		eep->baseEepHeader.length = word;

		word = swab16(eep->baseEepHeader.checksum);
		eep->baseEepHeader.checksum = word;

		word = swab16(eep->baseEepHeader.version);
		eep->baseEepHeader.version = word;

		word = swab16(eep->baseEepHeader.regDmn[0]);
		eep->baseEepHeader.regDmn[0] = word;

		word = swab16(eep->baseEepHeader.regDmn[1]);
		eep->baseEepHeader.regDmn[1] = word;

		word = swab16(eep->baseEepHeader.rfSilent);
		eep->baseEepHeader.rfSilent = word;

		word = swab16(eep->baseEepHeader.blueToothOptions);
		eep->baseEepHeader.blueToothOptions = word;

		word = swab16(eep->baseEepHeader.deviceCap);
		eep->baseEepHeader.deviceCap = word;

		for (j = 0; j < ARRAY_SIZE(eep->modalHeader); j++) {
			struct modal_eep_header *pModal =
				&eep->modalHeader[j];
			integer = swab32(pModal->antCtrlCommon);
			pModal->antCtrlCommon = integer;

			for (i = 0; i < AR5416_MAX_CHAINS; i++) {
				integer = swab32(pModal->antCtrlChain[i]);
				pModal->antCtrlChain[i] = integer;
			}

			for (i = 0; i < AR5416_EEPROM_MODAL_SPURS; i++) {
				word = swab16(pModal->spurChans[i].spurChan);
				pModal->spurChans[i].spurChan = word;
			}
		}
	}

	if (sum != 0xffff || ar5416_get_eep_ver(ahp) != AR5416_EEP_VER ||
	    ar5416_get_eep_rev(ahp) < AR5416_EEP_NO_BACK_VER) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			"Bad EEPROM checksum 0x%x or revision 0x%04x\n",
			sum, ar5416_get_eep_ver(ahp));
		return -EINVAL;
	}

	return 0;
}

static inline bool ath9k_hw_fill_vpd_table(u8 pwrMin, u8 pwrMax, u8 *pPwrList,
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

	return true;
}

static void ath9k_hw_get_gain_boundaries_pdadcs(struct ath_hal *ah,
				struct ath9k_channel *chan,
				struct cal_data_per_freq *pRawDataSet,
				u8 *bChans, u16 availPiers,
				u16 tPdGainOverlap, int16_t *pMinCalPower,
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

	ath9k_hw_get_channel_centers(ah, chan, &centers);

	for (numPiers = 0; numPiers < availPiers; numPiers++) {
		if (bChans[numPiers] == AR5416_BCHAN_UNUSED)
			break;
	}

	match = ath9k_hw_get_lower_upper_index((u8)FREQ2FBIN(centers.synth_center,
							     IS_CHAN_2GHZ(chan)),
					       bChans, numPiers, &idxL, &idxR);

	if (match) {
		for (i = 0; i < numXpdGains; i++) {
			minPwrT4[i] = pRawDataSet[idxL].pwrPdg[i][0];
			maxPwrT4[i] = pRawDataSet[idxL].pwrPdg[i][4];
			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
					pRawDataSet[idxL].pwrPdg[i],
					pRawDataSet[idxL].vpdPdg[i],
					AR5416_PD_GAIN_ICEPTS,
					vpdTableI[i]);
		}
	} else {
		for (i = 0; i < numXpdGains; i++) {
			pVpdL = pRawDataSet[idxL].vpdPdg[i];
			pPwrL = pRawDataSet[idxL].pwrPdg[i];
			pVpdR = pRawDataSet[idxR].vpdPdg[i];
			pPwrR = pRawDataSet[idxR].pwrPdg[i];

			minPwrT4[i] = max(pPwrL[0], pPwrR[0]);

			maxPwrT4[i] =
				min(pPwrL[AR5416_PD_GAIN_ICEPTS - 1],
				    pPwrR[AR5416_PD_GAIN_ICEPTS - 1]);


			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
						pPwrL, pVpdL,
						AR5416_PD_GAIN_ICEPTS,
						vpdTableL[i]);
			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
						pPwrR, pVpdR,
						AR5416_PD_GAIN_ICEPTS,
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

	*pMinCalPower = (int16_t)(minPwrT4[0] / 2);

	k = 0;

	for (i = 0; i < numXpdGains; i++) {
		if (i == (numXpdGains - 1))
			pPdGainBoundaries[i] =
				(u16)(maxPwrT4[i] / 2);
		else
			pPdGainBoundaries[i] =
				(u16)((maxPwrT4[i] + minPwrT4[i + 1]) / 4);

		pPdGainBoundaries[i] =
			min((u16)AR5416_MAX_RATE_POWER, pPdGainBoundaries[i]);

		if ((i == 0) && !AR_SREV_5416_V20_OR_LATER(ah)) {
			minDelta = pPdGainBoundaries[0] - 23;
			pPdGainBoundaries[0] = 23;
		} else {
			minDelta = 0;
		}

		if (i == 0) {
			if (AR_SREV_9280_10_OR_LATER(ah))
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

		if (tgtIndex > maxIndex) {
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

	while (i < AR5416_PD_GAINS_IN_MASK) {
		pPdGainBoundaries[i] = pPdGainBoundaries[i - 1];
		i++;
	}

	while (k < AR5416_NUM_PDADC_VALUES) {
		pPDADCValues[k] = pPDADCValues[k - 1];
		k++;
	}

	return;
}

static void ath9k_hw_get_legacy_target_powers(struct ath_hal *ah,
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
			} else if ((freq < ath9k_hw_fbin2freq(powInfo[i].bChannel,
						      IS_CHAN_2GHZ(chan))) &&
				   (freq > ath9k_hw_fbin2freq(powInfo[i - 1].bChannel,
						      IS_CHAN_2GHZ(chan)))) {
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

static void ath9k_hw_get_target_powers(struct ath_hal *ah,
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
				if ((freq < ath9k_hw_fbin2freq(powInfo[i].bChannel,
						       IS_CHAN_2GHZ(chan))) &&
				    (freq > ath9k_hw_fbin2freq(powInfo[i - 1].bChannel,
						       IS_CHAN_2GHZ(chan)))) {
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

static u16 ath9k_hw_get_max_edge_power(u16 freq,
				       struct cal_ctl_edges *pRdEdgesPower,
				       bool is2GHz)
{
	u16 twiceMaxEdgePower = AR5416_MAX_RATE_POWER;
	int i;

	for (i = 0; (i < AR5416_NUM_BAND_EDGES) &&
		     (pRdEdgesPower[i].bChannel != AR5416_BCHAN_UNUSED); i++) {
		if (freq == ath9k_hw_fbin2freq(pRdEdgesPower[i].bChannel, is2GHz)) {
			twiceMaxEdgePower = pRdEdgesPower[i].tPower;
			break;
		} else if ((i > 0) &&
			   (freq < ath9k_hw_fbin2freq(pRdEdgesPower[i].bChannel,
						      is2GHz))) {
			if (ath9k_hw_fbin2freq(pRdEdgesPower[i - 1].bChannel,
					       is2GHz) < freq &&
			    pRdEdgesPower[i - 1].flag) {
				twiceMaxEdgePower =
					pRdEdgesPower[i - 1].tPower;
			}
			break;
		}
	}

	return twiceMaxEdgePower;
}

int ath9k_hw_set_txpower(struct ath_hal *ah,
			 struct ath9k_channel *chan,
			 u16 cfgCtl,
			 u8 twiceAntennaReduction,
			 u8 twiceMaxRegulatoryPower,
			 u8 powerLimit)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *pEepData = &ahp->ah_eeprom;
	struct modal_eep_header *pModal =
		&(pEepData->modalHeader[IS_CHAN_2GHZ(chan)]);
	int16_t ratesArray[Ar5416RateSize];
	int16_t txPowerIndexOffset = 0;
	u8 ht40PowerIncForPdadc = 2;
	int i;

	memset(ratesArray, 0, sizeof(ratesArray));

	if ((pEepData->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_2) {
		ht40PowerIncForPdadc = pModal->ht40PowerIncForPdadc;
	}

	if (!ath9k_hw_set_power_per_rate_table(ah, chan,
					       &ratesArray[0], cfgCtl,
					       twiceAntennaReduction,
					       twiceMaxRegulatoryPower,
					       powerLimit)) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			"ath9k_hw_set_txpower: unable to set "
			"tx power per rate table\n");
		return -EIO;
	}

	if (!ath9k_hw_set_power_cal_table(ah, chan, &txPowerIndexOffset)) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			 "ath9k_hw_set_txpower: unable to set power table\n");
		return -EIO;
	}

	for (i = 0; i < ARRAY_SIZE(ratesArray); i++) {
		ratesArray[i] =	(int16_t)(txPowerIndexOffset + ratesArray[i]);
		if (ratesArray[i] > AR5416_MAX_RATE_POWER)
			ratesArray[i] = AR5416_MAX_RATE_POWER;
	}

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		for (i = 0; i < Ar5416RateSize; i++)
			ratesArray[i] -= AR5416_PWR_TABLE_OFFSET * 2;
	}

	REG_WRITE(ah, AR_PHY_POWER_TX_RATE1,
		  ATH9K_POW_SM(ratesArray[rate18mb], 24)
		  | ATH9K_POW_SM(ratesArray[rate12mb], 16)
		  | ATH9K_POW_SM(ratesArray[rate9mb], 8)
		  | ATH9K_POW_SM(ratesArray[rate6mb], 0));
	REG_WRITE(ah, AR_PHY_POWER_TX_RATE2,
		  ATH9K_POW_SM(ratesArray[rate54mb], 24)
		  | ATH9K_POW_SM(ratesArray[rate48mb], 16)
		  | ATH9K_POW_SM(ratesArray[rate36mb], 8)
		  | ATH9K_POW_SM(ratesArray[rate24mb], 0));

	if (IS_CHAN_2GHZ(chan)) {
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE3,
			  ATH9K_POW_SM(ratesArray[rate2s], 24)
			  | ATH9K_POW_SM(ratesArray[rate2l], 16)
			  | ATH9K_POW_SM(ratesArray[rateXr], 8)
			  | ATH9K_POW_SM(ratesArray[rate1l], 0));
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE4,
			  ATH9K_POW_SM(ratesArray[rate11s], 24)
			  | ATH9K_POW_SM(ratesArray[rate11l], 16)
			  | ATH9K_POW_SM(ratesArray[rate5_5s], 8)
			  | ATH9K_POW_SM(ratesArray[rate5_5l], 0));
	}

	REG_WRITE(ah, AR_PHY_POWER_TX_RATE5,
		  ATH9K_POW_SM(ratesArray[rateHt20_3], 24)
		  | ATH9K_POW_SM(ratesArray[rateHt20_2], 16)
		  | ATH9K_POW_SM(ratesArray[rateHt20_1], 8)
		  | ATH9K_POW_SM(ratesArray[rateHt20_0], 0));
	REG_WRITE(ah, AR_PHY_POWER_TX_RATE6,
		  ATH9K_POW_SM(ratesArray[rateHt20_7], 24)
		  | ATH9K_POW_SM(ratesArray[rateHt20_6], 16)
		  | ATH9K_POW_SM(ratesArray[rateHt20_5], 8)
		  | ATH9K_POW_SM(ratesArray[rateHt20_4], 0));

	if (IS_CHAN_HT40(chan)) {
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE7,
			  ATH9K_POW_SM(ratesArray[rateHt40_3] +
				       ht40PowerIncForPdadc, 24)
			  | ATH9K_POW_SM(ratesArray[rateHt40_2] +
					 ht40PowerIncForPdadc, 16)
			  | ATH9K_POW_SM(ratesArray[rateHt40_1] +
					 ht40PowerIncForPdadc, 8)
			  | ATH9K_POW_SM(ratesArray[rateHt40_0] +
					 ht40PowerIncForPdadc, 0));
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE8,
			  ATH9K_POW_SM(ratesArray[rateHt40_7] +
				       ht40PowerIncForPdadc, 24)
			  | ATH9K_POW_SM(ratesArray[rateHt40_6] +
					 ht40PowerIncForPdadc, 16)
			  | ATH9K_POW_SM(ratesArray[rateHt40_5] +
					 ht40PowerIncForPdadc, 8)
			  | ATH9K_POW_SM(ratesArray[rateHt40_4] +
					 ht40PowerIncForPdadc, 0));

		REG_WRITE(ah, AR_PHY_POWER_TX_RATE9,
			  ATH9K_POW_SM(ratesArray[rateExtOfdm], 24)
			  | ATH9K_POW_SM(ratesArray[rateExtCck], 16)
			  | ATH9K_POW_SM(ratesArray[rateDupOfdm], 8)
			  | ATH9K_POW_SM(ratesArray[rateDupCck], 0));
	}

	REG_WRITE(ah, AR_PHY_POWER_TX_SUB,
		  ATH9K_POW_SM(pModal->pwrDecreaseFor3Chain, 6)
		  | ATH9K_POW_SM(pModal->pwrDecreaseFor2Chain, 0));

	i = rate6mb;

	if (IS_CHAN_HT40(chan))
		i = rateHt40_0;
	else if (IS_CHAN_HT20(chan))
		i = rateHt20_0;

	if (AR_SREV_9280_10_OR_LATER(ah))
		ah->ah_maxPowerLevel =
			ratesArray[i] + AR5416_PWR_TABLE_OFFSET * 2;
	else
		ah->ah_maxPowerLevel = ratesArray[i];

	return 0;
}

void ath9k_hw_set_addac(struct ath_hal *ah, struct ath9k_channel *chan)
{
	struct modal_eep_header *pModal;
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	u8 biaslevel;

	if (ah->ah_macVersion != AR_SREV_VERSION_9160)
		return;

	if (ar5416_get_eep_rev(ahp) < AR5416_EEP_MINOR_VER_7)
		return;

	pModal = &(eep->modalHeader[IS_CHAN_2GHZ(chan)]);

	if (pModal->xpaBiasLvl != 0xff) {
		biaslevel = pModal->xpaBiasLvl;
	} else {
		u16 resetFreqBin, freqBin, freqCount = 0;
		struct chan_centers centers;

		ath9k_hw_get_channel_centers(ah, chan, &centers);

		resetFreqBin = FREQ2FBIN(centers.synth_center, IS_CHAN_2GHZ(chan));
		freqBin = pModal->xpaBiasLvlFreq[0] & 0xff;
		biaslevel = (u8) (pModal->xpaBiasLvlFreq[0] >> 14);

		freqCount++;

		while (freqCount < 3) {
			if (pModal->xpaBiasLvlFreq[freqCount] == 0x0)
				break;

			freqBin = pModal->xpaBiasLvlFreq[freqCount] & 0xff;
			if (resetFreqBin >= freqBin) {
				biaslevel = (u8)(pModal->xpaBiasLvlFreq[freqCount] >> 14);
			} else {
				break;
			}
			freqCount++;
		}
	}

	if (IS_CHAN_2GHZ(chan)) {
		INI_RA(&ahp->ah_iniAddac, 7, 1) =
			(INI_RA(&ahp->ah_iniAddac, 7, 1) & (~0x18)) | biaslevel	<< 3;
	} else {
		INI_RA(&ahp->ah_iniAddac, 6, 1) =
			(INI_RA(&ahp->ah_iniAddac, 6, 1) & (~0xc0)) | biaslevel	<< 6;
	}
}

bool ath9k_hw_set_power_per_rate_table(struct ath_hal *ah,
				       struct ath9k_channel *chan,
				       int16_t *ratesArray,
				       u16 cfgCtl,
				       u8 AntennaReduction,
				       u8 twiceMaxRegulatoryPower,
				       u8 powerLimit)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *pEepData = &ahp->ah_eeprom;
	u8 twiceMaxEdgePower = AR5416_MAX_RATE_POWER;
	static const u16 tpScaleReductionTable[5] =
		{ 0, 3, 6, 9, AR5416_MAX_RATE_POWER };

	int i;
	int8_t twiceLargestAntenna;
	struct cal_ctl_data *rep;
	struct cal_target_power_leg targetPowerOfdm, targetPowerCck = {
		0, { 0, 0, 0, 0}
	};
	struct cal_target_power_leg targetPowerOfdmExt = {
		0, { 0, 0, 0, 0} }, targetPowerCckExt = {
		0, { 0, 0, 0, 0 }
	};
	struct cal_target_power_ht targetPowerHt20, targetPowerHt40 = {
		0, {0, 0, 0, 0}
	};
	u8 scaledPower = 0, minCtlPower, maxRegAllowedPower;
	u16 ctlModesFor11a[] =
		{ CTL_11A, CTL_5GHT20, CTL_11A_EXT, CTL_5GHT40 };
	u16 ctlModesFor11g[] =
		{ CTL_11B, CTL_11G, CTL_2GHT20, CTL_11B_EXT, CTL_11G_EXT,
		  CTL_2GHT40
		};
	u16 numCtlModes, *pCtlMode, ctlMode, freq;
	struct chan_centers centers;
	int tx_chainmask;
	u8 twiceMinEdgePower;

	tx_chainmask = ahp->ah_txchainmask;

	ath9k_hw_get_channel_centers(ah, chan, &centers);

	twiceLargestAntenna = max(
		pEepData->modalHeader
			[IS_CHAN_2GHZ(chan)].antennaGainCh[0],
		pEepData->modalHeader
			[IS_CHAN_2GHZ(chan)].antennaGainCh[1]);

	twiceLargestAntenna = max((u8)twiceLargestAntenna,
				  pEepData->modalHeader
				  [IS_CHAN_2GHZ(chan)].antennaGainCh[2]);

	twiceLargestAntenna = (int8_t)min(AntennaReduction - twiceLargestAntenna, 0);

	maxRegAllowedPower = twiceMaxRegulatoryPower + twiceLargestAntenna;

	if (ah->ah_tpScale != ATH9K_TP_SCALE_MAX) {
		maxRegAllowedPower -=
			(tpScaleReductionTable[(ah->ah_tpScale)] * 2);
	}

	scaledPower = min(powerLimit, maxRegAllowedPower);

	switch (ar5416_get_ntxchains(tx_chainmask)) {
	case 1:
		break;
	case 2:
		scaledPower -=
			pEepData->modalHeader[IS_CHAN_2GHZ(chan)].pwrDecreaseFor2Chain;
		break;
	case 3:
		scaledPower -=
			pEepData->modalHeader[IS_CHAN_2GHZ(chan)].pwrDecreaseFor3Chain;
		break;
	}

	scaledPower = max(0, (int32_t) scaledPower);

	if (IS_CHAN_2GHZ(chan)) {
		numCtlModes = ARRAY_SIZE(ctlModesFor11g) -
			SUB_NUM_CTL_MODES_AT_2G_40;
		pCtlMode = ctlModesFor11g;

		ath9k_hw_get_legacy_target_powers(ah, chan,
			pEepData->calTargetPowerCck,
			AR5416_NUM_2G_CCK_TARGET_POWERS,
			&targetPowerCck, 4, false);
		ath9k_hw_get_legacy_target_powers(ah, chan,
			pEepData->calTargetPower2G,
			AR5416_NUM_2G_20_TARGET_POWERS,
			&targetPowerOfdm, 4, false);
		ath9k_hw_get_target_powers(ah, chan,
			pEepData->calTargetPower2GHT20,
			AR5416_NUM_2G_20_TARGET_POWERS,
			&targetPowerHt20, 8, false);

		if (IS_CHAN_HT40(chan)) {
			numCtlModes = ARRAY_SIZE(ctlModesFor11g);
			ath9k_hw_get_target_powers(ah, chan,
				pEepData->calTargetPower2GHT40,
				AR5416_NUM_2G_40_TARGET_POWERS,
				&targetPowerHt40, 8, true);
			ath9k_hw_get_legacy_target_powers(ah, chan,
				pEepData->calTargetPowerCck,
				AR5416_NUM_2G_CCK_TARGET_POWERS,
				&targetPowerCckExt, 4, true);
			ath9k_hw_get_legacy_target_powers(ah, chan,
				pEepData->calTargetPower2G,
				AR5416_NUM_2G_20_TARGET_POWERS,
				&targetPowerOfdmExt, 4, true);
		}
	} else {
		numCtlModes = ARRAY_SIZE(ctlModesFor11a) -
			SUB_NUM_CTL_MODES_AT_5G_40;
		pCtlMode = ctlModesFor11a;

		ath9k_hw_get_legacy_target_powers(ah, chan,
			pEepData->calTargetPower5G,
			AR5416_NUM_5G_20_TARGET_POWERS,
			&targetPowerOfdm, 4, false);
		ath9k_hw_get_target_powers(ah, chan,
			pEepData->calTargetPower5GHT20,
			AR5416_NUM_5G_20_TARGET_POWERS,
			&targetPowerHt20, 8, false);

		if (IS_CHAN_HT40(chan)) {
			numCtlModes = ARRAY_SIZE(ctlModesFor11a);
			ath9k_hw_get_target_powers(ah, chan,
				pEepData->calTargetPower5GHT40,
				AR5416_NUM_5G_40_TARGET_POWERS,
				&targetPowerHt40, 8, true);
			ath9k_hw_get_legacy_target_powers(ah, chan,
				pEepData->calTargetPower5G,
				AR5416_NUM_5G_20_TARGET_POWERS,
				&targetPowerOfdmExt, 4, true);
		}
	}

	for (ctlMode = 0; ctlMode < numCtlModes; ctlMode++) {
		bool isHt40CtlMode = (pCtlMode[ctlMode] == CTL_5GHT40) ||
			(pCtlMode[ctlMode] == CTL_2GHT40);
		if (isHt40CtlMode)
			freq = centers.synth_center;
		else if (pCtlMode[ctlMode] & EXT_ADDITIVE)
			freq = centers.ext_center;
		else
			freq = centers.ctl_center;

		if (ar5416_get_eep_ver(ahp) == 14 && ar5416_get_eep_rev(ahp) <= 2)
			twiceMaxEdgePower = AR5416_MAX_RATE_POWER;

		DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
			"LOOP-Mode ctlMode %d < %d, isHt40CtlMode %d, "
			"EXT_ADDITIVE %d\n",
			ctlMode, numCtlModes, isHt40CtlMode,
			(pCtlMode[ctlMode] & EXT_ADDITIVE));

		for (i = 0; (i < AR5416_NUM_CTLS) && pEepData->ctlIndex[i]; i++) {
			DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
				"  LOOP-Ctlidx %d: cfgCtl 0x%2.2x "
				"pCtlMode 0x%2.2x ctlIndex 0x%2.2x "
				"chan %d\n",
				i, cfgCtl, pCtlMode[ctlMode],
				pEepData->ctlIndex[i], chan->channel);

			if ((((cfgCtl & ~CTL_MODE_M) |
			      (pCtlMode[ctlMode] & CTL_MODE_M)) ==
			     pEepData->ctlIndex[i]) ||
			    (((cfgCtl & ~CTL_MODE_M) |
			      (pCtlMode[ctlMode] & CTL_MODE_M)) ==
			     ((pEepData->ctlIndex[i] & CTL_MODE_M) | SD_NO_CTL))) {
				rep = &(pEepData->ctlData[i]);

				twiceMinEdgePower = ath9k_hw_get_max_edge_power(freq,
				rep->ctlEdges[ar5416_get_ntxchains(tx_chainmask) - 1],
				IS_CHAN_2GHZ(chan));

				DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
					"    MATCH-EE_IDX %d: ch %d is2 %d "
					"2xMinEdge %d chainmask %d chains %d\n",
					i, freq, IS_CHAN_2GHZ(chan),
					twiceMinEdgePower, tx_chainmask,
					ar5416_get_ntxchains
					(tx_chainmask));
				if ((cfgCtl & ~CTL_MODE_M) == SD_NO_CTL) {
					twiceMaxEdgePower = min(twiceMaxEdgePower,
								twiceMinEdgePower);
				} else {
					twiceMaxEdgePower = twiceMinEdgePower;
					break;
				}
			}
		}

		minCtlPower = min(twiceMaxEdgePower, scaledPower);

		DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
			"    SEL-Min ctlMode %d pCtlMode %d "
			"2xMaxEdge %d sP %d minCtlPwr %d\n",
			ctlMode, pCtlMode[ctlMode], twiceMaxEdgePower,
			scaledPower, minCtlPower);

		switch (pCtlMode[ctlMode]) {
		case CTL_11B:
			for (i = 0; i < ARRAY_SIZE(targetPowerCck.tPow2x); i++) {
				targetPowerCck.tPow2x[i] =
					min(targetPowerCck.tPow2x[i],
					    minCtlPower);
			}
			break;
		case CTL_11A:
		case CTL_11G:
			for (i = 0; i < ARRAY_SIZE(targetPowerOfdm.tPow2x); i++) {
				targetPowerOfdm.tPow2x[i] =
					min(targetPowerOfdm.tPow2x[i],
					    minCtlPower);
			}
			break;
		case CTL_5GHT20:
		case CTL_2GHT20:
			for (i = 0; i < ARRAY_SIZE(targetPowerHt20.tPow2x); i++) {
				targetPowerHt20.tPow2x[i] =
					min(targetPowerHt20.tPow2x[i],
					    minCtlPower);
			}
			break;
		case CTL_11B_EXT:
			targetPowerCckExt.tPow2x[0] =
				min(targetPowerCckExt.tPow2x[0], minCtlPower);
			break;
		case CTL_11A_EXT:
		case CTL_11G_EXT:
			targetPowerOfdmExt.tPow2x[0] =
				min(targetPowerOfdmExt.tPow2x[0], minCtlPower);
			break;
		case CTL_5GHT40:
		case CTL_2GHT40:
			for (i = 0; i < ARRAY_SIZE(targetPowerHt40.tPow2x); i++) {
				targetPowerHt40.tPow2x[i] =
					min(targetPowerHt40.tPow2x[i],
					    minCtlPower);
			}
			break;
		default:
			break;
		}
	}

	ratesArray[rate6mb] = ratesArray[rate9mb] = ratesArray[rate12mb] =
		ratesArray[rate18mb] = ratesArray[rate24mb] =
		targetPowerOfdm.tPow2x[0];
	ratesArray[rate36mb] = targetPowerOfdm.tPow2x[1];
	ratesArray[rate48mb] = targetPowerOfdm.tPow2x[2];
	ratesArray[rate54mb] = targetPowerOfdm.tPow2x[3];
	ratesArray[rateXr] = targetPowerOfdm.tPow2x[0];

	for (i = 0; i < ARRAY_SIZE(targetPowerHt20.tPow2x); i++)
		ratesArray[rateHt20_0 + i] = targetPowerHt20.tPow2x[i];

	if (IS_CHAN_2GHZ(chan)) {
		ratesArray[rate1l] = targetPowerCck.tPow2x[0];
		ratesArray[rate2s] = ratesArray[rate2l] =
			targetPowerCck.tPow2x[1];
		ratesArray[rate5_5s] = ratesArray[rate5_5l] =
			targetPowerCck.tPow2x[2];
		;
		ratesArray[rate11s] = ratesArray[rate11l] =
			targetPowerCck.tPow2x[3];
		;
	}
	if (IS_CHAN_HT40(chan)) {
		for (i = 0; i < ARRAY_SIZE(targetPowerHt40.tPow2x); i++) {
			ratesArray[rateHt40_0 + i] =
				targetPowerHt40.tPow2x[i];
		}
		ratesArray[rateDupOfdm] = targetPowerHt40.tPow2x[0];
		ratesArray[rateDupCck] = targetPowerHt40.tPow2x[0];
		ratesArray[rateExtOfdm] = targetPowerOfdmExt.tPow2x[0];
		if (IS_CHAN_2GHZ(chan)) {
			ratesArray[rateExtCck] =
				targetPowerCckExt.tPow2x[0];
		}
	}
	return true;
}

bool ath9k_hw_set_power_cal_table(struct ath_hal *ah,
				  struct ath9k_channel *chan,
				  int16_t *pTxPowerIndexOffset)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *pEepData = &ahp->ah_eeprom;
	struct cal_data_per_freq *pRawDataset;
	u8 *pCalBChans = NULL;
	u16 pdGainOverlap_t2;
	static u8 pdadcValues[AR5416_NUM_PDADC_VALUES];
	u16 gainBoundaries[AR5416_PD_GAINS_IN_MASK];
	u16 numPiers, i, j;
	int16_t tMinCalPower;
	u16 numXpdGain, xpdMask;
	u16 xpdGainValues[AR5416_NUM_PD_GAINS] = { 0, 0, 0, 0 };
	u32 reg32, regOffset, regChainOffset;
	int16_t modalIdx;

	modalIdx = IS_CHAN_2GHZ(chan) ? 1 : 0;
	xpdMask = pEepData->modalHeader[modalIdx].xpdGain;

	if ((pEepData->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_2) {
		pdGainOverlap_t2 =
			pEepData->modalHeader[modalIdx].pdGainOverlap;
	} else {
		pdGainOverlap_t2 = (u16)(MS(REG_READ(ah, AR_PHY_TPCRG5),
					    AR_PHY_TPCRG5_PD_GAIN_OVERLAP));
	}

	if (IS_CHAN_2GHZ(chan)) {
		pCalBChans = pEepData->calFreqPier2G;
		numPiers = AR5416_NUM_2G_CAL_PIERS;
	} else {
		pCalBChans = pEepData->calFreqPier5G;
		numPiers = AR5416_NUM_5G_CAL_PIERS;
	}

	numXpdGain = 0;

	for (i = 1; i <= AR5416_PD_GAINS_IN_MASK; i++) {
		if ((xpdMask >> (AR5416_PD_GAINS_IN_MASK - i)) & 1) {
			if (numXpdGain >= AR5416_NUM_PD_GAINS)
				break;
			xpdGainValues[numXpdGain] =
				(u16)(AR5416_PD_GAINS_IN_MASK - i);
			numXpdGain++;
		}
	}

	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_NUM_PD_GAIN,
		      (numXpdGain - 1) & 0x3);
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_1,
		      xpdGainValues[0]);
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_2,
		      xpdGainValues[1]);
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_3,
		      xpdGainValues[2]);

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		if (AR_SREV_5416_V20_OR_LATER(ah) &&
		    (ahp->ah_rxchainmask == 5 || ahp->ah_txchainmask == 5) &&
		    (i != 0)) {
			regChainOffset = (i == 1) ? 0x2000 : 0x1000;
		} else
			regChainOffset = i * 0x1000;

		if (pEepData->baseEepHeader.txMask & (1 << i)) {
			if (IS_CHAN_2GHZ(chan))
				pRawDataset = pEepData->calPierData2G[i];
			else
				pRawDataset = pEepData->calPierData5G[i];

			ath9k_hw_get_gain_boundaries_pdadcs(ah, chan,
					    pRawDataset, pCalBChans,
					    numPiers, pdGainOverlap_t2,
					    &tMinCalPower, gainBoundaries,
					    pdadcValues, numXpdGain);

			if ((i == 0) || AR_SREV_5416_V20_OR_LATER(ah)) {
				REG_WRITE(ah,
					  AR_PHY_TPCRG5 + regChainOffset,
					  SM(pdGainOverlap_t2,
					     AR_PHY_TPCRG5_PD_GAIN_OVERLAP)
					  | SM(gainBoundaries[0],
					       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1)
					  | SM(gainBoundaries[1],
					       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2)
					  | SM(gainBoundaries[2],
					       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3)
					  | SM(gainBoundaries[3],
				       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4));
			}

			regOffset = AR_PHY_BASE + (672 << 2) + regChainOffset;
			for (j = 0; j < 32; j++) {
				reg32 = ((pdadcValues[4 * j + 0] & 0xFF) << 0) |
					((pdadcValues[4 * j + 1] & 0xFF) << 8) |
					((pdadcValues[4 * j + 2] & 0xFF) << 16) |
					((pdadcValues[4 * j + 3] & 0xFF) << 24);
				REG_WRITE(ah, regOffset, reg32);

				DPRINTF(ah->ah_sc, ATH_DBG_PHY_IO,
					"PDADC (%d,%4x): %4.4x %8.8x\n",
					i, regChainOffset, regOffset,
					reg32);
				DPRINTF(ah->ah_sc, ATH_DBG_PHY_IO,
					"PDADC: Chain %d | PDADC %3d Value %3d | "
					"PDADC %3d Value %3d | PDADC %3d Value %3d | "
					"PDADC %3d Value %3d |\n",
					i, 4 * j, pdadcValues[4 * j],
					4 * j + 1, pdadcValues[4 * j + 1],
					4 * j + 2, pdadcValues[4 * j + 2],
					4 * j + 3,
					pdadcValues[4 * j + 3]);

				regOffset += 4;
			}
		}
	}

	*pTxPowerIndexOffset = 0;

	return true;
}

/* XXX: Clean me up, make me more legible */
bool ath9k_hw_eeprom_set_board_values(struct ath_hal *ah,
				      struct ath9k_channel *chan)
{
	struct modal_eep_header *pModal;
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	int i, regChainOffset;
	u8 txRxAttenLocal;
	u16 ant_config;

	pModal = &(eep->modalHeader[IS_CHAN_2GHZ(chan)]);

	txRxAttenLocal = IS_CHAN_2GHZ(chan) ? 23 : 44;

	ath9k_hw_get_eeprom_antenna_cfg(ah, chan, 1, &ant_config);
	REG_WRITE(ah, AR_PHY_SWITCH_COM, ant_config);

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		if (AR_SREV_9280(ah)) {
			if (i >= 2)
				break;
		}

		if (AR_SREV_5416_V20_OR_LATER(ah) &&
		    (ahp->ah_rxchainmask == 5 || ahp->ah_txchainmask == 5)
		    && (i != 0))
			regChainOffset = (i == 1) ? 0x2000 : 0x1000;
		else
			regChainOffset = i * 0x1000;

		REG_WRITE(ah, AR_PHY_SWITCH_CHAIN_0 + regChainOffset,
			  pModal->antCtrlChain[i]);

		REG_WRITE(ah, AR_PHY_TIMING_CTRL4(0) + regChainOffset,
			  (REG_READ(ah,
				    AR_PHY_TIMING_CTRL4(0) +
				    regChainOffset) &
			   ~(AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF |
			     AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF)) |
			  SM(pModal->iqCalICh[i],
			     AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF) |
			  SM(pModal->iqCalQCh[i],
			     AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF));

		if ((i == 0) || AR_SREV_5416_V20_OR_LATER(ah)) {
			if ((eep->baseEepHeader.version &
			     AR5416_EEP_VER_MINOR_MASK) >=
			    AR5416_EEP_MINOR_VER_3) {
				txRxAttenLocal = pModal->txRxAttenCh[i];
				if (AR_SREV_9280_10_OR_LATER(ah)) {
					REG_RMW_FIELD(ah,
						AR_PHY_GAIN_2GHZ +
						regChainOffset,
						AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN,
						pModal->
						bswMargin[i]);
					REG_RMW_FIELD(ah,
						AR_PHY_GAIN_2GHZ +
						regChainOffset,
						AR_PHY_GAIN_2GHZ_XATTEN1_DB,
						pModal->
						bswAtten[i]);
					REG_RMW_FIELD(ah,
						AR_PHY_GAIN_2GHZ +
						regChainOffset,
						AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN,
						pModal->
						xatten2Margin[i]);
					REG_RMW_FIELD(ah,
						AR_PHY_GAIN_2GHZ +
						regChainOffset,
						AR_PHY_GAIN_2GHZ_XATTEN2_DB,
						pModal->
						xatten2Db[i]);
				} else {
					REG_WRITE(ah,
						  AR_PHY_GAIN_2GHZ +
						  regChainOffset,
						  (REG_READ(ah,
							    AR_PHY_GAIN_2GHZ +
							    regChainOffset) &
						   ~AR_PHY_GAIN_2GHZ_BSW_MARGIN)
						  | SM(pModal->
						  bswMargin[i],
						  AR_PHY_GAIN_2GHZ_BSW_MARGIN));
					REG_WRITE(ah,
						  AR_PHY_GAIN_2GHZ +
						  regChainOffset,
						  (REG_READ(ah,
							    AR_PHY_GAIN_2GHZ +
							    regChainOffset) &
						   ~AR_PHY_GAIN_2GHZ_BSW_ATTEN)
						  | SM(pModal->bswAtten[i],
						  AR_PHY_GAIN_2GHZ_BSW_ATTEN));
				}
			}
			if (AR_SREV_9280_10_OR_LATER(ah)) {
				REG_RMW_FIELD(ah,
					      AR_PHY_RXGAIN +
					      regChainOffset,
					      AR9280_PHY_RXGAIN_TXRX_ATTEN,
					      txRxAttenLocal);
				REG_RMW_FIELD(ah,
					      AR_PHY_RXGAIN +
					      regChainOffset,
					      AR9280_PHY_RXGAIN_TXRX_MARGIN,
					      pModal->rxTxMarginCh[i]);
			} else {
				REG_WRITE(ah,
					  AR_PHY_RXGAIN + regChainOffset,
					  (REG_READ(ah,
						    AR_PHY_RXGAIN +
						    regChainOffset) &
					   ~AR_PHY_RXGAIN_TXRX_ATTEN) |
					  SM(txRxAttenLocal,
					     AR_PHY_RXGAIN_TXRX_ATTEN));
				REG_WRITE(ah,
					  AR_PHY_GAIN_2GHZ +
					  regChainOffset,
					  (REG_READ(ah,
						    AR_PHY_GAIN_2GHZ +
						    regChainOffset) &
					   ~AR_PHY_GAIN_2GHZ_RXTX_MARGIN) |
					  SM(pModal->rxTxMarginCh[i],
					     AR_PHY_GAIN_2GHZ_RXTX_MARGIN));
			}
		}
	}

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		if (IS_CHAN_2GHZ(chan)) {
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF2G1_CH0,
						  AR_AN_RF2G1_CH0_OB,
						  AR_AN_RF2G1_CH0_OB_S,
						  pModal->ob);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF2G1_CH0,
						  AR_AN_RF2G1_CH0_DB,
						  AR_AN_RF2G1_CH0_DB_S,
						  pModal->db);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF2G1_CH1,
						  AR_AN_RF2G1_CH1_OB,
						  AR_AN_RF2G1_CH1_OB_S,
						  pModal->ob_ch1);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF2G1_CH1,
						  AR_AN_RF2G1_CH1_DB,
						  AR_AN_RF2G1_CH1_DB_S,
						  pModal->db_ch1);
		} else {
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF5G1_CH0,
						  AR_AN_RF5G1_CH0_OB5,
						  AR_AN_RF5G1_CH0_OB5_S,
						  pModal->ob);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF5G1_CH0,
						  AR_AN_RF5G1_CH0_DB5,
						  AR_AN_RF5G1_CH0_DB5_S,
						  pModal->db);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF5G1_CH1,
						  AR_AN_RF5G1_CH1_OB5,
						  AR_AN_RF5G1_CH1_OB5_S,
						  pModal->ob_ch1);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF5G1_CH1,
						  AR_AN_RF5G1_CH1_DB5,
						  AR_AN_RF5G1_CH1_DB5_S,
						  pModal->db_ch1);
		}
		ath9k_hw_analog_shift_rmw(ah, AR_AN_TOP2,
					  AR_AN_TOP2_XPABIAS_LVL,
					  AR_AN_TOP2_XPABIAS_LVL_S,
					  pModal->xpaBiasLvl);
		ath9k_hw_analog_shift_rmw(ah, AR_AN_TOP2,
					  AR_AN_TOP2_LOCALBIAS,
					  AR_AN_TOP2_LOCALBIAS_S,
					  pModal->local_bias);
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM, "ForceXPAon: %d\n",
			pModal->force_xpaon);
		REG_RMW_FIELD(ah, AR_PHY_XPA_CFG, AR_PHY_FORCE_XPA_CFG,
			      pModal->force_xpaon);
	}

	REG_RMW_FIELD(ah, AR_PHY_SETTLING, AR_PHY_SETTLING_SWITCH,
		      pModal->switchSettling);
	REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ, AR_PHY_DESIRED_SZ_ADC,
		      pModal->adcDesiredSize);

	if (!AR_SREV_9280_10_OR_LATER(ah))
		REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ,
			      AR_PHY_DESIRED_SZ_PGA,
			      pModal->pgaDesiredSize);

	REG_WRITE(ah, AR_PHY_RF_CTL4,
		  SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAA_OFF)
		  | SM(pModal->txEndToXpaOff,
		       AR_PHY_RF_CTL4_TX_END_XPAB_OFF)
		  | SM(pModal->txFrameToXpaOn,
		       AR_PHY_RF_CTL4_FRAME_XPAA_ON)
		  | SM(pModal->txFrameToXpaOn,
		       AR_PHY_RF_CTL4_FRAME_XPAB_ON));

	REG_RMW_FIELD(ah, AR_PHY_RF_CTL3, AR_PHY_TX_END_TO_A2_RX_ON,
		      pModal->txEndToRxOn);
	if (AR_SREV_9280_10_OR_LATER(ah)) {
		REG_RMW_FIELD(ah, AR_PHY_CCA, AR9280_PHY_CCA_THRESH62,
			      pModal->thresh62);
		REG_RMW_FIELD(ah, AR_PHY_EXT_CCA0,
			      AR_PHY_EXT_CCA0_THRESH62,
			      pModal->thresh62);
	} else {
		REG_RMW_FIELD(ah, AR_PHY_CCA, AR_PHY_CCA_THRESH62,
			      pModal->thresh62);
		REG_RMW_FIELD(ah, AR_PHY_EXT_CCA,
			      AR_PHY_EXT_CCA_THRESH62,
			      pModal->thresh62);
	}

	if ((eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_2) {
		REG_RMW_FIELD(ah, AR_PHY_RF_CTL2,
			      AR_PHY_TX_END_DATA_START,
			      pModal->txFrameToDataStart);
		REG_RMW_FIELD(ah, AR_PHY_RF_CTL2, AR_PHY_TX_END_PA_ON,
			      pModal->txFrameToPaOn);
	}

	if ((eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_3) {
		if (IS_CHAN_HT40(chan))
			REG_RMW_FIELD(ah, AR_PHY_SETTLING,
				      AR_PHY_SETTLING_SWITCH,
				      pModal->swSettleHt40);
	}

	return true;
}

int ath9k_hw_get_eeprom_antenna_cfg(struct ath_hal *ah,
				    struct ath9k_channel *chan,
				    u8 index, u16 *config)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	struct modal_eep_header *pModal =
		&(eep->modalHeader[IS_CHAN_2GHZ(chan)]);
	struct base_eep_header *pBase = &eep->baseEepHeader;

	switch (index) {
	case 0:
		*config = pModal->antCtrlCommon & 0xFFFF;
		return 0;
	case 1:
		if (pBase->version >= 0x0E0D) {
			if (pModal->useAnt1) {
				*config =
				((pModal->antCtrlCommon & 0xFFFF0000) >> 16);
				return 0;
			}
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

u8 ath9k_hw_get_num_ant_config(struct ath_hal *ah,
			       enum ieee80211_band freq_band)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	struct modal_eep_header *pModal =
		&(eep->modalHeader[IEEE80211_BAND_5GHZ == freq_band]);
	struct base_eep_header *pBase = &eep->baseEepHeader;
	u8 num_ant_config;

	num_ant_config = 1;

	if (pBase->version >= 0x0E0D)
		if (pModal->useAnt1)
			num_ant_config += 1;

	return num_ant_config;
}

u16 ath9k_hw_eeprom_get_spur_chan(struct ath_hal *ah, u16 i, bool is2GHz)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep =
		(struct ar5416_eeprom *) &ahp->ah_eeprom;
	u16 spur_val = AR_NO_SPUR;

	DPRINTF(ah->ah_sc, ATH_DBG_ANI,
		"Getting spur idx %d is2Ghz. %d val %x\n",
		i, is2GHz, ah->ah_config.spurchans[i][is2GHz]);

	switch (ah->ah_config.spurmode) {
	case SPUR_DISABLE:
		break;
	case SPUR_ENABLE_IOCTL:
		spur_val = ah->ah_config.spurchans[i][is2GHz];
		DPRINTF(ah->ah_sc, ATH_DBG_ANI,
			"Getting spur val from new loc. %d\n", spur_val);
		break;
	case SPUR_ENABLE_EEPROM:
		spur_val = eep->modalHeader[is2GHz].spurChans[i].spurChan;
		break;

	}

	return spur_val;
}

u32 ath9k_hw_get_eeprom(struct ath_hal *ah,
			enum eeprom_param param)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	struct modal_eep_header *pModal = eep->modalHeader;
	struct base_eep_header *pBase = &eep->baseEepHeader;

	switch (param) {
	case EEP_NFTHRESH_5:
		return -pModal[0].noiseFloorThreshCh[0];
	case EEP_NFTHRESH_2:
		return -pModal[1].noiseFloorThreshCh[0];
	case AR_EEPROM_MAC(0):
		return pBase->macAddr[0] << 8 | pBase->macAddr[1];
	case AR_EEPROM_MAC(1):
		return pBase->macAddr[2] << 8 | pBase->macAddr[3];
	case AR_EEPROM_MAC(2):
		return pBase->macAddr[4] << 8 | pBase->macAddr[5];
	case EEP_REG_0:
		return pBase->regDmn[0];
	case EEP_REG_1:
		return pBase->regDmn[1];
	case EEP_OP_CAP:
		return pBase->deviceCap;
	case EEP_OP_MODE:
		return pBase->opCapFlags;
	case EEP_RF_SILENT:
		return pBase->rfSilent;
	case EEP_OB_5:
		return pModal[0].ob;
	case EEP_DB_5:
		return pModal[0].db;
	case EEP_OB_2:
		return pModal[1].ob;
	case EEP_DB_2:
		return pModal[1].db;
	case EEP_MINOR_REV:
		return pBase->version & AR5416_EEP_VER_MINOR_MASK;
	case EEP_TX_MASK:
		return pBase->txMask;
	case EEP_RX_MASK:
		return pBase->rxMask;
	default:
		return 0;
	}
}

int ath9k_hw_eeprom_attach(struct ath_hal *ah)
{
	int status;

	if (ath9k_hw_use_flash(ah))
		ath9k_hw_flash_map(ah);

	if (!ath9k_hw_fill_eeprom(ah))
		return -EIO;

	status = ath9k_hw_check_eeprom(ah);

	return status;
}
