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

#define	DM_DIG_HIGH_PWR_THRESH_HIGH	75
#define	DM_DIG_HIGH_PWR_THRESH_LOW	70
#define	DM_DIG_MIN_Netcore		0x12

void rtl92s_dm_watchdog(struct ieee80211_hw *hw);
void rtl92s_dm_init(struct ieee80211_hw *hw);
void rtl92s_dm_init_edca_turbo(struct ieee80211_hw *hw);

#endif
