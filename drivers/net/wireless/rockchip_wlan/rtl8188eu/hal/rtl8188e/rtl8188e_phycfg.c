/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _RTL8188E_PHYCFG_C_

#include <drv_types.h>
#include <rtl8188e_hal.h>


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
/* Please refer to header file
 *--------------------Define export function prototype-----------------------*/

/*----------------------------Function Body----------------------------------*/
/*
 * 1. BB register R/W API
 *   */

#if (SIC_ENABLE == 1)
static BOOLEAN
sic_IsSICReady(
		PADAPTER	Adapter
)
{
	BOOLEAN		bRet = _FALSE;
	u32		retryCnt = 0;
	u8		sic_cmd = 0xff;

	while (1) {
		if (retryCnt++ >= SIC_MAX_POLL_CNT) {
			/* RTPRINT(FPHY, (PHY_SICR|PHY_SICW), ("[SIC], sic_IsSICReady() return FALSE\n")); */
			return _FALSE;
		}

		/* if(RT_SDIO_CANNOT_IO(Adapter)) */
		/*	return _FALSE; */

		sic_cmd = rtw_read8(Adapter, SIC_CMD_REG);
		/* sic_cmd = PlatformEFIORead1Byte(Adapter, SIC_CMD_REG); */
#if (SIC_HW_SUPPORT == 1)
		sic_cmd &= 0xf0;	/* [7:4] */
#endif
		/* RTPRINT(FPHY, (PHY_SICR|PHY_SICW), ("[SIC], sic_IsSICReady(), readback 0x%x=0x%x\n", SIC_CMD_REG, sic_cmd)); */
		if (sic_cmd == SIC_CMD_READY)
			return _TRUE;
		else {
			rtw_msleep_os(1);
			/* delay_ms(1); */
		}
	}

	return bRet;
}

/*
u32
sic_CalculateBitShift(
	u32 BitMask
	)
{
	u32 i;

	for(i=0; i<=31; i++)
	{
		if ( ((BitMask>>i) &  0x1 ) == 1)
			break;
	}

	return i;
}
*/

static u32
sic_Read4Byte(
	void		*Adapter,
	u32		offset
)
{
	u32	u4ret = 0xffffffff;
#if RTL8188E_SUPPORT == 1
	u8	retry = 0;
#endif

	/* RTPRINT(FPHY, PHY_SICR, ("[SIC], sic_Read4Byte(): read offset(%#x)\n", offset)); */

	if (sic_IsSICReady(Adapter)) {
#if (SIC_HW_SUPPORT == 1)
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_PREREAD);
		/* PlatformEFIOWrite1Byte(Adapter, SIC_CMD_REG, SIC_CMD_PREREAD); */
		/* RTPRINT(FPHY, PHY_SICR, ("write cmdreg 0x%x = 0x%x\n", SIC_CMD_REG, SIC_CMD_PREREAD)); */
#endif
		rtw_write8(Adapter, SIC_ADDR_REG, (u8)(offset & 0xff));
		/* PlatformEFIOWrite1Byte(Adapter, SIC_ADDR_REG, (u8)(offset&0xff)); */
		/* RTPRINT(FPHY, PHY_SICR, ("write 0x%x = 0x%x\n", SIC_ADDR_REG, (u8)(offset&0xff))); */
		rtw_write8(Adapter, SIC_ADDR_REG + 1, (u8)((offset & 0xff00) >> 8));
		/* PlatformEFIOWrite1Byte(Adapter, SIC_ADDR_REG+1, (u8)((offset&0xff00)>>8)); */
		/* RTPRINT(FPHY, PHY_SICR, ("write 0x%x = 0x%x\n", SIC_ADDR_REG+1, (u8)((offset&0xff00)>>8))); */
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_READ);
		/* PlatformEFIOWrite1Byte(Adapter, SIC_CMD_REG, SIC_CMD_READ); */
		/* RTPRINT(FPHY, PHY_SICR, ("write cmdreg 0x%x = 0x%x\n", SIC_CMD_REG, SIC_CMD_READ)); */

#if RTL8188E_SUPPORT == 1
		retry = 4;
		while (retry--) {
			rtw_udelay_os(50);
			/* PlatformStallExecution(50); */
		}
#else
		rtw_udelay_os(200);
		/* PlatformStallExecution(200); */
#endif

		if (sic_IsSICReady(Adapter)) {
			u4ret = rtw_read32(Adapter, SIC_DATA_REG);
			/* u4ret = PlatformEFIORead4Byte(Adapter, SIC_DATA_REG); */
			/* RTPRINT(FPHY, PHY_SICR, ("read 0x%x = 0x%x\n", SIC_DATA_REG, u4ret)); */
			/* DbgPrint("<===Read 0x%x = 0x%x\n", offset, u4ret); */
		}
	}

	return u4ret;
}

static void
sic_Write4Byte(
	void		*Adapter,
	u32		offset,
	u32		data
)
{
#if RTL8188E_SUPPORT == 1
	u8	retry = 6;
#endif
	/* DbgPrint("=>Write 0x%x = 0x%x\n", offset, data); */
	/* RTPRINT(FPHY, PHY_SICW, ("[SIC], sic_Write4Byte(): write offset(%#x)=0x%x\n", offset, data)); */
	if (sic_IsSICReady(Adapter)) {
#if (SIC_HW_SUPPORT == 1)
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_PREWRITE);
		/* PlatformEFIOWrite1Byte(Adapter, SIC_CMD_REG, SIC_CMD_PREWRITE); */
		/* RTPRINT(FPHY, PHY_SICW, ("write data 0x%x = 0x%x\n", SIC_CMD_REG, SIC_CMD_PREWRITE)); */
#endif
		rtw_write8(Adapter, SIC_ADDR_REG, (u8)(offset & 0xff));
		/* PlatformEFIOWrite1Byte(Adapter, SIC_ADDR_REG, (u8)(offset&0xff)); */
		/* RTPRINT(FPHY, PHY_SICW, ("write 0x%x=0x%x\n", SIC_ADDR_REG, (u8)(offset&0xff))); */
		rtw_write8(Adapter, SIC_ADDR_REG + 1, (u8)((offset & 0xff00) >> 8));
		/* PlatformEFIOWrite1Byte(Adapter, SIC_ADDR_REG+1, (u8)((offset&0xff00)>>8)); */
		/* RTPRINT(FPHY, PHY_SICW, ("write 0x%x=0x%x\n", (SIC_ADDR_REG+1), (u8)((offset&0xff00)>>8))); */
		rtw_write32(Adapter, SIC_DATA_REG, (u32)data);
		/* PlatformEFIOWrite4Byte(Adapter, SIC_DATA_REG, (u32)data); */
		/* RTPRINT(FPHY, PHY_SICW, ("write data 0x%x = 0x%x\n", SIC_DATA_REG, data)); */
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_WRITE);
		/* PlatformEFIOWrite1Byte(Adapter, SIC_CMD_REG, SIC_CMD_WRITE); */
		/* RTPRINT(FPHY, PHY_SICW, ("write data 0x%x = 0x%x\n", SIC_CMD_REG, SIC_CMD_WRITE)); */
#if RTL8188E_SUPPORT == 1
		while (retry--) {
			rtw_udelay_os(50);
			/* PlatformStallExecution(50); */
		}
#else
		rtw_udelay_os(150);
		/* PlatformStallExecution(150); */
#endif

	}
}
/* ************************************************************
 * extern function
 * ************************************************************ */
