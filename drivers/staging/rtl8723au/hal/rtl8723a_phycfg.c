/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#define _RTL8723A_PHYCFG_C_

#include <osdep_service.h>
#include <drv_types.h>

#include <rtl8723a_hal.h>
#include <usb_ops_linux.h>

/*---------------------------Define Local Constant---------------------------*/
/* Channel switch:The size of command tables for switch channel*/
#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

#define MAX_DOZE_WAITING_TIMES_9x 64

/*---------------------------Define Local Constant---------------------------*/

/*------------------------Define global variable-----------------------------*/

/*------------------------Define local variable------------------------------*/

/*--------------------Define export function prototype-----------------------*/
/*  Please refer to header file */
/*--------------------Define export function prototype-----------------------*/

/*----------------------------Function Body----------------------------------*/
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
		if (((BitMask>>i) & 0x1) == 1)
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
*	struct rtw_adapter *	Adapter,
*	u32			RegAddr,	Target address to be readback
*	u32			BitMask		Target bit position in the
*						target address to be readback
* Output:
*	None
* Return:
*	u32			Data		The readback register value
* Note:
*	This function is equal to "GetRegSetting" in PHY programming guide
*/
u32
PHY_QueryBBReg(struct rtw_adapter *Adapter, u32 RegAddr, u32 BitMask)
{
	u32	ReturnValue = 0, OriginalValue, BitShift;

	OriginalValue = rtl8723au_read32(Adapter, RegAddr);
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
*	struct rtw_adapter *	Adapter,
*	u32			RegAddr,	Target address to be modified
*	u32			BitMask		Target bit position in the
*						target address to be modified
*	u32			Data		The new register value in the
*						target bit position of the
*						 target address
*
* Output:
*	None
* Return:
*	None
* Note:
*	This function is equal to "PutRegSetting" in PHY programming guide
*/

void
PHY_SetBBReg(struct rtw_adapter *Adapter, u32 RegAddr, u32 BitMask, u32	Data)
{
	u32 OriginalValue, BitShift;

	if (BitMask != bMaskDWord) {/* if not "double word" write */
		OriginalValue = rtl8723au_read32(Adapter, RegAddr);
		BitShift = phy_CalculateBitShift(BitMask);
		Data = (OriginalValue & (~BitMask)) | (Data << BitShift);
	}

	rtl8723au_write32(Adapter, RegAddr, Data);

	/* RTPRINT(FPHY, PHY_BBW, ("BBW MASK = 0x%lx Addr[0x%lx]= 0x%lx\n", BitMask, RegAddr, Data)); */
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
*		struct rtw_adapter *		Adapter,
*		enum RF_RADIO_PATH	eRFPath,	Radio path of A/B/C/D
*		u32 Offset,			The target address to be read
*
* Output:	None
* Return:	u32			reback value
* Note:		Threre are three types of serial operations:
*		1. Software serial write
*		2. Hardware LSSI-Low Speed Serial Interface
*		3. Hardware HSSI-High speed
*		serial write. Driver need to implement (1) and (2).
*		This function is equal to the combination of RF_ReadReg() and
*		RFLSSIRead()
*/
static u32
phy_RFSerialRead(struct rtw_adapter *Adapter, enum RF_RADIO_PATH eRFPath,
		 u32 Offset)
{
	u32 retValue = 0;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct bb_reg_define *pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32 NewOffset;
	u32 tmplong, tmplong2;
	u8 RfPiEnable = 0;
	/*  */
	/*  Make sure RF register offset is correct */
	/*  */
	Offset &= 0x3f;

	/*  */
	/*  Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/*  2009/06/17 MH We can not execute IO for power save or
	    other accident mode. */
	/* if (RT_CANNOT_IO(Adapter)) */
	/*  */
	/*	RTPRINT(FPHY, PHY_RFR, ("phy_RFSerialRead return all one\n")); */
	/*	return	0xFFFFFFFF; */
	/*  */

	/*  For 92S LSSI Read RFLSSIRead */
	/*  For RF A/B write 0x824/82c(does not work in the future) */
	/*  We must use 0x824 for RF A and B to execute read trigger */
	tmplong = rtl8723au_read32(Adapter, rFPGA0_XA_HSSIParameter2);
	if (eRFPath == RF_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = rtl8723au_read32(Adapter, pPhyReg->rfHSSIPara2);

	tmplong2 = (tmplong2 & ~bLSSIReadAddress) |
		(NewOffset << 23) | bLSSIReadEdge;	/* T65 RF */

	rtl8723au_write32(Adapter, rFPGA0_XA_HSSIParameter2,
			  tmplong & (~bLSSIReadEdge));
	udelay(10);/*  PlatformStallExecution(10); */

	rtl8723au_write32(Adapter, pPhyReg->rfHSSIPara2, tmplong2);
	udelay(100);/* PlatformStallExecution(100); */

	rtl8723au_write32(Adapter, rFPGA0_XA_HSSIParameter2,
			  tmplong | bLSSIReadEdge);
	udelay(10);/* PlatformStallExecution(10); */

	if (eRFPath == RF_PATH_A)
		RfPiEnable = (u8)PHY_QueryBBReg(Adapter,
						rFPGA0_XA_HSSIParameter1,
						BIT(8));
	else if (eRFPath == RF_PATH_B)
		RfPiEnable = (u8)PHY_QueryBBReg(Adapter,
						rFPGA0_XB_HSSIParameter1,
						BIT(8));

	if (RfPiEnable)	{
		/* Read from BBreg8b8, 12 bits for 8190, 20bits for T65 RF */
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBackPi,
					  bLSSIReadBackData);
		/* DBG_8723A("Readback from RF-PI : 0x%x\n", retValue); */
	} else {
		/* Read from BBreg8a0, 12 bits for 8190, 20 bits for T65 RF */
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBack,
					  bLSSIReadBackData);
		/* DBG_8723A("Readback from RF-SI : 0x%x\n", retValue); */
	}
	/* DBG_8723A("RFR-%d Addr[0x%x]= 0x%x\n", eRFPath, pPhyReg->rfLSSIReadBack, retValue); */

	return retValue;
}

/**
* Function:	phy_RFSerialWrite
*
* OverView:	Write data to RF register (page 8~)
*
* Input:
*	struct rtw_adapter *		Adapter,
*	enum RF_RADIO_PATH	eRFPath,	Radio path of A/B/C/D
*	u32 Offset,			The target address to be read
*	u32 Data			The new register Data in the target
*					bit position of the target to be read
*
* Output:
*	None
* Return:
*	None
* Note:
*	Threre are three types of serial operations:
*		1. Software serial write
*		2. Hardware LSSI-Low Speed Serial Interface
*		3. Hardware HSSI-High speed
*		serial write. Driver need to implement (1) and (2).
*		This function is equal to the combination of RF_ReadReg() and
*		RFLSSIRead()
*
* Note:	  For RF8256 only
* The total count of RTL8256(Zebra4) register is around 36 bit it only employs
* 4-bit RF address. RTL8256 uses "register mode control bit"
* (Reg00[12], Reg00[10]) to access register address bigger than 0xf.
* See "Appendix-4 in PHY Configuration programming guide" for more details.
* Thus, we define a sub-finction for RTL8526 register address conversion
* ===========================================================
* Register Mode:	RegCTL[1]	RegCTL[0]	Note
*			(Reg00[12])	(Reg00[10])
* ===========================================================
* Reg_Mode0		0		x		Reg 0 ~15(0x0 ~ 0xf)
* ------------------------------------------------------------------
* Reg_Mode1		1		0		Reg 16 ~30(0x1 ~ 0xf)
* ------------------------------------------------------------------
* Reg_Mode2		1		1		Reg 31 ~ 45(0x1 ~ 0xf)
* ------------------------------------------------------------------
*
*	2008/09/02	MH	Add 92S RF definition
*/
static	void
phy_RFSerialWrite(struct rtw_adapter *Adapter, enum RF_RADIO_PATH eRFPath,
		  u32 Offset, u32 Data)
{
	u32 DataAndAddr = 0;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct bb_reg_define *pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32 NewOffset;

	/*  2009/06/17 MH We can not execute IO for power save or
	    other accident mode. */
	/* if (RT_CANNOT_IO(Adapter)) */
	/*  */
	/*	RTPRINT(FPHY, PHY_RFW, ("phy_RFSerialWrite stop\n")); */
	/*	return; */
	/*  */

	Offset &= 0x3f;

	/*  */
	/*  Shadow Update */
	/*  */
	/* PHY_RFShadowWrite(Adapter, eRFPath, Offset, Data); */

	/*  */
	/*  Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/*  */
	/*  Put write addr in [5:0]  and write data in [31:16] */
	/*  */
	/* DataAndAddr = (Data<<16) | (NewOffset&0x3f); */
	/*  T65 RF */
	DataAndAddr = ((NewOffset<<20) | (Data&0x000fffff)) & 0x0fffffff;

	/*  */
	/*  Write Operation */
	/*  */
	rtl8723au_write32(Adapter, pPhyReg->rf3wireOffset, DataAndAddr);
}

/**
* Function:	PHY_QueryRFReg
*
* OverView:	Query "Specific bits" to RF register (page 8~)
*
* Input:
*	struct rtw_adapter *		Adapter,
*	enum RF_RADIO_PATH	eRFPath,	Radio path of A/B/C/D
*	u32 RegAddr,			The target address to be read
*	u32BitMask			The target bit position in the target
*					address	to be read
*
* Output:
*	None
* Return:
*	u32				Readback value
* Note:
*	This function is equal to "GetRFRegSetting" in PHY programming guide
*/
u32
PHY_QueryRFReg(struct rtw_adapter *Adapter, enum RF_RADIO_PATH eRFPath,
	       u32 RegAddr, u32 BitMask)
{
	u32 Original_Value, Readback_Value, BitShift;
	/* struct hal_data_8723a	*pHalData = GET_HAL_DATA(Adapter); */
	/* u8	RFWaitCounter = 0; */
	/* _irqL	irqL; */

	Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);

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
*	struct rtw_adapter *		Adapter,
*	enum RF_RADIO_PATH	eRFPath,	Radio path of A/B/C/D
*	u32 RegAddr,			The target address to be modified
*	u32 BitMask			The target bit position in the target
*					address to be modified
*	u32 Data			The new register Data in the target
*					bit position of the target address
*
* Output:
*	None
* Return:
*	None
* Note:	This function is equal to "PutRFRegSetting" in PHY programming guide
*/
void
PHY_SetRFReg(struct rtw_adapter *Adapter, enum RF_RADIO_PATH eRFPath,
	     u32 RegAddr, u32 BitMask, u32 Data)
{
	/* struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter); */
	/* u8 RFWaitCounter	= 0; */
	u32 Original_Value, BitShift;

	/*  RF data is 12 bits only */
	if (BitMask != bRFRegOffsetMask) {
		Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);
		BitShift =  phy_CalculateBitShift(BitMask);
		Data = (Original_Value & (~BitMask)) | (Data << BitShift);
	}

	phy_RFSerialWrite(Adapter, eRFPath, RegAddr, Data);
}

