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
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>

#include <hal_intf.h>
#include <hal_com.h>

#ifdef CONFIG_RTL8192C
#include <rtl8192c_hal.h>
#endif
#ifdef CONFIG_RTL8192D
#include <rtl8192d_hal.h>
#endif
#ifdef CONFIG_RTL8723A
#include <rtl8723a_hal.h>
#endif
#ifdef CONFIG_RTL8188E
#include <rtl8188e_hal.h>
#endif

#define _HAL_INIT_C_

#ifdef CONFIG_RF_GAIN_OFFSET
#ifdef CONFIG_RTL8723A
#define	RF_GAIN_OFFSET_ON			BIT0
#define	REG_RF_BB_GAIN_OFFSET	0x7f
#define	RF_GAIN_OFFSET_MASK		0xfffff
#else
#define	RF_GAIN_OFFSET_ON			BIT4
#define	REG_RF_BB_GAIN_OFFSET	0x55
#define	RF_GAIN_OFFSET_MASK		0xfffff
#endif  //CONFIG_RTL8723A
#endif //CONFIG_RF_GAIN_OFFSET

void dump_chip_info(HAL_VERSION	ChipVersion)
{
	int cnt = 0;
	u8 buf[128];

	if(IS_81XXC(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: %s_", IS_92C_SERIAL(ChipVersion)?"CHIP_8192C":"CHIP_8188C");
	}
	else if(IS_92D(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8192D_");
	}
	else if(IS_8723_SERIES(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8723A_");
	}
	else if(IS_8188E(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8188E_");
	}

	cnt += sprintf((buf+cnt), "%s_", IS_NORMAL_CHIP(ChipVersion)?"Normal_Chip":"Test_Chip");
	cnt += sprintf((buf+cnt), "%s_", IS_CHIP_VENDOR_TSMC(ChipVersion)?"TSMC":"UMC");
	if(IS_A_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "A_CUT_");
	else if(IS_B_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "B_CUT_");
	else if(IS_C_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "C_CUT_");
	else if(IS_D_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "D_CUT_");
	else if(IS_E_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "E_CUT_");
	else if(IS_I_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "I_CUT_");
	else if(IS_J_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "J_CUT_");
	else if(IS_K_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "K_CUT_");
	else cnt += sprintf((buf+cnt), "UNKNOWN_CUT(%d)_", ChipVersion.CUTVersion);

	if(IS_1T1R(ChipVersion)) cnt += sprintf((buf+cnt), "1T1R_");
	else if(IS_1T2R(ChipVersion)) cnt += sprintf((buf+cnt), "1T2R_");
	else if(IS_2T2R(ChipVersion)) cnt += sprintf((buf+cnt), "2T2R_");
	else cnt += sprintf((buf+cnt), "UNKNOWN_RFTYPE(%d)_", ChipVersion.RFType);

	cnt += sprintf((buf+cnt), "RomVer(%d)\n", ChipVersion.ROMVer);

	DBG_871X("%s", buf);
}


#define	EEPROM_CHANNEL_PLAN_BY_HW_MASK	0x80

u8	//return the final channel plan decision
hal_com_get_channel_plan(
	IN	PADAPTER	padapter,
	IN	u8			hw_channel_plan,	//channel plan from HW (efuse/eeprom)
	IN	u8			sw_channel_plan,	//channel plan from SW (registry/module param)
	IN	u8			def_channel_plan,	//channel plan used when the former two is invalid
	IN	BOOLEAN		AutoLoadFail
	)
{
	u8 swConfig;
	u8 chnlPlan;

	swConfig = _TRUE;
	if (!AutoLoadFail)
	{
		if (!rtw_is_channel_plan_valid(sw_channel_plan))
			swConfig = _FALSE;
		if (hw_channel_plan & EEPROM_CHANNEL_PLAN_BY_HW_MASK)
			swConfig = _FALSE;
	}

	if (swConfig == _TRUE)
		chnlPlan = sw_channel_plan;
	else
		chnlPlan = hw_channel_plan & (~EEPROM_CHANNEL_PLAN_BY_HW_MASK);

	if (!rtw_is_channel_plan_valid(chnlPlan))
		chnlPlan = def_channel_plan;

	return chnlPlan;
}

u8	MRateToHwRate(u8 rate)
{
	u8	ret = DESC_RATE1M;
		
	switch(rate)
	{
		// CCK and OFDM non-HT rates
	case IEEE80211_CCK_RATE_1MB:	ret = DESC_RATE1M;	break;
	case IEEE80211_CCK_RATE_2MB:	ret = DESC_RATE2M;	break;
	case IEEE80211_CCK_RATE_5MB:	ret = DESC_RATE5_5M;	break;
	case IEEE80211_CCK_RATE_11MB:	ret = DESC_RATE11M;	break;
	case IEEE80211_OFDM_RATE_6MB:	ret = DESC_RATE6M;	break;
	case IEEE80211_OFDM_RATE_9MB:	ret = DESC_RATE9M;	break;
	case IEEE80211_OFDM_RATE_12MB:	ret = DESC_RATE12M;	break;
	case IEEE80211_OFDM_RATE_18MB:	ret = DESC_RATE18M;	break;
	case IEEE80211_OFDM_RATE_24MB:	ret = DESC_RATE24M;	break;
	case IEEE80211_OFDM_RATE_36MB:	ret = DESC_RATE36M;	break;
	case IEEE80211_OFDM_RATE_48MB:	ret = DESC_RATE48M;	break;
	case IEEE80211_OFDM_RATE_54MB:	ret = DESC_RATE54M;	break;

		// HT rates since here
	//case MGN_MCS0:		ret = DESC_RATEMCS0;	break;
	//case MGN_MCS1:		ret = DESC_RATEMCS1;	break;
	//case MGN_MCS2:		ret = DESC_RATEMCS2;	break;
	//case MGN_MCS3:		ret = DESC_RATEMCS3;	break;
	//case MGN_MCS4:		ret = DESC_RATEMCS4;	break;
	//case MGN_MCS5:		ret = DESC_RATEMCS5;	break;
	//case MGN_MCS6:		ret = DESC_RATEMCS6;	break;
	//case MGN_MCS7:		ret = DESC_RATEMCS7;	break;

	default:		break;
	}

	return ret;
}

void	HalSetBrateCfg(
	IN PADAPTER		Adapter,
	IN u8			*mBratesOS,
	OUT u16			*pBrateCfg)
{
	u8	i, is_brate, brate;

	for(i=0;i<NDIS_802_11_LENGTH_RATES_EX;i++)
	{
		is_brate = mBratesOS[i] & IEEE80211_BASIC_RATE_MASK;
		brate = mBratesOS[i] & 0x7f;
		
		if( is_brate )
		{		
			switch(brate)
			{
				case IEEE80211_CCK_RATE_1MB:	*pBrateCfg |= RATE_1M;	break;
				case IEEE80211_CCK_RATE_2MB:	*pBrateCfg |= RATE_2M;	break;
				case IEEE80211_CCK_RATE_5MB:	*pBrateCfg |= RATE_5_5M;break;
				case IEEE80211_CCK_RATE_11MB:	*pBrateCfg |= RATE_11M;	break;
				case IEEE80211_OFDM_RATE_6MB:	*pBrateCfg |= RATE_6M;	break;
				case IEEE80211_OFDM_RATE_9MB:	*pBrateCfg |= RATE_9M;	break;
				case IEEE80211_OFDM_RATE_12MB:	*pBrateCfg |= RATE_12M;	break;
				case IEEE80211_OFDM_RATE_18MB:	*pBrateCfg |= RATE_18M;	break;
				case IEEE80211_OFDM_RATE_24MB:	*pBrateCfg |= RATE_24M;	break;
				case IEEE80211_OFDM_RATE_36MB:	*pBrateCfg |= RATE_36M;	break;
				case IEEE80211_OFDM_RATE_48MB:	*pBrateCfg |= RATE_48M;	break;
				case IEEE80211_OFDM_RATE_54MB:	*pBrateCfg |= RATE_54M;	break;
			}
		}
	}
}

static VOID
_OneOutPipeMapping(
	IN	PADAPTER	pAdapter
	)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(pAdapter);

	pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];//VO
	pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];//VI
	pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[0];//BE
	pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];//BK
	
	pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
	pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
	pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];//HIGH
	pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD
}

