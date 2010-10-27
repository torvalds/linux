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
	rt30xx.c

	Abstract:
	Specific funcitons and variables for RT30xx.

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
*/

#ifdef RT30xx

#ifndef RTMP_RF_RW_SUPPORT
#error "You Should Enable compile flag RTMP_RF_RW_SUPPORT for this chip"
#endif /* RTMP_RF_RW_SUPPORT // */

#include "../rt_config.h"

/* */
/* RF register initialization set */
/* */
struct rt_reg_pair RT30xx_RFRegTable[] = {
	{RF_R04, 0x40}
	,
	{RF_R05, 0x03}
	,
	{RF_R06, 0x02}
	,
	{RF_R07, 0x70}
	,
	{RF_R09, 0x0F}
	,
	{RF_R10, 0x41}
	,
	{RF_R11, 0x21}
	,
	{RF_R12, 0x7B}
	,
	{RF_R14, 0x90}
	,
	{RF_R15, 0x58}
	,
	{RF_R16, 0xB3}
	,
	{RF_R17, 0x92}
	,
	{RF_R18, 0x2C}
	,
	{RF_R19, 0x02}
	,
	{RF_R20, 0xBA}
	,
	{RF_R21, 0xDB}
	,
	{RF_R24, 0x16}
	,
	{RF_R25, 0x01}
	,
	{RF_R29, 0x1F}
	,
};

u8 NUM_RF_REG_PARMS = (sizeof(RT30xx_RFRegTable) / sizeof(struct rt_reg_pair));

/* Antenna divesity use GPIO3 and EESK pin for control */
/* Antenna and EEPROM access are both using EESK pin, */
/* Therefor we should avoid accessing EESK at the same time */
/* Then restore antenna after EEPROM access */
/* The original name of this function is AsicSetRxAnt(), now change to */
/*void AsicSetRxAnt( */
void RT30xxSetRxAnt(struct rt_rtmp_adapter *pAd, u8 Ant)
{
	u32 Value;
#ifdef RTMP_MAC_PCI
	u32 x;
#endif

	if ((pAd->EepromAccess) ||
	    (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) ||
	    (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) ||
	    (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF)) ||
	    (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))) {
		return;
	}
	/* the antenna selection is through firmware and MAC register(GPIO3) */
	if (Ant == 0) {
		/* Main antenna */
#ifdef RTMP_MAC_PCI
		RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
		x |= (EESK);
		RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);
#else
		AsicSendCommandToMcu(pAd, 0x73, 0xFF, 0x1, 0x0);
#endif /* RTMP_MAC_PCI // */

		RTMP_IO_READ32(pAd, GPIO_CTRL_CFG, &Value);
		Value &= ~(0x0808);
		RTMP_IO_WRITE32(pAd, GPIO_CTRL_CFG, Value);
		DBGPRINT_RAW(RT_DEBUG_TRACE,
			     ("AsicSetRxAnt, switch to main antenna\n"));
	} else {
		/* Aux antenna */
#ifdef RTMP_MAC_PCI
		RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
		x &= ~(EESK);
		RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);
#else
		AsicSendCommandToMcu(pAd, 0x73, 0xFF, 0x0, 0x0);
#endif /* RTMP_MAC_PCI // */
		RTMP_IO_READ32(pAd, GPIO_CTRL_CFG, &Value);
		Value &= ~(0x0808);
		Value |= 0x08;
		RTMP_IO_WRITE32(pAd, GPIO_CTRL_CFG, Value);
		DBGPRINT_RAW(RT_DEBUG_TRACE,
			     ("AsicSetRxAnt, switch to aux antenna\n"));
	}
}

