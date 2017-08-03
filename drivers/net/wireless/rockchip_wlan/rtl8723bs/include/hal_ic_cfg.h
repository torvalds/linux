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
#define RTL8192E_SUPPORT				0
#define RTL8814A_SUPPORT				0
#define RTL8195A_SUPPORT				0
#define RTL8703B_SUPPORT				0
#define RTL8188F_SUPPORT				0
#define RTL8822B_SUPPORT				0
#define RTL8821B_SUPPORT				0

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
#endif

#ifdef CONFIG_RTL8812A
#undef RTL8812A_SUPPORT
#define RTL8812A_SUPPORT				1
#endif

#ifdef CONFIG_RTL8821A
#undef RTL8821A_SUPPORT
#define RTL8821A_SUPPORT				1
#endif

#ifdef CONFIG_RTL8192E
#undef RTL8192E_SUPPORT
#define RTL8192E_SUPPORT				1
#endif

#ifdef CONFIG_RTL8723B
#undef RTL8723B_SUPPORT
#define RTL8723B_SUPPORT				1
#endif

#ifdef CONFIG_RTL8814A
#undef RTL8814A_SUPPORT
#define RTL8814A_SUPPORT				1
#endif

#ifdef CONFIG_RTL8703B
#undef RTL8703B_SUPPORT
#define RTL8703B_SUPPORT				1
#endif

#ifdef CONFIG_RTL8188F
#undef RTL8188F_SUPPORT
#define RTL8188F_SUPPORT				1
#endif

#ifdef CONFIG_RTL8822B
#undef RTL8822B_SUPPORT
#define RTL8822B_SUPPORT				1
#endif

#endif /*__HAL_IC_CFG_H__*/

