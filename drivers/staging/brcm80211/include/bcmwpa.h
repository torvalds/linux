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

#ifndef _BCMWPA_H_
#define _BCMWPA_H_

#include <proto/wpa.h>
#include <proto/802.11.h>
#include <wlioctl.h>

/* Field sizes for WPA key hierarchy */
#define WPA_MIC_KEY_LEN		16
#define WPA_ENCR_KEY_LEN	16
#define WPA_TEMP_ENCR_KEY_LEN	16
#define WPA_TEMP_TX_KEY_LEN	8
#define WPA_TEMP_RX_KEY_LEN	8

#define PMK_LEN			32
#define TKIP_PTK_LEN		64
#define TKIP_TK_LEN		32
#define AES_PTK_LEN		48
#define AES_TK_LEN		16

/* limits for pre-shared key lengths */
#define WPA_MIN_PSK_LEN		8
#define WPA_MAX_PSK_LEN		64

#define WLC_SW_KEYS(wlc, bsscfg) ((((wlc)->wsec_swkeys) || \
	((bsscfg)->wsec & WSEC_SWFLAG)))

#define WSEC_WEP_ENABLED(wsec)	((wsec) & WEP_ENABLED)
#define WSEC_TKIP_ENABLED(wsec)	((wsec) & TKIP_ENABLED)
#define WSEC_AES_ENABLED(wsec)	((wsec) & AES_ENABLED)
#define WSEC_ENABLED(wsec)	((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))
#define WSEC_SES_OW_ENABLED(wsec)	((wsec) & SES_OW_ENABLED)
#define IS_WPA_AUTH(auth)	((auth) == WPA_AUTH_NONE || \
				 (auth) == WPA_AUTH_UNSPECIFIED || \
				 (auth) == WPA_AUTH_PSK)
#define INCLUDES_WPA_AUTH(auth)	\
			((auth) & (WPA_AUTH_NONE | WPA_AUTH_UNSPECIFIED | WPA_AUTH_PSK))

#define IS_WPA2_AUTH(auth)	((auth) == WPA2_AUTH_UNSPECIFIED || \
				 (auth) == WPA2_AUTH_PSK)(
#define INCLUDES_WPA2_AUTH(auth) \
			((auth) & (WPA2_AUTH_UNSPECIFIED | \
				   WPA2_AUTH_PSK))

#define IS_WPA_AKM(akm)	((akm) == RSN_AKM_NONE || \
				 (akm) == RSN_AKM_UNSPECIFIED || \
				 (akm) == RSN_AKM_PSK)
#define IS_WPA2_AKM(akm)	((akm) == RSN_AKM_UNSPECIFIED || \
				 (akm) == RSN_AKM_PSK)

#define MAX_ARRAY 1
#define MIN_ARRAY 0

/* convert wsec to WPA mcast cipher. algo is needed only when WEP is enabled. */
#define WPA_MCAST_CIPHER(wsec, algo)	(WSEC_WEP_ENABLED(wsec) ? \
		((algo) == CRYPTO_ALGO_WEP128 ? WPA_CIPHER_WEP_104 : WPA_CIPHER_WEP_40) : \
			WSEC_TKIP_ENABLED(wsec) ? WPA_CIPHER_TKIP : \
			WSEC_AES_ENABLED(wsec) ? WPA_CIPHER_AES_CCM : \
			WPA_CIPHER_NONE)

/* Look for a WPA IE; return it's address if found, NULL otherwise */
extern wpa_ie_fixed_t *BCMROMFN(bcm_find_wpaie) (u8 *parse, uint len);

/* Check whether the given IE looks like WFA IE with the specific type. */
extern bool bcm_is_wfa_ie(u8 *ie, u8 **tlvs, uint *tlvs_len,
			  u8 type);
/* Check whether pointed-to IE looks like WPA. */
#define bcm_is_wpa_ie(ie, tlvs, len)	bcm_is_wfa_ie(ie, tlvs, len, WFA_OUI_TYPE_WPA)

#endif				/* _BCMWPA_H_ */
