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

#include <proto/wpa.h>
#include <packed_section_start.h>

#define DOT11_A3_HDR_LEN		24
#define DOT11_A4_HDR_LEN		30
#define DOT11_MAC_HDR_LEN		DOT11_A3_HDR_LEN
#define DOT11_FCS_LEN			4
#define DOT11_ICV_AES_LEN		8
#define DOT11_QOS_LEN			2

#define DOT11_IV_MAX_LEN		8

#define DOT11_MAX_SSID_LEN		32

#define DOT11_DEFAULT_RTS_LEN		2347

#define DOT11_MIN_FRAG_LEN		256
#define DOT11_MAX_FRAG_LEN		2346
#define DOT11_DEFAULT_FRAG_LEN		2346

#define DOT11_MIN_BEACON_PERIOD		1
#define DOT11_MAX_BEACON_PERIOD		0xFFFF

#define DOT11_MIN_DTIM_PERIOD		1
#define DOT11_MAX_DTIM_PERIOD		0xFF

#define DOT11_OUI_LEN			3

BWL_PRE_PACKED_STRUCT struct dot11_header {
	u16 fc;
	u16 durid;
	struct ether_addr a1;
	struct ether_addr a2;
	struct ether_addr a3;
	u16 seq;
	struct ether_addr a4;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_rts_frame {
	u16 fc;
	u16 durid;
	struct ether_addr ra;
	struct ether_addr ta;
} BWL_POST_PACKED_STRUCT;

#define	DOT11_RTS_LEN		16
#define	DOT11_CTS_LEN		10
#define	DOT11_ACK_LEN		10

#define DOT11_BA_BITMAP_LEN		128
#define DOT11_BA_LEN		4

BWL_PRE_PACKED_STRUCT struct dot11_management_header {
	u16 fc;
	u16 durid;
	struct ether_addr da;
	struct ether_addr sa;
	struct ether_addr bssid;
	u16 seq;
} BWL_POST_PACKED_STRUCT;
#define	DOT11_MGMT_HDR_LEN	24

BWL_PRE_PACKED_STRUCT struct dot11_bcn_prb {
	u32 timestamp[2];
	u16 beacon_interval;
	u16 capability;
} BWL_POST_PACKED_STRUCT;
#define	DOT11_BCN_PRB_LEN	12

#define WME_OUI			"\x00\x50\xf2"
#define WME_VER			1
#define WME_TYPE		2
#define WME_SUBTYPE_PARAM_IE	1

#define AC_BE			0
#define AC_BK			1
#define AC_VI			2
#define AC_VO			3
#define AC_COUNT		4

typedef u8 ac_bitmap_t;

#define AC_BITMAP_ALL		0xf
#define AC_BITMAP_TST(ab, ac)	(((ab) & (1 << (ac))) != 0)

BWL_PRE_PACKED_STRUCT struct edcf_acparam {
	u8 ACI;
	u8 ECW;
	u16 TXOP;
} BWL_POST_PACKED_STRUCT;
typedef struct edcf_acparam edcf_acparam_t;

BWL_PRE_PACKED_STRUCT struct wme_param_ie {
	u8 oui[3];
	u8 type;
	u8 subtype;
	u8 version;
	u8 qosinfo;
	u8 rsvd;
	edcf_acparam_t acparam[AC_COUNT];
} BWL_POST_PACKED_STRUCT;
typedef struct wme_param_ie wme_param_ie_t;
#define WME_PARAM_IE_LEN            24

#define EDCF_AIFSN_MIN               1
#define EDCF_AIFSN_MAX               15
#define EDCF_AIFSN_MASK              0x0f
#define EDCF_ACM_MASK                0x10
#define EDCF_ACI_MASK                0x60
#define EDCF_ACI_SHIFT               5

#define EDCF_ECW2CW(exp)             ((1 << (exp)) - 1)
#define EDCF_ECWMIN_MASK             0x0f
#define EDCF_ECWMAX_MASK             0xf0
#define EDCF_ECWMAX_SHIFT            4

#define EDCF_TXOP2USEC(txop)         ((txop) << 5)

#define EDCF_AC_BE_ACI_STA           0x03
#define EDCF_AC_BE_ECW_STA           0xA4
#define EDCF_AC_BE_TXOP_STA          0x0000
#define EDCF_AC_BK_ACI_STA           0x27
#define EDCF_AC_BK_ECW_STA           0xA4
#define EDCF_AC_BK_TXOP_STA          0x0000
#define EDCF_AC_VI_ACI_STA           0x42
#define EDCF_AC_VI_ECW_STA           0x43
#define EDCF_AC_VI_TXOP_STA          0x005e
#define EDCF_AC_VO_ACI_STA           0x62
#define EDCF_AC_VO_ECW_STA           0x32
#define EDCF_AC_VO_TXOP_STA          0x002f

#define EDCF_AC_VO_TXOP_AP           0x002f

#define DOT11_OPEN_SYSTEM	0
#define DOT11_SHARED_KEY	1

#define FC_TYPE_MASK		0xC
#define FC_TYPE_SHIFT		2
#define FC_SUBTYPE_MASK		0xF0
#define FC_SUBTYPE_SHIFT	4
#define FC_MOREFRAG		0x400

#define SEQNUM_SHIFT		4
#define SEQNUM_MAX		0x1000
#define FRAGNUM_MASK		0xF

#define FC_TYPE_MNG		0
#define FC_TYPE_CTL		1
#define FC_TYPE_DATA		2

#define FC_SUBTYPE_PROBE_REQ		4
#define FC_SUBTYPE_PROBE_RESP		5
#define FC_SUBTYPE_BEACON		8
#define FC_SUBTYPE_PS_POLL		10
#define FC_SUBTYPE_RTS			11
#define FC_SUBTYPE_CTS			12

#define FC_SUBTYPE_ANY_QOS(s)		(((s) & 8) != 0)

#define FC_KIND_MASK		(FC_TYPE_MASK | FC_SUBTYPE_MASK)

#define FC_KIND(t, s)	(((t) << FC_TYPE_SHIFT) | ((s) << FC_SUBTYPE_SHIFT))

#define FC_SUBTYPE(fc)	(((fc) & FC_SUBTYPE_MASK) >> FC_SUBTYPE_SHIFT)
#define FC_TYPE(fc)	(((fc) & FC_TYPE_MASK) >> FC_TYPE_SHIFT)

#define FC_PROBE_REQ	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_PROBE_REQ)
#define FC_PROBE_RESP	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_PROBE_RESP)
#define FC_BEACON	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_BEACON)
#define FC_PS_POLL	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_PS_POLL)
#define FC_RTS		FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_RTS)
#define FC_CTS		FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CTS)

#define TLV_LEN_OFF		1
#define TLV_HDR_LEN		2
#define TLV_BODY_OFF		2

#define DOT11_MNG_RSN_ID			48
#define DOT11_MNG_WPA_ID			221
#define DOT11_MNG_VS_ID				221

#define DOT11_CAP_ESS				0x0001
#define DOT11_CAP_IBSS				0x0002
#define DOT11_CAP_PRIVACY			0x0010
#define DOT11_CAP_SHORT				0x0020
#define DOT11_CAP_SHORTSLOT			0x0400

#define DOT11_BSSTYPE_INFRASTRUCTURE		0
#define DOT11_BSSTYPE_ANY			2
#define DOT11_SCANTYPE_ACTIVE			0

#define PREN_PREAMBLE		24
#define PREN_MM_EXT		12
#define PREN_PREAMBLE_EXT	4

#define RIFS_11N_TIME		2

#define APHY_SLOT_TIME		9
#define APHY_SIFS_TIME		16
#define APHY_PREAMBLE_TIME	16
#define APHY_SIGNAL_TIME	4
#define APHY_SYMBOL_TIME	4
#define APHY_SERVICE_NBITS	16
#define APHY_TAIL_NBITS		6
#define	APHY_CWMIN		15

#define BPHY_SLOT_TIME		20
#define BPHY_SIFS_TIME		10
#define BPHY_PLCP_TIME		192
#define BPHY_PLCP_SHORT_TIME	96

#define DOT11_OFDM_SIGNAL_EXTENSION	6

#define PHY_CWMAX		1023

#define	DOT11_MAXNUMFRAGS	16

typedef struct d11cnt {
	u32 txfrag;
	u32 txmulti;
	u32 txfail;
	u32 txretry;
	u32 txretrie;
	u32 rxdup;
	u32 txrts;
	u32 txnocts;
	u32 txnoack;
	u32 rxfrag;
	u32 rxmulti;
	u32 rxcrc;
	u32 txfrmsnt;
	u32 rxundec;
} d11cnt_t;

#define MCSSET_LEN	16

BWL_PRE_PACKED_STRUCT struct ht_cap_ie {
	u16 cap;
	u8 params;
	u8 supp_mcs[MCSSET_LEN];
	u16 ext_htcap;
	u32 txbf_cap;
	u8 as_cap;
} BWL_POST_PACKED_STRUCT;
typedef struct ht_cap_ie ht_cap_ie_t;

#define HT_CAP_IE_LEN		26

#define HT_CAP_LDPC_CODING	0x0001
#define HT_CAP_40MHZ		0x0002
#define HT_CAP_MIMO_PS_MASK	0x000C
#define HT_CAP_MIMO_PS_SHIFT	0x0002
#define HT_CAP_MIMO_PS_OFF	0x0003
#define HT_CAP_MIMO_PS_ON	0x0000
#define HT_CAP_GF		0x0010
#define HT_CAP_SHORT_GI_20	0x0020
#define HT_CAP_SHORT_GI_40	0x0040
#define HT_CAP_TX_STBC		0x0080
#define HT_CAP_RX_STBC_MASK	0x0300
#define HT_CAP_RX_STBC_SHIFT	8
#define HT_CAP_MAX_AMSDU	0x0800
#define HT_CAP_DSSS_CCK	0x1000
#define HT_CAP_40MHZ_INTOLERANT 0x4000

#define HT_CAP_RX_STBC_NO		0x0
#define HT_CAP_RX_STBC_ONE_STREAM	0x1

#define HT_PARAMS_RX_FACTOR_MASK	0x03

#define AMPDU_MAX_MPDU_DENSITY	7
#define AMPDU_RX_FACTOR_16K	1
#define AMPDU_RX_FACTOR_32K	2
#define AMPDU_RX_FACTOR_64K	3

#define AMPDU_DELIMITER_LEN	4

#define DOT11N_TXBURST		0x0008

#define WPA_VERSION		1
#define WPA_OUI			"\x00\x50\xF2"

#define WFA_OUI			"\x00\x50\xF2"
#define WFA_OUI_LEN	3

#define WFA_OUI_TYPE_WPA	1

#define RSN_AKM_NONE		0
#define RSN_AKM_UNSPECIFIED	1
#define RSN_AKM_PSK		2

#define DOT11_MAX_DEFAULT_KEYS	4
#define DOT11_MAX_KEY_SIZE	32
#define DOT11_WPA_KEY_RSC_LEN   8

#define WEP1_KEY_SIZE		5
#define WEP128_KEY_SIZE		13
#define TKIP_KEY_SIZE		32
#define AES_KEY_SIZE		16

#define BRCM_OUI		"\x00\x10\x18"
#include <packed_section_end.h>

#endif				/* _802_11_H_ */
