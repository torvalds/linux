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

#ifndef _wlioctl_h_
#define	_wlioctl_h_

#include <typedefs.h>
#include <proto/ethernet.h>
#ifdef BRCM_FULLMAC
#include <proto/bcmeth.h>
#endif
#include <proto/bcmevent.h>
#include <proto/802.11.h>
#include <bcmwifi.h>

#ifndef INTF_NAME_SIZ
#define INTF_NAME_SIZ	16
#endif

/* require default structure packing */
#define BWL_DEFAULT_PACKING
#include <packed_section_start.h>

/* Legacy structure to help keep backward compatible wl tool and tray app */

#define	LEGACY_WL_BSS_INFO_VERSION	107	/* older version of wl_bss_info struct */

typedef struct wl_bss_info_107 {
	uint32 version;		/* version field */
	uint32 length;		/* byte length of data in this record,
				 * starting at version and including IEs
				 */
	struct ether_addr BSSID;
	uint16 beacon_period;	/* units are Kusec */
	uint16 capability;	/* Capability information */
	u8 SSID_len;
	u8 SSID[32];
	struct {
		uint count;	/* # rates in this set */
		u8 rates[16];	/* rates in 500kbps units w/hi bit set if basic */
	} rateset;		/* supported rates */
	u8 channel;		/* Channel no. */
	uint16 atim_window;	/* units are Kusec */
	u8 dtim_period;	/* DTIM period */
	int16 RSSI;		/* receive signal strength (in dBm) */
	int8 phy_noise;		/* noise (in dBm) */
	uint32 ie_length;	/* byte length of Information Elements */
	/* variable length Information Elements */
} wl_bss_info_107_t;

/*
 * Per-BSS information structure.
 */

#define	LEGACY2_WL_BSS_INFO_VERSION	108	/* old version of wl_bss_info struct */

/* BSS info structure
 * Applications MUST CHECK ie_offset field and length field to access IEs and
 * next bss_info structure in a vector (in wl_scan_results_t)
 */
typedef struct wl_bss_info_108 {
	uint32 version;		/* version field */
	uint32 length;		/* byte length of data in this record,
				 * starting at version and including IEs
				 */
	struct ether_addr BSSID;
	uint16 beacon_period;	/* units are Kusec */
	uint16 capability;	/* Capability information */
	u8 SSID_len;
	u8 SSID[32];
	struct {
		uint count;	/* # rates in this set */
		u8 rates[16];	/* rates in 500kbps units w/hi bit set if basic */
	} rateset;		/* supported rates */
	chanspec_t chanspec;	/* chanspec for bss */
	uint16 atim_window;	/* units are Kusec */
	u8 dtim_period;	/* DTIM period */
	int16 RSSI;		/* receive signal strength (in dBm) */
	int8 phy_noise;		/* noise (in dBm) */

	u8 n_cap;		/* BSS is 802.11N Capable */
	uint32 nbss_cap;	/* 802.11N BSS Capabilities (based on HT_CAP_*) */
	u8 ctl_ch;		/* 802.11N BSS control channel number */
	uint32 reserved32[1];	/* Reserved for expansion of BSS properties */
	u8 flags;		/* flags */
	u8 reserved[3];	/* Reserved for expansion of BSS properties */
	u8 basic_mcs[MCSSET_LEN];	/* 802.11N BSS required MCS set */

	uint16 ie_offset;	/* offset at which IEs start, from beginning */
	uint32 ie_length;	/* byte length of Information Elements */
	/* Add new fields here */
	/* variable length Information Elements */
} wl_bss_info_108_t;

#ifdef BRCM_FULLMAC
#define	WL_BSS_INFO_VERSION	108	/* current ver of wl_bss_info struct */
#else
#define	WL_BSS_INFO_VERSION	109	/* current ver of wl_bss_info struct */
#endif

/* BSS info structure
 * Applications MUST CHECK ie_offset field and length field to access IEs and
 * next bss_info structure in a vector (in wl_scan_results_t)
 */
typedef struct wl_bss_info {
	uint32 version;		/* version field */
	uint32 length;		/* byte length of data in this record,
				 * starting at version and including IEs
				 */
	struct ether_addr BSSID;
	uint16 beacon_period;	/* units are Kusec */
	uint16 capability;	/* Capability information */
	u8 SSID_len;
	u8 SSID[32];
	struct {
		uint count;	/* # rates in this set */
		u8 rates[16];	/* rates in 500kbps units w/hi bit set if basic */
	} rateset;		/* supported rates */
	chanspec_t chanspec;	/* chanspec for bss */
	uint16 atim_window;	/* units are Kusec */
	u8 dtim_period;	/* DTIM period */
	int16 RSSI;		/* receive signal strength (in dBm) */
	int8 phy_noise;		/* noise (in dBm) */

	u8 n_cap;		/* BSS is 802.11N Capable */
	uint32 nbss_cap;	/* 802.11N BSS Capabilities (based on HT_CAP_*) */
	u8 ctl_ch;		/* 802.11N BSS control channel number */
	uint32 reserved32[1];	/* Reserved for expansion of BSS properties */
	u8 flags;		/* flags */
	u8 reserved[3];	/* Reserved for expansion of BSS properties */
	u8 basic_mcs[MCSSET_LEN];	/* 802.11N BSS required MCS set */

	uint16 ie_offset;	/* offset at which IEs start, from beginning */
	uint32 ie_length;	/* byte length of Information Elements */
	int16 SNR;		/* average SNR of during frame reception */
	/* Add new fields here */
	/* variable length Information Elements */
} wl_bss_info_t;

typedef struct wlc_ssid {
	uint32 SSID_len;
	uchar SSID[32];
} wlc_ssid_t;

typedef struct chan_scandata {
	u8 txpower;
	u8 pad;
	chanspec_t channel;	/* Channel num, bw, ctrl_sb and band */
	uint32 channel_mintime;
	uint32 channel_maxtime;
} chan_scandata_t;

typedef enum wl_scan_type {
	EXTDSCAN_FOREGROUND_SCAN,
	EXTDSCAN_BACKGROUND_SCAN,
	EXTDSCAN_FORCEDBACKGROUND_SCAN
} wl_scan_type_t;

#define WLC_EXTDSCAN_MAX_SSID		5

#define WL_BSS_FLAGS_FROM_BEACON	0x01	/* bss_info derived from beacon */
#define WL_BSS_FLAGS_FROM_CACHE		0x02	/* bss_info collected from cache */
#define WL_BSS_FLAGS_RSSI_ONCHANNEL     0x04	/* rssi info was received on channel (vs offchannel) */

typedef struct wl_extdscan_params {
	int8 nprobes;		/* 0, passive, otherwise active */
	int8 split_scan;	/* split scan */
	int8 band;		/* band */
	int8 pad;
	wlc_ssid_t ssid[WLC_EXTDSCAN_MAX_SSID];	/* ssid list */
	uint32 tx_rate;		/* in 500ksec units */
	wl_scan_type_t scan_type;	/* enum */
	int32 channel_num;
	chan_scandata_t channel_list[1];	/* list of chandata structs */
} wl_extdscan_params_t;

#define WL_EXTDSCAN_PARAMS_FIXED_SIZE 	(sizeof(wl_extdscan_params_t) - sizeof(chan_scandata_t))

#define WL_BSSTYPE_INFRA 1
#define WL_BSSTYPE_INDEP 0
#define WL_BSSTYPE_ANY   2

/* Bitmask for scan_type */
#define WL_SCANFLAGS_PASSIVE 0x01	/* force passive scan */
#define WL_SCANFLAGS_RESERVED 0x02	/* Reserved */
#define WL_SCANFLAGS_PROHIBITED 0x04	/* allow scanning prohibited channels */

typedef struct wl_scan_params {
	wlc_ssid_t ssid;	/* default: {0, ""} */
	struct ether_addr bssid;	/* default: bcast */
	int8 bss_type;		/* default: any,
				 * DOT11_BSSTYPE_ANY/INFRASTRUCTURE/INDEPENDENT
				 */
	u8 scan_type;	/* flags, 0 use default */
	int32 nprobes;		/* -1 use default, number of probes per channel */
	int32 active_time;	/* -1 use default, dwell time per channel for
				 * active scanning
				 */
	int32 passive_time;	/* -1 use default, dwell time per channel
				 * for passive scanning
				 */
	int32 home_time;	/* -1 use default, dwell time for the home channel
				 * between channel scans
				 */
	int32 channel_num;	/* count of channels and ssids that follow
				 *
				 * low half is count of channels in channel_list, 0
				 * means default (use all available channels)
				 *
				 * high half is entries in wlc_ssid_t array that
				 * follows channel_list, aligned for int32 (4 bytes)
				 * meaning an odd channel count implies a 2-byte pad
				 * between end of channel_list and first ssid
				 *
				 * if ssid count is zero, single ssid in the fixed
				 * parameter portion is assumed, otherwise ssid in
				 * the fixed portion is ignored
				 */
	uint16 channel_list[1];	/* list of chanspecs */
} wl_scan_params_t;

/* size of wl_scan_params not including variable length array */
#define WL_SCAN_PARAMS_FIXED_SIZE 64

/* masks for channel and ssid count */
#define WL_SCAN_PARAMS_COUNT_MASK 0x0000ffff
#define WL_SCAN_PARAMS_NSSID_SHIFT 16

#define WL_SCAN_ACTION_START      1
#define WL_SCAN_ACTION_CONTINUE   2
#define WL_SCAN_ACTION_ABORT      3

#define ISCAN_REQ_VERSION 1

/* incremental scan struct */
typedef struct wl_iscan_params {
	uint32 version;
	uint16 action;
	uint16 scan_duration;
	wl_scan_params_t params;
} wl_iscan_params_t;

/* 3 fields + size of wl_scan_params, not including variable length array */
#define WL_ISCAN_PARAMS_FIXED_SIZE (OFFSETOF(wl_iscan_params_t, params) + sizeof(wlc_ssid_t))

typedef struct wl_scan_results {
	uint32 buflen;
	uint32 version;
	uint32 count;
	wl_bss_info_t bss_info[1];
} wl_scan_results_t;

/* size of wl_scan_results not including variable length array */
#define WL_SCAN_RESULTS_FIXED_SIZE (sizeof(wl_scan_results_t) - sizeof(wl_bss_info_t))

/* wl_iscan_results status values */
#define WL_SCAN_RESULTS_SUCCESS	0
#define WL_SCAN_RESULTS_PARTIAL	1
#define WL_SCAN_RESULTS_PENDING	2
#define WL_SCAN_RESULTS_ABORTED	3
#define WL_SCAN_RESULTS_NO_MEM	4

#define ESCAN_REQ_VERSION 1

typedef struct wl_escan_params {
	uint32 version;
	uint16 action;
	uint16 sync_id;
	wl_scan_params_t params;
} wl_escan_params_t;

#define WL_ESCAN_PARAMS_FIXED_SIZE (OFFSETOF(wl_escan_params_t, params) + sizeof(wlc_ssid_t))

typedef struct wl_escan_result {
	uint32 buflen;
	uint32 version;
	uint16 sync_id;
	uint16 bss_count;
	wl_bss_info_t bss_info[1];
} wl_escan_result_t;

#define WL_ESCAN_RESULTS_FIXED_SIZE (sizeof(wl_escan_result_t) - sizeof(wl_bss_info_t))

/* incremental scan results struct */
typedef struct wl_iscan_results {
	uint32 status;
	wl_scan_results_t results;
} wl_iscan_results_t;

/* size of wl_iscan_results not including variable length array */
#define WL_ISCAN_RESULTS_FIXED_SIZE \
	(WL_SCAN_RESULTS_FIXED_SIZE + OFFSETOF(wl_iscan_results_t, results))

typedef struct wl_probe_params {
	wlc_ssid_t ssid;
	struct ether_addr bssid;
	struct ether_addr mac;
} wl_probe_params_t;

#define WL_NUMRATES		16	/* max # of rates in a rateset */
typedef struct wl_rateset {
	uint32 count;		/* # rates in this set */
	u8 rates[WL_NUMRATES];	/* rates in 500kbps units w/hi bit set if basic */
} wl_rateset_t;

