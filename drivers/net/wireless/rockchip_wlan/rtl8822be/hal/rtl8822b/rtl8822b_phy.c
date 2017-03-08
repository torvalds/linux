/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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
#define _RTL8822B_PHY_C_

#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../hal_halmac.h"	/* rtw_halmac_phy_power_switch() */
#include "rtl8822b.h"


/*
 * Description:
 *	Initialize Register definition offset for Radio Path A/B/C/D
 *	The initialization value is constant and it should never be changes
 */
static void bb_rf_register_definition(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	/* RF Interface Sowrtware Control */
	hal->PHYRegDef[ODM_RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW;
	hal->PHYRegDef[ODM_RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW;

	/* RF Interface Output (and Enable) */
	hal->PHYRegDef[ODM_RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE;
	hal->PHYRegDef[ODM_RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE;

	/* RF Interface (Output and) Enable */
	hal->PHYRegDef[ODM_RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE;
	hal->PHYRegDef[ODM_RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE;

	hal->PHYRegDef[ODM_RF_PATH_A].rf3wireOffset = rA_LSSIWrite_Jaguar;
	hal->PHYRegDef[ODM_RF_PATH_B].rf3wireOffset = rB_LSSIWrite_Jaguar;

	hal->PHYRegDef[ODM_RF_PATH_A].rfHSSIPara2 = rHSSIRead_Jaguar;
	hal->PHYRegDef[ODM_RF_PATH_B].rfHSSIPara2 = rHSSIRead_Jaguar;

	/* Tranceiver Readback LSSI/HSPI mode */
	hal->PHYRegDef[ODM_RF_PATH_A].rfLSSIReadBack = rA_SIRead_Jaguar;
	hal->PHYRegDef[ODM_RF_PATH_B].rfLSSIReadBack = rB_SIRead_Jaguar;
	hal->PHYRegDef[ODM_RF_PATH_A].rfLSSIReadBackPi = rA_PIRead_Jaguar;
	hal->PHYRegDef[ODM_RF_PATH_B].rfLSSIReadBackPi = rB_PIRead_Jaguar;
}

/*
 * Description:
 *	Initialize MAC registers
 *
 * Return:
 *	_TRUE	Success
 *	_FALSE	Fail
 */
u8 rtl8822b_phy_init_mac_register(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u8 regfile[] = RTL8822B_PHY_MACREG;
	u8 ret = _TRUE;
	int res;
	HAL_STATUS status;


	hal = GET_HAL_DATA(adapter);

	ret = _FALSE;
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	res = phy_ConfigMACWithParaFile(adapter, regfile);
	if (_SUCCESS == res)
		ret = _TRUE;
#endif /* CONFIG_LOAD_PHY_PARA_FROM_FILE */
	if (_FALSE == ret) {
		status = ODM_ConfigMACWithHeaderFile(&hal->odmpriv);
		if (HAL_STATUS_SUCCESS == status)
			ret = _TRUE;
	}
	if (_FALSE == ret)
		RTW_INFO("%s: Write MAC Reg Fail!!", __FUNCTION__);

	return ret;
}

static u8 _init_bb_reg(PADAPTER Adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(Adapter);
	u8 regfile[] = RTL8822B_PHY_REG;
	u8 agcfile[] = RTL8822B_AGC_TAB;
	u8 regfile_mp[] = RTL8822B_PHY_REG_MP;
	u8 ret = _TRUE;
	int res;
	HAL_STATUS status;


	/*
	 * 1. Read PHY_REG.TXT BB INIT!!
	 */
	ret = _FALSE;
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	res = phy_ConfigBBWithParaFile(Adapter, regfile, CONFIG_BB_PHY_REG);
	if (_SUCCESS == res)
		ret = _TRUE;
#endif
	if (_FALSE == ret) {
		status = ODM_ConfigBBWithHeaderFile(&hal->odmpriv, CONFIG_BB_PHY_REG);
		if (HAL_STATUS_SUCCESS == status)
			ret = _TRUE;
	}
	if (_FALSE == ret) {
		RTW_INFO("%s: Write BB Reg Fail!!", __FUNCTION__);
		goto exit;
	}

#if 0 /* No parameter with MP using currently by BB@Stanley. */
/*#ifdef CONFIG_MP_INCLUDED*/
	if (Adapter->registrypriv.mp_mode == 1) {
		/*
		 * 1.1 Read PHY_REG_MP.TXT BB INIT!!
		 */
		ret = _FALSE;
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
		res = phy_ConfigBBWithMpParaFile(Adapter, regfile_mp);
		if (_SUCCESS == res)
			ret = _TRUE;
#endif
		if (_FALSE == ret) {
			status = ODM_ConfigBBWithHeaderFile(&hal->odmpriv, CONFIG_BB_PHY_REG_MP);
			if (HAL_STATUS_SUCCESS == status)
				ret = _TRUE;
		}
		if (_FALSE == ret) {
			RTW_INFO("%s: Write BB Reg MP Fail!!", __FUNCTION__);
			goto exit;
		}
	}
#endif /* CONFIG_MP_INCLUDED */

	/*
	 * 2. Read BB AGC table Initialization
	 */
	ret = _FALSE;
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	res = phy_ConfigBBWithParaFile(Adapter, agcfile, CONFIG_BB_AGC_TAB);
	if (_SUCCESS == res)
		ret = _TRUE;
#endif
	if (_FALSE == ret) {
		status = ODM_ConfigBBWithHeaderFile(&hal->odmpriv, CONFIG_BB_AGC_TAB);
		if (HAL_STATUS_SUCCESS == status)
			ret = _TRUE;
	}
	if (_FALSE == ret) {
		RTW_INFO("%s: Write AGC Table Fail!\n", __FUNCTION__);
		goto exit;
	}

exit:
	return ret;
}

static u8 init_bb_reg(PADAPTER adapter)
{
	u8 ret = _TRUE;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	/*
	 * Config BB and AGC
	 */
	ret = _init_bb_reg(adapter);

	hal_set_crystal_cap(adapter, hal->CrystalCap);

	return ret;
}

static u8 _init_rf_reg(PADAPTER adapter)
{
	u8 path, phydm_path;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 regfile_a[] = RTL8822B_PHY_RADIO_A;
	u8 regfile_b[] = RTL8822B_PHY_RADIO_B;
	u8 *regfile;
	u8 txpwrtrackfile[] = RTL8822B_TXPWR_TRACK;
	HAL_STATUS status;
	int res;
	u8 ret = _TRUE;


	/*
	 * Initialize RF
	 */
	for (path = 0; path < hal->NumTotalRFPath; path++) {
		/* Initialize RF from configuration file */
		switch (path) {
		case RF_PATH_A:
			phydm_path = ODM_RF_PATH_A;
			regfile = regfile_a;
			break;

		case RF_PATH_B:
			phydm_path = ODM_RF_PATH_B;
			regfile = regfile_b;
			break;

		default:
			RTW_INFO("%s: [WARN] Unknown path=%d, skip!\n", __FUNCTION__, path);
			continue;
		}

		ret = _FALSE;
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
		res = PHY_ConfigRFWithParaFile(adapter, regfile, phydm_path);
		if (_SUCCESS == res)
			ret = _TRUE;
#endif
		if (_FALSE == ret) {
			status = ODM_ConfigRFWithHeaderFile(&hal->odmpriv, CONFIG_RF_RADIO, (ODM_RF_RADIO_PATH_E)phydm_path);
			if (HAL_STATUS_SUCCESS != status)
				goto exit;
			ret = _TRUE;
		}
	}

	/*
	 * Configuration of Tx Power Tracking
	 */
	ret = _FALSE;
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	res = PHY_ConfigRFWithTxPwrTrackParaFile(adapter, txpwrtrackfile);
	if (_SUCCESS == res)
		ret = _TRUE;
#endif
	if (_FALSE == ret) {
		status = ODM_ConfigRFWithTxPwrTrackHeaderFile(&hal->odmpriv);
		if (HAL_STATUS_SUCCESS != status) {
			RTW_INFO("%s: Write PwrTrack Table Fail!\n", __FUNCTION__);
			goto exit;
		}
		ret = _TRUE;
	}

exit:
	return ret;
}

static u8 init_rf_reg(PADAPTER adapter)
{
	u8 ret = _TRUE;


	ret = _init_rf_reg(adapter);

	return ret;
}

/*
 * Description:
 *	Initialize PHY(BB/RF) related functions
 *
 * Return:
 *	_TRUE	Success
 *	_FALSE	Fail
 */
u8 rtl8822b_phy_init(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	PDM_ODM_T phydm;
	u8 rf_type;
	ODM_RF_PATH_E txpath, rxpath;
	BOOLEAN tx2path;
	u8 ok = _TRUE;
	BOOLEAN ret;


	hal = GET_HAL_DATA(adapter);
	phydm = &hal->odmpriv;

	bb_rf_register_definition(adapter);

	rtw_halmac_phy_power_switch(adapter_to_dvobj(adapter), _TRUE);

	ret = config_phydm_parameter_init(phydm, ODM_PRE_SETTING);
	if (FALSE == ret)
		return _FALSE;

	ok = init_bb_reg(adapter);
	if (_FALSE == ok)
		return _FALSE;
	ok = init_rf_reg(adapter);
	if (_FALSE == ok)
		return _FALSE;

	ret = config_phydm_parameter_init(phydm, ODM_POST_SETTING);
	if (FALSE == ret)
		return _FALSE;

	rf_type = RF_2T2R;
	rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, (u8 *)&rf_type);
	switch (rf_type) {
	case RF_1T1R:
		txpath = ODM_RF_A;
		rxpath = ODM_RF_A;
		break;
	case RF_1T2R:
		txpath = ODM_RF_A;
		rxpath = ODM_RF_A | ODM_RF_B;
		break;
	default:
	case RF_2T2R:
		txpath = ODM_RF_A | ODM_RF_B;
		rxpath = ODM_RF_A | ODM_RF_B;
		break;
	}
	tx2path = FALSE;
	ret = config_phydm_trx_mode_8822b(phydm, txpath, rxpath, tx2path);
	if (FALSE == ret)
		return _FALSE;

	return _TRUE;
}

static void dm_CheckProtection(PADAPTER	adapter)
{
}

static void dm_CheckStatistics(PADAPTER	adapter)
{
}

#ifdef CONFIG_SUPPORT_HW_WPS_PBC
static void dm_CheckPbcGPIO(PADAPTER adapter)
{
	u8 tmp1byte;
	u8 bPbcPressed = _FALSE;

	if (!adapter->registrypriv.hw_wps_pbc)
		return;

#ifdef CONFIG_USB_HCI
	tmp1byte = rtw_read8(adapter, GPIO_IO_SEL);
	tmp1byte |= (HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(adapter, GPIO_IO_SEL, tmp1byte); /* enable GPIO[2] as output mode */

	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(adapter, GPIO_IN, tmp1byte); /* reset the floating voltage level */

	tmp1byte = rtw_read8(adapter, GPIO_IO_SEL);
	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(adapter, GPIO_IO_SEL, tmp1byte); /* enable GPIO[2] as input mode */

	tmp1byte = rtw_read8(adapter, GPIO_IN);
	if (tmp1byte == 0xff)
		return;

	if (tmp1byte & HAL_8192C_HW_GPIO_WPS_BIT)
		bPbcPressed = _TRUE;
#else
	tmp1byte = rtw_read8(adapter, GPIO_IN);

	if ((tmp1byte == 0xff) || adapter->init_adpt_in_progress)
		return;

	if ((tmp1byte & HAL_8192C_HW_GPIO_WPS_BIT) == 0)
		bPbcPressed = _TRUE;
#endif

	if (_TRUE == bPbcPressed) {
		/*
		 * Here we only set bPbcPressed to true
		 * After trigger PBC, the variable will be set to false
		 */
		RTW_INFO("CheckPbcGPIO - PBC is pressed\n");
		rtw_request_wps_pbc_event(adapter);
	}
}
#endif /* CONFIG_SUPPORT_HW_WPS_PBC */


#ifdef CONFIG_PCI_HCI
/*
 * Description:
 *	Perform interrupt migration dynamically to reduce CPU utilization.
 *
 * Assumption:
 *	1. Do not enable migration under WIFI test.
 */
void dm_InterruptMigration(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	BOOLEAN bCurrentIntMt, bCurrentACIntDisable;
	BOOLEAN IntMtToSet = _FALSE;
	BOOLEAN ACIntToSet = _FALSE;


	/* Retrieve current interrupt migration and Tx four ACs IMR settings first. */
	bCurrentIntMt = hal->bInterruptMigration;
	bCurrentACIntDisable = hal->bDisableTxInt;

	/*
	 * <Roger_Notes> Currently we use busy traffic for reference instead of RxIntOK counts to prevent non-linear Rx statistics
	 * when interrupt migration is set before. 2010.03.05.
	 */
	if (!adapter->registrypriv.wifi_spec
	    && (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
	    && pmlmepriv->LinkDetectInfo.bHigherBusyTraffic) {
		IntMtToSet = _TRUE;

		/* To check whether we should disable Tx interrupt or not. */
		if (pmlmepriv->LinkDetectInfo.bHigherBusyRxTraffic)
			ACIntToSet = _TRUE;
	}

	/* Update current settings. */
	if (bCurrentIntMt != IntMtToSet) {
		RTW_INFO("%s: Update interrupt migration(%d)\n", __FUNCTION__, IntMtToSet);
		if (IntMtToSet) {
			/*
			 * <Roger_Notes> Set interrupt migration timer and corresponging Tx/Rx counter.
			 * timer 25ns*0xfa0=100us for 0xf packets.
			 * 2010.03.05.
			 */
			rtw_write32(adapter, REG_INT_MIG, 0xff000fa0); /* 0x306:Rx, 0x307:Tx */
			hal->bInterruptMigration = IntMtToSet;
		} else {
			/* Reset all interrupt migration settings. */
			rtw_write32(adapter, REG_INT_MIG, 0);
			hal->bInterruptMigration = IntMtToSet;
		}
	}
}
#endif /* CONFIG_PCI_HCI */

/*
 * ============================================================
 * functions
 * ============================================================
 */
static void init_phydm_cominfo(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	PDM_ODM_T pDM_Odm;
	u32 SupportAbility = 0;
	u8 cut_ver = ODM_CUT_A, fab_ver = ODM_TSMC;


	hal = GET_HAL_DATA(adapter);
	pDM_Odm = &hal->odmpriv;

	Init_ODM_ComInfo(adapter);

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_PACKAGE_TYPE, hal->PackageType);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_IC_TYPE, ODM_RTL8822B);

	if (IS_CHIP_VENDOR_TSMC(hal->VersionID))
		fab_ver = ODM_TSMC;
	else if (IS_CHIP_VENDOR_UMC(hal->VersionID))
		fab_ver = ODM_UMC;
	else if (IS_CHIP_VENDOR_SMIC(hal->VersionID))
		fab_ver = ODM_UMC + 1;
	else
		RTW_INFO("%s: unknown fab_ver=%d !!\n",
			 __FUNCTION__, GET_CVID_MANUFACTUER(hal->VersionID));

	if (IS_A_CUT(hal->VersionID))
		cut_ver = ODM_CUT_A;
	else if (IS_B_CUT(hal->VersionID))
		cut_ver = ODM_CUT_B;
	else if (IS_C_CUT(hal->VersionID))
		cut_ver = ODM_CUT_C;
	else if (IS_D_CUT(hal->VersionID))
		cut_ver = ODM_CUT_D;
	else if (IS_E_CUT(hal->VersionID))
		cut_ver = ODM_CUT_E;
	else if (IS_F_CUT(hal->VersionID))
		cut_ver = ODM_CUT_F;
	else if (IS_I_CUT(hal->VersionID))
		cut_ver = ODM_CUT_I;
	else if (IS_J_CUT(hal->VersionID))
		cut_ver = ODM_CUT_J;
	else if (IS_K_CUT(hal->VersionID))
		cut_ver = ODM_CUT_K;
	else
		RTW_INFO("%s: unknown cut_ver=%d !!\n",
			 __FUNCTION__, GET_CVID_CUT_VERSION(hal->VersionID));

	RTW_INFO("%s: fab_ver=%d cut_ver=%d\n", __FUNCTION__, fab_ver, cut_ver);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_FAB_VER, fab_ver);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_CUT_VER, cut_ver);

#ifdef CONFIG_DISABLE_ODM
	SupportAbility = 0;
#else /* !CONFIG_DISABLE_ODM */
	SupportAbility = ODM_RF_CALIBRATION | ODM_RF_TX_PWR_TRACK;
#endif /* !CONFIG_DISABLE_ODM */
	ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_ABILITY, SupportAbility);
}

static void update_phydm_cominfo(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	PDM_ODM_T pDM_Odm;
	u32 SupportAbility = 0;


	hal = GET_HAL_DATA(adapter);
	pDM_Odm = &hal->odmpriv;

	SupportAbility = 0
			 | ODM_BB_DIG
			 | ODM_BB_RA_MASK
			 | ODM_BB_DYNAMIC_TXPWR
			 | ODM_BB_FA_CNT
			 | ODM_BB_RSSI_MONITOR
			 | ODM_BB_CCK_PD
/*			 | ODM_BB_PWR_SAVE*/
			 | ODM_BB_CFO_TRACKING
			 | ODM_MAC_EDCA_TURBO
			 | ODM_RF_TX_PWR_TRACK
			 | ODM_RF_CALIBRATION
			 | ODM_BB_NHM_CNT
/*			 | ODM_BB_PWR_TRAIN*/
			 ;

	if (rtw_odm_adaptivity_needed(adapter) == _TRUE)
		SupportAbility |= ODM_BB_ADAPTIVITY;

#ifdef CONFIG_ANTENNA_DIVERSITY
	if (hal->AntDivCfg)
		SupportAbility |= ODM_BB_ANT_DIV;
#endif /* CONFIG_ANTENNA_DIVERSITY */

#ifdef CONFIG_MP_INCLUDED
	if (adapter->registrypriv.mp_mode == 1) {
		SupportAbility = 0
				 | ODM_RF_CALIBRATION
				 | ODM_RF_TX_PWR_TRACK
				 ;
	}
#endif /* CONFIG_MP_INCLUDED */

#ifdef CONFIG_DISABLE_ODM
	SupportAbility = 0;
#endif /* CONFIG_DISABLE_ODM */

	ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_ABILITY, SupportAbility);
}

static void _pa_bias_paser(u8 shift, u8 *minus, u8 *val)
{
	shift &= 0xF;
#if 0
	/* Skip 0x8(-4) */
	if ((shift == 0x8) || (shift > 0x9))
		shift = 0;
#endif

	/*
	 * BIT 0 is sign bit,
	 * 1 for positive, 0 for negative
	 */
	*minus = (shift & BIT(0)) ? 0 : 1;
	/* BIT 1~3 are value */
	*val = (shift & 0xE) >> 1;
}

/*
 * Description:
 *	Access TX PA Bias according to 0x3D8[3:0] or 0x3D7[3:0]
 *
 * Return:
 *	Value between 0~7.
 */
static u8 _pa_bias_adjust(u8 bias, u8 minus, u8 val)
{
	s8 tmp = bias;


	if (minus)
		tmp -= val;
	else
		tmp += val;

	if (tmp < 0)
		return 0;
	if (tmp > 7)
		return 7;
	return (u8)tmp;
}

static void _pa_bias_calibration(PADAPTER adapter, enum _ODM_RF_RADIO_PATH path, u8 shift)
{
	u32 rf_reg18_offset[12] = {
				0X10124,
				0X10524,
				0X10924,
				0X10D24,
				0X30164,
				0X30564,
				0X30964,
				0X30D64,
				0X50195,
				0X50595,
				0X50995,
				0X50D95
				};
	u8 offset;
	u32 bias_org, bias_new;
	u8 val8;
	u8 minus = 0;
	u8 val = 0;


	_pa_bias_paser(shift, &minus, &val);
	RTW_INFO("%s: RF-%u bias shift %s%u\n", __FUNCTION__, path, minus?"-":"+", val);
	if (!val)
		return;

	rtw_hal_write_rfreg(adapter, path, 0xEF, 0xFFFFF, 0x200);

	/* Set 12 sets of TxA value */
	for (offset = 0; offset < 12; offset++) {
		/* Read TX PA Bias Table default value */
		rtw_hal_write_rfreg(adapter, path, 0x18, 0xFFFFF, rf_reg18_offset[offset]);
		bias_org = rtw_hal_read_rfreg(adapter, path, 0x61, 0xFFFFF);
		val8 = bias_org & 0xF;
		val8 = _pa_bias_adjust(val8, minus, val);
		bias_new = (bias_org & 0xFF0) | val8;
		bias_new |= (offset << 12);
		rtw_hal_write_rfreg(adapter, path, 0x30, 0xFFFFF, bias_new);
	}

	rtw_hal_write_rfreg(adapter, path, 0xEF, 0xFFFFF, 0x0);
}

static void pa_bias_calibration(PADAPTER adapter)
{
	struct hal_com_data *hal;
	u32 rf_reg18_a, rf_reg18_b;
	u32 time;


	hal = GET_HAL_DATA(adapter);
	if (0xFF == hal->tx_pa_bias_a)
		return;

	time = rtw_get_current_time();

	/* Save RF register 0x18 */
	rf_reg18_a = rtw_hal_read_rfreg(adapter, ODM_RF_PATH_A, 0x18, 0xFFFFF);
	rf_reg18_b = rtw_hal_read_rfreg(adapter, ODM_RF_PATH_B, 0x18, 0xFFFFF);

	_pa_bias_calibration(adapter, ODM_RF_PATH_A, hal->tx_pa_bias_a);
	_pa_bias_calibration(adapter, ODM_RF_PATH_B, hal->tx_pa_bias_b);

	/* Restore RF register 0x18 */
	rtw_hal_write_rfreg(adapter, ODM_RF_PATH_A, 0x18, 0xFFFFF, rf_reg18_a);
	rtw_hal_write_rfreg(adapter, ODM_RF_PATH_B, 0x18, 0xFFFFF, rf_reg18_b);

	RTW_INFO("%s: done! cost %u ms\n", __FUNCTION__, rtw_get_passing_time_ms(time));
}

void rtl8822b_phy_init_dm_priv(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	PDM_ODM_T podmpriv = &hal->odmpriv;


	/*
	 * Call this before other phydm functions
	 * to guarantee debug messages of phydm would be printed.
	 */

	init_phydm_cominfo(adapter);
	ODM_InitAllTimers(podmpriv);
	PHYDM_InitDebugSetting(podmpriv);
}

void rtl8822b_phy_deinit_dm_priv(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	PDM_ODM_T podmpriv = &hal->odmpriv;


	ODM_CancelAllTimers(podmpriv);
}

void rtl8822b_phy_init_haldm(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	PDM_ODM_T pDM_Odm;


	hal = GET_HAL_DATA(adapter);
	pDM_Odm = &hal->odmpriv;

	hal->DM_Type = DM_Type_ByDriver;

	update_phydm_cominfo(adapter);

	ODM_DMInit(pDM_Odm);

	pa_bias_calibration(adapter);
}

static void check_rxfifo_full(PADAPTER adapter)
{
	struct dvobj_priv *psdpriv = adapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	HAL_DATA_TYPE *hal = GET_HAL_DATA(adapter);
	struct registry_priv *regsty = &adapter->registrypriv;
	u8 val8 = 0;

	if (regsty->check_hw_status == 1) {
		/* switch counter to RX fifo */
		val8 = rtw_read8(adapter, REG_RXERR_RPT_8822B + 3);
		rtw_write8(adapter, REG_RXERR_RPT_8822B + 3, (val8 | 0xa0));

		pdbgpriv->dbg_rx_fifo_last_overflow = pdbgpriv->dbg_rx_fifo_curr_overflow;
		pdbgpriv->dbg_rx_fifo_curr_overflow = rtw_read16(adapter, REG_RXERR_RPT_8822B);
		pdbgpriv->dbg_rx_fifo_diff_overflow =
			pdbgpriv->dbg_rx_fifo_curr_overflow - pdbgpriv->dbg_rx_fifo_last_overflow;
	}
}

void rtl8822b_phy_haldm_watchdog(PADAPTER adapter)
{
	BOOLEAN bFwCurrentInPSMode = _FALSE;
	BOOLEAN bFwPSAwake = _TRUE;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


#ifdef CONFIG_MP_INCLUDED
	/* for MP power tracking */
	if ((adapter->registrypriv.mp_mode == 1)
	    && (adapter->mppriv.mp_dm == 0))
		return;
#endif /* CONFIG_MP_INCLUDED */

	if (!rtw_is_hw_init_completed(adapter))
		goto skip_dm;

#ifdef CONFIG_LPS
	bFwCurrentInPSMode = adapter_to_pwrctl(adapter)->bFwCurrentInPSMode;
	rtw_hal_get_hwreg(adapter, HW_VAR_FWLPS_RF_ON, (u8 *)&bFwPSAwake);
#endif /* CONFIG_LPS */

#ifdef CONFIG_P2P_PS
	/*
	 * Fw is under p2p powersaving mode, driver should stop dynamic mechanism.
	 */
	if (adapter->wdinfo.p2p_ps_mode)
		bFwPSAwake = _FALSE;
#endif /* CONFIG_P2P */

	if ((rtw_is_hw_init_completed(adapter))
	    && ((!bFwCurrentInPSMode) && bFwPSAwake)) {

		/* check rx fifo */
		check_rxfifo_full(adapter);
		/*
		 * Dynamically switch RTS/CTS protection.
		 */
		dm_CheckProtection(adapter);
	}

	/* PHYDM */
	if (rtw_is_hw_init_completed(adapter)) {
		u8 bLinked = _FALSE;
		u8 bsta_state = _FALSE;
		u8 bBtDisabled = _TRUE;

		if (rtw_mi_check_status(adapter, MI_ASSOC)) {
			bLinked = _TRUE;
			if (rtw_mi_check_status(adapter, MI_STA_LINKED))
				bsta_state = _TRUE;
		}

		ODM_CmnInfoUpdate(&hal->odmpriv, ODM_CMNINFO_LINK, bLinked);
		ODM_CmnInfoUpdate(&hal->odmpriv, ODM_CMNINFO_STATION_STATE, bsta_state);

#ifdef CONFIG_BT_COEXIST
		bBtDisabled = rtw_btcoex_IsBtDisabled(adapter);
#endif /* CONFIG_BT_COEXIST */
		ODM_CmnInfoUpdate(&hal->odmpriv, ODM_CMNINFO_BT_ENABLED, ((bBtDisabled == _TRUE) ? _FALSE : _TRUE));

		ODM_DMWatchdog(&hal->odmpriv);
	}

skip_dm:
	/*
	 * Check GPIO to determine current RF on/off and Pbc status.
	 * Check Hardware Radio ON/OFF or not
	 */
#ifdef CONFIG_SUPPORT_HW_WPS_PBC
	dm_CheckPbcGPIO(adapter);
#else /* !CONFIG_SUPPORT_HW_WPS_PBC */
	return;
#endif /* !CONFIG_SUPPORT_HW_WPS_PBC */
}

void rtl8822b_phy_haldm_in_lps(PADAPTER adapter)
{
	u32 PWDB_rssi = 0;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	PDM_ODM_T pDM_Odm = &hal->odmpriv;
	pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *psta = NULL;


	RTW_INFO("%s: RSSI_Min=%d\n", __FUNCTION__, pDM_Odm->RSSI_Min);

	/* update IGI */
	ODM_Write_DIG(pDM_Odm, pDM_Odm->RSSI_Min);

	/* set rssi to fw */
	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta && (psta->rssi_stat.UndecoratedSmoothedPWDB > 0)) {
		PWDB_rssi = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB << 16));
		rtl8822b_set_FwRssiSetting_cmd(adapter, (u8 *)&PWDB_rssi);
	}
}

