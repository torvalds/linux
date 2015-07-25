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
#define _RTL8812A_PHYCFG_C_

//#include <drv_types.h>

#include <rtl8812a_hal.h>


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
PHY_QueryBBReg8812(
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
PHY_SetBBReg8812(
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
	u32							retValue = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	BOOLEAN						bIsPIMode = _FALSE;


	// 2009/06/17 MH We can not execute IO for power save or other accident mode.
	//if(RT_CANNOT_IO(Adapter))
	//{
	//	RT_DISP(FPHY, PHY_RFR, ("phy_RFSerialRead return all one\n"));
	//	return	0xFFFFFFFF;
	//}

	// <20120809, Kordan> CCA OFF(when entering), asked by James to avoid reading the wrong value.
	// <20120828, Kordan> Toggling CCA would affect RF 0x0, skip it!
	if (Offset != 0x0 &&  ! (IS_VENDOR_8812A_C_CUT(Adapter) || IS_HARDWARE_TYPE_8821(Adapter)))
		PHY_SetBBReg(Adapter, rCCAonSec_Jaguar, 0x8, 1);

	Offset &= 0xff;

	if (eRFPath == RF_PATH_A)
       	bIsPIMode = (BOOLEAN)PHY_QueryBBReg(Adapter, 0xC00, 0x4);
	else if (eRFPath == RF_PATH_B)
       	bIsPIMode = (BOOLEAN)PHY_QueryBBReg(Adapter, 0xE00, 0x4);

	PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, bHSSIRead_addr_Jaguar, Offset);

	if (IS_VENDOR_8812A_C_CUT(Adapter) || IS_HARDWARE_TYPE_8821(Adapter))
		rtw_udelay_os(20);

	if (bIsPIMode)
	{
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBackPi, rRead_data_Jaguar);
		//DBG_871X("[PI mode] RFR-%d Addr[0x%x]=0x%x\n", eRFPath, pPhyReg->rfLSSIReadBackPi, retValue);
	}
	else  
	{
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBack, rRead_data_Jaguar);
		//DBG_871X("[SI mode] RFR-%d Addr[0x%x]=0x%x\n", eRFPath, pPhyReg->rfLSSIReadBack, retValue);
	}

	// <20120809, Kordan> CCA ON(when exiting), asked by James to avoid reading the wrong value.
	// <20120828, Kordan> Toggling CCA would affect RF 0x0, skip it!
	if (Offset != 0x0 &&  ! (IS_VENDOR_8812A_C_CUT(Adapter) || IS_HARDWARE_TYPE_8821(Adapter)))
		PHY_SetBBReg(Adapter, rCCAonSec_Jaguar, 0x8, 0);

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
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];

	// 2009/06/17 MH We can not execute IO for power save or other accident mode.
	//if(RT_CANNOT_IO(Adapter))
	//{
	//	RTPRINT(FPHY, PHY_RFW, ("phy_RFSerialWrite stop\n"));
	//	return;
	//}
    
	Offset &= 0xff;

	// Shadow Update
	//PHY_RFShadowWrite(Adapter, eRFPath, Offset, Data);

	// Put write addr in [27:20]  and write data in [19:00]
	DataAndAddr = ((Offset<<20) | (Data&0x000fffff)) & 0x0fffffff;

	// Write Operation 
	// TODO: Dynamically determine whether using PI or SI to write RF registers.
	PHY_SetBBReg(Adapter, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);
		//DBG_871X("RFW-%d Addr[0x%x]=0x%x\n", eRFPath, pPhyReg->rf3wireOffset, DataAndAddr);

}

u32
PHY_QueryRFReg8812(
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
PHY_SetRFReg8812(
	IN	PADAPTER		Adapter,
	IN	u8				eRFPath,
	IN	u32				RegAddr,
	IN	u32				BitMask,
	IN	u32				Data
	)
{
#if (DISABLE_BB_RF == 1)
	return;
#endif

	if(BitMask == 0)
		return;

	// RF data is 20 bits only
	if (BitMask != bLSSIWrite_data_Jaguar) {
		u32	Original_Value, BitShift;
		Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);
		BitShift =  PHY_CalculateBitShift(BitMask);
		Data = ((Original_Value) & (~BitMask)) | (Data<< BitShift);
	}
	
	phy_RFSerialWrite(Adapter, eRFPath, RegAddr, Data);

}

//
// 3. Initial MAC/BB/RF config by reading MAC/BB/RF txt.
//

s32 PHY_MACConfig8812(PADAPTER Adapter)
{
	int				rtStatus = _FAIL;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	s8				*pszMACRegFile;
	s8				sz8812MACRegFile[] = RTL8812_PHY_MACREG;
	s8				sz8821MACRegFile[] = RTL8821_PHY_MACREG;

	if(IS_HARDWARE_TYPE_8812(Adapter))
	{
		pszMACRegFile = sz8812MACRegFile;
	}
	else
	{
		pszMACRegFile = sz8821MACRegFile;
	}

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
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 LSBs if read 32-bit from 0x870
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; // 16 MSBs if read 32-bit from 0x870 (16-bit for 0x872)

	// RF Interface Output (and Enable)
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x860
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; // 16 LSBs if read 32-bit from 0x864

	// RF Interface (Output and)  Enable
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x860 (16-bit for 0x862)
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; // 16 MSBs if read 32-bit from 0x864 (16-bit for 0x866)

	pHalData->PHYRegDef[ODM_RF_PATH_A].rf3wireOffset = rA_LSSIWrite_Jaguar; //LSSI Parameter
	pHalData->PHYRegDef[ODM_RF_PATH_B].rf3wireOffset = rB_LSSIWrite_Jaguar;

	pHalData->PHYRegDef[ODM_RF_PATH_A].rfHSSIPara2 = rHSSIRead_Jaguar;  //wire control parameter2
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfHSSIPara2 = rHSSIRead_Jaguar;  //wire control parameter2

	// Tranceiver Readback LSSI/HSPI mode 
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfLSSIReadBack = rA_SIRead_Jaguar;
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfLSSIReadBack = rB_SIRead_Jaguar;
	pHalData->PHYRegDef[ODM_RF_PATH_A].rfLSSIReadBackPi = rA_PIRead_Jaguar;
	pHalData->PHYRegDef[ODM_RF_PATH_B].rfLSSIReadBackPi = rB_PIRead_Jaguar;
}

VOID
PHY_BB8812_Config_1T(
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


static	int
phy_BB8812_Config_ParaFile(
	IN	PADAPTER	Adapter
	)
{
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	int			rtStatus = _SUCCESS;

	s8				sz8812BBRegFile[] = RTL8812_PHY_REG;	
	s8				sz8812AGCTableFile[] = RTL8812_AGC_TAB;
	s8				sz8812BBRegPgFile[] = RTL8812_PHY_REG_PG;
	s8				sz8812BBRegMpFile[] = RTL8812_PHY_REG_MP;	
	s8				sz8812BBRegLimitFile[] = RTL8812_TXPWR_LMT;	

	s8				sz8821BBRegFile[] = RTL8821_PHY_REG;	
	s8				sz8821AGCTableFile[] = RTL8821_AGC_TAB;
	s8				sz8821BBRegPgFile[] = RTL8821_PHY_REG_PG;
	s8				sz8821BBRegMpFile[] = RTL8821_PHY_REG_MP;
	s8				sz8821RFTxPwrLmtFile[] = RTL8821_TXPWR_LMT;
	s8				*pszBBRegFile = NULL, *pszAGCTableFile = NULL, 
					*pszBBRegPgFile = NULL, *pszBBRegMpFile=NULL,
					*pszRFTxPwrLmtFile = NULL;


	//DBG_871X("==>phy_BB8812_Config_ParaFile\n");

	if(IS_HARDWARE_TYPE_8812(Adapter))
	{
		pszBBRegFile=sz8812BBRegFile ;
		pszAGCTableFile =sz8812AGCTableFile;
		pszBBRegPgFile = sz8812BBRegPgFile;
		pszBBRegMpFile = sz8812BBRegMpFile;
		pszRFTxPwrLmtFile = sz8812BBRegLimitFile;
	}
	else 
	{
		pszBBRegFile=sz8821BBRegFile ;
		pszAGCTableFile =sz8821AGCTableFile;
		pszBBRegPgFile = sz8821BBRegPgFile;
		pszBBRegMpFile = sz8821BBRegMpFile;
		pszRFTxPwrLmtFile = sz8821RFTxPwrLmtFile;
	}

	DBG_871X("===> phy_BB8812_Config_ParaFile() EEPROMRegulatory %d\n", pHalData->EEPROMRegulatory );

	//DBG_871X(" ===> phy_BB8812_Config_ParaFile() phy_reg:%s\n",pszBBRegFile);
	//DBG_871X(" ===> phy_BB8812_Config_ParaFile() phy_reg_pg:%s\n",pszBBRegPgFile);
	//DBG_871X(" ===> phy_BB8812_Config_ParaFile() agc_table:%s\n",pszAGCTableFile);

	// Read Tx Power Limit 
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
			DBG_871X("%s():Read Tx power limit fail\n",__FUNCTION__	);
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
		DBG_871X("%s(): CONFIG_BB_PHY_REG Fail!!\n",__FUNCTION__	);
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
			DBG_871X("phy_BB8812_Config_ParaFile():Write BB Reg MP Fail!!\n");
			goto phy_BB_Config_ParaFile_Fail;
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
			DBG_871X("%s(): CONFIG_BB_PHY_REG_PG Fail!!\n",__FUNCTION__	);
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
		DBG_871X("%s(): CONFIG_BB_AGC_TAB Fail!!\n",__FUNCTION__	);
	}

phy_BB_Config_ParaFile_Fail:

	return rtStatus;
}

