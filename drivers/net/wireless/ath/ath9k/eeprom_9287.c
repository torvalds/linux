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
#include "ar9002_phy.h"

#define SIZE_EEPROM_AR9287 (sizeof(struct ar9287_eeprom) / sizeof(u16))

static int ath9k_hw_ar9287_get_eeprom_ver(struct ath_hw *ah)
{
	return (ah->eeprom.map9287.baseEepHeader.version >> 12) & 0xF;
}

static int ath9k_hw_ar9287_get_eeprom_rev(struct ath_hw *ah)
{
	return (ah->eeprom.map9287.baseEepHeader.version) & 0xFFF;
}

static bool __ath9k_hw_ar9287_fill_eeprom(struct ath_hw *ah)
{
	struct ar9287_eeprom *eep = &ah->eeprom.map9287;
	struct ath_common *common = ath9k_hw_common(ah);
	u16 *eep_data;
	int addr, eep_start_loc = AR9287_EEP_START_LOC;
	eep_data = (u16 *)eep;

	for (addr = 0; addr < SIZE_EEPROM_AR9287; addr++) {
		if (!ath9k_hw_nvram_read(common, addr + eep_start_loc,
					 eep_data)) {
			ath_dbg(common, ATH_DBG_EEPROM,
				"Unable to read eeprom region\n");
			return false;
		}
		eep_data++;
	}

	return true;
}

static bool __ath9k_hw_usb_ar9287_fill_eeprom(struct ath_hw *ah)
{
	u16 *eep_data = (u16 *)&ah->eeprom.map9287;

	ath9k_hw_usb_gen_fill_eeprom(ah, eep_data,
				     AR9287_HTC_EEP_START_LOC,
				     SIZE_EEPROM_AR9287);
	return true;
}

static bool ath9k_hw_ar9287_fill_eeprom(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	if (!ath9k_hw_use_flash(ah)) {
		ath_dbg(common, ATH_DBG_EEPROM,
			"Reading from EEPROM, not flash\n");
	}

	if (common->bus_ops->ath_bus_type == ATH_USB)
		return __ath9k_hw_usb_ar9287_fill_eeprom(ah);
	else
		return __ath9k_hw_ar9287_fill_eeprom(ah);
}

static int ath9k_hw_ar9287_check_eeprom(struct ath_hw *ah)
{
	u32 sum = 0, el, integer;
	u16 temp, word, magic, magic2, *eepdata;
	int i, addr;
	bool need_swap = false;
	struct ar9287_eeprom *eep = &ah->eeprom.map9287;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!ath9k_hw_use_flash(ah)) {
		if (!ath9k_hw_nvram_read(common, AR5416_EEPROM_MAGIC_OFFSET,
					 &magic)) {
			ath_err(common, "Reading Magic # failed\n");
			return false;
		}

		ath_dbg(common, ATH_DBG_EEPROM,
			"Read Magic = 0x%04X\n", magic);

		if (magic != AR5416_EEPROM_MAGIC) {
			magic2 = swab16(magic);

			if (magic2 == AR5416_EEPROM_MAGIC) {
				need_swap = true;
				eepdata = (u16 *)(&ah->eeprom);

				for (addr = 0; addr < SIZE_EEPROM_AR9287; addr++) {
					temp = swab16(*eepdata);
					*eepdata = temp;
					eepdata++;
				}
			} else {
				ath_err(common,
					"Invalid EEPROM Magic. Endianness mismatch.\n");
				return -EINVAL;
			}
		}
	}

	ath_dbg(common, ATH_DBG_EEPROM, "need_swap = %s.\n",
		need_swap ? "True" : "False");

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

		for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
			word = swab16(eep->modalHeader.spurChans[i].spurChan);
			eep->modalHeader.spurChans[i].spurChan = word;
		}
	}

	if (sum != 0xffff || ah->eep_ops->get_eeprom_ver(ah) != AR9287_EEP_VER
	    || ah->eep_ops->get_eeprom_rev(ah) < AR5416_EEP_NO_BACK_VER) {
		ath_err(common, "Bad EEPROM checksum 0x%x or revision 0x%04x\n",
			sum, ah->eep_ops->get_eeprom_ver(ah));
		return -EINVAL;
	}

	return 0;
}

