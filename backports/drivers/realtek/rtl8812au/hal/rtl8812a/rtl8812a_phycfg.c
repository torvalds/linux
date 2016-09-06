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

	if(IS_VENDOR_8812A_TEST_CHIP(Adapter))
		PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, bMaskDWord, 0);		

	PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, bHSSIRead_addr_Jaguar, Offset);
	
	if (IS_VENDOR_8812A_TEST_CHIP(Adapter) ) 
		PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, bMaskDWord, Offset|BIT8);

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

	//3 <Note> This is a workaround for 8812A test chips.
#ifdef CONFIG_USB_HCI
	// <20120427, Kordan> MAC first moves lower 16 bits and then upper 16 bits of a 32-bit data.
	// BaseBand doesn't know the two actions is actually only one action to access 32-bit data,
	// so that the lower 16 bits is overwritten by the upper 16 bits. (Asked by ynlin.)
	// (Unfortunately, the protection mechanism has not been implemented in 8812A yet.)    
	// 2012/10/26 MH Revise V3236 Lanhsin check in, if we do not enable the function
	// for 8821, then it can not scan.
	if ((!pHalData->bSupportUSB3) && (IS_TEST_CHIP(pHalData->VersionID))) // USB 2.0 or older
	{
		//if (IS_VENDOR_8812A_TEST_CHIP(Adapter) || IS_HARDWARE_TYPE_8821(Adapter) is)
		{
			rtw_write32(Adapter, 0x1EC, DataAndAddr);
			if (eRFPath == RF_PATH_A)
				rtw_write32(Adapter, 0x1E8, 0x4000F000|0xC90);
			else
				rtw_write32(Adapter, 0x1E8, 0x4000F000|0xE90);	
		}
	}
	else // USB 3.0
#endif
	{
		// Write Operation 
		// TODO: Dynamically determine whether using PI or SI to write RF registers.
		PHY_SetBBReg(Adapter, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);
		//DBG_871X("RFW-%d Addr[0x%x]=0x%x\n", eRFPath, pPhyReg->rf3wireOffset, DataAndAddr);
	}

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
	int				rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	s8				*pszMACRegFile;
	s8				sz8812MACRegFile[] = RTL8812_PHY_MACREG;

	pszMACRegFile = sz8812MACRegFile;

	//
	// Config MAC
	//
#ifdef CONFIG_EMBEDDED_FWIMG
	if(HAL_STATUS_SUCCESS != ODM_ConfigMACWithHeaderFile(&pHalData->odmpriv))
		rtStatus = _FAIL;
#else

	// Not make sure EEPROM, add later
	DBG_871X("Read MACREG.txt\n");
	rtStatus = phy_ConfigMACWithParaFile(Adapter, pszMACRegFile);	
#endif//CONFIG_EMBEDDED_FWIMG

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

	pHalData->PHYRegDef[RF_PATH_A].rf3wireOffset = rA_LSSIWrite_Jaguar; //LSSI Parameter
	pHalData->PHYRegDef[RF_PATH_B].rf3wireOffset = rB_LSSIWrite_Jaguar;

	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara2 = rHSSIRead_Jaguar;  //wire control parameter2
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara2 = rHSSIRead_Jaguar;  //wire control parameter2

	// Tranceiver Readback LSSI/HSPI mode 
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBack = rA_SIRead_Jaguar;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBack = rB_SIRead_Jaguar;
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBackPi = rA_PIRead_Jaguar;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBackPi = rB_PIRead_Jaguar;

	//pHalData->bPhyValueInitReady=_TRUE;
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

	PHY_InitPowerLimitTable( &(pHalData->odmpriv) );

	if ( ( Adapter->registrypriv.RegEnableTxPowerLimit == 1 && pHalData->EEPROMRegulatory != 2 ) || 
		 pHalData->EEPROMRegulatory == 1 )
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		if (HAL_STATUS_SUCCESS != ODM_ConfigRFWithHeaderFile(&pHalData->odmpriv, CONFIG_RF_TXPWR_LMT, 0))
			rtStatus = _FAIL;
#else
		rtStatus = PHY_ConfigBBWithPowerLimitTableParaFile( Adapter, pszRFTxPwrLmtFile );
#endif

		if(rtStatus != _SUCCESS){
			DBG_871X("phy_BB8812_Config_ParaFile():Write BB Reg Fail!!");
			goto phy_BB_Config_ParaFile_Fail;
		}
	}

	// Read PHY_REG.TXT BB INIT!!
#ifdef CONFIG_EMBEDDED_FWIMG
	if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG))
		rtStatus = _FAIL;
#else	
	// No matter what kind of CHIP we always read PHY_REG.txt. We must copy different 
	// type of parameter files to phy_reg.txt at first.	
	rtStatus = phy_ConfigBBWithParaFile(Adapter,pszBBRegFile);
#endif

	if(rtStatus != _SUCCESS){
		DBG_871X("phy_BB8812_Config_ParaFile():Write BB Reg Fail!!");
		goto phy_BB_Config_ParaFile_Fail;
	}

//f (MP_DRIVER == 1)
#if 0
	// Read PHY_REG_MP.TXT BB INIT!!
#ifdef CONFIG_EMBEDDED_FWIMG
	if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG))
		rtStatus = _FAIL; 
#else	
	// No matter what kind of CHIP we always read PHY_REG.txt. We must copy different 
	// type of parameter files to phy_reg.txt at first.	
	rtStatus = phy_ConfigBBWithMpParaFile(Adapter,pszBBRegMpFile);
#endif

	if(rtStatus != _SUCCESS){
		DBG_871X("phy_BB8812_Config_ParaFile():Write BB Reg MP Fail!!");
		goto phy_BB_Config_ParaFile_Fail;
	}	
#endif	// #if (MP_DRIVER == 1)

	// If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt
	//1 TODO
	if (pEEPROM->bautoload_fail_flag == _FALSE)
	{
		pHalData->pwrGroupCnt = 0;

#ifdef CONFIG_EMBEDDED_FWIMG
		if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG_PG))
			rtStatus = _FAIL;
#else
		rtStatus = phy_ConfigBBWithPgParaFile(Adapter, pszBBRegPgFile);
#endif

		if(rtStatus != _SUCCESS){
			DBG_871X("phy_BB8812_Config_ParaFile():BB_PG Reg Fail!!");
			goto phy_BB_Config_ParaFile_Fail;
		}

		if ( ( Adapter->registrypriv.RegEnableTxPowerLimit == 1 && pHalData->EEPROMRegulatory != 2 ) || 
		 	 pHalData->EEPROMRegulatory == 1 )
			PHY_ConvertPowerLimitToPowerIndex( Adapter );
	}


	// BB AGC table Initialization
#ifdef CONFIG_EMBEDDED_FWIMG
	if (HAL_STATUS_SUCCESS != ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_AGC_TAB))
		rtStatus = _FAIL;
#else
	rtStatus = phy_ConfigBBWithParaFile(Adapter, pszAGCTableFile);
