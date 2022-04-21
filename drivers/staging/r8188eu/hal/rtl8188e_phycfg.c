// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTL8188E_PHYCFG_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtw_iol.h"
#include "../include/rtl8188e_hal.h"

/*  */
/*  1. BB register R/W API */
/*  */

/**
* Function:	phy_CalculateBitShift
*
* OverView:	Get shifted position of the BitMask
*
* Input:
*			u32		BitMask,
*
* Output:	none
* Return:		u32		Return the shift bit bit position of the mask
*/
static	u32 phy_CalculateBitShift(u32 BitMask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((BitMask >> i) & 0x1) == 1)
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
*			struct adapter *Adapter,
*			u32			RegAddr,	The target address to be readback
*			u32			BitMask		The target bit position in the target address
*								to be readback
* Output:	None
* Return:		u32			Data		The readback register value
* Note:		This function is equal to "GetRegSetting" in PHY programming guide
*/
u32
rtl8188e_PHY_QueryBBReg(
		struct adapter *Adapter,
		u32 RegAddr,
		u32 BitMask
	)
{
	u32 ReturnValue = 0, OriginalValue, BitShift;

	OriginalValue = rtw_read32(Adapter, RegAddr);
	BitShift = phy_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;
	return ReturnValue;
}

/**
* Function:	PHY_SetBBReg
*
* OverView:	Write "Specific bits" to BB register (page 8~)
*
* Input:
*			struct adapter *Adapter,
*			u32			RegAddr,	The target address to be modified
*			u32			BitMask		The target bit position in the target address
*									to be modified
*			u32			Data		The new register value in the target bit position
*									of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRegSetting" in PHY programming guide
*/

void rtl8188e_PHY_SetBBReg(struct adapter *Adapter, u32 RegAddr, u32 BitMask, u32 Data)
{
	u32 OriginalValue, BitShift;

	if (BitMask != bMaskDWord) { /* if not "double word" write */
		OriginalValue = rtw_read32(Adapter, RegAddr);
		BitShift = phy_CalculateBitShift(BitMask);
		Data = ((OriginalValue & (~BitMask)) | (Data << BitShift));
	}

	rtw_write32(Adapter, RegAddr, Data);
}

/*  */
/*  2. RF register R/W API */
/*  */
/**
* Function:	phy_RFSerialRead
*
* OverView:	Read regster from RF chips
*
* Input:
*			struct adapter *Adapter,
*			u32			Offset,		The target address to be read
*
* Output:	None
* Return:		u32			reback value
* Note:		Threre are three types of serial operations:
*			1. Software serial write
*			2. Hardware LSSI-Low Speed Serial Interface
*			3. Hardware HSSI-High speed
*			serial write. Driver need to implement (1) and (2).
*			This function is equal to the combination of RF_ReadReg() and  RFLSSIRead()
*/
static	u32
phy_RFSerialRead(
		struct adapter *Adapter,
		u32 Offset
	)
{
	u32 retValue = 0;
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	struct bb_reg_def *pPhyReg = &pHalData->PHYRegDef;
	u32 NewOffset;
	u32 tmplong, tmplong2;
	u8 	RfPiEnable = 0;
	/*  */
	/*  Make sure RF register offset is correct */
	/*  */
	Offset &= 0xff;

	/*  */
	/*  Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/*  For 92S LSSI Read RFLSSIRead */
	/*  For RF A/B write 0x824/82c(does not work in the future) */
	/*  We must use 0x824 for RF A and B to execute read trigger */
	tmplong = rtl8188e_PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord);
	tmplong2 = tmplong;

	tmplong2 = (tmplong2 & (~bLSSIReadAddress)) | (NewOffset << 23) | bLSSIReadEdge;	/* T65 RF */

	rtl8188e_PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord, tmplong & (~bLSSIReadEdge));
	udelay(10);/*  PlatformStallExecution(10); */

	rtl8188e_PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, bMaskDWord, tmplong2);
	udelay(100);/* PlatformStallExecution(100); */

	udelay(10);/* PlatformStallExecution(10); */

	RfPiEnable = (u8)rtl8188e_PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter1, BIT(8));

	if (RfPiEnable) {	/*  Read from BBreg8b8, 12 bits for 8190, 20bits for T65 RF */
		retValue = rtl8188e_PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBackPi, bLSSIReadBackData);
	} else {	/* Read from BBreg8a0, 12 bits for 8190, 20 bits for T65 RF */
		retValue = rtl8188e_PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBack, bLSSIReadBackData);
	}
	return retValue;
}

