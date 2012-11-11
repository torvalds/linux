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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8188E_PHYCFG_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>

#ifdef CONFIG_IOL
#include <rtw_iol.h>
#endif

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
// Please refer to header file
/*--------------------Define export function prototype-----------------------*/

/*----------------------------Function Body----------------------------------*/
//
// 1. BB register R/W API
//

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

	for(i=0; i<=31; i++)
	{
		if ( ((BitMask>>i) &  0x1 ) == 1)
			break;
	}

	return (i);
}

#if(SIC_ENABLE == 1)
static BOOLEAN
sic_IsSICReady(
	IN	PADAPTER	Adapter
	)
{
	BOOLEAN		bRet=_FALSE;
	u32		retryCnt=0;
	u8		sic_cmd=0xff;

	while(1)
	{		
		if(retryCnt++ >= SIC_MAX_POLL_CNT)
		{
			//RTPRINT(FPHY, (PHY_SICR|PHY_SICW), ("[SIC], sic_IsSICReady() return FALSE\n"));
			return _FALSE;
		}

		//if(RT_SDIO_CANNOT_IO(Adapter))
		//	return _FALSE;

		sic_cmd = rtw_read8(Adapter, SIC_CMD_REG);
		//sic_cmd = PlatformEFIORead1Byte(Adapter, SIC_CMD_REG);
#if(SIC_HW_SUPPORT == 1)
		sic_cmd &= 0xf0;	// [7:4]
#endif
		//RTPRINT(FPHY, (PHY_SICR|PHY_SICW), ("[SIC], sic_IsSICReady(), readback 0x%x=0x%x\n", SIC_CMD_REG, sic_cmd));
		if(sic_cmd == SIC_CMD_READY)
			return _TRUE;
		else
		{
			rtw_msleep_os(1);
			//delay_ms(1);
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

	return (i);
}
*/

static u32
sic_Read4Byte(
	PVOID		Adapter,
	u32		offset
	)
{
	u32	u4ret=0xffffffff;
#if RTL8188E_SUPPORT == 1
	u8	retry = 0;
#endif

	//RTPRINT(FPHY, PHY_SICR, ("[SIC], sic_Read4Byte(): read offset(%#x)\n", offset));
	
	if(sic_IsSICReady(Adapter))
	{
#if(SIC_HW_SUPPORT == 1)
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_PREREAD);
		//PlatformEFIOWrite1Byte(Adapter, SIC_CMD_REG, SIC_CMD_PREREAD);
		//RTPRINT(FPHY, PHY_SICR, ("write cmdreg 0x%x = 0x%x\n", SIC_CMD_REG, SIC_CMD_PREREAD));
#endif
		rtw_write8(Adapter, SIC_ADDR_REG, (u8)(offset&0xff));
		//PlatformEFIOWrite1Byte(Adapter, SIC_ADDR_REG, (u1Byte)(offset&0xff));
		//RTPRINT(FPHY, PHY_SICR, ("write 0x%x = 0x%x\n", SIC_ADDR_REG, (u1Byte)(offset&0xff)));
		rtw_write8(Adapter, SIC_ADDR_REG+1, (u8)((offset&0xff00)>>8));
		//PlatformEFIOWrite1Byte(Adapter, SIC_ADDR_REG+1, (u1Byte)((offset&0xff00)>>8));
		//RTPRINT(FPHY, PHY_SICR, ("write 0x%x = 0x%x\n", SIC_ADDR_REG+1, (u1Byte)((offset&0xff00)>>8)));
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_READ);
		//PlatformEFIOWrite1Byte(Adapter, SIC_CMD_REG, SIC_CMD_READ);
		//RTPRINT(FPHY, PHY_SICR, ("write cmdreg 0x%x = 0x%x\n", SIC_CMD_REG, SIC_CMD_READ));

#if RTL8188E_SUPPORT == 1
		retry = 4;
		while(retry--){			
			rtw_udelay_os(50);
			//PlatformStallExecution(50);
		}
#else
		rtw_udelay_os(200);
		//PlatformStallExecution(200);
#endif

		if(sic_IsSICReady(Adapter))
		{
			u4ret = rtw_read32(Adapter, SIC_DATA_REG);
			//u4ret = PlatformEFIORead4Byte(Adapter, SIC_DATA_REG);
			//RTPRINT(FPHY, PHY_SICR, ("read 0x%x = 0x%x\n", SIC_DATA_REG, u4ret));
			//DbgPrint("<===Read 0x%x = 0x%x\n", offset, u4ret);
		}
	}
	
	return u4ret;
}

static VOID
sic_Write4Byte(
	PVOID		Adapter,
	u32		offset,
	u32		data
	)
{
#if RTL8188E_SUPPORT == 1
	u8	retry = 6;
#endif
	//DbgPrint("=>Write 0x%x = 0x%x\n", offset, data);
	//RTPRINT(FPHY, PHY_SICW, ("[SIC], sic_Write4Byte(): write offset(%#x)=0x%x\n", offset, data));
	if(sic_IsSICReady(Adapter))
	{
#if(SIC_HW_SUPPORT == 1)
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_PREWRITE);
		//PlatformEFIOWrite1Byte(Adapter, SIC_CMD_REG, SIC_CMD_PREWRITE);
		//RTPRINT(FPHY, PHY_SICW, ("write data 0x%x = 0x%x\n", SIC_CMD_REG, SIC_CMD_PREWRITE));
#endif
		rtw_write8(Adapter, SIC_ADDR_REG, (u8)(offset&0xff));
		//PlatformEFIOWrite1Byte(Adapter, SIC_ADDR_REG, (u1Byte)(offset&0xff));
		//RTPRINT(FPHY, PHY_SICW, ("write 0x%x=0x%x\n", SIC_ADDR_REG, (u1Byte)(offset&0xff)));
		rtw_write8(Adapter, SIC_ADDR_REG+1, (u8)((offset&0xff00)>>8));
		//PlatformEFIOWrite1Byte(Adapter, SIC_ADDR_REG+1, (u1Byte)((offset&0xff00)>>8));
		//RTPRINT(FPHY, PHY_SICW, ("write 0x%x=0x%x\n", (SIC_ADDR_REG+1), (u1Byte)((offset&0xff00)>>8)));
		rtw_write32(Adapter, SIC_DATA_REG, (u32)data);
		//PlatformEFIOWrite4Byte(Adapter, SIC_DATA_REG, (u4Byte)data);
		//RTPRINT(FPHY, PHY_SICW, ("write data 0x%x = 0x%x\n", SIC_DATA_REG, data));
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_WRITE);
		//PlatformEFIOWrite1Byte(Adapter, SIC_CMD_REG, SIC_CMD_WRITE);
		//RTPRINT(FPHY, PHY_SICW, ("write data 0x%x = 0x%x\n", SIC_CMD_REG, SIC_CMD_WRITE));
#if RTL8188E_SUPPORT == 1
		while(retry--){
			rtw_udelay_os(50);
			//PlatformStallExecution(50);
		}
#else
		rtw_udelay_os(150);
		//PlatformStallExecution(150);
#endif

	}
}
//============================================================
// extern function
//============================================================
static VOID
SIC_SetBBReg(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask,
	IN	u32		Data
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			OriginalValue, BitShift;
	u16			BBWaitCounter = 0;

	//RTPRINT(FPHY, PHY_SICW, ("[SIC], SIC_SetBBReg() start\n"));
/*
	while(PlatformAtomicExchange(&pHalData->bChangeBBInProgress, _TRUE) == _TRUE)
	{
		BBWaitCounter ++;
		delay_ms(10); // 1 ms

		if((BBWaitCounter > 100) || RT_CANNOT_IO(Adapter))
		{// Wait too long, return FALSE to avoid to be stuck here.
			RTPRINT(FPHY, PHY_SICW, ("[SIC], SIC_SetBBReg(), Fail to set BB offset(%#x)!!, WaitCnt(%d)\n", RegAddr, BBWaitCounter));
			return;
		}		
	}
*/
	//
	// Critical section start
	// 
	
	//RTPRINT(FPHY, PHY_SICW, ("[SIC], SIC_SetBBReg(), mask=0x%x, addr[0x%x]=0x%x\n", BitMask, RegAddr, Data));

	if(BitMask!= bMaskDWord){//if not "double word" write
		OriginalValue = sic_Read4Byte(Adapter, RegAddr);
		//BitShift = sic_CalculateBitShift(BitMask);
		BitShift = phy_CalculateBitShift(BitMask);		
		Data = (((OriginalValue) & (~BitMask)) | (Data << BitShift));
	}

	sic_Write4Byte(Adapter, RegAddr, Data);

	//PlatformAtomicExchange(&pHalData->bChangeBBInProgress, _FALSE);
	//RTPRINT(FPHY, PHY_SICW, ("[SIC], SIC_SetBBReg() end\n"));
}

static u32
SIC_QueryBBReg(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			ReturnValue = 0, OriginalValue, BitShift;
	u16			BBWaitCounter = 0;

	//RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_QueryBBReg() start\n"));

/*
	while(PlatformAtomicExchange(&pHalData->bChangeBBInProgress, _TRUE) == _TRUE)
	{
		BBWaitCounter ++;
		delay_ms(10); // 10 ms

		if((BBWaitCounter > 100) || RT_CANNOT_IO(Adapter))
		{// Wait too long, return FALSE to avoid to be stuck here.
			RTPRINT(FPHY, PHY_SICW, ("[SIC], SIC_QueryBBReg(), Fail to query BB offset(%#x)!!, WaitCnt(%d)\n", RegAddr, BBWaitCounter));
			return ReturnValue;
		}		
	}
*/
	OriginalValue = sic_Read4Byte(Adapter, RegAddr);
	//BitShift = sic_CalculateBitShift(BitMask);
	BitShift = phy_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;

	//RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_QueryBBReg(), 0x%x=0x%x\n", RegAddr, OriginalValue));
	//RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_QueryBBReg() end\n"));

	//PlatformAtomicExchange(&pHalData->bChangeBBInProgress, _FALSE);	
	return (ReturnValue);
}

VOID
SIC_Init(
	IN	PADAPTER	Adapter
	)
{
	// Here we need to write 0x1b8~0x1bf = 0 after fw is downloaded
	// because for 8723E at beginning 0x1b8=0x1e, that will cause
	// sic always not be ready
#if(SIC_HW_SUPPORT == 1)
	//RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_Init(), write 0x%x = 0x%x\n", 
	//	SIC_INIT_REG, SIC_INIT_VAL));
	rtw_write8(Adapter, SIC_INIT_REG, SIC_INIT_VAL);
	//PlatformEFIOWrite1Byte(Adapter, SIC_INIT_REG, SIC_INIT_VAL);
	//RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_Init(), write 0x%x = 0x%x\n", 
	//	SIC_CMD_REG, SIC_CMD_INIT));
	rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_INIT);
	//PlatformEFIOWrite1Byte(Adapter, SIC_CMD_REG, SIC_CMD_INIT);
#else
	//RTPRINT(FPHY, PHY_SICR, ("[SIC], SIC_Init(), write 0x1b8~0x1bf = 0x0\n"));
	rtw_write32(Adapter, SIC_CMD_REG, 0);
	//PlatformEFIOWrite4Byte(Adapter, SIC_CMD_REG, 0);
	rtw_write32(Adapter, SIC_CMD_REG+4, 0);
	//PlatformEFIOWrite4Byte(Adapter, SIC_CMD_REG+4, 0);
#endif
}

