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
#ifndef __HAL_INIT_H__
#define __HAL_INIT_H__

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#ifdef CONFIG_PCI_HCI
#include <pci_hal.h>
#endif


enum RTL871X_HCI_TYPE {
	RTW_PCIE,
	RTW_USB,
	RTW_SDIO
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


typedef enum _HW_VARIABLES{
	HW_VAR_MEDIA_STATUS,
	HW_VAR_MEDIA_STATUS1,
	HW_VAR_SET_OPMODE,
	HW_VAR_MAC_ADDR,
	HW_VAR_BSSID,
	HW_VAR_INIT_RTS_RATE,
	HW_VAR_BASIC_RATE,
	HW_VAR_TXPAUSE,
	HW_VAR_BCN_FUNC,
	HW_VAR_CORRECT_TSF,
	HW_VAR_CHECK_BSSID,
	HW_VAR_MLME_DISCONNECT,
	HW_VAR_MLME_SITESURVEY,
	HW_VAR_MLME_JOIN,
	HW_VAR_BEACON_INTERVAL,
	HW_VAR_SLOT_TIME,
	HW_VAR_SIFS,
	HW_VAR_ACK_PREAMBLE,
	HW_VAR_SEC_CFG,
	HW_VAR_TX_BCN_DONE,
	HW_VAR_RF_TYPE,
	HW_VAR_DM_FLAG,
	HW_VAR_DM_FUNC_OP,
	HW_VAR_DM_FUNC_SET,
	HW_VAR_DM_FUNC_CLR,
	HW_VAR_CAM_EMPTY_ENTRY,
	HW_VAR_CAM_INVALID_ALL,
	HW_VAR_CAM_WRITE,
	HW_VAR_CAM_READ,
	HW_VAR_AC_PARAM_VO,
	HW_VAR_AC_PARAM_VI,
	HW_VAR_AC_PARAM_BE,
	HW_VAR_AC_PARAM_BK,
	HW_VAR_ACM_CTRL,
	HW_VAR_AMPDU_MIN_SPACE,
	HW_VAR_AMPDU_FACTOR,
	HW_VAR_RXDMA_AGG_PG_TH,
	HW_VAR_SET_RPWM,
	HW_VAR_H2C_FW_PWRMODE,
	HW_VAR_H2C_FW_JOINBSSRPT,
	HW_VAR_FWLPS_RF_ON,
	HW_VAR_H2C_FW_P2P_PS_OFFLOAD,
	HW_VAR_TDLS_WRCR,
	HW_VAR_TDLS_INIT_CH_SEN,
	HW_VAR_TDLS_RS_RCR,
	HW_VAR_TDLS_DONE_CH_SEN,
	HW_VAR_INITIAL_GAIN,
	HW_VAR_TRIGGER_GPIO_0,
	HW_VAR_BT_SET_COEXIST,
	HW_VAR_BT_ISSUE_DELBA,	
	HW_VAR_CURRENT_ANTENNA,
	HW_VAR_ANTENNA_DIVERSITY_LINK,
	HW_VAR_ANTENNA_DIVERSITY_SELECT,
	HW_VAR_SWITCH_EPHY_WoWLAN,
	HW_VAR_EFUSE_USAGE,
	HW_VAR_EFUSE_BYTES,
	HW_VAR_EFUSE_BT_USAGE,
	HW_VAR_EFUSE_BT_BYTES,
	HW_VAR_FIFO_CLEARN_UP,
	HW_VAR_CHECK_TXBUF,
	HW_VAR_APFM_ON_MAC, //Auto FSM to Turn On, include clock, isolation, power control for MAC only
	// The valid upper nav range for the HW updating, if the true value is larger than the upper range, the HW won't update it.
	// Unit in microsecond. 0 means disable this function.
	HW_VAR_NAV_UPPER,
	HW_VAR_C2H_HANDLE,
	HW_VAR_RPT_TIMER_SETTING,
	HW_VAR_TX_RPT_MAX_MACID,	
	HW_VAR_H2C_MEDIA_STATUS_RPT,
}HW_VARIABLES;

typedef enum _HAL_DEF_VARIABLE{
	HAL_DEF_UNDERCORATEDSMOOTHEDPWDB,
	HAL_DEF_IS_SUPPORT_ANT_DIV,
	HAL_DEF_CURRENT_ANTENNA,
	HAL_DEF_DRVINFO_SZ,
	HAL_DEF_MAX_RECVBUF_SZ,
	HAL_DEF_RX_PACKET_OFFSET,
	HAL_DEF_DBG_DUMP_RXPKT,//for dbg
	HAL_DEF_DBG_DM_FUNC,//for dbg
	HAL_DEF_RA_DECISION_RATE,
	HAL_DEF_RA_SGI,
	HAL_DEF_PT_PWR_STATUS,
	HW_VAR_MAX_RX_AMPDU_FACTOR,
	HW_DEF_RA_INFO_DUMP,
	HAL_DEF_DBG_DUMP_TXPKT,
}HAL_DEF_VARIABLE;

typedef enum _HAL_ODM_VARIABLE{
	HAL_ODM_STA_INFO,	
	HAL_ODM_P2P_STATE,
	HAL_ODM_WIFI_DISPLAY_STATE,
}HAL_ODM_VARIABLE;

typedef enum _HAL_INTF_PS_FUNC{
	HAL_USB_SELECT_SUSPEND,
	HAL_MAX_ID,
}HAL_INTF_PS_FUNC;

struct hal_ops {
	u32	(*hal_init)(PADAPTER Adapter);
	u32	(*hal_deinit)(PADAPTER Adapter);

