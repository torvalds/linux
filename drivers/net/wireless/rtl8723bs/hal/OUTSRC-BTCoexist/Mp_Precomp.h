/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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
#ifndef __MP_PRECOMP_H__
#define __MP_PRECOMP_H__

#include <drv_types.h>
#include <hal_data.h>

#define BT_TMP_BUF_SIZE	100

#ifdef PLATFORM_LINUX
#define rsprintf snprintf
#elif defined(PLATFORM_WINDOWS)
#define rsprintf sprintf_s
#endif

#define DCMD_Printf			DBG_BT_INFO

#define delay_ms(ms)		rtw_mdelay_os(ms)

#ifdef bEnable
#undef bEnable
#endif

#include "HalBtcOutSrc.h"
#include "HalBtc8188c2Ant.h"
#include "HalBtc8192d2Ant.h"
#include "HalBtc8192e1Ant.h"
#include "HalBtc8192e2Ant.h"
#include "HalBtc8723a1Ant.h"
#include "HalBtc8723a2Ant.h"
#include "HalBtc8723b1Ant.h"
#include "HalBtc8723b2Ant.h"
#include "HalBtc8812a1Ant.h"
#include "HalBtc8812a2Ant.h"
#include "HalBtc8821a1Ant.h"
#include "HalBtc8821a2Ant.h"

#endif // __MP_PRECOMP_H__