void rtl8822b_phy_haldm_watchdog_in_lps(PADAPTER adapter)
{
	u8 bLinked = _FALSE;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	PDM_ODM_T pDM_Odm = &hal->odmpriv;
	pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *psta = NULL;

	if (!rtw_is_hw_init_completed(adapter))
		goto skip_lps_dm;

	if (rtw_mi_check_status(adapter, MI_ASSOC))
		bLinked = _TRUE;

	ODM_CmnInfoUpdate(&hal->odmpriv, ODM_CMNINFO_LINK, bLinked);

	if (bLinked == _FALSE)
		goto skip_lps_dm;

	if (!(pDM_Odm->SupportAbility & ODM_BB_RSSI_MONITOR))
		goto skip_lps_dm;

	/* Do DIG by RSSI In LPS-32K */

	/* 1. Find MIN-RSSI */
	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta == NULL)
		goto skip_lps_dm;

	hal->EntryMinUndecoratedSmoothedPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

	RTW_INFO("CurIGValue=%d, EntryMinUndecoratedSmoothedPWDB=%d\n",
		pDM_DigTable->CurIGValue, hal->EntryMinUndecoratedSmoothedPWDB);

	if (hal->EntryMinUndecoratedSmoothedPWDB <= 0)
		goto skip_lps_dm;

	hal->MinUndecoratedPWDBForDM = hal->EntryMinUndecoratedSmoothedPWDB;

	pDM_Odm->RSSI_Min = hal->MinUndecoratedPWDBForDM;

