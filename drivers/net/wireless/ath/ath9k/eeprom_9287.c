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

static int ath9k_hw_AR9287_get_eeprom_ver(struct ath_hw *ah)
{
	return (ah->eeprom.map9287.baseEepHeader.version >> 12) & 0xF;
}

static int ath9k_hw_AR9287_get_eeprom_rev(struct ath_hw *ah)
{
	return (ah->eeprom.map9287.baseEepHeader.version) & 0xFFF;
}

static bool ath9k_hw_AR9287_fill_eeprom(struct ath_hw *ah)
{
	struct ar9287_eeprom *eep = &ah->eeprom.map9287;
	struct ath_common *common = ath9k_hw_common(ah);
	u16 *eep_data;
	int addr, eep_start_loc = AR9287_EEP_START_LOC;
	eep_data = (u16 *)eep;

	if (!ath9k_hw_use_flash(ah)) {
		ath_print(common, ATH_DBG_EEPROM,
			  "Reading from EEPROM, not flash\n");
	}

	for (addr = 0; addr < sizeof(struct ar9287_eeprom) / sizeof(u16);
			addr++)	{
		if (!ath9k_hw_nvram_read(common,
					 addr + eep_start_loc, eep_data)) {
			ath_print(common, ATH_DBG_EEPROM,
				  "Unable to read eeprom region \n");
			return false;
		}
		eep_data++;
	}
	return true;
}

static int ath9k_hw_AR9287_check_eeprom(struct ath_hw *ah)
{
	u32 sum = 0, el, integer;
	u16 temp, word, magic, magic2, *eepdata;
	int i, addr;
	bool need_swap = false;
	struct ar9287_eeprom *eep = &ah->eeprom.map9287;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!ath9k_hw_use_flash(ah)) {
		if (!ath9k_hw_nvram_read(common,
					 AR5416_EEPROM_MAGIC_OFFSET, &magic)) {
			ath_print(common, ATH_DBG_FATAL,
				  "Reading Magic # failed\n");
			return false;
		}

		ath_print(common, ATH_DBG_EEPROM,
			  "Read Magic = 0x%04X\n", magic);
		if (magic != AR5416_EEPROM_MAGIC) {
			magic2 = swab16(magic);

			if (magic2 == AR5416_EEPROM_MAGIC) {
				need_swap = true;
				eepdata = (u16 *)(&ah->eeprom);

				for (addr = 0;
				     addr < sizeof(struct ar9287_eeprom) / sizeof(u16);
				     addr++) {
					temp = swab16(*eepdata);
					*eepdata = temp;
					eepdata++;
				}
			} else {
				ath_print(common, ATH_DBG_FATAL,
					  "Invalid EEPROM Magic. "
					  "endianness mismatch.\n");
				return -EINVAL;
			}
		}
	}
	ath_print(common, ATH_DBG_EEPROM, "need_swap = %s.\n", need_swap ?
		  "True" : "False");

	if (need_swap)
		el = swab16(ah->eeprom.map9287.baseEepHeader.length);
	else
		el = ah->eeprom.map9287.baseEepHeader.length;

	if (el > sizeof(struct ar9287_eeprom))
		el = sizeof(struct ar9287_eeprom) / sizeof(u16);
	else
		el = el / sizeof(u16);

	eepdata = (u16 *)(&ah->eeprom);
	for (i = 0; i < el; i++)
		sum ^= *eepdata++;

	if (need_swap) {
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

		integer = swab32(eep->modalHeader.antCtrlCommon);
		eep->modalHeader.antCtrlCommon = integer;

		for (i = 0; i < AR9287_MAX_CHAINS; i++) {
			integer = swab32(eep->modalHeader.antCtrlChain[i]);
			eep->modalHeader.antCtrlChain[i] = integer;
		}

		for (i = 0; i < AR9287_EEPROM_MODAL_SPURS; i++) {
			word = swab16(eep->modalHeader.spurChans[i].spurChan);
			eep->modalHeader.spurChans[i].spurChan = word;
		}
	}

	if (sum != 0xffff || ah->eep_ops->get_eeprom_ver(ah) != AR9287_EEP_VER
	    || ah->eep_ops->get_eeprom_rev(ah) < AR5416_EEP_NO_BACK_VER) {
		ath_print(common, ATH_DBG_FATAL,
			  "Bad EEPROM checksum 0x%x or revision 0x%04x\n",
			   sum, ah->eep_ops->get_eeprom_ver(ah));
		return -EINVAL;
	}

	return 0;
}

static u32 ath9k_hw_AR9287_get_eeprom(struct ath_hw *ah,
				      enum eeprom_param param)
{
	struct ar9287_eeprom *eep = &ah->eeprom.map9287;
	struct modal_eep_ar9287_header *pModal = &eep->modalHeader;
	struct base_eep_ar9287_header *pBase = &eep->baseEepHeader;
	u16 ver_minor;

	ver_minor = pBase->version & AR9287_EEP_VER_MINOR_MASK;
	switch (param) {
	case EEP_NFTHRESH_2:
		return pModal->noiseFloorThreshCh[0];
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
	case EEP_MINOR_REV:
		return ver_minor;
	case EEP_TX_MASK:
		return pBase->txMask;
	case EEP_RX_MASK:
		return pBase->rxMask;
	case EEP_DEV_TYPE:
		return pBase->deviceType;
	case EEP_OL_PWRCTRL:
		return pBase->openLoopPwrCntl;
	case EEP_TEMPSENSE_SLOPE:
		if (ver_minor >= AR9287_EEP_MINOR_VER_2)
			return pBase->tempSensSlope;
		else
			return 0;
	case EEP_TEMPSENSE_SLOPE_PAL_ON:
		if (ver_minor >= AR9287_EEP_MINOR_VER_3)
			return pBase->tempSensSlopePalOn;
		else
			return 0;
	default:
		return 0;
	}
}