/**
* Function:	phy_RFSerialWrite
*
* OverView:	Write data to RF register (page 8~)
*
* Input:
*			struct adapter *Adapter,
*			enum rf_radio_path eRFPath,	Radio path of A/B/C/D
*			u32			Offset,		The target address to be read
*			u32			Data		The new register Data in the target bit position
*									of the target to be read
*
* Output:	None
* Return:		None
* Note:		Threre are three types of serial operations:
*			1. Software serial write
*			2. Hardware LSSI-Low Speed Serial Interface
*			3. Hardware HSSI-High speed
*			serial write. Driver need to implement (1) and (2).
*			This function is equal to the combination of RF_ReadReg() and  RFLSSIRead()
 *
 * Note:		  For RF8256 only
 *			 The total count of RTL8256(Zebra4) register is around 36 bit it only employs
 *			 4-bit RF address. RTL8256 uses "register mode control bit" (Reg00[12], Reg00[10])
 *			 to access register address bigger than 0xf. See "Appendix-4 in PHY Configuration
 *			 programming guide" for more details.
 *			 Thus, we define a sub-finction for RTL8526 register address conversion
 *		       ===========================================================
 *			 Register Mode		RegCTL[1]		RegCTL[0]		Note
 *								(Reg00[12])		(Reg00[10])
 *		       ===========================================================
 *			 Reg_Mode0				0				x			Reg 0 ~15(0x0 ~ 0xf)
 *		       ------------------------------------------------------------------
 *			 Reg_Mode1				1				0			Reg 16 ~30(0x1 ~ 0xf)
 *		       ------------------------------------------------------------------
 *			 Reg_Mode2				1				1			Reg 31 ~ 45(0x1 ~ 0xf)
 *		       ------------------------------------------------------------------
 *
 *	2008/09/02	MH	Add 92S RF definition
 *
 *
 *
*/
static	void
phy_RFSerialWrite(
		struct adapter *Adapter,
		u32 Offset,
		u32 Data
	)
{
	u32 DataAndAddr = 0;
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	struct bb_reg_def *pPhyReg = &pHalData->PHYRegDef;
	u32 NewOffset;

	/*  2009/06/17 MH We can not execute IO for power save or other accident mode. */

	Offset &= 0xff;

	/*  */
	/*  Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/*  */
	/*  Put write addr in [5:0]  and write data in [31:16] */
	/*  */
	DataAndAddr = ((NewOffset << 20) | (Data & 0x000fffff)) & 0x0fffffff;	/*  T65 RF */

	/*  */
	/*  Write Operation */
	/*  */
	rtl8188e_PHY_SetBBReg(Adapter, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);
}

/**
* Function:	PHY_QueryRFReg
*
* OverView:	Query "Specific bits" to RF register (page 8~)
*
* Input:
*			struct adapter *Adapter,
*			u32			RegAddr,	The target address to be read
*			u32			BitMask		The target bit position in the target address
*									to be read
*
* Output:	None
* Return:		u32			Readback value
* Note:		This function is equal to "GetRFRegSetting" in PHY programming guide
*/
u32 rtl8188e_PHY_QueryRFReg(struct adapter *Adapter, u32 RegAddr, u32 BitMask)
{
	u32 Original_Value, Readback_Value, BitShift;

	Original_Value = phy_RFSerialRead(Adapter, RegAddr);

	BitShift =  phy_CalculateBitShift(BitMask);
	Readback_Value = (Original_Value & BitMask) >> BitShift;
	return Readback_Value;
}

