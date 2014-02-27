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
/******************************************************************************

 Module:	rtl8192c_phycfg.c	

 Note:		Merge 92SE/SU PHY config as below
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
	08/08/2008  MHC    	1. Port from 9x series phycfg.c
						2. Reorganize code arch and ad description.
						3. Collect similar function.
						4. Seperate extern/local API.
	08/12/2008	MHC		We must merge or move USB PHY relative function later.
	10/07/2008	MHC		Add IQ calibration for PHY.(Only 1T2R mode now!!!)
	11/06/2008	MHC		Add TX Power index PG file to config in 0xExx register
						area to map with EEPROM/EFUSE tx pwr index.
	
******************************************************************************/
#define _HAL_8192C_PHYCFG_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>

#ifdef CONFIG_IOL
#include <rtw_iol.h>
#endif

#include <rtl8192c_hal.h>


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
rtl8192c_PHY_QueryBBReg(
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
rtl8192c_PHY_SetBBReg(
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

	//RT_TRACE(COMP_RF, DBG_TRACE, ("--->PHY_SetBBReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx)\n", RegAddr, BitMask, Data));

	if(BitMask!= bMaskDWord){//if not "double word" write
		OriginalValue = rtw_read32(Adapter, RegAddr);
		BitShift = phy_CalculateBitShift(BitMask);
		Data = ((OriginalValue & (~BitMask)) | ((Data << BitShift) & BitMask));
	}

	rtw_write32(Adapter, RegAddr, Data);

	//RTPRINT(FPHY, PHY_BBW, ("BBW MASK=0x%lx Addr[0x%lx]=0x%lx\n", BitMask, RegAddr, Data));
	//RT_TRACE(COMP_RF, DBG_TRACE, ("<---PHY_SetBBReg(): RegAddr(%#lx), BitMask(%#lx), Data(%#lx)\n", RegAddr, BitMask, Data));
	
}


//
// 2. RF register R/W API
//

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
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				Offset	)
{
	u32		retValue = 0;		
	//RT_ASSERT(FALSE,("deprecate!\n"));
	return	(retValue);

}	/* phy_FwRFSerialRead */


/*-----------------------------------------------------------------------------
 * Function:	phy_FwRFSerialWrite()
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
static	VOID
phy_FwRFSerialWrite(
	IN	PADAPTER			Adapter,
	IN	RF_RADIO_PATH_E	eRFPath,
	IN	u32				Offset,
	IN	u32				Data	)
{
	//RT_ASSERT(FALSE,("deprecate!\n"));
}


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
	Offset &= 0x3f;

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
	
	PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord, tmplong|bLSSIReadEdge);	
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

	Offset &= 0x3f;

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
rtl8192c_PHY_QueryRFReg(
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
rtl8192c_PHY_SetRFReg(
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
	
	int		rtStatus = _SUCCESS;

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
	ArrayLength = MAC_2T_ArrayLength;
	ptrArray = Rtl819XMAC_Array;

#ifdef CONFIG_IOL_MAC
	if(rtw_IOL_applied(Adapter))
	{
		struct xmit_frame	*xmit_frame;
		if((xmit_frame=rtw_IOL_accquire_xmit_frame(Adapter)) == NULL)
			return _FAIL;

		for(i = 0 ;i < ArrayLength;i=i+2){ // Add by tynli for 2 column
			rtw_IOL_append_WB_cmd(xmit_frame, ptrArray[i], (u8)ptrArray[i+1]);
		}

		return rtw_IOL_exec_cmds_sync(Adapter, xmit_frame, 1000);
	}
	else
#endif
	{
		for(i = 0 ;i < ArrayLength;i=i+2){ // Add by tynli for 2 column
			rtw_write8(Adapter, ptrArray[i], (u8)ptrArray[i+1]);
		}
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
int
PHY_MACConfig8192C(
	IN	PADAPTER	Adapter
	)
{
	int		rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	s8			*pszMACRegFile;
	s8			sz88CMACRegFile[] = RTL8188C_PHY_MACREG;
	s8			sz92CMACRegFile[] = RTL8192C_PHY_MACREG;
	BOOLEAN		is92C = IS_92C_SERIAL(pHalData->VersionID);

	if(is92C)
		pszMACRegFile = sz92CMACRegFile;
	else
		pszMACRegFile = sz88CMACRegFile;

	//
	// Config MAC
	//
#ifdef CONFIG_EMBEDDED_FWIMG
	rtStatus = phy_ConfigMACWithHeaderFile(Adapter);
#else
	
	// Not make sure EEPROM, add later
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Read MACREG.txt\n"));
	rtStatus = phy_ConfigMACWithParaFile(Adapter, pszMACRegFile);
#endif

#ifdef CONFIG_PCI_HCI
	//this switching setting cause some 8192cu hw have redownload fw fail issue
	//improve 2-stream TX EVM by Jenyu
	if(is92C)
		rtw_write8(Adapter, REG_SPS0_CTRL+3,0x71);
#endif


	// 2010.07.13 AMPDU aggregation number 9
	//rtw_write16(Adapter, REG_MAX_AGGR_NUM, MAX_AGGR_NUM);
	rtw_write8(Adapter, REG_MAX_AGGR_NUM, 0x0A); //By tynli. 2010.11.18.
#ifdef CONFIG_USB_HCI
	if(is92C && (BOARD_USB_DONGLE == pHalData->BoardType))
		rtw_write8(Adapter, 0x40,0x04);	
#endif		

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
	int ret = _SUCCESS;

	//
	// 2009.11.24. Modified by tynli.
	//
	if(IS_92C_SERIAL(pHalData->VersionID))
	{
		AGCTAB_ArrayLen = AGCTAB_2TArrayLength;
		Rtl819XAGCTAB_Array_Table = Rtl819XAGCTAB_2TArray;
		PHY_REGArrayLen = PHY_REG_2TArrayLength;
		Rtl819XPHY_REGArray_Table = Rtl819XPHY_REG_2TArray;
#ifdef CONFIG_USB_HCI
		if(pHalData->BoardType == BOARD_MINICARD )
		{
			PHY_REGArrayLen = PHY_REG_2T_mCardArrayLength;
			Rtl819XPHY_REGArray_Table = Rtl819XPHY_REG_2T_mCardArray;			
		}
#endif
	}
	else
	{
		AGCTAB_ArrayLen = AGCTAB_1TArrayLength;
		Rtl819XAGCTAB_Array_Table = Rtl819XAGCTAB_1TArray;
		PHY_REGArrayLen = PHY_REG_1TArrayLength;
		Rtl819XPHY_REGArray_Table = Rtl819XPHY_REG_1TArray;
#ifdef CONFIG_USB_HCI
		if(pHalData->BoardType == BOARD_MINICARD )
		{
			PHY_REGArrayLen = PHY_REG_1T_mCardArrayLength;
			Rtl819XPHY_REGArray_Table = Rtl819XPHY_REG_1T_mCardArray;			
		}
		else if(pHalData->BoardType == BOARD_USB_High_PA)
		{
			AGCTAB_ArrayLen = AGCTAB_1T_HPArrayLength;
			Rtl819XAGCTAB_Array_Table = Rtl819XAGCTAB_1T_HPArray;
			PHY_REGArrayLen = PHY_REG_1T_HPArrayLength;
			Rtl819XPHY_REGArray_Table = Rtl819XPHY_REG_1T_HPArray;			
		}
#endif
	}

	if(ConfigType == BaseBand_Config_PHY_REG)
	{
		#ifdef CONFIG_IOL_BB_PHY_REG
		if(rtw_IOL_applied(Adapter))
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

				rtw_IOL_append_WD_cmd(xmit_frame, Rtl819XPHY_REGArray_Table[i], tmp_value);	
				//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XPHY_REGArray_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XPHY_REGArray_Table[i], Rtl819XPHY_REGArray_Table[i+1]));
			}
		
			ret = rtw_IOL_exec_cmds_sync(Adapter, xmit_frame, 1000);
		}
		else
		#endif
		{
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

				PHY_SetBBReg(Adapter, Rtl819XPHY_REGArray_Table[i], bMaskDWord, Rtl819XPHY_REGArray_Table[i+1]);

				// Add 1us delay between BB/RF register setting.
				rtw_udelay_os(1);

				//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XPHY_REGArray_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XPHY_REGArray_Table[i], Rtl819XPHY_REGArray_Table[i+1]));
			}
		}
	
		// for External PA
		phy_ConfigBBExternalPA(Adapter);
	}
	else if(ConfigType == BaseBand_Config_AGC_TAB)
	{
		#ifdef CONFIG_IOL_BB_AGC_TAB
		if(rtw_IOL_applied(Adapter))
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
		else
		#endif
		{
			for(i=0;i<AGCTAB_ArrayLen;i=i+2)
			{
				PHY_SetBBReg(Adapter, Rtl819XAGCTAB_Array_Table[i], bMaskDWord, Rtl819XAGCTAB_Array_Table[i+1]);		

				// Add 1us delay between BB/RF register setting.
				rtw_udelay_os(1);
				
				//RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl819XAGCTAB_Array_Table[0] is %lx Rtl819XPHY_REGArray[1] is %lx \n",Rtl819XAGCTAB_Array_Table[i], Rtl819XAGCTAB_Array_Table[i+1]));
			}
		}
	}

exit:
	return ret;
	
}


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

	// Default: pHalData->RF_Type = RF_2T2R.
	PHY_REGArrayPGLen = PHY_REG_Array_PGLength;
	Rtl819XPHY_REGArray_Table_PG = Rtl819XPHY_REG_Array_PG;

#ifdef CONFIG_USB_HCI
// 2010/10/19 Chiyoko According to Alex/Willson opinion, VAU/dongle can share the same PHY_REG_PG.txt
/*
	if(pHalData->BoardType == BOARD_MINICARD )
	{
		PHY_REGArrayPGLen = PHY_REG_Array_PG_mCardLength;
		Rtl819XPHY_REGArray_Table_PG = Rtl819XPHY_REG_Array_PG_mCard;
	}
	else */if(pHalData->BoardType ==BOARD_USB_High_PA )
	{
		PHY_REGArrayPGLen = PHY_REG_Array_PG_HPLength;
		Rtl819XPHY_REGArray_Table_PG = Rtl819XPHY_REG_Array_PG_HP;
	}
