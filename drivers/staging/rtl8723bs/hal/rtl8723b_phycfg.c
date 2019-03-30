// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTL8723B_PHYCFG_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <rtl8723b_hal.h>


/*---------------------------Define Local Constant---------------------------*/
/* Channel switch:The size of command tables for switch channel*/
#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

#define MAX_DOZE_WAITING_TIMES_9x 64

/**
* Function:	phy_CalculateBitShift
*
* OverView:	Get shifted position of the BitMask
*
* Input:
*		u32 	BitMask,
*
* Output:	none
* Return:		u32 	Return the shift bit bit position of the mask
*/
static	u32 phy_CalculateBitShift(u32 BitMask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((BitMask>>i) &  0x1) == 1)
			break;
	}
	return i;
}


/**
* Function:	PHY_QueryBBReg
*
* OverView:	Read "sepcific bits" from BB register
*
* Input:
*		struct adapter *	Adapter,
*		u32 		RegAddr,	The target address to be readback
*		u32 		BitMask		The target bit position in the target address
*							to be readback
* Output:	None
* Return:		u32 		Data		The readback register value
* Note:		This function is equal to "GetRegSetting" in PHY programming guide
*/
u32 PHY_QueryBBReg_8723B(struct adapter *Adapter, u32 RegAddr, u32 BitMask)
{
	u32 OriginalValue, BitShift;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	/* RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_QueryBBReg(): RegAddr(%#lx), BitMask(%#lx)\n", RegAddr, BitMask)); */

	OriginalValue = rtw_read32(Adapter, RegAddr);
	BitShift = phy_CalculateBitShift(BitMask);

	return (OriginalValue & BitMask) >> BitShift;

}


/**
* Function:	PHY_SetBBReg
*
* OverView:	Write "Specific bits" to BB register (page 8~)
*
* Input:
*		struct adapter *	Adapter,
*		u32 		RegAddr,	The target address to be modified
*		u32 		BitMask		The target bit position in the target address
*								to be modified
*		u32 		Data		The new register value in the target bit position
*								of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRegSetting" in PHY programming guide
*/

void PHY_SetBBReg_8723B(
	struct adapter *Adapter,
	u32 RegAddr,
	u32 BitMask,
	u32 Data
)
{
	/* u16 BBWaitCounter	= 0; */
	u32 OriginalValue, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

	/* RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_SetBBReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx)\n", RegAddr, BitMask, Data)); */

	if (BitMask != bMaskDWord) { /* if not "double word" write */
		OriginalValue = rtw_read32(Adapter, RegAddr);
		BitShift = phy_CalculateBitShift(BitMask);
		Data = ((OriginalValue & (~BitMask)) | ((Data << BitShift) & BitMask));
	}

	rtw_write32(Adapter, RegAddr, Data);

}


/*  */
/*  2. RF register R/W API */
/*  */

static u32 phy_RFSerialRead_8723B(
	struct adapter *Adapter, enum RF_PATH eRFPath, u32 Offset
)
{
	u32 retValue = 0;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	struct bb_register_def *pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32 NewOffset;
	u32 tmplong2;
	u8 RfPiEnable = 0;
	u32 MaskforPhySet = 0;
	int i = 0;

	/*  */
	/*  Make sure RF register offset is correct */
	/*  */
	Offset &= 0xff;

	NewOffset = Offset;

	if (eRFPath == RF_PATH_A) {
		tmplong2 = PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord);
		tmplong2 = (tmplong2 & (~bLSSIReadAddress)) | (NewOffset<<23) | bLSSIReadEdge;	/* T65 RF */
		PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord, tmplong2&(~bLSSIReadEdge));
	} else {
		tmplong2 = PHY_QueryBBReg(Adapter, rFPGA0_XB_HSSIParameter2|MaskforPhySet, bMaskDWord);
		tmplong2 = (tmplong2 & (~bLSSIReadAddress)) | (NewOffset<<23) | bLSSIReadEdge;	/* T65 RF */
		PHY_SetBBReg(Adapter, rFPGA0_XB_HSSIParameter2|MaskforPhySet, bMaskDWord, tmplong2&(~bLSSIReadEdge));
	}

	tmplong2 = PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord);
	PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord, tmplong2 & (~bLSSIReadEdge));
	PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord, tmplong2 | bLSSIReadEdge);

	udelay(10);

	for (i = 0; i < 2; i++)
		udelay(MAX_STALL_TIME);
	udelay(10);

	if (eRFPath == RF_PATH_A)
		RfPiEnable = (u8)PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter1|MaskforPhySet, BIT8);
	else if (eRFPath == RF_PATH_B)
		RfPiEnable = (u8)PHY_QueryBBReg(Adapter, rFPGA0_XB_HSSIParameter1|MaskforPhySet, BIT8);

	if (RfPiEnable) {
		/*  Read from BBreg8b8, 12 bits for 8190, 20bits for T65 RF */
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBackPi|MaskforPhySet, bLSSIReadBackData);

		/* RT_DISP(FINIT, INIT_RF, ("Readback from RF-PI : 0x%x\n", retValue)); */
	} else {
		/* Read from BBreg8a0, 12 bits for 8190, 20 bits for T65 RF */
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBack|MaskforPhySet, bLSSIReadBackData);

		/* RT_DISP(FINIT, INIT_RF, ("Readback from RF-SI : 0x%x\n", retValue)); */
	}
	return retValue;

}

