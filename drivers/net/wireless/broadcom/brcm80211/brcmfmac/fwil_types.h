// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2012 Broadcom Corporation
 */


#ifndef FWIL_TYPES_H_
#define FWIL_TYPES_H_

#include <linux/if_ether.h>


#define BRCMF_FIL_ACTION_FRAME_SIZE	1800

/* ARP Offload feature flags for arp_ol iovar */
#define BRCMF_ARP_OL_AGENT		0x00000001
#define BRCMF_ARP_OL_SNOOP		0x00000002
#define BRCMF_ARP_OL_HOST_AUTO_REPLY	0x00000004
#define BRCMF_ARP_OL_PEER_AUTO_REPLY	0x00000008

#define	BRCMF_BSS_INFO_VERSION	109 /* curr ver of brcmf_bss_info_le struct */
#define BRCMF_BSS_RSSI_ON_CHANNEL	0x0004

#define BRCMF_STA_BRCM			0x00000001	/* Running a Broadcom driver */
#define BRCMF_STA_WME			0x00000002	/* WMM association */
#define BRCMF_STA_NONERP		0x00000004	/* No ERP */
#define BRCMF_STA_AUTHE			0x00000008	/* Authenticated */
#define BRCMF_STA_ASSOC			0x00000010	/* Associated */
#define BRCMF_STA_AUTHO			0x00000020	/* Authorized */
#define BRCMF_STA_WDS			0x00000040	/* Wireless Distribution System */
#define BRCMF_STA_WDS_LINKUP		0x00000080	/* WDS traffic/probes flowing properly */
#define BRCMF_STA_PS			0x00000100	/* STA is in power save mode from AP's viewpoint */
#define BRCMF_STA_APSD_BE		0x00000200	/* APSD delv/trigger for AC_BE is default enabled */
#define BRCMF_STA_APSD_BK		0x00000400	/* APSD delv/trigger for AC_BK is default enabled */
#define BRCMF_STA_APSD_VI		0x00000800	/* APSD delv/trigger for AC_VI is default enabled */
#define BRCMF_STA_APSD_VO		0x00001000	/* APSD delv/trigger for AC_VO is default enabled */
#define BRCMF_STA_N_CAP			0x00002000	/* STA 802.11n capable */
#define BRCMF_STA_SCBSTATS		0x00004000	/* Per STA debug stats */
#define BRCMF_STA_AMPDU_CAP		0x00008000	/* STA AMPDU capable */
#define BRCMF_STA_AMSDU_CAP		0x00010000	/* STA AMSDU capable */
#define BRCMF_STA_MIMO_PS		0x00020000	/* mimo ps mode is enabled */
#define BRCMF_STA_MIMO_RTS		0x00040000	/* send rts in mimo ps mode */
#define BRCMF_STA_RIFS_CAP		0x00080000	/* rifs enabled */
#define BRCMF_STA_VHT_CAP		0x00100000	/* STA VHT(11ac) capable */
#define BRCMF_STA_WPS			0x00200000	/* WPS state */
#define BRCMF_STA_DWDS_CAP		0x01000000	/* DWDS CAP */
#define BRCMF_STA_DWDS			0x02000000	/* DWDS active */

/* size of brcmf_scan_params not including variable length array */
#define BRCMF_SCAN_PARAMS_FIXED_SIZE	64
#define BRCMF_SCAN_PARAMS_V2_FIXED_SIZE	72

/* version of brcmf_scan_params structure */
#define BRCMF_SCAN_PARAMS_VERSION_V2	2

/* masks for channel and ssid count */
#define BRCMF_SCAN_PARAMS_COUNT_MASK	0x0000ffff
#define BRCMF_SCAN_PARAMS_NSSID_SHIFT	16

/* scan type definitions */
#define BRCMF_SCANTYPE_DEFAULT		0xFF
#define BRCMF_SCANTYPE_ACTIVE		0
#define BRCMF_SCANTYPE_PASSIVE		1

#define BRCMF_WSEC_MAX_PSK_LEN		32
#define	BRCMF_WSEC_PASSPHRASE		BIT(0)

#define BRCMF_WSEC_MAX_SAE_PASSWORD_LEN 128

/* primary (ie tx) key */
#define BRCMF_PRIMARY_KEY		(1 << 1)
#define DOT11_BSSTYPE_ANY		2
#define BRCMF_ESCAN_REQ_VERSION		1
#define BRCMF_ESCAN_REQ_VERSION_V2	2

#define BRCMF_MAXRATES_IN_SET		16	/* max # of rates in rateset */

/* OBSS Coex Auto/On/Off */
#define BRCMF_OBSS_COEX_AUTO		(-1)
#define BRCMF_OBSS_COEX_OFF		0
#define BRCMF_OBSS_COEX_ON		1

