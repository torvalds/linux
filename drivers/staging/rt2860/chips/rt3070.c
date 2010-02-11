/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

	Module Name:
	rt3070.c

	Abstract:
	Specific funcitons and variables for RT3070

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
*/

#ifdef RT3070

#include "../rt_config.h"

#ifndef RTMP_RF_RW_SUPPORT
#error "You Should Enable compile flag RTMP_RF_RW_SUPPORT for this chip"
#endif /* RTMP_RF_RW_SUPPORT // */

void NICInitRT3070RFRegisters(struct rt_rtmp_adapter *pAd)
{
	int i;
	u8 RFValue;

	/* Driver must read EEPROM to get RfIcType before initial RF registers */
	/* Initialize RF register to default value */
	if (IS_RT3070(pAd) || IS_RT3071(pAd)) {
		/* Init RF calibration */
		/* Driver should toggle RF R30 bit7 before init RF registers */
		u32 RfReg = 0;
		u32 data;

		RT30xxReadRFRegister(pAd, RF_R30, (u8 *)& RfReg);
		RfReg |= 0x80;
		RT30xxWriteRFRegister(pAd, RF_R30, (u8)RfReg);
		RTMPusecDelay(1000);
		RfReg &= 0x7F;
		RT30xxWriteRFRegister(pAd, RF_R30, (u8)RfReg);

		/* Initialize RF register to default value */
		for (i = 0; i < NUM_RF_REG_PARMS; i++) {
			RT30xxWriteRFRegister(pAd,
					      RT30xx_RFRegTable[i].Register,
					      RT30xx_RFRegTable[i].Value);
		}

		/* add by johnli */
		if (IS_RT3070(pAd)) {
			/* */
			/* The DAC issue(LDO_CFG0) has been fixed in RT3070(F). */
			/* The voltage raising patch is no longer needed for RT3070(F) */
			/* */
			if ((pAd->MACVersion & 0xffff) < 0x0201) {
				/*  Update MAC 0x05D4 from 01xxxxxx to 0Dxxxxxx (voltage 1.2V to 1.35V) for RT3070 to improve yield rate */
				RTUSBReadMACRegister(pAd, LDO_CFG0, &data);
				data = ((data & 0xF0FFFFFF) | 0x0D000000);
				RTUSBWriteMACRegister(pAd, LDO_CFG0, data);
			}
		} else if (IS_RT3071(pAd)) {
			/* Driver should set RF R6 bit6 on before init RF registers */
			RT30xxReadRFRegister(pAd, RF_R06, (u8 *)& RfReg);
			RfReg |= 0x40;
			RT30xxWriteRFRegister(pAd, RF_R06, (u8)RfReg);

			/* init R31 */
			RT30xxWriteRFRegister(pAd, RF_R31, 0x14);

			/* RT3071 version E has fixed this issue */
			if ((pAd->NicConfig2.field.DACTestBit == 1)
			    && ((pAd->MACVersion & 0xffff) < 0x0211)) {
				/* patch tx EVM issue temporarily */
				RTUSBReadMACRegister(pAd, LDO_CFG0, &data);
				data = ((data & 0xE0FFFFFF) | 0x0D000000);
				RTUSBWriteMACRegister(pAd, LDO_CFG0, data);
			} else {
				RTMP_IO_READ32(pAd, LDO_CFG0, &data);
				data = ((data & 0xE0FFFFFF) | 0x01000000);
				RTMP_IO_WRITE32(pAd, LDO_CFG0, data);
			}

			/* patch LNA_PE_G1 failed issue */
			RTUSBReadMACRegister(pAd, GPIO_SWITCH, &data);
			data &= ~(0x20);
			RTUSBWriteMACRegister(pAd, GPIO_SWITCH, data);
		}
		/*For RF filter Calibration */
		RTMPFilterCalibration(pAd);

		/* Initialize RF R27 register, set RF R27 must be behind RTMPFilterCalibration() */
		/* */
		/* TX to RX IQ glitch(RF_R27) has been fixed in RT3070(F). */
		/* Raising RF voltage is no longer needed for RT3070(F) */
		/* */
		if ((IS_RT3070(pAd)) && ((pAd->MACVersion & 0xffff) < 0x0201)) {
			RT30xxWriteRFRegister(pAd, RF_R27, 0x3);
		} else if ((IS_RT3071(pAd))
			   && ((pAd->MACVersion & 0xffff) < 0x0211)) {
			RT30xxWriteRFRegister(pAd, RF_R27, 0x3);
		}
		/* set led open drain enable */
		RTUSBReadMACRegister(pAd, OPT_14, &data);
		data |= 0x01;
		RTUSBWriteMACRegister(pAd, OPT_14, data);

		/* move from RT30xxLoadRFNormalModeSetup because it's needed for both RT3070 and RT3071 */
		/* TX_LO1_en, RF R17 register Bit 3 to 0 */
		RT30xxReadRFRegister(pAd, RF_R17, &RFValue);
		RFValue &= (~0x08);
		/* to fix rx long range issue */
		if (pAd->NicConfig2.field.ExternalLNAForG == 0) {
			if ((IS_RT3071(pAd)
			     && ((pAd->MACVersion & 0xffff) >= 0x0211))
			    || IS_RT3070(pAd)) {
				RFValue |= 0x20;
			}
		}
		/* set RF_R17_bit[2:0] equal to EEPROM setting at 0x48h */
		if (pAd->TxMixerGain24G >= 1) {
			RFValue &= (~0x7);	/* clean bit [2:0] */
			RFValue |= pAd->TxMixerGain24G;
		}
		RT30xxWriteRFRegister(pAd, RF_R17, RFValue);

		if (IS_RT3071(pAd)) {
			/* add by johnli, RF power sequence setup, load RF normal operation-mode setup */
			RT30xxLoadRFNormalModeSetup(pAd);
		} else if (IS_RT3070(pAd)) {
			/* add by johnli, reset RF_R27 when interface down & up to fix throughput problem */
			/* LDORF_VC, RF R27 register Bit 2 to 0 */
			RT30xxReadRFRegister(pAd, RF_R27, &RFValue);
			/* TX to RX IQ glitch(RF_R27) has been fixed in RT3070(F). */
			/* Raising RF voltage is no longer needed for RT3070(F) */
			if ((pAd->MACVersion & 0xffff) < 0x0201)
				RFValue = (RFValue & (~0x77)) | 0x3;
			else
				RFValue = (RFValue & (~0x77));
			RT30xxWriteRFRegister(pAd, RF_R27, RFValue);
			/* end johnli */
		}
	}

}
#endif /* RT3070 // */