/*
	========================================================================

	Routine Description:
		For RF filter calibration purpose

	Arguments:
		pAd                          Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	========================================================================
*/
void RTMPFilterCalibration(struct rt_rtmp_adapter *pAd)
{
	u8 R55x = 0, value, FilterTarget = 0x1E, BBPValue = 0;
	u32 loop = 0, count = 0, loopcnt = 0, ReTry = 0;
	u8 RF_R24_Value = 0;

	/* Give bbp filter initial value */
	pAd->Mlme.CaliBW20RfR24 = 0x1F;
	pAd->Mlme.CaliBW40RfR24 = 0x2F;	/*Bit[5] must be 1 for BW 40 */

	do {
		if (loop == 1) {	/*BandWidth = 40 MHz */
			/* Write 0x27 to RF_R24 to program filter */
			RF_R24_Value = 0x27;
			RT30xxWriteRFRegister(pAd, RF_R24, RF_R24_Value);
			if (IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
				FilterTarget = 0x15;
			else
				FilterTarget = 0x19;

			/* when calibrate BW40, BBP mask must set to BW40. */
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
			BBPValue &= (~0x18);
			BBPValue |= (0x10);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);

			/* set to BW40 */
			RT30xxReadRFRegister(pAd, RF_R31, &value);
			value |= 0x20;
			RT30xxWriteRFRegister(pAd, RF_R31, value);
		} else {	/*BandWidth = 20 MHz */
			/* Write 0x07 to RF_R24 to program filter */
			RF_R24_Value = 0x07;
			RT30xxWriteRFRegister(pAd, RF_R24, RF_R24_Value);
			if (IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
				FilterTarget = 0x13;
			else
				FilterTarget = 0x16;

			/* set to BW20 */
			RT30xxReadRFRegister(pAd, RF_R31, &value);
			value &= (~0x20);
			RT30xxWriteRFRegister(pAd, RF_R31, value);
		}

		/* Write 0x01 to RF_R22 to enable baseband loopback mode */
		RT30xxReadRFRegister(pAd, RF_R22, &value);
		value |= 0x01;
		RT30xxWriteRFRegister(pAd, RF_R22, value);

		/* Write 0x00 to BBP_R24 to set power & frequency of passband test tone */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R24, 0);

		do {
			/* Write 0x90 to BBP_R25 to transmit test tone */
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R25, 0x90);

			RTMPusecDelay(1000);
			/* Read BBP_R55[6:0] for received power, set R55x = BBP_R55[6:0] */
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R55, &value);
			R55x = value & 0xFF;

		} while ((ReTry++ < 100) && (R55x == 0));

		/* Write 0x06 to BBP_R24 to set power & frequency of stopband test tone */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R24, 0x06);

		while (TRUE) {
			/* Write 0x90 to BBP_R25 to transmit test tone */
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R25, 0x90);

			/*We need to wait for calibration */
			RTMPusecDelay(1000);
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R55, &value);
			value &= 0xFF;
			if ((R55x - value) < FilterTarget) {
				RF_R24_Value++;
			} else if ((R55x - value) == FilterTarget) {
				RF_R24_Value++;
				count++;
			} else {
				break;
			}

			/* prevent infinite loop cause driver hang. */
			if (loopcnt++ > 100) {
				DBGPRINT(RT_DEBUG_ERROR,
					 ("RTMPFilterCalibration - can't find a valid value, loopcnt=%d stop calibrating",
					  loopcnt));
				break;
			}
			/* Write RF_R24 to program filter */
			RT30xxWriteRFRegister(pAd, RF_R24, RF_R24_Value);
		}

		if (count > 0) {
			RF_R24_Value = RF_R24_Value - ((count) ? (1) : (0));
		}
		/* Store for future usage */
		if (loopcnt < 100) {
			if (loop++ == 0) {
				/*BandWidth = 20 MHz */
				pAd->Mlme.CaliBW20RfR24 = (u8)RF_R24_Value;
			} else {
				/*BandWidth = 40 MHz */
				pAd->Mlme.CaliBW40RfR24 = (u8)RF_R24_Value;
				break;
			}
		} else
			break;

		RT30xxWriteRFRegister(pAd, RF_R24, RF_R24_Value);

		/* reset count */
		count = 0;
	} while (TRUE);

	/* */
	/* Set back to initial state */
	/* */
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R24, 0);

	RT30xxReadRFRegister(pAd, RF_R22, &value);
	value &= ~(0x01);
	RT30xxWriteRFRegister(pAd, RF_R22, value);

	/* set BBP back to BW20 */
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
	BBPValue &= (~0x18);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPFilterCalibration - CaliBW20RfR24=0x%x, CaliBW40RfR24=0x%x\n",
		  pAd->Mlme.CaliBW20RfR24, pAd->Mlme.CaliBW40RfR24));
}

/* add by johnli, RF power sequence setup */
/*
	==========================================================================
	Description:

	Load RF normal operation-mode setup

	==========================================================================
 */
