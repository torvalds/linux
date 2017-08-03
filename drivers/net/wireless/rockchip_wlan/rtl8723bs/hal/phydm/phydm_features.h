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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#ifndef	__PHYDM_FEATURES_H__
#define __PHYDM_FEATURES


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	/*Antenna Diversity*/
	#define CONFIG_PHYDM_ANTENNA_DIVERSITY
	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	
		#if (RTL8723B_SUPPORT == 1) || (RTL8821A_SUPPORT == 1) || (RTL8188F_SUPPORT == 1)
		#define	CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		#endif
		
		#if (RTL8821A_SUPPORT == 1)
		/*#define CONFIG_HL_SMART_ANTENNA_TYPE1*/
		#endif
	#endif

	/*#define CONFIG_PATH_DIVERSITY*/
	/*#define CONFIG_RA_DYNAMIC_RTY_LIMIT*/
	#define CONFIG_ANT_DETECTION
	#define CONFIG_RA_DBG_CMD

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	/*  [ Configure RA Debug H2C CMD ]*/
	#define CONFIG_RA_DBG_CMD
	
	/*#define CONFIG_PATH_DIVERSITY*/
	/*#define CONFIG_RA_DYNAMIC_RTY_LIMIT*/
	#define CONFIG_RA_DYNAMIC_RATE_ID
	
	/* [ Configure Antenna Diversity ] */
	#if defined(CONFIG_RTL_8881A_ANT_SWITCH) || defined(CONFIG_SLOT_0_ANT_SWITCH) || defined(CONFIG_SLOT_1_ANT_SWITCH)
		#define CONFIG_PHYDM_ANTENNA_DIVERSITY 
		#define ODM_EVM_ENHANCE_ANTDIV

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
	#endif

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

	/*Antenna Diversity*/
	#ifdef CONFIG_ANTENNA_DIVERSITY
		#define CONFIG_PHYDM_ANTENNA_DIVERSITY
		
		#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
		
			#if (RTL8723B_SUPPORT == 1) || (RTL8821A_SUPPORT == 1) || (RTL8188F_SUPPORT == 1)
			#define	CONFIG_S0S1_SW_ANTENNA_DIVERSITY
			#endif
			
			#if (RTL8821A_SUPPORT == 1)
			/*#define CONFIG_HL_SMART_ANTENNA_TYPE1*/
			#endif
		#endif
	#endif
	
	/*#define CONFIG_RA_DBG_CMD*/
	/*#define CONFIG_ANT_DETECTION*/
	/*#define CONFIG_PATH_DIVERSITY*/
	/*#define CONFIG_RA_DYNAMIC_RTY_LIMIT*/

	#ifdef CONFIG_BT_COEXIST
		#define BT_SUPPORT	1
	#endif

#endif


#endif