/**
* Function:	PHY_SetRFReg
*
* OverView:	Write "Specific bits" to RF register (page 8~)
*
* Input:
*			struct adapter *Adapter,
*			u32			RegAddr,	The target address to be modified
*			u32			BitMask		The target bit position in the target address
*									to be modified
*			u32			Data		The new register Data in the target bit position
*									of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRFRegSetting" in PHY programming guide
*/
void
rtl8188e_PHY_SetRFReg(
		struct adapter *Adapter,
		u32 RegAddr,
		u32 BitMask,
		u32 Data
	)
{
	u32 Original_Value, BitShift;

	/*  RF data is 12 bits only */
	if (BitMask != bRFRegOffsetMask) {
		Original_Value = phy_RFSerialRead(Adapter, RegAddr);
		BitShift =  phy_CalculateBitShift(BitMask);
		Data = ((Original_Value & (~BitMask)) | (Data << BitShift));
	}

	phy_RFSerialWrite(Adapter, RegAddr, Data);
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
 *---------------------------------------------------------------------------*/
s32 PHY_MACConfig8188E(struct adapter *Adapter)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	int rtStatus = _SUCCESS;

	/*  */
	/*  Config MAC */
	/*  */
	if (HAL_STATUS_FAILURE == ODM_ReadAndConfig_MAC_REG_8188E(&pHalData->odmpriv))
		rtStatus = _FAIL;

	/*  2010.07.13 AMPDU aggregation number B */
	rtw_write16(Adapter, REG_MAX_AGGR_NUM, MAX_AGGR_NUM);

	return rtStatus;
}

/**
* Function:	phy_InitBBRFRegisterDefinition
*
* OverView:	Initialize Register definition offset for Radio Path A/B/C/D
*
* Input:
*			struct adapter *Adapter,
*
* Output:	None
* Return:		None
* Note:		The initialization value is constant and it should never be changes
*/
static	void
phy_InitBBRFRegisterDefinition(
		struct adapter *Adapter
)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;

	/*  RF Interface Sowrtware Control */
	pHalData->PHYRegDef.rfintfs = rFPGA0_XAB_RFInterfaceSW; /*  16 LSBs if read 32-bit from 0x870 */

	/*  RF Interface Readback Value */
	pHalData->PHYRegDef.rfintfi = rFPGA0_XAB_RFInterfaceRB; /*  16 LSBs if read 32-bit from 0x8E0 */

	/*  RF Interface Output (and Enable) */
	pHalData->PHYRegDef.rfintfo = rFPGA0_XA_RFInterfaceOE; /*  16 LSBs if read 32-bit from 0x860 */

	/*  RF Interface (Output and)  Enable */
	pHalData->PHYRegDef.rfintfe = rFPGA0_XA_RFInterfaceOE; /*  16 MSBs if read 32-bit from 0x860 (16-bit for 0x862) */

	/* Addr of LSSI. Wirte RF register by driver */
	pHalData->PHYRegDef.rf3wireOffset = rFPGA0_XA_LSSIParameter; /* LSSI Parameter */

	/*  RF parameter */
	pHalData->PHYRegDef.rfLSSI_Select = rFPGA0_XAB_RFParameter;  /* BB Band Select */

	/*  Tx AGC Gain Stage (same for all path. Should we remove this?) */
	pHalData->PHYRegDef.rfTxGainStage = rFPGA0_TxGainStage; /* Tx gain stage */

	/*  Tranceiver A~D HSSI Parameter-1 */
	pHalData->PHYRegDef.rfHSSIPara1 = rFPGA0_XA_HSSIParameter1;  /* wire control parameter1 */

	/*  Tranceiver A~D HSSI Parameter-2 */
	pHalData->PHYRegDef.rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  /* wire control parameter2 */

	/*  RF switch Control */
	pHalData->PHYRegDef.rfSwitchControl = rFPGA0_XAB_SwitchControl; /* TR/Ant switch control */

	/*  AGC control 1 */
	pHalData->PHYRegDef.rfAGCControl1 = rOFDM0_XAAGCCore1;

	/*  AGC control 2 */
	pHalData->PHYRegDef.rfAGCControl2 = rOFDM0_XAAGCCore2;

	/*  RX AFE control 1 */
	pHalData->PHYRegDef.rfRxIQImbalance = rOFDM0_XARxIQImbalance;

	/*  RX AFE control 1 */
	pHalData->PHYRegDef.rfRxAFE = rOFDM0_XARxAFE;

	/*  Tx AFE control 1 */
	pHalData->PHYRegDef.rfTxIQImbalance = rOFDM0_XATxIQImbalance;

	/*  Tx AFE control 2 */
	pHalData->PHYRegDef.rfTxAFE = rOFDM0_XATxAFE;

	/*  Tranceiver LSSI Readback SI mode */
	pHalData->PHYRegDef.rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;

	/*  Tranceiver LSSI Readback PI mode */
	pHalData->PHYRegDef.rfLSSIReadBackPi = TransceiverA_HSPI_Readback;
}

