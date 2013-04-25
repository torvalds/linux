/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#ifdef RT30xx

#ifndef RTMP_RF_RW_SUPPORT
#error "You Should Enable compile flag RTMP_RF_RW_SUPPORT for this chip"
#endif /* RTMP_RF_RW_SUPPORT */

#include "rt_config.h"

/*
  RF register initialization set
*/
REG_PAIR   RT3020_RFRegTable[] = {
        {RF_R04,          0x40},
        {RF_R05,          0x03},
        {RF_R06,          0x02},
        {RF_R07,          0x60},      
        {RF_R09,          0x0F},
        {RF_R10,          0x41},
        {RF_R11,          0x21},
        {RF_R12,          0x7B},
        {RF_R14,          0x90},
        {RF_R15,          0x58},
        {RF_R16,          0xB3},
        {RF_R17,          0x92},
        {RF_R18,          0x2C},
        {RF_R19,          0x02},
        {RF_R20,          0xBA},
        {RF_R21,          0xDB},
        {RF_R24,          0x16},      
        {RF_R25,          0x03},
        {RF_R29,          0x1F},
};

UCHAR NUM_RF_3020_REG_PARMS = (sizeof(RT3020_RFRegTable) / sizeof(REG_PAIR));




/*
========================================================================
Routine Description:
	Initialize RT35xx.

Arguments:
	pAd					- WLAN control block pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RT30xx_Init(
	IN PRTMP_ADAPTER		pAd)
{
	RTMP_CHIP_OP *pChipOps = &pAd->chipOps;
	RTMP_CHIP_CAP *pChipCap = &pAd->chipCap;


	/* init capability */
	/* 
		WARNING: 
			Currently following table are shared by all RT30xx based IC, change it carefully when you add a new IC here.
	*/
	pChipCap->pRFRegTable = RT3020_RFRegTable;
	pChipCap->MaxNumOfBbpId = 185;

	/* init operator */
	if((IS_RT3070(pAd) || IS_RT3071(pAd)) || IS_RT3090(pAd))
	{
#ifdef RT3070
		if (pAd->infType == RTMP_DEV_INF_USB)
		{
			pChipOps->AsicRfInit = NICInitRT3070RFRegisters;
		}
#endif /* RT3070 */

		pChipOps->AsicHaltAction = RT30xxHaltAction;
		pChipOps->AsicRfTurnOff = RT30xxLoadRFSleepModeSetup;
		pChipOps->AsicReverseRfFromSleepMode = RT30xxReverseRFSleepModeSetup;
		pChipOps->ChipSwitchChannel = RT30xx_ChipSwitchChannel;
		pChipOps->ChipBBPAdjust = RT30xx_ChipBBPAdjust;
		pChipOps->RTMPSetAGCInitValue = RT30xx_RTMPSetAGCInitValue;
		pChipOps->SetRxAnt = RT30xxSetRxAnt; 

		pChipOps->ChipResumeMsduTransmission = NULL;
		pChipOps->VdrTuning1 = NULL;
		pChipOps->RxSensitivityTuning = NULL;
#ifdef RTMP_FREQ_CALIBRATION_SUPPORT
		pChipOps->AsicFreqCalInit = NULL;
		pChipOps->AsicFreqCalStop = NULL;
		pChipOps->AsicFreqCal = NULL;
		pChipOps->AsicFreqOffsetGet = NULL;
#endif /* RTMP_FREQ_CALIBRATION_SUPPORT */

}
}


