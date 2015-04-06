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
 *
 ******************************************************************************/
/******************************************************************************

 Module:	rtl8192d_phycfg.c

 Note:		Merge 92DE/SDU PHY config as below
			1. BB register R/W API
			2. RF register R/W API
			3. Initial BB/RF/MAC config by reading BB/MAC/RF txt.
			3. Power setting API
			4. Channel switch API
			5. Initial gain switch API.
			6. Other BB/MAC/RF API.

 Function:	PHY: Extern function, phy: local function

 Export:	PHY_FunctionName

 Abbrev:	NONE

 History:
	Data		Who		Remark
	08/08/2008  MHC		1. Port from 9x series phycfg.c
						2. Reorganize code arch and ad description.
						3. Collect similar function.
						4. Seperate extern/local API.
	08/12/2008	MHC		We must merge or move USB PHY relative function later.
	10/07/2008	MHC		Add IQ calibration for PHY.(Only 1T2R mode now!!!)
	11/06/2008	MHC		Add TX Power index PG file to config in 0xExx register
						area to map with EEPROM/EFUSE tx pwr index.

******************************************************************************/
#define _HAL_8192D_PHYCFG_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_efuse.h>

#include <hal_intf.h>
#include <rtl8192d_hal.h>
#include <hal8192dphycfg.h>

/*---------------------------Define Local Constant---------------------------*/

/*------------------------Define global variable-----------------------------*/
#ifdef CONFIG_DUALMAC_CONCURRENT
extern atomic_t GlobalCounterForMutex;
#endif
/*------------------------Define local variable------------------------------*/

/*--------------------Define export function prototype-----------------------*/
/*  Please refer to header file */
/*--------------------Define export function prototype-----------------------*/

/*---------------------Define local function prototype-----------------------*/
static void
phy_PathAFillIQKMatrix(
	struct rtw_adapter *	adapter,
	bool	bIQKOK,
	int		result[][8],
	u8		final_candidate,
	bool	bTxOnly
	);

static void
phy_PathAFillIQKMatrix_5G_Normal(
	struct rtw_adapter *	adapter,
	bool	bIQKOK,
	int		result[][8],
	u8		final_candidate,
	bool	bTxOnly
	);

static void
phy_PathBFillIQKMatrix(
	struct rtw_adapter *	adapter,
	bool	bIQKOK,
	int		result[][8],
	u8		final_candidate,
	bool	bTxOnly
	);

static void
phy_PathBFillIQKMatrix_5G_Normal(
	struct rtw_adapter *	adapter,
	bool	bIQKOK,
	int		result[][8],
	u8		final_candidate,
	bool	bTxOnly
	);
/*----------------------------Function Body----------------------------------*/

static u8 GetRightChnlPlace(u8 chnl)
{
	u8	channel_5G[TARGET_CHNL_NUM_2G_5G] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,161,163,165};
	u8	place = chnl;

	if (chnl > 14)
	{
		for (place = 14; place<sizeof(channel_5G); place++)
		{
			if (channel_5G[place] == chnl)
			{
				place++;
				break;
			}
		}
	}

	return place;
}

static u8 GetChnlFromPlace(u8 place)
{
	u8	channel_5G[TARGET_CHNL_NUM_2G_5G] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,161,163,165};

	return channel_5G[place];
}

u8 rtl8192d_GetRightChnlPlaceforIQK(u8 chnl)
{
	u8	channel_all[TARGET_CHNL_NUM_2G_5G] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,161,163,165};
	u8	place = chnl;

	if (chnl > 14)
	{
		for (place = 14; place<sizeof(channel_all); place++)
		{
			if (channel_all[place] == chnl)
			{
				return place-13;
			}
		}
	}

	return 0;
}

/*  */
/*  1. BB register R/W API */
/*  */
/**
* Function:	phy_CalculateBitShift
*
* OverView:	Get shifted position of the BitMask
*
* Input:
*			u4Byte		BitMask,
*
* Output:	none
* Return:		u4Byte		Return the shift bit bit position of the mask
*/
static	u32
phy_CalculateBitShift(
	u32 BitMask
	)
{
	u32 i;

	for (i=0; i<=31; i++)
	{
		if (((BitMask>>i) &  0x1) == 1)
			break;
	}

	return (i);
}

/*  */
/* To avoid miswrite Reg0x800 for 92D */
/*  */
void
rtl8192d_PHY_SetBBReg1Byte(
	struct rtw_adapter *	adapter,
	u32		RegAddr,
	u32		BitMask,
	u32		Data
	)
{
	u32			OriginalValue, BitShift,offset = 0;
       u8			value=0;

#if (DISABLE_BB_RF == 1)
	return;
#endif
	/*  BitMask only support bit0~bit7 or bit8~bit15,bit16~bit23,bit24~bit31,should in 1 byte scale; */
	BitShift = phy_CalculateBitShift(BitMask);
	offset = BitShift /8;

	OriginalValue = rtw_read32(adapter, RegAddr);
	Data = ((OriginalValue & (~BitMask)) | ((Data << BitShift) & BitMask));

	value =(u8)(Data>>(8*offset));

	rtw_write8(adapter, RegAddr+offset, value);
}

/**
* Function:	PHY_QueryBBReg
*
* OverView:	Read "sepcific bits" from BB register
*
* Input:
*			struct rtw_adapter *		adapter,
*			u4Byte			RegAddr,	The target address to be readback
*			u4Byte			BitMask		The target bit position in the target address
*								to be readback
* Output:	None
* Return:		u4Byte			Data		The readback register value
* Note:		This function is equal to "GetRegSetting" in PHY programming guide
*/
u32
rtl8192d_PHY_QueryBBReg(
	struct rtw_adapter *	adapter,
	u32		RegAddr,
	u32		BitMask
	)
{
	u32	ReturnValue = 0, OriginalValue, BitShift;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	OriginalValue = rtw_read32(adapter, RegAddr);
	BitShift = phy_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;
	return (ReturnValue);
}

/**
* Function:	PHY_SetBBReg
*
* OverView:	Write "Specific bits" to BB register (page 8~)
*
* Input:
*			struct rtw_adapter *		adapter,
*			u4Byte			RegAddr,	The target address to be modified
*			u4Byte			BitMask		The target bit position in the target address
*								to be modified
*			u4Byte			Data		The new register value in the target bit position
*								of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRegSetting" in PHY programming guide
*/

void
rtl8192d_PHY_SetBBReg(
	struct rtw_adapter *	adapter,
	u32		RegAddr,
	u32		BitMask,
	u32		Data
	)
{
	u32	OriginalValue, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

	if (BitMask!= bMaskDWord)
	{/* if not "double word" write */
		OriginalValue = rtw_read32(adapter, RegAddr);
		BitShift = phy_CalculateBitShift(BitMask);
		Data = ((OriginalValue & (~BitMask)) | ((Data << BitShift) & BitMask));
	}

	rtw_write32(adapter, RegAddr, Data);
}

/*  */
/*  2. RF register R/W API */
/*  */
/*-----------------------------------------------------------------------------
 * Function:	phy_FwRFSerialRead()
 *
 * Overview:	We support firmware to execute RF-R/W.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	01/21/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static	u32
phy_FwRFSerialRead(
	struct rtw_adapter *			adapter,
	enum RF_RADIO_PATH_E	eRFPath,
	u32				Offset	)
{
	u32		retValue = 0;
	return	(retValue);
}	/* phy_FwRFSerialRead */

/**
* Function:	phy_RFSerialRead
*
* OverView:	Read regster from RF chips
*
* Input:
*			struct rtw_adapter *		adapter,
*			enum RF_RADIO_PATH_E	eRFPath,	Radio path of A/B/C/D
*			u4Byte			Offset,		The target address to be read
*
* Output:	None
* Return:		u4Byte			reback value
* Note:		Threre are three types of serial operations:
*			1. Software serial write
*			2. Hardware LSSI-Low Speed Serial Interface
*			3. Hardware HSSI-High speed
*			serial write. Driver need to implement (1) and (2).
*			This function is equal to the combination of RF_ReadReg() and  RFLSSIRead()
*/
static	u32
phy_RFSerialRead(
	struct rtw_adapter *			adapter,
	enum RF_RADIO_PATH_E	eRFPath,
	u32				Offset
	)
{
	u32	retValue = 0;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct bb_register_def *pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32	NewOffset;
	u32	tmplong,tmplong2;
	u8	RfPiEnable=0;
	u8	i;
	u32	MaskforPhySet=0;

	/*  */
	/*  Make sure RF register offset is correct */
	/*  */
	if (Offset & MAC1_ACCESS_PHY0)
		MaskforPhySet = MAC1_ACCESS_PHY0;
	else if (Offset & MAC0_ACCESS_PHY1)
		MaskforPhySet = MAC0_ACCESS_PHY1;

	Offset &=0x7F;

	/*  */
	/*  Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/*  For 92S LSSI Read RFLSSIRead */
	/*  For RF A/B write 0x824/82c(does not work in the future) */
	/*  We must use 0x824 for RF A and B to execute read trigger */
	tmplong = PHY_QueryBBReg(adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord);
	if (eRFPath == RF_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = PHY_QueryBBReg(adapter, pPhyReg->rfHSSIPara2|MaskforPhySet, bMaskDWord);

	tmplong2 = (tmplong2 & (~bLSSIReadAddress)) | (NewOffset<<23) | bLSSIReadEdge;	/* T65 RF */

	PHY_SetBBReg(adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord, tmplong&(~bLSSIReadEdge));
	rtw_udelay_os(10);

	PHY_SetBBReg(adapter, pPhyReg->rfHSSIPara2|MaskforPhySet, bMaskDWord, tmplong2);
	for (i = 0; i < 2; i++)
		rtw_udelay_os(MAX_STALL_TIME);
	PHY_SetBBReg(adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord, tmplong|bLSSIReadEdge);
	rtw_udelay_os(10);

	if (eRFPath == RF_PATH_A)
		RfPiEnable = (u8)PHY_QueryBBReg(adapter, rFPGA0_XA_HSSIParameter1|MaskforPhySet, BIT8);
	else if (eRFPath == RF_PATH_B)
		RfPiEnable = (u8)PHY_QueryBBReg(adapter, rFPGA0_XB_HSSIParameter1|MaskforPhySet, BIT8);

	if (RfPiEnable) {
		/*  Read from BBreg8b8, 12 bits for 8190, 20bits for T65 RF */
		retValue = PHY_QueryBBReg(adapter, pPhyReg->rfLSSIReadBackPi|MaskforPhySet, bLSSIReadBackData);
	} else {
		/* Read from BBreg8a0, 12 bits for 8190, 20 bits for T65 RF */
		retValue = PHY_QueryBBReg(adapter, pPhyReg->rfLSSIReadBack|MaskforPhySet, bLSSIReadBackData);
	}

	return retValue;
}

/**
* Function:	phy_RFSerialWrite
*
* OverView:	Write data to RF register (page 8~)
*
* Input:
*			struct rtw_adapter *		adapter,
*			enum RF_RADIO_PATH_E	eRFPath,	Radio path of A/B/C/D
*			u4Byte			Offset,		The target address to be read
*			u4Byte			Data		The new register Data in the target bit position
*								of the target to be read
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
	struct rtw_adapter *			adapter,
	enum RF_RADIO_PATH_E	eRFPath,
	u32				Offset,
	u32				Data
	)
{
	u32	DataAndAddr = 0;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct bb_register_def *pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32	NewOffset,MaskforPhySet=0;

	/*  2009/06/17 MH We can not execute IO for power save or other accident mode. */

	if (Offset & MAC1_ACCESS_PHY0)
		MaskforPhySet = MAC1_ACCESS_PHY0;
	else if (Offset & MAC0_ACCESS_PHY1)
		MaskforPhySet = MAC0_ACCESS_PHY1;

	Offset &=0x7F;

	/*  */
	/* 92D RF offset >0x3f */

	/*  */
	/*  Shadow Update */
	/*  */

	/*  */
	/*  Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/*  */
	/*  Put write addr in [5:0]  and write data in [31:16] */
	/*  */
	DataAndAddr = ((NewOffset<<20) | (Data&0x000fffff)) & 0x0fffffff;	/*  T65 RF */

	/*  */
	/*  Write Operation */
	/*  */
	PHY_SetBBReg(adapter, pPhyReg->rf3wireOffset|MaskforPhySet, bMaskDWord, DataAndAddr);
}

/**
* Function:	PHY_QueryRFReg
*
* OverView:	Query "Specific bits" to RF register (page 8~)
*
* Input:
*			struct rtw_adapter *		adapter,
*			enum RF_RADIO_PATH_E	eRFPath,	Radio path of A/B/C/D
*			u4Byte			RegAddr,	The target address to be read
*			u4Byte			BitMask		The target bit position in the target address
*								to be read
*
* Output:	None
* Return:		u4Byte			Readback value
* Note:		This function is equal to "GetRFRegSetting" in PHY programming guide
*/
u32
rtl8192d_PHY_QueryRFReg(
	struct rtw_adapter *			adapter,
	enum RF_RADIO_PATH_E	eRFPath,
	u32				RegAddr,
	u32				BitMask
	)
{
	u32 Original_Value, Readback_Value, BitShift;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	if (!pHalData->bPhyValueInitReady)
		return 0;

	if (pHalData->bReadRFbyFW)
		Original_Value = rtw_read32(adapter,(0x66<<24|eRFPath<<16)|RegAddr); /* 0x66 Just a identifier.by wl */
	else
		Original_Value = phy_RFSerialRead(adapter, eRFPath, RegAddr);

	BitShift =  phy_CalculateBitShift(BitMask);
	Readback_Value = (Original_Value & BitMask) >> BitShift;

	return (Readback_Value);
}

/**
* Function:	PHY_SetRFReg
*
* OverView:	Write "Specific bits" to RF register (page 8~)
*
* Input:
*			struct rtw_adapter *		adapter,
*			enum RF_RADIO_PATH_E	eRFPath,	Radio path of A/B/C/D
*			u4Byte			RegAddr,	The target address to be modified
*			u4Byte			BitMask		The target bit position in the target address
*								to be modified
*			u4Byte			Data		The new register Data in the target bit position
*								of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRFRegSetting" in PHY programming guide
*/
void
rtl8192d_PHY_SetRFReg(
	struct rtw_adapter *			adapter,
	enum RF_RADIO_PATH_E	eRFPath,
	u32				RegAddr,
	u32				BitMask,
	u32				Data
	)
{

	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u32			Original_Value, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

	if (!pHalData->bPhyValueInitReady)
		return;

	if (BitMask == 0)
		return;

	/*  RF data is 12 bits only */
	if (BitMask != bRFRegOffsetMask)
	{
		Original_Value = phy_RFSerialRead(adapter, eRFPath, RegAddr);
		BitShift =  phy_CalculateBitShift(BitMask);
		Data = (((Original_Value) & (~BitMask)) | (Data<< BitShift));
	}

	phy_RFSerialWrite(adapter, eRFPath, RegAddr, Data);
}

/*  */
/*  3. Initial MAC/BB/RF config by reading MAC/BB/RF txt. */
/*  */

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigMACWithParaFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write
 *
 * Input:	struct rtw_adapter *		adapter
 *			ps1Byte				pFileName
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		The format of MACPHY_REG.txt is different from PHY and RF.
 *			[Register][Mask][Value]
 *---------------------------------------------------------------------------*/
#ifndef CONFIG_EMBEDDED_FWIMG
static	int
phy_ConfigMACWithParaFile(
	struct rtw_adapter *		adapter,
	u8*			pFileName
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	int		rtStatus = _SUCCESS;

	return rtStatus;
}
#endif /* CONFIG_EMBEDDED_FWIMG */
/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigMACWithHeaderFile()
 *
 * Overview:    This function read BB parameters from Header file we gen, and do register
 *			  Read/Write
 *
 * Input:	struct rtw_adapter *		adapter
 *			ps1Byte				pFileName
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		The format of MACPHY_REG.txt is different from PHY and RF.
 *			[Register][Mask][Value]
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigMACWithHeaderFile(
	struct rtw_adapter *		adapter
)
{
	u32					i = 0;
	u32					ArrayLength = 0;
	u32*				ptrArray;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	/* 2008.11.06 Modified by tynli. */

	ArrayLength = Rtl8192D_MAC_ArrayLength;
	ptrArray = (u32 *)Rtl8192D_MAC_Array;

	for (i = 0 ;i < ArrayLength;i=i+2) { /*  Add by tynli for 2 column */
		rtw_write8(adapter, ptrArray[i], (u8)ptrArray[i+1]);
	}

	return _SUCCESS;
}

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
int PHY_MACConfig8192D(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
#ifndef CONFIG_EMBEDDED_FWIMG
	char		*pszMACRegFile;
#endif
	char		sz92DMACRegFile[] = RTL8192D_PHY_MACREG;
	int		rtStatus = _SUCCESS;

	if (adapter->bSurpriseRemoved) {
		rtStatus = _FAIL;
		return rtStatus;
	}

#ifndef CONFIG_EMBEDDED_FWIMG
	pszMACRegFile = sz92DMACRegFile;
#endif

	/*  */
	/*  Config MAC */
	/*  */
#ifdef CONFIG_EMBEDDED_FWIMG
	rtStatus = phy_ConfigMACWithHeaderFile(adapter);
#else

	/*  Not make sure EEPROM, add later */
	rtStatus = phy_ConfigMACWithParaFile(adapter, pszMACRegFile);
#endif

	if (pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY)
	{
		/* improve 2-stream TX EVM by Jenyu */
		/*  2010.07.13 AMPDU aggregation number 9 */
		rtw_write8(adapter, REG_MAX_AGGR_NUM, 0x0B); /* By tynli. 2010.11.18. */
	}
	else
		rtw_write8(adapter, REG_MAX_AGGR_NUM, 0x07); /* 92D need to test to decide the num. */

	return rtStatus;
}

/**
* Function:	phy_InitBBRFRegisterDefinition
*
* OverView:	Initialize Register definition offset for Radio Path A/B/C/D
*
* Input:
*			struct rtw_adapter *		adapter,
*
* Output:	None
* Return:		None
* Note:		The initialization value is constant and it should never be changes
*/
static	void
phy_InitBBRFRegisterDefinition(
	struct rtw_adapter *		adapter
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	/*  RF Interface Sowrtware Control */
	pHalData->PHYRegDef[RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; /*  16 LSBs if read 32-bit from 0x870 */
	pHalData->PHYRegDef[RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; /*  16 MSBs if read 32-bit from 0x870 (16-bit for 0x872) */
	pHalData->PHYRegDef[RF_PATH_C].rfintfs = rFPGA0_XCD_RFInterfaceSW;/*  16 LSBs if read 32-bit from 0x874 */
	pHalData->PHYRegDef[RF_PATH_D].rfintfs = rFPGA0_XCD_RFInterfaceSW;/*  16 MSBs if read 32-bit from 0x874 (16-bit for 0x876) */

	/*  RF Interface Readback Value */
	pHalData->PHYRegDef[RF_PATH_A].rfintfi = rFPGA0_XAB_RFInterfaceRB; /*  16 LSBs if read 32-bit from 0x8E0 */
	pHalData->PHYRegDef[RF_PATH_B].rfintfi = rFPGA0_XAB_RFInterfaceRB;/*  16 MSBs if read 32-bit from 0x8E0 (16-bit for 0x8E2) */
	pHalData->PHYRegDef[RF_PATH_C].rfintfi = rFPGA0_XCD_RFInterfaceRB;/*  16 LSBs if read 32-bit from 0x8E4 */
	pHalData->PHYRegDef[RF_PATH_D].rfintfi = rFPGA0_XCD_RFInterfaceRB;/*  16 MSBs if read 32-bit from 0x8E4 (16-bit for 0x8E6) */

	/*  RF Interface Output (and Enable) */
	pHalData->PHYRegDef[RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; /*  16 LSBs if read 32-bit from 0x860 */
	pHalData->PHYRegDef[RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; /*  16 LSBs if read 32-bit from 0x864 */

	/*  RF Interface (Output and)  Enable */
	pHalData->PHYRegDef[RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; /*  16 MSBs if read 32-bit from 0x860 (16-bit for 0x862) */
	pHalData->PHYRegDef[RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; /*  16 MSBs if read 32-bit from 0x864 (16-bit for 0x866) */

	/* Addr of LSSI. Wirte RF register by driver */
	pHalData->PHYRegDef[RF_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter; /* LSSI Parameter */
	pHalData->PHYRegDef[RF_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;

	/*  RF parameter */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSI_Select = rFPGA0_XAB_RFParameter;  /* BB Band Select */
	pHalData->PHYRegDef[RF_PATH_B].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	pHalData->PHYRegDef[RF_PATH_C].rfLSSI_Select = rFPGA0_XCD_RFParameter;
	pHalData->PHYRegDef[RF_PATH_D].rfLSSI_Select = rFPGA0_XCD_RFParameter;

	/*  Tx AGC Gain Stage (same for all path. Should we remove this?) */
	pHalData->PHYRegDef[RF_PATH_A].rfTxGainStage = rFPGA0_TxGainStage; /* Tx gain stage */
	pHalData->PHYRegDef[RF_PATH_B].rfTxGainStage = rFPGA0_TxGainStage; /* Tx gain stage */
	pHalData->PHYRegDef[RF_PATH_C].rfTxGainStage = rFPGA0_TxGainStage; /* Tx gain stage */
	pHalData->PHYRegDef[RF_PATH_D].rfTxGainStage = rFPGA0_TxGainStage; /* Tx gain stage */

	/*  Tranceiver A~D HSSI Parameter-1 */
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara1 = rFPGA0_XA_HSSIParameter1;  /* wire control parameter1 */
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara1 = rFPGA0_XB_HSSIParameter1;  /* wire control parameter1 */

	/*  Tranceiver A~D HSSI Parameter-2 */
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  /* wire control parameter2 */
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;  /* wire control parameter2 */

	/*  RF switch Control */
	pHalData->PHYRegDef[RF_PATH_A].rfSwitchControl = rFPGA0_XAB_SwitchControl; /* TR/Ant switch control */
	pHalData->PHYRegDef[RF_PATH_B].rfSwitchControl = rFPGA0_XAB_SwitchControl;
	pHalData->PHYRegDef[RF_PATH_C].rfSwitchControl = rFPGA0_XCD_SwitchControl;
	pHalData->PHYRegDef[RF_PATH_D].rfSwitchControl = rFPGA0_XCD_SwitchControl;

	/*  AGC control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfAGCControl1 = rOFDM0_XAAGCCore1;
	pHalData->PHYRegDef[RF_PATH_B].rfAGCControl1 = rOFDM0_XBAGCCore1;
	pHalData->PHYRegDef[RF_PATH_C].rfAGCControl1 = rOFDM0_XCAGCCore1;
	pHalData->PHYRegDef[RF_PATH_D].rfAGCControl1 = rOFDM0_XDAGCCore1;

	/*  AGC control 2 */
	pHalData->PHYRegDef[RF_PATH_A].rfAGCControl2 = rOFDM0_XAAGCCore2;
	pHalData->PHYRegDef[RF_PATH_B].rfAGCControl2 = rOFDM0_XBAGCCore2;
	pHalData->PHYRegDef[RF_PATH_C].rfAGCControl2 = rOFDM0_XCAGCCore2;
	pHalData->PHYRegDef[RF_PATH_D].rfAGCControl2 = rOFDM0_XDAGCCore2;

	/*  RX AFE control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfRxIQImbalance = rOFDM0_XARxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_B].rfRxIQImbalance = rOFDM0_XBRxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_C].rfRxIQImbalance = rOFDM0_XCRxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_D].rfRxIQImbalance = rOFDM0_XDRxIQImbalance;

	/*  RX AFE control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfRxAFE = rOFDM0_XARxAFE;
	pHalData->PHYRegDef[RF_PATH_B].rfRxAFE = rOFDM0_XBRxAFE;
	pHalData->PHYRegDef[RF_PATH_C].rfRxAFE = rOFDM0_XCRxAFE;
	pHalData->PHYRegDef[RF_PATH_D].rfRxAFE = rOFDM0_XDRxAFE;

	/*  Tx AFE control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfTxIQImbalance = rOFDM0_XATxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_B].rfTxIQImbalance = rOFDM0_XBTxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_C].rfTxIQImbalance = rOFDM0_XCTxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_D].rfTxIQImbalance = rOFDM0_XDTxIQImbalance;

	/*  Tx AFE control 2 */
	pHalData->PHYRegDef[RF_PATH_A].rfTxAFE = rOFDM0_XATxAFE;
	pHalData->PHYRegDef[RF_PATH_B].rfTxAFE = rOFDM0_XBTxAFE;
	pHalData->PHYRegDef[RF_PATH_C].rfTxAFE = rOFDM0_XCTxAFE;
	pHalData->PHYRegDef[RF_PATH_D].rfTxAFE = rOFDM0_XDTxAFE;

	/*  Tranceiver LSSI Readback SI mode */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_C].rfLSSIReadBack = rFPGA0_XC_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_D].rfLSSIReadBack = rFPGA0_XD_LSSIReadBack;

	/*  Tranceiver LSSI Readback PI mode */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBackPi = TransceiverA_HSPI_Readback;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBackPi = TransceiverB_HSPI_Readback;
	pHalData->bPhyValueInitReady = true;
}

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithHeaderFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write
 *
 * Input:	struct rtw_adapter *		adapter
 *			u1Byte			ConfigType     0 => PHY_CONFIG
 *										 1 =>AGC_TAB
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithHeaderFile(
	struct rtw_adapter *		adapter,
	u8			ConfigType
)
{
	int i;
	u32*	Rtl819XPHY_REGArray_Table;
	u32*	Rtl819XAGCTAB_Array_Table=NULL;
	u32*	Rtl819XAGCTAB_5GArray_Table=NULL;
	u16	PHY_REGArrayLen=0, AGCTAB_ArrayLen=0, AGCTAB_5GArrayLen=0;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	/* Normal chip,Mac0 use AGC_TAB.txt for 2G and 5G band. */
	if (pHalData->interfaceIndex == 0) {
		AGCTAB_ArrayLen = Rtl8192D_AGCTAB_ArrayLength;
		Rtl819XAGCTAB_Array_Table = (u32 *)Rtl8192D_AGCTAB_Array;
	} else {
		if (pHalData->CurrentBandType92D == BAND_ON_2_4G) {
			AGCTAB_ArrayLen = Rtl8192D_AGCTAB_2GArrayLength;
			Rtl819XAGCTAB_Array_Table = (u32 *)Rtl8192D_AGCTAB_2GArray;
		} else {
			AGCTAB_5GArrayLen = Rtl8192D_AGCTAB_5GArrayLength;
			Rtl819XAGCTAB_5GArray_Table = (u32 *)Rtl8192D_AGCTAB_5GArray;
		}
	}

	PHY_REGArrayLen = Rtl8192D_PHY_REG_2TArrayLength;
	Rtl819XPHY_REGArray_Table = (u32 *)Rtl8192D_PHY_REG_2TArray;

	if (ConfigType == BaseBand_Config_PHY_REG) {
		for (i = 0; i < PHY_REGArrayLen; i = i+2) {
			if (Rtl819XPHY_REGArray_Table[i] == 0xfe || Rtl819XPHY_REGArray_Table[i] == 0xffe) {
				#ifdef CONFIG_LONG_DELAY_ISSUE
				rtw_msleep_os(50);
				#else
				rtw_mdelay_os(50);
				#endif
			}
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfd)
				rtw_mdelay_os(5);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfc)
				rtw_mdelay_os(1);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfb)
				rtw_udelay_os(50);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xfa)
				rtw_udelay_os(5);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xf9)
				rtw_udelay_os(1);
			else if (Rtl819XPHY_REGArray_Table[i] == 0xa24)
				pdmpriv->RegA24 = Rtl819XPHY_REGArray_Table[i+1];
			PHY_SetBBReg(adapter, Rtl819XPHY_REGArray_Table[i], bMaskDWord, Rtl819XPHY_REGArray_Table[i+1]);

			/*  Add 1us delay between BB/RF register setting. */
			rtw_udelay_os(1);
		}
	} else if (ConfigType == BaseBand_Config_AGC_TAB) {
		/* especial for 5G, vivi, 20100528 */
		if (pHalData->interfaceIndex == 0) {
			for (i = 0; i < AGCTAB_ArrayLen; i = i+2) {
				PHY_SetBBReg(adapter, Rtl819XAGCTAB_Array_Table[i], bMaskDWord, Rtl819XAGCTAB_Array_Table[i+1]);

				/*  Add 1us delay between BB/RF register setting. */
				rtw_udelay_os(1);

			}
		} else {
			if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
			{
				for (i=0;i<AGCTAB_ArrayLen;i=i+2)
				{
					PHY_SetBBReg(adapter, Rtl819XAGCTAB_Array_Table[i], bMaskDWord, Rtl819XAGCTAB_Array_Table[i+1]);

					/*  Add 1us delay between BB/RF register setting. */
					rtw_udelay_os(1);
				}
			} else {
				for (i=0;i<AGCTAB_5GArrayLen;i=i+2)
				{
					PHY_SetBBReg(adapter, Rtl819XAGCTAB_5GArray_Table[i], bMaskDWord, Rtl819XAGCTAB_5GArray_Table[i+1]);

					/*  Add 1us delay between BB/RF register setting. */
					rtw_udelay_os(1);
				}
			}
		}
	}

	return _SUCCESS;
}

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithParaFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write
 *
 * Input:	struct rtw_adapter *		adapter
 *			ps1Byte				pFileName
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *	2008/11/06	MH	For 92S we do not support silent reset now. Disable
 *					parameter file compare!!!!!!??
 *
 *---------------------------------------------------------------------------*/
#ifndef CONFIG_EMBEDDED_FWIMG
static	int
phy_ConfigBBWithParaFile(
	struct rtw_adapter *		adapter,
	u8*			pFileName
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	int		rtStatus = _SUCCESS;

	return rtStatus;
}
#endif /* CONFIG_EMBEDDED_FWIMG */
#if MP_DRIVER != 1
static void
storePwrIndexDiffRateOffset(
	struct rtw_adapter *	adapter,
	u32		RegAddr,
	u32		BitMask,
	u32		Data
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

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
	if (RegAddr == rTxAGC_A_Mcs15_Mcs12)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][5] = Data;
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
 * When			Who		Remark
 * 11/06/2008	MHC		Add later!!!!!!.. Please modify for new files!!!!
 * 11/10/2008	tynli		Modify to mew files.
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithPgHeaderFile(
	struct rtw_adapter *		adapter,
	u8			ConfigType)
{
	int i;
	u32*	Rtl819XPHY_REGArray_Table_PG;
	u16	PHY_REGArrayPGLen;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	PHY_REGArrayPGLen = Rtl8192D_PHY_REG_Array_PGLength;
	Rtl819XPHY_REGArray_Table_PG = (u32 *)Rtl8192D_PHY_REG_Array_PG;

	if (ConfigType == BaseBand_Config_PHY_REG) {
		for (i = 0; i < PHY_REGArrayPGLen; i = i+3) {
			storePwrIndexDiffRateOffset(adapter, Rtl819XPHY_REGArray_Table_PG[i],
				Rtl819XPHY_REGArray_Table_PG[i+1],
				Rtl819XPHY_REGArray_Table_PG[i+2]);
		}
	}

	return _SUCCESS;
}	/* phy_ConfigBBWithPgHeaderFile */
#endif

/*-----------------------------------------------------------------------------
 * Function:	phy_ConfigBBWithPgParaFile
 *
 * Overview:
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/06/2008	MHC		Create Version 0.
 * 2009/07/29	tynli		(porting from 92SE branch)2009/03/11 Add copy parameter file to buffer for silent reset
 *---------------------------------------------------------------------------*/
#ifndef CONFIG_EMBEDDED_FWIMG
static	int
phy_ConfigBBWithPgParaFile(
	struct rtw_adapter *		adapter,
	u8*			pFileName)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	int		rtStatus = _SUCCESS;

	return rtStatus;
}	/* phy_ConfigBBWithPgParaFile */
#endif /* CONFIG_EMBEDDED_FWIMG */
#if MP_DRIVER == 1
#ifndef CONFIG_EMBEDDED_FWIMG
/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithMpParaFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write
 *
 * Input:	struct rtw_adapter *		adapter
 *			ps1Byte				pFileName
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *	2008/11/06	MH	For 92S we do not support silent reset now. Disable
 *					parameter file compare!!!!!!??
 *
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithMpParaFile(
	struct rtw_adapter *	adapter,
	s8			*pFileName
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	int	rtStatus = _SUCCESS;

	return rtStatus;
}
#else
/*-----------------------------------------------------------------------------
 * Function:	phy_ConfigBBWithMpHeaderFile
 *
 * Overview:	Config PHY_REG_MP array
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 02/04/2010	chiyokolin		Modify to new files.
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithMpHeaderFile(
	struct rtw_adapter *		adapter,
	u1Byte			ConfigType)
{
	int	i;
	u32	*Rtl8192CPHY_REGArray_Table_MP;
	u16	PHY_REGArrayMPLen;

	PHY_REGArrayMPLen = Rtl8192D_PHY_REG_Array_MPLength;
	Rtl8192CPHY_REGArray_Table_MP = (u32 *)Rtl8192D_PHY_REG_Array_MP;

	if (ConfigType == BaseBand_Config_PHY_REG)
	{
		for (i=0;i<PHY_REGArrayMPLen;i=i+2)
		{
			if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfe) {
				#ifdef CONFIG_LONG_DELAY_ISSUE
				rtw_msleep_os(50);
				#else
				rtw_mdelay_os(50);
				#endif
			}
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfd)
				rtw_mdelay_os(5);
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfc)
				rtw_mdelay_os(1);
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfb)
				rtw_udelay_os(50);
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfa)
				rtw_udelay_os(5);
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xf9)
				rtw_udelay_os(1);
			PHY_SetBBReg(adapter, Rtl8192CPHY_REGArray_Table_MP[i], bMaskDWord, Rtl8192CPHY_REGArray_Table_MP[i+1]);

			/*  Add 1us delay between BB/RF register setting. */

			rtw_udelay_os(1);
		}
	}
	return _SUCCESS;
}	/* phy_ConfigBBWithPgHeaderFile */