static BOOLEAN
SIC_LedOff(
	IN	PADAPTER	Adapter
	)
{
	// When SIC is enabled, led pin will be used as debug pin,
	// so don't execute led function when SIC is enabled.
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
*			u4Byte			RegAddr,		//The target address to be readback
*			u4Byte			BitMask		//The target bit position in the target address
*										//to be readback
* Output:	None
* Return:		u4Byte			Data			//The readback register value
* Note:		This function is equal to "GetRegSetting" in PHY programming guide
*/
u32
rtl8188e_PHY_QueryBBReg(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask
	)
{
  	u32	ReturnValue = 0, OriginalValue, BitShift;
	u16	BBWaitCounter = 0;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

#if(SIC_ENABLE == 1)
	return SIC_QueryBBReg(Adapter, RegAddr, BitMask);
#endif

	//RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_QueryBBReg(): RegAddr(%#lx), BitMask(%#lx)\n", RegAddr, BitMask));

	OriginalValue = rtw_read32(Adapter, RegAddr);
	BitShift = phy_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;

	//RTPRINT(FPHY, PHY_BBR, ("BBR MASK=0x%lx Addr[0x%lx]=0x%lx\n", BitMask, RegAddr, OriginalValue));
	//RT_TRACE(COMP_RF, DBG_TRACE, ("<---PHY_QueryBBReg(): RegAddr(%#lx), BitMask(%#lx), OriginalValue(%#lx)\n", RegAddr, BitMask, OriginalValue));

	return (ReturnValue);

}


/**
* Function:	PHY_SetBBReg
*
* OverView:	Write "Specific bits" to BB register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			u4Byte			RegAddr,		//The target address to be modified
*			u4Byte			BitMask		//The target bit position in the target address
*										//to be modified
*			u4Byte			Data			//The new register value in the target bit position
*										//of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRegSetting" in PHY programming guide
*/

VOID
rtl8188e_PHY_SetBBReg(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask,
	IN	u32		Data
	)
{
	HAL_DATA_TYPE	*pHalData		= GET_HAL_DATA(Adapter);
	//u16			BBWaitCounter	= 0;
	u32			OriginalValue, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

#if(SIC_ENABLE == 1)
	SIC_SetBBReg(Adapter, RegAddr, BitMask, Data);
	return; 
#endif

	//RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_SetBBReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx)\n", RegAddr, BitMask, Data));

	if(BitMask!= bMaskDWord){//if not "double word" write
		OriginalValue = rtw_read32(Adapter, RegAddr);
		BitShift = phy_CalculateBitShift(BitMask);
		Data = ((OriginalValue & (~BitMask)) | (Data << BitShift));
	}

	rtw_write32(Adapter, RegAddr, Data);

	//RTPRINT(FPHY, PHY_BBW, ("BBW MASK=0x%lx Addr[0x%lx]=0x%lx\n", BitMask, RegAddr, Data));
	//RT_TRACE(COMP_RF, DBG_TRACE, ("<---PHY_SetBBReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx)\n", RegAddr, BitMask, Data));

}


//
// 2. RF register R/W API
//
/**
* Function:	phy_RFSerialRead
*
* OverView:	Read regster from RF chips
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	//Radio path of A/B/C/D
*			u4Byte			Offset,		//The target address to be read
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
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				Offset
	)
{
	u32						retValue = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32						NewOffset;
	u32 						tmplong,tmplong2;
	u8					RfPiEnable=0;
#if 0
	if(pHalData->RFChipID == RF_8225 && Offset > 0x24) //36 valid regs
		return	retValue;
	if(pHalData->RFChipID == RF_8256 && Offset > 0x2D) //45 valid regs
		return	retValue;
#endif
	//
	// Make sure RF register offset is correct
	//
	Offset &= 0xff;

	//
	// Switch page for 8256 RF IC
	//
	NewOffset = Offset;

	// 2009/06/17 MH We can not execute IO for power save or other accident mode.
	//if(RT_CANNOT_IO(Adapter))
	//{
	//	RTPRINT(FPHY, PHY_RFR, ("phy_RFSerialRead return all one\n"));
	//	return	0xFFFFFFFF;
	//}

	// For 92S LSSI Read RFLSSIRead
	// For RF A/B write 0x824/82c(does not work in the future)
	// We must use 0x824 for RF A and B to execute read trigger
	tmplong = PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord);
	if(eRFPath == RF_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = PHY_QueryBBReg(Adapter, pPhyReg->rfHSSIPara2, bMaskDWord);

	tmplong2 = (tmplong2 & (~bLSSIReadAddress)) | (NewOffset<<23) | bLSSIReadEdge;	//T65 RF

	PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord, tmplong&(~bLSSIReadEdge));
	rtw_udelay_os(10);// PlatformStallExecution(10);

	PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, bMaskDWord, tmplong2);
	rtw_udelay_os(100);//PlatformStallExecution(100);

	//PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord, tmplong|bLSSIReadEdge);
	rtw_udelay_os(10);//PlatformStallExecution(10);

	if(eRFPath == RF_PATH_A)
		RfPiEnable = (u8)PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter1, BIT8);
	else if(eRFPath == RF_PATH_B)
		RfPiEnable = (u8)PHY_QueryBBReg(Adapter, rFPGA0_XB_HSSIParameter1, BIT8);

	if(RfPiEnable)
	{	// Read from BBreg8b8, 12 bits for 8190, 20bits for T65 RF
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBackPi, bLSSIReadBackData);
		//DBG_8192C("Readback from RF-PI : 0x%x\n", retValue);
	}
	else
	{	//Read from BBreg8a0, 12 bits for 8190, 20 bits for T65 RF
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBack, bLSSIReadBackData);
		//DBG_8192C("Readback from RF-SI : 0x%x\n", retValue);
	}
	//DBG_8192C("RFR-%d Addr[0x%x]=0x%x\n", eRFPath, pPhyReg->rfLSSIReadBack, retValue);

	return retValue;

}



/**
* Function:	phy_RFSerialWrite
*
* OverView:	Write data to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	//Radio path of A/B/C/D
*			u4Byte			Offset,		//The target address to be read
*			u4Byte			Data			//The new register Data in the target bit position
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
 * Note: 		  For RF8256 only
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
static	VOID
phy_RFSerialWrite(
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				Offset,
	IN	u32				Data
	)
{
	u32						DataAndAddr = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32						NewOffset;

#if 0
	//<Roger_TODO> We should check valid regs for RF_6052 case.
	if(pHalData->RFChipID == RF_8225 && Offset > 0x24) //36 valid regs
		return;
	if(pHalData->RFChipID == RF_8256 && Offset > 0x2D) //45 valid regs
		return;
#endif

	// 2009/06/17 MH We can not execute IO for power save or other accident mode.
	//if(RT_CANNOT_IO(Adapter))
	//{
	//	RTPRINT(FPHY, PHY_RFW, ("phy_RFSerialWrite stop\n"));
	//	return;
	//}

	Offset &= 0xff;

	//
	// Shadow Update
	//
	//PHY_RFShadowWrite(Adapter, eRFPath, Offset, Data);

	//
	// Switch page for 8256 RF IC
	//
	NewOffset = Offset;

	//
	// Put write addr in [5:0]  and write data in [31:16]
	//
	//DataAndAddr = (Data<<16) | (NewOffset&0x3f);
	DataAndAddr = ((NewOffset<<20) | (Data&0x000fffff)) & 0x0fffffff;	// T65 RF

	//
	// Write Operation
	//
	PHY_SetBBReg(Adapter, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);
	//RTPRINT(FPHY, PHY_RFW, ("RFW-%d Addr[0x%lx]=0x%lx\n", eRFPath, pPhyReg->rf3wireOffset, DataAndAddr));

}


/**
* Function:	PHY_QueryRFReg
*
* OverView:	Query "Specific bits" to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	//Radio path of A/B/C/D
*			u4Byte			RegAddr,		//The target address to be read
*			u4Byte			BitMask		//The target bit position in the target address
*										//to be read
*
* Output:	None
* Return:		u4Byte			Readback value
* Note:		This function is equal to "GetRFRegSetting" in PHY programming guide
*/
u32
rtl8188e_PHY_QueryRFReg(
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask
	)
{
	u32 Original_Value, Readback_Value, BitShift;
	//HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//u8	RFWaitCounter = 0;
	//_irqL	irqL;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	//RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_QueryRFReg(): RegAddr(%#lx), eRFPath(%#x), BitMask(%#lx)\n", RegAddr, eRFPath,BitMask));

#ifdef CONFIG_USB_HCI
	//PlatformAcquireMutex(&pHalData->mxRFOperate);
#else
	//_enter_critical(&pHalData->rf_lock, &irqL);
#endif


	Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);

	BitShift =  phy_CalculateBitShift(BitMask);
	Readback_Value = (Original_Value & BitMask) >> BitShift;

#ifdef CONFIG_USB_HCI
	//PlatformReleaseMutex(&pHalData->mxRFOperate);
#else
	//_exit_critical(&pHalData->rf_lock, &irqL);
#endif


	//RTPRINT(FPHY, PHY_RFR, ("RFR-%d MASK=0x%lx Addr[0x%lx]=0x%lx\n", eRFPath, BitMask, RegAddr, Original_Value));//BitMask(%#lx),BitMask,
	//RT_TRACE(COMP_RF, DBG_TRACE, ("<---PHY_QueryRFReg(): RegAddr(%#lx), eRFPath(%#x),  Original_Value(%#lx)\n",
	//				RegAddr, eRFPath, Original_Value));

	return (Readback_Value);
}

/**
* Function:	PHY_SetRFReg
*
* OverView:	Write "Specific bits" to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	//Radio path of A/B/C/D
*			u4Byte			RegAddr,		//The target address to be modified
*			u4Byte			BitMask		//The target bit position in the target address
*										//to be modified
*			u4Byte			Data			//The new register Data in the target bit position
*										//of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRFRegSetting" in PHY programming guide
*/
VOID
rtl8188e_PHY_SetRFReg(
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask,
	IN	u32				Data
	)
{

	//HAL_DATA_TYPE	*pHalData		= GET_HAL_DATA(Adapter);
	//u1Byte			RFWaitCounter	= 0;
	u32		Original_Value, BitShift;
	//_irqL	irqL;

#if (DISABLE_BB_RF == 1)
	return;
#endif

	//RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_SetRFReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx), eRFPath(%#x)\n",
	//	RegAddr, BitMask, Data, eRFPath));
	//RTPRINT(FINIT, INIT_RF, ("PHY_SetRFReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx), eRFPath(%#x)\n",
	//	RegAddr, BitMask, Data, eRFPath));


#ifdef CONFIG_USB_HCI
	//PlatformAcquireMutex(&pHalData->mxRFOperate);
#else
	//_enter_critical(&pHalData->rf_lock, &irqL);
#endif


	// RF data is 12 bits only
	if (BitMask != bRFRegOffsetMask)
	{
		Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);
		BitShift =  phy_CalculateBitShift(BitMask);
		Data = ((Original_Value & (~BitMask)) | (Data<< BitShift));
	}

	phy_RFSerialWrite(Adapter, eRFPath, RegAddr, Data);


#ifdef CONFIG_USB_HCI
	//PlatformReleaseMutex(&pHalData->mxRFOperate);
#else
	//_exit_critical(&pHalData->rf_lock, &irqL);
#endif

	//PHY_QueryRFReg(Adapter,eRFPath,RegAddr,BitMask);
	//RT_TRACE(COMP_RF, DBG_TRACE, ("<---PHY_SetRFReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx), eRFPath(%#x)\n",
	//		RegAddr, BitMask, Data, eRFPath));

}


//
// 3. Initial MAC/BB/RF config by reading MAC/BB/RF txt.
//

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigMACWithParaFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write
 *
 * Input:      	PADAPTER		Adapter
 *			ps1Byte 			pFileName
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note: 		The format of MACPHY_REG.txt is different from PHY and RF.
 *			[Register][Mask][Value]
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigMACWithParaFile(
	IN	PADAPTER		Adapter,
	IN	u8* 			pFileName
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	int		rtStatus = _FAIL;

	return rtStatus;
}

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigMACWithHeaderFile()
 *
 * Overview:    This function read BB parameters from Header file we gen, and do register
 *			  Read/Write
 *
 * Input:      	PADAPTER		Adapter
 *			ps1Byte 			pFileName
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note: 		The format of MACPHY_REG.txt is different from PHY and RF.
 *			[Register][Mask][Value]
 *---------------------------------------------------------------------------*/
#ifndef CONFIG_PHY_SETTING_WITH_ODM
static	int
phy_ConfigMACWithHeaderFile(
	IN	PADAPTER		Adapter
)
{
	u32					i = 0;
	u32					ArrayLength = 0;
	u32*				ptrArray;
	//HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);

	//2008.11.06 Modified by tynli.
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Read Rtl819XMACPHY_Array\n"));
	ArrayLength = Rtl8188E_MAC_ArrayLength;
	ptrArray = (u32*)Rtl8188E_MAC_Array;

#ifdef CONFIG_IOL_MAC
	{
		struct xmit_frame	*xmit_frame;
		if((xmit_frame=rtw_IOL_accquire_xmit_frame(Adapter)) == NULL)
			return _FAIL;

		for(i = 0 ;i < ArrayLength;i=i+2){ // Add by tynli for 2 column
			rtw_IOL_append_WB_cmd(xmit_frame, ptrArray[i], (u8)ptrArray[i+1]);
		}

		return rtw_IOL_exec_cmds_sync(Adapter, xmit_frame, 1000);
	}
#else
	for(i = 0 ;i < ArrayLength;i=i+2){ // Add by tynli for 2 column
		rtw_write8(Adapter, ptrArray[i], (u8)ptrArray[i+1]);
	}
#endif

	return _SUCCESS;

}
#endif //#ifndef CONFIG_PHY_SETTING_WITH_ODM

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
	s8			*pszMACRegFile;
	s8			sz8188EMACRegFile[] = RTL8188E_PHY_MACREG;

	pszMACRegFile = sz8188EMACRegFile;

	//
	// Config MAC
	//
#ifdef CONFIG_EMBEDDED_FWIMG
	#ifdef CONFIG_PHY_SETTING_WITH_ODM
	if(HAL_STATUS_FAILURE == ODM_ConfigMACWithHeaderFile(&pHalData->odmpriv))
		rtStatus = _FAIL;
	#else
	rtStatus = phy_ConfigMACWithHeaderFile(Adapter);
	#endif//#ifdef CONFIG_PHY_SETTING_WITH_ODM
#else

	// Not make sure EEPROM, add later
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Read MACREG.txt\n"));
	rtStatus = phy_ConfigMACWithParaFile(Adapter, pszMACRegFile);	
#endif//CONFIG_EMBEDDED_FWIMG


	// 2010.07.13 AMPDU aggregation number B
	rtw_write16(Adapter, REG_MAX_AGGR_NUM, MAX_AGGR_NUM);
	//rtw_write8(Adapter, REG_MAX_AGGR_NUM, 0x0B); 

	return rtStatus;

}


