/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
#ifndef __HAL_DATA_H__
#define __HAL_DATA_H__

#if 1/* def  CONFIG_SINGLE_IMG */

#include "../hal/phydm/phydm_precomp.h"
#ifdef CONFIG_BT_COEXIST
	#include <hal_btcoex.h>
#endif
	#include <hal_btcoex_wifionly.h>

#ifdef CONFIG_SDIO_HCI
	#include <hal_sdio.h>
#endif
#ifdef CONFIG_GSPI_HCI
	#include <hal_gspi.h>
#endif

#if defined(CONFIG_RTW_ACS) || defined(CONFIG_BACKGROUND_NOISE_MONITOR)
#include "../hal/hal_dm_acs.h"
#endif

/*
 * <Roger_Notes> For RTL8723 WiFi/BT/GPS multi-function configuration. 2010.10.06.
 *   */
typedef enum _RT_MULTI_FUNC {
	RT_MULTI_FUNC_NONE	= 0x00,
	RT_MULTI_FUNC_WIFI	= 0x01,
	RT_MULTI_FUNC_BT		= 0x02,
	RT_MULTI_FUNC_GPS	= 0x04,
} RT_MULTI_FUNC, *PRT_MULTI_FUNC;
/*
 * <Roger_Notes> For RTL8723 WiFi PDn/GPIO polarity control configuration. 2010.10.08.
 *   */
typedef enum _RT_POLARITY_CTL {
	RT_POLARITY_LOW_ACT	= 0,
	RT_POLARITY_HIGH_ACT	= 1,
} RT_POLARITY_CTL, *PRT_POLARITY_CTL;

/* For RTL8723 regulator mode. by tynli. 2011.01.14. */
typedef enum _RT_REGULATOR_MODE {
	RT_SWITCHING_REGULATOR	= 0,
	RT_LDO_REGULATOR			= 1,
} RT_REGULATOR_MODE, *PRT_REGULATOR_MODE;

/*
 * Interface type.
 *   */
typedef	enum _INTERFACE_SELECT_PCIE {
	INTF_SEL0_SOLO_MINICARD			= 0,		/* WiFi solo-mCard */
	INTF_SEL1_BT_COMBO_MINICARD		= 1,		/* WiFi+BT combo-mCard */
	INTF_SEL2_PCIe						= 2,		/* PCIe Card */
} INTERFACE_SELECT_PCIE, *PINTERFACE_SELECT_PCIE;


typedef	enum _INTERFACE_SELECT_USB {
	INTF_SEL0_USB 				= 0,		/* USB */
	INTF_SEL1_USB_High_Power  	= 1,		/* USB with high power PA */
	INTF_SEL2_MINICARD		  	= 2,		/* Minicard */
	INTF_SEL3_USB_Solo 		= 3,		/* USB solo-Slim module */
	INTF_SEL4_USB_Combo		= 4,		/* USB Combo-Slim module */
	INTF_SEL5_USB_Combo_MF	= 5,		/* USB WiFi+BT Multi-Function Combo, i.e., Proprietary layout(AS-VAU) which is the same as SDIO card */
} INTERFACE_SELECT_USB, *PINTERFACE_SELECT_USB;

typedef enum _RT_AMPDU_BRUST_MODE {
	RT_AMPDU_BRUST_NONE		= 0,
	RT_AMPDU_BRUST_92D		= 1,
	RT_AMPDU_BRUST_88E		= 2,
	RT_AMPDU_BRUST_8812_4	= 3,
	RT_AMPDU_BRUST_8812_8	= 4,
	RT_AMPDU_BRUST_8812_12	= 5,
	RT_AMPDU_BRUST_8812_15	= 6,
	RT_AMPDU_BRUST_8723B		= 7,
} RT_AMPDU_BRUST, *PRT_AMPDU_BRUST_MODE;

/* Tx Power Limit Table Size */
#define MAX_REGULATION_NUM						4
#define MAX_RF_PATH_NUM_IN_POWER_LIMIT_TABLE	4
#define MAX_2_4G_BANDWIDTH_NUM					2
#define MAX_RATE_SECTION_NUM						10
#define MAX_5G_BANDWIDTH_NUM						4