/*
 Antenna divesity use GPIO3 and EESK pin for control
 Antenna and EEPROM access are both using EESK pin,
 Therefor we should avoid accessing EESK at the same time
 Then restore antenna after EEPROM access
 The original name of this function is AsicSetRxAnt(), now change to 
*/
VOID RT30xxSetRxAnt(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			Ant)
{
	UINT32	Value;

	if (/*(!pAd->NicConfig2.field.AntDiversity) ||*/
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS))	||
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))	||
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF)) ||
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
	{
		return;
	}

	/* the antenna selection is through firmware and MAC register(GPIO3) */
	if (IS_RT2070(pAd) || (IS_RT3070(pAd) && pAd->RfIcType == RFIC_3020) ||
			(IS_RT3090(pAd) && pAd->RfIcType == RFIC_3020))
	{
	if (Ant == 0)
	{
		/*
			Main antenna
			E2PROM_CSR only in PCI bus Reg., USB Bus need MCU commad to control the EESK pin.
		*/
#ifdef RTMP_MAC_USB
		AsicSendCommandToMcu(pAd, 0x73, 0xFF, 0x1, 0x0);
#endif /* RTMP_MAC_USB */

		RTMP_IO_READ32(pAd, GPIO_CTRL_CFG, &Value);
		Value &= ~(0x0808);
		RTMP_IO_WRITE32(pAd, GPIO_CTRL_CFG, Value);
			DBGPRINT(RT_DEBUG_TRACE, ("AsicSetRxAnt, switch to main antenna\n"));
	}
	else
	{
		/*
			Aux antenna
		 	E2PROM_CSR only in PCI bus Reg., USB Bus need MCU commad to control the EESK pin.
		*/
#ifdef RTMP_MAC_USB
		AsicSendCommandToMcu(pAd, 0x73, 0xFF, 0x0, 0x0);
#endif /* RTMP_MAC_USB */
		RTMP_IO_READ32(pAd, GPIO_CTRL_CFG, &Value);
		Value &= ~(0x0808);
		Value |= 0x08;
		RTMP_IO_WRITE32(pAd, GPIO_CTRL_CFG, Value);
			DBGPRINT(RT_DEBUG_TRACE, ("AsicSetRxAnt, switch to aux antenna\n"));
		}
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
VOID RTMPFilterCalibration(
	IN PRTMP_ADAPTER pAd)
{
	UCHAR	R55x = 0, value, FilterTarget = 0x1E, BBPValue=0;
	UINT	loop = 0, count = 0, loopcnt = 0, ReTry = 0;
	UCHAR	RF_R24_Value = 0;

	/* Give bbp filter initial value */
	pAd->Mlme.CaliBW20RfR24 = 0x1F;
	pAd->Mlme.CaliBW40RfR24 = 0x2F; /* Bit[5] must be 1 for BW 40 */


	do 
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))				
			return;
		if (loop == 1)	/*BandWidth = 40 MHz*/
		{
			/* Write 0x27 to RF_R24 to program filter*/
			RT30xxReadRFRegister(pAd, RF_R24, (PUCHAR)(&RF_R24_Value));
			RF_R24_Value = (RF_R24_Value & 0xC0) | 0x27; /* <bit 5>:tx_h20M<bit 5> and <bit 4:0>:tx_agc_fc<bit 4:0>*/
			RT30xxWriteRFRegister(pAd, RF_R24, RF_R24_Value);
			if (IS_RT3071(pAd) || IS_RT3572(pAd))
				FilterTarget = 0x15;
			else
				FilterTarget = 0x19;

			/* when calibrate BW40, BBP mask must set to BW40.*/
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
			BBPValue&= (~0x18);
			BBPValue|= (0x10);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);

			/* set to BW40*/
			RT30xxReadRFRegister(pAd, RF_R31, &value);
			value |= 0x20;
			RT30xxWriteRFRegister(pAd, RF_R31, value);
		}
		else	/*BandWidth = 20 MHz*/
		{
			/* Write 0x07 to RF_R24 to program filter*/
			RT30xxReadRFRegister(pAd, RF_R24, (PUCHAR)(&RF_R24_Value));
			RF_R24_Value = (RF_R24_Value & 0xC0) | 0x07; /* <bit 5>:tx_h20M<bit 5> and <bit 4:0>:tx_agc_fc<bit 4:0>*/
			RT30xxWriteRFRegister(pAd, RF_R24, RF_R24_Value);
			if (IS_RT3071(pAd) || IS_RT3572(pAd))
				FilterTarget = 0x13;
			else
				FilterTarget = 0x16;

			/*set to BW20*/
			RT30xxReadRFRegister(pAd, RF_R31, &value);
			value &= (~0x20);
			RT30xxWriteRFRegister(pAd, RF_R31, value);
		}

		/* Write 0x01 to RF_R22 to enable baseband loopback mode*/
		RT30xxReadRFRegister(pAd, RF_R22, &value);
		value |= 0x01;
		RT30xxWriteRFRegister(pAd, RF_R22, value);

		/* Write 0x00 to BBP_R24 to set power & frequency of passband test tone*/
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R24, 0);

		do
		{
			if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))				
				return;
			/* Write 0x90 to BBP_R25 to transmit test tone*/
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R25, 0x90);

			RTMPusecDelay(1000);
			/* Read BBP_R55[6:0] for received power, set R55x = BBP_R55[6:0]*/
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R55, &value);
			R55x = value & 0xFF;

		} while ((ReTry++ < 100) && (R55x == 0));
		
		/* Write 0x06 to BBP_R24 to set power & frequency of stopband test tone*/
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R24, 0x06);

		while(TRUE)
		{
			if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))				
				return;
			
			/* Write 0x90 to BBP_R25 to transmit test tone*/
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R25, 0x90);

			/*We need to wait for calibration*/
			RTMPusecDelay(1000);
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R55, &value);
			value &= 0xFF;
			if ((R55x - value) < FilterTarget)
			{
				RF_R24_Value ++;
			}
			else if ((R55x - value) == FilterTarget)
			{
				RF_R24_Value ++;
				count ++;
			}
			else
			{
				break;
			}

			/* prevent infinite loop cause driver hang.*/
			if (loopcnt++ > 100)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("RTMPFilterCalibration - can't find a valid value, loopcnt=%d stop calibrating", loopcnt));
				break;
			}

			/*Write RF_R24 to program filter*/
			RT30xxWriteRFRegister(pAd, RF_R24, RF_R24_Value);
		}

		if (count > 0)
		{
			RF_R24_Value = RF_R24_Value - ((count) ? (1) : (0));
		}

		/* Store for future usage*/
		if (loopcnt < 100)
		{
			if (loop++ == 0)
			{
				/*BandWidth = 20 MHz*/
				pAd->Mlme.CaliBW20RfR24 = (UCHAR)RF_R24_Value;
			}
			else
			{
				/*BandWidth = 40 MHz*/
				pAd->Mlme.CaliBW40RfR24 = (UCHAR)RF_R24_Value;
				break;
			}
		}
		else 
			break;

		RT30xxWriteRFRegister(pAd, RF_R24, RF_R24_Value);

		/* reset count*/
		count = 0;
	} while(TRUE);


	/* Set back to initial state*/

	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R24, 0);

	RT30xxReadRFRegister(pAd, RF_R22, &value);
	value &= ~(0x01);
	RT30xxWriteRFRegister(pAd, RF_R22, value);

	/* set BBP back to BW20*/
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
	BBPValue&= (~0x18);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);

	DBGPRINT(RT_DEBUG_TRACE, ("RTMPFilterCalibration - CaliBW20RfR24=0x%x, CaliBW40RfR24=0x%x\n", pAd->Mlme.CaliBW20RfR24, pAd->Mlme.CaliBW40RfR24));
}