/*  3. Initial MAC/BB/RF config by reading MAC/BB/RF txt. */

/*-----------------------------------------------------------------------------
 * Function:    PHY_MACConfig8723A
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
int PHY_MACConfig8723A(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);

	/*  */
	/*  Config MAC */
	/*  */
	ODM_ReadAndConfig_MAC_REG_8723A(&pHalData->odmpriv);

	/*  2010.07.13 AMPDU aggregation number 9 */
	rtl8723au_write8(Adapter, REG_MAX_AGGR_NUM, 0x0A);
	if (pHalData->rf_type == RF_2T2R &&
	    BOARD_USB_DONGLE == pHalData->BoardType)
		rtl8723au_write8(Adapter, 0x40, 0x04);

	return _SUCCESS;
}

/**
* Function:	phy_InitBBRFRegisterDefinition
*
* OverView:	Initialize Register definition offset for Radio Path A/B/C/D
*
* Input:
*			struct rtw_adapter *		Adapter,
*
* Output:	None
* Return:		None
* Note:
*	The initialization value is constant and it should never be changes
*/
static	void
phy_InitBBRFRegisterDefinition(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);

	/*  RF Interface Sowrtware Control */
	 /*  16 LSBs if read 32-bit from 0x870 */
	pHalData->PHYRegDef[RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW;
	 /*  16 MSBs if read 32-bit from 0x870 (16-bit for 0x872) */
	pHalData->PHYRegDef[RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW;

	/*  RF Interface Readback Value */
	/*  16 LSBs if read 32-bit from 0x8E0 */
	pHalData->PHYRegDef[RF_PATH_A].rfintfi = rFPGA0_XAB_RFInterfaceRB;
	/*  16 MSBs if read 32-bit from 0x8E0 (16-bit for 0x8E2) */
	pHalData->PHYRegDef[RF_PATH_B].rfintfi = rFPGA0_XAB_RFInterfaceRB;

	/*  RF Interface Output (and Enable) */
	/*  16 LSBs if read 32-bit from 0x860 */
	pHalData->PHYRegDef[RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE;
	 /*  16 LSBs if read 32-bit from 0x864 */
	pHalData->PHYRegDef[RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE;

	/*  RF Interface (Output and)  Enable */
	 /*  16 MSBs if read 32-bit from 0x860 (16-bit for 0x862) */
	pHalData->PHYRegDef[RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE;
	/*  16 MSBs if read 32-bit from 0x864 (16-bit for 0x866) */
	pHalData->PHYRegDef[RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE;

	/* Addr of LSSI. Wirte RF register by driver */
	pHalData->PHYRegDef[RF_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter;
	pHalData->PHYRegDef[RF_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;

	/*  RF parameter */
	/* BB Band Select */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSI_Select = rFPGA0_XAB_RFParameter;

	/*  Tx AGC Gain Stage (same for all path. Should we remove this?) */
	pHalData->PHYRegDef[RF_PATH_A].rfTxGainStage = rFPGA0_TxGainStage;
	pHalData->PHYRegDef[RF_PATH_B].rfTxGainStage = rFPGA0_TxGainStage;

	/*  Tranceiver A~D HSSI Parameter-1 */
	/* wire control parameter1 */
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara1 = rFPGA0_XA_HSSIParameter1;
	/* wire control parameter1 */
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara1 = rFPGA0_XB_HSSIParameter1;

	/*  Tranceiver A~D HSSI Parameter-2 */
	/* wire control parameter2 */
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;
	/* wire control parameter2 */
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;

	/*  RF switch Control */
	pHalData->PHYRegDef[RF_PATH_A].rfSwitchControl =
		rFPGA0_XAB_SwitchControl; /* TR/Ant switch control */
	pHalData->PHYRegDef[RF_PATH_B].rfSwitchControl =
		rFPGA0_XAB_SwitchControl;

	/*  AGC control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfAGCControl1 = rOFDM0_XAAGCCore1;
	pHalData->PHYRegDef[RF_PATH_B].rfAGCControl1 = rOFDM0_XBAGCCore1;

	/*  AGC control 2 */
	pHalData->PHYRegDef[RF_PATH_A].rfAGCControl2 = rOFDM0_XAAGCCore2;
	pHalData->PHYRegDef[RF_PATH_B].rfAGCControl2 = rOFDM0_XBAGCCore2;

	/*  RX AFE control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfRxIQImbalance = rOFDM0_XARxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_B].rfRxIQImbalance = rOFDM0_XBRxIQImbalance;

	/*  RX AFE control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfRxAFE = rOFDM0_XARxAFE;
	pHalData->PHYRegDef[RF_PATH_B].rfRxAFE = rOFDM0_XBRxAFE;

	/*  Tx AFE control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfTxIQImbalance = rOFDM0_XATxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_B].rfTxIQImbalance = rOFDM0_XBTxIQImbalance;

	/*  Tx AFE control 2 */
	pHalData->PHYRegDef[RF_PATH_A].rfTxAFE = rOFDM0_XATxAFE;
	pHalData->PHYRegDef[RF_PATH_B].rfTxAFE = rOFDM0_XBTxAFE;

	/*  Tranceiver LSSI Readback SI mode */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;

	/*  Tranceiver LSSI Readback PI mode */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBackPi =
		TransceiverA_HSPI_Readback;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBackPi =
		TransceiverB_HSPI_Readback;
}

/*  The following is for High Power PA */
static void
storePwrIndexDiffRateOffset(struct rtw_adapter *Adapter, u32 RegAddr,
			    u32 BitMask, u32 Data)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);

	if (RegAddr == rTxAGC_A_Rate18_06) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0] = Data;
	}
	if (RegAddr == rTxAGC_A_Rate54_24) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][1] = Data;
	}
	if (RegAddr == rTxAGC_A_CCK1_Mcs32) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][6] = Data;
	}
	if (RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0xffffff00) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][7] = Data;
	}
	if (RegAddr == rTxAGC_A_Mcs03_Mcs00) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][2] = Data;
	}
	if (RegAddr == rTxAGC_A_Mcs07_Mcs04) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][3] = Data;
	}
	if (RegAddr == rTxAGC_A_Mcs11_Mcs08) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4] = Data;
	}
	if (RegAddr == rTxAGC_A_Mcs15_Mcs12) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][5] = Data;
	}
	if (RegAddr == rTxAGC_B_Rate18_06) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][8] = Data;
	}
	if (RegAddr == rTxAGC_B_Rate54_24) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][9] = Data;
	}
	if (RegAddr == rTxAGC_B_CCK1_55_Mcs32) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][14] = Data;
	}
	if (RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0x000000ff) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][15] = Data;
	}
	if (RegAddr == rTxAGC_B_Mcs03_Mcs00) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][10] = Data;
	}
	if (RegAddr == rTxAGC_B_Mcs07_Mcs04) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][11] = Data;
	}
	if (RegAddr == rTxAGC_B_Mcs11_Mcs08) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][12] = Data;
	}
	if (RegAddr == rTxAGC_B_Mcs15_Mcs12) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][13] = Data;
		pHalData->pwrGroupCnt++;
	}
}