typedef struct wl_rateset_args {
	uint32 count;		/* # rates in this set */
	u8 rates[WL_NUMRATES];	/* rates in 500kbps units w/hi bit set if basic */
	u8 mcs[MCSSET_LEN];	/* supported mcs index bit map */
} wl_rateset_args_t;

/* uint32 list */
typedef struct wl_uint32_list {
	/* in - # of elements, out - # of entries */
	uint32 count;
	/* variable length uint32 list */
	uint32 element[1];
} wl_uint32_list_t;

/* used for association with a specific BSSID and chanspec list */
typedef struct wl_assoc_params {
	struct ether_addr bssid;	/* 00:00:00:00:00:00: broadcast scan */
	int32 chanspec_num;	/* 0: all available channels,
				 * otherwise count of chanspecs in chanspec_list
				 */
	chanspec_t chanspec_list[1];	/* list of chanspecs */
} wl_assoc_params_t;
#define WL_ASSOC_PARAMS_FIXED_SIZE 	(sizeof(wl_assoc_params_t) - sizeof(chanspec_t))

/* used for reassociation/roam to a specific BSSID and channel */
typedef wl_assoc_params_t wl_reassoc_params_t;
#define WL_REASSOC_PARAMS_FIXED_SIZE	WL_ASSOC_PARAMS_FIXED_SIZE

/* used for join with or without a specific bssid and channel list */
typedef struct wl_join_params {
	wlc_ssid_t ssid;
	wl_assoc_params_t params;	/* optional field, but it must include the fixed portion
					 * of the wl_assoc_params_t struct when it does present.
					 */
} wl_join_params_t;
#define WL_JOIN_PARAMS_FIXED_SIZE 	(sizeof(wl_join_params_t) - sizeof(chanspec_t))

/* defines used by the nrate iovar */
#define NRATE_MCS_INUSE	0x00000080	/* MSC in use,indicates b0-6 holds an mcs */
#define NRATE_RATE_MASK 0x0000007f	/* rate/mcs value */
#define NRATE_STF_MASK	0x0000ff00	/* stf mode mask: siso, cdd, stbc, sdm */
#define NRATE_STF_SHIFT	8	/* stf mode shift */
#define NRATE_OVERRIDE	0x80000000	/* bit indicates override both rate & mode */
#define NRATE_OVERRIDE_MCS_ONLY 0x40000000	/* bit indicate to override mcs only */
#define NRATE_SGI_MASK  0x00800000	/* sgi mode */
#define NRATE_SGI_SHIFT 23	/* sgi mode */
#define NRATE_LDPC_CODING 0x00400000	/* bit indicates adv coding in use */
#define NRATE_LDPC_SHIFT 22	/* ldpc shift */

#define NRATE_STF_SISO	0	/* stf mode SISO */
#define NRATE_STF_CDD	1	/* stf mode CDD */
#define NRATE_STF_STBC	2	/* stf mode STBC */
#define NRATE_STF_SDM	3	/* stf mode SDM */

#define ANTENNA_NUM_1	1	/* total number of antennas to be used */
#define ANTENNA_NUM_2	2
#define ANTENNA_NUM_3	3
#define ANTENNA_NUM_4	4

#define ANT_SELCFG_AUTO		0x80	/* bit indicates antenna sel AUTO */
#define ANT_SELCFG_MASK		0x33	/* antenna configuration mask */
#define ANT_SELCFG_MAX		4	/* max number of antenna configurations */
#define ANT_SELCFG_TX_UNICAST	0	/* unicast tx antenna configuration */
#define ANT_SELCFG_RX_UNICAST	1	/* unicast rx antenna configuration */
#define ANT_SELCFG_TX_DEF	2	/* default tx antenna configuration */
#define ANT_SELCFG_RX_DEF	3	/* default rx antenna configuration */

#define MAX_STREAMS_SUPPORTED	4	/* max number of streams supported */

typedef struct {
	u8 ant_config[ANT_SELCFG_MAX];	/* antenna configuration */
	u8 num_antcfg;	/* number of available antenna configurations */
} wlc_antselcfg_t;

#define HIGHEST_SINGLE_STREAM_MCS	7	/* MCS values greater than this enable multiple streams */

#define MAX_CCA_CHANNELS 38	/* Max number of 20 Mhz wide channels */
#define MAX_CCA_SECS     60	/* CCA keeps this many seconds history */

#define IBSS_MED        15	/* Mediom in-bss congestion percentage */
#define IBSS_HI         25	/* Hi in-bss congestion percentage */
#define OBSS_MED        12
#define OBSS_HI         25
#define INTERFER_MED    5
#define INTERFER_HI     10

#define  CCA_FLAG_2G_ONLY		0x01	/* Return a channel from 2.4 Ghz band */
#define  CCA_FLAG_5G_ONLY		0x02	/* Return a channel from 2.4 Ghz band */
#define  CCA_FLAG_IGNORE_DURATION	0x04	/* Ignore dwell time for each channel */
#define  CCA_FLAGS_PREFER_1_6_11	0x10
#define  CCA_FLAG_IGNORE_INTERFER 	0x20	/* do not exlude channel based on interfer level */

#define CCA_ERRNO_BAND 		1	/* After filtering for band pref, no choices left */
#define CCA_ERRNO_DURATION	2	/* After filtering for duration, no choices left */
#define CCA_ERRNO_PREF_CHAN	3	/* After filtering for chan pref, no choices left */
#define CCA_ERRNO_INTERFER	4	/* After filtering for interference, no choices left */
#define CCA_ERRNO_TOO_FEW	5	/* Only 1 channel was input */

typedef struct {
	uint32 duration;	/* millisecs spent sampling this channel */
	uint32 congest_ibss;	/* millisecs in our bss (presumably this traffic will */
	/*  move if cur bss moves channels) */
	uint32 congest_obss;	/* traffic not in our bss */
	uint32 interference;	/* millisecs detecting a non 802.11 interferer. */
	uint32 timestamp;	/* second timestamp */
} cca_congest_t;

typedef struct {
	chanspec_t chanspec;	/* Which channel? */
	u8 num_secs;		/* How many secs worth of data */
	cca_congest_t secs[1];	/* Data */
} cca_congest_channel_req_t;

#define WLC_CNTRY_BUF_SZ	4	/* Country string is 3 bytes + NUL */

typedef struct wl_country {
	char country_abbrev[WLC_CNTRY_BUF_SZ];	/* nul-terminated country code used in
						 * the Country IE
						 */
	int32 rev;		/* revision specifier for ccode
				 * on set, -1 indicates unspecified.
				 * on get, rev >= 0
				 */
	char ccode[WLC_CNTRY_BUF_SZ];	/* nul-terminated built-in country code.
					 * variable length, but fixed size in
					 * struct allows simple allocation for
					 * expected country strings <= 3 chars.
					 */
} wl_country_t;

typedef struct wl_channels_in_country {
	uint32 buflen;
	uint32 band;
	char country_abbrev[WLC_CNTRY_BUF_SZ];
	uint32 count;
	uint32 channel[1];
} wl_channels_in_country_t;

typedef struct wl_country_list {
	uint32 buflen;
	uint32 band_set;
	uint32 band;
	uint32 count;
	char country_abbrev[1];
} wl_country_list_t;

#define WL_NUM_RPI_BINS		8
#define WL_RM_TYPE_BASIC	1
#define WL_RM_TYPE_CCA		2
#define WL_RM_TYPE_RPI		3

#define WL_RM_FLAG_PARALLEL	(1<<0)

#define WL_RM_FLAG_LATE		(1<<1)
#define WL_RM_FLAG_INCAPABLE	(1<<2)
#define WL_RM_FLAG_REFUSED	(1<<3)

typedef struct wl_rm_req_elt {
	int8 type;
	int8 flags;
	chanspec_t chanspec;
	uint32 token;		/* token for this measurement */
	uint32 tsf_h;		/* TSF high 32-bits of Measurement start time */
	uint32 tsf_l;		/* TSF low 32-bits */
	uint32 dur;		/* TUs */
} wl_rm_req_elt_t;

typedef struct wl_rm_req {
	uint32 token;		/* overall measurement set token */
	uint32 count;		/* number of measurement requests */
	void *cb;		/* completion callback function: may be NULL */
	void *cb_arg;		/* arg to completion callback function */
	wl_rm_req_elt_t req[1];	/* variable length block of requests */
} wl_rm_req_t;
#define WL_RM_REQ_FIXED_LEN	OFFSETOF(wl_rm_req_t, req)

typedef struct wl_rm_rep_elt {
	int8 type;
	int8 flags;
	chanspec_t chanspec;
	uint32 token;		/* token for this measurement */
	uint32 tsf_h;		/* TSF high 32-bits of Measurement start time */
	uint32 tsf_l;		/* TSF low 32-bits */
	uint32 dur;		/* TUs */
	uint32 len;		/* byte length of data block */
	u8 data[1];		/* variable length data block */
} wl_rm_rep_elt_t;
#define WL_RM_REP_ELT_FIXED_LEN	24	/* length excluding data block */

#define WL_RPI_REP_BIN_NUM 8
typedef struct wl_rm_rpi_rep {
	u8 rpi[WL_RPI_REP_BIN_NUM];
	int8 rpi_max[WL_RPI_REP_BIN_NUM];
} wl_rm_rpi_rep_t;

typedef struct wl_rm_rep {
	uint32 token;		/* overall measurement set token */
	uint32 len;		/* length of measurement report block */
	wl_rm_rep_elt_t rep[1];	/* variable length block of reports */
} wl_rm_rep_t;
#define WL_RM_REP_FIXED_LEN	8

/* Enumerate crypto algorithms */
#define	CRYPTO_ALGO_OFF			0
#define	CRYPTO_ALGO_WEP1		1
#define	CRYPTO_ALGO_TKIP		2
#define	CRYPTO_ALGO_WEP128		3
#define CRYPTO_ALGO_AES_CCM		4
#define CRYPTO_ALGO_AES_RESERVED1	5
#define CRYPTO_ALGO_AES_RESERVED2	6
#define CRYPTO_ALGO_NALG		7

#define WSEC_GEN_MIC_ERROR	0x0001
#define WSEC_GEN_REPLAY		0x0002
#define WSEC_GEN_ICV_ERROR	0x0004

#define WL_SOFT_KEY	(1 << 0)	/* Indicates this key is using soft encrypt */
#define WL_PRIMARY_KEY	(1 << 1)	/* Indicates this key is the primary (ie tx) key */
#define WL_KF_RES_4	(1 << 4)	/* Reserved for backward compat */
#define WL_KF_RES_5	(1 << 5)	/* Reserved for backward compat */
#define WL_IBSS_PEER_GROUP_KEY	(1 << 6)	/* Indicates a group key for a IBSS PEER */

typedef struct wl_wsec_key {
	uint32 index;		/* key index */
	uint32 len;		/* key length */
	u8 data[DOT11_MAX_KEY_SIZE];	/* key data */
	uint32 pad_1[18];
	uint32 algo;		/* CRYPTO_ALGO_AES_CCM, CRYPTO_ALGO_WEP128, etc */
	uint32 flags;		/* misc flags */
	uint32 pad_2[2];
	int pad_3;
	int iv_initialized;	/* has IV been initialized already? */
	int pad_4;
	/* Rx IV */
	struct {
		uint32 hi;	/* upper 32 bits of IV */
		uint16 lo;	/* lower 16 bits of IV */
	} rxiv;
	uint32 pad_5[2];
	struct ether_addr ea;	/* per station */
} wl_wsec_key_t;

#define WSEC_MIN_PSK_LEN	8
#define WSEC_MAX_PSK_LEN	64

/* Flag for key material needing passhash'ing */
#define WSEC_PASSPHRASE		(1<<0)

/* receptacle for WLC_SET_WSEC_PMK parameter */
typedef struct {
	ushort key_len;		/* octets in key material */
	ushort flags;		/* key handling qualification */
	u8 key[WSEC_MAX_PSK_LEN];	/* PMK material */
} wsec_pmk_t;

