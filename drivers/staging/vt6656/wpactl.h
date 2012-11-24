/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: wpactl.h
 *
 * Purpose:
 *
 * Author: Lyndon Chen
 *
 * Date: March 1, 2005
 *
 */

#ifndef __WPACTL_H__
#define __WPACTL_H__

#include "device.h"
#include "iowpa.h"

/*---------------------  Export Definitions -------------------------*/


//WPA related

typedef enum { WPA_ALG_NONE, WPA_ALG_WEP, WPA_ALG_TKIP, WPA_ALG_CCMP } wpa_alg;

#define AUTH_ALG_OPEN_SYSTEM	0x01
#define AUTH_ALG_SHARED_KEY	0x02
#define AUTH_ALG_LEAP		0x04


typedef unsigned long long NDIS_802_11_KEY_RSC;

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

int wpa_set_keys(PSDevice pDevice, void *ctx, BOOL  fcpfkernel);

#endif /* __WPACL_H__ */