#endif
#endif

static	int
phy_BB8192D_Config_ParaFile(
	struct rtw_adapter *	adapter
	)
{
#if MP_DRIVER != 1
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(adapter);
#endif
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	int		rtStatus = _SUCCESS;
	s8		sz92DBBRegFile[] = RTL8192D_PHY_REG;
	s8		sz92DBBRegPgFile[] = RTL8192D_PHY_REG_PG;
	s8		sz92DBBRegMpFile[] = RTL8192D_PHY_REG_MP;
	s8		sz92DAGCTableFile[] = RTL8192D_AGC_TAB;
	s8		sz92D2GAGCTableFile[] = RTL8192D_AGC_TAB_2G;
	s8		sz92D5GAGCTableFile[] = RTL8192D_AGC_TAB_5G;
#ifndef CONFIG_EMBEDDED_FWIMG
	char		*pszBBRegFile;
	char *pszAGCTableFile;
	char *pszBBRegPgFile;
	char *pszBBRegMpFile;
#endif

#ifndef CONFIG_EMBEDDED_FWIMG
	pszBBRegFile = sz92DBBRegFile;
	pszBBRegPgFile = sz92DBBRegPgFile;

	/* Normal chip,Mac0 use AGC_TAB.txt for 2G and 5G band. */
	if (pHalData->interfaceIndex == 0) {
		pszAGCTableFile = sz92DAGCTableFile;
	} else {
		if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
			pszAGCTableFile = sz92D2GAGCTableFile;
		else
			pszAGCTableFile = sz92D5GAGCTableFile;
	}
	pszBBRegMpFile = sz92DBBRegMpFile;
#endif

	/*  1. Read PHY_REG.TXT BB INIT!! */
	/*  We will seperate as 88C / 92C according to chip version */
	/*  */
#ifdef CONFIG_EMBEDDED_FWIMG
	rtStatus = phy_ConfigBBWithHeaderFile(adapter, BaseBand_Config_PHY_REG);
#else
	/*  No matter what kind of CHIP we always read PHY_REG.txt. We must copy different */
	/*  type of parameter files to phy_reg.txt at first. */
	rtStatus = phy_ConfigBBWithParaFile(adapter,pszBBRegFile);
#endif

	if (rtStatus != _SUCCESS)
		goto phy_BB8190_Config_ParaFile_Fail;

#if MP_DRIVER == 1
	/*  */
	/*  1.1 Read PHY_REG_MP.TXT BB INIT!! */
	/*  We will seperate as 88C / 92C according to chip version */
	/*  */
#ifdef CONFIG_EMBEDDED_FWIMG
	rtStatus = phy_ConfigBBWithMpHeaderFile(adapter, BaseBand_Config_PHY_REG);
#else
	/*  No matter what kind of CHIP we always read PHY_REG.txt. We must copy different */
	/*  type of parameter files to phy_reg.txt at first. */
	rtStatus = phy_ConfigBBWithMpParaFile(adapter,pszBBRegMpFile);
#endif

	if (rtStatus != _SUCCESS)
		goto phy_BB8190_Config_ParaFile_Fail;
#endif

#if MP_DRIVER != 1
	/*  */
	/*  2. If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt */
	/*  */
	if (pEEPROM->bautoload_fail_flag == false)
	{
		pHalData->pwrGroupCnt = 0;

#ifdef CONFIG_EMBEDDED_FWIMG
		rtStatus = phy_ConfigBBWithPgHeaderFile(adapter, BaseBand_Config_PHY_REG);
#else
		rtStatus = phy_ConfigBBWithPgParaFile(adapter, pszBBRegPgFile);
#endif
	}

	if (rtStatus != _SUCCESS)
		goto phy_BB8190_Config_ParaFile_Fail;
#endif

	/*  */
	/*  3. BB AGC table Initialization */
	/*  */
#ifdef CONFIG_EMBEDDED_FWIMG
#ifdef CONFIG_DUALMAC_CONCURRENT
	if (pHalData->bSlaveOfDMSP)
	{
		DBG_8192D("BB config slave skip  2222\n");
	}
	else
#endif
	{
		rtStatus = phy_ConfigBBWithHeaderFile(adapter, BaseBand_Config_AGC_TAB);
	}
#else
	rtStatus = phy_ConfigBBWithParaFile(adapter, pszAGCTableFile);
#endif

	if (rtStatus != _SUCCESS)
		goto phy_BB8190_Config_ParaFile_Fail;

	/*  Check if the CCK HighPower is turned ON. */
	/*  This is used to calculate PWDB. */
	pHalData->bCckHighPower = (bool)(PHY_QueryBBReg(adapter, rFPGA0_XA_HSSIParameter2, 0x200));

phy_BB8190_Config_ParaFile_Fail:

	return rtStatus;
}

int
PHY_BBConfig8192D(
	struct rtw_adapter *	adapter
	)
{
	int	rtStatus = _SUCCESS;
	/* u8		PathMap = 0, index = 0, rf_num = 0; */
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u32	RegVal;
	u8	value;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;

	if (adapter->bSurpriseRemoved) {
		rtStatus = _FAIL;
		return rtStatus;
	}

	phy_InitBBRFRegisterDefinition(adapter);

	/*  Enable BB and RF */
	RegVal = rtw_read16(adapter, REG_SYS_FUNC_EN);
	rtw_write16(adapter, REG_SYS_FUNC_EN, RegVal|BIT13|BIT0|BIT1);

	/*  20090923 Joseph: Advised by Steven and Jenyu. Power sequence before init RF. */
	rtw_write8(adapter, REG_AFE_PLL_CTRL, 0x83);
	rtw_write8(adapter, REG_AFE_PLL_CTRL+1, 0xdb);
	value=rtw_read8(adapter, REG_RF_CTRL);     /*   0x1f bit7 bit6 represent for mac0/mac1 driver ready */
	rtw_write8(adapter, REG_RF_CTRL, value|RF_EN|RF_RSTB|RF_SDMRSTB);

	rtw_write8(adapter, REG_SYS_FUNC_EN, FEN_USBA | FEN_USBD | FEN_BB_GLB_RSTn | FEN_BBRSTB);
	/* undo clock gated */
	rtw_write32(adapter, rFPGA0_XCD_RFParameter, rtw_read32(adapter, rFPGA0_XCD_RFParameter)&(~BIT31));
	/* To Fix MAC loopback mode fail. Suggested by SD4 Johnny. 2010.03.23. */
	rtw_write8(adapter, REG_LDOHCI12_CTRL, 0x0f);
	rtw_write8(adapter, 0x15, 0xe9);

	rtw_write8(adapter, REG_AFE_XTAL_CTRL+1, 0x80);

	/*  */
	/*  Config BB and AGC */
	/*  */
	rtStatus = phy_BB8192D_Config_ParaFile(adapter);

	/* Crystal Calibration */
	PHY_SetBBReg(adapter, 0x24, 0xF0, pHalData->CrystalCap & 0x0F);
	PHY_SetBBReg(adapter, 0x28, 0xF0000000, ((pHalData->CrystalCap & 0xF0) >> 4));

	/* to save power for special 1T1R */
	if (pregistrypriv->special_rf_path == 1)
	{
		PHY_SetBBReg(adapter, rFPGA0_XCD_SwitchControl, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rBlue_Tooth, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rRx_Wait_CCA, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rTx_CCK_RFON, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rTx_CCK_BBON, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rTx_OFDM_RFON, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rTx_OFDM_BBON, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rTx_To_Rx, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rTx_To_Tx, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rRx_CCK, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rRx_OFDM, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rRx_Wait_RIFS, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rRx_TO_Rx, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rStandby, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rSleep, BIT24|BIT25|BIT27|BIT30, 0);
		PHY_SetBBReg(adapter, rPMPD_ANAEN, BIT24|BIT25|BIT27|BIT30, 0);
	}
	else if (pregistrypriv->special_rf_path == 2)
	{
		PHY_SetBBReg(adapter, rFPGA0_XCD_SwitchControl, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rBlue_Tooth, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rRx_Wait_CCA, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rTx_CCK_RFON, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rTx_CCK_BBON, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rTx_OFDM_RFON, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rTx_OFDM_BBON, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rTx_To_Rx, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rTx_To_Tx, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rRx_CCK, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rRx_OFDM, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rRx_Wait_RIFS, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rRx_TO_Rx, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rStandby, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rSleep, BIT22|BIT23|BIT26|BIT29, 0);
		PHY_SetBBReg(adapter, rPMPD_ANAEN, BIT22|BIT23|BIT26|BIT29, 0);
	}

	return rtStatus;
}

int
PHY_RFConfig8192D(
	struct rtw_adapter *	adapter
	)
{
	int		rtStatus = _SUCCESS;

	if (adapter->bSurpriseRemoved) {
		rtStatus = _FAIL;
		return rtStatus;
	}

	/*  */
	/*  RF config */
	/*  */
	rtStatus = PHY_RF6052_Config8192D(adapter);
	return rtStatus;
}

/*  */
/*-----------------------------------------------------------------------------
 * Function:    PHY_ConfigRFWithHeaderFile()
 *
 * Overview:    This function read RF parameters from general file format, and do RF 3-wire
 *
 * Input:	struct rtw_adapter *			adapter
 *			ps1Byte					pFileName
 *			enum RF_RADIO_PATH_E	eRFPath
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		Delay may be required for RF configuration
 *---------------------------------------------------------------------------*/
