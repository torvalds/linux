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
#define _RTL8723B_PHYCFG_C_

#include <rtl8723b_hal.h>


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
PHY_QueryBBReg_8723B(
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
PHY_SetBBReg_8723B(
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
	IN	RF_PATH			eRFPath,
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
	IN	RF_PATH			eRFPath,
	IN	u32				Offset,
	IN	u32				Data	)
{
	//RT_ASSERT(FALSE,("deprecate!\n"));
}

static	u32
phy_RFSerialRead_8723B(
	IN	PADAPTER			Adapter,
	IN	RF_PATH			eRFPath,
	IN	u32				Offset
	)
{
	u32						retValue = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32						NewOffset;
	u32 						tmplong,tmplong2;
	u8					RfPiEnable=0;
	u4Byte						MaskforPhySet=0;
	int i = 0;

	//
	// Make sure RF register offset is correct
	//
	Offset &= 0xff;

	NewOffset = Offset;

	if(eRFPath == RF_PATH_A)
	{
		tmplong2 = PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord);;
		tmplong2 = (tmplong2 & (~bLSSIReadAddress)) | (NewOffset<<23) | bLSSIReadEdge;	//T65 RF
		PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord, tmplong2&(~bLSSIReadEdge));	
	}
	else
	{
		tmplong2 = PHY_QueryBBReg(Adapter, rFPGA0_XB_HSSIParameter2|MaskforPhySet, bMaskDWord);
		tmplong2 = (tmplong2 & (~bLSSIReadAddress)) | (NewOffset<<23) | bLSSIReadEdge;	//T65 RF
		PHY_SetBBReg(Adapter, rFPGA0_XB_HSSIParameter2|MaskforPhySet, bMaskDWord, tmplong2&(~bLSSIReadEdge));	
	}

	tmplong2 = PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord);
	PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord, tmplong2 & (~bLSSIReadEdge));			
	PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2|MaskforPhySet, bMaskDWord, tmplong2 | bLSSIReadEdge);		
	
	rtw_udelay_os(10);

	for(i=0;i<2;i++)
		rtw_udelay_os(MAX_STALL_TIME);	
	rtw_udelay_os(10);

	if(eRFPath == RF_PATH_A)
		RfPiEnable = (u1Byte)PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter1|MaskforPhySet, BIT8);
	else if(eRFPath == RF_PATH_B)
		RfPiEnable = (u1Byte)PHY_QueryBBReg(Adapter, rFPGA0_XB_HSSIParameter1|MaskforPhySet, BIT8);
	
	if(RfPiEnable)
	{	// Read from BBreg8b8, 12 bits for 8190, 20bits for T65 RF
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBackPi|MaskforPhySet, bLSSIReadBackData);
	
		//RT_DISP(FINIT, INIT_RF, ("Readback from RF-PI : 0x%x\n", retValue));
	}
	else
	{	//Read from BBreg8a0, 12 bits for 8190, 20 bits for T65 RF
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBack|MaskforPhySet, bLSSIReadBackData);
		
		//RT_DISP(FINIT, INIT_RF,("Readback from RF-SI : 0x%x\n", retValue));
	}
	return retValue;

}

