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
#include "ar9002_phy.h"

static int ath9k_hw_4k_get_eeprom_ver(struct ath_hw *ah)
{
	return ((ah->eeprom.map4k.baseEepHeader.version >> 12) & 0xF);
}

static int ath9k_hw_4k_get_eeprom_rev(struct ath_hw *ah)
{
	return ((ah->eeprom.map4k.baseEepHeader.version) & 0xFFF);
}

static bool ath9k_hw_4k_fill_eeprom(struct ath_hw *ah)
{
#define SIZE_EEPROM_4K (sizeof(struct ar5416_eeprom_4k) / sizeof(u16))
	struct ath_common *common = ath9k_hw_common(ah);
	u16 *eep_data = (u16 *)&ah->eeprom.map4k;
	int addr, eep_start_loc = 0;

	eep_start_loc = 64;

	if (!ath9k_hw_use_flash(ah)) {
		ath_dbg(common, ATH_DBG_EEPROM,
			"Reading from EEPROM, not flash\n");
	}

	for (addr = 0; addr < SIZE_EEPROM_4K; addr++) {
		if (!ath9k_hw_nvram_read(common, addr + eep_start_loc, eep_data)) {
			ath_dbg(common, ATH_DBG_EEPROM,
				"Unable to read eeprom region\n");
			return false;
		}
		eep_data++;
	}

	return true;
#undef SIZE_EEPROM_4K
}

static int ath9k_hw_4k_check_eeprom(struct ath_hw *ah)
{
#define EEPROM_4K_SIZE (sizeof(struct ar5416_eeprom_4k) / sizeof(u16))
	struct ath_common *common = ath9k_hw_common(ah);
	struct ar5416_eeprom_4k *eep =
		(struct ar5416_eeprom_4k *) &ah->eeprom.map4k;
	u16 *eepdata, temp, magic, magic2;
	u32 sum = 0, el;
	bool need_swap = false;
	int i, addr;


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
				eepdata = (u16 *) (&ah->eeprom);

				for (addr = 0; addr < EEPROM_4K_SIZE; addr++) {
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
		el = swab16(ah->eeprom.map4k.baseEepHeader.length);
	else
		el = ah->eeprom.map4k.baseEepHeader.length;

	if (el > sizeof(struct ar5416_eeprom_4k))
		el = sizeof(struct ar5416_eeprom_4k) / sizeof(u16);
	else
		el = el / sizeof(u16);

	eepdata = (u16 *)(&ah->eeprom);

	for (i = 0; i < el; i++)
		sum ^= *eepdata++;

	if (need_swap) {
		u32 integer;
		u16 word;

		ath_dbg(common, ATH_DBG_EEPROM,
			"EEPROM Endianness is not native.. Changing\n");

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

		for (i = 0; i < AR5416_EEP4K_MAX_CHAINS; i++) {
			integer = swab32(eep->modalHeader.antCtrlChain[i]);
			eep->modalHeader.antCtrlChain[i] = integer;
		}

		for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
			word = swab16(eep->modalHeader.spurChans[i].spurChan);
			eep->modalHeader.spurChans[i].spurChan = word;
		}
	}

	if (sum != 0xffff || ah->eep_ops->get_eeprom_ver(ah) != AR5416_EEP_VER ||
	    ah->eep_ops->get_eeprom_rev(ah) < AR5416_EEP_NO_BACK_VER) {
		ath_err(common, "Bad EEPROM checksum 0x%x or revision 0x%04x\n",
			sum, ah->eep_ops->get_eeprom_ver(ah));
		return -EINVAL;
	}

	return 0;
#undef EEPROM_4K_SIZE
}

static u32 ath9k_hw_4k_get_eeprom(struct ath_hw *ah,
				  enum eeprom_param param)
{
	struct ar5416_eeprom_4k *eep = &ah->eeprom.map4k;
	struct modal_eep_4k_header *pModal = &eep->modalHeader;
	struct base_eep_header_4k *pBase = &eep->baseEepHeader;
	u16 ver_minor;

