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
#define _RTL8192E_PHYCFG_C_

//#include <drv_types.h>

#include <rtl8192e_hal.h>


const char *const GLBwSrc[]={
	"CHANNEL_WIDTH_20",
	"CHANNEL_WIDTH_40",
	"CHANNEL_WIDTH_80",
	"CHANNEL_WIDTH_160",
	"CHANNEL_WIDTH_80_80"
};
#define		ENABLE_POWER_BY_RATE		1
#define		POWERINDEX_ARRAY_SIZE		48 //= cckRatesSize + ofdmRatesSize + htRates1TSize + htRates2TSize + vhtRates1TSize + vhtRates1TSize;

/*---------------------Define local function prototype-----------------------*/

/*----------------------------Function Body----------------------------------*/

//
// 1. BB register R/W API
//

u32
PHY_QueryBBReg8192E(
	IN	PADAPTER	Adapter,
	IN	u32			RegAddr,
	IN	u32			BitMask
	)
{
	u32	ReturnValue = 0, OriginalValue, BitShift;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	//DBG_871X("--->PHY_QueryBBReg8812(): RegAddr(%#x), BitMask(%#x)\n", RegAddr, BitMask);

	
	OriginalValue = rtw_read32(Adapter, RegAddr);
	BitShift = PHY_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;

	//DBG_871X("BBR MASK=0x%x Addr[0x%x]=0x%x\n", BitMask, RegAddr, OriginalValue);
	return (ReturnValue);
}


VOID
PHY_SetBBReg8192E(
	IN	PADAPTER	Adapter,
	IN	u4Byte		RegAddr,
	IN	u4Byte		BitMask,
	IN	u4Byte		Data
	)
{
	u4Byte			OriginalValue, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

	if(BitMask!= bMaskDWord)
	{//if not "double word" write
		OriginalValue = rtw_read32(Adapter, RegAddr);
		BitShift = PHY_CalculateBitShift(BitMask);
		Data = ((OriginalValue) & (~BitMask)) |( ((Data << BitShift)) & BitMask);
	}

	rtw_write32(Adapter, RegAddr, Data);
	
	//DBG_871X("BBW MASK=0x%x Addr[0x%x]=0x%x\n", BitMask, RegAddr, Data);
}

//
// 2. RF register R/W API
//

static	u32
phy_RFSerialRead(
	IN	PADAPTER		Adapter,
	IN	u8				eRFPath,
	IN	u32				Offset
	)
{

	u4Byte						retValue = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u4Byte						NewOffset;
	u4Byte 						tmplong2;
	u1Byte						RfPiEnable=0;
	u1Byte						i;
	u4Byte						MaskforPhySet=0;

	Offset &= 0xff;		

//	RT_TRACE(COMP_INIT, DBG_LOUD, ("phy_RFSerialRead offset 0x%x\n", Offset));
	
	//
	// Switch page for 8256 RF IC
	//
	NewOffset = Offset;

	// For 92S LSSI Read RFLSSIRead
	// For RF A/B write 0x824/82c(does not work in the future) 
	// We must use 0x824 for RF A and B to execute read trigger

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
	
	rtw_udelay_os(10);// PlatformStallExecution(10);

	//for(i=0;i<2;i++)
	//	PlatformStallExecution(MAX_STALL_TIME);	
	rtw_udelay_os(10);//PlatformStallExecution(10);

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
	//RT_DISP(FPHY, PHY_RFR, ("RFR-%d Addr[0x%x]=0x%x\n", eRFPath, pPhyReg->rfLSSIReadBack, retValue));
	
	return retValue;	
		
}



static	VOID
phy_RFSerialWrite(
	IN	PADAPTER		Adapter,
	IN	u8				eRFPath,
	IN	u32				Offset,
	IN	u32				Data
	)
{
	u32							DataAndAddr = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T		*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32							NewOffset,MaskforPhySet=0;

	// 2009/06/17 MH We can not execute IO for power save or other accident mode.
	//if(RT_CANNOT_IO(Adapter))
	//{
	//	RTPRINT(FPHY, PHY_RFW, ("phy_RFSerialWrite stop\n"));
	//	return;
	//}

	// <20121026, Kordan> If 0x818 == 1, the second value written on the previous address.
	PHY_SetBBReg(Adapter, ODM_AFE_SETTING, 0x20000, 0x0);
    
	Offset &= 0xff;

	// Shadow Update
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
	PHY_SetBBReg(Adapter, pPhyReg->rf3wireOffset|MaskforPhySet, bMaskDWord, DataAndAddr);

	// <20121026, Kordan> Restore the value on exit.
	if (IS_HARDWARE_TYPE_8192EU(Adapter))
		PHY_SetBBReg(Adapter, ODM_AFE_SETTING, 0x20000, 0x1);
}

u32
PHY_QueryRFReg8192E(
	IN	PADAPTER		Adapter,
	IN	u8				eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask
	)
{
	u32				Original_Value, Readback_Value, BitShift;	

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);	   	
	
	BitShift =  PHY_CalculateBitShift(BitMask);
	Readback_Value = (Original_Value & BitMask) >> BitShift;	

	return (Readback_Value);
}

VOID
PHY_SetRFReg8192E(
	IN	PADAPTER		Adapter,
	IN	u8				eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask,
	IN	u32				Data
	)
{
	u32 			Original_Value, BitShift;
#if (DISABLE_BB_RF == 1)
	return;
#endif

	if(BitMask == 0)
		return;

	// RF data is 20 bits only
	if (BitMask != bRFRegOffsetMask) {
		Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);
		BitShift =  PHY_CalculateBitShift(BitMask);
		Data = ((Original_Value) & (~BitMask)) | (Data<< BitShift);
	}
	
	
	phy_RFSerialWrite(Adapter, eRFPath, RegAddr, Data);

}

//
// 3. Initial MAC/BB/RF config by reading MAC/BB/RF txt.
//