/*
	add by johnli, RF power sequence setup

	==========================================================================
	Description:

	Load RF normal operation-mode setup
	
	==========================================================================
 */
VOID RT30xxLoadRFNormalModeSetup(
	IN PRTMP_ADAPTER 	pAd)
{
	UCHAR RFValue, bbpreg = 0;

	{
		/* improve power consumption */
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R138, &bbpreg);
		if (pAd->Antenna.field.TxPath == 1)
		{
			/* turn off tx DAC_1*/
			bbpreg = (bbpreg | 0x20);
		}

		if (pAd->Antenna.field.RxPath == 1)
		{
			/* turn off tx ADC_1*/
			bbpreg &= (~0x2);
		}
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R138, bbpreg);
	}

	/*RX0_PD & TX0_PD, RF R1 register Bit 2 & Bit 3 to 0 and RF_BLOCK_en,RX1_PD & TX1_PD, Bit0, Bit 4 & Bit5 to 1*/
	RT30xxReadRFRegister(pAd, RF_R01, &RFValue);
	RFValue = (RFValue & (~0x0C)) | 0x31;
	RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

	/* TX_LO2_en, RF R15 register Bit 3 to 0*/
	RT30xxReadRFRegister(pAd, RF_R15, &RFValue);
	RFValue &= (~0x08);
	RT30xxWriteRFRegister(pAd, RF_R15, RFValue);


	/* TX_LO1_en, RF R17 register Bit 3 to 0*/
	RT30xxReadRFRegister(pAd, RF_R17, &RFValue);
	RFValue &= (~0x08);
	/* to fix rx long range issue*/
	if (((pAd->MACVersion & 0xffff) >= 0x0211) && (pAd->NicConfig2.field.ExternalLNAForG == 0))
	{
		RFValue |= 0x20;
	}
	/* set RF_R17_bit[2:0] equal to EEPROM setting at 0x48h*/
	if (pAd->TxMixerGain24G >= 2)
	{
		RFValue &= (~0x7);  /* clean bit [2:0]*/
		RFValue |= pAd->TxMixerGain24G;
	}
	RT30xxWriteRFRegister(pAd, RF_R17, RFValue);

	/* RX_LO1_en, RF R20 register Bit 3 to 0*/
	RT30xxReadRFRegister(pAd, RF_R20, &RFValue);
	RFValue &= (~0x08);
	RT30xxWriteRFRegister(pAd, RF_R20, RFValue);

	/* RX_LO2_en, RF R21 register Bit 3 to 0*/
	RT30xxReadRFRegister(pAd, RF_R21, &RFValue);
	RFValue &= (~0x08);
	RT30xxWriteRFRegister(pAd, RF_R21, RFValue);
}

