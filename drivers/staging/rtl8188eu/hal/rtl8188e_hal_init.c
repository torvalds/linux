// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _HAL_INIT_C_

#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include <drv_types.h>
#include <rtw_efuse.h>
#include <phy.h>
#include <rtl8188e_hal.h>

#include <rtw_iol.h>

void iol_mode_enable(struct adapter *padapter, u8 enable)
{
	u8 reg_0xf0 = 0;

	if (enable) {
		/* Enable initial offload */
		reg_0xf0 = usb_read8(padapter, REG_SYS_CFG);
		usb_write8(padapter, REG_SYS_CFG, reg_0xf0 | SW_OFFLOAD_EN);

		if (!padapter->bFWReady)
			_8051Reset88E(padapter);
	} else {
		/* disable initial offload */
		reg_0xf0 = usb_read8(padapter, REG_SYS_CFG);
		usb_write8(padapter, REG_SYS_CFG, reg_0xf0 & ~SW_OFFLOAD_EN);
	}
}

s32 iol_execute(struct adapter *padapter, u8 control)
{
	s32 status = _FAIL;
	u8 reg_0x88 = 0;
	unsigned long start = 0;

	control = control & 0x0f;
	reg_0x88 = usb_read8(padapter, REG_HMEBOX_E0);
	usb_write8(padapter, REG_HMEBOX_E0,  reg_0x88 | control);

	start = jiffies;
	while ((reg_0x88 = usb_read8(padapter, REG_HMEBOX_E0)) & control &&
	       jiffies_to_msecs(jiffies - start) < 1000) {
		udelay(5);
	}

	reg_0x88 = usb_read8(padapter, REG_HMEBOX_E0);
	status = (reg_0x88 & control) ? _FAIL : _SUCCESS;
	if (reg_0x88 & control << 4)
		status = _FAIL;
	return status;
}

static s32 iol_InitLLTTable(struct adapter *padapter, u8 txpktbuf_bndy)
{
	s32 rst = _SUCCESS;

	iol_mode_enable(padapter, 1);
	usb_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);
	rst = iol_execute(padapter, CMD_INIT_LLT);
	iol_mode_enable(padapter, 0);
	return rst;
}

s32 rtl8188e_iol_efuse_patch(struct adapter *padapter)
{
	s32	result = _SUCCESS;

	if (rtw_iol_applied(padapter)) {
		iol_mode_enable(padapter, 1);
		result = iol_execute(padapter, CMD_READ_EFUSE_MAP);
		if (result == _SUCCESS)
			result = iol_execute(padapter, CMD_EFUSE_PATCH);

		iol_mode_enable(padapter, 0);
	}
	return result;
}

#define MAX_REG_BOLCK_SIZE	196

void _8051Reset88E(struct adapter *padapter)
{
	u8 u1bTmp;

	u1bTmp = usb_read8(padapter, REG_SYS_FUNC_EN + 1);
	usb_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp & (~BIT(2)));
	usb_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp | (BIT(2)));
}

void rtl8188e_InitializeFirmwareVars(struct adapter *padapter)
{
	/*  Init Fw LPS related. */
	padapter->pwrctrlpriv.bFwCurrentInPSMode = false;

	/*  Init H2C counter. by tynli. 2009.12.09. */
	padapter->HalData->LastHMEBoxNum = 0;
}

void rtw_hal_free_data(struct adapter *padapter)
{
	kfree(padapter->HalData);
	padapter->HalData = NULL;
}

void rtw_hal_read_chip_version(struct adapter *padapter)
{
	u32				value32;
	struct HAL_VERSION		ChipVersion;
	struct hal_data_8188e *pHalData = padapter->HalData;

	value32 = usb_read32(padapter, REG_SYS_CFG);
	ChipVersion.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);
	ChipVersion.VendorType = ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : CHIP_VENDOR_TSMC);
	ChipVersion.CUTVersion = (value32 & CHIP_VER_RTL_MASK) >> CHIP_VER_RTL_SHIFT; /*  IC version (CUT) */

	dump_chip_info(ChipVersion);

	pHalData->VersionID = ChipVersion;
}

void rtw_hal_set_odm_var(struct adapter *Adapter, enum hal_odm_variable eVariable, void *pValue1, bool bSet)
{
	struct odm_dm_struct *podmpriv = &Adapter->HalData->odmpriv;

	switch (eVariable) {
	case HAL_ODM_STA_INFO:
		{
			struct sta_info *psta = pValue1;

			if (bSet) {
				ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->mac_id, psta);
				ODM_RAInfo_Init(podmpriv, psta->mac_id);
			} else {
				ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->mac_id, NULL);
			}
		}
		break;
	case HAL_ODM_P2P_STATE:
		podmpriv->bWIFI_Direct = bSet;
		break;
	case HAL_ODM_WIFI_DISPLAY_STATE:
		podmpriv->bWIFI_Display = bSet;
		break;
	default:
		break;
	}
}