int
rtl8192d_PHY_ConfigRFWithHeaderFile(
	struct rtw_adapter *			adapter,
	enum RF_CONTENT			Content,
	enum RF_RADIO_PATH_E		eRFPath
)
{
	int	i, j;
	int	rtStatus = _SUCCESS;
	u32*	Rtl819XRadioA_Array_Table;
	u32*	Rtl819XRadioB_Array_Table;
	u16		RadioA_ArrayLen,RadioB_ArrayLen;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u32	MaskforPhySet= (u32)(Content&0xE000);

	Content &= 0x1FFF;

	DBG_8192D(" ===> PHY_ConfigRFWithHeaderFile() intferace = %d, Radio_txt = 0x%x, eRFPath = %d,MaskforPhyAccess:0x%x.\n", pHalData->interfaceIndex, Content,eRFPath,MaskforPhySet);

	RadioA_ArrayLen = Rtl8192D_RadioA_2TArrayLength;
	Rtl819XRadioA_Array_Table = (u32 *)Rtl8192D_RadioA_2TArray;
	RadioB_ArrayLen = Rtl8192D_RadioB_2TArrayLength;
	Rtl819XRadioB_Array_Table = (u32 *)Rtl8192D_RadioB_2TArray;

	if (pHalData->InternalPA5G[0])
	{
		RadioA_ArrayLen = Rtl8192D_RadioA_2T_intPAArrayLength;
		Rtl819XRadioA_Array_Table = (u32 *)Rtl8192D_RadioA_2T_intPAArray;
	}

	if (pHalData->InternalPA5G[1])
	{
		RadioB_ArrayLen = Rtl8192D_RadioB_2T_intPAArrayLength;
		Rtl819XRadioB_Array_Table = (u32 *)Rtl8192D_RadioB_2T_intPAArray;
	}

	rtStatus = _SUCCESS;

	/* vivi added this for read parameter from header, 20100908 */
	/* 1this only happens when DMDP, mac0 start on 2.4G, mac1 start on 5G, */
	/* 1mac 0 has to set phy0&phy1 pathA or mac1 has to set phy0&phy1 pathA */
	if ((Content == radiob_txt)&&(eRFPath == RF_PATH_A)) {
		RadioA_ArrayLen = RadioB_ArrayLen;
		Rtl819XRadioA_Array_Table = Rtl819XRadioB_Array_Table;
	}

	switch (eRFPath) {
		case RF_PATH_A:
			for (i = 0;i<RadioA_ArrayLen; i=i+2)
			{
				if (Rtl819XRadioA_Array_Table[i] == 0xfe)
				{
					#ifdef CONFIG_LONG_DELAY_ISSUE
					rtw_msleep_os(50);
					#else
					rtw_mdelay_os(50);
					#endif
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xfd)
				{
					for (j=0;j<100;j++)
						rtw_udelay_os(MAX_STALL_TIME);
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xfc)
				{
					for (j=0;j<20;j++)
						rtw_udelay_os(MAX_STALL_TIME);
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xfb)
				{
					rtw_udelay_os(50);
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xfa)
				{
					rtw_udelay_os(5);
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xf9)
				{
					rtw_udelay_os(1);
				}
				else
				{
					PHY_SetRFReg(adapter, eRFPath, Rtl819XRadioA_Array_Table[i]|MaskforPhySet, bRFRegOffsetMask, Rtl819XRadioA_Array_Table[i+1]);
					/*  Add 1us delay between BB/RF register setting. */
					rtw_udelay_os(1);
				}
			}
			break;
		case RF_PATH_B:
			for (i = 0;i<RadioB_ArrayLen; i=i+2)
			{
				if (Rtl819XRadioB_Array_Table[i] == 0xfe)
				{ /*  Deay specific ms. Only RF configuration require delay. */
					#ifdef CONFIG_LONG_DELAY_ISSUE
					rtw_msleep_os(50);
					#else
					rtw_mdelay_os(50);
					#endif
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xfd)
				{
					/* rtw_mdelay_os(5); */
					for (j=0;j<100;j++)
						rtw_udelay_os(MAX_STALL_TIME);
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xfc)
				{
					/* rtw_mdelay_os(1); */
					for (j=0;j<20;j++)
						rtw_udelay_os(MAX_STALL_TIME);
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xfb)
				{
					rtw_udelay_os(50);
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xfa)
				{
					rtw_udelay_os(5);
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xf9)
				{
					rtw_udelay_os(1);
				}
				else
				{
					PHY_SetRFReg(adapter, eRFPath, Rtl819XRadioB_Array_Table[i]|MaskforPhySet, bRFRegOffsetMask, Rtl819XRadioB_Array_Table[i+1]);
					/*  Add 1us delay between BB/RF register setting. */
					rtw_udelay_os(1);
				}
			}
			break;
		case RF_PATH_C:
			break;
		case RF_PATH_D:
			break;
	}

	return rtStatus;
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_CheckBBAndRFOK()
 *
 * Overview:    This function is write register and then readback to make sure whether
 *			  BB[PHY0, PHY1], RF[Patha, path b, path c, path d] is Ok
 *
 * Input:	struct rtw_adapter *			adapter
 *			enum HW90_BLOCK		CheckBlock
 *			enum RF_RADIO_PATH_E	eRFPath		it is used only when CheckBlock is HW90_BLOCK_RF
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: PHY is OK
 *
 * Note:		This function may be removed in the ASIC
 *---------------------------------------------------------------------------*/
int
rtl8192d_PHY_CheckBBAndRFOK(
	struct rtw_adapter *			adapter,
	enum HW90_BLOCK		CheckBlock,
	enum RF_RADIO_PATH_E	eRFPath
	)
{
	int			rtStatus = _SUCCESS;

	u32				i, CheckTimes = 4,ulRegRead=0;

	u32				WriteAddr[4];
	u32				WriteData[] = {0xfffff027, 0xaa55a02f, 0x00000027, 0x55aa502f};

	/*  Initialize register address offset to be checked */
	WriteAddr[HW90_BLOCK_MAC] = 0x100;
	WriteAddr[HW90_BLOCK_PHY0] = 0x900;
	WriteAddr[HW90_BLOCK_PHY1] = 0x800;
	WriteAddr[HW90_BLOCK_RF] = 0x3;

	for (i=0 ; i < CheckTimes ; i++)
	{

		/*  */
		/*  Write Data to register and readback */
		/*  */
		switch (CheckBlock)
		{
		case HW90_BLOCK_MAC:
			break;

		case HW90_BLOCK_PHY0:
		case HW90_BLOCK_PHY1:
			rtw_write32(adapter, WriteAddr[CheckBlock], WriteData[i]);
			ulRegRead = rtw_read32(adapter, WriteAddr[CheckBlock]);
			break;

		case HW90_BLOCK_RF:
			/*  When initialization, we want the delay function(delay_ms(), delay_us() */
			/*  ==> actually we call PlatformStallExecution()) to do NdisStallExecution() */
			/*  [busy wait] instead of NdisMSleep(). So we acquire RT_INITIAL_SPINLOCK */
			/*  to run at Dispatch level to achive it. */
			/* cosa PlatformAcquireSpinLock(adapter, RT_INITIAL_SPINLOCK); */
			WriteData[i] &= 0xfff;
			PHY_SetRFReg(adapter, eRFPath, WriteAddr[HW90_BLOCK_RF], bRFRegOffsetMask, WriteData[i]);
			/*  TODO: we should not delay for such a long time. Ask SD3 */
			rtw_mdelay_os(10);
			ulRegRead = PHY_QueryRFReg(adapter, eRFPath, WriteAddr[HW90_BLOCK_RF], bRFRegOffsetMask);
			rtw_mdelay_os(10);
			/* cosa PlatformReleaseSpinLock(adapter, RT_INITIAL_SPINLOCK); */
			break;

		default:
			rtStatus = _FAIL;
			break;
		}

		/*  */
		/*  Check whether readback data is correct */
		/*  */
		if (ulRegRead != WriteData[i])
		{
			rtStatus = _FAIL;
			break;
		}
	}

	return rtStatus;
}

void
rtl8192d_PHY_GetHWRegOriginalValue(
	struct rtw_adapter *		adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	/*  read rx initial gain */
	pHalData->DefaultInitialGain[0] = (u8)PHY_QueryBBReg(adapter, rOFDM0_XAAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[1] = (u8)PHY_QueryBBReg(adapter, rOFDM0_XBAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[2] = (u8)PHY_QueryBBReg(adapter, rOFDM0_XCAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[3] = (u8)PHY_QueryBBReg(adapter, rOFDM0_XDAGCCore1, bMaskByte0);

	/*  read framesync */
	pHalData->framesync = (u8)PHY_QueryBBReg(adapter, rOFDM0_RxDetector3, bMaskByte0);
	pHalData->framesyncC34 = PHY_QueryBBReg(adapter, rOFDM0_RxDetector2, bMaskDWord);
}

/*  */
/*	Description: */
/*		Map dBm into Tx power index according to */
/*		current HW model, for example, RF and PA, and */
/*		current wireless mode. */
/*	By Bruce, 2008-01-29. */
/*  */
static	u8
phy_DbmToTxPwrIdx(
	struct rtw_adapter *		adapter,
	enum WIRELESS_MODE	WirelessMode,
	int			PowerInDbm
	)
{
	u8				TxPwrIdx = 0;
	int				Offset = 0;

	/*  */
	/*  Tested by MP, we found that CCK Index 0 equals to 8dbm, OFDM legacy equals to */
	/*  3dbm, and OFDM HT equals to 0dbm repectively. */
	/*  Note: */
	/*	The mapping may be different by different NICs. Do not use this formula for what needs accurate result. */
	/*  By Bruce, 2008-01-29. */
	/*  */
	switch (WirelessMode)
	{
	case WIRELESS_MODE_B:
		Offset = -7;
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		Offset = -8;
		break;

	default:
		break;
	}

	if ((PowerInDbm - Offset) > 0)
	{
		TxPwrIdx = (u8)((PowerInDbm - Offset) * 2);
	}
	else
	{
		TxPwrIdx = 0;
	}

	/*  Tx Power Index is too large. */
	if (TxPwrIdx > MAX_TXPWR_IDX_NMODE_92S)
		TxPwrIdx = MAX_TXPWR_IDX_NMODE_92S;

	return TxPwrIdx;
}

/*  */
/*	Description: */
/*		Map Tx power index into dBm according to */
/*		current HW model, for example, RF and PA, and */
/*		current wireless mode. */
/*	By Bruce, 2008-01-29. */
/*  */
static int
phy_TxPwrIdxToDbm(
	struct rtw_adapter *		adapter,
	enum WIRELESS_MODE	WirelessMode,
	u8			TxPwrIdx
	)
{
	int				Offset = 0;
	int				PwrOutDbm = 0;

	/*  */
	/*  Tested by MP, we found that CCK Index 0 equals to -7dbm, OFDM legacy equals to -8dbm. */
	/*  Note: */
	/*	The mapping may be different by different NICs. Do not use this formula for what needs accurate result. */
	/*  By Bruce, 2008-01-29. */
	/*  */
	switch (WirelessMode)
	{
	case WIRELESS_MODE_B:
		Offset = -7;
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		Offset = -8;
		break;

	default:
		break;
	}

	PwrOutDbm = TxPwrIdx / 2 + Offset; /*  Discard the decimal part. */

	return PwrOutDbm;
}

/*-----------------------------------------------------------------------------
 * Function:    GetTxPowerLevel8190()
 *
 * Overview:    This function is export to "common" moudule
 *
 * Input:       struct rtw_adapter *		adapter
 *			psByte			Power Level
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 *---------------------------------------------------------------------------*/
void
PHY_GetTxPowerLevel8192D(
	struct rtw_adapter *		adapter,
	u32*		powerlevel
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8			TxPwrLevel = 0;
	int			TxPwrDbm;

	/*  */
	/*  Because the Tx power indexes are different, we report the maximum of them to */
	/*  meet the CCX TPC request. By Bruce, 2008-01-31. */
	/*  */

	/*  CCK */
	TxPwrLevel = pHalData->CurrentCckTxPwrIdx;
	TxPwrDbm = phy_TxPwrIdxToDbm(adapter, WIRELESS_MODE_B, TxPwrLevel);

	/*  Legacy OFDM */
	TxPwrLevel = pHalData->CurrentOfdm24GTxPwrIdx + pHalData->LegacyHTTxPowerDiff;

	/*  Compare with Legacy OFDM Tx power. */
	if (phy_TxPwrIdxToDbm(adapter, WIRELESS_MODE_G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(adapter, WIRELESS_MODE_G, TxPwrLevel);

	/*  HT OFDM */
	TxPwrLevel = pHalData->CurrentOfdm24GTxPwrIdx;

	/*  Compare with HT OFDM Tx power. */
	if (phy_TxPwrIdxToDbm(adapter, WIRELESS_MODE_N_24G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(adapter, WIRELESS_MODE_N_24G, TxPwrLevel);

	*powerlevel = TxPwrDbm;
}

static void getTxPowerIndex(
	struct rtw_adapter *		adapter,
	u8			channel,
	u8*		cckPowerLevel,
	u8*		ofdmPowerLevel
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	index = (channel -1);

	/*  1. CCK */
	if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{
		cckPowerLevel[RF_PATH_A] = pHalData->TxPwrLevelCck[RF_PATH_A][index];	/* RF-A */
		cckPowerLevel[RF_PATH_B] = pHalData->TxPwrLevelCck[RF_PATH_B][index];	/* RF-B */
	}
	else
		cckPowerLevel[RF_PATH_A] = cckPowerLevel[RF_PATH_B] = 0;

	/*  2. OFDM for 1S or 2S */
	if (GET_RF_TYPE(adapter) == RF_1T2R || GET_RF_TYPE(adapter) == RF_1T1R)
	{
		/*  Read HT 40 OFDM TX power */
		ofdmPowerLevel[RF_PATH_A] = pHalData->TxPwrLevelHT40_1S[RF_PATH_A][index];
		ofdmPowerLevel[RF_PATH_B] = pHalData->TxPwrLevelHT40_1S[RF_PATH_B][index];
	}
	else if (GET_RF_TYPE(adapter) == RF_2T2R)
	{
		/*  Read HT 40 OFDM TX power */
		ofdmPowerLevel[RF_PATH_A] = pHalData->TxPwrLevelHT40_2S[RF_PATH_A][index];
		ofdmPowerLevel[RF_PATH_B] = pHalData->TxPwrLevelHT40_2S[RF_PATH_B][index];
	}
}

static void ccxPowerIndexCheck(
	struct rtw_adapter *		adapter,
	u8			channel,
	u8*		cckPowerLevel,
	u8*		ofdmPowerLevel
	)
{
}
/*-----------------------------------------------------------------------------
 * Function:    SetTxPowerLevel8190()
 *
 * Overview:    This function is export to "HalCommon" moudule
 *			We must consider RF path later!!!!!!!
 *
 * Input:       struct rtw_adapter *		adapter
 *			u1Byte		channel
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
PHY_SetTxPowerLevel8192D(
	struct rtw_adapter *		adapter,
	u8			channel
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	cckPowerLevel[2], ofdmPowerLevel[2];	/*  [0]:RF-A, [1]:RF-B */

#if (MP_DRIVER == 1)
	return;
#endif

	if ((adapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS)&&(adapter_to_dvobj(adapter)->ishighspeed == false))
		return;

	if (pHalData->bTXPowerDataReadFromEEPORM == false)
		return;

	channel = GetRightChnlPlace(channel);

	getTxPowerIndex(adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0]);

	if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
		ccxPowerIndexCheck(adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0]);

	if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
		rtl8192d_PHY_RF6052SetCckTxPower(adapter, &cckPowerLevel[0]);
	rtl8192d_PHY_RF6052SetOFDMTxPower(adapter, &ofdmPowerLevel[0], channel);
}

/*  */
/*	Description: */
/*		Update transmit power level of all channel supported. */
/*  */
/*	TODO: */
/*		A mode. */
/*	By Bruce, 2008-02-04. */
/*  */
bool
PHY_UpdateTxPowerDbm8192D(
	struct rtw_adapter *	adapter,
	int		powerInDbm
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	idx;
	u8	rf_path;

	/*  TODO: A mode Tx power. */
	u8	CckTxPwrIdx = phy_DbmToTxPwrIdx(adapter, WIRELESS_MODE_B, powerInDbm);
	u8	OfdmTxPwrIdx = phy_DbmToTxPwrIdx(adapter, WIRELESS_MODE_N_24G, powerInDbm);

	if (OfdmTxPwrIdx - pHalData->LegacyHTTxPowerDiff > 0)
		OfdmTxPwrIdx -= pHalData->LegacyHTTxPowerDiff;
	else
		OfdmTxPwrIdx = 0;

	for (idx = 0; idx < CHANNEL_MAX_NUMBER; idx++)
	{
		for (rf_path = 0; rf_path < 2; rf_path++)
		{
			if (idx < CHANNEL_MAX_NUMBER_2G)
				pHalData->TxPwrLevelCck[rf_path][idx] = CckTxPwrIdx;
			pHalData->TxPwrLevelHT40_1S[rf_path][idx] =
			pHalData->TxPwrLevelHT40_2S[rf_path][idx] = OfdmTxPwrIdx;
		}
	}

	return true;
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
_PHY_SetBWMode92D(
	struct rtw_adapter *	adapter
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	regBwOpMode;
	u8	regRRSR_RSC;

#ifdef CONFIG_DUALMAC_CONCURRENT
	/*  FOr 92D dual mac config. */
	struct rtw_adapter *Buddyadapter = adapter->pbuddy_adapter;
	struct hal_data_8192du *pHalDataBuddyadapter;
#endif

	if (pHalData->rf_chip == RF_PSEUDO_11N)
		return;

	/*  There is no 40MHz mode in RF_8225. */
	if (pHalData->rf_chip==RF_8225)
		return;

	if (adapter->bDriverStopped)
		return;

	/* 3 */
	/* 3<1>Set MAC register */
	/* 3 */
	/* adapter->HalFunc.SetBWModeHandler(); */

	regBwOpMode = rtw_read8(adapter, REG_BWOPMODE);
	regRRSR_RSC = rtw_read8(adapter, REG_RRSR+2);

	switch (pHalData->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			regBwOpMode |= BW_OPMODE_20MHZ;
			   /*  2007/02/07 Mark by Emily becasue we have not verify whether this register works */
			rtw_write8(adapter, REG_BWOPMODE, regBwOpMode);
			break;

		case HT_CHANNEL_WIDTH_40:
			regBwOpMode &= ~BW_OPMODE_20MHZ;
				/*  2007/02/07 Mark by Emily becasue we have not verify whether this register works */
			rtw_write8(adapter, REG_BWOPMODE, regBwOpMode);

			regRRSR_RSC = (regRRSR_RSC&0x90) |(pHalData->nCur40MhzPrimeSC<<5);
			rtw_write8(adapter, REG_RRSR+2, regRRSR_RSC);
			break;

		default:
			/*RT_TRACE(COMP_DBG, DBG_LOUD, ("PHY_SetBWModeCallback8192C():
						unknown Bandwidth: %#X\n",pHalData->CurrentChannelBW));*/
			break;
	}

	/* 3 */
	/* 3<2>Set PHY related register */
	/* 3 */
	switch (pHalData->CurrentChannelBW)
	{
		/* 20 MHz channel*/
		case HT_CHANNEL_WIDTH_20:
			PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, bRFMOD, 0x0);

			PHY_SetBBReg(adapter, rFPGA1_RFMOD, bRFMOD, 0x0);

			PHY_SetBBReg(adapter, rFPGA0_AnalogParameter2, BIT10|BIT11, 3);/*  SET BIT10 BIT11  for receive cck */

			break;

		/* 40 MHz channel*/
		case HT_CHANNEL_WIDTH_40:
			PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, bRFMOD, 0x1);

			PHY_SetBBReg(adapter, rFPGA1_RFMOD, bRFMOD, 0x1);

			/*  Set Control channel to upper or lower. These settings are required only for 40MHz */
			if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
			{
				PHY_SetBBReg(adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC>>1));
			}
			PHY_SetBBReg(adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);

			PHY_SetBBReg(adapter, rFPGA0_AnalogParameter2, BIT10|BIT11, 0);/*  SET BIT10 BIT11  for receive cck */

			PHY_SetBBReg(adapter, 0x818, (BIT26|BIT27), (pHalData->nCur40MhzPrimeSC==HAL_PRIME_CHNL_OFFSET_LOWER)?2:1);

			break;

		default:
			/*RT_TRACE(COMP_DBG, DBG_LOUD, ("PHY_SetBWModeCallback8192C(): unknown Bandwidth: %#X\n"\
						,pHalData->CurrentChannelBW));*/
			break;

	}

	/* 3<3>Set RF related register */
	switch (pHalData->rf_chip)
	{
		case RF_8225:
			break;

		case RF_8256:
			/*  Please implement this function in Hal8190PciPhy8256.c */
			break;

		case RF_8258:
			/*  Please implement this function in Hal8190PciPhy8258.c */
			break;

		case RF_PSEUDO_11N:
			/*  Do Nothing */
			break;

		case RF_6052:
			rtl8192d_PHY_RF6052SetBandwidth(adapter, pHalData->CurrentChannelBW);
			break;

		default:
			break;
	}

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (adapter->DualMacConcurrent == true && Buddyadapter != NULL)
	{
		if (pHalData->bMasterOfDMSP)
		{
			pHalDataBuddyadapter = GET_HAL_DATA(Buddyadapter);
			pHalDataBuddyadapter->CurrentChannelBW=pHalData->CurrentChannelBW;
			pHalDataBuddyadapter->nCur40MhzPrimeSC = pHalData->nCur40MhzPrimeSC;
		}
	}
#endif

}

 /*-----------------------------------------------------------------------------
 * Function:   SetBWMode8190Pci()
 *
 * Overview:  This function is export to "HalCommon" moudule
 *
 * Input:		struct rtw_adapter *			adapter
 *			HT_CHANNEL_WIDTH	Bandwidth	20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		We do not take j mode into consideration now
 *---------------------------------------------------------------------------*/
void
PHY_SetBWMode8192D(
	struct rtw_adapter *					adapter,
	enum HT_CHANNEL_WIDTH	Bandwidth,	/*  20M or 40M */
	unsigned char	Offset		/*  Upper, Lower, or Don't care */
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	enum HT_CHANNEL_WIDTH	tmpBW= pHalData->CurrentChannelBW;
#ifdef CONFIG_DUALMAC_CONCURRENT
	struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (pHalData->bInModeSwitchProcess)
	{
		DBG_8192D("PHY_SwChnl8192D(): During mode switch\n");
		return;
	}
#endif

	pHalData->CurrentChannelBW = Bandwidth;

	pHalData->nCur40MhzPrimeSC = Offset;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if ((Buddyadapter !=NULL) && (pHalData->bSlaveOfDMSP))
	{
		{
			DBG_8192D("PHY_SetBWMode92D():slave return when slave\n");
			return;
		}
	}
#endif

	if ((!adapter->bDriverStopped) && (!adapter->bSurpriseRemoved))
	{
#ifndef USE_WORKITEM
	_PHY_SetBWMode92D(adapter);
#endif
	}
	else
	{
		pHalData->CurrentChannelBW = tmpBW;
	}
}

/*******************************************************************
Descriptor:
			stop TRX Before change bandType dynamically

********************************************************************/
void
PHY_StopTRXBeforeChangeBand8192D(
	  struct rtw_adapter *		adapter
)
{
#if MP_DRIVER == 1
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	pdmpriv->RegC04_MP = (u8)PHY_QueryBBReg(adapter, rOFDM0_TRxPathEnable, bMaskByte0);
	pdmpriv->RegD04_MP = PHY_QueryBBReg(adapter, rOFDM1_TRxPathEnable, bDWord);
#endif

	PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x00);

	PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x00);
	PHY_SetBBReg(adapter, rOFDM1_TRxPathEnable, bDWord, 0x0);
}

static void PHY_SwitchWirelessBand(struct rtw_adapter *adapter, u8 Band)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8	i, value8;/*  RegValue */

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (pHalData->bInModeSwitchProcess || pHalData->bSlaveOfDMSP)
	{
		DBG_8192D("PHY_SwitchWirelessBand(): skip for mode switch or slave\n");
		return;
	}
#endif

	pHalData->BandSet92D = pHalData->CurrentBandType92D = (enum BAND_TYPE)Band;
	if (IS_92D_SINGLEPHY(pHalData->VersionID))
		pHalData->BandSet92D = BAND_ON_BOTH;

	if (pHalData->CurrentBandType92D == BAND_ON_5G)
	{
		pHalData->CurrentWirelessMode = WIRELESS_MODE_N_5G;
	}
	else
	{
		pHalData->CurrentWirelessMode = WIRELESS_MODE_N_24G;
	}

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (pHalData->bMasterOfDMSP)
	{
		struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
		if (Buddyadapter!=NULL)
		{
			if (Buddyadapter->hw_init_completed)
			{
				GET_HAL_DATA(Buddyadapter)->BandSet92D = pHalData->BandSet92D;
				GET_HAL_DATA(Buddyadapter)->CurrentBandType92D = pHalData->CurrentBandType92D;
				GET_HAL_DATA(Buddyadapter)->CurrentWirelessMode = pHalData->CurrentWirelessMode;
			}
		}
	}
#endif

	/* stop RX/Tx */
	PHY_StopTRXBeforeChangeBand8192D(adapter);

	/* reconfig BB/RF according to wireless mode */
	if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{
		/* BB & RF Config */
		if (pHalData->interfaceIndex == 1)
		{
#ifdef CONFIG_EMBEDDED_FWIMG
			phy_ConfigBBWithHeaderFile(adapter, BaseBand_Config_AGC_TAB);
#else
			PHY_SetAGCTab8192D(adapter);
#endif
		}
	}
	else	/* 5G band */
	{
		if (pHalData->interfaceIndex == 1)
		{
#ifdef CONFIG_EMBEDDED_FWIMG
			phy_ConfigBBWithHeaderFile(adapter, BaseBand_Config_AGC_TAB);
#else
			PHY_SetAGCTab8192D(adapter);
#endif
		}
	}

	PHY_UpdateBBRFConfiguration8192D(adapter, true);

	if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{
		update_tx_basic_rate(adapter, WIRELESS_11BG_24N);

		PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x3);
	}
	else
	{
		/* avoid using cck rate in 5G band */
		/*  Set RRSR rate table. */
		update_tx_basic_rate(adapter, WIRELESS_11A_5N);

		PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x2);
	}

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (pHalData->bMasterOfDMSP)
	{
		struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
		if (Buddyadapter!=NULL)
		{
			if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
				rtw_write16(Buddyadapter, REG_RRSR, 0x15d);
			else
				rtw_write16(Buddyadapter, REG_RRSR, 0x150);
		}
	}
#endif

	pdmpriv->bReloadtxpowerindex = true;

	/*  notice fw know band status  0x81[1]/0x53[1] = 0: 5G, 1: 2G */
	if (pHalData->CurrentBandType92D==BAND_ON_2_4G)
	{
		value8 = rtw_read8(adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1));
		value8 |= BIT1;
		rtw_write8(adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1),value8);
	}
	else
	{
		value8 = rtw_read8(adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1));
		value8 &= (~BIT1);
		rtw_write8(adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1),value8);
	}

	for (i=0;i<20;i++)
			rtw_udelay_os(MAX_STALL_TIME);

}

static void
PHY_EnableRFENV(
	struct rtw_adapter *		adapter,
	u8				eRFPath	,
	u32				MaskforPhySet,
	u32*			pu4RegValue
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct bb_register_def *pPhyReg = &pHalData->PHYRegDef[eRFPath];

	/*----Store original RFENV control type----*/
	switch (eRFPath)
	{
		case RF_PATH_A:
		case RF_PATH_C:
			*pu4RegValue = PHY_QueryBBReg(adapter, pPhyReg->rfintfs|MaskforPhySet, bRFSI_RFENV);
			break;
		case RF_PATH_B:
		case RF_PATH_D:
			*pu4RegValue = PHY_QueryBBReg(adapter, pPhyReg->rfintfs|MaskforPhySet, bRFSI_RFENV<<16);
			break;
	}

	/*----Set RF_ENV enable----*/
	PHY_SetBBReg(adapter, pPhyReg->rfintfe|MaskforPhySet, bRFSI_RFENV<<16, 0x1);
	rtw_udelay_os(1);

	/*----Set RF_ENV output high----*/
	PHY_SetBBReg(adapter, pPhyReg->rfintfo|MaskforPhySet, bRFSI_RFENV, 0x1);
	rtw_udelay_os(1);

	/* Set bit number of Address and Data for RF register */
	PHY_SetBBReg(adapter, pPhyReg->rfHSSIPara2|MaskforPhySet, b3WireAddressLength, 0x0);	/*  Set 1 to 4 bits for 8255 */
	rtw_udelay_os(1);

	PHY_SetBBReg(adapter, pPhyReg->rfHSSIPara2|MaskforPhySet, b3WireDataLength, 0x0); /*  Set 0 to 12	bits for 8255 */
	rtw_udelay_os(1);
}

static void
PHY_RestoreRFENV(
	struct rtw_adapter *		adapter,
	u8				eRFPath,
	u32				MaskforPhySet,
	u32*			pu4RegValue
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct bb_register_def *pPhyReg = &pHalData->PHYRegDef[eRFPath];

	/* If another MAC is ON,need do this? */
	/*----Restore RFENV control type----*/;
	switch (eRFPath)
	{
		case RF_PATH_A:
		case RF_PATH_C:
			PHY_SetBBReg(adapter, pPhyReg->rfintfs|MaskforPhySet, bRFSI_RFENV, *pu4RegValue);
			break;
		case RF_PATH_B :
		case RF_PATH_D:
			PHY_SetBBReg(adapter, pPhyReg->rfintfs|MaskforPhySet, bRFSI_RFENV<<16, *pu4RegValue);
			break;
	}
}

/*-----------------------------------------------------------------------------
 * Function:	phy_SwitchRfSetting
 *
 * Overview:	Change RF Setting when we siwthc channel for 92D C-cut.
 *
 * Input:       struct rtw_adapter *				adapter
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 01/08/2009	MHC		Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC		Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
 static	void
 phy_SwitchRfSetting(
	struct rtw_adapter *			adapter,
	u8					channel
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8			path = pHalData->CurrentBandType92D==BAND_ON_5G?RF_PATH_A:RF_PATH_B;
	u8			index = 0,	i = 0, eRFPath = RF_PATH_A;
	bool		bNeedPowerDownRadio = false, bInteralPA = false;
	u32			u4RegValue, mask = 0x1C000, value = 0, u4tmp, u4tmp2,MaskforPhySet=0;
	/* Query regB30 bit27 */
	u32			Regb30 = PHY_QueryBBReg(adapter, 0xb30, BIT27);

	/* only for 92D C-cut SMSP */

	if (adapter_to_dvobj(adapter)->ishighspeed == false)
		return;

	/* config path A for 5G */
	if (pHalData->CurrentBandType92D==BAND_ON_5G)
	{
		u4tmp = CurveIndex[GetRightChnlPlace(channel)-1];

		for (i = 0; i < RF_CHNL_NUM_5G; i++)
		{
			if (channel == RF_CHNL_5G[i] && channel <= 140)
				index = 0;
		}

		for (i = 0; i < RF_CHNL_NUM_5G_40M; i++)
		{
			if (channel == RF_CHNL_5G_40M[i] && channel <= 140)
				index = 1;
		}

		if (channel ==149 || channel == 155 || channel ==161)
			index = 2;
		else if (channel == 151 || channel == 153 || channel == 163 || channel == 165)
			index = 3;
		else if (channel == 157 || channel == 159)
			index = 4;

		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY && pHalData->interfaceIndex == 1)
		{
			bNeedPowerDownRadio = rtl8192d_PHY_EnableAnotherPHY(adapter, false);
			MaskforPhySet = MAC1_ACCESS_PHY0;
			/* asume no this case */
			if (bNeedPowerDownRadio)
				PHY_EnableRFENV(adapter, path, MaskforPhySet, &u4RegValue);
		}

		/* DMDP, if band = 5G,Mac0 need to set PHY1 when regB30[27]=1 */
		if (Regb30 && pHalData->interfaceIndex == 0)
		{
			DBG_8192D("===============phy_SwitchRfSetting8192D interface %d,B30&BIT27=1!!!!\n", pHalData->interfaceIndex);

			bNeedPowerDownRadio = rtl8192d_PHY_EnableAnotherPHY(adapter, true);
			MaskforPhySet= MAC0_ACCESS_PHY1;
			/* asume no this case */
			if (bNeedPowerDownRadio)
				PHY_EnableRFENV(adapter, path, MaskforPhySet, &u4RegValue);
		}

		for (i = 0; i < RF_REG_NUM_for_C_CUT_5G; i++)
		{
			if (i == 0 && (pHalData->MacPhyMode92D == DUALMAC_DUALPHY))
			{
				PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_5G[i]|MaskforPhySet, bRFRegOffsetMask, 0xE439D);
			}
			else if (RF_REG_for_C_CUT_5G[i] == RF_SYN_G4)
			{
#if SWLCK == 1
				u4tmp2= (RF_REG_Param_for_C_CUT_5G[index][i]&0x7FF)|(u4tmp << 11);

				if (channel == 36)
					u4tmp2 &= ~(BIT7|BIT6);

				PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_5G[i]|MaskforPhySet, bRFRegOffsetMask, u4tmp2);
#else
				u4tmp2= RF_REG_Param_for_C_CUT_5G[index][i];
				PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_5G[i]|MaskforPhySet, 0xFF8FF, u4tmp2);
#endif
			} else {
				PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_5G[i]|MaskforPhySet, bRFRegOffsetMask, RF_REG_Param_for_C_CUT_5G[index][i]);
			}
		}
		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY && pHalData->interfaceIndex == 1) {
			if (bNeedPowerDownRadio)
				PHY_RestoreRFENV(adapter, path,MaskforPhySet, &u4RegValue);
			rtl8192d_PHY_PowerDownAnotherPHY(adapter, false);
		}

		if (Regb30 && pHalData->interfaceIndex == 0) {
			if (bNeedPowerDownRadio)
				PHY_RestoreRFENV(adapter, path,MaskforPhySet, &u4RegValue);
			rtl8192d_PHY_PowerDownAnotherPHY(adapter, true);
		}

		if (channel < 149)
			value = 0x07;
		else if (channel >= 149)
			value = 0x02;

		if (channel >= 36 && channel <= 64)
			index = 0;
		else if (channel >=100 && channel <= 140)
			index = 1;
		else
			index = 2;

		for (eRFPath = RF_PATH_A; eRFPath < pHalData->NumTotalRFPath; eRFPath++)
		{
			if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY &&
				pHalData->interfaceIndex == 1)		/* MAC 1 5G */
				bInteralPA = pHalData->InternalPA5G[1];
			else
				bInteralPA = pHalData->InternalPA5G[eRFPath];

			if (bInteralPA)
			{
				for (i = 0; i < RF_REG_NUM_for_C_CUT_5G_internalPA; i++)
				{
					if (RF_REG_for_C_CUT_5G_internalPA[i] == 0x03 &&
						channel >=36 && channel <=64)
						PHY_SetRFReg(adapter, eRFPath, RF_REG_for_C_CUT_5G_internalPA[i], bRFRegOffsetMask, 0x7bdef);
					else
						PHY_SetRFReg(adapter, eRFPath, RF_REG_for_C_CUT_5G_internalPA[i], bRFRegOffsetMask, RF_REG_Param_for_C_CUT_5G_internalPA[index][i]);
				}
			}
			else
				PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, RF_TXPA_AG, mask, value);
		}
	}
	else if (pHalData->CurrentBandType92D==BAND_ON_2_4G)
	{
		u4tmp = CurveIndex[channel-1];

		if (channel == 1 || channel == 2 || channel ==4 || channel == 9 || channel == 10 ||
			channel == 11 || channel ==12)
			index = 0;
		else if (channel ==3 || channel == 13 || channel == 14)
			index = 1;
		else if (channel >= 5 && channel <= 8)
			index = 2;

		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY)
		{
			path = RF_PATH_A;
			if (pHalData->interfaceIndex == 0)
			{
				bNeedPowerDownRadio = rtl8192d_PHY_EnableAnotherPHY(adapter, true);
				MaskforPhySet = MAC0_ACCESS_PHY1;
				if (bNeedPowerDownRadio)
					PHY_EnableRFENV(adapter, path,MaskforPhySet,&u4RegValue);
			}

			/* DMDP, if band = 2G,MAC1 need to set PHY0 when regB30[27]=1 */
			if (Regb30 && pHalData->interfaceIndex == 1)
			{

				bNeedPowerDownRadio = rtl8192d_PHY_EnableAnotherPHY(adapter, false);
				MaskforPhySet= MAC1_ACCESS_PHY0;
				/* asume no this case */
				if (bNeedPowerDownRadio)
					PHY_EnableRFENV(adapter, path,MaskforPhySet,&u4RegValue);
			}
		}

		for (i = 0; i < RF_REG_NUM_for_C_CUT_2G; i++)
		{
#if SWLCK == 1
			if (RF_REG_for_C_CUT_2G[i] == RF_SYN_G7)
				PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_2G[i]|MaskforPhySet, bRFRegOffsetMask, (RF_REG_Param_for_C_CUT_2G[index][i] | BIT17));
			else
