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


#include "rt_config.h"



FREQUENCY_ITEM RtmpFreqItems3020[] =
{	
	/* ISM : 2.4 to 2.483 GHz                         */
	/* 11g*/
	/*-CH---N-------R---K-----------*/
	{1,    241,  2,  2},
	{2,    241,	 2,  7},
	{3,    242,	 2,  2},
	{4,    242,	 2,  7},
	{5,    243,	 2,  2},
	{6,    243,	 2,  7},
	{7,    244,	 2,  2},
	{8,    244,	 2,  7},
	{9,    245,	 2,  2},
	{10,   245,	 2,  7},
	{11,   246,	 2,  2},
	{12,   246,	 2,  7},
	{13,   247,	 2,  2},
	{14,   248,	 2,  4},
};

FREQUENCY_ITEM FreqItems3020_Xtal20M[] =
{	
	/*
	 * RF_R08:
	 * <7:0>: pll_N<7:0>
	 *
	 * RF_R09:
	 * <3:0>: pll_K<3:0>
	 * <4>: pll_N<8>
	 * <7:5>pll_N<11:9>
	 *
	 */
	/*-CH---N--------R---N[7:4]K[3:0]------*/
	{1,    0xE2,     2,  0x14},
	{2,    0xE3,	 2,  0x14},
	{3,    0xE4,	 2,  0x14},
	{4,    0xE5,	 2,  0x14},
	{5,    0xE6,	 2,  0x14},
	{6,    0xE7,	 2,  0x14},
	{7,    0xE8,	 2,  0x14},
	{8,    0xE9,	 2,  0x14},
	{9,    0xEA,	 2,  0x14},
	{10,   0xEB,	 2,  0x14},
	{11,   0xEC,	 2,  0x14},
	{12,   0xED,	 2,  0x14},
	{13,   0xEE,	 2,  0x14},
	{14,   0xF0,	 2,  0x18},
};

UCHAR	NUM_OF_3020_CHNL = (sizeof(RtmpFreqItems3020) / sizeof(FREQUENCY_ITEM));

FREQUENCY_ITEM *FreqItems3020 = RtmpFreqItems3020;

#if defined(RT28xx) || defined(RT2883)
/* Reset the RFIC setting to new series    */
RTMP_RF_REGS RF2850RegTable[] = {
/*		ch	 R1 		 R2 		 R3(TX0~4=0) R4*/
		{1,  0x98402ecc, 0x984c0786, 0x9816b455, 0x9800510b},
		{2,  0x98402ecc, 0x984c0786, 0x98168a55, 0x9800519f},
		{3,  0x98402ecc, 0x984c078a, 0x98168a55, 0x9800518b},
		{4,  0x98402ecc, 0x984c078a, 0x98168a55, 0x9800519f},
		{5,  0x98402ecc, 0x984c078e, 0x98168a55, 0x9800518b},
		{6,  0x98402ecc, 0x984c078e, 0x98168a55, 0x9800519f},
		{7,  0x98402ecc, 0x984c0792, 0x98168a55, 0x9800518b},
		{8,  0x98402ecc, 0x984c0792, 0x98168a55, 0x9800519f},
		{9,  0x98402ecc, 0x984c0796, 0x98168a55, 0x9800518b},
		{10, 0x98402ecc, 0x984c0796, 0x98168a55, 0x9800519f},
		{11, 0x98402ecc, 0x984c079a, 0x98168a55, 0x9800518b},
		{12, 0x98402ecc, 0x984c079a, 0x98168a55, 0x9800519f},
		{13, 0x98402ecc, 0x984c079e, 0x98168a55, 0x9800518b},
		{14, 0x98402ecc, 0x984c07a2, 0x98168a55, 0x98005193},

		/* 802.11 UNI / HyperLan 2*/
		{36, 0x98402ecc, 0x984c099a, 0x98158a55, 0x980ed1a3},
		{38, 0x98402ecc, 0x984c099e, 0x98158a55, 0x980ed193},
		{40, 0x98402ec8, 0x984c0682, 0x98158a55, 0x980ed183},
		{44, 0x98402ec8, 0x984c0682, 0x98158a55, 0x980ed1a3},
		{46, 0x98402ec8, 0x984c0686, 0x98158a55, 0x980ed18b},
		{48, 0x98402ec8, 0x984c0686, 0x98158a55, 0x980ed19b},
		{52, 0x98402ec8, 0x984c068a, 0x98158a55, 0x980ed193},
		{54, 0x98402ec8, 0x984c068a, 0x98158a55, 0x980ed1a3},
		{56, 0x98402ec8, 0x984c068e, 0x98158a55, 0x980ed18b},
		{60, 0x98402ec8, 0x984c0692, 0x98158a55, 0x980ed183},
		{62, 0x98402ec8, 0x984c0692, 0x98158a55, 0x980ed193},
		{64, 0x98402ec8, 0x984c0692, 0x98158a55, 0x980ed1a3}, /* Plugfest#4, Day4, change RFR3 left4th 9->5.*/

		/* 802.11 HyperLan 2*/
		{100, 0x98402ec8, 0x984c06b2, 0x98178a55, 0x980ed783},
		
		/* 2008.04.30 modified */
		/* The system team has AN to improve the EVM value */
		/* for channel 102 to 108 for the RT2850/RT2750 dual band solution.*/
		{102, 0x98402ec8, 0x985c06b2, 0x98578a55, 0x980ed793},
		{104, 0x98402ec8, 0x985c06b2, 0x98578a55, 0x980ed1a3},
		{108, 0x98402ecc, 0x985c0a32, 0x98578a55, 0x980ed193},

		{110, 0x98402ecc, 0x984c0a36, 0x98178a55, 0x980ed183},
		{112, 0x98402ecc, 0x984c0a36, 0x98178a55, 0x980ed19b},
		{116, 0x98402ecc, 0x984c0a3a, 0x98178a55, 0x980ed1a3},
		{118, 0x98402ecc, 0x984c0a3e, 0x98178a55, 0x980ed193},
		{120, 0x98402ec4, 0x984c0382, 0x98178a55, 0x980ed183},
		{124, 0x98402ec4, 0x984c0382, 0x98178a55, 0x980ed193},
		{126, 0x98402ec4, 0x984c0382, 0x98178a55, 0x980ed15b}, /* 0x980ed1bb->0x980ed15b required by Rory 20070927*/
		{128, 0x98402ec4, 0x984c0382, 0x98178a55, 0x980ed1a3},
		{132, 0x98402ec4, 0x984c0386, 0x98178a55, 0x980ed18b},
		{134, 0x98402ec4, 0x984c0386, 0x98178a55, 0x980ed193},
		{136, 0x98402ec4, 0x984c0386, 0x98178a55, 0x980ed19b},
		{140, 0x98402ec4, 0x984c038a, 0x98178a55, 0x980ed183},

		/* 802.11 UNII*/
		{149, 0x98402ec4, 0x984c038a, 0x98178a55, 0x980ed1a7},
		{151, 0x98402ec4, 0x984c038e, 0x98178a55, 0x980ed187},
		{153, 0x98402ec4, 0x984c038e, 0x98178a55, 0x980ed18f},
		{157, 0x98402ec4, 0x984c038e, 0x98178a55, 0x980ed19f},
		{159, 0x98402ec4, 0x984c038e, 0x98178a55, 0x980ed1a7},
		{161, 0x98402ec4, 0x984c0392, 0x98178a55, 0x980ed187},
		{165, 0x98402ec4, 0x984c0392, 0x98178a55, 0x980ed197},
		{167, 0x98402ec4, 0x984c03d2, 0x98179855, 0x9815531f},
		{169, 0x98402ec4, 0x984c03d2, 0x98179855, 0x98155327},
		{171, 0x98402ec4, 0x984c03d6, 0x98179855, 0x98155307},
		{173, 0x98402ec4, 0x984c03d6, 0x98179855, 0x9815530f},

		/* Japan*/
		{184, 0x95002ccc, 0x9500491e, 0x9509be55, 0x950c0a0b},
		{188, 0x95002ccc, 0x95004922, 0x9509be55, 0x950c0a13},
		{192, 0x95002ccc, 0x95004926, 0x9509be55, 0x950c0a1b},
		{196, 0x95002ccc, 0x9500492a, 0x9509be55, 0x950c0a23},
		{208, 0x95002ccc, 0x9500493a, 0x9509be55, 0x950c0a13},
		{212, 0x95002ccc, 0x9500493e, 0x9509be55, 0x950c0a1b},
		{216, 0x95002ccc, 0x95004982, 0x9509be55, 0x950c0a23},

		/* still lack of MMAC(Japan) ch 34,38,42,46*/
};
UCHAR	NUM_OF_2850_CHNL = (sizeof(RF2850RegTable) / sizeof(RTMP_RF_REGS));
#endif /* defined(RT28xx) || defined(RT2883) */

