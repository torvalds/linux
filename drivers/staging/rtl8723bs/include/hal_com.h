/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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

/*------------------------------ Tx Desc definition Macro ------------------------*/
/* pragma mark -- Tx Desc related definition. -- */
/*  */
/*  */
/* 	Rate */
/*  */
/*  CCK Rates, TxHT = 0 */
#define DESC_RATE1M					0x00
#define DESC_RATE2M					0x01
#define DESC_RATE5_5M				0x02
#define DESC_RATE11M				0x03

/*  OFDM Rates, TxHT = 0 */
#define DESC_RATE6M					0x04
#define DESC_RATE9M					0x05
#define DESC_RATE12M				0x06
#define DESC_RATE18M				0x07
#define DESC_RATE24M				0x08
#define DESC_RATE36M				0x09
#define DESC_RATE48M				0x0a
#define DESC_RATE54M				0x0b

/*  MCS Rates, TxHT = 1 */
#define DESC_RATEMCS0				0x0c
#define DESC_RATEMCS1				0x0d
#define DESC_RATEMCS2				0x0e
#define DESC_RATEMCS3				0x0f
#define DESC_RATEMCS4				0x10
#define DESC_RATEMCS5				0x11
#define DESC_RATEMCS6				0x12
#define DESC_RATEMCS7				0x13

#define HDATA_RATE(rate)\
(rate == DESC_RATE1M) ? "CCK_1M" : \
(rate == DESC_RATE2M) ? "CCK_2M" : \
(rate == DESC_RATE5_5M) ? "CCK5_5M" : \
(rate == DESC_RATE11M) ? "CCK_11M" : \
(rate == DESC_RATE6M) ? "OFDM_6M" : \
(rate == DESC_RATE9M) ? "OFDM_9M" : \
(rate == DESC_RATE12M) ? "OFDM_12M" : \
(rate == DESC_RATE18M) ? "OFDM_18M" : \
(rate == DESC_RATE24M) ? "OFDM_24M" : \
(rate == DESC_RATE36M) ? "OFDM_36M" : \
(rate == DESC_RATE48M) ? "OFDM_48M" : \
(rate == DESC_RATE54M) ? "OFDM_54M" : \
(rate == DESC_RATEMCS0) ? "MCS0" : \
(rate == DESC_RATEMCS1) ? "MCS1" : \
(rate == DESC_RATEMCS2) ? "MCS2" : \
(rate == DESC_RATEMCS3) ? "MCS3" : \
(rate == DESC_RATEMCS4) ? "MCS4" : \
(rate == DESC_RATEMCS5) ? "MCS5" : \
(rate == DESC_RATEMCS6) ? "MCS6" : \
(rate == DESC_RATEMCS7) ? "MCS7" : "UNKNOWN"

enum{
	UP_LINK,
	DOWN_LINK,
};
enum rt_media_status {
	RT_MEDIA_DISCONNECT = 0,
	RT_MEDIA_CONNECT       = 1
};

#define MAX_DLFW_PAGE_SIZE			4096	/*  @ page : 4k bytes */

/*  BK, BE, VI, VO, HCCA, MANAGEMENT, COMMAND, HIGH, BEACON. */
/* define MAX_TX_QUEUE		9 */

#define TX_SELE_HQ			BIT(0)		/*  High Queue */
#define TX_SELE_LQ			BIT(1)		/*  Low Queue */
#define TX_SELE_NQ			BIT(2)		/*  Normal Queue */
#define TX_SELE_EQ			BIT(3)		/*  Extern Queue */

#define PageNum_128(_Len)		((u32)(((_Len) >> 7) + ((_Len) & 0x7F ? 1 : 0)))

u8 rtw_hal_data_init(struct adapter *padapter);
void rtw_hal_data_deinit(struct adapter *padapter);

void dump_chip_info(struct hal_version	ChipVersion);

u8 /* return the final channel plan decision */
hal_com_config_channel_plan(
struct adapter *padapter,
u8 	hw_channel_plan,	/* channel plan from HW (efuse/eeprom) */
u8 	sw_channel_plan,	/* channel plan from SW (registry/module param) */
u8 	def_channel_plan,	/* channel plan used when the former two is invalid */
bool		AutoLoadFail
	);

bool
HAL_IsLegalChannel(
struct adapter *Adapter,
u32 		Channel
	);

u8 MRateToHwRate(u8 rate);

u8 HwRateToMRate(u8 rate);

void HalSetBrateCfg(
	struct adapter *Adapter,
	u8 *mBratesOS,
	u16	*pBrateCfg);

bool
Hal_MappingOutPipe(
struct adapter *padapter,
u8 NumOutPipe
	);

void hal_init_macaddr(struct adapter *adapter);

void rtw_init_hal_com_default_value(struct adapter *Adapter);

void c2h_evt_clear(struct adapter *adapter);
s32 c2h_evt_read_88xx(struct adapter *adapter, u8 *buf);

u8 rtw_get_mgntframe_raid(struct adapter *adapter, unsigned char network_type);
void rtw_hal_update_sta_rate_mask(struct adapter *padapter, struct sta_info *psta);

void hw_var_port_switch(struct adapter *adapter);

void SetHwReg(struct adapter *padapter, u8 variable, u8 *val);
void GetHwReg(struct adapter *padapter, u8 variable, u8 *val);
void rtw_hal_check_rxfifo_full(struct adapter *adapter);

u8 SetHalDefVar(struct adapter *adapter, enum hal_def_variable variable,
		void *value);
u8 GetHalDefVar(struct adapter *adapter, enum hal_def_variable variable,
		void *value);

bool eqNByte(u8 *str1, u8 *str2, u32 num);

bool IsHexDigit(char chTmp);

u32 MapCharToHexDigit(char chTmp);

bool GetHexValueFromString(char *szStr, u32 *pu4bVal, u32 *pu4bMove);

bool GetFractionValueFromString(char *szStr, u8 *pInteger, u8 *pFraction,
				u32 *pu4bMove);

bool IsCommentString(char *szStr);

bool ParseQualifiedString(char *In, u32 *Start, char *Out, char LeftQualifier,
			  char RightQualifier);

bool GetU1ByteIntegerFromStringInDecimal(char *str, u8 *in);

bool isAllSpaceOrTab(u8 *data, u8 size);

void linked_info_dump(struct adapter *padapter, u8 benable);
#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
void rtw_get_raw_rssi_info(void *sel, struct adapter *padapter);
void rtw_store_phy_info(struct adapter *padapter, union recv_frame *prframe);
void rtw_dump_raw_rssi_info(struct adapter *padapter);
#endif

#define		HWSET_MAX_SIZE			512

void rtw_bb_rf_gain_offset(struct adapter *padapter);

void GetHalODMVar(struct adapter *Adapter,
	enum hal_odm_variable		eVariable,
	void *pValue1,
	void *pValue2);
void SetHalODMVar(
	struct adapter *Adapter,
	enum hal_odm_variable		eVariable,
	void *pValue1,
	bool					bSet);
#endif /* __HAL_COMMON_H__ */
