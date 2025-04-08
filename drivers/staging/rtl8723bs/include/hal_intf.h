/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __HAL_INTF_H__
#define __HAL_INTF_H__


enum {
	RTW_PCIE	= BIT0,
	RTW_USB		= BIT1,
	RTW_SDIO	= BIT2,
	RTW_GSPI	= BIT3,
};

enum {
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
	HW_VAR_ON_RCR_AM,
	HW_VAR_OFF_RCR_AM,
	HW_VAR_BEACON_INTERVAL,
	HW_VAR_SLOT_TIME,
	HW_VAR_RESP_SIFS,
	HW_VAR_ACK_PREAMBLE,
	HW_VAR_SEC_CFG,
	HW_VAR_SEC_DK_CFG,
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
	HW_VAR_CPWM,
	HW_VAR_H2C_FW_PWRMODE,
	HW_VAR_H2C_PS_TUNE_PARAM,
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
	HW_VAR_PCIE_STOP_TX_DMA,
	HW_VAR_APFM_ON_MAC, /* Auto FSM to Turn On, include clock, isolation, power control for MAC only */
	/*  The valid upper nav range for the HW updating, if the true value is larger than the upper range, the HW won't update it. */
	/*  Unit in microsecond. 0 means disable this function. */
	HW_VAR_SYS_CLKR,
	HW_VAR_NAV_UPPER,
	HW_VAR_C2H_HANDLE,
	HW_VAR_RPT_TIMER_SETTING,
	HW_VAR_TX_RPT_MAX_MACID,
	HW_VAR_H2C_MEDIA_STATUS_RPT,
	HW_VAR_CHK_HI_QUEUE_EMPTY,
	HW_VAR_DL_BCN_SEL,
	HW_VAR_AMPDU_MAX_TIME,
	HW_VAR_WIRELESS_MODE,
	HW_VAR_USB_MODE,
	HW_VAR_PORT_SWITCH,
	HW_VAR_DO_IQK,
	HW_VAR_DM_IN_LPS,
	HW_VAR_SET_REQ_FW_PS,
	HW_VAR_FW_PS_STATE,
	HW_VAR_SOUNDING_ENTER,
	HW_VAR_SOUNDING_LEAVE,
	HW_VAR_SOUNDING_RATE,
	HW_VAR_SOUNDING_STATUS,
	HW_VAR_SOUNDING_FW_NDPA,
	HW_VAR_SOUNDING_CLK,
	HW_VAR_DL_RSVD_PAGE,
	HW_VAR_MACID_SLEEP,
	HW_VAR_MACID_WAKEUP,
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
	HAL_DEF_TX_LDPC,				/*  LDPC support */
	HAL_DEF_RX_LDPC,				/*  LDPC support */
	HAL_DEF_TX_STBC,				/*  TX STBC support */
	HAL_DEF_RX_STBC,				/*  RX STBC support */
	HAL_DEF_EXPLICIT_BEAMFORMER,/*  Explicit  Compressed Steering Capable */
	HAL_DEF_EXPLICIT_BEAMFORMEE,/*  Explicit Compressed Beamforming Feedback Capable */
	HW_VAR_MAX_RX_AMPDU_FACTOR,
	HW_DEF_RA_INFO_DUMP,
	HAL_DEF_DBG_DUMP_TXPKT,
	HW_DEF_FA_CNT_DUMP,
	HW_DEF_ODM_DBG_FLAG,
	HW_DEF_ODM_DBG_LEVEL,
	HAL_DEF_TX_PAGE_SIZE,
	HAL_DEF_TX_PAGE_BOUNDARY,
	HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN,
	HAL_DEF_ANT_DETECT,/* to do for 8723a */
	HAL_DEF_PCI_SUUPORT_L1_BACKDOOR, /*  Determine if the L1 Backdoor setting is turned on. */
	HAL_DEF_PCI_AMD_L1_SUPPORT,
	HAL_DEF_PCI_ASPM_OSC, /*  Support for ASPM OSC, added by Roger, 2013.03.27. */
	HAL_DEF_MACID_SLEEP, /*  Support for MACID sleep */
};

