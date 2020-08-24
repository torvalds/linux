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

#if (RTL8814A_SUPPORT || RTL8821C_SUPPORT || RTL8822B_SUPPORT ||\
	RTL8197F_SUPPORT || RTL8192F_SUPPORT || RTL8198F_SUPPORT ||\
	RTL8822C_SUPPORT)
	#define PHYDM_LA_MODE_SUPPORT			1
#else
	#define PHYDM_LA_MODE_SUPPORT			0
#endif

#if (RTL8822B_SUPPORT || RTL8812A_SUPPORT || RTL8197F_SUPPORT ||\
	RTL8192F_SUPPORT)
	#define DYN_ANT_WEIGHTING_SUPPORT
#endif

#if (RTL8822B_SUPPORT || RTL8821C_SUPPORT)
	#define FAHM_SUPPORT
#endif
	#define NHM_SUPPORT
	#define CLM_SUPPORT

#if (RTL8822C_SUPPORT)
	#define NHM_DYM_PW_TH_SUPPORT
#endif

#if (RTL8822B_SUPPORT)
	/*@#define PHYDM_PHYSTAUS_SMP_MODE*/
#endif

/*@#define PHYDM_TDMA_DIG_SUPPORT*/

#if (RTL8822B_SUPPORT || RTL8192F_SUPPORT || RTL8821C_SUPPORT ||\
	RTL8822C_SUPPORT || RTL8723D_SUPPORT)
	#ifdef CONFIG_TDMADIG
	#define PHYDM_TDMA_DIG_SUPPORT
	#ifdef PHYDM_TDMA_DIG_SUPPORT
	#define IS_USE_NEW_TDMA /*new tdma dig test*/
	#endif
	#endif
#endif

#if (RTL8814B_SUPPORT)
	/*@#define PHYDM_TDMA_DIG_SUPPORT*/
	#ifdef PHYDM_TDMA_DIG_SUPPORT
	/*@#define IS_USE_NEW_TDMA*/ /*new tdma dig test*/
	#endif
#endif

#if (RTL8197F_SUPPORT || RTL8822B_SUPPORT || RTL8814B_SUPPORT)
	/*@#define PHYDM_LNA_SAT_CHK_SUPPORT*/
	#ifdef PHYDM_LNA_SAT_CHK_SUPPORT

		#if (RTL8197F_SUPPORT)
		/*@#define PHYDM_LNA_SAT_CHK_SUPPORT_TYPE1*/
		#endif

		#if (RTL8822B_SUPPORT)
		/*@#define PHYDM_LNA_SAT_CHK_TYPE2*/
		#endif

		#if (RTL8814B_SUPPORT)
		/*@#define PHYDM_LNA_SAT_CHK_TYPE1*/
		#endif
	#endif
#endif

#if (RTL8822B_SUPPORT || RTL8192F_SUPPORT)
	#define PHYDM_POWER_TRAINING_SUPPORT
#endif

#if (RTL8822C_SUPPORT)
	#define PHYDM_PMAC_TX_SETTING_SUPPORT
#endif

#if (RTL8822C_SUPPORT)
	#define PHYDM_MP_SUPPORT
#endif

#if (RTL8822B_SUPPORT)
	#define PHYDM_TXA_CALIBRATION
#endif

#if (RTL8188E_SUPPORT)
	#define	PHYDM_PRIMARY_CCA
#endif

#if (RTL8188F_SUPPORT || RTL8710B_SUPPORT || RTL8821C_SUPPORT ||\
	RTL8822B_SUPPORT || RTL8192F_SUPPORT)
	#define	PHYDM_DC_CANCELLATION
#endif

#if (RTL8822B_SUPPORT || RTL8197F_SUPPORT || RTL8192F_SUPPORT)
	#define	CONFIG_ADAPTIVE_SOML
#endif

#if (RTL8188E_SUPPORT || RTL8192E_SUPPORT)
	#define	CONFIG_RECEIVER_BLOCKING
#endif