void rtw_hal_notch_filter(struct adapter *adapter, bool enable)
{
	if (enable)
		usb_write8(adapter, rOFDM0_RxDSP + 1, usb_read8(adapter, rOFDM0_RxDSP + 1) | BIT(1));
	else
		usb_write8(adapter, rOFDM0_RxDSP + 1, usb_read8(adapter, rOFDM0_RxDSP + 1) & ~BIT(1));
}

/*  */
/*  */
/*  LLT R/W/Init function */
/*  */
/*  */
static s32 _LLTWrite(struct adapter *padapter, u32 address, u32 data)
{
	s32	status = _SUCCESS;
	s32	count = 0;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);
	u16	LLTReg = REG_LLT_INIT;

	usb_write32(padapter, LLTReg, value);

	/* polling */
	do {
		value = usb_read32(padapter, LLTReg);
		if (_LLT_OP_VALUE(value) == _LLT_NO_ACTIVE)
			break;

		if (count > POLLING_LLT_THRESHOLD) {
			status = _FAIL;
			break;
		}
		udelay(5);
	} while (count++);

	return status;
}

s32 InitLLTTable(struct adapter *padapter, u8 txpktbuf_bndy)
{
	s32	status = _FAIL;
	u32	i;
	u32	Last_Entry_Of_TxPktBuf = LAST_ENTRY_OF_TX_PKT_BUFFER;/*  176, 22k */

	if (rtw_iol_applied(padapter)) {
		status = iol_InitLLTTable(padapter, txpktbuf_bndy);
	} else {
		for (i = 0; i < (txpktbuf_bndy - 1); i++) {
			status = _LLTWrite(padapter, i, i + 1);
			if (status != _SUCCESS)
				return status;
		}

		/*  end of list */
		status = _LLTWrite(padapter, (txpktbuf_bndy - 1), 0xFF);
		if (status != _SUCCESS)
			return status;

		/*  Make the other pages as ring buffer */
		/*  This ring buffer is used as beacon buffer if we config this MAC as two MAC transfer. */
		/*  Otherwise used as local loopback buffer. */
		for (i = txpktbuf_bndy; i < Last_Entry_Of_TxPktBuf; i++) {
			status = _LLTWrite(padapter, i, (i + 1));
			if (status != _SUCCESS)
				return status;
		}

		/*  Let last entry point to the start entry of ring buffer */
		status = _LLTWrite(padapter, Last_Entry_Of_TxPktBuf, txpktbuf_bndy);
		if (status != _SUCCESS)
			return status;
	}

	return status;
}

void Hal_InitPGData88E(struct adapter *padapter)
{
	if (!is_boot_from_eeprom(padapter))
		EFUSE_ShadowMapUpdate(padapter);
}

void Hal_EfuseParseIDCode88E(struct adapter *padapter, u8 *hwinfo)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u16			EEPROMId;

	/*  Checl 0x8129 again for making sure autoload status!! */
	EEPROMId = le16_to_cpu(*((__le16 *)hwinfo));
	if (EEPROMId != RTL_EEPROM_ID)
		pEEPROM->bautoload_fail_flag = true;
	else
		pEEPROM->bautoload_fail_flag = false;
}

