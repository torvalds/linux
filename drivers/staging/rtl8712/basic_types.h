/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __BASIC_TYPES_H__
#define __BASIC_TYPES_H__

#include <linux/types.h>

#define sint signed int

/* Should we extend this to be host_addr_t and target_addr_t for case:
 *	host : x86_64
 *	target : mips64
 */
#define addr_t unsigned long

#endif /*__BASIC_TYPES_H__*/