s32 PHY_MACConfig8192E(PADAPTER Adapter)
{
	int				rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	s8				*pszMACRegFile;
	s8				sz8192EMACRegFile[] = RTL8192E_PHY_MACREG;

	pszMACRegFile = sz8192EMACRegFile;

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


static	VOID
phy_InitBBRFRegisterDefinition(
	IN	PADAPTER		Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);	

	// RF Interface Sowrtware Control
	pHalData->PHYRegDef[RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 LSBs if read 32-bit from 0x870
	pHalData->PHYRegDef[RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 MSBs if read 32-bit from 0x870 (16-bit for 0x872)

	// RF Interface Output (and Enable)
	pHalData->PHYRegDef[RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x860
	pHalData->PHYRegDef[RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x864

	// RF Interface (Output and)  Enable
	pHalData->PHYRegDef[RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x860 (16-bit for 0x862)
	pHalData->PHYRegDef[RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x864 (16-bit for 0x866)

	pHalData->PHYRegDef[RF_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter; //LSSI Parameter
	pHalData->PHYRegDef[RF_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;

	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  //wire control parameter2
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;  //wire control parameter2

        // Tranceiver Readback LSSI/HSPI mode 
      pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBackPi = TransceiverA_HSPI_Readback;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBackPi = TransceiverB_HSPI_Readback;
	
	//pHalData->bPhyValueInitReady=TRUE;
}

static	int
phy_BB8192E_Config_ParaFile(
	IN	PADAPTER	Adapter
	)
{
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	int			rtStatus = _SUCCESS;

	s8				sz8192EBBRegFile[] = RTL8192E_PHY_REG;	
	s8				sz8192EAGCTableFile[] = RTL8192E_AGC_TAB;
	s8				sz8192EBBRegPgFile[] = RTL8192E_PHY_REG_PG;
	s8				sz8192EBBRegMpFile[] = RTL8192E_PHY_REG_MP;	
	s8				sz8192ERFRegLimitFile[] = RTL8192E_TXPWR_LMT;	

	s8				*pszBBRegFile = NULL, *pszAGCTableFile = NULL, 
					*pszBBRegPgFile = NULL, *pszBBRegMpFile=NULL,
					*pszBBRegLimitFile = NULL,*pszRFTxPwrLmtFile = NULL;


	//DBG_871X("==>phy_BB8192E_Config_ParaFile\n");

	pszBBRegFile=sz8192EBBRegFile ;
	pszAGCTableFile =sz8192EAGCTableFile;
	pszBBRegPgFile = sz8192EBBRegPgFile;
	pszBBRegMpFile = sz8192EBBRegMpFile;
	pszRFTxPwrLmtFile = sz8192ERFRegLimitFile;

	DBG_871X("===> phy_BB8192E_Config_ParaFile() EEPROMRegulatory %d\n", pHalData->EEPROMRegulatory );

	//DBG_871X(" ===> phy_BB8192E_Config_ParaFile() phy_reg:%s\n",pszBBRegFile);
	//DBG_871X(" ===> phy_BB8192E_Config_ParaFile() phy_reg_pg:%s\n",pszBBRegPgFile);
	//DBG_871X(" ===> phy_BB8192E_Config_ParaFile() txpwr_lmt_table:%s\n",pszRFTxPwrLmtFile);

	PHY_InitTxPowerLimit( Adapter );

	// EEPROMRegulatory: 
	// "0" enables PowerByRate, 
	// "1" enables both PowerByRate and PowerLimit, 
	// "2" disables both.
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
			DBG_871X("phy_BB8192E_Config_ParaFile():Read Tx power limit fail\n");
			goto phy_BB_Config_ParaFile_Fail;
		}
	}

	// Read PHY_REG.TXT BB INIT!!
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
		DBG_871X("phy_BB8192E_Config_ParaFile():Write BB Reg Fail!!\n");
		goto phy_BB_Config_ParaFile_Fail;
	}

	// Read PHY_REG_MP.TXT BB INIT!!
#if (MP_DRIVER == 1)
	if (Adapter->registrypriv.mp_mode == 1) {
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
			DBG_871X("%s():Write BB Reg MP Fail!!\n",__FUNCTION__);
			goto phy_BB_Config_ParaFile_Fail;
		}
	}
#endif	// #if (MP_DRIVER == 1)

	// If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt
	PHY_InitTxPowerByRate( Adapter );
	if ( Adapter->registrypriv.RegEnableTxPowerByRate== 1 || 
	     ( Adapter->registrypriv.RegEnableTxPowerByRate== 2 && pHalData->EEPROMRegulatory != 2 ) )
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
			DBG_871X("%s(): CONFIG_BB_PHY_REG_PG Fail!!\n",__FUNCTION__);
			goto phy_BB_Config_ParaFile_Fail;
		}
	}


	// BB AGC table Initialization
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
		DBG_871X("phy_BB8192E_Config_ParaFile():AGC Table Fail\n");
	}

phy_BB_Config_ParaFile_Fail:

	return rtStatus;
}

int
PHY_BBConfig8192E(
	IN	PADAPTER	Adapter
	)
{
	int	rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u16	TmpU2B=0;
	u8	CrystalCap;

	phy_InitBBRFRegisterDefinition(Adapter);

	// Enable BB and RF
	TmpU2B = rtw_read16(Adapter, REG_SYS_FUNC_EN);

#ifdef CONFIG_PCI_HCI	
	if(IS_HARDWARE_TYPE_8192EE(Adapter))
		TmpU2B |= (FEN_PPLL|FEN_PCIEA|FEN_DIO_PCIE);
#endif
#ifdef CONFIG_USB_HCI
	if(IS_HARDWARE_TYPE_8192EU(Adapter))
		TmpU2B |= (FEN_USBA| FEN_USBD);
#endif

	TmpU2B |=  (FEN_EN_25_1|FEN_BB_GLB_RSTn|FEN_BBRSTB);

	rtw_write16(Adapter, REG_SYS_FUNC_EN, TmpU2B);

	//6. 0x1f[7:0] = 0x07 PathA RF Power On
	rtw_write8(Adapter, REG_RF_CTRL, RF_EN|RF_RSTB|RF_SDMRSTB);

	//rtw_write8(Adapter, REG_AFE_XTAL_CTRL+1, 0x80);
	//
	// Config BB and AGC
	//
	rtStatus = phy_BB8192E_Config_ParaFile(Adapter);
	
	
	CrystalCap = pHalData->CrystalCap & 0x3F;
	
#if 1	
	// write 0x2C[17:12] = 0x2C[23:18] = CrystalCap [6 bits]
	if(CrystalCap == 0)
		CrystalCap = EEPROM_Default_CrystalCap_8192E;
	DBG_871X( "%s ==> CrystalCap:0x%02x \n",__FUNCTION__,CrystalCap);
	PHY_SetBBReg(Adapter, REG_AFE_CTRL3_8192E, 0xFFF000, (CrystalCap | (CrystalCap << 6)));

	// write 0x24= 000f81fb ,suggest by Ed		
	rtw_write32(Adapter,REG_AFE_CTRL1_8192E,0x000f81fb);
#endif

	return rtStatus;
	
}
#if 0
// <20130320, VincentLan> A workaround to eliminate the 2480MHz spur for 92E
void 
phy_FixSpur_8192E(
	IN	PADAPTER				Adapter,
	IN	SPUR_CAL_METHOD		Method
	)
{
	u32			reg0x18 = 0;
	u8			retryNum = 0;
	u8			MaxRetryCount = 8;
	u8			Pass_A = FALSE, Pass_B = FALSE;
	u8			SpurOccur = FALSE;
	u32			PSDReport = 0;

	//DBG_871X("==0419v2==Vincent== FixSpur_8192E \n");

	if (Method == PLL_RESET){
		MaxRetryCount = 3;
	}
	else if (Method == AFE_PHASE_SEL){
		//PHY_SetMacReg(Adapter, RF_TX_G1, BIT4, 0x1); // enable
		rtw_write8(Adapter, RF_TX_G1, (rtw_read8(Adapter, RF_TX_G1)|BIT4)); // enable
		//DBG_871X("===0x20 = %x \n", rtw_read8(Adapter, RF_TX_G1));
	}
	
	// Backup current channel
	reg0x18 = PHY_QueryRFReg(Adapter, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);

	
	while ((retryNum++ < MaxRetryCount))
	{
		PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x7C0D); //CH13
		PHY_SetBBReg(Adapter, rOFDM0_XAAGCCore1, bMaskByte0, 0x30); //Path A initial gain
		PHY_SetBBReg(Adapter, rOFDM0_XBAGCCore1, bMaskByte0, 0x30); //Path B initial gain
		PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter4, bMaskDWord, 0xccf000c0); 	// disable 3-wire

		// Path A
		PHY_SetBBReg(Adapter, rFPGA0_TxInfo, bMaskByte0, 0x3);
		//DBG_871X(" 0x804 = 0x%x\n",PHY_QueryBBReg(Adapter, rFPGA0_TxInfo, bMaskByte0));
		PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0xfccd);
		//DBG_871X(" 0x808 = 0x%x\n",PHY_QueryBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord));
		PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0x40fccd);
		//DBG_871X(" 0x808 = 0x%x\n",PHY_QueryBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord));
		//rtw_msleep_os(100);
		rtw_mdelay_os(100);
		PSDReport = PHY_QueryBBReg(Adapter, rFPGA0_PSDReport, bMaskDWord);
		//DBG_871X(" Path A== PSDReport = 0x%x\n",PSDReport);
		if (PSDReport < 0x16)
			Pass_A = TRUE;
		
		// Path B
		PHY_SetBBReg(Adapter, rFPGA0_TxInfo, bMaskByte0, 0x13);
		PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0xfccd);
		PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0x40fccd);
		//rtw_msleep_os(100);
		rtw_mdelay_os(100);
		PSDReport = PHY_QueryBBReg(Adapter, rFPGA0_PSDReport, bMaskDWord);
		//DBG_871X(" Path B== PSDReport = 0x%x\n",PSDReport);
		if (PSDReport < 0x16)
			Pass_B = TRUE;

		if (Pass_A && Pass_B)
		{
			//DBG_871X("=== PathA=%d, PathB=%d\n", Pass_A, Pass_B);
			DBG_871X("===FixSpur Pass!\n");
			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter4, bMaskDWord, 0xcc0000c0); 	// enable 3-wire
			PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0xfc00);
			PHY_SetBBReg(Adapter, rOFDM0_XAAGCCore1, bMaskByte0, 0x20);
			PHY_SetBBReg(Adapter, rOFDM0_XBAGCCore1, bMaskByte0, 0x20);
			break;
		}
		else
		{
			Pass_A = FALSE;
			Pass_B = FALSE;
			if (Method == PLL_RESET)
			{
				//PHY_SetMacReg(Adapter, 0x28, bMaskByte1, 0x7);	// PLL gated 320M CLK disable
				//PHY_SetMacReg(Adapter, 0x28, bMaskByte1, 0x47);	// PLL gated 320M CLK enable
				rtw_write8(Adapter,0x29, 0x7);	// PLL gated 320M CLK disable
				rtw_write8(Adapter,0x29, 0x47);	// PLL gated 320M CLK enable
				
			}
			else if (Method == AFE_PHASE_SEL)
			{
				if (!SpurOccur)
				{
					SpurOccur = TRUE;
					DBG_871X("===FixSpur NOT Pass!\n");
					//PHY_SetMacReg(Adapter, RF_TX_G1, BIT4, 0x1);
					//PHY_SetMacReg(Adapter, 0x28, bMaskByte0, 0x80);
					//PHY_SetMacReg(Adapter, 0x28, bMaskByte0, 0x83);
					rtw_write8(Adapter, RF_TX_G1, (rtw_read8(Adapter, RF_TX_G1)|BIT4)); // enable
					rtw_write8(Adapter, 0x28, 0x80);
					rtw_write8(Adapter, 0x28, 0x83);	
					
				}
				//DBG_871X("===Round %d\n", retryNum+1);
				if (retryNum < 6){
					//PHY_SetMacReg(Adapter, RF_TX_G1, BIT5|BIT6|BIT7, 1+retryNum);
					rtw_write8(Adapter, RF_TX_G1, (rtw_read8(Adapter, RF_TX_G1)&0x1F)|((1+retryNum)<<5));
				}
				else{
					break;
				}
			}
		}
	}
	PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0xfccd); // reset PSD
	PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, reg0x18); //restore chnl
	// 0x20 NOT RESET
	// PHY_SetMacReg(Adapter, RF_TX_G1, BIT4, 0x0); // reset 0x20

}
#endif