#ifdef CONFIG_LPS
	if ((pDM_DigTable->CurIGValue > pDM_Odm->RSSI_Min + 5)
	    || (pDM_DigTable->CurIGValue < pDM_Odm->RSSI_Min - 5))
		rtw_dm_in_lps_wk_cmd(adapter);
#endif

skip_lps_dm:
	return;
}

static u32 phy_calculatebitshift(u32 mask)
{
	u32 i;


	for (i = 0; i <= 31; i++)
		if (mask & BIT(i))
			break;

	return i;
}

u32 rtl8822b_read_bb_reg(PADAPTER adapter, u32 addr, u32 mask)
{
	u32 val = 0, val_org, shift;


#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	val_org = rtw_read32(adapter, addr);
	shift = phy_calculatebitshift(mask);
	val = (val_org & mask) >> shift;

	return val;
}

void rtl8822b_write_bb_reg(PADAPTER adapter, u32 addr, u32 mask, u32 val)
{
	u32 val_org, shift;


#if (DISABLE_BB_RF == 1)
	return;
#endif

	if (mask != 0xFFFFFFFF) {
		/* not "double word" write */
		val_org = rtw_read32(adapter, addr);
		shift = phy_calculatebitshift(mask);
		val = ((val_org & (~mask)) | ((val << shift) & mask));
	}

	rtw_write32(adapter, addr, val);
}

u32 rtl8822b_read_rf_reg(PADAPTER adapter, u8 path, u32 addr, u32 mask)
{
	PHAL_DATA_TYPE hal;
	PDM_ODM_T phydm;
	u32 val;


	hal = GET_HAL_DATA(adapter);
	phydm = &hal->odmpriv;

	val = config_phydm_read_rf_reg_8822b(phydm, path, addr, mask);
	if (!config_phydm_read_rf_check_8822b(val))
		RTW_INFO(FUNC_ADPT_FMT ": read RF reg path=%d addr=0x%x mask=0x%x FAIL!\n",
			 FUNC_ADPT_ARG(adapter), path, addr, mask);

	return val;
}

void rtl8822b_write_rf_reg(PADAPTER adapter, u8 path, u32 addr, u32 mask, u32 val)
{
	PHAL_DATA_TYPE hal;
	PDM_ODM_T phydm;
	u8 ret;


	hal = GET_HAL_DATA(adapter);
	phydm = &hal->odmpriv;

	ret = config_phydm_write_rf_reg_8822b(phydm, path, addr, mask, val);
	if (_FALSE == ret)
		RTW_INFO(FUNC_ADPT_FMT ": write RF reg path=%d addr=0x%x mask=0x%x val=0x%x FAIL!\n",
			 FUNC_ADPT_ARG(adapter), path, addr, mask, val);
}

static void set_tx_power_level_by_path(PADAPTER adapter, u8 channel, u8 path)
{
	PHY_SetTxPowerIndexByRateSection(adapter, path, channel, CCK);
	PHY_SetTxPowerIndexByRateSection(adapter, path, channel, OFDM);
	PHY_SetTxPowerIndexByRateSection(adapter, path, channel, HT_MCS0_MCS7);
	PHY_SetTxPowerIndexByRateSection(adapter, path, channel, HT_MCS8_MCS15);
	PHY_SetTxPowerIndexByRateSection(adapter, path, channel, VHT_1SSMCS0_1SSMCS9);
	PHY_SetTxPowerIndexByRateSection(adapter, path, channel, VHT_2SSMCS0_2SSMCS9);
}

void rtl8822b_set_tx_power_level(PADAPTER adapter, u8 channel)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	PDM_ODM_T phydm;
	pFAT_T pDM_FatTable;
	u8 path = ODM_RF_PATH_A;


	hal = GET_HAL_DATA(adapter);
	phydm = &hal->odmpriv;
	pDM_FatTable = &phydm->DM_FatTable;

	if (hal->AntDivCfg) {
		/* antenna diversity Enable */
		path = (pDM_FatTable->RxIdleAnt == MAIN_ANT) ? ODM_RF_PATH_A : ODM_RF_PATH_B;
		set_tx_power_level_by_path(adapter, channel, path);
	} else {
		/* antenna diversity disable */
		for (path = ODM_RF_PATH_A; path < hal->NumTotalRFPath; ++path)
			set_tx_power_level_by_path(adapter, channel, path);
	}
}

void rtl8822b_get_tx_power_level(PADAPTER adapter, s32 *power)
{
}

