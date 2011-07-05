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

/****************
 * Common types *
 */

#ifndef _BRCMF_H_
#define _BRCMF_H_

#define BRCMF_VERSION_STR		"4.218.248.5"

#define	BRCMF_C_IOCTL_SMLEN	256	/* "small" ioctl buffer required */
#define BRCMF_C_IOCTL_MEDLEN	1536	/* "med" ioctl buffer required */
#define	BRCMF_C_IOCTL_MAXLEN	8192

#define BRCMF_C_UP				2
#define BRCMF_C_SET_PROMISC			10
#define BRCMF_C_GET_RATE			12
#define BRCMF_C_GET_INFRA			19
#define BRCMF_C_SET_INFRA			20
#define BRCMF_C_GET_AUTH			21
#define BRCMF_C_SET_AUTH			22
#define BRCMF_C_GET_BSSID			23
#define BRCMF_C_GET_SSID			25
#define BRCMF_C_SET_SSID			26
#define BRCMF_C_GET_CHANNEL			29
#define BRCMF_C_GET_SRL				31
#define BRCMF_C_GET_LRL				33
#define BRCMF_C_GET_RADIO			37
#define BRCMF_C_SET_RADIO			38
#define BRCMF_C_GET_PHYTYPE			39
#define BRCMF_C_SET_KEY				45
#define BRCMF_C_SET_PASSIVE_SCAN		49
#define BRCMF_C_SCAN				50
#define BRCMF_C_SCAN_RESULTS			51
#define BRCMF_C_DISASSOC			52
#define BRCMF_C_REASSOC				53
#define BRCMF_C_SET_ROAM_TRIGGER		55
#define BRCMF_C_SET_ROAM_DELTA			57
#define BRCMF_C_GET_DTIMPRD			77
#define BRCMF_C_SET_COUNTRY			84
#define BRCMF_C_GET_PM				85
#define BRCMF_C_SET_PM				86
#define BRCMF_C_GET_AP				117
#define BRCMF_C_SET_AP				118
#define BRCMF_C_GET_RSSI			127
#define BRCMF_C_GET_WSEC			133
#define BRCMF_C_SET_WSEC			134
#define BRCMF_C_GET_PHY_NOISE			135
#define BRCMF_C_GET_BSS_INFO			136
#define BRCMF_C_SET_SCAN_CHANNEL_TIME		185
#define BRCMF_C_SET_SCAN_UNASSOC_TIME		187
#define BRCMF_C_SCB_DEAUTHENTICATE_FOR_REASON	201
#define BRCMF_C_GET_VALID_CHANNELS		217
#define BRCMF_C_GET_KEY_PRIMARY			235
#define BRCMF_C_SET_KEY_PRIMARY			236
#define BRCMF_C_SET_SCAN_PASSIVE_TIME		258
#define BRCMF_C_GET_VAR				262
#define BRCMF_C_SET_VAR				263

/* phy types (returned by WLC_GET_PHYTPE) */
#define	WLC_PHY_TYPE_A		0
#define	WLC_PHY_TYPE_B		1
#define	WLC_PHY_TYPE_G		2
#define	WLC_PHY_TYPE_N		4
#define	WLC_PHY_TYPE_LP		5
#define	WLC_PHY_TYPE_SSN	6
#define	WLC_PHY_TYPE_HT		7
#define	WLC_PHY_TYPE_LCN	8
#define	WLC_PHY_TYPE_NULL	0xf

#define BRCMF_PKT_FILTER_FIXED_LEN	offsetof(struct brcmf_pkt_filter, u)
#define BRCMF_PKT_FILTER_PATTERN_FIXED_LEN	\
	offsetof(struct brcmf_pkt_filter_pattern, mask_and_pattern)

#define BRCMF_EVENTING_MASK_LEN	16

#define TOE_TX_CSUM_OL		0x00000001
#define TOE_RX_CSUM_OL		0x00000002

/* maximum channels returned by the get valid channels iovar */
#define WL_NUMCHANNELS		64

#define	BRCMF_BSS_INFO_VERSION	108 /* current ver of brcmf_bss_info struct */

/* size of brcmf_scan_params not including variable length array */
#define BRCMF_SCAN_PARAMS_FIXED_SIZE 64

/* masks for channel and ssid count */
#define BRCMF_SCAN_PARAMS_COUNT_MASK 0x0000ffff
#define BRCMF_SCAN_PARAMS_NSSID_SHIFT 16

#define BRCMF_SCAN_ACTION_START      1
#define BRCMF_SCAN_ACTION_CONTINUE   2
#define WL_SCAN_ACTION_ABORT      3

#define BRCMF_ISCAN_REQ_VERSION 1