static void ath9k_hw_get_AR9287_gain_boundaries_pdadcs(struct ath_hw *ah,
				   struct ath9k_channel *chan,
				   struct cal_data_per_freq_ar9287 *pRawDataSet,
				   u8 *bChans,  u16 availPiers,
				   u16 tPdGainOverlap, int16_t *pMinCalPower,
				   u16 *pPdGainBoundaries, u8 *pPDADCValues,
				   u16 numXpdGains)
{
#define TMP_VAL_VPD_TABLE \
	((vpdTableI[i][sizeCurrVpdTable - 1] + (ss - maxIndex + 1) * vpdStep));

	int       i, j, k;
	int16_t   ss;
	u16  idxL = 0, idxR = 0, numPiers;
	u8   *pVpdL, *pVpdR, *pPwrL, *pPwrR;
	u8   minPwrT4[AR9287_NUM_PD_GAINS];
	u8   maxPwrT4[AR9287_NUM_PD_GAINS];
	int16_t   vpdStep;
	int16_t   tmpVal;
	u16  sizeCurrVpdTable, maxIndex, tgtIndex;
	bool    match;
	int16_t  minDelta = 0;
	struct chan_centers centers;
	static u8 vpdTableL[AR5416_EEP4K_NUM_PD_GAINS]
		[AR5416_MAX_PWR_RANGE_IN_HALF_DB];
	static u8 vpdTableR[AR5416_EEP4K_NUM_PD_GAINS]
		[AR5416_MAX_PWR_RANGE_IN_HALF_DB];
	static u8 vpdTableI[AR5416_EEP4K_NUM_PD_GAINS]
		[AR5416_MAX_PWR_RANGE_IN_HALF_DB];

	ath9k_hw_get_channel_centers(ah, chan, &centers);

	for (numPiers = 0; numPiers < availPiers; numPiers++) {
		if (bChans[numPiers] == AR9287_BCHAN_UNUSED)
			break;
	}

	match = ath9k_hw_get_lower_upper_index(
				   (u8)FREQ2FBIN(centers.synth_center,
				    IS_CHAN_2GHZ(chan)), bChans, numPiers,
				    &idxL, &idxR);

	if (match) {
		for (i = 0; i < numXpdGains; i++) {
			minPwrT4[i] = pRawDataSet[idxL].pwrPdg[i][0];
			maxPwrT4[i] = pRawDataSet[idxL].pwrPdg[i][4];
			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
					pRawDataSet[idxL].pwrPdg[i],
					pRawDataSet[idxL].vpdPdg[i],
					AR9287_PD_GAIN_ICEPTS, vpdTableI[i]);
		}
	} else {
		for (i = 0; i < numXpdGains; i++) {
			pVpdL = pRawDataSet[idxL].vpdPdg[i];
			pPwrL = pRawDataSet[idxL].pwrPdg[i];
			pVpdR = pRawDataSet[idxR].vpdPdg[i];
			pPwrR = pRawDataSet[idxR].pwrPdg[i];

			minPwrT4[i] = max(pPwrL[0], pPwrR[0]);

			maxPwrT4[i] =
				min(pPwrL[AR9287_PD_GAIN_ICEPTS - 1],
				    pPwrR[AR9287_PD_GAIN_ICEPTS - 1]);

			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
					pPwrL, pVpdL,
					AR9287_PD_GAIN_ICEPTS,
					vpdTableL[i]);
			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
					pPwrR, pVpdR,
					AR9287_PD_GAIN_ICEPTS,
					vpdTableR[i]);

			for (j = 0; j <= (maxPwrT4[i] - minPwrT4[i]) / 2; j++) {
				vpdTableI[i][j] =
					(u8)(ath9k_hw_interpolate((u16)
					FREQ2FBIN(centers. synth_center,
					IS_CHAN_2GHZ(chan)),
					bChans[idxL], bChans[idxR],
					vpdTableL[i][j], vpdTableR[i][j]));
			}
		}
	}
	*pMinCalPower = (int16_t)(minPwrT4[0] / 2);

	k = 0;
	for (i = 0; i < numXpdGains; i++) {
		if (i == (numXpdGains - 1))
			pPdGainBoundaries[i] = (u16)(maxPwrT4[i] / 2);
		else
			pPdGainBoundaries[i] = (u16)((maxPwrT4[i] +
						      minPwrT4[i+1]) / 4);

		pPdGainBoundaries[i] = min((u16)AR5416_MAX_RATE_POWER,
					    pPdGainBoundaries[i]);


		if ((i == 0) && !AR_SREV_5416_20_OR_LATER(ah)) {
			minDelta = pPdGainBoundaries[0] - 23;
			pPdGainBoundaries[0] = 23;
		} else
			minDelta = 0;

		if (i == 0) {
			if (AR_SREV_9280_10_OR_LATER(ah))
				ss = (int16_t)(0 - (minPwrT4[i] / 2));
			else
				ss = 0;
		} else
			ss = (int16_t)((pPdGainBoundaries[i-1] -
				       (minPwrT4[i] / 2)) -
				       tPdGainOverlap + 1 + minDelta);

		vpdStep = (int16_t)(vpdTableI[i][1] - vpdTableI[i][0]);
		vpdStep = (int16_t)((vpdStep < 1) ? 1 : vpdStep);
		while ((ss < 0) && (k < (AR9287_NUM_PDADC_VALUES - 1)))	{
			tmpVal = (int16_t)(vpdTableI[i][0] + ss * vpdStep);
			pPDADCValues[k++] = (u8)((tmpVal < 0) ? 0 : tmpVal);
			ss++;
		}

		sizeCurrVpdTable = (u8)((maxPwrT4[i] - minPwrT4[i]) / 2 + 1);
		tgtIndex = (u8)(pPdGainBoundaries[i] +
				tPdGainOverlap - (minPwrT4[i] / 2));
		maxIndex = (tgtIndex < sizeCurrVpdTable) ?
			    tgtIndex : sizeCurrVpdTable;

		while ((ss < maxIndex) && (k < (AR9287_NUM_PDADC_VALUES - 1)))
			pPDADCValues[k++] = vpdTableI[i][ss++];

		vpdStep = (int16_t)(vpdTableI[i][sizeCurrVpdTable - 1] -
				    vpdTableI[i][sizeCurrVpdTable - 2]);
		vpdStep = (int16_t)((vpdStep < 1) ? 1 : vpdStep);
		if (tgtIndex > maxIndex) {
			while ((ss <= tgtIndex) &&
				(k < (AR9287_NUM_PDADC_VALUES - 1))) {
				tmpVal = (int16_t) TMP_VAL_VPD_TABLE;
				pPDADCValues[k++] = (u8)((tmpVal > 255) ?
							  255 : tmpVal);
				ss++;
			}
		}
	}

	while (i < AR9287_PD_GAINS_IN_MASK) {
		pPdGainBoundaries[i] = pPdGainBoundaries[i-1];
		i++;
	}

	while (k < AR9287_NUM_PDADC_VALUES) {
		pPDADCValues[k] = pPDADCValues[k-1];
		k++;
	}