static void
SIC_SetBBReg(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask,
		u32		Data
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			OriginalValue, BitShift;
	u16			BBWaitCounter = 0;

	/* RTPRINT(FPHY, PHY_SICW, ("[SIC], SIC_SetBBReg() start\n")); */
#if 0
	while (PlatformAtomicExchange(&pHalData->bChangeBBInProgress, _TRUE) == _TRUE) {
		BBWaitCounter++;
		delay_ms(10); /*  1 ms */

		if ((BBWaitCounter > 100) || RT_CANNOT_IO(Adapter)) {
			/*  Wait too long, return FALSE to avoid to be stuck here. */
			RTPRINT(FPHY, PHY_SICW, ("[SIC], SIC_SetBBReg(), Fail to set BB offset(%#x)!!, WaitCnt(%d)\n", RegAddr, BBWaitCounter));
			return;
		}
	}
#endif
	/*  */
	/* Critical section start */
	/*  */

	/* RTPRINT(FPHY, PHY_SICW, ("[SIC], SIC_SetBBReg(), mask=0x%x, addr[0x%x]=0x%x\n", BitMask, RegAddr, Data)); */

	if (BitMask != bMaskDWord) { /* if not "double word" write */
		OriginalValue = sic_Read4Byte(Adapter, RegAddr);
		/* BitShift = sic_CalculateBitShift(BitMask); */
		BitShift = PHY_CalculateBitShift(BitMask);
		Data = (((OriginalValue)&(~BitMask)) | (Data << BitShift));
	}

	sic_Write4Byte(Adapter, RegAddr, Data);

	/* PlatformAtomicExchange(&pHalData->bChangeBBInProgress, _FALSE); */
	/* RTPRINT(FPHY, PHY_SICW, ("[SIC], SIC_SetBBReg() end\n")); */
}

static u32
SIC_QueryBBReg(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			ReturnValue = 0, OriginalValue, BitShift;
	u16			BBWaitCounter = 0;

	/* RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_QueryBBReg() start\n")); */

#if 0
	while (PlatformAtomicExchange(&pHalData->bChangeBBInProgress, _TRUE) == _TRUE) {
		BBWaitCounter++;
		delay_ms(10); /*  10 ms */

		if ((BBWaitCounter > 100) || RT_CANNOT_IO(Adapter)) {
			/*  Wait too long, return FALSE to avoid to be stuck here. */
			RTPRINT(FPHY, PHY_SICW, ("[SIC], SIC_QueryBBReg(), Fail to query BB offset(%#x)!!, WaitCnt(%d)\n", RegAddr, BBWaitCounter));
			return ReturnValue;
		}
	}
#endif
	OriginalValue = sic_Read4Byte(Adapter, RegAddr);
	/* BitShift = sic_CalculateBitShift(BitMask); */
	BitShift = PHY_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;

	/* RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_QueryBBReg(), 0x%x=0x%x\n", RegAddr, OriginalValue)); */
	/* RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_QueryBBReg() end\n")); */

	/* PlatformAtomicExchange(&pHalData->bChangeBBInProgress, _FALSE);	 */
	return ReturnValue;
}

void
SIC_Init(
		PADAPTER	Adapter
)
{
	/* Here we need to write 0x1b8~0x1bf = 0 after fw is downloaded */
	/* because for 8723E at beginning 0x1b8=0x1e, that will cause */
	/* sic always not be ready */
#if (SIC_HW_SUPPORT == 1)
	/* RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_Init(), write 0x%x = 0x%x\n",  */
	/*	SIC_INIT_REG, SIC_INIT_VAL)); */
	rtw_write8(Adapter, SIC_INIT_REG, SIC_INIT_VAL);
	/* PlatformEFIOWrite1Byte(Adapter, SIC_INIT_REG, SIC_INIT_VAL); */
	/* RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_Init(), write 0x%x = 0x%x\n",  */
	/*	SIC_CMD_REG, SIC_CMD_INIT)); */
	rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_INIT);
	/* PlatformEFIOWrite1Byte(Adapter, SIC_CMD_REG, SIC_CMD_INIT); */
#else
	/* RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_Init(), write 0x1b8~0x1bf = 0x0\n")); */
	rtw_write32(Adapter, SIC_CMD_REG, 0);
	/* PlatformEFIOWrite4Byte(Adapter, SIC_CMD_REG, 0); */
	rtw_write32(Adapter, SIC_CMD_REG + 4, 0);
	/* PlatformEFIOWrite4Byte(Adapter, SIC_CMD_REG+4, 0); */
#endif
}

static BOOLEAN
SIC_LedOff(
		PADAPTER	Adapter
)
{
	/* When SIC is enabled, led pin will be used as debug pin, */
	/* so don't execute led function when SIC is enabled. */
	return _TRUE;
}
#endif

/**
* Function:	PHY_QueryBBReg
*
* OverView:	Read "sepcific bits" from BB register
*
* Input:
*			PADAPTER		Adapter,
*			u32			RegAddr,		//The target address to be readback
*			u32			BitMask		//The target bit position in the target address
*										//to be readback
* Output:	None
* Return:		u32			Data			//The readback register value
* Note:		This function is equal to "GetRegSetting" in PHY programming guide
*/
u32
PHY_QueryBBReg8188E(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask
)
{
	u32	ReturnValue = 0, OriginalValue, BitShift;
	u16	BBWaitCounter = 0;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

#if (SIC_ENABLE == 1)
	return SIC_QueryBBReg(Adapter, RegAddr, BitMask);
#endif


	OriginalValue = rtw_read32(Adapter, RegAddr);
	BitShift = PHY_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;

	/* RTPRINT(FPHY, PHY_BBR, ("BBR MASK=0x%lx Addr[0x%lx]=0x%lx\n", BitMask, RegAddr, OriginalValue)); */

	return ReturnValue;

}


/**
* Function:	PHY_SetBBReg
*
* OverView:	Write "Specific bits" to BB register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			u32			RegAddr,		//The target address to be modified
*			u32			BitMask		//The target bit position in the target address
*										//to be modified
*			u32			Data			//The new register value in the target bit position
*										//of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRegSetting" in PHY programming guide
*/

void
PHY_SetBBReg8188E(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask,
		u32		Data
)
{
	HAL_DATA_TYPE	*pHalData		= GET_HAL_DATA(Adapter);
	/* u16			BBWaitCounter	= 0; */
	u32			OriginalValue, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

#if (SIC_ENABLE == 1)
	SIC_SetBBReg(Adapter, RegAddr, BitMask, Data);
	return;
#endif


	if (BitMask != bMaskDWord) { /* if not "double word" write */
		OriginalValue = rtw_read32(Adapter, RegAddr);
		BitShift = PHY_CalculateBitShift(BitMask);
		Data = ((OriginalValue & (~BitMask)) | ((Data << BitShift) & BitMask));
	}

	rtw_write32(Adapter, RegAddr, Data);

	/* RTPRINT(FPHY, PHY_BBW, ("BBW MASK=0x%lx Addr[0x%lx]=0x%lx\n", BitMask, RegAddr, Data)); */

}