/**
* Function:	phy_RFSerialWrite_8723B
*
* OverView:	Write data to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			RF_PATH			eRFPath,	//Radio path of A/B/C/D
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
phy_RFSerialWrite_8723B(
	IN	PADAPTER			Adapter,
	IN	RF_PATH			eRFPath,
	IN	u32				Offset,
	IN	u32				Data
	)
{
	u32						DataAndAddr = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32						NewOffset;

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
*			RF_PATH			eRFPath,	//Radio path of A/B/C/D
*			u4Byte			RegAddr,		//The target address to be read
*			u4Byte			BitMask		//The target bit position in the target address
*										//to be read
*
* Output:	None
* Return:		u4Byte			Readback value
* Note:		This function is equal to "GetRFRegSetting" in PHY programming guide
*/
u32
PHY_QueryRFReg_8723B(
	IN	PADAPTER			Adapter,
	IN	u8			eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask
	)
{
	u32 Original_Value, Readback_Value, BitShift;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	Original_Value = phy_RFSerialRead_8723B(Adapter, eRFPath, RegAddr);

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
*			PADAPTER		Adapter,
*			RF_PATH			eRFPath,	//Radio path of A/B/C/D
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
PHY_SetRFReg_8723B(
	IN	PADAPTER			Adapter,
	IN	u8				eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask,
	IN	u32				Data
	)
{
	u32		Original_Value, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

	// RF data is 12 bits only
	if (BitMask != bRFRegOffsetMask)
	{
		Original_Value = phy_RFSerialRead_8723B(Adapter, eRFPath, RegAddr);
		BitShift =  phy_CalculateBitShift(BitMask);
		Data = ((Original_Value & (~BitMask)) | (Data<< BitShift));
	}

	phy_RFSerialWrite_8723B(Adapter, eRFPath, RegAddr, Data);
}


//
// 3. Initial MAC/BB/RF config by reading MAC/BB/RF txt.
//


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
s32 PHY_MACConfig8723B(PADAPTER Adapter)
{
	int		rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	s8			*pszMACRegFile;
	s8			sz8723MACRegFile[] = RTL8723B_PHY_MACREG;


	pszMACRegFile = sz8723MACRegFile;

	//
	// Config MAC
	//
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	rtStatus = phy_ConfigMACWithParaFile(Adapter, pszMACRegFile);
	if (rtStatus == _FAIL)
#endif
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		ODM_ConfigMACWithHeaderFile(&pHalData->odmpriv);
		rtStatus = _SUCCESS;
#endif//CONFIG_EMBEDDED_FWIMG
	}

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
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 LSBs if read 32-bit from 0x870
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 MSBs if read 32-bit from 0x870 (16-bit for 0x872)

	// RF Interface Output (and Enable)
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x860
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x864

	// RF Interface (Output and)  Enable
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x860 (16-bit for 0x862)
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x864 (16-bit for 0x866)

	pHalData->PHYRegDef[ODM_RF_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter; //LSSI Parameter
	pHalData->PHYRegDef[ODM_RF_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;

	pHalData->PHYRegDef[ODM_RF_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  //wire control parameter2
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;  //wire control parameter2

        // Tranceiver Readback LSSI/HSPI mode 
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfLSSIReadBackPi = TransceiverA_HSPI_Readback;
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfLSSIReadBackPi = TransceiverB_HSPI_Readback;

}

#if (MP_DRIVER == 1)

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


	PHY_REGArrayMPLen = Rtl8723B_PHY_REG_Array_MPLength;
	Rtl8192CPHY_REGArray_Table_MP = (u32*)Rtl8723B_PHY_REG_Array_MP;

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

#if 0 //YJ,test,130321
static VOID
phy_BB8192C_Config_1T(
	IN PADAPTER Adapter
	)
{
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
#endif

static	int
phy_BB8723b_Config_ParaFile(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	int			rtStatus = _SUCCESS;
	u8	sz8723BBRegFile[] = RTL8723B_PHY_REG;
	u8	sz8723AGCTableFile[] = RTL8723B_AGC_TAB;
	u8	sz8723BBBRegPgFile[] = RTL8723B_PHY_REG_PG;
	u8	sz8723BBRegMpFile[] = RTL8723B_PHY_REG_MP;
	u8	sz8723BRFTxPwrLmtFile[] = RTL8723B_TXPWR_LMT;
	u8	*pszBBRegFile = NULL, *pszAGCTableFile = NULL, *pszBBRegPgFile = NULL, *pszBBRegMpFile=NULL, *pszRFTxPwrLmtFile = NULL;

	pszBBRegFile = sz8723BBRegFile ;
	pszAGCTableFile = sz8723AGCTableFile;
	pszBBRegPgFile = sz8723BBBRegPgFile;
	pszBBRegMpFile = sz8723BBRegMpFile;
	pszRFTxPwrLmtFile = sz8723BRFTxPwrLmtFile;

	// Read Tx Power Limit File
	PHY_InitTxPowerLimit( Adapter );
	if ( Adapter->registrypriv.RegEnableTxPowerLimit == 1 || 
	     ( Adapter->registrypriv.RegEnableTxPowerLimit == 2 && pHalData->EEPROMRegulatory == 1 ) )
	{
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
		if (PHY_ConfigRFWithPowerLimitTableParaFile( Adapter, pszRFTxPwrLmtFile )== _FAIL)
#endif
		{
#ifdef CONFIG_EMBEDDED_FWIMG
			if (HAL_STATUS_SUCCESS != ODM_ConfigRFWithHeaderFile(&pHalData->odmpriv, CONFIG_RF_TXPWR_LMT, (ODM_RF_RADIO_PATH_E)0))
				rtStatus = _FAIL;
#endif
		}

		if(rtStatus != _SUCCESS){
			DBG_871X("%s():Read Tx power limit fail\n",__func__);
			goto phy_BB8190_Config_ParaFile_Fail;
		}
	}

	//
	// 1. Read PHY_REG.TXT BB INIT!!
	//
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	if (phy_ConfigBBWithParaFile(Adapter, pszBBRegFile, CONFIG_BB_PHY_REG) == _FAIL)
#endif
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG))
			rtStatus = _FAIL;
#endif
	}

	if(rtStatus != _SUCCESS){
		DBG_8192C("%s():Write BB Reg Fail!!", __func__);
		goto phy_BB8190_Config_ParaFile_Fail;
	}

#if MP_DRIVER == 1
	if (Adapter->registrypriv.mp_mode == 1)
	{
		//
		// 1.1 Read PHY_REG_MP.TXT BB INIT!!
		//
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
		if (phy_ConfigBBWithMpParaFile(Adapter, pszBBRegMpFile) == _FAIL)
#endif
		{
#ifdef CONFIG_EMBEDDED_FWIMG
			if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG_MP))
				rtStatus = _FAIL;
#endif
		}

		if(rtStatus != _SUCCESS){
			DBG_8192C("%s():Write BB Reg MP Fail!!", __func__);
			goto phy_BB8190_Config_ParaFile_Fail;
		}
	}
#endif	// #if (MP_DRIVER == 1)

	// If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt
	PHY_InitTxPowerByRate( Adapter );
	if ( Adapter->registrypriv.RegEnableTxPowerByRate == 1 || 
	     ( Adapter->registrypriv.RegEnableTxPowerByRate == 2 && pHalData->EEPROMRegulatory != 2 ) )
	{
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
		if (phy_ConfigBBWithPgParaFile(Adapter, pszBBRegPgFile) == _FAIL)
#endif
		{
#ifdef CONFIG_EMBEDDED_FWIMG
			if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG_PG))
				rtStatus = _FAIL;
#endif
		}

		if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE )
			PHY_TxPowerByRateConfiguration( Adapter );

		if ( Adapter->registrypriv.RegEnableTxPowerLimit == 1 || 
	         ( Adapter->registrypriv.RegEnableTxPowerLimit == 2 && pHalData->EEPROMRegulatory == 1 ) )
			PHY_ConvertTxPowerLimitToPowerIndex( Adapter );

		if(rtStatus != _SUCCESS){
			DBG_8192C("%s():BB_PG Reg Fail!!\n",__func__);
		}
	}

	//
	// 2. Read BB AGC table Initialization
	//
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	if (phy_ConfigBBWithParaFile(Adapter, pszAGCTableFile, CONFIG_BB_AGC_TAB) == _FAIL)
#endif
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_AGC_TAB))
			rtStatus = _FAIL;
