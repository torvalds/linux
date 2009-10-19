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

/**
 * DOC: Programming Atheros 802.11n analog front end radios
 *
 * AR5416 MAC based PCI devices and AR518 MAC based PCI-Express
 * devices have either an external AR2133 analog front end radio for single
 * band 2.4 GHz communication or an AR5133 analog front end radio for dual
 * band 2.4 GHz / 5 GHz communication.
 *
 * All devices after the AR5416 and AR5418 family starting with the AR9280
 * have their analog front radios, MAC/BB and host PCIe/USB interface embedded
 * into a single-chip and require less programming.
 *
 * The following single-chips exist with a respective embedded radio:
 *
 * AR9280 - 11n dual-band 2x2 MIMO for PCIe
 * AR9281 - 11n single-band 1x2 MIMO for PCIe
 * AR9285 - 11n single-band 1x1 for PCIe
 * AR9287 - 11n single-band 2x2 MIMO for PCIe
 *
 * AR9220 - 11n dual-band 2x2 MIMO for PCI
 * AR9223 - 11n single-band 2x2 MIMO for PCI
 *
 * AR9287 - 11n single-band 1x1 MIMO for USB
 */

#include "hw.h"

/**
 * ath9k_hw_write_regs - ??
 *
 * @ah: atheros hardware structure
 * @modesIndex:
 * @freqIndex:
 * @regWrites:
 *
 * Used for both the chipsets with an external AR2133/AR5133 radios and
 * single-chip devices.
 */
void
ath9k_hw_write_regs(struct ath_hw *ah, u32 modesIndex, u32 freqIndex,
		    int regWrites)
{
	REG_WRITE_ARRAY(&ah->iniBB_RfGain, freqIndex, regWrites);
}

/**
 * ath9k_hw_set_channel - tune to a channel on the external AR2133/AR5133 radios
 * @ah: atheros hardware stucture
 * @chan:
 *
 * For the external AR2133/AR5133 radios, takes the MHz channel value and set
 * the channel value. Assumes writes enabled to analog bus and bank6 register
 * cache in ah->analogBank6Data.
 */
bool
ath9k_hw_set_channel(struct ath_hw *ah, struct ath9k_channel *chan)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 channelSel = 0;
	u32 bModeSynth = 0;
	u32 aModeRefSel = 0;
	u32 reg32 = 0;
	u16 freq;
	struct chan_centers centers;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = centers.synth_center;

	if (freq < 4800) {
		u32 txctl;

		if (((freq - 2192) % 5) == 0) {
			channelSel = ((freq - 672) * 2 - 3040) / 10;
			bModeSynth = 0;
		} else if (((freq - 2224) % 5) == 0) {
			channelSel = ((freq - 704) * 2 - 3040) / 10;
			bModeSynth = 1;
		} else {
			ath_print(common, ATH_DBG_FATAL,
				  "Invalid channel %u MHz\n", freq);
			return false;
		}

		channelSel = (channelSel << 2) & 0xff;
		channelSel = ath9k_hw_reverse_bits(channelSel, 8);

		txctl = REG_READ(ah, AR_PHY_CCK_TX_CTRL);
		if (freq == 2484) {

			REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				  txctl | AR_PHY_CCK_TX_CTRL_JAPAN);
		} else {
			REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				  txctl & ~AR_PHY_CCK_TX_CTRL_JAPAN);
		}

	} else if ((freq % 20) == 0 && freq >= 5120) {
		channelSel =
		    ath9k_hw_reverse_bits(((freq - 4800) / 20 << 2), 8);
		aModeRefSel = ath9k_hw_reverse_bits(1, 2);
	} else if ((freq % 10) == 0) {
		channelSel =
		    ath9k_hw_reverse_bits(((freq - 4800) / 10 << 1), 8);
		if (AR_SREV_9100(ah) || AR_SREV_9160_10_OR_LATER(ah))
			aModeRefSel = ath9k_hw_reverse_bits(2, 2);
		else
			aModeRefSel = ath9k_hw_reverse_bits(1, 2);
	} else if ((freq % 5) == 0) {
		channelSel = ath9k_hw_reverse_bits((freq - 4800) / 5, 8);
		aModeRefSel = ath9k_hw_reverse_bits(1, 2);
	} else {
		ath_print(common, ATH_DBG_FATAL,
			  "Invalid channel %u MHz\n", freq);
		return false;
	}

	reg32 =
	    (channelSel << 8) | (aModeRefSel << 2) | (bModeSynth << 1) |
	    (1 << 5) | 0x1;

	REG_WRITE(ah, AR_PHY(0x37), reg32);

	ah->curchan = chan;
	ah->curchan_rad_index = -1;

	return true;
}