static VOID
_TwoOutPipeMapping(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN	 	bWIFICfg
	)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(pAdapter);

	if(bWIFICfg){ //WMM
		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  0, 	1, 	0, 	1, 	0, 	0, 	0, 	0, 		0	};
		//0:H, 1:L 
		
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[1];//VO
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];//VI
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1];//BE
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];//BK
		
		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];//HIGH
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD
		
	}
	else{//typical setting

		
		//BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  1, 	1, 	0, 	0, 	0, 	0, 	0, 	0, 		0	};			
		//0:H, 1:L 
		
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];//VO
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];//VI
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1];//BE
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];//BK
		
		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];//HIGH
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD	
		
	}
	
}

static VOID _ThreeOutPipeMapping(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN	 	bWIFICfg
	)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(pAdapter);

	if(bWIFICfg){//for WMM
		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  1, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};
		//0:H, 1:N, 2:L 
		
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];//VO
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];//VI
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];//BE
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];//BK
		
		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];//HIGH
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD
		
	}
	else{//typical setting

		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  2, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};			
		//0:H, 1:N, 2:L 
		
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];//VO
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];//VI
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];//BE
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[2];//BK
		
		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];//HIGH
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD	
	}

}

BOOLEAN
Hal_MappingOutPipe(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
	)
{
	struct registry_priv *pregistrypriv = &pAdapter->registrypriv;

	BOOLEAN	 bWIFICfg = (pregistrypriv->wifi_spec) ?_TRUE:_FALSE;
	
	BOOLEAN result = _TRUE;

	switch(NumOutPipe)
	{
		case 2:
			_TwoOutPipeMapping(pAdapter, bWIFICfg);
			break;
		case 3:
			_ThreeOutPipeMapping(pAdapter, bWIFICfg);
			break;
		case 1:
			_OneOutPipeMapping(pAdapter);
			break;
		default:
			result = _FALSE;
			break;
	}

	return result;
	
}

