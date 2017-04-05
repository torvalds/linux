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
#ifndef __HAL_COMMON_H__
#define __HAL_COMMON_H__

#include "HalVerDef.h"
#include "hal_pg.h"
#include "hal_phy.h"
#include "hal_phy_reg.h"
#include "hal_com_reg.h"
#include "hal_com_phycfg.h"
#include "../hal/hal_com_c2h.h"

/*------------------------------ Tx Desc definition Macro ------------------------*/
/* #pragma mark -- Tx Desc related definition. -- */
/* ----------------------------------------------------------------------------
 * -----------------------------------------------------------
 *	Rate
 * -----------------------------------------------------------
 * CCK Rates, TxHT = 0 */
#define DESC_RATE1M					0x00
#define DESC_RATE2M					0x01
#define DESC_RATE5_5M				0x02
#define DESC_RATE11M				0x03

/* OFDM Rates, TxHT = 0 */
#define DESC_RATE6M					0x04
#define DESC_RATE9M					0x05
#define DESC_RATE12M				0x06
#define DESC_RATE18M				0x07
#define DESC_RATE24M				0x08
#define DESC_RATE36M				0x09
#define DESC_RATE48M				0x0a
#define DESC_RATE54M				0x0b

/* MCS Rates, TxHT = 1 */
#define DESC_RATEMCS0				0x0c
#define DESC_RATEMCS1				0x0d
#define DESC_RATEMCS2				0x0e
#define DESC_RATEMCS3				0x0f
#define DESC_RATEMCS4				0x10
#define DESC_RATEMCS5				0x11
#define DESC_RATEMCS6				0x12
#define DESC_RATEMCS7				0x13
#define DESC_RATEMCS8				0x14
#define DESC_RATEMCS9				0x15
#define DESC_RATEMCS10				0x16
#define DESC_RATEMCS11				0x17
#define DESC_RATEMCS12				0x18
#define DESC_RATEMCS13				0x19
#define DESC_RATEMCS14				0x1a
#define DESC_RATEMCS15				0x1b
#define DESC_RATEMCS16				0x1C
#define DESC_RATEMCS17				0x1D
#define DESC_RATEMCS18				0x1E
#define DESC_RATEMCS19				0x1F
#define DESC_RATEMCS20				0x20
#define DESC_RATEMCS21				0x21
#define DESC_RATEMCS22				0x22
#define DESC_RATEMCS23				0x23
#define DESC_RATEMCS24				0x24
#define DESC_RATEMCS25				0x25
#define DESC_RATEMCS26				0x26
#define DESC_RATEMCS27				0x27
#define DESC_RATEMCS28				0x28
#define DESC_RATEMCS29				0x29
#define DESC_RATEMCS30				0x2A
#define DESC_RATEMCS31				0x2B
#define DESC_RATEVHTSS1MCS0		0x2C
#define DESC_RATEVHTSS1MCS1		0x2D
#define DESC_RATEVHTSS1MCS2		0x2E
#define DESC_RATEVHTSS1MCS3		0x2F
#define DESC_RATEVHTSS1MCS4		0x30
#define DESC_RATEVHTSS1MCS5		0x31
#define DESC_RATEVHTSS1MCS6		0x32
#define DESC_RATEVHTSS1MCS7		0x33
#define DESC_RATEVHTSS1MCS8		0x34
#define DESC_RATEVHTSS1MCS9		0x35
#define DESC_RATEVHTSS2MCS0		0x36
#define DESC_RATEVHTSS2MCS1		0x37
#define DESC_RATEVHTSS2MCS2		0x38
#define DESC_RATEVHTSS2MCS3		0x39
#define DESC_RATEVHTSS2MCS4		0x3A
#define DESC_RATEVHTSS2MCS5		0x3B
#define DESC_RATEVHTSS2MCS6		0x3C
#define DESC_RATEVHTSS2MCS7		0x3D
#define DESC_RATEVHTSS2MCS8		0x3E
#define DESC_RATEVHTSS2MCS9		0x3F
#define DESC_RATEVHTSS3MCS0		0x40
#define DESC_RATEVHTSS3MCS1		0x41
#define DESC_RATEVHTSS3MCS2		0x42
#define DESC_RATEVHTSS3MCS3		0x43
#define DESC_RATEVHTSS3MCS4		0x44
#define DESC_RATEVHTSS3MCS5		0x45
#define DESC_RATEVHTSS3MCS6		0x46
#define DESC_RATEVHTSS3MCS7		0x47
#define DESC_RATEVHTSS3MCS8		0x48
#define DESC_RATEVHTSS3MCS9		0x49
#define DESC_RATEVHTSS4MCS0		0x4A
#define DESC_RATEVHTSS4MCS1		0x4B
#define DESC_RATEVHTSS4MCS2		0x4C
#define DESC_RATEVHTSS4MCS3		0x4D
#define DESC_RATEVHTSS4MCS4		0x4E
#define DESC_RATEVHTSS4MCS5		0x4F
#define DESC_RATEVHTSS4MCS6		0x50
#define DESC_RATEVHTSS4MCS7		0x51
#define DESC_RATEVHTSS4MCS8		0x52
#define DESC_RATEVHTSS4MCS9		0x53