static void Hal_ReadPowerValueFromPROM_8188E(struct txpowerinfo24g *pwrInfo24G, u8 *PROMContent, bool AutoLoadFail)
{
	u32 rfPath, eeAddr = EEPROM_TX_PWR_INX_88E, group, TxCount = 0;

	memset(pwrInfo24G, 0, sizeof(struct txpowerinfo24g));

	if (AutoLoadFail) {
		for (rfPath = 0; rfPath < MAX_RF_PATH; rfPath++) {
			/* 2.4G default value */
			for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
				pwrInfo24G->IndexCCK_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
				pwrInfo24G->IndexBW40_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
			}
			for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
				if (TxCount == 0) {
					pwrInfo24G->BW20_Diff[rfPath][0] = EEPROM_DEFAULT_24G_HT20_DIFF;
					pwrInfo24G->OFDM_Diff[rfPath][0] = EEPROM_DEFAULT_24G_OFDM_DIFF;
				} else {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
					pwrInfo24G->BW40_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
				}
			}
		}
		return;
	}

	for (rfPath = 0; rfPath < MAX_RF_PATH; rfPath++) {
		/* 2.4G default value */
		for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
			pwrInfo24G->IndexCCK_Base[rfPath][group] =	PROMContent[eeAddr++];
			if (pwrInfo24G->IndexCCK_Base[rfPath][group] == 0xFF)
				pwrInfo24G->IndexCCK_Base[rfPath][group] = EEPROM_DEFAULT_24G_INDEX;
		}
		for (group = 0; group < MAX_CHNL_GROUP_24G - 1; group++) {
			pwrInfo24G->IndexBW40_Base[rfPath][group] =	PROMContent[eeAddr++];
			if (pwrInfo24G->IndexBW40_Base[rfPath][group] == 0xFF)
				pwrInfo24G->IndexBW40_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
		}
		for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
			if (TxCount == 0) {
				pwrInfo24G->BW40_Diff[rfPath][TxCount] = 0;
				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] = EEPROM_DEFAULT_24G_HT20_DIFF;
				} else {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] = (PROMContent[eeAddr] & 0xf0) >> 4;
					if (pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_24G_OFDM_DIFF;
				} else {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr] & 0x0f);
					if (pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}
				pwrInfo24G->CCK_Diff[rfPath][TxCount] = 0;
				eeAddr++;
			} else {
				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	(PROMContent[eeAddr] & 0xf0) >> 4;
					if (pwrInfo24G->BW40_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW40_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	(PROMContent[eeAddr] & 0x0f);
					if (pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr] & 0xf0) >> 4;
					if (pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	(PROMContent[eeAddr] & 0x0f);
					if (pwrInfo24G->CCK_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->CCK_Diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;
			}
		}
	}
}

void Hal_GetChnlGroup88E(u8 chnl, u8 *group)
{
	if (chnl < 3)			/*  Channel 1-2 */
		*group = 0;
	else if (chnl < 6)		/*  Channel 3-5 */
		*group = 1;
	else if (chnl < 9)		/*  Channel 6-8 */
		*group = 2;
	else if (chnl < 12)		/*  Channel 9-11 */
		*group = 3;
	else if (chnl < 14)		/*  Channel 12-13 */
		*group = 4;
	else if (chnl == 14)		/*  Channel 14 */
		*group = 5;
}

void Hal_ReadPowerSavingMode88E(struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail)
{
	if (AutoLoadFail) {
		padapter->pwrctrlpriv.bHWPowerdown = false;
		padapter->pwrctrlpriv.bSupportRemoteWakeup = false;
	} else {
		/* hw power down mode selection , 0:rf-off / 1:power down */

		if (padapter->registrypriv.hwpdn_mode == 2)
			padapter->pwrctrlpriv.bHWPowerdown = (hwinfo[EEPROM_RF_FEATURE_OPTION_88E] & BIT(4));
		else
			padapter->pwrctrlpriv.bHWPowerdown = padapter->registrypriv.hwpdn_mode;

		/*  decide hw if support remote wakeup function */
		/*  if hw supported, 8051 (SIE) will generate WeakUP signal(D+/D- toggle) when autoresume */
		padapter->pwrctrlpriv.bSupportRemoteWakeup = (hwinfo[EEPROM_USB_OPTIONAL_FUNCTION0] & BIT(1)) ? true : false;
	}
}

void Hal_ReadTxPowerInfo88E(struct adapter *padapter, u8 *PROMContent, bool AutoLoadFail)
{
	struct hal_data_8188e *pHalData = padapter->HalData;
	struct txpowerinfo24g pwrInfo24G;
	u8 ch, group;
	u8 TxCount;

	Hal_ReadPowerValueFromPROM_8188E(&pwrInfo24G, PROMContent, AutoLoadFail);

	if (!AutoLoadFail)
		pHalData->bTXPowerDataReadFromEEPORM = true;

	for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
		Hal_GetChnlGroup88E(ch, &group);
		pHalData->Index24G_CCK_Base[0][ch] = pwrInfo24G.IndexCCK_Base[0][group];
		if (ch == 14)
			pHalData->Index24G_BW40_Base[0][ch] = pwrInfo24G.IndexBW40_Base[0][4];
		else
			pHalData->Index24G_BW40_Base[0][ch] = pwrInfo24G.IndexBW40_Base[0][group];
	}
	for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
		pHalData->CCK_24G_Diff[0][TxCount] = pwrInfo24G.CCK_Diff[0][TxCount];
		pHalData->OFDM_24G_Diff[0][TxCount] = pwrInfo24G.OFDM_Diff[0][TxCount];
		pHalData->BW20_24G_Diff[0][TxCount] = pwrInfo24G.BW20_Diff[0][TxCount];
		pHalData->BW40_24G_Diff[0][TxCount] = pwrInfo24G.BW40_Diff[0][TxCount];
	}

	/*  2010/10/19 MH Add Regulator recognize for CU. */
	if (!AutoLoadFail) {
		pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_88E] & 0x7);	/* bit0~2 */
		if (PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
			pHalData->EEPROMRegulatory = (EEPROM_DEFAULT_BOARD_OPTION & 0x7);	/* bit0~2 */
	} else {
		pHalData->EEPROMRegulatory = 0;
	}
}

