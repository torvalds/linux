/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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

/*[Driver] provide the define of _TRUE, _FALSE, NULL, u8, u16, u32*/
#ifndef NULL
#define NULL		((void *)0)
#endif

#define HALMAC_INLINE	inline

typedef u8	*pu8;
typedef u16	*pu16;
typedef u32	*pu32;
typedef s8	*ps8;
typedef s16	*ps16;
typedef s32	*ps32;


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

/*[Driver] define the Platform SDIO Bus CLK */
#define PLATFORM_SD_CLK	50000000 /*50MHz*/

/*[Driver] provide the type mutex*/
/* Mutex type */
typedef _mutex		HALMAC_MUTEX;

#endif /* _HALMAC_2_PLATFORM_H_ */