#endif

	if(ConfigType == BaseBand_Config_PHY_REG)
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
			//RT_TRACE(COMP_SEND, DBG_TRACE, ("The Rtl819XPHY_REGArray_Table_PG[0] is %lx Rtl819XPHY_REGArray_Table_PG[1] is %lx \n",Rtl819XPHY_REGArray_Table_PG[i], Rtl819XPHY_REGArray_Table_PG[i+1]));
		}
	}
	else
	{

		//RT_TRACE(COMP_SEND, DBG_LOUD, ("phy_ConfigBBWithPgHeaderFile(): ConfigType != BaseBand_Config_PHY_REG\n"));
	}
	
	return _SUCCESS;
	
}	/* phy_ConfigBBWithPgHeaderFile */

#if (MP_DRIVER == 1)

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithMpParaFile()
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
phy_ConfigBBWithMpParaFile(
	IN	PADAPTER		Adapter,
	IN	u8* 			pFileName
)
{
#if 1
	int		rtStatus = _SUCCESS;
#else
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	s4Byte			nLinesRead, ithLine;
	RT_STATUS		rtStatus = RT_STATUS_SUCCESS;
	ps1Byte 		szLine;
	u4Byte			u4bRegOffset, u4bRegMask, u4bRegValue;
	u4Byte			u4bMove;
	
	if(ADAPTER_TEST_STATUS_FLAG(Adapter, ADAPTER_STATUS_FIRST_INIT))
	{
		rtStatus = PlatformReadFile(
					Adapter, 
					pFileName,
					(pu1Byte)(pHalData->BufOfLines),
					MAX_LINES_HWCONFIG_TXT,
					MAX_BYTES_LINE_HWCONFIG_TXT,
					&nLinesRead
					);
		if(rtStatus == RT_STATUS_SUCCESS)
		{
			PlatformMoveMemory(pHalData->BufOfLines6, pHalData->BufOfLines, nLinesRead*MAX_BYTES_LINE_HWCONFIG_TXT);
			pHalData->nLinesRead6 = nLinesRead;
		}
		else
		{
			// Temporarily skip PHY_REG_MP.txt if file does not exist.
			pHalData->nLinesRead6 = 0;
			RT_TRACE(COMP_INIT, DBG_LOUD, ("No matched file \r\n"));
			return RT_STATUS_SUCCESS;
		}
	}
	else
	{
		PlatformMoveMemory(pHalData->BufOfLines, pHalData->BufOfLines6, MAX_LINES_HWCONFIG_TXT*MAX_BYTES_LINE_HWCONFIG_TXT);
		nLinesRead = pHalData->nLinesRead6;
		rtStatus = RT_STATUS_SUCCESS;
	}


	if(rtStatus == RT_STATUS_SUCCESS)
	{
		RT_TRACE(COMP_INIT, DBG_LOUD, ("phy_ConfigBBWithMpParaFile(): read %s ok\n", pFileName));

		for(ithLine = 0; ithLine < nLinesRead; ithLine++)
		{
			szLine = pHalData->BufOfLines[ithLine];

			if(!IsCommentString(szLine))
			{
				// Get 1st hex value as register offset.
				if(GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove))
				{
					if(u4bRegOffset == 0xff)
					{ // Ending.
						break;
					}
					else if (u4bRegOffset == 0xfe)
						delay_ms(50);
					else if (u4bRegOffset == 0xfd)
						delay_ms(5);
					else if (u4bRegOffset == 0xfc)
						delay_ms(1);
					else if (u4bRegOffset == 0xfb)
						PlatformStallExecution(50);
					else if (u4bRegOffset == 0xfa)
						PlatformStallExecution(5);
					else if (u4bRegOffset == 0xf9)
						PlatformStallExecution(1);
					
					// Get 2nd hex value as register value.
					szLine += u4bMove;
					if(GetHexValueFromString(szLine, &u4bRegValue, &u4bMove))
					{
						RT_TRACE(COMP_FPGA, DBG_TRACE, ("[ADDR]%03lX=%08lX\n", u4bRegOffset, u4bRegValue));
						PHY_SetBBReg(Adapter, u4bRegOffset, bMaskDWord, u4bRegValue);

						// Add 1us delay between BB/RF register setting.
						PlatformStallExecution(1);
					}
				}
			}
		}
	}
	else
	{
		RT_TRACE(COMP_INIT, DBG_LOUD, ("phy_ConfigBBWithMpParaFile(): Failed%s\n", pFileName));
	}
#endif

	return rtStatus;
}

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
static int
phy_ConfigBBWithMpHeaderFile(
	IN	PADAPTER		Adapter,
	IN	u1Byte 			ConfigType)
{
	int i;
	u32*	Rtl8192CPHY_REGArray_Table_MP;
	u16	PHY_REGArrayMPLen;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	PHY_REGArrayMPLen = PHY_REG_Array_MPLength;
	Rtl8192CPHY_REGArray_Table_MP = Rtl819XPHY_REG_Array_MP;

	if(ConfigType == BaseBand_Config_PHY_REG)
	{
		for(i=0;i<PHY_REGArrayMPLen;i=i+2)
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
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfb) {
				#ifdef CONFIG_LONG_DELAY_ISSUE
				rtw_msleep_os(50);
				#else
				rtw_mdelay_os(50);
				#endif
			}
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xfa)
				rtw_mdelay_os(5);
			else if (Rtl8192CPHY_REGArray_Table_MP[i] == 0xf9)
				rtw_mdelay_os(1);
			PHY_SetBBReg(Adapter, Rtl8192CPHY_REGArray_Table_MP[i], bMaskDWord, Rtl8192CPHY_REGArray_Table_MP[i+1]);		

			// Add 1us delay between BB/RF register setting.
			rtw_mdelay_os(1);

//			RT_TRACE(COMP_INIT, DBG_TRACE, ("The Rtl8192CPHY_REGArray_Table_MP[%d] is %lx Rtl8192CPHY_REGArray_Table_MP[%d] is %lx \n", i, i+1, Rtl8192CPHY_REGArray_Table_MP[i], Rtl8192CPHY_REGArray_Table_MP[i+1]));
		}
	}
	else
	{
//		RT_TRACE(COMP_SEND, DBG_LOUD, ("phy_ConfigBBWithMpHeaderFile(): ConfigType != BaseBand_Config_PHY_REG\n"));
	}

	return _SUCCESS;
}	/* phy_ConfigBBWithMpHeaderFile */