static u32 ath9k_hw_ar9287_get_eeprom(struct ath_hw *ah,
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
	case EEP_MAC_LSW:
		return pBase->macAddr[0] << 8 | pBase->macAddr[1];
	case EEP_MAC_MID:
		return pBase->macAddr[2] << 8 | pBase->macAddr[3];
	case EEP_MAC_MSW:
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

static void ar9287_eeprom_get_tx_gain_index(struct ath_hw *ah,
			    struct ath9k_channel *chan,
			    struct cal_data_op_loop_ar9287 *pRawDatasetOpLoop,
			    u8 *pCalChans,  u16 availPiers, int8_t *pPwr)
{
	u16 idxL = 0, idxR = 0, numPiers;
	bool match;
	struct chan_centers centers;

	ath9k_hw_get_channel_centers(ah, chan, &centers);

	for (numPiers = 0; numPiers < availPiers; numPiers++) {
		if (pCalChans[numPiers] == AR5416_BCHAN_UNUSED)
			break;
	}

	match = ath9k_hw_get_lower_upper_index(
		(u8)FREQ2FBIN(centers.synth_center, IS_CHAN_2GHZ(chan)),
		pCalChans, numPiers, &idxL, &idxR);

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

	/* Enable OLPC for chain 0 */

	tmpVal = REG_READ(ah, 0xa270);
	tmpVal = tmpVal & 0xFCFFFFFF;
	tmpVal = tmpVal | (0x3 << 24);
	REG_WRITE(ah, 0xa270, tmpVal);

	/* Enable OLPC for chain 1 */

	tmpVal = REG_READ(ah, 0xb270);
	tmpVal = tmpVal & 0xFCFFFFFF;
	tmpVal = tmpVal | (0x3 << 24);
	REG_WRITE(ah, 0xb270, tmpVal);

	/* Write the OLPC ref power for chain 0 */

	if (chain == 0) {
		tmpVal = REG_READ(ah, 0xa398);
		tmpVal = tmpVal & 0xff00ffff;
		a = (txPower)&0xff;
		tmpVal = tmpVal | (a << 16);
		REG_WRITE(ah, 0xa398, tmpVal);
	}

	/* Write the OLPC ref power for chain 1 */

	if (chain == 1) {
		tmpVal = REG_READ(ah, 0xb398);
		tmpVal = tmpVal & 0xff00ffff;
		a = (txPower)&0xff;
		tmpVal = tmpVal | (a << 16);
		REG_WRITE(ah, 0xb398, tmpVal);
	}
}

static void ath9k_hw_set_ar9287_power_cal_table(struct ath_hw *ah,
						struct ath9k_channel *chan,
						int16_t *pTxPowerIndexOffset)
{
	struct cal_data_per_freq_ar9287 *pRawDataset;
	struct cal_data_op_loop_ar9287 *pRawDatasetOpenLoop;
	u8 *pCalBChans = NULL;
	u16 pdGainOverlap_t2;
	u8 pdadcValues[AR5416_NUM_PDADC_VALUES];
	u16 gainBoundaries[AR5416_PD_GAINS_IN_MASK];
	u16 numPiers = 0, i, j;
	u16 numXpdGain, xpdMask;
	u16 xpdGainValues[AR5416_NUM_PD_GAINS] = {0, 0, 0, 0};
	u32 reg32, regOffset, regChainOffset, regval;
	int16_t diff = 0;
	struct ar9287_eeprom *pEepData = &ah->eeprom.map9287;

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
		if (ath9k_hw_ar9287_get_eeprom(ah, EEP_OL_PWRCTRL)) {
			pRawDatasetOpenLoop =
			(struct cal_data_op_loop_ar9287 *)pEepData->calPierData2G[0];
			ah->initPDADC = pRawDatasetOpenLoop->vpdPdg[0][0];
		}
	}

	numXpdGain = 0;

	/* Calculate the value of xpdgains from the xpdGain Mask */
	for (i = 1; i <= AR5416_PD_GAINS_IN_MASK; i++) {
		if ((xpdMask >> (AR5416_PD_GAINS_IN_MASK - i)) & 1) {
			if (numXpdGain >= AR5416_NUM_PD_GAINS)
				break;
			xpdGainValues[numXpdGain] =
				(u16)(AR5416_PD_GAINS_IN_MASK-i);
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
			pRawDatasetOpenLoop =
			(struct cal_data_op_loop_ar9287 *)pEepData->calPierData2G[i];

			if (ath9k_hw_ar9287_get_eeprom(ah, EEP_OL_PWRCTRL)) {
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

				ath9k_hw_get_gain_boundaries_pdadcs(ah, chan,
							   pRawDataset,
							   pCalBChans, numPiers,
							   pdGainOverlap_t2,
							   gainBoundaries,
							   pdadcValues,
							   numXpdGain);
			}

			ENABLE_REGWRITE_BUFFER(ah);

			if (i == 0) {
				if (!ath9k_hw_ar9287_get_eeprom(ah,
							EEP_OL_PWRCTRL)) {

					regval = SM(pdGainOverlap_t2,
						    AR_PHY_TPCRG5_PD_GAIN_OVERLAP)
						| SM(gainBoundaries[0],
						     AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1)
						| SM(gainBoundaries[1],
						     AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2)
						| SM(gainBoundaries[2],
						     AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3)
						| SM(gainBoundaries[3],
						     AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4);

					REG_WRITE(ah,
						  AR_PHY_TPCRG5 + regChainOffset,
						  regval);
				}
			}

			if ((int32_t)AR9287_PWR_TABLE_OFFSET_DB !=
			    pEepData->baseEepHeader.pwrTableOffset) {
				diff = (u16)(pEepData->baseEepHeader.pwrTableOffset -
					     (int32_t)AR9287_PWR_TABLE_OFFSET_DB);
				diff *= 2;

				for (j = 0; j < ((u16)AR5416_NUM_PDADC_VALUES-diff); j++)
					pdadcValues[j] = pdadcValues[j+diff];

				for (j = (u16)(AR5416_NUM_PDADC_VALUES-diff);
				     j < AR5416_NUM_PDADC_VALUES; j++)
					pdadcValues[j] =
					  pdadcValues[AR5416_NUM_PDADC_VALUES-diff];
			}

			if (!ath9k_hw_ar9287_get_eeprom(ah, EEP_OL_PWRCTRL)) {
				regOffset = AR_PHY_BASE +
					(672 << 2) + regChainOffset;

				for (j = 0; j < 32; j++) {
					reg32 = ((pdadcValues[4*j + 0] & 0xFF) << 0)
						| ((pdadcValues[4*j + 1] & 0xFF) << 8)
						| ((pdadcValues[4*j + 2] & 0xFF) << 16)
						| ((pdadcValues[4*j + 3] & 0xFF) << 24);

					REG_WRITE(ah, regOffset, reg32);
					regOffset += 4;
				}
			}
			REGWRITE_BUFFER_FLUSH(ah);
		}
	}

	*pTxPowerIndexOffset = 0;
}

