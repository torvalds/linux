/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#ifndef __HAL_INTF_H__
#define __HAL_INTF_H__

#include "osdep_service.h"
#include "drv_types.h"
#include "Hal8188EPhyCfg.h"

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

enum hw_variables {
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
	HW_VAR_RESP_SIFS,
	HW_VAR_ACK_PREAMBLE,
	HW_VAR_SEC_CFG,
	HW_VAR_BCN_VALID,
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
	HW_VAR_APFM_ON_MAC, /* Auto FSM to Turn On, include clock, isolation,
			     * power control for MAC only */
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
};

enum hal_odm_variable {
	HAL_ODM_STA_INFO,
	HAL_ODM_P2P_STATE,
	HAL_ODM_WIFI_DISPLAY_STATE,
};

enum hal_intf_ps_func {
	HAL_USB_SELECT_SUSPEND,
	HAL_MAX_ID,
};

typedef s32 (*c2h_id_filter)(u8 id);

struct hal_ops {
	u32	(*hal_power_on)(struct adapter *padapter);
	u32	(*hal_init)(struct adapter *padapter);
	u32	(*hal_deinit)(struct adapter *padapter);

	void	(*free_hal_data)(struct adapter *padapter);

	u32	(*inirp_init)(struct adapter *padapter);
	u32	(*inirp_deinit)(struct adapter *padapter);

	s32	(*init_xmit_priv)(struct adapter *padapter);

	s32	(*init_recv_priv)(struct adapter *padapter);
	void	(*free_recv_priv)(struct adapter *padapter);

	void	(*SetHwRegHandler)(struct adapter *padapter, u8	variable,
				   u8 *val);
	void	(*GetHwRegHandler)(struct adapter *padapter, u8	variable,
				   u8 *val);

	u8	(*GetHalDefVarHandler)(struct adapter *padapter,
				       enum hal_def_variable eVariable,
				       void *pValue);
	u8	(*SetHalDefVarHandler)(struct adapter *padapter,
				       enum hal_def_variable eVariable,
				       void *pValue);

	void	(*UpdateRAMaskHandler)(struct adapter *padapter,
				       u32 mac_id, u8 rssi_level);
	void	(*SetBeaconRelatedRegistersHandler)(struct adapter *padapter);

	void	(*Add_RateATid)(struct adapter *adapter, u32 bitmap, u8 arg,
				u8 rssi_level);
	void	(*run_thread)(struct adapter *adapter);
	void	(*cancel_thread)(struct adapter *adapter);

	u8	(*AntDivBeforeLinkHandler)(struct adapter *adapter);
	void	(*AntDivCompareHandler)(struct adapter *adapter,
					struct wlan_bssid_ex *dst,
					struct wlan_bssid_ex *src);

	s32	(*hal_xmit)(struct adapter *padapter,
			    struct xmit_frame *pxmitframe);
	s32 (*mgnt_xmit)(struct adapter *padapter,
			 struct xmit_frame *pmgntframe);

	u32	(*read_bbreg)(struct adapter *padapter, u32 RegAddr,
			      u32 BitMask);
	void	(*write_bbreg)(struct adapter *padapter, u32 RegAddr,
			       u32 BitMask, u32 Data);
	u32	(*read_rfreg)(struct adapter *padapter,
			      enum rf_radio_path eRFPath, u32 RegAddr,
			      u32 BitMask);
	void	(*write_rfreg)(struct adapter *padapter,
			       enum rf_radio_path eRFPath, u32 RegAddr,
			       u32 BitMask, u32 Data);

	void (*EfusePowerSwitch)(struct adapter *padapter, u8 bWrite,
				 u8 PwrState);
	void (*ReadEFuse)(struct adapter *padapter, u8 efuseType, u16 _offset,
			  u16 _size_byte, u8 *pbuf, bool bPseudoTest);
	void (*EFUSEGetEfuseDefinition)(struct adapter *padapter, u8 efuseType,
					u8 type, void *pOut, bool bPseudoTest);
	u16	(*EfuseGetCurrentSize)(struct adapter *padapter, u8 efuseType,
				       bool bPseudoTest);
	int	(*Efuse_PgPacketRead)(struct adapter *adapter, u8 offset,
				      u8 *data, bool bPseudoTest);
	int	(*Efuse_PgPacketWrite)(struct adapter *padapter, u8 offset,
				       u8 word_en, u8 *data, bool bPseudoTest);
	u8	(*Efuse_WordEnableDataWrite)(struct adapter *padapter,
					     u16 efuse_addr, u8 word_en,
					     u8 *data, bool bPseudoTest);

	void (*sreset_init_value)(struct adapter *padapter);
	void (*sreset_reset_value)(struct adapter *padapter);
	void (*silentreset)(struct adapter *padapter);
	void (*sreset_xmit_status_check)(struct adapter *padapter);
	void (*sreset_linked_status_check) (struct adapter *padapter);
	u8 (*sreset_get_wifi_status)(struct adapter *padapter);

