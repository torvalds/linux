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

#ifndef __PHYDM_FEATURES_CE_H__
#define __PHYDM_FEATURES_CE_H__

#define PHYDM_LA_MODE_SUPPORT			0

#if (RTL8822B_SUPPORT || RTL8812A_SUPPORT || RTL8197F_SUPPORT ||\
	RTL8192F_SUPPORT)
	#define DYN_ANT_WEIGHTING_SUPPORT
#endif

#if (RTL8822B_SUPPORT || RTL8821C_SUPPORT)
	#define FAHM_SUPPORT
#endif
	#define NHM_SUPPORT
	#define CLM_SUPPORT

#if (RTL8822B_SUPPORT)
	#define PHYDM_TXA_CALIBRATION
#endif

#if (RTL8188F_SUPPORT || RTL8710B_SUPPORT || RTL8821C_SUPPORT ||\
	RTL8822B_SUPPORT || RTL8192F_SUPPORT)
	#define	PHYDM_DC_CANCELLATION
#endif

#if (RTL8192F_SUPPORT == 1)
	/*#define	CONFIG_8912F_SPUR_CALIBRATION*/
#endif

#if (RTL8822B_SUPPORT == 1)
	/* #define	CONFIG_8822B_SPUR_CALIBRATION */
#endif

#define PHYDM_SUPPORT_CCKPD
#define PHYDM_SUPPORT_ADAPTIVITY

#ifdef CONFIG_DFS_MASTER
	#define CONFIG_PHYDM_DFS_MASTER
#endif

#define	CONFIG_BB_TXBF_API
#define	CONFIG_PHYDM_DEBUG_FUNCTION

#ifdef CONFIG_BT_COEXIST
	#define	ODM_CONFIG_BT_COEXIST
#endif
#define	PHYDM_SUPPORT_RSSI_MONITOR
#define CFG_DIG_DAMPING_CHK


#ifdef PHYDM_BEAMFORMING_SUPPORT
	#if (RTL8192F_SUPPORT || RTL8195B_SUPPORT || RTL8821C_SUPPORT ||\
	     RTL8822B_SUPPORT || RTL8197F_SUPPORT || RTL8198F_SUPPORT ||\
	     RTL8822C_SUPPORT || RTL8814B_SUPPORT)
		#define	DRIVER_BEAMFORMING_VERSION2
	#endif
#endif

#endif