/**
* Function:	phy_RFSerialWrite_8723B
*
* OverView:	Write data to RF register (page 8~)
*
* Input:
*		struct adapter *	Adapter,
*		RF_PATH			eRFPath,	Radio path of A/B/C/D
*		u32 		Offset,		The target address to be read
*		u32 		Data		The new register Data in the target bit position
*								of the target to be read
*
* Output:	None
* Return:		None
* Note:		Threre are three types of serial operations:
*		1. Software serial write
*		2. Hardware LSSI-Low Speed Serial Interface
*		3. Hardware HSSI-High speed
*		serial write. Driver need to implement (1) and (2).
*		This function is equal to the combination of RF_ReadReg() and  RFLSSIRead()
 *
 * Note:		  For RF8256 only
 *		 The total count of RTL8256(Zebra4) register is around 36 bit it only employs
 *		 4-bit RF address. RTL8256 uses "register mode control bit" (Reg00[12], Reg00[10])
 *		 to access register address bigger than 0xf. See "Appendix-4 in PHY Configuration
 *		 programming guide" for more details.
 *		 Thus, we define a sub-finction for RTL8526 register address conversion
 *	       ===========================================================
 *		 Register Mode		RegCTL[1]		RegCTL[0]		Note
 *							(Reg00[12])		(Reg00[10])
 *	       ===========================================================
 *		 Reg_Mode0				0				x			Reg 0 ~15(0x0 ~ 0xf)
 *	       ------------------------------------------------------------------
 *		 Reg_Mode1				1				0			Reg 16 ~30(0x1 ~ 0xf)
 *	       ------------------------------------------------------------------
 *		 Reg_Mode2				1				1			Reg 31 ~ 45(0x1 ~ 0xf)
 *	       ------------------------------------------------------------------
 *
 *2008/09/02	MH	Add 92S RF definition
 *
 *
 *
*/
static void phy_RFSerialWrite_8723B(
	struct adapter *Adapter,
	enum RF_PATH eRFPath,
	u32 Offset,
	u32 Data
)
{
	u32 DataAndAddr = 0;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	struct bb_register_def *pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32 NewOffset;

	Offset &= 0xff;

	/*  */
	/*  Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/*  */
	/*  Put write addr in [5:0]  and write data in [31:16] */
	/*  */
	/* DataAndAddr = (Data<<16) | (NewOffset&0x3f); */
	DataAndAddr = ((NewOffset<<20) | (Data&0x000fffff)) & 0x0fffffff;	/*  T65 RF */

	/*  */
	/*  Write Operation */
	/*  */
	PHY_SetBBReg(Adapter, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);
	/* RTPRINT(FPHY, PHY_RFW, ("RFW-%d Addr[0x%lx]= 0x%lx\n", eRFPath, pPhyReg->rf3wireOffset, DataAndAddr)); */

}


/**
* Function:	PHY_QueryRFReg
*
* OverView:	Query "Specific bits" to RF register (page 8~)
*
* Input:
*		struct adapter *	Adapter,
*		RF_PATH			eRFPath,	Radio path of A/B/C/D
*		u32 		RegAddr,	The target address to be read
*		u32 		BitMask		The target bit position in the target address
*								to be read
*
* Output:	None
* Return:		u32 		Readback value
* Note:		This function is equal to "GetRFRegSetting" in PHY programming guide
*/
u32 PHY_QueryRFReg_8723B(
	struct adapter *Adapter,
	u8 eRFPath,
	u32 RegAddr,
	u32 BitMask
)
{
	u32 Original_Value, BitShift;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	Original_Value = phy_RFSerialRead_8723B(Adapter, eRFPath, RegAddr);
	BitShift =  phy_CalculateBitShift(BitMask);

	return (Original_Value & BitMask) >> BitShift;
}

