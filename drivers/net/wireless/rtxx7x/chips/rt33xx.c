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


#ifdef RT33xx


#ifndef RTMP_RF_RW_SUPPORT
#error "You Should Enable compile flag RTMP_RF_RW_SUPPORT for this chip"
#endif /* RTMP_RF_RW_SUPPORT */

#include "rt_config.h"



/* RF register initialization set*/

#ifdef RT3370
REG_PAIR RT3370_RFRegTable[] = {
	{RF_R00,		0xA0},
	{RF_R01,		0xF1},
	{RF_R02,		0xF1},
	{RF_R03,		0x32},
	{RF_R04,		0x41},
	{RF_R05,		0x8F},
	{RF_R06,		0x4A},
	{RF_R07,		0xE0},
	{RF_R08,		0x4B}, /* Read only*/
	{RF_R09,		0x81},

	{RF_R10,		0x71}, /* Default value changed from 0x41 to 0x71*/
	{RF_R11,		0x11},
	{RF_R12,		0x28},
	{RF_R13,		0xE0},
	{RF_R14,		0x90},
	{RF_R15,		0x73},
	{RF_R16,		0x44},
	{RF_R17,		0x93},
	{RF_R18,		0x5C},
	{RF_R19,		0x84}, /* Default value changed from 0x82 to 0x84*/

	{RF_R20,		0xB2},
	{RF_R21,		0xE7},
	{RF_R22,		0x04},	
	{RF_R23,		0x26},
	{RF_R24,		BW20RFR24},
	{RF_R25,		0x23},
	{RF_R26,		0x85},
	{RF_R27,		0x02},
	{RF_R28,		0x60},
	{RF_R29,		0x90},
	{RF_R30,		0x29},
	{RF_R31,		BW20RFR31},
};

UCHAR RT3370_NUM_RF_REG_PARMS = (sizeof(RT3370_RFRegTable) / sizeof(REG_PAIR));
#endif /* RT3370 */


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
VOID RT33xx_Init(
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
	if (IS_RT3390(pAd))
	{
#ifdef RT3370
		if (pAd->infType == RTMP_DEV_INF_USB)
		{
			pChipCap->pRFRegTable = RT3370_RFRegTable;
			pChipCap->pBBPRegTable = RT3370_BBPRegTable;
			pChipCap->bbpRegTbSize = RT3370_NUM_BBP_REG_PARMS;
			pChipOps->AsicRfInit = NICInitRT3370RFRegisters;
		}
#endif /* RT3370 */

		pChipOps->AsicHaltAction = RT33xxHaltAction;
		pChipOps->AsicRfTurnOff = RT33xxLoadRFSleepModeSetup;		
		pChipOps->AsicReverseRfFromSleepMode = RT33xxReverseRFSleepModeSetup;
		pChipOps->ChipSwitchChannel = RT33xx_ChipSwitchChannel;
		pChipOps->ChipBBPAdjust = RT30xx_ChipBBPAdjust;
		pChipOps->RTMPSetAGCInitValue = RT30xx_RTMPSetAGCInitValue;
		pChipOps->SetRxAnt = RT33xxSetRxAnt;

		pChipOps->ChipResumeMsduTransmission = NULL;
		pChipOps->VdrTuning1 = NULL;
		pChipOps->RxSensitivityTuning = NULL;
		pChipCap->MaxNumOfBbpId = 185;

#ifdef RTMP_FREQ_CALIBRATION_SUPPORT
#ifdef CONFIG_STA_SUPPORT
		pChipOps->AsicFreqCalInit = InitFrequencyCalibration;
		pChipOps->AsicFreqCalStop = StopFrequencyCalibration;
		pChipOps->AsicFreqCal = FrequencyCalibration;
		pChipOps->AsicFreqOffsetGet = GetFrequencyOffset;
#endif /* CONFIG_STA_SUPPORT */		
#endif /* RTMP_FREQ_CALIBRATION_SUPPORT */
 	}
}

