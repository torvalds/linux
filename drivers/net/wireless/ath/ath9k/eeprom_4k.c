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

#include <asm/unaligned.h>
#include "hw.h"
#include "ar9002_phy.h"

static int ath9k_hw_4k_get_eeprom_ver(struct ath_hw *ah)
{
	u16 version = le16_to_cpu(ah->eeprom.map4k.baseEepHeader.version);

	return (version & AR5416_EEP_VER_MAJOR_MASK) >>
		AR5416_EEP_VER_MAJOR_SHIFT;
}

static int ath9k_hw_4k_get_eeprom_rev(struct ath_hw *ah)
{
	u16 version = le16_to_cpu(ah->eeprom.map4k.baseEepHeader.version);

	return version & AR5416_EEP_VER_MINOR_MASK;
}

#define SIZE_EEPROM_4K (sizeof(struct ar5416_eeprom_4k) / sizeof(u16))

static bool __ath9k_hw_4k_fill_eeprom(struct ath_hw *ah)
{
	u16 *eep_data = (u16 *)&ah->eeprom.map4k;
	int addr, eep_start_loc = 64;

	for (addr = 0; addr < SIZE_EEPROM_4K; addr++) {
		if (!ath9k_hw_nvram_read(ah, addr + eep_start_loc, eep_data))
			return false;
		eep_data++;
	}

	return true;
}

static bool __ath9k_hw_usb_4k_fill_eeprom(struct ath_hw *ah)
{
	u16 *eep_data = (u16 *)&ah->eeprom.map4k;

	ath9k_hw_usb_gen_fill_eeprom(ah, eep_data, 64, SIZE_EEPROM_4K);

	return true;
}

static bool ath9k_hw_4k_fill_eeprom(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	if (!ath9k_hw_use_flash(ah)) {
		ath_dbg(common, EEPROM, "Reading from EEPROM, not flash\n");
	}

	if (common->bus_ops->ath_bus_type == ATH_USB)
		return __ath9k_hw_usb_4k_fill_eeprom(ah);
	else
		return __ath9k_hw_4k_fill_eeprom(ah);
}

#ifdef CONFIG_ATH9K_COMMON_DEBUG
static u32 ath9k_dump_4k_modal_eeprom(char *buf, u32 len, u32 size,
				      struct modal_eep_4k_header *modal_hdr)
{
	PR_EEP("Chain0 Ant. Control", le16_to_cpu(modal_hdr->antCtrlChain[0]));
	PR_EEP("Ant. Common Control", le32_to_cpu(modal_hdr->antCtrlCommon));
	PR_EEP("Chain0 Ant. Gain", modal_hdr->antennaGainCh[0]);
	PR_EEP("Switch Settle", modal_hdr->switchSettling);
	PR_EEP("Chain0 TxRxAtten", modal_hdr->txRxAttenCh[0]);
	PR_EEP("Chain0 RxTxMargin", modal_hdr->rxTxMarginCh[0]);
	PR_EEP("ADC Desired size", modal_hdr->adcDesiredSize);
	PR_EEP("PGA Desired size", modal_hdr->pgaDesiredSize);
	PR_EEP("Chain0 xlna Gain", modal_hdr->xlnaGainCh[0]);
	PR_EEP("txEndToXpaOff", modal_hdr->txEndToXpaOff);
	PR_EEP("txEndToRxOn", modal_hdr->txEndToRxOn);
	PR_EEP("txFrameToXpaOn", modal_hdr->txFrameToXpaOn);
	PR_EEP("CCA Threshold)", modal_hdr->thresh62);
	PR_EEP("Chain0 NF Threshold", modal_hdr->noiseFloorThreshCh[0]);
	PR_EEP("xpdGain", modal_hdr->xpdGain);
	PR_EEP("External PD", modal_hdr->xpd);
	PR_EEP("Chain0 I Coefficient", modal_hdr->iqCalICh[0]);
	PR_EEP("Chain0 Q Coefficient", modal_hdr->iqCalQCh[0]);
	PR_EEP("pdGainOverlap", modal_hdr->pdGainOverlap);
	PR_EEP("O/D Bias Version", modal_hdr->version);
	PR_EEP("CCK OutputBias", modal_hdr->ob_0);
	PR_EEP("BPSK OutputBias", modal_hdr->ob_1);
	PR_EEP("QPSK OutputBias", modal_hdr->ob_2);
	PR_EEP("16QAM OutputBias", modal_hdr->ob_3);
	PR_EEP("64QAM OutputBias", modal_hdr->ob_4);
	PR_EEP("CCK Driver1_Bias", modal_hdr->db1_0);
	PR_EEP("BPSK Driver1_Bias", modal_hdr->db1_1);
	PR_EEP("QPSK Driver1_Bias", modal_hdr->db1_2);
	PR_EEP("16QAM Driver1_Bias", modal_hdr->db1_3);
	PR_EEP("64QAM Driver1_Bias", modal_hdr->db1_4);
	PR_EEP("CCK Driver2_Bias", modal_hdr->db2_0);
	PR_EEP("BPSK Driver2_Bias", modal_hdr->db2_1);
	PR_EEP("QPSK Driver2_Bias", modal_hdr->db2_2);
	PR_EEP("16QAM Driver2_Bias", modal_hdr->db2_3);
	PR_EEP("64QAM Driver2_Bias", modal_hdr->db2_4);
	PR_EEP("xPA Bias Level", modal_hdr->xpaBiasLvl);
	PR_EEP("txFrameToDataStart", modal_hdr->txFrameToDataStart);
	PR_EEP("txFrameToPaOn", modal_hdr->txFrameToPaOn);
	PR_EEP("HT40 Power Inc.", modal_hdr->ht40PowerIncForPdadc);
	PR_EEP("Chain0 bswAtten", modal_hdr->bswAtten[0]);
	PR_EEP("Chain0 bswMargin", modal_hdr->bswMargin[0]);
	PR_EEP("HT40 Switch Settle", modal_hdr->swSettleHt40);
	PR_EEP("Chain0 xatten2Db", modal_hdr->xatten2Db[0]);
	PR_EEP("Chain0 xatten2Margin", modal_hdr->xatten2Margin[0]);
	PR_EEP("Ant. Diversity ctl1", modal_hdr->antdiv_ctl1);
	PR_EEP("Ant. Diversity ctl2", modal_hdr->antdiv_ctl2);
	PR_EEP("TX Diversity", modal_hdr->tx_diversity);