int
PHY_RFConfig8192E(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;

	if (Adapter->bSurpriseRemoved)
		return _FAIL;

	switch(pHalData->rf_chip)
	{
		case RF_6052:
			rtStatus = PHY_RF6052_Config_8192E(Adapter);
			break;

		case RF_PSEUDO_11N:
			break;
		default: //for MacOs Warning: "RF_TYPE_MIN" not handled in switch
			break;
	}
// <20121002, Kordan> Do LCK, because the PHY reg files make no effect. (Asked by Edlu)
// Only Test chip need set 0xb1= 0x55418, (Edlu)
	//PHY_SetRFReg(Adapter, RF_PATH_A, RF_LDO, bRFRegOffsetMask, 0x55418);
	//PHY_SetRFReg(Adapter, RF_PATH_B, RF_LDO, bRFRegOffsetMask, 0x55418);	
	
	return rtStatus;
}

VOID
PHY_GetTxPowerLevel8192E(
	IN	PADAPTER		Adapter,
	OUT s32*    		powerlevel
	)
{
#if 0
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	s4Byte			TxPwrDbm = 13;
	RT_TRACE(COMP_TXAGC, DBG_LOUD, ("PHY_GetTxPowerLevel8192E(): TxPowerLevel: %#x\n", TxPwrDbm));

	if ( pMgntInfo->ClientConfigPwrInDbm != UNSPECIFIED_PWR_DBM )
		*powerlevel = pMgntInfo->ClientConfigPwrInDbm;
	else
		*powerlevel = TxPwrDbm;
#endif
}