#endif	// #if (MP_DRIVER == 1)

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
phy_BB8192C_Config_ParaFile(
	IN	PADAPTER	Adapter
	)
{
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	int			rtStatus = _SUCCESS;

	u8				szBBRegPgFile[] = RTL819X_PHY_REG_PG;
	
	u8				sz88CBBRegFile[] = RTL8188C_PHY_REG;	
	u8				sz88CAGCTableFile[] = RTL8188C_AGC_TAB;

	u8				sz92CBBRegFile[] = RTL8192C_PHY_REG;	
	u8				sz92CAGCTableFile[] = RTL8192C_AGC_TAB;
	
	u8				*pszBBRegFile, *pszAGCTableFile, *pszBBRegMpFile;
	
	//RT_TRACE(COMP_INIT, DBG_TRACE, ("==>phy_BB8192S_Config_ParaFile\n"));

	if(IS_92C_SERIAL(pHalData->VersionID)){
		pszBBRegFile=(u8*)&sz92CBBRegFile ;
		pszAGCTableFile =(u8*)&sz92CAGCTableFile;
	}
	else{
		pszBBRegFile=(u8*)&sz88CBBRegFile ;
		pszAGCTableFile =(u8*)&sz88CAGCTableFile;
	}

	//
	// 1. Read PHY_REG.TXT BB INIT!!
	// We will seperate as 88C / 92C according to chip version
	//
#ifdef CONFIG_EMBEDDED_FWIMG
	rtStatus = phy_ConfigBBWithHeaderFile(Adapter, BaseBand_Config_PHY_REG);	
#else	
	// No matter what kind of CHIP we always read PHY_REG.txt. We must copy different 
	// type of parameter files to phy_reg.txt at first.	
	rtStatus = phy_ConfigBBWithParaFile(Adapter,pszBBRegFile);
#endif

	if(rtStatus != _SUCCESS){
		//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("phy_BB8192S_Config_ParaFile():Write BB Reg Fail!!"));
		goto phy_BB8190_Config_ParaFile_Fail;
	}

#if MP_DRIVER == 1
	//
	// 1.1 Read PHY_REG_MP.TXT BB INIT!!
	// We will seperate as 88C / 92C according to chip version
	//
#ifdef CONFIG_EMBEDDED_FWIMG
	rtStatus = phy_ConfigBBWithMpHeaderFile(Adapter, BaseBand_Config_PHY_REG);	
#else	
	// No matter what kind of CHIP we always read PHY_REG.txt. We must copy different 
	// type of parameter files to phy_reg.txt at first.	
	rtStatus = phy_ConfigBBWithMpParaFile(Adapter, pszBBRegMpFile);
#endif

	if(rtStatus != _SUCCESS){
//		RT_TRACE(COMP_INIT, DBG_SERIOUS, ("phy_BB8192S_Config_ParaFile():Write BB Reg MP Fail!!"));
		goto phy_BB8190_Config_ParaFile_Fail;
	}	
#endif	// #if (MP_DRIVER == 1)

	//
	// 20100318 Joseph: Config 2T2R to 1T2R if necessary.
	//
	if(pHalData->rf_type == RF_1T2R)
	{
		phy_BB8192C_Config_1T(Adapter);
		DBG_8192C("phy_BB8192C_Config_ParaFile():Config to 1T!!\n");
	}

	//
	// 2. If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt
	//
	if (pEEPROM->bautoload_fail_flag == _FALSE)
	{
		pHalData->pwrGroupCnt = 0;

#ifdef CONFIG_EMBEDDED_FWIMG
		rtStatus = phy_ConfigBBWithPgHeaderFile(Adapter, BaseBand_Config_PHY_REG);
#else
		rtStatus = phy_ConfigBBWithPgParaFile(Adapter, (u8*)&szBBRegPgFile);
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
	rtStatus = phy_ConfigBBWithHeaderFile(Adapter, BaseBand_Config_AGC_TAB);
#else
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("phy_BB8192S_Config_ParaFile AGC_TAB.txt\n"));
	rtStatus = phy_ConfigBBWithParaFile(Adapter, pszAGCTableFile);
#endif

	if(rtStatus != _SUCCESS){
		//RT_TRACE(COMP_FPGA, DBG_SERIOUS, ("phy_BB8192S_Config_ParaFile():AGC Table Fail\n"));
		goto phy_BB8190_Config_ParaFile_Fail;
	}

	// Check if the CCK HighPower is turned ON.
	// This is used to calculate PWDB.
	pHalData->bCckHighPower = (BOOLEAN)(PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter2, 0x200));
	
phy_BB8190_Config_ParaFile_Fail:

	return rtStatus;
}


int
PHY_BBConfig8192C(
	IN	PADAPTER	Adapter
	)
{
	int	rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32	RegVal;
	u8	TmpU1B=0;
	u8	value8;

	phy_InitBBRFRegisterDefinition(Adapter);

	if(IS_HARDWARE_TYPE_8723A(Adapter))
	{
		// Suggested by Scott. tynli_test. 2010.12.30.
		//1. 0x28[1] = 1
		TmpU1B = rtw_read8(Adapter, REG_AFE_PLL_CTRL);
		rtw_udelay_os(2);
		rtw_write8(Adapter, REG_AFE_PLL_CTRL, (TmpU1B|BIT1));
		rtw_udelay_os(2);
		
		//2. 0x29[7:0] = 0xFF
		rtw_write8(Adapter, REG_AFE_PLL_CTRL+1, 0xff);
		rtw_udelay_os(2);
		
		//3. 0x02[1:0] = 2b'11
		TmpU1B = rtw_read8(Adapter, REG_SYS_FUNC_EN);
		rtw_write8(Adapter, REG_SYS_FUNC_EN, (TmpU1B|FEN_BB_GLB_RSTn|FEN_BBRSTB));
		
		//undo clock gated
		rtw_write32(Adapter, rFPGA0_XCD_RFParameter, rtw_read32(Adapter, rFPGA0_XCD_RFParameter)&(~BIT31));
		
		//4. 0x25[6] = 0
		TmpU1B = rtw_read8(Adapter, REG_AFE_XTAL_CTRL+1);
		rtw_write8(Adapter, REG_AFE_XTAL_CTRL+1, (TmpU1B&(~BIT6)));
		
		//5. 0x24[20] = 0 	//Advised by SD3 Alex Wang. 2011.02.09.
		TmpU1B = rtw_read8(Adapter, REG_AFE_XTAL_CTRL+2);
		rtw_write8(Adapter, REG_AFE_XTAL_CTRL+2, (TmpU1B&(~BIT4)));
		
		//6. 0x1f[7:0] = 0x07
		rtw_write8(Adapter, REG_RF_CTRL, 0x07);
	}
	else
	{
		// Enable BB and RF
		RegVal = rtw_read16(Adapter, REG_SYS_FUNC_EN);
		rtw_write16(Adapter, REG_SYS_FUNC_EN, (u16)(RegVal|BIT13|BIT0|BIT1));

		// 20090923 Joseph: Advised by Steven and Jenyu. Power sequence before init RF.
		rtw_write8(Adapter, REG_AFE_PLL_CTRL, 0x83);
		rtw_write8(Adapter, REG_AFE_PLL_CTRL+1, 0xdb);

		rtw_write8(Adapter, REG_RF_CTRL, RF_EN|RF_RSTB|RF_SDMRSTB);

#ifdef CONFIG_USB_HCI
		rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_USBA | FEN_USBD | FEN_BB_GLB_RSTn | FEN_BBRSTB);
#else
		rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_PPLL|FEN_PCIEA|FEN_DIO_PCIE|FEN_BB_GLB_RSTn|FEN_BBRSTB);
#endif
		
		//undo clock gated
		rtw_write32(Adapter, rFPGA0_XCD_RFParameter, rtw_read32(Adapter, rFPGA0_XCD_RFParameter)&(~BIT31));

		// 2009/10/21 by SD1 Jong. Modified by tynli. Not in Documented in V8.1. 
#ifdef CONFIG_USB_HCI
		//To Fix MAC loopback mode fail. Suggested by SD4 Johnny. 2010.03.23.
		rtw_write8(Adapter, REG_LDOHCI12_CTRL, 0x0f);	
		rtw_write8(Adapter, 0x15, 0xe9);
#endif

		rtw_write8(Adapter, REG_AFE_XTAL_CTRL+1, 0x80);

#ifdef CONFIG_PCI_HCI
		// Force use left antenna by default for 88C.
	//	if(!IS_92C_SERIAL(pHalData->VersionID) || IS_92C_1T2R(pHalData->VersionID))
		if(Adapter->ledpriv.LedStrategy != SW_LED_MODE10)
		{
			RegVal = rtw_read32(Adapter, REG_LEDCFG0);
			rtw_write32(Adapter, REG_LEDCFG0, RegVal|BIT23);
		}
#endif
	}

	//
	// Config BB and AGC
	//
	rtStatus = phy_BB8192C_Config_ParaFile(Adapter);
#if 0	
	switch(Adapter->MgntInfo.bRegHwParaFile)
	{
		case 0:
			phy_BB8190_Config_HardCode(Adapter);
			break;

		case 1:
			rtStatus = phy_BB8192C_Config_ParaFile(Adapter);
			break;

		case 2:
			// Partial Modify. 
			phy_BB8190_Config_HardCode(Adapter);
			phy_BB8192C_Config_ParaFile(Adapter);
			break;

		default:
			phy_BB8190_Config_HardCode(Adapter);
			break;
	}
#endif	
#ifdef CONFIG_USB_HCI
	if(IS_HARDWARE_TYPE_8192CU(Adapter)&&IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID)
		&&(pHalData->BoardType == BOARD_USB_High_PA))
			rtw_write8(Adapter, 0xc72, 0x50);		
#endif

	return rtStatus;
}