	ver_minor = pBase->version & AR5416_EEP_VER_MINOR_MASK;

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
	case EEP_OB_2:
		return pModal->ob_0;
	case EEP_DB_2:
		return pModal->db1_1;
	case EEP_MINOR_REV:
		return ver_minor;
	case EEP_TX_MASK:
		return pBase->txMask;
	case EEP_RX_MASK:
		return pBase->rxMask;
	case EEP_FRAC_N_5G:
		return 0;
	case EEP_PWR_TABLE_OFFSET:
		return AR5416_PWR_TABLE_OFFSET_DB;
	case EEP_MODAL_VER:
		return pModal->version;
	case EEP_ANT_DIV_CTL1:
		return pModal->antdiv_ctl1;
	case EEP_TXGAIN_TYPE:
		if (ver_minor >= AR5416_EEP_MINOR_VER_19)
			return pBase->txGainType;
		else
			return AR5416_EEP_TXGAIN_ORIGINAL;
	default:
		return 0;
	}
}

static void ath9k_hw_set_4k_power_cal_table(struct ath_hw *ah,
				  struct ath9k_channel *chan,
				  int16_t *pTxPowerIndexOffset)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ar5416_eeprom_4k *pEepData = &ah->eeprom.map4k;
	struct cal_data_per_freq_4k *pRawDataset;
	u8 *pCalBChans = NULL;
	u16 pdGainOverlap_t2;
	static u8 pdadcValues[AR5416_NUM_PDADC_VALUES];
	u16 gainBoundaries[AR5416_PD_GAINS_IN_MASK];
	u16 numPiers, i, j;
	u16 numXpdGain, xpdMask;
	u16 xpdGainValues[AR5416_EEP4K_NUM_PD_GAINS] = { 0, 0 };
	u32 reg32, regOffset, regChainOffset;

	xpdMask = pEepData->modalHeader.xpdGain;

	if ((pEepData->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_2) {
		pdGainOverlap_t2 =
			pEepData->modalHeader.pdGainOverlap;
	} else {
		pdGainOverlap_t2 = (u16)(MS(REG_READ(ah, AR_PHY_TPCRG5),
					    AR_PHY_TPCRG5_PD_GAIN_OVERLAP));
	}

	pCalBChans = pEepData->calFreqPier2G;
	numPiers = AR5416_EEP4K_NUM_2G_CAL_PIERS;

	numXpdGain = 0;

	for (i = 1; i <= AR5416_PD_GAINS_IN_MASK; i++) {
		if ((xpdMask >> (AR5416_PD_GAINS_IN_MASK - i)) & 1) {
			if (numXpdGain >= AR5416_EEP4K_NUM_PD_GAINS)
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
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_3, 0);

	for (i = 0; i < AR5416_EEP4K_MAX_CHAINS; i++) {
		if (AR_SREV_5416_20_OR_LATER(ah) &&
		    (ah->rxchainmask == 5 || ah->txchainmask == 5) &&
		    (i != 0)) {
			regChainOffset = (i == 1) ? 0x2000 : 0x1000;
		} else
			regChainOffset = i * 0x1000;

		if (pEepData->baseEepHeader.txMask & (1 << i)) {
			pRawDataset = pEepData->calPierData2G[i];

			ath9k_hw_get_gain_boundaries_pdadcs(ah, chan,
					    pRawDataset, pCalBChans,
					    numPiers, pdGainOverlap_t2,
					    gainBoundaries,
					    pdadcValues, numXpdGain);

			ENABLE_REGWRITE_BUFFER(ah);

			if ((i == 0) || AR_SREV_5416_20_OR_LATER(ah)) {
				REG_WRITE(ah, AR_PHY_TPCRG5 + regChainOffset,
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
					((pdadcValues[4 * j + 2] & 0xFF) << 16)|
					((pdadcValues[4 * j + 3] & 0xFF) << 24);
				REG_WRITE(ah, regOffset, reg32);

				ath_dbg(common, ATH_DBG_EEPROM,
					"PDADC (%d,%4x): %4.4x %8.8x\n",
					i, regChainOffset, regOffset,
					reg32);
				ath_dbg(common, ATH_DBG_EEPROM,
					"PDADC: Chain %d | "
					"PDADC %3d Value %3d | "
					"PDADC %3d Value %3d | "
					"PDADC %3d Value %3d | "
					"PDADC %3d Value %3d |\n",
					i, 4 * j, pdadcValues[4 * j],
					4 * j + 1, pdadcValues[4 * j + 1],
					4 * j + 2, pdadcValues[4 * j + 2],
					4 * j + 3, pdadcValues[4 * j + 3]);

				regOffset += 4;
			}

			REGWRITE_BUFFER_FLUSH(ah);
		}
	}

	*pTxPowerIndexOffset = 0;
}

static void ath9k_hw_set_4k_power_per_rate_table(struct ath_hw *ah,
						 struct ath9k_channel *chan,
						 int16_t *ratesArray,
						 u16 cfgCtl,
						 u16 AntennaReduction,
						 u16 twiceMaxRegulatoryPower,
						 u16 powerLimit)
{
#define CMP_TEST_GRP \
	(((cfgCtl & ~CTL_MODE_M)| (pCtlMode[ctlMode] & CTL_MODE_M)) ==	\
	 pEepData->ctlIndex[i])						\
	|| (((cfgCtl & ~CTL_MODE_M) | (pCtlMode[ctlMode] & CTL_MODE_M)) == \
	    ((pEepData->ctlIndex[i] & CTL_MODE_M) | SD_NO_CTL))

	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	int i;
	int16_t twiceLargestAntenna;
	u16 twiceMinEdgePower;
	u16 twiceMaxEdgePower = MAX_RATE_POWER;
	u16 scaledPower = 0, minCtlPower, maxRegAllowedPower;
	u16 numCtlModes;
	const u16 *pCtlMode;
	u16 ctlMode, freq;
	struct chan_centers centers;
	struct cal_ctl_data_4k *rep;
	struct ar5416_eeprom_4k *pEepData = &ah->eeprom.map4k;
	static const u16 tpScaleReductionTable[5] =
		{ 0, 3, 6, 9, MAX_RATE_POWER };
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
	static const u16 ctlModesFor11g[] = {
		CTL_11B, CTL_11G, CTL_2GHT20,
		CTL_11B_EXT, CTL_11G_EXT, CTL_2GHT40
	};

	ath9k_hw_get_channel_centers(ah, chan, &centers);

	twiceLargestAntenna = pEepData->modalHeader.antennaGainCh[0];
	twiceLargestAntenna = (int16_t)min(AntennaReduction -
					   twiceLargestAntenna, 0);

	maxRegAllowedPower = twiceMaxRegulatoryPower + twiceLargestAntenna;
	if (regulatory->tp_scale != ATH9K_TP_SCALE_MAX) {
		maxRegAllowedPower -=
			(tpScaleReductionTable[(regulatory->tp_scale)] * 2);
	}

	scaledPower = min(powerLimit, maxRegAllowedPower);
	scaledPower = max((u16)0, scaledPower);

	numCtlModes = ARRAY_SIZE(ctlModesFor11g) - SUB_NUM_CTL_MODES_AT_2G_40;
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
			twiceMaxEdgePower = MAX_RATE_POWER;

		for (i = 0; (i < AR5416_EEP4K_NUM_CTLS) &&
			     pEepData->ctlIndex[i]; i++) {

			if (CMP_TEST_GRP) {
				rep = &(pEepData->ctlData[i]);

				twiceMinEdgePower = ath9k_hw_get_max_edge_power(
					freq,
					rep->ctlEdges[
					ar5416_get_ntxchains(ah->txchainmask) - 1],
					IS_CHAN_2GHZ(chan),
					AR5416_EEP4K_NUM_BAND_EDGES);

				if ((cfgCtl & ~CTL_MODE_M) == SD_NO_CTL) {
					twiceMaxEdgePower =
						min(twiceMaxEdgePower,
						    twiceMinEdgePower);
				} else {
					twiceMaxEdgePower = twiceMinEdgePower;
					break;
				}
			}
		}

		minCtlPower = (u8)min(twiceMaxEdgePower, scaledPower);

		switch (pCtlMode[ctlMode]) {
		case CTL_11B:
			for (i = 0; i < ARRAY_SIZE(targetPowerCck.tPow2x); i++) {
				targetPowerCck.tPow2x[i] =
					min((u16)targetPowerCck.tPow2x[i],
					    minCtlPower);
			}
			break;
		case CTL_11G:
			for (i = 0; i < ARRAY_SIZE(targetPowerOfdm.tPow2x); i++) {
				targetPowerOfdm.tPow2x[i] =
					min((u16)targetPowerOfdm.tPow2x[i],
					    minCtlPower);
			}
			break;
		case CTL_2GHT20:
			for (i = 0; i < ARRAY_SIZE(targetPowerHt20.tPow2x); i++) {
				targetPowerHt20.tPow2x[i] =
					min((u16)targetPowerHt20.tPow2x[i],
					    minCtlPower);
			}
			break;
		case CTL_11B_EXT:
			targetPowerCckExt.tPow2x[0] =
				min((u16)targetPowerCckExt.tPow2x[0],
				    minCtlPower);
			break;
		case CTL_11G_EXT:
			targetPowerOfdmExt.tPow2x[0] =
				min((u16)targetPowerOfdmExt.tPow2x[0],
				    minCtlPower);
			break;
		case CTL_2GHT40:
			for (i = 0; i < ARRAY_SIZE(targetPowerHt40.tPow2x); i++) {
				targetPowerHt40.tPow2x[i] =
					min((u16)targetPowerHt40.tPow2x[i],
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

	ratesArray[rate1l] = targetPowerCck.tPow2x[0];
	ratesArray[rate2s] = ratesArray[rate2l] = targetPowerCck.tPow2x[1];
	ratesArray[rate5_5s] = ratesArray[rate5_5l] = targetPowerCck.tPow2x[2];
	ratesArray[rate11s] = ratesArray[rate11l] = targetPowerCck.tPow2x[3];

	if (IS_CHAN_HT40(chan)) {
		for (i = 0; i < ARRAY_SIZE(targetPowerHt40.tPow2x); i++) {
			ratesArray[rateHt40_0 + i] =
				targetPowerHt40.tPow2x[i];
		}
		ratesArray[rateDupOfdm] = targetPowerHt40.tPow2x[0];
		ratesArray[rateDupCck] = targetPowerHt40.tPow2x[0];
		ratesArray[rateExtOfdm] = targetPowerOfdmExt.tPow2x[0];
		ratesArray[rateExtCck] = targetPowerCckExt.tPow2x[0];
	}

#undef CMP_TEST_GRP
}

static void ath9k_hw_4k_set_txpower(struct ath_hw *ah,
				    struct ath9k_channel *chan,
				    u16 cfgCtl,
				    u8 twiceAntennaReduction,
				    u8 twiceMaxRegulatoryPower,
				    u8 powerLimit, bool test)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	struct ar5416_eeprom_4k *pEepData = &ah->eeprom.map4k;
	struct modal_eep_4k_header *pModal = &pEepData->modalHeader;
	int16_t ratesArray[Ar5416RateSize];
	int16_t txPowerIndexOffset = 0;
	u8 ht40PowerIncForPdadc = 2;
	int i;

	memset(ratesArray, 0, sizeof(ratesArray));

	if ((pEepData->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_2) {
		ht40PowerIncForPdadc = pModal->ht40PowerIncForPdadc;
	}

	ath9k_hw_set_4k_power_per_rate_table(ah, chan,
					     &ratesArray[0], cfgCtl,
					     twiceAntennaReduction,
					     twiceMaxRegulatoryPower,
					     powerLimit);

	ath9k_hw_set_4k_power_cal_table(ah, chan, &txPowerIndexOffset);

	regulatory->max_power_level = 0;
	for (i = 0; i < ARRAY_SIZE(ratesArray); i++) {
		ratesArray[i] =	(int16_t)(txPowerIndexOffset + ratesArray[i]);
		if (ratesArray[i] > MAX_RATE_POWER)
			ratesArray[i] = MAX_RATE_POWER;

		if (ratesArray[i] > regulatory->max_power_level)
			regulatory->max_power_level = ratesArray[i];
	}

	if (test)
	    return;

	/* Update regulatory */
	i = rate6mb;
	if (IS_CHAN_HT40(chan))
		i = rateHt40_0;
	else if (IS_CHAN_HT20(chan))
		i = rateHt20_0;

	regulatory->max_power_level = ratesArray[i];

	if (AR_SREV_9280_20_OR_LATER(ah)) {
		for (i = 0; i < Ar5416RateSize; i++)
			ratesArray[i] -= AR5416_PWR_TABLE_OFFSET_DB * 2;
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

	REGWRITE_BUFFER_FLUSH(ah);
}

static void ath9k_hw_4k_set_addac(struct ath_hw *ah,
				  struct ath9k_channel *chan)
{
	struct modal_eep_4k_header *pModal;
	struct ar5416_eeprom_4k *eep = &ah->eeprom.map4k;
	u8 biaslevel;

	if (ah->hw_version.macVersion != AR_SREV_VERSION_9160)
		return;

	if (ah->eep_ops->get_eeprom_rev(ah) < AR5416_EEP_MINOR_VER_7)
		return;

	pModal = &eep->modalHeader;

	if (pModal->xpaBiasLvl != 0xff) {
		biaslevel = pModal->xpaBiasLvl;
		INI_RA(&ah->iniAddac, 7, 1) =
		  (INI_RA(&ah->iniAddac, 7, 1) & (~0x18)) | biaslevel << 3;
	}
}

static void ath9k_hw_4k_set_gain(struct ath_hw *ah,
				 struct modal_eep_4k_header *pModal,
				 struct ar5416_eeprom_4k *eep,
				 u8 txRxAttenLocal)
{
	REG_WRITE(ah, AR_PHY_SWITCH_CHAIN_0,
		  pModal->antCtrlChain[0]);

	REG_WRITE(ah, AR_PHY_TIMING_CTRL4(0),
		  (REG_READ(ah, AR_PHY_TIMING_CTRL4(0)) &
		   ~(AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF |
		     AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF)) |
		  SM(pModal->iqCalICh[0], AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF) |
		  SM(pModal->iqCalQCh[0], AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF));

	if ((eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_3) {
		txRxAttenLocal = pModal->txRxAttenCh[0];

		REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ,
			      AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN, pModal->bswMargin[0]);
		REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ,
			      AR_PHY_GAIN_2GHZ_XATTEN1_DB, pModal->bswAtten[0]);
		REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ,
			      AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN,
			      pModal->xatten2Margin[0]);
		REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ,
			      AR_PHY_GAIN_2GHZ_XATTEN2_DB, pModal->xatten2Db[0]);

		/* Set the block 1 value to block 0 value */
		REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000,
			      AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN,
			      pModal->bswMargin[0]);
		REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000,
			      AR_PHY_GAIN_2GHZ_XATTEN1_DB, pModal->bswAtten[0]);
		REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000,
			      AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN,
			      pModal->xatten2Margin[0]);
		REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000,
			      AR_PHY_GAIN_2GHZ_XATTEN2_DB,
			      pModal->xatten2Db[0]);
	}

	REG_RMW_FIELD(ah, AR_PHY_RXGAIN,
		      AR9280_PHY_RXGAIN_TXRX_ATTEN, txRxAttenLocal);
	REG_RMW_FIELD(ah, AR_PHY_RXGAIN,
		      AR9280_PHY_RXGAIN_TXRX_MARGIN, pModal->rxTxMarginCh[0]);

	REG_RMW_FIELD(ah, AR_PHY_RXGAIN + 0x1000,
		      AR9280_PHY_RXGAIN_TXRX_ATTEN, txRxAttenLocal);
	REG_RMW_FIELD(ah, AR_PHY_RXGAIN + 0x1000,
		      AR9280_PHY_RXGAIN_TXRX_MARGIN, pModal->rxTxMarginCh[0]);
}

