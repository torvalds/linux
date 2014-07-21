/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 ******************************************************************************/
#ifndef __HAL_INTF_H__
#define __HAL_INTF_H__

#include <osdep_service.h>
#include <drv_types.h>

enum RTL871X_HCI_TYPE {
	RTW_PCIE	= BIT(0),
	RTW_USB		= BIT(1),
	RTW_SDIO	= BIT(2),
	RTW_GSPI	= BIT(3),
};

enum _CHIP_TYPE {
	NULL_CHIP_TYPE,
	RTL8712_8188S_8191S_8192S,
	RTL8188C_8192C,
	RTL8192D,
	RTL8723A,
	RTL8188E,
	MAX_CHIP_TYPE
};

enum hal_def_variable {
	HAL_DEF_UNDERCORATEDSMOOTHEDPWDB,
	HAL_DEF_IS_SUPPORT_ANT_DIV,
	HAL_DEF_CURRENT_ANTENNA,
	HAL_DEF_DRVINFO_SZ,
	HAL_DEF_MAX_RECVBUF_SZ,
	HAL_DEF_RX_PACKET_OFFSET,
	HAL_DEF_DBG_DUMP_RXPKT,/* for dbg */
	HAL_DEF_DBG_DM_FUNC,/* for dbg */
	HAL_DEF_RA_DECISION_RATE,
	HAL_DEF_RA_SGI,
	HAL_DEF_PT_PWR_STATUS,
	HW_VAR_MAX_RX_AMPDU_FACTOR,
	HW_DEF_RA_INFO_DUMP,
	HAL_DEF_DBG_DUMP_TXPKT,
	HW_DEF_FA_CNT_DUMP,
	HW_DEF_ODM_DBG_FLAG,
};

enum hal_odm_variable {
	HAL_ODM_STA_INFO,
	HAL_ODM_P2P_STATE,
	HAL_ODM_WIFI_DISPLAY_STATE,
};

enum rt_eeprom_type {
	EEPROM_93C46,
	EEPROM_93C56,
	EEPROM_BOOT_EFUSE,
};



#define RF_CHANGE_BY_INIT	0
#define RF_CHANGE_BY_IPS	BIT(28)
#define RF_CHANGE_BY_PS		BIT(29)
#define RF_CHANGE_BY_HW		BIT(30)
#define RF_CHANGE_BY_SW		BIT(31)

enum hardware_type {
	HARDWARE_TYPE_RTL8180,
	HARDWARE_TYPE_RTL8185,
	HARDWARE_TYPE_RTL8187,
	HARDWARE_TYPE_RTL8188,
	HARDWARE_TYPE_RTL8190P,
	HARDWARE_TYPE_RTL8192E,
	HARDWARE_TYPE_RTL819xU,
	HARDWARE_TYPE_RTL8192SE,
	HARDWARE_TYPE_RTL8192SU,
	HARDWARE_TYPE_RTL8192CE,
	HARDWARE_TYPE_RTL8192CU,
	HARDWARE_TYPE_RTL8192DE,
	HARDWARE_TYPE_RTL8192DU,
	HARDWARE_TYPE_RTL8723AE,
	HARDWARE_TYPE_RTL8723AU,
	HARDWARE_TYPE_RTL8723AS,
	HARDWARE_TYPE_RTL8188EE,
	HARDWARE_TYPE_RTL8188EU,
	HARDWARE_TYPE_RTL8188ES,
	HARDWARE_TYPE_MAX,
};

#define GET_EEPROM_EFUSE_PRIV(adapter) (&adapter->eeprompriv)

void rtw_hal_def_value_init23a(struct rtw_adapter *padapter);
int pm_netdev_open23a(struct net_device *pnetdev, u8 bnormal);
int rtw_resume_process23a(struct rtw_adapter *padapter);

int rtl8723au_hal_init(struct rtw_adapter *padapter);
int rtl8723au_hal_deinit(struct rtw_adapter *padapter);
void rtw_hal_stop(struct rtw_adapter *padapter);

void rtw_hal_update_ra_mask23a(struct sta_info *psta, u8 rssi_level);
void	rtw_hal_clone_data(struct rtw_adapter *dst_padapter, struct rtw_adapter *src_padapter);

void hw_var_set_correct_tsf(struct rtw_adapter *padapter);
void hw_var_set_mlme_disconnect(struct rtw_adapter *padapter);
void hw_var_set_opmode(struct rtw_adapter *padapter, u8 mode);
void hw_var_set_macaddr(struct rtw_adapter *padapter, u8 *val);
void hw_var_set_bssid(struct rtw_adapter *padapter, u8 *val);
void hw_var_set_mlme_join(struct rtw_adapter *padapter, u8 type);

int GetHalDefVar8192CUsb(struct rtw_adapter *Adapter,
			 enum hal_def_variable eVariable, void *pValue);

#endif /* __HAL_INTF_H__ */