int
PHY_RFConfig8192C(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;

	//
	// RF config
	//
	rtStatus = PHY_RF6052_Config8192C(Adapter);
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
rtl8192c_PHY_ConfigRFWithParaFile(
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
int
rtl8192c_PHY_ConfigRFWithHeaderFile(
	IN	PADAPTER			Adapter,
	RF_RADIO_PATH_E		eRFPath
)
{

	int			i;
	int			rtStatus = _SUCCESS;
	u32*		Rtl819XRadioA_Array_Table;
	u32*		Rtl819XRadioB_Array_Table;
	u16		RadioA_ArrayLen,RadioB_ArrayLen;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	//
	// 2009.11.24. Modified by tynli.
	//
	if(IS_92C_SERIAL(pHalData->VersionID))
	{
		RadioA_ArrayLen = RadioA_2TArrayLength;
		Rtl819XRadioA_Array_Table = Rtl819XRadioA_2TArray;
		RadioB_ArrayLen = RadioB_2TArrayLength;
		Rtl819XRadioB_Array_Table = Rtl819XRadioB_2TArray;
	}
	else
	{
		RadioA_ArrayLen = RadioA_1TArrayLength;
		Rtl819XRadioA_Array_Table = Rtl819XRadioA_1TArray;
		RadioB_ArrayLen = RadioB_1TArrayLength;	
		Rtl819XRadioB_Array_Table = Rtl819XRadioB_1TArray;
#ifdef CONFIG_USB_HCI
		if( BOARD_MINICARD == pHalData->BoardType )
		{
			RadioA_ArrayLen = RadioA_1T_mCardArrayLength;
			Rtl819XRadioA_Array_Table = Rtl819XRadioA_1T_mCardArray;
			RadioB_ArrayLen = RadioB_1T_mCardArrayLength;	
			Rtl819XRadioB_Array_Table = Rtl819XRadioB_1T_mCardArray;			
		}
		else if( BOARD_USB_High_PA == pHalData->BoardType )
		{
			RadioA_ArrayLen = RadioA_1T_HPArrayLength;
			Rtl819XRadioA_Array_Table = Rtl819XRadioA_1T_HPArray;
		}
#endif
	}

	switch(eRFPath){
		case RF_PATH_A:
			#ifdef CONFIG_IOL_RF_RF90_PATH_A
			if(rtw_IOL_applied(Adapter))
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
			else
			#endif
			{
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
			}

			//Add for High Power PA
			PHY_ConfigRFExternalPA(Adapter, eRFPath);
			break;
		case RF_PATH_B:
			#ifdef CONFIG_IOL_RF_RF90_PATH_B
			if(rtw_IOL_applied(Adapter))
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
			else
			#endif
			{
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
			}

			break;
		case RF_PATH_C:
			break;
		case RF_PATH_D:
			break;
	}

exit:	
	return rtStatus;

}


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
PHY_GetTxPowerLevel8192C(
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

static void ccxPowerIndexCheck(
	IN	PADAPTER		Adapter,
	IN	u8			channel,
	IN OUT u8*		cckPowerLevel,
	IN OUT u8*		ofdmPowerLevel
	)
{
#if 0
	PMGNT_INFO			pMgntInfo = &(Adapter->MgntInfo);
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
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

	pHalData->CurrentCckTxPwrIdx = cckPowerLevel[0];
	pHalData->CurrentOfdm24GTxPwrIdx = ofdmPowerLevel[0];

	RT_TRACE(COMP_TXAGC, DBG_LOUD, 
		("PHY_SetTxPowerLevel8192S(): CCK Tx power index : %d, Legacy OFDM Tx power index: %d, OFDM Tx power index: %d\n", 
		cckPowerLevel[0], ofdmPowerLevel[0] + pHalData->LegacyHTTxPowerDiff, ofdmPowerLevel[0]));
#endif	
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
PHY_SetTxPowerLevel8192C(
	IN	PADAPTER		Adapter,
	IN	u8			channel
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8	cckPowerLevel[2], ofdmPowerLevel[2];	// [0]:RF-A, [1]:RF-B

#if(MP_DRIVER == 1)
	return;
#endif

	if(pHalData->bTXPowerDataReadFromEEPORM == _FALSE)
		return;

	getTxPowerIndex(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0]);
	//RTPRINT(FPHY, PHY_TXPWR, ("Channel-%d, cckPowerLevel (A / B) = 0x%x / 0x%x,   ofdmPowerLevel (A / B) = 0x%x / 0x%x\n", 
	//	channel, cckPowerLevel[0], cckPowerLevel[1], ofdmPowerLevel[0], ofdmPowerLevel[1]));

	ccxPowerIndexCheck(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0]);

	rtl8192c_PHY_RF6052SetCckTxPower(Adapter, &cckPowerLevel[0]);
	rtl8192c_PHY_RF6052SetOFDMTxPower(Adapter, &ofdmPowerLevel[0], channel);

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
PHY_UpdateTxPowerDbm8192C(
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
PHY_ScanOperationBackup8192C(
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
			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 1);
			
			break;


		/* 40 MHz channel*/
		case HT_CHANNEL_WIDTH_40:
			PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x1);
			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x1);
			
			// Set Control channel to upper or lower. These settings are required only for 40MHz
			PHY_SetBBReg(Adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC>>1));
			PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);
			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 0);

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
			rtl8192c_PHY_RF6052SetBandwidth(Adapter, pHalData->CurrentChannelBW);
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
PHY_SetBWMode8192C(
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
#ifdef USE_WORKITEM	
		//PlatformScheduleWorkItem(&(pHalData->SetBWModeWorkItem));
#else
	#if 0
		//PlatformSetTimer(Adapter, &(pHalData->SetBWModeTimer), 0);
	#else
		_PHY_SetBWMode92C(Adapter);
	#endif
#endif		
	}
	else
	{
		//RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SetBWMode8192C() SetBWModeInProgress FALSE driver sleep or unload\n"));	
		//pHalData->SetBWModeInProgress= FALSE;	
		pHalData->CurrentChannelBW = tmpBW;
	}
	
}


