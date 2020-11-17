/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
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

#ifndef __HALRF_POWER_TRACKING_H__
#define __HALRF_POWER_TRACKING_H__

boolean
odm_check_power_status(void *dm_void);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
void halrf_update_pwr_track(void *dm_void, u8 rate);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void halrf_update_init_rate_work_item_callback(
	void *context);
#endif

void halrf_set_pwr_track(void *dm_void, u8 enable);

#endif /*#ifndef __HALRF_POWERTRACKING_H__*/