/* WOWL bits */
/* Wakeup on Magic packet: */
#define BRCMF_WOWL_MAGIC		(1 << 0)
/* Wakeup on Netpattern */
#define BRCMF_WOWL_NET			(1 << 1)
/* Wakeup on loss-of-link due to Disassoc/Deauth: */
#define BRCMF_WOWL_DIS			(1 << 2)
/* Wakeup on retrograde TSF: */
#define BRCMF_WOWL_RETR			(1 << 3)
/* Wakeup on loss of beacon: */
#define BRCMF_WOWL_BCN			(1 << 4)
/* Wakeup after test: */
#define BRCMF_WOWL_TST			(1 << 5)
/* Wakeup after PTK refresh: */
#define BRCMF_WOWL_M1			(1 << 6)
/* Wakeup after receipt of EAP-Identity Req: */
#define BRCMF_WOWL_EAPID		(1 << 7)
/* Wakeind via PME(0) or GPIO(1): */
#define BRCMF_WOWL_PME_GPIO		(1 << 8)
/* need tkip phase 1 key to be updated by the driver: */
#define BRCMF_WOWL_NEEDTKIP1		(1 << 9)
/* enable wakeup if GTK fails: */
#define BRCMF_WOWL_GTK_FAILURE		(1 << 10)
/* support extended magic packets: */
#define BRCMF_WOWL_EXTMAGPAT		(1 << 11)
/* support ARP/NS/keepalive offloading: */
#define BRCMF_WOWL_ARPOFFLOAD		(1 << 12)
/* read protocol version for EAPOL frames: */
#define BRCMF_WOWL_WPA2			(1 << 13)
/* If the bit is set, use key rotaton: */
#define BRCMF_WOWL_KEYROT		(1 << 14)
/* If the bit is set, frm received was bcast frame: */
#define BRCMF_WOWL_BCAST		(1 << 15)
/* If the bit is set, scan offload is enabled: */
#define BRCMF_WOWL_SCANOL		(1 << 16)
/* Wakeup on tcpkeep alive timeout: */
#define BRCMF_WOWL_TCPKEEP_TIME		(1 << 17)
/* Wakeup on mDNS Conflict Resolution: */
#define BRCMF_WOWL_MDNS_CONFLICT	(1 << 18)
/* Wakeup on mDNS Service Connect: */
#define BRCMF_WOWL_MDNS_SERVICE		(1 << 19)
/* tcp keepalive got data: */
#define BRCMF_WOWL_TCPKEEP_DATA		(1 << 20)
/* Firmware died in wowl mode: */
#define BRCMF_WOWL_FW_HALT		(1 << 21)
/* Enable detection of radio button changes: */
#define BRCMF_WOWL_ENAB_HWRADIO		(1 << 22)
/* Offloads detected MIC failure(s): */
#define BRCMF_WOWL_MIC_FAIL		(1 << 23)
/* Wakeup in Unassociated state (Net/Magic Pattern): */
#define BRCMF_WOWL_UNASSOC		(1 << 24)
/* Wakeup if received matched secured pattern: */
#define BRCMF_WOWL_SECURE		(1 << 25)
/* Wakeup on finding preferred network */
#define BRCMF_WOWL_PFN_FOUND		(1 << 27)
/* Wakeup on receiving pairwise key EAP packets: */
#define WIPHY_WOWL_EAP_PK		(1 << 28)
/* Link Down indication in WoWL mode: */
#define BRCMF_WOWL_LINKDOWN		(1 << 31)

#define BRCMF_WOWL_MAXPATTERNS		16
#define BRCMF_WOWL_MAXPATTERNSIZE	128

#define BRCMF_COUNTRY_BUF_SZ		4
#define BRCMF_ANT_MAX			4

#define BRCMF_MAX_ASSOCLIST		128

#define BRCMF_TXBF_SU_BFE_CAP		BIT(0)
#define BRCMF_TXBF_MU_BFE_CAP		BIT(1)
#define BRCMF_TXBF_SU_BFR_CAP		BIT(0)
#define BRCMF_TXBF_MU_BFR_CAP		BIT(1)

#define	BRCMF_MAXPMKID			16	/* max # PMKID cache entries */
#define BRCMF_NUMCHANNELS		64

#define BRCMF_PFN_MACADDR_CFG_VER	1
#define BRCMF_PFN_MAC_OUI_ONLY		BIT(0)
#define BRCMF_PFN_SET_MAC_UNASSOC	BIT(1)

#define BRCMF_MCSSET_LEN		16

#define BRCMF_RSN_KCK_LENGTH		16
#define BRCMF_RSN_KEK_LENGTH		16
#define BRCMF_RSN_REPLAY_LEN		8

#define BRCMF_MFP_NONE			0
#define BRCMF_MFP_CAPABLE		1
#define BRCMF_MFP_REQUIRED		2

#define BRCMF_VHT_CAP_MCS_MAP_NSS_MAX	8

#define BRCMF_HE_CAP_MCS_MAP_NSS_MAX	8

#define BRCMF_PMKSA_VER_2		2
#define BRCMF_PMKSA_VER_3		3
#define BRCMF_PMKSA_NO_EXPIRY		0xffffffff

/* MAX_CHUNK_LEN is the maximum length for data passing to firmware in each
 * ioctl. It is relatively small because firmware has small maximum size input
 * playload restriction for ioctls.
 */
#define MAX_CHUNK_LEN			1400

#define DLOAD_HANDLER_VER		1	/* Downloader version */
#define DLOAD_FLAG_VER_MASK		0xf000	/* Downloader version mask */
#define DLOAD_FLAG_VER_SHIFT		12	/* Downloader version shift */

#define DL_BEGIN			0x0002
#define DL_END				0x0004

#define DL_TYPE_CLM			2

/* join preference types for join_pref iovar */
enum brcmf_join_pref_types {
	BRCMF_JOIN_PREF_RSSI = 1,
	BRCMF_JOIN_PREF_WPA,
	BRCMF_JOIN_PREF_BAND,
	BRCMF_JOIN_PREF_RSSI_DELTA,
};

enum brcmf_fil_p2p_if_types {
	BRCMF_FIL_P2P_IF_CLIENT,
	BRCMF_FIL_P2P_IF_GO,
	BRCMF_FIL_P2P_IF_DYNBCN_GO,
	BRCMF_FIL_P2P_IF_DEV,
};

enum brcmf_wowl_pattern_type {
	BRCMF_WOWL_PATTERN_TYPE_BITMAP = 0,
	BRCMF_WOWL_PATTERN_TYPE_ARP,
	BRCMF_WOWL_PATTERN_TYPE_NA
};

struct brcmf_fil_p2p_if_le {
	u8 addr[ETH_ALEN];
	__le16 type;
	__le16 chspec;
};

struct brcmf_fil_chan_info_le {
	__le32 hw_channel;
	__le32 target_channel;
	__le32 scan_channel;
};

struct brcmf_fil_action_frame_le {
	u8	da[ETH_ALEN];
	__le16	len;
	__le32	packet_id;
	u8	data[BRCMF_FIL_ACTION_FRAME_SIZE];
};