static void _PHY_SwChnl8192C(PADAPTER Adapter, u8 channel)
{
	u8 eRFPath;
	u32 param1, param2;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if ( Adapter->bNotifyChannelChange )
	{
		DBG_871X( "[%s] ch = %d\n", __FUNCTION__, channel );
	}

	//s1. pre common command - CmdID_SetTxPowerLevel
	PHY_SetTxPowerLevel8192C(Adapter, channel);

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
PHY_SwChnl8192C(	// Call after initialization
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
#ifdef USE_WORKITEM	
		//bResult = PlatformScheduleWorkItem(&(pHalData->SwChnlWorkItem));
#else
		#if 0		
		//PlatformSetTimer(Adapter, &(pHalData->SwChnlTimer), 0);
		#else
		_PHY_SwChnl8192C(Adapter, channel);
		#endif
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

//-------------------------------------------------------------------------
//
//	IQK
//
//-------------------------------------------------------------------------
#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1 	//ms

static u8			//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
_PHY_PathA_IQK(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		configPathB
	)
{
	u32 regEAC, regE94, regE9C, regEA4;
	u8 result = 0x00;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	//RTPRINT(FINIT, INIT_IQK, ("Path A IQK!\n"));

	//path-A IQK setting
	//RTPRINT(FINIT, INIT_IQK, ("Path-A IQK setting!\n"));
	PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
	PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
	PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x82140102);

	PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, configPathB ? 0x28160202 : 
		IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID)?0x28160202:0x28160502);

	//path-B IQK setting
	if(configPathB)
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_B, bMaskDWord, 0x82140102);
		PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, 0x28160202);
	}

	//LO calibration setting
	//RTPRINT(FINIT, INIT_IQK, ("LO calibration setting!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x001028d1);

	//One shot, path A LOK & IQK
	//RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	// delay x ms
	//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME));
	rtw_udelay_os(IQK_DELAY_TIME*1000);//PlatformStallExecution(IQK_DELAY_TIME*1000);

	// Check failed
	regEAC = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%x\n", regEAC));
	regE94 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xe94 = 0x%x\n", regE94));
	regE9C= PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xe9c = 0x%x\n", regE9C));
	regEA4= PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xea4 = 0x%x\n", regEA4));

        if(!(regEAC & BIT28) &&		
		(((regE94 & 0x03FF0000)>>16) != 0x142) &&
		(((regE9C & 0x03FF0000)>>16) != 0x42) )
		result |= 0x01;
	else							//if Tx not OK, ignore Rx
		return result;

	if(!(regEAC & BIT27) &&		//if Tx is OK, check whether Rx is OK
		(((regEA4 & 0x03FF0000)>>16) != 0x132) &&
		(((regEAC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		DBG_8192C("Path A Rx IQK fail!!\n");
	
	return result;


}

static u8				//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
_PHY_PathB_IQK(
	IN	PADAPTER	pAdapter
	)
{
	u32 regEAC, regEB4, regEBC, regEC4, regECC;
	u8	result = 0x00;
	//RTPRINT(FINIT, INIT_IQK, ("Path B IQK!\n"));

	//One shot, path B LOK & IQK
	//RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	// delay x ms
	//RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for One shot, path B LOK & IQK.\n", IQK_DELAY_TIME));
	rtw_udelay_os(IQK_DELAY_TIME*1000);//PlatformStallExecution(IQK_DELAY_TIME*1000);

	// Check failed
	regEAC = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%x\n", regEAC));
	regEB4 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xeb4 = 0x%x\n", regEB4));
	regEBC= PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xebc = 0x%x\n", regEBC));
	regEC4= PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xec4 = 0x%x\n", regEC4));
	regECC= PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord);
	//RTPRINT(FINIT, INIT_IQK, ("0xecc = 0x%x\n", regECC));

	if(!(regEAC & BIT31) &&
		(((regEB4 & 0x03FF0000)>>16) != 0x142) &&
		(((regEBC & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	else
		return result;

	if(!(regEAC & BIT30) &&
		(((regEC4 & 0x03FF0000)>>16) != 0x132) &&
		(((regECC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		DBG_8192C("Path B Rx IQK fail!!\n");
	

	return result;

}

static VOID
_PHY_PathAFillIQKMatrix(
	IN	PADAPTER	pAdapter,
	IN 	BOOLEAN    	bIQKOK,
	IN	int			result[][8],
	IN	u8			final_candidate,
	IN 	BOOLEAN		bTxOnly
	)
{
	u32	Oldval_0, X, TX0_A, reg;
	s32	Y, TX0_C;

	DBG_8192C("Path A IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed");

	if(final_candidate == 0xFF)
		return;
	else if(bIQKOK)
	{
		Oldval_0 = (PHY_QueryBBReg(pAdapter, rOFDM0_XATxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX0_A = (X * Oldval_0) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("X = 0x%lx, TX0_A = 0x%lx, Oldval_0 0x%lx\n", X, TX0_A, Oldval_0));
		PHY_SetBBReg(pAdapter, rOFDM0_XATxIQImbalance, 0x3FF, TX0_A);
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(31), ((X* Oldval_0>>7) & 0x1));

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;
		TX0_C = (Y * Oldval_0) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("Y = 0x%lx, TX = 0x%lx\n", Y, TX0_C));
		PHY_SetBBReg(pAdapter, rOFDM0_XCTxAFE, 0xF0000000, ((TX0_C&0x3C0)>>6));
		PHY_SetBBReg(pAdapter, rOFDM0_XATxIQImbalance, 0x003F0000, (TX0_C&0x3F));
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(29), ((Y* Oldval_0>>7) & 0x1));

	        if(bTxOnly)
		{
			DBG_8192C("_PHY_PathAFillIQKMatrix only Tx OK\n");
			return;
		}

		reg = result[final_candidate][2];
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		PHY_SetBBReg(pAdapter, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
}

static VOID
_PHY_PathBFillIQKMatrix(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN   	bIQKOK,
	IN	int			result[][8],
	IN	u8			final_candidate,
	IN	BOOLEAN		bTxOnly			//do Tx only
	)
{
	u32	Oldval_1, X, TX1_A, reg;
	s32	Y, TX1_C;
	
	DBG_8192C("Path B IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed");

	if(final_candidate == 0xFF)
		return;
	else if(bIQKOK)
	{
		Oldval_1 = (PHY_QueryBBReg(pAdapter, rOFDM0_XBTxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;		
		TX1_A = (X * Oldval_1) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("X = 0x%lx, TX1_A = 0x%lx\n", X, TX1_A));
		PHY_SetBBReg(pAdapter, rOFDM0_XBTxIQImbalance, 0x3FF, TX1_A);
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(27), ((X* Oldval_1>>7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;		
		TX1_C = (Y * Oldval_1) >> 8;
		//RTPRINT(FINIT, INIT_IQK, ("Y = 0x%lx, TX1_C = 0x%lx\n", Y, TX1_C));
		PHY_SetBBReg(pAdapter, rOFDM0_XDTxAFE, 0xF0000000, ((TX1_C&0x3C0)>>6));
		PHY_SetBBReg(pAdapter, rOFDM0_XBTxIQImbalance, 0x003F0000, (TX1_C&0x3F));
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(25), ((Y* Oldval_1>>7) & 0x1));

		if(bTxOnly)
			return;

		reg = result[final_candidate][6];
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][7] & 0x3F;
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		PHY_SetBBReg(pAdapter, rOFDM0_AGCRSSITable, 0x0000F000, reg);
	}
}

static VOID
_PHY_SaveADDARegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		ADDAReg,
	IN	u32*		ADDABackup,
	IN	u32			RegisterNum
	)
{
	u32	i;
	
	//RTPRINT(FINIT, INIT_IQK, ("Save ADDA parameters.\n"));
	for( i = 0 ; i < RegisterNum ; i++){
		ADDABackup[i] = PHY_QueryBBReg(pAdapter, ADDAReg[i], bMaskDWord);
	}
}

static VOID
_PHY_SaveMACRegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		MACReg,
	IN	u32*		MACBackup
	)
{
	u32	i;
	
	//RTPRINT(FINIT, INIT_IQK, ("Save MAC parameters.\n"));
	for( i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++){
		MACBackup[i] =rtw_read8(pAdapter, MACReg[i]);		
	}
	MACBackup[i] = rtw_read32(pAdapter, MACReg[i]);		

}

static VOID
_PHY_ReloadADDARegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		ADDAReg,
	IN	u32*		ADDABackup,
	IN	u32			RegiesterNum
	)
{
	u32	i;

	//RTPRINT(FINIT, INIT_IQK, ("Reload ADDA power saving parameters !\n"));
	for(i = 0 ; i < RegiesterNum ; i++){
		PHY_SetBBReg(pAdapter, ADDAReg[i], bMaskDWord, ADDABackup[i]);
	}
}

static VOID
_PHY_ReloadMACRegisters(
	IN	PADAPTER	pAdapter,
	IN	u32*		MACReg,
	IN	u32*		MACBackup
	)
{
	u32	i;

	//RTPRINT(FINIT, INIT_IQK, ("Reload MAC parameters !\n"));
	for(i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++){
		rtw_write8(pAdapter, MACReg[i], (u8)MACBackup[i]);
	}
	rtw_write32(pAdapter, MACReg[i], MACBackup[i]);	
}

static VOID
_PHY_PathADDAOn(
	IN	PADAPTER	pAdapter,
	IN	u32*		ADDAReg,
	IN	BOOLEAN		isPathAOn,
	IN	BOOLEAN		is2T
	)
{
	u32	pathOn;
	u32	i;

	//RTPRINT(FINIT, INIT_IQK, ("ADDA ON.\n"));

	pathOn = isPathAOn ? 0x04db25a4 : 0x0b1b25a4;
	if(_FALSE == is2T){
		pathOn = 0x0bdb25a0;
		PHY_SetBBReg(pAdapter, ADDAReg[0], bMaskDWord, 0x0b1b25a0);
	}
	else{
		PHY_SetBBReg(pAdapter, ADDAReg[0], bMaskDWord, pathOn);
	}
	
	for( i = 1 ; i < IQK_ADDA_REG_NUM ; i++){
		PHY_SetBBReg(pAdapter, ADDAReg[i], bMaskDWord, pathOn);
	}
	
}

static VOID
_PHY_MACSettingCalibration(
	IN	PADAPTER	pAdapter,
	IN	u32*		MACReg,
	IN	u32*		MACBackup	
	)
{
	u32	i = 0;

	//RTPRINT(FINIT, INIT_IQK, ("MAC settings for Calibration.\n"));

	rtw_write8(pAdapter, MACReg[i], 0x3F);

	for(i = 1 ; i < (IQK_MAC_REG_NUM - 1); i++){
		rtw_write8(pAdapter, MACReg[i], (u8)(MACBackup[i]&(~BIT3)));
	}
	rtw_write8(pAdapter, MACReg[i], (u8)(MACBackup[i]&(~BIT5)));	

}

static VOID
_PHY_PathAStandBy(
	IN	PADAPTER	pAdapter
	)
{
	//RTPRINT(FINIT, INIT_IQK, ("Path-A standby mode!\n"));

	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x0);
	PHY_SetBBReg(pAdapter, 0x840, bMaskDWord, 0x00010000);
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
}

static VOID
_PHY_PIModeSwitch(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		PIMode
	)
{
	u32	mode;

	//RTPRINT(FINIT, INIT_IQK, ("BB Switch to %s mode!\n", (PIMode ? "PI" : "SI")));

	mode = PIMode ? 0x01000100 : 0x01000000;
	PHY_SetBBReg(pAdapter, 0x820, bMaskDWord, mode);
	PHY_SetBBReg(pAdapter, 0x828, bMaskDWord, mode);
}

/*
return _FALSE => do IQK again
*/
static BOOLEAN							
_PHY_SimularityCompare(
	IN	PADAPTER	pAdapter,
	IN	int 		result[][8],
	IN	u8		 c1,
	IN	u8		 c2
	)
{
	u32		i, j, diff, SimularityBitMap, bound = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	u8		final_candidate[2] = {0xFF, 0xFF};	//for path A and path B
	BOOLEAN		bResult = _TRUE, is2T = IS_92C_SERIAL( pHalData->VersionID);
	
	if(is2T)
		bound = 8;
	else
		bound = 4;

	SimularityBitMap = 0;
	
	for( i = 0; i < bound; i++ )
	{
		diff = (result[c1][i] > result[c2][i]) ? (result[c1][i] - result[c2][i]) : (result[c2][i] - result[c1][i]);
		if (diff > MAX_TOLERANCE)
		{
			if((i == 2 || i == 6) && !SimularityBitMap)
			{
				if(result[c1][i]+result[c1][i+1] == 0)
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
	
	if ( SimularityBitMap == 0)
	{
		for( i = 0; i < (bound/4); i++ )
		{
			if(final_candidate[i] != 0xFF)
			{
				for( j = i*4; j < (i+1)*4-2; j++)
					result[3][j] = result[final_candidate[i]][j];
				bResult = _FALSE;
			}
		}
		return bResult;
	}
	else if (!(SimularityBitMap & 0x0F))			//path A OK
	{
		for(i = 0; i < 4; i++)
			result[3][i] = result[c1][i];
		return _FALSE;
	}
	else if (!(SimularityBitMap & 0xF0) && is2T)	//path B OK
	{
		for(i = 4; i < 8; i++)
			result[3][i] = result[c1][i];
		return _FALSE;
	}	
	else		
		return _FALSE;
	
}

static VOID	
_PHY_IQCalibrate(
	IN	PADAPTER	pAdapter,
	IN	int 		result[][8],
	IN	u8		t,
	IN	BOOLEAN		is2T
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			i;
	u8			PathAOK, PathBOK;
	u32			ADDA_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };

	u32			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u32			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
							rOFDM0_TRxPathEnable, 		rOFDM0_TRMuxPar,	
							rFPGA0_XCD_RFInterfaceSW,	rConfig_AntA,	rConfig_AntB,
							rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,	
							rFPGA0_XB_RFInterfaceOE,	rFPGA0_RFMOD	
							};

#if MP_DRIVER
	const u32	retryCount = 9;
#else
	const u32	retryCount = 2;
#endif

	// Note: IQ calibration must be performed after loading 
	// 		PHY_REG.txt , and radio_a, radio_b.txt	
	
	u32 bbvalue;

	if(t==0)
	{
	 	bbvalue = PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bMaskDWord);
		//RTPRINT(FINIT, INIT_IQK, ("PHY_IQCalibrate()==>0x%08lx\n",bbvalue));

		//RTPRINT(FINIT, INIT_IQK, ("IQ Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));
	
	 	// Save ADDA parameters, turn Path A ADDA on
	 	_PHY_SaveADDARegisters(pAdapter, ADDA_REG, pdmpriv->ADDA_backup,IQK_ADDA_REG_NUM);
		_PHY_SaveMACRegisters(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);
		_PHY_SaveADDARegisters(pAdapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM);
	}
 	_PHY_PathADDAOn(pAdapter, ADDA_REG, _TRUE, is2T);

	if(t==0)
	{
		pdmpriv->bRfPiEnable = (u8)PHY_QueryBBReg(pAdapter, rFPGA0_XA_HSSIParameter1, BIT(8));
	}

	if(!pdmpriv->bRfPiEnable){
		// Switch BB to PI mode to do IQ Calibration.
		_PHY_PIModeSwitch(pAdapter, _TRUE);
	}

	PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, BIT24, 0x00);
	PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	PHY_SetBBReg(pAdapter, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22204000);
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0x01);
	PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0x01);
	PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, BIT10, 0x00);
	PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0x00);

	if(is2T)
	{
		PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00010000);
		PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00010000);
	}
	
	//MAC settings
	_PHY_MACSettingCalibration(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	//Page B init
	PHY_SetBBReg(pAdapter, rConfig_AntA, bMaskDWord, 0x00080000);
	
	if(is2T)
	{
		PHY_SetBBReg(pAdapter, rConfig_AntB, bMaskDWord, 0x00080000);
	}
	
	// IQ calibration setting
	//RTPRINT(FINIT, INIT_IQK, ("IQK setting!\n"));		
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(pAdapter, rTx_IQK, bMaskDWord, 0x01007c00);
	PHY_SetBBReg(pAdapter, rRx_IQK, bMaskDWord, 0x01004800);

	for(i = 0 ; i < retryCount ; i++){
		PathAOK = _PHY_PathA_IQK(pAdapter, is2T);
		if(PathAOK == 0x03){
				DBG_8192C("Path A IQK Success!!\n");
				result[t][0] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][1] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][2] = (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][3] = (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			break;
		}
		else if (i == (retryCount-1) && PathAOK == 0x01)	//Tx IQK OK
		{
			DBG_8192C("Path A IQK Only  Tx Success!!\n");
			
			result[t][0] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;			
		}
	}

	if(0x00 == PathAOK){		
		DBG_8192C("Path A IQK failed!!\n");
	}

	if(is2T){
		_PHY_PathAStandBy(pAdapter);

		// Turn Path B ADDA on
		_PHY_PathADDAOn(pAdapter, ADDA_REG, _FALSE, is2T);

		for(i = 0 ; i < retryCount ; i++){
			PathBOK = _PHY_PathB_IQK(pAdapter);
			if(PathBOK == 0x03){
				DBG_8192C("Path B IQK Success!!\n");
				result[t][4] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][6] = (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				break;
			}
			else if (i == (retryCount - 1) && PathBOK == 0x01)	//Tx IQK OK
			{
				DBG_8192C("Path B Only Tx IQK Success!!\n");
				result[t][4] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
			}
		}

		if(0x00 == PathBOK){		
			DBG_8192C("Path B IQK failed!!\n");
		}
	}

	//Back to BB mode, load original value
	//RTPRINT(FINIT, INIT_IQK, ("IQK:Back to BB mode, load original value!\n"));
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0);

	if(t!=0)
	{
		if(!pdmpriv->bRfPiEnable){
			// Switch back BB to SI mode after finish IQ Calibration.
			_PHY_PIModeSwitch(pAdapter, _FALSE);
		}

		// Reload ADDA power saving parameters
	 	_PHY_ReloadADDARegisters(pAdapter, ADDA_REG, pdmpriv->ADDA_backup, IQK_ADDA_REG_NUM);

		// Reload MAC parameters
		_PHY_ReloadMACRegisters(pAdapter, IQK_MAC_REG, pdmpriv->IQK_MAC_backup);

	 	// Reload BB parameters
	 	_PHY_ReloadADDARegisters(pAdapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup, IQK_BB_REG_NUM);

		// Restore RX initial gain
		PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032ed3);
		if(is2T){
			PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00032ed3);
		}

		//load 0xe30 IQC default value
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);

	}
	//RTPRINT(FINIT, INIT_IQK, ("_PHY_IQCalibrate() <==\n"));

}


