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

#ifndef __PHYDM_FEATURES_H__
#define __PHYDM_FEATURES_H__

#define CONFIG_RUN_IN_DRV
#define ODM_DC_CANCELLATION_SUPPORT		(ODM_RTL8188F | \
						 ODM_RTL8710B | \
						 ODM_RTL8192F | \
						 ODM_RTL8821C | \
						 ODM_RTL8822B | \
						 ODM_RTL8721D | \
						 ODM_RTL8723D | \
						 ODM_RTL8710C)
#define ODM_RECEIVER_BLOCKING_SUPPORT	(ODM_RTL8188E | ODM_RTL8192E)
#define ODM_DYM_BW_INDICATION_SUPPORT	(ODM_RTL8821C | \
					 ODM_RTL8822B | \
					 ODM_RTL8822C)
/*@20170103 YuChen add for FW API*/
#define PHYDM_FW_API_ENABLE_8822B		1
#define PHYDM_FW_API_FUNC_ENABLE_8822B		1
#define PHYDM_FW_API_ENABLE_8821C		1
#define PHYDM_FW_API_FUNC_ENABLE_8821C		1
#define PHYDM_FW_API_ENABLE_8195B		1
#define PHYDM_FW_API_FUNC_ENABLE_8195B		1
#define PHYDM_FW_API_ENABLE_8198F		1
#define PHYDM_FW_API_FUNC_ENABLE_8198F		1
#define PHYDM_FW_API_ENABLE_8822C 1
#define PHYDM_FW_API_FUNC_ENABLE_8822C 1
#define PHYDM_FW_API_ENABLE_8814B 1
#define PHYDM_FW_API_FUNC_ENABLE_8814B 1
#define PHYDM_FW_API_ENABLE_8812F 1
#define PHYDM_FW_API_FUNC_ENABLE_8812F 1
#define PHYDM_FW_API_ENABLE_8197G 1
#define PHYDM_FW_API_FUNC_ENABLE_8197G 1

#define CONFIG_POWERSAVING 0

#ifdef BEAMFORMING_SUPPORT
#if (BEAMFORMING_SUPPORT)
	#define PHYDM_BEAMFORMING_SUPPORT
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#include	"phydm_features_win.h"
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#include	"phydm_features_ce.h"
	/*@#include	"phydm_features_ce2_kernel.h"*/
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#include	"phydm_features_ap.h"
#elif (DM_ODM_SUPPORT_TYPE == ODM_IOT)
	#include	"phydm_features_iot.h"
#endif

#endif