#if (RTL8192F_SUPPORT == 1)
	/*#define	CONFIG_8912F_SPUR_CALIBRATION*/
#endif

#if (RTL8822B_SUPPORT == 1)
	#define	CONFIG_8822B_SPUR_CALIBRATION
#endif

#ifdef CONFIG_SUPPORT_DYNAMIC_TXPWR
#define CONFIG_DYNAMIC_TX_TWR
#endif
#define PHYDM_SUPPORT_CCKPD
#define PHYDM_SUPPORT_ADAPTIVITY

/*@Antenna Diversity*/
#ifdef CONFIG_ANTENNA_DIVERSITY
	#define CONFIG_PHYDM_ANTENNA_DIVERSITY

	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY

		#if (RTL8723B_SUPPORT || RTL8821A_SUPPORT ||\
		     RTL8188F_SUPPORT || RTL8821C_SUPPORT ||\
		     RTL8723D_SUPPORT)
			#define	CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		#endif

		#if (RTL8821A_SUPPORT)
			/*@#define CONFIG_HL_SMART_ANTENNA_TYPE1*/
		#endif

		#if (RTL8822B_SUPPORT)
			/*@#define CONFIG_HL_SMART_ANTENNA_TYPE2*/
		#endif

	#endif
#endif

#if (RTL8822C_SUPPORT)
	#define CONFIG_PATH_DIVERSITY
#endif

/*@[SmartAntenna]*/
/*@#define	CONFIG_SMART_ANTENNA*/
#ifdef CONFIG_SMART_ANTENNA
	/*@#define	CONFIG_CUMITEK_SMART_ANTENNA*/
#endif
/* @--------------------------------------------------*/

#ifdef CONFIG_DFS_MASTER
	#define CONFIG_PHYDM_DFS_MASTER
#endif

#if (RTL8812A_SUPPORT || RTL8821A_SUPPORT || RTL8881A_SUPPORT ||\
	RTL8192E_SUPPORT || RTL8723B_SUPPORT)
	/*@#define	CONFIG_RA_FW_DBG_CODE*/
#endif

#define	CONFIG_PSD_TOOL
/*@#define	CONFIG_ANT_DETECTION*/
/*@#define	CONFIG_RA_DYNAMIC_RTY_LIMIT*/
#define	CONFIG_BB_TXBF_API
#define	CONFIG_PHYDM_DEBUG_FUNCTION

#ifdef CONFIG_BT_COEXIST
	#define	ODM_CONFIG_BT_COEXIST
#endif
#define	PHYDM_SUPPORT_RSSI_MONITOR
#define	PHYDM_AUTO_DEGBUG
#define CFG_DIG_DAMPING_CHK


#ifdef PHYDM_BEAMFORMING_SUPPORT
	#if (RTL8812A_SUPPORT || RTL8821A_SUPPORT || RTL8192E_SUPPORT ||\
	     RTL8814A_SUPPORT || RTL8881A_SUPPORT)
		#define	PHYDM_BEAMFORMING_VERSION1
	#endif
	#if (RTL8192F_SUPPORT || RTL8195B_SUPPORT || RTL8821C_SUPPORT ||\
	     RTL8822B_SUPPORT || RTL8197F_SUPPORT || RTL8198F_SUPPORT ||\
	     RTL8822C_SUPPORT || RTL8814B_SUPPORT)
		#define	DRIVER_BEAMFORMING_VERSION2
	#endif
#endif

#if (RTL8822B_SUPPORT || RTL8822C_SUPPORT)
	#ifdef CONFIG_MCC_MODE
	#define	CONFIG_MCC_DM
	#endif
#endif

#if (RTL8822B_SUPPORT)
	#ifdef CONFIG_DYNAMIC_BYPASS_MODE
	#define	CONFIG_DYNAMIC_BYPASS
	#endif
#endif

#if (RTL8822B_SUPPORT || RTL8192F_SUPPORT)
	#define CONFIG_DIRECTIONAL_BF
#endif

#endif