int
PHY_BBConfig8812(
	IN	PADAPTER	Adapter
	)
{
	int	rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	TmpU1B=0;
	u8	CrystalCap;

	phy_InitBBRFRegisterDefinition(Adapter);

    	//tangw check start 20120412
    	// . APLL_EN,,APLL_320_GATEB,APLL_320BIAS,  auto config by hw fsm after pfsm_go (0x4 bit 8) set
    	TmpU1B = rtw_read8(Adapter, REG_SYS_FUNC_EN);

	if(IS_HARDWARE_TYPE_8812AU(Adapter) || IS_HARDWARE_TYPE_8821U(Adapter))
		TmpU1B |= FEN_USBA;
	else  if(IS_HARDWARE_TYPE_8812E(Adapter) || IS_HARDWARE_TYPE_8821E(Adapter))
		TmpU1B |= FEN_PCIEA;

	rtw_write8(Adapter, REG_SYS_FUNC_EN, TmpU1B);

	rtw_write8(Adapter, REG_SYS_FUNC_EN, (TmpU1B|FEN_BB_GLB_RSTn|FEN_BBRSTB));//same with 8812
	//6. 0x1f[7:0] = 0x07 PathA RF Power On
	rtw_write8(Adapter, REG_RF_CTRL, 0x07);//RF_SDMRSTB,RF_RSTB,RF_EN same with 8723a
	//7.  PathB RF Power On
	rtw_write8(Adapter, REG_OPT_CTRL_8812+2, 0x7);//RF_SDMRSTB,RF_RSTB,RF_EN same with 8723a
	//tangw check end 20120412


	//
	// Config BB and AGC
	//
	rtStatus = phy_BB8812_Config_ParaFile(Adapter);

	if(IS_HARDWARE_TYPE_8812(Adapter))
	{
		// write 0x2C[30:25] = 0x2C[24:19] = CrystalCap
		CrystalCap = pHalData->CrystalCap & 0x3F;
		PHY_SetBBReg(Adapter, REG_MAC_PHY_CTRL, 0x7FF80000, (CrystalCap | (CrystalCap << 6)));
	}
	else if (IS_HARDWARE_TYPE_8821(Adapter))
	{
		// 0x2C[23:18] = 0x2C[17:12] = CrystalCap
		CrystalCap = pHalData->CrystalCap & 0x3F;
		PHY_SetBBReg(Adapter, REG_MAC_PHY_CTRL, 0xFFF000, (CrystalCap | (CrystalCap << 6)));	
	}

	if(IS_HARDWARE_TYPE_JAGUAR(Adapter))
	{
		pHalData->Reg837 = rtw_read8(Adapter, 0x837);
	}

	return rtStatus;	
}

int
PHY_RFConfig8812(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;

	if (Adapter->bSurpriseRemoved)
		return _FAIL;

	switch(pHalData->rf_chip)
	{
		case RF_PSEUDO_11N:
			DBG_871X("%s(): RF_PSEUDO_11N\n",__FUNCTION__);
			break;
		default:
			rtStatus = PHY_RF6052_Config_8812(Adapter);
			break;
	}

	return rtStatus;
}

VOID
PHY_TxPowerTrainingByPath_8812(
	IN	PADAPTER			Adapter,
	IN	CHANNEL_WIDTH		BandWidth,
	IN	u8					Channel,
	IN	u8					RfPath
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	u8	i;
	u32	PowerLevel, writeData, writeOffset;

	if(RfPath >= pHalData->NumTotalRFPath)
		return;

	writeData = 0;
	if(RfPath == ODM_RF_PATH_A)
	{
		PowerLevel = PHY_GetTxPowerIndex_8812A(Adapter, ODM_RF_PATH_A, MGN_MCS7, BandWidth, Channel);
		writeOffset =  rA_TxPwrTraing_Jaguar;
	}	
	else 
	{
		PowerLevel = PHY_GetTxPowerIndex_8812A(Adapter, ODM_RF_PATH_B, MGN_MCS7, BandWidth, Channel);
		writeOffset =  rB_TxPwrTraing_Jaguar;
	}	
	
	for(i = 0; i < 3; i++)
	{
		if(i == 0)
			PowerLevel = PowerLevel - 10;
		else if(i == 1)
			PowerLevel = PowerLevel - 8;
		else
			PowerLevel = PowerLevel - 6;
		writeData |= (((PowerLevel > 2)?(PowerLevel):2) << (i * 8));
	}
	
	PHY_SetBBReg(Adapter, writeOffset, 0xffffff, writeData);
}

VOID
phy_TxPwrAdjInPercentage(
	IN		PADAPTER		Adapter,
	OUT		u8*				pTxPwrIdx)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	TxPwrInPercentage = 0;

	// Retrieve default TxPwr index settings from registry.
	TxPwrInPercentage = pHalData->TxPwrInPercentage;
	
	if(*pTxPwrIdx > RF6052_MAX_TX_PWR)
		*pTxPwrIdx = RF6052_MAX_TX_PWR;
	
	//
	// <Roger_Notes> NEC Spec: dB = 10*log(X/Y), X: target value, Y: default value.
	// For example: TxPower 50%, 10*log(50/100)=(nearly)-3dB
	// 2010.07.26.
	//
	if(TxPwrInPercentage & TX_PWR_PERCENTAGE_0)// 12.5% , -9dB
	{
		*pTxPwrIdx -=18;
	}
	else if(TxPwrInPercentage & TX_PWR_PERCENTAGE_1)// 25%, -6dB
	{
		*pTxPwrIdx -=12;
	}
	else if(TxPwrInPercentage & TX_PWR_PERCENTAGE_2)// 50%, -3dB
	{
		*pTxPwrIdx -=6;
	}

	if(*pTxPwrIdx > RF6052_MAX_TX_PWR) // Avoid underflow condition.
		*pTxPwrIdx = RF6052_MAX_TX_PWR;
}

VOID
PHY_GetTxPowerLevel8812(
	IN	PADAPTER		Adapter,
	OUT s32*    		powerlevel
	)
{
#if 0
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	s4Byte			TxPwrDbm = 13;
	RT_TRACE(COMP_TXAGC, DBG_LOUD, ("PHY_GetTxPowerLevel8812(): TxPowerLevel: %#x\n", TxPwrDbm));

	if ( pMgntInfo->ClientConfigPwrInDbm != UNSPECIFIED_PWR_DBM )
		*powerlevel = pMgntInfo->ClientConfigPwrInDbm;
	else
		*powerlevel = TxPwrDbm;
#endif
}

//create new definition of PHY_SetTxPowerLevel8812 by YP. 
//Page revised on 20121106
//the new way to set tx power by rate, NByte access, here N byte shall be 4 byte(DWord) or NByte(N>4) access. by page/YP, 20121106
VOID
PHY_SetTxPowerLevel8812(
	IN	PADAPTER		Adapter,
	IN	u8				Channel
	)
{

	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	u8			path = 0;

	//DBG_871X("==>PHY_SetTxPowerLevel8812()\n");

	for( path = ODM_RF_PATH_A; path < pHalData->NumTotalRFPath; ++path )
	{
		PHY_SetTxPowerLevelByPath(Adapter, Channel, path);
		PHY_TxPowerTrainingByPath_8812(Adapter, pHalData->CurrentChannelBW, Channel, path);
	}

	//DBG_871X("<==PHY_SetTxPowerLevel8812()\n");
}

u8
phy_GetCurrentTxNum_8812A(
	IN	PADAPTER		pAdapter,
	IN	u8				Rate
	)
{
	u8	tx_num = 0;
	
	if ( ( Rate >= MGN_MCS8 && Rate <= MGN_MCS15 ) || 
		 ( Rate >= MGN_VHT2SS_MCS0 && Rate <= MGN_VHT2SS_MCS9 ) )
		tx_num = RF_2TX;
	else
		tx_num = RF_1TX;
		
	return tx_num;
}

/**************************************************************************************************************
 *   Description: 
 *       The low-level interface to get the FINAL Tx Power Index , called  by both MP and Normal Driver.
 *
 *                                                                                    <20120830, Kordan>
 **************************************************************************************************************/