#define HDATA_RATE(rate)\
	(rate == DESC_RATE1M) ? "CCK_1M" :\
	(rate == DESC_RATE2M) ? "CCK_2M" :\
	(rate == DESC_RATE5_5M) ? "CCK5_5M" :\
	(rate == DESC_RATE11M) ? "CCK_11M" :\
	(rate == DESC_RATE6M) ? "OFDM_6M" :\
	(rate == DESC_RATE9M) ? "OFDM_9M" :\
	(rate == DESC_RATE12M) ? "OFDM_12M" :\
	(rate == DESC_RATE18M) ? "OFDM_18M" :\
	(rate == DESC_RATE24M) ? "OFDM_24M" :\
	(rate == DESC_RATE36M) ? "OFDM_36M" :\
	(rate == DESC_RATE48M) ? "OFDM_48M" :\
	(rate == DESC_RATE54M) ? "OFDM_54M" :\
	(rate == DESC_RATEMCS0) ? "MCS0" :\
	(rate == DESC_RATEMCS1) ? "MCS1" :\
	(rate == DESC_RATEMCS2) ? "MCS2" :\
	(rate == DESC_RATEMCS3) ? "MCS3" :\
	(rate == DESC_RATEMCS4) ? "MCS4" :\
	(rate == DESC_RATEMCS5) ? "MCS5" :\
	(rate == DESC_RATEMCS6) ? "MCS6" :\
	(rate == DESC_RATEMCS7) ? "MCS7" :\
	(rate == DESC_RATEMCS8) ? "MCS8" :\
	(rate == DESC_RATEMCS9) ? "MCS9" :\
	(rate == DESC_RATEMCS10) ? "MCS10" :\
	(rate == DESC_RATEMCS11) ? "MCS11" :\
	(rate == DESC_RATEMCS12) ? "MCS12" :\
	(rate == DESC_RATEMCS13) ? "MCS13" :\
	(rate == DESC_RATEMCS14) ? "MCS14" :\
	(rate == DESC_RATEMCS15) ? "MCS15" :\
	(rate == DESC_RATEMCS16) ? "MCS16" :\
	(rate == DESC_RATEMCS17) ? "MCS17" :\
	(rate == DESC_RATEMCS18) ? "MCS18" :\
	(rate == DESC_RATEMCS19) ? "MCS19" :\
	(rate == DESC_RATEMCS20) ? "MCS20" :\
	(rate == DESC_RATEMCS21) ? "MCS21" :\
	(rate == DESC_RATEMCS22) ? "MCS22" :\
	(rate == DESC_RATEMCS23) ? "MCS23" :\
	(rate == DESC_RATEVHTSS1MCS0) ? "VHTSS1MCS0" :\
	(rate == DESC_RATEVHTSS1MCS1) ? "VHTSS1MCS1" :\
	(rate == DESC_RATEVHTSS1MCS2) ? "VHTSS1MCS2" :\
	(rate == DESC_RATEVHTSS1MCS3) ? "VHTSS1MCS3" :\
	(rate == DESC_RATEVHTSS1MCS4) ? "VHTSS1MCS4" :\
	(rate == DESC_RATEVHTSS1MCS5) ? "VHTSS1MCS5" :\
	(rate == DESC_RATEVHTSS1MCS6) ? "VHTSS1MCS6" :\
	(rate == DESC_RATEVHTSS1MCS7) ? "VHTSS1MCS7" :\
	(rate == DESC_RATEVHTSS1MCS8) ? "VHTSS1MCS8" :\
	(rate == DESC_RATEVHTSS1MCS9) ? "VHTSS1MCS9" :\
	(rate == DESC_RATEVHTSS2MCS0) ? "VHTSS2MCS0" :\
	(rate == DESC_RATEVHTSS2MCS1) ? "VHTSS2MCS1" :\
	(rate == DESC_RATEVHTSS2MCS2) ? "VHTSS2MCS2" :\
	(rate == DESC_RATEVHTSS2MCS3) ? "VHTSS2MCS3" :\
	(rate == DESC_RATEVHTSS2MCS4) ? "VHTSS2MCS4" :\
	(rate == DESC_RATEVHTSS2MCS5) ? "VHTSS2MCS5" :\
	(rate == DESC_RATEVHTSS2MCS6) ? "VHTSS2MCS6" :\
	(rate == DESC_RATEVHTSS2MCS7) ? "VHTSS2MCS7" :\
	(rate == DESC_RATEVHTSS2MCS8) ? "VHTSS2MCS8" :\
	(rate == DESC_RATEVHTSS2MCS9) ? "VHTSS2MCS9" :\
	(rate == DESC_RATEVHTSS3MCS0) ? "VHTSS3MCS0" :\
	(rate == DESC_RATEVHTSS3MCS1) ? "VHTSS3MCS1" :\
	(rate == DESC_RATEVHTSS3MCS2) ? "VHTSS3MCS2" :\
	(rate == DESC_RATEVHTSS3MCS3) ? "VHTSS3MCS3" :\
	(rate == DESC_RATEVHTSS3MCS4) ? "VHTSS3MCS4" :\
	(rate == DESC_RATEVHTSS3MCS5) ? "VHTSS3MCS5" :\
	(rate == DESC_RATEVHTSS3MCS6) ? "VHTSS3MCS6" :\
	(rate == DESC_RATEVHTSS3MCS7) ? "VHTSS3MCS7" :\
	(rate == DESC_RATEVHTSS3MCS8) ? "VHTSS3MCS8" :\
	(rate == DESC_RATEVHTSS3MCS9) ? "VHTSS3MCS9" : "UNKNOWN"