/**
 * ath9k_hw_ar9280_set_channel - set channel on single-chip device
 * @ah: atheros hardware structure
 * @chan:
 *
 * This is the function to change channel on single-chip devices, that is
 * all devices after ar9280.
 *
 * This function takes the channel value in MHz and sets
 * hardware channel value. Assumes writes have been enabled to analog bus.
 *
 * Actual Expression,
 *
 * For 2GHz channel,
 * Channel Frequency = (3/4) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^17)
 * (freq_ref = 40MHz)
 *
 * For 5GHz channel,
 * Channel Frequency = (3/2) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^10)
 * (freq_ref = 40MHz/(24>>amodeRefSel))
 */
void ath9k_hw_ar9280_set_channel(struct ath_hw *ah,
				 struct ath9k_channel *chan)
{
	u16 bMode, fracMode, aModeRefSel = 0;
	u32 freq, ndiv, channelSel = 0, channelFrac = 0, reg32 = 0;
	struct chan_centers centers;
	u32 refDivA = 24;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = centers.synth_center;

	reg32 = REG_READ(ah, AR_PHY_SYNTH_CONTROL);
	reg32 &= 0xc0000000;

	if (freq < 4800) { /* 2 GHz, fractional mode */
		u32 txctl;
		int regWrites = 0;

		bMode = 1;
		fracMode = 1;
		aModeRefSel = 0;
		channelSel = (freq * 0x10000) / 15;

		if (AR_SREV_9287_11_OR_LATER(ah)) {
			if (freq == 2484) {
				/* Enable channel spreading for channel 14 */
				REG_WRITE_ARRAY(&ah->iniCckfirJapan2484,
						1, regWrites);
			} else {
				REG_WRITE_ARRAY(&ah->iniCckfirNormal,
						1, regWrites);
			}
		} else {
			txctl = REG_READ(ah, AR_PHY_CCK_TX_CTRL);
			if (freq == 2484) {
				/* Enable channel spreading for channel 14 */
				REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
					  txctl | AR_PHY_CCK_TX_CTRL_JAPAN);
			} else {
				REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
					  txctl &~ AR_PHY_CCK_TX_CTRL_JAPAN);
			}
		}
	} else {
		bMode = 0;
		fracMode = 0;

		switch(ah->eep_ops->get_eeprom(ah, EEP_FRAC_N_5G)) {
		case 0:
			if ((freq % 20) == 0) {
				aModeRefSel = 3;
			} else if ((freq % 10) == 0) {
				aModeRefSel = 2;
			}
			if (aModeRefSel)
				break;
		case 1:
		default:
			aModeRefSel = 0;
			/*
			 * Enable 2G (fractional) mode for channels
			 * which are 5MHz spaced.
			 */
			fracMode = 1;
			refDivA = 1;
			channelSel = (freq * 0x8000) / 15;

			/* RefDivA setting */
			REG_RMW_FIELD(ah, AR_AN_SYNTH9,
				      AR_AN_SYNTH9_REFDIVA, refDivA);

		}

		if (!fracMode) {
			ndiv = (freq * (refDivA >> aModeRefSel)) / 60;
			channelSel = ndiv & 0x1ff;
			channelFrac = (ndiv & 0xfffffe00) * 2;
			channelSel = (channelSel << 17) | channelFrac;
		}
	}

	reg32 = reg32 |
	    (bMode << 29) |
	    (fracMode << 28) | (aModeRefSel << 26) | (channelSel);

	REG_WRITE(ah, AR_PHY_SYNTH_CONTROL, reg32);

	ah->curchan = chan;
	ah->curchan_rad_index = -1;
}

