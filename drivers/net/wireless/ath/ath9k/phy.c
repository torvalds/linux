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

#include "ath9k.h"

void
ath9k_hw_write_regs(struct ath_hw *ah, u32 modesIndex, u32 freqIndex,
		    int regWrites)
{
	REG_WRITE_ARRAY(&ah->iniBB_RfGain, freqIndex, regWrites);
}

bool
ath9k_hw_set_channel(struct ath_hw *ah, struct ath9k_channel *chan)
{
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
			DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
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
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
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

	if (freq < 4800) {
		u32 txctl;

		bMode = 1;
		fracMode = 1;
		aModeRefSel = 0;
		channelSel = (freq * 0x10000) / 15;

		txctl = REG_READ(ah, AR_PHY_CCK_TX_CTRL);
		if (freq == 2484) {

			REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				  txctl | AR_PHY_CCK_TX_CTRL_JAPAN);
		} else {
			REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				  txctl & ~AR_PHY_CCK_TX_CTRL_JAPAN);
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
			fracMode = 1;
			refDivA = 1;
			channelSel = (freq * 0x8000) / 15;

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

bool
ath9k_hw_set_rf_regs(struct ath_hw *ah, struct ath9k_channel *chan,
		     u16 modesIndex)
{
	u32 eepMinorRev;
	u32 ob5GHz = 0, db5GHz = 0;
	u32 ob2GHz = 0, db2GHz = 0;
	int regWrites = 0;

	if (AR_SREV_9280_10_OR_LATER(ah))
		return true;

	eepMinorRev = ah->eep_ops->get_eeprom(ah, EEP_MINOR_REV);

	RF_BANK_SETUP(ah->analogBank0Data, &ah->iniBank0, 1);

	RF_BANK_SETUP(ah->analogBank1Data, &ah->iniBank1, 1);

	RF_BANK_SETUP(ah->analogBank2Data, &ah->iniBank2, 1);

	RF_BANK_SETUP(ah->analogBank3Data, &ah->iniBank3,
		      modesIndex);
	{
		int i;
		for (i = 0; i < ah->iniBank6TPC.ia_rows; i++) {
			ah->analogBank6Data[i] =
			    INI_RA(&ah->iniBank6TPC, i, modesIndex);
		}
	}

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

	RF_BANK_SETUP(ah->analogBank7Data, &ah->iniBank7, 1);

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

void
ath9k_hw_rfdetach(struct ath_hw *ah)
{
	if (ah->analogBank0Data != NULL) {
		kfree(ah->analogBank0Data);
		ah->analogBank0Data = NULL;
	}
	if (ah->analogBank1Data != NULL) {
		kfree(ah->analogBank1Data);
		ah->analogBank1Data = NULL;
	}
	if (ah->analogBank2Data != NULL) {
		kfree(ah->analogBank2Data);
		ah->analogBank2Data = NULL;
	}
	if (ah->analogBank3Data != NULL) {
		kfree(ah->analogBank3Data);
		ah->analogBank3Data = NULL;
	}
	if (ah->analogBank6Data != NULL) {
		kfree(ah->analogBank6Data);
		ah->analogBank6Data = NULL;
	}
	if (ah->analogBank6TPCData != NULL) {
		kfree(ah->analogBank6TPCData);
		ah->analogBank6TPCData = NULL;
	}
	if (ah->analogBank7Data != NULL) {
		kfree(ah->analogBank7Data);
		ah->analogBank7Data = NULL;
	}
	if (ah->addac5416_21 != NULL) {
		kfree(ah->addac5416_21);
		ah->addac5416_21 = NULL;
	}
	if (ah->bank6Temp != NULL) {
		kfree(ah->bank6Temp);
		ah->bank6Temp = NULL;
	}
}

bool ath9k_hw_init_rf(struct ath_hw *ah, int *status)
{
	if (!AR_SREV_9280_10_OR_LATER(ah)) {
		ah->analogBank0Data =
		    kzalloc((sizeof(u32) *
			     ah->iniBank0.ia_rows), GFP_KERNEL);
		ah->analogBank1Data =
		    kzalloc((sizeof(u32) *
			     ah->iniBank1.ia_rows), GFP_KERNEL);
		ah->analogBank2Data =
		    kzalloc((sizeof(u32) *
			     ah->iniBank2.ia_rows), GFP_KERNEL);
		ah->analogBank3Data =
		    kzalloc((sizeof(u32) *
			     ah->iniBank3.ia_rows), GFP_KERNEL);
		ah->analogBank6Data =
		    kzalloc((sizeof(u32) *
			     ah->iniBank6.ia_rows), GFP_KERNEL);
		ah->analogBank6TPCData =
		    kzalloc((sizeof(u32) *
			     ah->iniBank6TPC.ia_rows), GFP_KERNEL);
		ah->analogBank7Data =
		    kzalloc((sizeof(u32) *
			     ah->iniBank7.ia_rows), GFP_KERNEL);

		if (ah->analogBank0Data == NULL
		    || ah->analogBank1Data == NULL
		    || ah->analogBank2Data == NULL
		    || ah->analogBank3Data == NULL
		    || ah->analogBank6Data == NULL
		    || ah->analogBank6TPCData == NULL
		    || ah->analogBank7Data == NULL) {
			DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
				"Cannot allocate RF banks\n");
			*status = -ENOMEM;
			return false;
		}

		ah->addac5416_21 =
		    kzalloc((sizeof(u32) *
			     ah->iniAddac.ia_rows *
			     ah->iniAddac.ia_columns), GFP_KERNEL);
		if (ah->addac5416_21 == NULL) {
			DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
				"Cannot allocate addac5416_21\n");
			*status = -ENOMEM;
			return false;
		}

		ah->bank6Temp =
		    kzalloc((sizeof(u32) *
			     ah->iniBank6.ia_rows), GFP_KERNEL);
		if (ah->bank6Temp == NULL) {
			DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
				"Cannot allocate bank6Temp\n");
			*status = -ENOMEM;
			return false;
		}
	}

	return true;
}

void
ath9k_hw_decrease_chain_power(struct ath_hw *ah, struct ath9k_channel *chan)
{
	int i, regWrites = 0;
	u32 bank6SelMask;
	u32 *bank6Temp = ah->bank6Temp;

	switch (ah->diversity_control) {
	case ATH9K_ANT_FIXED_A:
		bank6SelMask =
		    (ah->
		     antenna_switch_swap & ANTSWAP_AB) ? REDUCE_CHAIN_0 :
		    REDUCE_CHAIN_1;
		break;
	case ATH9K_ANT_FIXED_B:
		bank6SelMask =
		    (ah->
		     antenna_switch_swap & ANTSWAP_AB) ? REDUCE_CHAIN_1 :
		    REDUCE_CHAIN_0;
		break;
	case ATH9K_ANT_VARIABLE:
		return;
		break;
	default:
		return;
		break;
	}

	for (i = 0; i < ah->iniBank6.ia_rows; i++)
		bank6Temp[i] = ah->analogBank6Data[i];

	REG_WRITE(ah, AR_PHY_BASE + 0xD8, bank6SelMask);

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