/**
* Function:	PHY_SetRFReg
*
* OverView:	Write "Specific bits" to RF register (page 8~)
*
* Input:
*		struct adapter *	Adapter,
*		RF_PATH			eRFPath,	Radio path of A/B/C/D
*		u32 		RegAddr,	The target address to be modified
*		u32 		BitMask		The target bit position in the target address
*								to be modified
*		u32 		Data		The new register Data in the target bit position
*								of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRFRegSetting" in PHY programming guide
*/
void PHY_SetRFReg_8723B(
	struct adapter *Adapter,
	u8 eRFPath,
	u32 RegAddr,
	u32 BitMask,
	u32 Data
)
{
	u32 Original_Value, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

	/*  RF data is 12 bits only */
	if (BitMask != bRFRegOffsetMask) {
		Original_Value = phy_RFSerialRead_8723B(Adapter, eRFPath, RegAddr);
		BitShift =  phy_CalculateBitShift(BitMask);
		Data = ((Original_Value & (~BitMask)) | (Data<<BitShift));
	}

	phy_RFSerialWrite_8723B(Adapter, eRFPath, RegAddr, Data);
}


/*  */
/*  3. Initial MAC/BB/RF config by reading MAC/BB/RF txt. */
/*  */


/*-----------------------------------------------------------------------------
 * Function:    PHY_MACConfig8192C
 *
 * Overview:	Condig MAC by header file or parameter file.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who		Remark
 *  08/12/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------
 */
s32 PHY_MACConfig8723B(struct adapter *Adapter)
{
	int rtStatus = _SUCCESS;
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	s8 *pszMACRegFile;
	s8 sz8723MACRegFile[] = RTL8723B_PHY_MACREG;


	pszMACRegFile = sz8723MACRegFile;

	/*  */
	/*  Config MAC */
	/*  */
	rtStatus = phy_ConfigMACWithParaFile(Adapter, pszMACRegFile);
	if (rtStatus == _FAIL) {
		ODM_ConfigMACWithHeaderFile(&pHalData->odmpriv);
		rtStatus = _SUCCESS;
	}

	return rtStatus;
}