/* brcmf_iscan_results status values */
#define BRCMF_SCAN_RESULTS_SUCCESS	0
#define BRCMF_SCAN_RESULTS_PARTIAL	1
#define BRCMF_SCAN_RESULTS_PENDING	2
#define BRCMF_SCAN_RESULTS_ABORTED	3
#define BRCMF_SCAN_RESULTS_NO_MEM	4

#define WL_SOFT_KEY	(1 << 0)	/* Indicates this key is using soft encrypt */
#define BRCMF_PRIMARY_KEY	(1 << 1)	/* primary (ie tx) key */
#define WL_KF_RES_4	(1 << 4)	/* Reserved for backward compat */
#define WL_KF_RES_5	(1 << 5)	/* Reserved for backward compat */
#define WL_IBSS_PEER_GROUP_KEY	(1 << 6)	/* Indicates a group key for a IBSS PEER */

/* For supporting multiple interfaces */
#define BRCMF_MAX_IFS	16
#define BRCMF_DEL_IF	-0xe
#define BRCMF_BAD_IF	-0xf

#define DOT11_BSSTYPE_ANY			2
#define DOT11_MAX_DEFAULT_KEYS	4

#define BRCMF_EVENT_MSG_LINK		0x01
#define BRCMF_EVENT_MSG_FLUSHTXQ	0x02
#define BRCMF_EVENT_MSG_GROUP		0x04

struct brcmf_event_msg {
	u16 version;
	u16 flags;
	u32 event_type;
	u32 status;
	u32 reason;
	u32 auth_type;
	u32 datalen;
	u8 addr[ETH_ALEN];
	char ifname[IFNAMSIZ];
} __packed;

struct brcm_ethhdr {
	u16 subtype;
	u16 length;
	u8 version;
	u8 oui[3];
	u16 usr_subtype;
} __packed;

struct brcmf_event {
	struct ethhdr eth;
	struct brcm_ethhdr hdr;
	struct brcmf_event_msg msg;
} __packed;

struct dngl_stats {
	unsigned long rx_packets;	/* total packets received */
	unsigned long tx_packets;	/* total packets transmitted */
	unsigned long rx_bytes;	/* total bytes received */
	unsigned long tx_bytes;	/* total bytes transmitted */
	unsigned long rx_errors;	/* bad packets received */
	unsigned long tx_errors;	/* packet transmit problems */
	unsigned long rx_dropped;	/* packets dropped by dongle */
	unsigned long tx_dropped;	/* packets dropped by dongle */
	unsigned long multicast;	/* multicast packets received */
};

#define BRCMF_E_SET_SSID			0
#define BRCMF_E_JOIN				1
#define BRCMF_E_START				2
#define BRCMF_E_AUTH				3
#define BRCMF_E_AUTH_IND			4
#define BRCMF_E_DEAUTH				5
#define BRCMF_E_DEAUTH_IND			6
#define BRCMF_E_ASSOC				7
#define BRCMF_E_ASSOC_IND			8
#define BRCMF_E_REASSOC				9
#define BRCMF_E_REASSOC_IND			10
#define BRCMF_E_DISASSOC			11
#define BRCMF_E_DISASSOC_IND			12
#define BRCMF_E_QUIET_START			13
#define BRCMF_E_QUIET_END			14
#define BRCMF_E_BEACON_RX			15
#define BRCMF_E_LINK				16
#define BRCMF_E_MIC_ERROR			17
#define BRCMF_E_NDIS_LINK			18
#define BRCMF_E_ROAM				19
#define BRCMF_E_TXFAIL				20
#define BRCMF_E_PMKID_CACHE			21
#define BRCMF_E_RETROGRADE_TSF			22
#define BRCMF_E_PRUNE				23
#define BRCMF_E_AUTOAUTH			24
#define BRCMF_E_EAPOL_MSG			25
#define BRCMF_E_SCAN_COMPLETE			26
#define BRCMF_E_ADDTS_IND			27
#define BRCMF_E_DELTS_IND			28
#define BRCMF_E_BCNSENT_IND			29
#define BRCMF_E_BCNRX_MSG			30
#define BRCMF_E_BCNLOST_MSG			31
#define BRCMF_E_ROAM_PREP			32
#define BRCMF_E_PFN_NET_FOUND			33
#define BRCMF_E_PFN_NET_LOST			34
#define BRCMF_E_RESET_COMPLETE			35
#define BRCMF_E_JOIN_START			36
#define BRCMF_E_ROAM_START			37
#define BRCMF_E_ASSOC_START			38
#define BRCMF_E_IBSS_ASSOC			39
#define BRCMF_E_RADIO				40
#define BRCMF_E_PSM_WATCHDOG			41
#define BRCMF_E_PROBREQ_MSG			44
#define BRCMF_E_SCAN_CONFIRM_IND		45
#define BRCMF_E_PSK_SUP				46
#define BRCMF_E_COUNTRY_CODE_CHANGED		47
#define	BRCMF_E_EXCEEDED_MEDIUM_TIME		48
#define BRCMF_E_ICV_ERROR			49
#define BRCMF_E_UNICAST_DECODE_ERROR		50
#define BRCMF_E_MULTICAST_DECODE_ERROR		51
#define BRCMF_E_TRACE				52
#define BRCMF_E_IF				54
#define BRCMF_E_RSSI				56
#define BRCMF_E_PFN_SCAN_COMPLETE		57
#define BRCMF_E_EXTLOG_MSG			58
#define BRCMF_E_ACTION_FRAME			59
#define BRCMF_E_ACTION_FRAME_COMPLETE		60
#define BRCMF_E_PRE_ASSOC_IND			61
#define BRCMF_E_PRE_REASSOC_IND			62
#define BRCMF_E_CHANNEL_ADOPTED			63
#define BRCMF_E_AP_STARTED			64
#define BRCMF_E_DFS_AP_STOP			65
#define BRCMF_E_DFS_AP_RESUME			66
#define BRCMF_E_RESERVED1			67
#define BRCMF_E_RESERVED2			68
#define BRCMF_E_ESCAN_RESULT			69
#define BRCMF_E_ACTION_FRAME_OFF_CHAN_COMPLETE	70
#define BRCMF_E_DCS_REQUEST			73