#endif
	}

	if(rtStatus != _SUCCESS){
		DBG_8192C("%s():AGC Table Fail\n", __func__);
		goto phy_BB8190_Config_ParaFile_Fail;
	}

phy_BB8190_Config_ParaFile_Fail:

	return rtStatus;
}


int
PHY_BBConfig8723B(
	IN	PADAPTER	Adapter
	)
{
	int	rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32	RegVal;
	u8	TmpU1B=0;
	u8	value8, CrystalCap;

	phy_InitBBRFRegisterDefinition(Adapter);

	// Enable BB and RF
	RegVal = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	rtw_write16(Adapter, REG_SYS_FUNC_EN, (u16)(RegVal|BIT13|BIT0|BIT1));

#ifdef CONFIG_USB_HCI
	rtw_write32(Adapter, 0x948, 0x0);	// USB use Antenna S0
#else
	rtw_write32(Adapter, 0x948, 0x280);	// Others use Antenna S1
#endif

	rtw_write8(Adapter, REG_RF_CTRL, RF_EN|RF_RSTB|RF_SDMRSTB);

	rtw_usleep_os(10);

	PHY_SetRFReg(Adapter, ODM_RF_PATH_A, 0x1, 0xfffff,0x780); 

#if 0
	// 20090923 Joseph: Advised by Steven and Jenyu. Power sequence before init RF.
	rtw_write8(Adapter, REG_AFE_PLL_CTRL, 0x83);
	rtw_write8(Adapter, REG_AFE_PLL_CTRL+1, 0xdb);
#endif

	rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_PPLL|FEN_PCIEA|FEN_DIO_PCIE|FEN_BB_GLB_RSTn|FEN_BBRSTB);

	rtw_write8(Adapter, REG_AFE_XTAL_CTRL+1, 0x80);

	//
	// Config BB and AGC
	//
	rtStatus = phy_BB8723b_Config_ParaFile(Adapter);

	// 0x2C[23:18] = 0x2C[17:12] = CrystalCap
	CrystalCap = pHalData->CrystalCap & 0x3F;
	PHY_SetBBReg(Adapter, REG_MAC_PHY_CTRL, 0xFFF000, (CrystalCap | (CrystalCap << 6)));	

	return rtStatus;
}

void phy_LCK_8723B(
	IN	PADAPTER	Adapter
	)
{
	PHY_SetRFReg(Adapter, RF_PATH_A, 0xB0, bRFRegOffsetMask, 0xDFBE0);
	PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x8C01);	
	rtw_mdelay_os(200);
	PHY_SetRFReg(Adapter, RF_PATH_A, 0xB0, bRFRegOffsetMask, 0xDFFE0);	
}

#if 0
// Block & Path enable
#define		rOFDMCCKEN_Jaguar 		0x808 // OFDM/CCK block enable
#define		bOFDMEN_Jaguar			0x20000000
#define		bCCKEN_Jaguar			0x10000000
#define		rRxPath_Jaguar			0x808	// Rx antenna
#define		bRxPath_Jaguar			0xff
#define		rTxPath_Jaguar			0x80c	// Tx antenna
#define		bTxPath_Jaguar			0x0fffffff
#define		rCCK_RX_Jaguar			0xa04	// for cck rx path selection
#define		bCCK_RX_Jaguar			0x0c000000 
#define		rVhtlen_Use_Lsig_Jaguar	0x8c3	// Use LSIG for VHT length
VOID
PHY_BB8723B_Config_1T(
	IN PADAPTER Adapter
	)
{
	// BB OFDM RX Path_A
	PHY_SetBBReg(Adapter, rRxPath_Jaguar, bRxPath_Jaguar, 0x11);
	// BB OFDM TX Path_A
	PHY_SetBBReg(Adapter, rTxPath_Jaguar, bMaskLWord, 0x1111);
	// BB CCK R/Rx Path_A
	PHY_SetBBReg(Adapter, rCCK_RX_Jaguar, bCCK_RX_Jaguar, 0x0);
	// MCS support
	PHY_SetBBReg(Adapter, 0x8bc, 0xc0000060, 0x4);
	// RF Path_B HSSI OFF
	PHY_SetBBReg(Adapter, 0xe00, 0xf, 0x4);	
	// RF Path_B Power Down
	PHY_SetBBReg(Adapter, 0xe90, bMaskDWord, 0);
	// ADDA Path_B OFF
	PHY_SetBBReg(Adapter, 0xe60, bMaskDWord, 0);
	PHY_SetBBReg(Adapter, 0xe64, bMaskDWord, 0);
}
#endif