/*
 * Parameters:
 *	padatper
 *	powerindex	power index for rate
 *	rfpath		Antenna(RF) path, type "enum _ODM_RF_RADIO_PATH"
 *	rate		data rate, type "enum MGN_RATE"
 */
void rtl8822b_set_tx_power_index(PADAPTER adapter, u32 powerindex, u8 rfpath, u8 rate)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	PDM_ODM_T phydm = &hal->odmpriv;
	u8 shift = 0;
	static u32 index = 0;


	rate = PHY_GetRateIndexOfTxPowerByRate(rate);

	/*
	* For 8822B, phydm api use 4 bytes txagc value
	* driver must combine every four 1 byte to one 4 byte and send to phydm api
	*/
	shift = rate % 4;
	index |= ((powerindex & 0xff) << (shift * 8));

	if (shift == 3) {
		rate = rate - 3;

		if (!config_phydm_write_txagc_8822b(phydm, index, rfpath, rate)) {
			RTW_INFO("%s(index:%d, rfpath:%d, rate:0x%02x, disable api:%d) fail\n",
				__FUNCTION__, index, rfpath, rate, phydm->bDisablePhyApi);

			rtw_warn_on(1);
		}
		index = 0;
	}
}

static u8 rtl8822b_phy_get_current_tx_num(PADAPTER adapter, u8 rate)
{
	u8 tx_num = 0;

	if ((rate >= MGN_MCS8 && rate <= MGN_MCS15) ||
	    (rate >= MGN_VHT2SS_MCS0 && rate <= MGN_VHT2SS_MCS9))
		tx_num = RF_2TX;
	else
		tx_num = RF_1TX;

	return tx_num;
}

/*
 * Parameters:
 *	padatper
 *	rfpath		Antenna(RF) path, type "enum _ODM_RF_RADIO_PATH"
 *	rate		data rate, type "enum MGN_RATE"
 *	bandwidth	Bandwidth, type "enum _CHANNEL_WIDTH"
 *	channel		Channel number
 *
 * Rteurn:
 *	tx_power	power index for rate
 */
u8 rtl8822b_get_tx_power_index(PADAPTER adapter, u8 rfpath, u8 rate, u8 bandwidth, u8 channel)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	s8 power_diff_by_rate = 0;
	s8 tx_power = 0, limit = 0;
	u8 tx_num = rtl8822b_phy_get_current_tx_num(adapter, rate);
	u8 bIn24G = _FALSE;


	tx_power = (s8) PHY_GetTxPowerIndexBase(adapter, rfpath, rate, bandwidth, channel, &bIn24G);

	power_diff_by_rate = PHY_GetTxPowerByRate(adapter, (u8)(!bIn24G), rfpath, tx_num, rate);

	limit = PHY_GetTxPowerLimit(adapter, adapter->registrypriv.RegPwrTblSel, (BAND_TYPE)(!bIn24G),
		    hal->CurrentChannelBW, rfpath, rate, hal->CurrentChannel);

	power_diff_by_rate = power_diff_by_rate > limit ? limit : power_diff_by_rate;

	tx_power += power_diff_by_rate;

#if 0
#if CCX_SUPPORT
	CCX_CellPowerLimit(pAdapter, Channel, Rate, (u8 *)&txPower);
#endif
#endif

	if (tx_power > MAX_POWER_INDEX)
		tx_power = MAX_POWER_INDEX;

	return tx_power;
}

/*
 * Description:
 *	Check need to switch band or not
 * Parameters:
 *	channelToSW	channel wiii be switch to
 * Return:
 *	_TRUE		need to switch band
 *	_FALSE		not need to switch band
 */
static u8 need_switch_band(PADAPTER adapter, u8 channelToSW)
{
	u8 u1tmp = 0;
	u8 ret_value = _TRUE;
	u8 Band = BAND_ON_5G, BandToSW = BAND_ON_5G;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	Band = hal->CurrentBandType;

	/* Use current swich channel to judge Band Type and switch Band if need */
	if (channelToSW > 14)
		BandToSW = BAND_ON_5G;
	else
		BandToSW = BAND_ON_2_4G;

	if (BandToSW != Band) {
		/* record current band type for other hal use */
		hal->CurrentBandType = (BAND_TYPE)BandToSW;
		ret_value = _TRUE;
	} else
		ret_value = _FALSE;

	return ret_value;
}