#endif

	if(rtStatus != _SUCCESS){
		DBG_871X("phy_BB8812_Config_ParaFile():AGC Table Fail\n");
		goto phy_BB_Config_ParaFile_Fail;
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
	else if ((IS_HARDWARE_TYPE_8723A(Adapter) && pHalData->EEPROMVersion >= 0x01) ||
		IS_HARDWARE_TYPE_8821(Adapter) || IS_HARDWARE_TYPE_8723B(Adapter) ||
		IS_HARDWARE_TYPE_8192E(Adapter))
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

BOOLEAN 
eqNByte(
	u8*	str1,
	u8*	str2,
	u32	num
	)
{
	if(num==0)
		return _FALSE;
	while(num>0)
	{
		num--;
		if(str1[num]!=str2[num])
			return _FALSE;
	}
	return _TRUE;
}

BOOLEAN
GetU1ByteIntegerFromStringInDecimal(
	IN		s8*	Str,
	IN OUT	u8*	pInt
	)
{
	u16 i = 0;
	*pInt = 0;

	while ( Str[i] != '\0' )
	{
		if ( Str[i] >= '0' && Str[i] <= '9' )
		{
			*pInt *= 10;
			*pInt += ( Str[i] - '0' );
		}
		else
		{
			return _FALSE;
		}
		++i;
	}

	return _TRUE;
}

static s8
phy_GetChannelGroup(
	IN BAND_TYPE	Band,
	IN u8			Channel
	)
{
	s8 channelGroup = -1;
	if ( Channel <= 14 && Band == BAND_ON_2_4G )
	{
		if		( 1 <= Channel && Channel <= 2 )	channelGroup = 0;
		else if ( 3  <= Channel && Channel <= 5 )   channelGroup = 1;
		else if ( 6  <= Channel && Channel <= 8 )   channelGroup = 2;
		else if ( 9  <= Channel && Channel <= 11 )  channelGroup = 3;
		else if ( 12 <= Channel && Channel <= 14)   channelGroup = 4;
		else
		{
			DBG_871X( "==> phy_GetChannelGroup() in 2.4 G, but Channel %d in Group not found \n", Channel );
			channelGroup = -1;
		}
	}
	else if( Band == BAND_ON_5G )
	{
		if      ( 36   <= Channel && Channel <=  42 )  channelGroup = 0;
		else if ( 44   <= Channel && Channel <=  48 )  channelGroup = 1;
		else if ( 50   <= Channel && Channel <=  58 )  channelGroup = 2;
		else if ( 60   <= Channel && Channel <=  64 )  channelGroup = 3;
		else if ( 100  <= Channel && Channel <= 106 )  channelGroup = 4;
		else if ( 108  <= Channel && Channel <= 114 )  channelGroup = 5;
		else if ( 116  <= Channel && Channel <= 122 )  channelGroup = 6;
		else if ( 124  <= Channel && Channel <= 130 )  channelGroup = 7;
		else if ( 132  <= Channel && Channel <= 138 )  channelGroup = 8;
		else if ( 140  <= Channel && Channel <= 144 )  channelGroup = 9;
		else if ( 149  <= Channel && Channel <= 155 )  channelGroup = 10;
		else if ( 157  <= Channel && Channel <= 161 )  channelGroup = 11;
		else if ( 165  <= Channel && Channel <= 171 )  channelGroup = 12;
		else if ( 173  <= Channel && Channel <= 177 )  channelGroup = 13;
		else
		{
			DBG_871X("==>phy_GetChannelGroup() in 5G, but Channel %d in Group not found \n", Channel );
			channelGroup = -1;
		}
	}
	else 
	{
		DBG_871X("==>phy_GetChannelGroup() in unsupported band %d\n", Band );
		channelGroup = -1;
	}

	return channelGroup;
}

u8
phy_getPowerByRateBaseIndex(
	IN	BAND_TYPE			Band,
	IN	u8					Rate
	)
{
	u8	index = 0;
	if ( Band == BAND_ON_2_4G )
	{
		switch ( Rate )
		{
			case MGN_1M: case MGN_2M: case MGN_5_5M: case MGN_11M:
				index = 0;
				break;

			case MGN_6M: case MGN_9M: case MGN_12M: case MGN_18M:
			case MGN_24M: case MGN_36M: case MGN_48M: case MGN_54M:
				index = 1;
				break;

			case MGN_MCS0: case MGN_MCS1: case MGN_MCS2: case MGN_MCS3: 
			case MGN_MCS4: case MGN_MCS5: case MGN_MCS6: case MGN_MCS7:
				index = 2;
				break;
				
			case MGN_MCS8: case MGN_MCS9: case MGN_MCS10: case MGN_MCS11: 
			case MGN_MCS12: case MGN_MCS13: case MGN_MCS14: case MGN_MCS15:
				index = 3;
				break;

			default:
				DBG_871X("Wrong rate 0x%x to obtain index in 2.4G in phy_getPowerByRateBaseIndex()\n", Rate );
				break;
		}
	}
	else if ( Band == BAND_ON_5G )
	{
		switch ( Rate )
		{
			case MGN_6M: case MGN_9M: case MGN_12M: case MGN_18M:
			case MGN_24M: case MGN_36M: case MGN_48M: case MGN_54M:
				index = 0;
				break;

			case MGN_MCS0: case MGN_MCS1: case MGN_MCS2: case MGN_MCS3: 
			case MGN_MCS4: case MGN_MCS5: case MGN_MCS6: case MGN_MCS7:
				index = 1;
				break;
				
			case MGN_MCS8: case MGN_MCS9: case MGN_MCS10: case MGN_MCS11: 
			case MGN_MCS12: case MGN_MCS13: case MGN_MCS14: case MGN_MCS15:
				index = 2;
				break;

			case MGN_VHT1SS_MCS0: case MGN_VHT1SS_MCS1: case MGN_VHT1SS_MCS2:
			case MGN_VHT1SS_MCS3: case MGN_VHT1SS_MCS4: case MGN_VHT1SS_MCS5:
			case MGN_VHT1SS_MCS6: case MGN_VHT1SS_MCS7: case MGN_VHT1SS_MCS8:
			case MGN_VHT1SS_MCS9:
					index = 3;
				break;
				
			case MGN_VHT2SS_MCS0: case MGN_VHT2SS_MCS1: case MGN_VHT2SS_MCS2:
			case MGN_VHT2SS_MCS3: case MGN_VHT2SS_MCS4: case MGN_VHT2SS_MCS5:
			case MGN_VHT2SS_MCS6: case MGN_VHT2SS_MCS7: case MGN_VHT2SS_MCS8:
			case MGN_VHT2SS_MCS9:
				index = 4;
				break;

			default:
				DBG_871X("Wrong rate 0x%x to obtain index in 5G in phy_getPowerByRateBaseIndex()\n", Rate );
				break;
		}
	}

	return index;
}

VOID
PHY_InitPowerLimitTable(
	IN	PDM_ODM_T	pDM_Odm
	)
{
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8				i, j, k, l, m;

	//DBG_871X( "=====> PHY_InitPowerLimitTable()!\n" );

	for ( i = 0; i < MAX_REGULATION_NUM; ++i )
	{
		for ( j = 0; j < MAX_2_4G_BANDWITH_NUM; ++j )
			for ( k = 0; k < MAX_2_4G_RATE_SECTION_NUM; ++k )
				for ( m = 0; m < MAX_2_4G_CHANNEL_NUM; ++m )
					for ( l = 0; l < GET_HAL_RFPATH_NUM(Adapter) ;++l )
						pHalData->TxPwrLimit_2_4G[i][j][k][m][l] = MAX_POWER_INDEX;
	}

	for ( i = 0; i < MAX_REGULATION_NUM; ++i )
	{
		for ( j = 0; j < MAX_5G_BANDWITH_NUM; ++j )
			for ( k = 0; k < MAX_5G_RATE_SECTION_NUM; ++k )
				for ( m = 0; m < MAX_5G_CHANNEL_NUM; ++m )
					for ( l = 0; l <  GET_HAL_RFPATH_NUM(Adapter) ; ++l )
						pHalData->TxPwrLimit_5G[i][j][k][m][l] = MAX_POWER_INDEX;
	}

	//DBG_871X("<===== PHY_InitPowerLimitTable()!\n" );
}

VOID 
PHY_ConvertPowerLimitToPowerIndex(
	IN	PADAPTER			Adapter
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8 				BW40PwrBasedBm2_4G, BW40PwrBasedBm5G;
	u8 				regulation, bw, channel, rateSection, group;	
	u8 				baseIndex2_4G;
	u8				baseIndex5G;
	s8 				tempValue = 0, tempPwrLmt = 0;
	u8 				rfPath = 0;

	DBG_871X( "=====> PHY_ConvertPowerLimitToPowerIndex()\n" );
	for ( regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation )
	{
		for ( bw = 0; bw < MAX_2_4G_BANDWITH_NUM; ++bw )
		{
			for ( group = 0; group < MAX_2_4G_CHANNEL_NUM; ++group )
			{
				if ( group == 0 )
						channel = 1;
					else if ( group == 1 )
						channel = 3;
					else if ( group == 2 )
						channel = 6;
					else if ( group == 3 )
						channel = 9;
					else if ( group == 4 )
						channel = 12;
					else 
						channel = 14;
					
				
				for ( rateSection = 0; rateSection < MAX_2_4G_RATE_SECTION_NUM; ++rateSection )
				{	
					if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE ) {
						// obtain the base dBm values in 2.4G band
						// CCK => 11M, OFDM => 54M, HT 1T => MCS7, HT 2T => MCS15
						if ( rateSection == 0 ) { //CCK
							baseIndex2_4G = phy_getPowerByRateBaseIndex( BAND_ON_2_4G, MGN_11M );
						}
						else if ( rateSection == 1 ) { //OFDM
							baseIndex2_4G = phy_getPowerByRateBaseIndex( BAND_ON_2_4G, MGN_54M );
						}
						else if ( rateSection == 2 ) { //HT IT
							baseIndex2_4G = phy_getPowerByRateBaseIndex( BAND_ON_2_4G, MGN_MCS7 );
						}
						else if ( rateSection == 3 ) { //HT 2T
							baseIndex2_4G = phy_getPowerByRateBaseIndex( BAND_ON_2_4G, MGN_MCS15 );
						}
					}

					// we initially record the raw power limit value in rf path A, so we must obtain the raw 
					// power limit value by using index rf path A and use it to calculate all the value of 
					// all the path
					tempPwrLmt = pHalData->TxPwrLimit_2_4G[regulation][bw][rateSection][group][ODM_RF_PATH_A];
					// process ODM_RF_PATH_A later
					for ( rfPath = 0; rfPath < MAX_RF_PATH_NUM; ++rfPath )
					{
						if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE )
							BW40PwrBasedBm2_4G = pHalData->TxPwrByRateBase2_4G[rfPath][baseIndex2_4G];
						else
							BW40PwrBasedBm2_4G = Adapter->registrypriv.RegPowerBase * 2;

						if ( tempPwrLmt != MAX_POWER_INDEX ) {
							tempValue = tempPwrLmt - BW40PwrBasedBm2_4G;
							pHalData->TxPwrLimit_2_4G[regulation][bw][rateSection][group][rfPath] = tempValue;
						}
						
						DBG_871X("TxPwrLimit_2_4G[regulation %d][bw %d][rateSection %d][group %d] %d=\n\
							(TxPwrLimit in dBm %d - BW40PwrLmt2_4G[channel %d][rfPath %d] %d) \n",
							regulation, bw, rateSection, group, pHalData->TxPwrLimit_2_4G[regulation][bw][rateSection][group][rfPath], 
							tempPwrLmt, channel, rfPath, BW40PwrBasedBm2_4G );
					}
				}
			}
		}
	}
	
	if ( IS_HARDWARE_TYPE_8812( Adapter ) || IS_HARDWARE_TYPE_8821( Adapter ) )
	{
		for ( regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation )
		{
			for ( bw = 0; bw < MAX_5G_BANDWITH_NUM; ++bw )
			{
				for ( group = 0; group < MAX_5G_CHANNEL_NUM; ++group )
				{

					/* channels of 5G band in Hal_ReadTxPowerInfo8812A()
					36,38,40,42,44,
					46,48,50,52,54,
					56,58,60,62,64,
					100,102,104,106,108,
					110,112,114,116,118,
					120,122,124,126,128,
					130,132,134,136,138,
					140,142,144,149,151,
	                		153,155,157,159,161,
	                		163,165,167,168,169,
	                		171,173,175,177 */
					if ( group == 0 )
						channel = 0; // index of chnl 36 in channel5G
					else if ( group == 1 )
						channel = 4; // index of chnl 44 in chanl5G
					else if ( group == 2 )
						channel = 7; // index of chnl 50 in chanl5G
					else if ( group == 3 )
						channel = 12; // index of chnl 60 in chanl5G
					else if ( group == 4 )
						channel = 15; // index of chnl 100 in chanl5G
					else if ( group == 5 )
						channel = 19; // index of chnl 108 in chanl5G
					else if ( group == 6 )
						channel = 23; // index of chnl 116 in chanl5G
					else if ( group == 7 )
						channel = 27; // index of chnl 124 in chanl5G
					else if ( group == 8 )
						channel = 31; // index of chnl 132 in chanl5G
					else if ( group == 9 )
						channel = 35; // index of chnl 140 in chanl5G
					else if ( group == 10 )
						channel = 38; // index of chnl 149 in chanl5G
					else if ( group == 11 )
						channel = 42; // index of chnl 157 in chanl5G
					else if ( group == 12 )
						channel = 46; // index of chnl 165 in chanl5G
					else
						channel = 51; // index of chnl 173 in chanl5G
						
					for ( rateSection = 0; rateSection < MAX_5G_RATE_SECTION_NUM; ++rateSection )
					{	
						if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE ) {
							// obtain the base dBm values in 5G band
							// OFDM => 54M, HT 1T => MCS7, HT 2T => MCS15, 
							// VHT => 1SSMCS7, VHT 2T => 2SSMCS7
							if ( rateSection == 0 ) { //CCK - Unused by 5g, but if baseIndex5G is undefined, causes crash
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_11M );
							}
							else if ( rateSection == 1 ) { //OFDM
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_54M );
							}
							else if ( rateSection == 2 ) { //HT 1T
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_MCS7 );
							}
							else if ( rateSection == 3 ) { //HT 2T
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_MCS15 );
							}
							else if ( rateSection == 4 ) { //VHT 1T
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_VHT1SS_MCS7 );
							}
							else if ( rateSection == 5 ) { //VHT 2T
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_VHT2SS_MCS7 );
							}
						}

						// we initially record the raw power limit value in rf path A, so we must obtain the raw 
						// power limit value by using index rf path A and use it to calculate all the value of 
						// all the path
						tempPwrLmt = pHalData->TxPwrLimit_5G[regulation][bw][rateSection][group][ODM_RF_PATH_A];
						if ( tempPwrLmt == MAX_POWER_INDEX )
						{
							if ( bw == 0 || bw == 1 ) { // 5G VHT and HT can cross reference
								DBG_871X( "No power limit table of the specified band %d, bandwidth %d, ratesection %d, group %d, rf path %d\n",
											1, bw, rateSection, group, ODM_RF_PATH_A );
								if ( rateSection == 2 ) {
									pHalData->TxPwrLimit_5G[regulation][bw][2][group][ODM_RF_PATH_A] = 
										pHalData->TxPwrLimit_5G[regulation][bw][4][group][ODM_RF_PATH_A];
									tempPwrLmt = pHalData->TxPwrLimit_5G[regulation]
															[bw][4][group][ODM_RF_PATH_A];
								}
								else if ( rateSection == 4 ) {
									pHalData->TxPwrLimit_5G[regulation][bw][4][group][ODM_RF_PATH_A] = 
										pHalData->TxPwrLimit_5G[regulation][bw][2][group][ODM_RF_PATH_A];
									tempPwrLmt = pHalData->TxPwrLimit_5G[regulation]
															[bw][2][group][ODM_RF_PATH_A];
								}
								else if ( rateSection == 3 ) {
									pHalData->TxPwrLimit_5G[regulation][bw][3][group][ODM_RF_PATH_A] = 
										pHalData->TxPwrLimit_5G[regulation][bw][5][group][ODM_RF_PATH_A];
									tempPwrLmt = pHalData->TxPwrLimit_5G[regulation]
															[bw][5][group][ODM_RF_PATH_A];
								}
								else if ( rateSection == 5 ) {
									pHalData->TxPwrLimit_5G[regulation][bw][5][group][ODM_RF_PATH_A] = 
										pHalData->TxPwrLimit_5G[regulation][bw][3][group][ODM_RF_PATH_A];
									tempPwrLmt = pHalData->TxPwrLimit_5G[regulation]
															[bw][3][group][ODM_RF_PATH_A];
								}

								DBG_871X("use other value %d", tempPwrLmt);
							}
						}

						// process ODM_RF_PATH_A later
						for ( rfPath = ODM_RF_PATH_B; rfPath < MAX_RF_PATH_NUM; ++rfPath )
						{
							if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE )
								BW40PwrBasedBm5G = pHalData->TxPwrByRateBase5G[rfPath][baseIndex5G];
							else
								BW40PwrBasedBm5G = Adapter->registrypriv.RegPowerBase * 2;

							if ( tempPwrLmt != MAX_POWER_INDEX ) {
								tempValue = tempPwrLmt - BW40PwrBasedBm5G;
								pHalData->TxPwrLimit_5G[regulation][bw][rateSection][group][rfPath] = tempValue;
							}
							
							DBG_871X("TxPwrLimit_5G[regulation %d][bw %d][rateSection %d][group %d] %d=\n\
								(TxPwrLimit in dBm %d - BW40PwrLmt5G[channel %d][rfPath %d] %d) \n",
								regulation, bw, rateSection, group, pHalData->TxPwrLimit_5G[regulation][bw][rateSection][group][rfPath], 
								tempPwrLmt, channel, rfPath, BW40PwrBasedBm5G );
						}

					}

				}
			}
		}
		
		// process value of ODM_RF_PATH_A
		for ( regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation )
		{
			for ( bw = 0; bw < MAX_5G_BANDWITH_NUM; ++bw )
			{
				for ( group = 0; group < MAX_5G_CHANNEL_NUM; ++group )
				{
					if ( group == 0 )
						channel = 0; // index of chnl 36 in channel5G
					else if ( group == 1 )
						channel = 4; // index of chnl 44 in chanl5G
					else if ( group == 2 )
						channel = 7; // index of chnl 50 in chanl5G
					else if ( group == 3 )
						channel = 12; // index of chnl 60 in chanl5G
					else if ( group == 4 )
						channel = 15; // index of chnl 100 in chanl5G
					else if ( group == 5 )
						channel = 19; // index of chnl 108 in chanl5G
					else if ( group == 6 )
						channel = 23; // index of chnl 116 in chanl5G
					else if ( group == 7 )
						channel = 27; // index of chnl 124 in chanl5G
					else if ( group == 8 )
						channel = 31; // index of chnl 132 in chanl5G
					else if ( group == 9 )
						channel = 35; // index of chnl 140 in chanl5G
					else if ( group == 10 )
						channel = 38; // index of chnl 149 in chanl5G
					else if ( group == 11 )
						channel = 42; // index of chnl 157 in chanl5G
					else if ( group == 12 )
						channel = 46; // index of chnl 165 in chanl5G
					else
						channel = 51; // index of chnl 173 in chanl5G
						
					for ( rateSection = 0; rateSection < MAX_5G_RATE_SECTION_NUM; ++rateSection )
					{	
						if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE ) {
							// obtain the base dBm values in 5G band
							// OFDM => 54M, HT 1T => MCS7, HT 2T => MCS15, 
							// VHT => 1SSMCS7, VHT 2T => 2SSMCS7
							if ( rateSection == 0 ) { //CCK - Unused by 5g, but if baseIndex5G is undefined, causes crash
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_11M );
							}
							else if ( rateSection == 1 ) { //OFDM
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_54M );
							}
							else if ( rateSection == 2 ) { //HT 1T
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_MCS7 );
							}
							else if ( rateSection == 3 ) { //HT 2T
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_MCS15 );
							}
							else if ( rateSection == 4 ) { //VHT 1T
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_VHT1SS_MCS7 );
							}
							else if ( rateSection == 5 ) { //VHT 2T
								baseIndex5G = phy_getPowerByRateBaseIndex( BAND_ON_5G, MGN_VHT2SS_MCS7 );
							}
						}

						tempPwrLmt = pHalData->TxPwrLimit_5G[regulation][bw][rateSection][group][ODM_RF_PATH_A];
						if ( tempPwrLmt == MAX_POWER_INDEX )
						{
							if ( bw == 0 || bw == 1 ) { // 5G VHT and HT can cross reference
								DBG_871X("No power limit table of the specified band %d, bandwidth %d, ratesection %d, group %d, rf path %d\n",
											1, bw, rateSection, group, ODM_RF_PATH_A );
								if ( rateSection == 2 )
									tempPwrLmt = pHalData->TxPwrLimit_5G[regulation]
															[bw][4][group][ODM_RF_PATH_A];
								else if ( rateSection == 4 )
									tempPwrLmt = pHalData->TxPwrLimit_5G[regulation]
															[bw][2][group][ODM_RF_PATH_A];
								else if ( rateSection == 3 )
									tempPwrLmt = pHalData->TxPwrLimit_5G[regulation]
															[bw][5][group][ODM_RF_PATH_A];
								else if ( rateSection == 5 )
									tempPwrLmt = pHalData->TxPwrLimit_5G[regulation]
															[bw][3][group][ODM_RF_PATH_A];

								DBG_871X("use other value %d", tempPwrLmt );
							}
						}
			

						if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE )
							BW40PwrBasedBm5G = pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][baseIndex5G];
						else
							BW40PwrBasedBm5G = Adapter->registrypriv.RegPowerBase * 2;

						if ( tempPwrLmt != MAX_POWER_INDEX ) {
							tempValue = tempPwrLmt - BW40PwrBasedBm5G;
							pHalData->TxPwrLimit_5G[regulation][bw][rateSection][group][ODM_RF_PATH_A] = tempValue;
						}
						
						DBG_871X("TxPwrLimit_5G[regulation %d][bw %d][rateSection %d][group %d] %d=\n\
							(TxPwrLimit in dBm %d - BW40PwrLmt5G[channel %d][rfPath %d] %d) \n",
							regulation, bw, rateSection, group, pHalData->TxPwrLimit_5G[regulation][bw][rateSection][group][ODM_RF_PATH_A], 
							tempPwrLmt, channel, ODM_RF_PATH_A, BW40PwrBasedBm5G );
					}
				}
			}
		}
	}
	DBG_871X("<===== PHY_ConvertPowerLimitToPowerIndex()\n" );
}

