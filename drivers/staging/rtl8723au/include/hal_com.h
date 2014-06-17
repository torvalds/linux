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
 ******************************************************************************/
#ifndef __HAL_COMMON_H__
#define __HAL_COMMON_H__

/*  */
/*        Rate Definition */
/*  */
/* CCK */
#define	RATR_1M					0x00000001
#define	RATR_2M					0x00000002
#define	RATR_55M				0x00000004
#define	RATR_11M				0x00000008
/* OFDM */
#define	RATR_6M					0x00000010
#define	RATR_9M					0x00000020
#define	RATR_12M				0x00000040
#define	RATR_18M				0x00000080
#define	RATR_24M				0x00000100
#define	RATR_36M				0x00000200
#define	RATR_48M				0x00000400
#define	RATR_54M				0x00000800
/* MCS 1 Spatial Stream */
#define	RATR_MCS0				0x00001000
#define	RATR_MCS1				0x00002000
#define	RATR_MCS2				0x00004000
#define	RATR_MCS3				0x00008000
#define	RATR_MCS4				0x00010000
#define	RATR_MCS5				0x00020000
#define	RATR_MCS6				0x00040000
#define	RATR_MCS7				0x00080000
/* MCS 2 Spatial Stream */
#define	RATR_MCS8				0x00100000
#define	RATR_MCS9				0x00200000
#define	RATR_MCS10				0x00400000
#define	RATR_MCS11				0x00800000
#define	RATR_MCS12				0x01000000
#define	RATR_MCS13				0x02000000
#define	RATR_MCS14				0x04000000
#define	RATR_MCS15				0x08000000

/* CCK */
#define RATE_1M					BIT(0)
#define RATE_2M					BIT(1)
#define RATE_5_5M				BIT(2)
#define RATE_11M				BIT(3)
/* OFDM */
#define RATE_6M					BIT(4)
#define RATE_9M					BIT(5)
#define RATE_12M				BIT(6)
#define RATE_18M				BIT(7)
#define RATE_24M				BIT(8)
#define RATE_36M				BIT(9)
#define RATE_48M				BIT(10)
#define RATE_54M				BIT(11)
/* MCS 1 Spatial Stream */
#define RATE_MCS0				BIT(12)
#define RATE_MCS1				BIT(13)
#define RATE_MCS2				BIT(14)
#define RATE_MCS3				BIT(15)
#define RATE_MCS4				BIT(16)
#define RATE_MCS5				BIT(17)
#define RATE_MCS6				BIT(18)
#define RATE_MCS7				BIT(19)
/* MCS 2 Spatial Stream */
#define RATE_MCS8				BIT(20)
#define RATE_MCS9				BIT(21)
#define RATE_MCS10				BIT(22)
#define RATE_MCS11				BIT(23)
#define RATE_MCS12				BIT(24)
#define RATE_MCS13				BIT(25)
#define RATE_MCS14				BIT(26)
#define RATE_MCS15				BIT(27)

/*  ALL CCK Rate */
#define	RATE_ALL_CCK	(RATR_1M | RATR_2M | RATR_55M | RATR_11M)
#define	RATE_ALL_OFDM_AG				\
	(RATR_6M | RATR_9M | RATR_12M | RATR_18M | RATR_24M| \
	 RATR_36M|RATR_48M|RATR_54M)
#define	RATE_ALL_OFDM_1SS				\
	(RATR_MCS0 | RATR_MCS1 | RATR_MCS2 | RATR_MCS3 |	\
	 RATR_MCS4 | RATR_MCS5 | RATR_MCS6 | RATR_MCS7)
#define	RATE_ALL_OFDM_2SS				\
	(RATR_MCS8 | RATR_MCS9 | RATR_MCS10 | RATR_MCS11|	\
	 RATR_MCS12 | RATR_MCS13 | RATR_MCS14 | RATR_MCS15)

/*------------------------------ Tx Desc definition Macro ------------------------*/
/* pragma mark -- Tx Desc related definition. -- */
/*  */
/*  */
/*	Rate */
/*  */
/*  CCK Rates, TxHT = 0 */
#define DESC_RATE1M				0x00
#define DESC_RATE2M				0x01
#define DESC_RATE5_5M				0x02
#define DESC_RATE11M				0x03

/*  OFDM Rates, TxHT = 0 */
#define DESC_RATE6M				0x04
#define DESC_RATE9M				0x05
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
#define DESC_RATEMCS8				0x14
#define DESC_RATEMCS9				0x15
#define DESC_RATEMCS10				0x16
#define DESC_RATEMCS11				0x17
#define DESC_RATEMCS12				0x18
#define DESC_RATEMCS13				0x19
#define DESC_RATEMCS14				0x1a
#define DESC_RATEMCS15				0x1b
#define DESC_RATEMCS15_SG			0x1c
#define DESC_RATEMCS32				0x20