int
PHY_RFConfig8723B(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;

	//
	// RF config
	//
	rtStatus = PHY_RF6052_Config8723B(Adapter);

	phy_LCK_8723B(Adapter);
	//PHY_BB8723B_Config_1T(Adapter);

	return rtStatus;
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_ConfigRFWithParaFile()
 *
 * Overview:    This function read RF parameters from general file format, and do RF 3-wire
 *
 * Input:      	PADAPTER			Adapter
 *			ps1Byte 				pFileName
 *			RF_PATH				eRFPath
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		Delay may be required for RF configuration
 *---------------------------------------------------------------------------*/
int
PHY_ConfigRFWithParaFile_8723B(
	IN	PADAPTER			Adapter,
	IN	u8* 				pFileName,
	RF_PATH				eRFPath
)
{
	return _SUCCESS;
}

//****************************************
/*-----------------------------------------------------------------------------
 * Function:    PHY_ConfigRFWithHeaderFile()
 *
 * Overview:    This function read RF parameters from general file format, and do RF 3-wire
 *
 * Input:      	PADAPTER			Adapter
 *			ps1Byte 				pFileName
 *			RF_PATH				eRFPath
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		Delay may be required for RF configuration
 *---------------------------------------------------------------------------*/
void phy_PowerIndexCheck8723B(
	IN	PADAPTER		Adapter,
	IN	u8			channel,
	IN OUT u8		*cckPowerLevel,
	IN OUT u8		*ofdmPowerLevel,
	IN OUT u8		*BW20PowerLevel,
	IN OUT u8		*BW40PowerLevel	
	)
{

	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	pHalData->CurrentCckTxPwrIdx = cckPowerLevel[0];
	pHalData->CurrentOfdm24GTxPwrIdx = ofdmPowerLevel[0];
	pHalData->CurrentBW2024GTxPwrIdx = BW20PowerLevel[0];
	pHalData->CurrentBW4024GTxPwrIdx = BW40PowerLevel[0];

	RT_TRACE(_module_hal_init_c_, _drv_info_, 
		("PHY_SetTxPowerLevel8723B(): CurrentCckTxPwrIdx : 0x%x,CurrentOfdm24GTxPwrIdx: 0x%x\n", 
		pHalData->CurrentCckTxPwrIdx, pHalData->CurrentOfdm24GTxPwrIdx));
}

/**************************************************************************************************************
 *   Description: 
 *       The low-level interface to set TxAGC , called by both MP and Normal Driver.
 *
 *                                                                                    <20120830, Kordan>
 **************************************************************************************************************/

VOID
PHY_SetTxPowerIndex_8723B(
	IN	PADAPTER			Adapter,
	IN	u32					PowerIndex,
	IN	u8					RFPath,	
	IN	u8					Rate
	)
{
	if (RFPath == ODM_RF_PATH_A || RFPath == ODM_RF_PATH_B)
	{
		switch (Rate)
		{
			case MGN_1M:    PHY_SetBBReg(Adapter, rTxAGC_A_CCK1_Mcs32,      bMaskByte1, PowerIndex); break;
			case MGN_2M:    PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte1, PowerIndex); break;
			case MGN_5_5M:  PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte2, PowerIndex); break;
			case MGN_11M:   PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte3, PowerIndex); break;

			case MGN_6M:    PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskByte0, PowerIndex); break;
			case MGN_9M:    PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskByte1, PowerIndex); break;
			case MGN_12M:   PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskByte2, PowerIndex); break;
			case MGN_18M:   PHY_SetBBReg(Adapter, rTxAGC_A_Rate18_06, bMaskByte3, PowerIndex); break;

			case MGN_24M:   PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskByte0, PowerIndex); break;
			case MGN_36M:   PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskByte1, PowerIndex); break;
			case MGN_48M:   PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskByte2, PowerIndex); break;
			case MGN_54M:   PHY_SetBBReg(Adapter, rTxAGC_A_Rate54_24, bMaskByte3, PowerIndex); break;

			case MGN_MCS0:  PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte0, PowerIndex); break;
			case MGN_MCS1:  PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte1, PowerIndex); break;
			case MGN_MCS2:  PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte2, PowerIndex); break;
			case MGN_MCS3:  PHY_SetBBReg(Adapter, rTxAGC_A_Mcs03_Mcs00, bMaskByte3, PowerIndex); break;

			case MGN_MCS4:  PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte0, PowerIndex); break;
			case MGN_MCS5:  PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte1, PowerIndex); break;
			case MGN_MCS6:  PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte2, PowerIndex); break;
			case MGN_MCS7:  PHY_SetBBReg(Adapter, rTxAGC_A_Mcs07_Mcs04, bMaskByte3, PowerIndex); break;

			default:
			     DBG_871X("Invalid Rate!!\n");
			     break;
		}
	}
	else
	{
		RT_TRACE(_module_hal_init_c_, _drv_err_,("Invalid RFPath!!\n"));
	}
}

u8
phy_GetCurrentTxNum_8723B(
	IN	PADAPTER		pAdapter
	)
{
	return RF_TX_NUM_NONIMPLEMENT;
}

u8
PHY_GetTxPowerIndex_8723B(
	IN	PADAPTER			pAdapter,
	IN	u8					RFPath,
	IN	u8					Rate,	
	IN	CHANNEL_WIDTH		BandWidth,	
	IN	u8					Channel
	)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pAdapter);
	s8					txPower = 0, powerDiffByRate = 0, limit = 0;
	BOOLEAN				bIn24G = _FALSE;

	//DBG_871X("===>%s\n", __FUNCTION__ );
	
	txPower = (s8) PHY_GetTxPowerIndexBase( pAdapter,RFPath, Rate, BandWidth, Channel, &bIn24G );
	powerDiffByRate = PHY_GetTxPowerByRate( pAdapter, BAND_ON_2_4G, ODM_RF_PATH_A, RF_1TX, Rate );

	limit = PHY_GetTxPowerLimit( pAdapter, pAdapter->registrypriv.RegPwrTblSel, (u8)(!bIn24G), pHalData->CurrentChannelBW, RFPath, Rate, pHalData->CurrentChannel);

	powerDiffByRate = powerDiffByRate > limit ? limit : powerDiffByRate;
	txPower += powerDiffByRate;
	
	txPower += PHY_GetTxPowerTrackingOffset( pAdapter, RFPath, Rate );

	if(txPower > MAX_POWER_INDEX)
		txPower = MAX_POWER_INDEX;

	//DBG_871X("Final Tx Power(RF-%c, Channel: %d) = %d(0x%X)\n", ((RFPath==0)?'A':'B'), Channel, txPower, txPower));
	return (u8) txPower;	
}