enum {
	UP_LINK,
	DOWN_LINK,
};
typedef enum _RT_MEDIA_STATUS {
	RT_MEDIA_DISCONNECT = 0,
	RT_MEDIA_CONNECT       = 1
} RT_MEDIA_STATUS;

#define MAX_DLFW_PAGE_SIZE			4096	/* @ page : 4k bytes */
typedef enum _FIRMWARE_SOURCE {
	FW_SOURCE_IMG_FILE = 0,
	FW_SOURCE_HEADER_FILE = 1,		/* from header file */
} FIRMWARE_SOURCE, *PFIRMWARE_SOURCE;

typedef enum _CH_SW_USE_CASE {
	CH_SW_USE_CASE_TDLS		= 0,
	CH_SW_USE_CASE_MCC		= 1
} CH_SW_USE_CASE;

typedef enum _WAKEUP_REASON{
	RX_PAIRWISEKEY					= 0x01,
	RX_GTK							= 0x02,
	RX_FOURWAY_HANDSHAKE			= 0x03,
	RX_DISASSOC						= 0x04,
	RX_DEAUTH						= 0x08,
	RX_ARP_REQUEST					= 0x09,
	FW_DECISION_DISCONNECT			= 0x10,
	RX_MAGIC_PKT					= 0x21,
	RX_UNICAST_PKT					= 0x22,
	RX_PATTERN_PKT					= 0x23,
	RTD3_SSID_MATCH					= 0x24,
	RX_REALWOW_V2_WAKEUP_PKT		= 0x30,
	RX_REALWOW_V2_ACK_LOST			= 0x31,
	ENABLE_FAIL_DMA_IDLE			= 0x40,
	ENABLE_FAIL_DMA_PAUSE			= 0x41,
	RTIME_FAIL_DMA_IDLE				= 0x42,
	RTIME_FAIL_DMA_PAUSE			= 0x43,
	RX_PNO							= 0x55,
	AP_OFFLOAD_WAKEUP				= 0x66,
	CLK_32K_UNLOCK					= 0xFD,
	CLK_32K_LOCK					= 0xFE
}WAKEUP_REASON;

/*
 * Queue Select Value in TxDesc
 *   */