static u8 get_pri_ch_id(PADAPTER adapter)
{
	u8 pri_ch_idx = 0;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	if (hal->CurrentChannelBW == CHANNEL_WIDTH_80) {
		/* primary channel is at lower subband of 80MHz & 40MHz */
		if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
			pri_ch_idx = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		/* primary channel is at lower subband of 80MHz & upper subband of 40MHz */
		else if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
			pri_ch_idx = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		/* primary channel is at upper subband of 80MHz & lower subband of 40MHz */
		else if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
			pri_ch_idx = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		/* primary channel is at upper subband of 80MHz & upper subband of 40MHz */
		else if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
			pri_ch_idx = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
		else {
			if (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
				pri_ch_idx = VHT_DATA_SC_40_LOWER_OF_80MHZ;
			else if (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
				pri_ch_idx = VHT_DATA_SC_40_UPPER_OF_80MHZ;
			else
				RTW_INFO("SCMapping: DONOT CARE Mode Setting\n");
		}
	} else if (hal->CurrentChannelBW == CHANNEL_WIDTH_40) {
		/* primary channel is at upper subband of 40MHz */
		if (hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			pri_ch_idx = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		/* primary channel is at lower subband of 40MHz */
		else if (hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			pri_ch_idx = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else
			RTW_INFO("SCMapping: DONOT CARE Mode Setting\n");
	}

	return  pri_ch_idx;
}

static void mac_switch_bandwidth(PADAPTER adapter, u8 pri_ch_idx)
{
	u8 channel = 0, bw = 0;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	int err;

	channel = hal->CurrentChannel;
	bw = hal->CurrentChannelBW;
	err = rtw_halmac_set_bandwidth(adapter_to_dvobj(adapter), channel, pri_ch_idx, bw);
	if (err) {
		RTW_INFO(FUNC_ADPT_FMT ": (channel=%d, pri_ch_idx=%d, bw=%d) fail\n",
			 FUNC_ADPT_ARG(adapter), channel, pri_ch_idx, bw);
	}
}

/*
 * Description:
 *	Set channel & bandwidth & offset
 */
void rtl8822b_switch_chnl_and_set_bw(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	PDM_ODM_T pDM_Odm = &hal->odmpriv;
	u8 center_ch = 0, ret = 0;

	if (adapter->bNotifyChannelChange) {
		RTW_INFO("[%s] bSwChnl=%d, ch=%d, bSetChnlBW=%d, bw=%d\n",
			 __FUNCTION__,
			 hal->bSwChnl,
			 hal->CurrentChannel,
			 hal->bSetChnlBW,
			 hal->CurrentChannelBW);
	}

	if (RTW_CANNOT_RUN(adapter)) {
		hal->bSwChnlAndSetBWInProgress = _FALSE;
		return;
	}

	/* set channel & Bandwidth register */
	/* 1. set switch band register if need to switch band */
	if (need_switch_band(adapter, hal->CurrentChannel) == _TRUE) {
#ifdef CONFIG_BT_COEXIST
		if (hal->EEPROMBluetoothCoexist) {
			struct mlme_ext_priv *mlmeext;

			/* switch band under site survey or not, must notify to BT COEX */
			mlmeext = &adapter->mlmeextpriv;
			if (mlmeext_scan_state(mlmeext) != SCAN_DISABLE)
				rtw_btcoex_switchband_notify(_TRUE, hal->CurrentBandType);
			else
				rtw_btcoex_switchband_notify(_FALSE, hal->CurrentBandType);
		}
#endif /* CONFIG_BT_COEXIST */

		/* hal->CurrentChannel is center channel of pmlmeext->cur_channel(primary channel) */
		ret = config_phydm_switch_band_8822b(pDM_Odm, hal->CurrentChannel);

		if (!ret) {
			RTW_INFO("%s: config_phydm_switch_band_8822b fail\n", __FUNCTION__);
			rtw_warn_on(1);
			return;
		}

		/* <2016/03/09> ** This Setting is for MP Driver Only*/
#ifdef CONFIG_MP_INCLUDED
		if (adapter->registrypriv.mp_mode == _TRUE) {
			/* <2016/02/25, VincentL> Add for 8822B Antenna Binding between "2.4G-WiFi"
						  And between "5G-BT", Suggested by RF SzuyiTsai*/
			if (hal->CurrentChannel <= 14) /* 2.4G*/
				PHY_SetRFPathSwitch_8822B(adapter, 1); /*To WiFi-2.4G*/
			else /* 5G */
				PHY_SetRFPathSwitch_8822B(adapter, 0); /*To BT-5G*/
		}
#endif

	}

	/* 2. set channel register */
	if (hal->bSwChnl) {
		ret = config_phydm_switch_channel_8822b(pDM_Odm, hal->CurrentChannel);
		hal->bSwChnl = _FALSE;

		if (!ret) {
			RTW_INFO("%s: config_phydm_switch_channel_8822b fail\n", __FUNCTION__);
			rtw_warn_on(1);
			return;
		}
	}

	/* 3. set Bandwidth register */
	if (hal->bSetChnlBW) {
		/* get primary channel index */
		u8 pri_ch_idx = get_pri_ch_id(adapter);

		/* 3.1 set MAC register */
		mac_switch_bandwidth(adapter, pri_ch_idx);

		/* 3.2 set BB/RF registet */
		ret = config_phydm_switch_bandwidth_8822b(pDM_Odm, pri_ch_idx, hal->CurrentChannelBW);
		hal->bSetChnlBW = _FALSE;

		if (!ret) {
			RTW_INFO("%s: config_phydm_switch_bandwidth_8822b fail\n", __FUNCTION__);
			rtw_warn_on(1);
			return;
		}
	}

	/* TX Power Setting */
	ODM_ClearTxPowerTrackingState(pDM_Odm);
	rtw_hal_set_tx_power_level(adapter, hal->CurrentChannel);

	/* IQK */
	if ((hal->bNeedIQK == _TRUE)
	    || (adapter->registrypriv.mp_mode == 1)) {
		PHY_IQCalibrate_8822B(pDM_Odm, _FALSE);
		hal->bNeedIQK = _FALSE;
	}
}

/*
 * Description:
 *	Store channel setting to hal date
 * Parameters:
 *	bSwitchChannel		swith channel or not
 *	bSetBandWidth		set band or not
 *	ChannelNum		center channel
 *	ChnlWidth		bandwidth
 *	ChnlOffsetOf40MHz	channel offset for 40MHz Bandwidth
 *	ChnlOffsetOf80MHz	channel offset for 80MHz Bandwidth
 *	CenterFrequencyIndex1	center channel index
 */

void rtl8822b_handle_sw_chnl_and_set_bw(
	PADAPTER Adapter, u8 bSwitchChannel, u8 bSetBandWidth,
	u8 ChannelNum, CHANNEL_WIDTH ChnlWidth, u8 ChnlOffsetOf40MHz,
	u8 ChnlOffsetOf80MHz, u8 CenterFrequencyIndex1)
{
	PADAPTER pDefAdapter = GetDefaultAdapter(Adapter);
	PHAL_DATA_TYPE hal = GET_HAL_DATA(pDefAdapter);
	u8 tmpChannel = hal->CurrentChannel;
	CHANNEL_WIDTH tmpBW = hal->CurrentChannelBW;
	u8 tmpnCur40MhzPrimeSC = hal->nCur40MhzPrimeSC;
	u8 tmpnCur80MhzPrimeSC = hal->nCur80MhzPrimeSC;
	u8 tmpCenterFrequencyIndex1 = hal->CurrentCenterFrequencyIndex1;
	struct mlme_ext_priv *pmlmeext = &Adapter->mlmeextpriv;


	/* check swchnl or setbw */
	if (!bSwitchChannel && !bSetBandWidth) {
		RTW_INFO("%s: not switch channel and not set bandwidth\n", __FUNCTION__);
		return;
	}

	/* skip switch channel operation for current channel & ChannelNum(will be switch) are the same */
	if (bSwitchChannel) {
		if (hal->CurrentChannel != ChannelNum) {
			if (HAL_IsLegalChannel(Adapter, ChannelNum))
				hal->bSwChnl = _TRUE;
			else
				return;
		}
	}

	/* check set BandWidth */
	if (bSetBandWidth) {
		/* initial channel bw setting */
		if (hal->bChnlBWInitialized == _FALSE) {
			hal->bChnlBWInitialized = _TRUE;
			hal->bSetChnlBW = _TRUE;
		} else if ((hal->CurrentChannelBW != ChnlWidth) || /* check whether need set band or not */
			   (hal->nCur40MhzPrimeSC != ChnlOffsetOf40MHz) ||
			   (hal->nCur80MhzPrimeSC != ChnlOffsetOf80MHz) ||
			(hal->CurrentCenterFrequencyIndex1 != CenterFrequencyIndex1))
			hal->bSetChnlBW = _TRUE;
	}

	/* return if not need set bandwidth nor channel after check*/
	if (!hal->bSetChnlBW && !hal->bSwChnl)
		return;

	/* set channel number to hal data */
	if (hal->bSwChnl) {
		hal->CurrentChannel = ChannelNum;
		hal->CurrentCenterFrequencyIndex1 = ChannelNum;
	}

	/* set bandwidth info to hal data */
	if (hal->bSetChnlBW) {
		hal->CurrentChannelBW = ChnlWidth;
		hal->nCur40MhzPrimeSC = ChnlOffsetOf40MHz;
		hal->nCur80MhzPrimeSC = ChnlOffsetOf80MHz;
		hal->CurrentCenterFrequencyIndex1 = CenterFrequencyIndex1;
	}

	/* switch channel & bandwidth */
	if (!RTW_CANNOT_RUN(Adapter))
		rtl8822b_switch_chnl_and_set_bw(Adapter);
	else {
		if (hal->bSwChnl) {
			hal->CurrentChannel = tmpChannel;
			hal->CurrentCenterFrequencyIndex1 = tmpChannel;
		}

		if (hal->bSetChnlBW) {
			hal->CurrentChannelBW = tmpBW;
			hal->nCur40MhzPrimeSC = tmpnCur40MhzPrimeSC;
			hal->nCur80MhzPrimeSC = tmpnCur80MhzPrimeSC;
			hal->CurrentCenterFrequencyIndex1 = tmpCenterFrequencyIndex1;
		}
	}
}

/*
 * Description:
 *	Change bandwidth, current channel is the same
 * Parameters:
 *	bw	bandwidth
 *	offset	channel offset for 40MHz or 80MHz Bandwidth according to bw
 */
void rtl8822b_set_bw_mode(PADAPTER adapter, CHANNEL_WIDTH bw, u8 offset)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	rtl8822b_handle_sw_chnl_and_set_bw(adapter, _FALSE, _TRUE, hal->CurrentChannel, bw, offset, offset, hal->CurrentChannel);
}

/*
 * Description:
 *	Change channel, and bandwidth & offset are not changed
 * Parameters:
 *	center_ch	center channel
 */
void rtl8822b_set_channel(PADAPTER adapter, u8 center_ch)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	rtl8822b_handle_sw_chnl_and_set_bw(adapter, _TRUE, _FALSE, center_ch, 0, 0, 0, center_ch);
}

/*
 * Description:
 *	Change channel, bandwidth & offset
 * Parameters:
 *	center_ch	center channel
 *	bw		bandwidth
 *	offset40	channel offset for 40MHz Bandwidth
 *	offset80	channel offset for 80MHz Bandwidth
 */
void rtl8822b_set_channel_bw(PADAPTER adapter, u8 center_ch, CHANNEL_WIDTH bw, u8 offset40, u8 offset80)
{
	rtl8822b_handle_sw_chnl_and_set_bw(adapter, _TRUE, _TRUE, center_ch, bw, offset40, offset80, center_ch);
}

void rtl8822b_notch_filter_switch(PADAPTER adapter, bool enable)
{
	if (enable)
		RTW_INFO("%s: Enable notch filter\n", __FUNCTION__);
	else
		RTW_INFO("%s: Disable notch filter\n", __FUNCTION__);
}

#ifdef CONFIG_MP_INCLUDED
/*
 * Description:
 *	Config RF path
 *
 * Parameters:
 *	adapter	pointer of struct _ADAPTER
 */
void rtl8822b_mp_config_rfpath(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	PMPT_CONTEXT mpt;
	ANTENNA_PATH anttx, antrx;
	ODM_RF_PATH_E rxant;


	hal = GET_HAL_DATA(adapter);
	mpt = &adapter->mppriv.MptCtx;
	anttx = hal->AntennaTxPath;
	antrx = hal->AntennaRxPath;
	hal->antenna_test = _TRUE;
	RTW_INFO("+Config RF Path, tx=0x%x rx=0x%x\n", anttx, antrx);

	switch (anttx) {
	case ANTENNA_A:
		mpt->MptRfPath = ODM_RF_A;
		break;
	case ANTENNA_B:
		mpt->MptRfPath = ODM_RF_B;
		break;
	case ANTENNA_AB:
	default:
		mpt->MptRfPath = ODM_RF_A | ODM_RF_B;
		break;
	}

	switch (antrx) {
	case ANTENNA_A:
		rxant = ODM_RF_A;
		break;
	case ANTENNA_B:
		rxant = ODM_RF_B;
		break;
	case ANTENNA_AB:
	default:
		rxant = ODM_RF_A | ODM_RF_B;
		break;
	}

	config_phydm_trx_mode_8822b(GET_PDM_ODM(adapter), mpt->MptRfPath, rxant, FALSE);

	RTW_INFO("-Config RF Path Finish\n");
}
#endif /* CONFIG_MP_INCLUDED */

#ifdef CONFIG_BEAMFORMING
void rtl8822b_phy_init_beamforming(PADAPTER adapter)
{
	u8 v8;
	u32 v32;

	v32 = rtw_read32(adapter, 0x14c0);
	/* Enable P1 aggr new packet according to P0 transfer time */
	v32 |= BIT(16);
	/* MU Retry Limit */
	v32 = (v32 & 0xFFFF0FFF) | 0xA000;
	/*Disable Tx MU-MIMO until sounding done*/
	v32 &= ~BIT(7);
	/* Clear validity of MU STAs */
	v32 &= 0xFFFFFFC0;
	rtw_write32(adapter, 0x14C0, v32);

	/* MU-MIMO Option as default value */
	rtw_write8(adapter, 0x167C, 0x70);
	/*MU-MIMO Control as default value*/
	rtw_write16(adapter, 0x1680, 0);

	/* Set MU NDPA rate & BW source */
	/* 0x42C[30] = 1 (0: from Tx desc, 1: from 0x45F) */
	v8 = rtw_read8(adapter, REG_TXBF_CTRL_8822B);
	v8 |= BIT(6);
	rtw_write8(adapter, REG_TXBF_CTRL_8822B, v8);
	/* 0x45F[7:0] = 0x10 (Rate=OFDM_6M, BW20) */
	rtw_write8(adapter, REG_NDPA_OPT_CTRL_8822B, 0x10);

	/* Temp Settings */
	/* STA2's CSI rate is fixed at 6M */
	v8 = rtw_read8(adapter, 0x6DF);
	v8 = (v8 & 0xC0) | 0x4; 
	rtw_write8(adapter, 0x6DF, v8);
	/* Grouping bitmap parameters */
	rtw_write32(adapter, 0x1C94, 0xAFFFAFFF);
}

static void _setbeamformrfmode(PADAPTER adapter, u8 cnt_bfee_su, u8 cnt_bfee_mu, u8 idx)
{
	BEAMFORMING_CAP	bf_cap = BEAMFORMING_CAP_NONE;


	if (GET_RF_TYPE(adapter) == RF_1T1R)
		return;

	/* RF Mode table write enable */
	PHY_SetRFReg(adapter, ODM_RF_PATH_A, RF_WeLut_Jaguar, 0x80000, 0x1);
	PHY_SetRFReg(adapter, ODM_RF_PATH_B, RF_WeLut_Jaguar, 0x80000, 0x1);

	if (cnt_bfee_su || cnt_bfee_mu) {
		/* Path_A */
		/* Select RX mode */
		PHY_SetRFReg(adapter, ODM_RF_PATH_A, RF_ModeTableAddr, 0xfffff, 0x18000);
		/* Set Table data */
		PHY_SetRFReg(adapter, ODM_RF_PATH_A, RF_ModeTableData0, 0xfffff, 0xBE77F);
		/* Enable TXIQGEN in RX mode */
		PHY_SetRFReg(adapter, ODM_RF_PATH_A, RF_ModeTableData1, 0xfffff, 0x226BF);
		/* Path_B */
		/* Select RX mode */
		PHY_SetRFReg(adapter, ODM_RF_PATH_B, RF_ModeTableAddr, 0xfffff, 0x18000);
		/* Set Table data */
		PHY_SetRFReg(adapter, ODM_RF_PATH_B, RF_ModeTableData0, 0xfffff, 0xBE77F);
		/* Enable TXIQGEN in RX mode */
		PHY_SetRFReg(adapter, ODM_RF_PATH_B, RF_ModeTableData1, 0xfffff, 0x226BF);

		/* Enable TXIQGEN in RX mode */
		PHY_SetRFReg(adapter, ODM_RF_PATH_A, RF_ModeTableData1, 0xfffff, 0xE26BF);
	}

	/* RF Mode table write disable */
	PHY_SetRFReg(adapter, ODM_RF_PATH_A, RF_WeLut_Jaguar, 0x80000, 0x0);
	PHY_SetRFReg(adapter, ODM_RF_PATH_B, RF_WeLut_Jaguar, 0x80000, 0x0);

	if (cnt_bfee_su) {
		/* enable BB TxBF ant mapping register */
		PHY_SetBBReg(adapter, REG_BB_TXBF_ANT_SET_BF1_8822B, BIT(28)|BIT(29), 0x2);

		if (idx == 0) {
			/* Nsts = 2, AB */
			PHY_SetBBReg(adapter, REG_BB_TXBF_ANT_SET_BF0_8822B, 0xffff, 0x0433);
			PHY_SetBBReg(adapter, REG_BB_TX_PATH_SEL_1_8822B, 0xfff00000, 0x043);
			/*PHY_SetBBReg(adapter, REG_BB_TX_PATH_SEL_2_8822B, bMaskLWord, 0x430);*/

		} else { /* idx = 1 */
			PHY_SetBBReg(adapter, REG_BB_TXBF_ANT_SET_BF1_8822B, 0xffff, 0x0433);
			PHY_SetBBReg(adapter, REG_BB_TX_PATH_SEL_1_8822B, 0xfff00000, 0x043);
			/*PHY_SetBBReg(adapter, REG_BB_TX_PATH_SEL_2_8822B, bMaskLWord, 0x430;*/
		}
	} else {
		PHY_SetBBReg(adapter, REG_BB_TX_PATH_SEL_1_8822B, 0xfff00000, 0x1); /* 1SS by path-A */
		PHY_SetBBReg(adapter, REG_BB_TX_PATH_SEL_2_8822B, bMaskLWord, 0x430); /* 2SS by path-A,B */
	}

	if (cnt_bfee_mu) {
		/* MU STAs share the common setting */
		PHY_SetBBReg(adapter, REG_BB_TXBF_ANT_SET_BF1_8822B, BIT(31), 1);
		PHY_SetBBReg(adapter, REG_BB_TXBF_ANT_SET_BF1_8822B, 0xffff, 0x0433);
		PHY_SetBBReg(adapter, REG_BB_TX_PATH_SEL_1_8822B, 0xfff00000, 0x043);
	}
}

static u8 _get_txbf_nrx(PADAPTER adapter)
{
	u8 rf;
	u8 nrx = 0;


	rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, &rf);
	switch (rf) {
	case RF_1T1R:
		nrx = 0;
		break;
	default:
	case RF_1T2R:
	case RF_2T2R:
		nrx = 1;
		break;
	}

	return nrx;
}

typedef enum _HW_CFG_SOUNDING_TYPE {
	HW_CFG_SOUNDING_TYPE_SOUNDDOWN,
	HW_CFG_SOUNDING_TYPE_LEAVE,
	HW_CFG_SOUNDING_TYPE_RESET,
	HW_CFG_SOUNDING_TYPE_MAX
} HW_CFG_SOUNDING_TYPE, *PHW_CFG_SOUNDING_TYPE;

static void _config_sounding(PADAPTER adapter, struct beamformee_entry *entry, u8 mu_sounding, HW_CFG_SOUNDING_TYPE cfg_type)
{
	struct mlme_priv *mlme;
	struct beamforming_info *bf_info;
	u8 su_cnt, mu_cnt;
	u16 BeamCtrlVal;
	u32 BeamCtrlReg;
	u8 is_bitmap_ready = _FALSE;
	u16 bitmap;
	u8 gid, i, j, count = 0;
	u8 id1, id0;
	u32 gid_valid[6] = {0};
	u32 val32;


	mlme = &adapter->mlmepriv;
	bf_info = GET_BEAMFORM_INFO(adapter);

	if (cfg_type == HW_CFG_SOUNDING_TYPE_RESET) {
		RTW_INFO("%s: HW_CFG_SOUNDING_TYPE_RESET\n",  __FUNCTION__);

		rtw_write8(adapter, REG_TXBF_CTRL_8822B+3, 0);

		/* Clear all MU entry table */
		for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
			entry = &bf_info->bfee_entry[i];
			for (i = 0; i < 8; i++)
				entry->gid_valid[i] = 0;
		}

		val32 = rtw_read32(adapter, 0x14C0);
		for (i = 0; i < 6; i++) {
			val32 &= ~(BIT(8)|BIT(9)|BIT(10));
			val32 |= ((i<<8) & (BIT(8)|BIT(9)|BIT(10)));
			rtw_write32(adapter, 0x14C0, val32);
			/* set MU STA gid valid table */
			rtw_write32(adapter, 0x14C4, 0);
		}

		/* Disable TxMU PPDU */
		val32 &= ~BIT(7);
		rtw_write32(adapter, 0x14C0, val32);
	} else {
		if (_FALSE == mu_sounding) {
			/* SU sounding */
			if ((check_fwstate(mlme, WIFI_ADHOC_STATE) == _TRUE)
			    || (check_fwstate(mlme, WIFI_ADHOC_MASTER_STATE) == _TRUE))
				BeamCtrlVal = entry->mac_id;
			else
				BeamCtrlVal = entry->p_aid;

			if (entry->su_reg_index == 0) {
				BeamCtrlReg = REG_TXBF_CTRL_8822B;
			} else {
				BeamCtrlReg = REG_TXBF_CTRL_8822B+2;
				BeamCtrlVal |= ((BIT_R_EN_NDPA_INT_8822B | BIT_USE_NDPA_PARAMETER_8822B | BIT_R_ENABLE_NDPA_8822B) >> 16);
			}

			/* Current BFee sound down */
			if (entry->bDeleteSounding
			    || (cfg_type == HW_CFG_SOUNDING_TYPE_LEAVE)) {
				RTW_INFO("%s: SU HW_CFG_SOUNDING_TYPE_LEAVE\n", __FUNCTION__);
				RTW_INFO("%s: Don't apply Vmatrix\n", __FUNCTION__);
				BeamCtrlVal &= ~(BIT(9)|BIT(10)|BIT(11));
			} else {
				if (entry->sound_bw == CHANNEL_WIDTH_20)
					BeamCtrlVal |= BIT(9);
				else if (entry->sound_bw == CHANNEL_WIDTH_40)
					BeamCtrlVal |= (BIT(9)|BIT(10));
				else if (entry->sound_bw == CHANNEL_WIDTH_80)
					BeamCtrlVal |= (BIT(9)|BIT(10)|BIT(11));
			}

			rtw_write16(adapter, BeamCtrlReg, BeamCtrlVal);
		} else {
			/* MU sounding */
			if (cfg_type == HW_CFG_SOUNDING_TYPE_LEAVE) {
				RTW_INFO("%s: MU HW_CFG_SOUNDING_TYPE_LEAVE\n", __FUNCTION__);

				/* Clear the entry table */
				val32 = rtw_read32(adapter, 0x14C0);
				if (TEST_FLAG(entry->cap, BEAMFORMEE_CAP_VHT_MU)) {
					for (i = 0; i < 8; i++)
						entry->gid_valid[i] = 0;

					val32 &= ~(BIT(8)|BIT(9)|BIT(10));
					val32 |= ((i<<8) & (BIT(8)|BIT(9)|BIT(10)));
					rtw_write32(adapter, 0x14C0, val32);
					/* Set MU STA gid valid table */
					rtw_write32(adapter, 0x14C4, 0);
				} else {
					RTW_ERR("%s: ERROR! It is not an MU BFee entry!!\n",  __FUNCTION__);
				}

				if (bf_info->beamformee_su_cnt == 0) {
					/* Disable TxMU PPDU */
					val32 &= ~BIT(7);
					rtw_write32(adapter, 0x14C0, val32);
				}
			} else if (cfg_type == HW_CFG_SOUNDING_TYPE_SOUNDDOWN) {
				RTW_INFO("%s: MU HW_CFG_SOUNDING_TYPE_SOUNDDOWN\n",  __FUNCTION__);

				/* Update all MU entry table */
				do {
					/* Check BB GID bitmap ready */
					val32 = PHY_QueryBBReg(adapter, 0xF4C, 0xFFFF0000);

					is_bitmap_ready = (val32 & BIT(15)) ? _TRUE : _FALSE;
					count++;
					rtw_udelay_os(5);
				} while ((_FALSE == is_bitmap_ready) && (count < 100));

				bitmap = (u16)(val32 & 0x3FFF);

				for (i = 0; i < 15; i++) {
					if (i < 5) {/*bit0~4*/
						id0 = 0;
						id1 = (u8)(i + 1);
					} else if (i < 9) { /*bit5~8*/
						id0 = 1;
						id1 = (u8)(i - 3);
					} else if (i < 12) { /*bit9~11*/
						id0 = 2;
						id1 = (u8)(i - 6);
					} else if (i < 14) { /*bit12~13*/
						id0 = 3;
						id1 = (u8)(i - 8);
					} else { /*bit14*/
						id0 = 4;
						id1 = (u8)(i - 9);
					}
					if (bitmap & BIT(i)) {
						/*Pair 1*/
						gid = (i << 1) + 1;
						gid_valid[id0] |= (BIT(gid));
						gid_valid[id1] |= (BIT(gid));
						/*Pair 2*/
						gid += 1;
						gid_valid[id0] |= (BIT(gid));
						gid_valid[id1] |= (BIT(gid));
					} else {
						/*Pair 1*/
						gid = (i << 1) + 1;
						gid_valid[id0] &= ~(BIT(gid));
						gid_valid[id1] &= ~(BIT(gid));
						/*Pair 2*/
						gid += 1;
						gid_valid[id0] &= ~(BIT(gid));
						gid_valid[id1] &= ~(BIT(gid));
					}
				}

				for (i = 0; i < MAX_BEAMFORMEE_ENTRY_NUM; i++) {
					entry = &bf_info->bfee_entry[i];
					if (TEST_FLAG(entry->cap, BEAMFORMEE_CAP_VHT_MU)
					    && (entry->mu_reg_index < 6)) {
						val32 = gid_valid[entry->mu_reg_index];
						for (j = 0; j < 4; j++) {
							entry->gid_valid[j] = (u8)(val32 & 0xFF);
							val32 >>= 8;
						}
					}
				}

				val32 = rtw_read32(adapter, 0x14C0);
				for (i = 0; i < 6; i++) {
					val32 &= ~(BIT8|BIT9|BIT10);
					val32 |= ((i<<8) & (BIT8|BIT9|BIT10));
					rtw_write32(adapter, 0x14C0, val32);
					/* Set MU STA gid valid table */
					rtw_write32(adapter, 0x14C4, gid_valid[i]);
				}

				/* Enable TxMU PPDU */
				val32 |= BIT(7);
				rtw_write32(adapter, 0x14C0, val32);
			}
		}
	}
}

