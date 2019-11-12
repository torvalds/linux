/******************************************************************************
 *
 * Copyright(c) 2015 - 2018 Realtek Corporation.
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
#define _RTL8822B_MAC_C_

#include <drv_types.h>		/* PADAPTER, basic_types.h and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../hal_halmac.h"	/* Register Definition and etc. */
#include "rtl8822b.h"		/* FW array */


inline u8 rtl8822b_rcr_config(PADAPTER p, u32 rcr)
{
	u32 v32;
	int err;


	v32 = GET_HAL_DATA(p)->ReceiveConfig;
	v32 ^= rcr;
	v32 &= BIT_APP_PHYSTS_8822B;
	if (v32) {
		v32 = rcr & BIT_APP_PHYSTS_8822B;
		RTW_INFO("%s: runtime %s rx phy status!\n",
			 __FUNCTION__, v32 ? "ENABLE" : "DISABLE");
		if (v32) {
			err = rtw_halmac_config_rx_info(adapter_to_dvobj(p), HALMAC_DRV_INFO_PHY_STATUS);
			if (err) {
				RTW_INFO("%s: Enable rx phy status FAIL!!", __FUNCTION__);
				rcr &= ~BIT_APP_PHYSTS_8822B;
			}
		} else {
			err = rtw_halmac_config_rx_info(adapter_to_dvobj(p), HALMAC_DRV_INFO_NONE);
			if (err) {
				RTW_INFO("%s: Disable rx phy status FAIL!!", __FUNCTION__);
				rcr |= BIT_APP_PHYSTS_8822B;
			}
		}
	}

	err = rtw_write32(p, REG_RCR_8822B, rcr);
	if (_FAIL == err)
		return _FALSE;

	GET_HAL_DATA(p)->ReceiveConfig = rcr;
	return _TRUE;
}

inline u8 rtl8822b_rx_ba_ssn_appended(PADAPTER p)
{
	return rtw_hal_rcr_check(p, BIT_APP_BASSN_8822B);
}

inline u8 rtl8822b_rx_fcs_append_switch(PADAPTER p, u8 enable)
{
	u32 rcr_bit;
	u8 ret = _TRUE;

	rcr_bit = BIT_APP_FCS_8822B;
	if (_TRUE == enable)
		ret = rtw_hal_rcr_add(p, rcr_bit);
	else
		ret = rtw_hal_rcr_clear(p, rcr_bit);

	return ret;
}

inline u8 rtl8822b_rx_fcs_appended(PADAPTER p)
{
	return rtw_hal_rcr_check(p, BIT_APP_FCS_8822B);
}

inline u8 rtl8822b_rx_tsf_addr_filter_config(PADAPTER p, u8 config)
{
	u8 v8;
	int err;

	v8 = GET_HAL_DATA(p)->rx_tsf_addr_filter_config;

	if (v8 != config) {

		err = rtw_write8(p, REG_NAN_RX_TSF_FILTER_8822B, config);
		if (_FAIL == err)
			return _FALSE;
	}

	GET_HAL_DATA(p)->rx_tsf_addr_filter_config = config;
	return _TRUE;
}

/*
 * Return:
 *	_SUCCESS	Download Firmware OK.
 *	_FAIL		Download Firmware FAIL!
 */
s32 rtl8822b_fw_dl(PADAPTER adapter, u8 wowlan)
{
	struct dvobj_priv *d = adapter_to_dvobj(adapter);
	HAL_DATA_TYPE *hal = GET_HAL_DATA(adapter);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	int err;
	u8 fw_bin = _TRUE;

#ifdef CONFIG_FILE_FWIMG
#ifdef CONFIG_WOWLAN
	if (wowlan)
		rtw_get_phy_file_path(adapter, MAC_FILE_FW_WW_IMG);
	else
#endif /* CONFIG_WOWLAN */
		rtw_get_phy_file_path(adapter, MAC_FILE_FW_NIC);

	if (rtw_is_file_readable(rtw_phy_para_file_path) == _TRUE) {
		RTW_INFO("%s acquire FW from file:%s\n", __FUNCTION__, rtw_phy_para_file_path);
		fw_bin = _TRUE;
	} else
#endif /* CONFIG_FILE_FWIMG */
	{
		RTW_INFO("%s fw source from array\n", __FUNCTION__);
		fw_bin = _FALSE;
	}

#ifdef CONFIG_FILE_FWIMG
	if (_TRUE == fw_bin) {
		err = rtw_halmac_dlfw_from_file(d, rtw_phy_para_file_path);
	} else
#endif /* CONFIG_FILE_FWIMG */
	{
		#ifdef CONFIG_WOWLAN
		if (_TRUE == wowlan)
			err = rtw_halmac_dlfw(d, array_mp_8822b_fw_wowlan, array_length_mp_8822b_fw_wowlan);
		else
		#endif /* CONFIG_WOWLAN */
			err = rtw_halmac_dlfw(d, array_mp_8822b_fw_nic, array_length_mp_8822b_fw_nic);
	}

	if (!err) {
		hal->bFWReady = _TRUE;
		hal->fw_ractrl = _TRUE;
		RTW_INFO("%s Download Firmware from %s success\n", __FUNCTION__, (fw_bin) ? "file" : "array");
		RTW_INFO("%s FW Version:%d SubVersion:%d FW size:%d\n", (wowlan) ? "WOW" : "NIC",
			hal->firmware_version, hal->firmware_sub_version, hal->firmware_size);
		return _SUCCESS;
	} else {
		hal->bFWReady = _FALSE;
		hal->fw_ractrl = _FALSE;
		RTW_ERR("%s Download Firmware from %s failed\n", __FUNCTION__, (fw_bin) ? "file" : "array");
		return _FAIL;
	}
}