struct brcmf_fil_af_params_le {
	__le32					channel;
	__le32					dwell_time;
	u8					bssid[ETH_ALEN];
	u8					pad[2];
	struct brcmf_fil_action_frame_le	action_frame;
};

struct brcmf_fil_bss_enable_le {
	__le32 bsscfgidx;
	__le32 enable;
};

struct brcmf_fil_bwcap_le {
	__le32 band;
	__le32 bw_cap;
};

/**
 * struct tdls_iovar - common structure for tdls iovars.
 *
 * @ea: ether address of peer station.
 * @mode: mode value depending on specific tdls iovar.
 * @chanspec: channel specification.
 * @pad: unused (for future use).
 */
struct brcmf_tdls_iovar_le {
	u8 ea[ETH_ALEN];		/* Station address */
	u8 mode;			/* mode: depends on iovar */
	__le16 chanspec;
	__le32 pad;			/* future */
};

enum brcmf_tdls_manual_ep_ops {
	BRCMF_TDLS_MANUAL_EP_CREATE = 1,
	BRCMF_TDLS_MANUAL_EP_DELETE = 3,
	BRCMF_TDLS_MANUAL_EP_DISCOVERY = 6
};

/* Pattern matching filter. Specifies an offset within received packets to
 * start matching, the pattern to match, the size of the pattern, and a bitmask
 * that indicates which bits within the pattern should be matched.
 */
struct brcmf_pkt_filter_pattern_le {
	/*
	 * Offset within received packet to start pattern matching.
	 * Offset '0' is the first byte of the ethernet header.
	 */
	__le32 offset;
	/* Size of the pattern.  Bitmask must be the same size.*/
	__le32 size_bytes;
	/*
	 * Variable length mask and pattern data. mask starts at offset 0.
	 * Pattern immediately follows mask.
	 */
	u8 mask_and_pattern[1];
};

/* IOVAR "pkt_filter_add" parameter. Used to install packet filters. */
struct brcmf_pkt_filter_le {
	__le32 id;		/* Unique filter id, specified by app. */
	__le32 type;		/* Filter type (WL_PKT_FILTER_TYPE_xxx). */
	__le32 negate_match;	/* Negate the result of filter matches */
	union {			/* Filter definitions */
		struct brcmf_pkt_filter_pattern_le pattern; /* Filter pattern */
	} u;
};

/* IOVAR "pkt_filter_enable" parameter. */
struct brcmf_pkt_filter_enable_le {
	__le32 id;		/* Unique filter id */
	__le32 enable;		/* Enable/disable bool */
};

/* BSS info structure
 * Applications MUST CHECK ie_offset field and length field to access IEs and
 * next bss_info structure in a vector (in struct brcmf_scan_results)
 */
struct brcmf_bss_info_le {
	__le32 version;		/* version field */
	__le32 length;		/* byte length of data in this record,
				 * starting at version and including IEs
				 */
	u8 BSSID[ETH_ALEN];
	__le16 beacon_period;	/* units are Kusec */
	__le16 capability;	/* Capability information */
	u8 SSID_len;
	u8 SSID[32];
	struct {
		__le32 count;   /* # rates in this set */
		u8 rates[16]; /* rates in 500kbps units w/hi bit set if basic */
	} rateset;		/* supported rates */
	__le16 chanspec;	/* chanspec for bss */
	__le16 atim_window;	/* units are Kusec */
	u8 dtim_period;	/* DTIM period */
	__le16 RSSI;		/* receive signal strength (in dBm) */
	s8 phy_noise;		/* noise (in dBm) */

	u8 n_cap;		/* BSS is 802.11N Capable */
	/* 802.11N BSS Capabilities (based on HT_CAP_*): */
	__le32 nbss_cap;
	u8 ctl_ch;		/* 802.11N BSS control channel number */
	__le32 reserved32[1];	/* Reserved for expansion of BSS properties */
	u8 flags;		/* flags */
	u8 reserved[3];	/* Reserved for expansion of BSS properties */
	u8 basic_mcs[BRCMF_MCSSET_LEN];	/* 802.11N BSS required MCS set */

	__le16 ie_offset;	/* offset at which IEs start, from beginning */
	__le32 ie_length;	/* byte length of Information Elements */
	__le16 SNR;		/* average SNR of during frame reception */
	/* Add new fields here */
	/* variable length Information Elements */
};

struct brcm_rateset_le {
	/* # rates in this set */
	__le32 count;
	/* rates in 500kbps units w/hi bit set if basic */
	u8 rates[BRCMF_MAXRATES_IN_SET];
};

struct brcmf_ssid_le {
	__le32 SSID_len;
	unsigned char SSID[IEEE80211_MAX_SSID_LEN];
};

/* Alternate SSID structure used in some places... */
struct brcmf_ssid8_le {
	u8 SSID_len;
	unsigned char SSID[IEEE80211_MAX_SSID_LEN];
};

struct brcmf_scan_params_le {
	struct brcmf_ssid_le ssid_le;	/* default: {0, ""} */
	u8 bssid[ETH_ALEN];	/* default: bcast */
	s8 bss_type;		/* default: any,
				 * DOT11_BSSTYPE_ANY/INFRASTRUCTURE/INDEPENDENT
				 */
	u8 scan_type;	/* flags, 0 use default */
	__le32 nprobes;	  /* -1 use default, number of probes per channel */
	__le32 active_time;	/* -1 use default, dwell time per channel for
				 * active scanning
				 */
	__le32 passive_time;	/* -1 use default, dwell time per channel
				 * for passive scanning
				 */
	__le32 home_time;	/* -1 use default, dwell time for the
				 * home channel between channel scans
				 */
	__le32 channel_num;	/* count of channels and ssids that follow
				 *
				 * low half is count of channels in
				 * channel_list, 0 means default (use all
				 * available channels)
				 *
				 * high half is entries in struct brcmf_ssid
				 * array that follows channel_list, aligned for
				 * s32 (4 bytes) meaning an odd channel count
				 * implies a 2-byte pad between end of
				 * channel_list and first ssid
				 *
				 * if ssid count is zero, single ssid in the
				 * fixed parameter portion is assumed, otherwise
				 * ssid in the fixed portion is ignored
				 */
	union {
		__le16 padding;	/* Reserve space for at least 1 entry for abort
				 * which uses an on stack brcmf_scan_params_le
				 */
		DECLARE_FLEX_ARRAY(__le16, channel_list);	/* chanspecs */
	};
};