/**
* Function:	phy_InitBBRFRegisterDefinition
*
* OverView:	Initialize Register definition offset for Radio Path A/B/C/D
*
* Input:
*		struct adapter *	Adapter,
*
* Output:	None
* Return:		None
* Note:		The initialization value is constant and it should never be changes
*/
static void phy_InitBBRFRegisterDefinition(struct adapter *Adapter)
{
	struct hal_com_data		*pHalData = GET_HAL_DATA(Adapter);

	/*  RF Interface Sowrtware Control */
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; /*  16 LSBs if read 32-bit from 0x870 */
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; /*  16 MSBs if read 32-bit from 0x870 (16-bit for 0x872) */

	/*  RF Interface Output (and Enable) */
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; /*  16 LSBs if read 32-bit from 0x860 */
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; /*  16 LSBs if read 32-bit from 0x864 */

	/*  RF Interface (Output and)  Enable */
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; /*  16 MSBs if read 32-bit from 0x860 (16-bit for 0x862) */
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; /*  16 MSBs if read 32-bit from 0x864 (16-bit for 0x866) */

	pHalData->PHYRegDef[ODM_RF_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter; /* LSSI Parameter */
	pHalData->PHYRegDef[ODM_RF_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;

	pHalData->PHYRegDef[ODM_RF_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  /* wire control parameter2 */
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;  /* wire control parameter2 */

	/*  Tranceiver Readback LSSI/HSPI mode */
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfLSSIReadBackPi = TransceiverA_HSPI_Readback;
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfLSSIReadBackPi = TransceiverB_HSPI_Readback;

}

static int phy_BB8723b_Config_ParaFile(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	int rtStatus = _SUCCESS;
	u8 sz8723BBRegFile[] = RTL8723B_PHY_REG;
	u8 sz8723AGCTableFile[] = RTL8723B_AGC_TAB;
	u8 sz8723BBBRegPgFile[] = RTL8723B_PHY_REG_PG;
	u8 sz8723BBRegMpFile[] = RTL8723B_PHY_REG_MP;
	u8 sz8723BRFTxPwrLmtFile[] = RTL8723B_TXPWR_LMT;
	u8 *pszBBRegFile = NULL, *pszAGCTableFile = NULL, *pszBBRegPgFile = NULL, *pszBBRegMpFile = NULL, *pszRFTxPwrLmtFile = NULL;

	pszBBRegFile = sz8723BBRegFile;
	pszAGCTableFile = sz8723AGCTableFile;
	pszBBRegPgFile = sz8723BBBRegPgFile;
	pszBBRegMpFile = sz8723BBRegMpFile;
	pszRFTxPwrLmtFile = sz8723BRFTxPwrLmtFile;

	/*  Read Tx Power Limit File */
	PHY_InitTxPowerLimit(Adapter);
	if (
		Adapter->registrypriv.RegEnableTxPowerLimit == 1 ||
		(Adapter->registrypriv.RegEnableTxPowerLimit == 2 && pHalData->EEPROMRegulatory == 1)
	) {
		if (PHY_ConfigRFWithPowerLimitTableParaFile(Adapter, pszRFTxPwrLmtFile) == _FAIL) {
			if (HAL_STATUS_SUCCESS != ODM_ConfigRFWithHeaderFile(&pHalData->odmpriv, CONFIG_RF_TXPWR_LMT, (ODM_RF_RADIO_PATH_E)0))
				rtStatus = _FAIL;
		}

		if (rtStatus != _SUCCESS) {
			DBG_871X("%s():Read Tx power limit fail\n", __func__);
			goto phy_BB8190_Config_ParaFile_Fail;
		}
	}

	/*  */
	/*  1. Read PHY_REG.TXT BB INIT!! */
	/*  */
	if (phy_ConfigBBWithParaFile(Adapter, pszBBRegFile, CONFIG_BB_PHY_REG) ==
		_FAIL) {
		if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG))
			rtStatus = _FAIL;
	}

	if (rtStatus != _SUCCESS) {
		DBG_8192C("%s():Write BB Reg Fail!!", __func__);
		goto phy_BB8190_Config_ParaFile_Fail;
	}

	/*  If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt */
	PHY_InitTxPowerByRate(Adapter);
	if (
		Adapter->registrypriv.RegEnableTxPowerByRate == 1 ||
		(Adapter->registrypriv.RegEnableTxPowerByRate == 2 && pHalData->EEPROMRegulatory != 2)
	) {
		if (phy_ConfigBBWithPgParaFile(Adapter, pszBBRegPgFile) ==
			_FAIL) {
			if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG_PG))
				rtStatus = _FAIL;
		}

		if (pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE)
			PHY_TxPowerByRateConfiguration(Adapter);

		if (
			Adapter->registrypriv.RegEnableTxPowerLimit == 1 ||
			(Adapter->registrypriv.RegEnableTxPowerLimit == 2 && pHalData->EEPROMRegulatory == 1)
		)
			PHY_ConvertTxPowerLimitToPowerIndex(Adapter);

		if (rtStatus != _SUCCESS) {
			DBG_8192C("%s():BB_PG Reg Fail!!\n", __func__);
		}
	}

	/*  */
	/*  2. Read BB AGC table Initialization */
	/*  */
	if (phy_ConfigBBWithParaFile(Adapter, pszAGCTableFile,
				     CONFIG_BB_AGC_TAB) == _FAIL) {
		if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_AGC_TAB))
			rtStatus = _FAIL;
	}

	if (rtStatus != _SUCCESS) {
		DBG_8192C("%s():AGC Table Fail\n", __func__);
		goto phy_BB8190_Config_ParaFile_Fail;
	}

phy_BB8190_Config_ParaFile_Fail:

	return rtStatus;
}


int PHY_BBConfig8723B(struct adapter *Adapter)
{
	int	rtStatus = _SUCCESS;
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	u32 RegVal;
	u8 CrystalCap;

	phy_InitBBRFRegisterDefinition(Adapter);

	/*  Enable BB and RF */
	RegVal = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	rtw_write16(Adapter, REG_SYS_FUNC_EN, (u16)(RegVal|BIT13|BIT0|BIT1));

	rtw_write32(Adapter, 0x948, 0x280);	/*  Others use Antenna S1 */

	rtw_write8(Adapter, REG_RF_CTRL, RF_EN|RF_RSTB|RF_SDMRSTB);

	msleep(1);

	PHY_SetRFReg(Adapter, ODM_RF_PATH_A, 0x1, 0xfffff, 0x780);

	rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_PPLL|FEN_PCIEA|FEN_DIO_PCIE|FEN_BB_GLB_RSTn|FEN_BBRSTB);

	rtw_write8(Adapter, REG_AFE_XTAL_CTRL+1, 0x80);

	/*  */
	/*  Config BB and AGC */
	/*  */
	rtStatus = phy_BB8723b_Config_ParaFile(Adapter);

	/*  0x2C[23:18] = 0x2C[17:12] = CrystalCap */
	CrystalCap = pHalData->CrystalCap & 0x3F;
	PHY_SetBBReg(Adapter, REG_MAC_PHY_CTRL, 0xFFF000, (CrystalCap | (CrystalCap << 6)));

	return rtStatus;
}