void hal_init_macaddr(_adapter *adapter)
{
	rtw_hal_set_hwreg(adapter, HW_VAR_MAC_ADDR, adapter->eeprompriv.mac_addr);
#ifdef  CONFIG_CONCURRENT_MODE
	if (adapter->pbuddy_adapter)
		rtw_hal_set_hwreg(adapter->pbuddy_adapter, HW_VAR_MAC_ADDR, adapter->pbuddy_adapter->eeprompriv.mac_addr);
#endif
}

/* 
* C2H event format:
* Field	 TRIGGER		CONTENT	   CMD_SEQ 	CMD_LEN		 CMD_ID
* BITS	 [127:120]	[119:16]      [15:8]		  [7:4]	 	   [3:0]
*/

void c2h_evt_clear(_adapter *adapter)
{
	rtw_write8(adapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
}

s32 c2h_evt_read(_adapter *adapter, u8 *buf)
{
	s32 ret = _FAIL;
	struct c2h_evt_hdr *c2h_evt;
	int i;
	u8 trigger;

	if (buf == NULL)
		goto exit;

	trigger = rtw_read8(adapter, REG_C2HEVT_CLEAR);

	if (trigger == C2H_EVT_HOST_CLOSE) {
		goto exit; /* Not ready */
	} else if (trigger != C2H_EVT_FW_CLOSE) {
		goto clear_evt; /* Not a valid value */
	}

	c2h_evt = (struct c2h_evt_hdr *)buf;

	_rtw_memset(c2h_evt, 0, 16);

	*buf = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL);
	*(buf+1) = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL + 1);	

	RT_PRINT_DATA(_module_hal_init_c_, _drv_info_, "c2h_evt_read(): ",
		&c2h_evt , sizeof(c2h_evt));

	if (0) {
		DBG_871X("%s id:%u, len:%u, seq:%u, trigger:0x%02x\n", __func__
			, c2h_evt->id, c2h_evt->plen, c2h_evt->seq, trigger);
	}

	/* Read the content */
	for (i = 0; i < c2h_evt->plen; i++)
		c2h_evt->payload[i] = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL + sizeof(*c2h_evt) + i);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_info_, "c2h_evt_read(): Command Content:\n",
		c2h_evt->payload, c2h_evt->plen);

	ret = _SUCCESS;