/* Antenna divesity use GPIO3 and EESK pin for control*/
/* Antenna and EEPROM access are both using EESK pin,*/
/* Therefor we should avoid accessing EESK at the same time*/
/* Then restore antenna after EEPROM access*/
/* The original name of this function is AsicSetRxAnt(), now change to */
/*VOID AsicSetRxAnt(*/

VOID RT33xxSetRxAnt(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			Ant)
{
	UINT32	Value;
	UINT32	x;

	if (/*(!pAd->NicConfig2.field.AntDiversity) ||*/
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS))	||
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))	||
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF)) ||
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
	{
		return;
	}

	/* the antenna selection is through firmware and MAC register(GPIO3)*/
	if (IS_RT3390(pAd) && pAd->RfIcType == RFIC_3320)
	{
	if (Ant == 0)
	{
		/* Main antenna*/
		/* E2PROM_CSR only in PCI bus Reg., USB Bus need MCU commad to control the EESK pin.*/
#ifdef RTMP_MAC_USB
		AsicSendCommandToMcu(pAd, 0x73, 0xff, 0x1, 0x0);
#endif /* RTMP_MAC_USB */

		RTMP_IO_READ32(pAd, GPIO_CTRL_CFG, &Value);
		Value &= ~(0x0808);
		RTMP_IO_WRITE32(pAd, GPIO_CTRL_CFG, Value);
			DBGPRINT(RT_DEBUG_TRACE, ("AsicSetRxAnt, switch to main antenna\n"));
	}
	else
	{
		/* Aux antenna*/
		/* E2PROM_CSR only in PCI bus Reg., USB Bus need MCU commad to control the EESK pin.*/
#ifdef RTMP_MAC_USB
		AsicSendCommandToMcu(pAd, 0x73, 0xff, 0x0, 0x0);
#endif /* RTMP_MAC_USB */
		RTMP_IO_READ32(pAd, GPIO_CTRL_CFG, &Value);
		Value &= ~(0x0808);
		Value |= 0x08;
		RTMP_IO_WRITE32(pAd, GPIO_CTRL_CFG, Value);
			DBGPRINT(RT_DEBUG_TRACE, ("AsicSetRxAnt, switch to aux antenna\n"));
		}
	}
}



/* add by johnli, RF power sequence setup*/
/*
	==========================================================================
	Description:

	Load RF normal operation-mode setup
	
	==========================================================================
 */