#endif
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_REG_for_C_CUT_2G[i]|MaskforPhySet, bRFRegOffsetMask, RF_REG_Param_for_C_CUT_2G[index][i]);
		}

#if SWLCK == 1
		/* for SWLCK */

		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_SYN_G4|MaskforPhySet, bRFRegOffsetMask, RF_REG_SYN_G4_for_C_CUT_2G | (u4tmp << 11));
#endif
		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY && pHalData->interfaceIndex == 0)
		{
			if (bNeedPowerDownRadio) {
				PHY_RestoreRFENV(adapter, path,MaskforPhySet, &u4RegValue);
			}
			rtl8192d_PHY_PowerDownAnotherPHY(adapter, true);
		}

		if (Regb30 && pHalData->interfaceIndex == 1) {
			if (bNeedPowerDownRadio)
				PHY_RestoreRFENV(adapter, path,MaskforPhySet, &u4RegValue);
			rtl8192d_PHY_PowerDownAnotherPHY(adapter, false);
		}
	}
}

/*-----------------------------------------------------------------------------
 * Function:	phy_ReloadLCKSetting
 *
 * Overview:	Change RF Setting when we siwthc channel for 92D C-cut.
 *
 * Input:       struct rtw_adapter *				adapter
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 01/08/2009	MHC		Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC		Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
static  void
 phy_ReloadLCKSetting(
	struct rtw_adapter *				adapter,
	u8					channel
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8		eRFPath = pHalData->CurrentBandType92D == BAND_ON_5G?RF_PATH_A:IS_92D_SINGLEPHY(pHalData->VersionID)?RF_PATH_B:RF_PATH_A;
	u32		u4tmp = 0, u4RegValue = 0;
	bool		bNeedPowerDownRadio = false;
	u32		MaskforPhySet = 0;

	/* only for 92D C-cut SMSP */

	if (pHalData->CurrentBandType92D == BAND_ON_5G)
	{
		/* Path-A for 5G */
		{
			u4tmp = CurveIndex[GetRightChnlPlace(channel)-1];

			if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY && pHalData->interfaceIndex == 1)
			{
				bNeedPowerDownRadio = rtl8192d_PHY_EnableAnotherPHY(adapter, false);
				MaskforPhySet = MAC1_ACCESS_PHY0;
				/* asume no this case */
				if (bNeedPowerDownRadio)
					PHY_EnableRFENV(adapter, eRFPath, MaskforPhySet,&u4RegValue);
			}

			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, RF_SYN_G4|MaskforPhySet, 0x3f800, u4tmp);

			if (bNeedPowerDownRadio) {
				PHY_RestoreRFENV(adapter, eRFPath,MaskforPhySet, &u4RegValue);
				rtl8192d_PHY_PowerDownAnotherPHY(adapter, false);
			}
		}
	}
	else if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{
		{
			u32 u4tmp=0;
			u4tmp = CurveIndex[channel-1];

			if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY && pHalData->interfaceIndex == 0)
			{
				bNeedPowerDownRadio = rtl8192d_PHY_EnableAnotherPHY(adapter, true);
				MaskforPhySet = MAC0_ACCESS_PHY1;
				if (bNeedPowerDownRadio)
					PHY_EnableRFENV(adapter, eRFPath,MaskforPhySet, &u4RegValue);
			}
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, RF_SYN_G4|MaskforPhySet, 0x3f800, u4tmp);

			if (bNeedPowerDownRadio) {
				PHY_RestoreRFENV(adapter, eRFPath,MaskforPhySet, &u4RegValue);
				rtl8192d_PHY_PowerDownAnotherPHY(adapter, true);
			}
		}
	}

}

/*-----------------------------------------------------------------------------
 * Function:	phy_ReloadIMRSetting
 *
 * Overview:	Change RF Setting when we siwthc channel for 92D C-cut.
 *
 * Input:       struct rtw_adapter *				adapter
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 01/08/2009	MHC		Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC		Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
 static void
 phy_ReloadIMRSetting(
	struct rtw_adapter *				adapter,
	u8					channel,
	u8					eRFPath
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u32		IMR_NUM = MAX_RF_IMR_INDEX;
	u32		RFMask=bRFRegOffsetMask;
	u8		group=0, i;

	if (adapter_to_dvobj(adapter)->ishighspeed == false)
		return;

	/* only for 92D C-cut SMSP */

	if (pHalData->CurrentBandType92D == BAND_ON_5G)
	{
		PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, BIT25|BIT24, 0);
		PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0x00f00000,	0xf);

		/*  fc area 0xd2c */
		if (channel>=149)
			PHY_SetBBReg(adapter, rOFDM1_CFOTracking, BIT13|BIT14,2);
		else
			PHY_SetBBReg(adapter, rOFDM1_CFOTracking, BIT13|BIT14,1);

		group = channel<=64?1:2; /* leave 0 for channel1-14. */
		IMR_NUM = MAX_RF_IMR_INDEX_NORMAL;

		for (i=0; i<IMR_NUM; i++) {
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, RF_REG_FOR_5G_SWCHNL_NORMAL[i], RFMask,RF_IMR_Param_Normal[0][group][i]);
		}
		PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0x00f00000,0);
		PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, bOFDMEn|bCCKEn, 2);
	}
	else { /* G band. */

		if (!pHalData->bLoadIMRandIQKSettingFor2G) {
			PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, BIT25|BIT24, 0);
			PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0x00f00000,	0xf);

			IMR_NUM = MAX_RF_IMR_INDEX_NORMAL;
			for (i=0; i<IMR_NUM; i++) {
				PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, RF_REG_FOR_5G_SWCHNL_NORMAL[i], bRFRegOffsetMask,RF_IMR_Param_Normal[0][0][i]);
			}
			PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0x00f00000,0);
			PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, bOFDMEn|bCCKEn, 3);
		}
	}

}

/*-----------------------------------------------------------------------------
 * Function:	phy_ReloadIQKSetting
 *
 * Overview:	Change RF Setting when we siwthc channel for 92D C-cut.
 *
 * Input:       struct rtw_adapter *				adapter
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 01/08/2009	MHC		Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC		Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
 static void
 phy_ReloadIQKSetting(
	struct rtw_adapter *				adapter,
	u8					channel
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8		Indexforchannel;/* index, */

	/* only for 92D C-cut SMSP */

	if (adapter_to_dvobj(adapter)->ishighspeed == false)
		return;

	/* Do IQK for normal chip and test chip 5G band---------------- */
	Indexforchannel = rtl8192d_GetRightChnlPlaceforIQK(channel);

#if MP_DRIVER == 1
	pHalData->bNeedIQK = true;
	pHalData->bLoadIMRandIQKSettingFor2G = false;
#endif

	if (pHalData->bNeedIQK && !pHalData->IQKMatrixRegSetting[Indexforchannel].bIQKDone)
	{ /* Re Do IQK. */
		DBG_8192D("Do IQK Matrix reg for channel:%d....\n", channel);
		rtl8192d_PHY_IQCalibrate(adapter);
	}
	else /* Just load the value. */
	{
		/*  2G band just load once. */
		if (((!pHalData->bLoadIMRandIQKSettingFor2G) && Indexforchannel==0) ||Indexforchannel>0)
		{

			if ((pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][0] != 0)/*&&(RegEA4 != 0)*/)
			{
				if (pHalData->CurrentBandType92D == BAND_ON_5G)
					phy_PathAFillIQKMatrix_5G_Normal(adapter, true, pHalData->IQKMatrixRegSetting[Indexforchannel].Value, 0, (pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][2] == 0));
				else
					phy_PathAFillIQKMatrix(adapter, true, pHalData->IQKMatrixRegSetting[Indexforchannel].Value, 0, (pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][2] == 0));
			}

			if (IS_92D_SINGLEPHY(pHalData->VersionID))
			{
				if ((pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][4] != 0)/*&&(RegEC4 != 0)*/)
				{
					if (pHalData->CurrentBandType92D == BAND_ON_5G)
						phy_PathBFillIQKMatrix_5G_Normal(adapter, true, pHalData->IQKMatrixRegSetting[Indexforchannel].Value, 0, (pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][6] == 0));
					else
						phy_PathBFillIQKMatrix(adapter, true, pHalData->IQKMatrixRegSetting[Indexforchannel].Value, 0, (pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][6] == 0));
				}
			}

			if ((adapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS)&&(Indexforchannel==0))
				pHalData->bLoadIMRandIQKSettingFor2G=true;
		}
	}
	pHalData->bNeedIQK = false;
}

static void _PHY_SwChnl8192D(struct rtw_adapter * adapter, u8 channel)
{
	u8	eRFPath;
	u32	param1, param2;
	u32	ret_value;
	enum BAND_TYPE	bandtype, target_bandtype;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_DUALMAC_CONCURRENT)
	/*  FOr 92D dual mac config. and sw concurrent mode */
	struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
#endif

	if (adapter->bNotifyChannelChange)
	{
		DBG_8192D("[%s] ch = %d\n", __func__, channel);
	}

	if (pHalData->BandSet92D == BAND_ON_BOTH) {
		/*  Need change band? */
		/*  BB {Reg878[0],[16]} bit0= 1 is 5G, bit0=0 is 2G. */
		ret_value = PHY_QueryBBReg(adapter, rFPGA0_XAB_RFParameter, bMaskDWord);

		if (ret_value & BIT0)
			bandtype = BAND_ON_5G;
		else
			bandtype = BAND_ON_2_4G;

		/*  Use current channel to judge Band Type and switch Band if need. */
		if (channel > 14)
		{
			target_bandtype = BAND_ON_5G;
		}
		else
		{
			target_bandtype = BAND_ON_2_4G;
		}

		if (target_bandtype != bandtype)
			PHY_SwitchWirelessBand(adapter,target_bandtype);
	}

	do{
		/* s1. pre common command - CmdID_SetTxPowerLevel */
		PHY_SetTxPowerLevel8192D(adapter, channel);

		/* s2. RF dependent command - CmdID_RF_WriteReg, param1=RF_CHNLBW, param2=channel */
		param1 = RF_CHNLBW;
		param2 = channel;
		for (eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
		{
			pHalData->RfRegChnlVal[eRFPath] = ((pHalData->RfRegChnlVal[eRFPath] & 0xffffff00) | param2);
			if (pHalData->CurrentBandType92D == BAND_ON_5G)
			{
				if (param2>99)
				{
					pHalData->RfRegChnlVal[eRFPath]=pHalData->RfRegChnlVal[eRFPath]|(BIT18);
				}
				else
				{
					pHalData->RfRegChnlVal[eRFPath]=pHalData->RfRegChnlVal[eRFPath]&(~BIT18);
				}
				pHalData->RfRegChnlVal[eRFPath] |= (BIT16|BIT8);
			}
			else
			{
				pHalData->RfRegChnlVal[eRFPath] &= ~(BIT8|BIT16|BIT18);
			}
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, param1, bRFRegOffsetMask, pHalData->RfRegChnlVal[eRFPath]);
			phy_ReloadIMRSetting(adapter, channel, eRFPath);
		}

		phy_SwitchRfSetting(adapter, channel);

		/* do IQK when all parameters are ready */
		phy_ReloadIQKSetting(adapter, channel);
		break;
	}while (true);

	/* s3. post common command - CmdID_End, None */

#ifdef CONFIG_CONCURRENT_MODE
	if (Buddyadapter) {
		GET_HAL_DATA(Buddyadapter)->CurrentChannel = channel;
		GET_HAL_DATA(Buddyadapter)->BandSet92D = pHalData->BandSet92D;
		GET_HAL_DATA(Buddyadapter)->CurrentBandType92D = pHalData->CurrentBandType92D;
		GET_HAL_DATA(Buddyadapter)->CurrentWirelessMode = pHalData->CurrentWirelessMode;
	}
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (adapter->DualMacConcurrent == true && Buddyadapter != NULL)
	{
		if (pHalData->bMasterOfDMSP)
		{
			GET_HAL_DATA(Buddyadapter)->CurrentChannel=channel;
		}
	}
#endif
}

void
PHY_SwChnl8192D(	/*  Call after initialization */
	struct rtw_adapter *adapter,
	u8		channel
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	tmpchannel = pHalData->CurrentChannel;
	bool  bResult = true;
	u32	timeout = 1000, timecount = 0;

#ifdef CONFIG_DUALMAC_CONCURRENT
	struct rtw_adapter *Buddyadapter = adapter->pbuddy_adapter;
	struct hal_data_8192du *pHalDataBuddyadapter;
#endif

	if (pHalData->rf_chip == RF_PSEUDO_11N)
	{
		return;									/* return immediately if it is peudo-phy */
	}

	if (adapter->mlmeextpriv.sitesurvey_res.state == SCAN_COMPLETE)
		pHalData->bLoadIMRandIQKSettingFor2G = false;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (pHalData->bInModeSwitchProcess)
	{
		DBG_8192D("PHY_SwChnl8192D(): During mode switch\n");
		return;
	}

	if (Buddyadapter != NULL &&
		((pHalData->interfaceIndex == 0 && pHalData->CurrentBandType92D == BAND_ON_2_4G) ||
		(pHalData->interfaceIndex == 1 && pHalData->CurrentBandType92D == BAND_ON_5G)))
	{
		pHalDataBuddyadapter=GET_HAL_DATA(Buddyadapter);
		while (pHalDataBuddyadapter->bLCKInProgress && timecount < timeout)
		{
			#ifdef CONFIG_LONG_DELAY_ISSUE
			rtw_msleep_os(50);
			#else
			rtw_mdelay_os(50);
			#endif
			timecount += 50;
		}
	}
#endif

	while (pHalData->bLCKInProgress && timecount < timeout)
	{
		#ifdef CONFIG_LONG_DELAY_ISSUE
		rtw_msleep_os(50);
		#else
		rtw_mdelay_os(50);
		#endif
		timecount += 50;
	}

	/*  */
	switch (pHalData->CurrentWirelessMode)
	{
		case WIRELESS_MODE_A:
		case WIRELESS_MODE_N_5G:
			/* Get first channel error when change between 5G and 2.4G band. */
			/* FIX ME!!! */
			break;
		case WIRELESS_MODE_B:
			break;
		case WIRELESS_MODE_G:
		case WIRELESS_MODE_N_24G:
			/* Get first channel error when change between 5G and 2.4G band. */
			break;

		default:
			break;
	}
	/*  */

	if (channel == 0) {/* FIXME!!!A band? */
		channel = 1;
	}

	pHalData->CurrentChannel=channel;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if ((Buddyadapter !=NULL) && (pHalData->bSlaveOfDMSP))
	{
		DBG_8192D("PHY_SwChnl8192D():slave return when slave\n");
		return;
	}
#endif

	if ((!adapter->bDriverStopped) && (!adapter->bSurpriseRemoved))
	{
#ifndef USE_WORKITEM
		_PHY_SwChnl8192D(adapter, channel);
#endif
		if (!bResult)
				pHalData->CurrentChannel = tmpchannel;

	} else {
			pHalData->CurrentChannel = tmpchannel;
	}
}

static	bool
phy_SetSwChnlCmdArray(
	struct sw_chnl_cmd *cmdtable,
	u32			cmdtableidx,
	u32			cmdtablesz,
	enum swchnl_cmdid	cmdid,
	u32			para1,
	u32			para2,
	u32			msdelay
	)
{
	struct sw_chnl_cmd *cmd;

	if (cmdtable == NULL)
		return false;
	if (cmdtableidx >= cmdtablesz)
		return false;

	cmd = cmdtable + cmdtableidx;
	cmd->cmdid = cmdid;
	cmd->Para1 = para1;
	cmd->Para2 = para2;
	cmd->msDelay = msdelay;

	return true;
}

/*  */
/*  Description: */
/*	Switch channel synchronously. Called by SwChnlByDelayHandler. */
/*  */
/*  Implemented by Bruce, 2008-02-14. */
/*  The following procedure is operted according to SwChanlCallback8190Pci(). */
/*  However, this procedure is performed synchronously  which should be running under */
/*  passive level. */
/*  */
void
PHY_SwChnlPhy8192D(	/*  Only called during initialize */
	struct rtw_adapter *	adapter,
	u8		channel
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	/*  Cannot IO. */

	/*  Channel Switching is in progress. */

	/* return immediately if it is peudo-phy */
	if (pHalData->rf_chip == RF_PSEUDO_11N)
	{
		return;
	}

	if (channel == 0)
		channel = 1;

	pHalData->CurrentChannel=channel;
}

/*  */
/*  */
/*	IQK */
/*  */
/*  */
#define MAX_TOLERANCE		5
#define MAX_TOLERANCE_92D	3
#define IQK_DELAY_TIME		1	/* ms */

static u8			/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_PathA_IQK(
	struct rtw_adapter *	adapter,
	bool		configPathB
	)
{
	u32	regEAC, regE94, regE9C, regEA4;
	u8	result = 0x00;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	/* path-A IQK setting */
	if (pHalData->interfaceIndex == 0)
	{
		PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
	}
	else
	{
		PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x10008c22);
	}

	PHY_SetBBReg(adapter, rTx_IQK_PI_A, bMaskDWord, 0x82140102);

	PHY_SetBBReg(adapter, rRx_IQK_PI_A, bMaskDWord, configPathB ? 0x28160202 :
		IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID)?0x28160202:0x28160502);

	/* path-B IQK setting */
	if (configPathB)
	{
		PHY_SetBBReg(adapter, rTx_IQK_Tone_B, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_B, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(adapter, rTx_IQK_PI_B, bMaskDWord, 0x82140102);
		if (IS_HARDWARE_TYPE_8192D(adapter))
			PHY_SetBBReg(adapter, rRx_IQK_PI_B, bMaskDWord, 0x28160206);
		else
			PHY_SetBBReg(adapter, rRx_IQK_PI_B, bMaskDWord, 0x28160202);
	}

	/* LO calibration setting */
	if (IS_HARDWARE_TYPE_8192D(adapter))
		PHY_SetBBReg(adapter, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);
	else
		PHY_SetBBReg(adapter, rIQK_AGC_Rsp, bMaskDWord, 0x001028d1);

	/* One shot, path A LOK & IQK */
	PHY_SetBBReg(adapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	PHY_SetBBReg(adapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	/*  delay x ms */
	rtw_udelay_os(IQK_DELAY_TIME*1000);

	/*  Check failed */
	regEAC = PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	regE94 = PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_A, bMaskDWord);
	regE9C= PHY_QueryBBReg(adapter, rTx_Power_After_IQK_A, bMaskDWord);
	regEA4= PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_A_2, bMaskDWord);

	if (!(regEAC & BIT28) &&
		(((regE94 & 0x03FF0000)>>16) != 0x142) &&
		(((regE9C & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	else							/* if Tx not OK, ignore Rx */
		return result;

	if (!(regEAC & BIT27) &&		/* if Tx is OK, check whether Rx is OK */
		(((regEA4 & 0x03FF0000)>>16) != 0x132) &&
		(((regEAC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		DBG_8192D("Path A Rx IQK fail!!\n");

	return result;

}

static u8			/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_PathA_IQK_5G_Normal(
	struct rtw_adapter *	adapter,
	bool		configPathB
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32	regEAC, regE94, regEA4;
	u8	result = 0x00;
	u8	i = 0;
#if MP_DRIVER == 1
	u8	retryCount = 9;
#else
	u8	retryCount = 2;
#endif
	u8	timeout = 20, timecount = 0;

	u32	TxOKBit = BIT28, RxOKBit = BIT27;

	if (pHalData->interfaceIndex == 1)	/* PHY1 */
	{
		TxOKBit = BIT31;
		RxOKBit = BIT30;
	}

	/* path-A IQK setting */

	PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x18008c1f);
	PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x18008c1f);
	PHY_SetBBReg(adapter, rTx_IQK_PI_A, bMaskDWord, 0x82140307);
	PHY_SetBBReg(adapter, rRx_IQK_PI_A, bMaskDWord, 0x68160960);

	/* path-B IQK setting */
	if (configPathB)
	{
		PHY_SetBBReg(adapter, rTx_IQK_Tone_B, bMaskDWord, 0x18008c2f);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_B, bMaskDWord, 0x18008c2f);
		PHY_SetBBReg(adapter, rTx_IQK_PI_B, bMaskDWord, 0x82110000);
		PHY_SetBBReg(adapter, rRx_IQK_PI_B, bMaskDWord, 0x68110000);
	}

	/* LO calibration setting */
	PHY_SetBBReg(adapter, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	/* path-A PA on */
	PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, bMaskDWord, 0x07000f60);
	PHY_SetBBReg(adapter, rFPGA0_XA_RFInterfaceOE, bMaskDWord, 0x66e60e30);

	for (i = 0 ; i < retryCount ; i++)
	{

		/* One shot, path A LOK & IQK */
		PHY_SetBBReg(adapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
		PHY_SetBBReg(adapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

		/*  delay x ms */
		rtw_mdelay_os(IQK_DELAY_TIME*10);

		while (timecount < timeout && PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, BIT26) == 0x00)
		{
			rtw_udelay_os(IQK_DELAY_TIME*1000*2);
			timecount++;
		}

		timecount = 0;
		while (timecount < timeout && PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_A_2, 0x3FF0000) == 0x00)
		{
			rtw_udelay_os(IQK_DELAY_TIME*1000*2);
			timecount++;
		}

		/*  Check failed */
		regEAC = PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord);
		regE94 = PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_A, bMaskDWord);
		regEA4= PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_A_2, bMaskDWord);

		if (!(regEAC & TxOKBit) &&
			(((regE94 & 0x03FF0000)>>16) != 0x142) )
		{
			result |= 0x01;
		}
		else			/* if Tx not OK, ignore Rx */
		{
			continue;
		}

		if (!(regEAC & RxOKBit) &&			/* if Tx is OK, check whether Rx is OK */
			(((regEA4 & 0x03FF0000)>>16) != 0x132))
		{
			result |= 0x02;
			break;
		}
		else
		{
		}
	}

	/* path A PA off */
	PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, bMaskDWord, pdmpriv->IQK_BB_backup[0]);
	PHY_SetBBReg(adapter, rFPGA0_XA_RFInterfaceOE, bMaskDWord, pdmpriv->IQK_BB_backup[1]);

	if (!(result & 0x01))	/* Tx IQK fail */
	{
		PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x19008c00);
	}

	if (!(result & 0x02))	/* Rx IQK fail */
	{
		PHY_SetBBReg(adapter, rOFDM0_XARxIQImbalance , bMaskDWord, 0x40000100);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_A , bMaskDWord, 0x19008c00);

		DBG_8192D("Path A Rx IQK fail!!0xe34 = 0x%x\n", PHY_QueryBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord));
	}

	return result;
}

