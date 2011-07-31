/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*  Module Name : athr_common.h                                         */
/*                                                                      */
/*  Abstract                                                            */
/*      WPA related function and data structure definitions.            */
/*                                                                      */
/*  NOTES                                                               */
/*      Platform dependent.                                             */
/*                                                                      */
/************************************************************************/

#ifndef _ATHR_COMMON_H
#define _ATHR_COMMON_H

#define ZD_IOCTL_WPA			(SIOCDEVPRIVATE + 1)
#define ZD_IOCTL_PARAM			(SIOCDEVPRIVATE + 2)
#define ZD_IOCTL_GETWPAIE		(SIOCDEVPRIVATE + 3)
#define ZD_PARAM_ROAMING		0x0001
#define ZD_PARAM_PRIVACY		0x0002
#define ZD_PARAM_WPA			0x0003
#define ZD_PARAM_COUNTERMEASURES	0x0004
#define ZD_PARAM_DROPUNENCRYPTED	0x0005
#define ZD_PARAM_AUTH_ALGS		0x0006

#define ZD_CMD_SET_ENCRYPT_KEY		0x0001
#define ZD_CMD_SET_MLME			0x0002
#define ZD_CMD_SCAN_REQ			0x0003
#define ZD_CMD_SET_GENERIC_ELEMENT	0x0004
#define ZD_CMD_GET_TSC			0x0005

#define ZD_FLAG_SET_TX_KEY              0x0001

#define ZD_GENERIC_ELEMENT_HDR_LEN \
((int) (&((struct athr_wlan_param *) 0)->u.generic_elem.data))

#define ZD_CRYPT_ALG_NAME_LEN		16
#define ZD_MAX_KEY_SIZE			32
#define ZD_MAX_GENERIC_SIZE		64

#define IEEE80211_ADDR_LEN		6
#define IEEE80211_MAX_IE_SIZE		256

#ifdef ZM_ENALBE_WAPI
#define ZM_CMD_WAPI_SETWAPI             0x0001
#define ZM_CMD_WAPI_GETWAPI             0x0002
#define ZM_CMD_WAPI_SETKEY              0x0003
#define ZM_CMD_WAPI_GETKEY              0x0004
#define ZM_CMD_WAPI_REKEY               0x0005

#define ZM_WAPI_WAI_REQUEST             0x00f1
#define ZM_WAPI_UNICAST_REKEY           0x00f2
#define ZM_WAPI_STA_AGING               0x00f3
#define ZM_WAPI_MULTI_REKEY             0x00f4

#define ZM_WAPI_KEY_SIZE                32
#define ZM_WAPI_IV_LEN                  16
#endif /* ZM_ENALBE_WAPI */
/* structure definition */

struct athr_wlan_param {
	u32 cmd;
	u8 sta_addr[ETH_ALEN];
	union {
		struct {
			u8 alg[ZD_CRYPT_ALG_NAME_LEN];
			u32 flags;
			u32 err;
			u8 idx;
			u8 seq[8]; /* sequence counter (set: RX, get: TX) */
			u16 key_len;
			u8 key[ZD_MAX_KEY_SIZE];
		} crypt;
		struct {
			u32 flags_and;
			u32 flags_or;
		} set_flags_sta;
		struct {
			u8 len;
			u8 data[ZD_MAX_GENERIC_SIZE];
		} generic_elem;
		struct {
#define MLME_STA_DEAUTH 0
#define MLME_STA_DISASSOC 1
			u16 cmd;
			u16 reason_code;
		} mlme;
		struct {
			u8 ssid_len;
			u8 ssid[32];
		} scan_req;
	} u;
};

struct ieee80211req_wpaie {
	u8 wpa_macaddr[IEEE80211_ADDR_LEN];
	u8 wpa_ie[IEEE80211_MAX_IE_SIZE];
};

#ifdef ZM_ENALBE_WAPI
struct athr_wapi_param {
	u16 cmd;
	u16 len;

	union {
		struct {
			u8 sta_addr[ETH_ALEN];
			u8 reserved;
			u8 keyid;
			u8 key[ZM_WAPI_KEY_SIZE];
		} crypt;
		struct {
			u8 wapi_policy;
		} info;
	} u;
};

struct athr_wapi_sta_info
{
	u16	msg_type;
	u16	datalen;
	u8	sta_mac[ETH_ALEN];
	u8	reserve_data[2];
	u8	gsn[ZM_WAPI_IV_LEN];
	u8	wie[256];
};
#endif /* ZM_ENALBE_WAPI */
#endif
