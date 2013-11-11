/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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