#define QSLT_BK							0x2/* 0x01 */
#define QSLT_BE							0x0
#define QSLT_VI							0x5/* 0x4 */
#define QSLT_VO							0x7/* 0x6 */
#define QSLT_BEACON						0x10
#define QSLT_HIGH						0x11
#define QSLT_MGNT						0x12
#define QSLT_CMD						0x13

/* BK, BE, VI, VO, HCCA, MANAGEMENT, COMMAND, HIGH, BEACON.
 * #define MAX_TX_QUEUE		9 */

#define TX_SELE_HQ			BIT(0)		/* High Queue */
#define TX_SELE_LQ			BIT(1)		/* Low Queue */
#define TX_SELE_NQ			BIT(2)		/* Normal Queue */
#define TX_SELE_EQ			BIT(3)		/* Extern Queue */

#define PageNum_128(_Len)		(u32)(((_Len)>>7) + ((_Len) & 0x7F ? 1 : 0))
#define PageNum_256(_Len)		(u32)(((_Len)>>8) + ((_Len) & 0xFF ? 1 : 0))
#define PageNum_512(_Len)		(u32)(((_Len)>>9) + ((_Len) & 0x1FF ? 1 : 0))
#define PageNum(_Len, _Size)		(u32)(((_Len)/(_Size)) + ((_Len)&((_Size) - 1) ? 1 : 0))

struct dbg_rx_counter {
	u32	rx_pkt_ok;
	u32	rx_pkt_crc_error;
	u32	rx_pkt_drop;
	u32	rx_ofdm_fa;
	u32	rx_cck_fa;
	u32	rx_ht_fa;
};

#ifdef CONFIG_MBSSID_CAM
	#define DBG_MBID_CAM_DUMP

	void rtw_mbid_cam_init(struct dvobj_priv *dvobj);
	void rtw_mbid_cam_deinit(struct dvobj_priv *dvobj);
	void rtw_mbid_cam_reset(_adapter *adapter);
	u8 rtw_get_max_mbid_cam_id(_adapter *adapter);
	u8 rtw_get_mbid_cam_entry_num(_adapter *adapter);
	int rtw_mbid_cam_cache_dump(void *sel, const char *fun_name , _adapter *adapter);
	int rtw_mbid_cam_dump(void *sel, const char *fun_name, _adapter *adapter);
	void rtw_mbid_cam_restore(_adapter *adapter);
#endif

#ifdef CONFIG_MI_WITH_MBSSID_CAM
	void rtw_hal_set_macaddr_mbid(_adapter *adapter, u8 *mac_addr);
	void rtw_hal_change_macaddr_mbid(_adapter *adapter, u8 *mac_addr);
#endif

void rtw_dump_mac_rx_counters(_adapter *padapter, struct dbg_rx_counter *rx_counter);
void rtw_dump_phy_rx_counters(_adapter *padapter, struct dbg_rx_counter *rx_counter);
void rtw_reset_mac_rx_counters(_adapter *padapter);
void rtw_reset_phy_rx_counters(_adapter *padapter);
void rtw_reset_phy_trx_ok_counters(_adapter *padapter);

#ifdef DBG_RX_COUNTER_DUMP
	#define DUMP_DRV_RX_COUNTER	BIT0
	#define DUMP_MAC_RX_COUNTER	BIT1
	#define DUMP_PHY_RX_COUNTER	BIT2
	#define DUMP_DRV_TRX_COUNTER_DATA	BIT3

	void rtw_dump_phy_rxcnts_preprocess(_adapter *padapter, u8 rx_cnt_mode);
	void rtw_dump_rx_counters(_adapter *padapter);
#endif

void dump_chip_info(HAL_VERSION	ChipVersion);
void rtw_hal_config_rftype(PADAPTER  padapter);

#define BAND_CAP_2G			BIT0
#define BAND_CAP_5G			BIT1
#define BAND_CAP_BIT_NUM	2

#define BW_CAP_5M		BIT0
#define BW_CAP_10M		BIT1
#define BW_CAP_20M		BIT2
#define BW_CAP_40M		BIT3
#define BW_CAP_80M		BIT4
#define BW_CAP_160M		BIT5
#define BW_CAP_80_80M	BIT6
#define BW_CAP_BIT_NUM	7