VOID
PHY_SetPowerLimitTableValue(
	IN	PDM_ODM_T		pDM_Odm,
	IN	s8*				Regulation,
	IN	s8*				Band,
	IN	s8*				Bandwidth,
	IN	s8*				RateSection,
	IN	s8*				RfPath,
	IN	s8* 				Channel,
	IN	s8*				PowerLimit
	)
{
	PADAPTER			Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA( Adapter );
	u8					regulation=0, bandwidth=0, rateSection=0, 
						channel, powerLimit, channelGroup;

	DBG_871X( "Index of power limit table \
		  [band %s][regulation %s][bw %s][rate section %s][rf path %s][chnl %s][val %s]\n", 
		  Band, Regulation, Bandwidth, RateSection, RfPath, Channel, PowerLimit ) ;

	if ( !GetU1ByteIntegerFromStringInDecimal( Channel, &channel ) ||
		 !GetU1ByteIntegerFromStringInDecimal( PowerLimit, &powerLimit ) )
	{
		DBG_871X("Illegal index of power limit table [chnl %s][val %s]\n", Channel, PowerLimit );
	}

	powerLimit = powerLimit > MAX_POWER_INDEX ? MAX_POWER_INDEX : powerLimit;

	if ( eqNByte( Regulation, "FCC", 3 ) ) regulation = 0;
	else if ( eqNByte( Regulation, "MKK", 3 ) ) regulation = 1;
	else if ( eqNByte( Regulation, "ETSI", 4 ) ) regulation = 2;

	if ( eqNByte( RateSection, "CCK", 3 ) )
		rateSection = 0;
	else if ( eqNByte( RateSection, "OFDM", 4 ) )
		rateSection = 1;
	else if ( eqNByte( RateSection, "HT", 2 ) && eqNByte( RfPath, "1T", 2 ) )
		rateSection = 2;
	else if ( eqNByte( RateSection, "HT", 2 ) && eqNByte( RfPath, "2T", 2 ) )
		rateSection = 3;
	else if ( eqNByte( RateSection, "VHT", 3 ) && eqNByte( RfPath, "1T", 2 ) )
		rateSection = 4;
	else if ( eqNByte( RateSection, "VHT", 3 ) && eqNByte( RfPath, "2T", 2 ) )
		rateSection = 5;
			

	if ( eqNByte( Bandwidth, "20M", 3 ) ) bandwidth = 0;
	else if ( eqNByte( Bandwidth, "40M", 3 ) ) bandwidth = 1;
	else if ( eqNByte( Bandwidth, "80M", 3 ) ) bandwidth = 2;
	else if ( eqNByte( Bandwidth, "160M", 4 ) ) bandwidth = 3;

	if ( eqNByte( Band, "2.4G", 4 ) )
	{
		DBG_871X( "2.4G Band value : [regulation %d][bw %d][rate_section %d][chnl %d][val %d]\n", 
			regulation, bandwidth, rateSection, channel, powerLimit );
		channelGroup = phy_GetChannelGroup( BAND_ON_2_4G, channel );
		pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelGroup][ODM_RF_PATH_A] = powerLimit;
	}
	else if ( eqNByte( Band, "5G", 2 ) )
	{
		DBG_871X("5G Band value : [regulation %d][bw %d][rate_section %d][chnl %d][val %d]\n", 
			  regulation, bandwidth, rateSection, channel, powerLimit );
		channelGroup = phy_GetChannelGroup( BAND_ON_5G, channel );
		pHalData->TxPwrLimit_5G[regulation][bandwidth][rateSection][channelGroup][ODM_RF_PATH_A] = powerLimit;
	}
	else
	{
		DBG_871X("Cannot recognize the band info in %s\n", Band );
		return;
	}
}

u8
PHY_GetPowerLimitValue(
	IN	PADAPTER			Adapter,
	IN	u32					RegPwrTblSel,
	IN	BAND_TYPE			Band,
	IN	CHANNEL_WIDTH	Bandwidth,
	IN	RF_PATH				RfPath,
	IN	u8					DataRate,
	IN	u8					Channel
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	s16				band = -1, regulation = -1, bandwidth = -1,
					rfPath = -1, rateSection = -1, channelGroup = -1;
	u8				powerLimit = MAX_POWER_INDEX;

	if ( ( Adapter->registrypriv.RegEnableTxPowerLimit == 0 && pHalData->EEPROMRegulatory != 1 ) ||
		  pHalData->EEPROMRegulatory == 2 )
		return MAX_POWER_INDEX;

	switch( RegPwrTblSel )
	{
		case 1:
				regulation = TXPWR_LMT_ETSI;
				break;
		case 2:
				regulation = TXPWR_LMT_MKK;
				break;
		case 3:
				regulation = TXPWR_LMT_FCC;
				break;

		default:
				regulation = TXPWR_LMT_FCC;
				break;
	}
	//DBG_871X("pregistrypriv->RegPwrTblSel %d\n", RegPwrTblSel);	


	if ( Band == BAND_ON_2_4G ) band = 0; 
	else if ( Band == BAND_ON_5G ) band = 1; 

	if ( Bandwidth == CHANNEL_WIDTH_20 ) bandwidth = 0;
	else if ( Bandwidth == CHANNEL_WIDTH_40 ) bandwidth = 1;
	else if ( Bandwidth == CHANNEL_WIDTH_80 ) bandwidth = 2;
	else if ( Bandwidth == CHANNEL_WIDTH_160 ) bandwidth = 3;

	switch ( DataRate )
	{
		case MGN_1M: case MGN_2M: case MGN_5_5M: case MGN_11M:
			rateSection = 0;
			break;

		case MGN_6M: case MGN_9M: case MGN_12M: case MGN_18M:
		case MGN_24M: case MGN_36M: case MGN_48M: case MGN_54M:
			rateSection = 1;
			break;

		case MGN_MCS0: case MGN_MCS1: case MGN_MCS2: case MGN_MCS3: 
		case MGN_MCS4: case MGN_MCS5: case MGN_MCS6: case MGN_MCS7:
			rateSection = 2;
			break;
			
		case MGN_MCS8: case MGN_MCS9: case MGN_MCS10: case MGN_MCS11: 
		case MGN_MCS12: case MGN_MCS13: case MGN_MCS14: case MGN_MCS15:
			rateSection = 3;
			break;

		case MGN_VHT1SS_MCS0: case MGN_VHT1SS_MCS1: case MGN_VHT1SS_MCS2:
		case MGN_VHT1SS_MCS3: case MGN_VHT1SS_MCS4: case MGN_VHT1SS_MCS5:
		case MGN_VHT1SS_MCS6: case MGN_VHT1SS_MCS7: case MGN_VHT1SS_MCS8:
		case MGN_VHT1SS_MCS9:
			rateSection = 4;
			break;
			
		case MGN_VHT2SS_MCS0: case MGN_VHT2SS_MCS1: case MGN_VHT2SS_MCS2:
		case MGN_VHT2SS_MCS3: case MGN_VHT2SS_MCS4: case MGN_VHT2SS_MCS5:
		case MGN_VHT2SS_MCS6: case MGN_VHT2SS_MCS7: case MGN_VHT2SS_MCS8:
		case MGN_VHT2SS_MCS9:
			rateSection = 5;
			break;

		default:
			DBG_871X("Wrong rate 0x%x\n", DataRate );
			break;
	}

	if ( Band == BAND_ON_2_4G  && rateSection > 3 )
			DBG_871X("Wrong rate 0x%x: No VHT in 2.4G Band\n", DataRate );
	if ( Band == BAND_ON_5G  && rateSection == 0 )
			DBG_871X("Wrong rate 0x%x: No CCK in 5G Band\n", DataRate );

	// workaround for wrong index combination to obtain tx power limit, 
	// OFDM only exists in BW 20M
	if ( rateSection == 1 )
		bandwidth = 0;

	// workaround for wrong indxe combination to obtain tx power limit, 
	// HT on 80M will reference to HT on 40M
	if ( ( rateSection == 2 || rateSection == 3 ) && Band == BAND_ON_5G && bandwidth == 2 ) {
		bandwidth = 1;
	}

	if ( Band == BAND_ON_2_4G )
		channelGroup = phy_GetChannelGroup( BAND_ON_2_4G, Channel );
	else if ( Band == BAND_ON_5G )
		channelGroup = phy_GetChannelGroup( BAND_ON_5G, Channel );
	else if ( Band == BAND_ON_BOTH )
	{
		// BAND_ON_BOTH don't care temporarily 
	}
	
	if ( band == -1 || regulation == -1 || bandwidth == -1 || 
	     rateSection == -1 || channelGroup == -1 )
	{
		DBG_871X("Wrong index value to access power limit table \
			  [band %d][regulation %d][bandwidth %d][rf_path %d][rate_section %d][chnlGroup %d]\n", 
			  band, regulation, bandwidth, RfPath, rateSection, channelGroup );

		return 0xFF;
	}

	if ( Band == BAND_ON_2_4G )
		powerLimit = pHalData->TxPwrLimit_2_4G[regulation]
			[bandwidth][rateSection][channelGroup][RfPath];
	else if ( Band == BAND_ON_5G )
		powerLimit = pHalData->TxPwrLimit_5G[regulation]
			[bandwidth][rateSection][channelGroup][RfPath];
	else 
		DBG_871X("No power limit table of the specified band\n" );

	// combine 5G VHT & HT rate
	// 5G 20M and 40M HT and VHT can cross reference
	/*if ( Band == BAND_ON_5G && powerLimit == MAX_POWER_INDEX ) {
		if ( bandwidth == 0 || bandwidth == 1 ) { 
			if ( rateSection == 2 )
				powerLimit = pHalData->TxPwrLimit_5G[regulation]
										[bandwidth][4][channelGroup][RfPath];
			else if ( rateSection == 4 )
				powerLimit = pHalData->TxPwrLimit_5G[regulation]
										[bandwidth][2][channelGroup][RfPath];
			else if ( rateSection == 3 )
				powerLimit = pHalData->TxPwrLimit_5G[regulation]
										[bandwidth][5][channelGroup][RfPath];
			else if ( rateSection == 5 )
				powerLimit = pHalData->TxPwrLimit_5G[regulation]
										[bandwidth][3][channelGroup][RfPath];
		}
	}*/

	//DBG_871X("TxPwrLmt[Regulation %d][Band %d][BW %d][RFPath %d][Rate 0x%x][Chnl %d] = %d\n", 
	//								regulation, pHalData->CurrentBandType, Bandwidth, RfPath, DataRate, Channel, powerLimit);

	return powerLimit;
}


//
// 2012/10/18
//
VOID
PHY_StorePwrByRateIndexVhtSeries(
	IN	PADAPTER	Adapter,
	IN	u32			RegAddr,
	IN	u32			BitMask,
	IN	u32			Data
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			rf_path, rate_section;

	//
	// For VHT series TX power by rate table.
	// VHT TX power by rate off setArray = 
	// Band:-2G&5G = 0 / 1
	// RF: at most 4*4 = ABCD=0/1/2/3
	// CCK=0 				11/5.5/2/1
	// OFDM=1/2 			18/12/9/6     54/48/36/24
	// HT=3/4/56 			MCS0-3 MCS4-7 MCS8-11 MCS12-15
	// VHT=7/8/9/10/11		1SSMCS0-3 1SSMCS4-7 2SSMCS1/0/1SSMCS/9/8 2SSMCS2-5
	//
	// #define		TX_PWR_BY_RATE_NUM_BAND			2
	// #define		TX_PWR_BY_RATE_NUM_RF			4
	// #define		TX_PWR_BY_RATE_NUM_SECTION		12
	//

	//
	// 1. Judge TX power by rate array band type.
	//
	//if(RegAddr == rTxAGC_A_CCK11_CCK1_JAguar || RegAddr == rTxAGC_B_CCK11_CCK1_JAguar)
	if ((RegAddr & 0xFFF) == 0xC20)
	{
		pHalData->TxPwrByRateTable++;	// Record that it is the first data to record.		
		pHalData->TxPwrByRateBand = 0;
	}

	if ((RegAddr & 0xFFF) == 0xe20)
	{
		pHalData->TxPwrByRateTable++;	// The value should be 2 now.
	}

	if ((RegAddr & 0xFFF) == 0xC24 && pHalData->TxPwrByRateTable != 1)
	{
		pHalData->TxPwrByRateTable++;	// The value should be 3 bow.		
		pHalData->TxPwrByRateBand = 1;
	}

	//
	// 2. Judge TX power by rate array RF type
	//
	if ((RegAddr & 0xF00) == 0xC00)
	{
		rf_path = 0;
	}
	else if ((RegAddr & 0xF00) == 0xE00)
	{
		rf_path = 1;
	}

	//
	// 3. Judge TX power by rate array rate section
	//
	if (rf_path == 0)
	{
		rate_section = (u8)((RegAddr&0xFFF)-0xC20)/4;
	}
	else if (rf_path == 1)
	{
		rate_section = (u8)((RegAddr&0xFFF)-0xE20)/4;
	}

	pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section] = Data;
	//DBG_871X("VHT TxPwrByRateOffset Addr-%x==>BAND/RF/SEC=%d/%d/%d = %08x\n", 
	//	RegAddr, pHalData->TxPwrByRateBand, rf_path, rate_section, Data);
	
}

VOID 
phy_ChangePGDataFromExactToRelativeValue(
	IN	u32*	pData,
	IN	u8		Start,
	IN	u8		End,
	IN	u8		BaseValue
	)
{
	s8	i = 0;
	u8	TempValue = 0;
	u32	TempData = 0;
	//BaseValue = ( BaseValue & 0xf ) + ( ( BaseValue >> 4 ) & 0xf ) * 10;
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Corrected BaseValue %u\n", BaseValue ) );
	
	for ( i = 3; i >= 0; --i )
	{
		if ( i >= Start && i <= End )
		{
			// Get the exact value
			TempValue = ( u8 ) ( *pData >> ( i * 8 ) ) & 0xF;
			TempValue += ( ( u8 ) ( ( *pData >> ( i * 8 + 4 ) ) & 0xF ) ) * 10; 

			// Change the value to a relative value
			TempValue = ( TempValue > BaseValue ) ? TempValue - BaseValue : BaseValue - TempValue;
		}
		else
		{
			TempValue = ( u8 ) ( *pData >> ( i * 8 ) ) & 0xFF;
		}
		
		
		
		TempData <<= 8;
		TempData |= TempValue;
	}

	*pData = TempData;
}