VOID
PHY_SetTxPowerIndex_8192E(
	IN	PADAPTER			Adapter,
	IN	u32					PowerIndex,
	IN	u8					RFPath,	
	IN	u8					Rate
	)
{
	if (RFPath == ODM_RF_PATH_A)
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

			case MGN_MCS8:  PHY_SetBBReg(Adapter, rTxAGC_A_Mcs11_Mcs08, bMaskByte0, PowerIndex); break;
			case MGN_MCS9:  PHY_SetBBReg(Adapter, rTxAGC_A_Mcs11_Mcs08, bMaskByte1, PowerIndex); break;
			case MGN_MCS10: PHY_SetBBReg(Adapter, rTxAGC_A_Mcs11_Mcs08, bMaskByte2, PowerIndex); break;
			case MGN_MCS11: PHY_SetBBReg(Adapter, rTxAGC_A_Mcs11_Mcs08, bMaskByte3, PowerIndex); break;

			case MGN_MCS12: PHY_SetBBReg(Adapter, rTxAGC_A_Mcs15_Mcs12, bMaskByte0, PowerIndex); break;
			case MGN_MCS13: PHY_SetBBReg(Adapter, rTxAGC_A_Mcs15_Mcs12, bMaskByte1, PowerIndex); break;
			case MGN_MCS14: PHY_SetBBReg(Adapter, rTxAGC_A_Mcs15_Mcs12, bMaskByte2, PowerIndex); break;
			case MGN_MCS15: PHY_SetBBReg(Adapter, rTxAGC_A_Mcs15_Mcs12, bMaskByte3, PowerIndex); break;

			default:
			     DBG_871X("Invalid Rate!!\n");
			     break;				
		}
	}
	else if (RFPath == ODM_RF_PATH_B)
	{
		switch (Rate)
		{
			case MGN_1M:    PHY_SetBBReg(Adapter, rTxAGC_B_CCK1_55_Mcs32, bMaskByte1, PowerIndex); break;
			case MGN_2M:    PHY_SetBBReg(Adapter, rTxAGC_B_CCK1_55_Mcs32, bMaskByte2, PowerIndex); break;
			case MGN_5_5M:  PHY_SetBBReg(Adapter, rTxAGC_B_CCK1_55_Mcs32, bMaskByte3, PowerIndex); break;
			case MGN_11M:   PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, PowerIndex); break;
			                                             
			case MGN_6M:    PHY_SetBBReg(Adapter, rTxAGC_B_Rate18_06, bMaskByte0, PowerIndex); break;
			case MGN_9M:    PHY_SetBBReg(Adapter, rTxAGC_B_Rate18_06, bMaskByte1, PowerIndex); break;
			case MGN_12M:   PHY_SetBBReg(Adapter, rTxAGC_B_Rate18_06, bMaskByte2, PowerIndex); break;
			case MGN_18M:   PHY_SetBBReg(Adapter, rTxAGC_B_Rate18_06, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_24M:   PHY_SetBBReg(Adapter, rTxAGC_B_Rate54_24, bMaskByte0, PowerIndex); break;
			case MGN_36M:   PHY_SetBBReg(Adapter, rTxAGC_B_Rate54_24, bMaskByte1, PowerIndex); break;
			case MGN_48M:   PHY_SetBBReg(Adapter, rTxAGC_B_Rate54_24, bMaskByte2, PowerIndex); break;
			case MGN_54M:   PHY_SetBBReg(Adapter, rTxAGC_B_Rate54_24, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_MCS0:  PHY_SetBBReg(Adapter, rTxAGC_B_Mcs03_Mcs00, bMaskByte0, PowerIndex); break;
			case MGN_MCS1:  PHY_SetBBReg(Adapter, rTxAGC_B_Mcs03_Mcs00, bMaskByte1, PowerIndex); break;
			case MGN_MCS2:  PHY_SetBBReg(Adapter, rTxAGC_B_Mcs03_Mcs00, bMaskByte2, PowerIndex); break;
			case MGN_MCS3:  PHY_SetBBReg(Adapter, rTxAGC_B_Mcs03_Mcs00, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_MCS4:  PHY_SetBBReg(Adapter, rTxAGC_B_Mcs07_Mcs04, bMaskByte0, PowerIndex); break;
			case MGN_MCS5:  PHY_SetBBReg(Adapter, rTxAGC_B_Mcs07_Mcs04, bMaskByte1, PowerIndex); break;
			case MGN_MCS6:  PHY_SetBBReg(Adapter, rTxAGC_B_Mcs07_Mcs04, bMaskByte2, PowerIndex); break;
			case MGN_MCS7:  PHY_SetBBReg(Adapter, rTxAGC_B_Mcs07_Mcs04, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_MCS8:  PHY_SetBBReg(Adapter, rTxAGC_B_Mcs11_Mcs08, bMaskByte0, PowerIndex); break;
			case MGN_MCS9:  PHY_SetBBReg(Adapter, rTxAGC_B_Mcs11_Mcs08, bMaskByte1, PowerIndex); break;
			case MGN_MCS10: PHY_SetBBReg(Adapter, rTxAGC_B_Mcs11_Mcs08, bMaskByte2, PowerIndex); break;
			case MGN_MCS11: PHY_SetBBReg(Adapter, rTxAGC_B_Mcs11_Mcs08, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_MCS12: PHY_SetBBReg(Adapter, rTxAGC_B_Mcs15_Mcs12, bMaskByte0, PowerIndex); break;
			case MGN_MCS13: PHY_SetBBReg(Adapter, rTxAGC_B_Mcs15_Mcs12, bMaskByte1, PowerIndex); break;
			case MGN_MCS14: PHY_SetBBReg(Adapter, rTxAGC_B_Mcs15_Mcs12, bMaskByte2, PowerIndex); break;
			case MGN_MCS15: PHY_SetBBReg(Adapter, rTxAGC_B_Mcs15_Mcs12, bMaskByte3, PowerIndex); break;

			default:
			     DBG_871X("Invalid Rate!!\n");
			     break;			
		}
	}
	else
	{
		DBG_871X("Invalid RFPath!!\n");
	}
}

u8
phy_GetCurrentTxNum_8192E(
	IN	PADAPTER		pAdapter,
	IN	u8				Rate
	)
{
	u8	tmpByte = 0;
	u32	tmpDWord = 0;
	u8	TxNum = RF_TX_NUM_NONIMPLEMENT;

	if ( ( Rate >= MGN_MCS8 && Rate <= MGN_MCS15 ) )
		TxNum = RF_2TX;
	else
		TxNum = RF_1TX;
	
#if 0
	if ( RateSection == CCK )
	{
		tmpByte = PlatformIORead1Byte( pAdapter , 0xA07 );
		if ( ( tmpByte >> 4 ) == 0x8 || ( tmpByte >> 4 ) == 0x4 )
			TxNum = RF_1TX;
		else if ( ( tmpByte >> 4 ) == 0xC )
			TxNum = RF_2TX;
	}
	else if ( RateSection == OFDM )
	{
		tmpDWord = PlatformIORead4Byte( pAdapter , 0x90C );
		if ( ( ( tmpByte & 0x00F0 ) >> 4 ) & == 0x1 || ( ( tmpByte & 0x00F0 ) >> 4 ) & == 0x2 )
			TxNum = RF_1TX;
		else if ( ( ( tmpByte & 0x00F0 ) >> 4 ) & == 0x3 )
			TxNum = RF_2TX;
	}
	else if ( RateSection == HT_MCS0_MCS7 )
	{
		tmpDWord = PlatformIORead4Byte( pAdapter , 0x90C );
		if ( ( ( tmpByte & 0x0FF00000 ) >> 4 ) & == 0x11 || ( ( tmpByte & 0x0FF00000 ) >> 4 ) & == 0x22 )
			TxNum = RF_1TX;
		else if ( ( ( tmpByte & 0x0FF00000 ) >> 4 ) & == 0x33 )
			TxNum = RF_2TX;
	}
	else
	{	
		RT_DISP( FPHY, PHY_TXPWR ( "Invalide RateSection %d in phy_GetCurrentTxNum_8192E()\n", RateSection ) );
	}
#endif
	return TxNum;
}

u8
PHY_GetTxPowerIndex_8192E(
	IN	PADAPTER			pAdapter,
	IN	u8					RFPath,
	IN	u8					Rate,	
	IN	CHANNEL_WIDTH		BandWidth,	
	IN	u8					Channel
	)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pAdapter);
	s8					txPower = 0, powerDiffByRate = 0, limit = 0,tpt_offset=0;
	u8					txNum = phy_GetCurrentTxNum_8192E( pAdapter, Rate );
	BOOLEAN				bIn24G = _FALSE;

	//DBG_871X("===> PHY_GetTxPowerIndex_8192E\n");

	txPower = (s8) PHY_GetTxPowerIndexBase( pAdapter,RFPath, Rate, BandWidth, Channel, &bIn24G );
	
	powerDiffByRate = PHY_GetTxPowerByRate( pAdapter, BAND_ON_2_4G, RFPath, txNum, Rate );

	limit = PHY_GetTxPowerLimit( pAdapter, pAdapter->registrypriv.RegPwrTblSel, (u8)(!bIn24G), pHalData->CurrentChannelBW, RFPath, Rate, pHalData->CurrentChannel);

	tpt_offset =  PHY_GetTxPowerTrackingOffset( pAdapter, RFPath, Rate );

#if defined(DBG_TX_POWER_IDX)			
	DBG_871X("%s (RF-%c, Channel: %d, BW:0x%02x ,Rate:0x%02x) \n==> txPower= (0x%02x),powerDiffByRate= (0x%02x),limit= (0x%02x),tpt_offset=(0x%02x)\n",
		__FUNCTION__,((RFPath==0)?'A':'B'), Channel,BandWidth,Rate,txPower,powerDiffByRate,limit,tpt_offset);
#endif
	powerDiffByRate = powerDiffByRate > limit ? limit : powerDiffByRate;

	txPower += powerDiffByRate;

	txPower += tpt_offset;

	if(txPower > MAX_POWER_INDEX)
		txPower = MAX_POWER_INDEX;
#if defined(DBG_TX_POWER_IDX)	
	DBG_871X("Final Tx Power(RF-%c, Channel: %d) = %d(0x%X)\n\n", ((RFPath==0)?'A':'B'), Channel, txPower, txPower);
#endif
	return (u8)txPower;	
}

VOID
PHY_SetTxPowerLevel8192E(
	IN	PADAPTER		Adapter,
	IN	u8				Channel
	)
{

	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	u8			path = 0;
		
	//DBG_871X("==>PHY_SetTxPowerLevel8192E()\n");

	for( path = ODM_RF_PATH_A; path < pHalData->NumTotalRFPath; ++path )
	{
		PHY_SetTxPowerLevelByPath(Adapter, Channel, path);
	}
	
	//DBG_871X("<==PHY_SetTxPowerLevel8192E()\n");
}

u8 
phy_GetSecondaryChnl_8192E(
	IN	PADAPTER	Adapter
)
{
	u8					SCSettingOf40 = 0, SCSettingOf20 = 0;
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);

	//DBG_871X("SCMapping: VHT Case: pHalData->CurrentChannelBW %d, pHalData->nCur80MhzPrimeSC %d, pHalData->nCur40MhzPrimeSC %d \n",pHalData->CurrentChannelBW,pHalData->nCur80MhzPrimeSC,pHalData->nCur40MhzPrimeSC);
	if(pHalData->CurrentChannelBW== CHANNEL_WIDTH_80)
	{
		if(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			SCSettingOf40 = VHT_DATA_SC_40_LOWER_OF_80MHZ;
		else if(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			SCSettingOf40 = VHT_DATA_SC_40_UPPER_OF_80MHZ;
		else
			DBG_871X("%s- CurrentChannelBW:%d, SCMapping: Not Correct Primary40MHz Setting \n",__FUNCTION__,pHalData->CurrentChannelBW);
		
		if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
			SCSettingOf20 = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
			SCSettingOf20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
			SCSettingOf20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
			SCSettingOf20 = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
		else
			DBG_871X("%s- CurrentChannelBW:%d, SCMapping: Not Correct Primary40MHz Setting \n",__FUNCTION__,pHalData->CurrentChannelBW);
	}
	else if(pHalData->CurrentChannelBW == CHANNEL_WIDTH_40)
	{
		//DBG_871X("SCMapping: VHT Case: pHalData->CurrentChannelBW %d, pHalData->nCur40MhzPrimeSC %d \n",pHalData->CurrentChannelBW,pHalData->nCur40MhzPrimeSC);

		if(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			SCSettingOf20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			SCSettingOf20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else if(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
			DBG_871X("%s- CurrentChannelBW:%d, PRIME_CHNL_OFFSET_DONT_CARE \n",__FUNCTION__,pHalData->CurrentChannelBW);
		else			
			DBG_871X("%s- CurrentChannelBW:%d, SCMapping: Not Correct Primary40MHz Setting \n",__FUNCTION__,pHalData->CurrentChannelBW);
	}

	//DBG_871X("SCMapping: SC Value %x \n", ( (SCSettingOf40 << 4) | SCSettingOf20));
	return  ( (SCSettingOf40 << 4) | SCSettingOf20);
}

VOID
phy_SetRegBW_8192E(
	IN	PADAPTER		Adapter,
	CHANNEL_WIDTH 	CurrentBW
)	
{
	u16	RegRfMod_BW, u2tmp = 0;
	RegRfMod_BW = rtw_read16(Adapter, REG_TRXPTCL_CTL_8192E);

	switch(CurrentBW)
	{
		case CHANNEL_WIDTH_20:
			rtw_write16(Adapter, REG_TRXPTCL_CTL_8192E, (RegRfMod_BW & 0xFE7F)); // BIT 7 = 0, BIT 8 = 0
			break;

		case CHANNEL_WIDTH_40:
			u2tmp = RegRfMod_BW | BIT7;
			rtw_write16(Adapter, REG_TRXPTCL_CTL_8192E, (u2tmp & 0xFEFF)); // BIT 7 = 1, BIT 8 = 0
			break;

		case CHANNEL_WIDTH_80:
			u2tmp = RegRfMod_BW | BIT8;
			rtw_write16(Adapter, REG_TRXPTCL_CTL_8192E, (u2tmp & 0xFF7F)); // BIT 7 = 0, BIT 8 = 1
			break;

		default:
			DBG_871X("phy_PostSetBWMode8812():	unknown Bandwidth: %#X\n",CurrentBW);
			break;
	}

}


VOID
phy_PostSetBwMode8192E(
	IN	PADAPTER	Adapter
)
{
	u1Byte			SubChnlNum = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);


	//3 Set Reg668 Reg440 BW
	phy_SetRegBW_8192E(Adapter, pHalData->CurrentChannelBW);

	//3 Set Reg483
	SubChnlNum = phy_GetSecondaryChnl_8192E(Adapter);
	rtw_write8(Adapter, REG_DATA_SC_8192E, SubChnlNum);

	switch(pHalData->CurrentChannelBW)
	{
		case CHANNEL_WIDTH_20:
			PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT0, 0x0); 
			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, BIT0, 0x0); 
			PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, BIT11|BIT10, 0x3);			
			PHY_SetRFReg(Adapter, RF_PATH_B, RF_CHNLBW, BIT11|BIT10, 0x3);	

			//PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 1);			
			PHY_SetBBReg(Adapter, rOFDM0_TxPseudoNoiseWgt, (BIT31|BIT30), 0x0);
			
			break;
			   
		case CHANNEL_WIDTH_40:
			PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT0, 0x1); 
			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, BIT0, 0x1); 
			PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, BIT11|BIT10, 0x1);						
			PHY_SetRFReg(Adapter, RF_PATH_B, RF_CHNLBW, BIT11|BIT10, 0x1);	

			// Set Control channel to upper or lower. These settings are required only for 40MHz
			PHY_SetBBReg(Adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC>>1));
		
			PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);
			