#define PROTO_CAP_11B		BIT0
#define PROTO_CAP_11G		BIT1
#define PROTO_CAP_11N		BIT2
#define PROTO_CAP_11AC		BIT3
#define PROTO_CAP_BIT_NUM	4

#define WL_FUNC_P2P			BIT0
#define WL_FUNC_MIRACAST	BIT1
#define WL_FUNC_TDLS		BIT2
#define WL_FUNC_FTM			BIT3
#define WL_FUNC_BIT_NUM		4

int hal_spec_init(_adapter *adapter);
void dump_hal_spec(void *sel, _adapter *adapter);

bool hal_chk_band_cap(_adapter *adapter, u8 cap);
bool hal_chk_bw_cap(_adapter *adapter, u8 cap);
bool hal_chk_proto_cap(_adapter *adapter, u8 cap);
bool hal_is_band_support(_adapter *adapter, u8 band);
bool hal_is_bw_support(_adapter *adapter, u8 bw);
bool hal_is_wireless_mode_support(_adapter *adapter, u8 mode);
u8 hal_largest_bw(_adapter *adapter, u8 in_bw);

bool hal_chk_wl_func(_adapter *adapter, u8 func);

u8 hal_com_config_channel_plan(
	IN	PADAPTER padapter,
	IN	char *hw_alpha2,
	IN	u8 hw_chplan,
	IN	char *sw_alpha2,
	IN	u8 sw_chplan,
	IN	u8 def_chplan,
	IN	BOOLEAN AutoLoadFail
);

int hal_config_macaddr(_adapter *adapter, bool autoload_fail);

BOOLEAN
HAL_IsLegalChannel(
	IN	PADAPTER	Adapter,
	IN	u32			Channel
);

u8	MRateToHwRate(u8 rate);

u8	hw_rate_to_m_rate(u8 rate);

void	HalSetBrateCfg(
	IN PADAPTER		Adapter,
	IN u8			*mBratesOS,
	OUT u16			*pBrateCfg);

BOOLEAN
Hal_MappingOutPipe(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
);

void rtw_dump_fw_info(void *sel, _adapter *adapter);
void rtw_restore_mac_addr(_adapter *adapter);/*set mac addr when hal_init for all iface*/
void rtw_hal_dump_macaddr(void *sel, _adapter *adapter);

void rtw_init_hal_com_default_value(PADAPTER Adapter);

#ifdef CONFIG_FW_C2H_REG
void c2h_evt_clear(_adapter *adapter);
s32 c2h_evt_read_88xx(_adapter *adapter, u8 *buf);
#endif

#ifdef CONFIG_FW_C2H_PKT
void rtw_hal_c2h_pkt_pre_hdl(_adapter *adapter, u8 *buf, u16 len);
void rtw_hal_c2h_pkt_hdl(_adapter *adapter, u8 *buf, u16 len);
#endif

u8  rtw_hal_networktype_to_raid(_adapter *adapter, struct sta_info *psta);
u8 rtw_get_mgntframe_raid(_adapter *adapter, unsigned char network_type);
void rtw_hal_update_sta_rate_mask(PADAPTER padapter, struct sta_info *psta);

/* access HW only */
u32 rtw_sec_read_cam(_adapter *adapter, u8 addr);
void rtw_sec_write_cam(_adapter *adapter, u8 addr, u32 wdata);
void rtw_sec_read_cam_ent(_adapter *adapter, u8 id, u8 *ctrl, u8 *mac, u8 *key);
void rtw_sec_write_cam_ent(_adapter *adapter, u8 id, u16 ctrl, u8 *mac, u8 *key);
void rtw_sec_clr_cam_ent(_adapter *adapter, u8 id);
bool rtw_sec_read_cam_is_gk(_adapter *adapter, u8 id);

void rtw_hal_set_msr(_adapter *adapter, u8 net_type);
void rtw_hal_set_macaddr_port(_adapter *adapter, u8 *val);
void rtw_hal_get_macaddr_port(_adapter *adapter, u8 *mac_addr);

void rtw_hal_set_bssid(_adapter *adapter, u8 *val);

void hw_var_port_switch(_adapter *adapter);

void SetHwReg(PADAPTER padapter, u8 variable, u8 *val);
void GetHwReg(PADAPTER padapter, u8 variable, u8 *val);
void rtw_hal_check_rxfifo_full(_adapter *adapter);
void rtw_hal_reqtxrpt(_adapter *padapter, u8 macid);