/*
	==========================================================================
	Description:

	Load RF sleep-mode setup
	
	==========================================================================
 */
VOID RT30xxLoadRFSleepModeSetup(
	IN PRTMP_ADAPTER 	pAd)
{
	UCHAR RFValue;
	UINT32 MACValue;


	if(!IS_RT3572(pAd))
	{
		{
			/* RF_BLOCK_en. RF R1 register Bit 0 to 0*/
			RT30xxReadRFRegister(pAd, RF_R01, &RFValue);
			RFValue &= (~0x01);
			RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

			/* VCO_IC, RF R7 register Bit 4 & Bit 5 to 0*/
			RT30xxReadRFRegister(pAd, RF_R07, &RFValue);
			RFValue &= (~0x30);
			RT30xxWriteRFRegister(pAd, RF_R07, RFValue);

			/* Idoh, RF R9 register Bit 1, Bit 2 & Bit 3 to 0*/
			RT30xxReadRFRegister(pAd, RF_R09, &RFValue);
			RFValue &= (~0x0E);
			RT30xxWriteRFRegister(pAd, RF_R09, RFValue);

			/* RX_CTB_en, RF R21 register Bit 7 to 0*/
			RT30xxReadRFRegister(pAd, RF_R21, &RFValue);
			RFValue &= (~0x80);
			RT30xxWriteRFRegister(pAd, RF_R21, RFValue);
		}
	}


	/* Don't touch LDO_CFG0 for 3090F & 3593, possibly the board is single power scheme*/
	if (IS_RT3090(pAd) ||	/*IS_RT3090 including RT309x and RT3071/72*/
		IS_RT3572(pAd) ||
		(IS_RT3070(pAd) && ((pAd->MACVersion & 0xffff) < 0x0201)))
	{
		if (!IS_RT3572(pAd))
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
VOID RT30xxReverseRFSleepModeSetup(
	IN PRTMP_ADAPTER 	pAd,
	IN BOOLEAN			FlgIsInitState)
{
	UCHAR RFValue;
	UINT32 MACValue;

	if(!IS_RT3572(pAd))
	{
		{
			/* RF_BLOCK_en, RF R1 register Bit 0 to 1*/
			RT30xxReadRFRegister(pAd, RF_R01, &RFValue);
			RFValue |= 0x01;
			RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

			/* VCO_IC, RF R7 register Bit 4 & Bit 5 to 1*/
			RT30xxReadRFRegister(pAd, RF_R07, &RFValue);
			RFValue |= 0x20;
			RT30xxWriteRFRegister(pAd, RF_R07, RFValue);

			/* Idoh, RF R9 register Bit 1, Bit 2 & Bit 3 to 1*/
			RT30xxReadRFRegister(pAd, RF_R09, &RFValue);
			RFValue |= 0x0E;
			RT30xxWriteRFRegister(pAd, RF_R09, RFValue);

			/* RX_CTB_en, RF R21 register Bit 7 to 1*/
			RT30xxReadRFRegister(pAd, RF_R21, &RFValue);
			RFValue |= 0x80;
			RT30xxWriteRFRegister(pAd, RF_R21, RFValue);
		}
	}

	if (IS_RT3090(pAd) ||	/* IS_RT3090 including RT309x and RT3071/72*/
		IS_RT3572(pAd) ||
		IS_RT3390(pAd) ||
		IS_RT3593(pAd) ||
		(IS_RT3070(pAd) && ((pAd->MACVersion & 0xffff) < 0x0201)))
	{
		if ((!IS_RT3572(pAd)) && (!IS_RT3593(pAd)))
		{
			RT30xxReadRFRegister(pAd, RF_R27, &RFValue);
			if ((pAd->MACVersion & 0xffff) < 0x0211)
				RFValue = (RFValue & (~0x77)) | 0x3;
			else
				RFValue = (RFValue & (~0x77));
			RT30xxWriteRFRegister(pAd, RF_R27, RFValue);
		}

		/* RT3071 version E has fixed this issue*/
		if ((pAd->NicConfig2.field.DACTestBit == 1) && ((pAd->MACVersion & 0xffff) < 0x0211))
		{
			/* patch tx EVM issue temporarily*/
			RTMP_IO_READ32(pAd, LDO_CFG0, &MACValue);
			MACValue = ((MACValue & 0xE0FFFFFF) | 0x0D000000);
			RTMP_IO_WRITE32(pAd, LDO_CFG0, MACValue);
		}
		else if ((!IS_RT3090(pAd) && !IS_RT3593(pAd)))
		{
			RTMP_IO_READ32(pAd, LDO_CFG0, &MACValue);
			MACValue = ((MACValue & 0xE0FFFFFF) | 0x01000000);
			RTMP_IO_WRITE32(pAd, LDO_CFG0, MACValue);
		}
	}

	if(IS_RT3572(pAd))
		RT30xxWriteRFRegister(pAd, RF_R08, 0x80);
}
/* end johnli*/

VOID RT30xxHaltAction(
	IN PRTMP_ADAPTER 	pAd)
{
	UINT32		TxPinCfg = 0x00050F0F;


	/* Turn off LNA_PE or TRSW_POL*/

	if (IS_RT3070(pAd) || IS_RT3071(pAd) || IS_RT3572(pAd))
	{
		if ((IS_RT3071(pAd) || IS_RT3572(pAd))
#ifdef RTMP_EFUSE_SUPPORT
			&& (pAd->bUseEfuse)
#endif /* RTMP_EFUSE_SUPPORT */
			)
		{
			TxPinCfg &= 0xFFFBF0F0; /* bit18 off */
		}
		else
		{
			TxPinCfg &= 0xFFFFF0F0;
		}

		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);   
	}
}


VOID RT30xx_ChipSwitchChannel(
	IN PRTMP_ADAPTER 			pAd,
	IN UCHAR					Channel,
	IN BOOLEAN					bScan)
{
	CHAR    TxPwer = 0, TxPwer2 = DEFAULT_RF_TX_POWER; /*Bbp94 = BBPR94_DEFAULT, TxPwer2 = DEFAULT_RF_TX_POWER;*/
	UCHAR	index;
	UINT32 	Value = 0; /*BbpReg, Value;*/
	UCHAR 	RFValue;
	UINT32 i = 0;
	UCHAR Tx0FinePowerCtrl = 0, Tx1FinePowerCtrl = 0;
	BBP_R109_STRUC BbpR109 = {{0}};


	i = i; /* avoid compile warning */
	RFValue = 0;
	/* Search Tx power value*/

	/*
		We can't use ChannelList to search channel, since some central channl's txpowr doesn't list 
		in ChannelList, so use TxPower array instead.
	*/
	for (index = 0; index < MAX_NUM_OF_CHANNELS; index++)
	{
		if (Channel == pAd->TxPower[index].Channel)
		{
			TxPwer = pAd->TxPower[index].Power;
			TxPwer2 = pAd->TxPower[index].Power2;

#ifdef RT33xx
#endif /* RT33xx */
			break;
		}
	}

	if (index == MAX_NUM_OF_CHANNELS)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("AsicSwitchChannel: Can't find the Channel#%d \n", Channel));
	}