#define MAX_BASE_NUM_IN_PHY_REG_PG_2_4G			10 /* CCK:1, OFDM:1, HT:4, VHT:4 */
#define MAX_BASE_NUM_IN_PHY_REG_PG_5G			9 /* OFDM:1, HT:4, VHT:4 */

#ifdef RTW_RX_AGGREGATION
typedef enum _RX_AGG_MODE {
	RX_AGG_DISABLE,
	RX_AGG_DMA,
	RX_AGG_USB,
	RX_AGG_MIX
} RX_AGG_MODE;

/* #define MAX_RX_DMA_BUFFER_SIZE	10240 */		/* 10K for 8192C RX DMA buffer */

#endif /* RTW_RX_AGGREGATION */

/* E-Fuse */
#ifdef CONFIG_RTL8188E
	#define EFUSE_MAP_SIZE	512
#endif
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8814A)
	#define EFUSE_MAP_SIZE	512
#endif
#ifdef CONFIG_RTL8192E
	#define EFUSE_MAP_SIZE	512
#endif
#ifdef CONFIG_RTL8723B
	#define EFUSE_MAP_SIZE	512
#endif
#ifdef CONFIG_RTL8814A
	#define EFUSE_MAP_SIZE	512
#endif
#ifdef CONFIG_RTL8703B
	#define EFUSE_MAP_SIZE	512
#endif
#ifdef CONFIG_RTL8723D
	#define EFUSE_MAP_SIZE	512
#endif
#ifdef CONFIG_RTL8188F
	#define EFUSE_MAP_SIZE	512
#endif
#ifdef CONFIG_RTL8188GTV
	#define EFUSE_MAP_SIZE	512
#endif
#ifdef CONFIG_RTL8710B
	#define EFUSE_MAP_SIZE	512
#endif
#ifdef CONFIG_RTL8192F
	#define EFUSE_MAP_SIZE	512
#endif

#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)
	#define EFUSE_MAX_SIZE	1024
#elif defined(CONFIG_RTL8188E) || defined(CONFIG_RTL8188F) || defined(CONFIG_RTL8188GTV) || defined(CONFIG_RTL8703B) || defined(CONFIG_RTL8710B)
	#define EFUSE_MAX_SIZE	256
#else
	#define EFUSE_MAX_SIZE	512
#endif
/* end of E-Fuse */

#define Mac_OFDM_OK			0x00000000
#define Mac_OFDM_Fail		0x10000000
#define Mac_OFDM_FasleAlarm	0x20000000
#define Mac_CCK_OK			0x30000000
#define Mac_CCK_Fail		0x40000000
#define Mac_CCK_FasleAlarm	0x50000000
#define Mac_HT_OK			0x60000000
#define Mac_HT_Fail			0x70000000
#define Mac_HT_FasleAlarm	0x90000000
#define Mac_DropPacket		0xA0000000

#ifdef CONFIG_RF_POWER_TRIM
#if defined(CONFIG_RTL8723B)
	#define REG_RF_BB_GAIN_OFFSET	0x7f
	#define RF_GAIN_OFFSET_MASK		0xfffff
#elif defined(CONFIG_RTL8188E)
	#define REG_RF_BB_GAIN_OFFSET	0x55
	#define RF_GAIN_OFFSET_MASK		0xfffff
#else
	#define REG_RF_BB_GAIN_OFFSET	0x55
	#define RF_GAIN_OFFSET_MASK		0xfffff
#endif /* CONFIG_RTL8723B */
#endif /*CONFIG_RF_POWER_TRIM*/

/* For store initial value of BB register */
typedef struct _BB_INIT_REGISTER {
	u16	offset;
	u32	value;

} BB_INIT_REGISTER, *PBB_INIT_REGISTER;

#define PAGE_SIZE_128	128
#define PAGE_SIZE_256	256
#define PAGE_SIZE_512	512

#define HCI_SUS_ENTER		0
#define HCI_SUS_LEAVING		1
#define HCI_SUS_LEAVE		2
#define HCI_SUS_ENTERING	3
#define HCI_SUS_ERR			4

#define EFUSE_FILE_UNUSED 0
#define EFUSE_FILE_FAILED 1
#define EFUSE_FILE_LOADED 2

#define MACADDR_FILE_UNUSED 0
#define MACADDR_FILE_FAILED 1
#define MACADDR_FILE_LOADED 2

