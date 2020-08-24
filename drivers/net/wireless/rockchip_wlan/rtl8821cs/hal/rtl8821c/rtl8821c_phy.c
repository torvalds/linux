/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
#define _RTL8821C_PHY_C_

#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../hal_halmac.h"	/* REG_CCK_CHECK_8821C */
#include "rtl8821c.h"


/*
 * Description:
 *	Initialize Register definition offset for Radio Path A/B/C/D
 *	The initialization value is constant and it should never be changes
 */
static void bb_rf_register_definition(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	/* RF Interface Sowrtware Control */
	hal->PHYRegDef[RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW;
	hal->PHYRegDef[RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW;

	/* RF Interface Output (and Enable) */
	hal->PHYRegDef[RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE;
	hal->PHYRegDef[RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE;

	/* RF Interface (Output and) Enable */
	hal->PHYRegDef[RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE;
	hal->PHYRegDef[RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE;

	hal->PHYRegDef[RF_PATH_A].rf3wireOffset = rA_LSSIWrite_Jaguar;
	hal->PHYRegDef[RF_PATH_B].rf3wireOffset = rB_LSSIWrite_Jaguar;

	hal->PHYRegDef[RF_PATH_A].rfHSSIPara2 = rHSSIRead_Jaguar;
	hal->PHYRegDef[RF_PATH_B].rfHSSIPara2 = rHSSIRead_Jaguar;

	/* Tranceiver Readback LSSI/HSPI mode */
	hal->PHYRegDef[RF_PATH_A].rfLSSIReadBack = rA_SIRead_Jaguar;
	hal->PHYRegDef[RF_PATH_B].rfLSSIReadBack = rB_SIRead_Jaguar;
	hal->PHYRegDef[RF_PATH_A].rfLSSIReadBackPi = rA_PIRead_Jaguar;
	hal->PHYRegDef[RF_PATH_B].rfLSSIReadBackPi = rB_PIRead_Jaguar;
}

static void init_bb_rf(PADAPTER adapter)
{
	u8 val8;
	u16 val16;


	/* Enable BB and RF */
	val8 = rtw_read8(adapter, REG_SYS_FUNC_EN_8821C);
	if (IS_HARDWARE_TYPE_8821CU(adapter))
		val8 |= BIT_FEN_USBA_8821C;
	else if (IS_HARDWARE_TYPE_8821CE(adapter))
		val8 |= BIT_FEN_PCIEA_8821C;
	rtw_write8(adapter, REG_SYS_FUNC_EN_8821C, val8);

	/*
	 * 8821C MP Chip => Reset BB/RF ??
	 * Need to set BBRSTB and GLB_RSTB = 1->0->1 to generate a postive edge and negtive edge for BB
	 */
	val8 |= BIT_FEN_BB_GLB_RSTN_8821C | BIT_FEN_BBRSTB_8821C;
	rtw_write8(adapter, REG_SYS_FUNC_EN_8821C, val8);
	val8 &= ~(BIT_FEN_BB_GLB_RSTN_8821C | BIT_FEN_BBRSTB_8821C);
	rtw_write8(adapter, REG_SYS_FUNC_EN_8821C, val8);
	val8 |= BIT_FEN_BB_GLB_RSTN_8821C | BIT_FEN_BBRSTB_8821C;
	rtw_write8(adapter, REG_SYS_FUNC_EN_8821C, val8);

	val8 = BIT_RF_EN_8821C | BIT_RF_RSTB_8821C | BIT_RF_SDMRSTB_8821C;
	/* 0x1F[7:0] = 0x07 PathA RF Power On */
	rtw_write8(adapter, REG_RF_CTRL_8821C, val8);
	rtw_usleep_os(10);

	/*0xEC [31:24],BIT_WLRF1_CTRL,	For WLRF1 control*/
	/* 0xEF[7:0] = 0x07 for RFE Type=2,BTG RF Power On*/
	rtw_write8(adapter, REG_WLRF1_8821C + 3, val8);
	rtw_usleep_os(10);

}

u8 rtl8821c_init_phy_parameter_mac(PADAPTER adapter)
{
	u8 ret = _FAIL;
	PHAL_DATA_TYPE hal;

	hal = GET_HAL_DATA(adapter);

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	ret = phy_ConfigMACWithParaFile(adapter, PHY_FILE_MAC_REG);
	if (ret == _FAIL)
#endif /* CONFIG_LOAD_PHY_PARA_FROM_FILE */
	{
		odm_config_mac_with_header_file(&hal->odmpriv);
		ret = _SUCCESS;
	}

	return ret;
}

static u8 _init_phy_parameter_bb(PADAPTER Adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(Adapter);
	u8 ret = _TRUE;
	int res;
	enum hal_status status;


	/*
	 * 1. Read PHY_REG.TXT BB INIT!!
	 */
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	res = phy_ConfigBBWithParaFile(Adapter, PHY_FILE_PHY_REG, CONFIG_BB_PHY_REG);
	if (res == _FAIL)
#endif
	{
		ret = _FALSE;
		status = odm_config_bb_with_header_file(&hal->odmpriv, CONFIG_BB_PHY_REG);
		if (HAL_STATUS_SUCCESS == status)
			ret = _TRUE;
	}

	if (ret != _TRUE) {
		RTW_INFO("%s: Write BB Reg Fail!!", __FUNCTION__);
		goto exit;
	}

#ifdef CONFIG_MP_INCLUDED
	if (Adapter->registrypriv.mp_mode == 1) {
		/*
		 * 1.1 Read PHY_REG_MP.TXT BB INIT!!
		 */
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
		res = phy_ConfigBBWithMpParaFile(Adapter, PHY_FILE_PHY_REG_MP);
		if (res == _FAIL)
#endif
		{
			ret = _FALSE;
			status = odm_config_bb_with_header_file(&hal->odmpriv, CONFIG_BB_PHY_REG_MP);
			if (HAL_STATUS_SUCCESS == status)
				ret = _TRUE;
		}

		if (ret != _TRUE) {
			RTW_INFO("%s : Write BB Reg MP Fail!!", __FUNCTION__);
			goto exit;
		}
	}
#endif /* CONFIG_MP_INCLUDED */

	/*
	 * 2. Read BB AGC table Initialization
	 */
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	res = phy_ConfigBBWithParaFile(Adapter, PHY_FILE_AGC_TAB, CONFIG_BB_AGC_TAB);
	if (res == _FAIL)
#endif
	{
		ret = _FALSE;
		status = odm_config_bb_with_header_file(&hal->odmpriv, CONFIG_BB_AGC_TAB);
		if (HAL_STATUS_SUCCESS == status)
			ret = _TRUE;
	}

	if (ret != _TRUE) {
		RTW_INFO("%s: AGC Table Fail\n", __FUNCTION__);
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
	ret = _init_phy_parameter_bb(adapter);

	if (rtw_phydm_set_crystal_cap(adapter, hal->crystal_cap) == _FALSE) {
		RTW_ERR("Init crystal_cap failed\n");
		rtw_warn_on(1);
		ret = _FALSE;
	}

	phy_set_bb_reg(adapter, rCCK0_FalseAlarmReport, BIT18 | BIT22, 0);
	return ret;
}

static u8 _init_phy_parameter_rf(PADAPTER adapter)
{
	u32 val32 = 0;
	enum rf_path eRFPath;
	PBB_REGISTER_DEFINITION_T pPhyReg;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	enum hal_status status;
	int res;
	u8 ret = _TRUE;


	/*
	 * Initialize RF
	 */
	for (eRFPath = RF_PATH_A; eRFPath < hal_spec->rf_reg_path_num; eRFPath++) {
		pPhyReg = &hal->PHYRegDef[eRFPath];

		/* Initialize RF from configuration file */
		switch (eRFPath) {
		case RF_PATH_A:
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
			res = PHY_ConfigRFWithParaFile(adapter, PHY_FILE_RADIO_A, eRFPath);
			if (res == _FAIL)
#endif
			{
				ret = _FALSE;
				status = odm_config_rf_with_header_file(&hal->odmpriv, CONFIG_RF_RADIO, eRFPath);
				if (HAL_STATUS_SUCCESS == status)
					ret = _TRUE;
			}
			break;
		case RF_PATH_B:
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
			res = PHY_ConfigRFWithParaFile(adapter, PHY_FILE_RADIO_B, eRFPath);
			if (res == _FAIL)
#endif
			{
				ret = _FALSE;
				status = odm_config_rf_with_header_file(&hal->odmpriv, CONFIG_RF_RADIO, eRFPath);
				if (HAL_STATUS_SUCCESS == status)
					ret = _TRUE;
			}
			break;
		default:
			RTW_INFO("Unknown RF path!! %d\r\n", eRFPath);
			break;
		}

		if (ret != _TRUE)
			goto exit;
	}

	/*
	 * Configuration of Tx Power Tracking
	 */
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	res = PHY_ConfigRFWithTxPwrTrackParaFile(adapter, PHY_FILE_TXPWR_TRACK);
	if (res == _FAIL)
#endif
	{
		ret = _FALSE;
		status = odm_config_rf_with_tx_pwr_track_header_file(&hal->odmpriv);
		if (HAL_STATUS_SUCCESS == status)
			ret = _TRUE;
	}
	if (ret != _TRUE)
		goto exit;

exit:
	return ret;
}

static u8 init_rf_reg(PADAPTER adapter)
{
	u8 ret = _TRUE;


	ret = _init_phy_parameter_rf(adapter);

	return ret;
}
u8 rtl8821c_phy_init(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct dm_struct *phydm = &hal->odmpriv;

	bb_rf_register_definition(adapter);

	init_bb_rf(adapter);

	if (_FALSE == config_phydm_parameter_init_8821c(phydm, ODM_PRE_SETTING))
		return _FALSE;

	if (_FALSE == init_bb_reg(adapter))
		return _FALSE;

	if (_FALSE == init_rf_reg(adapter))
		return _FALSE;

	if (_FALSE ==  config_phydm_parameter_init_8821c(phydm, ODM_POST_SETTING))
		return _FALSE;

	hal->phy_spec.trx_cap = query_phydm_trx_capability(phydm);
	hal->phy_spec.stbc_cap = query_phydm_stbc_capability(phydm);
	hal->phy_spec.ldpc_cap = query_phydm_ldpc_capability(phydm);
	hal->phy_spec.txbf_param = query_phydm_txbf_parameters(phydm);
	hal->phy_spec.txbf_cap = query_phydm_txbf_capability(phydm);
	/*rtw_dump_phy_cap(RTW_DBGDUMP, adapter);*/
	return _TRUE;
}

static u32 phy_calculatebitshift(u32 mask)
{
	u32 i;


	for (i = 0; i <= 31; i++)
		if (mask & BIT(i))
			break;

	return i;
}

u32 rtl8821c_read_bb_reg(PADAPTER adapter, u32 addr, u32 mask)
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

void rtl8821c_write_bb_reg(PADAPTER adapter, u32 addr, u32 mask, u32 val)
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

u32 rtl8821c_read_rf_reg(PADAPTER adapter, enum rf_path path, u32 addr, u32 mask)
{
	struct dm_struct *phydm = adapter_to_phydm(adapter);
	u32 val = 0;

	val = config_phydm_read_rf_reg_8821c(phydm, path, addr, mask);
	if (!config_phydm_read_rf_check_8821c(val))
		RTW_INFO(FUNC_ADPT_FMT ": read RF reg path=%d addr=0x%x mask=0x%x FAIL!\n",
			 FUNC_ADPT_ARG(adapter), path, addr, mask);
	return val;
}

void rtl8821c_write_rf_reg(PADAPTER adapter, enum rf_path path, u32 addr, u32 mask, u32 val)
{
	struct dm_struct *phydm = adapter_to_phydm(adapter);
	u8 ret;

	ret = config_phydm_write_rf_reg_8821c(phydm, path, addr, mask, val);
	if (_FALSE == ret)
		RTW_INFO(FUNC_ADPT_FMT ": write RF reg path=%d addr=0x%x mask=0x%x val=0x%x FAIL!\n",
			 FUNC_ADPT_ARG(adapter), path, addr, mask, val);

}

void rtl8821c_set_tx_power_level(PADAPTER adapter, u8 channel)
{
	u8 path = RF_PATH_A;
	struct dm_struct *phydm = adapter_to_phydm(adapter);
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 under_survey_ch = phy_check_under_survey_ch(adapter);
	u8 under_24g = (hal->current_band_type == BAND_ON_2_4G);

	/*((hal->RFEType == 2) || (hal->RFEType == 4) || (hal->RFEType == 7))*/
	if ((channel <= 14) && (SWITCH_TO_BTG == query_phydm_default_rf_set_8821c(phydm)))
		path = RF_PATH_B;

	/*if (adapter->registrypriv.mp_mode == 1)*/
	if (under_24g)
		phy_set_tx_power_index_by_rate_section(adapter, path, channel, CCK);

	phy_set_tx_power_index_by_rate_section(adapter, path, channel, OFDM);
	if (!under_survey_ch) {
		phy_set_tx_power_index_by_rate_section(adapter, path, channel, HT_MCS0_MCS7);
		phy_set_tx_power_index_by_rate_section(adapter, path, channel, VHT_1SSMCS0_1SSMCS9);
	}
}

/*
 * Parameters:
 *	padatper
 *	powerindex	power index for rate
 *	rfpath		Antenna(RF) path, type "enum rf_path"
 *	rate		data rate, type "enum MGN_RATE"
 */
void rtl8821c_set_tx_power_index(PADAPTER adapter, u32 powerindex, enum rf_path rfpath, u8 rate)
{
	HAL_DATA_TYPE *hal = GET_HAL_DATA(adapter);
	struct dm_struct *phydm = adapter_to_phydm(adapter);
	u8 reg_path;
	u8 shift = 0;
	boolean write_ret;

	if (!IS_1T_RATE(rate)) {
		RTW_ERR(FUNC_ADPT_FMT" invalid rate(%s)\n", FUNC_ADPT_ARG(adapter), MGN_RATE_STR(rate));
		rtw_warn_on(1);
		goto exit;
	}

	reg_path = RF_PATH_A;
	rate = MRateToHwRate(rate);

	/*
	* For 8821C, phydm api use 4 bytes txagc value
	* driver must combine every four 1 byte to one 4 byte and send to phydm api
	*/
	shift = rate % 4;
	hal->txagc_set_buf |= ((powerindex & 0xff) << (shift * 8));

	if (shift != 3 && rate != DESC_RATEVHTSS1MCS9)
		goto exit;

	rate = rate & 0xFC;
	write_ret = config_phydm_write_txagc_8821c(phydm, hal->txagc_set_buf, reg_path, rate);

	if (write_ret == true && !DBG_TX_POWER_IDX)
		goto clear_buf;

	RTW_INFO(FUNC_ADPT_FMT" (index:0x%08x, %c, rate:%s(0x%02x), disable api:%d) from %c %s\n"
		, FUNC_ADPT_ARG(adapter), hal->txagc_set_buf, rf_path_char(reg_path)
		, HDATA_RATE(rate), rate, phydm->is_disable_phy_api
		, rf_path_char(rfpath)
		, write_ret == true ? "OK" : "FAIL");

	rtw_warn_on(write_ret != true);

clear_buf:
	hal->txagc_set_buf = 0;

exit:
	return;
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

	Band = hal->current_band_type;

	/* Use current swich channel to judge Band Type and switch Band if need */
	if (channelToSW > 14)
		BandToSW = BAND_ON_5G;
	else
		BandToSW = BAND_ON_2_4G;

	if (BandToSW != Band) {
		/* record current band type for other hal use */
		hal->current_band_type = (BAND_TYPE)BandToSW;
		ret_value = _TRUE;
	} else
		ret_value = _FALSE;

	return ret_value;
}

static u8 get_pri_ch_id(PADAPTER adapter)
{
	u8 pri_ch_idx = 0;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	if (hal->current_channel_bw == CHANNEL_WIDTH_80) {
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
	} else if (hal->current_channel_bw == CHANNEL_WIDTH_40) {
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

	channel = hal->current_channel;
	bw = hal->current_channel_bw;
	err = rtw_halmac_set_bandwidth(adapter_to_dvobj(adapter), channel, pri_ch_idx, bw);
	if (err) {
		RTW_INFO(FUNC_ADPT_FMT ": (channel=%d, pri_ch_idx=%d, bw=%d) fail\n",
			 FUNC_ADPT_ARG(adapter), channel, pri_ch_idx, bw);
	}
}
u32 phy_get_tx_bbswing_8821c(_adapter *adapter, BAND_TYPE band, u8 rf_path)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(adapter);
	struct dm_struct		*pDM_Odm = &pHalData->odmpriv;
	struct dm_rf_calibration_struct	*pRFCalibrateInfo = &(pDM_Odm->rf_calibrate_info);
	s8	bbSwing_2G = -1 * GetRegTxBBSwing_2G(adapter);
	s8	bbSwing_5G = -1 * GetRegTxBBSwing_5G(adapter);
	u32	out = 0x200;
	const s8	AUTO = -1;

	if (pHalData->bautoload_fail_flag) {
		if (band == BAND_ON_2_4G) {
			pRFCalibrateInfo->bb_swing_diff_2g = bbSwing_2G;
			if (bbSwing_2G == 0)
				out = 0x200; /* 0 dB */
			else if (bbSwing_2G == -3)
				out = 0x16A; /* -3 dB */
			else if (bbSwing_2G == -6)
				out = 0x101; /* -6 dB */
			else if (bbSwing_2G == -9)
				out = 0x0B6; /* -9 dB */
			else {
				if (pHalData->ExternalPA_2G) {
					pRFCalibrateInfo->bb_swing_diff_2g = -3;
					out = 0x16A;
				} else  {
					pRFCalibrateInfo->bb_swing_diff_2g = 0;
					out = 0x200;
				}
			}
		} else if (band == BAND_ON_5G) {
			pRFCalibrateInfo->bb_swing_diff_5g = bbSwing_5G;
			if (bbSwing_5G == 0)
				out = 0x200; /* 0 dB */
			else if (bbSwing_5G == -3)
				out = 0x16A; /* -3 dB */
			else if (bbSwing_5G == -6)
				out = 0x101; /* -6 dB */
			else if (bbSwing_5G == -9)
				out = 0x0B6; /* -9 dB */
			else {
				if (pHalData->external_pa_5g) {
					pRFCalibrateInfo->bb_swing_diff_5g = -3;
					out = 0x16A;
				} else {
					pRFCalibrateInfo->bb_swing_diff_5g = 0;
					out = 0x200;
				}
			}
		} else {
			pRFCalibrateInfo->bb_swing_diff_2g = -3;
			pRFCalibrateInfo->bb_swing_diff_5g = -3;
			out = 0x16A; /* -3 dB */
		}
	} else {
		u32 swing = 0, onePathSwing = 0;

		if (band == BAND_ON_2_4G) {
			if (GetRegTxBBSwing_2G(adapter) == AUTO)
				swing = pHalData->tx_bbswing_24G;
			else if (bbSwing_2G ==  0)
				swing = 0x00; /* 0 dB */
			else if (bbSwing_2G == -3)
				swing = 0x55; /* -3 dB */
			else if (bbSwing_2G == -6)
				swing = 0xAA; /* -6 dB */
			else if (bbSwing_2G == -9)
				swing = 0xFF; /* -9 dB */
			else
				swing = 0x00;
		} else {
			if (GetRegTxBBSwing_5G(adapter) == AUTO)
				swing = pHalData->tx_bbswing_5G;
			else if (bbSwing_5G ==  0)
				swing = 0x00; /* 0 dB */
			else if (bbSwing_5G == -3)
				swing = 0x55; /* -3 dB */
			else if (bbSwing_5G == -6)
				swing = 0xAA; /* -6 dB */
			else if (bbSwing_5G == -9)
				swing = 0xFF; /* -9 dB */
			else
				swing = 0x00;
		}

		if (rf_path == RF_PATH_A)
			onePathSwing = (swing & 0x3) >> 0; /* 0xC6/C7[1:0] */

		if (onePathSwing == 0x0) {
			if (band == BAND_ON_2_4G)
				pRFCalibrateInfo->bb_swing_diff_2g = 0;
			else
				pRFCalibrateInfo->bb_swing_diff_5g = 0;
			out = 0x200; /* 0 dB */
		} else if (onePathSwing == 0x1) {
			if (band == BAND_ON_2_4G)
				pRFCalibrateInfo->bb_swing_diff_2g = -3;
			else
				pRFCalibrateInfo->bb_swing_diff_5g = -3;
			out = 0x16A; /* -3 dB */
		} else if (onePathSwing == 0x2) {
			if (band == BAND_ON_2_4G)
				pRFCalibrateInfo->bb_swing_diff_2g = -6;
			else
				pRFCalibrateInfo->bb_swing_diff_5g = -6;
			out = 0x101; /* -6 dB */
		} else if (onePathSwing == 0x3) {
			if (band == BAND_ON_2_4G)
				pRFCalibrateInfo->bb_swing_diff_2g = -9;
			else
				pRFCalibrateInfo->bb_swing_diff_5g = -9;
			out = 0x0B6; /* -9 dB */
		}
	}

	/* RTW_INFO("<=== PHY_GetTxBBSwing_8812C, out = 0x%X\n", out); */

	return out;
}

void phy_set_bb_swing_by_band_8821c(_adapter *adapter, u8 band, u8 previous_band)
{
	s8 BBDiffBetweenBand = 0;
	struct dm_struct *pDM_Odm = adapter_to_phydm(adapter);
	struct dm_rf_calibration_struct *pRFCalibrateInfo = &(pDM_Odm->rf_calibrate_info);

	phy_set_bb_reg(adapter, rA_TxScale_Jaguar, 0xFFE00000,
			phy_get_tx_bbswing_8821c(adapter, (BAND_TYPE)band, RF_PATH_A)); /* 0xC1C[31:21] */

	/* When TxPowerTrack is ON, we should take care of the change of BB swing. */
	/* That is, reset all info to trigger Tx power tracking. */
	{

		if (band != previous_band) {
			BBDiffBetweenBand = (pRFCalibrateInfo->bb_swing_diff_2g - pRFCalibrateInfo->bb_swing_diff_5g);
			BBDiffBetweenBand = (band == BAND_ON_2_4G) ? BBDiffBetweenBand : (-1 * BBDiffBetweenBand);
			pRFCalibrateInfo->default_ofdm_index += BBDiffBetweenBand * 2;
		}

		odm_clear_txpowertracking_state(pDM_Odm);
	}

}

void phy_switch_wireless_band_8821c(_adapter *adapter)
{
	u8 ret = 0;
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);
	struct dm_struct *pDM_Odm = &hal_data->odmpriv;
	u8 current_band = hal_data->current_band_type;

	if (need_switch_band(adapter, hal_data->current_channel) == _TRUE) {
#ifdef CONFIG_BT_COEXIST
		if (hal_data->EEPROMBluetoothCoexist) {
			struct mlme_ext_priv *mlmeext;

			/* switch band under site survey or not, must notify to BT COEX */
			mlmeext = &adapter->mlmeextpriv;
			if (mlmeext_scan_state(mlmeext) != SCAN_DISABLE)
				rtw_btcoex_switchband_notify(_TRUE, hal_data->current_band_type);
			else
				rtw_btcoex_switchband_notify(_FALSE, hal_data->current_band_type);
		} else
			rtw_btcoex_wifionly_switchband_notify(adapter);
#else /* !CONFIG_BT_COEXIST */
		rtw_btcoex_wifionly_switchband_notify(adapter);
#endif /* CONFIG_BT_COEXIST */

		/* hal->current_channel is center channel of pmlmeext->cur_channel(primary channel) */
		ret = config_phydm_switch_band_8821c(pDM_Odm, hal_data->current_channel);

		if (!ret) {
			RTW_ERR("%s: config_phydm_switch_band_8821c fail\n", __func__);
			rtw_warn_on(1);
			return;
		}
		phy_set_bb_swing_by_band_8821c(adapter, hal_data->current_band_type, current_band);
	}
}

/*
 * Description:
 *	Set channel & bandwidth & offset
 */
void rtl8821c_switch_chnl_and_set_bw(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct dm_struct *pDM_Odm = &hal->odmpriv;
	u8 center_ch = 0, ret = 0;

	if (adapter->bNotifyChannelChange) {
		RTW_INFO("[%s] bSwChnl=%d, ch=%d, bSetChnlBW=%d, bw=%d\n",
			 __FUNCTION__,
			 hal->bSwChnl,
			 hal->current_channel,
			 hal->bSetChnlBW,
			 hal->current_channel_bw);
	}

	if (RTW_CANNOT_RUN(adapter)) {
		hal->bSwChnlAndSetBWInProgress = _FALSE;
		return;
	}

	/* set channel & Bandwidth register */
	/* 1. set switch band register if need to switch band */
	phy_switch_wireless_band_8821c(adapter);

	/* 2. set channel register */
	if (hal->bSwChnl) {
		ret = config_phydm_switch_channel_8821c(pDM_Odm, hal->current_channel);
		hal->bSwChnl = _FALSE;

		if (!ret) {
			RTW_INFO("%s: config_phydm_switch_channel_8821c fail\n", __FUNCTION__);
			rtw_warn_on(1);
			return;
		}
	}
	phydm_config_kfree(pDM_Odm, hal->current_channel);

	/* 3. set Bandwidth register */
	if (hal->bSetChnlBW) {
		/* get primary channel index */
		u8 pri_ch_idx = get_pri_ch_id(adapter);

		/* 3.1 set MAC register */
		mac_switch_bandwidth(adapter, pri_ch_idx);

		/* 3.2 set BB/RF registet */
		ret = config_phydm_switch_bandwidth_8821c(pDM_Odm, pri_ch_idx, hal->current_channel_bw);
		hal->bSetChnlBW = _FALSE;

		if (!ret) {
			RTW_INFO("%s: config_phydm_switch_bandwidth_8821c fail\n", __FUNCTION__);
			rtw_warn_on(1);
			return;
		}
	}

	/* TX Power Setting */
	/* odm_clear_txpowertracking_state(pDM_Odm); */
	rtw_hal_set_tx_power_level(adapter, hal->current_channel);

	/* IQK */
	if ((hal->bNeedIQK == _TRUE)
	    || (adapter->registrypriv.mp_mode == 1))  {
		#ifdef CONFIG_IQK_MONITOR
		systime iqk_start_time = rtw_get_current_time();
		#endif

		/*phy_iq_calibrate_8821c(pDM_Odm, _FALSE);*/
		rtw_phydm_iqk_trigger(adapter);

		#ifdef CONFIG_IQK_MONITOR
		RTW_INFO(ADPT_FMT" switch CH(%d) DO IQK : %d ms\n", 
			ADPT_ARG(adapter), hal->current_channel, rtw_get_passing_time_ms(iqk_start_time));
		#endif
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

void rtl8821c_handle_sw_chnl_and_set_bw(
	PADAPTER Adapter, u8 bSwitchChannel, u8 bSetBandWidth,
	u8 ChannelNum, enum channel_width ChnlWidth, u8 ChnlOffsetOf40MHz,
	u8 ChnlOffsetOf80MHz, u8 CenterFrequencyIndex1)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(Adapter);
	u8 tmpChannel = hal->current_channel;
	enum channel_width tmpBW = hal->current_channel_bw;
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
		if (hal->current_channel != ChannelNum) {
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
		} else if ((hal->current_channel_bw != ChnlWidth) || /* check whether need set band or not */
			   (hal->nCur40MhzPrimeSC != ChnlOffsetOf40MHz) ||
			   (hal->nCur80MhzPrimeSC != ChnlOffsetOf80MHz) ||
			(hal->CurrentCenterFrequencyIndex1 != CenterFrequencyIndex1))
			hal->bSetChnlBW = _TRUE;
	}

	/* return if not need set bandwidth nor channel after check*/
	if (!hal->bSetChnlBW && !hal->bSwChnl && hal->bNeedIQK != _TRUE)
		return;

	/* set channel number to hal data */
	if (hal->bSwChnl) {
		hal->current_channel = ChannelNum;
		hal->CurrentCenterFrequencyIndex1 = ChannelNum;
	}

	/* set bandwidth info to hal data */
	if (hal->bSetChnlBW) {
		hal->current_channel_bw = ChnlWidth;
		hal->nCur40MhzPrimeSC = ChnlOffsetOf40MHz;
		hal->nCur80MhzPrimeSC = ChnlOffsetOf80MHz;
		hal->CurrentCenterFrequencyIndex1 = CenterFrequencyIndex1;
	}

	/* switch channel & bandwidth */
	if (!RTW_CANNOT_RUN(Adapter))
		rtl8821c_switch_chnl_and_set_bw(Adapter);
	else {
		if (hal->bSwChnl) {
			hal->current_channel = tmpChannel;
			hal->CurrentCenterFrequencyIndex1 = tmpChannel;
		}

		if (hal->bSetChnlBW) {
			hal->current_channel_bw = tmpBW;
			hal->nCur40MhzPrimeSC = tmpnCur40MhzPrimeSC;
			hal->nCur80MhzPrimeSC = tmpnCur80MhzPrimeSC;
			hal->CurrentCenterFrequencyIndex1 = tmpCenterFrequencyIndex1;
		}
	}
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
void rtl8821c_set_channel_bw(PADAPTER adapter, u8 center_ch, enum channel_width bw, u8 offset40, u8 offset80)
{
	rtl8821c_handle_sw_chnl_and_set_bw(adapter, _TRUE, _TRUE, center_ch, bw, offset40, offset80, center_ch);
}

void rtl8821c_notch_filter_switch(PADAPTER adapter, bool enable)
{
	if (enable)
		RTW_INFO("%s: Enable notch filter\n", __FUNCTION__);
	else
		RTW_INFO("%s: Disable notch filter\n", __FUNCTION__);
}
#ifdef CONFIG_BEAMFORMING
#ifdef RTW_BEAMFORMING_VERSION_2
/* REG_TXBF_CTRL		(Offset 0x42C) */
#define BITS_R_TXBF1_AID_8821C			(BIT_MASK_R_TXBF1_AID_8821C << BIT_SHIFT_R_TXBF1_AID_8821C)
#define BIT_CLEAR_R_TXBF1_AID_8821C(x)		((x) & (~BITS_R_TXBF1_AID_8821C))
#define BIT_SET_R_TXBF1_AID_8821C(x, v)		(BIT_CLEAR_R_TXBF1_AID_8821C(x) | BIT_R_TXBF1_AID_8821C(v))

#define BITS_R_TXBF0_AID_8821C			(BIT_MASK_R_TXBF0_AID_8821C << BIT_SHIFT_R_TXBF0_AID_8821C)
#define BIT_CLEAR_R_TXBF0_AID_8821C(x)		((x) & (~BITS_R_TXBF0_AID_8821C))
#define BIT_SET_R_TXBF0_AID_8821C(x, v)		(BIT_CLEAR_R_TXBF0_AID_8821C(x) | BIT_R_TXBF0_AID_8821C(v))

/* REG_NDPA_OPT_CTRL		(Offset 0x45F) */
#define BITS_R_NDPA_BW_8821C			(BIT_MASK_R_NDPA_BW_8821C << BIT_SHIFT_R_NDPA_BW_8821C)
#define BIT_CLEAR_R_NDPA_BW_8821C(x)		((x) & (~BITS_R_NDPA_BW_8821C))
#define BIT_SET_R_NDPA_BW_8821C(x, v)		(BIT_CLEAR_R_NDPA_BW_8821C(x) | BIT_R_NDPA_BW_8821C(v))

/* REG_ASSOCIATED_BFMEE_SEL	(Offset 0x714) */
#define BITS_AID1_8821C				(BIT_MASK_AID1_8821C << BIT_SHIFT_AID1_8821C)
#define BIT_CLEAR_AID1_8821C(x)			((x) & (~BITS_AID1_8821C))
#define BIT_SET_AID1_8821C(x, v)		(BIT_CLEAR_AID1_8821C(x) | BIT_AID1_8821C(v))

#define BITS_AID0_8821C				(BIT_MASK_AID0_8821C << BIT_SHIFT_AID0_8821C)
#define BIT_CLEAR_AID0_8821C(x)			((x) & (~BITS_AID0_8821C))
#define BIT_SET_AID0_8821C(x, v)		(BIT_CLEAR_AID0_8821C(x) | BIT_AID0_8821C(v))

/* REG_MU_TX_CTL		(Offset 0x14C0) */
#define BIT_R_MU_P1_WAIT_STATE_EN_8821C		BIT(16)

#define BIT_SHIFT_R_MU_RL_8821C			12
#define BITS_R_MU_RL_8821C			(BIT_MASK_R_MU_RL_8821C << BIT_SHIFT_R_MU_RL_8821C)
#define BIT_R_MU_RL_8821C(x)			(((x) & BIT_MASK_R_MU_RL_8821C) << BIT_SHIFT_R_MU_RL_8821C)
#define BIT_CLEAR_R_MU_RL_8821C(x)		((x) & (~BITS_R_MU_RL_8821C))
#define BIT_SET_R_MU_RL_8821C(x, v)		(BIT_CLEAR_R_MU_RL_8821C(x) | BIT_R_MU_RL_8821C(v))

#define BIT_SHIFT_R_MU_TAB_SEL_8821C		8
#define BIT_MASK_R_MU_TAB_SEL_8821C		0x7
#define BITS_R_MU_TAB_SEL_8821C			(BIT_MASK_R_MU_TAB_SEL_8821C << BIT_SHIFT_R_MU_TAB_SEL_8821C)
#define BIT_R_MU_TAB_SEL_8821C(x)		(((x) & BIT_MASK_R_MU_TAB_SEL_8821C) << BIT_SHIFT_R_MU_TAB_SEL_8821C)
#define BIT_CLEAR_R_MU_TAB_SEL_8821C(x)		((x) & (~BITS_R_MU_TAB_SEL_8821C))
#define BIT_SET_R_MU_TAB_SEL_8821C(x, v)	(BIT_CLEAR_R_MU_TAB_SEL_8821C(x) | BIT_R_MU_TAB_SEL_8821C(v))

#define BIT_R_EN_MU_MIMO_8821C			BIT(7)

#define BITS_R_MU_TABLE_VALID_8821C		(BIT_MASK_R_MU_TABLE_VALID_8821C << BIT_SHIFT_R_MU_TABLE_VALID_8821C)
#define BIT_CLEAR_R_MU_TABLE_VALID_8821C(x)	((x) & (~BITS_R_MU_TABLE_VALID_8821C))
#define BIT_SET_R_MU_TABLE_VALID_8821C(x, v)	(BIT_CLEAR_R_MU_TABLE_VALID_8821C(x) | BIT_R_MU_TABLE_VALID_8821C(v))

/* REG_WMAC_MU_BF_CTL		(Offset 0x1680) */
#define BITS_WMAC_MU_BFRPTSEG_SEL_8821C			(BIT_MASK_WMAC_MU_BFRPTSEG_SEL_8821C << BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL_8821C)
#define BIT_CLEAR_WMAC_MU_BFRPTSEG_SEL_8821C(x)		((x) & (~BITS_WMAC_MU_BFRPTSEG_SEL_8821C))
#define BIT_SET_WMAC_MU_BFRPTSEG_SEL_8821C(x, v)	(BIT_CLEAR_WMAC_MU_BFRPTSEG_SEL_8821C(x) | BIT_WMAC_MU_BFRPTSEG_SEL_8821C(v))

#define BITS_WMAC_MU_BF_MYAID_8821C		(BIT_MASK_WMAC_MU_BF_MYAID_8821C << BIT_SHIFT_WMAC_MU_BF_MYAID_8821C)
#define BIT_CLEAR_WMAC_MU_BF_MYAID_8821C(x)	((x) & (~BITS_WMAC_MU_BF_MYAID_8821C))
#define BIT_SET_WMAC_MU_BF_MYAID_8821C(x, v)	(BIT_CLEAR_WMAC_MU_BF_MYAID_8821C(x) | BIT_WMAC_MU_BF_MYAID_8821C(v))

/* REG_WMAC_ASSOCIATED_MU_BFMEE7	(Offset 0x168E) */
#define BIT_STATUS_BFEE7_8821C			BIT(10)


static u8 _bf_get_nrx(PADAPTER adapter)
{
	u8 nrx = 0;

	nrx = GET_HAL_RX_NSS(adapter);
	return (nrx - 1);
}


static void _config_beamformer_su(PADAPTER adapter, struct beamformer_entry *bfer)
{
	/* Beamforming */
	u8 nc_index = 0, nr_index = 0;
	u8 grouping = 0, codebookinfo = 0, coefficientsize = 0;
	u32 addr_bfer_info, addr_csi_rpt;
	u32 csi_param;
	/* Misc */
	u8 i;


	RTW_INFO("%s: Config SU BFer entry HW setting\n", __FUNCTION__);

	if (bfer->su_reg_index == 0) {
		addr_bfer_info = REG_ASSOCIATED_BFMER0_INFO_8821C;
		addr_csi_rpt = REG_TX_CSI_RPT_PARAM_BW20_8821C;
	} else {
		addr_bfer_info = REG_ASSOCIATED_BFMER1_INFO_8821C;
		addr_csi_rpt = REG_TX_CSI_RPT_PARAM_BW20_8821C + 2;
	}

	/* Sounding protocol control */
	rtw_write8(adapter, REG_SND_PTCL_CTRL_8821C, 0xDB);

	/* MAC address/Partial AID of Beamformer */
	for (i = 0; i < ETH_ALEN; i++)
		rtw_write8(adapter, addr_bfer_info+i, bfer->mac_addr[i]);

	/* CSI report parameters of Beamformer */
	nc_index = _bf_get_nrx(adapter);
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
	rtw_write16(adapter, addr_csi_rpt, csi_param);
	RTW_INFO("%s: nc=%d nr=%d group=%d codebookinfo=%d coefficientsize=%d\n",
		 __FUNCTION__, nc_index, nr_index, grouping, codebookinfo, coefficientsize);
	RTW_INFO("%s: csi=0x%04x\n", __FUNCTION__, csi_param);

	/* ndp_rx_standby_timer */
	rtw_write8(adapter, REG_SND_PTCL_CTRL_8821C+3, 0x70);
}

static void _config_beamformer_mu(PADAPTER adapter, struct beamformer_entry *bfer)
{
	/* General */
	PHAL_DATA_TYPE hal;
	/* Beamforming */
	struct beamforming_info *bf_info;
	u8 nc_index = 0, nr_index = 0;
	u8 grouping = 0, codebookinfo = 0, coefficientsize = 0;
	u32 csi_param;
	/* Misc */
	u8 i, val8;
	u16 val16;

	RTW_INFO("%s: Config MU BFer entry HW setting\n", __FUNCTION__);

	hal = GET_HAL_DATA(adapter);
	bf_info = GET_BEAMFORM_INFO(adapter);

	/* Reset GID table */
	for (i = 0; i < 8; i++)
		bfer->gid_valid[i] = 0;
	for (i = 0; i < 16; i++)
		bfer->user_position[i] = 0;

	/* CSI report parameters of Beamformer */
	nc_index = _bf_get_nrx(adapter);
	nr_index = 1; /* 0x718[7] = 1 use Nsts, 0x718[7] = 0 use reg setting. as Bfee, we use Nsts, so Nr_index don't care */
	grouping = 0; /* no grouping */
	codebookinfo = 1; /* 7 bit for psi, 9 bit for phi */
	coefficientsize = 0; /* This is nothing really matter */
	csi_param = (u16)((coefficientsize<<10)|(codebookinfo<<8)|
			(grouping<<6)|(nr_index<<3)|(nc_index));

	RTW_INFO("%s: nc=%d nr=%d group=%d codebookinfo=%d coefficientsize=%d\n",
		__func__, nc_index, nr_index, grouping, codebookinfo,
		coefficientsize);
	RTW_INFO("%s: csi=0x%04x\n", __func__, csi_param);

	rtw_halmac_bf_add_mu_bfer(adapter_to_dvobj(adapter), bfer->p_aid,
			csi_param, bfer->aid & 0xfff, HAL_CSI_SEG_4K,
			bfer->mac_addr);

	bf_info->cur_csi_rpt_rate = HALMAC_OFDM6;
	rtw_halmac_bf_cfg_sounding(adapter_to_dvobj(adapter), HAL_BFEE,
			bf_info->cur_csi_rpt_rate);

	/* Set 0x6A0[14] = 1 to accept action_no_ack */
	val8 = rtw_read8(adapter, REG_RXFLTMAP0_8821C+1);
	val8 |= (BIT_MGTFLT14EN_8821C >> 8);
	rtw_write8(adapter, REG_RXFLTMAP0_8821C+1, val8);

	/* Set 0x6A2[5:4] = 1 to NDPA and BF report poll */
	val8 = rtw_read8(adapter, REG_RXFLTMAP1_8821C);
	val8 |= BIT_CTRLFLT4EN_8821C | BIT_CTRLFLT5EN_8821C;
	rtw_write8(adapter, REG_RXFLTMAP1_8821C, val8);

	/* for B-Cut */
	if (IS_B_CUT(hal->version_id)) {
		phy_set_bb_reg(adapter, REG_RXFLTMAP0_8821C, BIT(20), 0);
		phy_set_bb_reg(adapter, REG_RXFLTMAP3_8821C, BIT(20), 0);
	}
}



static void _reset_beamformer_su(PADAPTER adapter, struct beamformer_entry *bfer)
{
	/* Beamforming */
	struct beamforming_info *info;
	u8 idx;


	info = GET_BEAMFORM_INFO(adapter);
	/* SU BFer */
	idx = bfer->su_reg_index;

	if (idx == 0) {
		rtw_write32(adapter, REG_ASSOCIATED_BFMER0_INFO_8821C, 0);
		rtw_write16(adapter, REG_ASSOCIATED_BFMER0_INFO_8821C+4, 0);
		rtw_write16(adapter, REG_TX_CSI_RPT_PARAM_BW20_8821C, 0);
	} else {
		rtw_write32(adapter, REG_ASSOCIATED_BFMER1_INFO_8821C, 0);
		rtw_write16(adapter, REG_ASSOCIATED_BFMER1_INFO_8821C+4, 0);
		rtw_write16(adapter, REG_TX_CSI_RPT_PARAM_BW20_8821C+2, 0);
	}

	info->beamformer_su_reg_maping &= ~BIT(idx);
	bfer->su_reg_index = 0xFF;

	RTW_INFO("%s: Clear SU BFer entry(%d) HW setting\n", __FUNCTION__, idx);
}

static void _reset_beamformer_mu(PADAPTER adapter, struct beamformer_entry *bfer)
{
	struct beamforming_info *bf_info;

	bf_info = GET_BEAMFORM_INFO(adapter);

	rtw_halmac_bf_del_mu_bfer(adapter_to_dvobj(adapter));

	if (bf_info->beamformer_su_cnt == 0 &&
			bf_info->beamformer_mu_cnt == 0)
		rtw_halmac_bf_del_sounding(adapter_to_dvobj(adapter), HAL_BFEE);

	RTW_INFO("%s: Clear MU BFer entry HW setting\n", __FUNCTION__);
}

void rtl8821c_phy_bf_init(PADAPTER adapter)
{
	u8 v8;
	u32 v32;

	v32 = rtw_read32(adapter, REG_MU_TX_CTL_8821C);
	/* Enable P1 aggr new packet according to P0 transfer time */
	v32 |= BIT_R_MU_P1_WAIT_STATE_EN_8821C;
	/* MU Retry Limit */
	v32 = BIT_SET_R_MU_RL_8821C(v32, 0xA);
	/* Disable Tx MU-MIMO until sounding done */
	v32 &= ~BIT_R_EN_MU_MIMO_8821C;
	/* Clear validity of MU STAs */
	v32 = BIT_SET_R_MU_TABLE_VALID_8821C(v32, 0);
	rtw_write32(adapter, REG_MU_TX_CTL_8821C, v32);

	/* MU-MIMO Option as default value */
	v8 = BIT_WMAC_TXMU_ACKPOLICY_8821C(3);
	v8 |= BIT_WMAC_TXMU_ACKPOLICY_EN_8821C;
	rtw_write8(adapter, REG_MU_BF_OPTION_8821C, v8);
	/* MU-MIMO Control as default value */
	rtw_write16(adapter, REG_WMAC_MU_BF_CTL_8821C, 0);

	/* Set MU NDPA rate & BW source */
	/* 0x42C[30] = 1 (0: from Tx desc, 1: from 0x45F) */
	v8 = rtw_read8(adapter, REG_TXBF_CTRL_8821C+3);
	v8 |= (BIT_USE_NDPA_PARAMETER_8821C >> 24);
	rtw_write8(adapter, REG_TXBF_CTRL_8821C+3, v8);
	/* 0x45F[7:0] = 0x10 (Rate=OFDM_6M, BW20) */
	rtw_write8(adapter, REG_NDPA_OPT_CTRL_8821C, 0x10);

	/* Temp Settings */
	/* STA2's CSI rate is fixed at 6M */
	v8 = rtw_read8(adapter, 0x6DF);
	v8 = (v8 & 0xC0) | 0x4;
	rtw_write8(adapter, 0x6DF, v8);
	/* Grouping bitmap parameters */
	rtw_write32(adapter, 0x1C94, 0xAFFFAFFF);
}

void rtl8821c_phy_bf_enter(PADAPTER adapter, struct sta_info *sta)
{
	struct beamforming_info *info;
	struct beamformer_entry *bfer;



	RTW_INFO("+%s: " MAC_FMT "\n", __FUNCTION__, MAC_ARG(sta->cmn.mac_addr));

	info = GET_BEAMFORM_INFO(adapter);
	bfer = rtw_bf_bfer_get_entry_by_addr(adapter, sta->cmn.mac_addr);


	info->bSetBFHwConfigInProgess = _TRUE;

	if (bfer) {
		bfer->state = BEAMFORM_ENTRY_HW_STATE_ADDING;

		if (TEST_FLAG(bfer->cap, BEAMFORMER_CAP_VHT_MU))
			_config_beamformer_mu(adapter, bfer);
		else if (TEST_FLAG(bfer->cap, BEAMFORMER_CAP_VHT_SU|BEAMFORMER_CAP_HT_EXPLICIT))
			_config_beamformer_su(adapter, bfer);

		bfer->state = BEAMFORM_ENTRY_HW_STATE_ADDED;
	}

	info->bSetBFHwConfigInProgess = _FALSE;

	RTW_INFO("-%s\n", __FUNCTION__);
}

void rtl8821c_phy_bf_leave(PADAPTER adapter, u8 *addr)
{
	struct beamforming_info *info;
	struct beamformer_entry *bfer;



	RTW_INFO("+%s: " MAC_FMT "\n", __FUNCTION__, MAC_ARG(addr));

	info = GET_BEAMFORM_INFO(adapter);

	bfer = rtw_bf_bfer_get_entry_by_addr(adapter, addr);


	/* Clear P_AID of Beamformee */
	/* Clear MAC address of Beamformer */
	/* Clear Associated Bfmee Sel */
	if (bfer) {
		bfer->state = BEAMFORM_ENTRY_HW_STATE_DELETING;

		rtw_write8(adapter, REG_SND_PTCL_CTRL_8821C, 0xD8);

		if (TEST_FLAG(bfer->cap, BEAMFORMER_CAP_VHT_MU))
			_reset_beamformer_mu(adapter, bfer);
		else if (TEST_FLAG(bfer->cap, BEAMFORMER_CAP_VHT_SU|BEAMFORMER_CAP_HT_EXPLICIT))
			_reset_beamformer_su(adapter, bfer);

		bfer->state = BEAMFORM_ENTRY_HW_STATE_NONE;
		bfer->cap = BEAMFORMING_CAP_NONE;
		bfer->used = _FALSE;
	}


	RTW_INFO("-%s\n", __FUNCTION__);
}

void rtl8821c_phy_bf_set_gid_table(PADAPTER adapter,
		struct beamformer_entry	*bfer_info)
{
	struct beamformer_entry *bfer;
	struct beamforming_info *info;
	u32 gid_valid[2] = {0};
	u32 user_position[4] = {0};
	int i;

	/* update bfer info */
	bfer = rtw_bf_bfer_get_entry_by_addr(adapter, bfer_info->mac_addr);
	if (!bfer) {
		RTW_INFO("%s: Cannot find BFer entry!!\n", __func__);
		return;
	}
	_rtw_memcpy(bfer->gid_valid, bfer_info->gid_valid, 8);
	_rtw_memcpy(bfer->user_position, bfer_info->user_position, 16);

	info = GET_BEAMFORM_INFO(adapter);
	info->bSetBFHwConfigInProgess = _TRUE;

	/* For GID 0~31 */
	for (i = 0; i < 4; i++)
		gid_valid[0] |= (bfer->gid_valid[i] << (i << 3));

	for (i = 0; i < 8; i++) {
		if (i < 4)
			user_position[0] |= (bfer->user_position[i] << (i << 3));
		else
			user_position[1] |= (bfer->user_position[i] << ((i - 4) << 3));
	}

	RTW_INFO("%s: STA0: gid_valid=0x%x, user_position_l=0x%x, user_position_h=0x%x\n",
		__func__, gid_valid[0], user_position[0], user_position[1]);

	/* For GID 32~64 */
	for (i = 4; i < 8; i++)
		gid_valid[1] |= (bfer->gid_valid[i] << ((i - 4) << 3));

	for (i = 8; i < 16; i++) {
		if (i < 12)
			user_position[2] |= (bfer->user_position[i] << ((i - 8) << 3));
		else
			user_position[3] |= (bfer->user_position[i] << ((i - 12) << 3));
	}

	RTW_INFO("%s: STA1: gid_valid=0x%x, user_position_l=0x%x, user_position_h=0x%x\n",
		__func__, gid_valid[1], user_position[2], user_position[3]);

	rtw_halmac_bf_cfg_mu_bfee(adapter_to_dvobj(adapter), gid_valid, user_position);

	info->bSetBFHwConfigInProgess = _FALSE;
}
#endif /* RTW_BEAMFORMING_VERSION_2 */
#endif /* CONFIG_BEAMFORMING */
#ifdef CONFIG_MP_INCLUDED
/*
 * Description:
 *	Config RF path
 *
 * Parameters:
 *	adapter	pointer of struct _ADAPTER
 */
void rtl8821c_mp_config_rfpath(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	PMPT_CONTEXT mpt;
	ANTENNA_PATH anttx, antrx;
	enum rf_path rxant;


	hal = GET_HAL_DATA(adapter);
	mpt = &adapter->mppriv.mpt_ctx;
	anttx = hal->antenna_tx_path;
	antrx = hal->AntennaRxPath;
	RTW_INFO("+Config RF Path, tx=0x%x rx=0x%x\n", anttx, antrx);

#if 0 /* phydm not ready */
	switch (anttx) {
	case ANTENNA_A:
		mpt->mpt_rf_path = ODM_RF_A;
		break;
	case ANTENNA_B:
		mpt->mpt_rf_path = ODM_RF_B;
		break;
	case ANTENNA_AB:
	default:
		mpt->mpt_rf_path = ODM_RF_A | ODM_RF_B;
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

	config_phydm_trx_mode_8821c(GET_PDM_ODM(adapter), mpt->mpt_rf_path, rxant, FALSE);
#endif
	RTW_INFO("-Config RF Path Finish\n");
}

#endif /* CONFIG_MP_INCLUDED */