void rtl8822b_phy_sounding_enter(PADAPTER adapter, struct sta_info *sta)
{
	PHAL_DATA_TYPE hal;
	struct mlme_priv *mlme;
	struct beamforming_info *bf_info;
	struct beamformer_entry *bfer;
	struct beamformee_entry *bfee;
	u8 cnt_bfee_su = 0, cnt_bfee_mu = 0;
	u8 i = 0, idx;
	u32 csi_param;
	u16 p_aid = 0;
	/* MU */
	u8 nc_index = 0, nr_index = 0, grouping = 0, codebookinfo = 0, coefficientsize = 0;
	u32 gid_valid = 0, user_position_l = 0, user_position_h = 0;
	u32 mu_reg[6] = {0x1684, 0x1686, 0x1688, 0x168A, 0x168C, 0x168E};
	/* General */
	u8 val8;
	u16 val16;
	u32 val32;


	RTW_INFO("+%s\n", __FUNCTION__);

	hal = GET_HAL_DATA(adapter);
	mlme = &adapter->mlmepriv;
	bf_info = GET_BEAMFORM_INFO(adapter);

	bfee = beamforming_get_bfee_entry_by_addr(adapter, sta->hwaddr);
	bfer = beamforming_get_bfer_entry_by_addr(adapter, sta->hwaddr);

	if (bfer) {
		bfer->state = BEAMFORM_ENTRY_HW_STATE_ADDING;

		if (TEST_FLAG(bfer->cap, BEAMFORMER_CAP_VHT_MU)) {
			/* Config MU BFer entry */
			RTW_INFO("%s: Config MU BFer entry HW setting\n", __FUNCTION__);

			/* Reset GID table */
			for (i = 0; i < 8; i++)
				bfer->gid_valid[i] = 0;
			for (i = 0; i < 16; i++)
				bfer->user_position[i] = 0;

			/* Sounding protocol control */
			rtw_write8(adapter, REG_SND_PTCL_CTRL_8822B, 0xDB);

			/* MAC address */
			for (i = 0; i < ETH_ALEN; i++)
				rtw_write8(adapter, REG_ASSOCIATED_BFMER0_INFO_8822B+i, bfer->mac_addr[i]);

			/* Set partial AID */
			rtw_write16(adapter, REG_ASSOCIATED_BFMER0_INFO_8822B+6, bfer->p_aid);

			/*
			 * Fill our AID to 0x1680[11:0] and
			 * [13:12] = 2b'00, BF report segement select to 3895 bytes
			 */
			val16 = rtw_read16(adapter, 0x1680);
			val16 &= ~0x3FFF;
			val16 |= (bfer->aid & 0xFFF);
			rtw_write16(adapter, 0x1680, val16);

			/* Set 80us for leaving ndp_rx_standby_state */
			rtw_write8(adapter, 0x71B, 0x50);

			/* Set 0x6A0[14] = 1 to accept action_no_ack */
			val8 = rtw_read8(adapter, REG_RXFLTMAP0_8822B+1);
			val8 |= 0x40;
			rtw_write8(adapter, REG_RXFLTMAP0_8822B+1, val8);

			/* Set 0x6A2[5:4] = 1 to NDPA and BF report poll */
			val8 = rtw_read8(adapter, REG_RXFLTMAP1_8822B);
			val8 |= 0x30;
			rtw_write8(adapter, REG_RXFLTMAP1_8822B, val8);

			/* CSI report parameters of Beamformer */
			nc_index = _get_txbf_nrx(adapter);
			/*
			 * 0x718[7] = 1 use Nsts
			 * 0x718[7] = 0 use reg setting
			 * As Bfee, we use Nsts, so nr_index don't care
			 */
			nr_index = 1;
			grouping = 0; /* no grouping */
			codebookinfo = 1; /* 7 bit for psi, 9 bit for phi */
			coefficientsize = 0; /* This is nothing really matter */
			csi_param = (u16)((coefficientsize<<10)|(codebookinfo<<8)|(grouping<<6)|(nr_index<<3)|(nc_index));
			rtw_write16(adapter, REG_TX_CSI_RPT_PARAM_BW20_8822B, csi_param);
			RTW_INFO("%s: nc=%d nr=%d group=%d codebookinfo=%d coefficientsize=%d\n",
			__FUNCTION__, nc_index, nr_index, grouping, codebookinfo, coefficientsize);
			RTW_INFO("%s: csi=0x%04x\n", __FUNCTION__, csi_param);

			/*for B-Cut*/
			PHY_SetBBReg(adapter, REG_RXFLTMAP0_8822B, BIT(20), 0);
			PHY_SetBBReg(adapter, REG_RXFLTMAP3_8822B, BIT(20), 0);
		} else if (TEST_FLAG(bfer->cap, BEAMFORMER_CAP_VHT_SU|BEAMFORMER_CAP_HT_EXPLICIT)) {
			/* Config SU BFer entry */
			RTW_INFO("%s: Config SU BFer entry HW setting.\n", __FUNCTION__);

			/* Sounding protocol control */
			rtw_write8(adapter, REG_SND_PTCL_CTRL_8822B, 0xDB);

			/* MAC address/Partial AID of Beamformer */
			if (bfer->su_reg_index == 0)
				for (i = 0; i < 6; i++)
					rtw_write8(adapter, (REG_ASSOCIATED_BFMER0_INFO_8822B+i), bfer->mac_addr[i]);
			else
				for (i = 0; i < 6; i++)
					rtw_write8(adapter, (REG_ASSOCIATED_BFMER1_INFO_8822B+i), bfer->mac_addr[i]);

			/* CSI report parameters of Beamformer */
			nc_index = _get_txbf_nrx(adapter);
			/*
			 * 0x718[7] = 1 use Nsts
			 * 0x718[7] = 0 use reg setting
			 * As Bfee, we use Nsts, so nr_index don't care
			 */
			nr_index = bfer->NumofSoundingDim;

			grouping = 0;

			/* for ac = 1, for n = 3 */
			if (TEST_FLAG(bfer->cap, BEAMFORMER_CAP_VHT_SU))
				codebookinfo = 1;
			else if (TEST_FLAG(bfer->cap, BEAMFORMER_CAP_HT_EXPLICIT))
				codebookinfo = 3;

			coefficientsize = 3;

			csi_param = (u16)((coefficientsize<<10)|(codebookinfo<<8)|(grouping<<6)|(nr_index<<3)|(nc_index));

			if (bfer->su_reg_index == 0)
				rtw_write16(adapter, REG_TX_CSI_RPT_PARAM_BW20_8822B, csi_param);
			else
				rtw_write16(adapter, REG_TX_CSI_RPT_PARAM_BW20_8822B+2, csi_param);
			/* ndp_rx_standby_timer */
			rtw_write8(adapter, REG_SND_PTCL_CTRL_8822B+3, 0x70);
		}

		bfer->state = BEAMFORM_ENTRY_HW_STATE_ADDED;
	}

	if (bfee) {
		bfee->state = BEAMFORM_ENTRY_HW_STATE_ADDING;

		if (TEST_FLAG(bfee->cap, BEAMFORMEE_CAP_VHT_MU)) {
			idx = bfee->mu_reg_index;
			/* User position table */
			switch (idx) {
			case 0:
				gid_valid = 0x7fe;
				user_position_l = 0x111110;
				user_position_h = 0x0;
				break;
			case 1:
				gid_valid = 0x7f806;
				user_position_l = 0x11000004;
				user_position_h = 0x11;
				break;
			case 2:
				gid_valid = 0x1f81818;
				user_position_l = 0x400040;
				user_position_h = 0x11100;
				break;
			case 3:
				gid_valid = 0x1e186060;
				user_position_l = 0x4000400;
				user_position_h = 0x1100040;
				break;
			case 4:
				gid_valid = 0x66618180;
				user_position_l = 0x40004000;
				user_position_h = 0x10040400;
				break;
			case 5:
				gid_valid = 0x79860600;
				user_position_l = 0x40000;
				user_position_h = 0x4404004;
				break;
			}

			for (i = 0; i < 8; i++) {
				if (i < 4) {
					bfee->gid_valid[i] = (u8)(gid_valid & 0xFF);
					gid_valid = (gid_valid >> 8);
				} else
					bfee->gid_valid[i] = 0;
			}
			for (i = 0; i < 16; i++) {
				if (i < 4)
					bfee->user_position[i] = (u8)((user_position_l >> (i*8)) & 0xFF);
				else if (i < 8)
					bfee->user_position[i] = (u8)((user_position_h >> ((i-4)*8)) & 0xFF);
				else
					bfee->user_position[i] = 0;
			}

			/* Sounding protocol control */
			rtw_write8(adapter, REG_SND_PTCL_CTRL_8822B, 0xDB);

			/* select MU STA table */
			val32 = rtw_read32(adapter, 0x14C0);
			val32 &= ~(BIT(8)|BIT(9)|BIT(10));
			val32 |= (idx << 8) & (BIT(8)|BIT(9)|BIT(10));
			rtw_write32(adapter, 0x14C0, val32);

			/* Reset gid_valid table */
			rtw_write32(adapter, 0x14C4, 0);
			rtw_write32(adapter, 0x14C8 , user_position_l);
			rtw_write32(adapter, 0x14CC , user_position_h);

			/* set validity of MU STAs */
			val32 &= 0xFFFFFFC0;
			val32 |= bf_info->beamformee_mu_reg_maping & 0x3F;
			rtw_write32(adapter, 0x14C0, val32);

			RTW_INFO("%s: RegMUTxCtrl=0x%x, user_position_l=0x%x, user_position_h=0x%x\n",
				__FUNCTION__, val32, user_position_l, user_position_h);

			val16 = rtw_read16(adapter, mu_reg[idx]);
			val16 &= 0xFE00; /* Clear PAID */
			val16 |= BIT(9); /* Enable MU BFee */
			val16 |= bfee->p_aid;
			rtw_write16(adapter, mu_reg[idx], val16);
			RTW_INFO("%s: Write mu_reg 0x%x = 0x%x\n",
				__FUNCTION__, mu_reg[idx], val16);

			/* 0x42C[30] = 1 (0: from Tx desc, 1: from 0x45F) */
			val8 = rtw_read8(adapter, REG_TXBF_CTRL_8822B+3);
			val8 |= 0xD0; /* Set bit 28, 30, 31 to 3b'111 */
			rtw_write8(adapter, REG_TXBF_CTRL_8822B+3, val8);
			/* Set NDPA to 6M*/
			rtw_write8(adapter, REG_NDPA_RATE_8822B, 0x4); /* 6M */

			val8 = rtw_read8(adapter, REG_NDPA_OPT_CTRL_8822B);
			val8 &= 0xFC; /* Clear bit 0, 1 */
			rtw_write8(adapter, REG_NDPA_OPT_CTRL_8822B, val8);

			val32 = rtw_read32(adapter, REG_SND_PTCL_CTRL_8822B);
			val32 = ((val32 & 0xFF0000FF) | 0x020200); /* Set [23:8] to 0x0202 */
			rtw_write32(adapter, REG_SND_PTCL_CTRL_8822B, val32);

			/* Set 0x6A0[14] = 1 to accept action_no_ack */
			val8 = rtw_read8(adapter, REG_RXFLTMAP0_8822B+1);
			val8 |= 0x40;
			rtw_write8(adapter, REG_RXFLTMAP0_8822B+1, val8);
			/* End of MAC registers setting */

			_setbeamformrfmode(adapter, bf_info->beamformee_su_cnt, bf_info->beamformee_mu_cnt, 0);

			/* <tynli_mark> <TODO> Need to set timer 2015.12.23 */
			/* Special for plugfest */
			rtw_mdelay_os(50); /* wait for 4-way handshake ending */
			beamforming_send_vht_gid_mgnt_packet(adapter, bfee);

#if 1 /* Fix rate. <tynli_note> Do we need to keep this action? */
			/* Set Ctrl Info */
			rtw_write16(adapter, 0x140, 0x660);

			/* Reset Ctrl Info */
			val32 = 0x8000 + 32 * bfee->mac_id;
			for (i = 0; i < 8; i++)
				rtw_write32(adapter, val32 + i*4, 0);

			rtw_write32(adapter, val32, (idx + 1) << 16);
			rtw_write8(adapter, 0x81, 0x80); /* RPTBUF ready */

			RTW_INFO("%s: mac_id=%d, ctrl_info_offset=0x%x, mu_reg_index=%x\n",
				__FUNCTION__, bfee->mac_id, val32, idx);
#endif
		} else if (TEST_FLAG(bfee->cap, BEAMFORMEE_CAP_VHT_SU|BEAMFORMEE_CAP_HT_EXPLICIT)) {
			RTW_INFO("%s: Config SU BFee entry HW setting\n", __FUNCTION__);

			idx = bfee->su_reg_index;

			if ((check_fwstate(mlme, WIFI_ADHOC_STATE) == _TRUE)
			    || (check_fwstate(mlme, WIFI_ADHOC_MASTER_STATE) == _TRUE))
				p_aid = bfee->mac_id;
			else
				p_aid = bfee->p_aid;

			_setbeamformrfmode(adapter, bf_info->beamformee_su_cnt, bf_info->beamformee_mu_cnt, idx);

			/* P_AID of Beamformee & enable NDPA transmission & enable NDPA interrupt */
			if (idx == 0) {
				rtw_write16(adapter, REG_TXBF_CTRL_8822B, p_aid);
				val8 = rtw_read8(adapter, REG_TXBF_CTRL_8822B+3);
				val8 |= ((BIT_R_EN_NDPA_INT_8822B | BIT_USE_NDPA_PARAMETER_8822B | BIT_R_ENABLE_NDPA_8822B) >> 24);
				rtw_write8(adapter, REG_TXBF_CTRL_8822B+3, val8);
			} else {
				val16 = p_aid;
				val16 |= ((BIT_R_EN_NDPA_INT_8822B | BIT_USE_NDPA_PARAMETER_8822B | BIT_R_ENABLE_NDPA_8822B) >> 16);
				rtw_write16(adapter, REG_TXBF_CTRL_8822B+2, val16);
			}

			/* CSI report parameters of Beamformee */
			if (idx == 0) {
				/* Get BIT24 & BIT(25)=BIT_TXUSER_ID1_8822B */
				val8 = rtw_read8(adapter, REG_ASSOCIATED_BFMEE_SEL_8822B+3) & 0x3;
				val8 |= 0x60;
				rtw_write8(adapter, REG_ASSOCIATED_BFMEE_SEL_8822B+3, val8);

				val16 = p_aid | BIT_TXUSER_ID0_8822B;
				rtw_write16(adapter, REG_ASSOCIATED_BFMEE_SEL_8822B, val16);
			} else {
				/* Set BIT(25)=BIT_TXUSER_ID1_8822B */
				val16 = p_aid | 0xE200;
				rtw_write16(adapter, REG_ASSOCIATED_BFMEE_SEL_8822B+2, val16);
			}
		}

		bfee->state = BEAMFORM_ENTRY_HW_STATE_ADDED;
	}

	RTW_INFO("-%s\n", __FUNCTION__);
}