/*-----------------------------------------------------------------------------
 * Function:	phy_ConfigBBWithPgHeaderFile
 *
 * Overview:	Config PHY_REG_PG array
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When		Who	Remark
 * 11/06/2008	MHC	Add later!!!!!!.. Please modify for new files!!!!
 * 11/10/2008	tynli	Modify to mew files.
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithPgHeaderFile(struct rtw_adapter *Adapter)
{
	int i;
	u32 *Rtl819XPHY_REGArray_Table_PG;
	u16 PHY_REGArrayPGLen;

	PHY_REGArrayPGLen = Rtl8723_PHY_REG_Array_PGLength;
	Rtl819XPHY_REGArray_Table_PG = (u32 *)Rtl8723_PHY_REG_Array_PG;

	for (i = 0; i < PHY_REGArrayPGLen; i = i + 3) {
		storePwrIndexDiffRateOffset(Adapter,
					    Rtl819XPHY_REGArray_Table_PG[i],
					    Rtl819XPHY_REGArray_Table_PG[i+1],
					    Rtl819XPHY_REGArray_Table_PG[i+2]);
	}

	return _SUCCESS;
}

static void
phy_BB8192C_Config_1T(struct rtw_adapter *Adapter)
{
	/* for path - B */
	PHY_SetBBReg(Adapter, rFPGA0_TxInfo, 0x3, 0x2);
	PHY_SetBBReg(Adapter, rFPGA1_TxInfo, 0x300033, 0x200022);

	/*  20100519 Joseph: Add for 1T2R config. Suggested by Kevin,
	    Jenyu and Yunan. */
	PHY_SetBBReg(Adapter, rCCK0_AFESetting, bMaskByte3, 0x45);
	PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x23);
	/*  B path first AGC */
	PHY_SetBBReg(Adapter, rOFDM0_AGCParameter1, 0x30, 0x1);

	PHY_SetBBReg(Adapter, 0xe74, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe78, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe7c, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe80, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe88, 0x0c000000, 0x2);
}