static VOID	
_PHY_LCCalibrate(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		is2T
	)
{
	u8	tmpReg;
	u32 	RF_Amode = 0, RF_Bmode = 0, LC_Cal;

	//Check continuous TX and Packet TX
	tmpReg = rtw_read8(pAdapter, 0xd03);

	if((tmpReg&0x70) != 0)			//Deal with contisuous TX case
		rtw_write8(pAdapter, 0xd03, tmpReg&0x8F);	//disable all continuous TX
	else 							// Deal with Packet TX case
		rtw_write8(pAdapter, REG_TXPAUSE, 0xFF);			// block all queues

	if((tmpReg&0x70) != 0)
	{
		//1. Read original RF mode
		//Path-A
		RF_Amode = PHY_QueryRFReg(pAdapter, RF_PATH_A, RF_AC, bMask12Bits);

		//Path-B
		if(is2T)
			RF_Bmode = PHY_QueryRFReg(pAdapter, RF_PATH_B, RF_AC, bMask12Bits);	

		//2. Set RF mode = standby mode
		//Path-A
		PHY_SetRFReg(pAdapter, RF_PATH_A, RF_AC, bMask12Bits, (RF_Amode&0x8FFFF)|0x10000);

		//Path-B
		if(is2T)
			PHY_SetRFReg(pAdapter, RF_PATH_B, RF_AC, bMask12Bits, (RF_Bmode&0x8FFFF)|0x10000);
	}
	
	//3. Read RF reg18
	LC_Cal = PHY_QueryRFReg(pAdapter, RF_PATH_A, RF_CHNLBW, bMask12Bits);
	
	//4. Set LC calibration begin
	PHY_SetRFReg(pAdapter, RF_PATH_A, RF_CHNLBW, bMask12Bits, LC_Cal|0x08000);

	#ifdef CONFIG_LONG_DELAY_ISSUE
	rtw_msleep_os(100);	
	#else
	rtw_mdelay_os(100);		
	#endif

	//Restore original situation
	if((tmpReg&0x70) != 0)	//Deal with contisuous TX case 
	{  
		//Path-A
		rtw_write8(pAdapter, 0xd03, tmpReg);
		PHY_SetRFReg(pAdapter, RF_PATH_A, RF_AC, bMask12Bits, RF_Amode);
		
		//Path-B
		if(is2T)
			PHY_SetRFReg(pAdapter, RF_PATH_B, RF_AC, bMask12Bits, RF_Bmode);
	}
	else // Deal with Packet TX case
	{
		rtw_write8(pAdapter, REG_TXPAUSE, 0x00);	
	}
	
}


//Analog Pre-distortion calibration
#define		APK_BB_REG_NUM	8
#define		APK_CURVE_REG_NUM 4
#define		PATH_NUM		2

static VOID	
_PHY_APCalibrate(
	IN	PADAPTER	pAdapter,
	IN	char 		delta,
	IN	BOOLEAN		is2T
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	u32 			regD[PATH_NUM];
	u32			tmpReg, index, offset, i, apkbound;
	u8			path, pathbound = PATH_NUM;
	u32			BB_backup[APK_BB_REG_NUM];
	u32			BB_REG[APK_BB_REG_NUM] = {	
						rFPGA1_TxBlock, 	rOFDM0_TRxPathEnable, 
						rFPGA0_RFMOD, 	rOFDM0_TRMuxPar, 
						rFPGA0_XCD_RFInterfaceSW,	rFPGA0_XAB_RFInterfaceSW, 
						rFPGA0_XA_RFInterfaceOE, 	rFPGA0_XB_RFInterfaceOE	};
	u32			BB_AP_MODE[APK_BB_REG_NUM] = {	
						0x00000020, 0x00a05430, 0x02040000, 
						0x000800e4, 0x00204000 };
	u32			BB_normal_AP_MODE[APK_BB_REG_NUM] = {	
						0x00000020, 0x00a05430, 0x02040000, 
						0x000800e4, 0x22204000 };						

	u32			AFE_backup[IQK_ADDA_REG_NUM];
	u32			AFE_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };

	u32			MAC_backup[IQK_MAC_REG_NUM];
	u32			MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u32			APK_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x1852c, 0x5852c, 0x1852c, 0x5852c},
					{0x2852e, 0x0852e, 0x3852e, 0x0852e, 0x0852e}
					};	

	u32			APK_normal_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x0a52c, 0x3a52c, 0x5a52c, 0x5a52c},	//path settings equal to path b settings
					{0x0852c, 0x0a52c, 0x5a52c, 0x5a52c, 0x5a52c}
					};
	
	u32			APK_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52014, 0x52013, 0x5200f, 0x5208d},
					{0x5201a, 0x52019, 0x52016, 0x52033, 0x52050}
					};

	u32			APK_normal_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a},	//path settings equal to path b settings
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a}
					};
