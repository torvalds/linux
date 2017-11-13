/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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

#ifndef	__PHYDM_FEATURES_AP_H__
#define __PHYDM_FEATURES_AP

#if (RTL8822B_SUPPORT == 1)
	/*#define PHYDM_PHYSTAUS_SMP_MODE*/
#endif

#if (RTL8197F_SUPPORT == 1)
	/*#define PHYDM_TDMA_DIG_SUPPORT*/
#endif

#if (RTL8197F_SUPPORT == 1)
	#define PHYDM_LNA_SAT_CHK_SUPPORT
#endif

#if (RTL8822B_SUPPORT == 1)
	/*#define PHYDM_POWER_TRAINING_SUPPORT*/
#endif

#if (RTL8822B_SUPPORT == 1)
	#define PHYDM_TXA_CALIBRATION
#endif

#if (RTL8188E_SUPPORT == 1) || (RTL8197F_SUPPORT == 1)
	#define	PHYDM_PRIMARY_CCA
#endif

#if (RTL8188F_SUPPORT == 1 || RTL8710B_SUPPORT == 1 || RTL8821C_SUPPORT == 1 || RTL8822B_SUPPORT == 1)
	#define	PHYDM_DC_CANCELLATION
#endif

#if (RTL8822B_SUPPORT == 1)
	/*#define	CONFIG_DYNAMIC_RX_PATH*/
#endif

#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1)
	/*#define	CONFIG_ADAPTIVE_SOML*/
#endif

#define PHYDM_DIG_MODE_DECISION_SUPPORT
/*#define	CONFIG_PSD_TOOL*/
#define PHYDM_SUPPORT_CCKPD
#define RA_MASK_PHYDMLIZE_AP
/* #define	CONFIG_RA_DBG_CMD*/
/*#define	CONFIG_RA_FW_DBG_CODE*/

/*#define	CONFIG_PATH_DIVERSITY*/
/*#define	CONFIG_RA_DYNAMIC_RTY_LIMIT*/
#define	CONFIG_RA_DYNAMIC_RATE_ID
#define	CONFIG_BB_TXBF_API
/*#define	ODM_CONFIG_BT_COEXIST*/
/*#define	PHYDM_3RD_REFORM_RA_MASK*/
#define	PHYDM_3RD_REFORM_RSSI_MONOTOR
#define	PHYDM_SUPPORT_RSSI_MONITOR
#if !defined(CONFIG_DISABLE_PHYDM_DEBUG_FUNCTION)
	#define CONFIG_PHYDM_DEBUG_FUNCTION
#endif

/* [ Configure Antenna Diversity ] */
#if defined(CONFIG_RTL_8881A_ANT_SWITCH) || defined(CONFIG_SLOT_0_ANT_SWITCH) || defined(CONFIG_SLOT_1_ANT_SWITCH)
	#define CONFIG_PHYDM_ANTENNA_DIVERSITY
	#define ODM_EVM_ENHANCE_ANTDIV
	#define SKIP_EVM_ANTDIV_TRAINING_PATCH

	/*----------*/

	#if (!defined(CONFIG_NO_2G_DIVERSITY) && !defined(CONFIG_2G5G_CG_TRX_DIVERSITY_8881A) && !defined(CONFIG_2G_CGCS_RX_DIVERSITY) && !defined(CONFIG_2G_CG_TRX_DIVERSITY) && !defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		#define CONFIG_NO_2G_DIVERSITY
	#endif

	#ifdef CONFIG_NO_5G_DIVERSITY_8881A
		#define CONFIG_NO_5G_DIVERSITY
	#elif defined(CONFIG_5G_CGCS_RX_DIVERSITY_8881A)
		#define CONFIG_5G_CGCS_RX_DIVERSITY
	#elif defined(CONFIG_5G_CG_TRX_DIVERSITY_8881A)
		#define CONFIG_5G_CG_TRX_DIVERSITY
	#elif defined(CONFIG_2G5G_CG_TRX_DIVERSITY_8881A)
		#define CONFIG_2G5G_CG_TRX_DIVERSITY
	#endif
	#if (!defined(CONFIG_NO_5G_DIVERSITY) && !defined(CONFIG_5G_CGCS_RX_DIVERSITY) && !defined(CONFIG_5G_CG_TRX_DIVERSITY) && !defined(CONFIG_2G5G_CG_TRX_DIVERSITY) && !defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY))
		#define CONFIG_NO_5G_DIVERSITY
	#endif
	/*----------*/
	#if (defined(CONFIG_NO_2G_DIVERSITY) && defined(CONFIG_NO_5G_DIVERSITY))
		#define CONFIG_NOT_SUPPORT_ANTDIV
	#elif (!defined(CONFIG_NO_2G_DIVERSITY) && defined(CONFIG_NO_5G_DIVERSITY))
		#define CONFIG_2G_SUPPORT_ANTDIV
	#elif (defined(CONFIG_NO_2G_DIVERSITY) && !defined(CONFIG_NO_5G_DIVERSITY))
		#define CONFIG_5G_SUPPORT_ANTDIV
	#elif ((!defined(CONFIG_NO_2G_DIVERSITY) && !defined(CONFIG_NO_5G_DIVERSITY)) || defined(CONFIG_2G5G_CG_TRX_DIVERSITY))
			#define CONFIG_2G5G_SUPPORT_ANTDIV
	#endif
		/*----------*/
#endif /*Antenna Diveristy*/

/*[SmartAntenna]*/
/*#define	CONFIG_SMART_ANTENNA*/
#ifdef CONFIG_SMART_ANTENNA
	/*#define	CONFIG_CUMITEK_SMART_ANTENNA*/
#endif
/* --------------------------------------------------*/

#endif