VOID phy_PreprocessVHTPGDataFromExactToRelativeValue(
	IN	PADAPTER	Adapter,
	IN	u32			RegAddr,
	IN	u32			BitMask,
	IN	u32*		pData
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			rf_path, rate_section, BaseValue = 0;
	//
	// For VHT series TX power by rate table.
	// VHT TX power by rate off setArray = 
	// Band:-2G&5G = 0 / 1
	// RF: at most 4*4 = ABCD=0/1/2/3
	// CCK=0 				11/5.5/2/1
	// OFDM=1/2 			18/12/9/6     54/48/36/24
	// HT=3/4/56 			MCS0-3 MCS4-7 MCS8-11 MCS12-15
	// VHT=7/8/9/10/11		1SSMCS0-3 1SSMCS4-7 2SSMCS1/0/1SSMCS/9/8 2SSMCS2-5
	//
	// #define		TX_PWR_BY_RATE_NUM_BAND			2
	// #define		TX_PWR_BY_RATE_NUM_RF			4
	// #define		TX_PWR_BY_RATE_NUM_SECTION		12
	//
	// Judge TX power by rate array RF type
	//
	if ( ( RegAddr & 0xF00 ) == 0xC00 )
	{
		rf_path = 0;
	}
	else if ( ( RegAddr & 0xF00 ) == 0xE00 )
	{
		rf_path = 1;
	}

	//
	// Judge TX power by rate array rate section
	//
	if ( rf_path == 0 )
	{
		rate_section = ( u8) ( ( RegAddr & 0xFFF ) - 0xC20 ) / 4;
	}
	else if ( rf_path == 1 )
	{
		rate_section = ( u8 ) ( ( RegAddr & 0xFFF ) - 0xE20 ) / 4;
	}
	
	switch ( RegAddr )
	{
		case 0xC20:
		case 0xE20:
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("RegAddr %x\n", RegAddr ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, before changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section, *pData ));
			BaseValue = ( ( u8 ) ( *pData >> 28 ) & 0xF ) *10 + ( ( u8 ) ( *pData >> 24 ) & 0xF );
			phy_ChangePGDataFromExactToRelativeValue( pData, 0, 3, BaseValue );
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, after changing to relative\n", 
			//		pHalData->TxPwrByRateBand, rf_path, rate_section, *pData ));
			break;

		case 0xC28:
		case 0xE28:
		case 0xC30:
		case 0xE30:
		case 0xC38:
		case 0xE38:
			
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("RegAddr %x\n", RegAddr ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, before changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section, *pData ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, before changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section - 1, pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] ));
			BaseValue = ( ( u8 ) ( *pData >> 28 ) & 0xF ) *10 + ( ( u8 ) ( *pData >> 24 ) & 0xF );
			phy_ChangePGDataFromExactToRelativeValue( pData, 0, 3, BaseValue );
			phy_ChangePGDataFromExactToRelativeValue( 
				&( pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] ),
				0, 3, BaseValue);
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, after changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section, *pData ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, after changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section - 1, pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] ));
			break;

		case 0xC44:
		case 0xE44:
			
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("RegAddr %x\n", RegAddr ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, before changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section, *pData ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, before changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section - 1, pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, before changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section - 2, pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 2] ));
			BaseValue = ( ( u8 ) ( pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] >> 28 ) & 0xF ) * 10 + 
						( ( u8 ) ( pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] >> 24 ) & 0xF );
			phy_ChangePGDataFromExactToRelativeValue( pData, 0, 1, BaseValue );
			phy_ChangePGDataFromExactToRelativeValue( 
				&( pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] ),
				0, 3, BaseValue);
			phy_ChangePGDataFromExactToRelativeValue( 
				&( pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 2] ),
				0, 3, BaseValue);
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, after changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section, *pData ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, after changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section - 1, pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, after changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section - 2, pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 2] ));
			break;
		
		case 0xC4C:
		case 0xE4C:
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("RegAddr %x\n", RegAddr ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, before changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section, *pData ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, before changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section - 1, pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, before changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section - 2, pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 2] ));
			BaseValue = ( ( u8 ) ( *pData >> 12 ) & 0xF ) *10 + ( ( u8 ) ( *pData >> 8 ) & 0xF );
			phy_ChangePGDataFromExactToRelativeValue( pData, 0, 3, BaseValue );
			phy_ChangePGDataFromExactToRelativeValue( 
				&( pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] ),
				0, 3, BaseValue);
			phy_ChangePGDataFromExactToRelativeValue( 
				&( pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 2] ),
				2, 3, BaseValue);
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, after changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section, *pData ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, after changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section - 1, pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 1] ));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("pHalData->TxPwrByRateOffset[%d][%d][%d] = 0x%x, after changing to relative\n", 
			//	pHalData->TxPwrByRateBand, rf_path, rate_section - 2, pHalData->TxPwrByRateOffset[pHalData->TxPwrByRateBand][rf_path][rate_section - 2] ));
			break;
	}
}

VOID
phy_PreprocessPGDataFromExactToRelativeValue(
	IN	PADAPTER	Adapter,
	IN	u4Byte		RegAddr,
	IN	u4Byte		BitMask,
	IN	pu4Byte		pData
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u1Byte			BaseValue = 0;
	
	if ( RegAddr == rTxAGC_A_Rate54_24 )
	{
		//DBG_871X("RegAddr %x\n", RegAddr );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][1] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][0] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0] );
		BaseValue = ( ( u8 ) ( *pData >> 28 ) & 0xF ) *10 + ( ( u8 ) ( *pData >> 24 ) & 0xF );
		//DBG_871X("BaseValue = %d\n", BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( pData, 0, 3, BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( 
			&( pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0] ),
			0, 3, BaseValue);
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][1] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][0] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0] );
	}
	
	if ( RegAddr == rTxAGC_A_CCK1_Mcs32 )
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][6] = *pData;
		//DBG_871X("MCSTxPowerLevelOriginalOffset[%d][6] = 0x%x\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][6]);
	}
	
	if ( RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0xffffff00 )
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][7] = *pData;
		//DBG_871X("MCSTxPowerLevelOriginalOffset[%d][7] = 0x%x\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][7]);
	}	

	if ( RegAddr == rTxAGC_A_Mcs07_Mcs04 )
	{
		//DBG_871X("RegAddr %x\n", RegAddr );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][3] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][2] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][2] );
		BaseValue = ( ( u8 ) ( *pData >> 28 ) & 0xF ) *10 + ( ( u8 ) ( *pData >> 24 ) & 0xF );
		//DBG_871X("BaseValue = %d\n", BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( pData, 0, 3, BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( 
			&( pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][2] ),
			0, 3, BaseValue);
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][3] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][2] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][2] );
	}
	
	if ( RegAddr == rTxAGC_A_Mcs11_Mcs08 )
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4] = *pData;
		//DBG_871X("MCSTxPowerLevelOriginalOffset[%d][4] = 0x%x\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4]);
	}
	
	if ( RegAddr == rTxAGC_A_Mcs15_Mcs12 )
	{
		//DBG_871X("RegAddr %x\n", RegAddr );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][5] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][4] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4] );
		BaseValue = ( ( u8 ) ( *pData >> 28 ) & 0xF ) *10 + ( ( u8 ) ( *pData >> 24 ) & 0xF );
		//DBG_871X("BaseValue = %d\n", BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( pData, 0, 3, BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( 
			&( pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4] ),
			0, 3, BaseValue);
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][5] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][4] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4] );
	}

	if ( RegAddr == rTxAGC_B_Rate54_24 )
	{
		//DBG_871X("RegAddr %x\n", RegAddr );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][9] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][8] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][8] );
		BaseValue = ( ( u8 ) ( *pData >> 28 ) & 0xF ) *10 + ( ( u8 ) ( *pData >> 24 ) & 0xF );
		//DBG_871X("BaseValue = %d\n", BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( pData, 0, 3, BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( 
				&( pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][8] ),
				0, 3, BaseValue);
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][9] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][8] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][8] );
				
	}
	
	if ( RegAddr == rTxAGC_B_CCK1_55_Mcs32 )
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][14] = *pData;
		//DBG_871X("MCSTxPowerLevelOriginalOffset[%d][14] = 0x%x\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][14]);
	}
	
	if ( RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0x000000ff )
	{
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][15] = *pData;
		//DBG_871X("MCSTxPowerLevelOriginalOffset[%d][15] = 0x%x\n", pHalData->pwrGroupCnt,
		//	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][15]);
	}	

	if ( RegAddr == rTxAGC_B_Mcs07_Mcs04 )
	{
		//DBG_871X("RegAddr %x\n", RegAddr );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][11] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][10] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][10] );
		BaseValue = ( ( u8 ) ( *pData >> 28 ) & 0xF ) *10 + ( ( u8 ) ( *pData >> 24 ) & 0xF );
		//DBG_871X("BaseValue = %d\n", BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( pData, 0, 3, BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( 
				&( pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][10] ),
				0, 3, BaseValue);
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][11] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][10] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][10] );
	}

	if ( RegAddr == rTxAGC_B_Mcs15_Mcs12 )
	{
		//DBG_871X("RegAddr %x\n", RegAddr );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][13] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][12] = 0x%x, before changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][12] );
		BaseValue = ( ( u8 ) ( *pData >> 28 ) & 0xF ) *10 + ( ( u8 ) ( *pData >> 24 ) & 0xF );
		//DBG_871X("BaseValue = %d\n", BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( pData, 0, 3, BaseValue );
		phy_ChangePGDataFromExactToRelativeValue( 
				&( pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][12] ),
				0, 3, BaseValue);
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][13] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, *pData );
		//DBG_871X("pHalData->MCSTxPowerLevelOriginalOffset[%d][12] = 0x%x, after changing to relative\n", 
		//	pHalData->pwrGroupCnt, pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][12] );
	}
	
	

	//
	// 1. Judge TX power by rate array band type.
	//
	//if(RegAddr == rTxAGC_A_CCK11_CCK1_JAguar || RegAddr == rTxAGC_B_CCK11_CCK1_JAguar)
	
	if ( IS_HARDWARE_TYPE_8812( Adapter ) ||
		 IS_HARDWARE_TYPE_8821( Adapter ) )
	{
		phy_PreprocessVHTPGDataFromExactToRelativeValue( Adapter, RegAddr, 
			BitMask, pData );
	}
	
}

VOID
phy_StorePwrByRateIndexBase(	
	IN	PADAPTER	Adapter,
	IN	u32			RegAddr,
	IN	u32			Data
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			Base = 0;
	

	if( pHalData->TxPwrByRateTable == 1 && pHalData->TxPwrByRateBand == 0 ) // 2.4G
	{
		if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE )
		{
			Base = ( ( ( u8 ) ( Data >> 28 ) & 0xF ) * 10 + 
					( ( u8 ) ( Data >> 24 ) & 0xF ) );
			
			switch( RegAddr ) {
				case 0xC20:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][0] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of CCK (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][0] ) );
					break;
				case 0xC28:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][1] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of OFDM 54M (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][1] ) );
					break;
				case 0xC30:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][2] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of MCS7 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][2] ) );
					break;
				case 0xC38:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][3] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of MCS15 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][3] ) );
					break;
				default:
					break;
			};
		}
		else
		{
			Base = ( u8 ) ( Data >> 24 );
			switch( RegAddr ) {
				case 0xC20:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][0] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of CCK (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][0] ) );
					break;
				case 0xC28:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][1] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of OFDM 54M (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][1] ) );
					break;
				case 0xC30:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][2] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of MCS7 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][2] ) );
					break;
				case 0xC38:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][3] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of MCS15 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][3] ) );
					break;
				default:
					break;
			};
		}
	}
	else if ( pHalData->TxPwrByRateTable == 3 && pHalData->TxPwrByRateBand == 1 ) // 5G
	{
		if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE )
		{
			Base = ( ( ( u8 ) ( Data >> 28 ) & 0xF ) * 10 + 
					( ( u8 ) ( Data >> 24 ) & 0xF ) );
			
			switch( RegAddr ) 
			{
				case 0xC28:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][0] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of OFDM 54M (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][0] ) );
					break;
				case 0xC30:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][1] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of MCS7 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][1] ) );
					break;
				case 0xC38:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][2] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of MCS15 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][2] ) );
					break;
				case 0xC40:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][3] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of 1SS MCS7 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][3] ) );
					break;
				case 0xC4C:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][4] = 
						( u8 ) ( ( Data >> 12 ) & 0xF ) * 10 +
						( u8 ) ( ( Data >> 8 ) & 0xF );
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of 2SS MCS7 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][4] ) );
					break;
				case 0xE28:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][0] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of OFDM 54M (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][0] ) );
					break;
				case 0xE30:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][1] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of MCS7 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][1] ) );
					break;
				case 0xE38:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][2] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of MCS15 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][2] ) );
					break;
				case 0xE40:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][3] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of 1SS MCS7 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][3] ) );
					break;
				case 0xE4C:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][4] = 
						( u8 ) ( ( Data >> 12 ) & 0xF ) * 10 +
						( u8 ) ( ( Data >> 8 ) & 0xF );
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of 2SS MCS7 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][4] ) );
					break;
				default:
					break;
			};
		}
		else
		{
			Base = ( u8 ) ( Data >> 24 );
			switch( RegAddr ) {
				case 0xC28:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][0]  = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of OFDM 54M (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][0] ) );
					break;
				case 0xC30:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][1]  = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of MCS7 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][1] ) );
					break;
				case 0xC38:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][2]  = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of MCS15 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][2] ) );
					break;
				case 0xC40:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][3] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of 1SS MCS7 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][3] ) );
					break;
				case 0xC4C:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][4] = ( u8 ) ( ( Data >> 8 ) & 0xFF );
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of 2SS MCS7 (RF path A) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_A][4] ) );
					break;
				case 0xE28:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][0] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of OFDM 54M (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][0] ) );
					break;
				case 0xE30:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][1] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of MCS7 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][1] ) );
					break;
				case 0xE38:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][2]  = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of MCS15 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][2] ) );
					break;
				case 0xE40:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][3] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of 1SS MCS7 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][3] ) );
					break;
				case 0xE4C:
					pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][4] = ( u8 ) ( ( Data >> 8 ) & 0xFF );
					//RT_DISP(FPHY, PHY_TXPWR, ("5G power by rate of 2SS MCS7 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase5G[ODM_RF_PATH_B][4] ) );
					break;
				default:
					break;
			};
		}
	}
	else if( pHalData->TxPwrByRateTable == 2 && pHalData->TxPwrByRateBand == 0 ) // 2.4G
	{
		if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE )
		{
			Base = ( ( ( u8 ) ( Data >> 28 ) & 0xF ) * 10 + 
					( ( u8 ) ( Data >> 24 ) & 0xF ) );
			
			switch( RegAddr ) {
				case 0xE20:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][0] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of CCK (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][0] ) );
					break;
				case 0xE28:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][1] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of OFDM 54M (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][1] ) );
					break;
				case 0xE30:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][2] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of MCS7 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][2] ) );
					break;
				case 0xE38:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][3] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of MCS15 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][3] ) );
					break;
				default:
					break;
			};
		
		}
		else
		{
			Base = ( u8 ) ( Data >> 24 );
			switch( RegAddr ) {
				case 0xC20:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][0] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of CCK (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][0] ) );
					break;
				case 0xC28:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][1] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of OFDM 54M (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][1] ) );
					break;
				case 0xC30:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][2] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of MCS7 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][2] ) );
					break;
				case 0xC38:
					pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][3] = Base;
					//RT_DISP(FPHY, PHY_TXPWR, ("2.4G  power by rate of MCS15 (RF path B) = %d\n", 
					//	pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][3] ) );
					break;
				default:
					break;
			};
		}
	}

	//-------------- following code is for 88E ----------------//

	if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE )
	{
		Base =  ( u8 ) ( ( Data >> 28 ) & 0xF ) * 10 + 
				( u8 ) ( ( Data >> 24 ) & 0xF );
	}
	else 
	{
		Base =  ( u8 ) ( ( Data >> 24 ) & 0xFF );
	}
	
	switch ( RegAddr )
	{
		
		case rTxAGC_A_Rate54_24:
			pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][1] = Base;
			pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][1] = Base;
			//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of OFDM 54M (RF path A) = %d\n", pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][1]));
			break;
		case rTxAGC_A_Mcs07_Mcs04:
			pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][2] = Base;
			pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][2] = Base;
			//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of MCS7 (RF path A) = %d\n", pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][2]));
			break;
		case rTxAGC_A_Mcs15_Mcs12:
			pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][3] = Base;
			pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_B][3] = Base;
			//RT_DISP(FPHY, PHY_TXPWR, ("2.4G power by rate of MCS15 (RF path A) = %d\n", pHalData->TxPwrByRateBase2_4G[ODM_RF_PATH_A][3]));
			break;
		default:
			break;
			
	};
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
	u32	tmpData = Data;

	// If the pHalData->DM_OutSrc.PhyRegPgValueType == 1, which means that the data in PHY_REG_PG data are
	// exact value, we must change them into relative values
	if ( pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE )
	{
		//DBG_871X("PhyRegPgValueType = PHY_REG_PG_EXACT_VALUE\n");
		phy_PreprocessPGDataFromExactToRelativeValue( Adapter, RegAddr, BitMask, &Data );
		//DBG_871X("Data = 0x%x, tmpData = 0x%x\n", Data, tmpData );
	}
	
	//
	// 2012/09/26 MH Add for VHT series. The power by rate table is diffeent as before.
	// 2012/10/24 MH Add description for the old tx power by rate method is only used
	// for 11 n series. T
	//
	if (IS_HARDWARE_TYPE_8812(Adapter) ||
		IS_HARDWARE_TYPE_8821(Adapter))
	{
		PHY_StorePwrByRateIndexVhtSeries(Adapter, RegAddr, BitMask, Data);
	}

	// Awk add to stroe the base power by rate value 
	phy_StorePwrByRateIndexBase(Adapter, RegAddr, tmpData );

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