#if 0
	u32			APK_RF_value_A[PATH_NUM][APK_BB_REG_NUM] = {
					{0x1adb0, 0x1adb0, 0x1ada0, 0x1ad90, 0x1ad80},		
					{0x00fb0, 0x00fb0, 0x00fa0, 0x00f90, 0x00f80}						
					};
#endif
	u32			AFE_on_off[PATH_NUM] = {
					0x04db25a4, 0x0b1b25a4};	//path A on path B off / path A off path B on

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
	
	u32			APK_result[PATH_NUM][APK_BB_REG_NUM];	//val_1_1a, val_1_2a, val_2a, val_3a, val_4a
	//u32			AP_curve[PATH_NUM][APK_CURVE_REG_NUM];

	int			BB_offset, delta_V, delta_offset;

#if (MP_DRIVER == 1)
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.MptCtx;

	pMptCtx->APK_bound[0] = 45;
	pMptCtx->APK_bound[1] = 52;
#endif

	//RTPRINT(FINIT, INIT_IQK, ("==>PHY_APCalibrate() delta %d\n", delta));
	
	//RTPRINT(FINIT, INIT_IQK, ("AP Calibration for %s %s\n", (is2T ? "2T2R" : "1T1R"), (isNormal ? "Normal chip" : "Test chip")));

	if(!is2T)
		pathbound = 1;

	//2 FOR NORMAL CHIP SETTINGS

// Temporarily do not allow normal driver to do the following settings because these offset
// and value will cause RF internal PA to be unpredictably disabled by HW, such that RF Tx signal
// will disappear after disable/enable card many times on 88CU. RF SD and DD have not find the
// root cause, so we remove these actions temporarily. Added by tynli and SD3 Allen. 2010.05.31.
#if (MP_DRIVER != 1)
	return;
#endif

	//settings adjust for normal chip
	for(index = 0; index < PATH_NUM; index ++)
	{
		APK_offset[index] = APK_normal_offset[index];
		APK_value[index] = APK_normal_value[index];
		AFE_on_off[index] = 0x6fdb25a4;
	}

	for(index = 0; index < APK_BB_REG_NUM; index ++)
	{
		for(path = 0; path < pathbound; path++)
		{
			APK_RF_init_value[path][index] = APK_normal_RF_init_value[path][index];
			APK_RF_value_0[path][index] = APK_normal_RF_value_0[path][index];
		}
		BB_AP_MODE[index] = BB_normal_AP_MODE[index];
	}

	apkbound = 6;

	//save BB default value	
	for(index = 0; index < APK_BB_REG_NUM ; index++)
	{
		if(index == 0)		//skip 
			continue;				
		BB_backup[index] = PHY_QueryBBReg(pAdapter, BB_REG[index], bMaskDWord);
	}

	//save MAC default value													
	_PHY_SaveMACRegisters(pAdapter, MAC_REG, MAC_backup);

	//save AFE default value
	_PHY_SaveADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	for(path = 0; path < pathbound; path++)
	{
		if(path == RF_PATH_A)
		{
			//path A APK
			//load APK setting
			//path-A		
			offset = rPdp_AntA;
			for(index = 0; index < 11; index ++)			
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
			
			offset = rConfig_AntA;
			for(; index < 13; index ++) 		
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			//page-B1
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x40000000);
			
			//path A
			offset = rPdp_AntA;
			for(index = 0; index < 16; index++)
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);							
		}
		else if(path == RF_PATH_B)
		{
			//path B APK
			//load APK setting
			//path-B		
			offset = rPdp_AntB;
			for(index = 0; index < 10; index ++)			
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntA, bMaskDWord, 0x12680000);
			
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
			
			offset = rConfig_AntA;
			index = 11;
			for(; index < 13; index ++) //offset 0xb68, 0xb6c		
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			//page-B1
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x40000000);
			
			//path B
			offset = 0xb60;
			for(index = 0; index < 16; index++)
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);							
		}		

		//save RF default value
		regD[path] = PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask);
		
		//Path A AFE all on, path B AFE All off or vise versa
		for(index = 0; index < IQK_ADDA_REG_NUM ; index++)
			PHY_SetBBReg(pAdapter, AFE_REG[index], bMaskDWord, AFE_on_off[path]);
		//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xe70 %x\n", PHY_QueryBBReg(pAdapter, 0xe70, bMaskDWord)));		

		//BB to AP mode
		if(path == 0)
		{
			for(index = 0; index < APK_BB_REG_NUM ; index++)
			{
				if(index == 0)		//skip 
					continue;
				else if (index < 5)
					PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_AP_MODE[index]);
				else if (BB_REG[index] == 0x870)
					PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_backup[index]|BIT10|BIT26);
				else
					PHY_SetBBReg(pAdapter, BB_REG[index], BIT10, 0x0);
			}
			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		}
		else		//path B
		{
			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x01008c00);
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x01008c00);
		}

		//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x800 %x\n", PHY_QueryBBReg(pAdapter, 0x800, bMaskDWord)));				

		//MAC settings
		_PHY_MACSettingCalibration(pAdapter, MAC_REG, MAC_backup);

		if(path == RF_PATH_A)	//Path B to standby mode
		{
			PHY_SetRFReg(pAdapter, RF_PATH_B, RF_AC, bRFRegOffsetMask, 0x10000);			
		}
		else			//Path A to standby mode
		{
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x10000);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE1, bRFRegOffsetMask, 0x1000f);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE2, bRFRegOffsetMask, 0x20103);						
		}

		delta_offset = ((delta+14)/2);
		if(delta_offset < 0)
			delta_offset = 0;
		else if (delta_offset > 12)
			delta_offset = 12;
			
		//AP calibration
		for(index = 0; index < APK_BB_REG_NUM; index++)
		{
			if(index != 1)		//only DO PA11+PAD01001, AP RF setting
				continue;
					
			tmpReg = APK_RF_init_value[path][index];
#if 1			
			if(!pdmpriv->bAPKThermalMeterIgnore)
			{
				BB_offset = (tmpReg & 0xF0000) >> 16;

				if(!(tmpReg & BIT15)) //sign bit 0
				{
					BB_offset = -BB_offset;
				}

				delta_V = APK_delta_mapping[index][delta_offset];
				
				BB_offset += delta_V;

				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() APK num %d delta_V %d delta_offset %d\n", index, delta_V, delta_offset));		
				
				if(BB_offset < 0)
				{
					tmpReg = tmpReg & (~BIT15);
					BB_offset = -BB_offset;
				}
				else
				{
					tmpReg = tmpReg | BIT15;
				}
				tmpReg = (tmpReg & 0xFFF0FFFF) | (BB_offset << 16);
			}
#endif

#ifdef CONFIG_PCI_HCI
			if(IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID))
				PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_IPA_A, bRFRegOffsetMask, 0x894ae);
			else