	void	(*free_hal_data)(PADAPTER Adapter);

	u32	(*inirp_init)(PADAPTER Adapter);
	u32	(*inirp_deinit)(PADAPTER Adapter);

	s32	(*init_xmit_priv)(PADAPTER Adapter);
	void	(*free_xmit_priv)(PADAPTER Adapter);

	s32	(*init_recv_priv)(PADAPTER Adapter);
	void	(*free_recv_priv)(PADAPTER Adapter);

	void	(*InitSwLeds)(PADAPTER Adapter);
	void	(*DeInitSwLeds)(PADAPTER Adapter);

	void	(*dm_init)(PADAPTER Adapter);
	void	(*dm_deinit)(PADAPTER Adapter);
	void	(*read_chip_version)(PADAPTER Adapter);

	void	(*init_default_value)(PADAPTER Adapter);

	void	(*intf_chip_configure)(PADAPTER Adapter);

	void	(*read_adapter_info)(PADAPTER Adapter);

	void	(*enable_interrupt)(PADAPTER Adapter);
	void	(*disable_interrupt)(PADAPTER Adapter);
	s32	(*interrupt_handler)(PADAPTER Adapter);

	void	(*set_bwmode_handler)(PADAPTER Adapter, HT_CHANNEL_WIDTH Bandwidth, u8 Offset);
	void	(*set_channel_handler)(PADAPTER Adapter, u8 channel);

	void	(*hal_dm_watchdog)(PADAPTER Adapter);

	void	(*SetHwRegHandler)(PADAPTER Adapter, u8	variable,u8* val);
	void	(*GetHwRegHandler)(PADAPTER Adapter, u8	variable,u8* val);

	u8	(*GetHalDefVarHandler)(PADAPTER Adapter, HAL_DEF_VARIABLE eVariable, PVOID pValue);
	u8	(*SetHalDefVarHandler)(PADAPTER Adapter, HAL_DEF_VARIABLE eVariable, PVOID pValue);

	void	(*GetHalODMVarHandler)(PADAPTER Adapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,BOOLEAN bSet);
	void	(*SetHalODMVarHandler)(PADAPTER Adapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,BOOLEAN bSet);

	void	(*UpdateRAMaskHandler)(PADAPTER Adapter, u32 mac_id);
	void	(*SetBeaconRelatedRegistersHandler)(PADAPTER Adapter);

	void	(*Add_RateATid)(PADAPTER Adapter, u32 bitmap, u8 arg);

#ifdef CONFIG_ANTENNA_DIVERSITY
	u8	(*SwAntDivBeforeLinkHandler)(PADAPTER Adapter);
	void	(*SwAntDivCompareHandler)(PADAPTER Adapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src);
#endif
	u8	(*interface_ps_func)(PADAPTER Adapter,HAL_INTF_PS_FUNC efunc_id, u8* val);

	s32	(*hal_xmit)(PADAPTER Adapter, struct xmit_frame *pxmitframe);
	void	(*mgnt_xmit)(PADAPTER Adapter, struct xmit_frame *pmgntframe);

	u32	(*read_bbreg)(PADAPTER Adapter, u32 RegAddr, u32 BitMask);
	void	(*write_bbreg)(PADAPTER Adapter, u32 RegAddr, u32 BitMask, u32 Data);
	u32	(*read_rfreg)(PADAPTER Adapter, u32 eRFPath, u32 RegAddr, u32 BitMask);
	void	(*write_rfreg)(PADAPTER Adapter, u32 eRFPath, u32 RegAddr, u32 BitMask, u32 Data);

#ifdef CONFIG_HOSTAPD_MLME
	s32	(*hostap_mgnt_xmit_entry)(PADAPTER Adapter, _pkt *pkt);
#endif