#ifdef RTMP_INTERNAL_TX_ALC
/* The desired TSSI over CCK */
CHAR desiredTSSIOverCCK[4] = {0};

/* The desired TSSI over OFDM */
CHAR desiredTSSIOverOFDM[8] = {0};

/* The desired TSSI over HT */
CHAR desiredTSSIOverHT[16] = {0};

/* The desired TSSI over HT using STBC */
CHAR desiredTSSIOverHTUsingSTBC[8] = {0};
#endif /* RTMP_INTERNAL_TX_ALC */




/* private function prototype */

static VOID RxSensitivityTuning(
	IN PRTMP_ADAPTER		pAd);

static VOID ChipResumeMsduTransmission(
	IN	PRTMP_ADAPTER		pAd);

#ifdef CONFIG_STA_SUPPORT
static UCHAR ChipStaBBPAdjust(
	IN PRTMP_ADAPTER		pAd,
	IN CHAR					Rssi,
	IN UCHAR				R66);
#endif /* CONFIG_STA_SUPPORT */

static VOID ChipBBPAdjust(
	IN RTMP_ADAPTER			*pAd);

static VOID ChipSwitchChannel(
	IN PRTMP_ADAPTER 		pAd,
	IN UCHAR				Channel,
	IN BOOLEAN				bScan);

#ifdef RTMP_INTERNAL_TX_ALC
static VOID InitDesiredTSSITableDefault(
	IN PRTMP_ADAPTER		pAd);

static VOID AsicTxAlcGetAutoAgcOffset(
	IN PRTMP_ADAPTER		pAd,
	IN PCHAR			pDeltaPwr,
	IN PCHAR			pTotalDeltaPwr,
	IN PCHAR			pAgcCompensate,
	IN PUCHAR			pBbpR49);
#endif /* RTMP_INTERNAL_TX_ALC */

static VOID AsicSetAGCInitValue(
	IN PRTMP_ADAPTER		pAd,
	IN UCHAR				BandWidth);

static VOID AsicAntennaDefaultReset(
	IN PRTMP_ADAPTER		pAd,
	IN EEPROM_ANTENNA_STRUC	*pAntenna);

static VOID NetDevNickNameInit(
	IN PRTMP_ADAPTER		pAd);




/*
========================================================================
Routine Description:
	Initialize specific beacon frame architecture.

Arguments:
	pAd				- WLAN control block pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpChipBcnSpecInit(
	IN RTMP_ADAPTER			*pAd)
{
#ifdef MBSS_SUPPORT
#ifdef SPECIFIC_BCN_BUF_SUPPORT
	RTMP_CHIP_CAP *pChipCap = &pAd->chipCap;


	pChipCap->FlgIsSupSpecBcnBuf = TRUE;
	pChipCap->BcnMaxHwNum = 16;
	pChipCap->WcidHwRsvNum = 255;

/* 	In 16-MBSS support mode, if AP-Client is enabled, 
	the last 8-MBSS would be occupied for AP-Client using. */
#ifdef APCLI_SUPPORT
	pChipCap->BcnMaxNum = (8 - MAX_MESH_NUM);
#else
	pChipCap->BcnMaxNum = (16 - MAX_MESH_NUM);
#endif /* APCLI_SUPPORT */

	pChipCap->BcnMaxHwSize = 0x2000;

	/* It's allowed to use the higher(secordary) 8KB shared memory */
	pChipCap->BcnBase[0] = 0x4000;
	pChipCap->BcnBase[1] = 0x4200;
	pChipCap->BcnBase[2] = 0x4400;
	pChipCap->BcnBase[3] = 0x4600;
	pChipCap->BcnBase[4] = 0x4800;
	pChipCap->BcnBase[5] = 0x4A00;
	pChipCap->BcnBase[6] = 0x4C00;
	pChipCap->BcnBase[7] = 0x4E00;
	pChipCap->BcnBase[8] = 0x5000;
	pChipCap->BcnBase[9] = 0x5200;
	pChipCap->BcnBase[10] = 0x5400;
	pChipCap->BcnBase[11] = 0x5600;
	pChipCap->BcnBase[12] = 0x5800;
	pChipCap->BcnBase[13] = 0x5A00;
	pChipCap->BcnBase[14] = 0x5C00;
	pChipCap->BcnBase[15] = 0x5E00;

	DBGPRINT(RT_DEBUG_TRACE, ("<<<<< Beacon Spec Information: >>>>>\n"));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnMaxHwNum = \t%d\n", pChipCap->BcnMaxHwNum));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnMaxNum = \t%d\n", pChipCap->BcnMaxNum));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnMaxHwSize = \t0x%x\n", pChipCap->BcnMaxHwSize));
	DBGPRINT(RT_DEBUG_TRACE, ("\tWcidHwRsvNum = \t%d\n", pChipCap->WcidHwRsvNum));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnBase[0] = \t0x%x\n", pChipCap->BcnBase[0]));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnBase[8] = \t0x%x\n", pChipCap->BcnBase[8]));
#endif /* SPECIFIC_BCN_BUF_SUPPORT */
#endif /* MBSS_SUPPORT */
}


/*
========================================================================
Routine Description:
	Initialize normal beacon frame architecture.

Arguments:
	pAd				- WLAN control block pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpChipBcnInit(
	IN RTMP_ADAPTER *pAd)
{
	RTMP_CHIP_CAP *pChipCap = &pAd->chipCap;


	pChipCap->FlgIsSupSpecBcnBuf = FALSE;
	pChipCap->BcnMaxHwNum = 8;
	pChipCap->BcnMaxNum = (pChipCap->BcnMaxHwNum - MAX_MESH_NUM - MAX_APCLI_NUM);
	pChipCap->BcnMaxHwSize = 0x1000;

	pChipCap->BcnBase[0] = 0x7800;
	pChipCap->BcnBase[1] = 0x7A00;
	pChipCap->BcnBase[2] = 0x7C00;
	pChipCap->BcnBase[3] = 0x7E00;
	pChipCap->BcnBase[4] = 0x7200;
	pChipCap->BcnBase[5] = 0x7400;
	pChipCap->BcnBase[6] = 0x5DC0;
	pChipCap->BcnBase[7] = 0x5BC0;

	/* If the MAX_MBSSID_NUM is larger than 6, */
	/* it shall reserve some WCID space(wcid 222~253) for beacon frames. */
	/* -	these wcid 238~253 are reserved for beacon#6(ra6).*/
	/* -	these wcid 222~237 are reserved for beacon#7(ra7).*/
	if (pChipCap->BcnMaxNum == 8)
		pChipCap->WcidHwRsvNum = 222;
	else if (pChipCap->BcnMaxNum == 7)
		pChipCap->WcidHwRsvNum = 238;
	else
		pChipCap->WcidHwRsvNum = 255;

	DBGPRINT(RT_DEBUG_TRACE, ("<<<<< Beacon Information: >>>>>\n"));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnMaxHwNum = \t%d\n", pChipCap->BcnMaxHwNum));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnMaxNum = \t%d\n", pChipCap->BcnMaxNum));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnMaxHwSize = \t0x%x\n", pChipCap->BcnMaxHwSize));
	DBGPRINT(RT_DEBUG_TRACE, ("\tWcidHwRsvNum = \t%d\n", pChipCap->WcidHwRsvNum));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnBase[0] = \t0x%x\n", pChipCap->BcnBase[0]));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnBase[1] = \t0x%x\n", pChipCap->BcnBase[1]));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnBase[2] = \t0x%x\n", pChipCap->BcnBase[2]));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnBase[3] = \t0x%x\n", pChipCap->BcnBase[3]));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnBase[4] = \t0x%x\n", pChipCap->BcnBase[4]));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnBase[5] = \t0x%x\n", pChipCap->BcnBase[5]));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnBase[6] = \t0x%x\n", pChipCap->BcnBase[6]));
	DBGPRINT(RT_DEBUG_TRACE, ("\tBcnBase[7] = \t0x%x\n", pChipCap->BcnBase[7]));
}