/* wireless security bitvec */
#define WEP_ENABLED		0x0001
#define TKIP_ENABLED		0x0002
#define AES_ENABLED		0x0004
#define WSEC_SWFLAG		0x0008
#define SES_OW_ENABLED		0x0040	/* to go into transition mode without setting wep */

/* WPA authentication mode bitvec */
#define WPA_AUTH_DISABLED	0x0000	/* Legacy (i.e., non-WPA) */
#define WPA_AUTH_NONE		0x0001	/* none (IBSS) */
#define WPA_AUTH_UNSPECIFIED	0x0002	/* over 802.1x */
#define WPA_AUTH_PSK		0x0004	/* Pre-shared key */
#define WPA_AUTH_RESERVED1	0x0008
#define WPA_AUTH_RESERVED2	0x0010
					/* #define WPA_AUTH_8021X 0x0020 *//* 802.1x, reserved */
#define WPA2_AUTH_RESERVED1	0x0020
#define WPA2_AUTH_UNSPECIFIED	0x0040	/* over 802.1x */
#define WPA2_AUTH_PSK		0x0080	/* Pre-shared key */
#define WPA2_AUTH_RESERVED3	0x0200
#define WPA2_AUTH_RESERVED4	0x0400
#define WPA2_AUTH_RESERVED5	0x0800

/* pmkid */
#define	MAXPMKID		16

typedef struct _pmkid {
	struct ether_addr BSSID;
	u8 PMKID[WPA2_PMKID_LEN];
} pmkid_t;

typedef struct _pmkid_list {
	uint32 npmkid;
	pmkid_t pmkid[1];
} pmkid_list_t;

typedef struct _pmkid_cand {
	struct ether_addr BSSID;
	u8 preauth;
} pmkid_cand_t;

typedef struct _pmkid_cand_list {
	uint32 npmkid_cand;
	pmkid_cand_t pmkid_cand[1];
} pmkid_cand_list_t;

typedef struct wl_led_info {
	uint32 index;		/* led index */
	uint32 behavior;
	u8 activehi;
} wl_led_info_t;

/* flags */
#define WLC_ASSOC_REQ_IS_REASSOC 0x01	/* assoc req was actually a reassoc */

/* srom read/write struct passed through ioctl */
typedef struct {
	uint byteoff;		/* byte offset */
	uint nbytes;		/* number of bytes */
	uint16 buf[1];
} srom_rw_t;

/* similar cis (srom or otp) struct [iovar: may not be aligned] */
typedef struct {
	uint32 source;		/* cis source */
	uint32 byteoff;		/* byte offset */
	uint32 nbytes;		/* number of bytes */
	/* data follows here */
} cis_rw_t;

#define WLC_CIS_DEFAULT	0	/* built-in default */
#define WLC_CIS_SROM	1	/* source is sprom */
#define WLC_CIS_OTP	2	/* source is otp */

/* R_REG and W_REG struct passed through ioctl */
typedef struct {
	uint32 byteoff;		/* byte offset of the field in d11regs_t */
	uint32 val;		/* read/write value of the field */
	uint32 size;		/* sizeof the field */
	uint band;		/* band (optional) */
} rw_reg_t;

/* Structure used by GET/SET_ATTEN ioctls - it controls power in b/g-band */
/* PCL - Power Control Loop */
/* current gain setting is replaced by user input */
#define WL_ATTEN_APP_INPUT_PCL_OFF	0	/* turn off PCL, apply supplied input */
#define WL_ATTEN_PCL_ON			1	/* turn on PCL */
/* current gain setting is maintained */
#define WL_ATTEN_PCL_OFF		2	/* turn off PCL. */

typedef struct {
	uint16 auto_ctrl;	/* WL_ATTEN_XX */
	uint16 bb;		/* Baseband attenuation */
	uint16 radio;		/* Radio attenuation */
	uint16 txctl1;		/* Radio TX_CTL1 value */
} atten_t;

/* Per-AC retry parameters */
struct wme_tx_params_s {
	u8 short_retry;
	u8 short_fallback;
	u8 long_retry;
	u8 long_fallback;
	uint16 max_rate;	/* In units of 512 Kbps */
};

typedef struct wme_tx_params_s wme_tx_params_t;

#define WL_WME_TX_PARAMS_IO_BYTES (sizeof(wme_tx_params_t) * AC_COUNT)

/* defines used by poweridx iovar - it controls power in a-band */
/* current gain setting is maintained */
#define WL_PWRIDX_PCL_OFF	-2	/* turn off PCL.  */
#define WL_PWRIDX_PCL_ON	-1	/* turn on PCL */
#define WL_PWRIDX_LOWER_LIMIT	-2	/* lower limit */
#define WL_PWRIDX_UPPER_LIMIT	63	/* upper limit */
/* value >= 0 causes
 *	- input to be set to that value
 *	- PCL to be off
 */

/* Used to get specific link/ac parameters */
typedef struct {
	int ac;
	u8 val;
	struct ether_addr ea;
} link_val_t;

#define BCM_MAC_STATUS_INDICATION	(0x40010200L)

typedef struct {
	uint16 ver;		/* version of this struct */
	uint16 len;		/* length in bytes of this structure */
	uint16 cap;		/* sta's advertised capabilities */
	uint32 flags;		/* flags defined below */
	uint32 idle;		/* time since data pkt rx'd from sta */
	struct ether_addr ea;	/* Station address */
	wl_rateset_t rateset;	/* rateset in use */
	uint32 in;		/* seconds elapsed since associated */
	uint32 listen_interval_inms;	/* Min Listen interval in ms for this STA */
	uint32 tx_pkts;		/* # of packets transmitted */
	uint32 tx_failures;	/* # of packets failed */
	uint32 rx_ucast_pkts;	/* # of unicast packets received */
	uint32 rx_mcast_pkts;	/* # of multicast packets received */
	uint32 tx_rate;		/* Rate of last successful tx frame */
	uint32 rx_rate;		/* Rate of last successful rx frame */
	uint32 rx_decrypt_succeeds;	/* # of packet decrypted successfully */
	uint32 rx_decrypt_failures;	/* # of packet decrypted unsuccessfully */
} sta_info_t;

#define WL_OLD_STAINFO_SIZE	OFFSETOF(sta_info_t, tx_pkts)

#define WL_STA_VER		3

/* Flags for sta_info_t indicating properties of STA */
#define WL_STA_BRCM		0x1	/* Running a Broadcom driver */
#define WL_STA_WME		0x2	/* WMM association */
#define WL_STA_ABCAP		0x4
#define WL_STA_AUTHE		0x8	/* Authenticated */
#define WL_STA_ASSOC		0x10	/* Associated */
#define WL_STA_AUTHO		0x20	/* Authorized */
#define WL_STA_WDS		0x40	/* Wireless Distribution System */
#define WL_STA_WDS_LINKUP	0x80	/* WDS traffic/probes flowing properly */
#define WL_STA_PS		0x100	/* STA is in power save mode from AP's viewpoint */
#define WL_STA_APSD_BE		0x200	/* APSD delv/trigger for AC_BE is default enabled */
#define WL_STA_APSD_BK		0x400	/* APSD delv/trigger for AC_BK is default enabled */
#define WL_STA_APSD_VI		0x800	/* APSD delv/trigger for AC_VI is default enabled */
#define WL_STA_APSD_VO		0x1000	/* APSD delv/trigger for AC_VO is default enabled */
#define WL_STA_N_CAP		0x2000	/* STA 802.11n capable */
#define WL_STA_SCBSTATS		0x4000	/* Per STA debug stats */

#define WL_WDS_LINKUP		WL_STA_WDS_LINKUP	/* deprecated */

/* Used to get specific STA parameters */
typedef struct {
	uint32 val;
	struct ether_addr ea;
} scb_val_t;

/* channel encoding */
typedef struct channel_info {
	int hw_channel;
	int target_channel;
	int scan_channel;
} channel_info_t;

/* For ioctls that take a list of MAC addresses */
struct maclist {
	uint count;		/* number of MAC addresses */
	struct ether_addr ea[1];	/* variable length array of MAC addresses */
};

/* get pkt count struct passed through ioctl */
typedef struct get_pktcnt {
	uint rx_good_pkt;
	uint rx_bad_pkt;
	uint tx_good_pkt;
	uint tx_bad_pkt;
	uint rx_ocast_good_pkt;	/* unicast packets destined for others */
} get_pktcnt_t;

/* Linux network driver ioctl encoding */
typedef struct wl_ioctl {
	uint cmd;		/* common ioctl definition */
	void *buf;		/* pointer to user buffer */
	uint len;		/* length of user buffer */
	u8 set;		/* get or set request (optional) */
	uint used;		/* bytes read or written (optional) */
	uint needed;		/* bytes needed (optional) */
} wl_ioctl_t;

/* reference to wl_ioctl_t struct used by usermode driver */
#define ioctl_subtype	set	/* subtype param */
#define ioctl_pid	used	/* pid param */
#define ioctl_status	needed	/* status param */

/*
 * Structure for passing hardware and software
 * revision info up from the driver.
 */
typedef struct wlc_rev_info {
	uint vendorid;		/* PCI vendor id */
	uint deviceid;		/* device id of chip */
	uint radiorev;		/* radio revision */
	uint chiprev;		/* chip revision */
	uint corerev;		/* core revision */
	uint boardid;		/* board identifier (usu. PCI sub-device id) */
	uint boardvendor;	/* board vendor (usu. PCI sub-vendor id) */
	uint boardrev;		/* board revision */
	uint driverrev;		/* driver version */
	uint ucoderev;		/* microcode version */
	uint bus;		/* bus type */
	uint chipnum;		/* chip number */
	uint phytype;		/* phy type */
	uint phyrev;		/* phy revision */
	uint anarev;		/* anacore rev */
	uint chippkg;		/* chip package info */
} wlc_rev_info_t;

#define WL_REV_INFO_LEGACY_LENGTH	48

#define WL_BRAND_MAX 10
typedef struct wl_instance_info {
	uint instance;
	char brand[WL_BRAND_MAX];
} wl_instance_info_t;

/* structure to change size of tx fifo */
typedef struct wl_txfifo_sz {
	uint16 magic;
	uint16 fifo;
	uint16 size;
} wl_txfifo_sz_t;
/* magic pattern used for mismatch driver and wl */
#define WL_TXFIFO_SZ_MAGIC	0xa5a5

/* Transfer info about an IOVar from the driver */
/* Max supported IOV name size in bytes, + 1 for nul termination */
#define WLC_IOV_NAME_LEN 30
typedef struct wlc_iov_trx_s {
	u8 module;
	u8 type;
	char name[WLC_IOV_NAME_LEN];
} wlc_iov_trx_t;

/* check this magic number */
#define WLC_IOCTL_MAGIC		0x14e46c77

#define PROC_ENTRY_NAME "brcm_debug"
/* bump this number if you change the ioctl interface */
#define WLC_IOCTL_VERSION	1

#ifdef BRCM_FULLMAC
#define	WLC_IOCTL_MAXLEN	8192
#else
#define	WLC_IOCTL_MAXLEN		3072	/* max length ioctl buffer required */
#endif
#define	WLC_IOCTL_SMLEN			256	/* "small" length ioctl buffer required */
#define WLC_IOCTL_MEDLEN		1536	/* "med" length ioctl buffer required */
#define WLC_SAMPLECOLLECT_MAXLEN	10240	/* Max Sample Collect buffer for two cores */

/* common ioctl definitions */
#define WLC_GET_MAGIC				0
#define WLC_GET_VERSION				1
#define WLC_UP					2
#define WLC_DOWN				3
#define WLC_GET_LOOP				4
#define WLC_SET_LOOP				5
#define WLC_DUMP				6
#define WLC_GET_MSGLEVEL			7
#define WLC_SET_MSGLEVEL			8
#define WLC_GET_PROMISC				9
#define WLC_SET_PROMISC				10
#define WLC_OVERLAY_IOCTL			11
#define WLC_GET_RATE				12
						      /* #define WLC_SET_RATE				13 *//* no longer supported */