#undef TMP_VAL_VPD_TABLE
}

static void ar9287_eeprom_get_tx_gain_index(struct ath_hw *ah,
			    struct ath9k_channel *chan,
			    struct cal_data_op_loop_ar9287 *pRawDatasetOpLoop,
			    u8 *pCalChans,  u16 availPiers,
			    int8_t *pPwr)
{
	u16  idxL = 0, idxR = 0, numPiers;
	bool match;
	struct chan_centers centers;

	ath9k_hw_get_channel_centers(ah, chan, &centers);

	for (numPiers = 0; numPiers < availPiers; numPiers++) {
		if (pCalChans[numPiers] == AR9287_BCHAN_UNUSED)
			break;
	}

	match = ath9k_hw_get_lower_upper_index(
			(u8)FREQ2FBIN(centers.synth_center, IS_CHAN_2GHZ(chan)),
			pCalChans, numPiers,
			&idxL, &idxR);

	if (match) {
		*pPwr = (int8_t) pRawDatasetOpLoop[idxL].pwrPdg[0][0];
	} else {
		*pPwr = ((int8_t) pRawDatasetOpLoop[idxL].pwrPdg[0][0] +
			    (int8_t) pRawDatasetOpLoop[idxR].pwrPdg[0][0])/2;
	}

}

static void ar9287_eeprom_olpc_set_pdadcs(struct ath_hw *ah,
					  int32_t txPower, u16 chain)
{
	u32 tmpVal;
	u32 a;

	tmpVal = REG_READ(ah, 0xa270);
	tmpVal = tmpVal & 0xFCFFFFFF;
	tmpVal = tmpVal | (0x3 << 24);
	REG_WRITE(ah, 0xa270, tmpVal);

	tmpVal = REG_READ(ah, 0xb270);
	tmpVal = tmpVal & 0xFCFFFFFF;
	tmpVal = tmpVal | (0x3 << 24);
	REG_WRITE(ah, 0xb270, tmpVal);

	if (chain == 0) {
		tmpVal = REG_READ(ah, 0xa398);
		tmpVal = tmpVal & 0xff00ffff;
		a = (txPower)&0xff;
		tmpVal = tmpVal | (a << 16);
		REG_WRITE(ah, 0xa398, tmpVal);
	}

	if (chain == 1) {
		tmpVal = REG_READ(ah, 0xb398);
		tmpVal = tmpVal & 0xff00ffff;
		a = (txPower)&0xff;
		tmpVal = tmpVal | (a << 16);
		REG_WRITE(ah, 0xb398, tmpVal);
	}
}