#define MAX_IQK_INFO_BACKUP_CHNL_NUM	5
#define MAX_IQK_INFO_BACKUP_REG_NUM		10

struct kfree_data_t {
	u8 flag;
	s8 bb_gain[BB_GAIN_NUM][RF_PATH_MAX];

#ifdef CONFIG_IEEE80211_BAND_5GHZ
	s8 pa_bias_5g[RF_PATH_MAX];
	s8 pad_bias_5g[RF_PATH_MAX];
#endif
	s8 thermal;
};

bool kfree_data_is_bb_gain_empty(struct kfree_data_t *data);

struct hal_spec_t {
	char *ic_name;
	u8 macid_num;

	u8 sec_cam_ent_num;
	u8 sec_cap;
	u8 macid_cap;
	u16 macid_txrpt;
	u8 macid_txrpt_pgsz;

	u8 rfpath_num_2g:4;	/* used for tx power index path */
	u8 rfpath_num_5g:4;	/* used for tx power index path */
	u8 txgi_max; /* maximum tx power gain index */
	u8 txgi_pdbm; /* tx power gain index per dBm */

	u8 max_tx_cnt;
	u8 tx_nss_num:4;
	u8 rx_nss_num:4;
	u8 band_cap;	/* value of BAND_CAP_XXX */
	u8 bw_cap;		/* value of BW_CAP_XXX */
	u8 port_num;
	u8 proto_cap;	/* value of PROTO_CAP_XXX */
	u8 wl_func;		/* value of WL_FUNC_XXX */

#if CONFIG_TX_AC_LIFETIME
	u8 tx_aclt_unit_factor; /* how many 32us */
#endif

	u8 rx_tsf_filter:1;

	u8 pg_txpwr_saddr; /* starting address of PG tx power info */
	u8 pg_txgi_diff_factor; /* PG tx power gain index diff to tx power gain index */

	u8 hci_type;	/* value of HCI Type */
};

#define HAL_SPEC_CHK_RF_PATH_2G(_spec, _path) ((_spec)->rfpath_num_2g > (_path))
#define HAL_SPEC_CHK_RF_PATH_5G(_spec, _path) ((_spec)->rfpath_num_5g > (_path))
#define HAL_SPEC_CHK_RF_PATH(_spec, _band, _path) ( \
	_band == BAND_ON_2_4G ? HAL_SPEC_CHK_RF_PATH_2G(_spec, _path) : \
	_band == BAND_ON_5G ? HAL_SPEC_CHK_RF_PATH_5G(_spec, _path) : 0)

#define HAL_SPEC_CHK_TX_CNT(_spec, _cnt_idx) ((_spec)->max_tx_cnt > (_cnt_idx))

#ifdef CONFIG_PHY_CAPABILITY_QUERY
struct phy_spec_t {
	u32 trx_cap;
	u32 stbc_cap;
	u32 ldpc_cap;
	u32 txbf_param;
	u32 txbf_cap;
};
#endif
struct hal_iqk_reg_backup {
	u8 central_chnl;
	u8 bw_mode;
	u32 reg_backup[MAX_RF_PATH][MAX_IQK_INFO_BACKUP_REG_NUM];
};


typedef struct hal_p2p_ps_para {
	/*DW0*/
	u8  offload_en:1;
	u8  role:1;
	u8  ctwindow_en:1;
	u8  noa_en:1;
	u8  noa_sel:1;
	u8  all_sta_sleep:1;
	u8  discovery:1;
	u8  disable_close_rf:1;
	u8  p2p_port_id;
	u8  p2p_group;
	u8  p2p_macid;

	/*DW1*/
	u8 ctwindow_length;
	u8 rsvd3;
	u8 rsvd4;
	u8 rsvd5;

	/*DW2*/
	u32 noa_duration_para;

	/*DW3*/
	u32 noa_interval_para;

	/*DW4*/
	u32 noa_start_time_para;

	/*DW5*/
	u32 noa_count_para;
} HAL_P2P_PS_PARA, *PHAL_P2P_PS_PARA;

#define TXPWR_LMT_RS_CCK	0
#define TXPWR_LMT_RS_OFDM	1
#define TXPWR_LMT_RS_HT		2
#define TXPWR_LMT_RS_VHT	3
#define TXPWR_LMT_RS_NUM	4