static void phy_LCK_8723B(struct adapter *Adapter)
{
	PHY_SetRFReg(Adapter, RF_PATH_A, 0xB0, bRFRegOffsetMask, 0xDFBE0);
	PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x8C01);
	mdelay(200);
	PHY_SetRFReg(Adapter, RF_PATH_A, 0xB0, bRFRegOffsetMask, 0xDFFE0);
}

int PHY_RFConfig8723B(struct adapter *Adapter)
{
	int rtStatus = _SUCCESS;

	/*  */
	/*  RF config */
	/*  */
	rtStatus = PHY_RF6052_Config8723B(Adapter);

	phy_LCK_8723B(Adapter);
	/* PHY_BB8723B_Config_1T(Adapter); */

	return rtStatus;
}

/**************************************************************************************************************
 *   Description:
 *       The low-level interface to set TxAGC , called by both MP and Normal Driver.
 *
 *                                                                                    <20120830, Kordan>
 **************************************************************************************************************/

void PHY_SetTxPowerIndex_8723B(
	struct adapter *Adapter,
	u32 PowerIndex,
	u8 RFPath,
	u8 Rate
)
{
	if (RFPath == ODM_RF_PATH_A || RFPath == ODM_RF_PATH_B) {
		switch (Rate) {
		case MGN_1M:
			PHY_SetBBReg(Adapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, PowerIndex);
			break;
		case MGN_2M:
			PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte1, PowerIndex);
			break;
		case MGN_5_5M:
			PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte2, PowerIndex);
			break;
		case MGN_11M:
			PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte3, PowerIndex);
			break;

		case MGN_6M:
			PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskByte0, PowerIndex);
			break;
		case MGN_9M:
			PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskByte1, PowerIndex);
			break;
		case MGN_12M:
			PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskByte2, PowerIndex);
			break;
		case MGN_18M:
			PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskByte3, PowerIndex);
			break;

		case MGN_24M:
			PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskByte0, PowerIndex);
			break;
		case MGN_36M:
			PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskByte1, PowerIndex);
			break;
		case MGN_48M:
			PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskByte2, PowerIndex);
			break;
		case MGN_54M:
			PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskByte3, PowerIndex);
			break;

		case MGN_MCS0:
			PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte0, PowerIndex);
			break;
		case MGN_MCS1:
			PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte1, PowerIndex);
			break;
		case MGN_MCS2:
			PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte2, PowerIndex);
			break;
		case MGN_MCS3:
			PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte3, PowerIndex);
			break;

		case MGN_MCS4:
			PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte0, PowerIndex);
			break;
		case MGN_MCS5:
			PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte1, PowerIndex);
			break;
		case MGN_MCS6:
			PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte2, PowerIndex);
			break;
		case MGN_MCS7:
			PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte3, PowerIndex);
			break;

		default:
			DBG_871X("Invalid Rate!!\n");
			break;
		}
	} else {
		RT_TRACE(_module_hal_init_c_, _drv_err_, ("Invalid RFPath!!\n"));
	}
}

u8 PHY_GetTxPowerIndex_8723B(
	struct adapter *padapter,
	u8 RFPath,
	u8 Rate,
	enum CHANNEL_WIDTH BandWidth,
	u8 Channel
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	s8 txPower = 0, powerDiffByRate = 0, limit = 0;
	bool bIn24G = false;

	/* DBG_871X("===>%s\n", __func__); */

	txPower = (s8) PHY_GetTxPowerIndexBase(padapter, RFPath, Rate, BandWidth, Channel, &bIn24G);
	powerDiffByRate = PHY_GetTxPowerByRate(padapter, BAND_ON_2_4G, ODM_RF_PATH_A, RF_1TX, Rate);

	limit = phy_get_tx_pwr_lmt(
		padapter,
		padapter->registrypriv.RegPwrTblSel,
		(u8)(!bIn24G),
		pHalData->CurrentChannelBW,
		RFPath,
		Rate,
		pHalData->CurrentChannel
	);

	powerDiffByRate = powerDiffByRate > limit ? limit : powerDiffByRate;
	txPower += powerDiffByRate;

	txPower += PHY_GetTxPowerTrackingOffset(padapter, RFPath, Rate);

	if (txPower > MAX_POWER_INDEX)
		txPower = MAX_POWER_INDEX;

	/* DBG_871X("Final Tx Power(RF-%c, Channel: %d) = %d(0x%X)\n", ((RFPath == 0)?'A':'B'), Channel, txPower, txPower)); */
	return (u8) txPower;
}