#define WLC_GET_INSTANCE			14
						      /* #define WLC_GET_FRAG				15 *//* no longer supported */
						      /* #define WLC_SET_FRAG				16 *//* no longer supported */
						      /* #define WLC_GET_RTS				17 *//* no longer supported */
						      /* #define WLC_SET_RTS				18 *//* no longer supported */
#define WLC_GET_INFRA				19
#define WLC_SET_INFRA				20
#define WLC_GET_AUTH				21
#define WLC_SET_AUTH				22
#define WLC_GET_BSSID				23
#define WLC_SET_BSSID				24
#define WLC_GET_SSID				25
#define WLC_SET_SSID				26
#define WLC_RESTART				27
						      /* #define WLC_DUMP_SCB				28 *//* no longer supported */
#define WLC_GET_CHANNEL				29
#define WLC_SET_CHANNEL				30
#define WLC_GET_SRL				31
#define WLC_SET_SRL				32
#define WLC_GET_LRL				33
#define WLC_SET_LRL				34
#define WLC_GET_PLCPHDR				35
#define WLC_SET_PLCPHDR				36
#define WLC_GET_RADIO				37
#define WLC_SET_RADIO				38
#define WLC_GET_PHYTYPE				39
#define WLC_DUMP_RATE				40
#define WLC_SET_RATE_PARAMS			41
#define WLC_GET_FIXRATE				42
#define WLC_SET_FIXRATE				43
						      /* #define WLC_GET_WEP				42 *//* no longer supported */
						      /* #define WLC_SET_WEP				43 *//* no longer supported */
#define WLC_GET_KEY				44
#define WLC_SET_KEY				45
#define WLC_GET_REGULATORY			46
#define WLC_SET_REGULATORY			47
#define WLC_GET_PASSIVE_SCAN			48
#define WLC_SET_PASSIVE_SCAN			49
#define WLC_SCAN				50
#define WLC_SCAN_RESULTS			51
#define WLC_DISASSOC				52
#define WLC_REASSOC				53
#define WLC_GET_ROAM_TRIGGER			54
#define WLC_SET_ROAM_TRIGGER			55
#define WLC_GET_ROAM_DELTA			56
#define WLC_SET_ROAM_DELTA			57
#define WLC_GET_ROAM_SCAN_PERIOD		58
#define WLC_SET_ROAM_SCAN_PERIOD		59
#define WLC_EVM					60	/* diag */
#define WLC_GET_TXANT				61
#define WLC_SET_TXANT				62
#define WLC_GET_ANTDIV				63
#define WLC_SET_ANTDIV				64
						      /* #define WLC_GET_TXPWR			65 *//* no longer supported */
						      /* #define WLC_SET_TXPWR			66 *//* no longer supported */
#define WLC_GET_CLOSED				67
#define WLC_SET_CLOSED				68
#define WLC_GET_MACLIST				69
#define WLC_SET_MACLIST				70
#define WLC_GET_RATESET				71
#define WLC_SET_RATESET				72
						      /* #define WLC_GET_LOCALE			73 *//* no longer supported */
#define WLC_LONGTRAIN				74
#define WLC_GET_BCNPRD				75
#define WLC_SET_BCNPRD				76
#define WLC_GET_DTIMPRD				77
#define WLC_SET_DTIMPRD				78
#define WLC_GET_SROM				79
#define WLC_SET_SROM				80
#define WLC_GET_WEP_RESTRICT			81
#define WLC_SET_WEP_RESTRICT			82
#define WLC_GET_COUNTRY				83
#define WLC_SET_COUNTRY				84
#define WLC_GET_PM				85
#define WLC_SET_PM				86
#define WLC_GET_WAKE				87
#define WLC_SET_WAKE				88
						      /* #define WLC_GET_D11CNTS			89 *//* -> "counters" iovar */
#define WLC_GET_FORCELINK			90	/* ndis only */
#define WLC_SET_FORCELINK			91	/* ndis only */
#define WLC_FREQ_ACCURACY			92	/* diag */
#define WLC_CARRIER_SUPPRESS			93	/* diag */
#define WLC_GET_PHYREG				94
#define WLC_SET_PHYREG				95
#define WLC_GET_RADIOREG			96
#define WLC_SET_RADIOREG			97
#define WLC_GET_REVINFO				98
#define WLC_GET_UCANTDIV			99
#define WLC_SET_UCANTDIV			100
#define WLC_R_REG				101
#define WLC_W_REG				102
/* #define WLC_DIAG_LOOPBACK			103	old tray diag */
						       /* #define WLC_RESET_D11CNTS			104 *//* -> "reset_d11cnts" iovar */
#define WLC_GET_MACMODE				105
#define WLC_SET_MACMODE				106
#define WLC_GET_MONITOR				107
#define WLC_SET_MONITOR				108
#define WLC_GET_GMODE				109
#define WLC_SET_GMODE				110
#define WLC_GET_LEGACY_ERP			111
#define WLC_SET_LEGACY_ERP			112
#define WLC_GET_RX_ANT				113
#define WLC_GET_CURR_RATESET			114	/* current rateset */
#define WLC_GET_SCANSUPPRESS			115
#define WLC_SET_SCANSUPPRESS			116
#define WLC_GET_AP				117
#define WLC_SET_AP				118
#define WLC_GET_EAP_RESTRICT			119
#define WLC_SET_EAP_RESTRICT			120
#define WLC_SCB_AUTHORIZE			121
#define WLC_SCB_DEAUTHORIZE			122
#define WLC_GET_WDSLIST				123
#define WLC_SET_WDSLIST				124
#define WLC_GET_ATIM				125
#define WLC_SET_ATIM				126
#define WLC_GET_RSSI				127
#define WLC_GET_PHYANTDIV			128
#define WLC_SET_PHYANTDIV			129
#define WLC_AP_RX_ONLY				130
#define WLC_GET_TX_PATH_PWR			131
#define WLC_SET_TX_PATH_PWR			132
#define WLC_GET_WSEC				133
#define WLC_SET_WSEC				134
#define WLC_GET_PHY_NOISE			135
#define WLC_GET_BSS_INFO			136
#define WLC_GET_PKTCNTS				137
#define WLC_GET_LAZYWDS				138
#define WLC_SET_LAZYWDS				139
#define WLC_GET_BANDLIST			140
#define WLC_GET_BAND				141
#define WLC_SET_BAND				142
#define WLC_SCB_DEAUTHENTICATE			143
#define WLC_GET_SHORTSLOT			144
#define WLC_GET_SHORTSLOT_OVERRIDE		145
#define WLC_SET_SHORTSLOT_OVERRIDE		146
#define WLC_GET_SHORTSLOT_RESTRICT		147
#define WLC_SET_SHORTSLOT_RESTRICT		148
#define WLC_GET_GMODE_PROTECTION		149
#define WLC_GET_GMODE_PROTECTION_OVERRIDE	150
#define WLC_SET_GMODE_PROTECTION_OVERRIDE	151
#define WLC_UPGRADE				152
						       /* #define WLC_GET_MRATE			153 *//* no longer supported */
						       /* #define WLC_SET_MRATE			154 *//* no longer supported */
#define WLC_GET_IGNORE_BCNS			155
#define WLC_SET_IGNORE_BCNS			156
#define WLC_GET_SCB_TIMEOUT			157
#define WLC_SET_SCB_TIMEOUT			158
#define WLC_GET_ASSOCLIST			159
#define WLC_GET_CLK				160
#define WLC_SET_CLK				161
#define WLC_GET_UP				162
#define WLC_OUT					163
#define WLC_GET_WPA_AUTH			164
#define WLC_SET_WPA_AUTH			165
#define WLC_GET_UCFLAGS				166
#define WLC_SET_UCFLAGS				167
#define WLC_GET_PWRIDX				168
#define WLC_SET_PWRIDX				169
#define WLC_GET_TSSI				170
#define WLC_GET_SUP_RATESET_OVERRIDE		171
#define WLC_SET_SUP_RATESET_OVERRIDE		172
						       /* #define WLC_SET_FAST_TIMER			173 *//* no longer supported */
						       /* #define WLC_GET_FAST_TIMER			174 *//* no longer supported */
						       /* #define WLC_SET_SLOW_TIMER			175 *//* no longer supported */
						       /* #define WLC_GET_SLOW_TIMER			176 *//* no longer supported */
						       /* #define WLC_DUMP_PHYREGS			177 *//* no longer supported */
#define WLC_GET_PROTECTION_CONTROL		178
#define WLC_SET_PROTECTION_CONTROL		179
#define WLC_GET_PHYLIST				180
#define WLC_ENCRYPT_STRENGTH			181	/* ndis only */
#define WLC_DECRYPT_STATUS			182	/* ndis only */
#define WLC_GET_KEY_SEQ				183
#define WLC_GET_SCAN_CHANNEL_TIME		184
#define WLC_SET_SCAN_CHANNEL_TIME		185
#define WLC_GET_SCAN_UNASSOC_TIME		186
#define WLC_SET_SCAN_UNASSOC_TIME		187
#define WLC_GET_SCAN_HOME_TIME			188
#define WLC_SET_SCAN_HOME_TIME			189
#define WLC_GET_SCAN_NPROBES			190
#define WLC_SET_SCAN_NPROBES			191
#define WLC_GET_PRB_RESP_TIMEOUT		192
#define WLC_SET_PRB_RESP_TIMEOUT		193
#define WLC_GET_ATTEN				194
#define WLC_SET_ATTEN				195
#define WLC_GET_SHMEM				196	/* diag */
#define WLC_SET_SHMEM				197	/* diag */
						       /* #define WLC_GET_GMODE_PROTECTION_CTS		198 *//* no longer supported */
						       /* #define WLC_SET_GMODE_PROTECTION_CTS		199 *//* no longer supported */
#define WLC_SET_WSEC_TEST			200
#define WLC_SCB_DEAUTHENTICATE_FOR_REASON	201
#define WLC_TKIP_COUNTERMEASURES		202
#define WLC_GET_PIOMODE				203
#define WLC_SET_PIOMODE				204
#define WLC_SET_ASSOC_PREFER			205
#define WLC_GET_ASSOC_PREFER			206
#define WLC_SET_ROAM_PREFER			207
#define WLC_GET_ROAM_PREFER			208
#define WLC_SET_LED				209
#define WLC_GET_LED				210
#define WLC_RESERVED6				211
#define WLC_RESERVED7				212
#define WLC_GET_CHANNEL_QA			213
#define WLC_START_CHANNEL_QA			214
#define WLC_GET_CHANNEL_SEL			215
#define WLC_START_CHANNEL_SEL			216
#define WLC_GET_VALID_CHANNELS			217
#define WLC_GET_FAKEFRAG			218
#define WLC_SET_FAKEFRAG			219
#define WLC_GET_PWROUT_PERCENTAGE		220
#define WLC_SET_PWROUT_PERCENTAGE		221
#define WLC_SET_BAD_FRAME_PREEMPT		222
#define WLC_GET_BAD_FRAME_PREEMPT		223
#define WLC_SET_LEAP_LIST			224
#define WLC_GET_LEAP_LIST			225
#define WLC_GET_CWMIN				226
#define WLC_SET_CWMIN				227
#define WLC_GET_CWMAX				228
#define WLC_SET_CWMAX				229
#define WLC_GET_WET				230
#define WLC_SET_WET				231
#define WLC_GET_PUB				232
						       /* #define WLC_SET_GLACIAL_TIMER		233 *//* no longer supported */
						       /* #define WLC_GET_GLACIAL_TIMER		234 *//* no longer supported */
#define WLC_GET_KEY_PRIMARY			235
#define WLC_SET_KEY_PRIMARY			236
						       /* #define WLC_DUMP_RADIOREGS			237 *//* no longer supported */
