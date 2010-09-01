//------------------------------------------------------------------------------

// <copyright file="ar6kap_common.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
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
//------------------------------------------------------------------------------

//==============================================================================

// This file contains the definitions of common AP mode data structures.
//
// Author(s): ="Atheros"
//==============================================================================

#ifndef _AR6KAP_COMMON_H_
#define _AR6KAP_COMMON_H_
/*
 * Used with AR6000_XIOCTL_AP_GET_STA_LIST
 */
typedef struct {
    A_UINT8     mac[ATH_MAC_LEN];
    A_UINT8     aid;
    A_UINT8     keymgmt;
    A_UINT8     ucipher;
    A_UINT8     auth;
} station_t;
typedef struct {
    station_t sta[AP_MAX_NUM_STA];
} ap_get_sta_t;
#endif /* _AR6KAP_COMMON_H_ */