static void ath9k_hw_set_ar9287_power_per_rate_table(struct ath_hw *ah,
						     struct ath9k_channel *chan,
						     int16_t *ratesArray,
						     u16 cfgCtl,
						     u16 AntennaReduction,
						     u16 twiceMaxRegulatoryPower,
						     u16 powerLimit)
{
#define CMP_CTL \
	(((cfgCtl & ~CTL_MODE_M) | (pCtlMode[ctlMode] & CTL_MODE_M)) == \
	 pEepData->ctlIndex[i])

#define CMP_NO_CTL \
	(((cfgCtl & ~CTL_MODE_M) | (pCtlMode[ctlMode] & CTL_MODE_M)) == \
	 ((pEepData->ctlIndex[i] & CTL_MODE_M) | SD_NO_CTL))

#define REDUCE_SCALED_POWER_BY_TWO_CHAIN     6
#define REDUCE_SCALED_POWER_BY_THREE_CHAIN   10

	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	u16 twiceMaxEdgePower = MAX_RATE_POWER;
	static const u16 tpScaleReductionTable[5] =
		{ 0, 3, 6, 9, MAX_RATE_POWER };
	int i;
	int16_t twiceLargestAntenna;
	struct cal_ctl_data_ar9287 *rep;
	struct cal_target_power_leg targetPowerOfdm = {0, {0, 0, 0, 0} },
				    targetPowerCck = {0, {0, 0, 0, 0} };
	struct cal_target_power_leg targetPowerOfdmExt = {0, {0, 0, 0, 0} },
				    targetPowerCckExt = {0, {0, 0, 0, 0} };
	struct cal_target_power_ht targetPowerHt20,
				    targetPowerHt40 = {0, {0, 0, 0, 0} };
	u16 scaledPower = 0, minCtlPower, maxRegAllowedPower;
	static const u16 ctlModesFor11g[] = {
		CTL_11B, CTL_11G, CTL_2GHT20,
		CTL_11B_EXT, CTL_11G_EXT, CTL_2GHT40
	};
	u16 numCtlModes = 0;
	const u16 *pCtlMode = NULL;
	u16 ctlMode, freq;
	struct chan_centers centers;
	int tx_chainmask;
	u16 twiceMinEdgePower;
	struct ar9287_eeprom *pEepData = &ah->eeprom.map9287;
	tx_chainmask = ah->txchainmask;

	ath9k_hw_get_channel_centers(ah, chan, &centers);

	/* Compute TxPower reduction due to Antenna Gain */
	twiceLargestAntenna = max(pEepData->modalHeader.antennaGainCh[0],
				  pEepData->modalHeader.antennaGainCh[1]);
	twiceLargestAntenna = (int16_t)min((AntennaReduction) -
					   twiceLargestAntenna, 0);

	/*
	 * scaledPower is the minimum of the user input power level
	 * and the regulatory allowed power level.
	 */
	maxRegAllowedPower = twiceMaxRegulatoryPower + twiceLargestAntenna;

	if (regulatory->tp_scale != ATH9K_TP_SCALE_MAX)
		maxRegAllowedPower -=
			(tpScaleReductionTable[(regulatory->tp_scale)] * 2);

	scaledPower = min(powerLimit, maxRegAllowedPower);

	/*
	 * Reduce scaled Power by number of chains active
	 * to get the per chain tx power level.
	 */
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

	/*
	 * Get TX power from EEPROM.
	 */
	if (IS_CHAN_2GHZ(chan))	{
		/* CTL_11B, CTL_11G, CTL_2GHT20 */
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
			/* All 2G CTLs */
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
		bool isHt40CtlMode =
			(pCtlMode[ctlMode] == CTL_2GHT40) ? true : false;

		if (isHt40CtlMode)
			freq = centers.synth_center;
		else if (pCtlMode[ctlMode] & EXT_ADDITIVE)
			freq = centers.ext_center;
		else
			freq = centers.ctl_center;

		/* Walk through the CTL indices stored in EEPROM */
		for (i = 0; (i < AR9287_NUM_CTLS) && pEepData->ctlIndex[i]; i++) {
			struct cal_ctl_edges *pRdEdgesPower;

			/*
			 * Compare test group from regulatory channel list
			 * with test mode from pCtlMode list
			 */
			if (CMP_CTL || CMP_NO_CTL) {
				rep = &(pEepData->ctlData[i]);
				pRdEdgesPower =
				rep->ctlEdges[ar5416_get_ntxchains(tx_chainmask) - 1];

				twiceMinEdgePower = ath9k_hw_get_max_edge_power(freq,
								pRdEdgesPower,
								IS_CHAN_2GHZ(chan),
								AR5416_NUM_BAND_EDGES);

				if ((cfgCtl & ~CTL_MODE_M) == SD_NO_CTL) {
					twiceMaxEdgePower = min(twiceMaxEdgePower,
								twiceMinEdgePower);
				} else {
					twiceMaxEdgePower = twiceMinEdgePower;
					break;
				}
			}
		}

		minCtlPower = (u8)min(twiceMaxEdgePower, scaledPower);

		/* Apply ctl mode to correct target power set */
		switch (pCtlMode[ctlMode]) {
		case CTL_11B:
			for (i = 0; i < ARRAY_SIZE(targetPowerCck.tPow2x); i++) {
				targetPowerCck.tPow2x[i] =
					(u8)min((u16)targetPowerCck.tPow2x[i],
						minCtlPower);
			}
			break;
		case CTL_11A:
		case CTL_11G:
			for (i = 0; i < ARRAY_SIZE(targetPowerOfdm.tPow2x); i++) {
				targetPowerOfdm.tPow2x[i] =
					(u8)min((u16)targetPowerOfdm.tPow2x[i],
						minCtlPower);
			}
			break;
		case CTL_5GHT20:
		case CTL_2GHT20:
			for (i = 0; i < ARRAY_SIZE(targetPowerHt20.tPow2x); i++) {
				targetPowerHt20.tPow2x[i] =
					(u8)min((u16)targetPowerHt20.tPow2x[i],
						minCtlPower);
			}
			break;
		case CTL_11B_EXT:
			targetPowerCckExt.tPow2x[0] =
				(u8)min((u16)targetPowerCckExt.tPow2x[0],
					minCtlPower);
			break;
		case CTL_11A_EXT:
		case CTL_11G_EXT:
			targetPowerOfdmExt.tPow2x[0] =
				(u8)min((u16)targetPowerOfdmExt.tPow2x[0],
					minCtlPower);
			break;
		case CTL_5GHT40:
		case CTL_2GHT40:
			for (i = 0; i < ARRAY_SIZE(targetPowerHt40.tPow2x); i++) {
				targetPowerHt40.tPow2x[i] =
					(u8)min((u16)targetPowerHt40.tPow2x[i],
						minCtlPower);
			}
			break;
		default:
			break;
		}
	}

	/* Now set the rates array */

	ratesArray[rate6mb] =
	ratesArray[rate9mb] =
	ratesArray[rate12mb] =
	ratesArray[rate18mb] =
	ratesArray[rate24mb] = targetPowerOfdm.tPow2x[0];

	ratesArray[rate36mb] = targetPowerOfdm.tPow2x[1];
	ratesArray[rate48mb] = targetPowerOfdm.tPow2x[2];
	ratesArray[rate54mb] = targetPowerOfdm.tPow2x[3];
	ratesArray[rateXr] = targetPowerOfdm.tPow2x[0];

	for (i = 0; i < ARRAY_SIZE(targetPowerHt20.tPow2x); i++)
		ratesArray[rateHt20_0 + i] = targetPowerHt20.tPow2x[i];

	if (IS_CHAN_2GHZ(chan))	{
		ratesArray[rate1l] = targetPowerCck.tPow2x[0];
		ratesArray[rate2s] =
		ratesArray[rate2l] = targetPowerCck.tPow2x[1];
		ratesArray[rate5_5s] =
		ratesArray[rate5_5l] = targetPowerCck.tPow2x[2];
		ratesArray[rate11s] =
		ratesArray[rate11l] = targetPowerCck.tPow2x[3];
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

#undef CMP_CTL
#undef CMP_NO_CTL
#undef REDUCE_SCALED_POWER_BY_TWO_CHAIN
#undef REDUCE_SCALED_POWER_BY_THREE_CHAIN
}

static void ath9k_hw_ar9287_set_txpower(struct ath_hw *ah,
					struct ath9k_channel *chan, u16 cfgCtl,
					u8 twiceAntennaReduction,
					u8 twiceMaxRegulatoryPower,
					u8 powerLimit, bool test)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	struct ar9287_eeprom *pEepData = &ah->eeprom.map9287;
	struct modal_eep_ar9287_header *pModal = &pEepData->modalHeader;
	int16_t ratesArray[Ar5416RateSize];
	int16_t txPowerIndexOffset = 0;
	u8 ht40PowerIncForPdadc = 2;
	int i;

	memset(ratesArray, 0, sizeof(ratesArray));

	if ((pEepData->baseEepHeader.version & AR9287_EEP_VER_MINOR_MASK) >=
	    AR9287_EEP_MINOR_VER_2)
		ht40PowerIncForPdadc = pModal->ht40PowerIncForPdadc;

	ath9k_hw_set_ar9287_power_per_rate_table(ah, chan,
						 &ratesArray[0], cfgCtl,
						 twiceAntennaReduction,
						 twiceMaxRegulatoryPower,
						 powerLimit);

	ath9k_hw_set_ar9287_power_cal_table(ah, chan, &txPowerIndexOffset);

	regulatory->max_power_level = 0;
	for (i = 0; i < ARRAY_SIZE(ratesArray); i++) {
		ratesArray[i] = (int16_t)(txPowerIndexOffset + ratesArray[i]);
		if (ratesArray[i] > MAX_RATE_POWER)
			ratesArray[i] = MAX_RATE_POWER;

		if (ratesArray[i] > regulatory->max_power_level)
			regulatory->max_power_level = ratesArray[i];
	}

	if (test)
		return;

	if (IS_CHAN_2GHZ(chan))
		i = rate1l;
	else
		i = rate6mb;

	regulatory->max_power_level = ratesArray[i];

	if (AR_SREV_9280_20_OR_LATER(ah)) {
		for (i = 0; i < Ar5416RateSize; i++)
			ratesArray[i] -= AR9287_PWR_TABLE_OFFSET_DB * 2;
	}

	ENABLE_REGWRITE_BUFFER(ah);

	/* OFDM power per rate */
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

	/* CCK power per rate */
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

	/* HT20 power per rate */
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

	/* HT40 power per rate */
	if (IS_CHAN_HT40(chan))	{
		if (ath9k_hw_ar9287_get_eeprom(ah, EEP_OL_PWRCTRL)) {
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

		/* Dup/Ext power per rate */
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE9,
			  ATH9K_POW_SM(ratesArray[rateExtOfdm], 24)
			  | ATH9K_POW_SM(ratesArray[rateExtCck], 16)
			  | ATH9K_POW_SM(ratesArray[rateDupOfdm], 8)
			  | ATH9K_POW_SM(ratesArray[rateDupCck], 0));
	}
	REGWRITE_BUFFER_FLUSH(ah);
}