/*
========================================================================
Routine Description:
	Initialize chip related information.

Arguments:
	pCB				- WLAN control block pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpChipOpsHook(
	IN VOID			*pCB)
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)pCB;
	RTMP_CHIP_OP *pChipOps = &pAd->chipOps;
	RTMP_CHIP_CAP *pChipCap = &pAd->chipCap;
	EEPROM_ANTENNA_STRUC Antenna;
	USHORT value;


	/* get RF IC type */
	RT28xx_EEPROM_READ16(pAd, EEPROM_NIC1_OFFSET, value);
	pAd->EEPROMDefaultValue[EEPROM_NIC_CFG1_OFFSET] = value;
	Antenna.word = pAd->EEPROMDefaultValue[EEPROM_NIC_CFG1_OFFSET];
	pAd->RfIcType = (UCHAR) Antenna.field.RfIcType;
	DBGPRINT(RT_DEBUG_TRACE, ("RF IC Type: %d\n", pAd->RfIcType));

	/* save the antenna for future use */
	pAd->Antenna.word = Antenna.word;


	/* init default value whatever chipsets */
	/* default pChipOps content will be 0x00 */
	pChipCap->bbpRegTbSize = 0;
	pChipCap->MaxNumOfRfId = 31;
	pChipCap->MaxNumOfBbpId = 136;
	pChipCap->SnrFormula = SNR_FORMULA1;
	pChipCap->RfReg17WtMethod = RF_REG_WT_METHOD_NONE;

#ifdef RTMP_INTERNAL_TX_ALC
	pChipCap->TxAlcTxPowerUpperBound = 45;
	pChipCap->TxAlcMaxMCS = 8;
#endif /* RTMP_INTERNAL_TX_ALC */

#ifdef RTMP_EFUSE_SUPPORT
	pChipCap->EFUSE_USAGE_MAP_START = 0x2d0;
	pChipCap->EFUSE_USAGE_MAP_END = 0x2fc;      
	pChipCap->EFUSE_USAGE_MAP_SIZE = 45;
	DBGPRINT(RT_DEBUG_ERROR, ("(Efuse for 3062/3562/3572) Size=0x%x [%x-%x] \n",pAd->chipCap.EFUSE_USAGE_MAP_SIZE,pAd->chipCap.EFUSE_USAGE_MAP_START,pAd->chipCap.EFUSE_USAGE_MAP_END));
#endif /* RTMP_EFUSE_SUPPORT */

	pChipCap->FlgIsVcoReCalSup = FALSE;

	RtmpChipBcnInit(pAd);


	pChipOps->RxSensitivityTuning = RxSensitivityTuning;
	pChipOps->ChipResumeMsduTransmission = ChipResumeMsduTransmission;
#ifdef CONFIG_STA_SUPPORT
	pChipOps->ChipStaBBPAdjust = ChipStaBBPAdjust;
#endif /* CONFIG_STA_SUPPORT */
	pChipOps->ChipBBPAdjust = ChipBBPAdjust;
	pChipOps->ChipSwitchChannel = ChipSwitchChannel;

#ifdef RTMP_INTERNAL_TX_ALC
	pChipOps->InitDesiredTSSITable = InitDesiredTSSITableDefault;
	pChipOps->AsicTxAlcGetAutoAgcOffset = AsicTxAlcGetAutoAgcOffset;
#endif /* RTMP_INTERNAL_TX_ALC */

	pChipOps->RTMPSetAGCInitValue = AsicSetAGCInitValue;

	pChipOps->AsicAntennaDefaultReset = AsicAntennaDefaultReset;
	pChipOps->NetDevNickNameInit = NetDevNickNameInit;
	/* Init value. If pChipOps->AsicResetBbpAgent==NULL, "AsicResetBbpAgent" as default. If your chipset has specific routine, please re-hook it at self init function */
	pChipOps->AsicResetBbpAgent = NULL;

#ifdef RT28xx
	pChipOps->ChipSwitchChannel = RT28xx_ChipSwitchChannel;
#endif /* RT28xx */

	/* We depends on RfICType and MACVersion to assign the corresponding operation callbacks. */


#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	if (IS_RT5390(pAd))
	{
		RT5390_Init(pAd);
	}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */






#ifdef RT30xx


	if (IS_RT30xx(pAd))
	{
		if (IS_RT3390(pAd))
			RT33xx_Init(pAd);
		else
		RT30xx_Init(pAd);
	}
#endif /* RT30xx */

	DBGPRINT(RT_DEBUG_TRACE, ("Chip specific bbpRegTbSize=%d!\n", pChipCap->bbpRegTbSize));
	
}





static VOID RxSensitivityTuning(
	IN PRTMP_ADAPTER		pAd)
{
	UCHAR R66;


	R66 = 0x26 + GET_LNA_GAIN(pAd);
#ifdef RALINK_ATE
	if (ATE_ON(pAd))
	{
		ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, (0x26 + GET_LNA_GAIN(pAd)));
	}
	else
#endif /* RALINK_ATE */
	{
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, (0x26 + GET_LNA_GAIN(pAd)));
	}
	DBGPRINT(RT_DEBUG_TRACE,("turn off R17 tuning, restore to 0x%02x\n", R66));
}


/*
	========================================================================

	Routine Description:
		Resume MSDU transmission
		
	Arguments:
		pAd 	Pointer to our adapter
		
	Return Value:
		None
		
	IRQL = DISPATCH_LEVEL
	
	Note:
	
	========================================================================
*/
static VOID ChipResumeMsduTransmission(
	IN	PRTMP_ADAPTER		pAd)
{
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, pAd->BbpTuning.R66CurrentValue);
}


#ifdef CONFIG_STA_SUPPORT
static UCHAR ChipStaBBPAdjust(
	IN PRTMP_ADAPTER		pAd,
	IN CHAR					Rssi,
	IN UCHAR				R66)
{
	UCHAR	OrigR66Value = 0;/*, R66UpperBound = 0x30, R66LowerBound = 0x30;*/

	if (pAd->LatchRfRegs.Channel <= 14)
	{	/*BG band*/
		{
			if (Rssi > RSSI_FOR_MID_LOW_SENSIBILITY)
			{
				R66 = (0x2E + GET_LNA_GAIN(pAd)) + 0x10;
				if (OrigR66Value != R66)
				{
					RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
				}
			}
			else
			{
				R66 = 0x2E + GET_LNA_GAIN(pAd);
				if (OrigR66Value != R66)
				{
					RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
				}
			}
		}
	}
	else
	{	/*A band*/
		if (pAd->CommonCfg.BBPCurrentBW == BW_20)
		{
			if (Rssi > RSSI_FOR_MID_LOW_SENSIBILITY)
			{
				R66 = 0x32 + (GET_LNA_GAIN(pAd)*5)/3 + 0x10;

				if (OrigR66Value != R66)
				{
						RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
				}
			}
			else
			{
				R66 = 0x32 + (GET_LNA_GAIN(pAd)*5)/3;

				if (OrigR66Value != R66)
				{
						RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
				}
			}
		}
		else
		{
			if (Rssi > RSSI_FOR_MID_LOW_SENSIBILITY)
			{
				R66 = 0x3A + (GET_LNA_GAIN(pAd)*5)/3 + 0x10;

				if (OrigR66Value != R66)
				{
						RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
				}
			}
			else
			{
				R66 = 0x3A + (GET_LNA_GAIN(pAd)*5)/3;

				if (OrigR66Value != R66)
				{
						RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
				}
			}
		}
	}

	return R66;
}
#endif /* CONFIG_STA_SUPPORT */