static u8				/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_PathB_IQK(
	struct rtw_adapter *	adapter
	)
{
	u32 regEAC, regEB4, regEBC, regEC4, regECC;
	u8	result = 0x00;

	/* One shot, path B LOK & IQK */
	PHY_SetBBReg(adapter, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	PHY_SetBBReg(adapter, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	/*  delay x ms */
	rtw_udelay_os(IQK_DELAY_TIME*1000);/* PlatformStallExecution(IQK_DELAY_TIME*1000); */

	/*  Check failed */
	regEAC = PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	regEB4 = PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_B, bMaskDWord);
	regEBC= PHY_QueryBBReg(adapter, rTx_Power_After_IQK_B, bMaskDWord);
	regEC4= PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_B_2, bMaskDWord);
	regECC= PHY_QueryBBReg(adapter, rRx_Power_After_IQK_B_2, bMaskDWord);

	if (!(regEAC & BIT31) &&
		(((regEB4 & 0x03FF0000)>>16) != 0x142) &&
		(((regEBC & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	else
		return result;

	if (!(regEAC & BIT30) &&
		(((regEC4 & 0x03FF0000)>>16) != 0x132) &&
		(((regECC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		DBG_8192D("Path B Rx IQK fail!!\n");

	return result;
}

static u8				/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_PathB_IQK_5G_Normal(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32	regEAC, regEB4, regEC4;
	u8	result = 0x00;
	u8	i = 0;
#if MP_DRIVER == 1
	u8	retryCount = 9;
#else
	u8	retryCount = 2;
#endif
	u8	timeout = 20, timecount = 0;

	/* path-A IQK setting */
	PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x18008c1f);
	PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x18008c1f);

	PHY_SetBBReg(adapter, rTx_IQK_PI_A, bMaskDWord, 0x82110000);
	PHY_SetBBReg(adapter, rRx_IQK_PI_A, bMaskDWord, 0x68110000);

	/* path-B IQK setting */
	PHY_SetBBReg(adapter, rTx_IQK_Tone_B, bMaskDWord, 0x18008c2f);
	PHY_SetBBReg(adapter, rRx_IQK_Tone_B, bMaskDWord, 0x18008c2f);
	PHY_SetBBReg(adapter, rTx_IQK_PI_B, bMaskDWord, 0x82140307);
	PHY_SetBBReg(adapter, rRx_IQK_PI_B, bMaskDWord, 0x68160960);

	/* LO calibration setting */
	PHY_SetBBReg(adapter, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	/* path-B PA on */
	PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, bMaskDWord, 0x0f600700);
	PHY_SetBBReg(adapter, rFPGA0_XB_RFInterfaceOE, bMaskDWord, 0x061f0d30);

	for (i = 0 ; i < retryCount ; i++)
	{
		/* One shot, path B LOK & IQK */
		PHY_SetBBReg(adapter, rIQK_AGC_Pts, bMaskDWord, 0xfa000000);
		PHY_SetBBReg(adapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

		/*  delay x ms */
		rtw_mdelay_os(IQK_DELAY_TIME*10);

		while (timecount < timeout && PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, BIT29) == 0x00) {
			rtw_udelay_os(IQK_DELAY_TIME*1000*2);
			timecount++;
		}

		timecount = 0;
		while (timecount < timeout && PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_B_2, 0x3FF0000) == 0x00) {
			rtw_udelay_os(IQK_DELAY_TIME*1000*2);
			timecount++;
		}

		/*  Check failed */
		regEAC = PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord);
		regEB4 = PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_B, bMaskDWord);
		regEC4= PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_B_2, bMaskDWord);

		if (!(regEAC & BIT31) &&
			(((regEB4 & 0x03FF0000)>>16) != 0x142))
			result |= 0x01;
		else
			continue;

		if (!(regEAC & BIT30) &&
			(((regEC4 & 0x03FF0000)>>16) != 0x132))
		{
			result |= 0x02;
			break;
		}
		else
		{
		}
	}

	/* path B PA off */
	PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, bMaskDWord, pdmpriv->IQK_BB_backup[0]);
	PHY_SetBBReg(adapter, rFPGA0_XB_RFInterfaceOE, bMaskDWord, pdmpriv->IQK_BB_backup[2]);

	if (!(result & 0x01))	/* Tx IQK fail */
	{
		PHY_SetBBReg(adapter, rTx_IQK_Tone_B, bMaskDWord, 0x19008c00);
	}

	if (!(result & 0x02))	/* Rx IQK fail */
	{
		PHY_SetBBReg(adapter, rOFDM0_XBRxIQImbalance , bMaskDWord, 0x40000100);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_B , bMaskDWord, 0x19008c00);
		DBG_8192D("Path B Rx IQK fail!!0xe54 = 0x%x\n", PHY_QueryBBReg(adapter, rRx_IQK_Tone_B, bMaskDWord));
	}

	return result;
}