	return len;
}

static u32 ath9k_hw_4k_dump_eeprom(struct ath_hw *ah, bool dump_base_hdr,
				       u8 *buf, u32 len, u32 size)
{
	struct ar5416_eeprom_4k *eep = &ah->eeprom.map4k;
	struct base_eep_header_4k *pBase = &eep->baseEepHeader;
	u32 binBuildNumber = le32_to_cpu(pBase->binBuildNumber);

	if (!dump_base_hdr) {
		len += scnprintf(buf + len, size - len,
				 "%20s :\n", "2GHz modal Header");
		len = ath9k_dump_4k_modal_eeprom(buf, len, size,
						 &eep->modalHeader);
		goto out;
	}

	PR_EEP("Major Version", ath9k_hw_4k_get_eeprom_ver(ah));
	PR_EEP("Minor Version", ath9k_hw_4k_get_eeprom_rev(ah));
	PR_EEP("Checksum", le16_to_cpu(pBase->checksum));
	PR_EEP("Length", le16_to_cpu(pBase->length));
	PR_EEP("RegDomain1", le16_to_cpu(pBase->regDmn[0]));
	PR_EEP("RegDomain2", le16_to_cpu(pBase->regDmn[1]));
	PR_EEP("TX Mask", pBase->txMask);
	PR_EEP("RX Mask", pBase->rxMask);
	PR_EEP("Allow 5GHz", !!(pBase->opCapFlags & AR5416_OPFLAGS_11A));
	PR_EEP("Allow 2GHz", !!(pBase->opCapFlags & AR5416_OPFLAGS_11G));
	PR_EEP("Disable 2GHz HT20", !!(pBase->opCapFlags &
					AR5416_OPFLAGS_N_2G_HT20));
	PR_EEP("Disable 2GHz HT40", !!(pBase->opCapFlags &
					AR5416_OPFLAGS_N_2G_HT40));
	PR_EEP("Disable 5Ghz HT20", !!(pBase->opCapFlags &
					AR5416_OPFLAGS_N_5G_HT20));
	PR_EEP("Disable 5Ghz HT40", !!(pBase->opCapFlags &
					AR5416_OPFLAGS_N_5G_HT40));
	PR_EEP("Big Endian", !!(pBase->eepMisc & AR5416_EEPMISC_BIG_ENDIAN));
	PR_EEP("Cal Bin Major Ver", (binBuildNumber >> 24) & 0xFF);
	PR_EEP("Cal Bin Minor Ver", (binBuildNumber >> 16) & 0xFF);
	PR_EEP("Cal Bin Build", (binBuildNumber >> 8) & 0xFF);
	PR_EEP("TX Gain type", pBase->txGainType);

	len += scnprintf(buf + len, size - len, "%20s : %pM\n", "MacAddress",
			 pBase->macAddr);

out:
	if (len > size)
		len = size;

	return len;
}
#else
static u32 ath9k_hw_4k_dump_eeprom(struct ath_hw *ah, bool dump_base_hdr,
				       u8 *buf, u32 len, u32 size)
{
	return 0;
}
#endif