static int
phy_BB8723a_Config_ParaFile(struct rtw_adapter *Adapter)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	int rtStatus = _SUCCESS;

	/*  */
	/*  1. Read PHY_REG.TXT BB INIT!! */
	/*  We will seperate as 88C / 92C according to chip version */
	/*  */
	ODM_ReadAndConfig_PHY_REG_1T_8723A(&pHalData->odmpriv);

	/*  */
	/*  20100318 Joseph: Config 2T2R to 1T2R if necessary. */
	/*  */
	if (pHalData->rf_type == RF_1T2R) {
		phy_BB8192C_Config_1T(Adapter);
		DBG_8723A("phy_BB8723a_Config_ParaFile():Config to 1T!!\n");
	}

	/*  */
	/*  2. If EEPROM or EFUSE autoload OK, We must config by
	    PHY_REG_PG.txt */
	/*  */
	if (pEEPROM->bautoload_fail_flag == false) {
		pHalData->pwrGroupCnt = 0;

		rtStatus = phy_ConfigBBWithPgHeaderFile(Adapter);
	}

	if (rtStatus != _SUCCESS)
		goto phy_BB8190_Config_ParaFile_Fail;

	/*  */
	/*  3. BB AGC table Initialization */
	/*  */
	ODM_ReadAndConfig_AGC_TAB_1T_8723A(&pHalData->odmpriv);

