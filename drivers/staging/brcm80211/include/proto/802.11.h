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

#ifndef _802_11_H_
#define _802_11_H_

#define MCSSET_LEN	16
#define DOT11_BSSTYPE_ANY			2
#define DOT11_MAX_DEFAULT_KEYS	4
#define WPA_OUI			"\x00\x50\xF2"
#define BRCM_OUI		"\x00\x10\x18"
#define DOT11_MNG_RSN_ID			48
#define DOT11_MNG_WPA_ID			221
#define DOT11_DEFAULT_RTS_LEN		2347
#define DOT11_DEFAULT_FRAG_LEN		2346
#define DOT11_OUI_LEN			3

#define AC_COUNT		4

#define AC_BITMAP_TST(ab, ac)	(((ab) & (1 << (ac))) != 0)

typedef u8 ac_bitmap_t;

struct edcf_acparam {
	u8 ACI;
	u8 ECW;
	u16 TXOP;
} __attribute__((packed));
typedef struct edcf_acparam edcf_acparam_t;

struct wme_param_ie {
	u8 oui[3];
	u8 type;
	u8 subtype;
	u8 version;
	u8 qosinfo;
	u8 rsvd;
	edcf_acparam_t acparam[AC_COUNT];
} __attribute__((packed));
typedef struct wme_param_ie wme_param_ie_t;

#define DOT11_MAC_HDR_LEN		24
#define	DOT11_ACK_LEN		10
#define DOT11_ICV_AES_LEN		8
#define DOT11_A4_HDR_LEN		30
#define DOT11_QOS_LEN			2
#define DOT11_IV_MAX_LEN		8
#define DOT11_BA_LEN		4
#define DOT11_OFDM_SIGNAL_EXTENSION	6
#define DOT11_MIN_FRAG_LEN		256
#define	DOT11_RTS_LEN		16
#define	DOT11_CTS_LEN		10
#define DOT11_BA_BITMAP_LEN		128
#define DOT11_MIN_BEACON_PERIOD		1
#define DOT11_MAX_BEACON_PERIOD		0xFFFF
#define	DOT11_MAXNUMFRAGS	16
#define DOT11_MAX_FRAG_LEN		2346

#define SEQNUM_SHIFT		4
#define AMPDU_DELIMITER_LEN	4
#define SEQNUM_MAX		0x1000

#define APHY_SLOT_TIME		9
#define BPHY_SLOT_TIME		20
#define	APHY_CWMIN		15
#define PHY_CWMAX		1023

#define EDCF_AIFSN_MIN               1
#define BPHY_PLCP_TIME		192

#define APHY_SYMBOL_TIME	4
#define APHY_PREAMBLE_TIME	16
#define APHY_SIGNAL_TIME	4
#define APHY_SIFS_TIME		16
#define APHY_SERVICE_NBITS	16
#define APHY_TAIL_NBITS		6
#define BPHY_SIFS_TIME		10
#define BPHY_PLCP_SHORT_TIME	96

#define PREN_PREAMBLE		24
#define PREN_MM_EXT		12
#define PREN_PREAMBLE_EXT	4

#define FRAGNUM_MASK		0xF

#define RIFS_11N_TIME		2

#define HT_CAP_RX_STBC_NO		0x0

#define EDCF_ACI_MASK                0x60
#define EDCF_ACI_SHIFT               5
#define EDCF_ECWMIN_MASK             0x0f
#define EDCF_ECWMAX_SHIFT            4
#define EDCF_AIFSN_MASK              0x0f
#define EDCF_AIFSN_MAX               15
#define EDCF_ECWMAX_MASK             0xf0

#define EDCF_AC_BE_TXOP_STA          0x0000
#define EDCF_AC_BK_TXOP_STA          0x0000
#define EDCF_AC_VO_ACI_STA           0x62
#define EDCF_AC_VO_ECW_STA           0x32
#define EDCF_AC_VI_ACI_STA           0x42
#define EDCF_AC_VI_ECW_STA           0x43
#define EDCF_AC_BK_ECW_STA           0xA4
#define EDCF_AC_VI_TXOP_STA          0x005e
#define EDCF_AC_VO_TXOP_STA          0x002f
#define EDCF_AC_BE_ACI_STA           0x03
#define EDCF_AC_BE_ECW_STA           0xA4
#define EDCF_AC_BK_ACI_STA           0x27
#define EDCF_AC_VO_TXOP_AP           0x002f

#define EDCF_TXOP2USEC(txop)         ((txop) << 5)
#define EDCF_ECW2CW(exp)             ((1 << (exp)) - 1)

#define WME_VER			1
#define WME_SUBTYPE_PARAM_IE	1
#define WME_TYPE		2
#define WME_OUI			"\x00\x50\xf2"

#define AC_BE			0
#define AC_BK			1
#define AC_VI			2
#define AC_VO			3

#define HT_CAP_RX_STBC_ONE_STREAM	0x1

#endif				/* _802_11_H_ */