static void ath9k_hw_ar9287_set_addac(struct ath_hw *ah,
				      struct ath9k_channel *chan)
{
}

static void ath9k_hw_ar9287_set_board_values(struct ath_hw *ah,
					     struct ath9k_channel *chan)
{
	struct ar9287_eeprom *eep = &ah->eeprom.map9287;
	struct modal_eep_ar9287_header *pModal = &eep->modalHeader;
	u32 regChainOffset, regval;
	u8 txRxAttenLocal;
	int i;

	pModal = &eep->modalHeader;

	REG_WRITE(ah, AR_PHY_SWITCH_COM, pModal->antCtrlCommon);

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

	regval = REG_READ(ah, AR9287_AN_RF2G3_CH0);
	regval &= ~(AR9287_AN_RF2G3_DB1 |
		    AR9287_AN_RF2G3_DB2 |
		    AR9287_AN_RF2G3_OB_CCK |
		    AR9287_AN_RF2G3_OB_PSK |
		    AR9287_AN_RF2G3_OB_QAM |
		    AR9287_AN_RF2G3_OB_PAL_OFF);
	regval |= (SM(pModal->db1, AR9287_AN_RF2G3_DB1) |
		   SM(pModal->db2, AR9287_AN_RF2G3_DB2) |
		   SM(pModal->ob_cck, AR9287_AN_RF2G3_OB_CCK) |
		   SM(pModal->ob_psk, AR9287_AN_RF2G3_OB_PSK) |
		   SM(pModal->ob_qam, AR9287_AN_RF2G3_OB_QAM) |
		   SM(pModal->ob_pal_off, AR9287_AN_RF2G3_OB_PAL_OFF));