/*
 * 2. RF register R/W API
 *
 **
* Function:	phy_RFSerialRead
*
* OverView:	Read regster from RF chips
*
* Input:
*			PADAPTER		Adapter,
			enum rf_path		eRFPath,	//Radio path of A/B/C/D
*			u32			Offset,		//The target address to be read
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
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset
)
{
	u32						retValue = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32						NewOffset;
	u32						tmplong, tmplong2;
	u8					RfPiEnable = 0;

	_enter_critical_mutex(&(adapter_to_dvobj(Adapter)->rf_read_reg_mutex) , NULL);
#if 0
	if (pHalData->RFChipID == RF_8225 && Offset > 0x24) /* 36 valid regs */
		return	retValue;
	if (pHalData->RFChipID == RF_8256 && Offset > 0x2D) /* 45 valid regs */
		return	retValue;
#endif
	/*  */
	/* Make sure RF register offset is correct */
	/*  */
	Offset &= 0xff;

	/*  */
	/* Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/* 2009/06/17 MH We can not execute IO for power save or other accident mode. */
	/* if(RT_CANNOT_IO(Adapter)) */
	/* { */
	/*	RTPRINT(FPHY, PHY_RFR, ("phy_RFSerialRead return all one\n")); */
	/*	return	0xFFFFFFFF; */
	/* } */

	/* For 92S LSSI Read RFLSSIRead */
	/* For RF A/B write 0x824/82c(does not work in the future) */
	/* We must use 0x824 for RF A and B to execute read trigger */
	tmplong = phy_query_bb_reg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord);
	if (eRFPath == RF_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = phy_query_bb_reg(Adapter, pPhyReg->rfHSSIPara2, bMaskDWord);

	tmplong2 = (tmplong2 & (~bLSSIReadAddress)) | (NewOffset << 23) | bLSSIReadEdge;	/* T65 RF */

	phy_set_bb_reg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord, tmplong & (~bLSSIReadEdge));
	rtw_udelay_os(10);/* PlatformStallExecution(10); */

	phy_set_bb_reg(Adapter, pPhyReg->rfHSSIPara2, bMaskDWord, tmplong2);
	rtw_udelay_os(100);/* PlatformStallExecution(100); */

	/* phy_set_bb_reg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord, tmplong|bLSSIReadEdge); */
	rtw_udelay_os(10);/* PlatformStallExecution(10); */

	if (eRFPath == RF_PATH_A)
		RfPiEnable = (u8)phy_query_bb_reg(Adapter, rFPGA0_XA_HSSIParameter1, BIT8);
	else if (eRFPath == RF_PATH_B)
		RfPiEnable = (u8)phy_query_bb_reg(Adapter, rFPGA0_XB_HSSIParameter1, BIT8);

	if (RfPiEnable) {
		/* Read from BBreg8b8, 12 bits for 8190, 20bits for T65 RF */
		retValue = phy_query_bb_reg(Adapter, pPhyReg->rfLSSIReadBackPi, bLSSIReadBackData);
		/* RTW_INFO("Readback from RF-PI : 0x%x\n", retValue); */
	} else {
		/* Read from BBreg8a0, 12 bits for 8190, 20 bits for T65 RF */
		retValue = phy_query_bb_reg(Adapter, pPhyReg->rfLSSIReadBack, bLSSIReadBackData);
		/* RTW_INFO("Readback from RF-SI : 0x%x\n", retValue); */
	}
	/* RTW_INFO("RFR-%d Addr[0x%x]=0x%x\n", eRFPath, pPhyReg->rfLSSIReadBack, retValue); */
	_exit_critical_mutex(&(adapter_to_dvobj(Adapter)->rf_read_reg_mutex) , NULL);
	return retValue;

}



/**
* Function:	phy_RFSerialWrite
*
* OverView:	Write data to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
			enum rf_path		eRFPath,	//Radio path of A/B/C/D
*			u32			Offset,		//The target address to be read
*			u32			Data			//The new register Data in the target bit position
*										//of the target to be read
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
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset,
		u32				Data
)
{
	u32						DataAndAddr = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32						NewOffset;

#if 0
	/* <Roger_TODO> We should check valid regs for RF_6052 case. */
	if (pHalData->RFChipID == RF_8225 && Offset > 0x24) /* 36 valid regs */
		return;
	if (pHalData->RFChipID == RF_8256 && Offset > 0x2D) /* 45 valid regs */
		return;
#endif

	/* 2009/06/17 MH We can not execute IO for power save or other accident mode. */
	/* if(RT_CANNOT_IO(Adapter)) */
	/* { */
	/*	RTPRINT(FPHY, PHY_RFW, ("phy_RFSerialWrite stop\n")); */
	/*	return; */
	/* } */

	Offset &= 0xff;

	/*  */
	/* Shadow Update */
	/*  */
	/* PHY_RFShadowWrite(Adapter, eRFPath, Offset, Data); */

	/*  */
	/* Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/*  */
	/* Put write addr in [5:0]  and write data in [31:16] */
	/*  */
	/* DataAndAddr = (Data<<16) | (NewOffset&0x3f); */
	DataAndAddr = ((NewOffset << 20) | (Data & 0x000fffff)) & 0x0fffffff;	/* T65 RF */

	/*  */
	/* Write Operation */
	/*  */
	phy_set_bb_reg(Adapter, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);
	/* RTPRINT(FPHY, PHY_RFW, ("RFW-%d Addr[0x%lx]=0x%lx\n", eRFPath, pPhyReg->rf3wireOffset, DataAndAddr)); */

}


/**
* Function:	PHY_QueryRFReg
*
* OverView:	Query "Specific bits" to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
			enum rf_path		eRFPath,	//Radio path of A/B/C/D
*			u32			RegAddr,		//The target address to be read
*			u32			BitMask		//The target bit position in the target address
*										//to be read
*
* Output:	None
* Return:		u32			Readback value
* Note:		This function is equal to "GetRFRegSetting" in PHY programming guide
*/
u32
PHY_QueryRFReg8188E(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				RegAddr,
		u32				BitMask
)
{
	u32 Original_Value, Readback_Value, BitShift;
	/* HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter); */
	/* u8	RFWaitCounter = 0; */
	/* _irqL	irqL; */

	if (eRFPath >= MAX_RF_PATH)
		return 0;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif


#ifdef CONFIG_USB_HCI
	/* PlatformAcquireMutex(&pHalData->mxRFOperate); */
#else
	/* _enter_critical(&pHalData->rf_lock, &irqL); */
#endif


	Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);

	BitShift =  PHY_CalculateBitShift(BitMask);
	Readback_Value = (Original_Value & BitMask) >> BitShift;

#ifdef CONFIG_USB_HCI
	/* PlatformReleaseMutex(&pHalData->mxRFOperate); */
#else
	/* _exit_critical(&pHalData->rf_lock, &irqL); */
#endif


	/* RTPRINT(FPHY, PHY_RFR, ("RFR-%d MASK=0x%lx Addr[0x%lx]=0x%lx\n", eRFPath, BitMask, RegAddr, Original_Value));//BitMask(%#lx),BitMask, */

	return Readback_Value;
}

