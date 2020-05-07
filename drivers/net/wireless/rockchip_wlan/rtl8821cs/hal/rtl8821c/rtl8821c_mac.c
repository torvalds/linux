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
#define _RTL8821C_MAC_C_

#include <drv_types.h>		/* PADAPTER, basic_types.h and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../hal_halmac.h"	/* Register Definition and etc. */

#ifdef CONFIG_XMIT_ACK
inline u8 rtl8821c_set_mgnt_xmit_ack(_adapter *adapter)
{
	int err;

	/*ack for xmit mgmt frames.*/
	err = rtw_write32(adapter, REG_FWHW_TXQ_CTRL_8821C, rtw_read32(adapter, REG_FWHW_TXQ_CTRL_8821C) | BIT(12));
	if (err == _FAIL)
		return _FAIL;

	return _SUCCESS;
}
#endif

inline u8 rtl8821c_rx_ba_ssn_appended(PADAPTER p)
{
	return rtw_hal_rcr_check(p, BIT_APP_BASSN_8821C);
}

inline u8 rtl8821c_rx_fcs_append_switch(PADAPTER p, u8 enable)
{
	u32 rcr_bit;
	u8 ret = _TRUE;

	rcr_bit = BIT_APP_FCS_8821C;
	if (_TRUE == enable)
		ret = rtw_hal_rcr_add(p, rcr_bit);
	else
		ret = rtw_hal_rcr_clear(p, rcr_bit);

	return ret;
}

inline u8 rtl8821c_rx_fcs_appended(PADAPTER p)
{
	return rtw_hal_rcr_check(p, BIT_APP_FCS_8821C);
}

u8 rtl8821c_rx_tsf_addr_filter_config(_adapter *adapter, u8 config)
{
	u8 v8;
	int err;

	v8 = GET_HAL_DATA(adapter)->rx_tsf_addr_filter_config;

	if (v8 != config) {

		err = rtw_write8(adapter, REG_NAN_RX_TSF_FILTER_8821C, config);
		if (_FAIL == err)
			return _FALSE;
	}

	GET_HAL_DATA(adapter)->rx_tsf_addr_filter_config = config;
	return _TRUE;
}

/*
 * Return:
 *	_SUCCESS	Download Firmware OK.
 *	_FAIL		Download Firmware FAIL!
 */
s32 rtl8821c_fw_dl(PADAPTER adapter, u8 wowlan)
{
	struct dvobj_priv *d = adapter_to_dvobj(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	int err;
	u8 fw_bin = _TRUE;

#ifdef CONFIG_FILE_FWIMG
	fw_bin = _TRUE;
	if (_TRUE == wowlan) {
		rtw_get_phy_file_path(adapter, MAC_FILE_FW_WW_IMG);
		err = rtw_halmac_dlfw_from_file(d, rtw_phy_para_file_path);
	} else {
		rtw_get_phy_file_path(adapter, MAC_FILE_FW_NIC);
		err = rtw_halmac_dlfw_from_file(d, rtw_phy_para_file_path);
	}
#else
	fw_bin = _FALSE;
	#ifdef CONFIG_WOWLAN
	if (_TRUE == wowlan)
		err = rtw_halmac_dlfw(d, array_mp_8821c_fw_wowlan, array_length_mp_8821c_fw_wowlan);
	else
	#endif
		err = rtw_halmac_dlfw(d, array_mp_8821c_fw_nic, array_length_mp_8821c_fw_nic);
#endif

	if (!err) {
		hal_data->bFWReady = _TRUE;
		hal_data->fw_ractrl = _TRUE;
		RTW_INFO("%s Download Firmware from %s success\n", __func__, (fw_bin) ? "file" : "array");
		RTW_INFO("%s FW Version:%d SubVersion:%d\n", (wowlan) ? "WOW" : "NIC", hal_data->firmware_version, hal_data->firmware_sub_version);

		return _SUCCESS;
	} else {
		RTW_ERR("%s Download Firmware from %s failed\n", __func__, (fw_bin) ? "file" : "array");
		return _FAIL;
	}
}
/*
 * Return:
 *	_SUCCESS	Download Firmware MEM OK.
 *	_FAIL		Download Firmware MEM FAIL!
 */
s32 rtl8821c_fw_mem_dl(PADAPTER adapter, enum fw_mem mem)
{
	struct dvobj_priv *d = adapter_to_dvobj(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	int err = 0;
	u8 fw_bin = _TRUE;

#ifdef CONFIG_FILE_FWIMG
	fw_bin = _TRUE;
	rtw_get_phy_file_path(adapter, MAC_FILE_FW_NIC);
	err = rtw_halmac_dlfw_mem_from_file(d, rtw_phy_para_file_path, mem);
#else
	fw_bin = _FALSE;
	err = rtw_halmac_dlfw_mem(d, array_mp_8821c_fw_nic, array_length_mp_8821c_fw_nic, mem);
#endif

	if (err) {
		RTW_ERR("%s Download Firmware MEM from %s failed\n", __func__, (fw_bin) ? "file" : "array");
		return _FAIL;
	}

	RTW_INFO("%s Download Firmware MEM from %s success\n", __func__, (fw_bin) ? "file" : "array");
	return _SUCCESS;

}
#ifdef CONFIG_AMPDU_PRETX_CD
#include "rtl8821c.h"
#define AMPDU_NUMBER				0x3F3F /*MAX AMPDU Number = 63*/
#define REG_PRECNT_CTRL_8821C		0x04E5
#define BIT_EN_PRECNT_8821C			BIT(11)
#define PRECNT_TH					0x1E4 /*6.05us*/

/* pre-tx count-down mechanism */
void rtl8821c_pretx_cd_config(_adapter *adapter)
{
	u8 burst_mode;
	u16 pre_cnt = PRECNT_TH | BIT_EN_PRECNT_8821C;

	/*Enable AMPDU PRE-TX, Reg0x4BC[6] = 1*/
	burst_mode = rtw_read8(adapter, REG_SW_AMPDU_BURST_MODE_CTRL_8821C);
	if (!(burst_mode & BIT_PRE_TX_CMD_8821C)) {
		burst_mode |= BIT_PRE_TX_CMD_8821C;
		rtw_write8(adapter, REG_SW_AMPDU_BURST_MODE_CTRL_8821C, burst_mode);
	}

	/*MAX AMPDU Number = 63, Reg0x4C8[21:16] = 0x3F*/
	rtw_write16(adapter, REG_PROT_MODE_CTRL_8821C + 2, AMPDU_NUMBER);

	/*Reg0x4E5[11] = 1, Reg0x4E5[10:0] = 0x1E4 */
	rtw_write8(adapter, REG_PRECNT_CTRL_8821C, (u8)(pre_cnt & 0xFF));
	rtw_write8(adapter, (REG_PRECNT_CTRL_8821C + 1), (u8)(pre_cnt >> 8));

	#if (defined(DBG_PRE_TX_HANG) && (defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)))
	rtw_write32(adapter, REG_HIMR1_8821C ,
		(rtw_read32(adapter, REG_HIMR1_8821C) | BIT_PRETXERR_HANDLE_IMR));
	#endif
}
#endif /*CONFIG_AMPDU_PRETX_CD*/