u8
PHY_GetTxPowerIndex_8812A(
	IN	PADAPTER			pAdapter,
	IN	u8					RFPath,
	IN	u8					Rate,	
	IN	CHANNEL_WIDTH		BandWidth,	
	IN	u8					Channel
	)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pAdapter);
	s8					powerDiffByRate = 0;
	s8					txPower = 0, limit = 0;
	u8					tx_num = phy_GetCurrentTxNum_8812A( pAdapter, Rate );
	BOOLEAN				bIn24G = _FALSE;

	//DBG_871X("===> PHY_GetTxPowerIndex_8812A\n");

	txPower = (s8) PHY_GetTxPowerIndexBase( pAdapter, RFPath, Rate, BandWidth, Channel, &bIn24G );

	powerDiffByRate = PHY_GetTxPowerByRate( pAdapter, (u8)(!bIn24G), RFPath, tx_num, Rate );

	limit = PHY_GetTxPowerLimit( pAdapter, pAdapter->registrypriv.RegPwrTblSel, (u8)(!bIn24G), pHalData->CurrentChannelBW, RFPath, Rate, pHalData->CurrentChannel);

	powerDiffByRate = powerDiffByRate > limit ? limit : powerDiffByRate;
	//DBG_871X("Rate-0x%x: (TxPower, PowerDiffByRate Path-%c) = (0x%X, %d)\n", Rate, ((RFPath==0)?'A':'B'), txPower, powerDiffByRate);

	// We need to reduce power index for VHT MCS 8 & 9.
	if (Rate == MGN_VHT1SS_MCS8 || Rate == MGN_VHT1SS_MCS9 ||
		Rate == MGN_VHT2SS_MCS8 || Rate == MGN_VHT2SS_MCS9)
	{
		txPower += powerDiffByRate;
	}
	else
	{
#ifdef CONFIG_USB_HCI
		//
		// 2013/01/29 MH For preventing VHT rate of 8812AU to be used in USB 2.0 mode
		// and the current will be more than 500mA and card disappear. We need to limit 
		// TX power with any power by rate for VHT in U2.
		// 2013/01/30 MH According to power current test compare with BCM AC NIC, we
		// decide to use host hub = 2.0 mode to enable tx power limit behavior.
		//
		if (adapter_to_dvobj(pAdapter)->usb_speed == RTW_USB_SPEED_2 && IS_HARDWARE_TYPE_8812AU(pAdapter))
		{
			powerDiffByRate = 0;
		}
#endif
		txPower += powerDiffByRate;
#if 0
		//
		// 2013/02/06 MH Add for ASUS requiremen for adjusting TX power limit.
		// This is a temporarily dirty fix for asus , neeed to revise later!
		// 2013/03/07 MH Asus add more request.
		// 2013/03/14 MH Asus add one more request for the power control.
		//
		if (Channel >= 36)
		{			
			txPower += pMgntInfo->RegTPCLvl5g;

			if (txPower > pMgntInfo->RegTPCLvl5gD)
				txPower -= pMgntInfo->RegTPCLvl5gD;
		}
		else
		{		
			txPower += pMgntInfo->RegTPCLvl;

			if (txPower > pMgntInfo->RegTPCLvlD)
				txPower -= pMgntInfo->RegTPCLvlD;
		}
#endif
	}	
	
	txPower += PHY_GetTxPowerTrackingOffset( pAdapter, RFPath, Rate );
	
	// 2012/09/26 MH We need to take care high power device limiation to prevent destroy EXT_PA.
	// This case had ever happened in CU/SU high power module. THe limitation = 0x20.
	// But for 8812, we still not know the value.
	phy_TxPwrAdjInPercentage(pAdapter, (u8 *)&txPower);

	if(txPower > MAX_POWER_INDEX)
		txPower = MAX_POWER_INDEX;

	if ( txPower % 2 == 1 && !IS_NORMAL_CHIP(pHalData->VersionID))
		--txPower;

	//DBG_871X("Final Tx Power(RF-%c, Channel: %d) = %d(0x%X)\n", ((RFPath==0)?'A':'B'), Channel,txPower, txPower);

	return (u8) txPower;
}

/**************************************************************************************************************
 *   Description: 
 *       The low-level interface to set TxAGC , called by both MP and Normal Driver.
 *
 *                                                                                    <20120830, Kordan>
 **************************************************************************************************************/