/**
* Function:	PHY_SetRFReg
*
* OverView:	Write "Specific bits" to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
			enum rf_path		eRFPath,	//Radio path of A/B/C/D
*			u32			RegAddr,		//The target address to be modified
*			u32			BitMask		//The target bit position in the target address
*										//to be modified
*			u32			Data			//The new register Data in the target bit position
*										//of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRFRegSetting" in PHY programming guide
*/
void
PHY_SetRFReg8188E(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				RegAddr,
		u32				BitMask,
		u32				Data
)
{

	/* HAL_DATA_TYPE	*pHalData		= GET_HAL_DATA(Adapter); */
	/* u8			RFWaitCounter	= 0; */
	u32		Original_Value, BitShift;
	/* _irqL	irqL; */

	if (eRFPath >= MAX_RF_PATH)
		return;

#if (DISABLE_BB_RF == 1)
	return;
#endif

	/* RTPRINT(FINIT, INIT_RF, ("phy_set_rf_reg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx), eRFPath(%#x)\n", */
	/*	RegAddr, BitMask, Data, eRFPath)); */


#ifdef CONFIG_USB_HCI
	/* PlatformAcquireMutex(&pHalData->mxRFOperate); */
#else
	/* _enter_critical(&pHalData->rf_lock, &irqL); */
#endif


	/* RF data is 12 bits only */
	if (BitMask != bRFRegOffsetMask) {
		Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);
		BitShift =  PHY_CalculateBitShift(BitMask);
		Data = ((Original_Value & (~BitMask)) | (Data << BitShift));
	}

	phy_RFSerialWrite(Adapter, eRFPath, RegAddr, Data);


#ifdef CONFIG_USB_HCI
	/* PlatformReleaseMutex(&pHalData->mxRFOperate); */
#else
	/* _exit_critical(&pHalData->rf_lock, &irqL); */
#endif

	/* phy_query_rf_reg(Adapter,eRFPath,RegAddr,BitMask); */

}


/*
 * 3. Initial MAC/BB/RF config by reading MAC/BB/RF txt.
 *   */

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
s32 PHY_MACConfig8188E(PADAPTER Adapter)
{
	int		rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u16		val = 0;

	/*  */
	/* Config MAC */
	/*  */
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	rtStatus = phy_ConfigMACWithParaFile(Adapter, PHY_FILE_MAC_REG);
	if (rtStatus == _FAIL)
#endif
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		if (HAL_STATUS_FAILURE == odm_config_mac_with_header_file(&pHalData->odmpriv))
			rtStatus = _FAIL;
		else
			rtStatus = _SUCCESS;
#endif/* CONFIG_EMBEDDED_FWIMG */
	}

	/* 2010.07.13 AMPDU aggregation number B */
	val |= MAX_AGGR_NUM;
	val = val << 8;
	val |= MAX_AGGR_NUM;
	rtw_write16(Adapter, REG_MAX_AGGR_NUM, val);
	/* rtw_write8(Adapter, REG_MAX_AGGR_NUM, 0x0B); */

	return rtStatus;

}