	int (*IOL_exec_cmds_sync)(struct adapter *padapter,
				  struct xmit_frame *frame, u32 max_wait,
				  u32 bndy_cnt);

	void (*hal_notch_filter)(struct adapter *adapter, bool enable);
};

#define RF_CHANGE_BY_INIT	0
#define RF_CHANGE_BY_IPS	BIT(28)
#define RF_CHANGE_BY_PS		BIT(29)
#define RF_CHANGE_BY_HW		BIT(30)
#define RF_CHANGE_BY_SW		BIT(31)

#define HARDWARE_TYPE_RTL8188EU 17

#define GET_EEPROM_EFUSE_PRIV(adapter) (&adapter->eeprompriv)

#define is_boot_from_eeprom(adapter) (adapter->eeprompriv.EepromOrEfuse)

void rtl8188eu_set_hal_ops(struct adapter *padapter);

void rtl8188eu_interface_configure(struct adapter *adapt);
void ReadAdapterInfo8188EU(struct adapter *Adapter);
void rtl8188eu_init_default_value(struct adapter *adapt);
void rtl8188e_SetHalODMVar(struct adapter *Adapter,
			   enum hal_odm_variable eVariable, void *pValue1, bool bSet);

void	rtw_hal_free_data(struct adapter *padapter);

u32 rtw_hal_power_on(struct adapter *padapter);
uint rtw_hal_init(struct adapter *padapter);
uint rtw_hal_deinit(struct adapter *padapter);
void rtw_hal_stop(struct adapter *padapter);
void rtw_hal_set_hwreg(struct adapter *padapter, u8 variable, u8 *val);
void rtw_hal_get_hwreg(struct adapter *padapter, u8 variable, u8 *val);

u8 rtw_hal_set_def_var(struct adapter *padapter,
		       enum hal_def_variable eVariable, void *pValue);
u8 rtw_hal_get_def_var(struct adapter *padapter,
		       enum hal_def_variable eVariable, void *pValue);

u32	rtw_hal_inirp_init(struct adapter *padapter);
u32	rtw_hal_inirp_deinit(struct adapter *padapter);

s32	rtw_hal_xmit(struct adapter *padapter, struct xmit_frame *pxmitframe);
s32	rtw_hal_mgnt_xmit(struct adapter *padapter,
			  struct xmit_frame *pmgntframe);

s32	rtw_hal_init_xmit_priv(struct adapter *padapter);

s32	rtw_hal_init_recv_priv(struct adapter *padapter);
void	rtw_hal_free_recv_priv(struct adapter *padapter);

void rtw_hal_update_ra_mask(struct adapter *padapter, u32 mac_id, u8 level);
void	rtw_hal_add_ra_tid(struct adapter *adapt, u32 bitmap, u8 arg, u8 level);
void	rtw_hal_clone_data(struct adapter *dst_adapt,
			   struct adapter *src_adapt);
void	rtw_hal_start_thread(struct adapter *padapter);
void	rtw_hal_stop_thread(struct adapter *padapter);

void rtw_hal_bcn_related_reg_setting(struct adapter *padapter);

u32	rtw_hal_read_bbreg(struct adapter *padapter, u32 RegAddr, u32 BitMask);
void	rtw_hal_write_bbreg(struct adapter *padapter, u32 RegAddr, u32 BitMask,
			    u32 Data);
u32	rtw_hal_read_rfreg(struct adapter *padapter, enum rf_radio_path eRFPath,
			   u32 RegAddr, u32 BitMask);
void	rtw_hal_write_rfreg(struct adapter *padapter,
			    enum rf_radio_path eRFPath, u32 RegAddr,
			    u32 BitMask, u32 Data);

u8	rtw_hal_antdiv_before_linked(struct adapter *padapter);
void	rtw_hal_antdiv_rssi_compared(struct adapter *padapter,
				     struct wlan_bssid_ex *dst,
				     struct wlan_bssid_ex *src);

void rtw_hal_sreset_init(struct adapter *padapter);
void rtw_hal_sreset_reset(struct adapter *padapter);
void rtw_hal_sreset_reset_value(struct adapter *padapter);
void rtw_hal_sreset_xmit_status_check(struct adapter *padapter);
void rtw_hal_sreset_linked_status_check(struct adapter *padapter);
u8   rtw_hal_sreset_get_wifi_status(struct adapter *padapter);

int rtw_hal_iol_cmd(struct adapter  *adapter, struct xmit_frame *xmit_frame,
		    u32 max_wating_ms, u32 bndy_cnt);

void rtw_hal_notch_filter(struct adapter *adapter, bool enable);

void indicate_wx_scan_complete_event(struct adapter *padapter);
u8 rtw_do_join(struct adapter *padapter);

#endif /* __HAL_INTF_H__ */