static void
phy_PathAFillIQKMatrix(
	struct rtw_adapter *	adapter,
	bool	bIQKOK,
	int		result[][8],
	u8		final_candidate,
	bool	bTxOnly
	)
{
	u32	Oldval_0, X, TX0_A, reg;
	int	Y, TX0_C;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	if (final_candidate == 0xFF)
		return;

	else if (bIQKOK)
	{
		Oldval_0 = (PHY_QueryBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord) >> 22) & 0x3FF;/* OFDM0_D */

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX0_A = (X * Oldval_0) >> 8;
		PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, 0x3FF, TX0_A);
		if (IS_HARDWARE_TYPE_8192D(adapter))
			PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT24, ((X* Oldval_0>>7) & 0x1));
		else
			PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT(31), ((X* Oldval_0>>7) & 0x1));

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		/* path B IQK result + 3 */
		if (pHalData->interfaceIndex == 1 && pHalData->CurrentBandType92D == BAND_ON_5G)
			Y += 3;

		TX0_C = (Y * Oldval_0) >> 8;
		PHY_SetBBReg(adapter, rOFDM0_XCTxAFE, 0xF0000000, ((TX0_C&0x3C0)>>6));
		PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, 0x003F0000, (TX0_C&0x3F));
		if (IS_HARDWARE_TYPE_8192D(adapter)/*&&is2T*/)
			PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT26, ((Y* Oldval_0>>7) & 0x1));
		else
			PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT(29), ((Y* Oldval_0>>7) & 0x1));

	        if (bTxOnly)
		{
			return;
		}

		reg = result[final_candidate][2];
		PHY_SetBBReg(adapter, rOFDM0_XARxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		PHY_SetBBReg(adapter, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		PHY_SetBBReg(adapter, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
}

static void
phy_PathAFillIQKMatrix_5G_Normal(
	struct rtw_adapter *	adapter,
	bool	bIQKOK,
	int		result[][8],
	u8		final_candidate,
	bool	bTxOnly
	)
{
	u32	X, reg;
	int	Y;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	if (bIQKOK && final_candidate != 0xFF)
	{
		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;

		PHY_SetBBReg(adapter, rTx_IQK_Tone_A, 0x3FF0000, X);
		PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT24, 0);

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		/* path A/B IQK result + 3, suggest by Jenyu */
		if (pHalData->CurrentBandType92D == BAND_ON_5G)
			Y += 3;

		PHY_SetBBReg(adapter, rTx_IQK_Tone_A, 0x003FF, Y);
		PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT26, 0);

		if (bTxOnly)
		{
			return;
		}

		reg = result[final_candidate][2];
		PHY_SetBBReg(adapter, rOFDM0_XARxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		PHY_SetBBReg(adapter, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		PHY_SetBBReg(adapter, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
	else
	{
		DBG_8192D("phy_PathAFillIQKMatrix Tx/Rx FAIL restore default value\n");
		PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x19008c00);
		PHY_SetBBReg(adapter, rOFDM0_XARxIQImbalance , bMaskDWord, 0x40000100);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_A , bMaskDWord, 0x19008c00);
	}
}

static void
phy_PathBFillIQKMatrix(
	struct rtw_adapter *	adapter,
	bool	bIQKOK,
	int		result[][8],
	u8		final_candidate,
	bool	bTxOnly			/* do Tx only */
	)
{
	u32	Oldval_1, X, TX1_A, reg;
	int	Y, TX1_C;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

        if (final_candidate == 0xFF)
		return;

	else if (bIQKOK)
	{
		Oldval_1 = (PHY_QueryBBReg(adapter, rOFDM0_XBTxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX1_A = (X * Oldval_1) >> 8;
		PHY_SetBBReg(adapter, rOFDM0_XBTxIQImbalance, 0x3FF, TX1_A);
		if (IS_HARDWARE_TYPE_8192D(adapter))
			PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT28, ((X* Oldval_1>>7) & 0x1));
		else
			PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT(27), ((X* Oldval_1>>7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;
		if (pHalData->CurrentBandType92D == BAND_ON_5G)
			Y += 3;		/* temp modify for preformance */
		TX1_C = (Y * Oldval_1) >> 8;
		PHY_SetBBReg(adapter, rOFDM0_XDTxAFE, 0xF0000000, ((TX1_C&0x3C0)>>6));
		PHY_SetBBReg(adapter, rOFDM0_XBTxIQImbalance, 0x003F0000, (TX1_C&0x3F));
		if (IS_HARDWARE_TYPE_8192D(adapter))
			PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT30, ((Y* Oldval_1>>7) & 0x1));
		else
			PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT(25), ((Y* Oldval_1>>7) & 0x1));

		if (bTxOnly)
			return;

		reg = result[final_candidate][6];
		PHY_SetBBReg(adapter, rOFDM0_XBRxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][7] & 0x3F;
		PHY_SetBBReg(adapter, rOFDM0_XBRxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		PHY_SetBBReg(adapter, rOFDM0_AGCRSSITable, 0x0000F000, reg);
	}
}

static void
phy_PathBFillIQKMatrix_5G_Normal(
	struct rtw_adapter *	adapter,
	bool	bIQKOK,
	int		result[][8],
	u8		final_candidate,
	bool	bTxOnly			/* do Tx only */
	)
{
	u32	X, reg;
	int	Y;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	if (bIQKOK && final_candidate != 0xFF)
	{
		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;

		PHY_SetBBReg(adapter, 0xe50, 0x3FF0000, X);
		PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT28, 0);

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;
		if (pHalData->CurrentBandType92D == BAND_ON_5G)
			Y += 3;		/* temp modify for preformance, suggest by Jenyu */

		PHY_SetBBReg(adapter, rTx_IQK_Tone_B, 0x003FF, Y);
		PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT30, 0);

		if (bTxOnly)
			return;

		reg = result[final_candidate][6];
		PHY_SetBBReg(adapter, rOFDM0_XBRxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][7] & 0x3F;
		PHY_SetBBReg(adapter, rOFDM0_XBRxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		PHY_SetBBReg(adapter, rOFDM0_AGCRSSITable, 0x0000F000, reg);
	}
	else
	{
		DBG_8192D("phy_PathBFillIQKMatrix Tx/Rx FAIL\n");
		PHY_SetBBReg(adapter, rTx_IQK_Tone_B, bMaskDWord, 0x19008c00);
		PHY_SetBBReg(adapter, rOFDM0_XBRxIQImbalance , bMaskDWord, 0x40000100);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_B , bMaskDWord, 0x19008c00);
	}
}

static void
phy_SaveADDARegisters(
	struct rtw_adapter *	adapter,
	u32*		ADDAReg,
	u32*		ADDABackup,
	u32			RegisterNum
	)
{
	u32	i;

	for (i = 0 ; i < RegisterNum ; i++) {
		ADDABackup[i] = PHY_QueryBBReg(adapter, ADDAReg[i], bMaskDWord);
	}
}

static void
phy_SaveMACRegisters(
	struct rtw_adapter *	adapter,
	u32*		MACReg,
	u32*		MACBackup
	)
{
	u32	i;

	for (i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++) {
		MACBackup[i] = rtw_read8(adapter, MACReg[i]);
	}
	MACBackup[i] = rtw_read32(adapter, MACReg[i]);
}

static void
phy_ReloadADDARegisters(
	struct rtw_adapter *	adapter,
	u32*		ADDAReg,
	u32*		ADDABackup,
	u32			RegiesterNum
	)
{
	u32	i;

	for (i = 0 ; i < RegiesterNum ; i++) {
		/* path-A/B BB to initial gain */
		if (ADDAReg[i] == rOFDM0_XAAGCCore1 || ADDAReg[i] == rOFDM0_XBAGCCore1)
			PHY_SetBBReg(adapter, ADDAReg[i], bMaskDWord, 0x50);
		PHY_SetBBReg(adapter, ADDAReg[i], bMaskDWord, ADDABackup[i]);
	}
}

static void
phy_ReloadMACRegisters(
	struct rtw_adapter *	adapter,
	u32*		MACReg,
	u32*		MACBackup
	)
{
	u32	i;

	for (i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++) {
		rtw_write8(adapter, MACReg[i], (u8)MACBackup[i]);
	}
	rtw_write32(adapter, MACReg[i], MACBackup[i]);
}

static void
phy_PathADDAOn(
	struct rtw_adapter *	adapter,
	u32*		ADDAReg,
	bool		isPathAOn,
	bool		is2T
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u32	pathOn;
	u32	i;

	pathOn = isPathAOn ? 0x04db25a4 : 0x0b1b25a4;
	/*  Modified by Neil Chen */
	/*  for Path diversity and original IQK */
	if (isPathAOn)     /*  Neil Chen */
		pathOn = pHalData->interfaceIndex == 0? 0x04db25a4 : 0x0b1b25a4;

	for (i = 0 ; i < IQK_ADDA_REG_NUM ; i++) {
		PHY_SetBBReg(adapter, ADDAReg[i], bMaskDWord, pathOn);
	}
}

static void
phy_MACSettingCalibration(
	struct rtw_adapter *	adapter,
	u32*		MACReg,
	u32*		MACBackup
	)
{
	u32	i = 0;

	rtw_write8(adapter, MACReg[i], 0x3F);

	for (i = 1 ; i < (IQK_MAC_REG_NUM - 1); i++) {
		rtw_write8(adapter, MACReg[i], (u8)(MACBackup[i]&(~BIT3)));
	}
	rtw_write8(adapter, MACReg[i], (u8)(MACBackup[i]&(~BIT5)));
}

static void
phy_PathAStandBy(
	struct rtw_adapter *	adapter
	)
{

	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x0);
	PHY_SetBBReg(adapter, 0x840, bMaskDWord, 0x00010000);
	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
}

static void
phy_PIModeSwitch(
	struct rtw_adapter *	adapter,
	bool		PIMode
	)
{
	u32	mode;

	mode = PIMode ? 0x01000100 : 0x01000000;
	PHY_SetBBReg(adapter, 0x820, bMaskDWord, mode);
	PHY_SetBBReg(adapter, 0x828, bMaskDWord, mode);
}

static bool
phy_SimularityCompare_92D(
	struct rtw_adapter *	adapter,
	int		result[][8],
	u8		 c1,
	u8		 c2
	)
{
	u32	i, j, diff, SimularityBitMap, bound = 0, u4temp = 0;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	final_candidate[2] = {0xFF, 0xFF};	/* for path A and path B */
	bool		bResult = true;
	bool		is2T = IS_92D_SINGLEPHY(pHalData->VersionID);

	if (is2T)
		bound = 8;
	else
		bound = 4;

	SimularityBitMap = 0;

	/* check Tx */
	for (i = 0; i < bound; i++)
	{
		diff = (result[c1][i] > result[c2][i]) ? (result[c1][i] - result[c2][i]) : (result[c2][i] - result[c1][i]);
		if (diff > MAX_TOLERANCE_92D)
		{
			if ((i == 2 || i == 6) && !SimularityBitMap)
			{
				if (result[c1][i]+result[c1][i+1] == 0)
					final_candidate[(i/4)] = c2;
				else if (result[c2][i]+result[c2][i+1] == 0)
					final_candidate[(i/4)] = c1;
				else
					SimularityBitMap = SimularityBitMap|(1<<i);
			}
			else
				SimularityBitMap = SimularityBitMap|(1<<i);
		}
	}

	if (SimularityBitMap == 0)
	{
		for (i = 0; i < (bound/4); i++)
		{
			if (final_candidate[i] != 0xFF)
			{
				for (j = i*4; j < (i+1)*4-2; j++)
					result[3][j] = result[final_candidate[i]][j];
				bResult = false;
			}
		}

		for (i = 0; i < bound; i++)
		{
			u4temp += (result[c1][i]+	result[c2][i]);
		}
		if (u4temp == 0)	/* IQK fail for c1 & c2 */
			bResult = false;

		return bResult;
	}

	if (!(SimularityBitMap & 0x0F))			/* path A OK */
	{
		for (i = 0; i < 4; i++)
			result[3][i] = result[c1][i];
	}
	else if (!(SimularityBitMap & 0x03))		/* path A, Tx OK */
	{
		for (i = 0; i < 2; i++)
			result[3][i] = result[c1][i];
	}

	if (!(SimularityBitMap & 0xF0) && is2T)		/* path B OK */
	{
		for (i = 4; i < 8; i++)
			result[3][i] = result[c1][i];
	}
	else if (!(SimularityBitMap & 0x30))		/* path B, Tx OK */
	{
		for (i = 4; i < 6; i++)
			result[3][i] = result[c1][i];
	}

	return false;
}

/*
return false => do IQK again
*/
static bool
phy_SimularityCompare(
	struct rtw_adapter *	adapter,
	int		result[][8],
	u8		 c1,
	u8		 c2
	)
{
	return phy_SimularityCompare_92D(adapter, result, c1, c2);
}

static void
phy_IQCalibrate(
	struct rtw_adapter *	adapter,
	int		result[][8],
	u8		t,
	bool		is2T
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			i;
	u8			PathAOK, PathBOK;
	u32			ADDA_REG[IQK_ADDA_REG_NUM] = {
						rFPGA0_XCD_SwitchControl,	rBlue_Tooth,
						rRx_Wait_CCA,		rTx_CCK_RFON,
						rTx_CCK_BBON,	rTx_OFDM_RFON,
						rTx_OFDM_BBON,	rTx_To_Rx,
						rTx_To_Tx,		rRx_CCK,
						rRx_OFDM,		rRx_Wait_RIFS,
						rRx_TO_Rx,		rStandby,
						rSleep,				rPMPD_ANAEN };
	u32			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE,		REG_BCN_CTRL,
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	/* since 92C & 92D have the different define in IQK_BB_REG */
	u32	IQK_BB_REG_92C[IQK_BB_REG_NUM_92C] = {
							rOFDM0_TRxPathEnable,		rOFDM0_TRMuxPar,
							rFPGA0_XCD_RFInterfaceSW,	rConfig_AntA,	rConfig_AntB,
							rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,
							rFPGA0_XB_RFInterfaceOE,	rFPGA0_RFMOD
							};

	u32	IQK_BB_REG_92D[IQK_BB_REG_NUM_92D] = {	/* for normal */
							rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,
							rFPGA0_XB_RFInterfaceOE,	rOFDM0_TRMuxPar,
							rFPGA0_XCD_RFInterfaceSW,	rOFDM0_TRxPathEnable,
							rFPGA0_RFMOD,			rFPGA0_AnalogParameter4,
							rOFDM0_XAAGCCore1,		rOFDM0_XBAGCCore1
						};

#if MP_DRIVER
	const u32	retryCount = 9;
#else
	const u32	retryCount = 2;
#endif

	/*  Note: IQ calibration must be performed after loading */
	/*		PHY_REG.txt , and radio_a, radio_b.txt */
	if (t == 0) {
		/*  Save ADDA parameters, turn Path A ADDA on */
		phy_SaveADDARegisters(adapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);
		phy_SaveMACRegisters(adapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);
		if (IS_HARDWARE_TYPE_8192D(adapter))
			phy_SaveADDARegisters(adapter, IQK_BB_REG_92D, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D);
		else
			phy_SaveADDARegisters(adapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92C);
	}

	phy_PathADDAOn(adapter, ADDA_REG, true, is2T);

	if (IS_HARDWARE_TYPE_8192D(adapter))
		PHY_SetBBReg(adapter, rPdp_AntA, bMaskDWord, 0x01017038);

	if (t==0)
		pdmpriv->bRfPiEnable = (u8)PHY_QueryBBReg(adapter, rFPGA0_XA_HSSIParameter1, BIT(8));

	if (!pdmpriv->bRfPiEnable) {
		/*  Switch BB to PI mode to do IQ Calibration. */
		phy_PIModeSwitch(adapter, true);
	}

	PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, BIT24, 0x00);
	PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	PHY_SetBBReg(adapter, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	PHY_SetBBReg(adapter, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22204000);
	if (IS_HARDWARE_TYPE_8192D(adapter))
		PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0xf00000, 0x0f);
	else
	{
		PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0x01);
		PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0x01);
		PHY_SetBBReg(adapter, rFPGA0_XA_RFInterfaceOE, BIT10, 0x00);
		PHY_SetBBReg(adapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0x00);
	}

	if (is2T)
	{
		PHY_SetBBReg(adapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00010000);
		PHY_SetBBReg(adapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00010000);
	}

	/* MAC settings */
	phy_MACSettingCalibration(adapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	if (IS_HARDWARE_TYPE_8192D(adapter))
	{
		PHY_SetBBReg(adapter, rConfig_AntA, bMaskDWord, 0x0f600000);

		if (is2T)
		{
			PHY_SetBBReg(adapter, rConfig_AntB, bMaskDWord, 0x0f600000);
		}
	}
	else
	{
		/* Page B init */
		PHY_SetBBReg(adapter, rConfig_AntA, bMaskDWord, 0x00080000);

		if (is2T)
		{
			PHY_SetBBReg(adapter, rConfig_AntB, bMaskDWord, 0x00080000);
		}
	}

	/*  IQ calibration setting */
	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(adapter, rTx_IQK, bMaskDWord, 0x01007c00);
	PHY_SetBBReg(adapter, rRx_IQK, bMaskDWord, 0x01004800);

	for (i = 0 ; i < retryCount ; i++) {
		PathAOK = phy_PathA_IQK(adapter, is2T);
		if (PathAOK == 0x03) {
			result[t][0] = (PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(adapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][2] = (PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			result[t][3] = (PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			break;
		}
		else if (i == (retryCount-1) && PathAOK == 0x01)	/* Tx IQK OK */
		{

			result[t][0] = (PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(adapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
		}
	}

	if (0x00 == PathAOK) {
		DBG_8192D("Path A IQK failed!!\n");
	}

	if (is2T) {
		phy_PathAStandBy(adapter);

		/*  Turn Path B ADDA on */
		phy_PathADDAOn(adapter, ADDA_REG, false, is2T);

		for (i = 0 ; i < retryCount ; i++) {
			PathBOK = phy_PathB_IQK(adapter);
			if (PathBOK == 0x03) {
				result[t][4] = (PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(adapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][6] = (PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (PHY_QueryBBReg(adapter, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				break;
			}
			else if (i == (retryCount - 1) && PathBOK == 0x01)	/* Tx IQK OK */
			{
				result[t][4] = (PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(adapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
			}
		}

		if (0x00 == PathBOK) {
			DBG_8192D("Path B IQK failed!!\n");
		}
	}

	/* Back to BB mode, load original value */
	/* RTPRINT(FINIT, INIT_IQK, ("IQK:Back to BB mode, load original value!\n")); */
	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0);

	if (t!=0)
	{
		if (!pdmpriv->bRfPiEnable) {
			/*  Switch back BB to SI mode after finish IQ Calibration. */
			phy_PIModeSwitch(adapter, false);
		}

		/*  Reload ADDA power saving parameters */
		phy_ReloadADDARegisters(adapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);

		/*  Reload MAC parameters */
		phy_ReloadMACRegisters(adapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

		/*  Reload BB parameters */
		if (IS_HARDWARE_TYPE_8192D(adapter))
		{
			if (is2T)
				phy_ReloadADDARegisters(adapter, IQK_BB_REG_92D, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D);
			else
				phy_ReloadADDARegisters(adapter, IQK_BB_REG_92D, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D -1);
		}
		else
			phy_ReloadADDARegisters(adapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92C);

		if (!IS_HARDWARE_TYPE_8192D(adapter))
		{
			/*  Restore RX initial gain */
			PHY_SetBBReg(adapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032ed3);
			if (is2T) {
				PHY_SetBBReg(adapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00032ed3);
			}
		}

		/* load 0xe30 IQC default value */
		PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);

	}
}

static void
phy_IQCalibrate_5G(
	struct rtw_adapter *	adapter,
	int		result[][8]
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			extPAon, REG0xe5c, RX0REG0xe40, REG0xe40, REG0xe94;
	u32			REG0xeac, RX1REG0xe40, REG0xeb4, REG0xea4,REG0xec4;
	u8			TX0IQKOK = false, TX1IQKOK = false ;
	u32			TX_X0, TX_Y0, TX_X1, TX_Y1, RX_X0, RX_Y0, RX_X1, RX_Y1;
	u32			ADDA_REG[IQK_ADDA_REG_NUM] = {
						rFPGA0_XCD_SwitchControl,	rBlue_Tooth,
						rRx_Wait_CCA,		rTx_CCK_RFON,
						rTx_CCK_BBON,	rTx_OFDM_RFON,
						rTx_OFDM_BBON,	rTx_To_Rx,
						rTx_To_Tx,		rRx_CCK,
						rRx_OFDM,		rRx_Wait_RIFS,
						rRx_TO_Rx,		rStandby,
						rSleep,				rPMPD_ANAEN };

	u32			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE,		REG_BCN_CTRL,
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u32			IQK_BB_REG[IQK_BB_REG_NUM_test] = {	/* for normal */
						rFPGA0_XAB_RFInterfaceSW,	rOFDM0_TRMuxPar,
						rFPGA0_XCD_RFInterfaceSW,	rOFDM0_TRxPathEnable,
						rFPGA0_RFMOD,			rFPGA0_AnalogParameter4
					};

	bool			is2T =  IS_92D_SINGLEPHY(pHalData->VersionID);

	DBG_8192D("IQK for 5G:Start!!!interface %d\n", pHalData->interfaceIndex);

	DBG_8192D("IQ Calibration for %s\n", (is2T ? "2T2R" : "1T1R"));

	/* Save MAC default value */
	phy_SaveMACRegisters(adapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	/* Save BB Parameter */
	phy_SaveADDARegisters(adapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_test);

	/* Save AFE Parameters */
	phy_SaveADDARegisters(adapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);

	/* 1 Path-A TX IQK */
	/* Path-A AFE all on */
	phy_PathADDAOn(adapter, ADDA_REG, true, true);

	/* MAC register setting */
	phy_MACSettingCalibration(adapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	/* IQK must be done in PI mode */
	pdmpriv->bRfPiEnable = (u8)PHY_QueryBBReg(adapter, rFPGA0_XA_HSSIParameter1, BIT(8));
	if (!pdmpriv->bRfPiEnable)
		phy_PIModeSwitch(adapter, true);

	/* TXIQK RF setting */
	PHY_SetBBReg(adapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x01940000);
	PHY_SetBBReg(adapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x01940000);

	/* BB setting */
	PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	PHY_SetBBReg(adapter, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	PHY_SetBBReg(adapter, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22208000);
	PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, BIT6|BIT5,  0x03);
	PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, BIT22|BIT21,  0x03);
	PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0xf00000,  0x0f);

	/* AP or IQK */
	PHY_SetBBReg(adapter, rConfig_AntA, bMaskDWord, 0x0f600000);
	PHY_SetBBReg(adapter, rConfig_AntB, bMaskDWord, 0x0f600000);

	/* IQK global setting */
	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(adapter, rTx_IQK, bMaskDWord, 0x10007c00);
	PHY_SetBBReg(adapter, rRx_IQK, bMaskDWord, 0x01004800);

	/* path-A IQK setting */
	if (pHalData->interfaceIndex == 0)
	{
		PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c1f);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1f);
	}
	else
	{
		PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c22);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x30008c22);
	}

	if (is2T)
		PHY_SetBBReg(adapter, rTx_IQK_PI_A, bMaskDWord, 0x821402e2);
	else
		PHY_SetBBReg(adapter, rTx_IQK_PI_A, bMaskDWord, 0x821402e6);
	PHY_SetBBReg(adapter, rRx_IQK_PI_A, bMaskDWord, 0x68110000);

	/* path-B IQK setting */
	if (is2T)
	{
		PHY_SetBBReg(adapter, rTx_IQK_Tone_B, bMaskDWord, 0x14008c22);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_B, bMaskDWord, 0x30008c22);
		PHY_SetBBReg(adapter, rTx_IQK_PI_B, bMaskDWord, 0x82110000);
		PHY_SetBBReg(adapter, rRx_IQK_PI_B, bMaskDWord, 0x68110000);
	}

	/* LO calibration setting */
	PHY_SetBBReg(adapter, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	/* One shot, path A LOK & IQK */
	PHY_SetBBReg(adapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	PHY_SetBBReg(adapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	/* Delay 1 ms */
	rtw_udelay_os(IQK_DELAY_TIME*1000);

	/* Exit IQK mode */
	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x00000000);

	/* Check_TX_IQK_A_result() */
	REG0xe40 = PHY_QueryBBReg(adapter, rTx_IQK, bMaskDWord);
	REG0xeac = PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	REG0xe94 = PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_A, bMaskDWord);

	if (((REG0xeac&BIT(28)) == 0) && (((REG0xe94&0x3FF0000)>>16)!=0x142))
	{
		TX_X0 = (PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
		TX_Y0 = (PHY_QueryBBReg(adapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
		RX0REG0xe40 =  0x80000000 | (REG0xe40 & 0xfc00fc00) | (TX_X0<<16) | TX_Y0;
		result[0][0] = TX_X0;
		result[0][1] = TX_Y0;
		TX0IQKOK = true;
		DBG_8192D("IQK for 5G: Path A TxOK interface %u\n", pHalData->interfaceIndex);
	}
	else
	{
		DBG_8192D("IQK for 5G: Path A Tx Fail interface %u\n", pHalData->interfaceIndex);
	}

	/* 1 path A RX IQK */
	if (TX0IQKOK == true)
	{

		DBG_8192D("IQK for 5G: Path A Rx  START interface %u\n", pHalData->interfaceIndex);

		/* TXIQK RF setting */
		PHY_SetBBReg(adapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x01900000);
		PHY_SetBBReg(adapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x01900000);

		/* turn on external PA */
		if (pHalData->interfaceIndex == 1)
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT(30), 0x01);

		/* IQK global setting */
		PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x80800000);

		/* path-A IQK setting */
		if (pHalData->interfaceIndex == 0)
		{
			PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c1f);
			PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x14008c1f);
		}
		else
		{
			PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c22);
			PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x14008c22);
		}
		PHY_SetBBReg(adapter, rTx_IQK_PI_A, bMaskDWord, 0x82110000);
		if (pHalData->interfaceIndex == 0)
			PHY_SetBBReg(adapter, rRx_IQK_PI_A, bMaskDWord, (pHalData->CurrentChannel<=140)?0x68160c62:0x68160c66);
		else
			PHY_SetBBReg(adapter, rRx_IQK_PI_A, bMaskDWord, 0x68160962);

		/* path-B IQK setting */
		if (is2T)
		{
			PHY_SetBBReg(adapter, rTx_IQK_Tone_B, bMaskDWord, 0x14008c22);
			PHY_SetBBReg(adapter, rRx_IQK_Tone_B, bMaskDWord, 0x14008c22);
			PHY_SetBBReg(adapter, rTx_IQK_PI_B, bMaskDWord, 0x82110000);
			PHY_SetBBReg(adapter, rRx_IQK_PI_B, bMaskDWord, 0x68110000);
		}

		/* load TX0 IMR setting */
		PHY_SetBBReg(adapter, rTx_IQK, bMaskDWord, RX0REG0xe40);
		/* Sleep(5) -> delay 1ms */
		rtw_udelay_os(IQK_DELAY_TIME*1000);

		/* LO calibration setting */
		PHY_SetBBReg(adapter, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

		/* One shot, path A LOK & IQK */
		PHY_SetBBReg(adapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
		PHY_SetBBReg(adapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

		/* Delay 3 ms */
		rtw_udelay_os(3*IQK_DELAY_TIME*1000);

		/* Exit IQK mode */
		PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x00000000);

		/* Check_RX_IQK_A_result() */
		REG0xeac = PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord);
		REG0xea4 = PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_A_2, bMaskDWord);
		if (pHalData->interfaceIndex == 0)
		{
			if (((REG0xeac&BIT(27)) == 0) && (((REG0xea4&0x3FF0000)>>16)!=0x132))
			{
				RX_X0 =  (PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				RX_Y0 =  (PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[0][2] = RX_X0;
				result[0][3] = RX_Y0;
			}
		}
		else
		{
			if (((REG0xeac&BIT(30)) == 0) && (((REG0xea4&0x3FF0000)>>16)!=0x132))
			{
				RX_X0 =  (PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				RX_Y0 =  (PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[0][2] = RX_X0;
				result[0][3] = RX_Y0;
			}
		}
	}

	if (!is2T)
		goto Exit_IQK;

	/* 1 path B TX IQK */
	/* Path-B AFE all on */

	DBG_8192D("IQK for 5G: Path B Tx  START interface %u\n", pHalData->interfaceIndex);

	phy_PathADDAOn(adapter, ADDA_REG, false, true);

	/* TXIQK RF setting */
	PHY_SetBBReg(adapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x01940000);
	PHY_SetBBReg(adapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x01940000);

	/* IQK global setting */
	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(adapter, rTx_IQK, bMaskDWord, 0x10007c00);

	/* path-A IQK setting */
	PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c1f);
	PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1f);
	PHY_SetBBReg(adapter, rTx_IQK_PI_A, bMaskDWord, 0x82110000);
	PHY_SetBBReg(adapter, rRx_IQK_PI_A, bMaskDWord, 0x68110000);

	/* path-B IQK setting */
	PHY_SetBBReg(adapter, rTx_IQK_Tone_B, bMaskDWord, 0x14008c22);
	PHY_SetBBReg(adapter, rRx_IQK_Tone_B, bMaskDWord, 0x30008c22);
	PHY_SetBBReg(adapter, rTx_IQK_PI_B, bMaskDWord, 0x82140386);
	PHY_SetBBReg(adapter, rRx_IQK_PI_B, bMaskDWord, 0x68110000);

	/* LO calibration setting */
	PHY_SetBBReg(adapter, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	/* One shot, path A LOK & IQK */
	PHY_SetBBReg(adapter, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	PHY_SetBBReg(adapter, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	/* Delay 1 ms */
	rtw_udelay_os(IQK_DELAY_TIME*1000);

	/* Exit IQK mode */
	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x00000000);

	/*  Check_TX_IQK_B_result() */
	REG0xe40 = PHY_QueryBBReg(adapter, rTx_IQK, bMaskDWord);
	REG0xeac = PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	REG0xeb4 = PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_B, bMaskDWord);
	if (((REG0xeac&BIT(31)) == 0) && ((REG0xeb4&0x3FF0000)!=0x142))
	{
		TX_X1 = (PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
		TX_Y1 = (PHY_QueryBBReg(adapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
		RX1REG0xe40 = 0x80000000 | (REG0xe40 & 0xfc00fc00) | (TX_X1<<16) | TX_Y1;
		result[0][4] = TX_X1;
		result[0][5] = TX_Y1;
		TX1IQKOK = true;
	}

	/* 1 path B RX IQK */
	if (TX1IQKOK == true)
	{

		DBG_8192D("IQK for 5G: Path B Rx  START interface %u\n", pHalData->interfaceIndex);

		if (pHalData->CurrentChannel<=140)
		{
			REG0xe5c = 0x68160960;
			extPAon = 0x1;
		}
		else
		{
			REG0xe5c = 0x68150c66;
			extPAon = 0x0;
		}

		/* TXIQK RF setting */
		PHY_SetBBReg(adapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x01900000);
		PHY_SetBBReg(adapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x01900000);

		/* turn on external PA */
		PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT(30), extPAon);

		/* BB setting */
		PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, bMaskDWord, 0xcc300080);

		/* IQK global setting */
		PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x80800000);

		/* path-A IQK setting */
		PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x14008c1f);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x34008c1f);
		PHY_SetBBReg(adapter, rTx_IQK_PI_A, bMaskDWord, 0x82110000);
		PHY_SetBBReg(adapter, rRx_IQK_PI_A, bMaskDWord, 0x68110000);

		/* path-B IQK setting */
		PHY_SetBBReg(adapter, rTx_IQK_Tone_B, bMaskDWord, 0x14008c22);
		PHY_SetBBReg(adapter, rRx_IQK_Tone_B, bMaskDWord, 0x14008c22);
		PHY_SetBBReg(adapter, rTx_IQK_PI_B, bMaskDWord, 0x82110000);
		PHY_SetBBReg(adapter, rRx_IQK_PI_B, bMaskDWord, REG0xe5c);

		/* load TX0 IMR setting */
		PHY_SetBBReg(adapter, rTx_IQK, bMaskDWord, RX1REG0xe40);

		/* Sleep(5) -> delay 1ms */
		rtw_udelay_os(IQK_DELAY_TIME*1000);

		/* LO calibration setting */
		PHY_SetBBReg(adapter, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

		/* One shot, path A LOK & IQK */
		PHY_SetBBReg(adapter, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
		PHY_SetBBReg(adapter, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

		/* Delay 1 ms */
		rtw_udelay_os(3*IQK_DELAY_TIME*1000);

		/* Check_RX_IQK_B_result() */
		REG0xeac = PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord);
		REG0xec4 = PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_B_2, bMaskDWord);
		if (((REG0xeac&BIT(30)) == 0) && (((REG0xec4&0x3FF0000)>>16)!=0x132))
		{
			RX_X1 =  (PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
			RX_Y1 =  (PHY_QueryBBReg(adapter, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
			result[0][6] = RX_X1;
			result[0][7] = RX_Y1;
		}
	}

Exit_IQK:
	/* turn off external PA */
	if (pHalData->interfaceIndex == 1 || is2T)
		PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT(30), 0);

	/* Exit IQK mode */
	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x00000000);
	phy_ReloadADDARegisters(adapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_test);

	PHY_SetBBReg(adapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x01900000);
	PHY_SetBBReg(adapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x01900000);
	PHY_SetBBReg(adapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032fff);
	PHY_SetBBReg(adapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00032fff);

	/* reload MAC default value */
	phy_ReloadMACRegisters(adapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	if (!pdmpriv->bRfPiEnable)
		phy_PIModeSwitch(adapter, false);
	/* Reload ADDA power saving parameters */
	phy_ReloadADDARegisters(adapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);
}

static void
phy_IQCalibrate_5G_Normal(
	struct rtw_adapter *	adapter,
	int		result[][8],
	u8		t
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			PathAOK, PathBOK;
	u32			ADDA_REG[IQK_ADDA_REG_NUM] = {
						rFPGA0_XCD_SwitchControl,	rBlue_Tooth,
						rRx_Wait_CCA,		rTx_CCK_RFON,
						rTx_CCK_BBON,	rTx_OFDM_RFON,
						rTx_OFDM_BBON,	rTx_To_Rx,
						rTx_To_Tx,		rRx_CCK,
						rRx_OFDM,		rRx_Wait_RIFS,
						rRx_TO_Rx,		rStandby,
						rSleep,				rPMPD_ANAEN };
	u32			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE,		REG_BCN_CTRL,
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u32			IQK_BB_REG[IQK_BB_REG_NUM] = {	/* for normal */
						rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,
						rFPGA0_XB_RFInterfaceOE,	rOFDM0_TRMuxPar,
						rFPGA0_XCD_RFInterfaceSW,	rOFDM0_TRxPathEnable,
						rFPGA0_RFMOD,			rFPGA0_AnalogParameter4,
						rOFDM0_XAAGCCore1,		rOFDM0_XBAGCCore1
					};

	/*  Note: IQ calibration must be performed after loading */
	/*		PHY_REG.txt , and radio_a, radio_b.txt */
	/* 3 PathDiv */
       /* Neil Chen--2011--05--19-- */
	u8                 rfPathDiv;   /* for Path Diversity */
	/*  */

	bool		is2T =  IS_92D_SINGLEPHY(pHalData->VersionID);

	rtw_mdelay_os(IQK_DELAY_TIME*20);

	if (t==0) {
		/*  Save ADDA parameters, turn Path A ADDA on */
		phy_SaveADDARegisters(adapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);
		phy_SaveMACRegisters(adapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);
		if (is2T)
			phy_SaveADDARegisters(adapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D);
		else
			phy_SaveADDARegisters(adapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D-1);
	}

	/*  */
	/* 3 Path Diversity */
	/* Path-A AFE all on */
	/* Neil Chen--2011--05--19 */
	rfPathDiv =(u8) (PHY_QueryBBReg(adapter, 0xB30, bMaskDWord)>>27);

	if ((rfPathDiv&0x01)==1)   /*  Div on */
	{
		phy_PathADDAOn(adapter, ADDA_REG, false, is2T);
	}
	else
		phy_PathADDAOn(adapter, ADDA_REG, true, is2T);
	/* 3 end */
       /*  */

	/* Path-A AFE all on */

	/* MAC settings */
	phy_MACSettingCalibration(adapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	if (t==0)
	{
		pdmpriv->bRfPiEnable = (u8)PHY_QueryBBReg(adapter, rFPGA0_XA_HSSIParameter1, BIT(8));
	}

	if (!pdmpriv->bRfPiEnable) {
		/*  Switch BB to PI mode to do IQ Calibration. */
		phy_PIModeSwitch(adapter, true);
	}

	PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, BIT24, 0x00);
	PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	PHY_SetBBReg(adapter, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	PHY_SetBBReg(adapter, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22208000);
	PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0xf00000, 0x0f);

	/* Page A AP setting for IQK */
	PHY_SetBBReg(adapter, rPdp_AntA, bMaskDWord, 0x00000000);
	PHY_SetBBReg(adapter, rConfig_AntA, bMaskDWord, 0x20000000);

	/* Page B AP setting for IQK */
	if (is2T) {
		PHY_SetBBReg(adapter, rPdp_AntB, bMaskDWord, 0x00000000);
		PHY_SetBBReg(adapter, rConfig_AntB, bMaskDWord, 0x20000000);
	}
	/*  IQ calibration setting */
	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(adapter, rTx_IQK, bMaskDWord, 0x10007c00);
	PHY_SetBBReg(adapter, rRx_IQK, bMaskDWord, 0x01004800);

	{
		PathAOK = phy_PathA_IQK_5G_Normal(adapter, is2T);
		if (PathAOK == 0x03) {
			DBG_8192D("Path A IQK Success!!\n");
			result[t][0] = (PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(adapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][2] = (PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			result[t][3] = (PHY_QueryBBReg(adapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
		}
		else if (PathAOK == 0x01)	/* Tx IQK OK */
		{
			DBG_8192D("Path A IQK Only  Tx Success!!\n");

			result[t][0] = (PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(adapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
		}
		else
		{
			PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0);
			DBG_8192D("0xe70 = 0x%x\n", PHY_QueryBBReg(adapter, rRx_Wait_CCA, bMaskDWord));
			DBG_8192D("RF path A 0x0 = 0x%x\n", PHY_QueryRFReg(adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask));
			PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
			DBG_8192D("Path A IQK Fail!!\n");
		}
	}

	if (is2T) {

		/*  Turn Path B ADDA on */
		phy_PathADDAOn(adapter, ADDA_REG, false, is2T);

		{
			PathBOK = phy_PathB_IQK_5G_Normal(adapter);
			if (PathBOK == 0x03) {
				DBG_8192D("Path B IQK Success!!\n");
				result[t][4] = (PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(adapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][6] = (PHY_QueryBBReg(adapter, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (PHY_QueryBBReg(adapter, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
			}
			else if (PathBOK == 0x01)	/* Tx IQK OK */
			{
				DBG_8192D("Path B Only Tx IQK Success!!\n");
				result[t][4] = (PHY_QueryBBReg(adapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(adapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
			}
			else {
				DBG_8192D("Path B IQK failed!!\n");
			}
		}
	}

	/* Back to BB mode, load original value */
	PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0);

	if (t!=0)
	{
		if (is2T)
			phy_ReloadADDARegisters(adapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D);
		else
			phy_ReloadADDARegisters(adapter, IQK_BB_REG, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM_92D-1);

		/* path A IQ path to DP block */
		PHY_SetBBReg(adapter, rPdp_AntA, bMaskDWord, 0x010170b8);

		/* path B IQ path to DP block */
		if (is2T)
			PHY_SetBBReg(adapter, rPdp_AntB, bMaskDWord, 0x010170b8);

		/*  Reload MAC parameters */
		phy_ReloadMACRegisters(adapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

		if (!pdmpriv->bRfPiEnable) {
			/*  Switch back BB to SI mode after finish IQ Calibration. */
			phy_PIModeSwitch(adapter, false);
		}

		/*  Reload ADDA power saving parameters */
		phy_ReloadADDARegisters(adapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);
	}
}

#if SWLCK != 1
static void
phy_LCCalibrate92D(
	struct rtw_adapter *	adapter,
	bool		is2T
	)
{
	u8	tmpReg, index = 0;
	u32	RF_mode[2], tmpu4Byte[2];
	u8	path = is2T?2:1;
#if SWLCK == 1
	u16	timeout = 800, timecount = 0;
#endif

	/* Check continuous TX and Packet TX */
	tmpReg = rtw_read8(adapter, 0xd03);

	if ((tmpReg&0x70) != 0)			/* Deal with contisuous TX case */
		rtw_write8(adapter, 0xd03, tmpReg&0x8F);	/* disable all continuous TX */
	else							/*  Deal with Packet TX case */
		rtw_write8(adapter, REG_TXPAUSE, 0xFF);			/*  block all queues */

	PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0xF00000, 0x0F);

	for (index = 0; index <path; index ++)
	{
		/* 1. Read original RF mode */
		RF_mode[index] = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_AC, bRFRegOffsetMask);

		/* 2. Set RF mode = standby mode */
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_AC, 0x70000, 0x01);

		tmpu4Byte[index] = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G4, bRFRegOffsetMask);
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G4, 0x700, 0x07);

		/* 4. Set LC calibration begin */
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_CHNLBW, 0x08000, 0x01);

	}

#if SWLCK == 1
	for (index = 0; index <path; index ++)
	{
		while (!(PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G6, BIT11)) &&
			timecount <= timeout)
		{

			#ifdef CONFIG_LONG_DELAY_ISSUE
			rtw_msleep_os(50);
			#else
			rtw_mdelay_os(50);
			#endif
			timecount += 50;
		}
	}
#else
	#ifdef CONFIG_LONG_DELAY_ISSUE
	rtw_msleep_os(100);
	#else
	rtw_mdelay_os(100);
	#endif
#endif

	/* Restore original situation */
	for (index = 0; index <path; index ++)
	{
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G4, bRFRegOffsetMask, tmpu4Byte[index]);
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_AC, bRFRegOffsetMask, RF_mode[index]);
	}

	if ((tmpReg&0x70) != 0)
	{
		/* Path-A */
		rtw_write8(adapter, 0xd03, tmpReg);
	}
	else /*  Deal with Packet TX case */
	{
		rtw_write8(adapter, REG_TXPAUSE, 0x00);
	}

	PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0xF00000, 0x00);
}
#endif  /* SWLCK != 1, amy, temp remove */

static u32
get_abs(
	u32	val1,
	u32	val2
	)
{
	u32 ret=0;

	if (val1 >= val2)
	{
		ret = val1 - val2;
	}
	else
	{
		ret = val2 - val1;
	}
	return ret;
}

#define	TESTFLAG			0
#define	BASE_CHNL_NUM		6
#define	BASE_CHNL_NUM_2G	2

static void
phy_CalcCurvIndex(
	struct rtw_adapter *	adapter,
	u32*		TargetChnl,
	u32*		CurveCountVal,
	bool		is5G,
	u32*		CurveIndex
	)
{
	u32	smallestABSVal = 0xffffffff, u4tmp;
	u8	i, channel=1, pre_channel=1, start = is5G?TARGET_CHNL_NUM_2G:0,
		start_base = is5G?BASE_CHNL_NUM_2G:0,
		end_base = is5G?BASE_CHNL_NUM:BASE_CHNL_NUM_2G;
	u8	chnl_num = is5G?TARGET_CHNL_NUM_2G_5G:TARGET_CHNL_NUM_2G;
	u8	Base_chnl[BASE_CHNL_NUM] = {1, 14, 36, 100, 149};
	u32	j, base_index = 0, search_bound = 128;

	for (i = start; i < chnl_num; i++) {
		if (is5G) {
			if (i != start)
				pre_channel = channel;
			channel = GetChnlFromPlace(i);	/* actual channel number */

			if (i == start)
				pre_channel = channel;
		} else {
			if (i != start)
				pre_channel = channel;
			channel = i+1;

			if (i == start)
				pre_channel = channel;
		}

		for (j = start_base; j < end_base; j++) {
			if (channel == Base_chnl[j]) {
				base_index = 0;
				search_bound = (CV_CURVE_CNT*2);	/* search every 128 */
				break;
			}
			else if (channel < Base_chnl[j] || j == end_base-1)
			{
				base_index = CurveIndex[GetRightChnlPlace(pre_channel)-1];

				if (base_index > 5)
					base_index -= 5;	/* search -5~5, not every 128 */
				else
					base_index = 0;
				search_bound = base_index+10;
				break;
			}
		}

		CurveIndex[i] = 0;
		for (j=base_index; j<base_index+search_bound; j++)
		{
			u4tmp = get_abs(TargetChnl[channel-1], CurveCountVal[j]);

			if (u4tmp < smallestABSVal)
			{
				CurveIndex[i] = j;
				smallestABSVal = u4tmp;
			}
		}

		smallestABSVal = 0xffffffff;
	}
}

static void
phy_LCCalibrate92DSW(
	struct rtw_adapter *	adapter,
	bool		is2T
	)
{
	u8	RF_mode[2], tmpReg, index = 0;
#if (TESTFLAG == 0)
	u32	tmpu4Byte[2];
#endif /* TESTFLAG == 0) */
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	u1bTmp=0,path = is2T?2:1;
	u32	i, u4tmp, offset;
	u32	curveCountVal[CV_CURVE_CNT*2]={0};
	u16	timeout = 800, timecount = 0;

	/* Check continuous TX and Packet TX */
	tmpReg = rtw_read8(adapter, 0xd03);

	if ((tmpReg&0x70) != 0)			/* Deal with contisuous TX case */
		rtw_write8(adapter, 0xd03, tmpReg&0x8F);	/* disable all continuous TX */
	else							/*  Deal with Packet TX case */
		rtw_write8(adapter, REG_TXPAUSE, 0xFF);			/*  block all queues */

	PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0xF00000, 0x0F);

	for (index = 0; index <path; index ++)
	{

		/* 1. Read original RF mode */
		offset = index == 0?rOFDM0_XAAGCCore1:rOFDM0_XBAGCCore1;
		RF_mode[index] = rtw_read8(adapter, offset);

		/* 2. Set RF mode = standby mode */
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_AC, bRFRegOffsetMask, 0x010000);
#if (TESTFLAG == 0)
		tmpu4Byte[index] = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G4, bRFRegOffsetMask);
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G4, 0x700, 0x07);
#endif

		if (adapter->hw_init_completed)
		{
			/*  switch CV-curve control by LC-calibration */
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G7, BIT17, 0x0);

			/* 4. Set LC calibration begin */
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_CHNLBW, 0x08000, 0x01);

		}
	}

	for (index = 0; index <path; index ++)
	{
		u4tmp = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G6, bRFRegOffsetMask);

		while ((!(u4tmp & BIT11)) &&
			timecount <= timeout)
		{
				#ifdef CONFIG_LONG_DELAY_ISSUE
				rtw_msleep_os(50);
				#else
				rtw_mdelay_os(50);
				#endif
			timecount += 50;
			u4tmp = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G6, bRFRegOffsetMask);
		}
	}
	/* Disable TX only need during phy lck, To reduce LCK affect on chariot, */
	/*  move enable tx here after PHY LCK finish, it will not affect sw lck result. */
	/*  zhiyuan 2011/06/03 */
	if ((tmpReg&0x70) != 0)
	{
		/* Path-A */
		rtw_write8(adapter, 0xd03, tmpReg);
	}
	else /*  Deal with Packet TX case */
	{
		rtw_write8(adapter, REG_TXPAUSE, 0x00);
	}
	PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0xF00000, 0x00);

	for (index = 0; index <path; index ++)
	{
		u4tmp = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G4, bRFRegOffsetMask);

		{

			memset(&curveCountVal[0], 0, CV_CURVE_CNT*2);

			/* Set LC calibration off */
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_CHNLBW, 0x08000, 0x0);

			/* save Curve-counting number */
			for (i=0; i<CV_CURVE_CNT; i++)
			{
				u32	readVal=0, readVal2=0;
				PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_TRSW, 0x7f, i);

				PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, 0x4D, bRFRegOffsetMask, 0x0);
				readVal = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)index, 0x4F, bRFRegOffsetMask);

				curveCountVal[2*i+1] = (readVal & 0xfffe0) >> 5;
				/*  reg 0x4f [4:0] */
				/*  reg 0x50 [19:10] */
				readVal2 = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)index, 0x50, 0xffc00);
				curveCountVal[2*i] = (((readVal & 0x1F) << 10) | readVal2);

			}

			if (index == 0 && pHalData->interfaceIndex == 0)
				phy_CalcCurvIndex(adapter, TargetChnl_5G, curveCountVal, true, CurveIndex);
			else
				phy_CalcCurvIndex(adapter, TargetChnl_2G, curveCountVal, false, CurveIndex);

			/*  switch CV-curve control mode */
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G7, BIT17, 0x1);
		}

	}

	/* Restore original situation */
	for (index = 0; index <path; index ++)
	{
#if (TESTFLAG == 0)
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)index, RF_SYN_G4, bRFRegOffsetMask, tmpu4Byte[index]);
#endif
		offset = index == 0?rOFDM0_XAAGCCore1:rOFDM0_XBAGCCore1;
		rtw_write8(adapter, offset, 0x50);
		rtw_write8(adapter, offset, RF_mode[index]);
	}

	phy_ReloadLCKSetting(adapter, pHalData->CurrentChannel);
}

static void
phy_LCCalibrate(
	struct rtw_adapter *	adapter,
	bool		is2T
	)
{
#if SWLCK == 1
	phy_LCCalibrate92DSW(adapter, is2T);
#else
	phy_LCCalibrate92D(adapter, is2T);
#endif
}

/* Analog Pre-distortion calibration */
#define		APK_BB_REG_NUM	8
#define		APK_CURVE_REG_NUM 4
#define		PATH_NUM		2

