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

#include "fweh.h"

/*******************************************************************************
 * IO codes that are interpreted by dongle firmware
 ******************************************************************************/
#define BRCMF_C_UP				2
#define BRCMF_C_DOWN				3
#define BRCMF_C_SET_PROMISC			10
#define BRCMF_C_GET_RATE			12
#define BRCMF_C_GET_INFRA			19
#define BRCMF_C_SET_INFRA			20
#define BRCMF_C_GET_AUTH			21
#define BRCMF_C_SET_AUTH			22
#define BRCMF_C_GET_BSSID			23
#define BRCMF_C_GET_SSID			25
#define BRCMF_C_SET_SSID			26
#define BRCMF_C_TERMINATED			28
#define BRCMF_C_GET_CHANNEL			29
#define BRCMF_C_SET_CHANNEL			30
#define BRCMF_C_GET_SRL				31
#define BRCMF_C_SET_SRL				32
#define BRCMF_C_GET_LRL				33
#define BRCMF_C_SET_LRL				34
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
#define BRCMF_C_GET_BCNPRD			75
#define BRCMF_C_SET_BCNPRD			76
#define BRCMF_C_GET_DTIMPRD			77
#define BRCMF_C_SET_DTIMPRD			78
#define BRCMF_C_SET_COUNTRY			84
#define BRCMF_C_GET_PM				85
#define BRCMF_C_SET_PM				86
#define BRCMF_C_GET_CURR_RATESET		114
#define BRCMF_C_GET_AP				117
#define BRCMF_C_SET_AP				118
#define BRCMF_C_GET_RSSI			127
#define BRCMF_C_GET_WSEC			133
#define BRCMF_C_SET_WSEC			134
#define BRCMF_C_GET_PHY_NOISE			135
#define BRCMF_C_GET_BSS_INFO			136
#define BRCMF_C_SET_SCB_TIMEOUT			158
#define BRCMF_C_GET_PHYLIST			180
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

#define BRCMF_EVENTING_MASK_LEN	16

#define TOE_TX_CSUM_OL		0x00000001
#define TOE_RX_CSUM_OL		0x00000002

#define	BRCMF_BSS_INFO_VERSION	109 /* curr ver of brcmf_bss_info_le struct */

/* size of brcmf_scan_params not including variable length array */
#define BRCMF_SCAN_PARAMS_FIXED_SIZE 64

/* masks for channel and ssid count */
#define BRCMF_SCAN_PARAMS_COUNT_MASK 0x0000ffff
#define BRCMF_SCAN_PARAMS_NSSID_SHIFT 16

/* primary (ie tx) key */
#define BRCMF_PRIMARY_KEY	(1 << 1)

/* For supporting multiple interfaces */
#define BRCMF_MAX_IFS	16

#define DOT11_BSSTYPE_ANY			2
#define DOT11_MAX_DEFAULT_KEYS	4

#define BRCMF_ESCAN_REQ_VERSION 1

#define WLC_BSS_RSSI_ON_CHANNEL		0x0002

#define BRCMF_MAXRATES_IN_SET		16	/* max # of rates in rateset */
#define BRCMF_STA_ASSOC			0x10		/* Associated */

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

#define BRCMF_E_REASON_LINK_BSSCFG_DIS		4
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

/* Small, medium and maximum buffer size for dcmd
 */
#define BRCMF_DCMD_SMLEN	256
#define BRCMF_DCMD_MEDLEN	1536
#define BRCMF_DCMD_MAXLEN	8192

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
	u8 basic_mcs[MCSSET_LEN];	/* 802.11N BSS required MCS set */

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

struct brcmf_ssid {
	u32 SSID_len;
	unsigned char SSID[32];
};

