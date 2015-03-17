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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __HAL_INTF_H__
#define __HAL_INTF_H__


enum RTL871X_HCI_TYPE {
	RTW_PCIE	= BIT0,
	RTW_USB 	= BIT1,
	RTW_SDIO 	= BIT2,
	RTW_GSPI	= BIT3,
};

enum _CHIP_TYPE {

	NULL_CHIP_TYPE,	
	RTL8188C_8192C,
	RTL8192D,
	RTL8723A,
	RTL8188E,
	RTL8192E,
	RTL8812,
	RTL8821, //RTL8811
	RTL8723B,
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
	HW_VAR_APFM_ON_MAC, //Auto FSM to Turn On, include clock, isolation, power control for MAC only
	// The valid upper nav range for the HW updating, if the true value is larger than the upper range, the HW won't update it.
	// Unit in microsecond. 0 means disable this function.
#ifdef CONFIG_WOWLAN
	HW_VAR_WOWLAN,
	HW_VAR_WAKEUP_REASON,
	HW_VAR_RPWM_TOG,
#endif
#ifdef CONFIG_AP_WOWLAN
	HW_VAR_AP_WOWLAN,
#endif
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
	HW_VAR_DUMP_MAC_QUEUE_INFO,
	HW_VAR_ASIX_IOT,
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
	HAL_DEF_TX_LDPC, 				// LDPC support
	HAL_DEF_RX_LDPC, 				// LDPC support
	HAL_DEF_TX_STBC, 				// TX STBC support
	HAL_DEF_RX_STBC, 				// RX STBC support
	HAL_DEF_EXPLICIT_BEAMFORMER,// Explicit  Compressed Steering Capable
	HAL_DEF_EXPLICIT_BEAMFORMEE,// Explicit Compressed Beamforming Feedback Capable
	HW_VAR_MAX_RX_AMPDU_FACTOR,
	HW_DEF_RA_INFO_DUMP,
	HAL_DEF_DBG_DUMP_TXPKT,
	HW_DEF_FA_CNT_DUMP,
	HW_DEF_ODM_DBG_FLAG,
	HW_DEF_ODM_DBG_LEVEL,
	HAL_DEF_TX_PAGE_SIZE,
	HAL_DEF_TX_PAGE_BOUNDARY,
	HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN,
	HAL_DEF_ANT_DETECT,//to do for 8723a
	HAL_DEF_PCI_SUUPORT_L1_BACKDOOR, // Determine if the L1 Backdoor setting is turned on.
	HAL_DEF_PCI_AMD_L1_SUPPORT,
	HAL_DEF_PCI_ASPM_OSC, // Support for ASPM OSC, added by Roger, 2013.03.27.
	HAL_DEF_MACID_SLEEP, // Support for MACID sleep
	HAL_DEF_DBG_RX_INFO_DUMP,
	HAL_DEF_DBG_DIS_PWT, //disable Tx power training or not.
}HAL_DEF_VARIABLE;

typedef enum _HAL_ODM_VARIABLE{
	HAL_ODM_STA_INFO,	
	HAL_ODM_P2P_STATE,
	HAL_ODM_WIFI_DISPLAY_STATE,
	HAL_ODM_NOISE_MONITOR,
	HAL_ODM_REGULATION,
}HAL_ODM_VARIABLE;

typedef enum _HAL_INTF_PS_FUNC{
	HAL_USB_SELECT_SUSPEND,
	HAL_MAX_ID,
}HAL_INTF_PS_FUNC;

typedef s32 (*c2h_id_filter)(u8 *c2h_evt);

struct hal_ops {
	u32	(*hal_power_on)(_adapter *padapter);
	void	(*hal_power_off)(_adapter *padapter);
	u32	(*hal_init)(_adapter *padapter);
	u32	(*hal_deinit)(_adapter *padapter);

	void	(*free_hal_data)(_adapter *padapter);

	u32	(*inirp_init)(_adapter *padapter);
	u32	(*inirp_deinit)(_adapter *padapter);
	void	(*irp_reset)(_adapter *padapter);

	s32	(*init_xmit_priv)(_adapter *padapter);
	void	(*free_xmit_priv)(_adapter *padapter);

	s32	(*init_recv_priv)(_adapter *padapter);
	void	(*free_recv_priv)(_adapter *padapter);