#define REG_P2P_CTWIN					0x0572 /*  1 Byte long (in unit of TU) */
#define REG_NOA_DESC_SEL				0x05CF
#define REG_NOA_DESC_DURATION		0x05E0
#define REG_NOA_DESC_INTERVAL			0x05E4
#define REG_NOA_DESC_START			0x05E8
#define REG_NOA_DESC_COUNT			0x05EC

#include "HalVerDef.h"
void dump_chip_info23a(struct hal_version	ChipVersion);


u8	/* return the final channel plan decision */
hal_com_get_channel_plan23a(
	struct rtw_adapter	*padapter,
	u8			hw_channel_plan,	/* channel plan from HW (efuse/eeprom) */
	u8			sw_channel_plan,	/* channel plan from SW (registry/module param) */
	u8			def_channel_plan,	/* channel plan used when the former two is invalid */
	bool		AutoLoadFail
	);

u8	MRateToHwRate23a(u8 rate);

void HalSetBrateCfg23a(struct rtw_adapter *padapter, u8 *mBratesOS);

bool
Hal_MappingOutPipe23a(struct rtw_adapter *pAdapter, u8 NumOutPipe);

void c2h_evt_clear23a(struct rtw_adapter *adapter);
s32 c2h_evt_read23a(struct rtw_adapter *adapter, u8 *buf);

void rtl8723a_set_ampdu_min_space(struct rtw_adapter *padapter, u8 MinSpacingToSet);
void rtl8723a_set_ampdu_factor(struct rtw_adapter *padapter, u8 FactorToSet);
void rtl8723a_set_acm_ctrl(struct rtw_adapter *padapter, u8 ctrl);
void rtl8723a_set_media_status(struct rtw_adapter *padapter, u8 status);
void rtl8723a_set_media_status1(struct rtw_adapter *padapter, u8 status);
void rtl8723a_set_bcn_func(struct rtw_adapter *padapter, u8 val);
void rtl8723a_check_bssid(struct rtw_adapter *padapter, u8 val);
void rtl8723a_mlme_sitesurvey(struct rtw_adapter *padapter, u8 flag);
void rtl8723a_on_rcr_am(struct rtw_adapter *padapter);
void rtl8723a_off_rcr_am(struct rtw_adapter *padapter);
void rtl8723a_set_slot_time(struct rtw_adapter *padapter, u8 slottime);
void rtl8723a_ack_preamble(struct rtw_adapter *padapter, u8 bShortPreamble);
void rtl8723a_set_sec_cfg(struct rtw_adapter *padapter, u8 sec);
void rtl8723a_cam_empty_entry(struct rtw_adapter *padapter, u8 ucIndex);
void rtl8723a_cam_invalid_all(struct rtw_adapter *padapter);
void rtl8723a_cam_write(struct rtw_adapter *padapter,
			u8 entry, u16 ctrl, const u8 *mac, const u8 *key);
void rtl8723a_fifo_cleanup(struct rtw_adapter *padapter);
void rtl8723a_set_apfm_on_mac(struct rtw_adapter *padapter, u8 val);
void rtl8723a_bcn_valid(struct rtw_adapter *padapter);
bool rtl8723a_get_bcn_valid(struct rtw_adapter *padapter);
void rtl8723a_set_beacon_interval(struct rtw_adapter *padapter, u16 interval);
void rtl8723a_set_resp_sifs(struct rtw_adapter *padapter,
			    u8 r2t1, u8 r2t2, u8 t2t1, u8 t2t2);
void rtl8723a_set_ac_param_vo(struct rtw_adapter *padapter, u32 vo);
void rtl8723a_set_ac_param_vi(struct rtw_adapter *padapter, u32 vi);
void rtl8723a_set_ac_param_be(struct rtw_adapter *padapter, u32 be);
void rtl8723a_set_ac_param_bk(struct rtw_adapter *padapter, u32 bk);
void rtl8723a_set_rxdma_agg_pg_th(struct rtw_adapter *padapter, u8 val);
void rtl8723a_set_nav_upper(struct rtw_adapter *padapter, u32 usNavUpper);
void rtl8723a_set_initial_gain(struct rtw_adapter *padapter, u32 rx_gain);

void rtl8723a_odm_support_ability_write(struct rtw_adapter *padapter, u32 val);
void rtl8723a_odm_support_ability_backup(struct rtw_adapter *padapter);
void rtl8723a_odm_support_ability_restore(struct rtw_adapter *padapter);
void rtl8723a_odm_support_ability_set(struct rtw_adapter *padapter, u32 val);
void rtl8723a_odm_support_ability_clr(struct rtw_adapter *padapter, u32 val);

void rtl8723a_set_rpwm(struct rtw_adapter *padapter, u8 val);
u8 rtl8723a_get_rf_type(struct rtw_adapter *padapter);
bool rtl8723a_get_fwlps_rf_on(struct rtw_adapter *padapter);
bool rtl8723a_chk_hi_queue_empty(struct rtw_adapter *padapter);

#endif /* __HAL_COMMON_H__ */