VOID
PHY_SetTxPowerLevel8723B(
	IN	PADAPTER		Adapter,
	IN	u8				Channel
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	pFAT_T			pDM_FatTable = &pDM_Odm->DM_FatTable;
	u8				RFPath = ODM_RF_PATH_A; 

	if(pHalData->AntDivCfg){// antenna diversity Enable
		RFPath = ( (pDM_FatTable->RxIdleAnt == MAIN_ANT) ? ODM_RF_PATH_A : ODM_RF_PATH_B);
	}
	else{ // antenna diversity disable
		RFPath = pHalData->ant_path;
	}

	RT_TRACE(_module_hal_init_c_, _drv_info_,("==>PHY_SetTxPowerLevel8723B()\n"));

	PHY_SetTxPowerLevelByPath(Adapter, Channel, RFPath);

	RT_TRACE(_module_hal_init_c_, _drv_info_,("<==PHY_SetTxPowerLevel8723B()\n"));
}

VOID
PHY_GetTxPowerLevel8723B(
	IN	PADAPTER		Adapter,
	OUT s32*		    		powerlevel
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	s32				TxPwrDbm = 13;
#if 0
	RT_TRACE(COMP_TXAGC, DBG_LOUD, ("PHY_GetTxPowerLevel8723B(): TxPowerLevel: %#x\n", TxPwrDbm));

	if ( pMgntInfo->ClientConfigPwrInDbm != UNSPECIFIED_PWR_DBM )
		*powerlevel = pMgntInfo->ClientConfigPwrInDbm;
	else
		*powerlevel = TxPwrDbm;
#endif
}


// <20130321, VincentLan> A workaround to eliminate the 2440MHz & 2480MHz spur of 8723B. (Asked by Rock.)
VOID
phy_SpurCalibration_8723B(
	IN	PADAPTER					pAdapter,
	IN	u1Byte						ToChannel,
	IN	u1Byte						threshold
	)
{
	u4Byte 		freq[6] = {0xFCCD, 0xFC4D, 0xFFCD, 0xFF4D, 0xFCCD, 0xFF9A}; // {chnl 5, 6, 7, 8, 13, 14}
	u1Byte 		idx = 0;
	u1Byte		b_doNotch = FALSE;
	u1Byte 		initial_gain;
	BOOLEAN		bHW_Ctrl = FALSE, bSW_Ctrl = FALSE,bHW_Ctrl_S1 = FALSE, bSW_Ctrl_S1 = FALSE;
	u4Byte		reg948;

	// add for notch
	u4Byte				wlan_channel, CurrentChannel, Is40MHz;
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(pAdapter);
	//PMGNT_INFO			pMgntInfo = &(pAdapter->MgntInfo);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	//PDM_ODM_T			pDM_Odm = &pHalData->DM_OutSrc;

	// check threshold
	if(threshold <= 0x0)
		threshold = 0x16;

	DBG_8192C("===>phy_SpurCalibration_8723B: Channel = %d\n", ToChannel);

	if (ToChannel == 5)
		idx = 0;
	else if (ToChannel == 6)
		idx = 1;
	else if (ToChannel == 7)
		idx = 2;
	else if (ToChannel == 8)
		idx = 3;
	else if (ToChannel == 13)
		idx = 4;
	else if (ToChannel == 14)
		idx = 5;
	else
		idx = 10;

	reg948 = PHY_QueryBBReg(pAdapter, rS0S1_PathSwitch, bMaskDWord);
	if((reg948 & BIT6) == 0x0)
		bSW_Ctrl = TRUE;
	else
		bHW_Ctrl = TRUE;

	if(bHW_Ctrl)
		bHW_Ctrl_S1 = (PHY_QueryBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT5|BIT4|BIT3)==0x1)? TRUE:FALSE;
	else if(bSW_Ctrl)
		bSW_Ctrl_S1 = ((reg948 & BIT9) == 0x0)? TRUE:FALSE;

	// If wlan at S1 (both HW control & SW control) and current channel=5,6,7,8,13,14
	if ((bHW_Ctrl_S1 || bSW_Ctrl_S1) && (idx <= 5)) 
	{
		initial_gain = (u1Byte) (ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskByte0) & 0x7f);
		ODM_Write_DIG(pDM_Odm, 0x30);
		PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, bMaskDWord, 0xccf000c0); 		// disable 3-wire

		PHY_SetBBReg(pAdapter, rFPGA0_PSDFunction, bMaskDWord, freq[idx]); 				// Setup PSD
		PHY_SetBBReg(pAdapter, rFPGA0_PSDFunction, bMaskDWord, 0x400000 | freq[idx]); // Start PSD	

		rtw_msleep_os(30);

		if(PHY_QueryBBReg(pAdapter, rFPGA0_PSDReport, bMaskDWord) >= threshold)
			b_doNotch = TRUE;
		
		PHY_SetBBReg(pAdapter, rFPGA0_PSDFunction, bMaskDWord, freq[idx]); // turn off PSD
		PHY_SetBBReg(pAdapter, rFPGA0_AnalogParameter4, bMaskDWord, 0xccc000c0); 	// enable 3-wire
		ODM_Write_DIG(pDM_Odm, initial_gain);
	}

	// --- Notch Filter --- Asked by Rock	
 	if (b_doNotch)
	{
		CurrentChannel = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);  
		wlan_channel   = CurrentChannel & 0x0f;						    //Get center frequency

		switch(wlan_channel)								    				//Set notch filter				
		{
			case 5:
			case 13:
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT28|BIT27|BIT26|BIT25|BIT24, 0xB);
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT9, 0x1);                     	//enable notch filter
				ODM_SetBBReg(pDM_Odm, 0xD40, bMaskDWord, 0x06000000);
				ODM_SetBBReg(pDM_Odm, 0xD44, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD48, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD4C, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD2C, BIT28, 0x1);                    	//enable CSI mask
				break;
			case 6:
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT28|BIT27|BIT26|BIT25|BIT24, 0x4);
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT9, 0x1);                   	 	//enable notch filter
				ODM_SetBBReg(pDM_Odm, 0xD40, bMaskDWord, 0x00000600);
				ODM_SetBBReg(pDM_Odm, 0xD44, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD48, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD4C, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD2C, BIT28, 0x1);                    	//enable CSI mask
				break;
			case 7:
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT28|BIT27|BIT26|BIT25|BIT24, 0x3);
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT9, 0x1);                    	//enable notch filter
				ODM_SetBBReg(pDM_Odm, 0xD40, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD44, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD48, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD4C, bMaskDWord, 0x06000000);
				ODM_SetBBReg(pDM_Odm, 0xD2C, BIT28, 0x1);                   	//enable CSI mask
				break;
			case 8:
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT28|BIT27|BIT26|BIT25|BIT24, 0xA);
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT9, 0x1);                    	//enable notch filter
				ODM_SetBBReg(pDM_Odm, 0xD40, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD44, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD48, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD4C, bMaskDWord, 0x00000380);
				ODM_SetBBReg(pDM_Odm, 0xD2C, BIT28, 0x1);                   	//enable CSI mask
				break;
			case 14:
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT28|BIT27|BIT26|BIT25|BIT24, 0x5);
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT9, 0x1);                   	 	//enable notch filter
				ODM_SetBBReg(pDM_Odm, 0xD40, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD44, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD48, bMaskDWord, 0x00000000);
				ODM_SetBBReg(pDM_Odm, 0xD4C, bMaskDWord, 0x00180000);
				ODM_SetBBReg(pDM_Odm, 0xD2C, BIT28, 0x1);                    	//enable CSI mask
				break;
			default:
				ODM_SetBBReg(pDM_Odm, 0xC40, BIT9, 0x0);				//disable notch filter
				ODM_SetBBReg(pDM_Odm, 0xD2C, BIT28, 0x0);                    	//disable CSI mask	function
				break;
		}//switch(wlan_channel)	
		return;
	}

	ODM_SetBBReg(pDM_Odm, 0xC40, BIT9, 0x0);                     //disable notch filter
	ODM_SetBBReg(pDM_Odm, 0xD2C, BIT28, 0x0);                    //disable CSI mask

}