static int ath9k_hw_4k_check_eeprom(struct ath_hw *ah)
{
	struct ar5416_eeprom_4k *eep = &ah->eeprom.map4k;
	u32 el;
	bool need_swap;
	int i, err;

	err = ath9k_hw_nvram_swap_data(ah, &need_swap, SIZE_EEPROM_4K);
	if (err)
		return err;

	if (need_swap)
		el = swab16((__force u16)eep->baseEepHeader.length);
	else
		el = le16_to_cpu(eep->baseEepHeader.length);

	el = min(el / sizeof(u16), SIZE_EEPROM_4K);
	if (!ath9k_hw_nvram_validate_checksum(ah, el))
		return -EINVAL;

	if (need_swap) {
		EEPROM_FIELD_SWAB16(eep->baseEepHeader.length);
		EEPROM_FIELD_SWAB16(eep->baseEepHeader.checksum);
		EEPROM_FIELD_SWAB16(eep->baseEepHeader.version);
		EEPROM_FIELD_SWAB16(eep->baseEepHeader.regDmn[0]);
		EEPROM_FIELD_SWAB16(eep->baseEepHeader.regDmn[1]);
		EEPROM_FIELD_SWAB16(eep->baseEepHeader.rfSilent);
		EEPROM_FIELD_SWAB16(eep->baseEepHeader.blueToothOptions);
		EEPROM_FIELD_SWAB16(eep->baseEepHeader.deviceCap);
		EEPROM_FIELD_SWAB32(eep->modalHeader.antCtrlCommon);

		for (i = 0; i < AR5416_EEP4K_MAX_CHAINS; i++)
			EEPROM_FIELD_SWAB32(eep->modalHeader.antCtrlChain[i]);

		for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++)
			EEPROM_FIELD_SWAB16(
				eep->modalHeader.spurChans[i].spurChan);
	}

	if (!ath9k_hw_nvram_check_version(ah, AR5416_EEP_VER,
	    AR5416_EEP_NO_BACK_VER))
		return -EINVAL;

	return 0;
}

#undef SIZE_EEPROM_4K

static u32 ath9k_hw_4k_get_eeprom(struct ath_hw *ah,
				  enum eeprom_param param)
{
	struct ar5416_eeprom_4k *eep = &ah->eeprom.map4k;
	struct modal_eep_4k_header *pModal = &eep->modalHeader;
	struct base_eep_header_4k *pBase = &eep->baseEepHeader;

	switch (param) {
	case EEP_NFTHRESH_2:
		return pModal->noiseFloorThreshCh[0];
	case EEP_MAC_LSW:
		return get_unaligned_be16(pBase->macAddr);
	case EEP_MAC_MID:
		return get_unaligned_be16(pBase->macAddr + 2);
	case EEP_MAC_MSW:
		return get_unaligned_be16(pBase->macAddr + 4);
	case EEP_REG_0:
		return le16_to_cpu(pBase->regDmn[0]);
	case EEP_OP_CAP:
		return le16_to_cpu(pBase->deviceCap);
	case EEP_OP_MODE:
		return pBase->opCapFlags;
	case EEP_RF_SILENT:
		return le16_to_cpu(pBase->rfSilent);
	case EEP_OB_2:
		return pModal->ob_0;
	case EEP_DB_2:
		return pModal->db1_1;
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
		return pBase->txGainType;
	case EEP_ANTENNA_GAIN_2G:
		return pModal->antennaGainCh[0];
	default:
		return 0;
	}
}