void RT30xxLoadRFNormalModeSetup(struct rt_rtmp_adapter *pAd)
{
	u8 RFValue;

	/* RX0_PD & TX0_PD, RF R1 register Bit 2 & Bit 3 to 0 and RF_BLOCK_en,RX1_PD & TX1_PD, Bit0, Bit 4 & Bit5 to 1 */
	RT30xxReadRFRegister(pAd, RF_R01, &RFValue);
	RFValue = (RFValue & (~0x0C)) | 0x31;
	RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

	/* TX_LO2_en, RF R15 register Bit 3 to 0 */
	RT30xxReadRFRegister(pAd, RF_R15, &RFValue);
	RFValue &= (~0x08);
	RT30xxWriteRFRegister(pAd, RF_R15, RFValue);

	/* move to NICInitRT30xxRFRegisters
	   // TX_LO1_en, RF R17 register Bit 3 to 0
	   RT30xxReadRFRegister(pAd, RF_R17, &RFValue);
	   RFValue &= (~0x08);
	   // to fix rx long range issue
	   if (((pAd->MACVersion & 0xffff) >= 0x0211) && (pAd->NicConfig2.field.ExternalLNAForG == 0))
	   {
	   RFValue |= 0x20;
	   }
	   // set RF_R17_bit[2:0] equal to EEPROM setting at 0x48h
	   if (pAd->TxMixerGain24G >= 2)
	   {
	   RFValue &= (~0x7);  // clean bit [2:0]
	   RFValue |= pAd->TxMixerGain24G;
	   }
	   RT30xxWriteRFRegister(pAd, RF_R17, RFValue);
	 */

	/* RX_LO1_en, RF R20 register Bit 3 to 0 */
	RT30xxReadRFRegister(pAd, RF_R20, &RFValue);
	RFValue &= (~0x08);
	RT30xxWriteRFRegister(pAd, RF_R20, RFValue);

	/* RX_LO2_en, RF R21 register Bit 3 to 0 */
	RT30xxReadRFRegister(pAd, RF_R21, &RFValue);
	RFValue &= (~0x08);
	RT30xxWriteRFRegister(pAd, RF_R21, RFValue);

	/* add by johnli, reset RF_R27 when interface down & up to fix throughput problem */
	/* LDORF_VC, RF R27 register Bit 2 to 0 */
	RT30xxReadRFRegister(pAd, RF_R27, &RFValue);
	/* TX to RX IQ glitch(RF_R27) has been fixed in RT3070(F). */
	/* Raising RF voltage is no longer needed for RT3070(F) */
	if (IS_RT3090(pAd)) {	/* RT309x and RT3071/72 */
		if ((pAd->MACVersion & 0xffff) < 0x0211)
			RFValue = (RFValue & (~0x77)) | 0x3;
		else
			RFValue = (RFValue & (~0x77));
		RT30xxWriteRFRegister(pAd, RF_R27, RFValue);
	}
	/* end johnli */
}

/*
	==========================================================================
	Description:

	Load RF sleep-mode setup

	==========================================================================
 */
void RT30xxLoadRFSleepModeSetup(struct rt_rtmp_adapter *pAd)
{
	u8 RFValue;
	u32 MACValue;

#ifdef RTMP_MAC_USB
	if (!IS_RT3572(pAd))
#endif /* RTMP_MAC_USB // */
	{
		/* RF_BLOCK_en. RF R1 register Bit 0 to 0 */
		RT30xxReadRFRegister(pAd, RF_R01, &RFValue);
		RFValue &= (~0x01);
		RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

		/* VCO_IC, RF R7 register Bit 4 & Bit 5 to 0 */
		RT30xxReadRFRegister(pAd, RF_R07, &RFValue);
		RFValue &= (~0x30);
		RT30xxWriteRFRegister(pAd, RF_R07, RFValue);

		/* Idoh, RF R9 register Bit 1, Bit 2 & Bit 3 to 0 */
		RT30xxReadRFRegister(pAd, RF_R09, &RFValue);
		RFValue &= (~0x0E);
		RT30xxWriteRFRegister(pAd, RF_R09, RFValue);

		/* RX_CTB_en, RF R21 register Bit 7 to 0 */
		RT30xxReadRFRegister(pAd, RF_R21, &RFValue);
		RFValue &= (~0x80);
		RT30xxWriteRFRegister(pAd, RF_R21, RFValue);
	}

	if (IS_RT3090(pAd) ||	/* IS_RT3090 including RT309x and RT3071/72 */
	    IS_RT3572(pAd) ||
	    (IS_RT3070(pAd) && ((pAd->MACVersion & 0xffff) < 0x0201))) {
#ifdef RTMP_MAC_USB
		if (!IS_RT3572(pAd))
#endif /* RTMP_MAC_USB // */
		{
			RT30xxReadRFRegister(pAd, RF_R27, &RFValue);
			RFValue |= 0x77;
			RT30xxWriteRFRegister(pAd, RF_R27, RFValue);
		}

		RTMP_IO_READ32(pAd, LDO_CFG0, &MACValue);
		MACValue |= 0x1D000000;
		RTMP_IO_WRITE32(pAd, LDO_CFG0, MACValue);
	}
}