VOID RT33xxLoadRFNormalModeSetup(
	IN PRTMP_ADAPTER 	pAd)
{
	UCHAR RFValue = 0, bbpreg = 0 ;

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

	/* RX0_PD & TX0_PD, RF R1 register Bit 2 & Bit 3 to 0 and RF_BLOCK_en,RX1_PD & TX1_PD, Bit0, Bit 4 & Bit5 to 1*/
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
VOID RT33xxLoadRFSleepModeSetup(
	IN PRTMP_ADAPTER 	pAd)
{
	UCHAR RFValue;
	UINT32 MACValue;

		/* RF_BLOCK_en. RF R1 register Bit 0 to 0*/
		RT30xxReadRFRegister(pAd, RF_R01, &RFValue);
		RFValue &= (~0x01);
		RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

		/* VCO_IC, RF R7 register Bit 4 & Bit 5 to 0*/
		RT30xxReadRFRegister(pAd, RF_R07, &RFValue);
		RFValue &= (~0x30);
		RT30xxWriteRFRegister(pAd, RF_R07, RFValue);


		/* RX_CTB_en, RF R21 register Bit 7 to 0*/
		RT30xxReadRFRegister(pAd, RF_R21, &RFValue);
		RFValue &= (~0x80);
		RT30xxWriteRFRegister(pAd, RF_R21, RFValue);

	if (IS_RT3390(pAd))
	{
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
VOID RT33xxReverseRFSleepModeSetup(
	IN PRTMP_ADAPTER 	pAd,
	IN BOOLEAN			FlgIsInitState)
{
	UCHAR RFValue;
	UINT32 MACValue;

		/* RF_BLOCK_en, RF R1 register Bit 0 to 1*/
		RT30xxReadRFRegister(pAd, RF_R01, &RFValue);
		RFValue |= 0x01;
		RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

		/* VCO_IC, RF R7 register Bit 4 & Bit 5 to 1*/
		RT30xxReadRFRegister(pAd, RF_R07, &RFValue);
		/* According to HK's comment for Max Input power issue.
		    RF 07 must set to 0x60. */
		RFValue |= 0x20; /* 0x30. */
		RT30xxWriteRFRegister(pAd, RF_R07, RFValue);

		/* RX_CTB_en, RF R21 register Bit 7 to 1*/
		RT30xxReadRFRegister(pAd, RF_R21, &RFValue);
		RFValue |= 0x80;
		RT30xxWriteRFRegister(pAd, RF_R21, RFValue);

	if (IS_RT3390(pAd))
		{
		/* RT3071 version E has fixed this issue*/
		if ((pAd->NicConfig2.field.DACTestBit == 1) && ((pAd->MACVersion & 0xffff) < 0x0211))
		{
			/* patch tx EVM issue temporarily*/
			RTMP_IO_READ32(pAd, LDO_CFG0, &MACValue);
			MACValue = ((MACValue & 0xE0FFFFFF) | 0x0D000000);
			RTMP_IO_WRITE32(pAd, LDO_CFG0, MACValue);
		}
		else
		{
			RTMP_IO_READ32(pAd, LDO_CFG0, &MACValue);
			MACValue = ((MACValue & 0xE0FFFFFF) | 0x01000000);
			RTMP_IO_WRITE32(pAd, LDO_CFG0, MACValue);
		}
	}
}
/* end johnli*/

VOID RT33xxHaltAction(
	IN PRTMP_ADAPTER 	pAd)
{
	UINT32		TxPinCfg = 0x00050F0F;

	
	/* Turn off LNA_PE or TRSW_POL*/
	
        /* Fixed suspend leakage current*/
                /* According to MAC 0x0580 bit [31], set MAC 0x1328 bit[18] during suspend mode.*/
                /* If SEL_EFUSE=0, set TRSW_POL=0 in suspend mode.*/
                /* If SEL_EFUSE=1, set TRSW_POL=1 in suspend mode.*/
	
		if (IS_RT3390(pAd)
#ifdef RTMP_EFUSE_SUPPORT
			&& (pAd->bUseEfuse)
#endif /* RTMP_EFUSE_SUPPORT */
			)
		{
			TxPinCfg &= 0xFFFBF0F0; /* bit18 off*/
		}
		else
		{
			TxPinCfg &= 0xFFFFF0F0;
		}

		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);   
	}

VOID RT33xx_ChipSwitchChannel(
	IN PRTMP_ADAPTER 			pAd,
	IN UCHAR					Channel,
	IN BOOLEAN					bScan)
{
	CHAR    TxPwer = 0, TxPwer2 = DEFAULT_RF_TX_POWER; /*Bbp94 = BBPR94_DEFAULT, TxPwer2 = DEFAULT_RF_TX_POWER;*/
	UCHAR	index;
	UINT32 	Value = 0; /*BbpReg, Value;*/
	UCHAR 	RFValue;
#ifdef DOT11N_SS3_SUPPORT
	CHAR    TxPwer3 = 0;
#endif /* DOT11N_SS3_SUPPORT */

#ifdef RT30xx
	UCHAR Tx0FinePowerCtrl = 0, Tx1FinePowerCtrl = 0;
	BBP_R109_STRUC BbpR109 = {{0}};
#endif /* RT30xx */


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
#ifdef DOT11N_SS3_SUPPORT
			if (IS_RT2883(pAd) || IS_RT3593(pAd) || IS_RT3883(pAd))
		    	TxPwer3 = pAd->TxPower[index].Power3;
#endif /* DOT11N_SS3_SUPPORT */

#ifdef RT30xx /*RT33xx*/
			if ((IS_RT3090A(pAd) || IS_RT3390(pAd) || IS_RT5390(pAd)))/*&&*/
				/*(pAd->infType == RTMP_DEV_INF_PCI || pAd->infType == RTMP_DEV_INF_PCIE))*/
			{
				Tx0FinePowerCtrl = pAd->TxPower[index].Tx0FinePowerCtrl;
				Tx1FinePowerCtrl = pAd->TxPower[index].Tx1FinePowerCtrl;
			}
#endif /* RT30xx */

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

				/* Set Tx1 Power*/
				RT30xxReadRFRegister(pAd, RF_R13, &RFValue);
				RFValue = (RFValue & 0xE0) | TxPwer2;
				RT30xxWriteRFRegister(pAd, RF_R13, RFValue);

#ifdef RT33xx
#endif /* RT33xx */

				/* Tx/Rx Stream setting*/
				RT30xxReadRFRegister(pAd, RF_R01, &RFValue);
				/*if (IS_RT3090(pAd))*/
				/*	RFValue |= 0x01;  Enable RF block.*/
				RFValue &= 0xC3;	/*clear bit[7~2]*/
				if (pAd->Antenna.field.TxPath == 1)
					RFValue |= 0x20;
				else if (pAd->Antenna.field.TxPath == 2)
					;
				if (pAd->Antenna.field.RxPath == 1)
					RFValue |= 0x10;
				else if (pAd->Antenna.field.RxPath == 2)
					;
				RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

				/* Set RF offset*/
				RT30xxReadRFRegister(pAd, RF_R23, &RFValue);
				RFValue = (RFValue & 0x80) | pAd->RfFreqOffset;
				RT30xxWriteRFRegister(pAd, RF_R23, RFValue);

				/* Set BW*/
				if (!bScan && (pAd->CommonCfg.BBPCurrentBW == BW_40))
				{
					calRFValue = pAd->Mlme.CaliBW40RfR24;
					/*DISABLE_11N_CHECK(pAd);*/
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
				calRFValue = (RFValue & 0xC0) | (calRFValue & ~0xC0); /* <bit 5>:rx_h20M<bit 5> and <bit 4:0>:rx_agc_fc<bit 4:0>				*/
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
	
#endif /* RT30xx */	

	/* Change BBP setting during siwtch from a->g, g->a*/
	if (Channel <= 14)
	{
		ULONG	TxPinCfg = 0x00050F0A;/*Gary 2007/08/09 0x050A0A*/


		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0);/*(0x44 - GET_LNA_GAIN(pAd)));	 According the Rory's suggestion to solve the middle range issue.*/

		/* Rx High power VGA offset for LNA select*/
		
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


		

		/* Turn off unused PA or LNA when only 1T or 1R*/
		if (pAd->Antenna.field.TxPath == 1)
		{
			TxPinCfg &= 0xFFFFFFF3;
		}
		if (pAd->Antenna.field.RxPath == 1)
		{
			TxPinCfg &= 0xFFFFF3FF;
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
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0);/*(0x44 - GET_LNA_GAIN(pAd)));    According the Rory's suggestion to solve the middle range issue.     */

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

		/* Turn off unused PA or LNA when only 1T or 1R*/
		if (pAd->Antenna.field.TxPath == 1)
		{
			TxPinCfg &= 0xFFFFFFF3;
		}
		if (pAd->Antenna.field.RxPath == 1)
		{
			TxPinCfg &= 0xFFFFF3FF;
		}
	

		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);

	}

	
	/* GPIO control*/
	

	/* R66 should be set according to Channel and use 20MHz when scanning*/
	/*RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, (0x2E + GET_LNA_GAIN(pAd)));*/
	if (bScan)
		RTMPSetAGCInitValue(pAd, BW_20);
	else
		RTMPSetAGCInitValue(pAd, pAd->CommonCfg.BBPCurrentBW);

	
	/* On 11A, We should delay and wait RF/BBP to be stable*/
	/* and the appropriate time should be 1000 micro seconds */
	/* 2005/06/05 - On 11G, We also need this delay time. Otherwise it's difficult to pass the WHQL.*/
	
	RTMPusecDelay(1000);  
}

#endif /* RT33xx */