VOID
PHY_SetTxPowerIndex_8812A(
	IN	PADAPTER		Adapter,
	IN	u32				PowerIndex,
	IN	u8				RFPath,	
	IN	u8				Rate
	)
{
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(Adapter);
	BOOLEAN				Direction = _FALSE;
	u32				TxagcOffset = 0;

	// <20120928, Kordan> A workaround in 8812A/8821A testchip, to fix the bug of odd Tx power indexes.
	if ( (PowerIndex % 2 == 1) && IS_HARDWARE_TYPE_JAGUAR(Adapter) && IS_TEST_CHIP(pHalData->VersionID) )
		PowerIndex -= 1;

	//2013.01.18 LukeLee: Modify TXAGC by dcmd_Dynamic_Ctrl()
	if(RFPath == RF_PATH_A)
	{
		Direction = pHalData->odmpriv.IsTxagcOffsetPositiveA;
		TxagcOffset = pHalData->odmpriv.TxagcOffsetValueA;
	}
	else if(RFPath == RF_PATH_B)
	{
		Direction = pHalData->odmpriv.IsTxagcOffsetPositiveB;
		TxagcOffset = pHalData->odmpriv.TxagcOffsetValueB;
	}
	if(Direction == _FALSE)
	{
		if(PowerIndex > TxagcOffset)
			PowerIndex -= TxagcOffset;
		else
			PowerIndex = 0;
	}
	else
	{
		PowerIndex += TxagcOffset;
		if(PowerIndex > 0x3F)
			PowerIndex = 0x3F;
	}

	if (RFPath == RF_PATH_A)
	{
		switch (Rate)
		{
			case MGN_1M:    PHY_SetBBReg(Adapter, rTxAGC_A_CCK11_CCK1_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_2M:    PHY_SetBBReg(Adapter, rTxAGC_A_CCK11_CCK1_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_5_5M:  PHY_SetBBReg(Adapter, rTxAGC_A_CCK11_CCK1_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_11M:   PHY_SetBBReg(Adapter, rTxAGC_A_CCK11_CCK1_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_6M:    PHY_SetBBReg(Adapter, rTxAGC_A_Ofdm18_Ofdm6_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_9M:    PHY_SetBBReg(Adapter, rTxAGC_A_Ofdm18_Ofdm6_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_12M:   PHY_SetBBReg(Adapter, rTxAGC_A_Ofdm18_Ofdm6_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_18M:   PHY_SetBBReg(Adapter, rTxAGC_A_Ofdm18_Ofdm6_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_24M:   PHY_SetBBReg(Adapter, rTxAGC_A_Ofdm54_Ofdm24_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_36M:   PHY_SetBBReg(Adapter, rTxAGC_A_Ofdm54_Ofdm24_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_48M:   PHY_SetBBReg(Adapter, rTxAGC_A_Ofdm54_Ofdm24_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_54M:   PHY_SetBBReg(Adapter, rTxAGC_A_Ofdm54_Ofdm24_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_MCS0:  PHY_SetBBReg(Adapter, rTxAGC_A_MCS3_MCS0_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_MCS1:  PHY_SetBBReg(Adapter, rTxAGC_A_MCS3_MCS0_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_MCS2:  PHY_SetBBReg(Adapter, rTxAGC_A_MCS3_MCS0_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_MCS3:  PHY_SetBBReg(Adapter, rTxAGC_A_MCS3_MCS0_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_MCS4:  PHY_SetBBReg(Adapter, rTxAGC_A_MCS7_MCS4_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_MCS5:  PHY_SetBBReg(Adapter, rTxAGC_A_MCS7_MCS4_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_MCS6:  PHY_SetBBReg(Adapter, rTxAGC_A_MCS7_MCS4_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_MCS7:  PHY_SetBBReg(Adapter, rTxAGC_A_MCS7_MCS4_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_MCS8:  PHY_SetBBReg(Adapter, rTxAGC_A_MCS11_MCS8_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_MCS9:  PHY_SetBBReg(Adapter, rTxAGC_A_MCS11_MCS8_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_MCS10: PHY_SetBBReg(Adapter, rTxAGC_A_MCS11_MCS8_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_MCS11: PHY_SetBBReg(Adapter, rTxAGC_A_MCS11_MCS8_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_MCS12: PHY_SetBBReg(Adapter, rTxAGC_A_MCS15_MCS12_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_MCS13: PHY_SetBBReg(Adapter, rTxAGC_A_MCS15_MCS12_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_MCS14: PHY_SetBBReg(Adapter, rTxAGC_A_MCS15_MCS12_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_MCS15: PHY_SetBBReg(Adapter, rTxAGC_A_MCS15_MCS12_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_VHT1SS_MCS0: PHY_SetBBReg(Adapter, rTxAGC_A_Nss1Index3_Nss1Index0_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_VHT1SS_MCS1: PHY_SetBBReg(Adapter, rTxAGC_A_Nss1Index3_Nss1Index0_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_VHT1SS_MCS2: PHY_SetBBReg(Adapter, rTxAGC_A_Nss1Index3_Nss1Index0_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_VHT1SS_MCS3: PHY_SetBBReg(Adapter, rTxAGC_A_Nss1Index3_Nss1Index0_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_VHT1SS_MCS4: PHY_SetBBReg(Adapter, rTxAGC_A_Nss1Index7_Nss1Index4_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_VHT1SS_MCS5: PHY_SetBBReg(Adapter, rTxAGC_A_Nss1Index7_Nss1Index4_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_VHT1SS_MCS6: PHY_SetBBReg(Adapter, rTxAGC_A_Nss1Index7_Nss1Index4_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_VHT1SS_MCS7: PHY_SetBBReg(Adapter, rTxAGC_A_Nss1Index7_Nss1Index4_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_VHT1SS_MCS8: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index1_Nss1Index8_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_VHT1SS_MCS9: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index1_Nss1Index8_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_VHT2SS_MCS0: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index1_Nss1Index8_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_VHT2SS_MCS1: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index1_Nss1Index8_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_VHT2SS_MCS2: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index5_Nss2Index2_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_VHT2SS_MCS3: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index5_Nss2Index2_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_VHT2SS_MCS4: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index5_Nss2Index2_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_VHT2SS_MCS5: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index5_Nss2Index2_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_VHT2SS_MCS6: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index9_Nss2Index6_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_VHT2SS_MCS7: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index9_Nss2Index6_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_VHT2SS_MCS8: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index9_Nss2Index6_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_VHT2SS_MCS9: PHY_SetBBReg(Adapter, rTxAGC_A_Nss2Index9_Nss2Index6_JAguar, bMaskByte3, PowerIndex); break;

			default:
				DBG_871X("Invalid Rate!!\n");
				break;				
		}
	}
	else if (RFPath == RF_PATH_B)
	{
		switch (Rate)
		{
			case MGN_1M:    PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_CCK1_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_2M:    PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_CCK1_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_5_5M:  PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_CCK1_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_11M:   PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_CCK1_JAguar, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_6M:    PHY_SetBBReg(Adapter, rTxAGC_B_Ofdm18_Ofdm6_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_9M:    PHY_SetBBReg(Adapter, rTxAGC_B_Ofdm18_Ofdm6_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_12M:   PHY_SetBBReg(Adapter, rTxAGC_B_Ofdm18_Ofdm6_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_18M:   PHY_SetBBReg(Adapter, rTxAGC_B_Ofdm18_Ofdm6_JAguar, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_24M:   PHY_SetBBReg(Adapter, rTxAGC_B_Ofdm54_Ofdm24_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_36M:   PHY_SetBBReg(Adapter, rTxAGC_B_Ofdm54_Ofdm24_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_48M:   PHY_SetBBReg(Adapter, rTxAGC_B_Ofdm54_Ofdm24_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_54M:   PHY_SetBBReg(Adapter, rTxAGC_B_Ofdm54_Ofdm24_JAguar, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_MCS0:  PHY_SetBBReg(Adapter, rTxAGC_B_MCS3_MCS0_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_MCS1:  PHY_SetBBReg(Adapter, rTxAGC_B_MCS3_MCS0_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_MCS2:  PHY_SetBBReg(Adapter, rTxAGC_B_MCS3_MCS0_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_MCS3:  PHY_SetBBReg(Adapter, rTxAGC_B_MCS3_MCS0_JAguar, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_MCS4:  PHY_SetBBReg(Adapter, rTxAGC_B_MCS7_MCS4_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_MCS5:  PHY_SetBBReg(Adapter, rTxAGC_B_MCS7_MCS4_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_MCS6:  PHY_SetBBReg(Adapter, rTxAGC_B_MCS7_MCS4_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_MCS7:  PHY_SetBBReg(Adapter, rTxAGC_B_MCS7_MCS4_JAguar, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_MCS8:  PHY_SetBBReg(Adapter, rTxAGC_B_MCS11_MCS8_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_MCS9:  PHY_SetBBReg(Adapter, rTxAGC_B_MCS11_MCS8_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_MCS10: PHY_SetBBReg(Adapter, rTxAGC_B_MCS11_MCS8_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_MCS11: PHY_SetBBReg(Adapter, rTxAGC_B_MCS11_MCS8_JAguar, bMaskByte3, PowerIndex); break;
			                                             
			case MGN_MCS12: PHY_SetBBReg(Adapter, rTxAGC_B_MCS15_MCS12_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_MCS13: PHY_SetBBReg(Adapter, rTxAGC_B_MCS15_MCS12_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_MCS14: PHY_SetBBReg(Adapter, rTxAGC_B_MCS15_MCS12_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_MCS15: PHY_SetBBReg(Adapter, rTxAGC_B_MCS15_MCS12_JAguar, bMaskByte3, PowerIndex); break;

			case MGN_VHT1SS_MCS0: PHY_SetBBReg(Adapter, rTxAGC_B_Nss1Index3_Nss1Index0_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_VHT1SS_MCS1: PHY_SetBBReg(Adapter, rTxAGC_B_Nss1Index3_Nss1Index0_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_VHT1SS_MCS2: PHY_SetBBReg(Adapter, rTxAGC_B_Nss1Index3_Nss1Index0_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_VHT1SS_MCS3: PHY_SetBBReg(Adapter, rTxAGC_B_Nss1Index3_Nss1Index0_JAguar, bMaskByte3, PowerIndex); break;
			                                                   
			case MGN_VHT1SS_MCS4: PHY_SetBBReg(Adapter, rTxAGC_B_Nss1Index7_Nss1Index4_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_VHT1SS_MCS5: PHY_SetBBReg(Adapter, rTxAGC_B_Nss1Index7_Nss1Index4_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_VHT1SS_MCS6: PHY_SetBBReg(Adapter, rTxAGC_B_Nss1Index7_Nss1Index4_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_VHT1SS_MCS7: PHY_SetBBReg(Adapter, rTxAGC_B_Nss1Index7_Nss1Index4_JAguar, bMaskByte3, PowerIndex); break;
			                                                   
			case MGN_VHT1SS_MCS8: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index1_Nss1Index8_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_VHT1SS_MCS9: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index1_Nss1Index8_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_VHT2SS_MCS0: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index1_Nss1Index8_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_VHT2SS_MCS1: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index1_Nss1Index8_JAguar, bMaskByte3, PowerIndex); break;
			                                                   
			case MGN_VHT2SS_MCS2: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index5_Nss2Index2_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_VHT2SS_MCS3: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index5_Nss2Index2_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_VHT2SS_MCS4: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index5_Nss2Index2_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_VHT2SS_MCS5: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index5_Nss2Index2_JAguar, bMaskByte3, PowerIndex); break;
			                                                   
			case MGN_VHT2SS_MCS6: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index9_Nss2Index6_JAguar, bMaskByte0, PowerIndex); break;
			case MGN_VHT2SS_MCS7: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index9_Nss2Index6_JAguar, bMaskByte1, PowerIndex); break;
			case MGN_VHT2SS_MCS8: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index9_Nss2Index6_JAguar, bMaskByte2, PowerIndex); break;
			case MGN_VHT2SS_MCS9: PHY_SetBBReg(Adapter, rTxAGC_B_Nss2Index9_Nss2Index6_JAguar, bMaskByte3, PowerIndex); break;

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

BOOLEAN
PHY_UpdateTxPowerDbm8812(
	IN	PADAPTER	Adapter,
	IN	int		powerInDbm
	)
{
	return _TRUE;
}


u32 PHY_GetTxBBSwing_8812A(
	IN	PADAPTER	Adapter,
	IN	BAND_TYPE 	Band,
	IN	u8			RFPath
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(GetDefaultAdapter(Adapter));
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	PODM_RF_CAL_T  	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	s8	bbSwing_2G = -1 * GetRegTxBBSwing_2G(Adapter);
	s8	bbSwing_5G = -1 * GetRegTxBBSwing_5G(Adapter);
	u32	out = 0x200;
	const s8	AUTO = -1;
	

	if (pEEPROM->bautoload_fail_flag) 
	{
		if ( Band == BAND_ON_2_4G ) {
			pRFCalibrateInfo->BBSwingDiff2G = bbSwing_2G;
			if      (bbSwing_2G == 0)  out = 0x200; //  0 dB
		        else if (bbSwing_2G == -3) out = 0x16A; // -3 dB
		        else if (bbSwing_2G == -6) out = 0x101; // -6 dB
		        else if (bbSwing_2G == -9) out = 0x0B6; // -9 dB
		        else {
				if ( pHalData->ExternalPA_2G ) {
					pRFCalibrateInfo->BBSwingDiff2G = -3;
					out = 0x16A;
				} else  {
					pRFCalibrateInfo->BBSwingDiff2G = 0;
					out = 0x200;
				}
			}
		} else if ( Band == BAND_ON_5G ) {
			pRFCalibrateInfo->BBSwingDiff5G = bbSwing_5G;
			if      (bbSwing_5G == 0)  out = 0x200; //  0 dB
			else if (bbSwing_5G == -3) out = 0x16A; // -3 dB
			else if (bbSwing_5G == -6) out = 0x101; // -6 dB
			else if (bbSwing_5G == -9) out = 0x0B6; // -9 dB
			else {
				if ( pHalData->ExternalPA_5G || IS_HARDWARE_TYPE_8821(Adapter)) {
					pRFCalibrateInfo->BBSwingDiff5G = -3;
					out = 0x16A;
				} else {
					pRFCalibrateInfo->BBSwingDiff5G = 0;
					out = 0x200;
				}
			}
		} else  {
	        	pRFCalibrateInfo->BBSwingDiff2G = -3;
       	 	pRFCalibrateInfo->BBSwingDiff5G = -3;			
			out = 0x16A; // -3 dB
		}
	}
	else
	{
		u32 swing = 0, onePathSwing = 0;

		if (Band == BAND_ON_2_4G) {
			if (GetRegTxBBSwing_2G(Adapter) == AUTO)
			{
				EFUSE_ShadowRead(Adapter, 1, EEPROM_TX_BBSWING_2G_8812, (u32 *)&swing);
				swing = (swing == 0xFF) ? 0x00 : swing;
			}			
			else if (bbSwing_2G ==  0) swing = 0x00; //  0 dB
			else if (bbSwing_2G == -3) swing = 0x05; // -3 dB
			else if (bbSwing_2G == -6) swing = 0x0A; // -6 dB
			else if (bbSwing_2G == -9) swing = 0xFF; // -9 dB
			else swing = 0x00;
		}
		else {
			if (GetRegTxBBSwing_5G(Adapter) == AUTO)
			{
				EFUSE_ShadowRead(Adapter, 1, EEPROM_TX_BBSWING_5G_8812, (u32 *)&swing);
				swing = (swing == 0xFF) ? 0x00 : swing;
			}
			else if (bbSwing_5G ==  0) swing = 0x00; //  0 dB
			else if (bbSwing_5G == -3) swing = 0x05; // -3 dB
			else if (bbSwing_5G == -6) swing = 0x0A; // -6 dB
			else if (bbSwing_5G == -9) swing = 0xFF; // -9 dB
			else swing = 0x00;
		}

		if (RFPath == ODM_RF_PATH_A)
			onePathSwing = (swing & 0x3) >> 0; // 0xC6/C7[1:0]
		else if(RFPath == ODM_RF_PATH_B)		
			onePathSwing = (swing & 0xC) >> 2; // 0xC6/C7[3:2]

		if (onePathSwing == 0x0) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = 0;
			else
				pRFCalibrateInfo->BBSwingDiff5G = 0;
			out = 0x200; // 0 dB
		} else if (onePathSwing == 0x1) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = -3;
			else
				pRFCalibrateInfo->BBSwingDiff5G = -3;
			out = 0x16A; // -3 dB
		} else if (onePathSwing == 0x2) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = -6;
			else
				pRFCalibrateInfo->BBSwingDiff5G = -6;
			out = 0x101; // -6 dB
		} else if (onePathSwing == 0x3) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = -9;
			else
				pRFCalibrateInfo->BBSwingDiff5G = -9;
			out = 0x0B6; // -9 dB
		}
	}

	//DBG_871X("<=== PHY_GetTxBBSwing_8812A, out = 0x%X\n", out);

	return out;
}

VOID
phy_SetRFEReg8812(
	IN PADAPTER		Adapter,
	IN u8			Band
)
{
	u1Byte			u1tmp = 0;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	if(Band == BAND_ON_2_4G)
	{
		switch(pHalData->RFEType){
		case 0: case 2:
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);
			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);
			PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
			break;
		case 1:
#ifdef CONFIG_BT_COEXIST
			if (hal_btcoex_IsBtExist(Adapter) && (Adapter->registrypriv.mp_mode==0))
			{				
				PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,0xffffff, 0x777777);
				PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);
				PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar, 0x33f00000, 0x000);
				PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
			}
			else
#endif
			{
				PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);
				PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);
				PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
				PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
			}	
			break;
		case 3:
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x54337770);
			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x54337770);
			PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x010);
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x010);
			PHY_SetBBReg(Adapter, r_ANTSEL_SW_Jaguar,0x00000303, 0x1);
			break;
		case 4:
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);
			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);
			PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x001);
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x001);
			break;
		case 5:
			rtw_write8(Adapter, rA_RFE_Pinmux_Jaguar+2, 0x77);

			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);
			u1tmp = rtw_read8(Adapter, rA_RFE_Inv_Jaguar+3);
			rtw_write8(Adapter, rA_RFE_Inv_Jaguar+3,  (u1tmp &= ~BIT0));
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar, bMask_RFEInv_Jaguar, 0x000);
			break;
		default:
			break;
       	}
	}
	else
	{
		switch(pHalData->RFEType){
		case 0:
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337717);
			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337717);
			PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x010);
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x010);
			break;
		case 1:
#ifdef CONFIG_BT_COEXIST
			if (hal_btcoex_IsBtExist(Adapter) && (Adapter->registrypriv.mp_mode==0))
			{			
				PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,0xffffff, 0x337717);
				PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337717);
				PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar, 0x33f00000, 0x000);
				PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
			}
			else
#endif
			{
				PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337717);
				PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337717);
				PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
				PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
			}
			break;			
		case 2:
		case 4:
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337777);
			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337777);
			PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x010);
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x010);
			break;
		case 3:
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x54337717);
			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x54337717);
			PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x010);
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x010);
			PHY_SetBBReg(Adapter, r_ANTSEL_SW_Jaguar,0x00000303, 0x1);
			break;
		case 5:
			rtw_write8(Adapter, rA_RFE_Pinmux_Jaguar+2, 0x33);
			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337777);
			u1tmp = rtw_read8(Adapter, rA_RFE_Inv_Jaguar+3);
			rtw_write8(Adapter, rA_RFE_Inv_Jaguar+3,  (u1tmp |= BIT0));
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar, bMask_RFEInv_Jaguar, 0x010);
			break;
		default:
			break;
		}
	}
}

void phy_SetBBSwingByBand_8812A(
	IN PADAPTER		Adapter,
	IN u8			Band,
	IN u1Byte		PreviousBand
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(GetDefaultAdapter(Adapter));

	//<20120903, Kordan> Tx BB swing setting for RL6286, asked by Ynlin.
	if (IS_NORMAL_CHIP(pHalData->VersionID) || IS_HARDWARE_TYPE_8821(Adapter))
	{
#if (MP_DRIVER == 1)		
		PMPT_CONTEXT	pMptCtx = &(Adapter->mppriv.MptCtx);
#endif
		s8	BBDiffBetweenBand = 0; 		
		PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
		PODM_RF_CAL_T  	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
		u8 	path = ODM_RF_PATH_A;

		PHY_SetBBReg(Adapter, rA_TxScale_Jaguar, 0xFFE00000, 
					 PHY_GetTxBBSwing_8812A(Adapter, (BAND_TYPE)Band, ODM_RF_PATH_A)); // 0xC1C[31:21]
		PHY_SetBBReg(Adapter, rB_TxScale_Jaguar, 0xFFE00000, 
					 PHY_GetTxBBSwing_8812A(Adapter, (BAND_TYPE)Band, ODM_RF_PATH_B)); // 0xE1C[31:21]

		// <20121005, Kordan> When TxPowerTrack is ON, we should take care of the change of BB swing.
		// That is, reset all info to trigger Tx power tracking.
		{
#if (MP_DRIVER == 1)
			path = pMptCtx->MptRfPath;
#endif

			if (Band != PreviousBand) 
			{
				BBDiffBetweenBand = (pRFCalibrateInfo->BBSwingDiff2G - pRFCalibrateInfo->BBSwingDiff5G);
				BBDiffBetweenBand = (Band == BAND_ON_2_4G) ? BBDiffBetweenBand : (-1 * BBDiffBetweenBand);
				pDM_Odm->DefaultOfdmIndex += BBDiffBetweenBand*2;
			}

			ODM_ClearTxPowerTrackingState(pDM_Odm);
		}
	}
}

s32
PHY_SwitchWirelessBand8812(
	IN PADAPTER		Adapter,
	IN u8			Band
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	u8				currentBand = pHalData->CurrentBandType;

	//DBG_871X("==>PHY_SwitchWirelessBand8812() %s\n", ((Band==0)?"2.4G":"5G"));

	pHalData->CurrentBandType =(BAND_TYPE)Band;
	
	if(Band == BAND_ON_2_4G)
	{// 2.4G band

		// STOP Tx/Rx
		PHY_SetBBReg(Adapter, rOFDMCCKEN_Jaguar, bOFDMEN_Jaguar|bCCKEN_Jaguar, 0x00);

		if (IS_HARDWARE_TYPE_8821(Adapter)) 
		{
			// Turn off RF PA and LNA
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0xF000, 0x7); 	// 0xCB0[15:12] = 0x7 (LNA_On)
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0xF0, 0x7); 	// 0xCB0[7:4] = 0x7 (PAPE_A)

			if (pHalData->ExternalLNA_2G) { 
				// <20130717, Kordan> Turn on 2.4G External LNA. (Asked by Alvin)
				PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar, BIT20, 1); 
				PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar, BIT22, 0);
				PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, BIT2|BIT1|BIT0, 0x2); // 0xCB0[2:0] = b'010 
				PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, BIT10|BIT9|BIT8, 0x2); // 0xCB0[10:8] = b'010				
			} 
		}

		if(IS_HARDWARE_TYPE_8812(Adapter))
		{
			PHY_SetBBReg(Adapter,rPwed_TH_Jaguar, 0xE, 0x4);		 	// 0x830[3:1] = 0x4 
			PHY_SetBBReg(Adapter,rBWIndication_Jaguar,0x3,0x1);			// 0x834[1:0] = 0x1
		}

		// AGC table select 
		if(IS_VENDOR_8821A_MP_CHIP(Adapter))
			PHY_SetBBReg(Adapter, rA_TxScale_Jaguar, 0xF00, 0); 		// 0xC1C[11:8] = 0
		else
			PHY_SetBBReg(Adapter, rAGC_table_Jaguar, 0x3, 0);			// 0x82C[1:0] = 2b'00

		if(IS_HARDWARE_TYPE_8812(Adapter))
		{
			if(GetRegbENRFEType(Adapter))
				phy_SetRFEReg8812(Adapter, Band);
			else
			{
				// PAPE_A (bypass RFE module in 2G)
				PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0x000000F0, 0x7);
				PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar, 0x000000F0, 0x7);	
				
				// PAPE_G (bypass RFE module in 5G)
				if (pHalData->ExternalPA_2G) {
					PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0x0000000F, 0x0);
					PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar, 0x0000000F, 0x0);
				} else {
					PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0x0000000F, 0x7);
					PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar, 0x0000000F, 0x7);				
				}

				// TRSW bypass RFE moudle in 2G
				if (pHalData->ExternalLNA_2G) {
					PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, bMaskByte2, 0x54);
					PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar, bMaskByte2, 0x54);
				} else {
					PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, bMaskByte2, 0x77);
					PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar, bMaskByte2, 0x77);
				}
			}
		}

		update_tx_basic_rate(Adapter, WIRELESS_11BG);

		// cck_enable
		PHY_SetBBReg(Adapter, rOFDMCCKEN_Jaguar, bOFDMEN_Jaguar|bCCKEN_Jaguar, 0x3);

		// CCK_CHECK_en
		rtw_write8(Adapter, REG_CCK_CHECK_8812, 0x0);
	}
	else	//5G band
	{
		u16	count = 0, reg41A = 0;

		if (IS_HARDWARE_TYPE_8821(Adapter)) 
		{
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0xF000, 0x5); 	// 0xCB0[15:12] = 0x5 (LNA_On)
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0xF0, 0x4); 	// 0xCB0[7:4] = 0x4 (PAPE_A)

			if (pHalData->ExternalLNA_2G) { 
				// <20130717, Kordan> Bypass the 2.4G External LNA. (Asked by Alvin)
				PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar, BIT20, 0); 
				PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar, BIT22, 0); 
				PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, BIT2|BIT1|BIT0, 0x7); // 0xCB0[2:0] = b'111 
				PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, BIT10|BIT9|BIT8, 0x7); // 0xCB0[10:8] = b'111
			} 
		}

		// CCK_CHECK_en
		rtw_write8(Adapter, REG_CCK_CHECK_8812, 0x80);

		count = 0;
		reg41A = rtw_read16(Adapter, REG_TXPKT_EMPTY);
		//DBG_871X("Reg41A value %d", reg41A);
		reg41A &= 0x30;
		while((reg41A!= 0x30) && (count < 50))
		{
			rtw_udelay_os(50);
			//DBG_871X("Delay 50us \n");

			reg41A = rtw_read16(Adapter, REG_TXPKT_EMPTY);
			reg41A &= 0x30;
			count++;
			//DBG_871X("Reg41A value %d", reg41A);
		}
		if(count != 0)
			DBG_871X("PHY_SwitchWirelessBand8812(): Switch to 5G Band. Count = %d reg41A=0x%x\n", count, reg41A);

		// STOP Tx/Rx
		PHY_SetBBReg(Adapter, rOFDMCCKEN_Jaguar, bOFDMEN_Jaguar|bCCKEN_Jaguar, 0x00);

		if(IS_HARDWARE_TYPE_8812(Adapter))
		{
			PHY_SetBBReg(Adapter,rPwed_TH_Jaguar, 0xE, 0x3);		// 0x830[3:1] = 0x3
			PHY_SetBBReg(Adapter,rBWIndication_Jaguar,0x3,0x2);	// 0x834[1:0] = 0x2
		}

		// AGC table select 
		if (IS_VENDOR_8821A_MP_CHIP(Adapter))
			PHY_SetBBReg(Adapter, rA_TxScale_Jaguar, 0xF00, 1);		// 0xC1C[11:8] = 1
		else		
			PHY_SetBBReg(Adapter, rAGC_table_Jaguar, 0x3, 1);		// 0x82C[1:0] = 2'b00

		if(IS_HARDWARE_TYPE_8812(Adapter))
		{
			if(GetRegbENRFEType(Adapter))
				phy_SetRFEReg8812(Adapter, Band);
			else
			{
				// PAPE_A (bypass RFE module in 2G)
				if (pHalData->ExternalPA_5G) { 
					PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0x000000F0, 0x1);
					PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar, 0x000000F0, 0x1);
				} else {
					PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0x000000F0, 0x0);
					PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar, 0x000000F0, 0x0);
				}
				
				// PAPE_G (bypass RFE module in 5G)
				PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0x0000000F, 0x7);
				PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar, 0x0000000F, 0x7);

				// TRSW bypass RFE moudle in 2G
				if (pHalData->ExternalLNA_5G) {
					PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, bMaskByte2, 0x54);
					PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar, bMaskByte2, 0x54);
				} else {
					PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, bMaskByte2, 0x77);
					PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar, bMaskByte2, 0x77);
				}
			}
		}

		//avoid using cck rate in 5G band
		// Set RRSR rate table.
		update_tx_basic_rate(Adapter, WIRELESS_11A);

		// cck_enable
		PHY_SetBBReg(Adapter, rOFDMCCKEN_Jaguar, bOFDMEN_Jaguar|bCCKEN_Jaguar, 0x2);

		//DBG_871X("==>PHY_SwitchWirelessBand8812() BAND_ON_5G settings OFDM index 0x%x\n", pHalData->OFDM_index[RF_PATH_A]);
	}

	phy_SetBBSwingByBand_8812A(Adapter, Band, currentBand);

	//DBG_871X("<==PHY_SwitchWirelessBand8812():Switch Band OK.\n");
	return _SUCCESS;	
}