static void
phy_APCalibrate(
	struct rtw_adapter *	adapter,
	char		delta,
	bool		is2T
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			regD[PATH_NUM];
	u32			tmpReg, index, offset, path, i, pathbound = PATH_NUM, apkbound;

	u32			BB_backup[APK_BB_REG_NUM];
	u32			BB_REG[APK_BB_REG_NUM] = {
						rFPGA1_TxBlock,		rOFDM0_TRxPathEnable,
						rFPGA0_RFMOD,	rOFDM0_TRMuxPar,
						rFPGA0_XCD_RFInterfaceSW,	rFPGA0_XAB_RFInterfaceSW,
						rFPGA0_XA_RFInterfaceOE,	rFPGA0_XB_RFInterfaceOE	};
	u32			BB_AP_MODE[APK_BB_REG_NUM] = {
						0x00000020, 0x00a05430, 0x02040000,
						0x000800e4, 0x00204000 };
	u32			BB_normal_AP_MODE[APK_BB_REG_NUM] = {
						0x00000020, 0x00a05430, 0x02040000,
						0x000800e4, 0x22204000 };

	u32			AFE_backup[IQK_ADDA_REG_NUM];
	u32			AFE_REG[IQK_ADDA_REG_NUM] = {
						rFPGA0_XCD_SwitchControl,	rBlue_Tooth,
						rRx_Wait_CCA,		rTx_CCK_RFON,
						rTx_CCK_BBON,	rTx_OFDM_RFON,
						rTx_OFDM_BBON,	rTx_To_Rx,
						rTx_To_Tx,		rRx_CCK,
						rRx_OFDM,		rRx_Wait_RIFS,
						rRx_TO_Rx,		rStandby,
						rSleep,				rPMPD_ANAEN };

	u32			MAC_backup[IQK_MAC_REG_NUM];
	u32			MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE,		REG_BCN_CTRL,
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u32			APK_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x1852c, 0x5852c, 0x1852c, 0x5852c},
					{0x2852e, 0x0852e, 0x3852e, 0x0852e, 0x0852e}
					};

	u32			APK_normal_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x0a52c, 0x3a52c, 0x5a52c, 0x5a52c},	/* path settings equal to path b settings */
					{0x0852c, 0x0a52c, 0x5a52c, 0x5a52c, 0x5a52c}
					};

	u32			APK_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52014, 0x52013, 0x5200f, 0x5208d},
					{0x5201a, 0x52019, 0x52016, 0x52033, 0x52050}
					};

	u32			APK_normal_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a},	/* path settings equal to path b settings */
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a}
					};
	u32			AFE_on_off[PATH_NUM] = {
					0x04db25a4, 0x0b1b25a4};	/* path A on path B off / path A off path B on */

	u32			APK_offset[PATH_NUM] = {
					rConfig_AntA, rConfig_AntB};

	u32			APK_normal_offset[PATH_NUM] = {
					rConfig_Pmpd_AntA, rConfig_Pmpd_AntB};

	u32			APK_value[PATH_NUM] = {
					0x92fc0000, 0x12fc0000};

	u32			APK_normal_value[PATH_NUM] = {
					0x92680000, 0x12680000};

	char			APK_delta_mapping[APK_BB_REG_NUM][13] = {
					{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-6, -4, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-11, -9, -7, -5, -3, -1, 0, 0, 0, 0, 0, 0, 0}
					};

	u32			APK_normal_setting_value_1[13] = {
					0x01017018, 0xf7ed8f84, 0x1b1a1816, 0x2522201e, 0x322e2b28,
					0x433f3a36, 0x5b544e49, 0x7b726a62, 0xa69a8f84, 0xdfcfc0b3,
					0x12680000, 0x00880000, 0x00880000
					};

	u32			APK_normal_setting_value_2[16] = {
					0x01c7021d, 0x01670183, 0x01000123, 0x00bf00e2, 0x008d00a3,
					0x0068007b, 0x004d0059, 0x003a0042, 0x002b0031, 0x001f0025,
					0x0017001b, 0x00110014, 0x000c000f, 0x0009000b, 0x00070008,
					0x00050006
					};

	u32			APK_result[PATH_NUM][APK_BB_REG_NUM];	/* val_1_1a, val_1_2a, val_2a, val_3a, val_4a */

	int			BB_offset, delta_V, delta_offset;

#if (MP_DRIVER == 1)
	PMPT_CONTEXT	pMptCtx = &adapter->mppriv.MptCtx;

	pMptCtx->APK_bound[0] = 45;
	pMptCtx->APK_bound[1] = 52;
#endif

	if (!is2T)
		pathbound = 1;

	/* 2 FOR NORMAL CHIP SETTINGS */

/*  Temporarily do not allow normal driver to do the following settings because these offset */
/*  and value will cause RF internal PA to be unpredictably disabled by HW, such that RF Tx signal */
/*  will disappear after disable/enable card many times on 88CU. RF SD and DD have not find the */
/*  root cause, so we remove these actions temporarily. Added by tynli and SD3 Allen. 2010.05.31. */
#if MP_DRIVER == 1

	/* settings adjust for normal chip */
	for (index = 0; index < PATH_NUM; index ++)
	{
		APK_offset[index] = APK_normal_offset[index];
		APK_value[index] = APK_normal_value[index];
		AFE_on_off[index] = 0x6fdb25a4;
	}

	for (index = 0; index < APK_BB_REG_NUM; index ++)
	{
		for (path = 0; path < pathbound; path++)
		{
			APK_RF_init_value[path][index] = APK_normal_RF_init_value[path][index];
			APK_RF_value_0[path][index] = APK_normal_RF_value_0[path][index];
		}
		BB_AP_MODE[index] = BB_normal_AP_MODE[index];
	}

	apkbound = 6;

	/* save BB default value */
	for (index = 0; index < APK_BB_REG_NUM ; index++)
	{
		if (index == 0)		/* skip */
			continue;
		BB_backup[index] = PHY_QueryBBReg(adapter, BB_REG[index], bMaskDWord);
	}

	/* save MAC default value */
	phy_SaveMACRegisters(adapter, MAC_REG, MAC_backup);

	/* save AFE default value */
	phy_SaveADDARegisters(adapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	for (path = 0; path < pathbound; path++)
	{

		if (path == RF_PATH_A)
		{
			/* path A APK */
			/* load APK setting */
			/* path-A */
			offset = rPdp_AntA;
			for (index = 0; index < 11; index ++)
			{
				PHY_SetBBReg(adapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);

				offset += 0x04;
			}

			PHY_SetBBReg(adapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);

			offset = rConfig_AntA;
			for (; index < 13; index ++)
			{
				PHY_SetBBReg(adapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);

				offset += 0x04;
			}

			/* page-B1 */
			PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x40000000);

			/* path A */
			offset = rPdp_AntA;
			for (index = 0; index < 16; index++)
			{
				PHY_SetBBReg(adapter, offset, bMaskDWord, APK_normal_setting_value_2[index]);

				offset += 0x04;
			}
			PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x00000000);
		}
		else if (path == RF_PATH_B)
		{
			/* path B APK */
			/* load APK setting */
			/* path-B */
			offset = rPdp_AntB;
			for (index = 0; index < 10; index ++)
			{
				PHY_SetBBReg(adapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);

				offset += 0x04;
			}
			PHY_SetBBReg(adapter, rConfig_Pmpd_AntA, bMaskDWord, 0x12680000);

			PHY_SetBBReg(adapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);

			offset = rConfig_AntA;
			index = 11;
			for (; index < 13; index ++) /* offset 0xb68, 0xb6c */
			{
				PHY_SetBBReg(adapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);

				offset += 0x04;
			}

			/* page-B1 */
			PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x40000000);

			/* path B */
			offset = 0xb60;
			for (index = 0; index < 16; index++)
			{
				PHY_SetBBReg(adapter, offset, bMaskDWord, APK_normal_setting_value_2[index]);

				offset += 0x04;
			}
			PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x00000000);
		}

		/* save RF default value */
		regD[path] = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask);

		/* Path A AFE all on, path B AFE All off or vise versa */
		for (index = 0; index < IQK_ADDA_REG_NUM ; index++)
			PHY_SetBBReg(adapter, AFE_REG[index], bMaskDWord, AFE_on_off[path]);

		/* BB to AP mode */
		if (path == 0)
		{
			for (index = 0; index < APK_BB_REG_NUM ; index++)
			{

				if (index == 0)		/* skip */
					continue;
				else if (index < 5)
				PHY_SetBBReg(adapter, BB_REG[index], bMaskDWord, BB_AP_MODE[index]);
				else if (BB_REG[index] == 0x870)
					PHY_SetBBReg(adapter, BB_REG[index], bMaskDWord, BB_backup[index]|BIT10|BIT26);
				else
					PHY_SetBBReg(adapter, BB_REG[index], BIT10, 0x0);
			}

			PHY_SetBBReg(adapter, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
			PHY_SetBBReg(adapter, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		}
		else		/* path B */
		{
			PHY_SetBBReg(adapter, rTx_IQK_Tone_B, bMaskDWord, 0x01008c00);
			PHY_SetBBReg(adapter, rRx_IQK_Tone_B, bMaskDWord, 0x01008c00);

		}

		/* MAC settings */
		phy_MACSettingCalibration(adapter, MAC_REG, MAC_backup);

		if (path == RF_PATH_A)	/* Path B to standby mode */
		{
			PHY_SetRFReg(adapter, RF_PATH_B, RF_AC, bRFRegOffsetMask, 0x10000);
		}
		else			/* Path A to standby mode */
		{
			PHY_SetRFReg(adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x10000);
			PHY_SetRFReg(adapter, RF_PATH_A, RF_MODE1, bRFRegOffsetMask, 0x1000f);
			PHY_SetRFReg(adapter, RF_PATH_A, RF_MODE2, bRFRegOffsetMask, 0x20103);
		}

		delta_offset = ((delta+14)/2);
		if (delta_offset < 0)
			delta_offset = 0;
		else if (delta_offset > 12)
			delta_offset = 12;

		/* AP calibration */
		for (index = 0; index < APK_BB_REG_NUM; index++) {
			if (index != 1)	/* only DO PA11+PAD01001, AP RF setting */
				continue;

			tmpReg = APK_RF_init_value[path][index];
			if (!pdmpriv->bAPKThermalMeterIgnore) {
				BB_offset = (tmpReg & 0xF0000) >> 16;

				if (!(tmpReg & BIT15)) /* sign bit 0 */
					BB_offset = -BB_offset;

				delta_V = APK_delta_mapping[index][delta_offset];

				BB_offset += delta_V;

				if (BB_offset < 0) {
					tmpReg = tmpReg & (~BIT15);
					BB_offset = -BB_offset;
				} else {
					tmpReg = tmpReg | BIT15;
				}
				tmpReg = (tmpReg & 0xFFF0FFFF) | (BB_offset << 16);
			}

				PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_IPA_A, bRFRegOffsetMask, 0x8992e);
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_AC, bRFRegOffsetMask, APK_RF_value_0[path][index]);
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask, tmpReg);

			/*  PA11+PAD01111, one shot */
			i = 0;
			do {
				PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x80000000);
				PHY_SetBBReg(adapter, APK_offset[path], bMaskDWord, APK_value[0]);
				rtw_mdelay_os(3);
				PHY_SetBBReg(adapter, APK_offset[path], bMaskDWord, APK_value[1]);

				rtw_mdelay_os(20);
				PHY_SetBBReg(adapter, rFPGA0_IQK, bMaskDWord, 0x00000000);

				if (path == RF_PATH_A)
				tmpReg = PHY_QueryBBReg(adapter, rAPK, 0x03E00000);
				else
					tmpReg = PHY_QueryBBReg(adapter, rAPK, 0xF8000000);

				i++;
			}
			while (tmpReg > apkbound && i < 4);

			APK_result[path][index] = tmpReg;
		}
	}

	/* reload MAC default value */
	phy_ReloadMACRegisters(adapter, MAC_REG, MAC_backup);

	/* reload BB default value */
	for (index = 0; index < APK_BB_REG_NUM ; index++)
	{

		if (index == 0)		/* skip */
			continue;
		PHY_SetBBReg(adapter, BB_REG[index], bMaskDWord, BB_backup[index]);
	}

	/* reload AFE default value */
	phy_ReloadADDARegisters(adapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	/* reload RF path default value */
	for (path = 0; path < pathbound; path++)
	{
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask, regD[path]);
		if (path == RF_PATH_B)
		{
			PHY_SetRFReg(adapter, RF_PATH_A, RF_MODE1, bRFRegOffsetMask, 0x1000f);
			PHY_SetRFReg(adapter, RF_PATH_A, RF_MODE2, bRFRegOffsetMask, 0x20101);
		}

		/* note no index == 0 */
		if (APK_result[path][1] > 6)
			APK_result[path][1] = 6;
	}

	for (path = 0; path < pathbound; path++)
	{
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G1_G4, bRFRegOffsetMask,
		((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (APK_result[path][1] << 5) | APK_result[path][1]));
		if (path == RF_PATH_A)
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G5_G8, bRFRegOffsetMask,
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x00 << 5) | 0x05));
		else
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G5_G8, bRFRegOffsetMask,
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x02 << 5) | 0x05));

	}

	pdmpriv->bAPKdone = true;
#endif
}

static void phy_SetRFPathSwitch(
	struct rtw_adapter *	adapter,
	bool		main,
	bool		is2T
	)
{

	if (!adapter->hw_init_completed)
	{
		PHY_SetBBReg(adapter, 0x4C, BIT23, 0x01);
		PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT13, 0x01);
	}

	if (main)
		PHY_SetBBReg(adapter, rFPGA0_XA_RFInterfaceOE, 0x300, 0x2);
	else
		PHY_SetBBReg(adapter, rFPGA0_XA_RFInterfaceOE, 0x300, 0x1);

}

void
rtl8192d_PHY_IQCalibrate(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	int			result[4][8];	/* last is final result */
	u8			i, final_candidate, Indexforchannel;
	bool		bPathAOK, bPathBOK;
	int			RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC, RegTmp = 0;
	bool		is12simular, is13simular, is23simular;
	bool		bStartContTx = false, bSingleTone = false, bCarrierSuppression = false;

#if (MP_DRIVER == 1)
	bStartContTx = adapter->mppriv.MptCtx.bStartContTx;
	bSingleTone = adapter->mppriv.MptCtx.bSingleTone;
	bCarrierSuppression = adapter->mppriv.MptCtx.bCarrierSuppression;
#endif

	/* ignore IQK when continuous Tx */
	if (bStartContTx || bSingleTone || bCarrierSuppression)
		return;

#if DISABLE_BB_RF
	return;
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (pHalData->bSlaveOfDMSP)
		return;
#endif

	for (i = 0; i < 8; i++)
	{
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}
	final_candidate = 0xff;
	bPathAOK = false;
	bPathBOK = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;

	for (i=0; i<3; i++)
	{
		if (pHalData->CurrentBandType92D == BAND_ON_5G)
		{
			phy_IQCalibrate_5G_Normal(adapter, result, i);
		}
		else if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
		{
			if (IS_92D_SINGLEPHY(pHalData->VersionID))
				phy_IQCalibrate(adapter, result, i, true);
			else
				phy_IQCalibrate(adapter, result, i, false);
		}

		if (i == 1)
		{
			is12simular = phy_SimularityCompare(adapter, result, 0, 1);
			if (is12simular)
			{
				final_candidate = 0;
				break;
			}
		}

		if (i == 2)
		{
			is13simular = phy_SimularityCompare(adapter, result, 0, 2);
			if (is13simular)
			{
				final_candidate = 0;
				break;
			}

			is23simular = phy_SimularityCompare(adapter, result, 1, 2);
			if (is23simular)
				final_candidate = 1;
			else
			{
				for (i = 0; i < 8; i++)
					RegTmp += result[3][i];

				if (RegTmp != 0)
					final_candidate = 3;
				else
					final_candidate = 0xFF;
			}
		}
	}

        for (i=0; i<4; i++)
	{
		RegE94 = result[i][0];
		RegE9C = result[i][1];
		RegEA4 = result[i][2];
		RegEAC = result[i][3];
		RegEB4 = result[i][4];
		RegEBC = result[i][5];
		RegEC4 = result[i][6];
		RegECC = result[i][7];
	}

	if (final_candidate != 0xff)
	{
		pdmpriv->RegE94 = RegE94 = result[final_candidate][0];
		pdmpriv->RegE9C = RegE9C = result[final_candidate][1];
		RegEA4 = result[final_candidate][2];
		RegEAC = result[final_candidate][3];
		pdmpriv->RegEB4 = RegEB4 = result[final_candidate][4];
		pdmpriv->RegEBC = RegEBC = result[final_candidate][5];
		RegEC4 = result[final_candidate][6];
		RegECC = result[final_candidate][7];
		DBG_8192D("IQK: final_candidate is %x\n", final_candidate);
		DBG_8192D("IQK: RegE94=%x RegE9C=%x RegEA4=%x RegEAC=%x RegEB4=%x RegEBC=%x RegEC4=%x RegECC=%x\n", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC);
		bPathAOK = bPathBOK = true;
	}
	else
	{
		pdmpriv->RegE94 = pdmpriv->RegEB4 = 0x100;	/* X default value */
		pdmpriv->RegE9C = pdmpriv->RegEBC = 0x0;		/* Y default value */
	}

	if ((RegE94 != 0)/*&&(RegEA4 != 0)*/)
	{
		if (pHalData->CurrentBandType92D == BAND_ON_5G)
			phy_PathAFillIQKMatrix_5G_Normal(adapter, bPathAOK, result, final_candidate, (RegEA4 == 0));
		else
			phy_PathAFillIQKMatrix(adapter, bPathAOK, result, final_candidate, (RegEA4 == 0));
	}

	if (IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
	{
		if ((RegEB4 != 0)/*&&(RegEC4 != 0)*/)
		{
			if (pHalData->CurrentBandType92D == BAND_ON_5G)
				phy_PathBFillIQKMatrix_5G_Normal(adapter, bPathBOK, result, final_candidate, (RegEC4 == 0));
			else
				phy_PathBFillIQKMatrix(adapter, bPathBOK, result, final_candidate, (RegEC4 == 0));
		}
	}

	if (IS_HARDWARE_TYPE_8192D(adapter) && final_candidate != 0xFF)
	{
		Indexforchannel = rtl8192d_GetRightChnlPlaceforIQK(pHalData->CurrentChannel);

		for (i = 0; i < IQK_Matrix_REG_NUM; i++)
		{
			pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][i] =
				result[final_candidate][i];
		}

		pHalData->IQKMatrixRegSetting[Indexforchannel].bIQKDone = true;

#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_buddy_adapter_up(adapter)) {
			struct rtw_adapter * pbuddy_adapter = adapter->pbuddy_adapter;
			struct hal_data_8192du *pbuddy_HalData = GET_HAL_DATA(pbuddy_adapter);

			for (i = 0; i < IQK_Matrix_REG_NUM; i++)
			{
				pbuddy_HalData->IQKMatrixRegSetting[Indexforchannel].Value[0][i] =
					result[final_candidate][i];
			}

			pbuddy_HalData->IQKMatrixRegSetting[Indexforchannel].bIQKDone = true;
		}
#endif
	}
}

void
rtl8192d_PHY_LCCalibrate(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	bool		bStartContTx = false, bSingleTone = false, bCarrierSuppression = false;
	u32			timeout = 2000, timecount = 0;
#ifdef CONFIG_DUALMAC_CONCURRENT
	struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
	struct mlme_priv	*pmlmeprivBuddyadapter;
#endif

#if MP_DRIVER == 1
	bStartContTx = adapter->mppriv.MptCtx.bStartContTx;
	bSingleTone = adapter->mppriv.MptCtx.bSingleTone;
	bCarrierSuppression = adapter->mppriv.MptCtx.bCarrierSuppression;
#endif

#if DISABLE_BB_RF
	return;
#endif

	/* ignore IQK when continuous Tx */
	if (bStartContTx || bSingleTone || bCarrierSuppression)
		return;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (Buddyadapter != NULL &&
		((pHalData->interfaceIndex == 0 && pHalData->CurrentBandType92D == BAND_ON_2_4G) ||
		(pHalData->interfaceIndex == 1 && pHalData->CurrentBandType92D == BAND_ON_5G)))
	{
		pmlmeprivBuddyadapter = &Buddyadapter->mlmepriv;
		while ((check_fwstate(pmlmeprivBuddyadapter, _FW_UNDER_LINKING|_FW_UNDER_SURVEY)==true) && timecount < timeout)
		{
			rtw_msleep_os(50);
			timecount += 50;
		}
	}
#endif

	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS)
		return;

	pHalData->bLCKInProgress = true;

	if (IS_92D_SINGLEPHY(pHalData->VersionID))
	{
		phy_LCCalibrate(adapter, true);
	}
	else {
		/*  For 88C 1T1R */
		phy_LCCalibrate(adapter, false);
	}

	pHalData->bLCKInProgress = false;

}

void
rtl8192d_PHY_APCalibrate(
	struct rtw_adapter *	adapter,
	char		delta
	)
{
}