clear_evt:
	/* 
	* Clear event to notify FW we have read the command.
	* If this field isn't clear, the FW won't update the next command message.
	*/
	c2h_evt_clear(adapter);
exit:
	return ret;
}

void SetHwReg(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);

_func_enter_;

	switch (variable) {
	case HW_VAR_SEC_CFG:
	{
		#if defined(CONFIG_CONCURRENT_MODE) && !defined(DYNAMIC_CAMID_ALLOC)
		// enable tx enc and rx dec engine, and no key search for MC/BC
		rtw_write8(adapter, REG_SECCFG, SCR_NoSKMC|SCR_RxDecEnable|SCR_TxEncEnable);
		#elif defined(DYNAMIC_CAMID_ALLOC)
		u16 reg_scr;

		reg_scr = rtw_read16(adapter, REG_SECCFG);
		rtw_write16(adapter, REG_SECCFG, reg_scr|SCR_CHK_KEYID|SCR_RxDecEnable|SCR_TxEncEnable);
		#else
		rtw_write8(adapter, REG_SECCFG, *((u8*)val));
		#endif
	}
		break;
	case HW_VAR_SEC_DK_CFG:
	{
		struct security_priv *sec = &adapter->securitypriv;
		u8 reg_scr = rtw_read8(adapter, REG_SECCFG);

		if (val) /* Enable default key related setting */
		{
			reg_scr |= SCR_TXBCUSEDK;
			if (sec->dot11AuthAlgrthm != dot11AuthAlgrthm_8021X)
				reg_scr |= (SCR_RxUseDK|SCR_TxUseDK);
		}
		else /* Disable default key related setting */
		{
			reg_scr &= ~(SCR_RXBCUSEDK|SCR_TXBCUSEDK|SCR_RxUseDK|SCR_TxUseDK);
		}

		rtw_write8(adapter, REG_SECCFG, reg_scr);
	}
		break;
	default:
		if (0)
		DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" variable(%d) not defined!\n",
			FUNC_ADPT_ARG(adapter), variable);
		break;
	}

_func_exit_;
}

void GetHwReg(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);

_func_enter_;

	switch (variable) {
	default:
		if (0)
		DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" variable(%d) not defined!\n",
			FUNC_ADPT_ARG(adapter), variable);
		break;
	}
}

u8
SetHalDefVar(_adapter *adapter, HAL_DEF_VARIABLE variable, void *value)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	PDM_ODM_T pDM_Odm = &(pHalData->odmpriv);
	u8 bResult = _SUCCESS;

	switch(variable) {
	case HW_DEF_FA_CNT_DUMP:
		if(*((u8*)value))
			pDM_Odm->DebugComponents |= (ODM_COMP_DIG |ODM_COMP_FA_CNT);
		else
			pDM_Odm->DebugComponents &= ~(ODM_COMP_DIG |ODM_COMP_FA_CNT);
		break;
	case HW_DEF_ODM_DBG_FLAG:
		ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_DBG_COMP, *((u8Byte*)value));
		break;
	case HW_DEF_ODM_DBG_LEVEL:
		ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_DBG_LEVEL, *((u4Byte*)value));
		break;
	default:
		DBG_871X_LEVEL(_drv_always_, "%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n", __FUNCTION__, variable);
		bResult = _FAIL;
		break;
	}

	return bResult;
}