	ath9k_hw_analog_shift_regwrite(ah, AR9287_AN_RF2G3_CH0, regval);

	regval = REG_READ(ah, AR9287_AN_RF2G3_CH1);
	regval &= ~(AR9287_AN_RF2G3_DB1 |
		    AR9287_AN_RF2G3_DB2 |
		    AR9287_AN_RF2G3_OB_CCK |
		    AR9287_AN_RF2G3_OB_PSK |
		    AR9287_AN_RF2G3_OB_QAM |
		    AR9287_AN_RF2G3_OB_PAL_OFF);
	regval |= (SM(pModal->db1, AR9287_AN_RF2G3_DB1) |
		   SM(pModal->db2, AR9287_AN_RF2G3_DB2) |
		   SM(pModal->ob_cck, AR9287_AN_RF2G3_OB_CCK) |
		   SM(pModal->ob_psk, AR9287_AN_RF2G3_OB_PSK) |
		   SM(pModal->ob_qam, AR9287_AN_RF2G3_OB_QAM) |
		   SM(pModal->ob_pal_off, AR9287_AN_RF2G3_OB_PAL_OFF));

	ath9k_hw_analog_shift_regwrite(ah, AR9287_AN_RF2G3_CH1, regval);

	REG_RMW_FIELD(ah, AR_PHY_RF_CTL2,
		      AR_PHY_TX_END_DATA_START, pModal->txFrameToDataStart);
	REG_RMW_FIELD(ah, AR_PHY_RF_CTL2,
		      AR_PHY_TX_END_PA_ON, pModal->txFrameToPaOn);