/*
	==========================================================================
	Description:

	Reverse RF sleep-mode setup

	==========================================================================
 */
void RT30xxReverseRFSleepModeSetup(struct rt_rtmp_adapter *pAd)
{
	u8 RFValue;
	u32 MACValue;

#ifdef RTMP_MAC_USB
	if (!IS_RT3572(pAd))
#endif /* RTMP_MAC_USB // */
	{
		/* RF_BLOCK_en, RF R1 register Bit 0 to 1 */
		RT30xxReadRFRegister(pAd, RF_R01, &RFValue);
		RFValue |= 0x01;
		RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

		/* VCO_IC, RF R7 register Bit 4 & Bit 5 to 1 */
		RT30xxReadRFRegister(pAd, RF_R07, &RFValue);
		RFValue |= 0x30;
		RT30xxWriteRFRegister(pAd, RF_R07, RFValue);

		/* Idoh, RF R9 register Bit 1, Bit 2 & Bit 3 to 1 */
		RT30xxReadRFRegister(pAd, RF_R09, &RFValue);
		RFValue |= 0x0E;
		RT30xxWriteRFRegister(pAd, RF_R09, RFValue);

		/* RX_CTB_en, RF R21 register Bit 7 to 1 */
		RT30xxReadRFRegister(pAd, RF_R21, &RFValue);
		RFValue |= 0x80;
		RT30xxWriteRFRegister(pAd, RF_R21, RFValue);
	}

	if (IS_RT3090(pAd) ||	/* IS_RT3090 including RT309x and RT3071/72 */
	    IS_RT3572(pAd) ||
	    IS_RT3390(pAd) ||
	    (IS_RT3070(pAd) && ((pAd->MACVersion & 0xffff) < 0x0201))) {
#ifdef RTMP_MAC_USB
		if (!IS_RT3572(pAd))
#endif /* RTMP_MAC_USB // */
		{
			RT30xxReadRFRegister(pAd, RF_R27, &RFValue);
			if ((pAd->MACVersion & 0xffff) < 0x0211)
				RFValue = (RFValue & (~0x77)) | 0x3;
			else
				RFValue = (RFValue & (~0x77));
			RT30xxWriteRFRegister(pAd, RF_R27, RFValue);
		}
		/* RT3071 version E has fixed this issue */
		if ((pAd->NicConfig2.field.DACTestBit == 1)
		    && ((pAd->MACVersion & 0xffff) < 0x0211)) {
			/* patch tx EVM issue temporarily */
			RTMP_IO_READ32(pAd, LDO_CFG0, &MACValue);
			MACValue = ((MACValue & 0xE0FFFFFF) | 0x0D000000);
			RTMP_IO_WRITE32(pAd, LDO_CFG0, MACValue);
		} else {
			RTMP_IO_READ32(pAd, LDO_CFG0, &MACValue);
			MACValue = ((MACValue & 0xE0FFFFFF) | 0x01000000);
			RTMP_IO_WRITE32(pAd, LDO_CFG0, MACValue);
		}
	}

	if (IS_RT3572(pAd))
		RT30xxWriteRFRegister(pAd, RF_R08, 0x80);
}

/* end johnli */

void RT30xxHaltAction(struct rt_rtmp_adapter *pAd)
{
	u32 TxPinCfg = 0x00050F0F;

	/* */
	/* Turn off LNA_PE or TRSW_POL */
	/* */
	if (IS_RT3070(pAd) || IS_RT3071(pAd) || IS_RT3572(pAd)) {
		if ((IS_RT3071(pAd) || IS_RT3572(pAd))
#ifdef RTMP_EFUSE_SUPPORT
		    && (pAd->bUseEfuse)
#endif /* RTMP_EFUSE_SUPPORT // */
		    ) {
			TxPinCfg &= 0xFFFBF0F0;	/* bit18 off */
		} else {
			TxPinCfg &= 0xFFFFF0F0;
		}

		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);
	}
}

#endif /* RT30xx // */
