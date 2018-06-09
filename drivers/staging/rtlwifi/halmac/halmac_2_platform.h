/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_2_PLATFORM_H_
#define _HALMAC_2_PLATFORM_H_

#include "../wifi.h"
#include <asm/byteorder.h>

#define HALMAC_PLATFORM_LITTLE_ENDIAN 1
#define HALMAC_PLATFORM_BIG_ENDIAN 0

/* Note : Named HALMAC_PLATFORM_LITTLE_ENDIAN / HALMAC_PLATFORM_BIG_ENDIAN
 * is not mandatory. But Little endian must be '1'. Big endian must be '0'
 */
#if defined(__LITTLE_ENDIAN)
#define HALMAC_SYSTEM_ENDIAN HALMAC_PLATFORM_LITTLE_ENDIAN
#elif defined(__BIG_ENDIAN)
#define HALMAC_SYSTEM_ENDIAN HALMAC_PLATFORM_BIG_ENDIAN
#else
#error
#endif

/* define the Platform SDIO Bus CLK */
#define PLATFORM_SD_CLK 50000000 /*50MHz*/

/* define the Rx FIFO expanding mode packet size unit for 8821C and 8822B */
/* Should be 8 Byte alignment */
#define HALMAC_RX_FIFO_EXPANDING_MODE_PKT_SIZE 16 /*Bytes*/

#endif /* _HALMAC_2_PLATFORM_H_ */