#define TXPWR_LMT_RS_NUM_2G	4 /* CCK, OFDM, HT, VHT */
#define TXPWR_LMT_RS_NUM_5G	3 /* OFDM, HT, VHT */

#if CONFIG_TXPWR_LIMIT
extern const char *const _txpwr_lmt_rs_str[];
#define txpwr_lmt_rs_str(rs) (((rs) >= TXPWR_LMT_RS_NUM) ? _txpwr_lmt_rs_str[TXPWR_LMT_RS_NUM] : _txpwr_lmt_rs_str[(rs)])

struct txpwr_lmt_ent {
	_list list;

	s8 lmt_2g[MAX_2_4G_BANDWIDTH_NUM]
		[TXPWR_LMT_RS_NUM_2G]
		[CENTER_CH_2G_NUM]
		[MAX_TX_COUNT];

#ifdef CONFIG_IEEE80211_BAND_5GHZ
	s8 lmt_5g[MAX_5G_BANDWIDTH_NUM]
		[TXPWR_LMT_RS_NUM_5G]
		[CENTER_CH_5G_ALL_NUM]
		[MAX_TX_COUNT];
#endif

	char regd_name[0];
};
#endif /* CONFIG_TXPWR_LIMIT */

typedef struct hal_com_data {
	HAL_VERSION			version_id;
	RT_MULTI_FUNC		MultiFunc; /* For multi-function consideration. */
	RT_POLARITY_CTL		PolarityCtl; /* For Wifi PDn Polarity control. */
	RT_REGULATOR_MODE	RegulatorMode; /* switching regulator or LDO */
	u8	hw_init_completed;
	/****** FW related ******/
	u32 firmware_size;
	u16 firmware_version;
	u16	FirmwareVersionRev;
	u16 firmware_sub_version;
	u16	FirmwareSignature;
	u8	RegFWOffload;
	u8	bFWReady;
	u8	bBTFWReady;
	u8	fw_ractrl;
	u8	LastHMEBoxNum;	/* H2C - for host message to fw */

	/****** current WIFI_PHY values ******/
	WIRELESS_MODE	CurrentWirelessMode;
	enum channel_width current_channel_bw;
	BAND_TYPE		current_band_type;	/* 0:2.4G, 1:5G */
	BAND_TYPE		BandSet;
	u8				current_channel;
	u8				cch_20;
	u8				cch_40;
	u8				cch_80;
	u8				CurrentCenterFrequencyIndex1;
	u8				nCur40MhzPrimeSC;	/* Control channel sub-carrier */
	u8				nCur80MhzPrimeSC;   /* used for primary 40MHz of 80MHz mode */
	BOOLEAN		bSwChnlAndSetBWInProgress;
	u8				bDisableSWChannelPlan; /* flag of disable software change channel plan	 */
	u16				BasicRateSet;
	u32				ReceiveConfig;
	u32				rcr_backup; /* used for switching back from monitor mode */
	u8				rx_tsf_addr_filter_config; /* for 8822B/8821C USE */
	BOOLEAN			bSwChnl;
	BOOLEAN			bSetChnlBW;
	BOOLEAN			bSWToBW40M;
	BOOLEAN			bSWToBW80M;
	BOOLEAN			bChnlBWInitialized;
	u32				BackUp_BB_REG_4_2nd_CCA[3];

#ifdef CONFIG_RTW_ACS
	struct auto_chan_sel acs;
#endif
#ifdef CONFIG_BCN_RECOVERY
	u8 issue_bcn_fail;
#endif /*CONFIG_BCN_RECOVERY*/

	/****** rf_ctrl *****/
	u8	rf_chip;
	u8	rf_type;	/*enum rf_type*/
	u8	PackageType;
	u8	NumTotalRFPath;
	u8	antenna_test;

	/****** Debug ******/
	u16	ForcedDataRate;	/* Force Data Rate. 0: Auto, 0x02: 1M ~ 0x6C: 54M. */
	u8	bDumpRxPkt;
	u8	bDumpTxPkt;
	u8	dis_turboedca; /* 1: disable turboedca, 
						  2: disable turboedca and setting EDCA parameter based on the input parameter*/
	u32 edca_param_mode;

	/****** EEPROM setting.******/
	u8	bautoload_fail_flag;
	u8	efuse_file_status;
	u8	macaddr_file_status;
	u8	EepromOrEfuse;
	u8	efuse_eeprom_data[EEPROM_MAX_SIZE]; /*92C:256bytes, 88E:512bytes, we use union set (512bytes)*/
	u8	InterfaceSel; /* board type kept in eFuse */
	u16	CustomerID;

	u16	EEPROMVID;
	u16	EEPROMSVID;
#ifdef CONFIG_USB_HCI
	u8	EEPROMUsbSwitch;
	u16	EEPROMPID;
	u16	EEPROMSDID;
#endif
#ifdef CONFIG_PCI_HCI
	u16	EEPROMDID;
	u16	EEPROMSMID;
#endif

	u8	EEPROMCustomerID;
	u8	EEPROMSubCustomerID;
	u8	EEPROMVersion;
	u8	EEPROMRegulatory;
	u8	eeprom_thermal_meter;
	u8	EEPROMBluetoothCoexist;
	u8	EEPROMBluetoothType;
	u8	EEPROMBluetoothAntNum;
	u8	EEPROMBluetoothAntIsolation;
	u8	EEPROMBluetoothRadioShared;
	u8	EEPROMMACAddr[ETH_ALEN];
	u8	tx_bbswing_24G;
	u8	tx_bbswing_5G;
	u8	efuse0x3d7;	/* efuse[0x3D7] */
	u8	efuse0x3d8;	/* efuse[0x3D8] */

#ifdef CONFIG_RF_POWER_TRIM
	u8	EEPROMRFGainOffset;
	u8	EEPROMRFGainVal;
	struct kfree_data_t kfree_data;
#endif /*CONFIG_RF_POWER_TRIM*/

#if defined(CONFIG_RTL8723B) || defined(CONFIG_RTL8703B) || \
	defined(CONFIG_RTL8723D) || \
	defined(CONFIG_RTL8192F)

	u8	adjuseVoltageVal;
	u8	need_restore;
#endif
	u8	EfuseUsedPercentage;
	u16	EfuseUsedBytes;
	/*u8		EfuseMap[2][HWSET_MAX_SIZE_JAGUAR];*/
	EFUSE_HAL	EfuseHal;

	/*---------------------------------------------------------------------------------*/
	/* 2.4G TX power info for target TX power*/
	u8	Index24G_CCK_Base[MAX_RF_PATH][CENTER_CH_2G_NUM];
	u8	Index24G_BW40_Base[MAX_RF_PATH][CENTER_CH_2G_NUM];
	s8	CCK_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	OFDM_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW20_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW40_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];

	/* 5G TX power info for target TX power*/
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	u8	Index5G_BW40_Base[MAX_RF_PATH][CENTER_CH_5G_ALL_NUM];
	u8	Index5G_BW80_Base[MAX_RF_PATH][CENTER_CH_5G_80M_NUM];
	s8	OFDM_5G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW20_5G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW40_5G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW80_5G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