#define WLC_RESERVED4				238
#define WLC_RESERVED5				239
#define WLC_UNSET_CALLBACK			240
#define WLC_SET_CALLBACK			241
#define WLC_GET_RADAR				242
#define WLC_SET_RADAR				243
#define WLC_SET_SPECT_MANAGMENT			244
#define WLC_GET_SPECT_MANAGMENT			245
#define WLC_WDS_GET_REMOTE_HWADDR		246	/* handled in wl_linux.c/wl_vx.c */
#define WLC_WDS_GET_WPA_SUP			247
#define WLC_SET_CS_SCAN_TIMER			248
#define WLC_GET_CS_SCAN_TIMER			249
#define WLC_MEASURE_REQUEST			250
#define WLC_INIT				251
#define WLC_SEND_QUIET				252
#define WLC_KEEPALIVE			253
#define WLC_SEND_PWR_CONSTRAINT			254
#define WLC_UPGRADE_STATUS			255
#define WLC_CURRENT_PWR				256
#define WLC_GET_SCAN_PASSIVE_TIME		257
#define WLC_SET_SCAN_PASSIVE_TIME		258
#define WLC_LEGACY_LINK_BEHAVIOR		259
#define WLC_GET_CHANNELS_IN_COUNTRY		260
#define WLC_GET_COUNTRY_LIST			261
#define WLC_GET_VAR				262	/* get value of named variable */
#define WLC_SET_VAR				263	/* set named variable to value */
#define WLC_NVRAM_GET				264	/* deprecated */
#define WLC_NVRAM_SET				265
#define WLC_NVRAM_DUMP				266
#define WLC_REBOOT				267
#define WLC_SET_WSEC_PMK			268
#define WLC_GET_AUTH_MODE			269
#define WLC_SET_AUTH_MODE			270
#define WLC_GET_WAKEENTRY			271
#define WLC_SET_WAKEENTRY			272
#define WLC_NDCONFIG_ITEM			273	/* currently handled in wl_oid.c */
#define WLC_NVOTPW				274
#define WLC_OTPW				275
#define WLC_IOV_BLOCK_GET			276
#define WLC_IOV_MODULES_GET			277
#define WLC_SOFT_RESET				278
#define WLC_GET_ALLOW_MODE			279
#define WLC_SET_ALLOW_MODE			280
#define WLC_GET_DESIRED_BSSID			281
#define WLC_SET_DESIRED_BSSID			282
#define	WLC_DISASSOC_MYAP			283
#define WLC_GET_RESERVED10			284
#define WLC_GET_RESERVED11			285
#define WLC_GET_RESERVED12			286
#define WLC_GET_RESERVED13			287
#define WLC_GET_RESERVED14			288
#define WLC_SET_RESERVED15			289
#define WLC_SET_RESERVED16			290
#define WLC_GET_RESERVED17			291
#define WLC_GET_RESERVED18			292
#define WLC_GET_RESERVED19			293
#define WLC_SET_RESERVED1A			294
#define WLC_GET_RESERVED1B			295
#define WLC_GET_RESERVED1C			296
#define WLC_GET_RESERVED1D			297
#define WLC_SET_RESERVED1E			298
#define WLC_GET_RESERVED1F			299
#define WLC_GET_RESERVED20			300
#define WLC_GET_RESERVED21			301
#define WLC_GET_RESERVED22			302
#define WLC_GET_RESERVED23			303
#define WLC_GET_RESERVED24			304
#define WLC_SET_RESERVED25			305
#define WLC_GET_RESERVED26			306
#define WLC_NPHY_SAMPLE_COLLECT			307	/* Nphy sample collect mode */
#define WLC_UM_PRIV				308	/* for usermode driver private ioctl */
#define WLC_GET_CMD				309
							/* #define WLC_LAST				310 *//* Never used - can be reused */
#define WLC_RESERVED8				311
#define WLC_RESERVED9				312
#define WLC_RESERVED1				313
#define WLC_RESERVED2				314
#define WLC_RESERVED3				315
#define WLC_LAST				316

#ifndef EPICTRL_COOKIE
#define EPICTRL_COOKIE		0xABADCEDE
#endif

#define WL_DECRYPT_STATUS_SUCCESS	1
#define WL_DECRYPT_STATUS_FAILURE	2
#define WL_DECRYPT_STATUS_UNKNOWN	3

/* allows user-mode app to poll the status of USB image upgrade */
#define WLC_UPGRADE_SUCCESS			0
#define WLC_UPGRADE_PENDING			1

/* WLC_GET_AUTH, WLC_SET_AUTH values */
#define WL_AUTH_OPEN_SYSTEM		0	/* d11 open authentication */
#define WL_AUTH_SHARED_KEY		1	/* d11 shared authentication */
#define WL_AUTH_OPEN_SHARED		2	/* try open, then shared if open failed w/rc 13 */

/* Bit masks for radio disabled status - returned by WL_GET_RADIO */
#define WL_RADIO_SW_DISABLE		(1<<0)
#define WL_RADIO_HW_DISABLE		(1<<1)
#define WL_RADIO_MPC_DISABLE		(1<<2)
#define WL_RADIO_COUNTRY_DISABLE	(1<<3)	/* some countries don't support any channel */

#define	WL_SPURAVOID_OFF	0
#define	WL_SPURAVOID_ON1	1
#define	WL_SPURAVOID_ON2	2

/* Override bit for WLC_SET_TXPWR.  if set, ignore other level limits */
#define WL_TXPWR_OVERRIDE	(1U<<31)

#define WL_PHY_PAVARS_LEN	6	/* Phy type, Band range, chain, a1, b0, b1 */

typedef struct wl_po {
	uint16 phy_type;	/* Phy type */
	uint16 band;
	uint16 cckpo;
	uint32 ofdmpo;
	uint16 mcspo[8];
} wl_po_t;

/* a large TX Power as an init value to factor out of MIN() calculations,
 * keep low enough to fit in an int8, units are .25 dBm
 */
#define WLC_TXPWR_MAX		(127)	/* ~32 dBm = 1,500 mW */

/* "diag" iovar argument and error code */
#define WL_DIAG_INTERRUPT			1	/* d11 loopback interrupt test */
#define WL_DIAG_LOOPBACK			2	/* d11 loopback data test */
#define WL_DIAG_MEMORY				3	/* d11 memory test */
#define WL_DIAG_LED				4	/* LED test */
#define WL_DIAG_REG				5	/* d11/phy register test */
#define WL_DIAG_SROM				6	/* srom read/crc test */
#define WL_DIAG_DMA				7	/* DMA test */

#define WL_DIAGERR_SUCCESS			0
#define WL_DIAGERR_FAIL_TO_RUN			1	/* unable to run requested diag */
#define WL_DIAGERR_NOT_SUPPORTED		2	/* diag requested is not supported */
#define WL_DIAGERR_INTERRUPT_FAIL		3	/* loopback interrupt test failed */
#define WL_DIAGERR_LOOPBACK_FAIL		4	/* loopback data test failed */
#define WL_DIAGERR_SROM_FAIL			5	/* srom read failed */
#define WL_DIAGERR_SROM_BADCRC			6	/* srom crc failed */
#define WL_DIAGERR_REG_FAIL			7	/* d11/phy register test failed */
#define WL_DIAGERR_MEMORY_FAIL			8	/* d11 memory test failed */
#define WL_DIAGERR_NOMEM			9	/* diag test failed due to no memory */
#define WL_DIAGERR_DMA_FAIL			10	/* DMA test failed */

#define WL_DIAGERR_MEMORY_TIMEOUT		11	/* d11 memory test didn't finish in time */
#define WL_DIAGERR_MEMORY_BADPATTERN		12	/* d11 memory test result in bad pattern */

/* band types */
#define	WLC_BAND_AUTO		0	/* auto-select */
#define	WLC_BAND_5G		1	/* 5 Ghz */
#define	WLC_BAND_2G		2	/* 2.4 Ghz */
#define	WLC_BAND_ALL		3	/* all bands */

/* band range returned by band_range iovar */
#define WL_CHAN_FREQ_RANGE_2G      0
#define WL_CHAN_FREQ_RANGE_5GL     1
#define WL_CHAN_FREQ_RANGE_5GM     2
#define WL_CHAN_FREQ_RANGE_5GH     3

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

/* MAC list modes */
#define WLC_MACMODE_DISABLED	0	/* MAC list disabled */
#define WLC_MACMODE_DENY	1	/* Deny specified (i.e. allow unspecified) */
#define WLC_MACMODE_ALLOW	2	/* Allow specified (i.e. deny unspecified) */

/*
 * 54g modes (basic bits may still be overridden)
 *
 * GMODE_LEGACY_B			Rateset: 1b, 2b, 5.5, 11
 *					Preamble: Long
 *					Shortslot: Off
 * GMODE_AUTO				Rateset: 1b, 2b, 5.5b, 11b, 18, 24, 36, 54
 *					Extended Rateset: 6, 9, 12, 48
 *					Preamble: Long
 *					Shortslot: Auto
 * GMODE_ONLY				Rateset: 1b, 2b, 5.5b, 11b, 18, 24b, 36, 54
 *					Extended Rateset: 6b, 9, 12b, 48
 *					Preamble: Short required
 *					Shortslot: Auto
 * GMODE_B_DEFERRED			Rateset: 1b, 2b, 5.5b, 11b, 18, 24, 36, 54
 *					Extended Rateset: 6, 9, 12, 48
 *					Preamble: Long
 *					Shortslot: On
 * GMODE_PERFORMANCE			Rateset: 1b, 2b, 5.5b, 6b, 9, 11b, 12b, 18, 24b, 36, 48, 54
 *					Preamble: Short required
 *					Shortslot: On and required
 * GMODE_LRS				Rateset: 1b, 2b, 5.5b, 11b
 *					Extended Rateset: 6, 9, 12, 18, 24, 36, 48, 54
 *					Preamble: Long
 *					Shortslot: Auto
 */
#define GMODE_LEGACY_B		0
#define GMODE_AUTO		1
#define GMODE_ONLY		2
#define GMODE_B_DEFERRED	3
#define GMODE_PERFORMANCE	4
#define GMODE_LRS		5
#define GMODE_MAX		6

/* values for PLCPHdr_override */
#define WLC_PLCP_AUTO	-1
#define WLC_PLCP_SHORT	0
#define WLC_PLCP_LONG	1

/* values for g_protection_override and n_protection_override */
#define WLC_PROTECTION_AUTO		-1
#define WLC_PROTECTION_OFF		0
#define WLC_PROTECTION_ON		1
#define WLC_PROTECTION_MMHDR_ONLY	2
#define WLC_PROTECTION_CTS_ONLY		3

/* values for g_protection_control and n_protection_control */
#define WLC_PROTECTION_CTL_OFF		0
#define WLC_PROTECTION_CTL_LOCAL	1
#define WLC_PROTECTION_CTL_OVERLAP	2

/* values for n_protection */
#define WLC_N_PROTECTION_OFF		0
#define WLC_N_PROTECTION_OPTIONAL	1
#define WLC_N_PROTECTION_20IN40		2
#define WLC_N_PROTECTION_MIXEDMODE	3

/* values for n_preamble_type */
#define WLC_N_PREAMBLE_MIXEDMODE	0
#define WLC_N_PREAMBLE_GF		1
#define WLC_N_PREAMBLE_GF_BRCM          2

/* values for band specific 40MHz capabilities */
#define WLC_N_BW_20ALL			0
#define WLC_N_BW_40ALL			1
#define WLC_N_BW_20IN2G_40IN5G		2

/* values to force tx/rx chain */
#define WLC_N_TXRX_CHAIN0		0
#define WLC_N_TXRX_CHAIN1		1

/* bitflags for SGI support (sgi_rx iovar) */
#define WLC_N_SGI_20			0x01
#define WLC_N_SGI_40			0x02

/* Values for PM */
#define PM_OFF	0
#define PM_MAX	1

/* interference mitigation options */
#define	INTERFERE_OVRRIDE_OFF	-1	/* interference override off */
#define	INTERFERE_NONE	0	/* off */
#define	NON_WLAN	1	/* foreign/non 802.11 interference, no auto detect */
#define	WLAN_MANUAL	2	/* ACI: no auto detection */
#define	WLAN_AUTO	3	/* ACI: auto detect */
#define	WLAN_AUTO_W_NOISE	4	/* ACI: auto - detect and non 802.11 interference */
#define	AUTO_ACTIVE	(1 << 7)	/* Auto is currently active */

