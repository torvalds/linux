/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#ifndef __HAL_INTF_H__
#define __HAL_INTF_H__

#include "osdep_service.h"
#include "drv_types.h"
#include "Hal8188EPhyCfg.h"

enum hw_variables {
	HW_VAR_SET_OPMODE,
	HW_VAR_BSSID,
	HW_VAR_INIT_RTS_RATE,
	HW_VAR_BASIC_RATE,
	HW_VAR_CORRECT_TSF,
	HW_VAR_MLME_DISCONNECT,
	HW_VAR_MLME_SITESURVEY,
	HW_VAR_MLME_JOIN,
	HW_VAR_SLOT_TIME,
	HW_VAR_RESP_SIFS,
	HW_VAR_ACK_PREAMBLE,
	HW_VAR_BCN_VALID,
	HW_VAR_DM_FLAG,
	HW_VAR_DM_FUNC_OP,
	HW_VAR_DM_FUNC_SET,
	HW_VAR_DM_FUNC_CLR,
	HW_VAR_CAM_READ,
	HW_VAR_AC_PARAM_BE,
	HW_VAR_ACM_CTRL,
	HW_VAR_AMPDU_MIN_SPACE,
	HW_VAR_AMPDU_FACTOR,
	HW_VAR_RXDMA_AGG_PG_TH,
	HW_VAR_H2C_FW_PWRMODE,
	HW_VAR_H2C_FW_JOINBSSRPT,
	HW_VAR_FWLPS_RF_ON,
	HW_VAR_H2C_FW_P2P_PS_OFFLOAD,
	HW_VAR_TDLS_WRCR,
	HW_VAR_TDLS_INIT_CH_SEN,
	HW_VAR_TDLS_RS_RCR,
	HW_VAR_TDLS_DONE_CH_SEN,
	HW_VAR_INITIAL_GAIN,
	HW_VAR_BT_SET_COEXIST,
	HW_VAR_BT_ISSUE_DELBA,
	HW_VAR_ANTENNA_DIVERSITY_LINK,
	HW_VAR_ANTENNA_DIVERSITY_SELECT,
	HW_VAR_SWITCH_EPHY_WoWLAN,
	HW_VAR_EFUSE_USAGE,
	HW_VAR_EFUSE_BT_USAGE,
	HW_VAR_EFUSE_BT_BYTES,
	HW_VAR_FIFO_CLEARN_UP,
	/*  The valid upper nav range for the HW updating, if the true value is
	 *  larger than the upper range, the HW won't update it. */
	/*  Unit in microsecond. 0 means disable this function. */
	HW_VAR_NAV_UPPER,
	HW_VAR_RPT_TIMER_SETTING,
	HW_VAR_TX_RPT_MAX_MACID,
	HW_VAR_H2C_MEDIA_STATUS_RPT,
	HW_VAR_CHK_HI_QUEUE_EMPTY,
};

enum hal_def_variable {
	HAL_DEF_UNDERCORATEDSMOOTHEDPWDB,
	HAL_DEF_IS_SUPPORT_ANT_DIV,
	HAL_DEF_CURRENT_ANTENNA,
	HAL_DEF_DBG_DUMP_RXPKT,/* for dbg */
	HAL_DEF_DBG_DM_FUNC,/* for dbg */
	HW_DEF_RA_INFO_DUMP,
	HAL_DEF_DBG_DUMP_TXPKT,
};

typedef s32 (*c2h_id_filter)(u8 id);

#define is_boot_from_eeprom(adapter) (adapter->eeprompriv.EepromOrEfuse)

void rtl8188eu_interface_configure(struct adapter *adapt);
void ReadAdapterInfo8188EU(struct adapter *Adapter);
void rtl8188eu_init_default_value(struct adapter *adapt);
void rtl8188e_SetHalODMVar(struct adapter *Adapter, void *pValue1, bool bSet);
u32 rtl8188eu_InitPowerOn(struct adapter *adapt);
void rtl8188e_EfusePowerSwitch(struct adapter *pAdapter, u8 PwrState);
void rtl8188e_ReadEFuse(struct adapter *Adapter, u16 _offset, u16 _size_byte, u8 *pbuf);

void hal_notch_filter_8188e(struct adapter *adapter, bool enable);

void SetBeaconRelatedRegisters8188EUsb(struct adapter *adapt);
void UpdateHalRAMask8188EUsb(struct adapter *adapt, u32 mac_id, u8 rssi_level);

int rtl8188e_IOL_exec_cmds_sync(struct adapter *adapter,
				struct xmit_frame *xmit_frame, u32 max_wating_ms, u32 bndy_cnt);

void SetHalDefVar8188EUsb(struct adapter *Adapter, enum hal_def_variable eVariable, void *pValue);
void GetHalDefVar8188EUsb(struct adapter *Adapter, enum hal_def_variable eVariable, void *pValue);

unsigned int rtl8188eu_inirp_init(struct adapter *Adapter);

void SetHwReg8188EU(struct adapter *Adapter, u8 variable, u8 *val);
void GetHwReg8188EU(struct adapter *Adapter, u8 variable, u8 *val);

uint rtw_hal_init(struct adapter *padapter);
uint rtw_hal_deinit(struct adapter *padapter);
void rtw_hal_stop(struct adapter *padapter);

u32 rtl8188eu_hal_init(struct adapter *Adapter);
u32 rtl8188eu_hal_deinit(struct adapter *Adapter);

void rtw_hal_update_ra_mask(struct adapter *padapter, u32 mac_id, u8 level);
void	rtw_hal_clone_data(struct adapter *dst_adapt,
			   struct adapter *src_adapt);

void indicate_wx_scan_complete_event(struct adapter *padapter);
u8 rtw_do_join(struct adapter *padapter);

#endif /* __HAL_INTF_H__ */