#endif

	u8 txpwr_by_rate_undefined_band_path[TX_PWR_BY_RATE_NUM_BAND]
		[TX_PWR_BY_RATE_NUM_RF];

	s8	TxPwrByRateOffset[TX_PWR_BY_RATE_NUM_BAND]
		[TX_PWR_BY_RATE_NUM_RF]
		[TX_PWR_BY_RATE_NUM_RATE];

	/* Store the original power by rate value of the base rate for each rate section and rf path */
	u8	TxPwrByRateBase2_4G[TX_PWR_BY_RATE_NUM_RF]
		[MAX_BASE_NUM_IN_PHY_REG_PG_2_4G];
	u8	TxPwrByRateBase5G[TX_PWR_BY_RATE_NUM_RF]
		[MAX_BASE_NUM_IN_PHY_REG_PG_5G];

#if defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8822C)
	u32 txagc_set_buf;
#endif

	u8	txpwr_by_rate_loaded:1;
	u8	txpwr_by_rate_from_file:1;
	u8	txpwr_limit_loaded:1;
	u8	txpwr_limit_from_file:1;
	u8	rf_power_tracking_type;

	/* Read/write are allow for following hardware information variables	 */
	u8	crystal_cap;

	u8	PAType_2G;
	u8	PAType_5G;
	u8	LNAType_2G;
	u8	LNAType_5G;
	u8	ExternalPA_2G;
	u8	ExternalLNA_2G;
	u8	external_pa_5g;
	u8	external_lna_5g;
	u16	TypeGLNA;
	u16	TypeGPA;
	u16	TypeALNA;
	u16	TypeAPA;
	u16	rfe_type;

	u8	bLedOpenDrain; /* Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16. */
	u32	ac_param_be; /* Original parameter for BE, use for EDCA turbo.	*/
	u8	is_turbo_edca;
	u8	prv_traffic_idx;
	BB_REGISTER_DEFINITION_T	PHYRegDef[MAX_RF_PATH];	/* Radio A/B/C/D */

	u32	RfRegChnlVal[MAX_RF_PATH];

	/* RDG enable */
	BOOLEAN	 bRDGEnable;

	#if defined (CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
	u32 RegRRSR;
	#endif

	/****** antenna diversity ******/
	u8	AntDivCfg;
	u8	with_extenal_ant_switch;
	u8	b_fix_tx_ant;
	u8	AntDetection;
	u8	TRxAntDivType;
	u8	ant_path; /* for 8723B s0/s1 selection	 */
	u32	antenna_tx_path;					/* Antenna path Tx */
	u32	AntennaRxPath;					/* Antenna path Rx */
	u8 sw_antdiv_bl_state;

	/******** PHY DM & DM Section **********/
	_lock		IQKSpinLock;
	u8			INIDATA_RATE[MACID_NUM_SW_LIMIT];

	struct dm_struct	 odmpriv;
	u64			bk_rf_ability;
	u8			bIQKInitialized;
	u8			bNeedIQK;
	u8			neediqk_24g;
	u8			IQK_MP_Switch;
	u8			bScanInProcess;
	/******** PHY DM & DM Section **********/



	/* 2010/08/09 MH Add CU power down mode. */
	BOOLEAN		pwrdown;

	/* Add for dual MAC  0--Mac0 1--Mac1 */
	u32	interfaceIndex;