phy_BB8190_Config_ParaFile_Fail:

	return rtStatus;
}

int
PHY_BBConfig8723A(struct rtw_adapter *Adapter)
{
	int rtStatus = _SUCCESS;
	struct hal_data_8723a	*pHalData = GET_HAL_DATA(Adapter);
	u8 TmpU1B = 0;
	u8 CrystalCap;

	phy_InitBBRFRegisterDefinition(Adapter);

	/*  Suggested by Scott. tynli_test. 2010.12.30. */
	/* 1. 0x28[1] = 1 */
	TmpU1B = rtl8723au_read8(Adapter, REG_AFE_PLL_CTRL);
	udelay(2);
	rtl8723au_write8(Adapter, REG_AFE_PLL_CTRL, TmpU1B | BIT(1));
	udelay(2);

	/* 2. 0x29[7:0] = 0xFF */
	rtl8723au_write8(Adapter, REG_AFE_PLL_CTRL+1, 0xff);
	udelay(2);

	/* 3. 0x02[1:0] = 2b'11 */
	TmpU1B = rtl8723au_read8(Adapter, REG_SYS_FUNC_EN);
	rtl8723au_write8(Adapter, REG_SYS_FUNC_EN,
			 (TmpU1B | FEN_BB_GLB_RSTn | FEN_BBRSTB));

	/* 4. 0x25[6] = 0 */
	TmpU1B = rtl8723au_read8(Adapter, REG_AFE_XTAL_CTRL + 1);
	rtl8723au_write8(Adapter, REG_AFE_XTAL_CTRL+1, TmpU1B & ~BIT(6));

	/* 5. 0x24[20] = 0	Advised by SD3 Alex Wang. 2011.02.09. */
	TmpU1B = rtl8723au_read8(Adapter, REG_AFE_XTAL_CTRL+2);
	rtl8723au_write8(Adapter, REG_AFE_XTAL_CTRL+2, TmpU1B & ~BIT(4));

	/* 6. 0x1f[7:0] = 0x07 */
	rtl8723au_write8(Adapter, REG_RF_CTRL, 0x07);

	/*  */
	/*  Config BB and AGC */
	/*  */
	rtStatus = phy_BB8723a_Config_ParaFile(Adapter);

/* only for B-cut */
	if (pHalData->EEPROMVersion >= 0x01) {
		CrystalCap = pHalData->CrystalCap & 0x3F;
		PHY_SetBBReg(Adapter, REG_MAC_PHY_CTRL, 0xFFF000,
			     (CrystalCap | (CrystalCap << 6)));
	}

	rtl8723au_write32(Adapter, REG_LDOA15_CTRL, 0x01572505);
	return rtStatus;
}