VOID
phy_SetRegBW_8723B(
	IN	PADAPTER		Adapter,
	CHANNEL_WIDTH 	CurrentBW
)	
{
	u16	RegRfMod_BW, u2tmp = 0;
	RegRfMod_BW = rtw_read16(Adapter, REG_TRXPTCL_CTL_8723B);

	switch(CurrentBW)
	{
		case CHANNEL_WIDTH_20:
			rtw_write16(Adapter, REG_TRXPTCL_CTL_8723B, (RegRfMod_BW & 0xFE7F)); // BIT 7 = 0, BIT 8 = 0
			break;

		case CHANNEL_WIDTH_40:
			u2tmp = RegRfMod_BW | BIT7;
			rtw_write16(Adapter, REG_TRXPTCL_CTL_8723B, (u2tmp & 0xFEFF)); // BIT 7 = 1, BIT 8 = 0
			break;

		case CHANNEL_WIDTH_80:
			u2tmp = RegRfMod_BW | BIT8;
			rtw_write16(Adapter, REG_TRXPTCL_CTL_8723B, (u2tmp & 0xFF7F)); // BIT 7 = 0, BIT 8 = 1
			break;

		default:
			DBG_871X("phy_PostSetBWMode8723B():	unknown Bandwidth: %#X\n",CurrentBW);
			break;
	}
}

u8 
phy_GetSecondaryChnl_8723B(
	IN	PADAPTER	Adapter
)
{
	u8	SCSettingOf40 = 0, SCSettingOf20 = 0;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	RT_TRACE(_module_hal_init_c_, _drv_info_,("SCMapping: VHT Case: pHalData->CurrentChannelBW %d, pHalData->nCur80MhzPrimeSC %d, pHalData->nCur40MhzPrimeSC %d \n",pHalData->CurrentChannelBW,pHalData->nCur80MhzPrimeSC,pHalData->nCur40MhzPrimeSC));
	if(pHalData->CurrentChannelBW== CHANNEL_WIDTH_80)
	{
		if(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			SCSettingOf40 = VHT_DATA_SC_40_LOWER_OF_80MHZ;
		else if(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			SCSettingOf40 = VHT_DATA_SC_40_UPPER_OF_80MHZ;
		else
			RT_TRACE(_module_hal_init_c_, _drv_err_,("SCMapping: Not Correct Primary40MHz Setting \n"));
		
		if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
			SCSettingOf20 = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
			SCSettingOf20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
			SCSettingOf20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
			SCSettingOf20 = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
		else
			RT_TRACE(_module_hal_init_c_, _drv_err_,("SCMapping: Not Correct Primary40MHz Setting \n"));
	}
	else if(pHalData->CurrentChannelBW == CHANNEL_WIDTH_40)
	{
		RT_TRACE(_module_hal_init_c_, _drv_info_,("SCMapping: VHT Case: pHalData->CurrentChannelBW %d, pHalData->nCur40MhzPrimeSC %d \n",pHalData->CurrentChannelBW,pHalData->nCur40MhzPrimeSC));

		if(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			SCSettingOf20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			SCSettingOf20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else
			RT_TRACE(_module_hal_init_c_, _drv_err_,("SCMapping: Not Correct Primary40MHz Setting \n"));
	}

	RT_TRACE(_module_hal_init_c_, _drv_info_,("SCMapping: SC Value %x \n", ( (SCSettingOf40 << 4) | SCSettingOf20)));
	return  ( (SCSettingOf40 << 4) | SCSettingOf20);
}

VOID
phy_PostSetBwMode8723B(
	IN	PADAPTER	Adapter
)
{
	u1Byte			SubChnlNum = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);


	//3 Set Reg668 Reg440 BW
	phy_SetRegBW_8723B(Adapter, pHalData->CurrentChannelBW);

	//3 Set Reg483
	SubChnlNum = phy_GetSecondaryChnl_8723B(Adapter);
	rtw_write8(Adapter, REG_DATA_SC_8723B, SubChnlNum);
	
	//3//
	//3//<2>Set PHY related register
	//3//
	switch(pHalData->CurrentChannelBW)
	{
		/* 20 MHz channel*/
		case CHANNEL_WIDTH_20:
			PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x0);
			
			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x0);
			
//			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 1);
			
			PHY_SetBBReg(Adapter, rOFDM0_TxPseudoNoiseWgt, (BIT31|BIT30), 0x0);
			break;


		/* 40 MHz channel*/
		case CHANNEL_WIDTH_40:
			PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x1);
			
			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x1);
			
			// Set Control channel to upper or lower. These settings are required only for 40MHz
			PHY_SetBBReg(Adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC>>1));
		
			PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);
			