//			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 0);
			
			PHY_SetBBReg(Adapter, 0x818, (BIT26|BIT27), (pHalData->nCur40MhzPrimeSC==HAL_PRIME_CHNL_OFFSET_LOWER)?2:1);
			break;

		default:
			//RT_DISP(FPHY, PHY_BBW, ("phy_PostSetBWMode8192E():	unknown Bandwidth: %#X\n",pHalData->CurrentChannelBW));
			break;
	}
}

// <20130320, VincentLan> A workaround to eliminate the 2480MHz spur for 92E
void 
phy_SpurCalibration_8192E(
	IN	PADAPTER			Adapter,
	IN	SPUR_CAL_METHOD	Method
	)
{
	u32			reg0x18 = 0;
	u8 			retryNum = 0;
	u8			MaxRetryCount = 8;
	u8			Pass_A = _FALSE, Pass_B = _FALSE;
	u8			SpurOccur = _FALSE;
	u32			PSDReport = 0;
	u32			Best_PSD_PathA = 999;
	u32			Best_Phase_PathA = 0;


	if (Method == PLL_RESET){
		MaxRetryCount = 3;
		DBG_871X("%s =>PLL_RESET \n",__FUNCTION__);
	}
	else if (Method == AFE_PHASE_SEL){		
		rtw_write8(Adapter, RF_TX_G1,rtw_read8(Adapter, RF_TX_G1)|BIT4); // enable 0x20[4]
		DBG_871X("%s =>AFE_PHASE_SEL \n",__FUNCTION__);
	}

	
	// Backup current channel
	reg0x18 = PHY_QueryRFReg(Adapter, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);

	
	while (retryNum++ < MaxRetryCount)
	{
		PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x7C0D); //CH13
		PHY_SetBBReg(Adapter, rOFDM0_XAAGCCore1, bMaskByte0, 0x30); //Path A initial gain
		PHY_SetBBReg(Adapter, rOFDM0_XBAGCCore1, bMaskByte0, 0x30); //Path B initial gain
		PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter4, bMaskDWord, 0xccf000c0); 	// disable 3-wire

		// Path A
		PHY_SetBBReg(Adapter, rFPGA0_TxInfo, bMaskByte0, 0x3);
		PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0xfccd);
		PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0x40fccd);
		//rtw_msleep_os(30);
		rtw_mdelay_os(30);
		PSDReport = PHY_QueryBBReg(Adapter, rFPGA0_PSDReport, bMaskDWord);
		//DBG_871X(" Path A== PSDReport = 0x%x (%d)\n",PSDReport,PSDReport);
		if (PSDReport < 0x16)
			Pass_A = _TRUE;
		if (PSDReport < Best_PSD_PathA){
			Best_PSD_PathA = PSDReport;
			Best_Phase_PathA = rtw_read8(Adapter, RF_TX_G1)>>5;
		}
		
		// Path B
		PHY_SetBBReg(Adapter, rFPGA0_TxInfo, bMaskByte0, 0x13);
		PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0xfccd);
		PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0x40fccd);
		//rtw_msleep_os(30);
		rtw_mdelay_os(30);
		PSDReport = PHY_QueryBBReg(Adapter, rFPGA0_PSDReport, bMaskDWord);
		//DBG_871X(" Path B== PSDReport = 0x%x (%d)\n",PSDReport,PSDReport);
		if (PSDReport < 0x16)
			Pass_B = _TRUE;

		if (Pass_A && Pass_B)
		{
			DBG_871X("=== PathA=%d, PathB=%d\n", Pass_A, Pass_B);
			DBG_871X("===FixSpur Pass!\n");
			PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter4, bMaskDWord, 0xcc0000c0); 	// enable 3-wire
			PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0xfc00);
			PHY_SetBBReg(Adapter, rOFDM0_XAAGCCore1, bMaskByte0, 0x20);
			PHY_SetBBReg(Adapter, rOFDM0_XBAGCCore1, bMaskByte0, 0x20);
			break;
		}
		else
		{
			Pass_A = _FALSE;
			Pass_B = _FALSE;
			if (Method == PLL_RESET)
			{
				//PHY_SetMacReg(Adapter, 0x28, bMaskByte1, 0x7);	// PLL gated 320M CLK disable
				//PHY_SetMacReg(Adapter, 0x28, bMaskByte1, 0x47);	// PLL gated 320M CLK enable
				rtw_write8(Adapter, 0x29, 0x7);	// PLL gated 320M CLK disable
				rtw_write8(Adapter, 0x29, 0x47);	// PLL gated 320M CLK enable
			}
			else if (Method == AFE_PHASE_SEL)
			{
				if (!SpurOccur)
				{
					SpurOccur = _TRUE;
					DBG_871X("===FixSpur NOT Pass!\n");
					//PHY_SetMacReg(Adapter, RF_TX_G1, BIT4, 0x1);
					//PHY_SetMacReg(Adapter, 0x28, bMaskByte0, 0x80);
					//PHY_SetMacReg(Adapter, 0x28, bMaskByte0, 0x83);
					rtw_write8(Adapter, RF_TX_G1,rtw_read8(Adapter, RF_TX_G1)|BIT4); // enable 0x20[4]
					rtw_write8(Adapter, 0x28, 0x80);
					rtw_write8(Adapter, 0x28, 0x83);	
					
				}
				//DBG_871X("===Round %d\n", retryNum+1);
				if (retryNum < 7)
					//PHY_SetMacReg(Adapter, RF_TX_G1, BIT5|BIT6|BIT7, 1+retryNum);
					rtw_write8(Adapter,RF_TX_G1,(rtw_read8(Adapter, RF_TX_G1)&0x1F)|((1+retryNum)<<5));
				else
					break;
			}
		}
	}
	
	if (Pass_A && Pass_B)
		;
	// 0x20 Selection Focus on Path A PSD Result
	else if (Method == AFE_PHASE_SEL){
		if (Best_Phase_PathA < 8)
			//PHY_SetMacReg(Adapter, RF_TX_G1, BIT5|BIT6|BIT7, Best_Phase_PathA);
			rtw_write8(Adapter,RF_TX_G1,(rtw_read8(Adapter, RF_TX_G1)&0x1F)|(Best_Phase_PathA<<5));
		else
			//PHY_SetMacReg(Adapter, RF_TX_G1, BIT5|BIT6|BIT7, 0);
			rtw_write8(Adapter,RF_TX_G1,(rtw_read8(Adapter, RF_TX_G1)&0x1F));
	}
	// Restore the settings
	PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter4, bMaskDWord, 0xcc0000c0); 	// enable 3-wire
	PHY_SetBBReg(Adapter, rFPGA0_PSDFunction, bMaskDWord, 0xfccd); // reset PSD
	PHY_SetBBReg(Adapter, rOFDM0_XAAGCCore1, bMaskByte0, 0x20);
	PHY_SetBBReg(Adapter, rOFDM0_XBAGCCore1, bMaskByte0, 0x20);
	
	PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, reg0x18); //restore chnl

}