#endif
				PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_IPA_A, bRFRegOffsetMask, 0x8992e);
			//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xc %x\n", PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, 0xc, bMaskDWord)));
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_AC, bRFRegOffsetMask, APK_RF_value_0[path][index]);
			//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x0 %x\n", PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, 0x0, bMaskDWord)));		
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask, tmpReg);
			//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xd %x\n", PHY_QueryRFReg(pAdapter, (RF_RADIO_PATH_E)path, 0xd, bMaskDWord)));

			// PA11+PAD01111, one shot	
			i = 0;
			do
			{
				PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x80000000);
				{
					PHY_SetBBReg(pAdapter, APK_offset[path], bMaskDWord, APK_value[0]);		
					//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", APK_offset[path], PHY_QueryBBReg(pAdapter, APK_offset[path], bMaskDWord)));
					rtw_mdelay_os(3);				
					PHY_SetBBReg(pAdapter, APK_offset[path], bMaskDWord, APK_value[1]);
					//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0x%x value 0x%x\n", APK_offset[path], PHY_QueryBBReg(pAdapter, APK_offset[path], bMaskDWord)));
					#ifdef CONFIG_LONG_DELAY_ISSUE
					rtw_msleep_os(20);
					#else
					rtw_mdelay_os(20);
					#endif
				}
				PHY_SetBBReg(pAdapter, rFPGA0_IQK, bMaskDWord, 0x00000000);
				
				if(path == RF_PATH_A)
					tmpReg = PHY_QueryBBReg(pAdapter, rAPK, 0x03E00000);
				else
					tmpReg = PHY_QueryBBReg(pAdapter, rAPK, 0xF8000000);
				//RTPRINT(FINIT, INIT_IQK, ("PHY_APCalibrate() offset 0xbd8[25:21] %x\n", tmpReg));

				i++;
			}
			while(tmpReg > apkbound && i < 4);

			APK_result[path][index] = tmpReg;
		}
	}

	//reload MAC default value	
	_PHY_ReloadMACRegisters(pAdapter, MAC_REG, MAC_backup);

	//reload BB default value	
	for(index = 0; index < APK_BB_REG_NUM ; index++)
	{
		if(index == 0)		//skip 
			continue;
		PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_backup[index]);
	}

	//reload AFE default value
	_PHY_ReloadADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	//reload RF path default value
	for(path = 0; path < pathbound; path++)
	{
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_TXBIAS_A, bRFRegOffsetMask, regD[path]);
		if(path == RF_PATH_B)
		{
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE1, bRFRegOffsetMask, 0x1000f);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE2, bRFRegOffsetMask, 0x20101);						
		}

		//note no index == 0
		if (APK_result[path][1] > 6)
			APK_result[path][1] = 6;
		//RTPRINT(FINIT, INIT_IQK, ("apk path %d result %d 0x%x \t", path, 1, APK_result[path][1]));
	}

	//RTPRINT(FINIT, INIT_IQK, ("\n"));
	

	for(path = 0; path < pathbound; path++)
	{
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G1_G4, bRFRegOffsetMask, 
		((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (APK_result[path][1] << 5) | APK_result[path][1]));
		if(path == RF_PATH_A)
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G5_G8, bRFRegOffsetMask, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x00 << 5) | 0x05));
		else
			PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G5_G8, bRFRegOffsetMask, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x02 << 5) | 0x05));
		PHY_SetRFReg(pAdapter, (RF_RADIO_PATH_E)path, RF_BS_PA_APSET_G9_G11, bRFRegOffsetMask, 
		((0x08 << 15) | (0x08 << 10) | (0x08 << 5) | 0x08));
	}

	pdmpriv->bAPKdone = _TRUE;

	//RTPRINT(FINIT, INIT_IQK, ("<==PHY_APCalibrate()\n"));
}

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

VOID
rtl8192c_PHY_IQCalibrate(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN 	bReCovery
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32			result[4][8];	//last is final result
	u8			i, final_candidate;
	BOOLEAN		bPathAOK, bPathBOK;
	s32			RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC, RegTmp = 0;
	BOOLEAN		is12simular, is13simular, is23simular;	
	BOOLEAN 	bStartContTx = _FALSE, bSingleTone = _FALSE, bCarrierSuppression = _FALSE;
	u32			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
					rOFDM0_XARxIQImbalance, 	rOFDM0_XBRxIQImbalance, 
					rOFDM0_ECCAThreshold, 	rOFDM0_AGCRSSITable,
					rOFDM0_XATxIQImbalance, 	rOFDM0_XBTxIQImbalance, 
					rOFDM0_XCTxAFE, 			rOFDM0_XDTxAFE, 
					rOFDM0_RxIQExtAnta};


#if MP_DRIVER == 1	
	bStartContTx = pAdapter->mppriv.MptCtx.bStartContTx;
	bSingleTone = pAdapter->mppriv.MptCtx.bSingleTone;
	bCarrierSuppression = pAdapter->mppriv.MptCtx.bCarrierSuppression;	
#endif

	//ignore IQK when continuous Tx
	if(bStartContTx || bSingleTone || bCarrierSuppression)
		return;

#if DISABLE_BB_RF
	return;
#endif

	if(bReCovery)
	{
		_PHY_ReloadADDARegisters(pAdapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup_recover, 9);
		return;
	}
	DBG_8192C("IQK:Start!!!\n");

	for(i = 0; i < 8; i++)
	{
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}
	final_candidate = 0xff;
	bPathAOK = _FALSE;
	bPathBOK = _FALSE;
	is12simular = _FALSE;
	is23simular = _FALSE;
	is13simular = _FALSE;

	for (i=0; i<3; i++)
	{
	 	if(IS_92C_SERIAL( pHalData->VersionID)){
			 _PHY_IQCalibrate(pAdapter, result, i, _TRUE);
	 	}
	 	else{
	 		// For 88C 1T1R
	 		_PHY_IQCalibrate(pAdapter, result, i, _FALSE);
 		}
		
		if(i == 1)
		{
			is12simular = _PHY_SimularityCompare(pAdapter, result, 0, 1);
			if(is12simular)
			{
				final_candidate = 0;
				break;
			}
		}
		
		if(i == 2)
		{
			is13simular = _PHY_SimularityCompare(pAdapter, result, 0, 2);
			if(is13simular)
			{
				final_candidate = 0;			
				break;
			}
			
			is23simular = _PHY_SimularityCompare(pAdapter, result, 1, 2);
			if(is23simular)
				final_candidate = 1;
			else
			{
				for(i = 0; i < 8; i++)
					RegTmp += result[3][i];

				if(RegTmp != 0)
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
		//RTPRINT(FINIT, INIT_IQK, ("IQK: RegE94=%lx RegE9C=%lx RegEA4=%lx RegEAC=%lx RegEB4=%lx RegEBC=%lx RegEC4=%lx RegECC=%lx\n ", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC));
	}

	if(final_candidate != 0xff)
	{
		pdmpriv->RegE94 = RegE94 = result[final_candidate][0];
		pdmpriv->RegE9C = RegE9C = result[final_candidate][1];
		RegEA4 = result[final_candidate][2];
		RegEAC = result[final_candidate][3];
		pdmpriv->RegEB4 = RegEB4 = result[final_candidate][4];
		pdmpriv->RegEBC = RegEBC = result[final_candidate][5];
		RegEC4 = result[final_candidate][6];
		RegECC = result[final_candidate][7];
		DBG_8192C("IQK: final_candidate is %x\n", final_candidate);
		DBG_8192C("IQK: RegE94=%x RegE9C=%x RegEA4=%x RegEAC=%x RegEB4=%x RegEBC=%x RegEC4=%x RegECC=%x\n ", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC);
		bPathAOK = bPathBOK = _TRUE;
	}
	else
	{
		RegE94 = RegEB4 = pdmpriv->RegE94 = pdmpriv->RegEB4 = 0x100;	//X default value
		RegE9C = RegEBC = pdmpriv->RegE9C = pdmpriv->RegEBC = 0x0;		//Y default value
	}
	
	if((RegE94 != 0)/*&&(RegEA4 != 0)*/)
		_PHY_PathAFillIQKMatrix(pAdapter, bPathAOK, result, final_candidate, (RegEA4 == 0));
	
	if(IS_92C_SERIAL( pHalData->VersionID)){
		if((RegEB4 != 0)/*&&(RegEC4 != 0)*/)
		_PHY_PathBFillIQKMatrix(pAdapter, bPathBOK, result, final_candidate, (RegEC4 == 0));
	}

	_PHY_SaveADDARegisters(pAdapter, IQK_BB_REG_92C, pdmpriv->IQK_BB_backup_recover, 9);

}


VOID
rtl8192c_PHY_LCCalibrate(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct mlme_ext_priv	*pmlmeext = &pAdapter->mlmeextpriv;
	BOOLEAN 	bStartContTx = _FALSE, bSingleTone = _FALSE, bCarrierSuppression = _FALSE;

#if MP_DRIVER == 1
	bStartContTx = pAdapter->mppriv.MptCtx.bStartContTx;
	bSingleTone = pAdapter->mppriv.MptCtx.bSingleTone;
	bCarrierSuppression = pAdapter->mppriv.MptCtx.bCarrierSuppression;
#endif

#if DISABLE_BB_RF
	return;
#endif

	//ignore IQK when continuous Tx
	if(bStartContTx || bSingleTone || bCarrierSuppression)
		return;

	if(pmlmeext->sitesurvey_res.state == SCAN_PROCESS)
		return;

	if(IS_92C_SERIAL( pHalData->VersionID)){
		_PHY_LCCalibrate(pAdapter, _TRUE);
	}
	else{
		// For 88C 1T1R
		_PHY_LCCalibrate(pAdapter, _FALSE);
	}
}

VOID
rtl8192c_PHY_APCalibrate(
	IN	PADAPTER	pAdapter,
	IN	char 		delta	
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	//default disable APK, because Tx NG issue, suggest by Jenyu, 2011.11.25
	return;

#if DISABLE_BB_RF
	return;
#endif

	if(pdmpriv->bAPKdone)
		return;

	if(IS_92C_SERIAL( pHalData->VersionID)){
		_PHY_APCalibrate(pAdapter, delta, _TRUE);
	}
	else{
		// For 88C 1T1R
		_PHY_APCalibrate(pAdapter, delta, _FALSE);
	}
}

VOID rtl8192c_PHY_SetRFPathSwitch(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		bMain
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

#if DISABLE_BB_RF
	return;
#endif

	if(IS_92C_SERIAL( pHalData->VersionID)){
		_PHY_SetRFPathSwitch(pAdapter, bMain, _TRUE);
	}
	else{
		// For 88C 1T1R
		_PHY_SetRFPathSwitch(pAdapter, bMain, _FALSE);
	}
}