static void getTxPowerIndex(struct rtw_adapter *Adapter,
			    u8 channel,	u8 *cckPowerLevel,  u8 *ofdmPowerLevel)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	u8 index = (channel - 1);
	/*  1. CCK */
	cckPowerLevel[RF_PATH_A] = pHalData->TxPwrLevelCck[RF_PATH_A][index];
	cckPowerLevel[RF_PATH_B] = pHalData->TxPwrLevelCck[RF_PATH_B][index];

	/*  2. OFDM for 1S or 2S */
	if (GET_RF_TYPE(Adapter) == RF_1T2R || GET_RF_TYPE(Adapter) == RF_1T1R) {
		/*  Read HT 40 OFDM TX power */
		ofdmPowerLevel[RF_PATH_A] =
			pHalData->TxPwrLevelHT40_1S[RF_PATH_A][index];
		ofdmPowerLevel[RF_PATH_B] =
			pHalData->TxPwrLevelHT40_1S[RF_PATH_B][index];
	} else if (GET_RF_TYPE(Adapter) == RF_2T2R) {
		/*  Read HT 40 OFDM TX power */
		ofdmPowerLevel[RF_PATH_A] =
			pHalData->TxPwrLevelHT40_2S[RF_PATH_A][index];
		ofdmPowerLevel[RF_PATH_B] =
			pHalData->TxPwrLevelHT40_2S[RF_PATH_B][index];
	}
}

