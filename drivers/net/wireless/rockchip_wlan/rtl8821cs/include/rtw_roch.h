/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2020 Realtek Corporation.
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
#ifndef __RTW_ROCH_H__
#define __RTW_ROCH_H__

#include <drv_types.h>

struct rtw_roch_parm;

#if (defined(CONFIG_P2P) && defined(CONFIG_CONCURRENT_MODE)) || defined(CONFIG_IOCTL_CFG80211)
struct roch_info {
#ifdef CONFIG_CONCURRENT_MODE
	_timer	ap_roch_ch_switch_timer;	/* Used to switch the channel between legacy AP and listen state. */
#ifdef CONFIG_IOCTL_CFG80211
	u32	min_home_dur;		/* min duration for traffic, home_time */
	u32	max_away_dur;		/* max acceptable away duration, home_away_time */
#endif
#endif

#ifdef CONFIG_IOCTL_CFG80211
	_timer remain_on_ch_timer;
	u8 restore_channel;
	struct ieee80211_channel remain_on_ch_channel;
	enum nl80211_channel_type remain_on_ch_type;
	ATOMIC_T ro_ch_cookie_gen;
	u64 remain_on_ch_cookie;
	bool is_ro_ch;
	struct wireless_dev *ro_ch_wdev;
	systime last_ro_ch_time;		/* this will be updated at the beginning and end of ro_ch */
#endif
};
#endif

#ifdef CONFIG_IOCTL_CFG80211
u8 rtw_roch_stay_in_cur_chan(_adapter *padapter);
#endif

#if (defined(CONFIG_P2P) && defined(CONFIG_CONCURRENT_MODE)) || defined(CONFIG_IOCTL_CFG80211)
s32 rtw_roch_wk_hdl(_adapter *padapter, int intCmdType, u8 *buf);
u8 rtw_roch_wk_cmd(_adapter *padapter, int intCmdType, struct rtw_roch_parm *roch_parm, u8 flags);

#ifdef CONFIG_CONCURRENT_MODE
void rtw_concurrent_handler(_adapter *padapter);
#endif

void rtw_init_roch_info(_adapter *padapter);
#endif /* (defined(CONFIG_P2P) && defined(CONFIG_CONCURRENT_MODE)) || defined(CONFIG_IOCTL_CFG80211) */

#endif