BOOLEAN
phy_SwBand8812(
	IN	PADAPTER	pAdapter,
	IN	u8			channelToSW
)
{
	u8			u1Btmp; 
	BOOLEAN		ret_value = _TRUE;
	u8			Band = BAND_ON_5G, BandToSW;

	u1Btmp = rtw_read8(pAdapter, REG_CCK_CHECK_8812);
	if(u1Btmp & BIT7)
		Band = BAND_ON_5G;
	else
		Band = BAND_ON_2_4G;

	// Use current channel to judge Band Type and switch Band if need.
	if(channelToSW > 14)
	{
		BandToSW = BAND_ON_5G;
	}
	else
	{
		BandToSW = BAND_ON_2_4G;
	}

	if(BandToSW != Band)
		PHY_SwitchWirelessBand8812(pAdapter,BandToSW);

	return ret_value;
}

u8 
phy_GetSecondaryChnl_8812(
	IN	PADAPTER	Adapter
)
{
	u8					SCSettingOf40 = 0, SCSettingOf20 = 0;
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);

	//DBG_871X("SCMapping: Case: pHalData->CurrentChannelBW %d, pHalData->nCur80MhzPrimeSC %d, pHalData->nCur40MhzPrimeSC %d \n",pHalData->CurrentChannelBW,pHalData->nCur80MhzPrimeSC,pHalData->nCur40MhzPrimeSC);
	if(pHalData->CurrentChannelBW== CHANNEL_WIDTH_80)
	{
		if(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			SCSettingOf40 = VHT_DATA_SC_40_LOWER_OF_80MHZ;
		else if(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			SCSettingOf40 = VHT_DATA_SC_40_UPPER_OF_80MHZ;
		else
			DBG_871X("SCMapping: Not Correct Primary80MHz Setting \n");
		
		if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
			SCSettingOf20 = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
			SCSettingOf20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
			SCSettingOf20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
			SCSettingOf20 = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
		else
			DBG_871X("SCMapping: Not Correct Primary80MHz Setting \n");
	}
	else if(pHalData->CurrentChannelBW == CHANNEL_WIDTH_40)
	{
		//DBG_871X("SCMapping: Case: pHalData->CurrentChannelBW %d, pHalData->nCur40MhzPrimeSC %d \n",pHalData->CurrentChannelBW,pHalData->nCur40MhzPrimeSC);

		if(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			SCSettingOf20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			SCSettingOf20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else
			DBG_871X("SCMapping: Not Correct Primary40MHz Setting \n");
	}

	//DBG_871X("SCMapping: SC Value %x \n", ( (SCSettingOf40 << 4) | SCSettingOf20));
	return  ( (SCSettingOf40 << 4) | SCSettingOf20);
}

VOID
phy_SetRegBW_8812(
	IN	PADAPTER		Adapter,
	CHANNEL_WIDTH 	CurrentBW
)	
{
	u16	RegRfMod_BW, u2tmp = 0;
	RegRfMod_BW = rtw_read16(Adapter, REG_WMAC_TRXPTCL_CTL);

	switch(CurrentBW)
	{
		case CHANNEL_WIDTH_20:
			rtw_write16(Adapter, REG_WMAC_TRXPTCL_CTL, (RegRfMod_BW & 0xFE7F)); // BIT 7 = 0, BIT 8 = 0
			break;

		case CHANNEL_WIDTH_40:
			u2tmp = RegRfMod_BW | BIT7;
			rtw_write16(Adapter, REG_WMAC_TRXPTCL_CTL, (u2tmp & 0xFEFF)); // BIT 7 = 1, BIT 8 = 0
			break;

		case CHANNEL_WIDTH_80:
			u2tmp = RegRfMod_BW | BIT8;
			rtw_write16(Adapter, REG_WMAC_TRXPTCL_CTL, (u2tmp & 0xFF7F)); // BIT 7 = 0, BIT 8 = 1
			break;

		default:
			DBG_871X("phy_PostSetBWMode8812():	unknown Bandwidth: %#X\n",CurrentBW);
			break;
	}

}

void 
phy_FixSpur_8812A(
	IN	PADAPTER	        pAdapter,
	IN  CHANNEL_WIDTH    Bandwidth,
	IN  u1Byte 			    Channel
)
{
	// C cut Item12 ADC FIFO CLOCK
	if(IS_VENDOR_8812A_C_CUT(pAdapter))
	{
		if(Bandwidth == CHANNEL_WIDTH_40 && Channel == 11)		
			PHY_SetBBReg(pAdapter, rRFMOD_Jaguar, 0xC00, 0x3)	;		// 0x8AC[11:10] = 2'b11
		else
			PHY_SetBBReg(pAdapter, rRFMOD_Jaguar, 0xC00, 0x2);		// 0x8AC[11:10] = 2'b10

		// <20120914, Kordan> A workarould to resolve 2480Mhz spur by setting ADC clock as 160M. (Asked by Binson)
		if (Bandwidth == CHANNEL_WIDTH_20 && 
			(Channel == 13 || Channel == 14)) {
			
			PHY_SetBBReg(pAdapter, rRFMOD_Jaguar, 0x300, 0x3);  		// 0x8AC[9:8] = 2'b11
			PHY_SetBBReg(pAdapter, rADC_Buf_Clk_Jaguar, BIT30, 1);  	// 0x8C4[30] = 1
			
		} else if (Bandwidth == CHANNEL_WIDTH_40 && 
			Channel == 11) {

			PHY_SetBBReg(pAdapter, rADC_Buf_Clk_Jaguar, BIT30, 1);  	// 0x8C4[30] = 1

		} else if (Bandwidth != CHANNEL_WIDTH_80) {
		
			PHY_SetBBReg(pAdapter, rRFMOD_Jaguar, 0x300, 0x2);  		// 0x8AC[9:8] = 2'b10	
			PHY_SetBBReg(pAdapter, rADC_Buf_Clk_Jaguar, BIT30, 0);  	// 0x8C4[30] = 0

		}
	}
	else if (IS_HARDWARE_TYPE_8812(pAdapter))
	{
		// <20120914, Kordan> A workarould to resolve 2480Mhz spur by setting ADC clock as 160M. (Asked by Binson)
		if (Bandwidth == CHANNEL_WIDTH_20 && 
			(Channel == 13 || Channel == 14))
			PHY_SetBBReg(pAdapter, rRFMOD_Jaguar, 0x300, 0x3);  // 0x8AC[9:8] = 11
		else if (Channel <= 14) // 2.4G only
			PHY_SetBBReg(pAdapter, rRFMOD_Jaguar, 0x300, 0x2);  // 0x8AC[9:8] = 10
	}

}

VOID
phy_PostSetBwMode8812(
	IN	PADAPTER	Adapter
)
{
	u8			SubChnlNum = 0;
	u8			L1pkVal = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);


	//3 Set Reg668 BW
	phy_SetRegBW_8812(Adapter, pHalData->CurrentChannelBW);

	//3 Set Reg483
	SubChnlNum = phy_GetSecondaryChnl_8812(Adapter);
	rtw_write8(Adapter, REG_DATA_SC_8812, SubChnlNum);

	if(pHalData->rf_chip == RF_PSEUDO_11N)
	{
		DBG_871X("phy_PostSetBwMode8812: return for PSEUDO \n");
		return;
	}

	//DBG_871X("[BW:CHNL], phy_PostSetBwMode8812(), set BW=%s !!\n", GLBwSrc[pHalData->CurrentChannelBW]);
	
	//3 Set Reg848 Reg864 Reg8AC Reg8C4 RegA00
	switch(pHalData->CurrentChannelBW)
	{
		case CHANNEL_WIDTH_20:
			PHY_SetBBReg(Adapter, rRFMOD_Jaguar, 0x003003C3, 0x00300200); // 0x8ac[21,20,9:6,1,0]=8'b11100000
			PHY_SetBBReg(Adapter, rADC_Buf_Clk_Jaguar, BIT30, 0);			// 0x8c4[30] = 1'b0

			if(pHalData->rf_type == RF_2T2R)
				PHY_SetBBReg(Adapter, rL1PeakTH_Jaguar, 0x03C00000, 7);	// 2R 0x848[25:22] = 0x7
			else	
				PHY_SetBBReg(Adapter, rL1PeakTH_Jaguar, 0x03C00000, 8);	// 1R 0x848[25:22] = 0x8

			break;
			   
		case CHANNEL_WIDTH_40:
			PHY_SetBBReg(Adapter, rRFMOD_Jaguar, 0x003003C3, 0x00300201); // 0x8ac[21,20,9:6,1,0]=8'b11100000		
			PHY_SetBBReg(Adapter, rADC_Buf_Clk_Jaguar, BIT30, 0);			// 0x8c4[30] = 1'b0
			PHY_SetBBReg(Adapter, rRFMOD_Jaguar, 0x3C, SubChnlNum);
			PHY_SetBBReg(Adapter, rCCAonSec_Jaguar, 0xf0000000, SubChnlNum);

			if(pHalData->Reg837 & BIT2)
				L1pkVal = 6;
			else
			{
				if(pHalData->rf_type == RF_2T2R)
					L1pkVal = 7;
				else
					L1pkVal = 8;
			}

			PHY_SetBBReg(Adapter, rL1PeakTH_Jaguar, 0x03C00000, L1pkVal);	// 0x848[25:22] = 0x6

			if(SubChnlNum == VHT_DATA_SC_20_UPPER_OF_80MHZ)
				PHY_SetBBReg(Adapter, rCCK_System_Jaguar, bCCK_System_Jaguar, 1);
			else
				PHY_SetBBReg(Adapter, rCCK_System_Jaguar, bCCK_System_Jaguar, 0);
			break;

		case CHANNEL_WIDTH_80:
			PHY_SetBBReg(Adapter, rRFMOD_Jaguar, 0x003003C3, 0x00300202); // 0x8ac[21,20,9:6,1,0]=8'b11100010
			PHY_SetBBReg(Adapter, rADC_Buf_Clk_Jaguar, BIT30, 1);			// 0x8c4[30] = 1
			PHY_SetBBReg(Adapter, rRFMOD_Jaguar, 0x3C, SubChnlNum);
			PHY_SetBBReg(Adapter, rCCAonSec_Jaguar, 0xf0000000, SubChnlNum);

			if(pHalData->Reg837 & BIT2)
				L1pkVal = 5;
			else
			{
				if(pHalData->rf_type == RF_2T2R)
					L1pkVal = 6;
				else
					L1pkVal = 7;
			}
			PHY_SetBBReg(Adapter, rL1PeakTH_Jaguar, 0x03C00000, L1pkVal);	// 0x848[25:22] = 0x5

			break;

		default:
			DBG_871X("phy_PostSetBWMode8812():	unknown Bandwidth: %#X\n",pHalData->CurrentChannelBW);
			break;
	}

	// <20121109, Kordan> A workaround for 8812A only.
	phy_FixSpur_8812A(Adapter, pHalData->CurrentChannelBW, pHalData->CurrentChannel);

	//DBG_871X("phy_PostSetBwMode8812(): Reg483: %x\n", rtw_read8(Adapter, 0x483));
	//DBG_871X("phy_PostSetBwMode8812(): Reg668: %x\n", rtw_read32(Adapter, 0x668));
	//DBG_871X("phy_PostSetBwMode8812(): Reg8AC: %x\n", PHY_QueryBBReg(Adapter, rRFMOD_Jaguar, 0xffffffff));

	//3 Set RF related register
	PHY_RF6052SetBandwidth8812(Adapter, pHalData->CurrentChannelBW);
}

//<20130207, Kordan> The variales initialized here are used in odm_LNAPowerControl(). 
VOID phy_InitRssiTRSW(	
	IN	PADAPTER					pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	u8 			channel = pHalData->CurrentChannel;

	if (pHalData->RFEType == 3){

		if (channel <= 14) {
			pDM_Odm->RSSI_TRSW_H    = 70; // Unit: percentage(%)
			pDM_Odm->RSSI_TRSW_iso  = 25;
		} else {
			pDM_Odm->RSSI_TRSW_H   = 80;
			pDM_Odm->RSSI_TRSW_iso = 25;
		}

		pDM_Odm->RSSI_TRSW_L = pDM_Odm->RSSI_TRSW_H - pDM_Odm->RSSI_TRSW_iso - 10;
	}
}

// <20130806, Kordan> Referenced from "WB-20130801-YN-RL6286 Settings for Spur Issues.xls".
VOID
phy_SpurCalibration_8812A(	
	IN	PADAPTER	pAdapter,
	IN	u8			Channel
	)
{
	//RT_TRACE(COMP_SCAN, DBG_LOUD, ("===>phy_SpurCalibration_8812A()\n"));
	
	//2 1. Reset
	PHY_SetBBReg(pAdapter, 0x878, BIT8|BIT7|BIT6, 0x3);
	PHY_SetBBReg(pAdapter, 0x878, BIT0, 0);
	PHY_SetBBReg(pAdapter, 0x87C, BIT13, 0);
	PHY_SetBBReg(pAdapter, 0x880, bMaskDWord, 0);
	PHY_SetBBReg(pAdapter, 0x884, bMaskDWord, 0);
	PHY_SetBBReg(pAdapter, 0x898, bMaskDWord, 0);
	PHY_SetBBReg(pAdapter, 0x89C, bMaskDWord, 0);

	
	//2 2. Register Setting 1 (False Alarm)
	switch (Channel)
	{
		case 149:
		case 153:
		case 151:
		case 155:
        {
		    PHY_SetBBReg(pAdapter, 0x878, BIT8|BIT7|BIT6, 0x4);
		    PHY_SetBBReg(pAdapter, 0x878, BIT0, 1);
		    PHY_SetBBReg(pAdapter, 0x87C, BIT13, 1);
		    break;
        }
		default:
			break;
	}
	
	//2 3. Register Setting 2 (SINR)
	PHY_SetBBReg(pAdapter, 0x874, BIT21, 1);
	PHY_SetBBReg(pAdapter, 0x874, BIT0 , 1);
	
	switch (Channel)
	{
		case 149:
			PHY_SetBBReg(pAdapter, 0x884, bMaskDWord, 0x00010000);
			break;
		case 153:
			PHY_SetBBReg(pAdapter, 0x89C, bMaskDWord, 0x00010000);
			break;
		case 165:
			PHY_SetBBReg(pAdapter, 0x884, bMaskDWord, 0x00010000);
			break;
		case 169:
			PHY_SetBBReg(pAdapter, 0x89C, bMaskDWord, 0x00010000);
			break;
		case 38:
		case 54:
		case 102:
		case 118:
		case 134:
			//PHY_SetBBReg(pAdapter, 0x884, bMaskDWord, 0x00000001);
			break;
		case 151:
		case 167:
			PHY_SetBBReg(pAdapter, 0x880, bMaskDWord, 0x00010000);
			break;
		case 42:
		case 58:
		case 106:
		case 122:
		case 138:
			PHY_SetBBReg(pAdapter, 0x89C, bMaskDWord, 0x00000001);
			break;
		case 155:
			PHY_SetBBReg(pAdapter, 0x898, bMaskDWord, 0x00010000);
			break;
		default:
		    break;
	}
	//RT_TRACE(COMP_SCAN, DBG_LOUD, ("<===phy_SpurCalibration_8812A()\n"));

}

VOID
phy_SwChnl8812(	
	IN	PADAPTER	pAdapter
	)
{
	u8	eRFPath = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8	channelToSW = pHalData->CurrentChannel;

	if(phy_SwBand8812(pAdapter, channelToSW) == _FALSE)
	{
		DBG_871X("error Chnl %d !\n", channelToSW);
	}

	//<20130313, Kordan> Sample code to demonstrate how to configure AGC_TAB_DIFF.(Disabled by now) 
#if  (MP_DRIVER == 1)
	if (pAdapter->registrypriv.mp_mode == 1)
		ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_AGC_TAB_DIFF);
#endif

	if(pHalData->rf_chip == RF_PSEUDO_11N)
	{
		DBG_871X("phy_SwChnl8812: return for PSEUDO \n");
		return;
	}
	  
	//DBG_871X("[BW:CHNL], phy_SwChnl8812(), switch to channel %d !!\n", channelToSW);
	
	// fc_area		
	if (36 <= channelToSW && channelToSW <= 48) 
		PHY_SetBBReg(pAdapter, rFc_area_Jaguar, 0x1ffe0000, 0x494); 
	else if (50 <= channelToSW && channelToSW <= 64) 
		PHY_SetBBReg(pAdapter, rFc_area_Jaguar, 0x1ffe0000, 0x453);  
	else if (100 <= channelToSW && channelToSW <= 116) 
		PHY_SetBBReg(pAdapter, rFc_area_Jaguar, 0x1ffe0000, 0x452);  
	else if (118 <= channelToSW) 
		PHY_SetBBReg(pAdapter, rFc_area_Jaguar, 0x1ffe0000, 0x412);  
	else
		PHY_SetBBReg(pAdapter, rFc_area_Jaguar, 0x1ffe0000, 0x96a);
	    	
	for(eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++)
	{
		// RF_MOD_AG
		if (36 <= channelToSW && channelToSW <= 64) 
			PHY_SetRFReg(pAdapter, eRFPath, RF_CHNLBW_Jaguar, BIT18|BIT17|BIT16|BIT9|BIT8, 0x101); //5'b00101); 
		else if (100 <= channelToSW && channelToSW <= 140) 
			PHY_SetRFReg(pAdapter, eRFPath, RF_CHNLBW_Jaguar, BIT18|BIT17|BIT16|BIT9|BIT8, 0x301); //5'b01101); 
		else if (140 < channelToSW) 
			PHY_SetRFReg(pAdapter, eRFPath, RF_CHNLBW_Jaguar, BIT18|BIT17|BIT16|BIT9|BIT8, 0x501); //5'b10101); 
		else	
			PHY_SetRFReg(pAdapter, eRFPath, RF_CHNLBW_Jaguar, BIT18|BIT17|BIT16|BIT9|BIT8, 0x000); //5'b00000); 

		// <20121109, Kordan> A workaround for 8812A only.
		phy_FixSpur_8812A(pAdapter, pHalData->CurrentChannelBW, channelToSW);

		PHY_SetRFReg(pAdapter, eRFPath, RF_CHNLBW_Jaguar, bMaskByte0, channelToSW);

		// <20130104, Kordan> APK for MP chip is done on initialization from folder.
		if (IS_HARDWARE_TYPE_8821U(pAdapter) && ( !IS_NORMAL_CHIP(pHalData->VersionID)) && channelToSW > 14 ) 
		{
			// <20121116, Kordan> For better result of APK. Asked by AlexWang.
			if (36 <= channelToSW && channelToSW <= 64) 
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x710E7); 
			else if (100 <= channelToSW && channelToSW <= 140) 
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x716E9); 				
			else
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x714E9); 
		}
		else if (IS_HARDWARE_TYPE_8821S(pAdapter) && channelToSW > 14) 
		{
			// <20130111, Kordan> For better result of APK. Asked by Willson.
			if (36 <= channelToSW && channelToSW <= 64) 
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x714E9); 
			else if (100 <= channelToSW && channelToSW <= 140) 
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x110E9); 				
			else
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x714E9); 		
		}			
		else if (IS_HARDWARE_TYPE_8821E(pAdapter) && channelToSW > 14) 
		{
			// <20130613, Kordan> For better result of APK. Asked by Willson.
			if (36 <= channelToSW && channelToSW <= 64)
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x114E9);
			else if (100 <= channelToSW && channelToSW <= 140)
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x110E9);
			else
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x110E9);
		}
	}

	if (IS_HARDWARE_TYPE_8812(pAdapter) && (pHalData->RFEType == 4 || pHalData->ExternalPA_5G == 0)) 
		phy_SpurCalibration_8812A(pAdapter, channelToSW);
}

