/*
 * Copyright (c) 2008 Atheros Communications, Inc.
 * All rights reserved.
 *
 *
 * 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
 *
 */

#if defined(AR6003_HEADERS_DEF) || defined(ATHR_WIN_DEF)
#define AR6002 1
#define AR6002_REV 4

#define WLAN_HEADERS 1
#include "common_drv.h"
#include "AR6002/hw4.0/hw/apb_map.h"
#include "AR6002/hw4.0/hw/gpio_reg.h"
#include "AR6002/hw4.0/hw/rtc_reg.h"
#include "AR6002/hw4.0/hw/si_reg.h"
#include "AR6002/hw4.0/hw/mbox_reg.h"
#include "AR6002/hw4.0/hw/mbox_wlan_host_reg.h"

#define MY_TARGET_DEF AR6003_TARGETdef
#define MY_HOST_DEF AR6003_HOSTdef
#define MY_TARGET_BOARD_DATA_SZ AR6003_BOARD_DATA_SZ
#define MY_TARGET_BOARD_EXT_DATA_SZ AR6003_BOARD_EXT_DATA_SZ
#define RTC_WMAC_BASE_ADDRESS              RTC_BASE_ADDRESS
#define RTC_SOC_BASE_ADDRESS               RTC_BASE_ADDRESS

#include "targetdef.h"
#include "hostdef.h"
#else
#include "common_drv.h"
#include "targetdef.h"
#include "hostdef.h"
struct targetdef_s *AR6003_TARGETdef=NULL;
struct hostdef_s *AR6003_HOSTdef=NULL;
#endif /*AR6003_HEADERS_DEF */