	void	(*InitSwLeds)(_adapter *padapter);
	void	(*DeInitSwLeds)(_adapter *padapter);

	void	(*dm_init)(_adapter *padapter);
	void	(*dm_deinit)(_adapter *padapter);
	void	(*read_chip_version)(_adapter *padapter);

	void	(*init_default_value)(_adapter *padapter);

	void	(*intf_chip_configure)(_adapter *padapter);

	void	(*read_adapter_info)(_adapter *padapter);

	void	(*enable_interrupt)(_adapter *padapter);
	void	(*disable_interrupt)(_adapter *padapter);
	u8		(*check_ips_status)(_adapter *padapter);
	s32		(*interrupt_handler)(_adapter *padapter);
	void    (*clear_interrupt)(_adapter *padapter);
	void	(*set_bwmode_handler)(_adapter *padapter, CHANNEL_WIDTH Bandwidth, u8 Offset);
	void	(*set_channel_handler)(_adapter *padapter, u8 channel);
	void	(*set_chnl_bw_handler)(_adapter *padapter, u8 channel, CHANNEL_WIDTH Bandwidth, u8 Offset40, u8 Offset80);

	void	(*set_tx_power_level_handler)(_adapter *padapter, u8 channel);
	void	(*get_tx_power_level_handler)(_adapter *padapter, s32 *powerlevel);

	void	(*hal_dm_watchdog)(_adapter *padapter);
	void	(*hal_dm_watchdog_in_lps)(_adapter *padapter);
	

	void	(*SetHwRegHandler)(_adapter *padapter, u8	variable,u8* val);
	void	(*GetHwRegHandler)(_adapter *padapter, u8	variable,u8* val);

#ifdef CONFIG_C2H_PACKET_EN
		void	(*SetHwRegHandlerWithBuf)(_adapter *padapter, u8 variable, u8* pbuf, int len);
#endif

	u8	(*GetHalDefVarHandler)(_adapter *padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue);
	u8	(*SetHalDefVarHandler)(_adapter *padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue);

	void	(*GetHalODMVarHandler)(_adapter *padapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,PVOID pValue2);
	void	(*SetHalODMVarHandler)(_adapter *padapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,BOOLEAN bSet);

	void	(*UpdateRAMaskHandler)(_adapter *padapter, u32 mac_id, u8 rssi_level);
	void	(*SetBeaconRelatedRegistersHandler)(_adapter *padapter);

	void	(*Add_RateATid)(_adapter *padapter, u32 bitmap, u8* arg, u8 rssi_level);

	void	(*run_thread)(_adapter *padapter);
	void	(*cancel_thread)(_adapter *padapter);

#ifdef CONFIG_ANTENNA_DIVERSITY
	u8	(*AntDivBeforeLinkHandler)(_adapter *padapter);
	void	(*AntDivCompareHandler)(_adapter *padapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src);
#endif
	u8	(*interface_ps_func)(_adapter *padapter,HAL_INTF_PS_FUNC efunc_id, u8* val);

	s32	(*hal_xmit)(_adapter *padapter, struct xmit_frame *pxmitframe);
	/*
	 * mgnt_xmit should be implemented to run in interrupt context
	 */
	s32 (*mgnt_xmit)(_adapter *padapter, struct xmit_frame *pmgntframe);
	s32	(*hal_xmitframe_enqueue)(_adapter *padapter, struct xmit_frame *pxmitframe);

	u32	(*read_bbreg)(_adapter *padapter, u32 RegAddr, u32 BitMask);
	void	(*write_bbreg)(_adapter *padapter, u32 RegAddr, u32 BitMask, u32 Data);
	u32	(*read_rfreg)(_adapter *padapter, u8 eRFPath, u32 RegAddr, u32 BitMask);
	void	(*write_rfreg)(_adapter *padapter, u8 eRFPath, u32 RegAddr, u32 BitMask, u32 Data);

#ifdef CONFIG_HOSTAPD_MLME
	s32	(*hostap_mgnt_xmit_entry)(_adapter *padapter, _pkt *pkt);
#endif