/*-----------------------------------------------------------------------------
* Function:	phy_InitBBRFRegisterDefinition
*
* OverView:	Initialize Register definition offset for Radio Path A/B/C/D
*
* Input:
*			PADAPTER		Adapter,
*
* Output:	None
* Return:		None
* Note:		The initialization value is constant and it should never be changes
-----------------------------------------------------------------------------*/
static	void
phy_InitBBRFRegisterDefinition(
		PADAPTER		Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	/* RF Interface Sowrtware Control */
	pHalData->PHYRegDef[RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; /* 16 LSBs if read 32-bit from 0x870 */
	pHalData->PHYRegDef[RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; /* 16 MSBs if read 32-bit from 0x870 (16-bit for 0x872) */
	pHalData->PHYRegDef[RF_PATH_C].rfintfs = rFPGA0_XCD_RFInterfaceSW;/* 16 LSBs if read 32-bit from 0x874 */
	pHalData->PHYRegDef[RF_PATH_D].rfintfs = rFPGA0_XCD_RFInterfaceSW;/* 16 MSBs if read 32-bit from 0x874 (16-bit for 0x876) */

	/* RF Interface Output (and Enable) */
	pHalData->PHYRegDef[RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; /* 16 LSBs if read 32-bit from 0x860 */
	pHalData->PHYRegDef[RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; /* 16 LSBs if read 32-bit from 0x864 */

	/* RF Interface (Output and)  Enable */
	pHalData->PHYRegDef[RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; /* 16 MSBs if read 32-bit from 0x860 (16-bit for 0x862) */
	pHalData->PHYRegDef[RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; /* 16 MSBs if read 32-bit from 0x864 (16-bit for 0x866) */

	/* Addr of LSSI. Wirte RF register by driver */
	pHalData->PHYRegDef[RF_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter; /* LSSI Parameter */
	pHalData->PHYRegDef[RF_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;

	/* Tranceiver A~D HSSI Parameter-2 */
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  /* wire control parameter2 */
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;  /* wire control parameter2 */

	/* Tranceiver LSSI Readback SI mode */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_C].rfLSSIReadBack = rFPGA0_XC_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_D].rfLSSIReadBack = rFPGA0_XD_LSSIReadBack;

	/* Tranceiver LSSI Readback PI mode */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBackPi = TransceiverA_HSPI_Readback;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBackPi = TransceiverB_HSPI_Readback;
	/* pHalData->PHYRegDef[RF_PATH_C].rfLSSIReadBackPi = rFPGA0_XC_LSSIReadBack; */
	/* pHalData->PHYRegDef[RF_PATH_D].rfLSSIReadBackPi = rFPGA0_XD_LSSIReadBack;	 */

}

static	int
phy_BB8188E_Config_ParaFile(
		PADAPTER	Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	int			rtStatus = _SUCCESS;

	/*  */
	/* 1. Read PHY_REG.TXT BB INIT!! */
	/*  */
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	if (phy_ConfigBBWithParaFile(Adapter, PHY_FILE_PHY_REG, CONFIG_BB_PHY_REG) == _FAIL)
#endif
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		if (HAL_STATUS_FAILURE == odm_config_bb_with_header_file(&pHalData->odmpriv, CONFIG_BB_PHY_REG))
			rtStatus = _FAIL;
#endif
	}

	if (rtStatus != _SUCCESS) {
		goto phy_BB8190_Config_ParaFile_Fail;
	}

#if (MP_DRIVER == 1)
	/*  */
	/* 1.1 Read PHY_REG_MP.TXT BB INIT!! */
	/*  */
	if (Adapter->registrypriv.mp_mode == 1) {
		/* 3 Read PHY_REG.TXT BB INIT!! */
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
		if (phy_ConfigBBWithMpParaFile(Adapter, PHY_FILE_PHY_REG_MP) == _FAIL)
#endif
		{
#ifdef CONFIG_EMBEDDED_FWIMG
			if (HAL_STATUS_SUCCESS != odm_config_bb_with_header_file(&pHalData->odmpriv, CONFIG_BB_PHY_REG_MP))
				rtStatus = _FAIL;
#endif
		}

		if (rtStatus != _SUCCESS) {
			RTW_INFO("phy_BB8188E_Config_ParaFile():Write BB Reg MP Fail!!");
			goto phy_BB8190_Config_ParaFile_Fail;
		}
	}
#endif	/*  #if (MP_DRIVER == 1) */

	/*  */
	/* 3. BB AGC table Initialization */
	/*  */
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	if (phy_ConfigBBWithParaFile(Adapter, PHY_FILE_AGC_TAB, CONFIG_BB_AGC_TAB) == _FAIL)
#endif
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		if (HAL_STATUS_FAILURE == odm_config_bb_with_header_file(&pHalData->odmpriv,  CONFIG_BB_AGC_TAB))
			rtStatus = _FAIL;
#endif
	}

	if (rtStatus != _SUCCESS) {
		goto phy_BB8190_Config_ParaFile_Fail;
	}


phy_BB8190_Config_ParaFile_Fail:

	return rtStatus;
}


int
PHY_BBConfig8188E(
		PADAPTER	Adapter
)
{
	int	rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32	RegVal;
	u8	TmpU1B = 0;
	u8	value8;

	phy_InitBBRFRegisterDefinition(Adapter);


	/* Enable BB and RF */
	RegVal = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	rtw_write16(Adapter, REG_SYS_FUNC_EN, (u16)(RegVal | BIT13 | BIT0 | BIT1));

	/* 20090923 Joseph: Advised by Steven and Jenyu. Power sequence before init RF. */
	/* rtw_write8(Adapter, REG_AFE_PLL_CTRL, 0x83); */
	/* rtw_write8(Adapter, REG_AFE_PLL_CTRL+1, 0xdb); */

	rtw_write8(Adapter, REG_RF_CTRL, RF_EN | RF_RSTB | RF_SDMRSTB);

#ifdef CONFIG_USB_HCI
	rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_USBA | FEN_USBD | FEN_BB_GLB_RSTn | FEN_BBRSTB);
#elif defined CONFIG_PCI_HCI
	rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_PPLL | FEN_PCIEA | FEN_DIO_PCIE | FEN_BB_GLB_RSTn | FEN_BBRSTB);
#endif

#if 0
#ifdef CONFIG_USB_HCI
	/* To Fix MAC loopback mode fail. Suggested by SD4 Johnny. 2010.03.23. */
	rtw_write8(Adapter, REG_LDOHCI12_CTRL, 0x0f);
	rtw_write8(Adapter, 0x15, 0xe9);
#endif

	rtw_write8(Adapter, REG_AFE_XTAL_CTRL + 1, 0x80);
#endif

#ifdef CONFIG_USB_HCI
	/* rtw_write8(Adapter, 0x15, 0xe9); */
#endif


#ifdef CONFIG_PCI_HCI
#ifdef CONFIG_RTW_LED
	/* Force use left antenna by default for 88C. */
	if (adapter_to_led(Adapter)->LedStrategy != SW_LED_MODE10) {
		RegVal = rtw_read32(Adapter, REG_LEDCFG0);
		rtw_write32(Adapter, REG_LEDCFG0, RegVal | BIT23);
	}
#endif
#endif

	/*  */
	/* Config BB and AGC */
	/*  */
	rtStatus = phy_BB8188E_Config_ParaFile(Adapter);

	if (rtw_phydm_set_crystal_cap(Adapter, pHalData->crystal_cap) == _FALSE) {
		RTW_ERR("Init crystal_cap failed\n");
		rtw_warn_on(1);
		rtStatus = _FAIL;
	}

	return rtStatus;

}


int
PHY_RFConfig8188E(
		PADAPTER	Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;

	/*  */
	/* RF config */
	/*  */
	rtStatus = PHY_RF6052_Config8188E(Adapter);
#if 0
	switch (pHalData->rf_chip) {
	case RF_6052:
		rtStatus = PHY_RF6052_Config(Adapter);
		break;
	case RF_8225:
		rtStatus = PHY_RF8225_Config(Adapter);
		break;
	case RF_8256:
		rtStatus = PHY_RF8256_Config(Adapter);
		break;
	case RF_8258:
		break;
	case RF_PSEUDO_11N:
		rtStatus = PHY_RF8225_Config(Adapter);
		break;
	default: /* for MacOs Warning: "RF_TYPE_MIN" not handled in switch */
		break;
	}
#endif
	return rtStatus;
}


/*-----------------------------------------------------------------------------
 * Function:    PHY_ConfigRFWithParaFile()
 *
 * Overview:    This function read RF parameters from general file format, and do RF 3-wire
 *
 * Input:	PADAPTER			Adapter
 *			ps1Byte				pFileName
 *			u8					eRFPath
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		Delay may be required for RF configuration
 *---------------------------------------------------------------------------*/
int
rtl8188e_PHY_ConfigRFWithParaFile(
		PADAPTER		Adapter,
		u8				*pFileName,
		enum rf_path		eRFPath
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	int	rtStatus = _SUCCESS;


	return rtStatus;

}

/* ****************************************
 * The following is for High Power PA
 * **************************************** */
#define HighPowerRadioAArrayLen 22
/* This is for High power PA */
u32 Rtl8192S_HighPower_RadioA_Array[HighPowerRadioAArrayLen] = {
	0x013, 0x00029ea4,
	0x013, 0x00025e74,
	0x013, 0x00020ea4,
	0x013, 0x0001ced0,
	0x013, 0x00019f40,
	0x013, 0x00014e70,
	0x013, 0x000106a0,
	0x013, 0x0000c670,
	0x013, 0x000082a0,
	0x013, 0x00004270,
	0x013, 0x00000240,
};

/*-----------------------------------------------------------------------------
 * Function:    SetTxPowerLevel8190()
 *
 * Overview:    This function is export to "HalCommon" moudule
 *			We must consider RF path later!!!!!!!
 *
 * Input:       PADAPTER		Adapter
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
		PADAPTER	Adapter,
		u8			Channel
)
{
	phy_set_tx_power_level_by_path(Adapter, Channel, RF_PATH_A);
}

void
PHY_SetTxPowerIndex_8188E(
		PADAPTER			Adapter,
		u32					PowerIndex,
		enum rf_path			RFPath,
		u8					Rate
)
{
	if (RFPath == RF_PATH_A) {
		switch (Rate) {
		case MGN_1M:
			phy_set_bb_reg(Adapter, rTxAGC_A_CCK1_Mcs32,      bMaskByte1, PowerIndex);
			break;
		case MGN_2M:
			phy_set_bb_reg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte1, PowerIndex);
			break;
		case MGN_5_5M:
			phy_set_bb_reg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte2, PowerIndex);
			break;
		case MGN_11M:
			phy_set_bb_reg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte3, PowerIndex);
			break;

		case MGN_6M:
			phy_set_bb_reg(Adapter, rTxAGC_A_Rate18_06, bMaskByte0, PowerIndex);
			break;
		case MGN_9M:
			phy_set_bb_reg(Adapter, rTxAGC_A_Rate18_06, bMaskByte1, PowerIndex);
			break;
		case MGN_12M:
			phy_set_bb_reg(Adapter, rTxAGC_A_Rate18_06, bMaskByte2, PowerIndex);
			break;
		case MGN_18M:
			phy_set_bb_reg(Adapter, rTxAGC_A_Rate18_06, bMaskByte3, PowerIndex);
			break;

		case MGN_24M:
			phy_set_bb_reg(Adapter, rTxAGC_A_Rate54_24, bMaskByte0, PowerIndex);
			break;
		case MGN_36M:
			phy_set_bb_reg(Adapter, rTxAGC_A_Rate54_24, bMaskByte1, PowerIndex);
			break;
		case MGN_48M:
			phy_set_bb_reg(Adapter, rTxAGC_A_Rate54_24, bMaskByte2, PowerIndex);
			break;
		case MGN_54M:
			phy_set_bb_reg(Adapter, rTxAGC_A_Rate54_24, bMaskByte3, PowerIndex);
			break;

		case MGN_MCS0:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte0, PowerIndex);
			break;
		case MGN_MCS1:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte1, PowerIndex);
			break;
		case MGN_MCS2:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte2, PowerIndex);
			break;
		case MGN_MCS3:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte3, PowerIndex);
			break;

		case MGN_MCS4:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte0, PowerIndex);
			break;
		case MGN_MCS5:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte1, PowerIndex);
			break;
		case MGN_MCS6:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte2, PowerIndex);
			break;
		case MGN_MCS7:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte3, PowerIndex);
			break;

		case MGN_MCS8:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs11_Mcs08, bMaskByte0, PowerIndex);
			break;
		case MGN_MCS9:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs11_Mcs08, bMaskByte1, PowerIndex);
			break;
		case MGN_MCS10:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs11_Mcs08, bMaskByte2, PowerIndex);
			break;
		case MGN_MCS11:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs11_Mcs08, bMaskByte3, PowerIndex);
			break;

		case MGN_MCS12:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs15_Mcs12, bMaskByte0, PowerIndex);
			break;
		case MGN_MCS13:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs15_Mcs12, bMaskByte1, PowerIndex);
			break;
		case MGN_MCS14:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs15_Mcs12, bMaskByte2, PowerIndex);
			break;
		case MGN_MCS15:
			phy_set_bb_reg(Adapter, rTxAGC_A_Mcs15_Mcs12, bMaskByte3, PowerIndex);
			break;

		default:
			RTW_INFO("Invalid Rate!!\n");
			break;
		}
	} else if (RFPath == RF_PATH_B) {
		switch (Rate) {
		case MGN_1M:
			phy_set_bb_reg(Adapter, rTxAGC_B_CCK1_55_Mcs32, bMaskByte1, PowerIndex);
			break;
		case MGN_2M:
			phy_set_bb_reg(Adapter, rTxAGC_B_CCK1_55_Mcs32, bMaskByte2, PowerIndex);
			break;
		case MGN_5_5M:
			phy_set_bb_reg(Adapter, rTxAGC_B_CCK1_55_Mcs32, bMaskByte3, PowerIndex);
			break;
		case MGN_11M:
			phy_set_bb_reg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, PowerIndex);
			break;

		case MGN_6M:
			phy_set_bb_reg(Adapter, rTxAGC_B_Rate18_06, bMaskByte0, PowerIndex);
			break;
		case MGN_9M:
			phy_set_bb_reg(Adapter, rTxAGC_B_Rate18_06, bMaskByte1, PowerIndex);
			break;
		case MGN_12M:
			phy_set_bb_reg(Adapter, rTxAGC_B_Rate18_06, bMaskByte2, PowerIndex);
			break;
		case MGN_18M:
			phy_set_bb_reg(Adapter, rTxAGC_B_Rate18_06, bMaskByte3, PowerIndex);
			break;

		case MGN_24M:
			phy_set_bb_reg(Adapter, rTxAGC_B_Rate54_24, bMaskByte0, PowerIndex);
			break;
		case MGN_36M:
			phy_set_bb_reg(Adapter, rTxAGC_B_Rate54_24, bMaskByte1, PowerIndex);
			break;
		case MGN_48M:
			phy_set_bb_reg(Adapter, rTxAGC_B_Rate54_24, bMaskByte2, PowerIndex);
			break;
		case MGN_54M:
			phy_set_bb_reg(Adapter, rTxAGC_B_Rate54_24, bMaskByte3, PowerIndex);
			break;

		case MGN_MCS0:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs03_Mcs00, bMaskByte0, PowerIndex);
			break;
		case MGN_MCS1:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs03_Mcs00, bMaskByte1, PowerIndex);
			break;
		case MGN_MCS2:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs03_Mcs00, bMaskByte2, PowerIndex);
			break;
		case MGN_MCS3:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs03_Mcs00, bMaskByte3, PowerIndex);
			break;

		case MGN_MCS4:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs07_Mcs04, bMaskByte0, PowerIndex);
			break;
		case MGN_MCS5:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs07_Mcs04, bMaskByte1, PowerIndex);
			break;
		case MGN_MCS6:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs07_Mcs04, bMaskByte2, PowerIndex);
			break;
		case MGN_MCS7:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs07_Mcs04, bMaskByte3, PowerIndex);
			break;

		case MGN_MCS8:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs11_Mcs08, bMaskByte0, PowerIndex);
			break;
		case MGN_MCS9:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs11_Mcs08, bMaskByte1, PowerIndex);
			break;
		case MGN_MCS10:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs11_Mcs08, bMaskByte2, PowerIndex);
			break;
		case MGN_MCS11:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs11_Mcs08, bMaskByte3, PowerIndex);
			break;

		case MGN_MCS12:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs15_Mcs12, bMaskByte0, PowerIndex);
			break;
		case MGN_MCS13:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs15_Mcs12, bMaskByte1, PowerIndex);
			break;
		case MGN_MCS14:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs15_Mcs12, bMaskByte2, PowerIndex);
			break;
		case MGN_MCS15:
			phy_set_bb_reg(Adapter, rTxAGC_B_Mcs15_Mcs12, bMaskByte3, PowerIndex);
			break;

		default:
			RTW_INFO("Invalid Rate!!\n");
			break;
		}
	} else
		RTW_INFO("Invalid RFPath!!\n");
}

s8 tx_power_extra_bias(
		enum rf_path		RFPath,
		u8				Rate,
		enum channel_width	BandWidth,
		u8				Channel
)
{
	s8 bias = 0;

	if (Rate == MGN_2M)
		bias = -9;

	return bias;
}

u8
PHY_GetTxPowerIndex_8188E(
		PADAPTER		pAdapter,
		enum rf_path		RFPath,
		u8				Rate,
		u8				BandWidth,
		u8				Channel,
	struct txpwr_idx_comp *tic
)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(pAdapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(pAdapter);
	s16 power_idx;
	u8 pg = 0;
	s8 by_rate_diff = 0, limit = 0, tpt_offset = 0, extra_bias = 0;
	BOOLEAN bIn24G = _FALSE;

	pg = phy_get_pg_txpwr_idx(pAdapter, RFPath, Rate, RF_1TX, BandWidth, Channel, &bIn24G);

	by_rate_diff = PHY_GetTxPowerByRate(pAdapter, BAND_ON_2_4G, RFPath, Rate);
	limit = PHY_GetTxPowerLimit(pAdapter, NULL, (u8)(!bIn24G), pHalData->current_channel_bw, RFPath, Rate, RF_1TX, pHalData->current_channel);

	tpt_offset = PHY_GetTxPowerTrackingOffset(pAdapter, RFPath, Rate);

	if (pAdapter->registrypriv.mp_mode != 1)
		extra_bias = tx_power_extra_bias(RFPath, Rate, BandWidth, Channel);

	if (tic)
		txpwr_idx_comp_set(tic, RF_1TX, pg, by_rate_diff, limit, tpt_offset, extra_bias, 0, 0);

	by_rate_diff = by_rate_diff > limit ? limit : by_rate_diff;
	power_idx = pg + by_rate_diff + tpt_offset + extra_bias;

	if (power_idx < 0)
		power_idx = 0;
	else if (power_idx > hal_spec->txgi_max)
		power_idx = hal_spec->txgi_max;

	return power_idx;
}

void
PHY_ScanOperationBackup8188E(
		PADAPTER	Adapter,
		u8		Operation
)
{
#if 0
	IO_TYPE	IoType;

	if (!rtw_is_drv_stopped(padapter)) {
		switch (Operation) {
		case SCAN_OPT_BACKUP:
			IoType = IO_CMD_PAUSE_DM_BY_SCAN;
			rtw_hal_set_hwreg(Adapter, HW_VAR_IO_CMD, (u8 *)&IoType);

			break;

		case SCAN_OPT_RESTORE:
			IoType = IO_CMD_RESUME_DM_BY_SCAN;
			rtw_hal_set_hwreg(Adapter, HW_VAR_IO_CMD, (u8 *)&IoType);
			break;

		default:
			break;
		}
	}
#endif
}
void
phy_SpurCalibration_8188E(
		PADAPTER			Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_struct		*p_dm_odm = &(pHalData->odmpriv);

	/* DbgPrint("===> phy_SpurCalibration_8188E  current_channel_bw = %d, current_channel = %d\n", pHalData->current_channel_bw, pHalData->current_channel);*/
	if (pHalData->current_channel_bw == CHANNEL_WIDTH_20 && (pHalData->current_channel == 13 || pHalData->current_channel == 14)) {
		phy_set_bb_reg(Adapter, rOFDM0_RxDSP, BIT(9), 0x1);/* enable notch filter */
		phy_set_bb_reg(Adapter, rOFDM1_IntfDet, BIT(8) | BIT(7) | BIT(6), 0x2);	/* intf_TH */
		phy_set_bb_reg(Adapter, rOFDM0_RxDSP, BIT(28) | BIT(27) | BIT(26) | BIT(25) | BIT(24), 0x1f);
		p_dm_odm->is_rx_blocking_en = false;
	} else if (pHalData->current_channel_bw == CHANNEL_WIDTH_40 && pHalData->current_channel == 11) {
		phy_set_bb_reg(Adapter, rOFDM0_RxDSP, BIT(9), 0x1);/* enable notch filter */
		phy_set_bb_reg(Adapter, rOFDM1_IntfDet, BIT(8) | BIT(7) | BIT(6), 0x2);	/* intf_TH */
		phy_set_bb_reg(Adapter, rOFDM0_RxDSP, BIT(28) | BIT(27) | BIT(26) | BIT(25) | BIT(24), 0x1f);
		p_dm_odm->is_rx_blocking_en = false;
	} else {
		if (Adapter->registrypriv.notch_filter == 0)
			phy_set_bb_reg(Adapter, rOFDM0_RxDSP, BIT(9), 0x0);/* disable notch filter */
	}
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_SetBWModeCallback8192C()
 *
 * Overview:    Timer callback function for SetSetBWMode
 *
 * Input:	PRT_TIMER		pTimer
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
_PHY_SetBWMode88E(
		PADAPTER	Adapter
)
{
	/*	PADAPTER			Adapter = (PADAPTER)pTimer->Adapter; */
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8				regBwOpMode;
	u8				regRRSR_RSC;

	/* return; */

	/* Added it for 20/40 mhz switch time evaluation by guangan 070531 */
	/* u32				NowL, NowH; */
	/* u64				BeginTime, EndTime; */

	if (pHalData->rf_chip == RF_PSEUDO_11N) {
		/* pHalData->SetBWModeInProgress= _FALSE; */
		return;
	}

	/* There is no 40MHz mode in RF_8225. */
	if (pHalData->rf_chip == RF_8225)
		return;

	if (rtw_is_drv_stopped(Adapter))
		return;

	/* Added it for 20/40 mhz switch time evaluation by guangan 070531 */
	/* NowL = PlatformEFIORead4Byte(Adapter, TSFR); */
	/* NowH = PlatformEFIORead4Byte(Adapter, TSFR+4); */
	/* BeginTime = ((u64)NowH << 32) + NowL; */

	/* 3 */
	/* 3 */ /* <1>Set MAC register */
	/* 3 */
	/* Adapter->hal_func.SetBWModeHandler(); */

	regBwOpMode = rtw_read8(Adapter, REG_BWOPMODE);
	regRRSR_RSC = rtw_read8(Adapter, REG_RRSR + 2);
	/* regBwOpMode = rtw_hal_get_hwreg(Adapter,HW_VAR_BWMODE,(u8 *)&regBwOpMode); */

	switch (pHalData->current_channel_bw) {
	case CHANNEL_WIDTH_20:
		regBwOpMode |= BW_OPMODE_20MHZ;
		/* 2007/02/07 Mark by Emily becasue we have not verify whether this register works */
		rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);
		break;

	case CHANNEL_WIDTH_40:
		regBwOpMode &= ~BW_OPMODE_20MHZ;
		/* 2007/02/07 Mark by Emily becasue we have not verify whether this register works */
		rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);

		regRRSR_RSC = (regRRSR_RSC & 0x90) | (pHalData->nCur40MhzPrimeSC << 5);
		rtw_write8(Adapter, REG_RRSR + 2, regRRSR_RSC);
		break;

	default:
		break;
	}

	/* 3 */
	/* 3 */ /* <2>Set PHY related register */
	/* 3 */
	switch (pHalData->current_channel_bw) {
	/* 20 MHz channel*/
	case CHANNEL_WIDTH_20:
		phy_set_bb_reg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x0);
		phy_set_bb_reg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x0);
		/* phy_set_bb_reg(Adapter, rFPGA0_AnalogParameter2, BIT10, 1); */

		break;


	/* 40 MHz channel*/
	case CHANNEL_WIDTH_40:
		phy_set_bb_reg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x1);
		phy_set_bb_reg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x1);

		/* Set Control channel to upper or lower. These settings are required only for 40MHz */
		phy_set_bb_reg(Adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC >> 1));
		phy_set_bb_reg(Adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);
		/* phy_set_bb_reg(Adapter, rFPGA0_AnalogParameter2, BIT10, 0); */

		phy_set_bb_reg(Adapter, 0x818, (BIT26 | BIT27), (pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);

		break;



	default:
		break;

	}
	/* Skip over setting of J-mode in BB register here. Default value is "None J mode". Emily 20070315 */

	/* Added it for 20/40 mhz switch time evaluation by guangan 070531 */
	/* NowL = PlatformEFIORead4Byte(Adapter, TSFR); */
	/* NowH = PlatformEFIORead4Byte(Adapter, TSFR+4); */
	/* EndTime = ((u64)NowH << 32) + NowL; */

	/* 3<3>Set RF related register */
	switch (pHalData->rf_chip) {
	case RF_8225:
		/* PHY_SetRF8225Bandwidth(Adapter, pHalData->current_channel_bw); */
		break;

	case RF_8256:
		/* Please implement this function in Hal8190PciPhy8256.c */
		/* PHY_SetRF8256Bandwidth(Adapter, pHalData->current_channel_bw); */
		break;

	case RF_8258:
		/* Please implement this function in Hal8190PciPhy8258.c */
		/* PHY_SetRF8258Bandwidth(); */
		break;

	case RF_PSEUDO_11N:
		/* Do Nothing */
		break;

	case RF_6052:
		rtl8188e_PHY_RF6052SetBandwidth(Adapter, pHalData->current_channel_bw);
		break;

	default:
		/* RT_ASSERT(FALSE, ("Unknown RFChipID: %d\n", pHalData->RFChipID)); */
		break;
	}

	/* pHalData->SetBWModeInProgress= FALSE; */

}