/**
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
*/
static	VOID
phy_InitBBRFRegisterDefinition(
	IN	PADAPTER		Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	// RF Interface Sowrtware Control
	pHalData->PHYRegDef[RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 LSBs if read 32-bit from 0x870
	pHalData->PHYRegDef[RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 MSBs if read 32-bit from 0x870 (16-bit for 0x872)
	pHalData->PHYRegDef[RF_PATH_C].rfintfs = rFPGA0_XCD_RFInterfaceSW;// 16 LSBs if read 32-bit from 0x874
	pHalData->PHYRegDef[RF_PATH_D].rfintfs = rFPGA0_XCD_RFInterfaceSW;// 16 MSBs if read 32-bit from 0x874 (16-bit for 0x876)

	// RF Interface Readback Value
	pHalData->PHYRegDef[RF_PATH_A].rfintfi = rFPGA0_XAB_RFInterfaceRB; // 16 LSBs if read 32-bit from 0x8E0	
	pHalData->PHYRegDef[RF_PATH_B].rfintfi = rFPGA0_XAB_RFInterfaceRB;// 16 MSBs if read 32-bit from 0x8E0 (16-bit for 0x8E2)
	pHalData->PHYRegDef[RF_PATH_C].rfintfi = rFPGA0_XCD_RFInterfaceRB;// 16 LSBs if read 32-bit from 0x8E4
	pHalData->PHYRegDef[RF_PATH_D].rfintfi = rFPGA0_XCD_RFInterfaceRB;// 16 MSBs if read 32-bit from 0x8E4 (16-bit for 0x8E6)

	// RF Interface Output (and Enable)
	pHalData->PHYRegDef[RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x860
	pHalData->PHYRegDef[RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x864

	// RF Interface (Output and)  Enable
	pHalData->PHYRegDef[RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x860 (16-bit for 0x862)
	pHalData->PHYRegDef[RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x864 (16-bit for 0x866)

	//Addr of LSSI. Wirte RF register by driver
	pHalData->PHYRegDef[RF_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter; //LSSI Parameter
	pHalData->PHYRegDef[RF_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;

	// RF parameter
	pHalData->PHYRegDef[RF_PATH_A].rfLSSI_Select = rFPGA0_XAB_RFParameter;  //BB Band Select
	pHalData->PHYRegDef[RF_PATH_B].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	pHalData->PHYRegDef[RF_PATH_C].rfLSSI_Select = rFPGA0_XCD_RFParameter;
	pHalData->PHYRegDef[RF_PATH_D].rfLSSI_Select = rFPGA0_XCD_RFParameter;

	// Tx AGC Gain Stage (same for all path. Should we remove this?)
	pHalData->PHYRegDef[RF_PATH_A].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	pHalData->PHYRegDef[RF_PATH_B].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	pHalData->PHYRegDef[RF_PATH_C].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage
	pHalData->PHYRegDef[RF_PATH_D].rfTxGainStage = rFPGA0_TxGainStage; //Tx gain stage

	// Tranceiver A~D HSSI Parameter-1
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara1 = rFPGA0_XA_HSSIParameter1;  //wire control parameter1
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara1 = rFPGA0_XB_HSSIParameter1;  //wire control parameter1

	// Tranceiver A~D HSSI Parameter-2
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  //wire control parameter2
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;  //wire control parameter2

	// RF switch Control
	pHalData->PHYRegDef[RF_PATH_A].rfSwitchControl = rFPGA0_XAB_SwitchControl; //TR/Ant switch control
	pHalData->PHYRegDef[RF_PATH_B].rfSwitchControl = rFPGA0_XAB_SwitchControl;
	pHalData->PHYRegDef[RF_PATH_C].rfSwitchControl = rFPGA0_XCD_SwitchControl;
	pHalData->PHYRegDef[RF_PATH_D].rfSwitchControl = rFPGA0_XCD_SwitchControl;

	// AGC control 1
	pHalData->PHYRegDef[RF_PATH_A].rfAGCControl1 = rOFDM0_XAAGCCore1;
	pHalData->PHYRegDef[RF_PATH_B].rfAGCControl1 = rOFDM0_XBAGCCore1;
	pHalData->PHYRegDef[RF_PATH_C].rfAGCControl1 = rOFDM0_XCAGCCore1;
	pHalData->PHYRegDef[RF_PATH_D].rfAGCControl1 = rOFDM0_XDAGCCore1;

	// AGC control 2
	pHalData->PHYRegDef[RF_PATH_A].rfAGCControl2 = rOFDM0_XAAGCCore2;
	pHalData->PHYRegDef[RF_PATH_B].rfAGCControl2 = rOFDM0_XBAGCCore2;
	pHalData->PHYRegDef[RF_PATH_C].rfAGCControl2 = rOFDM0_XCAGCCore2;
	pHalData->PHYRegDef[RF_PATH_D].rfAGCControl2 = rOFDM0_XDAGCCore2;

	// RX AFE control 1
	pHalData->PHYRegDef[RF_PATH_A].rfRxIQImbalance = rOFDM0_XARxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_B].rfRxIQImbalance = rOFDM0_XBRxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_C].rfRxIQImbalance = rOFDM0_XCRxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_D].rfRxIQImbalance = rOFDM0_XDRxIQImbalance;	

	// RX AFE control 1
	pHalData->PHYRegDef[RF_PATH_A].rfRxAFE = rOFDM0_XARxAFE;
	pHalData->PHYRegDef[RF_PATH_B].rfRxAFE = rOFDM0_XBRxAFE;
	pHalData->PHYRegDef[RF_PATH_C].rfRxAFE = rOFDM0_XCRxAFE;
	pHalData->PHYRegDef[RF_PATH_D].rfRxAFE = rOFDM0_XDRxAFE;	

	// Tx AFE control 1
	pHalData->PHYRegDef[RF_PATH_A].rfTxIQImbalance = rOFDM0_XATxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_B].rfTxIQImbalance = rOFDM0_XBTxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_C].rfTxIQImbalance = rOFDM0_XCTxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_D].rfTxIQImbalance = rOFDM0_XDTxIQImbalance;	

	// Tx AFE control 2
	pHalData->PHYRegDef[RF_PATH_A].rfTxAFE = rOFDM0_XATxAFE;
	pHalData->PHYRegDef[RF_PATH_B].rfTxAFE = rOFDM0_XBTxAFE;
	pHalData->PHYRegDef[RF_PATH_C].rfTxAFE = rOFDM0_XCTxAFE;
	pHalData->PHYRegDef[RF_PATH_D].rfTxAFE = rOFDM0_XDTxAFE;	

	// Tranceiver LSSI Readback SI mode
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_C].rfLSSIReadBack = rFPGA0_XC_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_D].rfLSSIReadBack = rFPGA0_XD_LSSIReadBack;	

	// Tranceiver LSSI Readback PI mode
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBackPi = TransceiverA_HSPI_Readback;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBackPi = TransceiverB_HSPI_Readback;
	//pHalData->PHYRegDef[RF_PATH_C].rfLSSIReadBackPi = rFPGA0_XC_LSSIReadBack;
	//pHalData->PHYRegDef[RF_PATH_D].rfLSSIReadBackPi = rFPGA0_XD_LSSIReadBack;	

}


/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithParaFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write
 *
 * Input:      	PADAPTER		Adapter
 *			ps1Byte 			pFileName
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *	2008/11/06	MH	For 92S we do not support silent reset now. Disable
 *					parameter file compare!!!!!!??
 *
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithParaFile(
	IN	PADAPTER		Adapter,
	IN	u8* 			pFileName
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	int		rtStatus = _SUCCESS;

	return rtStatus;
}



//****************************************
// The following is for High Power PA
//****************************************
VOID
phy_ConfigBBExternalPA(
	IN	PADAPTER			Adapter
)
{
#ifdef CONFIG_USB_HCI
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u16 i=0;
	u32 temp=0;

	if(!pHalData->ExternalPA)
	{
		return;
	}

	// 2010/10/19 MH According to Jenyu/EEChou 's opinion, we need not to execute the
	// same code as SU. It is already updated in PHY_REG_1T_HP.txt.
#if 0
	PHY_SetBBReg(Adapter, 0xee8, BIT28, 1);
	temp = PHY_QueryBBReg(Adapter, 0x860, bMaskDWord);
	temp |= (BIT26|BIT21|BIT10|BIT5);
	PHY_SetBBReg(Adapter, 0x860, bMaskDWord, temp);
	PHY_SetBBReg(Adapter, 0x870, BIT10, 0);
	PHY_SetBBReg(Adapter, 0xc80, bMaskDWord, 0x20000080);
	PHY_SetBBReg(Adapter, 0xc88, bMaskDWord, 0x40000100);
#endif

#endif
}

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithHeaderFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write
 *
 * Input:      	PADAPTER		Adapter
 *			u1Byte 			ConfigType     0 => PHY_CONFIG
 *										 1 =>AGC_TAB
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 *---------------------------------------------------------------------------*/
#ifndef CONFIG_PHY_SETTING_WITH_ODM
static	int
phy_ConfigBBWithHeaderFile(
	IN	PADAPTER		Adapter,
	IN	u8 			ConfigType
)
{
	int i;
	u32*	Rtl819XPHY_REGArray_Table;
	u32*	Rtl819XAGCTAB_Array_Table;
	u16	PHY_REGArrayLen, AGCTAB_ArrayLen;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	DM_ODM_T 		*podmpriv = &pHalData->odmpriv;
	int ret = _SUCCESS;


	AGCTAB_ArrayLen = Rtl8188E_AGCTAB_1TArrayLength;
	Rtl819XAGCTAB_Array_Table = (u32*)Rtl8188E_AGCTAB_1TArray;
	PHY_REGArrayLen = Rtl8188E_PHY_REG_1TArrayLength;
	Rtl819XPHY_REGArray_Table = (u32*)Rtl8188E_PHY_REG_1TArray;
//	RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() phy:Rtl8188EAGCTAB_1TArray\n"));
//	RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() agc:Rtl8188EPHY_REG_1TArray\n"));

	if(ConfigType == CONFIG_BB_PHY_REG)
	{
		#ifdef CONFIG_IOL_BB_PHY_REG
		{
			struct xmit_frame	*xmit_frame;
			u32 tmp_value;

			if((xmit_frame=rtw_IOL_accquire_xmit_frame(Adapter)) == NULL) {
				ret = _FAIL;
				goto exit;
			}

			for(i=0;i<PHY_REGArrayLen;i=i+2)
			{
				tmp_value=Rtl819XPHY_REGArray_Table[i+1];
				
				if (Rtl819XPHY_REGArray_Table[i] == 0xfe)
					rtw_IOL_append_DELAY_MS_cmd(xmit_frame, 50);
				else if (Rtl819XPHY_REGArray_Table[i] == 0xfd)
					rtw_IOL_append_DELAY_MS_cmd(xmit_frame, 5);
				else if (Rtl819XPHY_REGArray_Table[i] == 0xfc)
					rtw_IOL_append_DELAY_MS_cmd(xmit_frame, 1);
				else if (Rtl819XPHY_REGArray_Table[i] == 0xfb)
					rtw_IOL_append_DELAY_US_cmd(xmit_frame, 50);
				else if (Rtl819XPHY_REGArray_Table[i] == 0xfa)
					rtw_IOL_append_DELAY_US_cmd(xmit_frame, 5);
				else if (Rtl819XPHY_REGArray_Table[i] == 0xf9)
					rtw_IOL_append_DELAY_US_cmd(xmit_frame, 1);
				else if (Rtl819XPHY_REGArray_Table[i] == 0xa24)
					podmpriv->RFCalibrateInfo.RegA24 = Rtl819XPHY_REGArray_Table[i+1];	

				rtw_IOL_append_WD_cmd(xmit_frame, Rtl819XPHY_REGArray_Table[i], tmp_value);	
				//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XPHY_REGArray_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XPHY_REGArray_Table[i], Rtl819XPHY_REGArray_Table[i+1]));
			}
		
			ret = rtw_IOL_exec_cmds_sync(Adapter, xmit_frame, 1000);
		}
		#else
		for(i=0;i<PHY_REGArrayLen;i=i+2)
		{
			if (Rtl819XPHY_REGArray_Table[i] == 0xfe){
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
				podmpriv->RFCalibrateInfo.RegA24 = Rtl819XPHY_REGArray_Table[i+1];	
				
			PHY_SetBBReg(Adapter, Rtl819XPHY_REGArray_Table[i], bMaskDWord, Rtl819XPHY_REGArray_Table[i+1]);

			// Add 1us delay between BB/RF register setting.
			rtw_udelay_os(1);

			//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XPHY_REGArray_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XPHY_REGArray_Table[i], Rtl819XPHY_REGArray_Table[i+1]));
		}
		#endif
		// for External PA
		phy_ConfigBBExternalPA(Adapter);
	}
	else if(ConfigType == CONFIG_BB_AGC_TAB)
	{
		#ifdef CONFIG_IOL_BB_AGC_TAB
		{
			struct xmit_frame	*xmit_frame;

			if((xmit_frame=rtw_IOL_accquire_xmit_frame(Adapter)) == NULL) {
				ret = _FAIL;
				goto exit;
			}

			for(i=0;i<AGCTAB_ArrayLen;i=i+2)
			{
				rtw_IOL_append_WD_cmd(xmit_frame, Rtl819XAGCTAB_Array_Table[i], Rtl819XAGCTAB_Array_Table[i+1]);							
				//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XAGCTAB_Array_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XAGCTAB_Array_Table[i], Rtl819XAGCTAB_Array_Table[i+1]));
			}
		
			ret = rtw_IOL_exec_cmds_sync(Adapter, xmit_frame, 1000);
		}
		#else
		for(i=0;i<AGCTAB_ArrayLen;i=i+2)
		{
			PHY_SetBBReg(Adapter, Rtl819XAGCTAB_Array_Table[i], bMaskDWord, Rtl819XAGCTAB_Array_Table[i+1]);

			// Add 1us delay between BB/RF register setting.
			rtw_udelay_os(1);

			//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XAGCTAB_Array_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XAGCTAB_Array_Table[i], Rtl819XAGCTAB_Array_Table[i+1]));
		}
		#endif
	}

exit:
	return ret;
}
#endif //#ifndef CONFIG_PHY_SETTING_WITH_ODM

VOID
storePwrIndexDiffRateOffset(
	IN	PADAPTER	Adapter,
	IN	u32		RegAddr,
	IN	u32		BitMask,
	IN	u32		Data
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if(RegAddr == rTxAGC_A_Rate18_06)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][0] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0]));
	}
	if(RegAddr == rTxAGC_A_Rate54_24)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][1] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][1] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][1]));
	}
	if(RegAddr == rTxAGC_A_CCK1_Mcs32)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][6] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][6] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][6]));
	}
	if(RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0xffffff00)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][7] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][7] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][7]));
	}
	if(RegAddr == rTxAGC_A_Mcs03_Mcs00)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][2] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][2] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][2]));
	}
	if(RegAddr == rTxAGC_A_Mcs07_Mcs04)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][3] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][3] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][3]));
	}
	if(RegAddr == rTxAGC_A_Mcs11_Mcs08)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][4] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4]));
	}
	if(RegAddr == rTxAGC_A_Mcs15_Mcs12)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][5] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][5] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][5]));
		if(pHalData->rf_type== RF_1T1R)
		{
			pHalData->pwrGroupCnt++;
			//RT_TRACE(COMP_INIT, DBG_TRACE, ("pwrGroupCnt = %d\n", pHalData->pwrGroupCnt));
		}
	}
	if(RegAddr == rTxAGC_B_Rate18_06)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][8] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][8] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][8]));
	}
	if(RegAddr == rTxAGC_B_Rate54_24)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][9] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][9] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][9]));
	}
	if(RegAddr == rTxAGC_B_CCK1_55_Mcs32)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][14] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][14] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][14]));
	}
	if(RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0x000000ff)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][15] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][15] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][15]));
	}
	if(RegAddr == rTxAGC_B_Mcs03_Mcs00)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][10] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][10] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][10]));
	}
	if(RegAddr == rTxAGC_B_Mcs07_Mcs04)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][11] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][11] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][11]));
	}
	if(RegAddr == rTxAGC_B_Mcs11_Mcs08)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][12] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][12] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][12]));
	}
	if(RegAddr == rTxAGC_B_Mcs15_Mcs12)
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][13] = Data;
		//RT_TRACE(COMP_INIT, DBG_TRACE, ("MCSTxPowerLevelOriginalOffset[%d][13] = 0x%lx\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][13]));
		if(pHalData->rf_type != RF_1T1R)
			pHalData->pwrGroupCnt++;
	}
}
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
 * 11/06/2008 	MHC		Create Version 0.
 * 2009/07/29	tynli		(porting from 92SE branch)2009/03/11 Add copy parameter file to buffer for silent reset
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithPgParaFile(
	IN	PADAPTER		Adapter,
	IN	u8* 			pFileName)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	int		rtStatus = _SUCCESS;


	return rtStatus;

}	/* phy_ConfigBBWithPgParaFile */

