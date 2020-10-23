/******************************************************************************
 *
 * Copyright(c) 2013 - 2017 Realtek Corporation.
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
#ifndef __MP_PRECOMP_H__
#define __MP_PRECOMP_H__

#include <drv_types.h>
#include <hal_data.h>
#include "btc_basic_types.h"

#define BT_TMP_BUF_SIZE	100

#ifdef PLATFORM_LINUX
#define rsprintf snprintf
#define rstrncat(dst, src, src_size) strncat(dst, src, src_size)
#elif defined(PLATFORM_WINDOWS)
#define rsprintf sprintf_s
#endif

#define DCMD_Printf			DBG_BT_INFO

#define delay_ms(ms)		rtw_mdelay_os(ms)

#ifdef bEnable
#undef bEnable
#endif

#define WPP_SOFTWARE_TRACE 0

typedef enum _BTC_MSG_COMP_TYPE {
	COMP_COEX		= 0,
	COMP_MAX
} BTC_MSG_COMP_TYPE;
extern u4Byte GLBtcDbgType[];

#define DBG_OFF			0
#define DBG_SEC			1
#define DBG_SERIOUS		2
#define DBG_WARNING		3
#define DBG_LOUD		4
#define DBG_TRACE		5

#ifdef CONFIG_BT_COEXIST
#define BT_SUPPORT		1
#define COEX_SUPPORT	1
#define HS_SUPPORT		1
#else
#define BT_SUPPORT		0
#define COEX_SUPPORT	0
#define HS_SUPPORT		0
#endif

/* for wifi only mode */
#include "hal_btcoex_wifionly.h"

#ifdef CONFIG_BT_COEXIST
#define BTC_BTINFO_LENGTH_MAX 10

struct wifi_only_cfg;
struct btc_coexist;

#ifdef CONFIG_RTL8192E
#include "halbtc8192e1ant.h"
#include "halbtc8192e2ant.h"
#endif

#ifdef CONFIG_RTL8723B
#include "halbtc8723bwifionly.h"
#include "halbtc8723b1ant.h"
#include "halbtc8723b2ant.h"
#endif

#ifdef CONFIG_RTL8812A
#include "halbtc8812a1ant.h"
#include "halbtc8812a2ant.h"
#endif

#ifdef CONFIG_RTL8821A
#include "halbtc8821a1ant.h"
#include "halbtc8821a2ant.h"
#endif

#ifdef CONFIG_RTL8703B
#include "halbtc8703b1ant.h"
#endif

#ifdef CONFIG_RTL8723D
#include "halbtc8723d1ant.h"
#include "halbtc8723d2ant.h"
#endif

#ifdef CONFIG_RTL8822B
#include "halbtc8822bwifionly.h"
#include "halbtc8822b1ant.h"
#include "halbtc8822b2ant.h"
#endif

#ifdef CONFIG_RTL8821C
#include "halbtc8821cwifionly.h"
#include "halbtc8821c1ant.h"
#include "halbtc8821c2ant.h"
#endif

#ifdef CONFIG_RTL8814A
#include "halbtc8814a2ant.h"
#endif

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
#include "halbtccommon.h"

#ifdef CONFIG_RTL8822C
#include "halbtc8822cwifionly.h"
#include "halbtc8822c.h"
#endif

#ifdef CONFIG_RTL8723F
#include "halbtc8723fwifionly.h"
#include "halbtc8723f.h"
#endif

#ifdef CONFIG_RTL8192F
#include "halbtc8192f.h"
#endif

#endif

#include "halbtcoutsrc.h"

#else /* CONFIG_BT_COEXIST */

#ifdef CONFIG_RTL8723B
#include "halbtc8723bwifionly.h"
#endif

#ifdef CONFIG_RTL8822B
#include "halbtc8822bwifionly.h"
#endif

#ifdef CONFIG_RTL8821C
#include "halbtc8821cwifionly.h"
#endif

#ifdef CONFIG_RTL8822C
#include "halbtc8822cwifionly.h"
#endif

#ifdef CONFIG_RTL8723F
#include "halbtc8723fwifionly.h"
#endif

#ifdef CONFIG_RTL8814B
#include "halbtc8814bwifionly.h"
#endif

#endif /* CONFIG_BT_COEXIST */

#endif /*  __MP_PRECOMP_H__ */