#define WL_RSSI_ANT_VERSION	1	/* current version of wl_rssi_ant_t */
#define WL_ANT_RX_MAX		2	/* max 2 receive antennas */
#define WL_ANT_HT_RX_MAX	3	/* max 3 receive antennas/cores */
#define WL_ANT_IDX_1		0	/* antenna index 1 */
#define WL_ANT_IDX_2		1	/* antenna index 2 */

#ifndef WL_RSSI_ANT_MAX
#define WL_RSSI_ANT_MAX		4	/* max possible rx antennas */
#elif WL_RSSI_ANT_MAX != 4
#error "WL_RSSI_ANT_MAX does not match"
#endif

/* RSSI per antenna */
typedef struct {
	uint32 version;		/* version field */
	uint32 count;		/* number of valid antenna rssi */
	int8 rssi_ant[WL_RSSI_ANT_MAX];	/* rssi per antenna */
} wl_rssi_ant_t;

#define NUM_PWRCTRL_RATES 12

typedef struct {
	u8 txpwr_band_max[NUM_PWRCTRL_RATES];	/* User set target */
	u8 txpwr_limit[NUM_PWRCTRL_RATES];	/* reg and local power limit */
	u8 txpwr_local_max;	/* local max according to the AP */
	u8 txpwr_local_constraint;	/* local constraint according to the AP */
	u8 txpwr_chan_reg_max;	/* Regulatory max for this channel */
	u8 txpwr_target[2][NUM_PWRCTRL_RATES];	/* Latest target for 2.4 and 5 Ghz */
	u8 txpwr_est_Pout[2];	/* Latest estimate for 2.4 and 5 Ghz */
	u8 txpwr_opo[NUM_PWRCTRL_RATES];	/* On G phy, OFDM power offset */
	u8 txpwr_bphy_cck_max[NUM_PWRCTRL_RATES];	/* Max CCK power for this band (SROM) */
	u8 txpwr_bphy_ofdm_max;	/* Max OFDM power for this band (SROM) */
	u8 txpwr_aphy_max[NUM_PWRCTRL_RATES];	/* Max power for A band (SROM) */
	int8 txpwr_antgain[2];	/* Ant gain for each band - from SROM */
	u8 txpwr_est_Pout_gofdm;	/* Pwr estimate for 2.4 OFDM */
} tx_power_legacy_t;

#define WL_TX_POWER_RATES_LEGACY	45
#define WL_TX_POWER_MCS20_FIRST	        12
#define WL_TX_POWER_MCS20_NUM	        16
#define WL_TX_POWER_MCS40_FIRST	        28
#define WL_TX_POWER_MCS40_NUM	        17

typedef struct {
	uint32 flags;
	chanspec_t chanspec;	/* txpwr report for this channel */
	chanspec_t local_chanspec;	/* channel on which we are associated */
	u8 local_max;	/* local max according to the AP */
	u8 local_constraint;	/* local constraint according to the AP */
	int8 antgain[2];	/* Ant gain for each band - from SROM */
	u8 rf_cores;		/* count of RF Cores being reported */
	u8 est_Pout[4];	/* Latest tx power out estimate per RF
				 * chain without adjustment
				 */
	u8 est_Pout_cck;	/* Latest CCK tx power out estimate */
	u8 user_limit[WL_TX_POWER_RATES_LEGACY];	/* User limit */
	u8 reg_limit[WL_TX_POWER_RATES_LEGACY];	/* Regulatory power limit */
	u8 board_limit[WL_TX_POWER_RATES_LEGACY];	/* Max power board can support (SROM) */
	u8 target[WL_TX_POWER_RATES_LEGACY];	/* Latest target power */
} tx_power_legacy2_t;

#define WL_TX_POWER_RATES	       101
#define WL_TX_POWER_CCK_FIRST	       0
#define WL_TX_POWER_CCK_NUM	       4
#define WL_TX_POWER_OFDM_FIRST	       4	/* Index for first 20MHz OFDM SISO rate */
#define WL_TX_POWER_OFDM20_CDD_FIRST   12	/* Index for first 20MHz OFDM CDD rate */
#define WL_TX_POWER_OFDM40_SISO_FIRST  52	/* Index for first 40MHz OFDM SISO rate */
#define WL_TX_POWER_OFDM40_CDD_FIRST   60	/* Index for first 40MHz OFDM CDD rate */
#define WL_TX_POWER_OFDM_NUM	       8
#define WL_TX_POWER_MCS20_SISO_FIRST   20	/* Index for first 20MHz MCS SISO rate */
#define WL_TX_POWER_MCS20_CDD_FIRST    28	/* Index for first 20MHz MCS CDD rate */
#define WL_TX_POWER_MCS20_STBC_FIRST   36	/* Index for first 20MHz MCS STBC rate */
#define WL_TX_POWER_MCS20_SDM_FIRST    44	/* Index for first 20MHz MCS SDM rate */
#define WL_TX_POWER_MCS40_SISO_FIRST   68	/* Index for first 40MHz MCS SISO rate */
#define WL_TX_POWER_MCS40_CDD_FIRST    76	/* Index for first 40MHz MCS CDD rate */
#define WL_TX_POWER_MCS40_STBC_FIRST   84	/* Index for first 40MHz MCS STBC rate */
#define WL_TX_POWER_MCS40_SDM_FIRST    92	/* Index for first 40MHz MCS SDM rate */
#define WL_TX_POWER_MCS_1_STREAM_NUM   8
#define WL_TX_POWER_MCS_2_STREAM_NUM   8
#define WL_TX_POWER_MCS_32	       100	/* Index for 40MHz rate MCS 32 */
#define WL_TX_POWER_MCS_32_NUM	       1

/* sslpnphy specifics */
#define WL_TX_POWER_MCS20_SISO_FIRST_SSN   12	/* Index for first 20MHz MCS SISO rate */

/* tx_power_t.flags bits */
#define WL_TX_POWER_F_ENABLED	1
#define WL_TX_POWER_F_HW	2
#define WL_TX_POWER_F_MIMO	4
#define WL_TX_POWER_F_SISO	8

typedef struct {
	uint32 flags;
	chanspec_t chanspec;	/* txpwr report for this channel */
	chanspec_t local_chanspec;	/* channel on which we are associated */
	u8 local_max;	/* local max according to the AP */
	u8 local_constraint;	/* local constraint according to the AP */
	int8 antgain[2];	/* Ant gain for each band - from SROM */
	u8 rf_cores;		/* count of RF Cores being reported */
	u8 est_Pout[4];	/* Latest tx power out estimate per RF chain */
	u8 est_Pout_act[4];	/* Latest tx power out estimate per RF chain
				 * without adjustment
				 */
	u8 est_Pout_cck;	/* Latest CCK tx power out estimate */
	u8 tx_power_max[4];	/* Maximum target power among all rates */
	u8 tx_power_max_rate_ind[4];	/* Index of the rate with the max target power */
	u8 user_limit[WL_TX_POWER_RATES];	/* User limit */
	u8 reg_limit[WL_TX_POWER_RATES];	/* Regulatory power limit */
	u8 board_limit[WL_TX_POWER_RATES];	/* Max power board can support (SROM) */
	u8 target[WL_TX_POWER_RATES];	/* Latest target power */
} tx_power_t;

typedef struct tx_inst_power {
	u8 txpwr_est_Pout[2];	/* Latest estimate for 2.4 and 5 Ghz */
	u8 txpwr_est_Pout_gofdm;	/* Pwr estimate for 2.4 OFDM */
} tx_inst_power_t;

/* Message levels */
#define WL_ERROR_VAL		0x00000001
#define WL_TRACE_VAL		0x00000002
#define WL_AMPDU_VAL		0x20000000
#define WL_FFPLD_VAL		0x40000000

/* maximum channels returned by the get valid channels iovar */
#define WL_NUMCHANNELS		64
#define WL_NUMCHANSPECS		100

struct tsinfo_arg {
	u8 octets[3];
};

#define	NFIFO			6	/* # tx/rx fifopairs */

#define	WL_CNT_T_VERSION	7	/* current version of wl_cnt_t struct */