#ifndef CONFIG_PHY_SETTING_WITH_ODM
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
 * 11/06/2008 	MHC		Add later!!!!!!.. Please modify for new files!!!!
 * 11/10/2008	tynli		Modify to mew files.
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithPgHeaderFile(
	IN	PADAPTER		Adapter,
	IN	u8 			ConfigType)
{
	int i;
	u32*	Rtl819XPHY_REGArray_Table_PG;
	u16	PHY_REGArrayPGLen;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);


	PHY_REGArrayPGLen = Rtl8188E_PHY_REG_Array_PGLength;
	Rtl819XPHY_REGArray_Table_PG = (u32*)Rtl8188E_PHY_REG_Array_PG;

	if(ConfigType == CONFIG_BB_PHY_REG)
	{
		for(i=0;i<PHY_REGArrayPGLen;i=i+3)
		{
			#if 0 //without IO, no delay is neeeded...
			if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfe){
				#ifdef CONFIG_LONG_DELAY_ISSUE
				rtw_msleep_os(50);
				#else
				rtw_mdelay_os(50);
				#endif
			}
			else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfd)
				rtw_mdelay_os(5);
			else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfc)
				rtw_mdelay_os(1);
			else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfb)
				rtw_udelay_os(50);
			else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xfa)
				rtw_udelay_os(5);
			else if (Rtl819XPHY_REGArray_Table_PG[i] == 0xf9)
				rtw_udelay_os(1);
			//PHY_SetBBReg(Adapter, Rtl819XPHY_REGArray_Table_PG[i], Rtl819XPHY_REGArray_Table_PG[i+1], Rtl819XPHY_REGArray_Table_PG[i+2]);		
			#endif
			
			storePwrIndexDiffRateOffset(Adapter, Rtl819XPHY_REGArray_Table_PG[i],
				Rtl819XPHY_REGArray_Table_PG[i+1],
				Rtl819XPHY_REGArray_Table_PG[i+2]);
			//PHY_SetBBReg(Adapter, Rtl819XPHY_REGArray_Table_PG[i], Rtl819XPHY_REGArray_Table_PG[i+1], Rtl819XPHY_REGArray_Table_PG[i+2]);
			//RT_TRACE(COMP_SEND, DBG_TRACE, ("The Rtl819XPHY_REGArray_Table_PG[0] is %lx Rtl819XPHY_REGArray_Table_PG[1] is %lx \n",Rtl819XPHY_REGArray_Table_PG[i], Rtl819XPHY_REGArray_Table_PG[i+1]));
		}
	}
	else
	{

		//RT_TRACE(COMP_SEND, DBG_LOUD, ("phy_ConfigBBWithPgHeaderFile(): ConfigType != CONFIG_BB_PHY_REG\n"));
	}

	return _SUCCESS;

}	/* phy_ConfigBBWithPgHeaderFile */
#endif //CONFIG_PHY_SETTING_WITH_ODM



static VOID
phy_BB8192C_Config_1T(
	IN PADAPTER Adapter
	)
{
#if 0
	//for path - A
	PHY_SetBBReg(Adapter, rFPGA0_TxInfo, 0x3, 0x1);
	PHY_SetBBReg(Adapter, rFPGA1_TxInfo, 0x0303, 0x0101);
	PHY_SetBBReg(Adapter, 0xe74, 0x0c000000, 0x1);
	PHY_SetBBReg(Adapter, 0xe78, 0x0c000000, 0x1);
	PHY_SetBBReg(Adapter, 0xe7c, 0x0c000000, 0x1);
	PHY_SetBBReg(Adapter, 0xe80, 0x0c000000, 0x1);
	PHY_SetBBReg(Adapter, 0xe88, 0x0c000000, 0x1);
#endif
	//for path - B
	PHY_SetBBReg(Adapter, rFPGA0_TxInfo, 0x3, 0x2);
	PHY_SetBBReg(Adapter, rFPGA1_TxInfo, 0x300033, 0x200022);

	// 20100519 Joseph: Add for 1T2R config. Suggested by Kevin, Jenyu and Yunan.
	PHY_SetBBReg(Adapter, rCCK0_AFESetting, bMaskByte3, 0x45);
	PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x23);
	PHY_SetBBReg(Adapter, rOFDM0_AGCParameter1, 0x30, 0x1);	// B path first AGC

	PHY_SetBBReg(Adapter, 0xe74, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe78, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe7c, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe80, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe88, 0x0c000000, 0x2);


}

// Joseph test: new initialize order!!
// Test only!! This part need to be re-organized.
// Now it is just for 8256.
static	int
phy_BB8190_Config_HardCode(
	IN	PADAPTER	Adapter
	)
{
	//RT_ASSERT(FALSE, ("This function is not implement yet!! \n"));
	return _SUCCESS;
}

static	int
phy_BB8188E_Config_ParaFile(
	IN	PADAPTER	Adapter
	)
{
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	int			rtStatus = _SUCCESS;

	u8	sz8188EBBRegFile[] = RTL8188E_PHY_REG;
	u8	sz8188EAGCTableFile[] = RTL8188E_AGC_TAB;
	u8	sz8188EBBRegPgFile[] = RTL8188E_PHY_REG_PG;
	u8	sz8188EBBRegMpFile[] = RTL8188E_PHY_REG_MP;

	u8	*pszBBRegFile = NULL, *pszAGCTableFile = NULL, *pszBBRegPgFile = NULL, *pszBBRegMpFile=NULL;


	//RT_TRACE(COMP_INIT, DBG_TRACE, ("==>phy_BB8192S_Config_ParaFile\n"));

	pszBBRegFile = sz8188EBBRegFile ;
	pszAGCTableFile = sz8188EAGCTableFile;
	pszBBRegPgFile = sz8188EBBRegPgFile;
	pszBBRegMpFile = sz8188EBBRegMpFile;

	//
	// 1. Read PHY_REG.TXT BB INIT!!
	// We will seperate as 88C / 92C according to chip version
	//
#ifdef CONFIG_EMBEDDED_FWIMG
	#ifdef CONFIG_PHY_SETTING_WITH_ODM
	if(HAL_STATUS_FAILURE ==ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG))
		rtStatus = _FAIL;
	#else
	rtStatus = phy_ConfigBBWithHeaderFile(Adapter, CONFIG_BB_PHY_REG);
	#endif//#ifdef CONFIG_PHY_SETTING_WITH_ODM
#else
	// No matter what kind of CHIP we always read PHY_REG.txt. We must copy different
	// type of parameter files to phy_reg.txt at first.
	rtStatus = phy_ConfigBBWithParaFile(Adapter,pszBBRegFile);
#endif//#ifdef CONFIG_EMBEDDED_FWIMG

	if(rtStatus != _SUCCESS){
		//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("phy_BB8192S_Config_ParaFile():Write BB Reg Fail!!"));
		goto phy_BB8190_Config_ParaFile_Fail;
	}

	//
	// 20100318 Joseph: Config 2T2R to 1T2R if necessary.
	//
	//if(pHalData->rf_type == RF_1T2R)
	//{
		//phy_BB8192C_Config_1T(Adapter);
		//DBG_8192C("phy_BB8188E_Config_ParaFile():Config to 1T!!\n");
	//}

	//
	// 2. If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt
	//
	if (pEEPROM->bautoload_fail_flag == _FALSE)
	{
		pHalData->pwrGroupCnt = 0;

#ifdef CONFIG_EMBEDDED_FWIMG
		#ifdef CONFIG_PHY_SETTING_WITH_ODM	
		if(HAL_STATUS_FAILURE ==ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG_PG))
			rtStatus = _FAIL;
		#else
		rtStatus = phy_ConfigBBWithPgHeaderFile(Adapter, CONFIG_BB_PHY_REG_PG);
		#endif
#else
		rtStatus = phy_ConfigBBWithPgParaFile(Adapter, pszBBRegPgFile);
#endif
	}

	if(rtStatus != _SUCCESS){
		//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("phy_BB8192S_Config_ParaFile():BB_PG Reg Fail!!"));
		goto phy_BB8190_Config_ParaFile_Fail;
	}

	//
	// 3. BB AGC table Initialization
	//
#ifdef CONFIG_EMBEDDED_FWIMG
	#ifdef CONFIG_PHY_SETTING_WITH_ODM	
	if(HAL_STATUS_FAILURE ==ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv,  CONFIG_BB_AGC_TAB))
		rtStatus = _FAIL;
	#else
	rtStatus = phy_ConfigBBWithHeaderFile(Adapter, CONFIG_BB_AGC_TAB);
	#endif//#ifdef CONFIG_PHY_SETTING_WITH_ODM	
#else
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("phy_BB8192S_Config_ParaFile AGC_TAB.txt\n"));
	rtStatus = phy_ConfigBBWithParaFile(Adapter, pszAGCTableFile);
#endif//#ifdef CONFIG_EMBEDDED_FWIMG

	if(rtStatus != _SUCCESS){
		//RT_TRACE(COMP_FPGA, DBG_SERIOUS, ("phy_BB8192S_Config_ParaFile():AGC Table Fail\n"));
		goto phy_BB8190_Config_ParaFile_Fail;
	}


phy_BB8190_Config_ParaFile_Fail:

	return rtStatus;
}


int
PHY_BBConfig8188E(
	IN	PADAPTER	Adapter
	)
{
	int	rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32	RegVal;
	u8	TmpU1B=0;
	u8	value8;

	phy_InitBBRFRegisterDefinition(Adapter);


	// Enable BB and RF
	RegVal = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	rtw_write16(Adapter, REG_SYS_FUNC_EN, (u16)(RegVal|BIT13|BIT0|BIT1));

	// 20090923 Joseph: Advised by Steven and Jenyu. Power sequence before init RF.
	//rtw_write8(Adapter, REG_AFE_PLL_CTRL, 0x83);
	//rtw_write8(Adapter, REG_AFE_PLL_CTRL+1, 0xdb);

	rtw_write8(Adapter, REG_RF_CTRL, RF_EN|RF_RSTB|RF_SDMRSTB);

#ifdef CONFIG_USB_HCI
	rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_USBA | FEN_USBD | FEN_BB_GLB_RSTn | FEN_BBRSTB);
#else
	rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_PPLL|FEN_PCIEA|FEN_DIO_PCIE|FEN_BB_GLB_RSTn|FEN_BBRSTB);
#endif

#if 0
		// 2009/10/21 by SD1 Jong. Modified by tynli. Not in Documented in V8.1.
	if(!IS_NORMAL_CHIP(pHalData->VersionID))
	{
#ifdef CONFIG_USB_HCI
		rtw_write8(Adapter, REG_LDOHCI12_CTRL, 0x1f);
#else
		rtw_write8(Adapter, REG_LDOHCI12_CTRL, 0x1b);
#endif
	}
	else
	{
#ifdef CONFIG_USB_HCI
		//To Fix MAC loopback mode fail. Suggested by SD4 Johnny. 2010.03.23.
		rtw_write8(Adapter, REG_LDOHCI12_CTRL, 0x0f);
		rtw_write8(Adapter, 0x15, 0xe9);
#endif
	}

	rtw_write8(Adapter, REG_AFE_XTAL_CTRL+1, 0x80);
#endif

#ifdef CONFIG_USB_HCI
		//rtw_write8(Adapter, 0x15, 0xe9);
#endif


#ifdef CONFIG_PCI_HCI
	// Force use left antenna by default for 88C.
	//	if(!IS_92C_SERIAL(pHalData->VersionID) || IS_92C_1T2R(pHalData->VersionID))
	if(Adapter->ledpriv.LedStrategy != SW_LED_MODE10)
	{
		RegVal = rtw_read32(Adapter, REG_LEDCFG0);
		rtw_write32(Adapter, REG_LEDCFG0, RegVal|BIT23);
	}
#endif

	//
	// Config BB and AGC
	//
	rtStatus = phy_BB8188E_Config_ParaFile(Adapter);

	return rtStatus;
	
}