static void ccxPowerIndexCheck(struct rtw_adapter *Adapter, u8 channel,
			       u8 *cckPowerLevel, u8 *ofdmPowerLevel)
{
}

/*-----------------------------------------------------------------------------
 * Function:    SetTxPowerLevel8723A()
 *
 * Overview:    This function is export to "HalCommon" moudule
 *			We must consider RF path later!!!!!!!
 *
 * Input:       struct rtw_adapter *		Adapter
 *			u8		channel
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 *---------------------------------------------------------------------------*/
void PHY_SetTxPowerLevel8723A(struct rtw_adapter *Adapter, u8 channel)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	u8 cckPowerLevel[2], ofdmPowerLevel[2];	/*  [0]:RF-A, [1]:RF-B */

	if (pHalData->bTXPowerDataReadFromEEPORM == false)
		return;

	getTxPowerIndex(Adapter, channel, &cckPowerLevel[0],
			&ofdmPowerLevel[0]);

	ccxPowerIndexCheck(Adapter, channel, &cckPowerLevel[0],
			   &ofdmPowerLevel[0]);

	rtl823a_phy_rf6052setccktxpower(Adapter, &cckPowerLevel[0]);
	rtl8723a_PHY_RF6052SetOFDMTxPower(Adapter, &ofdmPowerLevel[0], channel);
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_SetBWMode23aCallback8192C()
 *
 * Overview:    Timer callback function for SetSetBWMode23a
 *
 * Input:		PRT_TIMER		pTimer
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:
 *	(1) We do not take j mode into consideration now
 *	(2) Will two workitem of "switch channel" and
 *	    "switch channel bandwidth" run concurrently?
 *---------------------------------------------------------------------------*/
