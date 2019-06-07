/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __MP_PRECOMP_H__
#define __MP_PRECOMP_H__

#include <drv_types.h>
#include <hal_data.h>

#define BT_TMP_BUF_SIZE	100

#define DCMD_Printf			DBG_BT_INFO

#ifdef bEnable
#undef bEnable
#endif

#include "HalBtcOutSrc.h"
#include "HalBtc8723b1Ant.h"
#include "HalBtc8723b2Ant.h"

#endif /*  __MP_PRECOMP_H__ */