int
PHY_RFConfig8188E(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;

	//
	// RF config
	//
	rtStatus = PHY_RF6052_Config8188E(Adapter);
#if 0
	switch(pHalData->rf_chip)
	{
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
		default: //for MacOs Warning: "RF_TYPE_MIN" not handled in switch
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
 * Input:      	PADAPTER			Adapter
 *			ps1Byte 				pFileName
 *			RF_RADIO_PATH_E	eRFPath
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		Delay may be required for RF configuration
 *---------------------------------------------------------------------------*/
int
rtl8188e_PHY_ConfigRFWithParaFile(
	IN	PADAPTER			Adapter,
	IN	u8* 				pFileName,
	RF_RADIO_PATH_E		eRFPath
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	int	rtStatus = _SUCCESS;


	return rtStatus;

}

//****************************************
// The following is for High Power PA
//****************************************
#define HighPowerRadioAArrayLen 22
//This is for High power PA
u32 Rtl8192S_HighPower_RadioA_Array[HighPowerRadioAArrayLen] = {
0x013,0x00029ea4,
0x013,0x00025e74,
0x013,0x00020ea4,
0x013,0x0001ced0,
0x013,0x00019f40,
0x013,0x00014e70,
0x013,0x000106a0,
0x013,0x0000c670,
0x013,0x000082a0,
0x013,0x00004270,
0x013,0x00000240,
};

int
PHY_ConfigRFExternalPA(
	IN	PADAPTER			Adapter,
	RF_RADIO_PATH_E		eRFPath
)
{
	int	rtStatus = _SUCCESS;
#ifdef CONFIG_USB_HCI
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u16 i=0;

	if(!pHalData->ExternalPA)
	{
		return rtStatus;
	}

	// 2010/10/19 MH According to Jenyu/EEChou 's opinion, we need not to execute the
	// same code as SU. It is already updated in radio_a_1T_HP.txt.
#if 0
	//add for SU High Power PA
	for(i = 0;i<HighPowerRadioAArrayLen; i=i+2)
	{
		RT_TRACE(COMP_INIT, DBG_LOUD, ("External PA, write RF 0x%lx=0x%lx\n", Rtl8192S_HighPower_RadioA_Array[i], Rtl8192S_HighPower_RadioA_Array[i+1]));
		PHY_SetRFReg(Adapter, eRFPath, Rtl8192S_HighPower_RadioA_Array[i], bRFRegOffsetMask, Rtl8192S_HighPower_RadioA_Array[i+1]);
	}
#endif

#endif
	return rtStatus;
}
//****************************************
/*-----------------------------------------------------------------------------
 * Function:    PHY_ConfigRFWithHeaderFile()
 *
 * Overview:    This function read RF parameters from general file format, and do RF 3-wire
 *
 * Input:      	PADAPTER			Adapter
 *			ps1Byte 				pFileName
 *			RF_RADIO_PATH_E	eRFPath
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		Delay may be required for RF configuration
 *---------------------------------------------------------------------------*/
#ifndef CONFIG_PHY_SETTING_WITH_ODM
int
rtl8188e_PHY_ConfigRFWithHeaderFile(
	IN	PADAPTER			Adapter,
	RF_RADIO_PATH_E			eRFPath
)
{

	int			i;
	int			rtStatus = _SUCCESS;
	u32*		Rtl819XRadioA_Array_Table;
	u32*		Rtl819XRadioB_Array_Table;
	u16		RadioA_ArrayLen,RadioB_ArrayLen;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);


	RadioA_ArrayLen = Rtl8188E_RadioA_1TArrayLength;
	Rtl819XRadioA_Array_Table = (u32*)Rtl8188E_RadioA_1TArray;
	RadioB_ArrayLen = Rtl8188E_RadioB_1TArrayLength;
	Rtl819XRadioB_Array_Table = (u32*)Rtl8188E_RadioB_1TArray;
//	RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> PHY_ConfigRFWithHeaderFile() Radio_A:Rtl8188ERadioA_1TArray\n"));
//	RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> PHY_ConfigRFWithHeaderFile() Radio_B:Rtl8188ERadioB_1TArray\n"));

	switch (eRFPath)
	{
		case RF_PATH_A:
                    #ifdef CONFIG_IOL_RF_RF_PATH_A
			{
				struct xmit_frame	*xmit_frame;
				if((xmit_frame=rtw_IOL_accquire_xmit_frame(Adapter)) == NULL) {
					rtStatus = _FAIL;
					goto exit;
				}

				for(i = 0;i<RadioA_ArrayLen; i=i+2)
				{
					if(Rtl819XRadioA_Array_Table[i] == 0xfe)
						rtw_IOL_append_DELAY_MS_cmd(xmit_frame, 50);
					else if (Rtl819XRadioA_Array_Table[i] == 0xfd)
						rtw_IOL_append_DELAY_MS_cmd(xmit_frame, 5);
					else if (Rtl819XRadioA_Array_Table[i] == 0xfc)
						rtw_IOL_append_DELAY_MS_cmd(xmit_frame, 1);
					else if (Rtl819XRadioA_Array_Table[i] == 0xfb)
						rtw_IOL_append_DELAY_US_cmd(xmit_frame, 50);
					else if (Rtl819XRadioA_Array_Table[i] == 0xfa)
						rtw_IOL_append_DELAY_US_cmd(xmit_frame, 5);
					else if (Rtl819XRadioA_Array_Table[i] == 0xf9)
						rtw_IOL_append_DELAY_US_cmd(xmit_frame, 1);
					else
					{
						BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
						u32	NewOffset = 0;
						u32	DataAndAddr = 0;

						NewOffset = Rtl819XRadioA_Array_Table[i] & 0x3f;
						DataAndAddr = ((NewOffset<<20) | (Rtl819XRadioA_Array_Table[i+1]&0x000fffff)) & 0x0fffffff;	// T65 RF
						rtw_IOL_append_WD_cmd(xmit_frame, pPhyReg->rf3wireOffset, DataAndAddr);
					}
				}
				rtStatus = rtw_IOL_exec_cmds_sync(Adapter, xmit_frame, 1000);
			}
			#else
			for(i = 0;i<RadioA_ArrayLen; i=i+2)
			{
				if(Rtl819XRadioA_Array_Table[i] == 0xfe) {
					#ifdef CONFIG_LONG_DELAY_ISSUE
					rtw_msleep_os(50);
					#else
					rtw_mdelay_os(50);
					#endif
				}
				else if (Rtl819XRadioA_Array_Table[i] == 0xfd)
					rtw_mdelay_os(5);
				else if (Rtl819XRadioA_Array_Table[i] == 0xfc)
					rtw_mdelay_os(1);
				else if (Rtl819XRadioA_Array_Table[i] == 0xfb)
					rtw_udelay_os(50);
				else if (Rtl819XRadioA_Array_Table[i] == 0xfa)
					rtw_udelay_os(5);
				else if (Rtl819XRadioA_Array_Table[i] == 0xf9)
					rtw_udelay_os(1);
				else
				{
					PHY_SetRFReg(Adapter, eRFPath, Rtl819XRadioA_Array_Table[i], bRFRegOffsetMask, Rtl819XRadioA_Array_Table[i+1]);
					// Add 1us delay between BB/RF register setting.
					rtw_udelay_os(1);
				}
			}
			#endif
			//Add for High Power PA
			PHY_ConfigRFExternalPA(Adapter, eRFPath);
			break;
		case RF_PATH_B:
                    #ifdef CONFIG_IOL_RF_RF_PATH_B
			{
				struct xmit_frame	*xmit_frame;
				if((xmit_frame=rtw_IOL_accquire_xmit_frame(Adapter)) == NULL) {
					rtStatus = _FAIL;
					goto exit;
				}

				for(i = 0;i<RadioB_ArrayLen; i=i+2)
				{
					if(Rtl819XRadioB_Array_Table[i] == 0xfe)
						rtw_IOL_append_DELAY_MS_cmd(xmit_frame, 50);
					else if (Rtl819XRadioB_Array_Table[i] == 0xfd)
						rtw_IOL_append_DELAY_MS_cmd(xmit_frame, 5);
					else if (Rtl819XRadioB_Array_Table[i] == 0xfc)
						rtw_IOL_append_DELAY_MS_cmd(xmit_frame, 1);
					else if (Rtl819XRadioB_Array_Table[i] == 0xfb)
						rtw_IOL_append_DELAY_US_cmd(xmit_frame, 50);
					else if (Rtl819XRadioB_Array_Table[i] == 0xfa)
						rtw_IOL_append_DELAY_US_cmd(xmit_frame, 5);
					else if (Rtl819XRadioB_Array_Table[i] == 0xf9)
						rtw_IOL_append_DELAY_US_cmd(xmit_frame, 1);
					else
					{
						BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
						u32	NewOffset = 0;
						u32	DataAndAddr = 0;

						NewOffset = Rtl819XRadioB_Array_Table[i] & 0x3f;
						DataAndAddr = ((NewOffset<<20) | (Rtl819XRadioB_Array_Table[i+1]&0x000fffff)) & 0x0fffffff;	// T65 RF
						rtw_IOL_append_WD_cmd(xmit_frame, pPhyReg->rf3wireOffset, DataAndAddr);
					}
				}
				rtStatus = rtw_IOL_exec_cmds_sync(Adapter, xmit_frame, 1000);
			}
			#else
			for(i = 0;i<RadioB_ArrayLen; i=i+2)
			{
				if(Rtl819XRadioB_Array_Table[i] == 0xfe)
				{ // Deay specific ms. Only RF configuration require delay.
#if 0//#ifdef CONFIG_USB_HCI
					#ifdef CONFIG_LONG_DELAY_ISSUE
					rtw_msleep_os(1000);
					#else
					rtw_mdelay_os(1000);
					#endif
#else
					#ifdef CONFIG_LONG_DELAY_ISSUE
					rtw_msleep_os(50);
					#else
					rtw_mdelay_os(50);
					#endif
#endif
				}
				else if (Rtl819XRadioB_Array_Table[i] == 0xfd)
					rtw_mdelay_os(5);
				else if (Rtl819XRadioB_Array_Table[i] == 0xfc)
					rtw_mdelay_os(1);
				else if (Rtl819XRadioB_Array_Table[i] == 0xfb)
					rtw_udelay_os(50);
				else if (Rtl819XRadioB_Array_Table[i] == 0xfa)
					rtw_udelay_os(5);
				else if (Rtl819XRadioB_Array_Table[i] == 0xf9)
					rtw_udelay_os(1);
				else
				{
					PHY_SetRFReg(Adapter, eRFPath, Rtl819XRadioB_Array_Table[i], bRFRegOffsetMask, Rtl819XRadioB_Array_Table[i+1]);
					// Add 1us delay between BB/RF register setting.
					rtw_udelay_os(1);
				}
			}
			#endif
			break;
		case RF_PATH_C:
			break;
		case RF_PATH_D:
			break;
	}

exit:
	return rtStatus;

}
#endif//#ifndef CONFIG_PHY_SETTING_WITH_ODM

/*-----------------------------------------------------------------------------
 * Function:    PHY_CheckBBAndRFOK()
 *
 * Overview:    This function is write register and then readback to make sure whether
 *			  BB[PHY0, PHY1], RF[Patha, path b, path c, path d] is Ok
 *
 * Input:      	PADAPTER			Adapter
 *			HW90_BLOCK_E		CheckBlock
 *			RF_RADIO_PATH_E	eRFPath		// it is used only when CheckBlock is HW90_BLOCK_RF
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: PHY is OK
 *
 * Note:		This function may be removed in the ASIC
 *---------------------------------------------------------------------------*/
int
PHY_CheckBBAndRFOK(
	IN	PADAPTER			Adapter,
	IN	HW90_BLOCK_E		CheckBlock,
	IN	RF_RADIO_PATH_E	eRFPath
	)
{
	int			rtStatus = _SUCCESS;

	u32				i, CheckTimes = 4,ulRegRead = 0;

	u32				WriteAddr[4];
	u32				WriteData[] = {0xfffff027, 0xaa55a02f, 0x00000027, 0x55aa502f};

	// Initialize register address offset to be checked
	WriteAddr[HW90_BLOCK_MAC] = 0x100;
	WriteAddr[HW90_BLOCK_PHY0] = 0x900;
	WriteAddr[HW90_BLOCK_PHY1] = 0x800;
	WriteAddr[HW90_BLOCK_RF] = 0x3;

	for(i=0 ; i < CheckTimes ; i++)
	{

		//
		// Write Data to register and readback
		//
		switch(CheckBlock)
		{
		case HW90_BLOCK_MAC:
			//RT_ASSERT(FALSE, ("PHY_CheckBBRFOK(): Never Write 0x100 here!"));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("PHY_CheckBBRFOK(): Never Write 0x100 here!\n"));
			break;

		case HW90_BLOCK_PHY0:
		case HW90_BLOCK_PHY1:
			rtw_write32(Adapter, WriteAddr[CheckBlock], WriteData[i]);
			ulRegRead = rtw_read32(Adapter, WriteAddr[CheckBlock]);
			break;

		case HW90_BLOCK_RF:
			// When initialization, we want the delay function(delay_ms(), delay_us()
			// ==> actually we call PlatformStallExecution()) to do NdisStallExecution()
			// [busy wait] instead of NdisMSleep(). So we acquire RT_INITIAL_SPINLOCK
			// to run at Dispatch level to achive it.
			//cosa PlatformAcquireSpinLock(Adapter, RT_INITIAL_SPINLOCK);
			WriteData[i] &= 0xfff;
			PHY_SetRFReg(Adapter, eRFPath, WriteAddr[HW90_BLOCK_RF], bRFRegOffsetMask, WriteData[i]);
			// TODO: we should not delay for such a long time. Ask SD3
			rtw_mdelay_os(10);
			ulRegRead = PHY_QueryRFReg(Adapter, eRFPath, WriteAddr[HW90_BLOCK_RF], bMaskDWord);
			rtw_mdelay_os(10);
			//cosa PlatformReleaseSpinLock(Adapter, RT_INITIAL_SPINLOCK);
			break;

		default:
			rtStatus = _FAIL;
			break;
		}


		//
		// Check whether readback data is correct
		//
		if(ulRegRead != WriteData[i])
		{
			//RT_TRACE(COMP_FPGA, DBG_LOUD, ("ulRegRead: %lx, WriteData: %lx \n", ulRegRead, WriteData[i]));
			rtStatus = _FAIL;
			break;
		}
	}

	return rtStatus;
}


VOID
rtl8192c_PHY_GetHWRegOriginalValue(
	IN	PADAPTER		Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	// read rx initial gain
	pHalData->DefaultInitialGain[0] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XAAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[1] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XBAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[2] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XCAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[3] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XDAGCCore1, bMaskByte0);
	//RT_TRACE(COMP_INIT, DBG_LOUD,
	//("Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x) \n",
	//pHalData->DefaultInitialGain[0], pHalData->DefaultInitialGain[1],
	//pHalData->DefaultInitialGain[2], pHalData->DefaultInitialGain[3]));

	// read framesync
	pHalData->framesync = (u8)PHY_QueryBBReg(Adapter, rOFDM0_RxDetector3, bMaskByte0);
	pHalData->framesyncC34 = PHY_QueryBBReg(Adapter, rOFDM0_RxDetector2, bMaskDWord);
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Default framesync (0x%x) = 0x%x \n",
	//	rOFDM0_RxDetector3, pHalData->framesync));
}


//
//	Description:
//		Map dBm into Tx power index according to
//		current HW model, for example, RF and PA, and
//		current wireless mode.
//	By Bruce, 2008-01-29.
//
static	u8
phy_DbmToTxPwrIdx(
	IN	PADAPTER		Adapter,
	IN	WIRELESS_MODE	WirelessMode,
	IN	int			PowerInDbm
	)
{
	u8				TxPwrIdx = 0;
	int				Offset = 0;


	//
	// Tested by MP, we found that CCK Index 0 equals to 8dbm, OFDM legacy equals to
	// 3dbm, and OFDM HT equals to 0dbm repectively.
	// Note:
	//	The mapping may be different by different NICs. Do not use this formula for what needs accurate result.
	// By Bruce, 2008-01-29.
	//
	switch(WirelessMode)
	{
	case WIRELESS_MODE_B:
		Offset = -7;
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		Offset = -8;
		break;
	default:
		Offset = -8;
		break;
	}

	if((PowerInDbm - Offset) > 0)
	{
		TxPwrIdx = (u8)((PowerInDbm - Offset) * 2);
	}
	else
	{
		TxPwrIdx = 0;
	}

	// Tx Power Index is too large.
	if(TxPwrIdx > MAX_TXPWR_IDX_NMODE_92S)
		TxPwrIdx = MAX_TXPWR_IDX_NMODE_92S;

	return TxPwrIdx;
}

//
//	Description:
//		Map Tx power index into dBm according to
//		current HW model, for example, RF and PA, and
//		current wireless mode.
//	By Bruce, 2008-01-29.
//
int
phy_TxPwrIdxToDbm(
	IN	PADAPTER		Adapter,
	IN	WIRELESS_MODE	WirelessMode,
	IN	u8			TxPwrIdx
	)
{
	int				Offset = 0;
	int				PwrOutDbm = 0;

	//
	// Tested by MP, we found that CCK Index 0 equals to -7dbm, OFDM legacy equals to -8dbm.
	// Note:
	//	The mapping may be different by different NICs. Do not use this formula for what needs accurate result.
	// By Bruce, 2008-01-29.
	//
	switch(WirelessMode)
	{
	case WIRELESS_MODE_B:
		Offset = -7;
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		Offset = -8;
	default:
		Offset = -8;
		break;
	}

	PwrOutDbm = TxPwrIdx / 2 + Offset; // Discard the decimal part.

	return PwrOutDbm;
}