#define BRCMF_E_FIFO_CREDIT_MAP			74

#define BRCMF_E_LAST				75

#define BRCMF_E_STATUS_SUCCESS			0
#define BRCMF_E_STATUS_FAIL			1
#define BRCMF_E_STATUS_TIMEOUT			2
#define BRCMF_E_STATUS_NO_NETWORKS		3
#define BRCMF_E_STATUS_ABORT			4
#define BRCMF_E_STATUS_NO_ACK			5
#define BRCMF_E_STATUS_UNSOLICITED		6
#define BRCMF_E_STATUS_ATTEMPT			7
#define BRCMF_E_STATUS_PARTIAL			8
#define BRCMF_E_STATUS_NEWSCAN			9
#define BRCMF_E_STATUS_NEWASSOC			10
#define BRCMF_E_STATUS_11HQUIET			11
#define BRCMF_E_STATUS_SUPPRESS			12
#define BRCMF_E_STATUS_NOCHANS			13
#define BRCMF_E_STATUS_CS_ABORT			15
#define BRCMF_E_STATUS_ERROR			16

#define BRCMF_E_REASON_INITIAL_ASSOC		0
#define BRCMF_E_REASON_LOW_RSSI			1
#define BRCMF_E_REASON_DEAUTH			2
#define BRCMF_E_REASON_DISASSOC			3
#define BRCMF_E_REASON_BCNS_LOST		4
#define BRCMF_E_REASON_MINTXRATE		9
#define BRCMF_E_REASON_TXFAIL			10

#define BRCMF_E_REASON_FAST_ROAM_FAILED		5
#define BRCMF_E_REASON_DIRECTED_ROAM		6
#define BRCMF_E_REASON_TSPEC_REJECTED		7
#define BRCMF_E_REASON_BETTER_AP		8

#define BRCMF_E_PRUNE_ENCR_MISMATCH		1
#define BRCMF_E_PRUNE_BCAST_BSSID		2
#define BRCMF_E_PRUNE_MAC_DENY			3
#define BRCMF_E_PRUNE_MAC_NA			4
#define BRCMF_E_PRUNE_REG_PASSV			5
#define BRCMF_E_PRUNE_SPCT_MGMT			6
#define BRCMF_E_PRUNE_RADAR			7
#define BRCMF_E_RSN_MISMATCH			8
#define BRCMF_E_PRUNE_NO_COMMON_RATES		9
#define BRCMF_E_PRUNE_BASIC_RATES		10
#define BRCMF_E_PRUNE_CIPHER_NA			12
#define BRCMF_E_PRUNE_KNOWN_STA			13
#define BRCMF_E_PRUNE_WDS_PEER			15
#define BRCMF_E_PRUNE_QBSS_LOAD			16
#define BRCMF_E_PRUNE_HOME_AP			17