u8 SetHalDefVar(_adapter *adapter, HAL_DEF_VARIABLE variable, void *value);
u8 GetHalDefVar(_adapter *adapter, HAL_DEF_VARIABLE variable, void *value);

BOOLEAN
eqNByte(
	u8	*str1,
	u8	*str2,
	u32	num
);

u32
MapCharToHexDigit(
	IN	char	chTmp
);

BOOLEAN
GetHexValueFromString(
	IN		char			*szStr,
	IN OUT	u32			*pu4bVal,
	IN OUT	u32			*pu4bMove
);

BOOLEAN
GetFractionValueFromString(
	IN		char		*szStr,
	IN OUT	u8			*pInteger,
	IN OUT	u8			*pFraction,
	IN OUT	u32		*pu4bMove
);

BOOLEAN
IsCommentString(
	IN		char		*szStr
);

BOOLEAN
ParseQualifiedString(
	IN	char *In,
	IN OUT  u32 *Start,
	OUT	char *Out,
	IN	char  LeftQualifier,
	IN	char  RightQualifier
);

BOOLEAN
GetU1ByteIntegerFromStringInDecimal(
	IN		char *Str,
	IN OUT	u8 *pInt
);

BOOLEAN
isAllSpaceOrTab(
	u8	*data,
	u8	size
);

void linked_info_dump(_adapter *padapter, u8 benable);
#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
	void rtw_get_raw_rssi_info(void *sel, _adapter *padapter);
	void rtw_dump_raw_rssi_info(_adapter *padapter, void *sel);
#endif

#ifdef DBG_RX_DFRAME_RAW_DATA
	void rtw_dump_rx_dframe_info(_adapter *padapter, void *sel);
#endif
void rtw_store_phy_info(_adapter *padapter, union recv_frame *prframe);
#define		HWSET_MAX_SIZE			1024
#ifdef CONFIG_EFUSE_CONFIG_FILE
	#define		EFUSE_FILE_COLUMN_NUM		16
	u32 Hal_readPGDataFromConfigFile(PADAPTER padapter);
	u32 Hal_ReadMACAddrFromFile(PADAPTER padapter, u8 *mac_addr);
#endif /* CONFIG_EFUSE_CONFIG_FILE */

int check_phy_efuse_tx_power_info_valid(PADAPTER padapter);
int hal_efuse_macaddr_offset(_adapter *adapter);
int Hal_GetPhyEfuseMACAddr(PADAPTER padapter, u8 *mac_addr);
void rtw_dump_cur_efuse(PADAPTER padapter);

#ifdef CONFIG_RF_POWER_TRIM
	void rtw_bb_rf_gain_offset(_adapter *padapter);
#endif /*CONFIG_RF_POWER_TRIM*/

void dm_DynamicUsbTxAgg(_adapter *padapter, u8 from_timer);
u8 rtw_hal_busagg_qsel_check(_adapter *padapter, u8 pre_qsel, u8 next_qsel);
void GetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	PVOID					pValue2);
void SetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	BOOLEAN					bSet);

#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
struct noise_info {
	u8		bPauseDIG;
	u8		IGIValue;
	u32 	max_time;/* ms	 */
	u8		chan;
};
#endif

void rtw_get_noise(_adapter *padapter);
u8 rtw_get_current_tx_rate(_adapter *padapter, u8 macid);
u8 rtw_get_current_tx_sgi(_adapter *padapter, u8 macid);
void rtw_hal_construct_NullFunctionData(PADAPTER, u8 *pframe, u32 *pLength, u8 *StaAddr, u8 bQoS, u8 AC, u8 bEosp, u8 bForcePowerSave);

void rtw_hal_set_fw_rsvd_page(_adapter *adapter, bool finished);

#ifdef CONFIG_TDLS
	#ifdef CONFIG_TDLS_CH_SW
		s32 rtw_hal_ch_sw_oper_offload(_adapter *padapter, u8 channel, u8 channel_offset, u16 bwmode);
	#endif
#endif
#if defined(CONFIG_BT_COEXIST) && defined(CONFIG_FW_MULTI_PORT_SUPPORT)
s32 rtw_hal_set_wifi_port_id_cmd(_adapter *adapter);
#endif

