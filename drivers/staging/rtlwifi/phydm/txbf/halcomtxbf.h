/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef __HAL_COM_TXBF_H__
#define __HAL_COM_TXBF_H__

enum txbf_set_type {
	TXBF_SET_SOUNDING_ENTER,
	TXBF_SET_SOUNDING_LEAVE,
	TXBF_SET_SOUNDING_RATE,
	TXBF_SET_SOUNDING_STATUS,
	TXBF_SET_SOUNDING_FW_NDPA,
	TXBF_SET_SOUNDING_CLK,
	TXBF_SET_TX_PATH_RESET,
	TXBF_SET_GET_TX_RATE
};

enum txbf_get_type {
	TXBF_GET_EXPLICIT_BEAMFORMEE,
	TXBF_GET_EXPLICIT_BEAMFORMER,
	TXBF_GET_MU_MIMO_STA,
	TXBF_GET_MU_MIMO_AP
};

/* 2 HAL TXBF related */
struct _HAL_TXBF_INFO {
	u8 txbf_idx;
	u8 ndpa_idx;
	u8 BW;
	u8 rate;

	struct timer_list txbf_fw_ndpa_timer;
};

#define hal_com_txbf_beamform_init(dm_void) NULL
#define hal_com_txbf_config_gtab(dm_void) NULL
#define hal_com_txbf_enter_work_item_callback(_adapter) NULL
#define hal_com_txbf_leave_work_item_callback(_adapter) NULL
#define hal_com_txbf_fw_ndpa_work_item_callback(_adapter) NULL
#define hal_com_txbf_clk_work_item_callback(_adapter) NULL
#define hal_com_txbf_rate_work_item_callback(_adapter) NULL
#define hal_com_txbf_fw_ndpa_timer_callback(_adapter) NULL
#define hal_com_txbf_status_work_item_callback(_adapter) NULL
#define hal_com_txbf_get(_adapter, _get_type, _pout_buf)

#endif /*  #ifndef __HAL_COM_TXBF_H__ */