void rtl8822b_phy_sounding_leave(PADAPTER adapter, u8 *addr)
{
	PHAL_DATA_TYPE hal;
	struct beamforming_info *bf_info;
	struct beamformer_entry *bfer_entry;
	struct beamformee_entry *bfee_entry;
	u8 cnt_bfee_su = 0, cnt_bfee_mu = 0;
	u8 i = 0, idx;
	u16 p_aid = 0;
	/* MU */
	u32 gid_valid, user_position_l, user_position_h;
	u32 mu_reg[6] = {0x1684, 0x1686, 0x1688, 0x168a, 0x168c, 0x168e};
	/* General */
	u8 val8;
	u16 val16;
	u32 val32;


	RTW_INFO("+%s\n", __FUNCTION__);

	hal = GET_HAL_DATA(adapter);
	bf_info = GET_BEAMFORM_INFO(adapter);

	bfee_entry = beamforming_get_bfee_entry_by_addr(adapter, addr);
	bfer_entry = beamforming_get_bfer_entry_by_addr(adapter, addr);

	/* Clear P_AID of Beamformee */
	/* Clear MAC address of Beamformer */
	/* Clear Associated Bfmee Sel */
	if (bfer_entry) {
		bfer_entry->state = BEAMFORM_ENTRY_HW_STATE_DELETING;

		rtw_write8(adapter, REG_SND_PTCL_CTRL_8822B, 0xD8);

		if (TEST_FLAG(bfer_entry->cap, BEAMFORMER_CAP_VHT_MU)) {
			/* MU BFer entry */
			/* Clear validity of MU STA0 and MU STA1*/
			val32 = rtw_read32(adapter, 0x14C0);
			val32 &= 0xFFFFFFC0;
			rtw_write32(adapter, 0x14C0, val32);

			RTW_INFO("%s: Clear MU BFer entry HW setting\n", __FUNCTION__);
		} else if (TEST_FLAG(bfer_entry->cap, BEAMFORMEE_CAP_VHT_SU|BEAMFORMEE_CAP_HT_EXPLICIT)) {
			/* SU BFer entry */
			idx = bfer_entry->su_reg_index;
			if (idx == 0) {
				rtw_write32(adapter, REG_ASSOCIATED_BFMER0_INFO_8822B, 0);
				rtw_write16(adapter, REG_ASSOCIATED_BFMER0_INFO_8822B+4, 0);
				rtw_write16(adapter, REG_TX_CSI_RPT_PARAM_BW20_8822B, 0);
			} else {
				rtw_write32(adapter, REG_ASSOCIATED_BFMER1_INFO_8822B, 0);
				rtw_write16(adapter, REG_ASSOCIATED_BFMER1_INFO_8822B+4, 0);
				rtw_write16(adapter, REG_TX_CSI_RPT_PARAM_BW20_8822B+2, 0);
			}

			bf_info->beamformer_su_reg_maping &= ~BIT(idx);
			bfer_entry->su_reg_index = 0xFF;

			RTW_INFO("%s: Clear SU BFer entry HW setting\n", __FUNCTION__);
		}

		bfer_entry->used = _FALSE;
		bfer_entry->state = BEAMFORM_ENTRY_HW_STATE_NONE;
		bfer_entry->cap = BEAMFORMING_CAP_NONE;
	}

	if (bfee_entry) {
		bfee_entry->state = BEAMFORM_ENTRY_HW_STATE_DELETING;

		_setbeamformrfmode(adapter, bf_info->beamformee_su_cnt, bf_info->beamformee_mu_cnt, bfee_entry->su_reg_index);

		if (TEST_FLAG(bfee_entry->cap, BEAMFORMER_CAP_VHT_MU)) {
			/*MU BFee */
			idx = bfee_entry->mu_reg_index;

			/* Disable sending NDPA & BF-rpt-poll to this BFee */
			rtw_write16(adapter, mu_reg[idx] , 0);
			/* Set validity of MU STA */
			val32 = rtw_read32(adapter, 0x14C0);
			val32 &= ~(BIT(idx));
			rtw_write32(adapter, 0x14C0, val32);

			/* Force disable sounding config */
			_config_sounding(adapter, bfee_entry, _TRUE, HW_CFG_SOUNDING_TYPE_LEAVE);

			bf_info->beamformee_mu_reg_maping &= ~BIT(idx);
			bfee_entry->mu_reg_index = 0xFF;

			RTW_INFO("%s: Clear MU BFee entry HW setting\n", __FUNCTION__);
		} else if (TEST_FLAG(bfee_entry->cap, BEAMFORMER_CAP_VHT_SU|BEAMFORMER_CAP_HT_EXPLICIT)) {
			/* SU BFee */
			idx = bfee_entry->su_reg_index;

			if (idx == 0) {
				rtw_write16(adapter, REG_TXBF_CTRL_8822B, 0x0);
				val8 = rtw_read8(adapter, REG_TXBF_CTRL_8822B+3);
				val8 |= ((BIT_R_EN_NDPA_INT_8822B | BIT_USE_NDPA_PARAMETER_8822B | BIT_R_ENABLE_NDPA_8822B) >> 24);
				rtw_write8(adapter, REG_TXBF_CTRL_8822B+3, val8);
				rtw_write16(adapter, REG_ASSOCIATED_BFMEE_SEL_8822B, 0);
			} else {
				val16 = 0x0;
				val16 |= ((BIT_R_EN_NDPA_INT_8822B | BIT_USE_NDPA_PARAMETER_8822B | BIT_R_ENABLE_NDPA_8822B) >> 16);
				rtw_write16(adapter, REG_TXBF_CTRL_8822B+2, val16);

				val16 = rtw_read16(adapter, REG_ASSOCIATED_BFMEE_SEL_8822B+2);
				val16 &= 0x60;
				rtw_write16(adapter, REG_ASSOCIATED_BFMEE_SEL_8822B+2, val16);
			}

			/* Force disable sounding config */
			_config_sounding(adapter, bfee_entry, _FALSE, HW_CFG_SOUNDING_TYPE_LEAVE);

			bf_info->beamformee_su_reg_maping &= ~BIT(idx);
			bfee_entry->su_reg_index = 0xFF;

			RTW_INFO("%s: Clear SU BFee entry HW setting\n", __FUNCTION__);
		}
	}

	RTW_INFO("-%s\n", __FUNCTION__);
}