/*-----------------------------------------------------------------------------
 * Function:    GetTxPowerLevel8190()
 *
 * Overview:    This function is export to "common" moudule
 *
 * Input:       PADAPTER		Adapter
 *			psByte			Power Level
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 *---------------------------------------------------------------------------*/
VOID
PHY_GetTxPowerLevel8188E(
	IN	PADAPTER		Adapter,
	OUT u32*    		powerlevel
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			TxPwrLevel = 0;
	int			TxPwrDbm;

	//
	// Because the Tx power indexes are different, we report the maximum of them to
	// meet the CCX TPC request. By Bruce, 2008-01-31.
	//

	// CCK
	TxPwrLevel = pHalData->CurrentCckTxPwrIdx;
	TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_B, TxPwrLevel);

	// Legacy OFDM
	TxPwrLevel = pHalData->CurrentOfdm24GTxPwrIdx + pHalData->LegacyHTTxPowerDiff;

	// Compare with Legacy OFDM Tx power.
	if(phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_G, TxPwrLevel);

	// HT OFDM
	TxPwrLevel = pHalData->CurrentOfdm24GTxPwrIdx;

	// Compare with HT OFDM Tx power.
	if(phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_N_24G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_N_24G, TxPwrLevel);

	*powerlevel = TxPwrDbm;
}

#if 0
static void getTxPowerIndex(
	IN	PADAPTER		Adapter,
	IN	u8			channel,
	IN OUT u8*		cckPowerLevel,
	IN OUT u8*		ofdmPowerLevel
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8				index = (channel -1);
	// 1. CCK
	cckPowerLevel[RF_PATH_A] = pHalData->TxPwrLevelCck[RF_PATH_A][index];	//RF-A
	cckPowerLevel[RF_PATH_B] = pHalData->TxPwrLevelCck[RF_PATH_B][index];	//RF-B

	// 2. OFDM for 1S or 2S
	if (GET_RF_TYPE(Adapter) == RF_1T2R || GET_RF_TYPE(Adapter) == RF_1T1R)
	{
		// Read HT 40 OFDM TX power
		ofdmPowerLevel[RF_PATH_A] = pHalData->TxPwrLevelHT40_1S[RF_PATH_A][index];
		ofdmPowerLevel[RF_PATH_B] = pHalData->TxPwrLevelHT40_1S[RF_PATH_B][index];
	}
	else if (GET_RF_TYPE(Adapter) == RF_2T2R)
	{
		// Read HT 40 OFDM TX power
		ofdmPowerLevel[RF_PATH_A] = pHalData->TxPwrLevelHT40_2S[RF_PATH_A][index];
		ofdmPowerLevel[RF_PATH_B] = pHalData->TxPwrLevelHT40_2S[RF_PATH_B][index];
	}
	//RTPRINT(FPHY, PHY_TXPWR, ("Channel-%d, set tx power index !!\n", channel));
}
#endif

void getTxPowerIndex88E(
	IN	PADAPTER		Adapter,
	IN	u8				channel,
	IN OUT u8*			cckPowerLevel,
	IN OUT u8*			ofdmPowerLevel,
	IN OUT u8*			BW20PowerLevel,
	IN OUT u8*			BW40PowerLevel
	)
{

	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8				index = (channel -1);
	u8				TxCount=0,path_nums;
		
	
	if((RF_1T2R == pHalData->rf_type) ||(RF_1T1R ==pHalData->rf_type ))	
		path_nums = 1;
	else	
		path_nums = 2;
	
	for(TxCount=0;TxCount< path_nums ;TxCount++)
	{
		if(TxCount==RF_PATH_A)
		{
			// 1. CCK
			cckPowerLevel[TxCount]		= pHalData->Index24G_CCK_Base[TxCount][index];
			//2. OFDM
			ofdmPowerLevel[TxCount]		= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
				pHalData->OFDM_24G_Diff[TxCount][RF_PATH_A];	
			// 1. BW20
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[TxCount][RF_PATH_A];
			//2. BW40
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];
			//RTPRINT(FPHY, PHY_TXPWR, ("getTxPowerIndex88E(): 40MBase=0x%x  20Mdiff=%d  20MBase=0x%x!!\n", 
			//	pHalData->Index24G_BW40_Base[RF_PATH_A][index],
			//	pHalData->BW20_24G_Diff[TxCount][RF_PATH_A],
			//	BW20PowerLevel[TxCount]));
		}
		else if(TxCount==RF_PATH_B)
		{
			// 1. CCK
			cckPowerLevel[TxCount]		= pHalData->Index24G_CCK_Base[TxCount][index];
			//2. OFDM
			ofdmPowerLevel[TxCount]		= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[TxCount][index];	
			// 1. BW20
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[TxCount][RF_PATH_A]+
			pHalData->BW20_24G_Diff[TxCount][index];
			//2. BW40
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];		
		}
		else if(TxCount==RF_PATH_C)
		{
	// 1. CCK
			cckPowerLevel[TxCount]		= pHalData->Index24G_CCK_Base[TxCount][index];
			//2. OFDM
			ofdmPowerLevel[TxCount]		= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_B][index]+
			pHalData->BW20_24G_Diff[TxCount][index];
			// 1. BW20
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_B][index]+
			pHalData->BW20_24G_Diff[TxCount][index];
			//2. BW40
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];		
		}
		else if(TxCount==RF_PATH_D)
		{
			// 1. CCK
			cckPowerLevel[TxCount]		= pHalData->Index24G_CCK_Base[TxCount][index];
			//2. OFDM
			ofdmPowerLevel[TxCount]		= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[RF_PATH_B][index]+
				pHalData->BW20_24G_Diff[RF_PATH_C][index]+
				pHalData->BW20_24G_Diff[TxCount][index];

			// 1. BW20
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[RF_PATH_B][index]+
				pHalData->BW20_24G_Diff[RF_PATH_C][index]+
				pHalData->BW20_24G_Diff[TxCount][index];

			//2. BW40
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];		
		}	
		else
		{
		}
	}

#if 0 // (INTEL_PROXIMITY_SUPPORT == 1)
	switch(pMgntInfo->IntelProximityModeInfo.PowerOutput){
		case 1: // 100%
			break;
		case 2: // 70%
			cckPowerLevel[0] -= 3;
			cckPowerLevel[1] -= 3;
			ofdmPowerLevel[0] -=3;
			ofdmPowerLevel[1] -= 3; 
			break;
		case 3: // 50%
			cckPowerLevel[0] -= 6;
			cckPowerLevel[1] -= 6;
			ofdmPowerLevel[0] -=6;
			ofdmPowerLevel[1] -= 6; 
			break;
		case 4: // 35%
			cckPowerLevel[0] -= 9;
			cckPowerLevel[1] -= 9;
			ofdmPowerLevel[0] -=9;
			ofdmPowerLevel[1] -= 9; 
			break;
		case 5: // 15%
			cckPowerLevel[0] -= 17;
			cckPowerLevel[1] -= 17;
			ofdmPowerLevel[0] -=17;
			ofdmPowerLevel[1] -= 17; 
			break;
	
		default:
			break;
	}	
#endif
	//RTPRINT(FPHY, PHY_TXPWR, ("Channel-%d, set tx power index !!\n", channel));
}

void phy_PowerIndexCheck88E(
	IN	PADAPTER	Adapter,
	IN	u8			channel,
	IN OUT u8 *		cckPowerLevel,
	IN OUT u8 *		ofdmPowerLevel,
	IN OUT u8 *		BW20PowerLevel,
	IN OUT u8 *		BW40PowerLevel	
	)
{

	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
#if 0 // (CCX_SUPPORT == 1)
	PMGNT_INFO			pMgntInfo = &(Adapter->MgntInfo);
	PRT_CCX_INFO		pCcxInfo = GET_CCX_INFO(pMgntInfo);

	//
	// CCX 2 S31, AP control of client transmit power:
	// 1. We shall not exceed Cell Power Limit as possible as we can.
	// 2. Tolerance is +/- 5dB.
	// 3. 802.11h Power Contraint takes higher precedence over CCX Cell Power Limit.
	// 
	// TODO: 
	// 1. 802.11h power contraint 
	//
	// 071011, by rcnjko.
	//
	if(	pMgntInfo->OpMode == RT_OP_MODE_INFRASTRUCTURE && 
		pMgntInfo->mAssoc &&
		pCcxInfo->bUpdateCcxPwr &&
		pCcxInfo->bWithCcxCellPwr &&
		channel == pMgntInfo->dot11CurrentChannelNumber)
	{
		u1Byte	CckCellPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_B, pCcxInfo->CcxCellPwr);
		u1Byte	LegacyOfdmCellPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_G, pCcxInfo->CcxCellPwr);
		u1Byte	OfdmCellPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_N_24G, pCcxInfo->CcxCellPwr);

		RT_TRACE(COMP_TXAGC, DBG_LOUD, 
		("CCX Cell Limit: %d dbm => CCK Tx power index : %d, Legacy OFDM Tx power index : %d, OFDM Tx power index: %d\n", 
		pCcxInfo->CcxCellPwr, CckCellPwrIdx, LegacyOfdmCellPwrIdx, OfdmCellPwrIdx));
		RT_TRACE(COMP_TXAGC, DBG_LOUD, 
		("EEPROM channel(%d) => CCK Tx power index: %d, Legacy OFDM Tx power index : %d, OFDM Tx power index: %d\n",
		channel, cckPowerLevel[0], ofdmPowerLevel[0] + pHalData->LegacyHTTxPowerDiff, ofdmPowerLevel[0])); 

		// CCK
		if(cckPowerLevel[0] > CckCellPwrIdx)
			cckPowerLevel[0] = CckCellPwrIdx;
		// Legacy OFDM, HT OFDM
		if(ofdmPowerLevel[0] + pHalData->LegacyHTTxPowerDiff > LegacyOfdmCellPwrIdx)
		{
			if((OfdmCellPwrIdx - pHalData->LegacyHTTxPowerDiff) > 0)
			{
				ofdmPowerLevel[0] = OfdmCellPwrIdx - pHalData->LegacyHTTxPowerDiff;
			}
			else
			{
				ofdmPowerLevel[0] = 0;
			}
		}

		RT_TRACE(COMP_TXAGC, DBG_LOUD, 
		("Altered CCK Tx power index : %d, Legacy OFDM Tx power index: %d, OFDM Tx power index: %d\n", 
		cckPowerLevel[0], ofdmPowerLevel[0] + pHalData->LegacyHTTxPowerDiff, ofdmPowerLevel[0]));
	}
#else
	// Add or not ???
#endif

	pHalData->CurrentCckTxPwrIdx = cckPowerLevel[0];
	pHalData->CurrentOfdm24GTxPwrIdx = ofdmPowerLevel[0];
	pHalData->CurrentBW2024GTxPwrIdx = BW20PowerLevel[0];
	pHalData->CurrentBW4024GTxPwrIdx = BW40PowerLevel[0];

	//DBG_871X("PHY_SetTxPowerLevel8188E(): CurrentCckTxPwrIdx : 0x%x,CurrentOfdm24GTxPwrIdx: 0x%x, CurrentBW2024GTxPwrIdx: 0x%dx, CurrentBW4024GTxPwrIdx: 0x%x \n", 
	//	pHalData->CurrentCckTxPwrIdx, pHalData->CurrentOfdm24GTxPwrIdx, pHalData->CurrentBW2024GTxPwrIdx, pHalData->CurrentBW4024GTxPwrIdx);
}
/*-----------------------------------------------------------------------------
 * Function:    SetTxPowerLevel8190()
 *
 * Overview:    This function is export to "HalCommon" moudule
 *			We must consider RF path later!!!!!!!
 *
 * Input:       PADAPTER		Adapter
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
VOID
PHY_SetTxPowerLevel8188E(
	IN	PADAPTER		Adapter,
	IN	u8				channel
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	
	u8	cckPowerLevel[MAX_TX_COUNT], ofdmPowerLevel[MAX_TX_COUNT];// [0]:RF-A, [1]:RF-B
	u8	BW20PowerLevel[MAX_TX_COUNT], BW40PowerLevel[MAX_TX_COUNT];
	u8	i=0;

#if(MP_DRIVER == 1)
	return;
#endif

	//getTxPowerIndex(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0]);
	getTxPowerIndex88E(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0],&BW20PowerLevel[0],&BW40PowerLevel[0]);

	//printk("Channel-%d, cckPowerLevel =  0x%x,   ofdmPowerLeve = 0x%x, BW20PowerLevel  =  0x%x,   BW40PowerLevel = 0x%x,\n", 
	//		channel, cckPowerLevel[0],  ofdmPowerLevel[0], BW20PowerLevel[0] ,BW40PowerLevel[0]);
	
	//RTPRINT(FPHY, PHY_TXPWR, ("Channel-%d, cckPowerLevel (A / B) = 0x%x / 0x%x,   ofdmPowerLevel (A / B) = 0x%x / 0x%x\n",
	//	channel, cckPowerLevel[0], cckPowerLevel[1], ofdmPowerLevel[0], ofdmPowerLevel[1]));

	//ccxPowerIndexCheck(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0]);
	phy_PowerIndexCheck88E(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0],&BW20PowerLevel[0],&BW40PowerLevel[0]);

	rtl8188e_PHY_RF6052SetCckTxPower(Adapter, &cckPowerLevel[0]);
	rtl8188e_PHY_RF6052SetOFDMTxPower(Adapter, &ofdmPowerLevel[0],&BW20PowerLevel[0],&BW40PowerLevel[0], channel);

#if 0
	switch(pHalData->rf_chip)
	{
		case RF_8225:
			PHY_SetRF8225CckTxPower(Adapter, cckPowerLevel[0]);
			PHY_SetRF8225OfdmTxPower(Adapter, ofdmPowerLevel[0]);
		break;

		case RF_8256:
			PHY_SetRF8256CCKTxPower(Adapter, cckPowerLevel[0]);
			PHY_SetRF8256OFDMTxPower(Adapter, ofdmPowerLevel[0]);
			break;

		case RF_6052:
			PHY_RF6052SetCckTxPower(Adapter, &cckPowerLevel[0]);
			PHY_RF6052SetOFDMTxPower(Adapter, &ofdmPowerLevel[0], channel);
			break;

		case RF_8258:
			break;
	}
#endif

}


//
//	Description:
//		Update transmit power level of all channel supported.
//
//	TODO:
//		A mode.
//	By Bruce, 2008-02-04.
//
BOOLEAN
PHY_UpdateTxPowerDbm8188E(
	IN	PADAPTER	Adapter,
	IN	int		powerInDbm
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8				idx;
	u8			rf_path;

	// TODO: A mode Tx power.
	u8	CckTxPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_B, powerInDbm);
	u8	OfdmTxPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_N_24G, powerInDbm);

	if(OfdmTxPwrIdx - pHalData->LegacyHTTxPowerDiff > 0)
		OfdmTxPwrIdx -= pHalData->LegacyHTTxPowerDiff;
	else
		OfdmTxPwrIdx = 0;

	//RT_TRACE(COMP_TXAGC, DBG_LOUD, ("PHY_UpdateTxPowerDbm8192S(): %ld dBm , CckTxPwrIdx = %d, OfdmTxPwrIdx = %d\n", powerInDbm, CckTxPwrIdx, OfdmTxPwrIdx));

	for(idx = 0; idx < 14; idx++)
	{
		for (rf_path = 0; rf_path < 2; rf_path++)
		{
			pHalData->TxPwrLevelCck[rf_path][idx] = CckTxPwrIdx;
			pHalData->TxPwrLevelHT40_1S[rf_path][idx] =
			pHalData->TxPwrLevelHT40_2S[rf_path][idx] = OfdmTxPwrIdx;
		}
	}

	//Adapter->HalFunc.SetTxPowerLevelHandler(Adapter, pHalData->CurrentChannel);//gtest:todo

	return _TRUE;
}


/*
	Description:
		When beacon interval is changed, the values of the
		hw registers should be modified.
	By tynli, 2008.10.24.

*/