/*
 * Read EEPROM header info and program the device for correct operation
 * given the channel value.
 */
static void ath9k_hw_4k_set_board_values(struct ath_hw *ah,
					 struct ath9k_channel *chan)
{
	struct modal_eep_4k_header *pModal;
	struct ar5416_eeprom_4k *eep = &ah->eeprom.map4k;
	u8 txRxAttenLocal;
	u8 ob[5], db1[5], db2[5];
	u8 ant_div_control1, ant_div_control2;
	u32 regVal;

	pModal = &eep->modalHeader;
	txRxAttenLocal = 23;

	REG_WRITE(ah, AR_PHY_SWITCH_COM, pModal->antCtrlCommon);

	/* Single chain for 4K EEPROM*/
	ath9k_hw_4k_set_gain(ah, pModal, eep, txRxAttenLocal);

	/* Initialize Ant Diversity settings from EEPROM */
	if (pModal->version >= 3) {
		ant_div_control1 = pModal->antdiv_ctl1;
		ant_div_control2 = pModal->antdiv_ctl2;

		regVal = REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
		regVal &= (~(AR_PHY_9285_ANT_DIV_CTL_ALL));

		regVal |= SM(ant_div_control1,
			     AR_PHY_9285_ANT_DIV_CTL);
		regVal |= SM(ant_div_control2,
			     AR_PHY_9285_ANT_DIV_ALT_LNACONF);
		regVal |= SM((ant_div_control2 >> 2),
			     AR_PHY_9285_ANT_DIV_MAIN_LNACONF);
		regVal |= SM((ant_div_control1 >> 1),
			     AR_PHY_9285_ANT_DIV_ALT_GAINTB);
		regVal |= SM((ant_div_control1 >> 2),
			     AR_PHY_9285_ANT_DIV_MAIN_GAINTB);


		REG_WRITE(ah, AR_PHY_MULTICHAIN_GAIN_CTL, regVal);
		regVal = REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
		regVal = REG_READ(ah, AR_PHY_CCK_DETECT);
		regVal &= (~AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
		regVal |= SM((ant_div_control1 >> 3),
			     AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);

		REG_WRITE(ah, AR_PHY_CCK_DETECT, regVal);
		regVal = REG_READ(ah, AR_PHY_CCK_DETECT);
	}

	if (pModal->version >= 2) {
		ob[0] = pModal->ob_0;
		ob[1] = pModal->ob_1;
		ob[2] = pModal->ob_2;
		ob[3] = pModal->ob_3;
		ob[4] = pModal->ob_4;

		db1[0] = pModal->db1_0;
		db1[1] = pModal->db1_1;
		db1[2] = pModal->db1_2;
		db1[3] = pModal->db1_3;
		db1[4] = pModal->db1_4;

		db2[0] = pModal->db2_0;
		db2[1] = pModal->db2_1;
		db2[2] = pModal->db2_2;
		db2[3] = pModal->db2_3;
		db2[4] = pModal->db2_4;
	} else if (pModal->version == 1) {
		ob[0] = pModal->ob_0;
		ob[1] = ob[2] = ob[3] = ob[4] = pModal->ob_1;
		db1[0] = pModal->db1_0;
		db1[1] = db1[2] = db1[3] = db1[4] = pModal->db1_1;
		db2[0] = pModal->db2_0;
		db2[1] = db2[2] = db2[3] = db2[4] = pModal->db2_1;
	} else {
		int i;

		for (i = 0; i < 5; i++) {
			ob[i] = pModal->ob_0;
			db1[i] = pModal->db1_0;
			db2[i] = pModal->db1_0;
		}
	}

	if (AR_SREV_9271(ah)) {
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9271_AN_RF2G3_OB_cck,
					  AR9271_AN_RF2G3_OB_cck_S,
					  ob[0]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9271_AN_RF2G3_OB_psk,
					  AR9271_AN_RF2G3_OB_psk_S,
					  ob[1]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9271_AN_RF2G3_OB_qam,
					  AR9271_AN_RF2G3_OB_qam_S,
					  ob[2]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9271_AN_RF2G3_DB_1,
					  AR9271_AN_RF2G3_DB_1_S,
					  db1[0]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G4,
					  AR9271_AN_RF2G4_DB_2,
					  AR9271_AN_RF2G4_DB_2_S,
					  db2[0]);
	} else {
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9285_AN_RF2G3_OB_0,
					  AR9285_AN_RF2G3_OB_0_S,
					  ob[0]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9285_AN_RF2G3_OB_1,
					  AR9285_AN_RF2G3_OB_1_S,
					  ob[1]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9285_AN_RF2G3_OB_2,
					  AR9285_AN_RF2G3_OB_2_S,
					  ob[2]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9285_AN_RF2G3_OB_3,
					  AR9285_AN_RF2G3_OB_3_S,
					  ob[3]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9285_AN_RF2G3_OB_4,
					  AR9285_AN_RF2G3_OB_4_S,
					  ob[4]);

		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9285_AN_RF2G3_DB1_0,
					  AR9285_AN_RF2G3_DB1_0_S,
					  db1[0]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9285_AN_RF2G3_DB1_1,
					  AR9285_AN_RF2G3_DB1_1_S,
					  db1[1]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G3,
					  AR9285_AN_RF2G3_DB1_2,
					  AR9285_AN_RF2G3_DB1_2_S,
					  db1[2]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G4,
					  AR9285_AN_RF2G4_DB1_3,
					  AR9285_AN_RF2G4_DB1_3_S,
					  db1[3]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G4,
					  AR9285_AN_RF2G4_DB1_4,
					  AR9285_AN_RF2G4_DB1_4_S, db1[4]);

		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G4,
					  AR9285_AN_RF2G4_DB2_0,
					  AR9285_AN_RF2G4_DB2_0_S,
					  db2[0]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G4,
					  AR9285_AN_RF2G4_DB2_1,
					  AR9285_AN_RF2G4_DB2_1_S,
					  db2[1]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G4,
					  AR9285_AN_RF2G4_DB2_2,
					  AR9285_AN_RF2G4_DB2_2_S,
					  db2[2]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G4,
					  AR9285_AN_RF2G4_DB2_3,
					  AR9285_AN_RF2G4_DB2_3_S,
					  db2[3]);
		ath9k_hw_analog_shift_rmw(ah,
					  AR9285_AN_RF2G4,
					  AR9285_AN_RF2G4_DB2_4,
					  AR9285_AN_RF2G4_DB2_4_S,
					  db2[4]);
	}


	REG_RMW_FIELD(ah, AR_PHY_SETTLING, AR_PHY_SETTLING_SWITCH,
		      pModal->switchSettling);
	REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ, AR_PHY_DESIRED_SZ_ADC,
		      pModal->adcDesiredSize);

	REG_WRITE(ah, AR_PHY_RF_CTL4,
		  SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAA_OFF) |
		  SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAB_OFF) |
		  SM(pModal->txFrameToXpaOn, AR_PHY_RF_CTL4_FRAME_XPAA_ON)  |
		  SM(pModal->txFrameToXpaOn, AR_PHY_RF_CTL4_FRAME_XPAB_ON));

	REG_RMW_FIELD(ah, AR_PHY_RF_CTL3, AR_PHY_TX_END_TO_A2_RX_ON,
		      pModal->txEndToRxOn);

	if (AR_SREV_9271_10(ah))
		REG_RMW_FIELD(ah, AR_PHY_RF_CTL3, AR_PHY_TX_END_TO_A2_RX_ON,
			      pModal->txEndToRxOn);
	REG_RMW_FIELD(ah, AR_PHY_CCA, AR9280_PHY_CCA_THRESH62,
		      pModal->thresh62);
	REG_RMW_FIELD(ah, AR_PHY_EXT_CCA0, AR_PHY_EXT_CCA0_THRESH62,
		      pModal->thresh62);

	if ((eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
						AR5416_EEP_MINOR_VER_2) {
		REG_RMW_FIELD(ah, AR_PHY_RF_CTL2, AR_PHY_TX_END_DATA_START,
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
}

static u16 ath9k_hw_4k_get_spur_channel(struct ath_hw *ah, u16 i, bool is2GHz)
{
#define EEP_MAP4K_SPURCHAN \
	(ah->eeprom.map4k.modalHeader.spurChans[i].spurChan)
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
		spur_val = EEP_MAP4K_SPURCHAN;
		break;
	}

	return spur_val;

#undef EEP_MAP4K_SPURCHAN
}

const struct eeprom_ops eep_4k_ops = {
	.check_eeprom		= ath9k_hw_4k_check_eeprom,
	.get_eeprom		= ath9k_hw_4k_get_eeprom,
	.fill_eeprom		= ath9k_hw_4k_fill_eeprom,
	.get_eeprom_ver		= ath9k_hw_4k_get_eeprom_ver,
	.get_eeprom_rev		= ath9k_hw_4k_get_eeprom_rev,
	.set_board_values	= ath9k_hw_4k_set_board_values,
	.set_addac		= ath9k_hw_4k_set_addac,
	.set_txpower		= ath9k_hw_4k_set_txpower,
	.get_spur_channel	= ath9k_hw_4k_get_spur_channel
};