enum hal_odm_variable {
	HAL_ODM_STA_INFO,
	HAL_ODM_P2P_STATE,
	HAL_ODM_WIFI_DISPLAY_STATE,
	HAL_ODM_NOISE_MONITOR,
};

enum hal_intf_ps_func {
	HAL_USB_SELECT_SUSPEND,
	HAL_MAX_ID,
};

typedef s32 (*c2h_id_filter)(u8 *c2h_evt);

struct hal_ops {
	void (*SetHalODMVarHandler)(struct adapter *padapter, enum hal_odm_variable eVariable, void *pValue1, bool bSet);

	u8 (*Efuse_WordEnableDataWrite)(struct adapter *padapter, u16 efuse_addr, u8 word_en, u8 *data, bool bPseudoTest);

	s32 (*xmit_thread_handler)(struct adapter *padapter);
	void (*hal_notch_filter)(struct adapter *adapter, bool enable);
	void (*hal_reset_security_engine)(struct adapter *adapter);
	s32 (*c2h_handler)(struct adapter *padapter, u8 *c2h_evt);
	c2h_id_filter c2h_id_filter_ccx;

	s32 (*fill_h2c_cmd)(struct adapter *, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);
};

#define RF_CHANGE_BY_INIT	0
#define RF_CHANGE_BY_IPS	BIT28
#define RF_CHANGE_BY_PS		BIT29
#define RF_CHANGE_BY_HW		BIT30
#define RF_CHANGE_BY_SW		BIT31

#define GET_EEPROM_EFUSE_PRIV(adapter) (&adapter->eeprompriv)
#define is_boot_from_eeprom(adapter) (adapter->eeprompriv.EepromOrEfuse)

#define Rx_Pairwisekey			0x01
#define Rx_GTK					0x02
#define Rx_DisAssoc				0x04
#define Rx_DeAuth				0x08
#define Rx_ARPReq				0x09
#define FWDecisionDisconnect	0x10
#define Rx_MagicPkt				0x21
#define Rx_UnicastPkt			0x22
#define Rx_PatternPkt			0x23
#define	RX_PNOWakeUp			0x55
#define	AP_WakeUp			0x66

void rtw_hal_def_value_init(struct adapter *padapter);

void rtw_hal_free_data(struct adapter *padapter);

void rtw_hal_dm_init(struct adapter *padapter);
void rtw_hal_dm_deinit(struct adapter *padapter);

uint rtw_hal_init(struct adapter *padapter);
uint rtw_hal_deinit(struct adapter *padapter);
void rtw_hal_stop(struct adapter *padapter);
void rtw_hal_set_hwreg(struct adapter *padapter, u8 variable, u8 *val);
void rtw_hal_get_hwreg(struct adapter *padapter, u8 variable, u8 *val);

void rtw_hal_set_hwreg_with_buf(struct adapter *padapter, u8 variable, u8 *pbuf, int len);

void rtw_hal_chip_configure(struct adapter *padapter);
void rtw_hal_read_chip_info(struct adapter *padapter);
void rtw_hal_read_chip_version(struct adapter *padapter);

u8 rtw_hal_set_def_var(struct adapter *padapter, enum hal_def_variable eVariable, void *pValue);
u8 rtw_hal_get_def_var(struct adapter *padapter, enum hal_def_variable eVariable, void *pValue);

void rtw_hal_set_odm_var(struct adapter *padapter, enum hal_odm_variable eVariable, void *pValue1, bool bSet);

void rtw_hal_enable_interrupt(struct adapter *padapter);
void rtw_hal_disable_interrupt(struct adapter *padapter);

u8 rtw_hal_check_ips_status(struct adapter *padapter);

s32	rtw_hal_xmitframe_enqueue(struct adapter *padapter, struct xmit_frame *pxmitframe);
s32	rtw_hal_xmit(struct adapter *padapter, struct xmit_frame *pxmitframe);
s32	rtw_hal_mgnt_xmit(struct adapter *padapter, struct xmit_frame *pmgntframe);

s32	rtw_hal_init_xmit_priv(struct adapter *padapter);
void rtw_hal_free_xmit_priv(struct adapter *padapter);

s32	rtw_hal_init_recv_priv(struct adapter *padapter);
void rtw_hal_free_recv_priv(struct adapter *padapter);