static VOID ChipBBPAdjust(
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

		if (pAd->CommonCfg.Channel > 14)
		{ 	/* request by Gary 20070208 for middle and long range A Band*/
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x48);
		}
		else
		{	/* request by Gary 20070208 for middle and long range G Band*/
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x38);
		}	
		/* */
		if (pAd->MACVersion == 0x28600100)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x1A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x16);
		}
		else
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x12);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x10);
		}	

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
		
		if (pAd->CommonCfg.Channel > 14)
		{ 	/* request by Gary 20070208 for middle and long range A Band*/
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x48);
		}
		else
		{ 	/* request by Gary 20070208 for middle and long range G band*/
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x38);
		}	
	
		
		if (pAd->MACVersion == 0x28600100)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x1A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x16);
		}
		else
		{	
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x12);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x10);
		}
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
		if (pAd->CommonCfg.Channel > 14)
		{	 /* request by Gary 20070208*/
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x40);
		}	
		else
		{	/* request by Gary 20070208*/
			/*RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x30);*/
			/* request by Brian 20070306*/
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x38);
		}	
				 
		if (pAd->MACVersion == 0x28600100)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x16);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x08);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x11);
		}
		else
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x12);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0a);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x10);
		}

#ifdef DOT11_N_SUPPORT
		DBGPRINT(RT_DEBUG_TRACE, ("ApStartUp : 20MHz, ChannelWidth=%d, Channel=%d, ExtChanOffset=%d(%d) \n",
										pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth, 
										pAd->CommonCfg.Channel, 
										pAd->CommonCfg.RegTransmitSetting.field.EXTCHA,
										pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset));
#endif /* DOT11_N_SUPPORT */
	}
	
	if (pAd->CommonCfg.Channel > 14)
	{	/* request by Gary 20070208 for middle and long range A Band*/
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, 0x1D);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, 0x1D);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, 0x1D);
		/*RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0x1D);*/
	}
	else
	{ 	/* request by Gary 20070208 for middle and long range G band*/
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, 0x2D);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, 0x2D);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, 0x2D);
			/*RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0x2D);*/
	}	
}


static VOID ChipSwitchChannel(
	IN PRTMP_ADAPTER 		pAd,
	IN UCHAR				Channel,
	IN BOOLEAN				bScan) 
{
	CHAR    TxPwer = 0, TxPwer2 = DEFAULT_RF_TX_POWER; /*Bbp94 = BBPR94_DEFAULT, TxPwer2 = DEFAULT_RF_TX_POWER;*/
	UCHAR	index;
	UINT32 	Value = 0; /*BbpReg, Value;*/
	UCHAR 	RFValue;
	UINT32 i = 0;

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
			break;
		}
	}

	if (index == MAX_NUM_OF_CHANNELS)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("AsicSwitchChannel: Can't find the Channel#%d \n", Channel));
	}

	{
#if defined(RT28xx)
		ULONG	R2 = 0, R3 = DEFAULT_RF_TX_POWER, R4 = 0;
		RTMP_RF_REGS *RFRegTable;

		RFRegTable = RF2850RegTable;
#endif /* Rdefined(RT28xx) */
	
		switch (pAd->RfIcType)
		{
#if defined(RT28xx)
#if defined(RT28xx)
			case RFIC_2820:
			case RFIC_2850:
			case RFIC_2720:
			case RFIC_2750:
#endif /* defined(RT28xx) */
				for (index = 0; index < NUM_OF_2850_CHNL; index++)
				{
					if (Channel == RFRegTable[index].Channel)
					{
						R2 = RFRegTable[index].R2;
						if (pAd->Antenna.field.TxPath == 1)
						{
							R2 |= 0x4000;	/* If TXpath is 1, bit 14 = 1;*/
						}

						if (pAd->Antenna.field.RxPath == 2
)
						{
							R2 |= 0x40;	/* write 1 to off Rxpath.*/
						}
						else if (pAd->Antenna.field.RxPath == 1
)
						{
							R2 |= 0x20040;	/* write 1 to off RxPath*/
						}

						if (Channel > 14)
						{
							/* initialize R3, R4*/
							R3 = (RFRegTable[index].R3 & 0xffffc1ff);
							R4 = (RFRegTable[index].R4 & (~0x001f87c0)) | (pAd->RfFreqOffset << 15);

							/* 5G band power range: 0xF9~0X0F, TX0 Reg3 bit9/TX1 Reg4 bit6="0" means the TX power reduce 7dB*/
							/* R3*/
							if ((TxPwer >= -7) && (TxPwer < 0))
							{
								TxPwer = (7+TxPwer);

								/* TxPwer is not possible larger than 15 */
/*								TxPwer = (TxPwer > 0xF) ? (0xF) : (TxPwer);*/

								R3 |= (TxPwer << 10);
								DBGPRINT(RT_DEBUG_TRACE, ("AsicSwitchChannel: TxPwer=%d \n", TxPwer));
							}
							else
							{
								TxPwer = (TxPwer > 0xF) ? (0xF) : (TxPwer);
								R3 |= (TxPwer << 10) | (1 << 9);
							}

							/* R4*/
							if ((TxPwer2 >= -7) && (TxPwer2 < 0))
							{
								TxPwer2 = (7+TxPwer2);
								R4 |= (TxPwer2 << 7);
								DBGPRINT(RT_DEBUG_TRACE, ("AsicSwitchChannel: TxPwer2=%d \n", TxPwer2));
							}
							else
							{
								TxPwer2 = (TxPwer2 > 0xF) ? (0xF) : (TxPwer2);
								R4 |= (TxPwer2 << 7) | (1 << 6);
							}                        
						}
						else
						{
							R3 = (RFRegTable[index].R3 & 0xffffc1ff) | (TxPwer << 9); /* set TX power0*/
							R4 = (RFRegTable[index].R4 & (~0x001f87c0)) | (pAd->RfFreqOffset << 15) | (TxPwer2 <<6);/* Set freq Offset & TxPwr1*/
						}

						/* Based on BBP current mode before changing RF channel.*/
						if (!bScan && (pAd->CommonCfg.BBPCurrentBW == BW_40)
							)
						{
							R4 |=0x200000;
						}

						/* Update variables*/
						pAd->LatchRfRegs.Channel = Channel;
						pAd->LatchRfRegs.R1 = RFRegTable[index].R1;
						pAd->LatchRfRegs.R2 = R2;
						pAd->LatchRfRegs.R3 = R3;
						pAd->LatchRfRegs.R4 = R4;

						/* Set RF value 1's set R3[bit2] = [0]*/
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
						RTMP_RF_IO_WRITE32(pAd, (pAd->LatchRfRegs.R3 & (~0x04)));
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);

						RTMPusecDelay(200);

						/* Set RF value 2's set R3[bit2] = [1]*/
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
						RTMP_RF_IO_WRITE32(pAd, (pAd->LatchRfRegs.R3 | 0x04));
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);

						RTMPusecDelay(200);

						/* Set RF value 3's set R3[bit2] = [0]*/
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
						RTMP_RF_IO_WRITE32(pAd, (pAd->LatchRfRegs.R3 & (~0x04)));
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);

						break;
					}
				}

				DBGPRINT(RT_DEBUG_TRACE, ("SwitchChannel#%d(RF=%d, Pwr0=%lu, Pwr1=%lu, %dT) to , R1=0x%08x, R2=0x%08x, R3=0x%08x, R4=0x%08x\n",
							  Channel, 
							  pAd->RfIcType, 
							  (R3 & 0x00003e00) >> 9,
							  (R4 & 0x000007c0) >> 6,
							  pAd->Antenna.field.TxPath,
							  pAd->LatchRfRegs.R1, 
							  pAd->LatchRfRegs.R2, 
							  pAd->LatchRfRegs.R3, 
							  pAd->LatchRfRegs.R4));
			
				break;