//			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 0);
			
			PHY_SetBBReg(Adapter, 0x818, (BIT26|BIT27), (pHalData->nCur40MhzPrimeSC==HAL_PRIME_CHNL_OFFSET_LOWER)?2:1);
			
			break;


			
		default:
			/*RT_TRACE(COMP_DBG, DBG_LOUD, ("phy_SetBWMode8723B(): unknown Bandwidth: %#X\n"\
						,pHalData->CurrentChannelBW));*/
			break;
			
	}

	//3<3>Set RF related register
	PHY_RF6052SetBandwidth8723B(Adapter, pHalData->CurrentChannelBW);
}

VOID
phy_SwChnl8723B(	
	IN	PADAPTER					pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8 			channelToSW = pHalData->CurrentChannel;

	if(pHalData->rf_chip == RF_PSEUDO_11N)
	{
		//RT_TRACE(COMP_MLME,DBG_LOUD,("phy_SwChnl8723B: return for PSEUDO \n"));
		return;
	}
	pHalData->RfRegChnlVal[0] = ((pHalData->RfRegChnlVal[0] & 0xfffff00) | channelToSW  );
	PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, pHalData->RfRegChnlVal[0] );
	PHY_SetRFReg(pAdapter, ODM_RF_PATH_B, RF_CHNLBW, 0x3FF, pHalData->RfRegChnlVal[0] );

	DBG_8192C("===>phy_SwChnl8723B: Channel = %d\n", channelToSW);
	//phy_SpurCalibration_8723B(pAdapter, channelToSW, 0x16);
}

VOID
phy_SwChnlAndSetBwMode8723B(
	IN  PADAPTER		Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	//RT_TRACE(COMP_SCAN, DBG_LOUD, ("phy_SwChnlAndSetBwMode8723B(): bSwChnl %d, bSetChnlBW %d \n", pHalData->bSwChnl, pHalData->bSetChnlBW));
	if ( Adapter->bNotifyChannelChange )
	{
		DBG_871X( "[%s] bSwChnl=%d, ch=%d, bSetChnlBW=%d, bw=%d\n", 
			__FUNCTION__, 
			pHalData->bSwChnl,
			pHalData->CurrentChannel,
			pHalData->bSetChnlBW,
			pHalData->CurrentChannelBW);
	}

	if((Adapter->bDriverStopped) || (Adapter->bSurpriseRemoved))
	{
		return;
	}

	if(pHalData->bSwChnl)
	{
		phy_SwChnl8723B(Adapter);
		pHalData->bSwChnl = _FALSE;
	}	

	if(pHalData->bSetChnlBW)
	{
		phy_PostSetBwMode8723B(Adapter);
		pHalData->bSetChnlBW = _FALSE;
	}	

	PHY_SetTxPowerLevel8723B(Adapter, pHalData->CurrentChannel);
}