#if 0
	/* -----------------------------------------------------------------------------
	* * Function:   SetBWMode8190Pci()
	* *
	* * Overview:  This function is export to "HalCommon" moudule
	* *
	* * Input:	PADAPTER			Adapter
	* *			CHANNEL_WIDTH	Bandwidth	 20M or 40M
	* *
	* * Output:      NONE
	* *
	* * Return:      NONE
	* *
	* * Note:		We do not take j mode into consideration now
	* *--------------------------------------------------------------------------- */
#endif
void
PHY_SetBWMode8188E(
		PADAPTER					Adapter,
		enum channel_width	Bandwidth,	/* 20M or 40M */
		unsigned char	Offset		/* Upper, Lower, or Don't care */
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	enum channel_width	tmpBW = pHalData->current_channel_bw;
	/* Modified it for 20/40 mhz switch by guangan 070531 */
	/* PMGNT_INFO	pMgntInfo=&Adapter->MgntInfo; */

	/* return; */

	/* if(pHalData->SwChnlInProgress)
	*	if(pMgntInfo->bScanInProgress)
	*	{
	*		return;
	*	} */

	/*	if(pHalData->SetBWModeInProgress)
	 *	{
	 * 	 */ /* Modified it for 20/40 mhz switch by guangan 070531
 *		PlatformCancelTimer(Adapter, &pHalData->SetBWModeTimer);
 * 	 */ /* return;
 *	} */

	/* if(pHalData->SetBWModeInProgress) */
	/*	return; */

	/* pHalData->SetBWModeInProgress= TRUE; */

	pHalData->current_channel_bw = Bandwidth;

#if 0
	if (Offset == EXTCHNL_OFFSET_LOWER)
		pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if (Offset == EXTCHNL_OFFSET_UPPER)
		pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_LOWER;
	else
		pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
#else
	pHalData->nCur40MhzPrimeSC = Offset;
#endif

	if (!RTW_CANNOT_RUN(Adapter)) {
#if 0
		/* PlatformSetTimer(Adapter, &(pHalData->SetBWModeTimer), 0); */
#else
		_PHY_SetBWMode88E(Adapter);
#endif
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
		if (IS_VENDOR_8188E_I_CUT_SERIES(Adapter))
			phy_SpurCalibration_8188E(Adapter);
#endif
	} else {
		/* pHalData->SetBWModeInProgress= FALSE; */
		pHalData->current_channel_bw = tmpBW;
	}

}