#ifdef CONFIG_P2P
#ifdef CONFIG_P2P_PS_NOA_USE_MACID_SLEEP
	u16 p2p_ps_offload;
#else
	u8	p2p_ps_offload;
#endif
#endif
	/* Auto FSM to Turn On, include clock, isolation, power control for MAC only */
	u8	bMacPwrCtrlOn;
	u8 hci_sus_state;

	u8	RegIQKFWOffload;
	struct submit_ctx	iqk_sctx;
	u8 ch_switch_offload;
	struct submit_ctx chsw_sctx;

	RT_AMPDU_BRUST		AMPDUBurstMode; /* 92C maybe not use, but for compile successfully */

	u8	OutEpQueueSel;
	u8	OutEpNumber;

#ifdef RTW_RX_AGGREGATION
	RX_AGG_MODE rxagg_mode;

	/* For RX Aggregation DMA Mode */
	u8 rxagg_dma_size;
	u8 rxagg_dma_timeout;
#endif /* RTW_RX_AGGREGATION */

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	/*  */
	/* For SDIO Interface HAL related */
	/*  */

	/*  */
	/* SDIO ISR Related */
	/*
	*	u32			IntrMask[1];
	*	u32			IntrMaskToSet[1];
	*	LOG_INTERRUPT		InterruptLog; */
	u32			sdio_himr;
	u32			sdio_hisr;
#ifndef RTW_HALMAC
	/*  */
	/* SDIO Tx FIFO related. */
	/*  */
	/* HIQ, MID, LOW, PUB free pages; padapter->xmitpriv.free_txpg */
#ifdef CONFIG_RTL8192F
	u16			SdioTxFIFOFreePage[SDIO_TX_FREE_PG_QUEUE];
#else
	u8			SdioTxFIFOFreePage[SDIO_TX_FREE_PG_QUEUE];
#endif/*CONFIG_RTL8192F*/
	_lock		SdioTxFIFOFreePageLock;
	u8			SdioTxOQTMaxFreeSpace;
	u8			SdioTxOQTFreeSpace;
#else /* RTW_HALMAC */
	u16			SdioTxOQTFreeSpace;
#endif /* RTW_HALMAC */

	/*  */
	/* SDIO Rx FIFO related. */
	/*  */
	u8			SdioRxFIFOCnt;
#ifdef CONFIG_RTL8822C
	u32			SdioRxFIFOSize;