struct brcmf_scan_params_v2_le {
	__le16 version;		/* structure version */
	__le16 length;		/* structure length */
	struct brcmf_ssid_le ssid_le;	/* default: {0, ""} */
	u8 bssid[ETH_ALEN];	/* default: bcast */
	s8 bss_type;		/* default: any,
				 * DOT11_BSSTYPE_ANY/INFRASTRUCTURE/INDEPENDENT
				 */
	u8 pad;
	__le32 scan_type;	/* flags, 0 use default */
	__le32 nprobes;		/* -1 use default, number of probes per channel */
	__le32 active_time;	/* -1 use default, dwell time per channel for
				 * active scanning
				 */
	__le32 passive_time;	/* -1 use default, dwell time per channel
				 * for passive scanning
				 */
	__le32 home_time;	/* -1 use default, dwell time for the
				 * home channel between channel scans
				 */
	__le32 channel_num;	/* count of channels and ssids that follow
				 *
				 * low half is count of channels in
				 * channel_list, 0 means default (use all
				 * available channels)
				 *
				 * high half is entries in struct brcmf_ssid
				 * array that follows channel_list, aligned for
				 * s32 (4 bytes) meaning an odd channel count
				 * implies a 2-byte pad between end of
				 * channel_list and first ssid
				 *
				 * if ssid count is zero, single ssid in the
				 * fixed parameter portion is assumed, otherwise
				 * ssid in the fixed portion is ignored
				 */
	union {
		__le16 padding;	/* Reserve space for at least 1 entry for abort
				 * which uses an on stack brcmf_scan_params_v2_le
				 */
		DECLARE_FLEX_ARRAY(__le16, channel_list);	/* chanspecs */
	};
};

struct brcmf_scan_results {
	u32 buflen;
	u32 version;
	u32 count;
	struct brcmf_bss_info_le bss_info_le[];
};

struct brcmf_escan_params_le {
	__le32 version;
	__le16 action;
	__le16 sync_id;
	union {
		struct brcmf_scan_params_le params_le;
		struct brcmf_scan_params_v2_le params_v2_le;
	};
};

struct brcmf_escan_result_le {
	__le32 buflen;
	__le32 version;
	__le16 sync_id;
	__le16 bss_count;
	struct brcmf_bss_info_le bss_info_le;
};

#define WL_ESCAN_RESULTS_FIXED_SIZE (sizeof(struct brcmf_escan_result_le) - \
	sizeof(struct brcmf_bss_info_le))

/* used for association with a specific BSSID and chanspec list */
struct brcmf_assoc_params_le {
	/* 00:00:00:00:00:00: broadcast scan */
	u8 bssid[ETH_ALEN];
	/* 0: all available channels, otherwise count of chanspecs in
	 * chanspec_list */
	__le32 chanspec_num;
	/* list of chanspecs */
	__le16 chanspec_list[1];
};

/**
 * struct join_pref params - parameters for preferred join selection.
 *
 * @type: preference type (see enum brcmf_join_pref_types).
 * @len: length of bytes following (currently always 2).
 * @rssi_gain: signal gain for selection (only when @type is RSSI_DELTA).
 * @band: band to which selection preference applies.
 *	This is used if @type is BAND or RSSI_DELTA.
 */
struct brcmf_join_pref_params {
	u8 type;
	u8 len;
	u8 rssi_gain;
	u8 band;
};

/* used for join with or without a specific bssid and channel list */
struct brcmf_join_params {
	struct brcmf_ssid_le ssid_le;
	struct brcmf_assoc_params_le params_le;
};

/* scan params for extended join */
struct brcmf_join_scan_params_le {
	u8 scan_type;		/* 0 use default, active or passive scan */
	__le32 nprobes;		/* -1 use default, nr of probes per channel */
	__le32 active_time;	/* -1 use default, dwell time per channel for
				 * active scanning
				 */
	__le32 passive_time;	/* -1 use default, dwell time per channel
				 * for passive scanning
				 */
	__le32 home_time;	/* -1 use default, dwell time for the home
				 * channel between channel scans
				 */
};

/* extended join params */
struct brcmf_ext_join_params_le {
	struct brcmf_ssid_le ssid_le;	/* {0, ""}: wildcard scan */
	struct brcmf_join_scan_params_le scan_le;
	struct brcmf_assoc_params_le assoc_le;
};

struct brcmf_wsec_key {
	u32 index;		/* key index */
	u32 len;		/* key length */
	u8 data[WLAN_MAX_KEY_LEN];	/* key data */
	u32 pad_1[18];
	u32 algo;	/* CRYPTO_ALGO_AES_CCM, CRYPTO_ALGO_WEP128, etc */
	u32 flags;	/* misc flags */
	u32 pad_2[3];
	u32 iv_initialized;	/* has IV been initialized already? */
	u32 pad_3;
	/* Rx IV */
	struct {
		u32 hi;	/* upper 32 bits of IV */
		u16 lo;	/* lower 16 bits of IV */
	} rxiv;
	u32 pad_4[2];
	u8 ea[ETH_ALEN];	/* per station */
};

/*
 * dongle requires same struct as above but with fields in little endian order
 */
struct brcmf_wsec_key_le {
	__le32 index;		/* key index */
	__le32 len;		/* key length */
	u8 data[WLAN_MAX_KEY_LEN];	/* key data */
	__le32 pad_1[18];
	__le32 algo;	/* CRYPTO_ALGO_AES_CCM, CRYPTO_ALGO_WEP128, etc */
	__le32 flags;	/* misc flags */
	__le32 pad_2[3];
	__le32 iv_initialized;	/* has IV been initialized already? */
	__le32 pad_3;
	/* Rx IV */
	struct {
		__le32 hi;	/* upper 32 bits of IV */
		__le16 lo;	/* lower 16 bits of IV */
	} rxiv;
	__le32 pad_4[2];
	u8 ea[ETH_ALEN];	/* per station */
};