u8
GetHalDefVar(_adapter *adapter, HAL_DEF_VARIABLE variable, void *value)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	PDM_ODM_T pDM_Odm = &(pHalData->odmpriv);
	u8 bResult = _SUCCESS;

	switch(variable) {
	case HW_DEF_ODM_DBG_FLAG:
		*((u8Byte*)value) = pDM_Odm->DebugComponents;
		break;
	case HW_DEF_ODM_DBG_LEVEL:
		*((u4Byte*)value) = pDM_Odm->DebugLevel;
		break;
	case HAL_DEF_DBG_DM_FUNC:
		*((u32*)value) = pHalData->odmpriv.SupportAbility;
		break;
	case HAL_DEF_MACID_SLEEP:
		*(u8*)value = _FALSE; // support macid sleep
		break;
	default:
		DBG_871X_LEVEL(_drv_always_, "%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n", __FUNCTION__, variable);
		bResult = _FAIL;
		break;
	}

	return bResult;
}

#ifdef CONFIG_RF_GAIN_OFFSET
void rtw_bb_rf_gain_offset(_adapter *padapter)
{
	u8      value = padapter->eeprompriv.EEPROMRFGainOffset;
	u8      tmp = 0x3e;
	u32     res;

	DBG_871X("+%s value: 0x%02x+\n", __func__, value);

	if (value & RF_GAIN_OFFSET_ON) {
		//DBG_871X("Offset RF Gain.\n");
		//DBG_871X("Offset RF Gain.  padapter->eeprompriv.EEPROMRFGainVal=0x%x\n",padapter->eeprompriv.EEPROMRFGainVal);
		if(padapter->eeprompriv.EEPROMRFGainVal != 0xff){
#ifdef CONFIG_RTL8723A
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0xd, 0xffffffff);
			//DBG_871X("Offset RF Gain. reg 0xd=0x%x\n",res);
			res &= 0xfff87fff;

			res |= (padapter->eeprompriv.EEPROMRFGainVal & 0x0f)<< 15;
			//DBG_871X("Offset RF Gain.    reg 0xd=0x%x\n",res);

			rtw_hal_write_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, RF_GAIN_OFFSET_MASK, res);

			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0xe, 0xffffffff);
			DBG_871X("Offset RF Gain. reg 0xe=0x%x\n",res);
			res &= 0xfffffff0;

			res |= (padapter->eeprompriv.EEPROMRFGainVal & 0x0f);
			//DBG_871X("Offset RF Gain.    reg 0xe=0x%x\n",res);

			rtw_hal_write_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, RF_GAIN_OFFSET_MASK, res);
#else
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, 0xffffffff);
			DBG_871X("REG_RF_BB_GAIN_OFFSET=%x \n",res);
			res &= 0xfff87fff;
			res |= (padapter->eeprompriv.EEPROMRFGainVal & 0x0f)<< 15;
			DBG_871X("write REG_RF_BB_GAIN_OFFSET=%x \n",res);
			rtw_hal_write_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, RF_GAIN_OFFSET_MASK, res);
#endif
		}
		else
		{
			//DBG_871X("Offset RF Gain.  padapter->eeprompriv.EEPROMRFGainVal=0x%x  != 0xff, didn't run Kfree\n",padapter->eeprompriv.EEPROMRFGainVal);
		}
	} else {
		//DBG_871X("Using the default RF gain.\n");
	}

}
#endif //CONFIG_RF_GAIN_OFFSET