#endif /* defined(RT28xx) */
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
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0);/*(0x44 - GET_LNA_GAIN(pAd)));	 According the Rory's suggestion to solve the middle range issue.*/

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


#ifdef RTMP_INTERNAL_TX_ALC
static VOID AsicTxAlcGetAutoAgcOffset(
	IN PRTMP_ADAPTER		pAd,
	IN PCHAR				pDeltaPwr,
	IN PCHAR				pTotalDeltaPwr,
	IN PCHAR				pAgcCompensate,
	IN PUCHAR				pBbpR49)
{
	extern TX_POWER_TUNING_ENTRY_STRUCT *TxPowerTuningTable;
	BBP_R49_STRUC BbpR49;
	CHAR desiredTSSI = 0, currentTSSI = 0;
	PTX_POWER_TUNING_ENTRY_STRUCT pTxPowerTuningEntry = NULL;
	UCHAR RFValue = 0;
	CHAR DeltaPwr = 0;
	CHAR TotalDeltaPower = 0; // (non-positive number) including the transmit power controlled by the MAC and the BBP R1


	BbpR49.byte = 0;

	if (pAd->Mlme.OneSecPeriodicRound % 4 == 0)
	{
		desiredTSSI = GetDesiredTSSI(pAd);

		if(desiredTSSI == -1) {
			goto LabelFail;
		}

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49.byte);
		currentTSSI = BbpR49.field.TSSI;

		if (desiredTSSI > currentTSSI)
		{
			pAd->TxPowerCtrl.idxTxPowerTable++;
		}

		if (desiredTSSI < currentTSSI)
		{
			pAd->TxPowerCtrl.idxTxPowerTable--;
		}

		if (pAd->TxPowerCtrl.idxTxPowerTable < LOWERBOUND_TX_POWER_TUNING_ENTRY)
		{
			pAd->TxPowerCtrl.idxTxPowerTable = LOWERBOUND_TX_POWER_TUNING_ENTRY;
		}

		if (pAd->TxPowerCtrl.idxTxPowerTable >= UPPERBOUND_TX_POWER_TUNING_ENTRY(pAd))
		{
			pAd->TxPowerCtrl.idxTxPowerTable = UPPERBOUND_TX_POWER_TUNING_ENTRY(pAd);
		}

		//
		// Valide pAd->TxPowerCtrl.idxTxPowerTable: -30 ~ 45
		//

		pTxPowerTuningEntry = &TxPowerTuningTable[pAd->TxPowerCtrl.idxTxPowerTable + TX_POWER_TUNING_ENTRY_OFFSET]; // zero-based array
		pAd->TxPowerCtrl.RF_R12_Value = pTxPowerTuningEntry->RF_R12_Value;
		pAd->TxPowerCtrl.MAC_PowerDelta = pTxPowerTuningEntry->MAC_PowerDelta;

		//
		// Tx power adjustment over RF
		//

		{
			RT30xxReadRFRegister(pAd, RF_R12, (PUCHAR)(&RFValue));
			RFValue = ((RFValue & 0xE0) | pAd->TxPowerCtrl.RF_R12_Value);
			RT30xxWriteRFRegister(pAd, RF_R12, (UCHAR)(RFValue));
		}

		//
		// Tx power adjustment over MAC
		//
		TotalDeltaPower = pAd->TxPowerCtrl.MAC_PowerDelta;

		DBGPRINT(RT_DEBUG_TRACE, ("%s: desiredTSSI = %d, currentTSSI = %d, idxTxPowerTable = %d, {RF_R12_Value = %d, MAC_PowerDelta = %d}\n", 
			__FUNCTION__, 
			desiredTSSI, 
			currentTSSI, 
			pAd->TxPowerCtrl.idxTxPowerTable, 
			pTxPowerTuningEntry->RF_R12_Value, 
			pTxPowerTuningEntry->MAC_PowerDelta));
	}

LabelFail:
	*pBbpR49 = BbpR49.byte;
	*pDeltaPwr = DeltaPwr;
	*pTotalDeltaPwr = TotalDeltaPower;
}
#endif // RTMP_INTERNAL_TX_ALC //


static VOID AsicSetAGCInitValue(
	IN PRTMP_ADAPTER		pAd,
	IN UCHAR				BandWidth)
{
	UCHAR	R66 = 0x30;
	
	if (pAd->LatchRfRegs.Channel <= 14)
	{	// BG band
#ifdef RT30xx
		/* Gary was verified Amazon AP and find that RT307x has BBP_R66 invalid default value */
		if (IS_RT3070(pAd)||IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd) || IS_RT3593(pAd))
		{
			R66 = 0x1C + 2*GET_LNA_GAIN(pAd);
			{
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
			}
		}
		else
#endif // RT30xx //
		{
			R66 = 0x2E + GET_LNA_GAIN(pAd);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
		}
	}
	else
	{	//A band
		{	
			if (BandWidth == BW_20)
			{
				R66 = (UCHAR)(0x32 + (GET_LNA_GAIN(pAd)*5)/3);

				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
			}
#ifdef DOT11_N_SUPPORT
			else
			{
				R66 = (UCHAR)(0x3A + (GET_LNA_GAIN(pAd)*5)/3);

				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
			}
#endif // DOT11_N_SUPPORT //
		}		
	}

}


#ifdef HW_ANTENNA_DIVERSITY_SUPPORT
UINT32 SetHWAntennaDivsersity(
	IN PRTMP_ADAPTER		pAd,
	IN BOOLEAN				Enable)
{
	if (Enable == TRUE)
	{
		UINT8 BBPValue = 0, RFValue = 0;
		USHORT value;

		// RF_R29 bit7:6
		RT28xx_EEPROM_READ16(pAd, EEPROM_RSSI_GAIN, value);
		
		RT30xxReadRFRegister(pAd, RF_R29, &RFValue);
		RFValue &= 0x3f; // clear bit7:6
		RFValue |= (value << 6);			
		RT30xxWriteRFRegister(pAd, RF_R29, RFValue);

		// BBP_R47 bit7=1
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BBPValue);
		BBPValue |= 0x80;
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BBPValue);
	
		BBPValue = 0xbe;			
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R150, BBPValue);
		BBPValue = 0xb0;			
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R151, BBPValue);
		BBPValue = 0x23;			
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R152, BBPValue);
		BBPValue = 0x3a;			
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R153, BBPValue);
		BBPValue = 0x10;			
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R154, BBPValue);
		BBPValue = 0x3b;			
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R155, BBPValue);
		BBPValue = 0x04;			
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R253, BBPValue);

		DBGPRINT(RT_DEBUG_TRACE, ("HwAnDi> Enable!\n"));
	}
	else
	{
		UINT8 BBPValue = 0;

		/*
			main antenna: BBP_R152 bit7=1
			aux antenna: BBP_R152 bit7=0
		 */
		if (pAd->FixDefaultAntenna == 0)
		{
			/* fix to main antenna */
			/* do not care BBP R153, R155, R253 */
			BBPValue = 0x3e;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R150, BBPValue);
			BBPValue = 0x30;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R151, BBPValue);
			BBPValue = 0x23;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R152, BBPValue);
			BBPValue = 0x00;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R154, BBPValue);
		}
		else
		{
			/* fix to aux antenna */
			/* do not care BBP R153, R155, R253 */
			BBPValue = 0x3e;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R150, BBPValue);
			BBPValue = 0x30;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R151, BBPValue);
			BBPValue = 0xa3;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R152, BBPValue);
			BBPValue = 0x00;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R154, BBPValue);
		}

		DBGPRINT(RT_DEBUG_TRACE, ("HwAnDi> Disable!\n"));
	}

	return 0;
}
#endif // HW_ANTENNA_DIVERSITY_SUPPORT // 