static void ath9k_hw_set_4k_power_cal_table(struct ath_hw *ah,
				  struct ath9k_channel *chan)
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

	if (ath9k_hw_4k_get_eeprom_rev(ah) >= AR5416_EEP_MINOR_VER_2)
		pdGainOverlap_t2 =
			pEepData->modalHeader.pdGainOverlap;
	else
		pdGainOverlap_t2 = (u16)(MS(REG_READ(ah, AR_PHY_TPCRG5),
					    AR_PHY_TPCRG5_PD_GAIN_OVERLAP));

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

	ENABLE_REG_RMW_BUFFER(ah);
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_NUM_PD_GAIN,
		      (numXpdGain - 1) & 0x3);
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_1,
		      xpdGainValues[0]);
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_2,
		      xpdGainValues[1]);
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_3, 0);
	REG_RMW_BUFFER_FLUSH(ah);

	for (i = 0; i < AR5416_EEP4K_MAX_CHAINS; i++) {
		regChainOffset = i * 0x1000;

		if (pEepData->baseEepHeader.txMask & (1 << i)) {
			pRawDataset = pEepData->calPierData2G[i];

			ath9k_hw_get_gain_boundaries_pdadcs(ah, chan,
					    pRawDataset, pCalBChans,
					    numPiers, pdGainOverlap_t2,
					    gainBoundaries,
					    pdadcValues, numXpdGain);

			ENABLE_REGWRITE_BUFFER(ah);

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

			regOffset = AR_PHY_BASE + (672 << 2) + regChainOffset;
			for (j = 0; j < 32; j++) {
				reg32 = get_unaligned_le32(&pdadcValues[4 * j]);
				REG_WRITE(ah, regOffset, reg32);

				ath_dbg(common, EEPROM,
					"PDADC (%d,%4x): %4.4x %8.8x\n",
					i, regChainOffset, regOffset,
					reg32);
				ath_dbg(common, EEPROM,
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
}

static void ath9k_hw_set_4k_power_per_rate_table(struct ath_hw *ah,
						 struct ath9k_channel *chan,
						 int16_t *ratesArray,
						 u16 cfgCtl,
						 u16 antenna_reduction,
						 u16 powerLimit)
{
#define CMP_TEST_GRP \
	(((cfgCtl & ~CTL_MODE_M)| (pCtlMode[ctlMode] & CTL_MODE_M)) ==	\
	 pEepData->ctlIndex[i])						\
	|| (((cfgCtl & ~CTL_MODE_M) | (pCtlMode[ctlMode] & CTL_MODE_M)) == \
	    ((pEepData->ctlIndex[i] & CTL_MODE_M) | SD_NO_CTL))

	int i;
	u16 twiceMinEdgePower;
	u16 twiceMaxEdgePower;
	u16 scaledPower = 0, minCtlPower;
	u16 numCtlModes;
	const u16 *pCtlMode;
	u16 ctlMode, freq;
	struct chan_centers centers;
	struct cal_ctl_data_4k *rep;
	struct ar5416_eeprom_4k *pEepData = &ah->eeprom.map4k;
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

	scaledPower = powerLimit - antenna_reduction;
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
				    u8 powerLimit, bool test)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	struct ar5416_eeprom_4k *pEepData = &ah->eeprom.map4k;
	struct modal_eep_4k_header *pModal = &pEepData->modalHeader;
	int16_t ratesArray[Ar5416RateSize];
	u8 ht40PowerIncForPdadc = 2;
	int i;

	memset(ratesArray, 0, sizeof(ratesArray));

	if (ath9k_hw_4k_get_eeprom_rev(ah) >= AR5416_EEP_MINOR_VER_2)
		ht40PowerIncForPdadc = pModal->ht40PowerIncForPdadc;

	ath9k_hw_set_4k_power_per_rate_table(ah, chan,
					     &ratesArray[0], cfgCtl,
					     twiceAntennaReduction,
					     powerLimit);

	ath9k_hw_set_4k_power_cal_table(ah, chan);

	regulatory->max_power_level = 0;
	for (i = 0; i < ARRAY_SIZE(ratesArray); i++) {
		if (ratesArray[i] > MAX_RATE_POWER)
			ratesArray[i] = MAX_RATE_POWER;

		if (ratesArray[i] > regulatory->max_power_level)
			regulatory->max_power_level = ratesArray[i];
	}

	if (test)
	    return;

	for (i = 0; i < Ar5416RateSize; i++)
		ratesArray[i] -= AR5416_PWR_TABLE_OFFSET_DB * 2;

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

	/* TPC initializations */
	if (ah->tpc_enabled) {
		int ht40_delta;

		ht40_delta = (IS_CHAN_HT40(chan)) ? ht40PowerIncForPdadc : 0;
		ar5008_hw_init_rate_txpower(ah, ratesArray, chan, ht40_delta);
		/* Enable TPC */
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE_MAX,
			MAX_RATE_POWER | AR_PHY_POWER_TX_RATE_MAX_TPC_ENABLE);
	} else {
		/* Disable TPC */
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE_MAX, MAX_RATE_POWER);
	}

	REGWRITE_BUFFER_FLUSH(ah);
}