#ifdef RT30xx
	/* The RF programming sequence is difference between 3xxx and 2xxx*/
	if ((IS_RT30xx(pAd)) && 
		((pAd->RfIcType == RFIC_3020) || (pAd->RfIcType == RFIC_2020) ||
		(pAd->RfIcType == RFIC_3021) || (pAd->RfIcType == RFIC_3022) || (pAd->RfIcType == RFIC_3320)))
	{
		/* modify by WY for Read RF Reg. error */
		UCHAR	calRFValue;
		for (index = 0; index < NUM_OF_3020_CHNL; index++)
		{
			if (Channel == FreqItems3020[index].Channel)
			{
				/* Programming channel parameters*/
				RT30xxWriteRFRegister(pAd, RF_R02, FreqItems3020[index].N);
				/*
					RT3370/RT3390 RF version is 0x3320 RF_R3 [7:4] is not reserved bits
					RF_R3[6:4] (pa1_bc_cck) : PA1 Bias CCK
					RF_R3[7] (pa2_cc_cck) : PA2 Cascode Bias CCK
				 */
				RT30xxReadRFRegister(pAd, RF_R03, (PUCHAR)(&RFValue));
				RFValue = (RFValue & 0xF0) | (FreqItems3020[index].K & ~0xF0); /* <bit 3:0>:K<bit 3:0>*/
				RT30xxWriteRFRegister(pAd, RF_R03, RFValue);
				RT30xxReadRFRegister(pAd, RF_R06, &RFValue);
				RFValue = (RFValue & 0xFC) | FreqItems3020[index].R;
				RT30xxWriteRFRegister(pAd, RF_R06, RFValue);

				/* Set Tx0 Power*/
				RT30xxReadRFRegister(pAd, RF_R12, &RFValue);
				RFValue = (RFValue & 0xE0) | TxPwer;
				RT30xxWriteRFRegister(pAd, RF_R12, RFValue);

				/*Set Tx1 Power*/
				RT30xxReadRFRegister(pAd, RF_R13, &RFValue);
				RFValue = (RFValue & 0xE0) | TxPwer2;
				RT30xxWriteRFRegister(pAd, RF_R13, RFValue);

#ifdef RT33xx
#endif /* RT33xx */

				/* Tx/Rx Stream setting*/
				RT30xxReadRFRegister(pAd, RF_R01, &RFValue);

				RFValue &= 0x03; /*clear bit[7~2]*/
				if (pAd->Antenna.field.TxPath == 1)
					RFValue |= 0xA0;
				else if (pAd->Antenna.field.TxPath == 2)
					RFValue |= 0x80;
				if (pAd->Antenna.field.RxPath == 1)
					RFValue |= 0x50;
				else if (pAd->Antenna.field.RxPath == 2)
					RFValue |= 0x40;
				RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

				/* Set RF offset*/
				RT30xxReadRFRegister(pAd, RF_R23, &RFValue);
				RFValue = (RFValue & 0x80) | pAd->RfFreqOffset;
				RT30xxWriteRFRegister(pAd, RF_R23, RFValue);

				/* Set BW*/
				if (!bScan && (pAd->CommonCfg.BBPCurrentBW == BW_40))
				{
					calRFValue = pAd->Mlme.CaliBW40RfR24;
				}
				else
				{
					calRFValue = pAd->Mlme.CaliBW20RfR24;
				}
				/*
					RT3370/RT3390 RF version is 0x3320 RF_R24 [7:6] is not reserved bits
					RF_R24[6] (BB_Rx1_out_en) : enable baseband output and ADC input
					RF_R24[7] (BB_Tx1_out_en) : enable DAC output or baseband input
				 */
				RT30xxReadRFRegister(pAd, RF_R24, (PUCHAR)(&RFValue));
				calRFValue = (RFValue & 0xC0) | (calRFValue & ~0xC0); /* <bit 5>:tx_h20M<bit 5> and <bit 4:0>:tx_agc_fc<bit 4:0>*/
				RT30xxWriteRFRegister(pAd, RF_R24, calRFValue);

				/*
					RT3370/RT3390 RF version is 0x3320 RF_R31 [7:6] is not reserved bits
					RF_R31[4:0] (rx_agc_fc) : capacitor control in baseband filter
					RF_R31[5] (rx_ h20M) : rx_ h20M: 0=10 MHz and 1=20MHz
					RF_R31[7:6] (drv_bc_cck) : Driver Bias CCK
				 */
				/* Set BW*/
				if (IS_RT3390(pAd)) /* RT3390 has different AGC for Tx and Rx*/
				{
					if (!bScan && (pAd->CommonCfg.BBPCurrentBW == BW_40))
					{
						calRFValue = pAd->Mlme.CaliBW40RfR31;
					}
					else
					{
						calRFValue = pAd->Mlme.CaliBW20RfR31;
					}
				}
				RT30xxReadRFRegister(pAd, RF_R31, (PUCHAR)(&RFValue));
				calRFValue = (RFValue & 0xC0) | (calRFValue & ~0xC0); /* <bit 5>:rx_h20M<bit 5> and <bit 4:0>:rx_agc_fc<bit 4:0>*/				
				RT30xxWriteRFRegister(pAd, RF_R31, calRFValue);

				/* Enable RF tuning*/
				RT30xxReadRFRegister(pAd, RF_R07, &RFValue);
				RFValue = RFValue | 0x1;
				RT30xxWriteRFRegister(pAd, RF_R07, RFValue);

				/* latch channel for future usage.*/
				pAd->LatchRfRegs.Channel = Channel;
				
		DBGPRINT(RT_DEBUG_TRACE, ("SwitchChannel#%d(RF=%d, Pwr0=%d, Pwr1=%d, %dT), N=0x%02X, K=0x%02X, R=0x%02X\n",
			Channel, 
			pAd->RfIcType, 
			TxPwer,
			TxPwer2,
			pAd->Antenna.field.TxPath,
			FreqItems3020[index].N, 
			FreqItems3020[index].K, 
			FreqItems3020[index].R));

				break;
			}
		}
	}
	else