/**
 * ath9k_phy_modify_rx_buffer() - perform analog swizzling of parameters
 * @rfbuf:
 * @reg32:
 * @numBits:
 * @firstBit:
 * @column:
 *
 * Performs analog "swizzling" of parameters into their location.
 * Used on external AR2133/AR5133 radios.
 */
static void
ath9k_phy_modify_rx_buffer(u32 *rfBuf, u32 reg32,
			   u32 numBits, u32 firstBit,
			   u32 column)
{
	u32 tmp32, mask, arrayEntry, lastBit;
	int32_t bitPosition, bitsLeft;

	tmp32 = ath9k_hw_reverse_bits(reg32, numBits);
	arrayEntry = (firstBit - 1) / 8;
	bitPosition = (firstBit - 1) % 8;
	bitsLeft = numBits;
	while (bitsLeft > 0) {
		lastBit = (bitPosition + bitsLeft > 8) ?
		    8 : bitPosition + bitsLeft;
		mask = (((1 << lastBit) - 1) ^ ((1 << bitPosition) - 1)) <<
		    (column * 8);
		rfBuf[arrayEntry] &= ~mask;
		rfBuf[arrayEntry] |= ((tmp32 << bitPosition) <<
				      (column * 8)) & mask;
		bitsLeft -= 8 - bitPosition;
		tmp32 = tmp32 >> (8 - bitPosition);
		bitPosition = 0;
		arrayEntry++;
	}
}

/* *
 * ath9k_hw_set_rf_regs - programs rf registers based on EEPROM
 * @ah: atheros hardware structure
 * @chan:
 * @modesIndex:
 *
 * Used for the external AR2133/AR5133 radios.
 *
 * Reads the EEPROM header info from the device structure and programs
 * all rf registers. This routine requires access to the analog
 * rf device. This is not required for single-chip devices.
 */