void storePwrIndexDiffRateOffset(struct adapter *Adapter, u32 RegAddr, u32 BitMask, u32 Data)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;

	if (RegAddr == rTxAGC_A_Rate18_06)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0] = Data;
	if (RegAddr == rTxAGC_A_Rate54_24)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][1] = Data;
	if (RegAddr == rTxAGC_A_CCK1_Mcs32)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][6] = Data;
	if (RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0xffffff00)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][7] = Data;
	if (RegAddr == rTxAGC_A_Mcs03_Mcs00)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][2] = Data;
	if (RegAddr == rTxAGC_A_Mcs07_Mcs04)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][3] = Data;
	if (RegAddr == rTxAGC_A_Mcs11_Mcs08)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4] = Data;
	if (RegAddr == rTxAGC_A_Mcs15_Mcs12) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][5] = Data;
		pHalData->pwrGroupCnt++;
	}
	if (RegAddr == rTxAGC_B_Rate18_06)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][8] = Data;
	if (RegAddr == rTxAGC_B_Rate54_24)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][9] = Data;
	if (RegAddr == rTxAGC_B_CCK1_55_Mcs32)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][14] = Data;
	if (RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0x000000ff)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][15] = Data;
	if (RegAddr == rTxAGC_B_Mcs03_Mcs00)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][10] = Data;
	if (RegAddr == rTxAGC_B_Mcs07_Mcs04)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][11] = Data;
	if (RegAddr == rTxAGC_B_Mcs11_Mcs08)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][12] = Data;
	if (RegAddr == rTxAGC_B_Mcs15_Mcs12)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][13] = Data;
}

static	int phy_BB8188E_Config_ParaFile(struct adapter *Adapter)
{
	struct eeprom_priv *pEEPROM = &Adapter->eeprompriv;
	struct hal_data_8188e *pHalData = &Adapter->haldata;

	/*  */
	/*  1. Read PHY_REG.TXT BB INIT!! */
	/*  We will separate as 88C / 92C according to chip version */
	/*  */
	if (HAL_STATUS_FAILURE == ODM_ReadAndConfig_PHY_REG_1T_8188E(&pHalData->odmpriv))
		return _FAIL;

	/*  2. If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt */
	if (!pEEPROM->bautoload_fail_flag) {
		pHalData->pwrGroupCnt = 0;
		ODM_ReadAndConfig_PHY_REG_PG_8188E(&pHalData->odmpriv);
	}

	/*  3. BB AGC table Initialization */
	if (HAL_STATUS_FAILURE == ODM_ReadAndConfig_AGC_TAB_1T_8188E(&pHalData->odmpriv))
		return _FAIL;

	return _SUCCESS;
}