void
rtl8192c_PHY_SetBeaconHwReg(
	IN	PADAPTER		Adapter,
	IN	u16			BeaconInterval
	)
{

}


VOID
PHY_ScanOperationBackup8188E(
	IN	PADAPTER	Adapter,
	IN	u8		Operation
	)
{
#if 0
	IO_TYPE	IoType;

	if(!Adapter->bDriverStopped)
	{
		switch(Operation)
		{
			case SCAN_OPT_BACKUP:
				IoType = IO_CMD_PAUSE_DM_BY_SCAN;
				rtw_hal_set_hwreg(Adapter,HW_VAR_IO_CMD,  (pu1Byte)&IoType);

				break;

			case SCAN_OPT_RESTORE:
				IoType = IO_CMD_RESUME_DM_BY_SCAN;
				rtw_hal_set_hwreg(Adapter,HW_VAR_IO_CMD,  (pu1Byte)&IoType);
				break;

			default:
				RT_TRACE(COMP_SCAN, DBG_LOUD, ("Unknown Scan Backup Operation. \n"));
				break;
		}
	}
#endif
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_SetBWModeCallback8192C()
 *
 * Overview:    Timer callback function for SetSetBWMode
 *
 * Input:       	PRT_TIMER		pTimer
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		(1) We do not take j mode into consideration now
 *			(2) Will two workitem of "switch channel" and "switch channel bandwidth" run
 *			     concurrently?
 *---------------------------------------------------------------------------*/
static VOID
_PHY_SetBWMode92C(
	IN	PADAPTER	Adapter
)
{
//	PADAPTER			Adapter = (PADAPTER)pTimer->Adapter;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8				regBwOpMode;
	u8				regRRSR_RSC;

	//return;

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//u4Byte				NowL, NowH;
	//u8Byte				BeginTime, EndTime;

	/*RT_TRACE(COMP_SCAN, DBG_LOUD, ("==>PHY_SetBWModeCallback8192C()  Switch to %s bandwidth\n", \
					pHalData->CurrentChannelBW == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz"))*/

	if(pHalData->rf_chip == RF_PSEUDO_11N)
	{
		//pHalData->SetBWModeInProgress= _FALSE;
		return;
	}

	// There is no 40MHz mode in RF_8225.
	if(pHalData->rf_chip==RF_8225)
		return;

	if(Adapter->bDriverStopped)
		return;

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//NowL = PlatformEFIORead4Byte(Adapter, TSFR);
	//NowH = PlatformEFIORead4Byte(Adapter, TSFR+4);
	//BeginTime = ((u8Byte)NowH << 32) + NowL;

	//3//
	//3//<1>Set MAC register
	//3//
	//Adapter->HalFunc.SetBWModeHandler();

	regBwOpMode = rtw_read8(Adapter, REG_BWOPMODE);
	regRRSR_RSC = rtw_read8(Adapter, REG_RRSR+2);
	//regBwOpMode = rtw_hal_get_hwreg(Adapter,HW_VAR_BWMODE,(pu1Byte)&regBwOpMode);

	switch(pHalData->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			regBwOpMode |= BW_OPMODE_20MHZ;
			   // 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);
			break;

		case HT_CHANNEL_WIDTH_40:
			regBwOpMode &= ~BW_OPMODE_20MHZ;
				// 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);

			regRRSR_RSC = (regRRSR_RSC&0x90) |(pHalData->nCur40MhzPrimeSC<<5);
			rtw_write8(Adapter, REG_RRSR+2, regRRSR_RSC);
			break;

		default:
			/*RT_TRACE(COMP_DBG, DBG_LOUD, ("PHY_SetBWModeCallback8192C():
						unknown Bandwidth: %#X\n",pHalData->CurrentChannelBW));*/
			break;
	}

	//3//
	//3//<2>Set PHY related register
	//3//
	switch(pHalData->CurrentChannelBW)
	{
		/* 20 MHz channel*/
		case HT_CHANNEL_WIDTH_20:
			PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x0);
			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x0);
			//PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 1);

			break;


		/* 40 MHz channel*/
		case HT_CHANNEL_WIDTH_40:
			PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x1);
			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x1);

			// Set Control channel to upper or lower. These settings are required only for 40MHz
			PHY_SetBBReg(Adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC>>1));
			PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);
			//PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 0);

			PHY_SetBBReg(Adapter, 0x818, (BIT26|BIT27), (pHalData->nCur40MhzPrimeSC==HAL_PRIME_CHNL_OFFSET_LOWER)?2:1);

			break;



		default:
			/*RT_TRACE(COMP_DBG, DBG_LOUD, ("PHY_SetBWModeCallback8192C(): unknown Bandwidth: %#X\n"\
						,pHalData->CurrentChannelBW));*/
			break;

	}
	//Skip over setting of J-mode in BB register here. Default value is "None J mode". Emily 20070315

	// Added it for 20/40 mhz switch time evaluation by guangan 070531
	//NowL = PlatformEFIORead4Byte(Adapter, TSFR);
	//NowH = PlatformEFIORead4Byte(Adapter, TSFR+4);
	//EndTime = ((u8Byte)NowH << 32) + NowL;
	//RT_TRACE(COMP_SCAN, DBG_LOUD, ("SetBWModeCallback8190Pci: time of SetBWMode = %I64d us!\n", (EndTime - BeginTime)));

	//3<3>Set RF related register
	switch(pHalData->rf_chip)
	{
		case RF_8225:
			//PHY_SetRF8225Bandwidth(Adapter, pHalData->CurrentChannelBW);
			break;

		case RF_8256:
			// Please implement this function in Hal8190PciPhy8256.c
			//PHY_SetRF8256Bandwidth(Adapter, pHalData->CurrentChannelBW);
			break;

		case RF_8258:
			// Please implement this function in Hal8190PciPhy8258.c
			// PHY_SetRF8258Bandwidth();
			break;

		case RF_PSEUDO_11N:
			// Do Nothing
			break;

		case RF_6052:
			rtl8188e_PHY_RF6052SetBandwidth(Adapter, pHalData->CurrentChannelBW);
			break;

		default:
			//RT_ASSERT(FALSE, ("Unknown RFChipID: %d\n", pHalData->RFChipID));
			break;
	}

	//pHalData->SetBWModeInProgress= FALSE;

	//RT_TRACE(COMP_SCAN, DBG_LOUD, ("<==PHY_SetBWModeCallback8192C() \n" ));
}


 /*-----------------------------------------------------------------------------
 * Function:   SetBWMode8190Pci()
 *
 * Overview:  This function is export to "HalCommon" moudule
 *
 * Input:       	PADAPTER			Adapter
 *			HT_CHANNEL_WIDTH	Bandwidth	//20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		We do not take j mode into consideration now
 *---------------------------------------------------------------------------*/
VOID
PHY_SetBWMode8188E(
	IN	PADAPTER					Adapter,
	IN	HT_CHANNEL_WIDTH	Bandwidth,	// 20M or 40M
	IN	unsigned char	Offset		// Upper, Lower, or Don't care
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	HT_CHANNEL_WIDTH 	tmpBW= pHalData->CurrentChannelBW;
	// Modified it for 20/40 mhz switch by guangan 070531
	//PMGNT_INFO	pMgntInfo=&Adapter->MgntInfo;

	//return;

	//if(pHalData->SwChnlInProgress)
//	if(pMgntInfo->bScanInProgress)
//	{
//		RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SetBWMode8192C() %s Exit because bScanInProgress!\n",
//					Bandwidth == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz"));
//		return;
//	}

//	if(pHalData->SetBWModeInProgress)
//	{
//		// Modified it for 20/40 mhz switch by guangan 070531
//		RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SetBWMode8192C() %s cancel last timer because SetBWModeInProgress!\n",
//					Bandwidth == HT_CHANNEL_WIDTH_20?"20MHz":"40MHz"));
//		PlatformCancelTimer(Adapter, &pHalData->SetBWModeTimer);
//		//return;
//	}

	//if(pHalData->SetBWModeInProgress)
	//	return;

	//pHalData->SetBWModeInProgress= TRUE;

	pHalData->CurrentChannelBW = Bandwidth;

#if 0
	if(Offset==HT_EXTCHNL_OFFSET_LOWER)
		pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if(Offset==HT_EXTCHNL_OFFSET_UPPER)
		pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_LOWER;
	else
		pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
#else
	pHalData->nCur40MhzPrimeSC = Offset;
#endif

	if((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved))
	{
	#if 0
		//PlatformSetTimer(Adapter, &(pHalData->SetBWModeTimer), 0);
	#else
		_PHY_SetBWMode92C(Adapter);
	#endif
	}
	else
	{
		//RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SetBWMode8192C() SetBWModeInProgress FALSE driver sleep or unload\n"));
		//pHalData->SetBWModeInProgress= FALSE;
		pHalData->CurrentChannelBW = tmpBW;
	}
	ODM_CmnInfoUpdate(&pHalData->odmpriv,ODM_CMNINFO_BW, pHalData->CurrentChannelBW );
	ODM_CmnInfoUpdate(&pHalData->odmpriv,ODM_CMNINFO_SEC_CHNL_OFFSET,pHalData->nCur40MhzPrimeSC );
}


static void _PHY_SwChnl8192C(PADAPTER Adapter, u8 channel)
{
	u8 eRFPath;
	u32 param1, param2;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	//s1. pre common command - CmdID_SetTxPowerLevel
	PHY_SetTxPowerLevel8188E(Adapter, channel);

	//s2. RF dependent command - CmdID_RF_WriteReg, param1=RF_CHNLBW, param2=channel
	param1 = RF_CHNLBW;
	param2 = channel;
	for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	{
		pHalData->RfRegChnlVal[eRFPath] = ((pHalData->RfRegChnlVal[eRFPath] & 0xfffffc00) | param2);
		PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, param1, bRFRegOffsetMask, pHalData->RfRegChnlVal[eRFPath]);
	}


	//s3. post common command - CmdID_End, None

}

VOID
PHY_SwChnl8188E(	// Call after initialization
	IN	PADAPTER	Adapter,
	IN	u8		channel
	)
{
	//PADAPTER Adapter =  ADJUST_TO_ADAPTIVE_ADAPTER(pAdapter, _TRUE);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	tmpchannel = pHalData->CurrentChannel;
	BOOLEAN  bResult = _TRUE;

	if(pHalData->rf_chip == RF_PSEUDO_11N)
	{
		//pHalData->SwChnlInProgress=FALSE;
		return; 								//return immediately if it is peudo-phy
	}

	//if(pHalData->SwChnlInProgress)
	//	return;

	//if(pHalData->SetBWModeInProgress)
	//	return;

	//--------------------------------------------
	switch(pHalData->CurrentWirelessMode)
	{
		case WIRELESS_MODE_A:
		case WIRELESS_MODE_N_5G:
			//RT_ASSERT((channel>14), ("WIRELESS_MODE_A but channel<=14"));
			break;

		case WIRELESS_MODE_B:
			//RT_ASSERT((channel<=14), ("WIRELESS_MODE_B but channel>14"));
			break;

		case WIRELESS_MODE_G:
		case WIRELESS_MODE_N_24G:
			//RT_ASSERT((channel<=14), ("WIRELESS_MODE_G but channel>14"));
			break;

		default:
			//RT_ASSERT(FALSE, ("Invalid WirelessMode(%#x)!!\n", pHalData->CurrentWirelessMode));
			break;
	}
	//--------------------------------------------

	//pHalData->SwChnlInProgress = TRUE;
	if(channel == 0)
		channel = 1;

	pHalData->CurrentChannel=channel;

	//pHalData->SwChnlStage=0;
	//pHalData->SwChnlStep=0;

	if((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved))
	{
		#if 0
		//PlatformSetTimer(Adapter, &(pHalData->SwChnlTimer), 0);
		#else
		_PHY_SwChnl8192C(Adapter, channel);
		#endif

		if(bResult)
		{
			//RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SwChnl8192C SwChnlInProgress TRUE schdule workitem done\n"));
		}
		else
		{
			//RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SwChnl8192C SwChnlInProgress FALSE schdule workitem error\n"));
			//if(IS_HARDWARE_TYPE_8192SU(Adapter))
			//{
			//	pHalData->SwChnlInProgress = FALSE;
				pHalData->CurrentChannel = tmpchannel;
			//}
		}

	}
	else
	{
		//RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SwChnl8192C SwChnlInProgress FALSE driver sleep or unload\n"));
		//if(IS_HARDWARE_TYPE_8192SU(Adapter))
		//{
		//	pHalData->SwChnlInProgress = FALSE;
			pHalData->CurrentChannel = tmpchannel;
		//}
	}
}


