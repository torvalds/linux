/******************************************************************************
 *
 * Copyright(c) 2015 - 2018 Realtek Corporation. All rights reserved.
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
 ******************************************************************************/

#ifndef _HALMAC_2_PLATFORM_H_
#define _HALMAC_2_PLATFORM_H_

/*[Driver] always set BUILD_TEST =0*/
#define BUILD_TEST	0

#if BUILD_TEST
#include "../Platform/App/Test/halmac_2_platformapi.h"
#else
/*[Driver] use their own header files*/
#include <drv_conf.h>			/* for basic_types.h and osdep_service.h */
#include <basic_types.h>		/* u8, u16, u32 and etc.*/
#include <osdep_service.h>		/* __BIG_ENDIAN, __LITTLE_ENDIAN, _sema, _mutex */
#endif

/*[Driver] provide the define of NULL, u8, u16, u32*/
#ifndef NULL
#define NULL		((void *)0)
#endif

#define HALMAC_INLINE	inline

/*
 * Ignore following typedef because Linux already have these
 * u8, u16, u32, s8, s16, s32
 * __le16, __le32, __be16, __be32
 */

#define HALMAC_PLATFORM_LITTLE_ENDIAN	1
#define HALMAC_PLATFORM_BIG_ENDIAN	0

/* Note : Named HALMAC_PLATFORM_LITTLE_ENDIAN / HALMAC_PLATFORM_BIG_ENDIAN
 * is not mandatory. But Little endian must be '1'. Big endian must be '0'
 */
/*[Driver] config the system endian*/
#ifdef __LITTLE_ENDIAN
#define HALMAC_SYSTEM_ENDIAN	HALMAC_PLATFORM_LITTLE_ENDIAN
#else /* !__LITTLE_ENDIAN */
#define HALMAC_SYSTEM_ENDIAN	HALMAC_PLATFORM_BIG_ENDIAN
#endif /* !__LITTLE_ENDIAN */

/*[Driver] config if the operating platform*/
#define HALMAC_PLATFORM_WINDOWS		0
#define HALMAC_PLATFORM_LINUX		1
#define HALMAC_PLATFORM_AP		0
/*[Driver] must set HALMAC_PLATFORM_TESTPROGRAM = 0*/
#define HALMAC_PLATFORM_TESTPROGRAM	0

/*[Driver] config if enable the dbg msg or notl*/
#define HALMAC_DBG_MSG_ENABLE		1

#define HALMAC_MSG_LEVEL_TRACE		3
#define HALMAC_MSG_LEVEL_WARNING	2
#define HALMAC_MSG_LEVEL_ERR		1
#define HALMAC_MSG_LEVEL_NO_LOG		0
/*[Driver] config halmac msg level
 * Use HALMAC_MSG_LEVEL_XXXX
 */
#define HALMAC_MSG_LEVEL HALMAC_MSG_LEVEL_TRACE

#ifdef DBG_IO
#define HALMAC_DBG_MONITOR_IO		1
#else
#define HALMAC_DBG_MONITOR_IO		0
#endif /*DBG_IO*/

/*[Driver] define the Rx FIFO expanding mode packet size unit for 8821C and 8822B */
/*Should be 8 Byte alignment*/
#define HALMAC_RX_FIFO_EXPANDING_MODE_PKT_SIZE	80 /*Bytes*/

#define HALMAC_USE_TYPEDEF		0

/*[Driver] provide the type mutex*/
/* Mutex type */
typedef _mutex		HALMAC_MUTEX;

#endif /* _HALMAC_2_PLATFORM_H_ */