static u8
phy_DbmToTxPwrIdx(
	IN	PADAPTER		Adapter,
	IN	WIRELESS_MODE	WirelessMode,
	IN	int			PowerInDbm
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8	TxPwrIdx = 0;
	s32	Offset = 0;

#if 0
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
		//Offset = -7;
		Offset = -6;	// For 88 RU test only		
		TxPwrIdx = (u8)((pHalData->OriginalCckTxPwrIdx*( PowerInDbm-pHalData->MinCCKDbm))/(pHalData->MaxCCKDbm-pHalData->MinCCKDbm));		
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		Offset = -8;		
		TxPwrIdx = (u8)((pHalData->OriginalOfdm24GTxPwrIdx* (PowerInDbm-pHalData->MinHOFDMDbm))/(pHalData->MaxHOFDMDbm-pHalData->MinHOFDMDbm));
		break;
	
	default: //for MacOSX compiler warning
		break;		
	}

	if (PowerInDbm <= pHalData->MinCCKDbm || 
		PowerInDbm <= pHalData->MinLOFDMDbm ||
		PowerInDbm <= pHalData->MinHOFDMDbm)
	{
		TxPwrIdx = 0;
	}

	// Simple judge to prevent tx power exceed the limitation.
	if (PowerInDbm >= pHalData->MaxCCKDbm || 
		PowerInDbm >= pHalData->MaxLOFDMDbm ||
		PowerInDbm >= pHalData->MaxHOFDMDbm)
	{
		if (WirelessMode == WIRELESS_MODE_B)
			TxPwrIdx = pHalData->OriginalCckTxPwrIdx;
		else
			TxPwrIdx = pHalData->OriginalOfdm24GTxPwrIdx;
	}
#endif
	return TxPwrIdx;
}

static int
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
		break;

	default: //for MacOSX compiler warning
		break;
	}

	PwrOutDbm = TxPwrIdx / 2 + Offset; // Discard the decimal part.

	return PwrOutDbm;
}

VOID
PHY_GetTxPowerLevel8812(
	IN	PADAPTER		Adapter,
	OUT u32*    		powerlevel
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			TxPwrLevel = 0;
	int			TxPwrDbm;
#if 0
	//
	// Because the Tx power indexes are different, we report the maximum of them to
	// meet the CCX TPC request. By Bruce, 2008-01-31.
	//

	// CCK
	TxPwrLevel = pHalData->CurrentCckTxPwrIdx;
	TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_B, TxPwrLevel);
	pHalData->MaxCCKDbm = TxPwrDbm;

	// Legacy OFDM
	TxPwrLevel = pHalData->CurrentOfdm24GTxPwrIdx + pHalData->LegacyHTTxPowerDiff;

	// Compare with Legacy OFDM Tx power.
	pHalData->MaxLOFDMDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_G, TxPwrLevel);
	if(phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_G, TxPwrLevel);

	// HT OFDM
	TxPwrLevel = pHalData->CurrentOfdm24GTxPwrIdx;

	// Compare with HT OFDM Tx power.
	pHalData->MaxHOFDMDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_G, TxPwrLevel);
	if(phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_N_24G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_N_24G, TxPwrLevel);
	pHalData->MaxHOFDMDbm = TxPwrDbm;

	*powerlevel = TxPwrDbm;
#endif
}

void phy_PowerIndexCheck8812(
	IN	PADAPTER	Adapter,
	IN	u8			channel,
	IN OUT u8 *		cckPowerLevel,
	IN OUT u8 *		ofdmPowerLevel,
	IN OUT u8 *		BW20PowerLevel,
	IN OUT u8 *		BW40PowerLevel	
	)
{

	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
#if 0//(CCX_SUPPORT == 1)
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

	//RT_TRACE(COMP_TXAGC, DBG_LOUD, 
	//	("phy_PowerIndexCheck8812(): CurrentCckTxPwrIdx : 0x%x,CurrentOfdm24GTxPwrIdx: 0x%x, CurrentBW2024GTxPwrIdx: 0x%dx, CurrentBW4024GTxPwrIdx: 0x%x \n", 
	//	pHalData->CurrentCckTxPwrIdx, pHalData->CurrentOfdm24GTxPwrIdx, pHalData->CurrentBW2024GTxPwrIdx, pHalData->CurrentBW4024GTxPwrIdx));
}

BOOLEAN 
phy_GetChnlIndex8812A(
	IN	u8 	Channel,
	OUT u8*	ChannelIdx
	)
{
	u8 	channel5G[CHANNEL_MAX_NUMBER_5G] = 
				 {36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,
				114,116,118,120,122,124,126,128,130,132,134,136,138,140,142,144,149,151,
				153,155,157,159,161,163,165,167,168,169,171,173,175,177};
	u8	i = 0;
	BOOLEAN bIn24G=_TRUE;

	if(Channel <= 14)
	{
		bIn24G=_TRUE;
        	*ChannelIdx = Channel -1;
	}
	else
	{
		bIn24G = _FALSE;

		for (i = 0; i < sizeof(channel5G)/sizeof(u8); ++i)
		{
			if ( channel5G[i] == Channel) {
				*ChannelIdx = i;
				return bIn24G;
			}
		}
	}
	return bIn24G;
	
}

//
// For VHT series, we will use a new TX pwr by rate array to meet new spec.
//
u32 
phy_GetTxPwrByRateOffset_8812(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	u8			Rf_Path,
	IN	u8			Rate_Section
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	u8			shift = 0, original_rate = Rate_Section;
	u32			tx_pwr_diff = 0;

	//
	// For VHT series TX power by rate table.
	// VHT TX power by rate off setArray = 
	// Band:-2G&5G = 0 / 1
	// RF: at most 4*4 = ABCD=0/1/2/3
	// CCK=0 				11/5.5/2/1
	// OFDM=1/2 			18/12/9/6     54/48/36/24
	// HT=3/4/5/6 			MCS0-3 MCS4-7 MCS8-11 MCS12-15
	// VHT=7/8/9/10/11		1SSMCS0-3 1SSMCS4-7 2SSMCS1/0/1SSMCS/9/8 2SSMCS2-5
	//
	// #define		TX_PWR_BY_RATE_NUM_BAND			2
	// #define		TX_PWR_BY_RATE_NUM_RF			4
	// #define		TX_PWR_BY_RATE_NUM_SECTION		12
	//

	switch	(Rate_Section)
	{
		case	MGN_1M:
		case	MGN_2M:
		case	MGN_5_5M:	
		case	MGN_11M:	
			Rate_Section =0;
			break;

		case	MGN_6M:		
		case	MGN_9M:		
		case	MGN_12M:	
		case	MGN_18M:	
			Rate_Section =1;
			break;

		case	MGN_24M:	
		case	MGN_36M:    
		case	MGN_48M:    
		case	MGN_54M:    
			Rate_Section =2;
			break;
		
		case	MGN_MCS0:	
		case	MGN_MCS1:   
		case	MGN_MCS2:   
		case	MGN_MCS3:   
			Rate_Section =3;
			break;

		case	MGN_MCS4:	
		case	MGN_MCS5:   
		case	MGN_MCS6:   
		case	MGN_MCS7:   
			Rate_Section =4;
			break;

		case	MGN_MCS8:	
		case	MGN_MCS9:   
		case	MGN_MCS10:  
		case	MGN_MCS11:  
			Rate_Section =5;
			break;

		case	MGN_MCS12:	
		case	MGN_MCS13:  
		case	MGN_MCS14:  
		case	MGN_MCS15:  
			Rate_Section =6;
			break;
		
		case	MGN_VHT1SS_MCS0:	
		case	MGN_VHT1SS_MCS1:    
		case	MGN_VHT1SS_MCS2:    
		case	MGN_VHT1SS_MCS3:    
			Rate_Section =7;
			break;

		case	MGN_VHT1SS_MCS4:	
		case	MGN_VHT1SS_MCS5:    
		case	MGN_VHT1SS_MCS6:    
		case	MGN_VHT1SS_MCS7:    
			Rate_Section =8;
			break;

		case	MGN_VHT1SS_MCS8:	
		case	MGN_VHT1SS_MCS9:    
		case	MGN_VHT2SS_MCS0:    
		case	MGN_VHT2SS_MCS1:    
			Rate_Section =9;
			break;

		case	MGN_VHT2SS_MCS2:	
		case	MGN_VHT2SS_MCS3:    
		case	MGN_VHT2SS_MCS4:    
		case	MGN_VHT2SS_MCS5:    
			Rate_Section =10;
			break;

		case	MGN_VHT2SS_MCS6:	
		case	MGN_VHT2SS_MCS7:    
		case	MGN_VHT2SS_MCS8:    
		case	MGN_VHT2SS_MCS9:    
			Rate_Section =11;
			break;
			
		default:
			DBG_871X("Rate_Section is Illegal\n");
			break;
	}
	
	switch	(original_rate)
	{
		case	MGN_1M:		shift = 0;		break;
		case	MGN_2M:		shift = 8;		break;
		case	MGN_5_5M:	shift = 16;		break;
		case	MGN_11M:	shift = 24;		break;			

		case	MGN_6M:		shift = 0;		break;
		case	MGN_9M:		shift = 8;      break;
		case	MGN_12M:	shift = 16;     break;
		case	MGN_18M:	shift = 24;     break;
			
		case	MGN_24M:	shift = 0;    	break;
		case	MGN_36M:    shift = 8;      break;
		case	MGN_48M:    shift = 16;     break;
		case	MGN_54M:    shift = 24;     break;
			
		case	MGN_MCS0:	shift = 0; 		break;
		case	MGN_MCS1:   shift = 8;      break;
		case	MGN_MCS2:   shift = 16;     break;
		case	MGN_MCS3:   shift = 24;     break;
			
		case	MGN_MCS4:	shift = 0; 		break;
		case	MGN_MCS5:   shift = 8;      break;
		case	MGN_MCS6:   shift = 16;     break;
		case	MGN_MCS7:   shift = 24;     break;
			
		case	MGN_MCS8:	shift = 0; 		break;
		case	MGN_MCS9:   shift = 8;      break;
		case	MGN_MCS10:  shift = 16;     break;
		case	MGN_MCS11:  shift = 24;     break;
			
		case	MGN_MCS12:	shift = 0; 		break;
		case	MGN_MCS13:  shift = 8;      break;
		case	MGN_MCS14:  shift = 16;     break;
		case	MGN_MCS15:  shift = 24;     break;
			
		case	MGN_VHT1SS_MCS0:	shift = 0; 		break;
		case	MGN_VHT1SS_MCS1:    shift = 8;      break;
		case	MGN_VHT1SS_MCS2:    shift = 16;     break;
		case	MGN_VHT1SS_MCS3:    shift = 24;     break;
			
		case	MGN_VHT1SS_MCS4:	shift = 0; 		break;
		case	MGN_VHT1SS_MCS5:    shift = 8;      break;
		case	MGN_VHT1SS_MCS6:    shift = 16;     break;
		case	MGN_VHT1SS_MCS7:    shift = 24;     break;
			
		case	MGN_VHT1SS_MCS8:	shift = 0; 		break;
		case	MGN_VHT1SS_MCS9:    shift = 8;      break;
		case	MGN_VHT2SS_MCS0:    shift = 16;     break;
		case	MGN_VHT2SS_MCS1:    shift = 24;     break;
			
		case	MGN_VHT2SS_MCS2:	shift = 0; 		break;
		case	MGN_VHT2SS_MCS3:    shift = 8;      break;
		case	MGN_VHT2SS_MCS4:    shift = 16;     break;
		case	MGN_VHT2SS_MCS5:    shift = 24;     break;
			
		case	MGN_VHT2SS_MCS6:	shift = 0; 		break;
		case	MGN_VHT2SS_MCS7:    shift = 8;      break;
		case	MGN_VHT2SS_MCS8:    shift = 16;     break;
		case	MGN_VHT2SS_MCS9:    shift = 24;     break;
			
		default:
			DBG_871X("Rate_Section is Illegal\n");
			break;
	}

	// Willis suggest to adopt 5G VHT power by rate for 2.4G
	if ( Band == BAND_ON_2_4G && ( Rate_Section >= 7 && Rate_Section <= 11 ) )
		Band = BAND_ON_5G;

	tx_pwr_diff = (pHalData->TxPwrByRateOffset[Band][Rf_Path][Rate_Section] >> shift) & 0xff;

	//DBG_871X("TxPwrByRateOffset-BAND(%d)-RF(%d)-RAS(%d)=%x tx_pwr_diff=%d shift=%d\n", 
	//Band, Rf_Path, Rate_Section, pHalData->TxPwrByRateOffset[Band][Rf_Path][Rate_Section], tx_pwr_diff, shift);

	return	tx_pwr_diff;

}	// phy_GetTxPwrByRateOffset_8812