void rtw_hal_update_ra_mask(struct sta_info *psta, u8 rssi_level);
void rtw_hal_add_ra_tid(struct adapter *padapter, u32 bitmap, u8 *arg, u8 rssi_level);

void rtw_hal_start_thread(struct adapter *padapter);
void rtw_hal_stop_thread(struct adapter *padapter);

void beacon_timing_control(struct adapter *padapter);

u32 rtw_hal_read_bbreg(struct adapter *padapter, u32 RegAddr, u32 BitMask);
void rtw_hal_write_bbreg(struct adapter *padapter, u32 RegAddr, u32 BitMask, u32 Data);
u32 rtw_hal_read_rfreg(struct adapter *padapter, u32 eRFPath, u32 RegAddr, u32 BitMask);
void rtw_hal_write_rfreg(struct adapter *padapter, u32 eRFPath, u32 RegAddr, u32 BitMask, u32 Data);

#define PHY_QueryBBReg(Adapter, RegAddr, BitMask) rtw_hal_read_bbreg((Adapter), (RegAddr), (BitMask))
#define PHY_SetBBReg(Adapter, RegAddr, BitMask, Data) rtw_hal_write_bbreg((Adapter), (RegAddr), (BitMask), (Data))
#define PHY_QueryRFReg(Adapter, eRFPath, RegAddr, BitMask) rtw_hal_read_rfreg((Adapter), (eRFPath), (RegAddr), (BitMask))
#define PHY_SetRFReg(Adapter, eRFPath, RegAddr, BitMask, Data) rtw_hal_write_rfreg((Adapter), (eRFPath), (RegAddr), (BitMask), (Data))

#define PHY_SetMacReg	PHY_SetBBReg
#define PHY_QueryMacReg PHY_QueryBBReg

void rtw_hal_set_chan(struct adapter *padapter, u8 channel);
void rtw_hal_set_chnl_bw(struct adapter *padapter, u8 channel, enum channel_width Bandwidth, u8 Offset40, u8 Offset80);
void rtw_hal_dm_watchdog(struct adapter *padapter);
void rtw_hal_dm_watchdog_in_lps(struct adapter *padapter);

s32 rtw_hal_xmit_thread_handler(struct adapter *padapter);

void rtw_hal_notch_filter(struct adapter *adapter, bool enable);
void rtw_hal_reset_security_engine(struct adapter *adapter);

bool rtw_hal_c2h_valid(struct adapter *adapter, u8 *buf);
s32 rtw_hal_c2h_handler(struct adapter *adapter, u8 *c2h_evt);
c2h_id_filter rtw_hal_c2h_id_filter_ccx(struct adapter *adapter);

s32 rtw_hal_macid_sleep(struct adapter *padapter, u32 macid);
s32 rtw_hal_macid_wakeup(struct adapter *padapter, u32 macid);

s32 rtw_hal_fill_h2c_cmd(struct adapter *, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);

void SetHwReg8723BS(struct adapter *padapter, u8 variable, u8 *val);
void GetHwReg8723BS(struct adapter *padapter, u8 variable, u8 *val);
void SetHwRegWithBuf8723B(struct adapter *padapter, u8 variable, u8 *pbuf, int len);
u8 GetHalDefVar8723BSDIO(struct adapter *Adapter, enum hal_def_variable eVariable, void *pValue);
u8 SetHalDefVar8723BSDIO(struct adapter *Adapter, enum hal_def_variable eVariable, void *pValue);
void UpdateHalRAMask8723B(struct adapter *padapter, u32 mac_id, u8 rssi_level);
void rtl8723b_SetBeaconRelatedRegisters(struct adapter *padapter);
void Hal_EfusePowerSwitch(struct adapter *padapter, u8 bWrite, u8 PwrState);
void Hal_ReadEFuse(struct adapter *padapter, u8 efuseType, u16 _offset,
		   u16 _size_byte, u8 *pbuf, bool bPseudoTest);
void Hal_GetEfuseDefinition(struct adapter *padapter, u8 efuseType, u8 type,
			    void *pOut, bool bPseudoTest);
u16 Hal_EfuseGetCurrentSize(struct adapter *padapter, u8 efuseType, bool bPseudoTest);
#endif /* __HAL_INTF_H__ */