void Hal_EfuseParseXtal_8188E(struct adapter *pAdapter, u8 *hwinfo, bool AutoLoadFail)
{
	struct hal_data_8188e *pHalData = pAdapter->HalData;

	if (!AutoLoadFail) {
		pHalData->CrystalCap = hwinfo[EEPROM_XTAL_88E];
		if (pHalData->CrystalCap == 0xFF)
			pHalData->CrystalCap = EEPROM_Default_CrystalCap_88E;
	} else {
		pHalData->CrystalCap = EEPROM_Default_CrystalCap_88E;
	}
}

void Hal_EfuseParseBoardType88E(struct adapter *pAdapter, u8 *hwinfo, bool AutoLoadFail)
{
	struct hal_data_8188e *pHalData = pAdapter->HalData;

	if (!AutoLoadFail)
		pHalData->BoardType = (hwinfo[EEPROM_RF_BOARD_OPTION_88E]
					& 0xE0) >> 5;
	else
		pHalData->BoardType = 0;
}

void Hal_EfuseParseEEPROMVer88E(struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail)
{
	struct hal_data_8188e *pHalData = padapter->HalData;

	if (!AutoLoadFail) {
		pHalData->EEPROMVersion = hwinfo[EEPROM_VERSION_88E];
		if (pHalData->EEPROMVersion == 0xFF)
			pHalData->EEPROMVersion = EEPROM_Default_Version;
	} else {
		pHalData->EEPROMVersion = 1;
	}
}

void rtl8188e_EfuseParseChnlPlan(struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail)
{
	padapter->mlmepriv.ChannelPlan =
		 hal_com_get_channel_plan(hwinfo ? hwinfo[EEPROM_ChannelPlan_88E] : 0xFF,
					  padapter->registrypriv.channel_plan,
					  RT_CHANNEL_DOMAIN_WORLD_WIDE_13, AutoLoadFail);

}

void Hal_EfuseParseCustomerID88E(struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail)
{
	struct hal_data_8188e	*pHalData = padapter->HalData;

	if (!AutoLoadFail) {
		pHalData->EEPROMCustomerID = hwinfo[EEPROM_CUSTOMERID_88E];
	} else {
		pHalData->EEPROMCustomerID = 0;
		pHalData->EEPROMSubCustomerID = 0;
	}
}

void Hal_ReadAntennaDiversity88E(struct adapter *pAdapter, u8 *PROMContent, bool AutoLoadFail)
{
	struct hal_data_8188e *pHalData = pAdapter->HalData;
	struct registry_priv	*registry_par = &pAdapter->registrypriv;

	if (!AutoLoadFail) {
		/*  Antenna Diversity setting. */
		if (registry_par->antdiv_cfg == 2) { /*  2:By EFUSE */
			pHalData->AntDivCfg = (PROMContent[EEPROM_RF_BOARD_OPTION_88E] & 0x18) >> 3;
			if (PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
				pHalData->AntDivCfg = (EEPROM_DEFAULT_BOARD_OPTION & 0x18) >> 3;
		} else {
			pHalData->AntDivCfg = registry_par->antdiv_cfg;  /*  0:OFF , 1:ON, 2:By EFUSE */
		}

		if (registry_par->antdiv_type == 0) {
			/* If TRxAntDivType is AUTO in advanced setting, use EFUSE value instead. */
			pHalData->TRxAntDivType = PROMContent[EEPROM_RF_ANTENNA_OPT_88E];
			if (pHalData->TRxAntDivType == 0xFF)
				pHalData->TRxAntDivType = CG_TRX_HW_ANTDIV; /*  For 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port) */
		} else {
			pHalData->TRxAntDivType = registry_par->antdiv_type;
		}

		if (pHalData->TRxAntDivType == CG_TRX_HW_ANTDIV || pHalData->TRxAntDivType == CGCS_RX_HW_ANTDIV)
			pHalData->AntDivCfg = 1; /*  0xC1[3] is ignored. */
	} else {
		pHalData->AntDivCfg = 0;
	}
}

void Hal_ReadThermalMeter_88E(struct adapter *Adapter, u8 *PROMContent, bool AutoloadFail)
{
	struct hal_data_8188e *pHalData = Adapter->HalData;

	/*  ThermalMeter from EEPROM */
	if (!AutoloadFail)
		pHalData->EEPROMThermalMeter = PROMContent[EEPROM_THERMAL_METER_88E];
	else
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_88E;

	if (pHalData->EEPROMThermalMeter == 0xff || AutoloadFail) {
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_88E;
	}
}