//
//	Description: 
//		Subtract number of TxPwr index from different advance settings.
//
//	2010.03.09, added by Roger.
//
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

/**************************************************************************************************************
 *   Description: 
 *       The low-level interface to get the FINAL Tx Power Index , called  by both MP and Normal Driver.
 *
 *                                                                                    <20120830, Kordan>
 **************************************************************************************************************/
u32
PHY_GetTxPowerIndex_8812A(
	IN	PADAPTER			pAdapter,
	IN	u8					RFPath,
	IN	u8					Rate,	
	IN	CHANNEL_WIDTH	BandWidth,	
	IN	u8					Channel
	)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pAdapter);
	PDM_ODM_T			pDM_Odm = &pHalData->odmpriv;
	u8					i = 0;	//default set to 1S
	struct registry_priv	*pregistrypriv = &pAdapter->registrypriv;
	u32					powerDiffByRate = 0;
	u32					txPower = 0;
	u8					chnlIdx = (Channel-1);
	BOOLEAN				bIn24G = _FALSE;

	//DBG_871X("===> PHY_GetTxPowerIndex_8812A\n");
	
	if (HAL_IsLegalChannel(pAdapter, Channel) == _FALSE)
	{
		chnlIdx = 0;
		DBG_871X("Illegal channel!!\n");
	}	

	bIn24G = phy_GetChnlIndex8812A(Channel, &chnlIdx);

	//DBG_871X("[%s] Channel Index: %d\n", (bIn24G?"2.4G":"5G"), chnlIdx);

	if (bIn24G) //3 ============================== 2.4 G ==============================
	{
		if ( IS_CCK_RATE(Rate) )
		{
			txPower = pHalData->Index24G_CCK_Base[RFPath][chnlIdx];	
		}
		else if ( MGN_6M <= Rate )
		{				
			txPower = pHalData->Index24G_BW40_Base[RFPath][chnlIdx];
		}
		else
		{
			DBG_871X("===> mpt_ProQueryCaltxPower_Jaguar: INVALID Rate.\n");
		}

		//DBG_871X("Base Tx power(RF-%c, Rate #%d, Channel Index %d) = 0x%X\n", ((RFPath==0)?'A':'B'), Rate, chnlIdx, txPower);
		
		// OFDM-1T
		if ( MGN_6M <= Rate && Rate <= MGN_54M && ! IS_CCK_RATE(Rate) )
		{
			txPower += pHalData->OFDM_24G_Diff[RFPath][TX_1S];
			//DBG_871X("+PowerDiff 2.4G (RF-%c): (OFDM-1T) = (%d)\n", ((RFPath==0)?'A':'B'), pHalData->OFDM_24G_Diff[RFPath][TX_1S]);
		}
		// BW20-1S, BW20-2S
		if (BandWidth == CHANNEL_WIDTH_20)
		{
			if ( (MGN_MCS0 <= Rate && Rate <= MGN_MCS15) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
				txPower += pHalData->BW20_24G_Diff[RFPath][TX_1S];
			if ( (MGN_MCS8 <= Rate && Rate <= MGN_MCS15) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
				txPower += pHalData->BW20_24G_Diff[RFPath][TX_2S];

			//DBG_871X("+PowerDiff 2.4G (RF-%c): (BW20-1S, BW20-2S) = (%d, %d)\n", ((RFPath==0)?'A':'B'), 
			//	pHalData->BW20_24G_Diff[RFPath][TX_1S], pHalData->BW20_24G_Diff[RFPath][TX_2S]);
		}
		// BW40-1S, BW40-2S
		else if (BandWidth == CHANNEL_WIDTH_40)
		{
			if ( (MGN_MCS0 <= Rate && Rate <= MGN_MCS15) || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_1S];
			if ( (MGN_MCS8 <= Rate && Rate <= MGN_MCS15) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_2S];

			//DBG_871X("+PowerDiff 2.4G (RF-%c): (BW40-1S, BW40-2S) = (%d, %d)\n", ((RFPath==0)?'A':'B'), 
			//	pHalData->BW40_24G_Diff[RFPath][TX_1S], pHalData->BW40_24G_Diff[RFPath][TX_2S]);
		}
		// Willis suggest adopt BW 40M power index while in BW 80 mode
		else if ( BandWidth == CHANNEL_WIDTH_80 )
		{
			if ( (MGN_MCS0 <= Rate && Rate <= MGN_MCS15) || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_1S];
			if ( (MGN_MCS8 <= Rate && Rate <= MGN_MCS15) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_2S];

			//DBG_871X("+PowerDiff 2.4G (RF-%c): (BW40-1S, BW40-2S) = (%d, %d) P.S. Current is in BW 80MHz\n", ((RFPath==0)?'A':'B'), 
			//	pHalData->BW40_24G_Diff[RFPath][TX_1S], pHalData->BW40_24G_Diff[RFPath][TX_2S]);
		}

		//
		// 2012/09/26 MH Accordng to BB team's opinion, there might 40M VHT mode in the future.?
		// We need to judge VHT mode by what?
		//
	}
	else //3 ============================== 5 G ==============================
	{
		if ( MGN_6M <= Rate )
		{				
			txPower = pHalData->Index5G_BW40_Base[RFPath][chnlIdx];
		}
		else
		{
			DBG_871X("===> mpt_ProQueryCalTxPower_Jaguar: INVALID Rate.\n");
		}		

		//DBG_871X("Base Tx power(RF-%c, Rate #%d, Channel Index %d) = 0x%X\n", ((RFPath==0)?'A':'B'), Rate, chnlIdx, txPower);

		// OFDM-1T
		if ( MGN_6M <= Rate && Rate <= MGN_54M && ! IS_CCK_RATE(Rate))
		{
			txPower += pHalData->OFDM_5G_Diff[RFPath][TX_1S];
			//DBG_871X("+PowerDiff 5G (RF-%c): (OFDM-1T) = (%d)\n", ((RFPath==0)?'A':'B'), pHalData->OFDM_5G_Diff[RFPath][TX_1S]);
		}
		
		// BW20-1S, BW20-2S
		if (BandWidth == CHANNEL_WIDTH_20)
		{
			if ( (MGN_MCS0 <= Rate && Rate <= MGN_MCS15)  || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
			    txPower += pHalData->BW20_5G_Diff[RFPath][TX_1S];
			if ( (MGN_MCS8 <= Rate && Rate <= MGN_MCS15) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
			    txPower += pHalData->BW20_5G_Diff[RFPath][TX_2S];

			//DBG_871X("+PowerDiff 5G (RF-%c): (BW20-1S, BW20-2S) = (%d, %d)\n", ((RFPath==0)?'A':'B'), 
			//	pHalData->BW20_5G_Diff[RFPath][TX_1S], pHalData->BW20_5G_Diff[RFPath][TX_2S]);
		}
		// BW40-1S, BW40-2S
		else if (BandWidth == CHANNEL_WIDTH_40)
		{
			if ( (MGN_MCS0 <= Rate && Rate <= MGN_MCS15)  || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
			    txPower += pHalData->BW40_5G_Diff[RFPath][TX_1S];
			if ( (MGN_MCS8 <= Rate && Rate <= MGN_MCS15) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
			    txPower += pHalData->BW40_5G_Diff[RFPath][TX_2S];

			//DBG_871X("+PowerDiff 5G(RF-%c): (BW40-1S, BW40-2S) = (%d, %d)\n", ((RFPath==0)?'A':'B'), 
			//	pHalData->BW40_5G_Diff[RFPath][TX_1S], pHalData->BW40_5G_Diff[RFPath][TX_2S]);
		}
		// BW80-1S, BW80-2S
		else if (BandWidth== CHANNEL_WIDTH_80)
		{
			// <20121220, Kordan> Get the index of array "Index5G_BW80_Base".
			u8	channel5G_80M[CHANNEL_MAX_NUMBER_5G_80M] = {42, 58, 106, 122, 138, 155, 171};
			for (i = 0; i < sizeof(channel5G_80M)/sizeof(u8); ++i)
				if ( channel5G_80M[i] == Channel) 
					chnlIdx = i;
		
			if ( (MGN_MCS0 <= Rate && Rate <= MGN_MCS15)  || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
				txPower = pHalData->Index5G_BW80_Base[RFPath][chnlIdx] + pHalData->BW80_5G_Diff[RFPath][TX_1S];
			if ( (MGN_MCS8 <= Rate && Rate <= MGN_MCS15) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT2SS_MCS9))
				txPower = pHalData->Index5G_BW80_Base[RFPath][chnlIdx] + pHalData->BW80_5G_Diff[RFPath][TX_1S] + pHalData->BW80_5G_Diff[RFPath][TX_2S];

			//DBG_871X("+PowerDiff 5G(RF-%c): (BW80-1S, BW80-2S) = (%d, %d)\n", ((RFPath==0)?'A':'B'), 
			//	pHalData->BW80_5G_Diff[RFPath][TX_1S], pHalData->BW80_5G_Diff[RFPath][TX_2S]);
		}
	}
	
	// Band:-2G&5G = 0 / 1
	// Becasue in the functionwe use the bIn24G = 1=2.4G. Then we need to convert the value.
	// RF: at most 4*4 = ABCD=0/1/2/3
	// CCK=0 				11/5.5/2/1
	// OFDM=1/2 			18/12/9/6     54/48/36/24
	// HT=3/4/5/6 			MCS0-3 MCS4-7 MCS8-11 MCS12-15
	// VHT=7/8/9/10/11		1SSMCS0-3 1SSMCS4-7 2SSMCS1/0/1SSMCS/9/8 2SSMCS2-5	
	if (pregistrypriv->RegPwrByRate == _FALSE && pHalData->EEPROMRegulatory != 2)
	{
		powerDiffByRate = phy_GetTxPwrByRateOffset_8812(pAdapter, (u8)(!bIn24G), RFPath, Rate);

		if ( ( pregistrypriv->RegEnableTxPowerLimit == 1 && pHalData->EEPROMRegulatory != 2 ) || 
		 	  pHalData->EEPROMRegulatory == 1 ) 
		{
			u8 limit = 0;
			limit = PHY_GetPowerLimitValue(pAdapter, pregistrypriv->RegPwrTblSel, (u8)(!bIn24G) ? BAND_ON_5G : BAND_ON_2_4G, BandWidth, (ODM_RF_RADIO_PATH_E)RFPath, Rate, Channel);

			if ( Rate == MGN_VHT1SS_MCS8 || Rate == MGN_VHT1SS_MCS9  || 
				 Rate == MGN_VHT2SS_MCS8 || Rate == MGN_VHT2SS_MCS9 ) 
			{
				if ( limit < 0 ) 
				{
					if ( powerDiffByRate < -limit )
						powerDiffByRate = -limit;
				}
			}
			else
			{
				if ( limit < 0 )
					powerDiffByRate = limit;
				else
					powerDiffByRate = powerDiffByRate > limit ? limit : powerDiffByRate;
			}
			//DBG_871X("Maximum power by rate %d, final power by rate %d\n", limit, powerDiffByRate );
		}
	}

	//DBG_871X("Rate-%x txPower=%x +PowerDiffByRate(RF-%c) = %d\n", Rate, txPower, ((RFPath==0)?'A':'B'), powerDiffByRate);

	// We need to reduce power index for VHT MCS 8 & 9.
	if (Rate == MGN_VHT1SS_MCS8 || Rate == MGN_VHT1SS_MCS9 ||
		Rate == MGN_VHT2SS_MCS8 || Rate == MGN_VHT2SS_MCS9)
	{
		txPower -= powerDiffByRate;
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
		if (adapter_to_dvobj(pAdapter)->usb_speed <= RTW_USB_SPEED_2 && IS_HARDWARE_TYPE_8812AU(pAdapter))
		{
			powerDiffByRate = 0;
		}
#endif // CONFIG_USB_HCI

		txPower += powerDiffByRate;
	}
	//DBG_871X("BASE ON HT MCS7\n");
	//DBG_871X("Final Tx Power(RF-%c, Channel: %d) = %d(0x%X)\n", ((RFPath==0)?'A':'B'), chnlIdx+1, txPower, txPower);

	if(pDM_Odm->Modify_TxAGC_Flag_PathA || pDM_Odm->Modify_TxAGC_Flag_PathB)    //20130424 Mimic whether path A or B has to modify TxAGC
	{
		//DBG_871X("Before add Remanant_OFDMSwingIdx[rfpath %u] %d", txPower);
		txPower += pDM_Odm->Remnant_OFDMSwingIdx[RFPath]; 
		//DBG_871X("After add Remanant_OFDMSwingIdx[rfpath %u] %d => txPower %d", RFPath, pDM_Odm->Remnant_OFDMSwingIdx[RFPath], txPower);
	}

	if(txPower > MAX_POWER_INDEX)
		txPower = MAX_POWER_INDEX;

	// 2012/09/26 MH We need to take care high power device limiation to prevent destroy EXT_PA.
	// This case had ever happened in CU/SU high power module. THe limitation = 0x20.
	// But for 8812, we still not know the value.
	phy_TxPwrAdjInPercentage(pAdapter, (u8 *)&txPower);
	
	return txPower;	
}

/**************************************************************************************************************
 *   Description: 
 *       The low-level interface to set TxAGC , called by both MP and Normal Driver.
 *
 *                                                                                    <20120830, Kordan>
 **************************************************************************************************************/

VOID
PHY_SetTxPowerIndex_8812A(
	IN	PADAPTER			Adapter,
	IN	u4Byte				PowerIndex,
	IN	u1Byte				RFPath,	
	IN	u1Byte				Rate
	)
{
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(Adapter);
	BOOLEAN				Direction = FALSE;
	u4Byte				TxagcOffset = 0;

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
	if(Direction == FALSE)
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

VOID 
phy_SetTxPowerIndexByRateArray(
	IN	PADAPTER			pAdapter,
	IN 	u8  					RFPath,
	IN	CHANNEL_WIDTH	BandWidth,	
	IN	u8					Channel,
	IN	u8*					Rates,
	IN	u8					RateArraySize
	)
{
	u32	powerIndex = 0;
	int	i = 0;

	for (i = 0; i < RateArraySize; ++i) 
	{
		powerIndex = PHY_GetTxPowerIndex_8812A(pAdapter, RFPath, Rates[i], BandWidth, Channel);

		PHY_SetTxPowerIndex_8812A(pAdapter, powerIndex, RFPath, Rates[i]);
	}

}

VOID
PHY_GetTxPowerIndexByRateArray_8812A(
	IN	PADAPTER			pAdapter,
	IN	u8  					RFPath,
	IN	CHANNEL_WIDTH	BandWidth,	
	IN	u8					Channel,
	IN	u8*					Rate,
	OUT	u8*					PowerIndex,
	IN	u8					ArraySize
	)
{	
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(pAdapter);
	u8 i;
	for(i=0 ; i<ArraySize; i++)
	{
		PowerIndex[i] = (u8)PHY_GetTxPowerIndex_8812A(pAdapter, RFPath, Rate[i], BandWidth, Channel);
		if ( (PowerIndex[i] % 2 == 1) && IS_HARDWARE_TYPE_JAGUAR(pAdapter) && ! IS_NORMAL_CHIP(pHalData->VersionID) )
			PowerIndex[i] -= 1;
	}
	
}

VOID
phy_TxPowerTrainingByPath_8812(
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
PHY_SetTxPowerLevelByPath8812(
	IN	PADAPTER		Adapter,
	IN	u8				channel,
	IN	u8				path
	)
{

	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;
	u8			cckRates[]   = {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M};
	u8			ofdmRates[]  = {MGN_6M, MGN_9M, MGN_12M, MGN_18M, MGN_24M, MGN_36M, MGN_48M, MGN_54M};
	u8			htRates1T[]  = {MGN_MCS0, MGN_MCS1, MGN_MCS2, MGN_MCS3, MGN_MCS4, MGN_MCS5, MGN_MCS6, MGN_MCS7};
	u8			htRates2T[]  = {MGN_MCS8, MGN_MCS9, MGN_MCS10, MGN_MCS11, MGN_MCS12, MGN_MCS13, MGN_MCS14, MGN_MCS15};
	u8			vhtRates1T[] = {MGN_VHT1SS_MCS0, MGN_VHT1SS_MCS1, MGN_VHT1SS_MCS2, MGN_VHT1SS_MCS3, MGN_VHT1SS_MCS4, 
                            		MGN_VHT1SS_MCS5, MGN_VHT1SS_MCS6, MGN_VHT1SS_MCS7, MGN_VHT1SS_MCS8, MGN_VHT1SS_MCS9};
	u8			vhtRates2T[] = {MGN_VHT2SS_MCS0, MGN_VHT2SS_MCS1, MGN_VHT2SS_MCS2, MGN_VHT2SS_MCS3, MGN_VHT2SS_MCS4, 
                            		MGN_VHT2SS_MCS5, MGN_VHT2SS_MCS6, MGN_VHT2SS_MCS7, MGN_VHT2SS_MCS8, MGN_VHT2SS_MCS9};

	//DBG_871X("==>PHY_SetTxPowerLevelByPath8812()\n");
#if(MP_DRIVER == 1)
	if (pregistrypriv->mp_mode == 1)
		return;
#endif

	//if(pMgntInfo->RegNByteAccess == 0)
	{
		if(pHalData->CurrentBandType == BAND_ON_2_4G)
			phy_SetTxPowerIndexByRateArray(Adapter, path, pHalData->CurrentChannelBW, channel,
									  cckRates, sizeof(cckRates)/sizeof(u1Byte));

		phy_SetTxPowerIndexByRateArray(Adapter, path, pHalData->CurrentChannelBW, channel,
									  ofdmRates, sizeof(ofdmRates)/sizeof(u1Byte));
		phy_SetTxPowerIndexByRateArray(Adapter, path, pHalData->CurrentChannelBW, channel,
									  htRates1T, sizeof(htRates1T)/sizeof(u1Byte));
		phy_SetTxPowerIndexByRateArray(Adapter, path, pHalData->CurrentChannelBW, channel,
								  	  vhtRates1T, sizeof(vhtRates1T)/sizeof(u1Byte));

		if(pHalData->NumTotalRFPath >= 2)
		{
			phy_SetTxPowerIndexByRateArray(Adapter, path, pHalData->CurrentChannelBW, channel,
								  htRates2T, sizeof(htRates2T)/sizeof(u1Byte));
			phy_SetTxPowerIndexByRateArray(Adapter, path, pHalData->CurrentChannelBW, channel,
								  vhtRates2T, sizeof(vhtRates2T)/sizeof(u1Byte));
		}
	}
	/*else
	{
		u1Byte cckRatesSize = sizeof(cckRates)/sizeof(u1Byte);
		u1Byte ofdmRatesSize = sizeof(ofdmRates)/sizeof(u1Byte);
		u1Byte htRates1TSize = sizeof(htRates1T)/sizeof(u1Byte);
		u1Byte htRates2TSize = sizeof(htRates2T)/sizeof(u1Byte);
		u1Byte vhtRates1TSize = sizeof(vhtRates1T)/sizeof(u1Byte);
		u1Byte vhtRates2TSize = sizeof(vhtRates2T)/sizeof(u1Byte);
		u1Byte PowerIndexArray[POWERINDEX_ARRAY_SIZE]; 
		
		u1Byte Length;
		u4Byte RegAddress;
		

		RT_TRACE(COMP_SCAN, DBG_LOUD, ("PHY_SetTxPowerLevel8812ByPath(): path = %d.\n",path));
		
		PHY_GetTxPowerIndexByRateArray_8812A(Adapter, path,pHalData->CurrentChannelBW, channel,ofdmRates,&PowerIndexArray[cckRatesSize],ofdmRatesSize);
		PHY_GetTxPowerIndexByRateArray_8812A(Adapter, path,pHalData->CurrentChannelBW, channel,htRates1T,&PowerIndexArray[cckRatesSize+ofdmRatesSize],htRates1TSize);
		if(pHalData->CurrentBandType == BAND_ON_2_4G)
		{
			PHY_GetTxPowerIndexByRateArray_8812A(Adapter, path,pHalData->CurrentChannelBW, channel,cckRates,&PowerIndexArray[0],cckRatesSize);
			PHY_GetTxPowerIndexByRateArray_8812A(Adapter, path,pHalData->CurrentChannelBW, channel,vhtRates1T,&PowerIndexArray[cckRatesSize+ofdmRatesSize+htRates1TSize+htRates2TSize],vhtRates1TSize);
			Length = cckRatesSize + ofdmRatesSize + htRates1TSize + htRates2TSize + vhtRates1TSize;
				
			if(pHalData->NumTotalRFPath >= 2)
			{
				PHY_GetTxPowerIndexByRateArray_8812A(Adapter, path,pHalData->CurrentChannelBW, channel,htRates2T,&PowerIndexArray[cckRatesSize+ofdmRatesSize+htRates1TSize],htRates2TSize);
				PHY_GetTxPowerIndexByRateArray_8812A(Adapter, path,pHalData->CurrentChannelBW, channel,vhtRates2T,&PowerIndexArray[cckRatesSize+ofdmRatesSize+htRates1TSize+htRates2TSize+vhtRates1TSize],vhtRates2TSize);
				Length += vhtRates2TSize;
			}
			
			if(path == ODM_RF_PATH_A)
				RegAddress = rTxAGC_A_CCK11_CCK1_JAguar;
			else					//ODM_RF_PATH_B
				RegAddress = rTxAGC_B_CCK11_CCK1_JAguar;

#ifdef CONFIG_USB_HCI
			if(pMgntInfo->RegNByteAccess == 2)  //N Byte access
			{
				PlatformIOWriteNByte(Adapter,RegAddress,Length,PowerIndexArray);
			}
			else if(pMgntInfo->RegNByteAccess == 1) //DW access
#endif			
			{
				u1Byte i, j;
				for(i = 0;i < Length;i+=4)
				{
					u4Byte powerIndex = 0;
					for(j = 0;j < 4; j++)
					{
						powerIndex |= (PowerIndexArray[i+j]<<(8*j));
					}
					
					PHY_SetBBReg(Adapter, RegAddress+i, bMaskDWord, powerIndex);
				}
			}
		}
		else if(pHalData->CurrentBandType == BAND_ON_5G)
		{
			PHY_GetTxPowerIndexByRateArray_8812A(Adapter, path,pHalData->CurrentChannelBW, channel,vhtRates1T,&PowerIndexArray[cckRatesSize+ofdmRatesSize+htRates1TSize+htRates2TSize],vhtRates1TSize);
			
			if(pHalData->NumTotalRFPath >= 2)
			{
				PHY_GetTxPowerIndexByRateArray_8812A(Adapter, path,pHalData->CurrentChannelBW, channel,htRates2T,&PowerIndexArray[cckRatesSize+ofdmRatesSize+htRates1TSize],htRates2TSize);
				PHY_GetTxPowerIndexByRateArray_8812A(Adapter, path,pHalData->CurrentChannelBW, channel,vhtRates2T,&PowerIndexArray[cckRatesSize+ofdmRatesSize+htRates1TSize+htRates2TSize+vhtRates1TSize],vhtRates2TSize);

				Length = ofdmRatesSize + htRates1TSize + htRates2TSize + vhtRates1TSize + vhtRates2TSize;
			}
			else
			{
				if(path == ODM_RF_PATH_A)
					RegAddress = rTxAGC_A_Nss1Index3_Nss1Index0_JAguar;
				else					// ODM_RF_PATH_B
					RegAddress = rTxAGC_B_Nss1Index3_Nss1Index0_JAguar;

#ifdef CONFIG_USB_HCI
				if(pMgntInfo->RegNByteAccess == 2)
				{
					PlatformIOWriteNByte(Adapter,RegAddress,vhtRates1TSize,&PowerIndexArray[cckRatesSize + ofdmRatesSize + htRates1TSize + htRates2TSize]);
				}
				else if(pMgntInfo->RegNByteAccess == 1) //DW access
#endif				
				{
					u1Byte i, j;
					for(i = 0;i < vhtRates1TSize;i+=4)
					{
						u4Byte powerIndex = 0;
						for(j = 0;j < 4; j++)
						{
							powerIndex |= (PowerIndexArray[cckRatesSize + ofdmRatesSize + htRates1TSize + htRates2TSize+i+j]<<(8*j));
						}
						
						PHY_SetBBReg(Adapter, RegAddress+i, bMaskDWord, powerIndex);
					}
					
					{
						u4Byte powerIndex = 0;
						//i+=4;
						for(j = 0;j < vhtRates1TSize%4;j++)  // for Nss1 MCS8,9
						{
							powerIndex |= (PowerIndexArray[cckRatesSize + ofdmRatesSize + htRates1TSize + htRates2TSize+i+j]<<(8*j));
						}
						PHY_SetBBReg(Adapter, RegAddress+i, bMaskLWord, powerIndex);
					}
				}

				Length = ofdmRatesSize + htRates1TSize;
			}
			
			if(path == ODM_RF_PATH_A)
				RegAddress = rTxAGC_A_Ofdm18_Ofdm6_JAguar;
			else					// ODM_RF_PATH_B
				RegAddress = rTxAGC_B_Ofdm18_Ofdm6_JAguar;

#ifdef CONFIG_USB_HCI
			if(pMgntInfo->RegNByteAccess == 2)
			{
				PlatformIOWriteNByte(Adapter,RegAddress,Length,&PowerIndexArray[cckRatesSize]);
			}
			else if(pMgntInfo->RegNByteAccess == 1) //DW
#endif			
			{
				u1Byte i, j;
				for(i = 0;i < Length;i+=4)
				{
					u4Byte powerIndex = 0;
					for(j = 0;j < 4; j++)
					{
						powerIndex |= (PowerIndexArray[cckRatesSize+i+j]<<(8*j));
					}
					
					PHY_SetBBReg(Adapter, RegAddress+i, bMaskDWord, powerIndex);
				}
			}
			
		}
	}*/

	phy_TxPowerTrainingByPath_8812(Adapter, pHalData->CurrentChannelBW, channel, path);

	//DBG_871X("<==PHY_SetTxPowerLevelByPath8812()\n");
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
		PHY_SetTxPowerLevelByPath8812(Adapter, Channel, path);
	}

	//DBG_871X("<==PHY_SetTxPowerLevel8812()\n");
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
				if ( pHalData->ExternalPA_5G ) {
					pRFCalibrateInfo->BBSwingDiff5G = -3;
					out = 0x16A;
				} else  {
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
		u32	swing = 0, swingA = 0, swingB = 0;

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
        
		swingA = (swing & 0x3) >> 0; // 0xC6/C7[1:0]
		swingB = (swing & 0xC) >> 2; // 0xC6/C7[3:2]

		//DBG_871X("===> PHY_GetTxBBSwing_8812A, swingA: 0x%X, swingB: 0x%X\n", swingA, swingB);

		//3 Path-A
		if (swingA == 0x00) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = 0;
			else
				pRFCalibrateInfo->BBSwingDiff5G = 0;
			out = 0x200; // 0 dB
		} else if (swingA == 0x01) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = -3;
			else
				pRFCalibrateInfo->BBSwingDiff5G = -3;
			out = 0x16A; // -3 dB
		} else if (swingA == 0x10) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = -6;
			else
				pRFCalibrateInfo->BBSwingDiff5G = -6;
			out = 0x101; // -6 dB
		} else if (swingA == 0x11) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = -9;
			else
				pRFCalibrateInfo->BBSwingDiff5G = -9;
			out = 0x0B6; // -9 dB
		}
		
		//3 Path-B
		if (swingB == 0x00) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = 0;
			else
				pRFCalibrateInfo->BBSwingDiff5G = 0;
			out = 0x200; // 0 dB
		} else if (swingB == 0x01) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = -3;
			else
				pRFCalibrateInfo->BBSwingDiff5G = -3;
			out = 0x16A; // -3 dB
		} else if (swingB == 0x10) {
			if (Band == BAND_ON_2_4G) 
				pRFCalibrateInfo->BBSwingDiff2G = -6;
			else
				pRFCalibrateInfo->BBSwingDiff5G = -6;
			out = 0x101; // -6 dB
		} else if (swingB == 0x11) {
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
		case 0: case 1: case 2:
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);
			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);
			PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
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
			//if(BT_IsBtExist(Adapter))
			{	
				//rtw_write16(Adapter, rA_RFE_Pinmux_Jaguar, 0x7777);
				rtw_write8(Adapter, rA_RFE_Pinmux_Jaguar+2, 0x77);
			}
			//else
				//PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);

			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77777777);

			//if(BT_IsBtExist(Adapter))
			{
				//u1tmp = rtw_read8(Adapter, rA_RFE_Inv_Jaguar+2);
				//rtw_write8(Adapter, rA_RFE_Inv_Jaguar+2,  (u1tmp &0x0f));
				u1tmp = rtw_read8(Adapter, rA_RFE_Inv_Jaguar+3);
				rtw_write8(Adapter, rA_RFE_Inv_Jaguar+3,  (u1tmp &= ~BIT0));
			}
			//else
				//PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar, bMask_RFEInv_Jaguar, 0x000);

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
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337717);
			PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337717);
			PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar,bMask_RFEInv_Jaguar, 0x000);
			break;			
		case 2: case 4:
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
			//if(BT_IsBtExist(Adapter))
			{	
				//rtw_write16(Adapter, rA_RFE_Pinmux_Jaguar, 0x7777);
				if(pHalData->ExternalPA_5G)
					PlatformEFIOWrite1Byte(Adapter, rA_RFE_Pinmux_Jaguar+2, 0x33);
				else
					PlatformEFIOWrite1Byte(Adapter, rA_RFE_Pinmux_Jaguar+2, 0x73);
			}
		#if 0
			else
			{
				if (pHalData->ExternalPA_5G)
					PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337777);
				else
					PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar,bMaskDWord, 0x77737777);
			}
		#endif

			if (pHalData->ExternalPA_5G)
				PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77337777);
			else
				PHY_SetBBReg(Adapter, rB_RFE_Pinmux_Jaguar,bMaskDWord, 0x77737777);

			//if(BT_IsBtExist(Adapter))
			{
				//u1tmp = rtw_read8(Adapter, rA_RFE_Inv_Jaguar+2);
				//rtw_write8(Adapter, rA_RFE_Inv_Jaguar+2,  (u1tmp &0x0f));
				u1tmp = rtw_read8(Adapter, rA_RFE_Inv_Jaguar+3);
				rtw_write8(Adapter, rA_RFE_Inv_Jaguar+3,  (u1tmp |= BIT0));
			}
			//else
				//PHY_SetBBReg(Adapter, rA_RFE_Inv_Jaguar, bMask_RFEInv_Jaguar, 0x010);
			
			PHY_SetBBReg(Adapter, rB_RFE_Inv_Jaguar, bMask_RFEInv_Jaguar, 0x010);
			break;
		default:
			break;
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
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0xF000, 0x7); // 0xCB0[15:12] = 0x7 (LNA_On)
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0xF0, 0x7); // 0xCB0[7:4] = 0x7 (PAPE_A)			
		}

		// AGC table select 
		if(IS_VENDOR_8821A_MP_CHIP(Adapter))
			PHY_SetBBReg(Adapter, rA_TxScale_Jaguar, 0xF00, 0); // 0xC1C[11:8] = 0
		else
			PHY_SetBBReg(Adapter, rAGC_table_Jaguar, 0x3, 0);

		if(IS_VENDOR_8812A_TEST_CHIP(Adapter))
		{
			// r_select_5G for path_A/B		
			PHY_SetBBReg(Adapter, rA_RFE_Jaguar, BIT12, 0x0);
			PHY_SetBBReg(Adapter, rB_RFE_Jaguar, BIT12, 0x0);

			// LANON (5G uses external LNA)
			PHY_SetBBReg(Adapter, rA_RFE_Jaguar, BIT15, 0x1);
			PHY_SetBBReg(Adapter, rB_RFE_Jaguar, BIT15, 0x1);
		}
		else if(IS_VENDOR_8812A_MP_CHIP(Adapter))
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

		// SYN Setting
		if(IS_VENDOR_8812A_TEST_CHIP(Adapter))
		{
			PHY_SetRFReg(Adapter, RF_PATH_A, 0xEF, bLSSIWrite_data_Jaguar, 0x40000); 
			PHY_SetRFReg(Adapter, RF_PATH_A, 0x3E, bLSSIWrite_data_Jaguar, 0x00000); 
			PHY_SetRFReg(Adapter, RF_PATH_A, 0x3F, bLSSIWrite_data_Jaguar, 0x0001c);
			PHY_SetRFReg(Adapter, RF_PATH_A, 0xEF, bLSSIWrite_data_Jaguar, 0x00000); 
			PHY_SetRFReg(Adapter, RF_PATH_A, 0xB5, bLSSIWrite_data_Jaguar, 0x16BFF); 
		}	

		// CCK_CHECK_en
		rtw_write8(Adapter, REG_CCK_CHECK_8812, 0x0);
	}
	else	//5G band
	{
		u16	count = 0, reg41A = 0;

		if (IS_HARDWARE_TYPE_8821(Adapter)) 
		{
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0xF000, 0x5); // 0xCB0[15:12] = 0x5 (LNA_On)
			PHY_SetBBReg(Adapter, rA_RFE_Pinmux_Jaguar, 0xF0, 0x4); // 0xCB0[7:4] = 0x4 (PAPE_A)			
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

		// AGC table select 
		if (IS_VENDOR_8821A_MP_CHIP(Adapter))
			PHY_SetBBReg(Adapter, rA_TxScale_Jaguar, 0xF00, 1); // 0xC1C[11:8] = 1
		else		
			PHY_SetBBReg(Adapter, rAGC_table_Jaguar, 0x3, 1);

		if(IS_VENDOR_8812A_TEST_CHIP(Adapter))
		{
			// r_select_5G for path_A/B		
			PHY_SetBBReg(Adapter, rA_RFE_Jaguar, BIT12, 0x1);
			PHY_SetBBReg(Adapter, rB_RFE_Jaguar, BIT12, 0x1);

			// LANON (5G uses external LNA)
			PHY_SetBBReg(Adapter, rA_RFE_Jaguar, BIT15, 0x0);
			PHY_SetBBReg(Adapter, rB_RFE_Jaguar, BIT15, 0x0);
		}
		else if(IS_VENDOR_8812A_MP_CHIP(Adapter))
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

		// SYN Setting
		if(IS_VENDOR_8812A_TEST_CHIP(Adapter))
		{
			PHY_SetRFReg(Adapter, RF_PATH_A, 0xEF, bLSSIWrite_data_Jaguar, 0x40000); 
			PHY_SetRFReg(Adapter, RF_PATH_A, 0x3E, bLSSIWrite_data_Jaguar, 0x00000); 
			PHY_SetRFReg(Adapter, RF_PATH_A, 0x3F, bLSSIWrite_data_Jaguar, 0x00017);
			PHY_SetRFReg(Adapter, RF_PATH_A, 0xEF, bLSSIWrite_data_Jaguar, 0x00000); 
			PHY_SetRFReg(Adapter, RF_PATH_A, 0xB5, bLSSIWrite_data_Jaguar, 0x04BFF); 
		}	

		//DBG_871X("==>PHY_SwitchWirelessBand8812() BAND_ON_5G settings OFDM index 0x%x\n", pHalData->OFDM_index[RF_PATH_A]);
	}
	
	//<20120903, Kordan> Tx BB swing setting for RL6286, asked by Ynlin.
	if (IS_NORMAL_CHIP(pHalData->VersionID) || IS_HARDWARE_TYPE_8821(Adapter))
	{			
		s8	BBDiffBetweenBand = 0;
		HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(GetDefaultAdapter(Adapter));
		PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
		PODM_RF_CAL_T  	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
		
		PHY_SetBBReg(Adapter, rA_TxScale_Jaguar, 0xFFE00000, 
					 PHY_GetTxBBSwing_8812A(Adapter, (BAND_TYPE)Band, ODM_RF_PATH_A)); // 0xC1C[31:21]
		PHY_SetBBReg(Adapter, rB_TxScale_Jaguar, 0xFFE00000, 
					 PHY_GetTxBBSwing_8812A(Adapter, (BAND_TYPE)Band, ODM_RF_PATH_B)); // 0xE1C[31:21]
					 
		// <20121005, Kordan> When TxPowerTrack is ON, we should take care of the change of BB swing.
		// That is, reset all info to trigger Tx power tracking.
		{
			if (Band != currentBand) 
			{
				BBDiffBetweenBand = (pRFCalibrateInfo->BBSwingDiff2G - pRFCalibrateInfo->BBSwingDiff5G);
				BBDiffBetweenBand = (Band == BAND_ON_2_4G) ? BBDiffBetweenBand : (-1 * BBDiffBetweenBand);
				pDM_Odm->DefaultOfdmIndex += BBDiffBetweenBand*2;				
			}

			ODM_ClearTxPowerTrackingState(pDM_Odm);
		}
	}
	
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

	//DBG_871X("SCMapping: VHT Case: pHalData->CurrentChannelBW %d, pHalData->nCur80MhzPrimeSC %d, pHalData->nCur40MhzPrimeSC %d \n",pHalData->CurrentChannelBW,pHalData->nCur80MhzPrimeSC,pHalData->nCur40MhzPrimeSC);
	if(pHalData->CurrentChannelBW== CHANNEL_WIDTH_80)
	{
		if(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			SCSettingOf40 = VHT_DATA_SC_40_LOWER_OF_80MHZ;
		else if(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			SCSettingOf40 = VHT_DATA_SC_40_UPPER_OF_80MHZ;
		else
			DBG_871X("SCMapping: Not Correct Primary40MHz Setting \n");
		
		if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
			SCSettingOf20 = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
			SCSettingOf20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
			SCSettingOf20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
			SCSettingOf20 = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
		else
			DBG_871X("SCMapping: Not Correct Primary40MHz Setting \n");
	}
	else if(pHalData->CurrentChannelBW == CHANNEL_WIDTH_40)
	{
		//DBG_871X("SCMapping: VHT Case: pHalData->CurrentChannelBW %d, pHalData->nCur40MhzPrimeSC %d \n",pHalData->CurrentChannelBW,pHalData->nCur40MhzPrimeSC);

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


	//3 Set Reg668 Reg440 BW
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

			PHY_SetBBReg(Adapter, rFPGA0_XB_RFInterfaceOE, 0x001C0000, 4);	// 0x864[20:18] = 3'b4

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

			PHY_SetBBReg(Adapter, rFPGA0_XB_RFInterfaceOE, 0x001C0000, 2);	// 0x864[20:18] = 3'b2

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

			PHY_SetBBReg(Adapter, rFPGA0_XB_RFInterfaceOE, 0x001C0000, 2);	// 0x864[20:18] = 3'b2

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
		} else if (36 <= channel && channel <= 64) {
			pDM_Odm->RSSI_TRSW_H   = 70; 
			pDM_Odm->RSSI_TRSW_iso = 25;
		} else if (100 <= channel && channel <= 144) {
			pDM_Odm->RSSI_TRSW_H   = 80; 
			pDM_Odm->RSSI_TRSW_iso = 35;
		} else if (149 <= channel) {
			pDM_Odm->RSSI_TRSW_H   = 75; 
			pDM_Odm->RSSI_TRSW_iso = 30;
		}

		pDM_Odm->RSSI_TRSW_L = pDM_Odm->RSSI_TRSW_H - pDM_Odm->RSSI_TRSW_iso - 10;
	}
}