static void _PHY_SwChnl8188E(PADAPTER Adapter, u8 channel)
{
	enum rf_path eRFPath;
	u32 param1, param2;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if (Adapter->bNotifyChannelChange)
		RTW_INFO("[%s] ch = %d\n", __FUNCTION__, channel);

	/* s1. pre common command - CmdID_SetTxPowerLevel */
	rtw_hal_set_tx_power_level(Adapter, channel);

	/* s2. RF dependent command - CmdID_RF_WriteReg, param1=RF_CHNLBW, param2=channel */
	param1 = RF_CHNLBW;
	param2 = channel;
	for (eRFPath = RF_PATH_A; eRFPath < pHalData->NumTotalRFPath; eRFPath++) {
		pHalData->RfRegChnlVal[eRFPath] = ((pHalData->RfRegChnlVal[eRFPath] & 0xfffffc00) | param2);
		phy_set_rf_reg(Adapter, eRFPath, param1, bRFRegOffsetMask, pHalData->RfRegChnlVal[eRFPath]);
	}


	/* s3. post common command - CmdID_End, None */

}
void
PHY_SwChnl8188E(/* Call after initialization */
		PADAPTER	Adapter,
		u8		channel
)
{
	/* PADAPTER Adapter =  ADJUST_TO_ADAPTIVE_ADAPTER(pAdapter, _TRUE); */
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	tmpchannel = pHalData->current_channel;
	BOOLEAN  bResult = _TRUE;

	if (pHalData->rf_chip == RF_PSEUDO_11N) {
		/* pHalData->SwChnlInProgress=FALSE; */
		return; 								/* return immediately if it is peudo-phy */
	}

	/* if(pHalData->SwChnlInProgress) */
	/*	return; */

	/* if(pHalData->SetBWModeInProgress) */
	/*	return; */

	while (pHalData->odmpriv.rf_calibrate_info.is_lck_in_progress)
		rtw_msleep_os(50);

	/* -------------------------------------------- */
	switch (pHalData->CurrentWirelessMode) {
	case WIRELESS_MODE_A:
	case WIRELESS_MODE_N_5G:
		/* RT_ASSERT((channel>14), ("WIRELESS_MODE_A but channel<=14")); */
		break;

	case WIRELESS_MODE_B:
		/* RT_ASSERT((channel<=14), ("WIRELESS_MODE_B but channel>14")); */
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		/* RT_ASSERT((channel<=14), ("WIRELESS_MODE_G but channel>14")); */
		break;

	default:
		/* RT_ASSERT(FALSE, ("Invalid WirelessMode(%#x)!!\n", pHalData->CurrentWirelessMode)); */
		break;
	}
	/* -------------------------------------------- */

	/* pHalData->SwChnlInProgress = TRUE; */
	if (channel == 0)
		channel = 1;

	pHalData->current_channel = channel;

	/* pHalData->SwChnlStage=0; */
	/* pHalData->SwChnlStep=0; */

	if (!RTW_CANNOT_RUN(Adapter)) {
#if 0
		/* PlatformSetTimer(Adapter, &(pHalData->SwChnlTimer), 0); */
#else
		_PHY_SwChnl8188E(Adapter, channel);
#endif

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
		if (IS_VENDOR_8188E_I_CUT_SERIES(Adapter))
			phy_SpurCalibration_8188E(Adapter);
#endif



		if (!bResult) {
			/* if(IS_HARDWARE_TYPE_8192SU(Adapter)) */
			/* { */
			/*	pHalData->SwChnlInProgress = FALSE; */
			pHalData->current_channel = tmpchannel;
			/* } */
		}

	} else {
		/* if(IS_HARDWARE_TYPE_8192SU(Adapter)) */
		/* { */
		/*	pHalData->SwChnlInProgress = FALSE; */
		pHalData->current_channel = tmpchannel;
		/* } */
	}
}

