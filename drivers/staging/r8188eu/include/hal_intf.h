/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#ifndef __HAL_INTF_H__
#define __HAL_INTF_H__

#include "osdep_service.h"
#include "drv_types.h"
#include "Hal8188EPhyCfg.h"

enum hw_variables {
	HW_VAR_SET_OPMODE,
	HW_VAR_BASIC_RATE,
	HW_VAR_CORRECT_TSF,
	HW_VAR_MLME_SITESURVEY,
	HW_VAR_MLME_JOIN,
	HW_VAR_SLOT_TIME,
	HW_VAR_DM_FLAG,
	HW_VAR_DM_FUNC_OP,
	HW_VAR_DM_FUNC_RESET,
	HW_VAR_DM_FUNC_CLR,
	HW_VAR_AC_PARAM_BE,
	HW_VAR_AMPDU_FACTOR,
	HW_VAR_H2C_FW_PWRMODE,
	HW_VAR_INITIAL_GAIN,
	HW_VAR_FIFO_CLEARN_UP,
	HW_VAR_H2C_MEDIA_STATUS_RPT,
};

enum hal_def_variable {
	HAL_DEF_IS_SUPPORT_ANT_DIV,
	HAL_DEF_CURRENT_ANTENNA,
	HAL_DEF_DBG_DM_FUNC,/* for dbg */
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