static void ath9k_hw_set_AR9287_power_cal_table(struct ath_hw *ah,
						struct ath9k_channel *chan,
						int16_t *pTxPowerIndexOffset)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct cal_data_per_freq_ar9287 *pRawDataset;
	struct cal_data_op_loop_ar9287 *pRawDatasetOpenLoop;
	u8  *pCalBChans = NULL;
	u16 pdGainOverlap_t2;
	u8  pdadcValues[AR9287_NUM_PDADC_VALUES];
	u16 gainBoundaries[AR9287_PD_GAINS_IN_MASK];
	u16 numPiers = 0, i, j;
	int16_t  tMinCalPower;
	u16 numXpdGain, xpdMask;
	u16 xpdGainValues[AR9287_NUM_PD_GAINS] = {0, 0, 0, 0};
	u32 reg32, regOffset, regChainOffset;
	int16_t   modalIdx, diff = 0;
	struct ar9287_eeprom *pEepData = &ah->eeprom.map9287;
	modalIdx = IS_CHAN_2GHZ(chan) ? 1 : 0;
	xpdMask = pEepData->modalHeader.xpdGain;
	if ((pEepData->baseEepHeader.version & AR9287_EEP_VER_MINOR_MASK) >=
			AR9287_EEP_MINOR_VER_2)
		pdGainOverlap_t2 = pEepData->modalHeader.pdGainOverlap;
	else
		pdGainOverlap_t2 = (u16)(MS(REG_READ(ah, AR_PHY_TPCRG5),
					    AR_PHY_TPCRG5_PD_GAIN_OVERLAP));

	if (IS_CHAN_2GHZ(chan)) {
		pCalBChans = pEepData->calFreqPier2G;
		numPiers = AR9287_NUM_2G_CAL_PIERS;
		if (ath9k_hw_AR9287_get_eeprom(ah, EEP_OL_PWRCTRL)) {
			pRawDatasetOpenLoop =
				(struct cal_data_op_loop_ar9287 *)
				pEepData->calPierData2G[0];
			ah->initPDADC = pRawDatasetOpenLoop->vpdPdg[0][0];
		}
	}

	numXpdGain = 0;
	for (i = 1; i <= AR9287_PD_GAINS_IN_MASK; i++) {
		if ((xpdMask >> (AR9287_PD_GAINS_IN_MASK - i)) & 1) {
			if (numXpdGain >= AR9287_NUM_PD_GAINS)
				break;
			xpdGainValues[numXpdGain] =
				(u16)(AR9287_PD_GAINS_IN_MASK-i);
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

	for (i = 0; i < AR9287_MAX_CHAINS; i++)	{
		regChainOffset = i * 0x1000;
		if (pEepData->baseEepHeader.txMask & (1 << i)) {
			pRawDatasetOpenLoop = (struct cal_data_op_loop_ar9287 *)
					       pEepData->calPierData2G[i];
			if (ath9k_hw_AR9287_get_eeprom(ah, EEP_OL_PWRCTRL)) {
				int8_t txPower;
				ar9287_eeprom_get_tx_gain_index(ah, chan,
							  pRawDatasetOpenLoop,
							  pCalBChans, numPiers,
							  &txPower);
				ar9287_eeprom_olpc_set_pdadcs(ah, txPower, i);
			} else {
				pRawDataset =
					(struct cal_data_per_freq_ar9287 *)
					pEepData->calPierData2G[i];
				ath9k_hw_get_AR9287_gain_boundaries_pdadcs(
						  ah, chan, pRawDataset,
						  pCalBChans, numPiers,
						  pdGainOverlap_t2,
						  &tMinCalPower, gainBoundaries,
						  pdadcValues, numXpdGain);
			}

			if (i == 0) {
				if (!ath9k_hw_AR9287_get_eeprom(
					    ah, EEP_OL_PWRCTRL)) {
					REG_WRITE(ah, AR_PHY_TPCRG5 +
					  regChainOffset,
					  SM(pdGainOverlap_t2,
					     AR_PHY_TPCRG5_PD_GAIN_OVERLAP) |
					  SM(gainBoundaries[0],
					     AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1)
					  | SM(gainBoundaries[1],
					       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2)
					  | SM(gainBoundaries[2],
					       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3)
					  | SM(gainBoundaries[3],
					       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4));
				}
			}

			if ((int32_t)AR9287_PWR_TABLE_OFFSET_DB !=
				     pEepData->baseEepHeader.pwrTableOffset) {
				diff = (u16)
				       (pEepData->baseEepHeader.pwrTableOffset
					- (int32_t)AR9287_PWR_TABLE_OFFSET_DB);
				diff *= 2;

				for (j = 0;
				     j < ((u16)AR9287_NUM_PDADC_VALUES-diff);
				     j++)
					pdadcValues[j] = pdadcValues[j+diff];

				for (j = (u16)(AR9287_NUM_PDADC_VALUES-diff);
				     j < AR9287_NUM_PDADC_VALUES; j++)
					pdadcValues[j] =
					  pdadcValues[
					  AR9287_NUM_PDADC_VALUES-diff];
			}

			if (!ath9k_hw_AR9287_get_eeprom(ah, EEP_OL_PWRCTRL)) {
				regOffset = AR_PHY_BASE + (672 << 2) +
							   regChainOffset;
				for (j = 0; j < 32; j++) {
					reg32 = ((pdadcValues[4*j + 0]
						  & 0xFF) << 0)  |
						((pdadcValues[4*j + 1]
						  & 0xFF) << 8)  |
						((pdadcValues[4*j + 2]
						  & 0xFF) << 16) |
						((pdadcValues[4*j + 3]
						  & 0xFF) << 24) ;
					REG_WRITE(ah, regOffset, reg32);

					ath_print(common, ATH_DBG_EEPROM,
						  "PDADC (%d,%4x): %4.4x "
						  "%8.8x\n",
						  i, regChainOffset, regOffset,
						  reg32);

					ath_print(common, ATH_DBG_EEPROM,
						  "PDADC: Chain %d | "
						  "PDADC %3d Value %3d | "
						  "PDADC %3d Value %3d | "
						  "PDADC %3d Value %3d | "
						  "PDADC %3d Value %3d |\n",
						  i, 4 * j, pdadcValues[4 * j],
						  4 * j + 1,
						  pdadcValues[4 * j + 1],
						  4 * j + 2,
						  pdadcValues[4 * j + 2],
						  4 * j + 3,
						  pdadcValues[4 * j + 3]);

					regOffset += 4;
				}
			}
		}
	}

	*pTxPowerIndexOffset = 0;
}

static void ath9k_hw_set_AR9287_power_per_rate_table(struct ath_hw *ah,
		struct ath9k_channel *chan, int16_t *ratesArray, u16 cfgCtl,
		u16 AntennaReduction, u16 twiceMaxRegulatoryPower,
		u16 powerLimit)
{
#define REDUCE_SCALED_POWER_BY_TWO_CHAIN     6
#define REDUCE_SCALED_POWER_BY_THREE_CHAIN   10
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	u16 twiceMaxEdgePower = AR5416_MAX_RATE_POWER;
	static const u16 tpScaleReductionTable[5] =
		{ 0, 3, 6, 9, AR5416_MAX_RATE_POWER };
	int i;
	int16_t  twiceLargestAntenna;
	struct cal_ctl_data_ar9287 *rep;
	struct cal_target_power_leg targetPowerOfdm = {0, {0, 0, 0, 0} },
				    targetPowerCck = {0, {0, 0, 0, 0} };
	struct cal_target_power_leg targetPowerOfdmExt = {0, {0, 0, 0, 0} },
				    targetPowerCckExt = {0, {0, 0, 0, 0} };
	struct cal_target_power_ht  targetPowerHt20,
				    targetPowerHt40 = {0, {0, 0, 0, 0} };
	u16 scaledPower = 0, minCtlPower, maxRegAllowedPower;
	u16 ctlModesFor11g[] =
		{CTL_11B, CTL_11G, CTL_2GHT20,
		 CTL_11B_EXT, CTL_11G_EXT, CTL_2GHT40};
	u16 numCtlModes = 0, *pCtlMode = NULL, ctlMode, freq;
	struct chan_centers centers;
	int tx_chainmask;
	u16 twiceMinEdgePower;
	struct ar9287_eeprom *pEepData = &ah->eeprom.map9287;
	tx_chainmask = ah->txchainmask;

	ath9k_hw_get_channel_centers(ah, chan, &centers);

	twiceLargestAntenna = max(pEepData->modalHeader.antennaGainCh[0],
				  pEepData->modalHeader.antennaGainCh[1]);

	twiceLargestAntenna =  (int16_t)min((AntennaReduction) -
					    twiceLargestAntenna, 0);

	maxRegAllowedPower = twiceMaxRegulatoryPower + twiceLargestAntenna;
	if (regulatory->tp_scale != ATH9K_TP_SCALE_MAX)
		maxRegAllowedPower -=
			(tpScaleReductionTable[(regulatory->tp_scale)] * 2);

	scaledPower = min(powerLimit, maxRegAllowedPower);

	switch (ar5416_get_ntxchains(tx_chainmask)) {
	case 1:
		break;
	case 2:
		scaledPower -= REDUCE_SCALED_POWER_BY_TWO_CHAIN;
		break;
	case 3:
		scaledPower -= REDUCE_SCALED_POWER_BY_THREE_CHAIN;
		break;
	}
	scaledPower = max((u16)0, scaledPower);

	if (IS_CHAN_2GHZ(chan))	{
		numCtlModes =
			ARRAY_SIZE(ctlModesFor11g) - SUB_NUM_CTL_MODES_AT_2G_40;
		pCtlMode = ctlModesFor11g;

		ath9k_hw_get_legacy_target_powers(ah, chan,
						  pEepData->calTargetPowerCck,
						  AR9287_NUM_2G_CCK_TARGET_POWERS,
						  &targetPowerCck, 4, false);
		ath9k_hw_get_legacy_target_powers(ah, chan,
						  pEepData->calTargetPower2G,
						  AR9287_NUM_2G_20_TARGET_POWERS,
						  &targetPowerOfdm, 4, false);
		ath9k_hw_get_target_powers(ah, chan,
					   pEepData->calTargetPower2GHT20,
					   AR9287_NUM_2G_20_TARGET_POWERS,
					   &targetPowerHt20, 8, false);

		if (IS_CHAN_HT40(chan))	{
			numCtlModes = ARRAY_SIZE(ctlModesFor11g);
			ath9k_hw_get_target_powers(ah, chan,
						   pEepData->calTargetPower2GHT40,
						   AR9287_NUM_2G_40_TARGET_POWERS,
						   &targetPowerHt40, 8, true);
			ath9k_hw_get_legacy_target_powers(ah, chan,
						  pEepData->calTargetPowerCck,
						  AR9287_NUM_2G_CCK_TARGET_POWERS,
						  &targetPowerCckExt, 4, true);
			ath9k_hw_get_legacy_target_powers(ah, chan,
						  pEepData->calTargetPower2G,
						  AR9287_NUM_2G_20_TARGET_POWERS,
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

		if (ah->eep_ops->get_eeprom_ver(ah) == 14 &&
		    ah->eep_ops->get_eeprom_rev(ah) <= 2)
			twiceMaxEdgePower = AR5416_MAX_RATE_POWER;

		for (i = 0; (i < AR9287_NUM_CTLS) && pEepData->ctlIndex[i]; i++) {
			if ((((cfgCtl & ~CTL_MODE_M) |
			      (pCtlMode[ctlMode] & CTL_MODE_M)) ==
			     pEepData->ctlIndex[i]) ||
			    (((cfgCtl & ~CTL_MODE_M) |
			      (pCtlMode[ctlMode] & CTL_MODE_M)) ==
			     ((pEepData->ctlIndex[i] &
			       CTL_MODE_M) | SD_NO_CTL))) {

				rep = &(pEepData->ctlData[i]);
				twiceMinEdgePower = ath9k_hw_get_max_edge_power(
				    freq,
				    rep->ctlEdges[ar5416_get_ntxchains(
				    tx_chainmask) - 1],
				    IS_CHAN_2GHZ(chan), AR5416_NUM_BAND_EDGES);

				if ((cfgCtl & ~CTL_MODE_M) == SD_NO_CTL)
					twiceMaxEdgePower = min(
							    twiceMaxEdgePower,
							    twiceMinEdgePower);
				else {
					twiceMaxEdgePower = twiceMinEdgePower;
					break;
				}
			}
		}

		minCtlPower = (u8)min(twiceMaxEdgePower, scaledPower);

		switch (pCtlMode[ctlMode]) {
		case CTL_11B:
			for (i = 0;
			     i < ARRAY_SIZE(targetPowerCck.tPow2x);
			     i++) {
				targetPowerCck.tPow2x[i] = (u8)min(
					(u16)targetPowerCck.tPow2x[i],
					minCtlPower);
			}
			break;
		case CTL_11A:
		case CTL_11G:
			for (i = 0;
			     i < ARRAY_SIZE(targetPowerOfdm.tPow2x);
			     i++) {
				targetPowerOfdm.tPow2x[i] = (u8)min(
					(u16)targetPowerOfdm.tPow2x[i],
					minCtlPower);
			}
			break;
		case CTL_5GHT20:
		case CTL_2GHT20:
			for (i = 0;
			     i < ARRAY_SIZE(targetPowerHt20.tPow2x);
			     i++) {
				targetPowerHt20.tPow2x[i] = (u8)min(
					(u16)targetPowerHt20.tPow2x[i],
					minCtlPower);
			}
			break;
		case CTL_11B_EXT:
			targetPowerCckExt.tPow2x[0] = (u8)min(
				    (u16)targetPowerCckExt.tPow2x[0],
				    minCtlPower);
			break;
		case CTL_11A_EXT:
		case CTL_11G_EXT:
			targetPowerOfdmExt.tPow2x[0] = (u8)min(
				    (u16)targetPowerOfdmExt.tPow2x[0],
				    minCtlPower);
			break;
		case CTL_5GHT40:
		case CTL_2GHT40:
			for (i = 0;
			     i < ARRAY_SIZE(targetPowerHt40.tPow2x);
			     i++) {
				targetPowerHt40.tPow2x[i] = (u8)min(
					(u16)targetPowerHt40.tPow2x[i],
					minCtlPower);
			}
			break;
		default:
			break;
		}
	}

	ratesArray[rate6mb] =
	ratesArray[rate9mb] =
	ratesArray[rate12mb] =
	ratesArray[rate18mb] =
	ratesArray[rate24mb] =
	targetPowerOfdm.tPow2x[0];

	ratesArray[rate36mb] = targetPowerOfdm.tPow2x[1];
	ratesArray[rate48mb] = targetPowerOfdm.tPow2x[2];
	ratesArray[rate54mb] = targetPowerOfdm.tPow2x[3];
	ratesArray[rateXr] = targetPowerOfdm.tPow2x[0];

	for (i = 0; i < ARRAY_SIZE(targetPowerHt20.tPow2x); i++)
		ratesArray[rateHt20_0 + i] = targetPowerHt20.tPow2x[i];

	if (IS_CHAN_2GHZ(chan))	{
		ratesArray[rate1l] = targetPowerCck.tPow2x[0];
		ratesArray[rate2s] = ratesArray[rate2l] =
			targetPowerCck.tPow2x[1];
		ratesArray[rate5_5s] = ratesArray[rate5_5l] =
			targetPowerCck.tPow2x[2];
		ratesArray[rate11s] = ratesArray[rate11l] =
			targetPowerCck.tPow2x[3];
	}
	if (IS_CHAN_HT40(chan))	{
		for (i = 0; i < ARRAY_SIZE(targetPowerHt40.tPow2x); i++)
			ratesArray[rateHt40_0 + i] = targetPowerHt40.tPow2x[i];

		ratesArray[rateDupOfdm] = targetPowerHt40.tPow2x[0];
		ratesArray[rateDupCck]  = targetPowerHt40.tPow2x[0];
		ratesArray[rateExtOfdm] = targetPowerOfdmExt.tPow2x[0];
		if (IS_CHAN_2GHZ(chan))
			ratesArray[rateExtCck] = targetPowerCckExt.tPow2x[0];
	}

#undef REDUCE_SCALED_POWER_BY_TWO_CHAIN
#undef REDUCE_SCALED_POWER_BY_THREE_CHAIN
}

static void ath9k_hw_AR9287_set_txpower(struct ath_hw *ah,
					struct ath9k_channel *chan, u16 cfgCtl,
					u8 twiceAntennaReduction,
					u8 twiceMaxRegulatoryPower,
					u8 powerLimit)
{
#define INCREASE_MAXPOW_BY_TWO_CHAIN     6
#define INCREASE_MAXPOW_BY_THREE_CHAIN   10
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	struct ar9287_eeprom *pEepData = &ah->eeprom.map9287;
	struct modal_eep_ar9287_header *pModal = &pEepData->modalHeader;
	int16_t ratesArray[Ar5416RateSize];
	int16_t  txPowerIndexOffset = 0;
	u8 ht40PowerIncForPdadc = 2;
	int i;

	memset(ratesArray, 0, sizeof(ratesArray));

	if ((pEepData->baseEepHeader.version & AR9287_EEP_VER_MINOR_MASK) >=
	    AR9287_EEP_MINOR_VER_2)
		ht40PowerIncForPdadc = pModal->ht40PowerIncForPdadc;

	ath9k_hw_set_AR9287_power_per_rate_table(ah, chan,
						 &ratesArray[0], cfgCtl,
						 twiceAntennaReduction,
						 twiceMaxRegulatoryPower,
						 powerLimit);

	ath9k_hw_set_AR9287_power_cal_table(ah, chan, &txPowerIndexOffset);

	for (i = 0; i < ARRAY_SIZE(ratesArray); i++) {
		ratesArray[i] = (int16_t)(txPowerIndexOffset + ratesArray[i]);
		if (ratesArray[i] > AR9287_MAX_RATE_POWER)
			ratesArray[i] = AR9287_MAX_RATE_POWER;
	}

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		for (i = 0; i < Ar5416RateSize; i++)
			ratesArray[i] -= AR9287_PWR_TABLE_OFFSET_DB * 2;
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

	if (IS_CHAN_2GHZ(chan))	{
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

	if (IS_CHAN_HT40(chan))	{
		if (ath9k_hw_AR9287_get_eeprom(ah, EEP_OL_PWRCTRL)) {
			REG_WRITE(ah, AR_PHY_POWER_TX_RATE7,
				  ATH9K_POW_SM(ratesArray[rateHt40_3], 24)
				  | ATH9K_POW_SM(ratesArray[rateHt40_2], 16)
				  | ATH9K_POW_SM(ratesArray[rateHt40_1], 8)
				  | ATH9K_POW_SM(ratesArray[rateHt40_0], 0));

			REG_WRITE(ah, AR_PHY_POWER_TX_RATE8,
				  ATH9K_POW_SM(ratesArray[rateHt40_7], 24)
				  | ATH9K_POW_SM(ratesArray[rateHt40_6], 16)
				  | ATH9K_POW_SM(ratesArray[rateHt40_5], 8)
				  | ATH9K_POW_SM(ratesArray[rateHt40_4], 0));
		} else {
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
		}

		REG_WRITE(ah, AR_PHY_POWER_TX_RATE9,
			  ATH9K_POW_SM(ratesArray[rateExtOfdm], 24)
			  | ATH9K_POW_SM(ratesArray[rateExtCck], 16)
			  | ATH9K_POW_SM(ratesArray[rateDupOfdm], 8)
			  | ATH9K_POW_SM(ratesArray[rateDupCck], 0));
	}

	if (IS_CHAN_2GHZ(chan))
		i = rate1l;
	else
		i = rate6mb;

	if (AR_SREV_9280_10_OR_LATER(ah))
		regulatory->max_power_level =
			ratesArray[i] + AR9287_PWR_TABLE_OFFSET_DB * 2;
	else
		regulatory->max_power_level = ratesArray[i];

	switch (ar5416_get_ntxchains(ah->txchainmask)) {
	case 1:
		break;
	case 2:
		regulatory->max_power_level +=
			INCREASE_MAXPOW_BY_TWO_CHAIN;
		break;
	case 3:
		regulatory->max_power_level +=
			INCREASE_MAXPOW_BY_THREE_CHAIN;
		break;
	default:
		ath_print(common, ATH_DBG_EEPROM,
			  "Invalid chainmask configuration\n");
		break;
	}
}

static void ath9k_hw_AR9287_set_addac(struct ath_hw *ah,
				      struct ath9k_channel *chan)
{
}

static void ath9k_hw_AR9287_set_board_values(struct ath_hw *ah,
					     struct ath9k_channel *chan)
{
	struct ar9287_eeprom *eep = &ah->eeprom.map9287;
	struct modal_eep_ar9287_header *pModal = &eep->modalHeader;
	u16 antWrites[AR9287_ANT_16S];
	u32 regChainOffset;
	u8 txRxAttenLocal;
	int i, j, offset_num;

	pModal = &eep->modalHeader;

	antWrites[0] = (u16)((pModal->antCtrlCommon >> 28) & 0xF);
	antWrites[1] = (u16)((pModal->antCtrlCommon >> 24) & 0xF);
	antWrites[2] = (u16)((pModal->antCtrlCommon >> 20) & 0xF);
	antWrites[3] = (u16)((pModal->antCtrlCommon >> 16) & 0xF);
	antWrites[4] = (u16)((pModal->antCtrlCommon >> 12) & 0xF);
	antWrites[5] = (u16)((pModal->antCtrlCommon >> 8) & 0xF);
	antWrites[6] = (u16)((pModal->antCtrlCommon >> 4)  & 0xF);
	antWrites[7] = (u16)(pModal->antCtrlCommon & 0xF);

	offset_num = 8;

	for (i = 0, j = offset_num; i < AR9287_MAX_CHAINS; i++) {
		antWrites[j++] = (u16)((pModal->antCtrlChain[i] >> 28) & 0xf);
		antWrites[j++] = (u16)((pModal->antCtrlChain[i] >> 10) & 0x3);
		antWrites[j++] = (u16)((pModal->antCtrlChain[i] >> 8) & 0x3);
		antWrites[j++] = 0;
		antWrites[j++] = (u16)((pModal->antCtrlChain[i] >> 6) & 0x3);
		antWrites[j++] = (u16)((pModal->antCtrlChain[i] >> 4) & 0x3);
		antWrites[j++] = (u16)((pModal->antCtrlChain[i] >> 2) & 0x3);
		antWrites[j++] = (u16)(pModal->antCtrlChain[i] & 0x3);
	}

	REG_WRITE(ah, AR_PHY_SWITCH_COM,
		  ah->eep_ops->get_eeprom_antenna_cfg(ah, chan));

	for (i = 0; i < AR9287_MAX_CHAINS; i++)	{
		regChainOffset = i * 0x1000;

		REG_WRITE(ah, AR_PHY_SWITCH_CHAIN_0 + regChainOffset,
			  pModal->antCtrlChain[i]);

		REG_WRITE(ah, AR_PHY_TIMING_CTRL4(0) + regChainOffset,
			  (REG_READ(ah, AR_PHY_TIMING_CTRL4(0) + regChainOffset)
			   & ~(AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF |
			       AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF)) |
			  SM(pModal->iqCalICh[i],
			     AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF) |
			  SM(pModal->iqCalQCh[i],
			     AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF));

		txRxAttenLocal = pModal->txRxAttenCh[i];

		REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + regChainOffset,
			      AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN,
			      pModal->bswMargin[i]);
		REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + regChainOffset,
			      AR_PHY_GAIN_2GHZ_XATTEN1_DB,
			      pModal->bswAtten[i]);
		REG_RMW_FIELD(ah, AR_PHY_RXGAIN + regChainOffset,
			      AR9280_PHY_RXGAIN_TXRX_ATTEN,
			      txRxAttenLocal);
		REG_RMW_FIELD(ah, AR_PHY_RXGAIN + regChainOffset,
			      AR9280_PHY_RXGAIN_TXRX_MARGIN,
			      pModal->rxTxMarginCh[i]);
	}


	if (IS_CHAN_HT40(chan))
		REG_RMW_FIELD(ah, AR_PHY_SETTLING,
			      AR_PHY_SETTLING_SWITCH, pModal->swSettleHt40);
	else
		REG_RMW_FIELD(ah, AR_PHY_SETTLING,
			      AR_PHY_SETTLING_SWITCH, pModal->switchSettling);

	REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ,
		      AR_PHY_DESIRED_SZ_ADC, pModal->adcDesiredSize);

	REG_WRITE(ah, AR_PHY_RF_CTL4,
		  SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAA_OFF)
		  | SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAB_OFF)
		  | SM(pModal->txFrameToXpaOn, AR_PHY_RF_CTL4_FRAME_XPAA_ON)
		  | SM(pModal->txFrameToXpaOn, AR_PHY_RF_CTL4_FRAME_XPAB_ON));

	REG_RMW_FIELD(ah, AR_PHY_RF_CTL3,
		      AR_PHY_TX_END_TO_A2_RX_ON, pModal->txEndToRxOn);

	REG_RMW_FIELD(ah, AR_PHY_CCA,
		      AR9280_PHY_CCA_THRESH62, pModal->thresh62);
	REG_RMW_FIELD(ah, AR_PHY_EXT_CCA0,
		      AR_PHY_EXT_CCA0_THRESH62, pModal->thresh62);

	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH0, AR9287_AN_RF2G3_DB1,
				  AR9287_AN_RF2G3_DB1_S, pModal->db1);
	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH0, AR9287_AN_RF2G3_DB2,
				  AR9287_AN_RF2G3_DB2_S, pModal->db2);
	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH0,
				  AR9287_AN_RF2G3_OB_CCK,
				  AR9287_AN_RF2G3_OB_CCK_S, pModal->ob_cck);
	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH0,
				  AR9287_AN_RF2G3_OB_PSK,
				  AR9287_AN_RF2G3_OB_PSK_S, pModal->ob_psk);
	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH0,
				  AR9287_AN_RF2G3_OB_QAM,
				  AR9287_AN_RF2G3_OB_QAM_S, pModal->ob_qam);
	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH0,
				  AR9287_AN_RF2G3_OB_PAL_OFF,
				  AR9287_AN_RF2G3_OB_PAL_OFF_S,
				  pModal->ob_pal_off);

	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH1,
				  AR9287_AN_RF2G3_DB1, AR9287_AN_RF2G3_DB1_S,
				  pModal->db1);
	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH1, AR9287_AN_RF2G3_DB2,
				  AR9287_AN_RF2G3_DB2_S, pModal->db2);
	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH1,
				  AR9287_AN_RF2G3_OB_CCK,
				  AR9287_AN_RF2G3_OB_CCK_S, pModal->ob_cck);
	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH1,
				  AR9287_AN_RF2G3_OB_PSK,
				  AR9287_AN_RF2G3_OB_PSK_S, pModal->ob_psk);
	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH1,
				  AR9287_AN_RF2G3_OB_QAM,
				  AR9287_AN_RF2G3_OB_QAM_S, pModal->ob_qam);
	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_RF2G3_CH1,
				  AR9287_AN_RF2G3_OB_PAL_OFF,
				  AR9287_AN_RF2G3_OB_PAL_OFF_S,
				  pModal->ob_pal_off);

	REG_RMW_FIELD(ah, AR_PHY_RF_CTL2,
		      AR_PHY_TX_END_DATA_START, pModal->txFrameToDataStart);
	REG_RMW_FIELD(ah, AR_PHY_RF_CTL2,
		      AR_PHY_TX_END_PA_ON, pModal->txFrameToPaOn);

	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_TOP2,
				  AR9287_AN_TOP2_XPABIAS_LVL,
				  AR9287_AN_TOP2_XPABIAS_LVL_S,
				  pModal->xpaBiasLvl);
}

