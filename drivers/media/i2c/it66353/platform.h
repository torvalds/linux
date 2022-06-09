// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * it66353 HDMI 3 in 1 out driver.
 *
 * Author: Kenneth.Hung@ite.com.tw
 *         Wangqiang Guo <kay.guo@rock-chips.com>
 * Version: IT66353_SAMPLE_1.08
 *
 */
#ifndef _PLATFORM_H_
#define _PLATFORM_H_
/*
 * #ifndef u8
 * typedef unsigned char u8 ;
 * #endif
 * #ifndef u16
 * typedef unsigned short u16;
 * #endif
 * #ifndef u32
 * typedef unsigned long u32;
 * #endif
 * #ifndef __tick
 * typedef unsigned long __tick;
 * #endif
 *
 * #ifndef __cplusplus
 * #ifndef bool
 * typedef unsigned char bool ;
 * #endif
 * #endif
 */

typedef unsigned long __tick;
#define CONST const
/*
 * #ifndef true
 * #define true 1
 * #endif
 *
 * #ifndef false
 * #define false 0
 * #endif
 */
/*
 * assign the print function
 *
 * #define pr_err    dev_err
 * #define pr_info   dev_info
 * #define pr_info2  dev_dbg
 */

// ---------- for CEC

#define iTE_FALSE	0
#define iTE_TRUE	1

#define WIN32

#ifdef _MCU_8051_
	typedef bit iTE_u1;
	#define _CODE code
	#define _CODE_3K code

#elif defined (WIN32)
	typedef int iTE_u1;
	#define _CODE const
	#define _CODE_3K const
/*
 * #elif defined (_MCU_IT6350_)
 * typedef unsigned char iTE_u1;
 * #define _CODE  __attribute__ ((section ("._OEM_BU1_RODATA ")))
 * #define _CODE_3K __attribute__ ((section ("._3K_RODATA ")))
 */
#elif defined (__WIN32__)
	typedef unsigned char iTE_u1;
	#define _CODE const
	#define _CODE_3K const

#else
	#error("Please define this section by your platform")
	typedef int iTE_u1;
	#define _CODE
	#define _CODE_3K
#endif // _MCU_8051_

/*
 * output TXOE state on JP47 (GPC5)
 * by nVidia's clock detect request
 */
#define REPORT_TXOE_0(x) {GPDRC &= ~0x20; } //GPC5=0;
#define REPORT_TXOE_1(x) {GPDRC |= 0x20; } //GPC5=1;

/*
 * typedef char iTE_s8, *iTE_ps8;
 * typedef unsigned char iTE_u8, *iTE_pu8;
 * typedef short iTE_s16, *iTE_ps16;
 * typedef unsigned short iTE_u16, *iTE_pu16;
 */

#endif