#define BRCMF_E_SUP_OTHER			0
#define BRCMF_E_SUP_DECRYPT_KEY_DATA		1
#define BRCMF_E_SUP_BAD_UCAST_WEP128		2
#define BRCMF_E_SUP_BAD_UCAST_WEP40		3
#define BRCMF_E_SUP_UNSUP_KEY_LEN		4
#define BRCMF_E_SUP_PW_KEY_CIPHER		5
#define BRCMF_E_SUP_MSG3_TOO_MANY_IE		6
#define BRCMF_E_SUP_MSG3_IE_MISMATCH		7
#define BRCMF_E_SUP_NO_INSTALL_FLAG		8
#define BRCMF_E_SUP_MSG3_NO_GTK			9
#define BRCMF_E_SUP_GRP_KEY_CIPHER		10
#define BRCMF_E_SUP_GRP_MSG1_NO_GTK		11
#define BRCMF_E_SUP_GTK_DECRYPT_FAIL		12
#define BRCMF_E_SUP_SEND_FAIL			13
#define BRCMF_E_SUP_DEAUTH			14

#define BRCMF_E_IF_ADD				1
#define BRCMF_E_IF_DEL				2
#define BRCMF_E_IF_CHANGE			3

#define BRCMF_E_IF_ROLE_STA			0
#define BRCMF_E_IF_ROLE_AP			1
#define BRCMF_E_IF_ROLE_WDS			2

#define BRCMF_E_LINK_BCN_LOSS			1
#define BRCMF_E_LINK_DISASSOC			2
#define BRCMF_E_LINK_ASSOC_REC			3
#define BRCMF_E_LINK_BSSCFG_DIS			4

/* The level of bus communication with the dongle */
enum brcmf_bus_state {
	BRCMF_BUS_DOWN,		/* Not ready for frame transfers */
	BRCMF_BUS_LOAD,		/* Download access only (CPU reset) */
	BRCMF_BUS_DATA		/* Ready for frame transfers */
};

/* Pattern matching filter. Specifies an offset within received packets to
 * start matching, the pattern to match, the size of the pattern, and a bitmask
 * that indicates which bits within the pattern should be matched.
 */
struct brcmf_pkt_filter_pattern {
	u32 offset;		/* Offset within received packet to start pattern matching.
				 * Offset '0' is the first byte of the ethernet header.
				 */
	u32 size_bytes;	/* Size of the pattern.  Bitmask must be the same size. */
	u8 mask_and_pattern[1];	/* Variable length mask and pattern data.  mask starts
					 * at offset 0.  Pattern immediately follows mask.
					 */
};

/* IOVAR "pkt_filter_add" parameter. Used to install packet filters. */
struct brcmf_pkt_filter {
	u32 id;		/* Unique filter id, specified by app. */
	u32 type;		/* Filter type (WL_PKT_FILTER_TYPE_xxx). */
	u32 negate_match;	/* Negate the result of filter matches */
	union {			/* Filter definitions */
		struct brcmf_pkt_filter_pattern pattern; /* Filter pattern */
	} u;
};

/* IOVAR "pkt_filter_enable" parameter. */
struct brcmf_pkt_filter_enable {
	u32 id;		/* Unique filter id */
	u32 enable;		/* Enable/disable bool */
};

/* BSS info structure
 * Applications MUST CHECK ie_offset field and length field to access IEs and
 * next bss_info structure in a vector (in struct brcmf_scan_results)
 */
struct brcmf_bss_info {
	u32 version;		/* version field */
	u32 length;		/* byte length of data in this record,
				 * starting at version and including IEs
				 */
	u8 BSSID[ETH_ALEN];
	u16 beacon_period;	/* units are Kusec */
	u16 capability;	/* Capability information */
	u8 SSID_len;
	u8 SSID[32];
	struct {
		uint count;	/* # rates in this set */
		u8 rates[16];	/* rates in 500kbps units w/hi bit set if basic */
	} rateset;		/* supported rates */
	chanspec_t chanspec;	/* chanspec for bss */
	u16 atim_window;	/* units are Kusec */
	u8 dtim_period;	/* DTIM period */
	s16 RSSI;		/* receive signal strength (in dBm) */
	s8 phy_noise;		/* noise (in dBm) */

	u8 n_cap;		/* BSS is 802.11N Capable */
	u32 nbss_cap;	/* 802.11N BSS Capabilities (based on HT_CAP_*) */
	u8 ctl_ch;		/* 802.11N BSS control channel number */
	u32 reserved32[1];	/* Reserved for expansion of BSS properties */
	u8 flags;		/* flags */
	u8 reserved[3];	/* Reserved for expansion of BSS properties */
	u8 basic_mcs[MCSSET_LEN];	/* 802.11N BSS required MCS set */

	u16 ie_offset;	/* offset at which IEs start, from beginning */
	u32 ie_length;	/* byte length of Information Elements */
	s16 SNR;		/* average SNR of during frame reception */
	/* Add new fields here */
	/* variable length Information Elements */
};

struct brcmf_ssid {
	u32 SSID_len;
	unsigned char SSID[32];
};