int
PHY_BBConfig8188E(
		struct adapter *Adapter
	)
{
	int	rtStatus = _SUCCESS;
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	u32 RegVal;
	u8 CrystalCap;

	phy_InitBBRFRegisterDefinition(Adapter);

	/*  Enable BB and RF */
	RegVal = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	rtw_write16(Adapter, REG_SYS_FUNC_EN, (u16)(RegVal | BIT(13) | BIT(0) | BIT(1)));

	/*  20090923 Joseph: Advised by Steven and Jenyu. Power sequence before init RF. */

	rtw_write8(Adapter, REG_RF_CTRL, RF_EN | RF_RSTB | RF_SDMRSTB);

	rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_USBA | FEN_USBD | FEN_BB_GLB_RSTn | FEN_BBRSTB);

	/*  Config BB and AGC */
	rtStatus = phy_BB8188E_Config_ParaFile(Adapter);

	/*  write 0x24[16:11] = 0x24[22:17] = CrystalCap */
	CrystalCap = pHalData->CrystalCap & 0x3F;
	rtl8188e_PHY_SetBBReg(Adapter, REG_AFE_XTAL_CTRL, 0x7ff800, (CrystalCap | (CrystalCap << 6)));

	return rtStatus;
}

int PHY_RFConfig8188E(struct adapter *Adapter)
{
	int		rtStatus = _SUCCESS;

	/*  RF config */
	rtStatus = PHY_RF6052_Config8188E(Adapter);
	return rtStatus;
}

static void getTxPowerIndex88E(struct adapter *Adapter, u8 channel, u8 *cckPowerLevel,
			       u8 *ofdmPowerLevel, u8 *BW20PowerLevel,
			       u8 *BW40PowerLevel)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	u8 index = (channel - 1);

	/*  1. CCK */
	cckPowerLevel[RF_PATH_A] = pHalData->Index24G_CCK_Base[index];
	/* 2. OFDM */
	ofdmPowerLevel[RF_PATH_A] = pHalData->Index24G_BW40_Base[index] +
		pHalData->OFDM_24G_Diff[RF_PATH_A];
	/*  1. BW20 */
	BW20PowerLevel[RF_PATH_A] = pHalData->Index24G_BW40_Base[index] +
		pHalData->BW20_24G_Diff[RF_PATH_A];
	/* 2. BW40 */
	BW40PowerLevel[RF_PATH_A] = pHalData->Index24G_BW40_Base[index];
}

/*-----------------------------------------------------------------------------
 * Function:    SetTxPowerLevel8190()
 *
 * Overview:    This function is export to "HalCommon" moudule
 *			We must consider RF path later!!!!!!!
 *
 * Input:       struct adapter *Adapter
 *			u8		channel
 *
 * Output:      NONE
 *
 * Return:      NONE
 *	2008/11/04	MHC		We remove EEPROM_93C56.
 *						We need to move CCX relative code to independet file.
 *	2009/01/21	MHC		Support new EEPROM format from SD3 requirement.
 *
 *---------------------------------------------------------------------------*/
void
PHY_SetTxPowerLevel8188E(
		struct adapter *Adapter,
		u8 channel
	)
{
	u8 cckPowerLevel[MAX_TX_COUNT] = {0};
	u8 ofdmPowerLevel[MAX_TX_COUNT] = {0};/*  [0]:RF-A, [1]:RF-B */
	u8 BW20PowerLevel[MAX_TX_COUNT] = {0};
	u8 BW40PowerLevel[MAX_TX_COUNT] = {0};

	getTxPowerIndex88E(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0], &BW20PowerLevel[0], &BW40PowerLevel[0]);

	rtl8188e_PHY_RF6052SetCckTxPower(Adapter, &cckPowerLevel[0]);
	rtl8188e_PHY_RF6052SetOFDMTxPower(Adapter, &ofdmPowerLevel[0], &BW20PowerLevel[0], &BW40PowerLevel[0], channel);
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_SetBWModeCallback8192C()
 *
 * Overview:    Timer callback function for SetSetBWMode
 *
 * Input:		PRT_TIMER		pTimer
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		(1) We do not take j mode into consideration now
 *			(2) Will two workitem of "switch channel" and "switch channel bandwidth" run
 *			     concurrently?
 *---------------------------------------------------------------------------*/