#endif /* RT30xx */
	{
		switch (pAd->RfIcType)
		{
			default:
				DBGPRINT(RT_DEBUG_TRACE, ("SwitchChannel#%d : unknown RFIC=%d\n",
					  Channel, pAd->RfIcType));
				break;
		}	
	}

	/* Change BBP setting during siwtch from a->g, g->a*/
	if (Channel <= 14)
	{
		ULONG	TxPinCfg = 0x00050F0A;/*Gary 2007/08/09 0x050A0A*/

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0);/*(0x44 - GET_LNA_GAIN(pAd)));	According the Rory's suggestion to solve the middle range issue.*/

		/* Rx High power VGA offset for LNA select*/
		{
			if (pAd->NicConfig2.field.ExternalLNAForG)
			{
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x62);
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x46);
			}
			else
			{
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x84);
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x50);
			}
		}

		/* 5G band selection PIN, bit1 and bit2 are complement*/
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x6);
		Value |= (0x04);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		{
			/* Turn off unused PA or LNA when only 1T or 1R*/
			if (pAd->Antenna.field.TxPath == 1)
			{
				TxPinCfg &= 0xFFFFFFF3;
			}
			if (pAd->Antenna.field.RxPath == 1)
			{
				TxPinCfg &= 0xFFFFF3FF;
			}
		}

		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);

	}
	else
	{
		ULONG	TxPinCfg = 0x00050F05;/*Gary 2007/8/9 0x050505*/
		UINT8	bbpValue;
		
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0);/*(0x44 - GET_LNA_GAIN(pAd)));    According the Rory's suggestion to solve the middle range issue.*/   

		/* Set the BBP_R82 value here */
		bbpValue = 0xF2;
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, bbpValue);


		/* Rx High power VGA offset for LNA select*/
		if (pAd->NicConfig2.field.ExternalLNAForA)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x46);
		}
		else
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x50);
		}

		/* 5G band selection PIN, bit1 and bit2 are complement*/
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x6);
		Value |= (0x02);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		/* Turn off unused PA or LNA when only 1T or 1R*/
		{
			/* Turn off unused PA or LNA when only 1T or 1R*/
			if (pAd->Antenna.field.TxPath == 1)
			{
				TxPinCfg &= 0xFFFFFFF3;
			}
			if (pAd->Antenna.field.RxPath == 1)
			{
				TxPinCfg &= 0xFFFFF3FF;
			}
		}

		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);
	}

	/* R66 should be set according to Channel and use 20MHz when scanning*/

	if (bScan)
		RTMPSetAGCInitValue(pAd, BW_20);
	else
		RTMPSetAGCInitValue(pAd, pAd->CommonCfg.BBPCurrentBW);

	/*
		On 11A, We should delay and wait RF/BBP to be stable
		and the appropriate time should be 1000 micro seconds 
		2005/06/05 - On 11G, We also need this delay time. Otherwise it's difficult to pass the WHQL.
	*/
	RTMPusecDelay(1000);
}