VOID
phy_SwChnl8192E(	
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8 				channelToSW = pHalData->CurrentChannel;

	if(pHalData->rf_chip == RF_PSEUDO_11N)
	{
		//RT_TRACE(COMP_MLME,DBG_LOUD,("phy_SwChnl8192E: return for PSEUDO \n"));
		return;
	}
	pHalData->RfRegChnlVal[0] = ((pHalData->RfRegChnlVal[0] & 0xfffff00) | channelToSW  );
	PHY_SetRFReg(pAdapter, RF_PATH_A, RF_CHNLBW, 0x3FF, pHalData->RfRegChnlVal[0] );
	PHY_SetRFReg(pAdapter, RF_PATH_B, RF_CHNLBW, 0x3FF, pHalData->RfRegChnlVal[0] );


#if 0 //to do	
	// <20130422, VincentLan> A workaround to eliminate the 2480MHz spur for 92E
	if (channelToSW == 13)
	{
		if (pMgntInfo->RegSpurCalMethod == 1)//if AFE == 40MHz,MAC REG_0X78
			phy_SpurCalibration_8192E(pAdapter, AFE_PHASE_SEL); 
		else
			phy_SpurCalibration_8192E(pAdapter, PLL_RESET);

	}
#endif
	
}

VOID
phy_SwChnlAndSetBwMode8192E(
	IN  PADAPTER		Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	//DBG_871X("phy_SwChnlAndSetBwMode8192E(): bSwChnl %d, bSetChnlBW %d \n", pHalData->bSwChnl, pHalData->bSetChnlBW);
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
		phy_SwChnl8192E(Adapter);
		pHalData->bSwChnl = _FALSE;
	}	

	if(pHalData->bSetChnlBW)
	{
		phy_PostSetBwMode8192E(Adapter);
		pHalData->bSetChnlBW = _FALSE;
	}	

	PHY_SetTxPowerLevel8192E(Adapter, pHalData->CurrentChannel);

}