/**
 * struct brcmf_wsec_pmk_le - firmware pmk material.
 *
 * @key_len: number of octets in key material.
 * @flags: key handling qualifiers.
 * @key: PMK key material.
 */
struct brcmf_wsec_pmk_le {
	__le16  key_len;
	__le16  flags;
	u8 key[2 * BRCMF_WSEC_MAX_PSK_LEN + 1];
};

/**
 * struct brcmf_wsec_sae_pwd_le - firmware SAE password material.
 *
 * @key_len: number of octets in key materials.
 * @key: SAE password material.
 */
struct brcmf_wsec_sae_pwd_le {
	__le16 key_len;
	u8 key[BRCMF_WSEC_MAX_SAE_PASSWORD_LEN];
};

/* Used to get specific STA parameters */
struct brcmf_scb_val_le {
	__le32 val;
	u8 ea[ETH_ALEN];
};

/* channel encoding */
struct brcmf_channel_info_le {
	__le32 hw_channel;
	__le32 target_channel;
	__le32 scan_channel;
};

struct brcmf_sta_info_le {
	__le16 ver;		/* version of this struct */
	__le16 len;		/* length in bytes of this structure */
	__le16 cap;		/* sta's advertised capabilities */
	__le32 flags;		/* flags defined below */
	__le32 idle;		/* time since data pkt rx'd from sta */
	u8 ea[ETH_ALEN];		/* Station address */
	__le32 count;			/* # rates in this set */
	u8 rates[BRCMF_MAXRATES_IN_SET];	/* rates in 500kbps units */
						/* w/hi bit set if basic */
	__le32 in;		/* seconds elapsed since associated */
	__le32 listen_interval_inms; /* Min Listen interval in ms for STA */

	/* Fields valid for ver >= 3 */
	__le32 tx_pkts;	/* # of packets transmitted */
	__le32 tx_failures;	/* # of packets failed */
	__le32 rx_ucast_pkts;	/* # of unicast packets received */
	__le32 rx_mcast_pkts;	/* # of multicast packets received */
	__le32 tx_rate;	/* Rate of last successful tx frame */
	__le32 rx_rate;	/* Rate of last successful rx frame */
	__le32 rx_decrypt_succeeds;	/* # of packet decrypted successfully */
	__le32 rx_decrypt_failures;	/* # of packet decrypted failed */

	/* Fields valid for ver >= 4 */
	__le32 tx_tot_pkts;    /* # of tx pkts (ucast + mcast) */
	__le32 rx_tot_pkts;    /* # of data packets recvd (uni + mcast) */
	__le32 tx_mcast_pkts;  /* # of mcast pkts txed */
	__le64 tx_tot_bytes;   /* data bytes txed (ucast + mcast) */
	__le64 rx_tot_bytes;   /* data bytes recvd (ucast + mcast) */
	__le64 tx_ucast_bytes; /* data bytes txed (ucast) */
	__le64 tx_mcast_bytes; /* # data bytes txed (mcast) */
	__le64 rx_ucast_bytes; /* data bytes recvd (ucast) */
	__le64 rx_mcast_bytes; /* data bytes recvd (mcast) */
	s8 rssi[BRCMF_ANT_MAX];   /* per antenna rssi */
	s8 nf[BRCMF_ANT_MAX];     /* per antenna noise floor */
	__le16 aid;                    /* association ID */
	__le16 ht_capabilities;        /* advertised ht caps */
	__le16 vht_flags;              /* converted vht flags */
	__le32 tx_pkts_retry_cnt;      /* # of frames where a retry was
					 * exhausted.
					 */
	__le32 tx_pkts_retry_exhausted; /* # of user frames where a retry
					 * was exhausted
					 */
	s8 rx_lastpkt_rssi[BRCMF_ANT_MAX]; /* Per antenna RSSI of last
					    * received data frame.
					    */
	/* TX WLAN retry/failure statistics:
	 * Separated for host requested frames and locally generated frames.
	 * Include unicast frame only where the retries/failures can be counted.
	 */
	__le32 tx_pkts_total;          /* # user frames sent successfully */
	__le32 tx_pkts_retries;        /* # user frames retries */
	__le32 tx_pkts_fw_total;       /* # FW generated sent successfully */
	__le32 tx_pkts_fw_retries;     /* # retries for FW generated frames */
	__le32 tx_pkts_fw_retry_exhausted;     /* # FW generated where a retry
						* was exhausted
						*/
	__le32 rx_pkts_retried;        /* # rx with retry bit set */
	__le32 tx_rate_fallback;       /* lowest fallback TX rate */

	union {
		struct {
			struct {
				__le32 count;					/* # rates in this set */
				u8 rates[BRCMF_MAXRATES_IN_SET];		/* rates in 500kbps units w/hi bit set if basic */
				u8 mcs[BRCMF_MCSSET_LEN];			/* supported mcs index bit map */
				__le16 vht_mcs[BRCMF_VHT_CAP_MCS_MAP_NSS_MAX];	/* supported mcs index bit map per nss */
			} rateset_adv;
		} v5;