VOID RT30xx_ChipBBPAdjust(
	IN RTMP_ADAPTER			*pAd)
{
	UINT32 Value;
	UCHAR byteValue = 0;

#ifdef DOT11_N_SUPPORT
	if ((pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth  == BW_40) && 
		(pAd->CommonCfg.RegTransmitSetting.field.EXTCHA == EXTCHA_ABOVE)
		/*(pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset == EXTCHA_ABOVE)*/
	)
	{
		{
		pAd->CommonCfg.BBPCurrentBW = BW_40;
		pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel + 2;
		}
		/*  TX : control channel at lower */
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x1);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		/*  RX : control channel at lower */
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &byteValue);
		byteValue &= (~0x20);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, byteValue);

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &byteValue);
		byteValue &= (~0x18);
		byteValue |= 0x10;
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, byteValue);


		/* request by Gary 20070208 for middle and long range G Band*/
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x38);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x12);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x10);
			

		DBGPRINT(RT_DEBUG_TRACE, ("ApStartUp : ExtAbove, ChannelWidth=%d, Channel=%d, ExtChanOffset=%d(%d) \n",
									pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth, 
									pAd->CommonCfg.Channel, 
									pAd->CommonCfg.RegTransmitSetting.field.EXTCHA,
									pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset));
	}
	else if ((pAd->CommonCfg.Channel > 2) && 
			(pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth  == BW_40) && 
			(pAd->CommonCfg.RegTransmitSetting.field.EXTCHA == EXTCHA_BELOW)
			/*(pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset == EXTCHA_BELOW)*/)
	{
		pAd->CommonCfg.BBPCurrentBW = BW_40;

		if (pAd->CommonCfg.Channel == 14)
			pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel - 1;
		else
			pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel - 2;
		/*  TX : control channel at upper */
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value |= (0x1);		
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		/*  RX : control channel at upper */
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &byteValue);
		byteValue |= (0x20);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, byteValue);

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &byteValue);
		byteValue &= (~0x18);
		byteValue |= 0x10;
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, byteValue);
		

	 	/* request by Gary 20070208 for middle and long range G band*/
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x38);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x12);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x10);
		
		DBGPRINT(RT_DEBUG_TRACE, ("ApStartUp : ExtBlow, ChannelWidth=%d, Channel=%d, ExtChanOffset=%d(%d) \n",
									pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth, 
									pAd->CommonCfg.Channel, 
									pAd->CommonCfg.RegTransmitSetting.field.EXTCHA,
									pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset));
	}
	else