static void
_PHY_SetBWMode23a92C(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	u8 regBwOpMode;
	u8 regRRSR_RSC;

	if (Adapter->bDriverStopped)
		return;

	/* 3 */
	/* 3<1>Set MAC register */
	/* 3 */

	regBwOpMode = rtl8723au_read8(Adapter, REG_BWOPMODE);
	regRRSR_RSC = rtl8723au_read8(Adapter, REG_RRSR+2);

	switch (pHalData->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		regBwOpMode |= BW_OPMODE_20MHZ;
		rtl8723au_write8(Adapter, REG_BWOPMODE, regBwOpMode);
		break;
	case HT_CHANNEL_WIDTH_40:
		regBwOpMode &= ~BW_OPMODE_20MHZ;
		rtl8723au_write8(Adapter, REG_BWOPMODE, regBwOpMode);
		regRRSR_RSC = (regRRSR_RSC & 0x90) |
			(pHalData->nCur40MhzPrimeSC << 5);
		rtl8723au_write8(Adapter, REG_RRSR+2, regRRSR_RSC);
		break;

	default:
		break;
	}

	/* 3 */
	/* 3<2>Set PHY related register */
	/* 3 */
	switch (pHalData->CurrentChannelBW) {
		/* 20 MHz channel*/
	case HT_CHANNEL_WIDTH_20:
		PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x0);
		PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x0);
		PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT(10), 1);

		break;

		/* 40 MHz channel*/
	case HT_CHANNEL_WIDTH_40:
		PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x1);
		PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x1);

		/*  Set Control channel to upper or lower. These settings
		    are required only for 40MHz */
		PHY_SetBBReg(Adapter, rCCK0_System, bCCKSideBand,
			     (pHalData->nCur40MhzPrimeSC >> 1));
		PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0xC00,
			     pHalData->nCur40MhzPrimeSC);
		PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT(10), 0);

		PHY_SetBBReg(Adapter, 0x818, BIT(26) | BIT(27),
			     (pHalData->nCur40MhzPrimeSC ==
			      HAL_PRIME_CHNL_OFFSET_LOWER) ? 2:1);
		break;

	default:
		break;
	}
	/* Skip over setting of J-mode in BB register here. Default value
	   is "None J mode". Emily 20070315 */

	/*  Added it for 20/40 mhz switch time evaluation by guangan 070531 */
	/* NowL = PlatformEFIORead4Byte(Adapter, TSFR); */
	/* NowH = PlatformEFIORead4Byte(Adapter, TSFR+4); */
	/* EndTime = ((u64)NowH << 32) + NowL; */

	rtl8723a_phy_rf6052set_bw(Adapter, pHalData->CurrentChannelBW);
}

 /*-----------------------------------------------------------------------------
 * Function:   SetBWMode23a8190Pci()
 *
 * Overview:  This function is export to "HalCommon" moudule
 *
 * Input:		struct rtw_adapter *			Adapter
 *			enum ht_channel_width	Bandwidth	20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		We do not take j mode into consideration now
 *---------------------------------------------------------------------------*/
void
PHY_SetBWMode23a8723A(struct rtw_adapter *Adapter,
		   enum ht_channel_width Bandwidth, unsigned char Offset)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	enum ht_channel_width tmpBW = pHalData->CurrentChannelBW;

	pHalData->CurrentChannelBW = Bandwidth;

	pHalData->nCur40MhzPrimeSC = Offset;

	if ((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved))
		_PHY_SetBWMode23a92C(Adapter);
	else
		pHalData->CurrentChannelBW = tmpBW;
}

static void _PHY_SwChnl8723A(struct rtw_adapter *Adapter, u8 channel)
{
	enum RF_RADIO_PATH eRFPath;
	u32 param1, param2;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);

	/* s1. pre common command - CmdID_SetTxPowerLevel */
	PHY_SetTxPowerLevel8723A(Adapter, channel);

	/* s2. RF dependent command - CmdID_RF_WriteReg,
	   param1 = RF_CHNLBW, param2 = channel */
	param1 = RF_CHNLBW;
	param2 = channel;
	for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++) {
		pHalData->RfRegChnlVal[eRFPath] =
			(pHalData->RfRegChnlVal[eRFPath] & 0xfffffc00) | param2;
		PHY_SetRFReg(Adapter, eRFPath, param1,
			     bRFRegOffsetMask, pHalData->RfRegChnlVal[eRFPath]);
	}

	/* s3. post common command - CmdID_End, None */
}

void PHY_SwChnl8723A(struct rtw_adapter *Adapter, u8 channel)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	u8 tmpchannel = pHalData->CurrentChannel;
	bool  result = true;

	if (channel == 0)
		channel = 1;

	pHalData->CurrentChannel = channel;

	if ((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved)) {
		_PHY_SwChnl8723A(Adapter, channel);

		if (!result)
			pHalData->CurrentChannel = tmpchannel;
	} else {
		pHalData->CurrentChannel = tmpchannel;
	}
}