	void (*EfusePowerSwitch)(_adapter *padapter, u8 bWrite, u8 PwrState);
	void (*BTEfusePowerSwitch)(_adapter *padapter, u8 bWrite, u8 PwrState);
	void (*ReadEFuse)(_adapter *padapter, u8 efuseType, u16 _offset, u16 _size_byte, u8 *pbuf, BOOLEAN bPseudoTest);
	void (*EFUSEGetEfuseDefinition)(_adapter *padapter, u8 efuseType, u8 type, void *pOut, BOOLEAN bPseudoTest);
	u16	(*EfuseGetCurrentSize)(_adapter *padapter, u8 efuseType, BOOLEAN bPseudoTest);
	int 	(*Efuse_PgPacketRead)(_adapter *padapter, u8 offset, u8 *data, BOOLEAN bPseudoTest);
	int 	(*Efuse_PgPacketWrite)(_adapter *padapter, u8 offset, u8 word_en, u8 *data, BOOLEAN bPseudoTest);
	u8	(*Efuse_WordEnableDataWrite)(_adapter *padapter, u16 efuse_addr, u8 word_en, u8 *data, BOOLEAN bPseudoTest);
	BOOLEAN	(*Efuse_PgPacketWrite_BT)(_adapter *padapter, u8 offset, u8 word_en, u8 *data, BOOLEAN bPseudoTest);
	
#ifdef DBG_CONFIG_ERROR_DETECT
	void (*sreset_init_value)(_adapter *padapter);
	void (*sreset_reset_value)(_adapter *padapter);		
	void (*silentreset)(_adapter *padapter);
	void (*sreset_xmit_status_check)(_adapter *padapter);
	void (*sreset_linked_status_check) (_adapter *padapter);
	u8 (*sreset_get_wifi_status)(_adapter *padapter);
	bool (*sreset_inprogress)(_adapter *padapter);
#endif

#ifdef CONFIG_IOL
	int (*IOL_exec_cmds_sync)(_adapter *padapter, struct xmit_frame *xmit_frame, u32 max_wating_ms, u32 bndy_cnt);
#endif

#ifdef CONFIG_XMIT_THREAD_MODE
	s32 (*xmit_thread_handler)(_adapter *padapter);
#endif
	void (*hal_notch_filter)(_adapter * adapter, bool enable);
	void (*hal_reset_security_engine)(_adapter * adapter);
	s32 (*c2h_handler)(_adapter *padapter, u8 *c2h_evt);
	c2h_id_filter c2h_id_filter_ccx;
	s32 (*fill_h2c_cmd)(PADAPTER, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);
#if defined(CONFIG_RTL8188E) || defined(CONFIG_RTL8723A) || defined(CONFIG_RTL8723B)
	void (*hal_cal_txdesc_chksum)(struct tx_desc *ptxdesc);
#else
	void (*hal_cal_txdesc_chksum)(u8 *ptxdesc);
#endif
#ifdef CONFIG_WOWLAN
	void (*hal_set_wowlan_fw)(_adapter *adapter, u8 sleep);
#endif //CONFIG_WOWLAN
	u8 (*hal_get_tx_buff_rsvd_page_num)(_adapter *adapter, bool wowlan);
};

typedef	enum _RT_EEPROM_TYPE{
	EEPROM_93C46,
	EEPROM_93C56,
	EEPROM_BOOT_EFUSE,
}RT_EEPROM_TYPE,*PRT_EEPROM_TYPE;



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
//	NEW_GENERATION_IC
	HARDWARE_TYPE_RTL8192EE,
	HARDWARE_TYPE_RTL8192EU,
	HARDWARE_TYPE_RTL8192ES,
	HARDWARE_TYPE_RTL8812E,
	HARDWARE_TYPE_RTL8812AU,
	HARDWARE_TYPE_RTL8811AU,
	HARDWARE_TYPE_RTL8821E,
	HARDWARE_TYPE_RTL8821U,
	HARDWARE_TYPE_RTL8821S,
	HARDWARE_TYPE_RTL8723BE,
	HARDWARE_TYPE_RTL8723BU,
	HARDWARE_TYPE_RTL8723BS,
	HARDWARE_TYPE_RTL8813AE,
	HARDWARE_TYPE_RTL8813AU,
	HARDWARE_TYPE_RTL8813AS,

	HARDWARE_TYPE_MAX,
}HARDWARE_TYPE;