#ifdef CONFIG_GPIO_API
	u8 rtw_hal_get_gpio(_adapter *adapter, u8 gpio_num);
	int rtw_hal_set_gpio_output_value(_adapter *adapter, u8 gpio_num, bool isHigh);
	int rtw_hal_config_gpio(_adapter *adapter, u8 gpio_num, bool isOutput);
	int rtw_hal_register_gpio_interrupt(_adapter *adapter, int gpio_num, void(*callback)(u8 level));
	int rtw_hal_disable_gpio_interrupt(_adapter *adapter, int gpio_num);
#endif

s8 rtw_hal_ch_sw_iqk_info_search(_adapter *padapter, u8 central_chnl, u8 bw_mode);
void rtw_hal_ch_sw_iqk_info_backup(_adapter *adapter);
void rtw_hal_ch_sw_iqk_info_restore(_adapter *padapter, u8 ch_sw_use_case);

#ifdef CONFIG_GPIO_WAKEUP
	void rtw_hal_switch_gpio_wl_ctrl(_adapter *padapter, u8 index, u8 enable);
	void rtw_hal_set_output_gpio(_adapter *padapter, u8 index, u8 outputval);
#endif

typedef enum _HAL_PHYDM_OPS {
	HAL_PHYDM_DIS_ALL_FUNC,
	HAL_PHYDM_FUNC_SET,
	HAL_PHYDM_FUNC_CLR,
	HAL_PHYDM_ABILITY_BK,
	HAL_PHYDM_ABILITY_RESTORE,
	HAL_PHYDM_ABILITY_SET,
	HAL_PHYDM_ABILITY_GET,
} HAL_PHYDM_OPS;


#define DYNAMIC_FUNC_DISABLE		(0x0)
u32 rtw_phydm_ability_ops(_adapter *adapter, HAL_PHYDM_OPS ops, u32 ability);

#define rtw_phydm_func_disable_all(adapter)	\
	rtw_phydm_ability_ops(adapter, HAL_PHYDM_DIS_ALL_FUNC, 0)

#define rtw_phydm_func_for_offchannel(adapter) \
	do { \
		rtw_phydm_ability_ops(adapter, HAL_PHYDM_DIS_ALL_FUNC, 0); \
		if (rtw_odm_adaptivity_needed(adapter)) \
			rtw_phydm_ability_ops(adapter, HAL_PHYDM_FUNC_SET, ODM_BB_ADAPTIVITY); \
	} while (0)

#define rtw_phydm_func_set(adapter, ability)	\
	rtw_phydm_ability_ops(adapter, HAL_PHYDM_FUNC_SET, ability)

#define rtw_phydm_func_clr(adapter, ability)	\
	rtw_phydm_ability_ops(adapter, HAL_PHYDM_FUNC_CLR, ability)

#define rtw_phydm_ability_backup(adapter)	\
	rtw_phydm_ability_ops(adapter, HAL_PHYDM_ABILITY_BK, 0)

#define rtw_phydm_ability_restore(adapter)	\
	rtw_phydm_ability_ops(adapter, HAL_PHYDM_ABILITY_RESTORE, 0)

#define rtw_phydm_ability_set(adapter, ability)	\
	rtw_phydm_ability_ops(adapter, HAL_PHYDM_ABILITY_SET, ability)

static inline u32 rtw_phydm_ability_get(_adapter *adapter)
{
	return rtw_phydm_ability_ops(adapter, HAL_PHYDM_ABILITY_GET, 0);
}

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	extern char *rtw_phy_file_path;
	extern char rtw_phy_para_file_path[PATH_LENGTH_MAX];
	#define GetLineFromBuffer(buffer)   strsep(&buffer, "\r\n")
#endif

void update_IOT_info(_adapter *padapter);

#ifdef CONFIG_AUTO_CHNL_SEL_NHM
	void rtw_acs_start(_adapter *padapter, bool bStart);
#endif

void hal_set_crystal_cap(_adapter *adapter, u8 crystal_cap);
void rtw_hal_correct_tsf(_adapter *padapter, u8 hw_port, u64 tsf);

void ResumeTxBeacon(_adapter *padapter);
void StopTxBeacon(_adapter *padapter);
#ifdef CONFIG_MI_WITH_MBSSID_CAM /*HW port0 - MBSS*/
	void hw_var_set_opmode_mbid(_adapter *Adapter, u8 mode);
	u8 rtw_mbid_camid_alloc(_adapter *adapter, u8 *mac_addr);