VOID
PHY_HandleSwChnlAndSetBW8723B(
	IN	PADAPTER			Adapter,
	IN	BOOLEAN				bSwitchChannel,
	IN	BOOLEAN				bSetBandWidth,
	IN	u8					ChannelNum,
	IN	CHANNEL_WIDTH	ChnlWidth,
	IN	EXTCHNL_OFFSET	ExtChnlOffsetOf40MHz,
	IN	EXTCHNL_OFFSET	ExtChnlOffsetOf80MHz,
	IN	u8					CenterFrequencyIndex1
)
{
	//static BOOLEAN		bInitialzed = _FALSE;
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);
	u8					tmpChannel = pHalData->CurrentChannel;
	CHANNEL_WIDTH 	tmpBW= pHalData->CurrentChannelBW;
	u8					tmpnCur40MhzPrimeSC = pHalData->nCur40MhzPrimeSC;
	u8					tmpnCur80MhzPrimeSC = pHalData->nCur80MhzPrimeSC;
	u8					tmpCenterFrequencyIndex1 =pHalData->CurrentCenterFrequencyIndex1;
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;

	//DBG_871X("=> PHY_HandleSwChnlAndSetBW8812: bSwitchChannel %d, bSetBandWidth %d \n",bSwitchChannel,bSetBandWidth);

	//check is swchnl or setbw
	if(!bSwitchChannel && !bSetBandWidth)
	{
		DBG_871X("PHY_HandleSwChnlAndSetBW8812:  not switch channel and not set bandwidth \n");
		return;
	}

	//skip change for channel or bandwidth is the same
	if(bSwitchChannel)
	{
		//if(pHalData->CurrentChannel != ChannelNum)
		{
			if (HAL_IsLegalChannel(Adapter, ChannelNum))
				pHalData->bSwChnl = _TRUE;
		}
	}

	if(bSetBandWidth)
	{
		#if 0
		if(bInitialzed == _FALSE)
		{
			bInitialzed = _TRUE;
			pHalData->bSetChnlBW = _TRUE;
		}
		else if((pHalData->CurrentChannelBW != ChnlWidth) ||(pHalData->nCur40MhzPrimeSC != ExtChnlOffsetOf40MHz) || (pHalData->CurrentCenterFrequencyIndex1!= CenterFrequencyIndex1))
		{
			pHalData->bSetChnlBW = _TRUE;
		}
		#else
			pHalData->bSetChnlBW = _TRUE;
		#endif
	}

	if(!pHalData->bSetChnlBW && !pHalData->bSwChnl)
	{
		//DBG_871X("<= PHY_HandleSwChnlAndSetBW8812: bSwChnl %d, bSetChnlBW %d \n",pHalData->bSwChnl,pHalData->bSetChnlBW);
		return;
	}


	if(pHalData->bSwChnl)
	{
		pHalData->CurrentChannel=ChannelNum;
		pHalData->CurrentCenterFrequencyIndex1 = ChannelNum;
	}
	

	if(pHalData->bSetChnlBW)
	{
		pHalData->CurrentChannelBW = ChnlWidth;
#if 0
		if(ExtChnlOffsetOf40MHz==EXTCHNL_OFFSET_LOWER)
			pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_UPPER;
		else if(ExtChnlOffsetOf40MHz==EXTCHNL_OFFSET_UPPER)
			pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_LOWER;
		else
			pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

		if(ExtChnlOffsetOf80MHz==EXTCHNL_OFFSET_LOWER)
			pHalData->nCur80MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_UPPER;
		else if(ExtChnlOffsetOf80MHz==EXTCHNL_OFFSET_UPPER)
			pHalData->nCur80MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_LOWER;
		else
			pHalData->nCur80MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
#else
		pHalData->nCur40MhzPrimeSC = ExtChnlOffsetOf40MHz;
		pHalData->nCur80MhzPrimeSC = ExtChnlOffsetOf80MHz;
#endif

		pHalData->CurrentCenterFrequencyIndex1 = CenterFrequencyIndex1;		
	}

	//Switch workitem or set timer to do switch channel or setbandwidth operation
	if((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved))
	{
		phy_SwChnlAndSetBwMode8723B(Adapter);
	}
	else
	{
		if(pHalData->bSwChnl)
		{
			pHalData->CurrentChannel = tmpChannel;
			pHalData->CurrentCenterFrequencyIndex1 = tmpChannel;
		}	
		if(pHalData->bSetChnlBW)
		{
			pHalData->CurrentChannelBW = tmpBW;
			pHalData->nCur40MhzPrimeSC = tmpnCur40MhzPrimeSC;
			pHalData->nCur80MhzPrimeSC = tmpnCur80MhzPrimeSC;
			pHalData->CurrentCenterFrequencyIndex1 = tmpCenterFrequencyIndex1;
		}
	}

	//DBG_871X("Channel %d ChannelBW %d ",pHalData->CurrentChannel, pHalData->CurrentChannelBW);
	//DBG_871X("40MhzPrimeSC %d 80MhzPrimeSC %d ",pHalData->nCur40MhzPrimeSC, pHalData->nCur80MhzPrimeSC);
	//DBG_871X("CenterFrequencyIndex1 %d \n",pHalData->CurrentCenterFrequencyIndex1);

	//DBG_871X("<= PHY_HandleSwChnlAndSetBW8812: bSwChnl %d, bSetChnlBW %d \n",pHalData->bSwChnl,pHalData->bSetChnlBW);

}

VOID
PHY_SetBWMode8723B(
	IN	PADAPTER					Adapter,
	IN	CHANNEL_WIDTH	Bandwidth,	// 20M or 40M
	IN	unsigned char	Offset		// Upper, Lower, or Don't care
)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);

	PHY_HandleSwChnlAndSetBW8723B(Adapter, _FALSE, _TRUE, pHalData->CurrentChannel, Bandwidth, Offset, Offset, pHalData->CurrentChannel);
}

VOID
PHY_SwChnl8723B(	// Call after initialization
	IN	PADAPTER	Adapter,
	IN	u8		channel
	)
{
	PHY_HandleSwChnlAndSetBW8723B(Adapter, _TRUE, _FALSE, channel, 0, 0, 0, channel);
}

VOID
PHY_SetSwChnlBWMode8723B(
	IN	PADAPTER			Adapter,
	IN	u8					channel,
	IN	CHANNEL_WIDTH	Bandwidth,
	IN	u8					Offset40,
	IN	u8					Offset80
)
{
	//DBG_871X("%s()===>\n",__FUNCTION__);

	PHY_HandleSwChnlAndSetBW8723B(Adapter, _TRUE, _TRUE, channel, Bandwidth, Offset40, Offset80, channel);

	//DBG_871X("<==%s()\n",__FUNCTION__);
}

static VOID
_PHY_DumpRFReg_8723B(IN	PADAPTER	pAdapter)
{
	u32 rfRegValue,rfRegOffset;

	RT_TRACE(_module_hal_init_c_, _drv_info_, ("_PHY_DumpRFReg_8723B()====>\n"));

	for(rfRegOffset = 0x00;rfRegOffset<=0x30;rfRegOffset++){
		rfRegValue = PHY_QueryRFReg_8723B(pAdapter,RF_PATH_A, rfRegOffset, bMaskDWord);
		RT_TRACE(_module_hal_init_c_, _drv_info_, (" 0x%02x = 0x%08x\n",rfRegOffset,rfRegValue));
	}
	RT_TRACE(_module_hal_init_c_, _drv_info_, ("<===== _PHY_DumpRFReg_8723B()\n"));
}


