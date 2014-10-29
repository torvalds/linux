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
 *
 * File: vntwifi.h
 *
 * Purpose: export VNT Host WiFi library function
 *
 * Author: Yiching Chen
 *
 * Date: Jan 7, 2004
 *
 */

#ifndef __VNTWIFI_H__
#define __VNTWIFI_H__

#include "ttype.h"
#include "80211mgr.h"
#include "card.h"

/*---------------------  Export Definitions -------------------------*/

// key CipherSuite
#define KEY_CTL_WEP         0x00
#define KEY_CTL_NONE        0x01
#define KEY_CTL_TKIP        0x02
#define KEY_CTL_CCMP        0x03
#define KEY_CTL_INVALID     0xFF

#define CHANNEL_MAX_24G         14

#define MAX_BSS_NUM             42

// Pre-configured Authenticaiton Mode (from XP)
typedef enum tagWMAC_AUTHENTICATION_MODE {
	WMAC_AUTH_OPEN,
	WMAC_AUTH_SHAREKEY,
	WMAC_AUTH_AUTO,
	WMAC_AUTH_WPA,
	WMAC_AUTH_WPAPSK,
	WMAC_AUTH_WPANONE,
	WMAC_AUTH_WPA2,
	WMAC_AUTH_WPA2PSK,
	WMAC_AUTH_MAX       // Not a real mode, defined as upper bound
} WMAC_AUTHENTICATION_MODE, *PWMAC_AUTHENTICATION_MODE;

typedef enum tagWMAC_ENCRYPTION_MODE {
	WMAC_ENCRYPTION_WEPEnabled,
	WMAC_ENCRYPTION_WEPDisabled,
	WMAC_ENCRYPTION_WEPKeyAbsent,
	WMAC_ENCRYPTION_WEPNotSupported,
	WMAC_ENCRYPTION_TKIPEnabled,
	WMAC_ENCRYPTION_TKIPKeyAbsent,
	WMAC_ENCRYPTION_AESEnabled,
	WMAC_ENCRYPTION_AESKeyAbsent
} WMAC_ENCRYPTION_MODE, *PWMAC_ENCRYPTION_MODE;

// Pre-configured Mode (from XP)

typedef enum tagWMAC_CONFIG_MODE {
	WMAC_CONFIG_ESS_STA = 0,
	WMAC_CONFIG_IBSS_STA,
	WMAC_CONFIG_AUTO,
	WMAC_CONFIG_AP
} WMAC_CONFIG_MODE, *PWMAC_CONFIG_MODE;

typedef enum tagWMAC_POWER_MODE {
	WMAC_POWER_CAM,
	WMAC_POWER_FAST,
	WMAC_POWER_MAX
} WMAC_POWER_MODE, *PWMAC_POWER_MODE;


#endif //__VNTWIFI_H__
