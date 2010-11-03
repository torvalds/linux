/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _proto_wpa_h_
#define _proto_wpa_h_

#include <proto/ethernet.h>

#include <packed_section_start.h>

#define DOT11_RC_INVALID_WPA_IE		13
#define DOT11_RC_MIC_FAILURE		14
#define DOT11_RC_4WH_TIMEOUT		15
#define DOT11_RC_GTK_UPDATE_TIMEOUT	16
#define DOT11_RC_WPA_IE_MISMATCH	17
#define DOT11_RC_INVALID_MC_CIPHER	18
#define DOT11_RC_INVALID_UC_CIPHER	19
#define DOT11_RC_INVALID_AKMP		20
#define DOT11_RC_BAD_WPA_VERSION	21
#define DOT11_RC_INVALID_WPA_CAP	22
#define DOT11_RC_8021X_AUTH_FAIL	23

#define WPA2_PMKID_LEN	16

typedef BWL_PRE_PACKED_STRUCT struct {
	u8 tag;
	u8 length;
	u8 oui[3];
	u8 oui_type;
	BWL_PRE_PACKED_STRUCT struct {
		u8 low;
		u8 high;
	} BWL_POST_PACKED_STRUCT version;
} BWL_POST_PACKED_STRUCT wpa_ie_fixed_t;
#define WPA_IE_OUITYPE_LEN	4
#define WPA_IE_FIXED_LEN	8
#define WPA_IE_TAG_FIXED_LEN	6

typedef BWL_PRE_PACKED_STRUCT struct {
	u8 tag;
	u8 length;
	BWL_PRE_PACKED_STRUCT struct {
		u8 low;
		u8 high;
	} BWL_POST_PACKED_STRUCT version;
} BWL_POST_PACKED_STRUCT wpa_rsn_ie_fixed_t;
#define WPA_RSN_IE_FIXED_LEN	4
#define WPA_RSN_IE_TAG_FIXED_LEN	2
typedef u8 wpa_pmkid_t[WPA2_PMKID_LEN];

typedef BWL_PRE_PACKED_STRUCT struct {
	u8 oui[3];
	u8 type;
} BWL_POST_PACKED_STRUCT wpa_suite_t, wpa_suite_mcast_t;
#define WPA_SUITE_LEN	4

typedef BWL_PRE_PACKED_STRUCT struct {
	BWL_PRE_PACKED_STRUCT struct {
		u8 low;
		u8 high;
	} BWL_POST_PACKED_STRUCT count;
	wpa_suite_t list[1];
} BWL_POST_PACKED_STRUCT wpa_suite_ucast_t, wpa_suite_auth_key_mgmt_t;
#define WPA_IE_SUITE_COUNT_LEN	2
typedef BWL_PRE_PACKED_STRUCT struct {
	BWL_PRE_PACKED_STRUCT struct {
		u8 low;
		u8 high;
	} BWL_POST_PACKED_STRUCT count;
	wpa_pmkid_t list[1];
} BWL_POST_PACKED_STRUCT wpa_pmkid_list_t;

#define WPA_CIPHER_NONE		0
#define WPA_CIPHER_WEP_40	1
#define WPA_CIPHER_TKIP		2
#define WPA_CIPHER_AES_OCB	3
#define WPA_CIPHER_AES_CCM	4
#define WPA_CIPHER_WEP_104	5

#define IS_WPA_CIPHER(cipher)	((cipher) == WPA_CIPHER_NONE || \
				 (cipher) == WPA_CIPHER_WEP_40 || \
				 (cipher) == WPA_CIPHER_WEP_104 || \
				 (cipher) == WPA_CIPHER_TKIP || \
				 (cipher) == WPA_CIPHER_AES_OCB || \
				 (cipher) == WPA_CIPHER_AES_CCM)

#define WPA_TKIP_CM_DETECT	60
#define WPA_TKIP_CM_BLOCK	60

#define RSN_CAP_LEN		2

#define RSN_CAP_PREAUTH			0x0001
#define RSN_CAP_NOPAIRWISE		0x0002
#define RSN_CAP_PTK_REPLAY_CNTR_MASK	0x000C
#define RSN_CAP_PTK_REPLAY_CNTR_SHIFT	2
#define RSN_CAP_GTK_REPLAY_CNTR_MASK	0x0030
#define RSN_CAP_GTK_REPLAY_CNTR_SHIFT	4
#define RSN_CAP_1_REPLAY_CNTR		0
#define RSN_CAP_2_REPLAY_CNTRS		1
#define RSN_CAP_4_REPLAY_CNTRS		2
#define RSN_CAP_16_REPLAY_CNTRS		3

#define WPA_CAP_4_REPLAY_CNTRS		RSN_CAP_4_REPLAY_CNTRS
#define WPA_CAP_16_REPLAY_CNTRS		RSN_CAP_16_REPLAY_CNTRS
#define WPA_CAP_REPLAY_CNTR_SHIFT	RSN_CAP_PTK_REPLAY_CNTR_SHIFT
#define WPA_CAP_REPLAY_CNTR_MASK	RSN_CAP_PTK_REPLAY_CNTR_MASK

#define WPA_CAP_LEN	RSN_CAP_LEN

#define	WPA_CAP_WPA2_PREAUTH		RSN_CAP_PREAUTH

#include <packed_section_end.h>

#endif				/* _proto_wpa_h_ */