#ifdef CONFIG_EFUSE_CONFIG_FILE
int check_phy_efuse_tx_power_info_valid(PADAPTER padapter) {
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8* pContent = pEEPROM->efuse_eeprom_data;
	int index = 0;
	u16 tx_index_offset = 0x0000;

	switch(padapter->chip_type) {
		case RTL8723A:
			tx_index_offset = EEPROM_CCK_TX_PWR_INX_8723A;
		break;
		default:
			tx_index_offset = 0x0010;
		break;
	}
	for (index = 0 ; index < 12 ; index++) {
		if (pContent[tx_index_offset + index] == 0xFF) {
			return _FALSE;
		} 
	}
	DBG_871X("phy efuse with valid tx power info\n");
	return _TRUE;
}

int check_phy_efuse_macaddr_info_valid(PADAPTER padapter) {

	u8 val = 0;
	u16 addr_offset = 0x0000;

	switch(padapter->chip_type) {
		case RTL8723A:
			if (padapter->interface_type == RTW_USB) 
			{
				addr_offset = EEPROM_MAC_ADDR_8723AU;
				DBG_871X("%s: interface is USB\n", __func__);
			}
			else if (padapter->interface_type == RTW_SDIO)
			{
				addr_offset = EEPROM_MAC_ADDR_8723AS;
				DBG_871X("%s: interface is SDIO\n", __func__);
			}
			else if (padapter->interface_type == RTW_PCIE)
			{
				addr_offset = EEPROM_MAC_ADDR_8723AE;
				DBG_871X("%s: interface is PCIE\n", __func__);
			}
		break;
	}

	if (addr_offset == 0x0000) {
		DBG_871X("phy efuse MAC addr offset is 0!!\n");
		return _FALSE;
	} else {
		rtw_efuse_map_read(padapter, addr_offset, 1, &val);
	}

	if (val == 0xFF) {
		return _FALSE;
	} else {
		DBG_871X("phy efuse with valid MAC addr\n");
		return _TRUE;
	}
}

u32 Hal_readPGDataFromConfigFile(
	PADAPTER	padapter,
	struct file *fp)
{
	u32 i;
	mm_segment_t fs;
	u8 temp[3];
	loff_t pos = 0;
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8	*PROMContent = pEEPROM->efuse_eeprom_data;

	temp[2] = 0; // add end of string '\0'

	fs = get_fs();
	set_fs(KERNEL_DS);

	for (i = 0 ; i < HWSET_MAX_SIZE ; i++) {
		vfs_read(fp, temp, 2, &pos);
		PROMContent[i] = simple_strtoul(temp, NULL, 16);
		if ((i % EFUSE_FILE_COLUMN_NUM) == (EFUSE_FILE_COLUMN_NUM - 1)) {
			//Filter the lates space char.
			vfs_read(fp, temp, 1, &pos);
			if (strchr(temp, ' ') == NULL) {
				pos--;
				vfs_read(fp, temp, 2, &pos);
			}
		} else {
			pos += 1; // Filter the space character
		}
	}

	set_fs(fs);
	pEEPROM->bloadfile_fail_flag = _FALSE;

#ifdef CONFIG_DEBUG
	DBG_871X("Efuse configure file:\n");
	for (i=0; i<HWSET_MAX_SIZE; i++)
	{
		if (i % 16 == 0)
			printk("\n");

		printk("%02X ", PROMContent[i]);
	}
	printk("\n");
#endif

	return _SUCCESS;
}

void Hal_ReadMACAddrFromFile(
	PADAPTER		padapter,
	struct file *fp)
{
	u32 i;
	mm_segment_t fs;
	u8 source_addr[18];
	loff_t pos = 0;
	u32	curtime = rtw_get_current_time();
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8 *head, *end;