	ath9k_hw_analog_shift_rmw(ah, AR9287_AN_TOP2,
				  AR9287_AN_TOP2_XPABIAS_LVL,
				  AR9287_AN_TOP2_XPABIAS_LVL_S,
				  pModal->xpaBiasLvl);
}

static u16 ath9k_hw_ar9287_get_spur_channel(struct ath_hw *ah,
					    u16 i, bool is2GHz)
{
#define EEP_MAP9287_SPURCHAN \
	(ah->eeprom.map9287.modalHeader.spurChans[i].spurChan)

	struct ath_common *common = ath9k_hw_common(ah);
	u16 spur_val = AR_NO_SPUR;

	ath_dbg(common, ATH_DBG_ANI,
		"Getting spur idx:%d is2Ghz:%d val:%x\n",
		i, is2GHz, ah->config.spurchans[i][is2GHz]);

	switch (ah->config.spurmode) {
	case SPUR_DISABLE:
		break;
	case SPUR_ENABLE_IOCTL:
		spur_val = ah->config.spurchans[i][is2GHz];
		ath_dbg(common, ATH_DBG_ANI,
			"Getting spur val from new loc. %d\n", spur_val);
		break;
	case SPUR_ENABLE_EEPROM:
		spur_val = EEP_MAP9287_SPURCHAN;
		break;
	}

	return spur_val;

#undef EEP_MAP9287_SPURCHAN
}

const struct eeprom_ops eep_ar9287_ops = {
	.check_eeprom		= ath9k_hw_ar9287_check_eeprom,
	.get_eeprom		= ath9k_hw_ar9287_get_eeprom,
	.fill_eeprom		= ath9k_hw_ar9287_fill_eeprom,
	.get_eeprom_ver		= ath9k_hw_ar9287_get_eeprom_ver,
	.get_eeprom_rev		= ath9k_hw_ar9287_get_eeprom_rev,
	.set_board_values	= ath9k_hw_ar9287_set_board_values,
	.set_addac		= ath9k_hw_ar9287_set_addac,
	.set_txpower		= ath9k_hw_ar9287_set_txpower,
	.get_spur_channel	= ath9k_hw_ar9287_get_spur_channel
};