static void ath9k_hw_4k_set_gain(struct ath_hw *ah,
				 struct modal_eep_4k_header *pModal,
				 struct ar5416_eeprom_4k *eep,
				 u8 txRxAttenLocal)
{
	ENABLE_REG_RMW_BUFFER(ah);
	REG_RMW(ah, AR_PHY_SWITCH_CHAIN_0,
		le32_to_cpu(pModal->antCtrlChain[0]), 0);

	REG_RMW(ah, AR_PHY_TIMING_CTRL4(0),
		SM(pModal->iqCalICh[0], AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF) |
		SM(pModal->iqCalQCh[0], AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF),
		AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF | AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF);

	if (ath9k_hw_4k_get_eeprom_rev(ah) >= AR5416_EEP_MINOR_VER_3) {
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
	REG_RMW_BUFFER_FLUSH(ah);
}

/*
 * Read EEPROM header info and program the device for correct operation
 * given the channel value.
 */
static void ath9k_hw_4k_set_board_values(struct ath_hw *ah,
					 struct ath9k_channel *chan)
{
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	struct modal_eep_4k_header *pModal;
	struct ar5416_eeprom_4k *eep = &ah->eeprom.map4k;
	struct base_eep_header_4k *pBase = &eep->baseEepHeader;
	u8 txRxAttenLocal;
	u8 ob[5], db1[5], db2[5];
	u8 ant_div_control1, ant_div_control2;
	u8 bb_desired_scale;
	u32 regVal;

	pModal = &eep->modalHeader;
	txRxAttenLocal = 23;

	REG_WRITE(ah, AR_PHY_SWITCH_COM, le32_to_cpu(pModal->antCtrlCommon));

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

		if (pCap->hw_caps & ATH9K_HW_CAP_ANT_DIV_COMB) {
			/*
			 * If diversity combining is enabled,
			 * set MAIN to LNA1 and ALT to LNA2 initially.
			 */
			regVal = REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
			regVal &= (~(AR_PHY_9285_ANT_DIV_MAIN_LNACONF |
				     AR_PHY_9285_ANT_DIV_ALT_LNACONF));

			regVal |= (ATH_ANT_DIV_COMB_LNA1 <<
				   AR_PHY_9285_ANT_DIV_MAIN_LNACONF_S);
			regVal |= (ATH_ANT_DIV_COMB_LNA2 <<
				   AR_PHY_9285_ANT_DIV_ALT_LNACONF_S);
			regVal &= (~(AR_PHY_9285_FAST_DIV_BIAS));
			regVal |= (0 << AR_PHY_9285_FAST_DIV_BIAS_S);
			REG_WRITE(ah, AR_PHY_MULTICHAIN_GAIN_CTL, regVal);
		}
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

	ENABLE_REG_RMW_BUFFER(ah);
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
	REG_RMW_BUFFER_FLUSH(ah);

	ENABLE_REG_RMW_BUFFER(ah);
	REG_RMW_FIELD(ah, AR_PHY_SETTLING, AR_PHY_SETTLING_SWITCH,
		      pModal->switchSettling);
	REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ, AR_PHY_DESIRED_SZ_ADC,
		      pModal->adcDesiredSize);

	REG_RMW(ah, AR_PHY_RF_CTL4,
		SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAA_OFF) |
		SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAB_OFF) |
		SM(pModal->txFrameToXpaOn, AR_PHY_RF_CTL4_FRAME_XPAA_ON)  |
		SM(pModal->txFrameToXpaOn, AR_PHY_RF_CTL4_FRAME_XPAB_ON), 0);

	REG_RMW_FIELD(ah, AR_PHY_RF_CTL3, AR_PHY_TX_END_TO_A2_RX_ON,
		      pModal->txEndToRxOn);

	if (AR_SREV_9271_10(ah))
		REG_RMW_FIELD(ah, AR_PHY_RF_CTL3, AR_PHY_TX_END_TO_A2_RX_ON,
			      pModal->txEndToRxOn);
	REG_RMW_FIELD(ah, AR_PHY_CCA, AR9280_PHY_CCA_THRESH62,
		      pModal->thresh62);
	REG_RMW_FIELD(ah, AR_PHY_EXT_CCA0, AR_PHY_EXT_CCA0_THRESH62,
		      pModal->thresh62);

	if (ath9k_hw_4k_get_eeprom_rev(ah) >= AR5416_EEP_MINOR_VER_2) {
		REG_RMW_FIELD(ah, AR_PHY_RF_CTL2, AR_PHY_TX_END_DATA_START,
			      pModal->txFrameToDataStart);
		REG_RMW_FIELD(ah, AR_PHY_RF_CTL2, AR_PHY_TX_END_PA_ON,
			      pModal->txFrameToPaOn);
	}

	if (ath9k_hw_4k_get_eeprom_rev(ah) >= AR5416_EEP_MINOR_VER_3) {
		if (IS_CHAN_HT40(chan))
			REG_RMW_FIELD(ah, AR_PHY_SETTLING,
				      AR_PHY_SETTLING_SWITCH,
				      pModal->swSettleHt40);
	}

	REG_RMW_BUFFER_FLUSH(ah);

	bb_desired_scale = (pModal->bb_scale_smrt_antenna &
			EEP_4K_BB_DESIRED_SCALE_MASK);
	if ((pBase->txGainType == 0) && (bb_desired_scale != 0)) {
		u32 pwrctrl, mask, clr;

		mask = BIT(0)|BIT(5)|BIT(10)|BIT(15)|BIT(20)|BIT(25);
		pwrctrl = mask * bb_desired_scale;
		clr = mask * 0x1f;
		ENABLE_REG_RMW_BUFFER(ah);
		REG_RMW(ah, AR_PHY_TX_PWRCTRL8, pwrctrl, clr);
		REG_RMW(ah, AR_PHY_TX_PWRCTRL10, pwrctrl, clr);
		REG_RMW(ah, AR_PHY_CH0_TX_PWRCTRL12, pwrctrl, clr);

		mask = BIT(0)|BIT(5)|BIT(15);
		pwrctrl = mask * bb_desired_scale;
		clr = mask * 0x1f;
		REG_RMW(ah, AR_PHY_TX_PWRCTRL9, pwrctrl, clr);

		mask = BIT(0)|BIT(5);
		pwrctrl = mask * bb_desired_scale;
		clr = mask * 0x1f;
		REG_RMW(ah, AR_PHY_CH0_TX_PWRCTRL11, pwrctrl, clr);
		REG_RMW(ah, AR_PHY_CH0_TX_PWRCTRL13, pwrctrl, clr);
		REG_RMW_BUFFER_FLUSH(ah);
	}
}

static u16 ath9k_hw_4k_get_spur_channel(struct ath_hw *ah, u16 i, bool is2GHz)
{
	return le16_to_cpu(ah->eeprom.map4k.modalHeader.spurChans[i].spurChan);
}

static u8 ath9k_hw_4k_get_eepmisc(struct ath_hw *ah)
{
	return ah->eeprom.map4k.baseEepHeader.eepMisc;
}

const struct eeprom_ops eep_4k_ops = {
	.check_eeprom		= ath9k_hw_4k_check_eeprom,
	.get_eeprom		= ath9k_hw_4k_get_eeprom,
	.fill_eeprom		= ath9k_hw_4k_fill_eeprom,
	.dump_eeprom		= ath9k_hw_4k_dump_eeprom,
	.get_eeprom_ver		= ath9k_hw_4k_get_eeprom_ver,
	.get_eeprom_rev		= ath9k_hw_4k_get_eeprom_rev,
	.set_board_values	= ath9k_hw_4k_set_board_values,
	.set_txpower		= ath9k_hw_4k_set_txpower,
	.get_spur_channel	= ath9k_hw_4k_get_spur_channel,
	.get_eepmisc		= ath9k_hw_4k_get_eepmisc
};