void rtl8822b_phy_sounding_set_gid_table(PADAPTER adapter, struct beamformer_entry *bfer)
{
	struct beamforming_info *bf_info;
	u32 beamctrlreg;
	u16 beamctrlval;
	/* MU */
	u32 gid_valid, user_position_l, user_position_h;
	u32 mu_reg[6] = {0x1684, 0x1686, 0x1688, 0x168a, 0x168c, 0x168e};
	/* General */
	u8 val8;
	u16 val16;
	u32 val32;
	int i;


	bf_info = GET_BEAMFORM_INFO(adapter);

	/* For GID 0~31 */
	gid_valid = 0;
	user_position_l = 0;
	user_position_h = 0;
	for (i = 0; i < 4; i++)
		gid_valid |= (bfer->gid_valid[i] << (i << 3));
	for (i = 0; i < 8; i++) {
		if (i < 4)
			user_position_l |= (bfer->user_position[i] << (i << 3));
		else
			user_position_h |= (bfer->user_position[i] << ((i - 4) << 3));
	}
	/* select MU STA0 table */
	val32 = rtw_read32(adapter, 0x14C0);
	val32 &= ~(BIT(8)|BIT(9)|BIT(10));
	rtw_write32(adapter, 0x14C0, val32);
	rtw_write32(adapter, 0x14C4, gid_valid); 
	rtw_write32(adapter, 0x14C8, user_position_l);
	rtw_write32(adapter, 0x14CC, user_position_h);

	RTW_INFO("%s: STA0: gid_valid=0x%x, user_position_l=0x%x, user_position_h=0x%x\n",
		__FUNCTION__, gid_valid, user_position_l, user_position_h);

	/* For GID 32~64 */
	gid_valid = 0;
	user_position_l = 0;
	user_position_h = 0;
	for (i = 4; i < 8; i++)
		gid_valid |= (bfer->gid_valid[i] << ((i - 4)<<3));
	for (i = 8; i < 16; i++) {
		if (i < 12)
			user_position_l |= (bfer->user_position[i] << ((i - 8) << 3));
		else
			user_position_h |= (bfer->user_position[i] << ((i - 12) << 3));
	}
	/* select MU STA1 table */
	val32 = rtw_read32(adapter, 0x14C0);
	val32 &= ~(BIT(8)|BIT(9)|BIT(10));
	val32 |= BIT(8);
	rtw_write32(adapter, 0x14C0, val32);
	rtw_write32(adapter, 0x14C4, gid_valid); 
	rtw_write32(adapter, 0x14C8, user_position_l);
	rtw_write32(adapter, 0x14CC, user_position_h);
	
	RTW_INFO("%s: STA1: gid_valid=0x%x, user_position_l=0x%x, user_position_h=0x%x\n",
		__FUNCTION__, gid_valid, user_position_l, user_position_h);

	/* Set validity of MU STA0 and MU STA1 */
	val32 = rtw_read32(adapter, 0x14C0);
	val32 &= 0xFFFFFFC0;
	val32 |= 0x3; /* STA0, STA1 */
	rtw_write32(adapter, 0x14C0, val32);
}
#endif /* CONFIG_BEAMFORMING */