#endif /* DOT11_N_SUPPORT */
	{
		pAd->CommonCfg.BBPCurrentBW = BW_20;
		pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel;
		
		/*  TX : control channel at lower */
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x1);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &byteValue);
		byteValue &= (~0x18);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, byteValue);
		
		/* 20 MHz bandwidth*/

		/* request by Gary 20070208*/
		/*RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x30);*/
		/* request by Brian 20070306*/
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x38);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x12);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0a);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x10);
	

#ifdef DOT11_N_SUPPORT
		DBGPRINT(RT_DEBUG_TRACE, ("ApStartUp : 20MHz, ChannelWidth=%d, Channel=%d, ExtChanOffset=%d(%d) \n",
										pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth, 
										pAd->CommonCfg.Channel, 
										pAd->CommonCfg.RegTransmitSetting.field.EXTCHA,
										pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset));
#endif /* DOT11_N_SUPPORT */
	}
	
	
 	/* request by Gary 20070208 for middle and long range G band*/
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, 0x2D);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, 0x2D);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, 0x2D);
	/*RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0x2D);*/
		

}	

VOID RT30xx_RTMPSetAGCInitValue(
	IN PRTMP_ADAPTER		pAd,
	IN UCHAR				BandWidth)
{

	UCHAR	R66 = 0x30;
	
	if (pAd->LatchRfRegs.Channel <= 14)
	{	/* BG band*/
		/* Gary was verified Amazon AP and find that RT307x has BBP_R66 invalid default value */
		if (IS_RT3070(pAd)||IS_RT3090(pAd) || IS_RT3390(pAd) || IS_RT3593(pAd))
		{
			R66 = 0x1C + 2*GET_LNA_GAIN(pAd);
			{
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
			}
		}
	}	


}

#endif /* RT30xx */