VOID
phy_SwChnl8812(	
	IN	PADAPTER	pAdapter
	)
{
	u8	eRFPath = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8	channelToSW = pHalData->CurrentChannel;

	if (pAdapter->registrypriv.mp_mode == 0) {
		if(phy_SwBand8812(pAdapter, channelToSW) == _FALSE)
		{
			DBG_871X("error Chnl %d !\n", channelToSW);
		}
	}

	//<20130313, Kordan> Sample code to demonstrate how to configure AGC_TAB_DIFF.(Disabled by now) 
#if 0
	if (36 <= channelToSW && channelToSW <= 48) 
		AGC_DIFF_CONFIG(8812A,LB);
	else if (50 <= channelToSW && channelToSW <= 64) 
		AGC_DIFF_CONFIG(8812A,MB);
	else if (100 <= channelToSW && channelToSW <= 116) 
		AGC_DIFF_CONFIG(8812A,HB);
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
		// [2.4G] LC Tank
		if(IS_VENDOR_8812A_TEST_CHIP(pAdapter))
		{
			if (1 <= channelToSW && channelToSW <= 7) 
				PHY_SetRFReg(pAdapter, eRFPath, RF_TxLCTank_Jaguar, bLSSIWrite_data_Jaguar, 0x0017e); 
			else if (8 <= channelToSW && channelToSW <= 14) 
				PHY_SetRFReg(pAdapter, eRFPath, RF_TxLCTank_Jaguar, bLSSIWrite_data_Jaguar, 0x0013e);             
		}

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
		if (IS_HARDWARE_TYPE_8811AU(pAdapter) && ( !IS_NORMAL_CHIP(pHalData->VersionID)) && channelToSW > 14 ) 
		{
			// <20121116, Kordan> For better result of APK. Asked by AlexWang.
			if (36 <= channelToSW && channelToSW <= 64) 
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x710E7); 
			else if (100 <= channelToSW && channelToSW <= 140) 
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x716E9); 				
			else
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x714E9); 
		}
		else if ((IS_HARDWARE_TYPE_8821E(pAdapter) || IS_HARDWARE_TYPE_8821S(pAdapter)) 
			      && channelToSW > 14) 
		{
			// <20130111, Kordan> For better result of APK. Asked by Willson.
			if (36 <= channelToSW && channelToSW <= 64) 
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x714E9); 
			else if (100 <= channelToSW && channelToSW <= 140) 
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x110E9); 				
			else
				PHY_SetRFReg(pAdapter, eRFPath, RF_APK_Jaguar, bRFRegOffsetMask, 0x714E9); 		
		}
	}
}

VOID
phy_SwChnlAndSetBwMode8812(
	IN  PADAPTER		Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	//DBG_871X("phy_SwChnlAndSetBwMode8812(): bSwChnl %d, bSetChnlBW %d \n", pHalData->bSwChnl, pHalData->bSetChnlBW);

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
		if(pHalData->bChnlBWInitialzed == _FALSE)
		{
			pHalData->bChnlBWInitialzed = _TRUE;
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