VOID
PHY_HandleSwChnlAndSetBW8192E(
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
	PADAPTER  			pDefAdapter =  GetDefaultAdapter(Adapter);
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pDefAdapter);
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
		if(pHalData->CurrentChannel != ChannelNum)
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
	if((!pDefAdapter->bDriverStopped) && (!pDefAdapter->bSurpriseRemoved))
	{
		phy_SwChnlAndSetBwMode8192E(Adapter);
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
PHY_SwChnl8192E(
	IN	PADAPTER	Adapter,
	IN	u8			channel
	)
{
	//DBG_871X("%s()===>\n",__FUNCTION__);

	PHY_HandleSwChnlAndSetBW8192E(Adapter, _TRUE, _FALSE, channel, 0, 0, 0, channel);

#if (MP_DRIVER == 1) 
	// <20120712, Kordan> IQK on each channel, asked by James.
	//PHY_IQCalibrate(Adapter, _FALSE);
#endif

	//DBG_871X("<==%s()\n",__FUNCTION__);
}
VOID
PHY_SetBWMode8192E(
	IN	PADAPTER			Adapter,
	IN	CHANNEL_WIDTH	Bandwidth,	// 20M or 40M
	IN	u8					Offset		// Upper, Lower, or Don't care
)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);

	//DBG_871X("%s()===>\n",__FUNCTION__);

	PHY_HandleSwChnlAndSetBW8192E(Adapter, _FALSE, _TRUE, pHalData->CurrentChannel, Bandwidth, Offset, Offset, pHalData->CurrentChannel);

	//DBG_871X("<==%s()\n",__FUNCTION__);
}
VOID
PHY_SetSwChnlBWMode8192E(
	IN	PADAPTER			Adapter,
	IN	u8					channel,
	IN	CHANNEL_WIDTH	Bandwidth,
	IN	u8					Offset40,
	IN	u8					Offset80
)
{
	//DBG_871X("%s()===>\n",__FUNCTION__);

	PHY_HandleSwChnlAndSetBW8192E(Adapter, _TRUE, _TRUE, channel, Bandwidth, Offset40, Offset80, channel);

	//DBG_871X("<==%s()\n",__FUNCTION__);
}

