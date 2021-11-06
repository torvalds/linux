/*
 * RadioTap utility routines for WL and Apps
 * This header file housing the define and function prototype use by
 * both the wl driver, tools & Apps.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _BCMWIFI_RADIOTAP_H_
#define _BCMWIFI_RADIOTAP_H_

#include <ieee80211_radiotap.h>
#include <siutils.h>
#include <monitor.h>
#include <802.11.h>
#include <802.11ax.h>
#include "bcmwifi_monitor.h"
#include <bcmwifi_rspec.h>
#include <bcmwifi_rates.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>
/*
 * RadioTap header specific implementation. Used by MacOS implementation only.
 */
BWL_PRE_PACKED_STRUCT struct wl_radiotap_hdr {
	struct ieee80211_radiotap_header ieee_radiotap;
	uint64 tsft;
	uint8 flags;
	union {
		uint8 rate;
		uint8 pad;
	} u;
	uint16 channel_freq;
	uint16 channel_flags;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct wl_radiotap_sna {
	uint8 signal;
	uint8 noise;
	uint8 antenna;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct wl_radiotap_xchan {
	uint32 xchannel_flags;
	uint16 xchannel_freq;
	uint8 xchannel_channel;
	uint8 xchannel_maxpower;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct wl_radiotap_ampdu {
	uint32 ref_num;
	uint16 flags;
	uint8 delimiter_crc;
	uint8 reserved;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct wl_htmcs {
	uint8 mcs_known;
	uint8 mcs_flags;
	uint8 mcs_index;
	uint8 pad;		/* pad to 32 bit aligned */
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct wl_vhtmcs {
	uint16 vht_known;	/* IEEE80211_RADIOTAP_VHT */
	uint8 vht_flags;
	uint8 vht_bw;
	uint8 vht_mcs_nss[4];
	uint8 vht_coding;
	uint8 vht_group_id;
	uint16 vht_partial_aid;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct wl_radiotap_ht_tail {
	struct wl_radiotap_xchan xc;
	struct wl_radiotap_ampdu ampdu;
	union {
		struct wl_htmcs ht;
		struct wl_vhtmcs vht;
	} u;
} BWL_POST_PACKED_STRUCT;

typedef struct bsd_header_rx {
	struct wl_radiotap_hdr hdr;
	/*
	 * include extra space beyond wl_radiotap_ht size
	 * (larger of two structs in union):
	 *   signal/noise/ant plus max of 3 pad for xchannel
	 *   tail struct (xchannel and MCS info)
	 */
	uint8 pad[3];
	uint8 ht[sizeof(struct wl_radiotap_ht_tail)];
} bsd_header_rx_t;

typedef struct radiotap_parse {
	struct ieee80211_radiotap_header *hdr;
	void *fields;
	uint fields_len;
	uint idx;
	uint offset;
} radiotap_parse_t;

struct rtap_field {
	uint len;
	uint align;
};

/* he radiotap - https://www.radiotap.org/fields/HE.html */
#define HE_RADIOTAP_BSS_COLOR_SHIFT		0u
#define HE_RADIOTAP_BEAM_CHANGE_SHIFT		6u
#define HE_RADIOTAP_DL_UL_SHIFT			7u
#define HE_RADIOTAP_MCS_SHIFT			8u
#define HE_RADIOTAP_DCM_SHIFT			12u
#define HE_RADIOTAP_CODING_SHIFT		13u
#define HE_RADIOTAP_LDPC_SHIFT			14u
#define HE_RADIOTAP_STBC_SHIFT			15u
#define HE_RADIOTAP_SR_SHIFT			0u
#define HE_RADIOTAP_STAID_SHIFT			4u
#define HE_RADIOTAP_SR1_SHIFT			0u
#define HE_RADIOTAP_SR2_SHIFT			4u
#define HE_RADIOTAP_SR3_SHIFT			8u
#define HE_RADIOTAP_SR4_SHIFT			12u
#define HE_RADIOTAP_BW_SHIFT			0u
#define HE_RADIOTAP_RU_ALLOC_SHIFT		0u
#define HE_RADIOTAP_GI_SHIFT			4u
#define HE_RADIOTAP_LTF_SIZE_SHIFT		6u
#define HE_RADIOTAP_NUM_LTF_SHIFT		8u
#define HE_RADIOTAP_PADDING_SHIFT		12u
#define HE_RADIOTAP_TXBF_SHIFT			14u
#define HE_RADIOTAP_PE_SHIFT			15u
#define HE_RADIOTAP_NSTS_SHIFT			0u
#define HE_RADIOTAP_DOPPLER_SHIFT		4u
#define HE_RADIOTAP_TXOP_SHIFT			8u
#define HE_RADIOTAP_MIDAMBLE_SHIFT		15u
#define HE_RADIOTAP_DOPPLER_SET_NSTS_SHIFT	0u
#define HE_RADIOTAP_DOPPLER_NOTSET_NSTS_SHIFT	0u

/* he mu radiotap - https://www.radiotap.org/fields/HE-MU.html */
#define HE_RADIOTAP_SIGB_MCS_SHIFT		0u
#define HE_RADIOTAP_SIGB_MCS_KNOWN_SHIFT	4u
#define HE_RADIOTAP_SIGB_DCM_SHIFT		5u
#define HE_RADIOTAP_SIGB_DCM_KNOWN_SHIFT	6u
#define HE_RADIOTAP_SIGB_COMP_KNOWN_SHIFT	14u
#define HE_RADIOTAP_SIGB_COMP_SHIFT		3u
#define HE_RADIOTAP_SIGB_SYMB_SHIFT		18u
#define HE_RADIOTAP_BW_SIGA_SHIFT		0u
#define HE_RADIOTAP_BW_SIGA_KNOWN_SHIFT		2u
#define HE_RADIOTAP_SIGB_SYM_MU_MIMO_USER_SHIFT	4u
#define HE_RADIOTAP_PRE_PUNCR_SIGA_SHIFT	8u
#define HE_RADIOTAP_PRE_PUNCR_SIGA_KNOWN_SHIFT	10u

#define WL_RADIOTAP_BRCM_SNS			0x01
#define WL_RADIOTAP_BRCM_MCS			0x00000001
#define WL_RADIOTAP_LEGACY_SNS			0x02
#define WL_RADIOTAP_LEGACY_VHT			0x00000001
#define WL_RADIOTAP_BRCM_PAD_SNS		0x3

#define IEEE80211_RADIOTAP_HTMOD_40		0x01
#define IEEE80211_RADIOTAP_HTMOD_SGI		0x02
#define IEEE80211_RADIOTAP_HTMOD_GF		0x04
#define IEEE80211_RADIOTAP_HTMOD_LDPC		0x08
#define IEEE80211_RADIOTAP_HTMOD_STBC_MASK	0x30
#define IEEE80211_RADIOTAP_HTMOD_STBC_SHIFT	4

/* Dyanmic bandwidth for VHT signaled in NONHT */
#define WL_RADIOTAP_F_NONHT_VHT_DYN_BW		0x01
/* VHT BW is valid in NONHT */
#define WL_RADIOTAP_F_NONHT_VHT_BW		0x02

typedef struct ieee80211_radiotap_header ieee80211_radiotap_header_t;

/* VHT information in non-HT frames; primarily VHT b/w signaling
 * in frames received at legacy rates.
 */
BWL_PRE_PACKED_STRUCT struct wl_radiotap_nonht_vht {
	uint8 len;	/* length of the field excluding 'len' field */
	uint8 flags;
	uint8 bw;
	uint8 PAD;	/* Add a pad so the next vendor entry, if any, will be 16 bit aligned */
} BWL_POST_PACKED_STRUCT;

typedef struct wl_radiotap_nonht_vht wl_radiotap_nonht_vht_t;

BWL_PRE_PACKED_STRUCT struct wl_radiotap_basic {
	uint32 tsft_l;
	uint32 tsft_h;
	uint8  flags;
	uint8  rate; /* this field acts as a pad for non legacy packets */
	uint16 channel_freq;
	uint16 channel_flags;
	uint8  signal;
	uint8  noise;
	int8   antenna;
} BWL_POST_PACKED_STRUCT;

typedef struct wl_radiotap_basic wl_radiotap_basic_t;

/* radiotap standard - non-HT, non-VHT information with Broadcom vendor namespace extension
 * that includes VHT information.
 * Used with monitor type 3 when received by HT/Legacy PHY and received rate is legacy.
 * Struct ieee80211_radiotap_header is of variable length due to possible
 * extra it_present bitmap fields.
 * It should not be included as a static length field here
 */
BWL_PRE_PACKED_STRUCT struct wl_radiotap_legacy {
	wl_radiotap_basic_t basic;
	uint8 PAD;
} BWL_POST_PACKED_STRUCT;

typedef struct wl_radiotap_legacy wl_radiotap_legacy_t;

#define WL_RADIOTAP_LEGACY_SKIP_LEN htol16(sizeof(struct wl_radiotap_legacy) - \
	OFFSETOF(struct wl_radiotap_legacy, nonht_vht))

#define WL_RADIOTAP_NONHT_VHT_LEN (sizeof(wl_radiotap_nonht_vht_t) - 1)

/* Radiotap standard that includes HT information. This is for use with monitor type 3
 * whenever frame is received by HT-PHY, and received rate is non-VHT.
 * Struct ieee80211_radiotap_header is of variable length due to possible
 * extra it_present bitmap fields.
 * It should not be included as a static length field here
 */
BWL_PRE_PACKED_STRUCT struct wl_radiotap_ht {
	wl_radiotap_basic_t basic;
	uint8  PAD[3];
	uint32 xchannel_flags;
	uint16 xchannel_freq;
	uint8  xchannel_channel;
	uint8  xchannel_maxpower;
	uint8  mcs_known;
	uint8  mcs_flags;
	uint8  mcs_index;
	uint8  PAD;
	uint32 ampdu_ref_num;		/* A-MPDU ID */
	uint16 ampdu_flags;		/* A-MPDU flags */
	uint8  ampdu_delim_crc;		/* Delimiter CRC if present in flags */
	uint8  ampdu_reserved;
} BWL_POST_PACKED_STRUCT;

typedef struct wl_radiotap_ht wl_radiotap_ht_t;

/* Radiotap standard that includes VHT information.
 * This is for use with monitor type 3 whenever frame is
 * received by HT-PHY (VHT-PHY), and received rate is VHT.
 * Struct ieee80211_radiotap_header is of variable length due to possible
 * extra it_present bitmap fields.
 * It should not be included as a static length field here
 */
BWL_PRE_PACKED_STRUCT struct wl_radiotap_vht {
	wl_radiotap_basic_t basic;
	uint8  PAD[3];
	uint32 ampdu_ref_num;		/* A-MPDU ID */
	uint16 ampdu_flags;		/* A-MPDU flags */
	uint8  ampdu_delim_crc;		/* Delimiter CRC if present in flags */
	uint8  ampdu_reserved;
	uint16 vht_known;		/* IEEE80211_RADIOTAP_VHT */
	uint8  vht_flags;		/* IEEE80211_RADIOTAP_VHT */
	uint8  vht_bw;			/* IEEE80211_RADIOTAP_VHT */
	uint8  vht_mcs_nss[4];		/* IEEE80211_RADIOTAP_VHT */
	uint8  vht_coding;		/* IEEE80211_RADIOTAP_VHT */
	uint8  vht_group_id;		/* IEEE80211_RADIOTAP_VHT */
	uint16 vht_partial_aid;		/* IEEE80211_RADIOTAP_VHT */
} BWL_POST_PACKED_STRUCT;

typedef struct wl_radiotap_vht wl_radiotap_vht_t;

/* Radiotap standard that includes HE information. */
BWL_PRE_PACKED_STRUCT struct wl_radiotap_he {
	wl_radiotap_basic_t basic;
	uint8  PAD[3];
	uint32 ampdu_ref_num;		/* A-MPDU ID */
	uint16 ampdu_flags;		/* A-MPDU flags */
	uint8  ampdu_delim_crc;		/* Delimiter CRC if present in flags */
	uint8  ampdu_reserved;
	uint16 data1;
	uint16 data2;
	uint16 data3;
	uint16 data4;
	uint16 data5;
	uint16 data6;
} BWL_POST_PACKED_STRUCT;

typedef struct wl_radiotap_he wl_radiotap_he_t;

BWL_PRE_PACKED_STRUCT struct radiotap_vendor_ns {
	uint8 vend_oui[3];
	uint8 sns;
	uint16 skip_len;
} BWL_POST_PACKED_STRUCT;

typedef struct radiotap_vendor_ns radiotap_vendor_ns_t;

#define WL_RADIOTAP_PRESENT_BASIC			\
	((1 << IEEE80211_RADIOTAP_TSFT) |		\
	 (1 << IEEE80211_RADIOTAP_FLAGS) |		\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |		\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL) |	\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE) |	\
	 (1 << IEEE80211_RADIOTAP_ANTENNA))

#define WL_RADIOTAP_PRESENT_LEGACY			\
	WL_RADIOTAP_PRESENT_BASIC |			\
	(1 << IEEE80211_RADIOTAP_RATE)

#define WL_RADIOTAP_PRESENT_HT				\
	WL_RADIOTAP_PRESENT_BASIC |			\
	((1 << IEEE80211_RADIOTAP_XCHANNEL) |		\
	 (1 << IEEE80211_RADIOTAP_MCS) |		\
	 (1 << IEEE80211_RADIOTAP_AMPDU))

#define WL_RADIOTAP_PRESENT_VHT			        \
	WL_RADIOTAP_PRESENT_BASIC |			\
	((1 << IEEE80211_RADIOTAP_AMPDU) |		\
	 (1 << IEEE80211_RADIOTAP_VHT))

#define WL_RADIOTAP_PRESENT_HE				\
	WL_RADIOTAP_PRESENT_BASIC |			\
	((1 << IEEE80211_RADIOTAP_AMPDU) |		\
	 (1 << IEEE80211_RADIOTAP_HE))

/* include/linux/if_arp.h
 *	#define ARPHRD_IEEE80211_PRISM 802 IEEE 802.11 + Prism2 header
 *	#define ARPHRD_IEEE80211_RADIOTAP 803 IEEE 802.11 + radiotap header
 * include/net/ieee80211_radiotap.h
 *	radiotap structure
 */

#ifndef ARPHRD_IEEE80211_RADIOTAP
#define ARPHRD_IEEE80211_RADIOTAP 803
#endif

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

extern void wl_rtapParseInit(radiotap_parse_t *rtap, uint8 *rtap_header);
extern ratespec_t wl_calcRspecFromRTap(uint8 *rtap_header);
extern bool wl_rtapFlags(uint8 *rtap_header, uint8* flags);
extern uint wl_radiotap_rx(struct dot11_header *mac_header, wl_rxsts_t *rxsts,
	bsd_header_rx_t *bsd_header);
extern uint wl_radiotap_rx_legacy(struct dot11_header *mac_header, wl_rxsts_t *rxsts,
	ieee80211_radiotap_header_t* rtap_hdr);
extern uint wl_radiotap_rx_ht(struct dot11_header *mac_header, wl_rxsts_t *rxsts,
	ieee80211_radiotap_header_t* rtap_hdr);
extern uint wl_radiotap_rx_vht(struct dot11_header *mac_header, wl_rxsts_t *rxsts,
        ieee80211_radiotap_header_t* rtap_hdr);
extern uint wl_radiotap_rx_he(struct dot11_header *mac_header, wl_rxsts_t *rxsts,
        ieee80211_radiotap_header_t* rtap_hdr);
extern uint wl_radiotap_rx_eht(struct dot11_header *mac_header, wl_rxsts_t *rxsts,
	ieee80211_radiotap_header_t *rtap_hdr);

/* Legacy phy radiotap header may include VHT bw signaling VS element */
#define MAX_RADIOTAP_LEGACY_SIZE (sizeof(wl_radiotap_legacy_t) + \
				  sizeof(radiotap_vendor_ns_t) + sizeof(wl_radiotap_nonht_vht_t))

/* RadioTap header starts with a fixed struct ieee80211_radiotap_header,
 * followed by variable fields for the 4 encodings supported, HE, VHT, HT, and Legacy
 */
#define MAX_RADIOTAP_SIZE	(sizeof(struct ieee80211_radiotap_header) +     \
				 MAX(sizeof(wl_radiotap_he_t),                  \
				     MAX(sizeof(wl_radiotap_vht_t),             \
				         MAX(sizeof(wl_radiotap_ht_t), MAX_RADIOTAP_LEGACY_SIZE))))
#define MAX_MON_PKT_SIZE	(4096 + MAX_RADIOTAP_SIZE)

#endif	/* _BCMWIFI_RADIOTAP_H_ */