	void (*EfusePowerSwitch)(PADAPTER pAdapter, u8 bWrite, u8 PwrState);
	void (*ReadEFuse)(PADAPTER Adapter, u8 efuseType, u16 _offset, u16 _size_byte, u8 *pbuf, BOOLEAN bPseudoTest);
	void (*EFUSEGetEfuseDefinition)(PADAPTER pAdapter, u8 efuseType, u8 type, void *pOut, BOOLEAN bPseudoTest);
	u16	(*EfuseGetCurrentSize)(PADAPTER pAdapter, u8 efuseType, BOOLEAN bPseudoTest);
	int 	(*Efuse_PgPacketRead)(PADAPTER pAdapter, u8 offset, u8 *data, BOOLEAN bPseudoTest);
	int 	(*Efuse_PgPacketWrite)(PADAPTER pAdapter, u8 offset, u8 word_en, u8 *data, BOOLEAN bPseudoTest);
	u8	(*Efuse_WordEnableDataWrite)(PADAPTER pAdapter, u16 efuse_addr, u8 word_en, u8 *data, BOOLEAN bPseudoTest);
	BOOLEAN	(*Efuse_PgPacketWrite_BT)(PADAPTER pAdapter, u8 offset, u8 word_en, u8 *data, BOOLEAN bPseudoTest);
	
#ifdef DBG_CONFIG_ERROR_DETECT
	void (*sreset_init_value)(_adapter *padapter);
	void (*sreset_reset_value)(_adapter *padapter);		
	void (*silentreset)(_adapter *padapter);
	void (*sreset_xmit_status_check)(_adapter *padapter);
	void (*sreset_linked_status_check) (_adapter *padapter);
	u8 (*sreset_get_wifi_status)(_adapter *padapter);
#endif

#ifdef CONFIG_IOL
	int (*IOL_exec_cmds_sync)(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms);
#endif
};

typedef	enum _RT_EEPROM_TYPE{
	EEPROM_93C46,
	EEPROM_93C56,
	EEPROM_BOOT_EFUSE,
}RT_EEPROM_TYPE,*PRT_EEPROM_TYPE;

#define USB_HIGH_SPEED_BULK_SIZE	512
#define USB_FULL_SPEED_BULK_SIZE	64

#define RF_CHANGE_BY_INIT	0
#define RF_CHANGE_BY_IPS 	BIT28
#define RF_CHANGE_BY_PS 	BIT29
#define RF_CHANGE_BY_HW 	BIT30
#define RF_CHANGE_BY_SW 	BIT31

typedef enum _HARDWARE_TYPE{
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
}HARDWARE_TYPE;

//
// RTL8192C Series
//
#define IS_HARDWARE_TYPE_8192CE(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8192CE)
#define IS_HARDWARE_TYPE_8192CU(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8192CU)
#define	IS_HARDWARE_TYPE_8192C(_Adapter)			\
(IS_HARDWARE_TYPE_8192CE(_Adapter) || IS_HARDWARE_TYPE_8192CU(_Adapter))

//
// RTL8192D Series
//
#define IS_HARDWARE_TYPE_8192DE(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8192DE)
#define IS_HARDWARE_TYPE_8192DU(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8192DU)
#define	IS_HARDWARE_TYPE_8192D(_Adapter)			\
(IS_HARDWARE_TYPE_8192DE(_Adapter) || IS_HARDWARE_TYPE_8192DU(_Adapter))

//
// RTL8723A Series
//
#define IS_HARDWARE_TYPE_8723AE(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8723AE)
#define IS_HARDWARE_TYPE_8723AU(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8723AU)
#define IS_HARDWARE_TYPE_8723AS(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8723AS)
#define	IS_HARDWARE_TYPE_8723A(_Adapter)	\
(IS_HARDWARE_TYPE_8723AE(_Adapter) || IS_HARDWARE_TYPE_8723AU(_Adapter) || IS_HARDWARE_TYPE_8723AS(_Adapter))

//
// RTL8188E Series
//
#define IS_HARDWARE_TYPE_8188EE(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8188EE)
#define IS_HARDWARE_TYPE_8188EU(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8188EU)
#define IS_HARDWARE_TYPE_8188ES(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8188ES)
#define	IS_HARDWARE_TYPE_8188E(_Adapter)	\
(IS_HARDWARE_TYPE_8188EE(_Adapter) || IS_HARDWARE_TYPE_8188EU(_Adapter) || IS_HARDWARE_TYPE_8188ES(_Adapter))


typedef struct eeprom_priv EEPROM_EFUSE_PRIV, *PEEPROM_EFUSE_PRIV;
#define GET_EEPROM_EFUSE_PRIV(priv)	(&priv->eeprompriv)


void rtw_dm_init(_adapter *padapter);
void rtw_dm_deinit(_adapter *padapter);
void rtw_sw_led_init(_adapter *padapter);
void rtw_sw_led_deinit(_adapter *padapter);

uint rtw_hal_init(_adapter *padapter);
uint rtw_hal_deinit(_adapter *padapter);
void rtw_hal_stop(_adapter *padapter);
void rtw_hal_SetHwReg(PADAPTER padapter, u8 variable, u8 *val);
void rtw_hal_GetHwReg(PADAPTER padapter, u8 variable, u8 *val);

void intf_chip_configure(_adapter *padapter);
void intf_read_chip_info(_adapter *padapter);
void intf_read_chip_version(_adapter *padapter);

#ifdef DBG_CONFIG_ERROR_DETECT
void rtw_sreset_init(_adapter *padapter);
#endif

#endif //__HAL_INIT_H__