#if 0
static VOID
_PHY_SetBWMode8192E(
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
					pHalData->CurrentChannelBW == CHANNEL_WIDTH_20?"20MHz":"40MHz"))*/

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
		case CHANNEL_WIDTH_20:
			regBwOpMode |= BW_OPMODE_20MHZ;
			   // 2007/02/07 Mark by Emily becasue we have not verify whether this register works
			rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);
			break;

		case CHANNEL_WIDTH_40:
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
		case CHANNEL_WIDTH_20:
			PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x0);
			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x0);
			//PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 1);
			PHY_SetBBReg(Adapter, rOFDM0_TxPseudoNoiseWgt, (BIT31|BIT30), 0x0);
			break;


		/* 40 MHz channel*/
		case CHANNEL_WIDTH_40:
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
		case RF_PSEUDO_11N:
			// Do Nothing
			break;

		case RF_6052:
			PHY_RF6052SetBandwidth8192E(Adapter, pHalData->CurrentChannelBW);
			break;

		default:
			//RT_ASSERT(FALSE, ("Unknown RFChipID: %d\n", pHalData->rfchip));
			break;
	}

	//pHalData->SetBWModeInProgress= FALSE;

	//RT_TRACE(COMP_SCAN, DBG_LOUD, ("<==PHY_SetBWModeCallback8192C() \n" ));
}

VOID
PHY_SetBWMode8192E(
	IN	PADAPTER			Adapter,
	IN	CHANNEL_WIDTH	Bandwidth,	// 20M or 40M
	IN	u8					Offset		// Upper, Lower, or Don't care
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	CHANNEL_WIDTH 	tmpBW= pHalData->CurrentChannelBW;
	// Modified it for 20/40 mhz switch by guangan 070531
	//PMGNT_INFO	pMgntInfo=&Adapter->MgntInfo;

	//return;

	//if(pHalData->SwChnlInProgress)
//	if(pMgntInfo->bScanInProgress)
//	{
//		RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SetBWMode8192C() %s Exit because bScanInProgress!\n",
//					Bandwidth == CHANNEL_WIDTH_20?"20MHz":"40MHz"));
//		return;
//	}

//	if(pHalData->SetBWModeInProgress)
//	{
//		// Modified it for 20/40 mhz switch by guangan 070531
//		RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SetBWMode8192C() %s cancel last timer because SetBWModeInProgress!\n",
//					Bandwidth == CHANNEL_WIDTH_20?"20MHz":"40MHz"));
//		PlatformCancelTimer(Adapter, &pHalData->SetBWModeTimer);
//		//return;
//	}

	//if(pHalData->SetBWModeInProgress)
	//	return;

	//pHalData->SetBWModeInProgress= TRUE;

	pHalData->CurrentChannelBW = Bandwidth;

#if 0
	if(Offset==EXTCHNL_OFFSET_LOWER)
		pHalData->nCur40MhzPrimeSC = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if(Offset==EXTCHNL_OFFSET_UPPER)
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
		_PHY_SetBWMode8192E(Adapter);
	#endif
	}
	else
	{
		//RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SetBWMode8192C() SetBWModeInProgress FALSE driver sleep or unload\n"));
		//pHalData->SetBWModeInProgress= FALSE;
		pHalData->CurrentChannelBW = tmpBW;
	}
	
}

#endif