	_rtw_memset(source_addr, 0, 18);
	_rtw_memset(pEEPROM->mac_addr, 0, ETH_ALEN);

	fs = get_fs();
	set_fs(KERNEL_DS);

	DBG_871X("wifi mac address:\n");
	vfs_read(fp, source_addr, 18, &pos);
	source_addr[17] = ':';

	head = end = source_addr;
	for (i=0; i<ETH_ALEN; i++) {
		while (end && (*end != ':') )
			end++;

		if (end && (*end == ':') )
			*end = '\0';

		pEEPROM->mac_addr[i] = simple_strtoul(head, NULL, 16 );

		if (end) {
			end++;
			head = end;
		}
	}

	set_fs(fs);
	pEEPROM->bloadmac_fail_flag = _FALSE;

	if (rtw_check_invalid_mac_address(pEEPROM->mac_addr) == _TRUE) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
		get_random_bytes(pEEPROM->mac_addr, ETH_ALEN);
		pEEPROM->mac_addr[0] = 0x00;
		pEEPROM->mac_addr[1] = 0xe0;
		pEEPROM->mac_addr[2] = 0x4c;
#else
		pEEPROM->mac_addr[0] = 0x00;
		pEEPROM->mac_addr[1] = 0xe0;
		pEEPROM->mac_addr[2] = 0x4c;
		pEEPROM->mac_addr[3] = (u8)(curtime & 0xff) ;
		pEEPROM->mac_addr[4] = (u8)((curtime>>8) & 0xff) ;
		pEEPROM->mac_addr[5] = (u8)((curtime>>16) & 0xff) ;
#endif
                DBG_871X("MAC Address from wifimac error is invalid, assign random MAC !!!\n");
	}

	DBG_871X("%s: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
			__func__, pEEPROM->mac_addr[0], pEEPROM->mac_addr[1],
			pEEPROM->mac_addr[2], pEEPROM->mac_addr[3],
			pEEPROM->mac_addr[4], pEEPROM->mac_addr[5]);
}

void Hal_GetPhyEfuseMACAddr(PADAPTER padapter, u8* mac_addr) {
	int i = 0;
	u16 addr_offset = 0x0000;

	switch(padapter->chip_type) {
		case RTL8723A:
			if (padapter->interface_type == RTW_USB) 
			{
				addr_offset = EEPROM_MAC_ADDR_8723AU;
				DBG_871X("%s: interface is USB\n", __func__);
			}
			else if (padapter->interface_type == RTW_SDIO)
			{
				addr_offset = EEPROM_MAC_ADDR_8723AS;
				DBG_871X("%s: interface is SDIO\n", __func__);
			}
			else if (padapter->interface_type == RTW_PCIE)
			{
				addr_offset = EEPROM_MAC_ADDR_8723AE;
				DBG_871X("%s: interface is PCIE\n", __func__);
			}
		break;
	}

	rtw_efuse_map_read(padapter, addr_offset, ETH_ALEN, mac_addr);

	if (rtw_check_invalid_mac_address(mac_addr) == _TRUE) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
		get_random_bytes(mac_addr, ETH_ALEN);
		mac_addr[0] = 0x00;
		mac_addr[1] = 0xe0;
		mac_addr[2] = 0x4c;
#else
		mac_addr[0] = 0x00;
		mac_addr[1] = 0xe0;
		mac_addr[2] = 0x4c;
		mac_addr[3] = (u8)(curtime & 0xff) ;
		mac_addr[4] = (u8)((curtime>>8) & 0xff) ;
		mac_addr[5] = (u8)((curtime>>16) & 0xff) ;
#endif
                DBG_871X("MAC Address from phy efuse error, assign random MAC !!!\n");
	}

	DBG_871X("%s: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
			__func__, mac_addr[0], mac_addr[1], mac_addr[2],
			mac_addr[3], mac_addr[4], mac_addr[5]);
}
#endif //CONFIG_EFUSE_CONFIG_FILE


