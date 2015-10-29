/*
 * Copyright (c) 2012 Broadcom Corporation
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
#define BRCMF_BSS_RSSI_ON_CHANNEL	0x0002

#define BRCMF_STA_WME              0x00000002      /* WMM association */
#define BRCMF_STA_AUTHE            0x00000008      /* Authenticated */
#define BRCMF_STA_ASSOC            0x00000010      /* Associated */
#define BRCMF_STA_AUTHO            0x00000020      /* Authorized */
#define BRCMF_STA_SCBSTATS         0x00004000      /* Per STA debug stats */

/* size of brcmf_scan_params not including variable length array */
#define BRCMF_SCAN_PARAMS_FIXED_SIZE	64

/* masks for channel and ssid count */
#define BRCMF_SCAN_PARAMS_COUNT_MASK	0x0000ffff
#define BRCMF_SCAN_PARAMS_NSSID_SHIFT	16

/* primary (ie tx) key */
#define BRCMF_PRIMARY_KEY		(1 << 1)
#define DOT11_BSSTYPE_ANY		2
#define BRCMF_ESCAN_REQ_VERSION		1

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
/* Link Down indication in WoWL mode: */
#define BRCMF_WOWL_LINKDOWN		(1 << 31)

#define BRCMF_WOWL_MAXPATTERNS		8
#define BRCMF_WOWL_MAXPATTERNSIZE	128

#define BRCMF_COUNTRY_BUF_SZ		4
#define BRCMF_ANT_MAX			4

#define BRCMF_MAX_ASSOCLIST		128

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
	__le32 tx_pkts;	/* # of packets transmitted */
	__le32 tx_failures;	/* # of packets failed */
	__le32 rx_ucast_pkts;	/* # of unicast packets received */
	__le32 rx_mcast_pkts;	/* # of multicast packets received */
	__le32 tx_rate;	/* Rate of last successful tx frame */
	__le32 rx_rate;	/* Rate of last successful rx frame */
	__le32 rx_decrypt_succeeds;	/* # of packet decrypted successfully */
	__le32 rx_decrypt_failures;	/* # of packet decrypted failed */
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
};

struct brcmf_chanspec_list {
	__le32	count;		/* # of entries */
	__le32	element[1];	/* variable length uint32 list */
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
 * struct brcmf_assoclist_le - request assoc list.
 *
 * @count: indicates number of stations.
 * @mac: MAC addresses of stations.
 */
struct brcmf_assoclist_le {
	__le32 count;
	u8 mac[BRCMF_MAX_ASSOCLIST][ETH_ALEN];
};

#endif /* FWIL_TYPES_H_ */