VOID
phy_SwChnlAndSetBwMode8812(
	IN  PADAPTER		Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	//DBG_871X("phy_SwChnlAndSetBwMode8812(): bSwChnl %d, bSetChnlBW %d \n", pHalData->bSwChnl, pHalData->bSetChnlBW);
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
		phy_SwChnl8812(Adapter);
		pHalData->bSwChnl = _FALSE;
	}	

	if(pHalData->bSetChnlBW)
	{
		phy_PostSetBwMode8812(Adapter);
		pHalData->bSetChnlBW = _FALSE;
	}	

	ODM_ClearTxPowerTrackingState(&pHalData->odmpriv);
	PHY_SetTxPowerLevel8812(Adapter, pHalData->CurrentChannel);

	if(IS_HARDWARE_TYPE_8812(Adapter))
		phy_InitRssiTRSW(Adapter);

	if ( (pHalData->bNeedIQK == _TRUE)
#if (MP_DRIVER == 1) 
		|| (Adapter->registrypriv.mp_mode == 1)
#endif
		) 
	{
		if(IS_HARDWARE_TYPE_8812(Adapter))
		{
#if (RTL8812A_SUPPORT == 1)
			PHY_IQCalibrate_8812A(Adapter, _FALSE);
#endif 
		}
		else if(IS_HARDWARE_TYPE_8821(Adapter))
		{
#if (RTL8821A_SUPPORT == 1)
			PHY_IQCalibrate_8821A(Adapter, _FALSE);
#endif
		}	
		pHalData->bNeedIQK = _FALSE;
	}
}