#ifdef RTMP_INTERNAL_TX_ALC
//
// Initialize the desired TSSI table
//
// Parameters
//	pAd: The adapter data structure
//
// Return Value:
//	None
//
static VOID InitDesiredTSSITableDefault(
	IN PRTMP_ADAPTER			pAd)
{
	UCHAR TSSIBase = 0; // The TSSI over OFDM 54Mbps
	USHORT TSSIStepOver2dot4G = 0; // The TSSI value/step (0.5 dB/unit)
	UCHAR RFValue = 0;
	BBP_R49_STRUC BbpR49 = {{0}};
	ULONG i = 0;
	USHORT TxPower = 0, TxPowerOFDM54 = 0, temp = 0;
	UINT32 MaxMCS;

	if (pAd->TxPowerCtrl.bInternalTxALC == FALSE)
	{
		return;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("---> %s\n", __FUNCTION__));

	RT28xx_EEPROM_READ16(pAd, EEPROM_TSSI_OVER_OFDM_54, temp);
	TSSIBase = (temp & 0x000F);
	
	RT28xx_EEPROM_READ16(pAd, (EEPROM_TSSI_STEP_OVER_2DOT4G - 1), TSSIStepOver2dot4G);
	TSSIStepOver2dot4G = (0x000F & (TSSIStepOver2dot4G >> 8));

	RT28xx_EEPROM_READ16(pAd, (EEPROM_OFDM_MCS6_MCS7 - 1), TxPowerOFDM54);
	TxPowerOFDM54 = (0x000F & (TxPowerOFDM54 >> 8));

	DBGPRINT(RT_DEBUG_TRACE, ("%s: TSSIBase = %d, TSSIStepOver2dot4G = %d, TxPowerOFDM54 = %d\n", 
		__FUNCTION__, 
		TSSIBase, 
		TSSIStepOver2dot4G, 
		TxPowerOFDM54));

	//
	// The desired TSSI over CCK
	//
	RT28xx_EEPROM_READ16(pAd, EEPROM_CCK_MCS0_MCS1, TxPower);
	TxPower = (TxPower & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xDE = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverCCK[MCS_0] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)) + 6);
	desiredTSSIOverCCK[MCS_1] = desiredTSSIOverCCK[MCS_0];
	RT28xx_EEPROM_READ16(pAd, (EEPROM_CCK_MCS2_MCS3 - 1), TxPower);
	TxPower = ((TxPower >> 8) & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xDF = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverCCK[MCS_2] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)) + 6);
	desiredTSSIOverCCK[MCS_3] = desiredTSSIOverCCK[MCS_2];

	//
	// Boundary verification: the desired TSSI value
	//
	for (i = 0; i < 4; i++) // CCK: MCS 0 ~ MCS 3
	{
		if (desiredTSSIOverCCK[i] < 0x00)
		{
			desiredTSSIOverCCK[i] = 0x00;
		}
		else if (desiredTSSIOverCCK[i] > 0x1F)
		{
			desiredTSSIOverCCK[i] = 0x1F;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("%s: desiredTSSIOverCCK[0] = %d, desiredTSSIOverCCK[1] = %d, desiredTSSIOverCCK[2] = %d, desiredTSSIOverCCK[3] = %d\n", 
		__FUNCTION__, 
		desiredTSSIOverCCK[0], 
		desiredTSSIOverCCK[1], 
		desiredTSSIOverCCK[2], 
		desiredTSSIOverCCK[3]));

	//
	// The desired TSSI over OFDM
	//
	RT28xx_EEPROM_READ16(pAd, EEPROM_OFDM_MCS0_MCS1, TxPower);
	TxPower = (TxPower & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xE0 = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverOFDM[MCS_0] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverOFDM[MCS_1] = desiredTSSIOverOFDM[MCS_0];
	RT28xx_EEPROM_READ16(pAd, (EEPROM_OFDM_MCS2_MCS3 - 1), TxPower);
	TxPower = ((TxPower >> 8) & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xE1 = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverOFDM[MCS_2] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverOFDM[MCS_3] = desiredTSSIOverOFDM[MCS_2];
	RT28xx_EEPROM_READ16(pAd, EEPROM_OFDM_MCS4_MCS5, TxPower);
	TxPower = (TxPower & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xE2 = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverOFDM[MCS_4] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverOFDM[MCS_5] = desiredTSSIOverOFDM[MCS_4];
	desiredTSSIOverOFDM[MCS_6] = TSSIBase;
	desiredTSSIOverOFDM[MCS_7] = TSSIBase;

	//
	// Boundary verification: the desired TSSI value
	//
	for (i = 0; i < 8; i++) // OFDM: MCS 0 ~ MCS 7
	{
		if (desiredTSSIOverOFDM[i] < 0x00)
		{
			desiredTSSIOverOFDM[i] = 0x00;
		}
		else if (desiredTSSIOverOFDM[i] > 0x1F)
		{
			desiredTSSIOverOFDM[i] = 0x1F;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("%s: desiredTSSIOverOFDM[0] = %d, desiredTSSIOverOFDM[1] = %d, desiredTSSIOverOFDM[2] = %d, desiredTSSIOverOFDM[3] = %d\n", 
		__FUNCTION__, 
		desiredTSSIOverOFDM[0], 
		desiredTSSIOverOFDM[1], 
		desiredTSSIOverOFDM[2], 
		desiredTSSIOverOFDM[3]));
	DBGPRINT(RT_DEBUG_TRACE, ("%s: desiredTSSIOverOFDM[4] = %d, desiredTSSIOverOFDM[5] = %d, desiredTSSIOverOFDM[6] = %d, desiredTSSIOverOFDM[7] = %d\n", 
		__FUNCTION__, 
		desiredTSSIOverOFDM[4], 
		desiredTSSIOverOFDM[5], 
		desiredTSSIOverOFDM[6], 
		desiredTSSIOverOFDM[7]));

	//
	// The desired TSSI over HT
	//
	RT28xx_EEPROM_READ16(pAd, EEPROM_HT_MCS0_MCS1, TxPower);
	TxPower = (TxPower & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xE4 = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverHT[MCS_0] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverHT[MCS_1] = desiredTSSIOverHT[MCS_0];
	RT28xx_EEPROM_READ16(pAd, (EEPROM_HT_MCS2_MCS3 - 1), TxPower);
	TxPower = ((TxPower >> 8) & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xE5 = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverHT[MCS_2] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverHT[MCS_3] = desiredTSSIOverHT[MCS_2];
	RT28xx_EEPROM_READ16(pAd, EEPROM_HT_MCS4_MCS5, TxPower);
	TxPower = (TxPower & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xE6 = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverHT[MCS_4] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverHT[MCS_5] = desiredTSSIOverHT[MCS_4];
	RT28xx_EEPROM_READ16(pAd, (EEPROM_HT_MCS6_MCS7 - 1), TxPower);
	TxPower = ((TxPower >> 8) & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xE7 = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverHT[MCS_6] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverHT[MCS_7] = desiredTSSIOverHT[MCS_6];

	MaxMCS = 8;

	if (pAd->chipCap.TxAlcMaxMCS > 8)
	{
		MaxMCS = 16;
		RT28xx_EEPROM_READ16(pAd, EEPROM_HT_MCS8_MCS9, TxPower);
		TxPower = (TxPower & 0x000F);
		DBGPRINT(RT_DEBUG_TRACE, ("EEPROM_HT_MCS9_MCS9(0xE8) = 0x%X\n", TxPower));
		desiredTSSIOverHT[MCS_8] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
		desiredTSSIOverHT[MCS_9] = desiredTSSIOverHT[MCS_8];

		RT28xx_EEPROM_READ16(pAd, (EEPROM_HT_MCS10_MCS11-1), TxPower);
		TxPower = ((TxPower >> 8) & 0x000F);
		DBGPRINT(RT_DEBUG_TRACE, ("EEPROM_HT_MCS10_MCS11(0xE9) = 0x%X\n", TxPower));
		desiredTSSIOverHT[MCS_10] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
		desiredTSSIOverHT[MCS_11] = desiredTSSIOverHT[MCS_10];

		RT28xx_EEPROM_READ16(pAd, EEPROM_HT_MCS12_MCS13, TxPower);
		TxPower = (TxPower & 0x000F);
		DBGPRINT(RT_DEBUG_TRACE, ("EEPROM_HT_MCS12_MCS13(0xEA) = 0x%X\n", TxPower));
		desiredTSSIOverHT[MCS_12] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
		desiredTSSIOverHT[MCS_13] = desiredTSSIOverHT[MCS_12];

		RT28xx_EEPROM_READ16(pAd, (EEPROM_HT_MCS14_MCS15-1), TxPower);
		TxPower = ((TxPower >> 8) & 0x000F);
		DBGPRINT(RT_DEBUG_TRACE, ("EEPROM_HT_MCS14_MCS15(0xEB) = 0x%X\n", TxPower));
		desiredTSSIOverHT[MCS_14] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
		desiredTSSIOverHT[MCS_15] = desiredTSSIOverHT[MCS_14]-1;
	}

	//
	// Boundary verification: the desired TSSI value
	//
	for (i = 0; i < MaxMCS; i++) // HT: MCS 0 ~ MCS 7 or MCS 15
	{
		if (desiredTSSIOverHT[i] < 0x00)
		{
			desiredTSSIOverHT[i] = 0x00;
		}
		else if (desiredTSSIOverHT[i] > 0x1F)
		{
			desiredTSSIOverHT[i] = 0x1F;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("%s: desiredTSSIOverHT[0] = %d, desiredTSSIOverHT[1] = %d, desiredTSSIOverHT[2] = %d, desiredTSSIOverHT[3] = %d\n", 
		__FUNCTION__, 
		desiredTSSIOverHT[0], 
		desiredTSSIOverHT[1], 
		desiredTSSIOverHT[2], 
		desiredTSSIOverHT[3]));
	DBGPRINT(RT_DEBUG_TRACE, ("%s: desiredTSSIOverHT[4] = %d, desiredTSSIOverHT[5] = %d, desiredTSSIOverHT[6] = %d, desiredTSSIOverHT[7] = %d\n", 
		__FUNCTION__, 
		desiredTSSIOverHT[4], 
		desiredTSSIOverHT[5], 
		desiredTSSIOverHT[6], 
		desiredTSSIOverHT[7]));

	//
	// The desired TSSI over HT using STBC
	//
	RT28xx_EEPROM_READ16(pAd, EEPROM_HT_USING_STBC_MCS0_MCS1, TxPower);
	TxPower = (TxPower & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xEC = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverHTUsingSTBC[MCS_0] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverHTUsingSTBC[MCS_1] = desiredTSSIOverHTUsingSTBC[MCS_0];
	RT28xx_EEPROM_READ16(pAd, (EEPROM_HT_USING_STBC_MCS2_MCS3 - 1), TxPower);
	TxPower = ((TxPower >> 8) & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xED = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverHTUsingSTBC[MCS_2] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverHTUsingSTBC[MCS_3] = desiredTSSIOverHTUsingSTBC[MCS_2];
	RT28xx_EEPROM_READ16(pAd, EEPROM_HT_USING_STBC_MCS4_MCS5, TxPower);
	TxPower = (TxPower & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xEE = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverHTUsingSTBC[MCS_4] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverHTUsingSTBC[MCS_5] = desiredTSSIOverHTUsingSTBC[MCS_4];
	RT28xx_EEPROM_READ16(pAd, (EEPROM_HT_USING_STBC_MCS6_MCS7 - 1), TxPower);
	TxPower = ((TxPower >> 8) & 0x000F);
	DBGPRINT(RT_DEBUG_TRACE, ("%s: 0xEF = 0x%X\n", __FUNCTION__, TxPower));
	desiredTSSIOverHTUsingSTBC[MCS_6] = (TSSIBase + ((TxPower - TxPowerOFDM54) * (TSSIStepOver2dot4G * 2)));
	desiredTSSIOverHTUsingSTBC[MCS_7] = desiredTSSIOverHTUsingSTBC[MCS_6];

	//
	// Boundary verification: the desired TSSI value
	//
	for (i = 0; i < 8; i++) // HT using STBC: MCS 0 ~ MCS 7
	{
		if (desiredTSSIOverHTUsingSTBC[i] < 0x00)
		{
			desiredTSSIOverHTUsingSTBC[i] = 0x00;
		}
		else if (desiredTSSIOverHTUsingSTBC[i] > 0x1F)
		{
			desiredTSSIOverHTUsingSTBC[i] = 0x1F;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("%s: desiredTSSIOverHTUsingSTBC[0] = %d, desiredTSSIOverHTUsingSTBC[1] = %d, desiredTSSIOverHTUsingSTBC[2] = %d, desiredTSSIOverHTUsingSTBC[3] = %d\n", 
		__FUNCTION__, 
		desiredTSSIOverHTUsingSTBC[0], 
		desiredTSSIOverHTUsingSTBC[1], 
		desiredTSSIOverHTUsingSTBC[2], 
		desiredTSSIOverHTUsingSTBC[3]));
	DBGPRINT(RT_DEBUG_TRACE, ("%s: desiredTSSIOverHTUsingSTBC[4] = %d, desiredTSSIOverHTUsingSTBC[5] = %d, desiredTSSIOverHTUsingSTBC[6] = %d, desiredTSSIOverHTUsingSTBC[7] = %d\n", 
		__FUNCTION__, 
		desiredTSSIOverHTUsingSTBC[4], 
		desiredTSSIOverHTUsingSTBC[5], 
		desiredTSSIOverHTUsingSTBC[6], 
		desiredTSSIOverHTUsingSTBC[7]));

	{
		RT30xxReadRFRegister(pAd, RF_R27, (PUCHAR)(&RFValue));
		RFValue = (RFValue | 0x88); // <7>: IF_Rxout_en, <3>: IF_Txout_en
		RT30xxWriteRFRegister(pAd, RF_R27, RFValue);

		RT30xxReadRFRegister(pAd, RF_R28, (PUCHAR)(&RFValue));
		RFValue = (RFValue & (~0x60)); // <6:5>: tssi_atten
		RT30xxWriteRFRegister(pAd, RF_R28, RFValue);
	}

#if defined (RT3350)
	if (IS_RT3350(pAd))
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R255, 0); 
#endif /* RT3350 */

	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49.byte);
	//BbpR49.field.adc5_in_sel = 0; // (default): TSSI
	BbpR49.field.adc5_in_sel = 1; // PSI
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R49, BbpR49.byte);		

	DBGPRINT(RT_DEBUG_TRACE, ("<--- %s\n", __FUNCTION__));
}


/*
	==========================================================================
	Description:
		Get the desired TSSI based on the latest packet

	Arguments:
		pAd

	Return Value:
		The desired TSSI
	==========================================================================
 */
CHAR GetDesiredTSSI(
	IN PRTMP_ADAPTER		pAd)
{
	PHTTRANSMIT_SETTING pLatestTxHTSetting = (PHTTRANSMIT_SETTING)(&pAd->LastTxRate);
	UCHAR desiredTSSI = 0;
	UCHAR MCS = 0;
	UCHAR MaxMCS = 7;

	MCS = (UCHAR)(pLatestTxHTSetting->field.MCS);
	
	if (pLatestTxHTSetting->field.MODE == MODE_CCK)
	{
		if (MCS > 3) /* boundary verification */
		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: incorrect MCS: MCS = %d\n", 
				__FUNCTION__, 
				MCS));
			
			MCS = 0;
		}
	
		desiredTSSI = desiredTSSIOverCCK[MCS];
	}
	else if (pLatestTxHTSetting->field.MODE == MODE_OFDM)
	{
		if (MCS > 7) /* boundary verification */
		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: incorrect MCS: MCS = %d\n", 
				__FUNCTION__, 
				MCS));

			MCS = 0;
		}

		desiredTSSI = desiredTSSIOverOFDM[MCS];
	}
	else if ((pLatestTxHTSetting->field.MODE == MODE_HTMIX) || (pLatestTxHTSetting->field.MODE == MODE_HTGREENFIELD))
	{
		MaxMCS = pAd->chipCap.TxAlcMaxMCS - 1;

		if (MCS > MaxMCS) /* boundary verification */
		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: incorrect MCS: MCS = %d\n", 
				__FUNCTION__, 
				MCS));

			MCS = 0;
		}

		if (pLatestTxHTSetting->field.STBC == 1)
		{
			desiredTSSI = desiredTSSIOverHT[MCS];
		}
		else
		{
			desiredTSSI = desiredTSSIOverHTUsingSTBC[MCS];
		}

		
		/* For HT BW40 MCS 7 with/without STBC configuration, the desired TSSI value should subtract one from the formula. */
		
		if ((pLatestTxHTSetting->field.BW == BW_40) && (MCS == MCS_7))
		{
			desiredTSSI -= 1;
		}
	}

	DBGPRINT(RT_DEBUG_INFO, ("%s: desiredTSSI = %d, Latest Tx HT setting: MODE = %d, MCS = %d, STBC = %d\n", 
		__FUNCTION__, 
		desiredTSSI, 
		pLatestTxHTSetting->field.MODE, 
		pLatestTxHTSetting->field.MCS, 
		pLatestTxHTSetting->field.STBC));

	DBGPRINT(RT_DEBUG_INFO, ("<--- %s\n", __FUNCTION__));

	return desiredTSSI;
}
#endif /* RTMP_INTERNAL_TX_ALC */


VOID AsicGetTxPowerOffset(
	IN PRTMP_ADAPTER pAd,
	IN PULONG TxPwr)
{
	CONFIGURATION_OF_TX_POWER_CONTROL_OVER_MAC CfgOfTxPwrCtrlOverMAC;

	/* non-3593 */
	{
		CfgOfTxPwrCtrlOverMAC.NumOfEntries = 5; // MAC 0x1314, 0x1318, 0x131C, 0x1320 and 1324

		if (pAd->CommonCfg.BBPCurrentBW == BW_40)
		{
			if (pAd->CommonCfg.CentralChannel > 14)
			{
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[0].MACRegisterOffset = TX_PWR_CFG_0;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[0].RegisterValue = pAd->Tx40MPwrCfgABand[0];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[1].MACRegisterOffset = TX_PWR_CFG_1;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[1].RegisterValue = pAd->Tx40MPwrCfgABand[1];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[2].MACRegisterOffset = TX_PWR_CFG_2;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[2].RegisterValue = pAd->Tx40MPwrCfgABand[2];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[3].MACRegisterOffset = TX_PWR_CFG_3;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[3].RegisterValue = pAd->Tx40MPwrCfgABand[3];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[4].MACRegisterOffset = TX_PWR_CFG_4;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[4].RegisterValue = pAd->Tx40MPwrCfgABand[4];
			}
			else
			{
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[0].MACRegisterOffset = TX_PWR_CFG_0;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[0].RegisterValue = pAd->Tx40MPwrCfgGBand[0];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[1].MACRegisterOffset = TX_PWR_CFG_1;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[1].RegisterValue = pAd->Tx40MPwrCfgGBand[1];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[2].MACRegisterOffset = TX_PWR_CFG_2;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[2].RegisterValue = pAd->Tx40MPwrCfgGBand[2];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[3].MACRegisterOffset = TX_PWR_CFG_3;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[3].RegisterValue = pAd->Tx40MPwrCfgGBand[3];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[4].MACRegisterOffset = TX_PWR_CFG_4;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[4].RegisterValue = pAd->Tx40MPwrCfgGBand[4];
			}
		}
		else
		{
			if (pAd->CommonCfg.CentralChannel > 14)
			{
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[0].MACRegisterOffset = TX_PWR_CFG_0;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[0].RegisterValue = pAd->Tx20MPwrCfgABand[0];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[1].MACRegisterOffset = TX_PWR_CFG_1;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[1].RegisterValue = pAd->Tx20MPwrCfgABand[1];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[2].MACRegisterOffset = TX_PWR_CFG_2;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[2].RegisterValue = pAd->Tx20MPwrCfgABand[2];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[3].MACRegisterOffset = TX_PWR_CFG_3;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[3].RegisterValue = pAd->Tx20MPwrCfgABand[3];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[4].MACRegisterOffset = TX_PWR_CFG_4;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[4].RegisterValue = pAd->Tx20MPwrCfgABand[4];
			}
			else
			{
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[0].MACRegisterOffset = TX_PWR_CFG_0;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[0].RegisterValue = pAd->Tx20MPwrCfgGBand[0];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[1].MACRegisterOffset = TX_PWR_CFG_1;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[1].RegisterValue = pAd->Tx20MPwrCfgGBand[1];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[2].MACRegisterOffset = TX_PWR_CFG_2;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[2].RegisterValue = pAd->Tx20MPwrCfgGBand[2];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[3].MACRegisterOffset = TX_PWR_CFG_3;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[3].RegisterValue = pAd->Tx20MPwrCfgGBand[3];
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[4].MACRegisterOffset = TX_PWR_CFG_4;
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[4].RegisterValue = pAd->Tx20MPwrCfgGBand[4];
			}
		}

		NdisCopyMemory(TxPwr, (UCHAR *)&CfgOfTxPwrCtrlOverMAC, sizeof(CfgOfTxPwrCtrlOverMAC));
	}


}


static VOID AsicAntennaDefaultReset(
	IN PRTMP_ADAPTER		pAd,
	IN EEPROM_ANTENNA_STRUC	*pAntenna)
{
#ifdef RT30xx
	if(IS_RT3090(pAd))
	{
		pAntenna->word = 0;
		pAntenna->field.RfIcType = RFIC_3020;
		pAntenna->field.TxPath = 1;
		pAntenna->field.RxPath = 1;		
	}
	else
#endif /* RT30xx */
#ifdef RT33xx
	if (IS_RT3390(pAd))
	{
		pAntenna->word = 0;
		pAntenna->field.RfIcType = RFIC_3320;
		pAntenna->field.TxPath = 1;
		pAntenna->field.RxPath = 1;
	}
	else
#endif /* RT33xx */
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
		if (IS_RT5390(pAd))
		{
			pAntenna->word = 0;
			pAntenna->field.RfIcType = 0xF; /* Reserved */
			if (IS_RT5392(pAd))
			{
				pAntenna->field.TxPath = 2;
				pAntenna->field.RxPath = 2;
			}
			else
			{
			pAntenna->field.TxPath = 1;
			pAntenna->field.RxPath = 1;
			}
		}
		else
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
	{

		pAntenna->word = 0;
		pAntenna->field.RfIcType = RFIC_2820;
		pAntenna->field.TxPath = 1;
		pAntenna->field.RxPath = 2;
	}
	DBGPRINT(RT_DEBUG_WARN, ("E2PROM error, hard code as 0x%04x\n", pAntenna->word));	
}


static VOID NetDevNickNameInit(
	IN PRTMP_ADAPTER		pAd)
{
#ifdef CONFIG_STA_SUPPORT
#ifdef RTMP_MAC_USB
		snprintf((PSTRING) pAd->nickname, sizeof(pAd->nickname), "RT2870STA");
#endif /* RTMP_MAC_USB */
#endif /* CONFIG_STA_SUPPORT */
}

/* End of rtmp_chip.c */