void PHY_SetTxPowerLevel8723B(struct adapter *Adapter, u8 Channel)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T pDM_Odm = &pHalData->odmpriv;
	pFAT_T pDM_FatTable = &pDM_Odm->DM_FatTable;
	u8 RFPath = ODM_RF_PATH_A;

	if (pHalData->AntDivCfg) {/*  antenna diversity Enable */
		RFPath = ((pDM_FatTable->RxIdleAnt == MAIN_ANT) ? ODM_RF_PATH_A : ODM_RF_PATH_B);
	} else { /*  antenna diversity disable */
		RFPath = pHalData->ant_path;
	}

	RT_TRACE(_module_hal_init_c_, _drv_info_, ("==>PHY_SetTxPowerLevel8723B()\n"));

	PHY_SetTxPowerLevelByPath(Adapter, Channel, RFPath);

	RT_TRACE(_module_hal_init_c_, _drv_info_, ("<==PHY_SetTxPowerLevel8723B()\n"));
}

void PHY_GetTxPowerLevel8723B(struct adapter *Adapter, s32 *powerlevel)
{
}

static void phy_SetRegBW_8723B(
	struct adapter *Adapter, enum CHANNEL_WIDTH CurrentBW
)
{
	u16 RegRfMod_BW, u2tmp = 0;
	RegRfMod_BW = rtw_read16(Adapter, REG_TRXPTCL_CTL_8723B);

	switch (CurrentBW) {
	case CHANNEL_WIDTH_20:
		rtw_write16(Adapter, REG_TRXPTCL_CTL_8723B, (RegRfMod_BW & 0xFE7F)); /*  BIT 7 = 0, BIT 8 = 0 */
		break;

	case CHANNEL_WIDTH_40:
		u2tmp = RegRfMod_BW | BIT7;
		rtw_write16(Adapter, REG_TRXPTCL_CTL_8723B, (u2tmp & 0xFEFF)); /*  BIT 7 = 1, BIT 8 = 0 */
		break;

	case CHANNEL_WIDTH_80:
		u2tmp = RegRfMod_BW | BIT8;
		rtw_write16(Adapter, REG_TRXPTCL_CTL_8723B, (u2tmp & 0xFF7F)); /*  BIT 7 = 0, BIT 8 = 1 */
		break;

	default:
		DBG_871X("phy_PostSetBWMode8723B():	unknown Bandwidth: %#X\n", CurrentBW);
		break;
	}
}