VOID
PHY_HandleSwChnlAndSetBW8812(
	IN	PADAPTER			Adapter,
	IN	BOOLEAN				bSwitchChannel,
	IN	BOOLEAN				bSetBandWidth,
	IN	u8					ChannelNum,
	IN	CHANNEL_WIDTH		ChnlWidth,
	IN	u8					ChnlOffsetOf40MHz,
	IN	u8					ChnlOffsetOf80MHz,
	IN	u8					CenterFrequencyIndex1
)
{
	PADAPTER  			pDefAdapter =  GetDefaultAdapter(Adapter);
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pDefAdapter);
	u8					tmpChannel = pHalData->CurrentChannel;
	CHANNEL_WIDTH		tmpBW= pHalData->CurrentChannelBW;
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
			else
				return;
		}
	}

	if(bSetBandWidth)
	{
		if(pHalData->bChnlBWInitialized == _FALSE)
		{
			pHalData->bChnlBWInitialized = _TRUE;
			pHalData->bSetChnlBW = _TRUE;
		}
		else if((pHalData->CurrentChannelBW != ChnlWidth) ||
			(pHalData->nCur40MhzPrimeSC != ChnlOffsetOf40MHz) || 
			(pHalData->nCur80MhzPrimeSC != ChnlOffsetOf80MHz) ||
			(pHalData->CurrentCenterFrequencyIndex1!= CenterFrequencyIndex1))
		{
			pHalData->bSetChnlBW = _TRUE;
		}
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
		pHalData->nCur40MhzPrimeSC = ChnlOffsetOf40MHz;
		pHalData->nCur80MhzPrimeSC = ChnlOffsetOf80MHz;
#endif

		pHalData->CurrentCenterFrequencyIndex1 = CenterFrequencyIndex1;		
	}

	//Switch workitem or set timer to do switch channel or setbandwidth operation
	if((!pDefAdapter->bDriverStopped) && (!pDefAdapter->bSurpriseRemoved))
	{
		phy_SwChnlAndSetBwMode8812(Adapter);
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
PHY_SetBWMode8812(
	IN	PADAPTER			Adapter,
	IN	CHANNEL_WIDTH	Bandwidth,	// 20M or 40M
	IN	u8					Offset		// Upper, Lower, or Don't care
)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);

	//DBG_871X("%s()===>\n",__FUNCTION__);

	PHY_HandleSwChnlAndSetBW8812(Adapter, _FALSE, _TRUE, pHalData->CurrentChannel, Bandwidth, Offset, Offset, pHalData->CurrentChannel);

	//DBG_871X("<==%s()\n",__FUNCTION__);
}

VOID
PHY_SwChnl8812(
	IN	PADAPTER	Adapter,
	IN	u8			channel
	)
{
	//DBG_871X("%s()===>\n",__FUNCTION__);

	PHY_HandleSwChnlAndSetBW8812(Adapter, _TRUE, _FALSE, channel, 0, 0, 0, channel);

	//DBG_871X("<==%s()\n",__FUNCTION__);
}

VOID
PHY_SetSwChnlBWMode8812(
	IN	PADAPTER			Adapter,
	IN	u8					channel,
	IN	CHANNEL_WIDTH		Bandwidth,
	IN	u8					Offset40,
	IN	u8					Offset80
)
{
	//DBG_871X("%s()===>\n",__FUNCTION__);

	PHY_HandleSwChnlAndSetBW8812(Adapter, _TRUE, _TRUE, channel, Bandwidth, Offset40, Offset80, channel);

	//DBG_871X("<==%s()\n",__FUNCTION__);
}