#define IS_NEW_GENERATION_IC(_Adapter)	(((PADAPTER)_Adapter)->HardwareType >=HARDWARE_TYPE_RTL8192EE)
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

#define IS_HARDWARE_TYPE_OLDER_THAN_8723A(_Adapter)	\
(IS_HARDWARE_TYPE_8192D(_Adapter) || IS_HARDWARE_TYPE_8192C(_Adapter))

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


#define	IS_HARDWARE_TYPE_8188E_before(_Adapter)			\
(IS_HARDWARE_TYPE_8192C(_Adapter) ||IS_HARDWARE_TYPE_8192D(_Adapter) ||IS_HARDWARE_TYPE_8723A(_Adapter))


#define IS_HARDWARE_TYPE_OLDER_THAN_8812A(_Adapter)	\
(IS_HARDWARE_TYPE_8192D(_Adapter) || IS_HARDWARE_TYPE_8192C(_Adapter) ||\
 IS_HARDWARE_TYPE_8723AE(_Adapter) || IS_HARDWARE_TYPE_8723AU(_Adapter) || IS_HARDWARE_TYPE_8723AS(_Adapter) ||\
 IS_HARDWARE_TYPE_8188EE(_Adapter) || IS_HARDWARE_TYPE_8188EU(_Adapter) || IS_HARDWARE_TYPE_8188ES(_Adapter)\
)

// RTL8812 Series
#define IS_HARDWARE_TYPE_8812E(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8812E)
#define IS_HARDWARE_TYPE_8812AU(_Adapter)	(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8812AU)
#define IS_HARDWARE_TYPE_8812(_Adapter)			\
(IS_HARDWARE_TYPE_8812E(_Adapter) || IS_HARDWARE_TYPE_8812AU(_Adapter))