struct brcmf_scan_params {
	struct brcmf_ssid ssid;	/* default: {0, ""} */
	u8 bssid[ETH_ALEN];	/* default: bcast */
	s8 bss_type;		/* default: any,
				 * DOT11_BSSTYPE_ANY/INFRASTRUCTURE/INDEPENDENT
				 */
	u8 scan_type;	/* flags, 0 use default */
	s32 nprobes;		/* -1 use default, number of probes per channel */
	s32 active_time;	/* -1 use default, dwell time per channel for
				 * active scanning
				 */
	s32 passive_time;	/* -1 use default, dwell time per channel
				 * for passive scanning
				 */
	s32 home_time;	/* -1 use default, dwell time for the home channel
				 * between channel scans
				 */
	s32 channel_num;	/* count of channels and ssids that follow
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
	u16 channel_list[1];	/* list of chanspecs */
};

/* incremental scan struct */
struct brcmf_iscan_params {
	u32 version;
	u16 action;
	u16 scan_duration;
	struct brcmf_scan_params params;
};

/* 3 fields + size of brcmf_scan_params, not including variable length array */
#define BRCMF_ISCAN_PARAMS_FIXED_SIZE \
	(offsetof(struct brcmf_iscan_params, params) + \
	 sizeof(struct brcmf_ssid))

struct brcmf_scan_results {
	u32 buflen;
	u32 version;
	u32 count;
	struct brcmf_bss_info bss_info[1];
};

/* used for association with a specific BSSID and chanspec list */
struct brcmf_assoc_params {
	u8 bssid[ETH_ALEN];	/* 00:00:00:00:00:00: broadcast scan */
	s32 chanspec_num;	/* 0: all available channels,
				 * otherwise count of chanspecs in chanspec_list
				 */
	chanspec_t chanspec_list[1];	/* list of chanspecs */
};
#define BRCMF_ASSOC_PARAMS_FIXED_SIZE \
	(sizeof(struct brcmf_assoc_params) - sizeof(chanspec_t))

/* used for join with or without a specific bssid and channel list */
struct brcmf_join_params {
	struct brcmf_ssid ssid;
	struct brcmf_assoc_params params;
};

/* size of brcmf_scan_results not including variable length array */
#define BRCMF_SCAN_RESULTS_FIXED_SIZE \
	(sizeof(struct brcmf_scan_results) - sizeof(struct brcmf_bss_info))

/* incremental scan results struct */
struct brcmf_iscan_results {
	u32 status;
	struct brcmf_scan_results results;
};

/* size of brcmf_iscan_results not including variable length array */
#define BRCMF_ISCAN_RESULTS_FIXED_SIZE \
	(BRCMF_SCAN_RESULTS_FIXED_SIZE + \
	 offsetof(struct brcmf_iscan_results, results))

struct brcmf_wsec_key {
	u32 index;		/* key index */
	u32 len;		/* key length */
	u8 data[WLAN_MAX_KEY_LEN];	/* key data */
	u32 pad_1[18];
	u32 algo;		/* CRYPTO_ALGO_AES_CCM, CRYPTO_ALGO_WEP128, etc */
	u32 flags;		/* misc flags */
	u32 pad_2[2];
	int pad_3;
	int iv_initialized;	/* has IV been initialized already? */
	int pad_4;
	/* Rx IV */
	struct {
		u32 hi;	/* upper 32 bits of IV */
		u16 lo;	/* lower 16 bits of IV */
	} rxiv;
	u32 pad_5[2];
	u8 ea[ETH_ALEN];	/* per station */
};

/* Used to get specific STA parameters */
struct brcmf_scb_val {
	u32 val;
	u8 ea[ETH_ALEN];
};

/* channel encoding */
struct brcmf_channel_info {
	int hw_channel;
	int target_channel;
	int scan_channel;
};

/* Linux network driver ioctl encoding */
struct brcmf_ioctl {
	uint cmd;		/* common ioctl definition */
	void *buf;		/* pointer to user buffer */
	uint len;		/* length of user buffer */
	u8 set;		/* get or set request (optional) */
	uint used;		/* bytes read or written (optional) */
	uint needed;		/* bytes needed (optional) */
};

/* Forward decls for struct brcmf_pub (see below) */
struct brcmf_bus;		/* device bus info */
struct brcmf_proto;	/* device communication protocol info */
struct brcmf_info;	/* device driver info */

/* Common structure for module and instance linkage */
struct brcmf_pub {
	/* Linkage ponters */
	struct brcmf_bus *bus;
	struct brcmf_proto *prot;
	struct brcmf_info *info;