#else
	u16			SdioRxFIFOSize;
#endif

#ifndef RTW_HALMAC
	u32			sdio_tx_max_len[SDIO_MAX_TX_QUEUE];/* H, N, L, used for sdio tx aggregation max length per queue */
#else
#ifdef CONFIG_RTL8821C
	u16			tx_high_page;
	u16			tx_low_page;
	u16			tx_normal_page;
	u16			tx_extra_page;
	u16			tx_pub_page;
	u8			max_oqt_size;
	#ifdef XMIT_BUF_SIZE
	u32			max_xmit_size_vovi;
	u32			max_xmit_size_bebk;
	#endif /*XMIT_BUF_SIZE*/
	u16			max_xmit_page;
	u16			max_xmit_page_vo;
	u16			max_xmit_page_vi;
	u16			max_xmit_page_be;
	u16			max_xmit_page_bk;

#endif /*#ifdef CONFIG_RTL8821C*/
#endif /* !RTW_HALMAC */
#endif /* CONFIG_SDIO_HCI */

#ifdef CONFIG_USB_HCI

	/* 2010/12/10 MH Add for USB aggreation mode dynamic shceme. */
	BOOLEAN		UsbRxHighSpeedMode;
	BOOLEAN		UsbTxVeryHighSpeedMode;
	u32			UsbBulkOutSize;
	BOOLEAN		bSupportUSB3;
	u8			usb_intf_start;

	/* Interrupt relatd register information. */
	u32			IntArray[3];/* HISR0,HISR1,HSISR */
	u32			IntrMask[3];
#ifdef CONFIG_USB_TX_AGGREGATION
	u8			UsbTxAggMode;
	u8			UsbTxAggDescNum;
#endif /* CONFIG_USB_TX_AGGREGATION */

#ifdef CONFIG_USB_RX_AGGREGATION
	u16			HwRxPageSize;				/* Hardware setting */

	/* For RX Aggregation USB Mode */
	u8			rxagg_usb_size;
	u8			rxagg_usb_timeout;
#endif/* CONFIG_USB_RX_AGGREGATION */
#endif /* CONFIG_USB_HCI */


#ifdef CONFIG_PCI_HCI
	/*  */
	/* EEPROM setting. */
	/*  */
	u32			TransmitConfig;
	u32			IntrMaskToSet[2];
	u32			IntArray[4];
	u32			IntrMask[4];
	u32			SysIntArray[1];
	u32			SysIntrMask[1];
	u32			IntrMaskReg[2];
	u32			IntrMaskDefault[4];

	BOOLEAN		bL1OffSupport;
	BOOLEAN	bSupportBackDoor;
	u32			pci_backdoor_ctrl;

	u8			bDefaultAntenna;

	u8			bInterruptMigration;
	u8			bDisableTxInt;

	u16			RxTag;
#ifdef CONFIG_PCI_DYNAMIC_ASPM
	BOOLEAN		bAspmL1LastIdle;
#endif
#endif /* CONFIG_PCI_HCI */


#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv srestpriv;
#endif /* #ifdef DBG_CONFIG_ERROR_DETECT */

#ifdef CONFIG_BT_COEXIST
	/* For bluetooth co-existance */
	BT_COEXIST		bt_coexist;
#endif /* CONFIG_BT_COEXIST */

#if defined(CONFIG_RTL8723B) || defined(CONFIG_RTL8703B) \
	|| defined(CONFIG_RTL8188F) || defined(CONFIG_RTL8188GTV) || defined(CONFIG_RTL8723D)|| defined(CONFIG_RTL8192F)
#ifndef CONFIG_PCI_HCI	/* mutual exclusive with PCI -- so they're SDIO and GSPI */
	/* Interrupt relatd register information. */
	u32			SysIntrStatus;
	u32			SysIntrMask;
#endif
#endif /*endif CONFIG_RTL8723B	*/

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	char	para_file_buf[MAX_PARA_FILE_BUF_LEN];
	char *mac_reg;
	u32	mac_reg_len;
	char *bb_phy_reg;
	u32	bb_phy_reg_len;
	char *bb_agc_tab;
	u32	bb_agc_tab_len;
	char *bb_phy_reg_pg;
	u32	bb_phy_reg_pg_len;
	char *bb_phy_reg_mp;
	u32	bb_phy_reg_mp_len;
	char *rf_radio_a;
	u32	rf_radio_a_len;
	char *rf_radio_b;
	u32	rf_radio_b_len;
	char *rf_tx_pwr_track;
	u32	rf_tx_pwr_track_len;
	char *rf_tx_pwr_lmt;
	u32	rf_tx_pwr_lmt_len;