typedef struct {
	uint16 version;		/* see definition of WL_CNT_T_VERSION */
	uint16 length;		/* length of entire structure */

	/* transmit stat counters */
	uint32 txframe;		/* tx data frames */
	uint32 txbyte;		/* tx data bytes */
	uint32 txretrans;	/* tx mac retransmits */
	uint32 txerror;		/* tx data errors (derived: sum of others) */
	uint32 txctl;		/* tx management frames */
	uint32 txprshort;	/* tx short preamble frames */
	uint32 txserr;		/* tx status errors */
	uint32 txnobuf;		/* tx out of buffers errors */
	uint32 txnoassoc;	/* tx discard because we're not associated */
	uint32 txrunt;		/* tx runt frames */
	uint32 txchit;		/* tx header cache hit (fastpath) */
	uint32 txcmiss;		/* tx header cache miss (slowpath) */
	uint32 ieee_tx_status;	/* calls to ieee80211_tx_status */
	uint32 ieee_tx;		/* tx calls frm mac0211 */
	uint32 ieee_rx;		/* calls to ieee_rx */

	/* transmit chip error counters */
	uint32 txuflo;		/* tx fifo underflows */
	uint32 txphyerr;	/* tx phy errors (indicated in tx status) */
	uint32 txphycrs;

	/* receive stat counters */
	uint32 rxframe;		/* rx data frames */
	uint32 rxbyte;		/* rx data bytes */
	uint32 rxerror;		/* rx data errors (derived: sum of others) */
	uint32 rxctl;		/* rx management frames */
	uint32 rxnobuf;		/* rx out of buffers errors */
	uint32 rxnondata;	/* rx non data frames in the data channel errors */
	uint32 rxbadds;		/* rx bad DS errors */
	uint32 rxbadcm;		/* rx bad control or management frames */
	uint32 rxfragerr;	/* rx fragmentation errors */
	uint32 rxrunt;		/* rx runt frames */
	uint32 rxgiant;		/* rx giant frames */
	uint32 rxnoscb;		/* rx no scb error */
	uint32 rxbadproto;	/* rx invalid frames */
	uint32 rxbadsrcmac;	/* rx frames with Invalid Src Mac */
	uint32 rxbadda;		/* rx frames tossed for invalid da */
	uint32 rxfilter;	/* rx frames filtered out */

	/* receive chip error counters */
	uint32 rxoflo;		/* rx fifo overflow errors */
	uint32 rxuflo[NFIFO];	/* rx dma descriptor underflow errors */

	uint32 d11cnt_txrts_off;	/* d11cnt txrts value when reset d11cnt */
	uint32 d11cnt_rxcrc_off;	/* d11cnt rxcrc value when reset d11cnt */
	uint32 d11cnt_txnocts_off;	/* d11cnt txnocts value when reset d11cnt */

	/* misc counters */
	uint32 dmade;		/* tx/rx dma descriptor errors */
	uint32 dmada;		/* tx/rx dma data errors */
	uint32 dmape;		/* tx/rx dma descriptor protocol errors */
	uint32 reset;		/* reset count */
	uint32 tbtt;		/* cnts the TBTT int's */
	uint32 txdmawar;
	uint32 pkt_callback_reg_fail;	/* callbacks register failure */

	/* MAC counters: 32-bit version of d11.h's macstat_t */
	uint32 txallfrm;	/* total number of frames sent, incl. Data, ACK, RTS, CTS,
				 * Control Management (includes retransmissions)
				 */
	uint32 txrtsfrm;	/* number of RTS sent out by the MAC */
	uint32 txctsfrm;	/* number of CTS sent out by the MAC */
	uint32 txackfrm;	/* number of ACK frames sent out */
	uint32 txdnlfrm;	/* Not used */
	uint32 txbcnfrm;	/* beacons transmitted */
	uint32 txfunfl[8];	/* per-fifo tx underflows */
	uint32 txtplunfl;	/* Template underflows (mac was too slow to transmit ACK/CTS
				 * or BCN)
				 */
	uint32 txphyerror;	/* Transmit phy error, type of error is reported in tx-status for
				 * driver enqueued frames
				 */
	uint32 rxfrmtoolong;	/* Received frame longer than legal limit (2346 bytes) */
	uint32 rxfrmtooshrt;	/* Received frame did not contain enough bytes for its frame type */
	uint32 rxinvmachdr;	/* Either the protocol version != 0 or frame type not
				 * data/control/management
				 */
	uint32 rxbadfcs;	/* number of frames for which the CRC check failed in the MAC */
	uint32 rxbadplcp;	/* parity check of the PLCP header failed */
	uint32 rxcrsglitch;	/* PHY was able to correlate the preamble but not the header */
	uint32 rxstrt;		/* Number of received frames with a good PLCP
				 * (i.e. passing parity check)
				 */
	uint32 rxdfrmucastmbss;	/* Number of received DATA frames with good FCS and matching RA */
	uint32 rxmfrmucastmbss;	/* number of received mgmt frames with good FCS and matching RA */
	uint32 rxcfrmucast;	/* number of received CNTRL frames with good FCS and matching RA */
	uint32 rxrtsucast;	/* number of unicast RTS addressed to the MAC (good FCS) */
	uint32 rxctsucast;	/* number of unicast CTS addressed to the MAC (good FCS) */
	uint32 rxackucast;	/* number of ucast ACKS received (good FCS) */
	uint32 rxdfrmocast;	/* number of received DATA frames (good FCS and not matching RA) */
	uint32 rxmfrmocast;	/* number of received MGMT frames (good FCS and not matching RA) */
	uint32 rxcfrmocast;	/* number of received CNTRL frame (good FCS and not matching RA) */
	uint32 rxrtsocast;	/* number of received RTS not addressed to the MAC */
	uint32 rxctsocast;	/* number of received CTS not addressed to the MAC */
	uint32 rxdfrmmcast;	/* number of RX Data multicast frames received by the MAC */
	uint32 rxmfrmmcast;	/* number of RX Management multicast frames received by the MAC */
	uint32 rxcfrmmcast;	/* number of RX Control multicast frames received by the MAC
				 * (unlikely to see these)
				 */
	uint32 rxbeaconmbss;	/* beacons received from member of BSS */
	uint32 rxdfrmucastobss;	/* number of unicast frames addressed to the MAC from
				 * other BSS (WDS FRAME)
				 */
	uint32 rxbeaconobss;	/* beacons received from other BSS */
	uint32 rxrsptmout;	/* Number of response timeouts for transmitted frames
				 * expecting a response
				 */
	uint32 bcntxcancl;	/* transmit beacons canceled due to receipt of beacon (IBSS) */
	uint32 rxf0ovfl;	/* Number of receive fifo 0 overflows */
	uint32 rxf1ovfl;	/* Number of receive fifo 1 overflows (obsolete) */
	uint32 rxf2ovfl;	/* Number of receive fifo 2 overflows (obsolete) */
	uint32 txsfovfl;	/* Number of transmit status fifo overflows (obsolete) */
	uint32 pmqovfl;		/* Number of PMQ overflows */
	uint32 rxcgprqfrm;	/* Number of received Probe requests that made it into
				 * the PRQ fifo
				 */
	uint32 rxcgprsqovfl;	/* Rx Probe Request Que overflow in the AP */
	uint32 txcgprsfail;	/* Tx Probe Response Fail. AP sent probe response but did
				 * not get ACK
				 */
	uint32 txcgprssuc;	/* Tx Probe Response Success (ACK was received) */
	uint32 prs_timeout;	/* Number of probe requests that were dropped from the PRQ
				 * fifo because a probe response could not be sent out within
				 * the time limit defined in M_PRS_MAXTIME
				 */
	uint32 rxnack;
	uint32 frmscons;
	uint32 txnack;
	uint32 txglitch_nack;	/* obsolete */
	uint32 txburst;		/* obsolete */

	/* 802.11 MIB counters, pp. 614 of 802.11 reaff doc. */
	uint32 txfrag;		/* dot11TransmittedFragmentCount */
	uint32 txmulti;		/* dot11MulticastTransmittedFrameCount */
	uint32 txfail;		/* dot11FailedCount */
	uint32 txretry;		/* dot11RetryCount */
	uint32 txretrie;	/* dot11MultipleRetryCount */
	uint32 rxdup;		/* dot11FrameduplicateCount */
	uint32 txrts;		/* dot11RTSSuccessCount */
	uint32 txnocts;		/* dot11RTSFailureCount */
	uint32 txnoack;		/* dot11ACKFailureCount */
	uint32 rxfrag;		/* dot11ReceivedFragmentCount */
	uint32 rxmulti;		/* dot11MulticastReceivedFrameCount */
	uint32 rxcrc;		/* dot11FCSErrorCount */
	uint32 txfrmsnt;	/* dot11TransmittedFrameCount (bogus MIB?) */
	uint32 rxundec;		/* dot11WEPUndecryptableCount */

	/* WPA2 counters (see rxundec for DecryptFailureCount) */
	uint32 tkipmicfaill;	/* TKIPLocalMICFailures */
	uint32 tkipcntrmsr;	/* TKIPCounterMeasuresInvoked */
	uint32 tkipreplay;	/* TKIPReplays */
	uint32 ccmpfmterr;	/* CCMPFormatErrors */
	uint32 ccmpreplay;	/* CCMPReplays */
	uint32 ccmpundec;	/* CCMPDecryptErrors */
	uint32 fourwayfail;	/* FourWayHandshakeFailures */
	uint32 wepundec;	/* dot11WEPUndecryptableCount */
	uint32 wepicverr;	/* dot11WEPICVErrorCount */
	uint32 decsuccess;	/* DecryptSuccessCount */
	uint32 tkipicverr;	/* TKIPICVErrorCount */
	uint32 wepexcluded;	/* dot11WEPExcludedCount */

	uint32 rxundec_mcst;	/* dot11WEPUndecryptableCount */

	/* WPA2 counters (see rxundec for DecryptFailureCount) */
	uint32 tkipmicfaill_mcst;	/* TKIPLocalMICFailures */
	uint32 tkipcntrmsr_mcst;	/* TKIPCounterMeasuresInvoked */
	uint32 tkipreplay_mcst;	/* TKIPReplays */
	uint32 ccmpfmterr_mcst;	/* CCMPFormatErrors */
	uint32 ccmpreplay_mcst;	/* CCMPReplays */
	uint32 ccmpundec_mcst;	/* CCMPDecryptErrors */
	uint32 fourwayfail_mcst;	/* FourWayHandshakeFailures */
	uint32 wepundec_mcst;	/* dot11WEPUndecryptableCount */
	uint32 wepicverr_mcst;	/* dot11WEPICVErrorCount */
	uint32 decsuccess_mcst;	/* DecryptSuccessCount */
	uint32 tkipicverr_mcst;	/* TKIPICVErrorCount */
	uint32 wepexcluded_mcst;	/* dot11WEPExcludedCount */

	uint32 txchanrej;	/* Tx frames suppressed due to channel rejection */
	uint32 txexptime;	/* Tx frames suppressed due to timer expiration */
	uint32 psmwds;		/* Count PSM watchdogs */
	uint32 phywatchdog;	/* Count Phy watchdogs (triggered by ucode) */

	/* MBSS counters, AP only */
	uint32 prq_entries_handled;	/* PRQ entries read in */
	uint32 prq_undirected_entries;	/*    which were bcast bss & ssid */
	uint32 prq_bad_entries;	/*    which could not be translated to info */
	uint32 atim_suppress_count;	/* TX suppressions on ATIM fifo */
	uint32 bcn_template_not_ready;	/* Template marked in use on send bcn ... */
	uint32 bcn_template_not_ready_done;	/* ...but "DMA done" interrupt rcvd */
	uint32 late_tbtt_dpc;	/* TBTT DPC did not happen in time */

	/* per-rate receive stat counters */
	uint32 rx1mbps;		/* packets rx at 1Mbps */
	uint32 rx2mbps;		/* packets rx at 2Mbps */
	uint32 rx5mbps5;	/* packets rx at 5.5Mbps */
	uint32 rx6mbps;		/* packets rx at 6Mbps */
	uint32 rx9mbps;		/* packets rx at 9Mbps */
	uint32 rx11mbps;	/* packets rx at 11Mbps */
	uint32 rx12mbps;	/* packets rx at 12Mbps */
	uint32 rx18mbps;	/* packets rx at 18Mbps */
	uint32 rx24mbps;	/* packets rx at 24Mbps */
	uint32 rx36mbps;	/* packets rx at 36Mbps */
	uint32 rx48mbps;	/* packets rx at 48Mbps */
	uint32 rx54mbps;	/* packets rx at 54Mbps */
	uint32 rx108mbps;	/* packets rx at 108mbps */
	uint32 rx162mbps;	/* packets rx at 162mbps */
	uint32 rx216mbps;	/* packets rx at 216 mbps */
	uint32 rx270mbps;	/* packets rx at 270 mbps */
	uint32 rx324mbps;	/* packets rx at 324 mbps */
	uint32 rx378mbps;	/* packets rx at 378 mbps */
	uint32 rx432mbps;	/* packets rx at 432 mbps */
	uint32 rx486mbps;	/* packets rx at 486 mbps */
	uint32 rx540mbps;	/* packets rx at 540 mbps */

	/* pkteng rx frame stats */
	uint32 pktengrxducast;	/* unicast frames rxed by the pkteng code */
	uint32 pktengrxdmcast;	/* multicast frames rxed by the pkteng code */

	uint32 rfdisable;	/* count of radio disables */
	uint32 bphy_rxcrsglitch;	/* PHY count of bphy glitches */

	uint32 txmpdu_sgi;	/* count for sgi transmit */
	uint32 rxmpdu_sgi;	/* count for sgi received */
	uint32 txmpdu_stbc;	/* count for stbc transmit */
	uint32 rxmpdu_stbc;	/* count for stbc received */
} wl_cnt_t;

#define	WL_DELTA_STATS_T_VERSION	1	/* current version of wl_delta_stats_t struct */

typedef struct {
	uint16 version;		/* see definition of WL_DELTA_STATS_T_VERSION */
	uint16 length;		/* length of entire structure */

	/* transmit stat counters */
	uint32 txframe;		/* tx data frames */
	uint32 txbyte;		/* tx data bytes */
	uint32 txretrans;	/* tx mac retransmits */
	uint32 txfail;		/* tx failures */

	/* receive stat counters */
	uint32 rxframe;		/* rx data frames */
	uint32 rxbyte;		/* rx data bytes */

	/* per-rate receive stat counters */
	uint32 rx1mbps;		/* packets rx at 1Mbps */
	uint32 rx2mbps;		/* packets rx at 2Mbps */
	uint32 rx5mbps5;	/* packets rx at 5.5Mbps */
	uint32 rx6mbps;		/* packets rx at 6Mbps */
	uint32 rx9mbps;		/* packets rx at 9Mbps */
	uint32 rx11mbps;	/* packets rx at 11Mbps */
	uint32 rx12mbps;	/* packets rx at 12Mbps */
	uint32 rx18mbps;	/* packets rx at 18Mbps */
	uint32 rx24mbps;	/* packets rx at 24Mbps */
	uint32 rx36mbps;	/* packets rx at 36Mbps */
	uint32 rx48mbps;	/* packets rx at 48Mbps */
	uint32 rx54mbps;	/* packets rx at 54Mbps */
	uint32 rx108mbps;	/* packets rx at 108mbps */
	uint32 rx162mbps;	/* packets rx at 162mbps */
	uint32 rx216mbps;	/* packets rx at 216 mbps */
	uint32 rx270mbps;	/* packets rx at 270 mbps */
	uint32 rx324mbps;	/* packets rx at 324 mbps */
	uint32 rx378mbps;	/* packets rx at 378 mbps */
	uint32 rx432mbps;	/* packets rx at 432 mbps */
	uint32 rx486mbps;	/* packets rx at 486 mbps */
	uint32 rx540mbps;	/* packets rx at 540 mbps */
} wl_delta_stats_t;

#define WL_WME_CNT_VERSION	1	/* current version of wl_wme_cnt_t */

typedef struct {
	uint32 packets;
	uint32 bytes;
} wl_traffic_stats_t;

