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
#ifndef __HAL_IC_CFG_H__
#define __HAL_IC_CFG_H__

#define RTL8188E_SUPPORT				0
#define RTL8812A_SUPPORT				0
#define RTL8821A_SUPPORT				0
#define RTL8723B_SUPPORT				0
#define RTL8723D_SUPPORT				0
#define RTL8192E_SUPPORT				0
#define RTL8814A_SUPPORT				0
#define RTL8195A_SUPPORT				0
#define RTL8197F_SUPPORT				0
#define RTL8703B_SUPPORT				0
#define RTL8188F_SUPPORT				0
#define RTL8822B_SUPPORT				0
#define RTL8821B_SUPPORT				0
#define RTL8821C_SUPPORT				0

/*#if (RTL8188E_SUPPORT==1)*/
#define RATE_ADAPTIVE_SUPPORT			0
#define POWER_TRAINING_ACTIVE			0

#ifdef CONFIG_MULTIDRV
#endif

#ifdef CONFIG_RTL8188E
	#undef RTL8188E_SUPPORT
	#undef RATE_ADAPTIVE_SUPPORT
	#undef POWER_TRAINING_ACTIVE

	#define RTL8188E_SUPPORT				1
	#define RATE_ADAPTIVE_SUPPORT			1
	#define POWER_TRAINING_ACTIVE			1
	#define CONFIG_GET_RAID_BY_DRV
#endif

#ifdef CONFIG_RTL8812A
	#undef RTL8812A_SUPPORT
	#define RTL8812A_SUPPORT				1
	#ifndef CONFIG_FW_C2H_PKT
		#define CONFIG_FW_C2H_PKT
	#endif
#endif

#ifdef CONFIG_RTL8821A
	#undef RTL8821A_SUPPORT
	#define RTL8821A_SUPPORT				1
	#ifndef CONFIG_FW_C2H_PKT
		#define CONFIG_FW_C2H_PKT
	#endif
#endif

#ifdef CONFIG_RTL8192E
	#undef RTL8192E_SUPPORT
	#define RTL8192E_SUPPORT				1
	#ifndef CONFIG_FW_C2H_PKT
		#define CONFIG_FW_C2H_PKT
	#endif
#endif

#ifdef CONFIG_RTL8723B
	#undef RTL8723B_SUPPORT
	#define RTL8723B_SUPPORT				1
	#ifndef CONFIG_FW_C2H_PKT
		#define CONFIG_FW_C2H_PKT
	#endif
#endif

#ifdef CONFIG_RTL8723D
	#undef RTL8723D_SUPPORT
	#define RTL8723D_SUPPORT				1
	#ifndef CONFIG_FW_C2H_PKT
		#define CONFIG_FW_C2H_PKT
	#endif
	#ifndef CONFIG_RTW_MAC_HIDDEN_RPT
		#define CONFIG_RTW_MAC_HIDDEN_RPT
	#endif
	#ifndef CONFIG_RTW_CUSTOMER_STR
		#define CONFIG_RTW_CUSTOMER_STR
	#endif
#endif

#ifdef CONFIG_RTL8814A
	#undef RTL8814A_SUPPORT
	#define RTL8814A_SUPPORT				1
	#ifndef CONFIG_FW_C2H_PKT
		#define CONFIG_FW_C2H_PKT
	#endif
#endif

#ifdef CONFIG_RTL8703B
	#undef RTL8703B_SUPPORT
	#define RTL8703B_SUPPORT				1
	#ifndef CONFIG_FW_C2H_PKT
		#define CONFIG_FW_C2H_PKT
	#endif
	#ifndef CONFIG_RTW_MAC_HIDDEN_RPT
		#define CONFIG_RTW_MAC_HIDDEN_RPT
	#endif
#endif

#ifdef CONFIG_RTL8188F
	#undef RTL8188F_SUPPORT
	#define RTL8188F_SUPPORT				1
	#ifndef CONFIG_FW_C2H_PKT
		#define CONFIG_FW_C2H_PKT
	#endif
	#ifndef CONFIG_RTW_MAC_HIDDEN_RPT
		#define CONFIG_RTW_MAC_HIDDEN_RPT
	#endif
	#ifndef CONFIG_RTW_CUSTOMER_STR
		#define CONFIG_RTW_CUSTOMER_STR
	#endif
#endif

#ifdef CONFIG_RTL8822B
	#undef RTL8822B_SUPPORT
	#define RTL8822B_SUPPORT				1
	#ifndef CONFIG_FW_C2H_PKT
		#define CONFIG_FW_C2H_PKT
	#endif /* CONFIG_FW_C2H_PKT */

	#ifdef CONFIG_WOWLAN
		#define CONFIG_GTK_OL
		#define CONFIG_ARP_KEEP_ALIVE
		#ifndef CONFIG_DEFAULT_PATTERNS_EN
			#warning "Force to enable CONFIG_DEFAULT_PATTERNS_EN under WOW"
			#define CONFIG_DEFAULT_PATTERNS_EN
		#endif /* !CONFIG_DEFAULT_PATTERNS_EN */

		#ifdef CONFIG_GPIO_WAKEUP
			#ifndef WAKEUP_GPIO_IDX
				#define WAKEUP_GPIO_IDX	6	/* WIFI Chip Side */
			#endif /* !WAKEUP_GPIO_IDX */
		#endif /* CONFIG_GPIO_WAKEUP */

	#endif /* CONFIG_WOWLAN */
	#ifdef CONFIG_CONCURRENT_MODE
		#define CONFIG_AP_PORT_SWAP
	#endif /* CONFIG_CONCURRENT_MODE */

	/*
	 * Beamforming related definition
	 */
	#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_BEAMFORMING)
		#undef CONFIG_BEAMFORMING
		#warning "Not support Beamforming in concurrent mode yet!!"
	#endif /* CONFIG_CONCURRENT_MODE && CONFIG_BEAMFORMING */
	/* Beamforming mechanism is on driver not phydm, always disable it */
	#define BEAMFORMING_SUPPORT				0
	/* Only support new beamforming mechanism */
	#ifdef CONFIG_BEAMFORMING
		#define RTW_BEAMFORMING_VERSION_2
	#endif /* CONFIG_BEAMFORMING */

	#ifndef CONFIG_RTW_MAC_HIDDEN_RPT
		#define CONFIG_RTW_MAC_HIDDEN_RPT
#endif /* CONFIG_RTW_MAC_HIDDEN_RPT */

#endif /* CONFIG_RTL8822B */

#ifdef CONFIG_RTL8821C
	#undef RTL8821C_SUPPORT
	#define RTL8821C_SUPPORT				1
	#ifndef CONFIG_FW_C2H_PKT
		#define CONFIG_FW_C2H_PKT
	#endif
	#ifdef CONFIG_NO_FW
		#ifdef CONFIG_RTW_MAC_HIDDEN_RPT
			#undef CONFIG_RTW_MAC_HIDDEN_RPT
		#endif
	#else
		#ifndef CONFIG_RTW_MAC_HIDDEN_RPT
			#define CONFIG_RTW_MAC_HIDDEN_RPT
		#endif
	#endif
	#define LOAD_FW_HEADER_FROM_DRIVER
	#define CONFIG_PHY_CAPABILITY_QUERY
	#ifdef CONFIG_CONCURRENT_MODE
	#define CONFIG_AP_PORT_SWAP
	/*#define CONFIG_FW_MULTI_PORT_SUPPORT*/
	#endif
	#define CONFIG_SUPPORT_FIFO_DUMP
#endif

#endif /*__HAL_IC_CFG_H__*/
