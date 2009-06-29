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

#if !defined(__DEVICE_H__)
#include "device.h"
#endif
#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
#if !defined(__IOWPA_H__)
#include "iowpa.h"
#endif
#endif

/*---------------------  Export Definitions -------------------------*/


//WPA related

typedef enum { WPA_ALG_NONE, WPA_ALG_WEP, WPA_ALG_TKIP, WPA_ALG_CCMP } wpa_alg;
typedef enum { CIPHER_NONE, CIPHER_WEP40, CIPHER_TKIP, CIPHER_CCMP,
	       CIPHER_WEP104 } wpa_cipher;
typedef enum { KEY_MGMT_802_1X, KEY_MGMT_CCKM,KEY_MGMT_PSK, KEY_MGMT_NONE,
	       KEY_MGMT_802_1X_NO_WPA, KEY_MGMT_WPA_NONE } wpa_key_mgmt;

#define AUTH_ALG_OPEN_SYSTEM	0x01
#define AUTH_ALG_SHARED_KEY	0x02
#define AUTH_ALG_LEAP		0x04

#define GENERIC_INFO_ELEM 0xdd
#define RSN_INFO_ELEM 0x30



typedef ULONGLONG   NDIS_802_11_KEY_RSC;

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/


#ifdef __cplusplus
extern "C" {                            /* Assume C declarations for C++ */
#endif /* __cplusplus */

int wpa_set_wpadev(PSDevice pDevice, int val);
int wpa_ioctl(PSDevice pDevice, struct iw_point *p);
int wpa_set_keys(PSDevice pDevice, void *ctx, BOOL  fcpfkernel);

#ifdef __cplusplus
}                                       /* End of extern "C" { */
#endif /* __cplusplus */




#endif // __WPACL_H__



