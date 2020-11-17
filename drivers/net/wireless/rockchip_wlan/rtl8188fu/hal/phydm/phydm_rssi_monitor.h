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

#ifndef __PHYDM_RSSI_MONITOR_H__
#define __PHYDM_RSSI_MONITOR_H__

#define RSSI_MONITOR_VERSION "2.0"

/* @1 ============================================================
 * 1  Definition
 * 1 ============================================================
 */

/* @1 ============================================================
 * 1  structure
 * 1 ============================================================
 */

/* @1 ============================================================
 * 1  enumeration
 * 1 ============================================================
 */

/* @1 ============================================================
 * 1  function prototype
 * 1 ============================================================
 */

void phydm_rssi_monitor_check(void *dm_void);

void phydm_rssi_monitor_init(void *dm_void);

#endif
