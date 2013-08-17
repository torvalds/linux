/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef	__RTL_92S_DM_H__
#define __RTL_92S_DM_H__

struct dig_t {
	u8 dig_enable_flag;
	u8 dig_algorithm;
	u8 dig_twoport_algorithm;
	u8 dig_ext_port_stage;
	u8 dig_dbgmode;
	u8 dig_slgorithm_switch;

	long rssi_lowthresh;
	long rssi_highthresh;

	u32 fa_lowthresh;
	u32 fa_highthresh;

	long rssi_highpower_lowthresh;
	long rssi_highpower_highthresh;

	u8 dig_state;
	u8 dig_highpwrstate;
	u8 cur_sta_connectstate;
	u8 pre_sta_connectstate;
	u8 cur_ap_connectstate;
	u8 pre_ap_connectstate;

	u8 cur_pd_thstate;
	u8 pre_pd_thstate;
	u8 cur_cs_ratiostate;
	u8 pre_cs_ratiostate;

	u32 pre_igvalue;
	u32	cur_igvalue;

	u8 backoff_enable_flag;
	char backoff_val;
	char backoffval_range_max;
	char backoffval_range_min;
	u8 rx_gain_range_max;
	u8 rx_gain_range_min;

	long rssi_val;
};

enum dm_dig_alg {
	DIG_ALGO_BY_FALSE_ALARM = 0,
	DIG_ALGO_BY_RSSI	= 1,
	DIG_ALGO_BEFORE_CONNECT_BY_RSSI_AND_ALARM = 2,
	DIG_ALGO_BY_TOW_PORT = 3,
	DIG_ALGO_MAX
};

enum dm_dig_two_port_alg {
	DIG_TWO_PORT_ALGO_RSSI = 0,
	DIG_TWO_PORT_ALGO_FALSE_ALARM = 1,
};

enum dm_dig_dbg {
	DM_DBG_OFF = 0,
	DM_DBG_ON = 1,
	DM_DBG_MAX
};

enum dm_dig_sta {
	DM_STA_DIG_OFF = 0,
	DM_STA_DIG_ON,
	DM_STA_DIG_MAX
};

enum dm_dig_connect {
	DIG_STA_DISCONNECT = 0,
	DIG_STA_CONNECT = 1,
	DIG_STA_BEFORE_CONNECT = 2,
	DIG_AP_DISCONNECT = 3,
	DIG_AP_CONNECT = 4,
	DIG_AP_ADD_STATION = 5,
	DIG_CONNECT_MAX
};

enum dm_dig_ext_port_alg {
	DIG_EXT_PORT_STAGE_0 = 0,
	DIG_EXT_PORT_STAGE_1 = 1,
	DIG_EXT_PORT_STAGE_2 = 2,
	DIG_EXT_PORT_STAGE_3 = 3,
	DIG_EXT_PORT_STAGE_MAX = 4,
};

enum dm_ratr_sta {
	DM_RATR_STA_HIGH = 0,
	DM_RATR_STA_MIDDLEHIGH = 1,
	DM_RATR_STA_MIDDLE = 2,
	DM_RATR_STA_MIDDLELOW = 3,
	DM_RATR_STA_LOW = 4,
	DM_RATR_STA_ULTRALOW = 5,
	DM_RATR_STA_MAX
};

#define DM_TYPE_BYFW			0
#define DM_TYPE_BYDRIVER		1

#define	TX_HIGH_PWR_LEVEL_NORMAL	0
#define	TX_HIGH_PWR_LEVEL_LEVEL1	1
#define	TX_HIGH_PWR_LEVEL_LEVEL2	2

#define	HAL_DM_DIG_DISABLE		BIT(0)	/* Disable Dig */
#define	HAL_DM_HIPWR_DISABLE		BIT(1)	/* Disable High Power */

#define	TX_HIGHPWR_LEVEL_NORMAL		0
#define	TX_HIGHPWR_LEVEL_NORMAL1	1
#define	TX_HIGHPWR_LEVEL_NORMAL2	2

#define	TX_POWER_NEAR_FIELD_THRESH_LVL2	74
#define	TX_POWER_NEAR_FIELD_THRESH_LVL1	67

#define DM_DIG_THRESH_HIGH		40
#define DM_DIG_THRESH_LOW		35
#define	DM_FALSEALARM_THRESH_LOW	40
#define	DM_FALSEALARM_THRESH_HIGH	1000
#define	DM_DIG_HIGH_PWR_THRESH_HIGH	75
#define	DM_DIG_HIGH_PWR_THRESH_LOW	70
#define	DM_DIG_BACKOFF			12
#define	DM_DIG_MAX			0x3e
#define	DM_DIG_MIN			0x1c
#define	DM_DIG_MIN_Netcore		0x12
#define	DM_DIG_BACKOFF_MAX		12
#define	DM_DIG_BACKOFF_MIN		-4

extern struct dig_t digtable;

void rtl92s_dm_watchdog(struct ieee80211_hw *hw);
void rtl92s_dm_init(struct ieee80211_hw *hw);
void rtl92s_dm_init_edca_turbo(struct ieee80211_hw *hw);

#endif