#endif

#ifdef CONFIG_ANTENNA_DIVERSITY
	u8	rtw_hal_antdiv_before_linked(_adapter *padapter);
	void	rtw_hal_antdiv_rssi_compared(_adapter *padapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src);
#endif

#ifdef DBG_SEC_CAM_MOVE
	void rtw_hal_move_sta_gk_to_dk(_adapter *adapter);
	void rtw_hal_read_sta_dk_key(_adapter *adapter, u8 key_id);
#endif

#ifdef CONFIG_LPS_PG
#define LPSPG_RSVD_PAGE_SET_MACID(_rsvd_pag, _value)		SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x00, 0, 8, _value)/*used macid*/
#define LPSPG_RSVD_PAGE_SET_MBSSCAMID(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x00, 8, 8, _value)/*used BSSID CAM entry*/
#define LPSPG_RSVD_PAGE_SET_PMC_NUM(_rsvd_pag, _value)		SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x00, 16, 8, _value)/*Max used Pattern Match CAM entry*/
#define LPSPG_RSVD_PAGE_SET_MU_RAID_GID(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x00, 24, 8, _value)/*Max MU rate table Group ID*/
#define LPSPG_RSVD_PAGE_SET_SEC_CAM_NUM(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x04, 0, 8, _value)/*used Security CAM entry number*/
#define LPSPG_RSVD_PAGE_SET_DRV_RSVDPAGE_NUM(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x04, 8, 8, _value)/*Txbuf used page number for fw offload*/
#define LPSPG_RSVD_PAGE_SET_SEC_CAM_ID1(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x08, 0, 8, _value)/*used Security CAM entry -1*/
#define LPSPG_RSVD_PAGE_SET_SEC_CAM_ID2(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x08, 8, 8, _value)/*used Security CAM entry -2*/
#define LPSPG_RSVD_PAGE_SET_SEC_CAM_ID3(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x08, 16, 8, _value)/*used Security CAM entry -3*/
#define LPSPG_RSVD_PAGE_SET_SEC_CAM_ID4(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x08, 24, 8, _value)/*used Security CAM entry -4*/
#define LPSPG_RSVD_PAGE_SET_SEC_CAM_ID5(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x0C, 0, 8, _value)/*used Security CAM entry -5*/
#define LPSPG_RSVD_PAGE_SET_SEC_CAM_ID6(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x0C, 8, 8, _value)/*used Security CAM entry -6*/
#define LPSPG_RSVD_PAGE_SET_SEC_CAM_ID7(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x0C, 16, 8, _value)/*used Security CAM entry -7*/
#define LPSPG_RSVD_PAGE_SET_SEC_CAM_ID8(_rsvd_pag, _value)	SET_BITS_TO_LE_4BYTE(_rsvd_pag+0x0C, 24, 8, _value)/*used Security CAM entry -8*/
enum lps_pg_hdl_id {
	LPS_PG_INFO_CFG = 0,
	LPS_PG_REDLEMEM,
	LPS_PG_RESEND_H2C,
};

	u8 rtw_hal_set_lps_pg_info(_adapter *adapter);
#endif

int rtw_hal_get_rsvd_page(_adapter *adapter, u32 page_offset, u32 page_num, u8 *buffer, u32 buffer_size);

#ifdef CONFIG_WOWLAN
struct rtl_wow_pattern {
	u16	crc;
	u8	type;
	u32	mask[4];
};
void rtw_wow_pattern_cam_dump(_adapter *adapter);

#ifdef CONFIG_WOW_PATTERN_HW_CAM
void rtw_wow_pattern_read_cam_ent(_adapter *adapter, u8 id, struct  rtl_wow_pattern *context);
void rtw_dump_wow_pattern(void *sel, struct rtl_wow_pattern *pwow_pattern, u8 idx);
#endif
#endif
void rtw_dump_phy_cap(void *sel, _adapter *adapter);
void rtw_dump_rsvd_page(void *sel, _adapter *adapter, u8 page_offset, u8 page_num);

#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
s32 rtw_hal_set_default_port_id_cmd(_adapter *adapter, u8 mac_id);
s32 rtw_set_default_port_id(_adapter *adapter);
s32 rtw_set_ps_rsvd_page(_adapter *adapter);
#endif

#endif /* __HAL_COMMON_H__ */