#endif

#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
	struct noise_monitor nm;
#endif

	struct hal_spec_t hal_spec;
#ifdef CONFIG_PHY_CAPABILITY_QUERY
	struct phy_spec_t phy_spec;
#endif
	u8	RfKFreeEnable;
	u8	RfKFree_ch_group;
	BOOLEAN				bCCKinCH14;
	BB_INIT_REGISTER	RegForRecover[5];

#if defined(CONFIG_PCI_HCI) && defined(RTL8814AE_SW_BCN)
	BOOLEAN bCorrectBCN;
#endif
	u32 RxGainOffset[4]; /*{2G, 5G_Low, 5G_Middle, G_High}*/
	u8 BackUp_IG_REG_4_Chnl_Section[4]; /*{A,B,C,D}*/

	struct hal_iqk_reg_backup iqk_reg_backup[MAX_IQK_INFO_BACKUP_CHNL_NUM];

#ifdef RTW_HALMAC
	u16 drv_rsvd_page_number;
#endif

#ifdef CONFIG_BEAMFORMING
	u8 backup_snd_ptcl_ctrl;
#ifdef RTW_BEAMFORMING_VERSION_2
	struct beamforming_info beamforming_info;
#endif /* RTW_BEAMFORMING_VERSION_2 */
#endif /* CONFIG_BEAMFORMING */

	u8 not_xmitframe_fw_dl; /*not use xmitframe to download fw*/
	u8 phydm_op_mode;

	u8 in_cta_test;

#ifdef CONFIG_RTW_LED
	struct led_priv led;
#endif
	/* for multi channel case (ex: MCC/TDLS) */
	u8 multi_ch_switch_mode;

} HAL_DATA_COMMON, *PHAL_DATA_COMMON;

typedef struct hal_com_data HAL_DATA_TYPE, *PHAL_DATA_TYPE;
#define GET_HAL_DATA(__pAdapter)			((HAL_DATA_TYPE *)(((struct _ADAPTER*)__pAdapter)->HalData))
#define GET_HAL_SPEC(__pAdapter)			(&(GET_HAL_DATA((__pAdapter))->hal_spec))
#define adapter_to_led(adapter) (&(GET_HAL_DATA(adapter)->led))

#define GET_HAL_RFPATH_NUM(__pAdapter)		(((HAL_DATA_TYPE *)((__pAdapter)->HalData))->NumTotalRFPath)
#define RT_GetInterfaceSelection(_Adapter)		(GET_HAL_DATA(_Adapter)->InterfaceSel)
#define GET_RF_TYPE(__pAdapter)				(GET_HAL_DATA(__pAdapter)->rf_type)
#define GET_KFREE_DATA(_adapter) (&(GET_HAL_DATA((_adapter))->kfree_data))

#define	SUPPORT_HW_RADIO_DETECT(Adapter)	(RT_GetInterfaceSelection(Adapter) == INTF_SEL2_MINICARD || \
		RT_GetInterfaceSelection(Adapter) == INTF_SEL3_USB_Solo || \
		RT_GetInterfaceSelection(Adapter) == INTF_SEL4_USB_Combo)

#define get_hal_mac_addr(adapter)				(GET_HAL_DATA(adapter)->EEPROMMACAddr)
#define is_boot_from_eeprom(adapter)			(GET_HAL_DATA(adapter)->EepromOrEfuse)
#define rtw_get_hw_init_completed(adapter)		(GET_HAL_DATA(adapter)->hw_init_completed)
#define rtw_set_hw_init_completed(adapter, cmp)	(GET_HAL_DATA(adapter)->hw_init_completed = cmp)
#define rtw_is_hw_init_completed(adapter)		(GET_HAL_DATA(adapter)->hw_init_completed == _TRUE)
#endif

#ifdef RTW_HALMAC
int rtw_halmac_deinit_adapter(struct dvobj_priv *);
#endif /* RTW_HALMAC */

#endif /* __HAL_DATA_H__ */