	/* Internal brcmf items */
	bool up;		/* Driver up/down (to OS) */
	bool txoff;		/* Transmit flow-controlled */
	bool dongle_reset;	/* true = DEVRESET put dongle into reset */
	enum brcmf_bus_state busstate;
	uint hdrlen;		/* Total BRCMF header length (proto + bus) */
	uint maxctl;		/* Max size rxctl request from proto to bus */
	uint rxsz;		/* Rx buffer size bus module should use */
	u8 wme_dp;		/* wme discard priority */

	/* Dongle media info */
	bool iswl;		/* Dongle-resident driver is wl */
	unsigned long drv_version;	/* Version of dongle-resident driver */
	u8 mac[ETH_ALEN];			/* MAC address obtained from dongle */
	struct dngl_stats dstats;	/* Stats for dongle-based data */

	/* Additional stats for the bus level */
	unsigned long tx_packets;	/* Data packets sent to dongle */
	unsigned long tx_multicast;	/* Multicast data packets sent to dongle */
	unsigned long tx_errors;	/* Errors in sending data to dongle */
	unsigned long tx_ctlpkts;	/* Control packets sent to dongle */
	unsigned long tx_ctlerrs;	/* Errors sending control frames to dongle */
	unsigned long rx_packets;	/* Packets sent up the network interface */
	unsigned long rx_multicast;	/* Multicast packets sent up the network
					 interface */
	unsigned long rx_errors;	/* Errors processing rx data packets */
	unsigned long rx_ctlpkts;	/* Control frames processed from dongle */
	unsigned long rx_ctlerrs;	/* Errors in processing rx control frames */
	unsigned long rx_dropped;	/* Packets dropped locally (no memory) */
	unsigned long rx_flushed;	/* Packets flushed due to
				unscheduled sendup thread */
	unsigned long wd_dpc_sched;	/* Number of times dpc scheduled by
					 watchdog timer */

	unsigned long rx_readahead_cnt;	/* Number of packets where header read-ahead
					 was used. */
	unsigned long tx_realloc;	/* Number of tx packets we had to realloc for
					 headroom */
	unsigned long fc_packets;	/* Number of flow control pkts recvd */

	/* Last error return */
	int bcmerror;
	uint tickcnt;

	/* Last error from dongle */
	int dongle_error;

	/* Suspend disable flag  flag */
	int suspend_disable_flag;	/* "1" to disable all extra powersaving
					 during suspend */
	int in_suspend;		/* flag set to 1 when early suspend called */
	int dtim_skip;		/* dtim skip , default 0 means wake each dtim */

	/* Pkt filter defination */
	char *pktfilter[100];
	int pktfilter_count;

	u8 country_code[BRCM_CNTRY_BUF_SZ];
	char eventmask[BRCMF_EVENTING_MASK_LEN];

};

struct brcmf_if_event {
	u8 ifidx;
	u8 action;
	u8 flags;
	u8 bssidx;
};

struct brcmf_timeout {
	u32 limit;		/* Expiration time (usec) */
	u32 increment;	/* Current expiration increment (usec) */
	u32 elapsed;		/* Current elapsed time (usec) */
	u32 tick;		/* O/S tick time (usec) */
};

struct bcmevent_name {
	uint event;
	const char *name;
};

#if defined(CONFIG_PM_SLEEP)
extern atomic_t brcmf_mmc_suspend;
#define BRCMF_PM_RESUME_WAIT_INIT(a) DECLARE_WAIT_QUEUE_HEAD(a);
#define _BRCMF_PM_RESUME_WAIT(a, b) do { \
		int retry = 0; \
		while (atomic_read(&brcmf_mmc_suspend) && retry++ != b) { \
			wait_event_timeout(a, false, HZ/100); \
		} \
	}	while (0)
#define BRCMF_PM_RESUME_WAIT(a)	_BRCMF_PM_RESUME_WAIT(a, 30)
#define BRCMF_PM_RESUME_RETURN_ERROR(a)	\
	do { if (atomic_read(&brcmf_mmc_suspend)) return a; } while (0)

#define BRCMF_SPINWAIT_SLEEP_INIT(a) DECLARE_WAIT_QUEUE_HEAD(a);
#define BRCMF_SPINWAIT_SLEEP(a, exp, us) do { \
		uint countdown = (us) + 9999; \
		while ((exp) && (countdown >= 10000)) { \
			wait_event_timeout(a, false, HZ/100); \
			countdown -= 10000; \
		} \
	} while (0)

#else

#define BRCMF_PM_RESUME_WAIT_INIT(a)
#define BRCMF_PM_RESUME_WAIT(a)
#define BRCMF_PM_RESUME_RETURN_ERROR(a)

