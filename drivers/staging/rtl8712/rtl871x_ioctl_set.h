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
#ifndef __IOCTL_SET_H
#define __IOCTL_SET_H

#include "drv_types.h"

typedef u8 NDIS_802_11_PMKID_VALUE[16];

struct BSSIDInfo {
	unsigned char BSSID[6];
	NDIS_802_11_PMKID_VALUE PMKID;
};

u8 r8712_set_802_11_authentication_mode(struct _adapter *pdapter,
			enum NDIS_802_11_AUTHENTICATION_MODE authmode);

u8 r8712_set_802_11_bssid(struct _adapter *padapter, u8 *bssid);

u8 r8712_set_802_11_add_wep(struct _adapter *padapter,
			    struct NDIS_802_11_WEP *wep);

u8 r8712_set_802_11_disassociate(struct _adapter *padapter);

u8 r8712_set_802_11_bssid_list_scan(struct _adapter *padapter);

void r8712_set_802_11_infrastructure_mode(struct _adapter *padapter,
			enum NDIS_802_11_NETWORK_INFRASTRUCTURE networktype);

void r8712_set_802_11_ssid(struct _adapter *padapter,
			   struct ndis_802_11_ssid *ssid);

#endif