		struct {
			__le32 rx_dur_total;	/* total user RX duration (estimated) */
			__le16 chanspec;	/** chanspec this sta is on */
			__le16 pad_1;
			struct {
				__le16 version;					/* version */
				__le16 len;					/* length */
				__le32 count;					/* # rates in this set */
				u8 rates[BRCMF_MAXRATES_IN_SET];		/* rates in 500kbps units w/hi bit set if basic */
				u8 mcs[BRCMF_MCSSET_LEN];			/* supported mcs index bit map */
				__le16 vht_mcs[BRCMF_VHT_CAP_MCS_MAP_NSS_MAX];	/* supported mcs index bit map per nss */
				__le16 he_mcs[BRCMF_HE_CAP_MCS_MAP_NSS_MAX];	/* supported he mcs index bit map per nss */
			} rateset_adv;		/* rateset along with mcs index bitmap */
			__le16 wpauth;		/* authentication type */
			u8 algo;		/* crypto algorithm */
			u8 pad_2;
			__le32 tx_rspec;	/* Rate of last successful tx frame */
			__le32 rx_rspec;	/* Rate of last successful rx frame */
			__le32 wnm_cap;		/* wnm capabilities */
		} v7;
	};
};

struct brcmf_chanspec_list {
	__le32	count;		/* # of entries */
	__le32  element[];	/* variable length uint32 list */
};

/*
 * WLC_E_PROBRESP_MSG
 * WLC_E_P2P_PROBREQ_MSG
 * WLC_E_ACTION_FRAME_RX
 */
struct brcmf_rx_mgmt_data {
	__be16	version;
	__be16	chanspec;
	__be32	rssi;
	__be32	mactime;
	__be32	rate;
};

/**
 * struct brcmf_fil_wowl_pattern_le - wowl pattern configuration struct.
 *
 * @cmd: "add", "del" or "clr".
 * @masksize: Size of the mask in #of bytes
 * @offset: Pattern byte offset in packet
 * @patternoffset: Offset of start of pattern. Starting from field masksize.
 * @patternsize: Size of the pattern itself in #of bytes
 * @id: id
 * @reasonsize: Size of the wakeup reason code
 * @type: Type of pattern (enum brcmf_wowl_pattern_type)
 */
struct brcmf_fil_wowl_pattern_le {
	u8	cmd[4];
	__le32	masksize;
	__le32	offset;
	__le32	patternoffset;
	__le32	patternsize;
	__le32	id;
	__le32	reasonsize;
	__le32	type;
	/* u8 mask[] - Mask follows the structure above */
	/* u8 pattern[] - Pattern follows the mask is at 'patternoffset' */
};

struct brcmf_mbss_ssid_le {
	__le32	bsscfgidx;
	__le32	SSID_len;
	unsigned char SSID[32];
};

/**
 * struct brcmf_fil_country_le - country configuration structure.
 *
 * @country_abbrev: null-terminated country code used in the country IE.
 * @rev: revision specifier for ccode. on set, -1 indicates unspecified.
 * @ccode: null-terminated built-in country code.
 */
struct brcmf_fil_country_le {
	char country_abbrev[BRCMF_COUNTRY_BUF_SZ];
	__le32 rev;
	char ccode[BRCMF_COUNTRY_BUF_SZ];
};

/**
 * struct brcmf_rev_info_le - device revision info.
 *
 * @vendorid: PCI vendor id.
 * @deviceid: device id of chip.
 * @radiorev: radio revision.
 * @chiprev: chip revision.
 * @corerev: core revision.
 * @boardid: board identifier (usu. PCI sub-device id).
 * @boardvendor: board vendor (usu. PCI sub-vendor id).
 * @boardrev: board revision.
 * @driverrev: driver version.
 * @ucoderev: microcode version.
 * @bus: bus type.
 * @chipnum: chip number.
 * @phytype: phy type.
 * @phyrev: phy revision.
 * @anarev: anacore rev.
 * @chippkg: chip package info.
 * @nvramrev: nvram revision number.
 */
struct brcmf_rev_info_le {
	__le32 vendorid;
	__le32 deviceid;
	__le32 radiorev;
	__le32 chiprev;
	__le32 corerev;
	__le32 boardid;
	__le32 boardvendor;
	__le32 boardrev;
	__le32 driverrev;
	__le32 ucoderev;
	__le32 bus;
	__le32 chipnum;
	__le32 phytype;
	__le32 phyrev;
	__le32 anarev;
	__le32 chippkg;
	__le32 nvramrev;
};

/**
 * struct brcmf_wlc_version_le - firmware revision info.
 *
 * @version: structure version.
 * @length: structure length.
 * @epi_ver_major: EPI major version
 * @epi_ver_minor: EPI minor version
 * @epi_ver_rc: EPI rc version
 * @epi_ver_incr: EPI increment version
 * @wlc_ver_major: WLC major version
 * @wlc_ver_minor: WLC minor version
 */
struct brcmf_wlc_version_le {
	__le16 version;
	__le16 length;

	__le16 epi_ver_major;
	__le16 epi_ver_minor;
	__le16 epi_ver_rc;
	__le16 epi_ver_incr;

	__le16 wlc_ver_major;
	__le16 wlc_ver_minor;
};

/**
 * struct brcmf_assoclist_le - request assoc list.
 *
 * @count: indicates number of stations.
 * @mac: MAC addresses of stations.
 */
struct brcmf_assoclist_le {
	__le32 count;
	u8 mac[BRCMF_MAX_ASSOCLIST][ETH_ALEN];
};

/**
 * struct brcmf_rssi_be - RSSI threshold event format
 *
 * @rssi: receive signal strength (in dBm)
 * @snr: signal-noise ratio
 * @noise: noise (in dBm)
 */
struct brcmf_rssi_be {
	__be32 rssi;
	__be32 snr;
	__be32 noise;
};

#define BRCMF_MAX_RSSI_LEVELS 8

/**
 * struct brcm_rssi_event_le - rssi_event IOVAR format
 *
 * @rate_limit_msec: RSSI event rate limit
 * @rssi_level_num: number of supplied RSSI levels
 * @rssi_levels: RSSI levels in ascending order
 */
struct brcmf_rssi_event_le {
	__le32 rate_limit_msec;
	s8 rssi_level_num;
	s8 rssi_levels[BRCMF_MAX_RSSI_LEVELS];
};

/**
 * struct brcmf_wowl_wakeind_le - Wakeup indicators
 *	Note: note both fields contain same information.
 *
 * @pci_wakeind: Whether PCI PMECSR PMEStatus bit was set.
 * @ucode_wakeind: What wakeup-event indication was set by ucode
 */