struct brcmf_ssid_le {
	__le32 SSID_len;
	unsigned char SSID[32];
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
	__le16 channel_list[1];	/* list of chanspecs */
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
	struct brcmf_scan_params_le params_le;
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
	__le16	ver;		/* version of this struct */
	__le16	len;		/* length in bytes of this structure */
	__le16	cap;		/* sta's advertised capabilities */
	__le32	flags;		/* flags defined below */
	__le32	idle;		/* time since data pkt rx'd from sta */
	u8	ea[ETH_ALEN];		/* Station address */
	__le32	count;			/* # rates in this set */
	u8	rates[BRCMF_MAXRATES_IN_SET];	/* rates in 500kbps units */
						/* w/hi bit set if basic */
	__le32	in;		/* seconds elapsed since associated */
	__le32	listen_interval_inms; /* Min Listen interval in ms for STA */
	__le32	tx_pkts;	/* # of packets transmitted */
	__le32	tx_failures;	/* # of packets failed */
	__le32	rx_ucast_pkts;	/* # of unicast packets received */
	__le32	rx_mcast_pkts;	/* # of multicast packets received */
	__le32	tx_rate;	/* Rate of last successful tx frame */
	__le32	rx_rate;	/* Rate of last successful rx frame */
	__le32	rx_decrypt_succeeds;	/* # of packet decrypted successfully */
	__le32	rx_decrypt_failures;	/* # of packet decrypted failed */
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

/* Bus independent dongle command */
struct brcmf_dcmd {
	uint cmd;		/* common dongle cmd definition */
	void *buf;		/* pointer to user buffer */
	uint len;		/* length of user buffer */
	u8 set;			/* get or set request (optional) */
	uint used;		/* bytes read or written (optional) */
	uint needed;		/* bytes needed (optional) */
};

/* Forward decls for struct brcmf_pub (see below) */
struct brcmf_proto;	/* device communication protocol info */
struct brcmf_cfg80211_dev; /* cfg80211 device info */

/* Common structure for module and instance linkage */
struct brcmf_pub {
	/* Linkage ponters */
	struct brcmf_bus *bus_if;
	struct brcmf_proto *prot;
	struct brcmf_cfg80211_info *config;

	/* Internal brcmf items */
	uint hdrlen;		/* Total BRCMF header length (proto + bus) */
	uint rxsz;		/* Rx buffer size bus module should use */
	u8 wme_dp;		/* wme discard priority */

	/* Dongle media info */
	unsigned long drv_version;	/* Version of dongle-resident driver */
	u8 mac[ETH_ALEN];		/* MAC address obtained from dongle */

	/* Multicast data packets sent to dongle */
	unsigned long tx_multicast;

	struct brcmf_if *iflist[BRCMF_MAX_IFS];

	struct mutex proto_block;
	unsigned char proto_buf[BRCMF_DCMD_MAXLEN];

	struct brcmf_fweh_info fweh;
#ifdef DEBUG
	struct dentry *dbgfs_dir;
#endif
};

struct brcmf_if_event {
	u8 ifidx;
	u8 action;
	u8 flags;
	u8 bssidx;
};

/* forward declaration */
struct brcmf_cfg80211_vif;

/**
 * struct brcmf_if - interface control information.
 *
 * @drvr: points to device related information.
 * @vif: points to cfg80211 specific interface information.
 * @ndev: associated network device.
 * @stats: interface specific network statistics.
 * @ifidx: interface index in device firmware.
 * @bssidx: index of bss associated with this interface.
 * @mac_addr: assigned mac address.
 * @pend_8021x_cnt: tracks outstanding number of 802.1x frames.
 * @pend_8021x_wait: used for signalling change in count.
 */
struct brcmf_if {
	struct brcmf_pub *drvr;
	struct brcmf_cfg80211_vif *vif;
	struct net_device *ndev;
	struct net_device_stats stats;
	struct work_struct setmacaddr_work;
	struct work_struct multicast_work;
	int ifidx;
	s32 bssidx;
	u8 mac_addr[ETH_ALEN];
	atomic_t pend_8021x_cnt;
	wait_queue_head_t pend_8021x_wait;
};


extern int brcmf_netdev_wait_pend8021x(struct net_device *ndev);

/* Return pointer to interface name */
extern char *brcmf_ifname(struct brcmf_pub *drvr, int idx);

/* Query dongle */
extern int brcmf_proto_cdc_query_dcmd(struct brcmf_pub *drvr, int ifidx,
				       uint cmd, void *buf, uint len);
extern int brcmf_proto_cdc_set_dcmd(struct brcmf_pub *drvr, int ifidx, uint cmd,
				    void *buf, uint len);

/* Remove any protocol-specific data header. */
extern int brcmf_proto_hdrpull(struct brcmf_pub *drvr, u8 *ifidx,
			       struct sk_buff *rxp);

extern int brcmf_net_attach(struct brcmf_if *ifp);
extern struct brcmf_if *brcmf_add_if(struct brcmf_pub *drvr, s32 bssidx,
				     s32 ifidx, char *name, u8 *mac_addr);
extern void brcmf_del_if(struct brcmf_pub *drvr, s32 bssidx);
extern u32 brcmf_get_chip_info(struct brcmf_if *ifp);

#endif				/* _BRCMF_H_ */