bool
ath9k_hw_set_rf_regs(struct ath_hw *ah, struct ath9k_channel *chan,
		     u16 modesIndex)
{
	u32 eepMinorRev;
	u32 ob5GHz = 0, db5GHz = 0;
	u32 ob2GHz = 0, db2GHz = 0;
	int regWrites = 0;

	/*
	 * Software does not need to program bank data
	 * for single chip devices, that is AR9280 or anything
	 * after that.
	 */
	if (AR_SREV_9280_10_OR_LATER(ah))
		return true;

	/* Setup rf parameters */
	eepMinorRev = ah->eep_ops->get_eeprom(ah, EEP_MINOR_REV);

	/* Setup Bank 0 Write */
	RF_BANK_SETUP(ah->analogBank0Data, &ah->iniBank0, 1);

	/* Setup Bank 1 Write */
	RF_BANK_SETUP(ah->analogBank1Data, &ah->iniBank1, 1);

	/* Setup Bank 2 Write */
	RF_BANK_SETUP(ah->analogBank2Data, &ah->iniBank2, 1);

	/* Setup Bank 6 Write */
	RF_BANK_SETUP(ah->analogBank3Data, &ah->iniBank3,
		      modesIndex);
	{
		int i;
		for (i = 0; i < ah->iniBank6TPC.ia_rows; i++) {
			ah->analogBank6Data[i] =
			    INI_RA(&ah->iniBank6TPC, i, modesIndex);
		}
	}

	/* Only the 5 or 2 GHz OB/DB need to be set for a mode */
	if (eepMinorRev >= 2) {
		if (IS_CHAN_2GHZ(chan)) {
			ob2GHz = ah->eep_ops->get_eeprom(ah, EEP_OB_2);
			db2GHz = ah->eep_ops->get_eeprom(ah, EEP_DB_2);
			ath9k_phy_modify_rx_buffer(ah->analogBank6Data,
						   ob2GHz, 3, 197, 0);
			ath9k_phy_modify_rx_buffer(ah->analogBank6Data,
						   db2GHz, 3, 194, 0);
		} else {
			ob5GHz = ah->eep_ops->get_eeprom(ah, EEP_OB_5);
			db5GHz = ah->eep_ops->get_eeprom(ah, EEP_DB_5);
			ath9k_phy_modify_rx_buffer(ah->analogBank6Data,
						   ob5GHz, 3, 203, 0);
			ath9k_phy_modify_rx_buffer(ah->analogBank6Data,
						   db5GHz, 3, 200, 0);
		}
	}

	/* Setup Bank 7 Setup */
	RF_BANK_SETUP(ah->analogBank7Data, &ah->iniBank7, 1);

	/* Write Analog registers */
	REG_WRITE_RF_ARRAY(&ah->iniBank0, ah->analogBank0Data,
			   regWrites);
	REG_WRITE_RF_ARRAY(&ah->iniBank1, ah->analogBank1Data,
			   regWrites);
	REG_WRITE_RF_ARRAY(&ah->iniBank2, ah->analogBank2Data,
			   regWrites);
	REG_WRITE_RF_ARRAY(&ah->iniBank3, ah->analogBank3Data,
			   regWrites);
	REG_WRITE_RF_ARRAY(&ah->iniBank6TPC, ah->analogBank6Data,
			   regWrites);
	REG_WRITE_RF_ARRAY(&ah->iniBank7, ah->analogBank7Data,
			   regWrites);

	return true;
}

/**
 * ath9k_hw_rf_free - Free memory for analog bank scratch buffers
 * @ah: atheros hardware struture
 * For the external AR2133/AR5133 radios.
 */
void
ath9k_hw_rf_free(struct ath_hw *ah)
{
#define ATH_FREE_BANK(bank) do { \
		kfree(bank); \
		bank = NULL; \
	} while (0);

	ATH_FREE_BANK(ah->analogBank0Data);
	ATH_FREE_BANK(ah->analogBank1Data);
	ATH_FREE_BANK(ah->analogBank2Data);
	ATH_FREE_BANK(ah->analogBank3Data);
	ATH_FREE_BANK(ah->analogBank6Data);
	ATH_FREE_BANK(ah->analogBank6TPCData);
	ATH_FREE_BANK(ah->analogBank7Data);
	ATH_FREE_BANK(ah->addac5416_21);
	ATH_FREE_BANK(ah->bank6Temp);
#undef ATH_FREE_BANK
}

/**
 * ath9k_hw_rf_alloc_ext_banks - allocates banks for external radio programming
 * @ah: atheros hardware structure
 *
 * Only required for older devices with external AR2133/AR5133 radios.
 */
int ath9k_hw_rf_alloc_ext_banks(struct ath_hw *ah)
{
#define ATH_ALLOC_BANK(bank, size) do { \
		bank = kzalloc((sizeof(u32) * size), GFP_KERNEL); \
		if (!bank) { \
			ath_print(common, ATH_DBG_FATAL, \
				  "Cannot allocate RF banks\n"); \
			return -ENOMEM; \
		} \
	} while (0);

	struct ath_common *common = ath9k_hw_common(ah);

	BUG_ON(AR_SREV_9280_10_OR_LATER(ah));

	ATH_ALLOC_BANK(ah->analogBank0Data, ah->iniBank0.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank1Data, ah->iniBank1.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank2Data, ah->iniBank2.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank3Data, ah->iniBank3.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank6Data, ah->iniBank6.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank6TPCData, ah->iniBank6TPC.ia_rows);
	ATH_ALLOC_BANK(ah->analogBank7Data, ah->iniBank7.ia_rows);
	ATH_ALLOC_BANK(ah->addac5416_21,
		       ah->iniAddac.ia_rows * ah->iniAddac.ia_columns);
	ATH_ALLOC_BANK(ah->bank6Temp, ah->iniBank6.ia_rows);

	return 0;