struct brcmf_wowl_wakeind_le {
	__le32 pci_wakeind;
	__le32 ucode_wakeind;
};

/**
 * struct brcmf_pmksa - PMK Security Association
 *
 * @bssid: The AP's BSSID.
 * @pmkid: he PMK material itself.
 */
struct brcmf_pmksa {
	u8 bssid[ETH_ALEN];
	u8 pmkid[WLAN_PMKID_LEN];
};

/**
 * struct brcmf_pmksa_v2 - PMK Security Association
 *
 * @length: Length of the structure.
 * @bssid: The AP's BSSID.
 * @pmkid: The PMK ID.
 * @pmk: PMK material for FILS key derivation.
 * @pmk_len: Length of PMK data.
 * @ssid: The AP's SSID.
 * @fils_cache_id: FILS cache identifier
 */
struct brcmf_pmksa_v2 {
	__le16 length;
	u8 bssid[ETH_ALEN];
	u8 pmkid[WLAN_PMKID_LEN];
	u8 pmk[WLAN_PMK_LEN_SUITE_B_192];
	__le16 pmk_len;
	struct brcmf_ssid8_le ssid;
	u16 fils_cache_id;
};

/**
 * struct brcmf_pmksa_v3 - PMK Security Association
 *
 * @bssid: The AP's BSSID.
 * @pmkid: The PMK ID.
 * @pmkid_len: The length of the PMK ID.
 * @pmk: PMK material for FILS key derivation.
 * @pmk_len: Length of PMK data.
 * @fils_cache_id: FILS cache identifier
 * @ssid: The AP's SSID.
 * @time_left: Remaining time until expiry. 0 = expired, ~0 = no expiry.
 */
struct brcmf_pmksa_v3 {
	u8 bssid[ETH_ALEN];
	u8 pmkid[WLAN_PMKID_LEN];
	u8 pmkid_len;
	u8 pmk[WLAN_PMK_LEN_SUITE_B_192];
	u8 pmk_len;
	__le16 fils_cache_id;
	u8 pad;
	struct brcmf_ssid8_le ssid;
	__le32 time_left;
};

/**
 * struct brcmf_pmk_list_le - List of pmksa's.
 *
 * @npmk: Number of pmksa's.
 * @pmk: PMK SA information.
 */
struct brcmf_pmk_list_le {
	__le32 npmk;
	struct brcmf_pmksa pmk[BRCMF_MAXPMKID];
};

/**
 * struct brcmf_pmk_list_v2_le - List of pmksa's.
 *
 * @version: Request version.
 * @length: Length of this structure.
 * @pmk: PMK SA information.
 */
struct brcmf_pmk_list_v2_le {
	__le16 version;
	__le16 length;
	struct brcmf_pmksa_v2 pmk[BRCMF_MAXPMKID];
};

/**
 * struct brcmf_pmk_op_v3_le - Operation on PMKSA list.
 *
 * @version: Request version.
 * @length: Length of this structure.
 * @pmk: PMK SA information.
 */
struct brcmf_pmk_op_v3_le {
	__le16 version;
	__le16 length;
	__le16 count;
	__le16 pad;
	struct brcmf_pmksa_v3 pmk[BRCMF_MAXPMKID];
};

/**
 * struct brcmf_pno_param_le - PNO scan configuration parameters
 *
 * @version: PNO parameters version.
 * @scan_freq: scan frequency.
 * @lost_network_timeout: #sec. to declare discovered network as lost.
 * @flags: Bit field to control features of PFN such as sort criteria auto
 *	enable switch and background scan.
 * @rssi_margin: Margin to avoid jitter for choosing a PFN based on RSSI sort
 *	criteria.
 * @bestn: number of best networks in each scan.
 * @mscan: number of scans recorded.
 * @repeat: minimum number of scan intervals before scan frequency changes
 *	in adaptive scan.
 * @exp: exponent of 2 for maximum scan interval.
 * @slow_freq: slow scan period.
 */
struct brcmf_pno_param_le {
	__le32 version;
	__le32 scan_freq;
	__le32 lost_network_timeout;
	__le16 flags;
	__le16 rssi_margin;
	u8 bestn;
	u8 mscan;
	u8 repeat;
	u8 exp;
	__le32 slow_freq;
};

/**
 * struct brcmf_pno_config_le - PNO channel configuration.
 *
 * @reporttype: determines what is reported.
 * @channel_num: number of channels specified in @channel_list.
 * @channel_list: channels to use in PNO scan.
 * @flags: reserved.
 */
struct brcmf_pno_config_le {
	__le32  reporttype;
	__le32  channel_num;
	__le16  channel_list[BRCMF_NUMCHANNELS];
	__le32  flags;
};

/**
 * struct brcmf_pno_net_param_le - scan parameters per preferred network.
 *
 * @ssid: ssid name and its length.
 * @flags: bit2: hidden.
 * @infra: BSS vs IBSS.
 * @auth: Open vs Closed.
 * @wpa_auth: WPA type.
 * @wsec: wsec value.
 */
struct brcmf_pno_net_param_le {
	struct brcmf_ssid_le ssid;
	__le32 flags;
	__le32 infra;
	__le32 auth;
	__le32 wpa_auth;
	__le32 wsec;
};

/**
 * struct brcmf_pno_net_info_le - information per found network.
 *
 * @bssid: BSS network identifier.
 * @channel: channel number only.
 * @SSID_len: length of ssid.
 * @SSID: ssid characters.
 * @RSSI: receive signal strength (in dBm).
 * @timestamp: age in seconds.
 */
struct brcmf_pno_net_info_le {
	u8 bssid[ETH_ALEN];
	u8 channel;
	u8 SSID_len;
	u8 SSID[32];
	__le16	RSSI;
	__le16	timestamp;
};

/**
 * struct brcmf_pno_scanresults_le - result returned in PNO NET FOUND event.
 *
 * @version: PNO version identifier.
 * @status: indicates completion status of PNO scan.
 * @count: amount of brcmf_pno_net_info_le entries appended.
 */