static void
_PHY_SetBWMode92C(
		struct adapter *Adapter
)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	u8 regBwOpMode;
	u8 regRRSR_RSC;

	if (Adapter->bDriverStopped)
		return;

	/* 3 */
	/* 3<1>Set MAC register */
	/* 3 */

	regBwOpMode = rtw_read8(Adapter, REG_BWOPMODE);
	regRRSR_RSC = rtw_read8(Adapter, REG_RRSR + 2);

	switch (pHalData->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		regBwOpMode |= BW_OPMODE_20MHZ;
		/*  2007/02/07 Mark by Emily because we have not verify whether this register works */
		rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);
		break;
	case HT_CHANNEL_WIDTH_40:
		regBwOpMode &= ~BW_OPMODE_20MHZ;
		/*  2007/02/07 Mark by Emily because we have not verify whether this register works */
		rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);
		regRRSR_RSC = (regRRSR_RSC & 0x90) | (pHalData->nCur40MhzPrimeSC << 5);
		rtw_write8(Adapter, REG_RRSR + 2, regRRSR_RSC);
		break;
	default:
		break;
	}

	/* 3  */
	/* 3 <2>Set PHY related register */
	/* 3 */
	switch (pHalData->CurrentChannelBW) {
	/* 20 MHz channel*/
	case HT_CHANNEL_WIDTH_20:
		rtl8188e_PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x0);
		rtl8188e_PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x0);
		break;
	/* 40 MHz channel*/
	case HT_CHANNEL_WIDTH_40:
		rtl8188e_PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x1);
		rtl8188e_PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x1);
		/*  Set Control channel to upper or lower. These settings are required only for 40MHz */
		rtl8188e_PHY_SetBBReg(Adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC >> 1));
		rtl8188e_PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);
		rtl8188e_PHY_SetBBReg(Adapter, 0x818, (BIT(26) | BIT(27)),
			     (pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);
		break;
	default:
		break;
	}
	/* Skip over setting of J-mode in BB register here. Default value is "None J mode". Emily 20070315 */

	rtl8188e_PHY_RF6052SetBandwidth(Adapter, pHalData->CurrentChannelBW);
}

 /*-----------------------------------------------------------------------------
 * Function:   SetBWMode8190Pci()
 *
 * Overview:  This function is export to "HalCommon" moudule
 *
 * Input:		struct adapter *Adapter
 *			enum ht_channel_width Bandwidth	20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		We do not take j mode into consideration now
 *---------------------------------------------------------------------------*/
void PHY_SetBWMode8188E(struct adapter *Adapter, enum ht_channel_width Bandwidth,	/*  20M or 40M */
			unsigned char	Offset)		/*  Upper, Lower, or Don't care */
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	enum ht_channel_width tmpBW = pHalData->CurrentChannelBW;

	pHalData->CurrentChannelBW = Bandwidth;

	pHalData->nCur40MhzPrimeSC = Offset;

	if ((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved))
		_PHY_SetBWMode92C(Adapter);
	else
		pHalData->CurrentChannelBW = tmpBW;
}

static void _PHY_SwChnl8192C(struct adapter *Adapter, u8 channel)
{
	u32 param1, param2;
	struct hal_data_8188e *pHalData = &Adapter->haldata;

	/* s1. pre common command - CmdID_SetTxPowerLevel */
	PHY_SetTxPowerLevel8188E(Adapter, channel);

	/* s2. RF dependent command - CmdID_RF_WriteReg, param1=RF_CHNLBW, param2=channel */
	param1 = RF_CHNLBW;
	param2 = channel;
	pHalData->RfRegChnlVal = ((pHalData->RfRegChnlVal & 0xfffffc00) | param2);
	rtl8188e_PHY_SetRFReg(Adapter, param1, bRFRegOffsetMask, pHalData->RfRegChnlVal);
}

void PHY_SwChnl8188E(struct adapter *Adapter, u8 channel)
{
	/*  Call after initialization */
	struct hal_data_8188e *pHalData = &Adapter->haldata;

	if (channel == 0)
		channel = 1;

	if ((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved)) {
		pHalData->CurrentChannel = channel;
		_PHY_SwChnl8192C(Adapter, channel);
	}
}