static u8 phy_GetSecondaryChnl_8723B(struct adapter *Adapter)
{
	u8 SCSettingOf40 = 0, SCSettingOf20 = 0;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	RT_TRACE(
		_module_hal_init_c_,
		_drv_info_,
		(
			"SCMapping: VHT Case: pHalData->CurrentChannelBW %d, pHalData->nCur80MhzPrimeSC %d, pHalData->nCur40MhzPrimeSC %d\n",
			pHalData->CurrentChannelBW,
			pHalData->nCur80MhzPrimeSC,
			pHalData->nCur40MhzPrimeSC
		)
	);
	if (pHalData->CurrentChannelBW == CHANNEL_WIDTH_80) {
		if (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			SCSettingOf40 = VHT_DATA_SC_40_LOWER_OF_80MHZ;
		else if (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			SCSettingOf40 = VHT_DATA_SC_40_UPPER_OF_80MHZ;
		else
			RT_TRACE(_module_hal_init_c_, _drv_err_, ("SCMapping: Not Correct Primary40MHz Setting\n"));

		if (
			(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) &&
			(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
		)
			SCSettingOf20 = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		else if (
			(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) &&
			(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
		)
			SCSettingOf20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else if (
			(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) &&
			(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
		)
			SCSettingOf20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if (
			(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) &&
			(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
		)
			SCSettingOf20 = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
		else
			RT_TRACE(_module_hal_init_c_, _drv_err_, ("SCMapping: Not Correct Primary40MHz Setting\n"));
	} else if (pHalData->CurrentChannelBW == CHANNEL_WIDTH_40) {
		RT_TRACE(
			_module_hal_init_c_,
			_drv_info_,
			(
				"SCMapping: VHT Case: pHalData->CurrentChannelBW %d, pHalData->nCur40MhzPrimeSC %d\n",
				pHalData->CurrentChannelBW,
				pHalData->nCur40MhzPrimeSC
			)
		);

		if (pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			SCSettingOf20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if (pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			SCSettingOf20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else
			RT_TRACE(_module_hal_init_c_, _drv_err_, ("SCMapping: Not Correct Primary40MHz Setting\n"));
	}

	RT_TRACE(_module_hal_init_c_, _drv_info_, ("SCMapping: SC Value %x\n", ((SCSettingOf40 << 4) | SCSettingOf20)));
	return  (SCSettingOf40 << 4) | SCSettingOf20;
}

static void phy_PostSetBwMode8723B(struct adapter *Adapter)
{
	u8 SubChnlNum = 0;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);


	/* 3 Set Reg668 Reg440 BW */
	phy_SetRegBW_8723B(Adapter, pHalData->CurrentChannelBW);

	/* 3 Set Reg483 */
	SubChnlNum = phy_GetSecondaryChnl_8723B(Adapter);
	rtw_write8(Adapter, REG_DATA_SC_8723B, SubChnlNum);

	/* 3 */
	/* 3<2>Set PHY related register */
	/* 3 */
	switch (pHalData->CurrentChannelBW) {
	/* 20 MHz channel*/
	case CHANNEL_WIDTH_20:
		PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x0);

		PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x0);

/* 			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 1); */

		PHY_SetBBReg(Adapter, rOFDM0_TxPseudoNoiseWgt, (BIT31|BIT30), 0x0);
		break;

	/* 40 MHz channel*/
	case CHANNEL_WIDTH_40:
		PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x1);

		PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x1);

		/*  Set Control channel to upper or lower. These settings are required only for 40MHz */
		PHY_SetBBReg(Adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC>>1));

		PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);

/* PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 0); */

		PHY_SetBBReg(Adapter, 0x818, (BIT26|BIT27), (pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);

		break;

	default:
		/*RT_TRACE(COMP_DBG, DBG_LOUD, ("phy_SetBWMode8723B(): unknown Bandwidth: %#X\n"\
					, pHalData->CurrentChannelBW));*/
		break;
	}

	/* 3<3>Set RF related register */
	PHY_RF6052SetBandwidth8723B(Adapter, pHalData->CurrentChannelBW);
}

static void phy_SwChnl8723B(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	u8 channelToSW = pHalData->CurrentChannel;

	if (pHalData->rf_chip == RF_PSEUDO_11N) {
		/* RT_TRACE(COMP_MLME, DBG_LOUD, ("phy_SwChnl8723B: return for PSEUDO\n")); */
		return;
	}
	pHalData->RfRegChnlVal[0] = ((pHalData->RfRegChnlVal[0] & 0xfffff00) | channelToSW);
	PHY_SetRFReg(padapter, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, pHalData->RfRegChnlVal[0]);
	PHY_SetRFReg(padapter, ODM_RF_PATH_B, RF_CHNLBW, 0x3FF, pHalData->RfRegChnlVal[0]);

	DBG_8192C("===>phy_SwChnl8723B: Channel = %d\n", channelToSW);
}

static void phy_SwChnlAndSetBwMode8723B(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	/* RT_TRACE(COMP_SCAN, DBG_LOUD, ("phy_SwChnlAndSetBwMode8723B(): bSwChnl %d, bSetChnlBW %d\n", pHalData->bSwChnl, pHalData->bSetChnlBW)); */
	if (Adapter->bNotifyChannelChange) {
		DBG_871X("[%s] bSwChnl =%d, ch =%d, bSetChnlBW =%d, bw =%d\n",
			__func__,
			pHalData->bSwChnl,
			pHalData->CurrentChannel,
			pHalData->bSetChnlBW,
			pHalData->CurrentChannelBW);
	}

	if (Adapter->bDriverStopped || Adapter->bSurpriseRemoved)
		return;

	if (pHalData->bSwChnl) {
		phy_SwChnl8723B(Adapter);
		pHalData->bSwChnl = false;
	}

	if (pHalData->bSetChnlBW) {
		phy_PostSetBwMode8723B(Adapter);
		pHalData->bSetChnlBW = false;
	}

	PHY_SetTxPowerLevel8723B(Adapter, pHalData->CurrentChannel);
}

static void PHY_HandleSwChnlAndSetBW8723B(
	struct adapter *Adapter,
	bool bSwitchChannel,
	bool bSetBandWidth,
	u8 ChannelNum,
	enum CHANNEL_WIDTH ChnlWidth,
	enum EXTCHNL_OFFSET ExtChnlOffsetOf40MHz,
	enum EXTCHNL_OFFSET ExtChnlOffsetOf80MHz,
	u8 CenterFrequencyIndex1
)
{
	/* static bool		bInitialzed = false; */
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	u8 tmpChannel = pHalData->CurrentChannel;
	enum CHANNEL_WIDTH tmpBW = pHalData->CurrentChannelBW;
	u8 tmpnCur40MhzPrimeSC = pHalData->nCur40MhzPrimeSC;
	u8 tmpnCur80MhzPrimeSC = pHalData->nCur80MhzPrimeSC;
	u8 tmpCenterFrequencyIndex1 = pHalData->CurrentCenterFrequencyIndex1;

	/* DBG_871X("=> PHY_HandleSwChnlAndSetBW8812: bSwitchChannel %d, bSetBandWidth %d\n", bSwitchChannel, bSetBandWidth); */

	/* check is swchnl or setbw */
	if (!bSwitchChannel && !bSetBandWidth) {
		DBG_871X("PHY_HandleSwChnlAndSetBW8812:  not switch channel and not set bandwidth\n");
		return;
	}

	/* skip change for channel or bandwidth is the same */
	if (bSwitchChannel) {
		/* if (pHalData->CurrentChannel != ChannelNum) */
		{
			if (HAL_IsLegalChannel(Adapter, ChannelNum))
				pHalData->bSwChnl = true;
		}
	}

	if (bSetBandWidth)
		pHalData->bSetChnlBW = true;

	if (!pHalData->bSetChnlBW && !pHalData->bSwChnl) {
		/* DBG_871X("<= PHY_HandleSwChnlAndSetBW8812: bSwChnl %d, bSetChnlBW %d\n", pHalData->bSwChnl, pHalData->bSetChnlBW); */
		return;
	}


	if (pHalData->bSwChnl) {
		pHalData->CurrentChannel = ChannelNum;
		pHalData->CurrentCenterFrequencyIndex1 = ChannelNum;
	}


	if (pHalData->bSetChnlBW) {
		pHalData->CurrentChannelBW = ChnlWidth;
		pHalData->nCur40MhzPrimeSC = ExtChnlOffsetOf40MHz;
		pHalData->nCur80MhzPrimeSC = ExtChnlOffsetOf80MHz;
		pHalData->CurrentCenterFrequencyIndex1 = CenterFrequencyIndex1;
	}

	/* Switch workitem or set timer to do switch channel or setbandwidth operation */
	if ((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved)) {
		phy_SwChnlAndSetBwMode8723B(Adapter);
	} else {
		if (pHalData->bSwChnl) {
			pHalData->CurrentChannel = tmpChannel;
			pHalData->CurrentCenterFrequencyIndex1 = tmpChannel;
		}

		if (pHalData->bSetChnlBW) {
			pHalData->CurrentChannelBW = tmpBW;
			pHalData->nCur40MhzPrimeSC = tmpnCur40MhzPrimeSC;
			pHalData->nCur80MhzPrimeSC = tmpnCur80MhzPrimeSC;
			pHalData->CurrentCenterFrequencyIndex1 = tmpCenterFrequencyIndex1;
		}
	}
}

void PHY_SetBWMode8723B(
	struct adapter *Adapter,
	enum CHANNEL_WIDTH Bandwidth, /*  20M or 40M */
	unsigned char Offset /*  Upper, Lower, or Don't care */
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	PHY_HandleSwChnlAndSetBW8723B(Adapter, false, true, pHalData->CurrentChannel, Bandwidth, Offset, Offset, pHalData->CurrentChannel);
}

/*  Call after initialization */
void PHY_SwChnl8723B(struct adapter *Adapter, u8 channel)
{
	PHY_HandleSwChnlAndSetBW8723B(Adapter, true, false, channel, 0, 0, 0, channel);
}

void PHY_SetSwChnlBWMode8723B(
	struct adapter *Adapter,
	u8 channel,
	enum CHANNEL_WIDTH Bandwidth,
	u8 Offset40,
	u8 Offset80
)
{
	/* DBG_871X("%s() ===>\n", __func__); */

	PHY_HandleSwChnlAndSetBW8723B(Adapter, true, true, channel, Bandwidth, Offset40, Offset80, channel);

	/* DBG_871X("<==%s()\n", __func__); */
}