void
PHY_UpdateBBRFConfiguration8192D(
	struct rtw_adapter * adapter,
	bool bisBandSwitch
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8	eRFPath = 0;
	bool			bInternalPA;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;

	/* Update BB */
	/* r_select_5G for path_A/B.0 for 2.4G,1 for 5G */
	if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{/*  2.4G band */
		/* r_select_5G for path_A/B,0x878 */

		PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT0, 0x0);
		PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT15, 0x0);
		if (pHalData->MacPhyMode92D != DUALMAC_DUALPHY)
		{
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT16, 0x0);
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT31, 0x0);
		}

		/* rssi_table_select:index 0 for 2.4G.1~3 for 5G,0xc78 */
		PHY_SetBBReg(adapter, rOFDM0_AGCRSSITable, BIT6|BIT7, 0x0);

		/* fc_area 0xd2c */
		PHY_SetBBReg(adapter, rOFDM1_CFOTracking, BIT14|BIT13, 0x0);
		/*  5G LAN ON */
		PHY_SetBBReg(adapter, 0xB30, 0x00F00000, 0xa);

		/* TX BB gain shift*1,Just for testchip,0xc80,0xc88 */
		PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x40000100);
		PHY_SetBBReg(adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, 0x40000100);

		if (pregistrypriv->special_rf_path == 1)
			PHY_SetBBReg(adapter, rCCK0_AFESetting, bMaskByte3, 0x80);
		else if (pregistrypriv->special_rf_path == 2)
			PHY_SetBBReg(adapter, rCCK0_AFESetting, bMaskByte3, 0x45);

		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY)
		{
			PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x40000100);
			pdmpriv->OFDM_index[RF_PATH_A] = 0x0c;
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, BIT10|BIT6|BIT5,
				((pHalData->EEPROMC9&BIT3) >> 3)|(pHalData->EEPROMC9&BIT1)|((pHalData->EEPROMCC&BIT1) << 4));
			PHY_SetBBReg(adapter, rFPGA0_XA_RFInterfaceOE, BIT10|BIT6|BIT5,
				((pHalData->EEPROMC9&BIT2) >> 2)|((pHalData->EEPROMC9&BIT0) << 1)|((pHalData->EEPROMCC&BIT0) << 5));
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT15, 0);
			PHY_SetBBReg(adapter, rPdp_AntA, bMaskDWord, 0x01017038);
			PHY_SetBBReg(adapter, rConfig_AntA, bMaskDWord, 0x0f600000);
		}
		else
		{
			PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x40000100);
			PHY_SetBBReg(adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, 0x40000100);
			pdmpriv->OFDM_index[RF_PATH_A] = 0x0c;
			pdmpriv->OFDM_index[RF_PATH_B] = 0x0c;

			PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, BIT26|BIT22|BIT21|BIT10|BIT6|BIT5,
				((pHalData->EEPROMC9&BIT3) >> 3)|(pHalData->EEPROMC9&BIT1)|((pHalData->EEPROMCC&BIT1) << 4)|((pHalData->EEPROMC9&BIT7) << 9)|((pHalData->EEPROMC9&BIT5) << 12)|((pHalData->EEPROMCC&BIT3) << 18));
			PHY_SetBBReg(adapter, rFPGA0_XA_RFInterfaceOE, BIT10|BIT6|BIT5,
				((pHalData->EEPROMC9&BIT2) >> 2)|((pHalData->EEPROMC9&BIT0) << 1)|((pHalData->EEPROMCC&BIT0) << 5));
			PHY_SetBBReg(adapter, rFPGA0_XB_RFInterfaceOE, BIT10|BIT6|BIT5,
				((pHalData->EEPROMC9&BIT6) >> 6)|((pHalData->EEPROMC9&BIT4) >> 3)|((pHalData->EEPROMCC&BIT2) << 3));
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT31|BIT15, 0);
			PHY_SetBBReg(adapter, rPdp_AntA, bMaskDWord, 0x01017038);
			PHY_SetBBReg(adapter, rPdp_AntB, bMaskDWord, 0x01017038);

			PHY_SetBBReg(adapter, rConfig_AntA, bMaskDWord, 0x0f600000);
			PHY_SetBBReg(adapter, rConfig_AntB, bMaskDWord, 0x0f600000);
		}
		pdmpriv->CCK_index = 0x0c;

	}
	else	/* 5G band */
	{

		/* r_select_5G for path_A/B */
		PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT0, 0x1);
		PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT15, 0x1);
		if (pHalData->MacPhyMode92D != DUALMAC_DUALPHY)
		{
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT16, 0x1);
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT31, 0x1);
		}

		/* rssi_table_select:index 0 for 2.4G.1~3 for 5G */
		PHY_SetBBReg(adapter, rOFDM0_AGCRSSITable, BIT6|BIT7, 0x1);

		/* fc_area */
		PHY_SetBBReg(adapter, rOFDM1_CFOTracking, BIT14|BIT13, 0x1);
		/*  5G LAN ON */
		PHY_SetBBReg(adapter, 0xB30, 0x00F00000, 0x0);

		/* TX BB gain shift,Just for testchip,0xc80,0xc88 */
		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY)
		{
			if (pHalData->interfaceIndex == 0)
				bInternalPA = pHalData->InternalPA5G[0];
			else
				bInternalPA = pHalData->InternalPA5G[1];

			if (bInternalPA)
			{
				PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x2d4000b5);
				pdmpriv->OFDM_index[RF_PATH_A] = 0x12;
			}
			else
			{
				PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x20000080);
				pdmpriv->OFDM_index[RF_PATH_A] = 0x18;
			}
		}
		else
		{
			if (pHalData->InternalPA5G[0])
			{
				PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x2d4000b5);
				pdmpriv->OFDM_index[RF_PATH_A] = 0x12;
			}
			else
			{
				PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord, 0x20000080);
				pdmpriv->OFDM_index[RF_PATH_A] = 0x18;
			}

			if (pHalData->InternalPA5G[1])
			{
				PHY_SetBBReg(adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, 0x2d4000b5);
				pdmpriv->OFDM_index[RF_PATH_B] = 0x12;
			}
			else
			{
				PHY_SetBBReg(adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, 0x20000080);
				pdmpriv->OFDM_index[RF_PATH_A] = 0x18;
			}
		}

		PHY_SetBBReg(adapter, 0xB30, BIT27, 0x0);
		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY)
		{
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, BIT10|BIT6|BIT5,
				(pHalData->EEPROMCC&BIT5));
			PHY_SetBBReg(adapter, rFPGA0_XA_RFInterfaceOE, BIT10,
				((pHalData->EEPROMCC&BIT4) >> 4));
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT15,
				(pHalData->EEPROMCC&BIT4) >> 4);
			PHY_SetBBReg(adapter, rPdp_AntA, bMaskDWord, 0x01017098);
			if (pdmpriv->bDPKdone[RF_PATH_A])
				PHY_SetBBReg(adapter, 0xb68, bMaskDWord, 0x08080000);
			else
				PHY_SetBBReg(adapter, 0xb68, bMaskDWord, 0x20000000);
		}
		else
		{
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFInterfaceSW, BIT26|BIT22|BIT21|BIT10|BIT6|BIT5,
				(pHalData->EEPROMCC&BIT5)|((pHalData->EEPROMCC&BIT7) << 14));
			PHY_SetBBReg(adapter, rFPGA0_XA_RFInterfaceOE, BIT10,
				((pHalData->EEPROMCC&BIT4) >> 4));
			PHY_SetBBReg(adapter, rFPGA0_XB_RFInterfaceOE, BIT10,
				((pHalData->EEPROMCC&BIT6) >> 6));
			PHY_SetBBReg(adapter, rFPGA0_XAB_RFParameter, BIT31|BIT15,
				((pHalData->EEPROMCC&BIT4) >> 4)|((pHalData->EEPROMCC&BIT6) << 10));
			PHY_SetBBReg(adapter, rPdp_AntA, bMaskDWord, 0x01017098);
			PHY_SetBBReg(adapter, rPdp_AntB, bMaskDWord, 0x01017098);
			if (pdmpriv->bDPKdone[RF_PATH_A])
				PHY_SetBBReg(adapter, 0xb68, bMaskDWord, 0x08080000);
			else
				PHY_SetBBReg(adapter, 0xb68, bMaskDWord, 0x20000000);
			if (pdmpriv->bDPKdone[RF_PATH_B])
				PHY_SetBBReg(adapter, 0xb6c, bMaskDWord, 0x08080000);
			else
				PHY_SetBBReg(adapter, 0xb6c, bMaskDWord, 0x20000000);
		}

	}

	/* update IQK related settings */
	{
		PHY_SetBBReg(adapter, rOFDM0_XARxIQImbalance, bMaskDWord, 0x40000100);
		PHY_SetBBReg(adapter, rOFDM0_XBRxIQImbalance, bMaskDWord, 0x40000100);
		PHY_SetBBReg(adapter, rOFDM0_XCTxAFE, 0xF0000000, 0x00);
		PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold,  BIT30|BIT28|BIT26|BIT24,  0x00);
		PHY_SetBBReg(adapter, rOFDM0_XDTxAFE, 0xF0000000, 0x00);
		PHY_SetBBReg(adapter, rOFDM0_RxIQExtAnta, 0xF0000000, 0x00);
		PHY_SetBBReg(adapter, rOFDM0_AGCRSSITable, 0x0000F000, 0x00);
	}

	/* Update RF */
	for (eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	{
		if (pHalData->CurrentBandType92D == BAND_ON_2_4G) {
			/* MOD_AG for RF paht_A 0x18 BIT8,BIT16 */
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, RF_CHNLBW, BIT8|BIT16|BIT18|0xFF, 1);

			/* RF0x0b[16:14] =3b'111 */
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, RF_TXPA_AG, 0x1c000, 0x07);
		}
		else { /* 5G band */
			/* MOD_AG for RF paht_A 0x18 BIT8,BIT16 */
			PHY_SetRFReg(adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x97524); /* set channel 36 */

		}

		if ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G) ||
		    (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)) {
			/* Set right channel on RF reg0x18 for another mac. */
			if (pHalData->interfaceIndex == 0) /* set MAC1 default channel if MAC1 not up. */
			{
				if (!(rtw_read8(adapter, REG_MAC1)&MAC1_ON)) {
					rtl8192d_PHY_EnableAnotherPHY(adapter, true);
					PHY_SetRFReg(adapter, RF_PATH_A, RF_CHNLBW|MAC0_ACCESS_PHY1, bRFRegOffsetMask, 0x97524); /* set channel 36 */
					rtl8192d_PHY_PowerDownAnotherPHY(adapter, true);
				}
			} else if (pHalData->interfaceIndex == 1) { /* set MAC0 default channel */
				if (!(rtw_read8(adapter, REG_MAC0)&MAC0_ON)) {
					rtl8192d_PHY_EnableAnotherPHY(adapter, false);
					PHY_SetRFReg(adapter, RF_PATH_A, RF_CHNLBW|MAC1_ACCESS_PHY0, bRFRegOffsetMask, 0x87401); /*  set channel 1 */
					rtl8192d_PHY_PowerDownAnotherPHY(adapter, false);
				}
			}
		}
	}

	/* Update for all band. */
	if (pHalData->rf_type == RF_1T1R)
	{ /* DMDP */
		/* Use antenna 0,0xc04,0xd04 */
#if MP_DRIVER == 1
		if (!bisBandSwitch)
#endif
		{
		PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x11);
		PHY_SetBBReg(adapter, rOFDM1_TRxPathEnable, bDWord, 0x1);
		}

		/* enable ad/da clock1 for dual-phy reg0x888 */
		if (pHalData->interfaceIndex == 0)
			PHY_SetBBReg(adapter, rFPGA0_AdDaClockEn, BIT12|BIT13, 0x3);
		else
		{
			bool bMAC0NotUp =false;

			/* 3 Path Div */
			/*  Neil Chen---2011--05--31---Begin */

			bMAC0NotUp = rtl8192d_PHY_EnableAnotherPHY(adapter, false);
			if (bMAC0NotUp)
			{
				PHY_SetBBReg(adapter, rFPGA0_AdDaClockEn|MAC1_ACCESS_PHY0, BIT12|BIT13, 0x3);
				rtl8192d_PHY_PowerDownAnotherPHY(adapter, false);
			}
		}

		/* supported mcs */
		PHY_SetBBReg(adapter, rOFDM1_LSTF, BIT19|BIT20, 0x0);
	}
	else /*  2T2R Single PHY */
	{
		if (pregistrypriv->special_rf_path == 2)
		{
			PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x22);
			PHY_SetBBReg(adapter, rOFDM1_TRxPathEnable, bDWord, 0x2);
			PHY_SetBBReg(adapter, rFPGA1_TxInfo, bMaskDWord, 0x82221322);	/* OFDM Tx */
		}
		else if (pregistrypriv->special_rf_path == 1)
		{
			PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x11);
			PHY_SetBBReg(adapter, rOFDM1_TRxPathEnable, bDWord, 0x1);
			PHY_SetBBReg(adapter, rFPGA1_TxInfo, bMaskDWord, 0x81121311);	/* OFDM Tx */
		}
		else
		{
			/* Use antenna 0 & 1,0xc04,0xd04 */
			PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x33);
			PHY_SetBBReg(adapter, rOFDM1_TRxPathEnable, bDWord, 0x3);
		}

		/* disable ad/da clock1,0x888 */
		PHY_SetBBReg(adapter, rFPGA0_AdDaClockEn, BIT12|BIT13, 0);

		/* supported mcs */
		PHY_SetBBReg(adapter, rOFDM1_LSTF, BIT19|BIT20, 0x1);
	}

#if MP_DRIVER == 1
	if (bisBandSwitch)
	{
		PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable, bMaskByte0, pdmpriv->RegC04_MP);
		PHY_SetBBReg(adapter, rOFDM1_TRxPathEnable, bDWord, pdmpriv->RegD04_MP);
	}
#endif

	for (eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	{
		pHalData->RfRegChnlVal[eRFPath] = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, RF_CHNLBW, bRFRegOffsetMask);
		pdmpriv->RegRF3C[eRFPath] = PHY_QueryRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, RF_RXRF_A3, bRFRegOffsetMask);
	}

}

/*  */
/*	Description: */
/*		Read HW adapter information through EEPROM 93C46. */
/*		Or For EFUSE 92S .And Get and Set 92D MACPHY mode and Band Type. */
/*		MacPhyMode:DMDP,SMSP. */
/*		BandType:2.4G,5G. */
/*  */
/*	Assumption: */
/*		1. Boot from EEPROM and CR9346 regiser has verified. */
/*		2. PASSIVE_LEVEL (USB interface) */
/*  */
void PHY_ReadMacPhyMode92D(
		struct rtw_adapter *			adapter,
		bool		AutoloadFail
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	MacPhyCrValue = 0;

	if (AutoloadFail)
	{
		pHalData->MacPhyMode92D = SINGLEMAC_SINGLEPHY;
		return;
	}

	MacPhyCrValue = rtw_read8(adapter, REG_MAC_PHY_CTRL_NORMAL);

	DBG_8192D("PHY_ReadMacPhyMode92D():   MAC_PHY_CTRL Value %x\n",MacPhyCrValue);

	if ((MacPhyCrValue&0x03) == 0x03)
	{
		pHalData->MacPhyMode92D = DUALMAC_DUALPHY;
	}
	else if ((MacPhyCrValue&0x03) == 0x01)
	{
		pHalData->MacPhyMode92D = DUALMAC_SINGLEPHY;
	}
	else
	{
		pHalData->MacPhyMode92D = SINGLEMAC_SINGLEPHY;
	}
}

/*  */
/*	Description: */
/*		Read HW adapter information through EEPROM 93C46. */
/*		Or For EFUSE 92S .And Get and Set 92D MACPHY mode and Band Type. */
/*		MacPhyMode:DMDP,SMSP. */
/*		BandType:2.4G,5G. */
/*  */
/*	Assumption: */
/*		1. Boot from EEPROM and CR9346 regiser has verified. */
/*		2. PASSIVE_LEVEL (USB interface) */
/*  */
void PHY_ConfigMacPhyMode92D(
		struct rtw_adapter *			adapter
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	offset = REG_MAC_PHY_CTRL_NORMAL, temp;

	temp = rtw_read8(adapter, offset);
	temp &= ~(BIT(0)|BIT(1)|BIT(2));
	switch (pHalData->MacPhyMode92D) {
	case DUALMAC_DUALPHY:
		pr_info("r8192du: MacPhyMode: DUALMAC_DUALPHY\n");
		rtw_write8(adapter, offset, temp | BIT(0)|BIT(1));
		break;
	case SINGLEMAC_SINGLEPHY:
		pr_info("r8192du: MacPhyMode: SINGLEMAC_SINGLEPHY\n");
		rtw_write8(adapter, offset, temp | BIT(2));
		break;
	case DUALMAC_SINGLEPHY:
		pr_info("r8192du: MacPhyMode: DUALMAC_SINGLEPHY\n");
		rtw_write8(adapter, offset, temp | BIT(0));
		break;
	}
}

/*  */
/*	Description: */
/*		Read HW adapter information through EEPROM 93C46. */
/*		Or For EFUSE 92S .And Get and Set 92D MACPHY mode and Band Type. */
/*		MacPhyMode:DMDP,SMSP. */
/*		BandType:2.4G,5G. */
/*  */
/*	Assumption: */
/*		1. Boot from EEPROM and CR9346 regiser has verified. */
/*		2. PASSIVE_LEVEL (USB interface) */
/*  */
void PHY_ConfigMacPhyModeInfo92D(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
#ifdef CONFIG_DUALMAC_CONCURRENT
	struct rtw_adapter *Buddyadapter = adapter->pbuddy_adapter;
	struct hal_data_8192du *pHalDataBuddyadapter;
#endif

	switch (pHalData->MacPhyMode92D) {
		case DUALMAC_SINGLEPHY:
			pHalData->rf_type = RF_2T2R;
			pHalData->VersionID = (enum VERSION_8192D)(pHalData->VersionID | RF_TYPE_2T2R);
			pHalData->BandSet92D = BAND_ON_BOTH;
			pHalData->CurrentBandType92D = BAND_ON_2_4G;
#ifdef CONFIG_DUALMAC_CONCURRENT
/* get bMasetOfDMSP and bSlaveOfDMSP sync with buddy adapter */
			ACQUIRE_GLOBAL_MUTEX(GlobalCounterForMutex);
			if (Buddyadapter != NULL)
			{
				pHalDataBuddyadapter = GET_HAL_DATA(Buddyadapter);
				pHalData->bMasterOfDMSP = !pHalDataBuddyadapter->bMasterOfDMSP;
				pHalData->bSlaveOfDMSP = !pHalDataBuddyadapter->bSlaveOfDMSP;
				pHalData->CurrentBandType92D = pHalDataBuddyadapter->CurrentBandType92D;
			}
			else
			{
				if (pHalData->interfaceIndex == 0)
				{
					pHalData->bMasterOfDMSP = true;
					pHalData->bSlaveOfDMSP = false;
				}
				else if (pHalData->interfaceIndex == 1)
				{
					pHalData->bMasterOfDMSP = false;
					pHalData->bSlaveOfDMSP = true;
				}
			}
			RELEASE_GLOBAL_MUTEX(GlobalCounterForMutex);
#endif
			break;

		case SINGLEMAC_SINGLEPHY:
			pHalData->rf_type = RF_2T2R;
			pHalData->VersionID = (enum VERSION_8192D)(pHalData->VersionID | RF_TYPE_2T2R);
			pHalData->BandSet92D = BAND_ON_BOTH;
			pHalData->CurrentBandType92D = BAND_ON_2_4G;
			pHalData->bMasterOfDMSP = false;
			pHalData->bSlaveOfDMSP = false;
			break;

		case DUALMAC_DUALPHY:
			pHalData->rf_type = RF_1T1R;
			pHalData->VersionID = (enum VERSION_8192D)(pHalData->VersionID & RF_TYPE_1T1R);
			if (pHalData->interfaceIndex == 1) {
				pHalData->BandSet92D = BAND_ON_5G;
				pHalData->CurrentBandType92D = BAND_ON_5G;/* Now we let MAC1 run on 5G band. */
			}
			else {
				pHalData->BandSet92D = BAND_ON_2_4G;
				pHalData->CurrentBandType92D = BAND_ON_2_4G;/*  */
			}
			pHalData->bMasterOfDMSP = false;
			pHalData->bSlaveOfDMSP = false;
			break;

		default:
			break;
	}

	/*if (adapter->bInHctTest&&(pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY))
	{
		pHalData->CurrentBandType92D=BAND_ON_2_4G;
		pHalData->BandSet92D = BAND_ON_2_4G;
	}*/

	if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
		pHalData->CurrentChannel = 1;
	else
		pHalData->CurrentChannel = 36;

	adapter->registrypriv.channel = pHalData->CurrentChannel;

#if DBG
	switch (pHalData->VersionID)
	{
		case VERSION_NORMAL_CHIP_92D_D_CUT_SINGLEPHY:
			MSG_8192D("Chip Version ID: VERSION_NORMAL_CHIP_92D_D_CUT_SINGLEPHY.\n");
			break;
		case VERSION_NORMAL_CHIP_92D_D_CUT_DUALPHY:
			MSG_8192D("Chip Version ID: VERSION_NORMAL_CHIP_92D_D_CUT_DUALPHY.\n");
			break;
		case VERSION_NORMAL_CHIP_92D_E_CUT_SINGLEPHY:
			MSG_8192D("Chip Version ID: VERSION_NORMAL_CHIP_92D_E_CUT_SINGLEPHY.\n");
			break;
		case VERSION_NORMAL_CHIP_92D_E_CUT_DUALPHY:
			MSG_8192D("Chip Version ID: VERSION_NORMAL_CHIP_92D_E_CUT_DUALPHY.\n");
			break;
		case VERSION_NORMAL_CHIP_92D_C_CUT_SINGLEPHY:
			MSG_8192D("Chip Version ID: VERSION_NORMAL_CHIP_92D_C_CUT_SINGLEPHY.\n");
			break;
		case VERSION_NORMAL_CHIP_92D_C_CUT_DUALPHY:
			MSG_8192D("Chip Version ID: VERSION_NORMAL_CHIP_92D_C_CUT_DUALPHY.\n");
			break;
		case VERSION_TEST_CHIP_92D_SINGLEPHY:
			MSG_8192D("Chip Version ID: VERSION_TEST_CHIP_92D_SINGLEPHY.\n");
			break;
		case VERSION_TEST_CHIP_92D_DUALPHY:
			MSG_8192D("Chip Version ID: VERSION_TEST_CHIP_92D_DUALPHY.\n");
			break;
		default:
			MSG_8192D("Chip Version ID: ???????????????.0x%04X\n",pHalData->VersionID);
			break;
	}
#endif

	switch (pHalData->BandSet92D)
	{
		case BAND_ON_2_4G:
			adapter->registrypriv.wireless_mode = WIRELESS_11BG_24N;
			break;

		case BAND_ON_5G:
			adapter->registrypriv.wireless_mode = WIRELESS_11A_5N;
			break;

		case BAND_ON_BOTH:
			adapter->registrypriv.wireless_mode = WIRELESS_11ABGN;
			break;

		default:
			adapter->registrypriv.wireless_mode = WIRELESS_11ABGN;
			break;
	}
	DBG_8192D("%s(): wireless_mode = %x\n",__func__,adapter->registrypriv.wireless_mode);
}

/*  */
/*	Description: */
/*	set RX packet buffer and other setting acording to dual mac mode */
/*  */
/*	Assumption: */
/*		1. Boot from EEPROM and CR9346 regiser has verified. */
/*		2. PASSIVE_LEVEL (USB interface) */
/*  */
void PHY_ConfigMacCoexist_RFPage92D(
		struct rtw_adapter *			adapter
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	switch (pHalData->MacPhyMode92D)
	{
		case DUALMAC_DUALPHY:
			rtw_write8(adapter,REG_DMC, 0x0);
			rtw_write8(adapter,REG_RX_PKT_LIMIT,0x08);
			rtw_write16(adapter,(REG_TRXFF_BNDY+2), 0x13ff);
			break;
		case DUALMAC_SINGLEPHY:
			rtw_write8(adapter,REG_DMC, 0xf8);
			rtw_write8(adapter,REG_RX_PKT_LIMIT,0x08);
			rtw_write16(adapter,(REG_TRXFF_BNDY+2), 0x13ff);
			break;
		case SINGLEMAC_SINGLEPHY:
			rtw_write8(adapter,REG_DMC, 0x0);
			rtw_write8(adapter,REG_RX_PKT_LIMIT,0x10);
			rtw_write16(adapter, (REG_TRXFF_BNDY + 2), 0x27FF);
			break;
		default:
			break;
	}
}

void
rtl8192d_PHY_InitRxSetting(
	struct rtw_adapter * adapter
	)
{
#if MP_DRIVER == 1
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	if (pHalData->interfaceIndex == 0)
	{
		rtw_write32(adapter, REG_MACID, 0x87654321);
		rtw_write32(adapter, 0x0700, 0x87654321);
	}
	else
	{
		rtw_write32(adapter, REG_MACID, 0x12345678);
		rtw_write32(adapter, 0x0700, 0x12345678);
	}
#endif
}

void rtl8192d_PHY_ResetIQKResult(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8			i;

	for (i = 0; i < IQK_Matrix_Settings_NUM; i++) {
		pHalData->IQKMatrixRegSetting[i].Value[0][0] = 0x100;
		pHalData->IQKMatrixRegSetting[i].Value[0][2] = 0x100;
		pHalData->IQKMatrixRegSetting[i].Value[0][4] = 0x100;
		pHalData->IQKMatrixRegSetting[i].Value[0][6] = 0x100;

		pHalData->IQKMatrixRegSetting[i].Value[0][1] = 0;
		pHalData->IQKMatrixRegSetting[i].Value[0][3] = 0;
		pHalData->IQKMatrixRegSetting[i].Value[0][5] = 0;
		pHalData->IQKMatrixRegSetting[i].Value[0][7] = 0;

		pHalData->IQKMatrixRegSetting[i].bIQKDone = false;
	}
}

void rtl8192d_PHY_SetRFPathSwitch(struct rtw_adapter *adapter, bool main)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

#if DISABLE_BB_RF
	return ;
#else

	if (IS_92D_SINGLEPHY(pHalData->VersionID)) {
		phy_SetRFPathSwitch(adapter, main, true);
	} else {
		/*  For 88C 1T1R */
		phy_SetRFPathSwitch(adapter, main, false);
	}
#endif
}

void
HalChangeCCKStatus8192D(
	struct rtw_adapter *	adapter,
	bool		bCCKDisable
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	i;

	if (pHalData->BandSet92D != BAND_ON_BOTH)
	{
		return;
	}

	if (bCCKDisable)
	{
		rtw_write16(adapter, REG_RL,0x0101);
		for (i=0;i<30;i++)
		{
			if (rtw_read32(adapter, 0x200) != rtw_read32(adapter, 0x204))
			{
				DBG_8192D("packet in tx packet buffer aaaaaaaaa 0x204 %x\n", rtw_read32(adapter, 0x204));
				DBG_8192D("packet in tx packet buffer aaaaaaa 0x200 %x\n", rtw_read32(adapter, 0x200));
				rtw_udelay_os(1000);
			}
			else
			{
				break;
			}
		}

		/*if ((Buddyadapter != NULL) && Buddyadapter->bHWInitReady && (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY))
		{
			if (ACTING_AS_AP(Buddyadapter) ||ACTING_AS_AP(ADJUST_TO_ADAPTIVE_ADAPTER(Buddyadapter, FALSE)) || Buddyadapter->MgntInfo.mIbss)
				StopTxBeacon(Buddyadapter);
			PlatformEFIOWrite2Byte(Buddyadapter, REG_RL,0x0101);
			for (i=0;i<30;i++)
			{
				if (PlatformEFIORead4Byte(Buddyadapter, 0x200) != PlatformEFIORead4Byte(Buddyadapter, 0x204))
				{
					RT_TRACE(COMP_EASY_CONCURRENT,DBG_LOUD,("packet in tx packet buffer aaaaaaaaa 0x204 %x\n", PlatformEFIORead4Byte(Buddyadapter, 0x204)));
					RT_TRACE(COMP_EASY_CONCURRENT,DBG_LOUD,("packet in tx packet buffer aaaaaaa 0x200 %x\n", PlatformEFIORead4Byte(Buddyadapter, 0x200)));
					PlatformStallExecution(1000);
				}
				else
				{
					RT_TRACE(COMP_EASY_CONCURRENT,DBG_LOUD,("no packet in tx packet buffer\n"));
					break;
				}
			}

		}*/

		PHY_SetBBReg1Byte(adapter, rFPGA0_RFMOD, bOFDMEn|bCCKEn, 3);
	}
	else
	{
		u8	RetryLimit = 0x30;

		rtw_write16(adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
	}
}

void
PHY_InitPABias92D(struct rtw_adapter * adapter)
{
	u8	tmpU1b;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	bool		is92 = IS_92D_SINGLEPHY(pHalData->VersionID);
	enum RF_RADIO_PATH_E eRFPath = RF_PATH_A;

	tmpU1b = EFUSE_Read1Byte(adapter, 0x3FA);

	DBG_8192D("PHY_InitPABias92D 0x3FA 0x%x\n",tmpU1b);

	if (!(tmpU1b & BIT0) && (is92 || pHalData->interfaceIndex == 0))
	{
		PHY_SetRFReg(adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x07401);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x70000);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x0F425);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x4F425);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x8F425);

		/* Back to RX Mode */
		PHY_SetRFReg(adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x30000);
		DBG_8192D("2G PA BIAS path A\n");
	}

	if (!(tmpU1b & BIT1) && (is92 || pHalData->interfaceIndex == 1))
	{
		eRFPath = pHalData->interfaceIndex == 1?RF_PATH_A:RF_PATH_B;
		PHY_SetRFReg(adapter, eRFPath, RF_CHNLBW, bRFRegOffsetMask, 0x07401);
		PHY_SetRFReg(adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x70000);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x0F425);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x4F425);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x8F425);

		/* Back to RX Mode */
		PHY_SetRFReg(adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x30000);
		DBG_8192D("2G PA BIAS path B\n");
	}

	if (!(tmpU1b & BIT2) && (is92 || pHalData->interfaceIndex == 0))
	{
		/* 5GL_channel */
		PHY_SetRFReg(adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x17524);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x70000);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x4F496);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x8F496);

		/* 5GM_channel */
		PHY_SetRFReg(adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x37564);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x70000);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x4F496);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x8F496);

		/* 5GH_channel */
		PHY_SetRFReg(adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x57595);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x70000);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x4F496);
		PHY_SetRFReg(adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x8F496);

		/* Back to RX Mode */
		PHY_SetRFReg(adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x30000);

		DBG_8192D("5G PA BIAS path A\n");
	}

	if (!(tmpU1b & BIT3) && (is92 || pHalData->interfaceIndex == 1))
	{
		eRFPath = (pHalData->interfaceIndex == 1)?RF_PATH_A:RF_PATH_B;
		/* 5GL_channel */
		PHY_SetRFReg(adapter, eRFPath, RF_CHNLBW, bRFRegOffsetMask, 0x17524);
		PHY_SetRFReg(adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x70000);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x4F496);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x8F496);

		/* 5GM_channel */
		PHY_SetRFReg(adapter, eRFPath, RF_CHNLBW, bRFRegOffsetMask, 0x37564);
		PHY_SetRFReg(adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x70000);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x4F496);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x8F496);

		/* 5GH_channel */
		PHY_SetRFReg(adapter, eRFPath, RF_CHNLBW, bRFRegOffsetMask, 0x57595);
		PHY_SetRFReg(adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x70000);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x4F496);
		PHY_SetRFReg(adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x8F496);

		/* Back to RX Mode */
		PHY_SetRFReg(adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x30000);
		DBG_8192D("5G PA BIAS path B\n");
	}
}