// RTL8821 Series
#define IS_HARDWARE_TYPE_8821E(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8821E)
#define IS_HARDWARE_TYPE_8811AU(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8811AU)
#define IS_HARDWARE_TYPE_8821U(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8821U ||\
	              								 ((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8811AU)
#define IS_HARDWARE_TYPE_8821S(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8821S)
#define IS_HARDWARE_TYPE_8821(_Adapter)			\
(IS_HARDWARE_TYPE_8821E(_Adapter) || IS_HARDWARE_TYPE_8821U(_Adapter)|| IS_HARDWARE_TYPE_8821S(_Adapter))

#define IS_HARDWARE_TYPE_JAGUAR(_Adapter)		\
(IS_HARDWARE_TYPE_8812(_Adapter) || IS_HARDWARE_TYPE_8821(_Adapter))

//RTL8192E Series
#define IS_HARDWARE_TYPE_8192EE(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8192EE)
#define IS_HARDWARE_TYPE_8192EU(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8192EU)
#define IS_HARDWARE_TYPE_8192ES(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8192ES)

#define IS_HARDWARE_TYPE_8192E(_Adapter)		\
(IS_HARDWARE_TYPE_8192EE(_Adapter) || IS_HARDWARE_TYPE_8192EU(_Adapter) ||IS_HARDWARE_TYPE_8192ES(_Adapter))

#define IS_HARDWARE_TYPE_8723BE(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8723BE)
#define IS_HARDWARE_TYPE_8723BU(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8723BU)
#define IS_HARDWARE_TYPE_8723BS(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8723BS)


#define IS_HARDWARE_TYPE_8723B(_Adapter) \
	(IS_HARDWARE_TYPE_8723BE(_Adapter) || IS_HARDWARE_TYPE_8723BU(_Adapter) ||IS_HARDWARE_TYPE_8723BS(_Adapter))

//RTL8813A Series
#define IS_HARDWARE_TYPE_8813AE(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8813AE)
#define IS_HARDWARE_TYPE_8813AU(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8813AU)
#define IS_HARDWARE_TYPE_8813AS(_Adapter)		(((PADAPTER)_Adapter)->HardwareType==HARDWARE_TYPE_RTL8813AS)

#define IS_HARDWARE_TYPE_8813A(_Adapter)		\
(IS_HARDWARE_TYPE_8813AE(_Adapter) || IS_HARDWARE_TYPE_8813AU(_Adapter) ||IS_HARDWARE_TYPE_8813AS(_Adapter))


typedef struct eeprom_priv EEPROM_EFUSE_PRIV, *PEEPROM_EFUSE_PRIV;
#define GET_EEPROM_EFUSE_PRIV(adapter) (&adapter->eeprompriv)
#define is_boot_from_eeprom(adapter) (adapter->eeprompriv.EepromOrEfuse)

typedef enum _wowlan_subcode{
	WOWLAN_PATTERN_MATCH	= 1,
	WOWLAN_MAGIC_PACKET		= 2,
	WOWLAN_UNICAST			= 3,
	WOWLAN_SET_PATTERN		= 4,
	WOWLAN_DUMP_REG			= 5,
	WOWLAN_ENABLE			= 6,
	WOWLAN_DISABLE			= 7,
	WOWLAN_STATUS			= 8,
	WOWLAN_DEBUG_RELOAD_FW	= 9,
	WOWLAN_DEBUG_1			=10,
	WOWLAN_DEBUG_2			=11,
	WOWLAN_AP_ENABLE		=12,
	WOWLAN_AP_DISABLE		=13
}wowlan_subcode;

struct wowlan_ioctl_param{
	unsigned int subcode;
	unsigned int subcode_value;
	unsigned int wakeup_reason;
	unsigned int len;
	unsigned char pattern[0];
};

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

void rtw_hal_def_value_init(_adapter *padapter);

void	rtw_hal_free_data(_adapter *padapter);

void rtw_hal_dm_init(_adapter *padapter);
void rtw_hal_dm_deinit(_adapter *padapter);
void rtw_hal_sw_led_init(_adapter *padapter);
void rtw_hal_sw_led_deinit(_adapter *padapter);

u32 rtw_hal_power_on(_adapter *padapter);
void rtw_hal_power_off(_adapter *padapter);
uint rtw_hal_init(_adapter *padapter);
uint rtw_hal_deinit(_adapter *padapter);
void rtw_hal_stop(_adapter *padapter);
void rtw_hal_set_hwreg(PADAPTER padapter, u8 variable, u8 *val);
void rtw_hal_get_hwreg(PADAPTER padapter, u8 variable, u8 *val);

#ifdef CONFIG_C2H_PACKET_EN
void rtw_hal_set_hwreg_with_buf(_adapter *padapter, u8 variable, u8 *pbuf, int len);
#endif

void rtw_hal_chip_configure(_adapter *padapter);
void rtw_hal_read_chip_info(_adapter *padapter);
void rtw_hal_read_chip_version(_adapter *padapter);

u8 rtw_hal_set_def_var(_adapter *padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue);
u8 rtw_hal_get_def_var(_adapter *padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue);

void rtw_hal_set_odm_var(_adapter *padapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,BOOLEAN bSet);
void	rtw_hal_get_odm_var(_adapter *padapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,PVOID pValue2);
	
void rtw_hal_enable_interrupt(_adapter *padapter);
void rtw_hal_disable_interrupt(_adapter *padapter);

u8 rtw_hal_check_ips_status(_adapter *padapter);

u32	rtw_hal_inirp_init(_adapter *padapter);
u32	rtw_hal_inirp_deinit(_adapter *padapter);

void	rtw_hal_irp_reset(_adapter *padapter);

u8	rtw_hal_intf_ps_func(_adapter *padapter,HAL_INTF_PS_FUNC efunc_id, u8* val);

s32	rtw_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
s32	rtw_hal_xmit(_adapter *padapter, struct xmit_frame *pxmitframe);
s32	rtw_hal_mgnt_xmit(_adapter *padapter, struct xmit_frame *pmgntframe);

s32	rtw_hal_init_xmit_priv(_adapter *padapter);
void	rtw_hal_free_xmit_priv(_adapter *padapter);

s32	rtw_hal_init_recv_priv(_adapter *padapter);
void	rtw_hal_free_recv_priv(_adapter *padapter);

void rtw_hal_update_ra_mask(struct sta_info *psta, u8 rssi_level);
void	rtw_hal_add_ra_tid(_adapter *padapter, u32 bitmap, u8* arg, u8 rssi_level);

void	rtw_hal_start_thread(_adapter *padapter);
void	rtw_hal_stop_thread(_adapter *padapter);

void rtw_hal_bcn_related_reg_setting(_adapter *padapter);

u32	rtw_hal_read_bbreg(_adapter *padapter, u32 RegAddr, u32 BitMask);
void	rtw_hal_write_bbreg(_adapter *padapter, u32 RegAddr, u32 BitMask, u32 Data);
u32	rtw_hal_read_rfreg(_adapter *padapter, u32 eRFPath, u32 RegAddr, u32 BitMask);
void	rtw_hal_write_rfreg(_adapter *padapter, u32 eRFPath, u32 RegAddr, u32 BitMask, u32 Data);

#define PHY_QueryBBReg(Adapter, RegAddr, BitMask) rtw_hal_read_bbreg((Adapter), (RegAddr), (BitMask))
#define PHY_SetBBReg(Adapter, RegAddr, BitMask, Data) rtw_hal_write_bbreg((Adapter), (RegAddr), (BitMask), (Data))
#define PHY_QueryRFReg(Adapter, eRFPath, RegAddr, BitMask) rtw_hal_read_rfreg((Adapter), (eRFPath), (RegAddr), (BitMask))
#define PHY_SetRFReg(Adapter, eRFPath, RegAddr, BitMask, Data) rtw_hal_write_rfreg((Adapter), (eRFPath), (RegAddr), (BitMask), (Data))

#define PHY_SetMacReg	PHY_SetBBReg
#define PHY_QueryMacReg PHY_QueryBBReg

s32	rtw_hal_interrupt_handler(_adapter *padapter);

void	rtw_hal_set_bwmode(_adapter *padapter, CHANNEL_WIDTH Bandwidth, u8 Offset);
void	rtw_hal_set_chan(_adapter *padapter, u8 channel);
void	rtw_hal_set_chnl_bw(_adapter *padapter, u8 channel, CHANNEL_WIDTH Bandwidth, u8 Offset40, u8 Offset80);
void	rtw_hal_dm_watchdog(_adapter *padapter);
void	rtw_hal_dm_watchdog_in_lps(_adapter *padapter);

void	rtw_hal_set_tx_power_level(_adapter *padapter, u8 channel);
void	rtw_hal_get_tx_power_level(_adapter *padapter, s32 *powerlevel);

#ifdef CONFIG_ANTENNA_DIVERSITY
u8	rtw_hal_antdiv_before_linked(_adapter *padapter);
void	rtw_hal_antdiv_rssi_compared(_adapter *padapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src);
#endif

#ifdef CONFIG_HOSTAPD_MLME
s32	rtw_hal_hostap_mgnt_xmit_entry(_adapter *padapter, _pkt *pkt);
#endif

#ifdef DBG_CONFIG_ERROR_DETECT
void rtw_hal_sreset_init(_adapter *padapter);
void rtw_hal_sreset_reset(_adapter *padapter);
void rtw_hal_sreset_reset_value(_adapter *padapter);
void rtw_hal_sreset_xmit_status_check(_adapter *padapter);
void rtw_hal_sreset_linked_status_check (_adapter *padapter);
u8   rtw_hal_sreset_get_wifi_status(_adapter *padapter);
bool rtw_hal_sreset_inprogress(_adapter *padapter);
#endif

#ifdef CONFIG_IOL
int rtw_hal_iol_cmd(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms, u32 bndy_cnt);
#endif

#ifdef CONFIG_XMIT_THREAD_MODE
s32 rtw_hal_xmit_thread_handler(_adapter *padapter);
#endif

void rtw_hal_notch_filter(_adapter * adapter, bool enable);
void rtw_hal_reset_security_engine(_adapter * adapter);

bool rtw_hal_c2h_valid(_adapter *adapter, u8 *buf);
s32 rtw_hal_c2h_evt_read(_adapter *adapter, u8 *buf);
s32 rtw_hal_c2h_handler(_adapter *adapter, u8 *c2h_evt);
c2h_id_filter rtw_hal_c2h_id_filter_ccx(_adapter *adapter);

s32 rtw_hal_is_disable_sw_channel_plan(PADAPTER padapter);

s32 rtw_hal_macid_sleep(PADAPTER padapter, u8 macid);
s32 rtw_hal_macid_wakeup(PADAPTER padapter, u8 macid);

s32 rtw_hal_fill_h2c_cmd(PADAPTER, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);

#endif //__HAL_INTF_H__