static	BOOLEAN
phy_SwChnlStepByStep(
	IN	PADAPTER	Adapter,
	IN	u8		channel,
	IN	u8		*stage,
	IN	u8		*step,
	OUT u32		*delay
	)
{
#if 0
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PCHANNEL_ACCESS_SETTING	pChnlAccessSetting;
	SwChnlCmd				PreCommonCmd[MAX_PRECMD_CNT];
	u4Byte					PreCommonCmdCnt;
	SwChnlCmd				PostCommonCmd[MAX_POSTCMD_CNT];
	u4Byte					PostCommonCmdCnt;
	SwChnlCmd				RfDependCmd[MAX_RFDEPENDCMD_CNT];
	u4Byte					RfDependCmdCnt;
	SwChnlCmd				*CurrentCmd;
	u1Byte					eRFPath;
	u4Byte					RfTXPowerCtrl;
	BOOLEAN					bAdjRfTXPowerCtrl = _FALSE;


	RT_ASSERT((Adapter != NULL), ("Adapter should not be NULL\n"));
#if(MP_DRIVER != 1)
	RT_ASSERT(IsLegalChannel(Adapter, channel), ("illegal channel: %d\n", channel));
#endif
	RT_ASSERT((pHalData != NULL), ("pHalData should not be NULL\n"));

	pChnlAccessSetting = &Adapter->MgntInfo.Info8185.ChannelAccessSetting;
	RT_ASSERT((pChnlAccessSetting != NULL), ("pChnlAccessSetting should not be NULL\n"));

	//for(eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	//for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	//{
		// <1> Fill up pre common command.
	PreCommonCmdCnt = 0;
	phy_SetSwChnlCmdArray(PreCommonCmd, PreCommonCmdCnt++, MAX_PRECMD_CNT,
				CmdID_SetTxPowerLevel, 0, 0, 0);
	phy_SetSwChnlCmdArray(PreCommonCmd, PreCommonCmdCnt++, MAX_PRECMD_CNT,
				CmdID_End, 0, 0, 0);

		// <2> Fill up post common command.
	PostCommonCmdCnt = 0;

	phy_SetSwChnlCmdArray(PostCommonCmd, PostCommonCmdCnt++, MAX_POSTCMD_CNT,
				CmdID_End, 0, 0, 0);

		// <3> Fill up RF dependent command.
	RfDependCmdCnt = 0;
	switch( pHalData->RFChipID )
	{
		case RF_8225:
		RT_ASSERT((channel >= 1 && channel <= 14), ("illegal channel for Zebra: %d\n", channel));
		// 2008/09/04 MH Change channel.
		if(channel==14) channel++;
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
			CmdID_RF_WriteReg, rZebra1_Channel, (0x10+channel-1), 10);
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
		CmdID_End, 0, 0, 0);
		break;

	case RF_8256:
		// TEST!! This is not the table for 8256!!
		RT_ASSERT((channel >= 1 && channel <= 14), ("illegal channel for Zebra: %d\n", channel));
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
			CmdID_RF_WriteReg, rRfChannel, channel, 10);
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
		CmdID_End, 0, 0, 0);
		break;

	case RF_6052:
		RT_ASSERT((channel >= 1 && channel <= 14), ("illegal channel for Zebra: %d\n", channel));
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
			CmdID_RF_WriteReg, RF_CHNLBW, channel, 10);
		phy_SetSwChnlCmdArray(RfDependCmd, RfDependCmdCnt++, MAX_RFDEPENDCMD_CNT,
		CmdID_End, 0, 0, 0);

		break;

	case RF_8258:
		break;

	// For FPGA two MAC verification
	case RF_PSEUDO_11N:
		return TRUE;
	default:
		RT_ASSERT(FALSE, ("Unknown RFChipID: %d\n", pHalData->RFChipID));
		return FALSE;
		break;
	}


	do{
		switch(*stage)
		{
		case 0:
			CurrentCmd=&PreCommonCmd[*step];
			break;
		case 1:
			CurrentCmd=&RfDependCmd[*step];
			break;
		case 2:
			CurrentCmd=&PostCommonCmd[*step];
			break;
		}

		if(CurrentCmd->CmdID==CmdID_End)
		{
			if((*stage)==2)
			{
				return TRUE;
			}
			else
			{
				(*stage)++;
				(*step)=0;
				continue;
			}
		}

		switch(CurrentCmd->CmdID)
		{
		case CmdID_SetTxPowerLevel:
			PHY_SetTxPowerLevel8192C(Adapter,channel);
			break;
		case CmdID_WritePortUlong:
			PlatformEFIOWrite4Byte(Adapter, CurrentCmd->Para1, CurrentCmd->Para2);
			break;
		case CmdID_WritePortUshort:
			PlatformEFIOWrite2Byte(Adapter, CurrentCmd->Para1, (u2Byte)CurrentCmd->Para2);
			break;
		case CmdID_WritePortUchar:
			PlatformEFIOWrite1Byte(Adapter, CurrentCmd->Para1, (u1Byte)CurrentCmd->Para2);
			break;
		case CmdID_RF_WriteReg:	// Only modify channel for the register now !!!!!
			for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
			{
#if 1
				pHalData->RfRegChnlVal[eRFPath] = ((pHalData->RfRegChnlVal[eRFPath] & 0xfffffc00) | CurrentCmd->Para2);
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, CurrentCmd->Para1, bRFRegOffsetMask, pHalData->RfRegChnlVal[eRFPath]);
#else
				PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, CurrentCmd->Para1, bRFRegOffsetMask, (CurrentCmd->Para2));
#endif
			}
			break;
		}

		break;
	}while(TRUE);
	//cosa }/*for(Number of RF paths)*/

	(*delay)=CurrentCmd->msDelay;
	(*step)++;
	return FALSE;
#endif
	return _TRUE;
}


static	BOOLEAN
phy_SetSwChnlCmdArray(
	SwChnlCmd*		CmdTable,
	u32			CmdTableIdx,
	u32			CmdTableSz,
	SwChnlCmdID		CmdID,
	u32			Para1,
	u32			Para2,
	u32			msDelay
	)
{
	SwChnlCmd* pCmd;

	if(CmdTable == NULL)
	{
		//RT_ASSERT(FALSE, ("phy_SetSwChnlCmdArray(): CmdTable cannot be NULL.\n"));
		return _FALSE;
	}
	if(CmdTableIdx >= CmdTableSz)
	{
		//RT_ASSERT(FALSE,
		//		("phy_SetSwChnlCmdArray(): Access invalid index, please check size of the table, CmdTableIdx:%ld, CmdTableSz:%ld\n",
		//		CmdTableIdx, CmdTableSz));
		return _FALSE;
	}

	pCmd = CmdTable + CmdTableIdx;
	pCmd->CmdID = CmdID;
	pCmd->Para1 = Para1;
	pCmd->Para2 = Para2;
	pCmd->msDelay = msDelay;

	return _TRUE;
}


static	void
phy_FinishSwChnlNow(	// We should not call this function directly
		IN	PADAPTER	Adapter,
		IN	u8		channel
		)
{
#if 0
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			delay;

	while(!phy_SwChnlStepByStep(Adapter,channel,&pHalData->SwChnlStage,&pHalData->SwChnlStep,&delay))
	{
		if(delay>0)
			rtw_mdelay_os(delay);
	}
#endif
}



//
// Description:
//	Switch channel synchronously. Called by SwChnlByDelayHandler.
//
// Implemented by Bruce, 2008-02-14.
// The following procedure is operted according to SwChanlCallback8190Pci().
// However, this procedure is performed synchronously  which should be running under
// passive level.
//
VOID
PHY_SwChnlPhy8192C(	// Only called during initialize
	IN	PADAPTER	Adapter,
	IN	u8		channel
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	//RT_TRACE(COMP_SCAN | COMP_RM, DBG_LOUD, ("==>PHY_SwChnlPhy8192S(), switch from channel %d to channel %d.\n", pHalData->CurrentChannel, channel));

	// Cannot IO.
	//if(RT_CANNOT_IO(Adapter))
	//	return;

	// Channel Switching is in progress.
	//if(pHalData->SwChnlInProgress)
	//	return;

	//return immediately if it is peudo-phy
	if(pHalData->rf_chip == RF_PSEUDO_11N)
	{
		//pHalData->SwChnlInProgress=FALSE;
		return;
	}

	//pHalData->SwChnlInProgress = TRUE;
	if( channel == 0)
		channel = 1;

	pHalData->CurrentChannel=channel;

	//pHalData->SwChnlStage = 0;
	//pHalData->SwChnlStep = 0;

	phy_FinishSwChnlNow(Adapter,channel);

	//pHalData->SwChnlInProgress = FALSE;
}


//
//	Description:
//		Configure H/W functionality to enable/disable Monitor mode.
//		Note, because we possibly need to configure BB and RF in this function,
//		so caller should in PASSIVE_LEVEL. 080118, by rcnjko.
//
VOID
PHY_SetMonitorMode8192C(
	IN	PADAPTER			pAdapter,
	IN	BOOLEAN				bEnableMonitorMode
	)
{
#if 0
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(pAdapter);
	BOOLEAN				bFilterOutNonAssociatedBSSID = FALSE;

	//2 Note: we may need to stop antenna diversity.
	if(bEnableMonitorMode)
	{
		bFilterOutNonAssociatedBSSID = FALSE;
		RT_TRACE(COMP_RM, DBG_LOUD, ("PHY_SetMonitorMode8192S(): enable monitor mode\n"));

		pHalData->bInMonitorMode = TRUE;
		pAdapter->HalFunc.AllowAllDestAddrHandler(pAdapter, TRUE, TRUE);
		rtw_hal_set_hwreg(pAdapter, HW_VAR_CHECK_BSSID, (pu1Byte)&bFilterOutNonAssociatedBSSID);
	}
	else
	{
		bFilterOutNonAssociatedBSSID = TRUE;
		RT_TRACE(COMP_RM, DBG_LOUD, ("PHY_SetMonitorMode8192S(): disable monitor mode\n"));

		pAdapter->HalFunc.AllowAllDestAddrHandler(pAdapter, FALSE, TRUE);
		pHalData->bInMonitorMode = FALSE;
		rtw_hal_set_hwreg(pAdapter, HW_VAR_CHECK_BSSID, (pu1Byte)&bFilterOutNonAssociatedBSSID);
	}
#endif
}


/*-----------------------------------------------------------------------------
 * Function:	PHYCheckIsLegalRfPath8190Pci()
 *
 * Overview:	Check different RF type to execute legal judgement. If RF Path is illegal
 *			We will return false.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	11/15/2007	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
BOOLEAN
PHY_CheckIsLegalRfPath8192C(
	IN	PADAPTER	pAdapter,
	IN	u32	eRFPath)
{
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	BOOLEAN				rtValue = _TRUE;

	// NOt check RF Path now.!
#if 0
	if (pHalData->RF_Type == RF_1T2R && eRFPath != RF_PATH_A)
	{
		rtValue = FALSE;
	}
	if (pHalData->RF_Type == RF_1T2R && eRFPath != RF_PATH_A)
	{

	}
#endif
	return	rtValue;

}	/* PHY_CheckIsLegalRfPath8192C */

static VOID _PHY_SetRFPathSwitch(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		bMain,
	IN	BOOLEAN		is2T
	)
{
	u8	u1bTmp;

	if(!pAdapter->hw_init_completed)
	{
		u1bTmp = rtw_read8(pAdapter, REG_LEDCFG2) | BIT7;
		rtw_write8(pAdapter, REG_LEDCFG2, u1bTmp);
		//PHY_SetBBReg(pAdapter, REG_LEDCFG0, BIT23, 0x01);
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT13, 0x01);
	}

	if(is2T)
	{
		if(bMain)
			PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6, 0x1);	//92C_Path_A
		else
			PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6, 0x2);	//BT
	}
	else
	{

		if(bMain)
			PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, 0x300, 0x2);	//Main
		else
			PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, 0x300, 0x1);	//Aux
	}

}

//return value TRUE => Main; FALSE => Aux

static BOOLEAN _PHY_QueryRFPathSwitch(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		is2T
	)
{
//	if(is2T)
//		return _TRUE;

	if(!pAdapter->hw_init_completed)
	{
		PHY_SetBBReg(pAdapter, REG_LEDCFG0, BIT23, 0x01);
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT13, 0x01);
	}

	if(is2T)
	{
		if(PHY_QueryBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6) == 0x01)
			return _TRUE;
		else
			return _FALSE;
	}
	else
	{
		if(PHY_QueryBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, 0x300) == 0x02)
			return _TRUE;
		else
			return _FALSE;
	}
}


static VOID
_PHY_DumpRFReg(IN	PADAPTER	pAdapter)
{
	u32 rfRegValue,rfRegOffset;

	//RTPRINT(FINIT, INIT_RF, ("PHY_DumpRFReg()====>\n"));

	for(rfRegOffset = 0x00;rfRegOffset<=0x30;rfRegOffset++){
		rfRegValue = PHY_QueryRFReg(pAdapter,RF_PATH_A, rfRegOffset, bMaskDWord);
		//RTPRINT(FINIT, INIT_RF, (" 0x%02x = 0x%08x\n",rfRegOffset,rfRegValue));
	}
	//RTPRINT(FINIT, INIT_RF, ("<===== PHY_DumpRFReg()\n"));
}


//
// Move from phycfg.c to gen.c to be code independent later
//
//-------------------------Move to other DIR later----------------------------*/
#ifdef CONFIG_USB_HCI

//
//	Description:
// 		To dump all Tx FIFO LLT related link-list table.
//		Added by Roger, 2009.03.10.
//
VOID
DumpBBDbgPort_92CU(
	IN	PADAPTER			Adapter
	)
{

	//RT_TRACE(COMP_SEND, DBG_WARNING, ("\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n"));
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("BaseBand Debug Ports:\n"));

	PHY_SetBBReg(Adapter, 0x0908, 0xffff, 0x0000);
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0xdf4, PHY_QueryBBReg(Adapter, 0x0df4, bMaskDWord)));

	PHY_SetBBReg(Adapter, 0x0908, 0xffff, 0x0803);
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0xdf4, PHY_QueryBBReg(Adapter, 0x0df4, bMaskDWord)));

	PHY_SetBBReg(Adapter, 0x0908, 0xffff, 0x0a06);
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0xdf4, PHY_QueryBBReg(Adapter, 0x0df4, bMaskDWord)));

	PHY_SetBBReg(Adapter, 0x0908, 0xffff, 0x0007);
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0xdf4, PHY_QueryBBReg(Adapter, 0x0df4, bMaskDWord)));

	PHY_SetBBReg(Adapter, 0x0908, 0xffff, 0x0100);
	PHY_SetBBReg(Adapter, 0x0a28, 0x00ff0000, 0x000f0000);
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0xdf4, PHY_QueryBBReg(Adapter, 0x0df4, bMaskDWord)));

	PHY_SetBBReg(Adapter, 0x0908, 0xffff, 0x0100);
	PHY_SetBBReg(Adapter, 0x0a28, 0x00ff0000, 0x00150000);
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0xdf4, PHY_QueryBBReg(Adapter, 0x0df4, bMaskDWord)));

	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0x800, PHY_QueryBBReg(Adapter, 0x0800, bMaskDWord)));
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0x900, PHY_QueryBBReg(Adapter, 0x0900, bMaskDWord)));
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0xa00, PHY_QueryBBReg(Adapter, 0x0a00, bMaskDWord)));
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0xa54, PHY_QueryBBReg(Adapter, 0x0a54, bMaskDWord)));
	//RT_TRACE(COMP_SEND, DBG_WARNING, ("Offset[%x]: %x\n", 0xa58, PHY_QueryBBReg(Adapter, 0x0a58, bMaskDWord)));

}
#endif