#define BRCMF_SPINWAIT_SLEEP_INIT(a)
#define BRCMF_SPINWAIT_SLEEP(a, exp, us)  do { \
		uint countdown = (us) + 9; \
		while ((exp) && (countdown >= 10)) { \
			udelay(10);  \
			countdown -= 10;  \
		} \
	} while (0)

#endif	/* defined(CONFIG_PM_SLEEP) */

/*
 * Insmod parameters for debug/test
 */

/* Use interrupts */
extern uint brcmf_intr;

/* Use polling */
extern uint brcmf_poll;

/* ARP offload agent mode */
extern uint brcmf_arp_mode;

/* ARP offload enable */
extern uint brcmf_arp_enable;

/* Pkt filte enable control */
extern uint brcmf_pkt_filter_enable;

/*  Pkt filter init setup */
extern uint brcmf_pkt_filter_init;

/* Pkt filter mode control */
extern uint brcmf_master_mode;

/* Roaming mode control */
extern uint brcmf_roam;

/* Roaming mode control */
extern uint brcmf_radio_up;

/* Initial idletime ticks (may be -1 for immediate idle, 0 for no idle) */
extern int brcmf_idletime;
#define BRCMF_IDLETIME_TICKS 1

/* SDIO Drive Strength */
extern uint brcmf_sdiod_drive_strength;

/* Override to force tx queueing all the time */
extern uint brcmf_force_tx_queueing;

#ifdef SDTEST
/* Echo packet generator (SDIO), pkts/s */
extern uint brcmf_pktgen;

/* Echo packet len (0 => sawtooth, max 1800) */
extern uint brcmf_pktgen_len;
#define BRCMF_MAX_PKTGEN_LEN 1800
#endif

extern const struct bcmevent_name bcmevent_names[];
extern const int bcmevent_names_size;


static inline void MUTEX_LOCK_INIT(struct brcmf_pub *drvr)
{
}

static inline void MUTEX_LOCK(struct brcmf_pub *drvr)
{
}

static inline void MUTEX_UNLOCK(struct brcmf_pub *drvr)
{
}

static inline void MUTEX_LOCK_SOFTAP_SET_INIT(struct brcmf_pub *drvr)
{
}

static inline void MUTEX_LOCK_SOFTAP_SET(struct brcmf_pub *drvr)
{
}

static inline void MUTEX_UNLOCK_SOFTAP_SET(struct brcmf_pub *drvr)
{
}

static inline void MUTEX_LOCK_WL_SCAN_SET_INIT(void)
{
}

static inline void MUTEX_LOCK_WL_SCAN_SET(void)
{
}

static inline void MUTEX_UNLOCK_WL_SCAN_SET(void)
{
}

/* Indication from bus module regarding presence/insertion of dongle.
 * Return struct brcmf_pub pointer, used as handle to OS module in later calls.
 * Returned structure should have bus and prot pointers filled in.
 * bus_hdrlen specifies required headroom for bus module header.
 */
extern struct brcmf_pub *brcmf_attach(struct brcmf_bus *bus,
				      uint bus_hdrlen);
extern int brcmf_net_attach(struct brcmf_pub *drvr, int idx);
extern int brcmf_netdev_wait_pend8021x(struct net_device *dev);

/* Indication from bus module regarding removal/absence of dongle */
extern void brcmf_detach(struct brcmf_pub *drvr);

/* Indication from bus module to change flow-control state */
extern void brcmf_txflowcontrol(struct brcmf_pub *drvr, int ifidx, bool on);

extern bool brcmf_c_prec_enq(struct brcmf_pub *drvr, struct pktq *q,
			 struct sk_buff *pkt, int prec);

/* Receive frame for delivery to OS.  Callee disposes of rxp. */
extern void brcmf_rx_frame(struct brcmf_pub *drvr, int ifidx,
			 struct sk_buff *rxp, int numpkt);

/* Return pointer to interface name */
extern char *brcmf_ifname(struct brcmf_pub *drvr, int idx);

/* Notify tx completion */
extern void brcmf_txcomplete(struct brcmf_pub *drvr, struct sk_buff *txp,
			     bool success);

/* Query ioctl */
extern int brcmf_proto_cdc_query_ioctl(struct brcmf_pub *drvr, int ifidx,
				       uint cmd, void *buf, uint len);

/* OS independent layer functions */
extern int brcmf_os_proto_block(struct brcmf_pub *drvr);
extern int brcmf_os_proto_unblock(struct brcmf_pub *drvr);
extern int brcmf_os_ioctl_resp_wait(struct brcmf_pub *drvr, uint *condition,
				  bool *pending);
extern int brcmf_os_ioctl_resp_wake(struct brcmf_pub *drvr);
extern unsigned int brcmf_os_get_ioctl_resp_timeout(void);
extern void brcmf_os_set_ioctl_resp_timeout(unsigned int timeout_msec);
#ifdef BCMDBG
extern int brcmf_write_to_file(struct brcmf_pub *drvr, u8 *buf, int size);
#endif				/* BCMDBG */

