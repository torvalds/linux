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
 * File: wpa.h
 *
 * Purpose: Defines the macros, types, and functions for dealing
 *          with WPA informations.
 *
 * Author: Kyle Hsu
 *
 * Date: Jul 14, 2003
 *
 */

#ifndef __WPA_H__
#define __WPA_H__

#include "80211hdr.h"

/*---------------------  Export Definitions -------------------------*/

#define WPA_NONE            0
#define WPA_WEP40           1
#define WPA_TKIP            2
#define WPA_AESWRAP         3
#define WPA_AESCCMP         4
#define WPA_WEP104          5
#define WPA_AUTH_IEEE802_1X 1
#define WPA_AUTH_PSK        2

#define WPA_GROUPFLAG       0x02
#define WPA_REPLAYBITSSHIFT 2
#define WPA_REPLAYBITS      0x03

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Types  ------------------------------*/


/*---------------------  Export Functions  --------------------------*/

void
WPA_ClearRSN(
     PKnownBSS        pBSSList
    );

void
WPA_ParseRSN(
     PKnownBSS        pBSSList,
     PWLAN_IE_RSN_EXT pRSN
    );

bool
WPA_SearchRSN(
    u8                byCmd,
    u8                byEncrypt,
     PKnownBSS        pBSSList
    );

bool
WPAb_Is_RSN(
     PWLAN_IE_RSN_EXT pRSN
    );

#endif /* __WPA_H__ */