#undef ATH_ALLOC_BANK
}

/**
 * ath9k_hw_decrease_chain_power()
 *
 * @ah: atheros hardware structure
 * @chan:
 *
 * Only used on the AR5416 and AR5418 with the external AR2133/AR5133 radios.
 *
 * Sets a chain internal RF path to the lowest output power. Any
 * further writes to bank6 after this setting will override these
 * changes. Thus this function must be the last function in the
 * sequence to modify bank 6.
 *
 * This function must be called after ar5416SetRfRegs() which is
 * called from ath9k_hw_process_ini() due to swizzling of bank 6.
 * Depends on ah->analogBank6Data being initialized by
 * ath9k_hw_set_rf_regs()
 *
 * Additional additive reduction in power -
 * change chain's switch table so chain's tx state is actually the rx
 * state value. May produce different results in 2GHz/5GHz as well as
 * board to board but in general should be a reduction.
 *
 * Activated by #ifdef ALTER_SWITCH.  Not tried yet.  If so, must be
 * called after ah->eep_ops->set_board_values() due to RMW of
 * PHY_SWITCH_CHAIN_0.
 */
void
ath9k_hw_decrease_chain_power(struct ath_hw *ah, struct ath9k_channel *chan)
{
	int i, regWrites = 0;
	u32 bank6SelMask;
	u32 *bank6Temp = ah->bank6Temp;

	switch (ah->config.diversity_control) {
	case ATH9K_ANT_FIXED_A:
		bank6SelMask =
		    (ah->config.antenna_switch_swap & ANTSWAP_AB) ?
			REDUCE_CHAIN_0 : /* swapped, reduce chain 0 */
			REDUCE_CHAIN_1; /* normal, select chain 1/2 to reduce */
		break;
	case ATH9K_ANT_FIXED_B:
		bank6SelMask =
		    (ah->config.antenna_switch_swap & ANTSWAP_AB) ?
			REDUCE_CHAIN_1 : /* swapped, reduce chain 1/2 */
			REDUCE_CHAIN_0; /* normal, select chain 0 to reduce */
		break;
	case ATH9K_ANT_VARIABLE:
		return; /* do not change anything */
		break;
	default:
		return; /* do not change anything */
		break;
	}

	for (i = 0; i < ah->iniBank6.ia_rows; i++)
		bank6Temp[i] = ah->analogBank6Data[i];

	/* Write Bank 5 to switch Bank 6 write to selected chain only */
	REG_WRITE(ah, AR_PHY_BASE + 0xD8, bank6SelMask);

	/*
	 * Modify Bank6 selected chain to use lowest amplification.
	 * Modifies the parameters to a value of 1.
	 * Depends on existing bank 6 values to be cached in
	 * ah->analogBank6Data
	 */
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 189, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 190, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 191, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 192, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 193, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 222, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 245, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 246, 0);
	ath9k_phy_modify_rx_buffer(bank6Temp, 1, 1, 247, 0);

	REG_WRITE_RF_ARRAY(&ah->iniBank6, bank6Temp, regWrites);

	REG_WRITE(ah, AR_PHY_BASE + 0xD8, 0x00000053);
#ifdef ALTER_SWITCH
	REG_WRITE(ah, PHY_SWITCH_CHAIN_0,
		  (REG_READ(ah, PHY_SWITCH_CHAIN_0) & ~0x38)
		  | ((REG_READ(ah, PHY_SWITCH_CHAIN_0) >> 3) & 0x38));
#endif
}