u8 rtl8822b_get_rx_drv_info_size(struct _ADAPTER *a)
{
	struct dvobj_priv *d;
	u8 size = 80;	/* HALMAC_RX_DESC_DUMMY_SIZE_MAX_88XX */
	int err = 0;


	d = adapter_to_dvobj(a);

	err = rtw_halmac_get_rx_drv_info_sz(d, &size);
	if (err) {
		RTW_WARN(FUNC_ADPT_FMT ": Fail to get DRV INFO size!!(err=%d)\n",
			 FUNC_ADPT_ARG(a), err);
		size = 80;
	}

	return size;
}

u32 rtl8822b_get_tx_desc_size(struct _ADAPTER *a)
{
	struct dvobj_priv *d;
	u32 size = 48;	/* HALMAC_TX_DESC_SIZE_8822B */
	int err = 0;


	d = adapter_to_dvobj(a);

	err = rtw_halmac_get_tx_desc_size(d, &size);
	if (err) {
		RTW_WARN(FUNC_ADPT_FMT ": Fail to get TX Descriptor size!!(err=%d)\n",
			 FUNC_ADPT_ARG(a), err);
		size = 48;
	}

	return size;
}

u32 rtl8822b_get_rx_desc_size(struct _ADAPTER *a)
{
	struct dvobj_priv *d;
	u32 size = 24;	/* HALMAC_RX_DESC_SIZE_8822B */
	int err = 0;


	d = adapter_to_dvobj(a);

	err = rtw_halmac_get_rx_desc_size(d, &size);
	if (err) {
		RTW_WARN(FUNC_ADPT_FMT ": Fail to get RX Descriptor size!!(err=%d)\n",
			 FUNC_ADPT_ARG(a), err);
		size = 24;
	}

	return size;
}

/*
 * _rx_report - Get/Reset RX counter report
 * @a:		struct _ADAPTER*
 * @type:	rx report type
 * @reset:	reset counter or not
 *		0: read counter and don't reset counter
 *		1: reset counter only
 *
 * Get/Reset RX (error) report counter from hardware.
 *
 * Rteurn counter when reset==0, otherwise always return 0.
 */
u16 _rx_report(struct _ADAPTER *a, enum rx_rpt_type type, u8 reset)
{
	u32 sel = 0;
	u16 counter = 0;


	/* Rx packet counter report selection */
	sel = BIT_RXERR_RPT_SEL_V1_3_0_8822B(type);
	if (type & BIT(4))
		sel |= BIT_RXERR_RPT_SEL_V1_4_8822B;

	if (reset)
		sel |= BIT_RXERR_RPT_RST_8822B;

	rtw_write8(a, REG_RXERR_RPT_8822B + 3, (sel >> 24) & 0xFF);

	if (!reset)
		counter = rtw_read16(a, REG_RXERR_RPT_8822B);

	return counter;
}

/**
 * rtl8822b_rx_report_get - Get RX counter report
 * @a:		struct _ADAPTER*
 * @type:	rx report type
 *
 * Get RX (error) report counter from hardware.
 *
 * Rteurn counter for specific rx report.
 */
u16 rtl8822b_rx_report_get(struct _ADAPTER *a, enum rx_rpt_type type)
{
	return _rx_report(a, type, 0);
}

/**
 * rtl8822b_rx_report_reset - Reset RX counter report
 * @a:		struct _ADAPTER*
 *
 * Reset RX (error) report counter of hardware.
 */
void rtl8822b_rx_report_reset(struct _ADAPTER *a, enum rx_rpt_type type)
{
	_rx_report(a, type, 1);
}