typedef struct {
	uint16 version;		/* see definition of WL_WME_CNT_VERSION */
	uint16 length;		/* length of entire structure */

	wl_traffic_stats_t tx[AC_COUNT];	/* Packets transmitted */
	wl_traffic_stats_t tx_failed[AC_COUNT];	/* Packets dropped or failed to transmit */
	wl_traffic_stats_t rx[AC_COUNT];	/* Packets received */
	wl_traffic_stats_t rx_failed[AC_COUNT];	/* Packets failed to receive */

	wl_traffic_stats_t forward[AC_COUNT];	/* Packets forwarded by AP */

	wl_traffic_stats_t tx_expired[AC_COUNT];	/* packets dropped due to lifetime expiry */

} wl_wme_cnt_t;

struct wl_msglevel2 {
	uint32 low;
	uint32 high;
};

#ifdef WLBA

#define	WLC_BA_CNT_VERSION	1	/* current version of wlc_ba_cnt_t */

/* block ack related stats */
typedef struct wlc_ba_cnt {
	uint16 version;		/* WLC_BA_CNT_VERSION */
	uint16 length;		/* length of entire structure */

	/* transmit stat counters */
	uint32 txpdu;		/* pdus sent */
	uint32 txsdu;		/* sdus sent */
	uint32 txfc;		/* tx side flow controlled packets */
	uint32 txfci;		/* tx side flow control initiated */
	uint32 txretrans;	/* retransmitted pdus */
	uint32 txbatimer;	/* ba resend due to timer */
	uint32 txdrop;		/* dropped packets */
	uint32 txaddbareq;	/* addba req sent */
	uint32 txaddbaresp;	/* addba resp sent */
	uint32 txdelba;		/* delba sent */
	uint32 txba;		/* ba sent */
	uint32 txbar;		/* bar sent */
	uint32 txpad[4];	/* future */

	/* receive side counters */
	uint32 rxpdu;		/* pdus recd */
	uint32 rxqed;		/* pdus buffered before sending up */
	uint32 rxdup;		/* duplicate pdus */
	uint32 rxnobuf;		/* pdus discarded due to no buf */
	uint32 rxaddbareq;	/* addba req recd */
	uint32 rxaddbaresp;	/* addba resp recd */
	uint32 rxdelba;		/* delba recd */
	uint32 rxba;		/* ba recd */
	uint32 rxbar;		/* bar recd */
	uint32 rxinvba;		/* invalid ba recd */
	uint32 rxbaholes;	/* ba recd with holes */
	uint32 rxunexp;		/* unexpected packets */
	uint32 rxpad[4];	/* future */
} wlc_ba_cnt_t;
#endif				/* WLBA */

/* structure for per-tid ampdu control */
struct ampdu_tid_control {
	u8 tid;		/* tid */
	u8 enable;		/* enable/disable */
};

/* structure for identifying ea/tid for sending addba/delba */
struct ampdu_ea_tid {
	struct ether_addr ea;	/* Station address */
	u8 tid;		/* tid */
};
/* structure for identifying retry/tid for retry_limit_tid/rr_retry_limit_tid */
struct ampdu_retry_tid {
	u8 tid;		/* tid */
	u8 retry;		/* retry value */
};

/* structure for addts arguments */
/* For ioctls that take a list of TSPEC */
struct tslist {
	int count;		/* number of tspecs */
	struct tsinfo_arg tsinfo[1];	/* variable length array of tsinfo */
};

/* structure for addts/delts arguments */
typedef struct tspec_arg {
	uint16 version;		/* see definition of TSPEC_ARG_VERSION */
	uint16 length;		/* length of entire structure */
	uint flag;		/* bit field */
	/* TSPEC Arguments */
	struct tsinfo_arg tsinfo;	/* TS Info bit field */
	uint16 nom_msdu_size;	/* (Nominal or fixed) MSDU Size (bytes) */
	uint16 max_msdu_size;	/* Maximum MSDU Size (bytes) */
	uint min_srv_interval;	/* Minimum Service Interval (us) */
	uint max_srv_interval;	/* Maximum Service Interval (us) */
	uint inactivity_interval;	/* Inactivity Interval (us) */
	uint suspension_interval;	/* Suspension Interval (us) */
	uint srv_start_time;	/* Service Start Time (us) */
	uint min_data_rate;	/* Minimum Data Rate (bps) */
	uint mean_data_rate;	/* Mean Data Rate (bps) */
	uint peak_data_rate;	/* Peak Data Rate (bps) */
	uint max_burst_size;	/* Maximum Burst Size (bytes) */
	uint delay_bound;	/* Delay Bound (us) */
	uint min_phy_rate;	/* Minimum PHY Rate (bps) */
	uint16 surplus_bw;	/* Surplus Bandwidth Allowance (range 1.0 to 8.0) */
	uint16 medium_time;	/* Medium Time (32 us/s periods) */
	u8 dialog_token;	/* dialog token */
} tspec_arg_t;

/* tspec arg for desired station */
typedef struct tspec_per_sta_arg {
	struct ether_addr ea;
	struct tspec_arg ts;
} tspec_per_sta_arg_t;

/* structure for max bandwidth for each access category */
typedef struct wme_max_bandwidth {
	uint32 ac[AC_COUNT];	/* max bandwidth for each access category */
} wme_max_bandwidth_t;

#define WL_WME_MBW_PARAMS_IO_BYTES (sizeof(wme_max_bandwidth_t))

/* current version of wl_tspec_arg_t struct */
#define	TSPEC_ARG_VERSION		2	/* current version of wl_tspec_arg_t struct */
#define TSPEC_ARG_LENGTH		55	/* argument length from tsinfo to medium_time */
#define TSPEC_DEFAULT_DIALOG_TOKEN	42	/* default dialog token */
#define TSPEC_DEFAULT_SBW_FACTOR	0x3000	/* default surplus bw */

/* define for flag */
#define TSPEC_PENDING		0	/* TSPEC pending */
#define TSPEC_ACCEPTED		1	/* TSPEC accepted */
#define TSPEC_REJECTED		2	/* TSPEC rejected */
#define TSPEC_UNKNOWN		3	/* TSPEC unknown */
#define TSPEC_STATUS_MASK	7	/* TSPEC status mask */

/* Software feature flag defines used by wlfeatureflag */
#define WL_SWFL_NOHWRADIO	0x0004
#define WL_SWFL_FLOWCONTROL     0x0008	/* Enable backpressure to OS stack */
#define WL_SWFL_WLBSSSORT	0x0010	/* Per-port supports sorting of BSS */

#define WL_LIFETIME_MAX 0xFFFF	/* Max value in ms */

/*
 * Dongle pattern matching filter.
 */

/* Packet filter types. Currently, only pattern matching is supported. */
typedef enum wl_pkt_filter_type {
	WL_PKT_FILTER_TYPE_PATTERN_MATCH	/* Pattern matching filter */
} wl_pkt_filter_type_t;

#define WL_PKT_FILTER_TYPE wl_pkt_filter_type_t

/* Pattern matching filter. Specifies an offset within received packets to
 * start matching, the pattern to match, the size of the pattern, and a bitmask
 * that indicates which bits within the pattern should be matched.
 */
typedef struct wl_pkt_filter_pattern {
	uint32 offset;		/* Offset within received packet to start pattern matching.
				 * Offset '0' is the first byte of the ethernet header.
				 */
	uint32 size_bytes;	/* Size of the pattern.  Bitmask must be the same size. */
	u8 mask_and_pattern[1];	/* Variable length mask and pattern data.  mask starts
					 * at offset 0.  Pattern immediately follows mask.
					 */
} wl_pkt_filter_pattern_t;

/* IOVAR "pkt_filter_add" parameter. Used to install packet filters. */
typedef struct wl_pkt_filter {
	uint32 id;		/* Unique filter id, specified by app. */
	uint32 type;		/* Filter type (WL_PKT_FILTER_TYPE_xxx). */
	uint32 negate_match;	/* Negate the result of filter matches */
	union {			/* Filter definitions */
		wl_pkt_filter_pattern_t pattern;	/* Pattern matching filter */
	} u;
} wl_pkt_filter_t;

#define WL_PKT_FILTER_FIXED_LEN		  OFFSETOF(wl_pkt_filter_t, u)
#define WL_PKT_FILTER_PATTERN_FIXED_LEN	  OFFSETOF(wl_pkt_filter_pattern_t, mask_and_pattern)

/* IOVAR "pkt_filter_enable" parameter. */
typedef struct wl_pkt_filter_enable {
	uint32 id;		/* Unique filter id */
	uint32 enable;		/* Enable/disable bool */
} wl_pkt_filter_enable_t;

/* IOVAR "pkt_filter_list" parameter. Used to retrieve a list of installed filters. */
typedef struct wl_pkt_filter_list {
	uint32 num;		/* Number of installed packet filters */
	wl_pkt_filter_t filter[1];	/* Variable array of packet filters. */
} wl_pkt_filter_list_t;

#define WL_PKT_FILTER_LIST_FIXED_LEN	  OFFSETOF(wl_pkt_filter_list_t, filter)

/* IOVAR "pkt_filter_stats" parameter. Used to retrieve debug statistics. */
typedef struct wl_pkt_filter_stats {
	uint32 num_pkts_matched;	/* # filter matches for specified filter id */
	uint32 num_pkts_forwarded;	/* # packets fwded from dongle to host for all filters */
	uint32 num_pkts_discarded;	/* # packets discarded by dongle for all filters */
} wl_pkt_filter_stats_t;

typedef struct wl_pkteng {
	uint32 flags;
	uint32 delay;		/* Inter-packet delay */
	uint32 nframes;		/* Number of frames */
	uint32 length;		/* Packet length */
	u8 seqno;		/* Enable/disable sequence no. */
	struct ether_addr dest;	/* Destination address */
	struct ether_addr src;	/* Source address */
} wl_pkteng_t;

#define	WLC_RSSI_INVALID	 0	/* invalid RSSI value */

/* require default structure packing */
#include <packed_section_end.h>

/* n-mode support capability */
/* 2x2 includes both 1x1 & 2x2 devices
 * reserved #define 2 for future when we want to separate 1x1 & 2x2 and
 * control it independently
 */
#define WL_11N_2x2			1
#define WL_11N_3x3			3
#define WL_11N_4x4			4

/* define 11n feature disable flags */
#define WLFEATURE_DISABLE_11N		0x00000001
#define WLFEATURE_DISABLE_11N_STBC_TX	0x00000002
#define WLFEATURE_DISABLE_11N_STBC_RX	0x00000004
#define WLFEATURE_DISABLE_11N_SGI_TX	0x00000008
#define WLFEATURE_DISABLE_11N_SGI_RX	0x00000010
#define WLFEATURE_DISABLE_11N_AMPDU_TX	0x00000020
#define WLFEATURE_DISABLE_11N_AMPDU_RX	0x00000040
#define WLFEATURE_DISABLE_11N_GF	0x00000080

#define WL_EVENTING_MASK_LEN	16

#define TOE_TX_CSUM_OL		0x00000001
#define TOE_RX_CSUM_OL		0x00000002

#define PM_OFF	0
#define PM_MAX	1
#define PM_FAST 2

typedef enum sup_auth_status {
	WLC_SUP_DISCONNECTED = 0,
	WLC_SUP_CONNECTING,
	WLC_SUP_IDREQUIRED,
	WLC_SUP_AUTHENTICATING,
	WLC_SUP_AUTHENTICATED,
	WLC_SUP_KEYXCHANGE,
	WLC_SUP_KEYED,
	WLC_SUP_TIMEOUT,
	WLC_SUP_LAST_BASIC_STATE,
	WLC_SUP_KEYXCHANGE_WAIT_M1 = WLC_SUP_AUTHENTICATED,
	WLC_SUP_KEYXCHANGE_PREP_M2 = WLC_SUP_KEYXCHANGE,
	WLC_SUP_KEYXCHANGE_WAIT_M3 = WLC_SUP_LAST_BASIC_STATE,
	WLC_SUP_KEYXCHANGE_PREP_M4,
	WLC_SUP_KEYXCHANGE_WAIT_G1,
	WLC_SUP_KEYXCHANGE_PREP_G2
} sup_auth_status_t;
#endif				/* _wlioctl_h_ */
