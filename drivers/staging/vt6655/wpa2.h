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
 * File: wpa2.h
 *
 * Purpose: Defines the macros, types, and functions for dealing
 *          with WPA2 informations.
 *
 * Author: Yiching Chen
 *
 * Date: Oct. 4, 2004
 *
 */

#ifndef __WPA2_H__
#define __WPA2_H__


#if !defined(__TTYPE_H__)
#include "ttype.h"
#endif
#if !defined(__80211MGR_H__)
#include "80211mgr.h"
#endif
#if !defined(__80211HDR_H__)
#include "80211hdr.h"
#endif
#if !defined(__BSSDB_H__)
#include "bssdb.h"
#endif
#if !defined(__VNTWIFI_H__)
#include "vntwifi.h"
#endif



/*---------------------  Export Definitions -------------------------*/

typedef struct tagsPMKIDInfo {
    BYTE    abyBSSID[6];
    BYTE    abyPMKID[16];
} PMKIDInfo, *PPMKIDInfo;

typedef struct tagSPMKIDCache {
    ULONG       BSSIDInfoCount;
    PMKIDInfo   BSSIDInfo[MAX_PMKID_CACHE];
} SPMKIDCache, *PSPMKIDCache;


/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Types  ------------------------------*/

/*---------------------  Export Functions  --------------------------*/
#ifdef __cplusplus
extern "C" {                            /* Assume C declarations for C++ */
#endif /* __cplusplus */

VOID
WPA2_ClearRSN (
    IN PKnownBSS        pBSSNode
    );

VOID
WPA2vParseRSN (
    IN PKnownBSS        pBSSNode,
    IN PWLAN_IE_RSN     pRSN
    );

UINT
WPA2uSetIEs(
    IN PVOID pMgmtHandle,
    OUT PWLAN_IE_RSN pRSNIEs
    );


#ifdef __cplusplus
}                                       /* End of extern "C" { */
#endif /* __cplusplus */


#endif // __WPA2_H__