static u8 ath9k_hw_AR9287_get_num_ant_config(struct ath_hw *ah,
					     enum ieee80211_band freq_band)
{
	return 1;
}

static u16 ath9k_hw_AR9287_get_eeprom_antenna_cfg(struct ath_hw *ah,
						  struct ath9k_channel *chan)
{
	struct ar9287_eeprom *eep = &ah->eeprom.map9287;
	struct modal_eep_ar9287_header *pModal = &eep->modalHeader;

	return pModal->antCtrlCommon & 0xFFFF;
}

static u16 ath9k_hw_AR9287_get_spur_channel(struct ath_hw *ah,
					    u16 i, bool is2GHz)
{
#define EEP_MAP9287_SPURCHAN \
	(ah->eeprom.map9287.modalHeader.spurChans[i].spurChan)
	struct ath_common *common = ath9k_hw_common(ah);
	u16 spur_val = AR_NO_SPUR;

	ath_print(common, ATH_DBG_ANI,
		  "Getting spur idx %d is2Ghz. %d val %x\n",
		  i, is2GHz, ah->config.spurchans[i][is2GHz]);

	switch (ah->config.spurmode) {
	case SPUR_DISABLE:
		break;
	case SPUR_ENABLE_IOCTL:
		spur_val = ah->config.spurchans[i][is2GHz];
		ath_print(common, ATH_DBG_ANI,
			  "Getting spur val from new loc. %d\n", spur_val);
		break;
	case SPUR_ENABLE_EEPROM:
		spur_val = EEP_MAP9287_SPURCHAN;
		break;
	}

	return spur_val;

#undef EEP_MAP9287_SPURCHAN
}

const struct eeprom_ops eep_AR9287_ops = {
	.check_eeprom		= ath9k_hw_AR9287_check_eeprom,
	.get_eeprom		= ath9k_hw_AR9287_get_eeprom,
	.fill_eeprom		= ath9k_hw_AR9287_fill_eeprom,
	.get_eeprom_ver		= ath9k_hw_AR9287_get_eeprom_ver,
	.get_eeprom_rev		= ath9k_hw_AR9287_get_eeprom_rev,
	.get_num_ant_config	= ath9k_hw_AR9287_get_num_ant_config,
	.get_eeprom_antenna_cfg	= ath9k_hw_AR9287_get_eeprom_antenna_cfg,
	.set_board_values	= ath9k_hw_AR9287_set_board_values,
	.set_addac		= ath9k_hw_AR9287_set_addac,
	.set_txpower		= ath9k_hw_AR9287_set_txpower,
	.get_spur_channel	= ath9k_hw_AR9287_get_spur_channel
};