struct brcmf_pno_scanresults_le {
	__le32 version;
	__le32 status;
	__le32 count;
};

struct brcmf_pno_scanresults_v2_le {
	__le32 version;
	__le32 status;
	__le32 count;
	__le32 scan_ch_bucket;
};

/**
 * struct brcmf_pno_macaddr_le - to configure PNO macaddr randomization.
 *
 * @version: PNO version identifier.
 * @flags: Flags defining how mac addrss should be used.
 * @mac: MAC address.
 */
struct brcmf_pno_macaddr_le {
	u8 version;
	u8 flags;
	u8 mac[ETH_ALEN];
};

/**
 * struct brcmf_dload_data_le - data passing to firmware for downloading
 * @flag: flags related to download data.
 * @dload_type: type of download data.
 * @len: length in bytes of download data.
 * @crc: crc of download data.
 * @data: download data.
 */
struct brcmf_dload_data_le {
	__le16 flag;
	__le16 dload_type;
	__le32 len;
	__le32 crc;
	u8 data[];
};

/**
 * struct brcmf_pno_bssid_le - bssid configuration for PNO scan.
 *
 * @bssid: BSS network identifier.
 * @flags: flags for this BSSID.
 */
struct brcmf_pno_bssid_le {
	u8 bssid[ETH_ALEN];
	__le16 flags;
};

/**
 * struct brcmf_pktcnt_le - packet counters.
 *
 * @rx_good_pkt: packets (MSDUs & MMPDUs) received from this station
 * @rx_bad_pkt: failed rx packets
 * @tx_good_pkt: packets (MSDUs & MMPDUs) transmitted to this station
 * @tx_bad_pkt: failed tx packets
 * @rx_ocast_good_pkt: unicast packets destined for others
 */
struct brcmf_pktcnt_le {
	__le32 rx_good_pkt;
	__le32 rx_bad_pkt;
	__le32 tx_good_pkt;
	__le32 tx_bad_pkt;
	__le32 rx_ocast_good_pkt;
};

/**
 * struct brcmf_gtk_keyinfo_le - GTP rekey data
 *
 * @kck: key confirmation key.
 * @kek: key encryption key.
 * @replay_counter: replay counter.
 */
struct brcmf_gtk_keyinfo_le {
	u8 kck[BRCMF_RSN_KCK_LENGTH];
	u8 kek[BRCMF_RSN_KEK_LENGTH];
	u8 replay_counter[BRCMF_RSN_REPLAY_LEN];
};

#define BRCMF_PNO_REPORT_NO_BATCH	BIT(2)

/**
 * struct brcmf_gscan_bucket_config - configuration data for channel bucket.
 *
 * @bucket_end_index: last channel index in @channel_list in
 *	@struct brcmf_pno_config_le.
 * @bucket_freq_multiple: scan interval expressed in N * @scan_freq.
 * @flag: channel bucket report flags.
 * @reserved: for future use.
 * @repeat: number of scan at interval for exponential scan.
 * @max_freq_multiple: maximum scan interval for exponential scan.
 */
struct brcmf_gscan_bucket_config {
	u8 bucket_end_index;
	u8 bucket_freq_multiple;
	u8 flag;
	u8 reserved;
	__le16 repeat;
	__le16 max_freq_multiple;
};

/* version supported which must match firmware */
#define BRCMF_GSCAN_CFG_VERSION                     2

/**
 * enum brcmf_gscan_cfg_flags - bit values for gscan flags.
 *
 * @BRCMF_GSCAN_CFG_FLAGS_ALL_RESULTS: send probe responses/beacons to host.
 * @BRCMF_GSCAN_CFG_ALL_BUCKETS_IN_1ST_SCAN: all buckets will be included in
 *	first scan cycle.
 * @BRCMF_GSCAN_CFG_FLAGS_CHANGE_ONLY: indicated only flags member is changed.
 */
enum brcmf_gscan_cfg_flags {
	BRCMF_GSCAN_CFG_FLAGS_ALL_RESULTS = BIT(0),
	BRCMF_GSCAN_CFG_ALL_BUCKETS_IN_1ST_SCAN = BIT(3),
	BRCMF_GSCAN_CFG_FLAGS_CHANGE_ONLY = BIT(7),
};

/**
 * struct brcmf_gscan_config - configuration data for gscan.
 *
 * @version: version of the api to match firmware.
 * @flags: flags according %enum brcmf_gscan_cfg_flags.
 * @buffer_threshold: percentage threshold of buffer to generate an event.
 * @swc_nbssid_threshold: number of BSSIDs with significant change that
 *	will generate an event.
 * @swc_rssi_window_size: size of rssi cache buffer (max=8).
 * @count_of_channel_buckets: number of array members in @bucket.
 * @retry_threshold: !unknown!
 * @lost_ap_window: !unknown!
 * @bucket: array of channel buckets.
 */
struct brcmf_gscan_config {
	__le16 version;
	u8 flags;
	u8 buffer_threshold;
	u8 swc_nbssid_threshold;
	u8 swc_rssi_window_size;
	u8 count_of_channel_buckets;
	u8 retry_threshold;
	__le16  lost_ap_window;
	struct brcmf_gscan_bucket_config bucket[];
};

/**
 * struct brcmf_mkeep_alive_pkt_le - configuration data for keep-alive frame.
 *
 * @version: version for mkeep_alive
 * @length: length of fixed parameters in the structure.
 * @period_msec: keep-alive period in milliseconds.
 * @len_bytes: size of the data.
 * @keep_alive_id: ID  (0 - 3).
 * @data: keep-alive frame data.
 */
struct brcmf_mkeep_alive_pkt_le {
	__le16  version;
	__le16  length;
	__le32  period_msec;
	__le16  len_bytes;
	u8   keep_alive_id;
	u8   data[];
} __packed;

#endif /* FWIL_TYPES_H_ */