extern void brcmf_timeout_start(struct brcmf_timeout *tmo, uint usec);
extern int brcmf_timeout_expired(struct brcmf_timeout *tmo);

extern int brcmf_ifname2idx(struct brcmf_info *drvr_priv, char *name);
extern int brcmf_c_host_event(struct brcmf_info *drvr_priv, int *idx,
			      void *pktdata, struct brcmf_event_msg *,
			      void **data_ptr);

extern void brcmf_c_init(void);

extern int brcmf_add_if(struct brcmf_info *drvr_priv, int ifidx, void *handle,
		      char *name, u8 *mac_addr, u32 flags, u8 bssidx);
extern void brcmf_del_if(struct brcmf_info *drvr_priv, int ifidx);

/* Send packet to dongle via data channel */
extern int brcmf_sendpkt(struct brcmf_pub *drvr, int ifidx,\
			 struct sk_buff *pkt);

extern int brcmf_bus_devreset(struct brcmf_pub *drvr, u8 flag);
extern int brcmf_bus_start(struct brcmf_pub *drvr);

extern void brcmf_c_pktfilter_offload_set(struct brcmf_pub *drvr, char *arg);
extern void brcmf_c_pktfilter_offload_enable(struct brcmf_pub *drvr, char *arg,
					     int enable, int master_mode);

/* Linux network driver ioctl encoding */
struct brcmf_c_ioctl {
	uint cmd;		/* common ioctl definition */
	void *buf;		/* pointer to user buffer */
	uint len;		/* length of user buffer */
	bool set;		/* get or set request (optional) */
	uint used;		/* bytes read or written (optional) */
	uint needed;		/* bytes needed (optional) */
	uint driver;		/* to identify target driver */
};

/* per-driver magic numbers */
#define BRCMF_IOCTL_MAGIC		0x00444944

/* bump this number if you change the ioctl interface */
#define BRCMF_IOCTL_VERSION	1
#define	BRCMF_IOCTL_MAXLEN	8192	/* max length ioctl buffer required */

/* common ioctl definitions */
#define BRCMF_GET_MAGIC				0
#define BRCMF_GET_VERSION				1
#define BRCMF_GET_VAR				2
#define BRCMF_SET_VAR				3

/* message levels */
#define BRCMF_ERROR_VAL	0x0001
#define BRCMF_TRACE_VAL	0x0002
#define BRCMF_INFO_VAL	0x0004
#define BRCMF_DATA_VAL	0x0008
#define BRCMF_CTL_VAL	0x0010
#define BRCMF_TIMER_VAL	0x0020
#define BRCMF_HDRS_VAL	0x0040
#define BRCMF_BYTES_VAL	0x0080
#define BRCMF_INTR_VAL	0x0100
#define BRCMF_GLOM_VAL	0x0400
#define BRCMF_EVENT_VAL	0x0800
#define BRCMF_BTA_VAL	0x1000
#define BRCMF_ISCAN_VAL 0x2000

#ifdef SDTEST
/* For pktgen iovar */
struct brcmf_pktgen {
	uint version;		/* To allow structure change tracking */
	uint freq;		/* Max ticks between tx/rx attempts */
	uint count;		/* Test packets to send/rcv each attempt */
	uint print;		/* Print counts every <print> attempts */
	uint total;		/* Total packets (or bursts) */
	uint minlen;		/* Minimum length of packets to send */
	uint maxlen;		/* Maximum length of packets to send */
	uint numsent;		/* Count of test packets sent */
	uint numrcvd;		/* Count of test packets received */
	uint numfail;		/* Count of test send failures */
	uint mode;		/* Test mode (type of test packets) */
	uint stop;		/* Stop after this many tx failures */
};

/* Version in case structure changes */
#define BRCMF_PKTGEN_VERSION	2

/* Type of test packets to use */
#define BRCMF_PKTGEN_ECHO	1	/* Send echo requests */
#define BRCMF_PKTGEN_SEND	2	/* Send discard packets */
#define BRCMF_PKTGEN_RXBURST	3	/* Request dongle send N packets */
#define BRCMF_PKTGEN_RECV		4	/* Continuous rx from continuous
					 tx dongle */
#endif				/* SDTEST */

/* Enter idle immediately (no timeout) */
#define BRCMF_IDLE_IMMEDIATE	(-1)

/* Values for idleclock iovar: other values are the sd_divisor to use
	 when idle */
#define BRCMF_IDLE_ACTIVE	0	/* Do not request any SD clock change
				 when idle */

#endif				/* _BRCMF_H_ */