void
PHY_SetSwChnlBWMode8188E(
		PADAPTER			Adapter,
		u8					channel,
		enum channel_width	Bandwidth,
		u8					Offset40,
		u8					Offset80
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	/* RTW_INFO("%s()===>\n",__FUNCTION__); */

	PHY_SwChnl8188E(Adapter, channel);
	PHY_SetBWMode8188E(Adapter, Bandwidth, Offset40);
	if (pHalData->bNeedIQK == _TRUE) {
		if (pHalData->neediqk_24g == _TRUE) {

			halrf_iqk_trigger(&pHalData->odmpriv, _FALSE);
			pHalData->bIQKInitialized = _TRUE;
			pHalData->neediqk_24g = _FALSE;
		}
		pHalData->bNeedIQK = _FALSE;
	}
	/* RTW_INFO("<==%s()\n",__FUNCTION__); */
}


void
PHY_SetRFEReg_8188E(
	PADAPTER		Adapter
)
{
	u8			u1tmp = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if ((pHalData->ExternalPA_2G == 0) && (pHalData->ExternalLNA_2G == 0))
		return;

	switch (pHalData->rfe_type) {
	/* 88EU rfe_type should always be 0 */
	case 0:
	default:
			phy_set_bb_reg(Adapter, 0x40, BIT2|BIT3, 0x3); /*0x3 << 2*/
			phy_set_bb_reg(Adapter, 0xEE8, BIT28, 0x1);
		phy_set_bb_reg(Adapter, 0x87C, BIT0, 0x0);
		break;
	}
}
