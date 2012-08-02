/*
 * Custom OID/ioctl definitions for
 * Broadcom 802.11abg Networking Device Driver
 *
 * Definitions subject to change without notice.
 *
 * Copyright (C) 1999-2012, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: wlioctl.h 334796 2012-05-23 22:59:44Z $
 */

#ifndef _wlioctl_h_
#define	_wlioctl_h_

#include <typedefs.h>
#include <proto/ethernet.h>
#include <proto/bcmeth.h>
#include <proto/bcmevent.h>
#include <proto/802.11.h>
#include <bcmwifi_channels.h>
#include <bcmwifi_rates.h>

#ifndef LINUX_POSTMOGRIFY_REMOVAL
#include <bcm_mpool_pub.h>
#include <bcmcdc.h>
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/*  LINUX_POSTMOGRIFY_REMOVAL: undefined during compile phase, so its
 *  a no-op for most cases. For hybrid and other open source releases,
 *  its defined during a second pass and mogrified out for distribution.
 */


#ifndef LINUX_POSTMOGRIFY_REMOVAL

#ifndef INTF_NAME_SIZ
#define INTF_NAME_SIZ	16
#endif

/* Used to send ioctls over the transport pipe */
typedef struct remote_ioctl {
	cdc_ioctl_t 	msg;
	uint		data_len;
	char            intf_name[INTF_NAME_SIZ];
} rem_ioctl_t;
#define REMOTE_SIZE	sizeof(rem_ioctl_t)

#define ACTION_FRAME_SIZE 1800

typedef struct wl_action_frame {
	struct ether_addr 	da;
	uint16 			len;
	uint32 			packetId;
	uint8			data[ACTION_FRAME_SIZE];
} wl_action_frame_t;

#define WL_WIFI_ACTION_FRAME_SIZE sizeof(struct wl_action_frame)

typedef struct ssid_info
{
	uint8		ssid_len;	/* the length of SSID */
	uint8		ssid[32];	/* SSID string */
} ssid_info_t;

typedef struct wl_af_params {
	uint32 			channel;
	int32 			dwell_time;
	struct ether_addr 	BSSID;
	wl_action_frame_t	action_frame;
} wl_af_params_t;

#define WL_WIFI_AF_PARAMS_SIZE sizeof(struct wl_af_params)

#define MFP_TEST_FLAG_NORMAL	0
#define MFP_TEST_FLAG_ANY_KEY	1
typedef struct wl_sa_query {
	uint32			flag;
	uint8 			action;
	uint16 			id;
	struct ether_addr 	da;
} wl_sa_query_t;

#endif /*  LINUX_POSTMOGRIFY_REMOVAL */

/* require default structure packing */
#define BWL_DEFAULT_PACKING
#include <packed_section_start.h>


#ifndef LINUX_POSTMOGRIFY_REMOVAL
/* Legacy structure to help keep backward compatible wl tool and tray app */

#define	LEGACY_WL_BSS_INFO_VERSION	107	/* older version of wl_bss_info struct */

typedef struct wl_bss_info_107 {
	uint32		version;		/* version field */
	uint32		length;			/* byte length of data in this record,
						 * starting at version and including IEs
						 */
	struct ether_addr BSSID;
	uint16		beacon_period;		/* units are Kusec */
	uint16		capability;		/* Capability information */
	uint8		SSID_len;
	uint8		SSID[32];
	struct {
		uint	count;			/* # rates in this set */
		uint8	rates[16];		/* rates in 500kbps units w/hi bit set if basic */
	} rateset;				/* supported rates */
	uint8		channel;		/* Channel no. */
	uint16		atim_window;		/* units are Kusec */
	uint8		dtim_period;		/* DTIM period */
	int16		RSSI;			/* receive signal strength (in dBm) */
	int8		phy_noise;		/* noise (in dBm) */
	uint32		ie_length;		/* byte length of Information Elements */
	/* variable length Information Elements */
} wl_bss_info_107_t;

/*
 * Per-BSS information structure.
 */

#define	LEGACY2_WL_BSS_INFO_VERSION	108		/* old version of wl_bss_info struct */

/* BSS info structure
 * Applications MUST CHECK ie_offset field and length field to access IEs and
 * next bss_info structure in a vector (in wl_scan_results_t)
 */
typedef struct wl_bss_info_108 {
	uint32		version;		/* version field */
	uint32		length;			/* byte length of data in this record,
						 * starting at version and including IEs
						 */
	struct ether_addr BSSID;
	uint16		beacon_period;		/* units are Kusec */
	uint16		capability;		/* Capability information */
	uint8		SSID_len;
	uint8		SSID[32];
	struct {
		uint	count;			/* # rates in this set */
		uint8	rates[16];		/* rates in 500kbps units w/hi bit set if basic */
	} rateset;				/* supported rates */
	chanspec_t	chanspec;		/* chanspec for bss */
	uint16		atim_window;		/* units are Kusec */
	uint8		dtim_period;		/* DTIM period */
	int16		RSSI;			/* receive signal strength (in dBm) */
	int8		phy_noise;		/* noise (in dBm) */

	uint8		n_cap;			/* BSS is 802.11N Capable */
	uint32		nbss_cap;		/* 802.11N BSS Capabilities (based on HT_CAP_*) */
	uint8		ctl_ch;			/* 802.11N BSS control channel number */
	uint32		reserved32[1];		/* Reserved for expansion of BSS properties */
	uint8		flags;			/* flags */
	uint8		reserved[3];		/* Reserved for expansion of BSS properties */
	uint8		basic_mcs[MCSSET_LEN];	/* 802.11N BSS required MCS set */

	uint16		ie_offset;		/* offset at which IEs start, from beginning */
	uint32		ie_length;		/* byte length of Information Elements */
	/* Add new fields here */
	/* variable length Information Elements */
} wl_bss_info_108_t;

#endif /* LINUX_POSTMOGRIFY_REMOVAL */

#define	WL_BSS_INFO_VERSION	109		/* current version of wl_bss_info struct */

/* BSS info structure
 * Applications MUST CHECK ie_offset field and length field to access IEs and
 * next bss_info structure in a vector (in wl_scan_results_t)
 */
typedef struct wl_bss_info {
	uint32		version;		/* version field */
	uint32		length;			/* byte length of data in this record,
						 * starting at version and including IEs
						 */
	struct ether_addr BSSID;
	uint16		beacon_period;		/* units are Kusec */
	uint16		capability;		/* Capability information */
	uint8		SSID_len;
	uint8		SSID[32];
	struct {
		uint	count;			/* # rates in this set */
		uint8	rates[16];		/* rates in 500kbps units w/hi bit set if basic */
	} rateset;				/* supported rates */
	chanspec_t	chanspec;		/* chanspec for bss */
	uint16		atim_window;		/* units are Kusec */
	uint8		dtim_period;		/* DTIM period */
	int16		RSSI;			/* receive signal strength (in dBm) */
	int8		phy_noise;		/* noise (in dBm) */

	uint8		n_cap;			/* BSS is 802.11N Capable */
	uint32		nbss_cap;		/* 802.11N+AC BSS Capabilities */
	uint8		ctl_ch;			/* 802.11N BSS control channel number */
	uint8		padding1[3];		/* explicit struct alignment padding */
	uint16		vht_rxmcsmap;		/* VHT rx mcs map */
	uint16		vht_txmcsmap;		/* VHT tx mcs map */
	uint8		flags;			/* flags */
	uint8		vht_cap;		/* BSS is vht capable */
	uint8		reserved[2];		/* Reserved for expansion of BSS properties */
	uint8		basic_mcs[MCSSET_LEN];	/* 802.11N BSS required MCS set */

	uint16		ie_offset;		/* offset at which IEs start, from beginning */
	uint32		ie_length;		/* byte length of Information Elements */
	int16		SNR;			/* average SNR of during frame reception */
	/* Add new fields here */
	/* variable length Information Elements */
} wl_bss_info_t;

/* bss_info_cap_t flags */
#define WL_BSS_FLAGS_FROM_BEACON	0x01	/* bss_info derived from beacon */
#define WL_BSS_FLAGS_FROM_CACHE		0x02	/* bss_info collected from cache */
#define WL_BSS_FLAGS_RSSI_ONCHANNEL 0x04 /* rssi info was received on channel (vs offchannel) */

/* bssinfo flag for nbss_cap */
#define VHT_BI_SGI_80MHZ			0x00000100

#ifndef LINUX_POSTMOGRIFY_REMOVAL

typedef struct wl_bsscfg {
	uint32	wsec;
	uint32	WPA_auth;
	uint32	wsec_index;
	uint32	associated;
	uint32	BSS;
	uint32	phytest_on;
	struct ether_addr	prev_BSSID;
	struct ether_addr	BSSID;
	uint32  targetbss_wpa2_flags;
	uint32 assoc_type;
	uint32 assoc_state;
} wl_bsscfg_t;

typedef struct wl_bss_config {
	uint32	atim_window;
	uint32	beacon_period;
	uint32	chanspec;
} wl_bss_config_t;

#define DLOAD_HANDLER_VER			1	/* Downloader version */
#define DLOAD_FLAG_VER_MASK		0xf000	/* Downloader version mask */
#define DLOAD_FLAG_VER_SHIFT	12	/* Downloader version shift */

#define DL_CRC_NOT_INUSE 			0x0001

/* generic download types & flags */
enum {
	DL_TYPE_UCODE = 1,
	DL_TYPE_CLM = 2
};

/* ucode type values */
enum {
	UCODE_FW,
	INIT_VALS,
	BS_INIT_VALS
};

struct wl_dload_data {
	uint16 flag;
	uint16 dload_type;
	uint32 len;
	uint32 crc;
	uint8  data[1];
};
typedef struct wl_dload_data wl_dload_data_t;

struct wl_ucode_info {
	uint32 ucode_type;
	uint32 num_chunks;
	uint32 chunk_len;
	uint32 chunk_num;
	uint8  data_chunk[1];
};
typedef struct wl_ucode_info wl_ucode_info_t;

struct wl_clm_dload_info {
	uint32 ds_id;
	uint32 clm_total_len;
	uint32 num_chunks;
	uint32 chunk_len;
	uint32 chunk_offset;
	uint8  data_chunk[1];
};
typedef struct wl_clm_dload_info wl_clm_dload_info_t;

#endif /* LINUX_POSTMOGRIFY_REMOVAL */

typedef struct wlc_ssid {
	uint32		SSID_len;
	uchar		SSID[32];
} wlc_ssid_t;

#ifndef LINUX_POSTMOGRIFY_REMOVAL

#define MAX_PREFERRED_AP_NUM     5
typedef struct wlc_fastssidinfo {
	uint32				SSID_channel[MAX_PREFERRED_AP_NUM];
	wlc_ssid_t		SSID_info[MAX_PREFERRED_AP_NUM];
} wlc_fastssidinfo_t;

typedef BWL_PRE_PACKED_STRUCT struct wnm_url {
	uint8   len;
	uint8   data[1];
} BWL_POST_PACKED_STRUCT wnm_url_t;

typedef struct chan_scandata {
	uint8		txpower;
	uint8		pad;
	chanspec_t	channel;	/* Channel num, bw, ctrl_sb and band */
	uint32		channel_mintime;
	uint32		channel_maxtime;
} chan_scandata_t;

typedef enum wl_scan_type {
	EXTDSCAN_FOREGROUND_SCAN,
	EXTDSCAN_BACKGROUND_SCAN,
	EXTDSCAN_FORCEDBACKGROUND_SCAN
} wl_scan_type_t;

#define WLC_EXTDSCAN_MAX_SSID		5

typedef struct wl_extdscan_params {
	int8 		nprobes;		/* 0, passive, otherwise active */
	int8    	split_scan;		/* split scan */
	int8		band;			/* band */
	int8		pad;
	wlc_ssid_t 	ssid[WLC_EXTDSCAN_MAX_SSID]; /* ssid list */
	uint32		tx_rate;		/* in 500ksec units */
	wl_scan_type_t	scan_type;		/* enum */
	int32 		channel_num;
	chan_scandata_t channel_list[1];	/* list of chandata structs */
} wl_extdscan_params_t;

#define WL_EXTDSCAN_PARAMS_FIXED_SIZE 	(sizeof(wl_extdscan_params_t) - sizeof(chan_scandata_t))

#define WL_BSSTYPE_INFRA 1
#define WL_BSSTYPE_INDEP 0
#define WL_BSSTYPE_ANY   2

/* Bitmask for scan_type */
#define WL_SCANFLAGS_PASSIVE	0x01	/* force passive scan */
#define WL_SCANFLAGS_RESERVED	0x02	/* Reserved */
#define WL_SCANFLAGS_PROHIBITED	0x04	/* allow scanning prohibited channels */

#define WL_SCAN_PARAMS_SSID_MAX 	10

typedef struct wl_scan_params {
	wlc_ssid_t ssid;		/* default: {0, ""} */
	struct ether_addr bssid;	/* default: bcast */
	int8 bss_type;			/* default: any,
					 * DOT11_BSSTYPE_ANY/INFRASTRUCTURE/INDEPENDENT
					 */
	uint8 scan_type;		/* flags, 0 use default */
	int32 nprobes;			/* -1 use default, number of probes per channel */
	int32 active_time;		/* -1 use default, dwell time per channel for
					 * active scanning
					 */
	int32 passive_time;		/* -1 use default, dwell time per channel
					 * for passive scanning
					 */
	int32 home_time;		/* -1 use default, dwell time for the home channel
					 * between channel scans
					 */
	int32 channel_num;		/* count of channels and ssids that follow
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
	uint16 channel_list[1];		/* list of chanspecs */
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
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

typedef struct wl_scan_results {
	uint32 buflen;
	uint32 version;
	uint32 count;
	wl_bss_info_t bss_info[1];
} wl_scan_results_t;

#ifndef LINUX_POSTMOGRIFY_REMOVAL
/* size of wl_scan_results not including variable length array */
#define WL_SCAN_RESULTS_FIXED_SIZE (sizeof(wl_scan_results_t) - sizeof(wl_bss_info_t))

/* wl_iscan_results status values */
#define WL_SCAN_RESULTS_SUCCESS	0
#define WL_SCAN_RESULTS_PARTIAL	1
#define WL_SCAN_RESULTS_PENDING	2
#define WL_SCAN_RESULTS_ABORTED	3
#define WL_SCAN_RESULTS_NO_MEM  4

/* Used in EXT_STA */
#define DNGL_RXCTXT_SIZE	45


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
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

#define WL_MAXRATES_IN_SET		16	/* max # of rates in a rateset */
typedef struct wl_rateset {
	uint32	count;			/* # rates in this set */
	uint8	rates[WL_MAXRATES_IN_SET];	/* rates in 500kbps units w/hi bit set if basic */
} wl_rateset_t;

typedef struct wl_rateset_args {
	uint32	count;			/* # rates in this set */
	uint8	rates[WL_MAXRATES_IN_SET];	/* rates in 500kbps units w/hi bit set if basic */
	uint8   mcs[MCSSET_LEN];        /* supported mcs index bit map */
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
	int32 chanspec_num;		/* 0: all available channels,
					 * otherwise count of chanspecs in chanspec_list
					 */
	chanspec_t chanspec_list[1];	/* list of chanspecs */
} wl_assoc_params_t;
#define WL_ASSOC_PARAMS_FIXED_SIZE 	OFFSETOF(wl_assoc_params_t, chanspec_list)

/* used for reassociation/roam to a specific BSSID and channel */
typedef wl_assoc_params_t wl_reassoc_params_t;
#define WL_REASSOC_PARAMS_FIXED_SIZE	WL_ASSOC_PARAMS_FIXED_SIZE

/* used for association to a specific BSSID and channel */
typedef wl_assoc_params_t wl_join_assoc_params_t;
#define WL_JOIN_ASSOC_PARAMS_FIXED_SIZE	WL_ASSOC_PARAMS_FIXED_SIZE

/* used for join with or without a specific bssid and channel list */
typedef struct wl_join_params {
	wlc_ssid_t ssid;
	wl_assoc_params_t params;	/* optional field, but it must include the fixed portion
					 * of the wl_assoc_params_t struct when it does present.
					 */
} wl_join_params_t;

#ifndef  LINUX_POSTMOGRIFY_REMOVAL
#define WL_JOIN_PARAMS_FIXED_SIZE 	(OFFSETOF(wl_join_params_t, params) + \
					 WL_ASSOC_PARAMS_FIXED_SIZE)
/* scan params for extended join */
typedef struct wl_join_scan_params {
	uint8 scan_type;		/* 0 use default, active or passive scan */
	int32 nprobes;			/* -1 use default, number of probes per channel */
	int32 active_time;		/* -1 use default, dwell time per channel for
					 * active scanning
					 */
	int32 passive_time;		/* -1 use default, dwell time per channel
					 * for passive scanning
					 */
	int32 home_time;		/* -1 use default, dwell time for the home channel
					 * between channel scans
					 */
} wl_join_scan_params_t;

/* extended join params */
typedef struct wl_extjoin_params {
	wlc_ssid_t ssid;		/* {0, ""}: wildcard scan */
	wl_join_scan_params_t scan;
	wl_join_assoc_params_t assoc;	/* optional field, but it must include the fixed portion
					 * of the wl_join_assoc_params_t struct when it does
					 * present.
					 */
} wl_extjoin_params_t;
#define WL_EXTJOIN_PARAMS_FIXED_SIZE 	(OFFSETOF(wl_extjoin_params_t, assoc) + \
					 WL_JOIN_ASSOC_PARAMS_FIXED_SIZE)

/* All builds use the new 11ac ratespec/chanspec */
#undef  D11AC_IOTYPES
#define D11AC_IOTYPES

#ifndef D11AC_IOTYPES

/* defines used by the nrate iovar */
#define NRATE_MCS_INUSE	0x00000080	/* MSC in use,indicates b0-6 holds an mcs */
#define NRATE_RATE_MASK 0x0000007f	/* rate/mcs value */
#define NRATE_STF_MASK	0x0000ff00	/* stf mode mask: siso, cdd, stbc, sdm */
#define NRATE_STF_SHIFT	8			/* stf mode shift */
#define NRATE_OVERRIDE	0x80000000	/* bit indicates override both rate & mode */
#define NRATE_OVERRIDE_MCS_ONLY 0x40000000 /* bit indicate to override mcs only */
#define NRATE_SGI_MASK  0x00800000      /* sgi mode */
#define NRATE_SGI_SHIFT 23              /* sgi mode */
#define NRATE_LDPC_CODING 0x00400000    /* bit indicates adv coding in use */
#define NRATE_LDPC_SHIFT 22             /* ldpc shift */

#define NRATE_STF_SISO	0		/* stf mode SISO */
#define NRATE_STF_CDD	1		/* stf mode CDD */
#define NRATE_STF_STBC	2		/* stf mode STBC */
#define NRATE_STF_SDM	3		/* stf mode SDM */

#else /* D11AC_IOTYPES */

/* WL_RSPEC defines for rate information */
#define WL_RSPEC_RATE_MASK      0x000000FF      /* rate or HT MCS value */
#define WL_RSPEC_VHT_MCS_MASK   0x0000000F      /* VHT MCS value */
#define WL_RSPEC_VHT_NSS_MASK   0x000000F0      /* VHT Nss value */
#define WL_RSPEC_VHT_NSS_SHIFT  4               /* VHT Nss value shift */
#define WL_RSPEC_TXEXP_MASK     0x00000300
#define WL_RSPEC_TXEXP_SHIFT    8
#define WL_RSPEC_BW_MASK        0x00070000      /* bandwidth mask */
#define WL_RSPEC_BW_SHIFT       16              /* bandwidth shift */
#define WL_RSPEC_STBC           0x00100000      /* STBC encoding, Nsts = 2 x Nss */
#define WL_RSPEC_LDPC           0x00400000      /* bit indicates adv coding in use */
#define WL_RSPEC_SGI            0x00800000      /* Short GI mode */
#define WL_RSPEC_ENCODING_MASK  0x03000000      /* Encoding of Rate/MCS field */
#define WL_RSPEC_OVERRIDE_RATE  0x40000000      /* bit indicate to override mcs only */
#define WL_RSPEC_OVERRIDE_MODE  0x80000000      /* bit indicates override both rate & mode */

/* WL_RSPEC_ENCODING field defs */
#define WL_RSPEC_ENCODE_RATE    0x00000000      /* Legacy rate is stored in RSPEC_RATE_MASK */
#define WL_RSPEC_ENCODE_HT      0x01000000      /* HT MCS is stored in RSPEC_RATE_MASK */
#define WL_RSPEC_ENCODE_VHT     0x02000000      /* VHT MCS and Nss is stored in RSPEC_RATE_MASK */

/* WL_RSPEC_BW field defs */
#define WL_RSPEC_BW_UNSPECIFIED 0
#define WL_RSPEC_BW_20MHZ       0x00010000
#define WL_RSPEC_BW_40MHZ       0x00020000
#define WL_RSPEC_BW_80MHZ       0x00030000
#define WL_RSPEC_BW_160MHZ      0x00040000

/* Legacy defines for the nrate iovar */
#define OLD_NRATE_MCS_INUSE         0x00000080 /* MSC in use,indicates b0-6 holds an mcs */
#define OLD_NRATE_RATE_MASK         0x0000007f /* rate/mcs value */
#define OLD_NRATE_STF_MASK          0x0000ff00 /* stf mode mask: siso, cdd, stbc, sdm */
#define OLD_NRATE_STF_SHIFT         8          /* stf mode shift */
#define OLD_NRATE_OVERRIDE          0x80000000 /* bit indicates override both rate & mode */
#define OLD_NRATE_OVERRIDE_MCS_ONLY 0x40000000 /* bit indicate to override mcs only */
#define OLD_NRATE_SGI               0x00800000 /* sgi mode */
#define OLD_NRATE_LDPC_CODING       0x00400000 /* bit indicates adv coding in use */

#define OLD_NRATE_STF_SISO	0		/* stf mode SISO */
#define OLD_NRATE_STF_CDD	1		/* stf mode CDD */
#define OLD_NRATE_STF_STBC	2		/* stf mode STBC */
#define OLD_NRATE_STF_SDM	3		/* stf mode SDM */

#endif /* D11AC_IOTYPES */

#define ANTENNA_NUM_1	1		/* total number of antennas to be used */
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
	uint8 ant_config[ANT_SELCFG_MAX];	/* antenna configuration */
	uint8 num_antcfg;	/* number of available antenna configurations */
} wlc_antselcfg_t;

#define HIGHEST_SINGLE_STREAM_MCS	7 /* MCS values greater than this enable multiple streams */

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
#define  CCA_FLAG_IGNORE_INTERFER 	0x20 /* do not exlude channel based on interfer level */

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
	uint8 num_secs;		/* How many secs worth of data */
	cca_congest_t  secs[1];	/* Data */
} cca_congest_channel_req_t;

/* interference source detection and identification mode */
#define ITFR_MODE_DISABLE	0	/* disable feature */
#define ITFR_MODE_MANUAL_ENABLE	1	/* enable manual detection */
#define ITFR_MODE_AUTO_ENABLE	2	/* enable auto detection */

/* interference sources */
enum interference_source {
	ITFR_NONE = 0,		/* interference */
	ITFR_PHONE,		/* wireless phone */
	ITFR_VIDEO_CAMERA,	/* wireless video camera */
	ITFR_MICROWAVE_OVEN,	/* microwave oven */
	ITFR_BABY_MONITOR,	/* wireless baby monitor */
	ITFR_BLUETOOTH,		/* bluetooth */
	ITFR_VIDEO_CAMERA_OR_BABY_MONITOR,	/* wireless camera or baby monitor */
	ITFR_BLUETOOTH_OR_BABY_MONITOR,	/* bluetooth or baby monitor */
	ITFR_VIDEO_CAMERA_OR_PHONE,	/* video camera or phone */
	ITFR_UNIDENTIFIED	/* interference from unidentified source */
};

/* structure for interference source report */
typedef struct {
	uint32 flags;	/* flags.  bit definitions below */
	uint32 source;	/* last detected interference source */
	uint32 timestamp;	/* second timestamp on interferenced flag change */
} interference_source_rep_t;

/* bit definitions for flags in interference source report */
#define ITFR_INTERFERENCED	1	/* interference detected */
#define ITFR_HOME_CHANNEL	2	/* home channel has interference */
#define ITFR_NOISY_ENVIRONMENT	4	/* noisy environemnt so feature stopped */

#endif /* LINUX_POSTMOGRIFY_REMOVAL */

#define WLC_CNTRY_BUF_SZ	4		/* Country string is 3 bytes + NUL */

#ifndef LINUX_POSTMOGRIFY_REMOVAL

typedef struct wl_country {
	char country_abbrev[WLC_CNTRY_BUF_SZ];	/* nul-terminated country code used in
						 * the Country IE
						 */
	int32 rev;				/* revision specifier for ccode
						 * on set, -1 indicates unspecified.
						 * on get, rev >= 0
						 */
	char ccode[WLC_CNTRY_BUF_SZ];		/* nul-terminated built-in country code.
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
	int8	type;
	int8	flags;
	chanspec_t	chanspec;
	uint32	token;		/* token for this measurement */
	uint32	tsf_h;		/* TSF high 32-bits of Measurement start time */
	uint32	tsf_l;		/* TSF low 32-bits */
	uint32	dur;		/* TUs */
} wl_rm_req_elt_t;

typedef struct wl_rm_req {
	uint32	token;		/* overall measurement set token */
	uint32	count;		/* number of measurement requests */
	void	*cb;		/* completion callback function: may be NULL */
	void	*cb_arg;	/* arg to completion callback function */
	wl_rm_req_elt_t	req[1];	/* variable length block of requests */
} wl_rm_req_t;
#define WL_RM_REQ_FIXED_LEN	OFFSETOF(wl_rm_req_t, req)

typedef struct wl_rm_rep_elt {
	int8	type;
	int8	flags;
	chanspec_t	chanspec;
	uint32	token;		/* token for this measurement */
	uint32	tsf_h;		/* TSF high 32-bits of Measurement start time */
	uint32	tsf_l;		/* TSF low 32-bits */
	uint32	dur;		/* TUs */
	uint32	len;		/* byte length of data block */
	uint8	data[1];	/* variable length data block */
} wl_rm_rep_elt_t;
#define WL_RM_REP_ELT_FIXED_LEN	24	/* length excluding data block */

#define WL_RPI_REP_BIN_NUM 8
typedef struct wl_rm_rpi_rep {
	uint8	rpi[WL_RPI_REP_BIN_NUM];
	int8	rpi_max[WL_RPI_REP_BIN_NUM];
} wl_rm_rpi_rep_t;

typedef struct wl_rm_rep {
	uint32	token;		/* overall measurement set token */
	uint32	len;		/* length of measurement report block */
	wl_rm_rep_elt_t	rep[1];	/* variable length block of reports */
} wl_rm_rep_t;
#define WL_RM_REP_FIXED_LEN	8


typedef enum sup_auth_status {
	/* Basic supplicant authentication states */
	WLC_SUP_DISCONNECTED = 0,
	WLC_SUP_CONNECTING,
	WLC_SUP_IDREQUIRED,
	WLC_SUP_AUTHENTICATING,
	WLC_SUP_AUTHENTICATED,
	WLC_SUP_KEYXCHANGE,
	WLC_SUP_KEYED,
	WLC_SUP_TIMEOUT,
	WLC_SUP_LAST_BASIC_STATE,

	/* Extended supplicant authentication states */
	/* Waiting to receive handshake msg M1 */
	WLC_SUP_KEYXCHANGE_WAIT_M1 = WLC_SUP_AUTHENTICATED,
	/* Preparing to send handshake msg M2 */
	WLC_SUP_KEYXCHANGE_PREP_M2 = WLC_SUP_KEYXCHANGE,
	/* Waiting to receive handshake msg M3 */
	WLC_SUP_KEYXCHANGE_WAIT_M3 = WLC_SUP_LAST_BASIC_STATE,
	WLC_SUP_KEYXCHANGE_PREP_M4,	/* Preparing to send handshake msg M4 */
	WLC_SUP_KEYXCHANGE_WAIT_G1,	/* Waiting to receive handshake msg G1 */
	WLC_SUP_KEYXCHANGE_PREP_G2	/* Preparing to send handshake msg G2 */
} sup_auth_status_t;
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* Enumerate crypto algorithms */
#define	CRYPTO_ALGO_OFF			0
#define	CRYPTO_ALGO_WEP1		1
#define	CRYPTO_ALGO_TKIP		2
#define	CRYPTO_ALGO_WEP128		3
#define CRYPTO_ALGO_AES_CCM		4
#define CRYPTO_ALGO_AES_OCB_MSDU	5
#define CRYPTO_ALGO_AES_OCB_MPDU	6
#define CRYPTO_ALGO_NALG		7
#define CRYPTO_ALGO_PMK			12	/* for 802.1x supp to set PMK before 4-way */

#define WSEC_GEN_MIC_ERROR	0x0001
#define WSEC_GEN_REPLAY		0x0002
#define WSEC_GEN_ICV_ERROR	0x0004
#define WSEC_GEN_MFP_ACT_ERROR	0x0008
#define WSEC_GEN_MFP_DISASSOC_ERROR	0x0010
#define WSEC_GEN_MFP_DEAUTH_ERROR	0x0020

#define WL_SOFT_KEY	(1 << 0)	/* Indicates this key is using soft encrypt */
#define WL_PRIMARY_KEY	(1 << 1)	/* Indicates this key is the primary (ie tx) key */
#define WL_KF_RES_4	(1 << 4)	/* Reserved for backward compat */
#define WL_KF_RES_5	(1 << 5)	/* Reserved for backward compat */
#define WL_IBSS_PEER_GROUP_KEY	(1 << 6)	/* Indicates a group key for a IBSS PEER */

typedef struct wl_wsec_key {
	uint32		index;		/* key index */
	uint32		len;		/* key length */
	uint8		data[DOT11_MAX_KEY_SIZE];	/* key data */
	uint32		pad_1[18];
	uint32		algo;		/* CRYPTO_ALGO_AES_CCM, CRYPTO_ALGO_WEP128, etc */
	uint32		flags;		/* misc flags */
	uint32		pad_2[2];
	int		pad_3;
	int		iv_initialized;	/* has IV been initialized already? */
	int		pad_4;
	/* Rx IV */
	struct {
		uint32	hi;		/* upper 32 bits of IV */
		uint16	lo;		/* lower 16 bits of IV */
	} rxiv;
	uint32		pad_5[2];
	struct ether_addr ea;		/* per station */
} wl_wsec_key_t;

#define WSEC_MIN_PSK_LEN	8
#define WSEC_MAX_PSK_LEN	64

/* Flag for key material needing passhash'ing */
#define WSEC_PASSPHRASE		(1<<0)

/* receptacle for WLC_SET_WSEC_PMK parameter */
typedef struct {
	ushort	key_len;		/* octets in key material */
	ushort	flags;			/* key handling qualification */
	uint8	key[WSEC_MAX_PSK_LEN];	/* PMK material */
} wsec_pmk_t;

/* wireless security bitvec */
#define WEP_ENABLED		0x0001
#define TKIP_ENABLED		0x0002
#define AES_ENABLED		0x0004
#define WSEC_SWFLAG		0x0008
#define SES_OW_ENABLED		0x0040	/* to go into transition mode without setting wep */

/* wsec macros for operating on the above definitions */
#define WSEC_WEP_ENABLED(wsec)	((wsec) & WEP_ENABLED)
#define WSEC_TKIP_ENABLED(wsec)	((wsec) & TKIP_ENABLED)
#define WSEC_AES_ENABLED(wsec)	((wsec) & AES_ENABLED)

#define WSEC_ENABLED(wsec)	((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))
#define WSEC_SES_OW_ENABLED(wsec)	((wsec) & SES_OW_ENABLED)

#ifdef MFP
#define MFP_CAPABLE		0x0200
#define MFP_REQUIRED	0x0400
#define MFP_SHA256		0x0800 /* a special configuration for STA for WIFI test tool */
#endif /* MFP */

/* WPA authentication mode bitvec */
#define WPA_AUTH_DISABLED	0x0000	/* Legacy (i.e., non-WPA) */
#define WPA_AUTH_NONE		0x0001	/* none (IBSS) */
#define WPA_AUTH_UNSPECIFIED	0x0002	/* over 802.1x */
#define WPA_AUTH_PSK		0x0004	/* Pre-shared key */
/* #define WPA_AUTH_8021X 0x0020 */	/* 802.1x, reserved */
#define WPA2_AUTH_UNSPECIFIED	0x0040	/* over 802.1x */
#define WPA2_AUTH_PSK		0x0080	/* Pre-shared key */
#define BRCM_AUTH_PSK           0x0100  /* BRCM specific PSK */
#define BRCM_AUTH_DPT		0x0200	/* DPT PSK without group keys */
#define WPA2_AUTH_MFP           0x1000  /* MFP (11w) in contrast to CCX */
#define WPA2_AUTH_TPK		0x2000 	/* TDLS Peer Key */
#define WPA2_AUTH_FT		0x4000 	/* Fast Transition. */
#define WPA_AUTH_PFN_ANY	0xffffffff	/* for PFN, match only ssid */

/* pmkid */
#define	MAXPMKID		16

typedef struct _pmkid {
	struct ether_addr	BSSID;
	uint8			PMKID[WPA2_PMKID_LEN];
} pmkid_t;

typedef struct _pmkid_list {
	uint32	npmkid;
	pmkid_t	pmkid[1];
} pmkid_list_t;

typedef struct _pmkid_cand {
	struct ether_addr	BSSID;
	uint8			preauth;
} pmkid_cand_t;

typedef struct _pmkid_cand_list {
	uint32	npmkid_cand;
	pmkid_cand_t	pmkid_cand[1];
} pmkid_cand_list_t;

#ifndef LINUX_POSTMOGRIFY_REMOVAL
typedef struct wl_assoc_info {
	uint32		req_len;
	uint32		resp_len;
	uint32		flags;
	struct dot11_assoc_req req;
	struct ether_addr reassoc_bssid; /* used in reassoc's */
	struct dot11_assoc_resp resp;
} wl_assoc_info_t;

/* flags */
#define WLC_ASSOC_REQ_IS_REASSOC 0x01 /* assoc req was actually a reassoc */

typedef struct wl_led_info {
	uint32      index;      /* led index */
	uint32      behavior;
	uint8       activehi;
} wl_led_info_t;


/* srom read/write struct passed through ioctl */
typedef struct {
	uint	byteoff;	/* byte offset */
	uint	nbytes;		/* number of bytes */
	uint16	buf[1];
} srom_rw_t;

/* similar cis (srom or otp) struct [iovar: may not be aligned] */
typedef struct {
	uint32	source;		/* cis source */
	uint32	byteoff;	/* byte offset */
	uint32	nbytes;		/* number of bytes */
	/* data follows here */
} cis_rw_t;

#define WLC_CIS_DEFAULT	0	/* built-in default */
#define WLC_CIS_SROM	1	/* source is sprom */
#define WLC_CIS_OTP	2	/* source is otp */

/* R_REG and W_REG struct passed through ioctl */
typedef struct {
	uint32	byteoff;	/* byte offset of the field in d11regs_t */
	uint32	val;		/* read/write value of the field */
	uint32	size;		/* sizeof the field */
	uint	band;		/* band (optional) */
} rw_reg_t;

/* Structure used by GET/SET_ATTEN ioctls - it controls power in b/g-band */
/* PCL - Power Control Loop */
/* current gain setting is replaced by user input */
#define WL_ATTEN_APP_INPUT_PCL_OFF	0	/* turn off PCL, apply supplied input */
#define WL_ATTEN_PCL_ON			1	/* turn on PCL */
/* current gain setting is maintained */
#define WL_ATTEN_PCL_OFF		2	/* turn off PCL. */

typedef struct {
	uint16	auto_ctrl;	/* WL_ATTEN_XX */
	uint16	bb;		/* Baseband attenuation */
	uint16	radio;		/* Radio attenuation */
	uint16	txctl1;		/* Radio TX_CTL1 value */
} atten_t;

/* Per-AC retry parameters */
struct wme_tx_params_s {
	uint8  short_retry;
	uint8  short_fallback;
	uint8  long_retry;
	uint8  long_fallback;
	uint16 max_rate;  /* In units of 512 Kbps */
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
	uint8 val;
	struct ether_addr ea;
} link_val_t;

#define BCM_MAC_STATUS_INDICATION	(0x40010200L)

typedef struct {
	uint16			ver;		/* version of this struct */
	uint16			len;		/* length in bytes of this structure */
	uint16			cap;		/* sta's advertised capabilities */
	uint32			flags;		/* flags defined below */
	uint32			idle;		/* time since data pkt rx'd from sta */
	struct ether_addr	ea;		/* Station address */
	wl_rateset_t		rateset;	/* rateset in use */
	uint32			in;		/* seconds elapsed since associated */
	uint32			listen_interval_inms; /* Min Listen interval in ms for this STA */
	uint32			tx_pkts;	/* # of packets transmitted */
	uint32			tx_failures;	/* # of packets failed */
	uint32			rx_ucast_pkts;	/* # of unicast packets received */
	uint32			rx_mcast_pkts;	/* # of multicast packets received */
	uint32			tx_rate;	/* Rate of last successful tx frame */
	uint32			rx_rate;	/* Rate of last successful rx frame */
	uint32			rx_decrypt_succeeds;	/* # of packet decrypted successfully */
	uint32			rx_decrypt_failures;	/* # of packet decrypted unsuccessfully */
} sta_info_t;

#define WL_OLD_STAINFO_SIZE	OFFSETOF(sta_info_t, tx_pkts)

#define WL_STA_VER		3

/* Flags for sta_info_t indicating properties of STA */
#define WL_STA_BRCM		0x1		/* Running a Broadcom driver */
#define WL_STA_WME		0x2		/* WMM association */
#define WL_STA_UNUSED		0x4
#define WL_STA_AUTHE		0x8		/* Authenticated */
#define WL_STA_ASSOC		0x10		/* Associated */
#define WL_STA_AUTHO		0x20		/* Authorized */
#define WL_STA_WDS		0x40		/* Wireless Distribution System */
#define WL_STA_WDS_LINKUP	0x80		/* WDS traffic/probes flowing properly */
#define WL_STA_PS		0x100		/* STA is in power save mode from AP's viewpoint */
#define WL_STA_APSD_BE		0x200		/* APSD delv/trigger for AC_BE is default enabled */
#define WL_STA_APSD_BK		0x400		/* APSD delv/trigger for AC_BK is default enabled */
#define WL_STA_APSD_VI		0x800		/* APSD delv/trigger for AC_VI is default enabled */
#define WL_STA_APSD_VO		0x1000		/* APSD delv/trigger for AC_VO is default enabled */
#define WL_STA_N_CAP		0x2000		/* STA 802.11n capable */
#define WL_STA_SCBSTATS		0x4000		/* Per STA debug stats */

#define WL_WDS_LINKUP		WL_STA_WDS_LINKUP	/* deprecated */

/* Values for TX Filter override mode */
#define WLC_TXFILTER_OVERRIDE_DISABLED  0
#define WLC_TXFILTER_OVERRIDE_ENABLED   1

#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* Used to get specific STA parameters */
typedef struct {
	uint32	val;
	struct ether_addr ea;
} scb_val_t;

/* Used by iovar versions of some ioctls, i.e. WLC_SCB_AUTHORIZE et al */
typedef struct {
	uint32 code;
	scb_val_t ioctl_args;
} authops_t;

/* channel encoding */
typedef struct channel_info {
	int hw_channel;
	int target_channel;
	int scan_channel;
} channel_info_t;

/* For ioctls that take a list of MAC addresses */
struct maclist {
	uint count;			/* number of MAC addresses */
	struct ether_addr ea[1];	/* variable length array of MAC addresses */
};

#ifndef LINUX_POSTMOGRIFY_REMOVAL
/* get pkt count struct passed through ioctl */
typedef struct get_pktcnt {
	uint rx_good_pkt;
	uint rx_bad_pkt;
	uint tx_good_pkt;
	uint tx_bad_pkt;
	uint rx_ocast_good_pkt; /* unicast packets destined for others */
} get_pktcnt_t;

/* NINTENDO2 */
#define LQ_IDX_MIN              0
#define LQ_IDX_MAX              1
#define LQ_IDX_AVG              2
#define LQ_IDX_SUM              2
#define LQ_IDX_LAST             3
#define LQ_STOP_MONITOR         0
#define LQ_START_MONITOR        1

/* Get averages RSSI, Rx PHY rate and SNR values */
typedef struct {
	int rssi[LQ_IDX_LAST];  /* Array to keep min, max, avg rssi */
	int snr[LQ_IDX_LAST];   /* Array to keep min, max, avg snr */
	int isvalid;            /* Flag indicating whether above data is valid */
} wl_lq_t; /* Link Quality */

typedef enum wl_wakeup_reason_type {
	LCD_ON = 1,
	LCD_OFF,
	DRC1_WAKE,
	DRC2_WAKE,
	REASON_LAST
} wl_wr_type_t;

typedef struct {
/* Unique filter id */
	uint32	id;

/* stores the reason for the last wake up */
	uint8	reason;
} wl_wr_t;

/* Get MAC specific rate histogram command */
typedef struct {
	struct	ether_addr ea;	/* MAC Address */
	uint8	ac_cat;	/* Access Category */
	uint8	num_pkts;	/* Number of packet entries to be averaged */
} wl_mac_ratehisto_cmd_t;	/* MAC Specific Rate Histogram command */

/* Get MAC rate histogram response */
typedef struct {
	uint32	rate[WLC_MAXRATE + 1];	/* Rates */
	uint32	mcs[WL_RATESET_SZ_HT_MCS * WL_TX_CHAINS_MAX];	/* MCS counts */
	uint32	vht[WL_RATESET_SZ_VHT_MCS][WL_TX_CHAINS_MAX];	/* VHT counts */
	uint32	tsf_timer[2][2];	/* Start and End time for 8bytes value */
} wl_mac_ratehisto_res_t;	/* MAC Specific Rate Histogram Response */

/* Values for TX Filter override mode */
#define WLC_TXFILTER_OVERRIDE_DISABLED  0
#define WLC_TXFILTER_OVERRIDE_ENABLED   1

#define WL_IOCTL_ACTION_GET				0x0
#define WL_IOCTL_ACTION_SET				0x1
#define WL_IOCTL_ACTION_OVL_IDX_MASK	0x1e
#define WL_IOCTL_ACTION_OVL_RSV			0x20
#define WL_IOCTL_ACTION_OVL				0x40
#define WL_IOCTL_ACTION_MASK			0x7e
#define WL_IOCTL_ACTION_OVL_SHIFT		1

#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* Linux network driver ioctl encoding */
typedef struct wl_ioctl {
	uint cmd;	/* common ioctl definition */
	void *buf;	/* pointer to user buffer */
	uint len;	/* length of user buffer */
	uint8 set;		/* 1=set IOCTL; 0=query IOCTL */
	uint used;	/* bytes read or written (optional) */
	uint needed;	/* bytes needed (optional) */
} wl_ioctl_t;

#ifndef LINUX_POSTMOGRIFY_REMOVAL

/* reference to wl_ioctl_t struct used by usermode driver */
#define ioctl_subtype	set		/* subtype param */
#define ioctl_pid	used		/* pid param */
#define ioctl_status	needed		/* status param */

/*
 * Structure for passing hardware and software
 * revision info up from the driver.
 */
typedef struct wlc_rev_info {
	uint		vendorid;	/* PCI vendor id */
	uint		deviceid;	/* device id of chip */
	uint		radiorev;	/* radio revision */
	uint		chiprev;	/* chip revision */
	uint		corerev;	/* core revision */
	uint		boardid;	/* board identifier (usu. PCI sub-device id) */
	uint		boardvendor;	/* board vendor (usu. PCI sub-vendor id) */
	uint		boardrev;	/* board revision */
	uint		driverrev;	/* driver version */
	uint		ucoderev;	/* microcode version */
	uint		bus;		/* bus type */
	uint		chipnum;	/* chip number */
	uint		phytype;	/* phy type */
	uint		phyrev;		/* phy revision */
	uint		anarev;		/* anacore rev */
	uint		chippkg;	/* chip package info */
} wlc_rev_info_t;

#define WL_REV_INFO_LEGACY_LENGTH	48

#define WL_BRAND_MAX 10
typedef struct wl_instance_info {
	uint instance;
	char brand[WL_BRAND_MAX];
} wl_instance_info_t;

/* structure to change size of tx fifo */
typedef struct wl_txfifo_sz {
	uint16	magic;
	uint16	fifo;
	uint16	size;
} wl_txfifo_sz_t;
/* magic pattern used for mismatch driver and wl */
#define WL_TXFIFO_SZ_MAGIC	0xa5a5

/* Transfer info about an IOVar from the driver */
/* Max supported IOV name size in bytes, + 1 for nul termination */
#define WLC_IOV_NAME_LEN 30
typedef struct wlc_iov_trx_s {
	uint8 module;
	uint8 type;
	char name[WLC_IOV_NAME_LEN];
} wlc_iov_trx_t;

/* check this magic number */
#define WLC_IOCTL_MAGIC		0x14e46c77

/* bump this number if you change the ioctl interface */
#ifdef D11AC_IOTYPES
#define WLC_IOCTL_VERSION	2
#define WLC_IOCTL_VERSION_LEGACY_IOTYPES	1
#else
#define WLC_IOCTL_VERSION	1
#endif /* D11AC_IOTYPES */
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

#define	WLC_IOCTL_MAXLEN		8192	/* max length ioctl buffer required */
#define	WLC_IOCTL_SMLEN			256	/* "small" length ioctl buffer required */
#define WLC_IOCTL_MEDLEN		1536    /* "med" length ioctl buffer required */
#if defined(LCNCONF) || defined(LCN40CONF)
#define WLC_SAMPLECOLLECT_MAXLEN	8192	/* Max Sample Collect buffer */
#else
#define WLC_SAMPLECOLLECT_MAXLEN	10240	/* Max Sample Collect buffer for two cores */
#endif

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
/* #define WLC_OVERLAY_IOCTL			11 */ /* not supported */
#define WLC_GET_RATE				12
#define WLC_GET_MAX_RATE			13
#define WLC_GET_INSTANCE			14
/* #define WLC_GET_FRAG				15 */ /* no longer supported */
/* #define WLC_SET_FRAG				16 */ /* no longer supported */
/* #define WLC_GET_RTS				17 */ /* no longer supported */
/* #define WLC_SET_RTS				18 */ /* no longer supported */
#define WLC_GET_INFRA				19
#define WLC_SET_INFRA				20
#define WLC_GET_AUTH				21
#define WLC_SET_AUTH				22
#define WLC_GET_BSSID				23
#define WLC_SET_BSSID				24
#define WLC_GET_SSID				25
#define WLC_SET_SSID				26
#define WLC_RESTART				27
#define WLC_TERMINATED             		28
/* #define WLC_DUMP_SCB				28 */ /* no longer supported */
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
/* #define WLC_GET_WEP				42 */ /* no longer supported */
/* #define WLC_SET_WEP				43 */ /* no longer supported */
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
/* #define WLC_GET_TXPWR			65 */ /* no longer supported */
/* #define WLC_SET_TXPWR			66 */ /* no longer supported */
#define WLC_GET_CLOSED				67
#define WLC_SET_CLOSED				68
#define WLC_GET_MACLIST				69
#define WLC_SET_MACLIST				70
#define WLC_GET_RATESET				71
#define WLC_SET_RATESET				72
/* #define WLC_GET_LOCALE			73 */ /* no longer supported */
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
/* #define WLC_GET_D11CNTS			89 */ /* -> "counters" iovar */
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
/* #define WLC_RESET_D11CNTS			104 */ /* -> "reset_d11cnts" iovar */
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

#ifndef LINUX_POSTMOGRIFY_REMOVAL
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
/* #define WLC_GET_MRATE			153 */ /* no longer supported */
/* #define WLC_SET_MRATE			154 */ /* no longer supported */
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
/* #define WLC_SET_FAST_TIMER			173 */ /* no longer supported */
/* #define WLC_GET_FAST_TIMER			174 */ /* no longer supported */
/* #define WLC_SET_SLOW_TIMER			175 */ /* no longer supported */
/* #define WLC_GET_SLOW_TIMER			176 */ /* no longer supported */
/* #define WLC_DUMP_PHYREGS			177 */ /* no longer supported */
#define WLC_GET_PROTECTION_CONTROL		178
#define WLC_SET_PROTECTION_CONTROL		179
#endif /* LINUX_POSTMOGRIFY_REMOVAL  */
#define WLC_GET_PHYLIST				180
#ifndef LINUX_POSTMOGRIFY_REMOVAL
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
/* #define WLC_GET_GMODE_PROTECTION_CTS		198 */ /* no longer supported */
/* #define WLC_SET_GMODE_PROTECTION_CTS		199 */ /* no longer supported */
#define WLC_SET_WSEC_TEST			200
#endif /* LINUX_POSTMOGRIFY_REMOVAL */
#define WLC_SCB_DEAUTHENTICATE_FOR_REASON	201
#ifndef LINUX_POSTMOGRIFY_REMOVAL
#define WLC_TKIP_COUNTERMEASURES		202
#define WLC_GET_PIOMODE				203
#define WLC_SET_PIOMODE				204
#define WLC_SET_ASSOC_PREFER			205
#define WLC_GET_ASSOC_PREFER			206
#define WLC_SET_ROAM_PREFER			207
#define WLC_GET_ROAM_PREFER			208
#define WLC_SET_LED				209
#define WLC_GET_LED				210
#define WLC_GET_INTERFERENCE_MODE		211
#define WLC_SET_INTERFERENCE_MODE		212
#define WLC_GET_CHANNEL_QA			213
#define WLC_START_CHANNEL_QA			214
#define WLC_GET_CHANNEL_SEL			215
#define WLC_START_CHANNEL_SEL			216
#endif /* LINUX_POSTMOGRIFY_REMOVAL */
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
/* #define WLC_SET_GLACIAL_TIMER		233 */ /* no longer supported */
/* #define WLC_GET_GLACIAL_TIMER		234 */ /* no longer supported */
#define WLC_GET_KEY_PRIMARY			235
#define WLC_SET_KEY_PRIMARY			236

#ifndef LINUX_POSTMOGRIFY_REMOVAL

/* #define WLC_DUMP_RADIOREGS			237 */ /* no longer supported */
#define WLC_GET_ACI_ARGS			238
#define WLC_SET_ACI_ARGS			239
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
#endif /* LINUX_POSTMOGRIFY_REMOVAL */
#define WLC_GET_VAR				262	/* get value of named variable */
#define WLC_SET_VAR				263	/* set named variable to value */
#ifndef LINUX_POSTMOGRIFY_REMOVAL
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
#define WLC_GET_NBANDS				284	/* for Dongle EXT_STA support */
#define WLC_GET_BANDSTATES			285	/* for Dongle EXT_STA support */
#define WLC_GET_WLC_BSS_INFO			286	/* for Dongle EXT_STA support */
#define WLC_GET_ASSOC_INFO			287	/* for Dongle EXT_STA support */
#define WLC_GET_OID_PHY				288	/* for Dongle EXT_STA support */
#define WLC_SET_OID_PHY				289	/* for Dongle EXT_STA support */
#define WLC_SET_ASSOC_TIME			290	/* for Dongle EXT_STA support */
#define WLC_GET_DESIRED_SSID			291	/* for Dongle EXT_STA support */
#define WLC_GET_CHANSPEC			292	/* for Dongle EXT_STA support */
#define WLC_GET_ASSOC_STATE			293	/* for Dongle EXT_STA support */
#define WLC_SET_PHY_STATE			294	/* for Dongle EXT_STA support */
#define WLC_GET_SCAN_PENDING			295	/* for Dongle EXT_STA support */
#define WLC_GET_SCANREQ_PENDING			296	/* for Dongle EXT_STA support */
#define WLC_GET_PREV_ROAM_REASON		297	/* for Dongle EXT_STA support */
#define WLC_SET_PREV_ROAM_REASON		298	/* for Dongle EXT_STA support */
#define WLC_GET_BANDSTATES_PI			299	/* for Dongle EXT_STA support */
#define WLC_GET_PHY_STATE			300	/* for Dongle EXT_STA support */
#define WLC_GET_BSS_WPA_RSN			301	/* for Dongle EXT_STA support */
#define WLC_GET_BSS_WPA2_RSN			302	/* for Dongle EXT_STA support */
#define WLC_GET_BSS_BCN_TS			303	/* for Dongle EXT_STA support */
#define WLC_GET_INT_DISASSOC			304	/* for Dongle EXT_STA support */
#define WLC_SET_NUM_PEERS			305     /* for Dongle EXT_STA support */
#define WLC_GET_NUM_BSS				306	/* for Dongle EXT_STA support */
#define WLC_PHY_SAMPLE_COLLECT			307	/* phy sample collect mode */
/* #define WLC_UM_PRIV				308 */	/* Deprecated: usermode driver */
#define WLC_GET_CMD				309
/* #define WLC_LAST				310 */	/* Never used - can be reused */
#define WLC_SET_INTERFERENCE_OVERRIDE_MODE	311	/* set inter mode override */
#define WLC_GET_INTERFERENCE_OVERRIDE_MODE	312	/* get inter mode override */
/* #define WLC_GET_WAI_RESTRICT			313 */	/* for WAPI, deprecated use iovar instead */
/* #define WLC_SET_WAI_RESTRICT			314 */	/* for WAPI, deprecated use iovar instead */
/* #define WLC_SET_WAI_REKEY			315 */	/* for WAPI, deprecated use iovar instead */
#define WLC_SET_NAT_CONFIG			316	/* for configuring NAT filter driver */
#define WLC_GET_NAT_STATE			317
#define WLC_LAST				318

#ifndef EPICTRL_COOKIE
#define EPICTRL_COOKIE		0xABADCEDE
#endif

/* vx wlc ioctl's offset */
#define CMN_IOCTL_OFF 0x180

/*
 * custom OID support
 *
 * 0xFF - implementation specific OID
 * 0xE4 - first byte of Broadcom PCI vendor ID
 * 0x14 - second byte of Broadcom PCI vendor ID
 * 0xXX - the custom OID number
 */

/* begin 0x1f values beyond the start of the ET driver range. */
#define WL_OID_BASE		0xFFE41420

/* NDIS overrides */
#define OID_WL_GETINSTANCE	(WL_OID_BASE + WLC_GET_INSTANCE)
#define OID_WL_GET_FORCELINK	(WL_OID_BASE + WLC_GET_FORCELINK)
#define OID_WL_SET_FORCELINK	(WL_OID_BASE + WLC_SET_FORCELINK)
#define	OID_WL_ENCRYPT_STRENGTH	(WL_OID_BASE + WLC_ENCRYPT_STRENGTH)
#define OID_WL_DECRYPT_STATUS	(WL_OID_BASE + WLC_DECRYPT_STATUS)
#define OID_LEGACY_LINK_BEHAVIOR (WL_OID_BASE + WLC_LEGACY_LINK_BEHAVIOR)
#define OID_WL_NDCONFIG_ITEM	(WL_OID_BASE + WLC_NDCONFIG_ITEM)

/* EXT_STA Dongle suuport */
#define OID_STA_CHANSPEC	(WL_OID_BASE + WLC_GET_CHANSPEC)
#define OID_STA_NBANDS		(WL_OID_BASE + WLC_GET_NBANDS)
#define OID_STA_GET_PHY		(WL_OID_BASE + WLC_GET_OID_PHY)
#define OID_STA_SET_PHY		(WL_OID_BASE + WLC_SET_OID_PHY)
#define OID_STA_ASSOC_TIME	(WL_OID_BASE + WLC_SET_ASSOC_TIME)
#define OID_STA_DESIRED_SSID	(WL_OID_BASE + WLC_GET_DESIRED_SSID)
#define OID_STA_SET_PHY_STATE	(WL_OID_BASE + WLC_SET_PHY_STATE)
#define OID_STA_SCAN_PENDING	(WL_OID_BASE + WLC_GET_SCAN_PENDING)
#define OID_STA_SCANREQ_PENDING (WL_OID_BASE + WLC_GET_SCANREQ_PENDING)
#define OID_STA_GET_ROAM_REASON (WL_OID_BASE + WLC_GET_PREV_ROAM_REASON)
#define OID_STA_SET_ROAM_REASON (WL_OID_BASE + WLC_SET_PREV_ROAM_REASON)
#define OID_STA_GET_PHY_STATE	(WL_OID_BASE + WLC_GET_PHY_STATE)
#define OID_STA_INT_DISASSOC	(WL_OID_BASE + WLC_GET_INT_DISASSOC)
#define OID_STA_SET_NUM_PEERS	(WL_OID_BASE + WLC_SET_NUM_PEERS)
#define OID_STA_GET_NUM_BSS	(WL_OID_BASE + WLC_GET_NUM_BSS)

/* NAT filter driver support */
#define OID_NAT_SET_CONFIG	(WL_OID_BASE + WLC_SET_NAT_CONFIG)
#define OID_NAT_GET_STATE	(WL_OID_BASE + WLC_GET_NAT_STATE)

#define WL_DECRYPT_STATUS_SUCCESS	1
#define WL_DECRYPT_STATUS_FAILURE	2
#define WL_DECRYPT_STATUS_UNKNOWN	3

/* allows user-mode app to poll the status of USB image upgrade */
#define WLC_UPGRADE_SUCCESS			0
#define WLC_UPGRADE_PENDING			1

#ifdef CONFIG_USBRNDIS_RETAIL
/* struct passed in for WLC_NDCONFIG_ITEM */
typedef struct {
	char *name;
	void *param;
} ndconfig_item_t;
#endif


/* WLC_GET_AUTH, WLC_SET_AUTH values */
#define WL_AUTH_OPEN_SYSTEM		0	/* d11 open authentication */
#define WL_AUTH_SHARED_KEY		1	/* d11 shared authentication */
#ifdef BCM4330_CHIP
#define WL_AUTH_OPEN_SHARED		2	/* try open, then shared if open failed w/rc 13 */
#else
/* BCM4334(Phoenex branch) value changed to 3 */
#define WL_AUTH_OPEN_SHARED		3	/* try open, then shared if open failed w/rc 13 */
#endif
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

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
#define WL_TXPWR_NEG   (1U<<30)

#ifndef LINUX_POSTMOGRIFY_REMOVAL
#define WL_PHY_PAVARS_LEN	32	/* Phy type, Band range, chain, a1[0], b0[0], b1[0] ... */

#define WL_PHY_PAVAR_VER	1	/* pavars version */

typedef struct wl_po {
	uint16	phy_type;	/* Phy type */
	uint16	band;
	uint16	cckpo;
	uint32	ofdmpo;
	uint16	mcspo[8];
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
#define WL_DIAG_LOOPBACK_EXT			8	/* enhenced d11 loopback data test */

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

#define WL_CHAN_FREQ_RANGE_5G_BAND0     1
#define WL_CHAN_FREQ_RANGE_5G_BAND1     2
#define WL_CHAN_FREQ_RANGE_5G_BAND2     3
#define WL_CHAN_FREQ_RANGE_5G_BAND3     4

#define WL_CHAN_FREQ_RANGE_5G_4BAND    	5
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* phy types (returned by WLC_GET_PHYTPE) */
#define	WLC_PHY_TYPE_A		0
#define	WLC_PHY_TYPE_B		1
#define	WLC_PHY_TYPE_G		2
#define	WLC_PHY_TYPE_N		4
#define	WLC_PHY_TYPE_LP		5
#define	WLC_PHY_TYPE_SSN	6
#define	WLC_PHY_TYPE_HT		7
#define	WLC_PHY_TYPE_LCN	8
#define	WLC_PHY_TYPE_LCN40	10
#define WLC_PHY_TYPE_AC		11
#define	WLC_PHY_TYPE_NULL	0xf

/* Values for PM */
#define PM_OFF	0
#define PM_MAX	1
#define PM_FAST 2
#define PM_FORCE_OFF 3 		/* use this bit to force PM off even bt is active */

#ifndef LINUX_POSTMOGRIFY_REMOVAL
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

/* values for band specific 40MHz capabilities (deprecated) */
#define WLC_N_BW_20ALL			0
#define WLC_N_BW_40ALL			1
#define WLC_N_BW_20IN2G_40IN5G		2

#define WLC_BW_20MHZ_BIT		(1<<0)
#define WLC_BW_40MHZ_BIT		(1<<1)
#define WLC_BW_80MHZ_BIT		(1<<2)

/* Bandwidth capabilities */
#define WLC_BW_CAP_20MHZ		(WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_40MHZ		(WLC_BW_40MHZ_BIT|WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_80MHZ		(WLC_BW_80MHZ_BIT|WLC_BW_40MHZ_BIT|WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_UNRESTRICTED		0xFF

#define WL_BW_CAP_20MHZ(bw_cap)	(((bw_cap) & WLC_BW_20MHZ_BIT) ? TRUE : FALSE)
#define WL_BW_CAP_40MHZ(bw_cap)	(((bw_cap) & WLC_BW_40MHZ_BIT) ? TRUE : FALSE)
#define WL_BW_CAP_80MHZ(bw_cap)	(((bw_cap) & WLC_BW_80MHZ_BIT) ? TRUE : FALSE)

/* values to force tx/rx chain */
#define WLC_N_TXRX_CHAIN0		0
#define WLC_N_TXRX_CHAIN1		1

/* bitflags for SGI support (sgi_rx iovar) */
#define WLC_N_SGI_20			0x01
#define WLC_N_SGI_40			0x02
#define WLC_VHT_SGI_80			0x04

/* when sgi_tx==WLC_SGI_ALL, bypass rate selection, enable sgi for all mcs */
#define WLC_SGI_ALL				0x02

#define LISTEN_INTERVAL			10
/* interference mitigation options */
#define	INTERFERE_OVRRIDE_OFF	-1	/* interference override off */
#define	INTERFERE_NONE	0	/* off */
#define	NON_WLAN	1	/* foreign/non 802.11 interference, no auto detect */
#define	WLAN_MANUAL	2	/* ACI: no auto detection */
#define	WLAN_AUTO	3	/* ACI: auto detect */
#define	WLAN_AUTO_W_NOISE	4	/* ACI: auto - detect and non 802.11 interference */
#define AUTO_ACTIVE	(1 << 7) /* Auto is currently active */

typedef struct wl_aci_args {
	int enter_aci_thresh; /* Trigger level to start detecting ACI */
	int exit_aci_thresh; /* Trigger level to exit ACI mode */
	int usec_spin; /* microsecs to delay between rssi samples */
	int glitch_delay; /* interval between ACI scans when glitch count is consistently high */
	uint16 nphy_adcpwr_enter_thresh;	/* ADC power to enter ACI mitigation mode */
	uint16 nphy_adcpwr_exit_thresh;	/* ADC power to exit ACI mitigation mode */
	uint16 nphy_repeat_ctr;		/* Number of tries per channel to compute power */
	uint16 nphy_num_samples;	/* Number of samples to compute power on one channel */
	uint16 nphy_undetect_window_sz;	/* num of undetects to exit ACI Mitigation mode */
	uint16 nphy_b_energy_lo_aci;	/* low ACI power energy threshold for bphy */
	uint16 nphy_b_energy_md_aci;	/* mid ACI power energy threshold for bphy */
	uint16 nphy_b_energy_hi_aci;	/* high ACI power energy threshold for bphy */
	uint16 nphy_noise_noassoc_glitch_th_up; /* wl interference 4 */
	uint16 nphy_noise_noassoc_glitch_th_dn;
	uint16 nphy_noise_assoc_glitch_th_up;
	uint16 nphy_noise_assoc_glitch_th_dn;
	uint16 nphy_noise_assoc_aci_glitch_th_up;
	uint16 nphy_noise_assoc_aci_glitch_th_dn;
	uint16 nphy_noise_assoc_enter_th;
	uint16 nphy_noise_noassoc_enter_th;
	uint16 nphy_noise_assoc_rx_glitch_badplcp_enter_th;
	uint16 nphy_noise_noassoc_crsidx_incr;
	uint16 nphy_noise_assoc_crsidx_incr;
	uint16 nphy_noise_crsidx_decr;
} wl_aci_args_t;

#define TRIGGER_NOW				0
#define TRIGGER_CRS				0x01
#define TRIGGER_CRSDEASSERT			0x02
#define TRIGGER_GOODFCS				0x04
#define TRIGGER_BADFCS				0x08
#define TRIGGER_BADPLCP				0x10
#define TRIGGER_CRSGLITCH			0x20
#define WL_ACI_ARGS_LEGACY_LENGTH	16	/* bytes of pre NPHY aci args */
#define	WL_SAMPLECOLLECT_T_VERSION	2	/* version of wl_samplecollect_args_t struct */
typedef struct wl_samplecollect_args {
	/* version 0 fields */
	uint8 coll_us;
	int cores;
	/* add'l version 1 fields */
	uint16 version;     /* see definition of WL_SAMPLECOLLECT_T_VERSION */
	uint16 length;      /* length of entire structure */
	int8 trigger;
	uint16 timeout;
	uint16 mode;
	uint32 pre_dur;
	uint32 post_dur;
	uint8 gpio_sel;
	bool downsamp;
	bool be_deaf;
	bool agc;		/* loop from init gain and going down */
	bool filter;		/* override high pass corners to lowest */
	/* add'l version 2 fields */
	uint8 trigger_state;
	uint8 module_sel1;
	uint8 module_sel2;
	uint16 nsamps;
} wl_samplecollect_args_t;

#define	WL_SAMPLEDATA_HEADER_TYPE	1
#define WL_SAMPLEDATA_HEADER_SIZE	80	/* sample collect header size (bytes) */
#define	WL_SAMPLEDATA_TYPE		2
#define	WL_SAMPLEDATA_SEQ		0xff	/* sequence # */
#define	WL_SAMPLEDATA_MORE_DATA		0x100	/* more data mask */
#define	WL_SAMPLEDATA_T_VERSION		1	/* version of wl_samplecollect_args_t struct */
/* version for unpacked sample data, int16 {(I,Q),Core(0..N)} */
#define	WL_SAMPLEDATA_T_VERSION_SPEC_AN 2

typedef struct wl_sampledata {
	uint16 version;	/* structure version */
	uint16 size;	/* size of structure */
	uint16 tag;	/* Header/Data */
	uint16 length;	/* data length */
	uint32 flag;	/* bit def */
} wl_sampledata_t;

/* wl_radar_args_t */
typedef struct {
	int npulses; 	/* required number of pulses at n * t_int */
	int ncontig; 	/* required number of pulses at t_int */
	int min_pw; 	/* minimum pulse width (20 MHz clocks) */
	int max_pw; 	/* maximum pulse width (20 MHz clocks) */
	uint16 thresh0;	/* Radar detection, thresh 0 */
	uint16 thresh1;	/* Radar detection, thresh 1 */
	uint16 blank;	/* Radar detection, blank control */
	uint16 fmdemodcfg;	/* Radar detection, fmdemod config */
	int npulses_lp;  /* Radar detection, minimum long pulses */
	int min_pw_lp; /* Minimum pulsewidth for long pulses */
	int max_pw_lp; /* Maximum pulsewidth for long pulses */
	int min_fm_lp; /* Minimum fm for long pulses */
	int max_span_lp;  /* Maximum deltat for long pulses */
	int min_deltat; /* Minimum spacing between pulses */
	int max_deltat; /* Maximum spacing between pulses */
	uint16 autocorr;	/* Radar detection, autocorr on or off */
	uint16 st_level_time;	/* Radar detection, start_timing level */
	uint16 t2_min; /* minimum clocks needed to remain in state 2 */
	uint32 version; /* version */
	uint32 fra_pulse_err;	/* sample error margin for detecting French radar pulsed */
	int npulses_fra;  /* Radar detection, minimum French pulses set */
	int npulses_stg2;  /* Radar detection, minimum staggered-2 pulses set */
	int npulses_stg3;  /* Radar detection, minimum staggered-3 pulses set */
	uint16 percal_mask;	/* defines which period cal is masked from radar detection */
	int quant;	/* quantization resolution to pulse positions */
	uint32 min_burst_intv_lp;	/* minimum burst to burst interval for bin3 radar */
	uint32 max_burst_intv_lp;	/* maximum burst to burst interval for bin3 radar */
	int nskip_rst_lp;	/* number of skipped pulses before resetting lp buffer */
	int max_pw_tol;	/* maximum tollerance allowed in detected pulse width for radar detection */
	uint16 feature_mask; /* 16-bit mask to specify enabled features */
} wl_radar_args_t;

#define WL_RADAR_ARGS_VERSION 2

typedef struct {
	uint32 version; /* version */
	uint16 thresh0_20_lo;	/* Radar detection, thresh 0 (range 5250-5350MHz) for BW 20MHz */
	uint16 thresh1_20_lo;	/* Radar detection, thresh 1 (range 5250-5350MHz) for BW 20MHz */
	uint16 thresh0_40_lo;	/* Radar detection, thresh 0 (range 5250-5350MHz) for BW 40MHz */
	uint16 thresh1_40_lo;	/* Radar detection, thresh 1 (range 5250-5350MHz) for BW 40MHz */
	uint16 thresh0_80_lo;	/* Radar detection, thresh 0 (range 5250-5350MHz) for BW 80MHz */
	uint16 thresh1_80_lo;	/* Radar detection, thresh 1 (range 5250-5350MHz) for BW 80MHz */
	uint16 thresh0_160_lo;	/* Radar detection, thresh 0 (range 5250-5350MHz) for BW 160MHz */
	uint16 thresh1_160_lo;	/* Radar detection, thresh 1 (range 5250-5350MHz) for BW 160MHz */
	uint16 thresh0_20_hi;	/* Radar detection, thresh 0 (range 5470-5725MHz) for BW 20MHz */
	uint16 thresh1_20_hi;	/* Radar detection, thresh 1 (range 5470-5725MHz) for BW 20MHz */
	uint16 thresh0_40_hi;	/* Radar detection, thresh 0 (range 5470-5725MHz) for BW 40MHz */
	uint16 thresh1_40_hi;	/* Radar detection, thresh 1 (range 5470-5725MHz) for BW 40MHz */
	uint16 thresh0_80_hi;	/* Radar detection, thresh 0 (range 5470-5725MHz) for BW 80MHz */
	uint16 thresh1_80_hi;	/* Radar detection, thresh 1 (range 5470-5725MHz) for BW 80MHz */
	uint16 thresh0_160_hi;	/* Radar detection, thresh 0 (range 5470-5725MHz) for BW 160MHz */
	uint16 thresh1_160_hi;	/* Radar detection, thresh 1 (range 5470-5725MHz) for BW 160MHz */
} wl_radar_thr_t;

#define WL_RADAR_THR_VERSION	2
#define WL_THRESHOLD_LO_BAND	70	/* range from 5250MHz - 5350MHz */

/* radar iovar SET defines */
#define WL_RADAR_DETECTOR_OFF		0	/* radar detector off */
#define WL_RADAR_DETECTOR_ON		1	/* radar detector on */
#define WL_RADAR_SIMULATED		2	/* force radar detector to declare
						 * detection once
						 */
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
	uint32	version;		/* version field */
	uint32	count;			/* number of valid antenna rssi */
	int8 rssi_ant[WL_RSSI_ANT_MAX];	/* rssi per antenna */
} wl_rssi_ant_t;

/* dfs_status iovar-related defines */

/* cac - channel availability check,
 * ism - in-service monitoring
 * csa - channel switching announcement
 */

/* cac state values */
#define WL_DFS_CACSTATE_IDLE		0	/* state for operating in non-radar channel */
#define	WL_DFS_CACSTATE_PREISM_CAC	1	/* CAC in progress */
#define WL_DFS_CACSTATE_ISM		2	/* ISM in progress */
#define WL_DFS_CACSTATE_CSA		3	/* csa */
#define WL_DFS_CACSTATE_POSTISM_CAC	4	/* ISM CAC */
#define WL_DFS_CACSTATE_PREISM_OOC	5	/* PREISM OOC */
#define WL_DFS_CACSTATE_POSTISM_OOC	6	/* POSTISM OOC */
#define WL_DFS_CACSTATES		7	/* this many states exist */

/* data structure used in 'dfs_status' wl interface, which is used to query dfs status */
typedef struct {
	uint state;		/* noted by WL_DFS_CACSTATE_XX. */
	uint duration;		/* time spent in ms in state. */
	/* as dfs enters ISM state, it removes the operational channel from quiet channel
	 * list and notes the channel in channel_cleared. set to 0 if no channel is cleared
	 */
	chanspec_t chanspec_cleared;
	/* chanspec cleared used to be a uint, add another to uint16 to maintain size */
	uint16 pad;
} wl_dfs_status_t;

#define NUM_PWRCTRL_RATES 12

typedef struct {
	uint8 txpwr_band_max[NUM_PWRCTRL_RATES];	/* User set target */
	uint8 txpwr_limit[NUM_PWRCTRL_RATES];		/* reg and local power limit */
	uint8 txpwr_local_max;				/* local max according to the AP */
	uint8 txpwr_local_constraint;			/* local constraint according to the AP */
	uint8 txpwr_chan_reg_max;			/* Regulatory max for this channel */
	uint8 txpwr_target[2][NUM_PWRCTRL_RATES];	/* Latest target for 2.4 and 5 Ghz */
	uint8 txpwr_est_Pout[2];			/* Latest estimate for 2.4 and 5 Ghz */
	uint8 txpwr_opo[NUM_PWRCTRL_RATES];		/* On G phy, OFDM power offset */
	uint8 txpwr_bphy_cck_max[NUM_PWRCTRL_RATES];	/* Max CCK power for this band (SROM) */
	uint8 txpwr_bphy_ofdm_max;			/* Max OFDM power for this band (SROM) */
	uint8 txpwr_aphy_max[NUM_PWRCTRL_RATES];	/* Max power for A band (SROM) */
	int8  txpwr_antgain[2];				/* Ant gain for each band - from SROM */
	uint8 txpwr_est_Pout_gofdm;			/* Pwr estimate for 2.4 OFDM */
} tx_power_legacy_t;

#define WL_TX_POWER_RATES_LEGACY    45
#define WL_TX_POWER_MCS20_FIRST         12
#define WL_TX_POWER_MCS20_NUM           16
#define WL_TX_POWER_MCS40_FIRST         28
#define WL_TX_POWER_MCS40_NUM           17

typedef struct {
	uint32 flags;
	chanspec_t chanspec;                 /* txpwr report for this channel */
	chanspec_t local_chanspec;           /* channel on which we are associated */
	uint8 local_max;                 /* local max according to the AP */
	uint8 local_constraint;              /* local constraint according to the AP */
	int8  antgain[2];                /* Ant gain for each band - from SROM */
	uint8 rf_cores;                  /* count of RF Cores being reported */
	uint8 est_Pout[4];                           /* Latest tx power out estimate per RF
							  * chain without adjustment
							  */
	uint8 est_Pout_cck;                          /* Latest CCK tx power out estimate */
	uint8 user_limit[WL_TX_POWER_RATES_LEGACY];  /* User limit */
	uint8 reg_limit[WL_TX_POWER_RATES_LEGACY];   /* Regulatory power limit */
	uint8 board_limit[WL_TX_POWER_RATES_LEGACY]; /* Max power board can support (SROM) */
	uint8 target[WL_TX_POWER_RATES_LEGACY];      /* Latest target power */
} tx_power_legacy2_t;

/* TX Power index defines */
#define WL_NUM_RATES_CCK			4 /* 1, 2, 5.5, 11 Mbps */
#define WL_NUM_RATES_OFDM			8 /* 6, 9, 12, 18, 24, 36, 48, 54 Mbps SISO/CDD */
#define WL_NUM_RATES_MCS_1STREAM	8 /* MCS 0-7 1-stream rates - SISO/CDD/STBC/MCS */
#define WL_NUM_RATES_EXTRA_VHT		2 /* Additional VHT 11AC rates */
#define WL_NUM_RATES_VHT			10
#define WL_NUM_RATES_MCS32			1

#define WLC_NUM_RATES_CCK       WL_NUM_RATES_CCK
#define WLC_NUM_RATES_OFDM      WL_NUM_RATES_OFDM
#define WLC_NUM_RATES_MCS_1_STREAM  WL_NUM_RATES_MCS_1STREAM
#define WLC_NUM_RATES_MCS_2_STREAM  WL_NUM_RATES_MCS_1STREAM
#define WLC_NUM_RATES_MCS32     WL_NUM_RATES_MCS32
#define WL_TX_POWER_CCK_NUM     WL_NUM_RATES_CCK
#define WL_TX_POWER_OFDM_NUM        WL_NUM_RATES_OFDM
#define WL_TX_POWER_MCS_1_STREAM_NUM    WL_NUM_RATES_MCS_1STREAM
#define WL_TX_POWER_MCS_2_STREAM_NUM    WL_NUM_RATES_MCS_1STREAM
#define WL_TX_POWER_MCS_32_NUM      WL_NUM_RATES_MCS32

#define WL_NUM_2x2_ELEMENTS		4
#define WL_NUM_3x3_ELEMENTS		6

typedef struct txppr {
	/* start of 20MHz tx power limits */
	uint8 b20_1x1dsss[WL_NUM_RATES_CCK];		/* Legacy CCK/DSSS */
	uint8 b20_1x1ofdm[WL_NUM_RATES_OFDM];		/* Legacy OFDM transmission */
	uint8 b20_1x1mcs0[WL_NUM_RATES_MCS_1STREAM];		/* SISO MCS 0-7 */

	uint8 b20_1x2dsss[WL_NUM_RATES_CCK];		/* Legacy CCK/DSSS */
	uint8 b20_1x2cdd_ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM CDD transmission */
	uint8 b20_1x2cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* CDD MCS 0-7 */
	uint8 b20_2x2stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b20_2x2sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* MCS 8-15 */

	uint8 b20_1x3dsss[WL_NUM_RATES_CCK];		/* Legacy CCK/DSSS */
	uint8 b20_1x3cdd_ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM CDD transmission */
	uint8 b20_1x3cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* 1 Nsts to 3 Tx Chain */
	uint8 b20_2x3stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b20_2x3sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* 2 Nsts to 3 Tx Chain */
	uint8 b20_3x3sdm_mcs16[WL_NUM_RATES_MCS_1STREAM];	/* 3 Nsts to 3 Tx Chain */

	uint8 b20_1x1vht[WL_NUM_RATES_EXTRA_VHT];		/* VHT8_9SS1 */
	uint8 b20_1x2cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD1 */
	uint8 b20_2x2stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC */
	uint8 b20_2x2sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2 */
	uint8 b20_1x3cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD2 */
	uint8 b20_2x3stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC_SPEXP1 */
	uint8 b20_2x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2_SPEXP1 */
	uint8 b20_3x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS3 */

	/* start of 40MHz tx power limits */
	uint8 b40_dummy1x1dsss[WL_NUM_RATES_CCK];	/* Legacy CCK/DSSS */
	uint8 b40_1x1ofdm[WL_NUM_RATES_OFDM];		/* Legacy OFDM transmission */
	uint8 b40_1x1mcs0[WL_NUM_RATES_MCS_1STREAM];		/* SISO MCS 0-7 */

	uint8 b40_dummy1x2dsss[WL_NUM_RATES_CCK];	/* Legacy CCK/DSSS */
	uint8 b40_1x2cdd_ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM CDD transmission */
	uint8 b40_1x2cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* CDD MCS 0-7 */
	uint8 b40_2x2stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b40_2x2sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* MCS 8-15 */

	uint8 b40_dummy1x3dsss[WL_NUM_RATES_CCK];	/* Legacy CCK/DSSS */
	uint8 b40_1x3cdd_ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM CDD transmission */
	uint8 b40_1x3cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* 1 Nsts to 3 Tx Chain */
	uint8 b40_2x3stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b40_2x3sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* 2 Nsts to 3 Tx Chain */
	uint8 b40_3x3sdm_mcs16[WL_NUM_RATES_MCS_1STREAM];	/* 3 Nsts to 3 Tx Chain */

	uint8 b40_1x1vht[WL_NUM_RATES_EXTRA_VHT];		/* VHT8_9SS1 */
	uint8 b40_1x2cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD1 */
	uint8 b40_2x2stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC */
	uint8 b40_2x2sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2 */
	uint8 b40_1x3cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD2 */
	uint8 b40_2x3stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC_SPEXP1 */
	uint8 b40_2x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2_SPEXP1 */
	uint8 b40_3x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS3 */

	/* start of 20in40MHz tx power limits */
	uint8 b20in40_1x1dsss[WL_NUM_RATES_CCK];	/* Legacy CCK/DSSS */
	uint8 b20in40_1x1ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM transmission */
	uint8 b20in40_1x1mcs0[WL_NUM_RATES_MCS_1STREAM];	/* SISO MCS 0-7 */

	uint8 b20in40_1x2dsss[WL_NUM_RATES_CCK];		/* Legacy CCK/DSSS */
	uint8 b20in40_1x2cdd_ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM CDD transmission */
	uint8 b20in40_1x2cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* CDD MCS 0-7 */
	uint8 b20in40_2x2stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b20in40_2x2sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* MCS 8-15 */

	uint8 b20in40_1x3dsss[WL_NUM_RATES_CCK];		/* Legacy CCK/DSSS */
	uint8 b20in40_1x3cdd_ofdm[WL_NUM_RATES_OFDM];	/* 20 in 40 MHz Legacy OFDM CDD */
	uint8 b20in40_1x3cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* 1 Nsts to 3 Tx Chain */
	uint8 b20in40_2x3stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b20in40_2x3sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* 2 Nsts to 3 Tx Chain */
	uint8 b20in40_3x3sdm_mcs16[WL_NUM_RATES_MCS_1STREAM];	/* 3 Nsts to 3 Tx Chain */

	uint8 b20in40_1x1vht[WL_NUM_RATES_EXTRA_VHT];		/* VHT8_9SS1 */
	uint8 b20in40_1x2cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD1 */
	uint8 b20in40_2x2stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC */
	uint8 b20in40_2x2sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2 */
	uint8 b20in40_1x3cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD2 */
	uint8 b20in40_2x3stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC_SPEXP1 */
	uint8 b20in40_2x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2_SPEXP1 */
	uint8 b20in40_3x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS3 */

	/* start of 80MHz tx power limits */
	uint8 b80_dummy1x1dsss[WL_NUM_RATES_CCK];		/* Legacy CCK/DSSS */
	uint8 b80_1x1ofdm[WL_NUM_RATES_OFDM];			/* Legacy OFDM transmission */
	uint8 b80_1x1mcs0[WL_NUM_RATES_MCS_1STREAM];	/* SISO MCS 0-7 */

	uint8 b80_dummy1x2dsss[WL_NUM_RATES_CCK];	/* Legacy CCK/DSSS */
	uint8 b80_1x2cdd_ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM CDD transmission */
	uint8 b80_1x2cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* CDD MCS 0-7 */
	uint8 b80_2x2stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b80_2x2sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* MCS 8-15 */

	uint8 b80_dummy1x3dsss[WL_NUM_RATES_CCK];	/* Legacy CCK/DSSS */
	uint8 b80_1x3cdd_ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM CDD transmission */
	uint8 b80_1x3cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* 1 Nsts to 3 Tx Chain */
	uint8 b80_2x3stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b80_2x3sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* 2 Nsts to 3 Tx Chain */
	uint8 b80_3x3sdm_mcs16[WL_NUM_RATES_MCS_1STREAM];	/* 3 Nsts to 3 Tx Chain */

	uint8 b80_1x1vht[WL_NUM_RATES_EXTRA_VHT];		/* VHT8_9SS1 */
	uint8 b80_1x2cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD1 */
	uint8 b80_2x2stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC */
	uint8 b80_2x2sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2 */
	uint8 b80_1x3cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD2 */
	uint8 b80_2x3stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC_SPEXP1 */
	uint8 b80_2x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2_SPEXP1 */
	uint8 b80_3x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS3 */

	/* start of 20in80MHz tx power limits */
	uint8 b20in80_1x1dsss[WL_NUM_RATES_CCK];	/* Legacy CCK/DSSS */
	uint8 b20in80_1x1ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM transmission */
	uint8 b20in80_1x1mcs0[WL_NUM_RATES_MCS_1STREAM];	/* SISO MCS 0-7 */

	uint8 b20in80_1x2dsss[WL_NUM_RATES_CCK];		/* Legacy CCK/DSSS */
	uint8 b20in80_1x2cdd_ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM CDD transmission */
	uint8 b20in80_1x2cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* CDD MCS 0-7 */
	uint8 b20in80_2x2stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b20in80_2x2sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* MCS 8-15 */

	uint8 b20in80_1x3dsss[WL_NUM_RATES_CCK];		/* Legacy CCK/DSSS */
	uint8 b20in80_1x3cdd_ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM CDD transmission */
	uint8 b20in80_1x3cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* 1 Nsts to 3 Tx Chain */
	uint8 b20in80_2x3stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b20in80_2x3sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* 2 Nsts to 3 Tx Chain */
	uint8 b20in80_3x3sdm_mcs16[WL_NUM_RATES_MCS_1STREAM];	/* 3 Nsts to 3 Tx Chain */

	uint8 b20in80_1x1vht[WL_NUM_RATES_EXTRA_VHT];		/* VHT8_9SS1 */
	uint8 b20in80_1x2cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD1 */
	uint8 b20in80_2x2stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC */
	uint8 b20in80_2x2sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2 */
	uint8 b20in80_1x3cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD2 */
	uint8 b20in80_2x3stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC_SPEXP1 */
	uint8 b20in80_2x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2_SPEXP1 */
	uint8 b20in80_3x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS3 */

	/* start of 40in80MHz tx power limits */
	uint8 b40in80_dummy1x1dsss[WL_NUM_RATES_CCK];	/* Legacy CCK/DSSS */
	uint8 b40in80_1x1ofdm[WL_NUM_RATES_OFDM];		/* Legacy OFDM transmission */
	uint8 b40in80_1x1mcs0[WL_NUM_RATES_MCS_1STREAM];	/* SISO MCS 0-7 */

	uint8 b40in80_dummy1x2dsss[WL_NUM_RATES_CCK];	/* Legacy CCK/DSSS */
	uint8 b40in80_1x2cdd_ofdm[WL_NUM_RATES_OFDM];	/* Legacy OFDM CDD transmission */
	uint8 b40in80_1x2cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* CDD MCS 0-7 */
	uint8 b40in80_2x2stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b40in80_2x2sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* MCS 8-15 */

	uint8 b40in80_dummy1x3dsss[WL_NUM_RATES_CCK];	/* Legacy CCK/DSSS */
	uint8 b40in80_1x3cdd_ofdm[WL_NUM_RATES_OFDM];	/* MHz Legacy OFDM CDD */
	uint8 b40in80_1x3cdd_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* 1 Nsts to 3 Tx Chain */
	uint8 b40in80_2x3stbc_mcs0[WL_NUM_RATES_MCS_1STREAM];	/* STBC MCS 0-7 */
	uint8 b40in80_2x3sdm_mcs8[WL_NUM_RATES_MCS_1STREAM];	/* 2 Nsts to 3 Tx Chain */
	uint8 b40in80_3x3sdm_mcs16[WL_NUM_RATES_MCS_1STREAM];	/* 3 Nsts to 3 Tx Chain */

	uint8 b40in80_1x1vht[WL_NUM_RATES_EXTRA_VHT];		/* VHT8_9SS1 */
	uint8 b40in80_1x2cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD1 */
	uint8 b40in80_2x2stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC */
	uint8 b40in80_2x2sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2 */
	uint8 b40in80_1x3cdd_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_CDD2 */
	uint8 b40in80_2x3stbc_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS1_STBC_SPEXP1 */
	uint8 b40in80_2x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS2_SPEXP1 */
	uint8 b40in80_3x3sdm_vht[WL_NUM_RATES_EXTRA_VHT];	/* VHT8_9SS3 */

	uint8 mcs32; /* C_CHECK - THIS NEEDS TO BE REMOVED THROUGHOUT THE CODE */
} txppr_t;

/* 20MHz */
#define WL_TX_POWER_CCK_FIRST					OFFSETOF(txppr_t, b20_1x1dsss)
#define WL_TX_POWER_OFDM20_FIRST				OFFSETOF(txppr_t, b20_1x1ofdm)
#define WL_TX_POWER_MCS20_SISO_FIRST			OFFSETOF(txppr_t, b20_1x1mcs0)
#define WL_TX_POWER_20_S1x1_FIRST				OFFSETOF(txppr_t, b20_1x1mcs0)

#define WL_TX_POWER_CCK_CDD_S1x2_FIRST			OFFSETOF(txppr_t, b20_1x2dsss)
#define WL_TX_POWER_OFDM20_CDD_FIRST			OFFSETOF(txppr_t, b20_1x2cdd_ofdm)
#define WL_TX_POWER_MCS20_CDD_FIRST				OFFSETOF(txppr_t, b20_1x2cdd_mcs0)
#define WL_TX_POWER_20_S1x2_FIRST				OFFSETOF(txppr_t, b20_1x2cdd_mcs0)
#define WL_TX_POWER_MCS20_STBC_FIRST			OFFSETOF(txppr_t, b20_2x2stbc_mcs0)
#define WL_TX_POWER_MCS20_SDM_FIRST				OFFSETOF(txppr_t, b20_2x2sdm_mcs8)
#define WL_TX_POWER_20_S2x2_FIRST				OFFSETOF(txppr_t, b20_2x2sdm_mcs8)

#define WL_TX_POWER_CCK_CDD_S1x3_FIRST			OFFSETOF(txppr_t, b20_1x3dsss)
#define WL_TX_POWER_OFDM20_CDD_S1x3_FIRST		OFFSETOF(txppr_t, b20_1x3cdd_ofdm)
#define WL_TX_POWER_20_S1x3_FIRST				OFFSETOF(txppr_t, b20_1x3cdd_mcs0)
#define WL_TX_POWER_20_STBC_S2x3_FIRST			OFFSETOF(txppr_t, b20_2x3stbc_mcs0)
#define WL_TX_POWER_20_S2x3_FIRST				OFFSETOF(txppr_t, b20_2x3sdm_mcs8)
#define WL_TX_POWER_20_S3x3_FIRST				OFFSETOF(txppr_t, b20_3x3sdm_mcs16)

#define WL_TX_POWER_20_S1X1_VHT					OFFSETOF(txppr_t, b20_1x1vht)
#define WL_TX_POWER_20_S1X2_CDD_VHT				OFFSETOF(txppr_t, b20_1x2cdd_vht)
#define WL_TX_POWER_20_S2X2_STBC_VHT			OFFSETOF(txppr_t, b20_2x2stbc_vht)
#define WL_TX_POWER_20_S2X2_VHT					OFFSETOF(txppr_t, b20_2x2sdm_vht)
#define WL_TX_POWER_20_S1X3_CDD_VHT				OFFSETOF(txppr_t, b20_1x3cdd_vht)
#define WL_TX_POWER_20_S2X3_STBC_VHT			OFFSETOF(txppr_t, b20_2x3stbc_vht)
#define WL_TX_POWER_20_S2X3_VHT					OFFSETOF(txppr_t, b20_2x3sdm_vht)
#define WL_TX_POWER_20_S3X3_VHT					OFFSETOF(txppr_t, b20_3x3sdm_vht)

/* 40MHz */
#define WL_TX_POWER_40_DUMMY_CCK_FIRST			OFFSETOF(txppr_t, b40_dummy1x1dsss)
#define WL_TX_POWER_OFDM40_FIRST				OFFSETOF(txppr_t, b40_1x1ofdm)
#define WL_TX_POWER_MCS40_SISO_FIRST			OFFSETOF(txppr_t, b40_1x1mcs0)
#define WL_TX_POWER_40_S1x1_FIRST				OFFSETOF(txppr_t, b40_1x1mcs0)

#define WL_TX_POWER_40_DUMMY_CCK_CDD_S1x2_FIRST	OFFSETOF(txppr_t, b40_dummy1x2dsss)
#define WL_TX_POWER_OFDM40_CDD_FIRST			OFFSETOF(txppr_t, b40_1x2cdd_ofdm)
#define WL_TX_POWER_MCS40_CDD_FIRST				OFFSETOF(txppr_t, b40_1x2cdd_mcs0)
#define WL_TX_POWER_40_S1x2_FIRST				OFFSETOF(txppr_t, b40_1x2cdd_mcs0)
#define WL_TX_POWER_MCS40_STBC_FIRST			OFFSETOF(txppr_t, b40_2x2stbc_mcs0)
#define WL_TX_POWER_MCS40_SDM_FIRST				OFFSETOF(txppr_t, b40_2x2sdm_mcs8)
#define WL_TX_POWER_40_S2x2_FIRST				OFFSETOF(txppr_t, b40_2x2sdm_mcs8)

#define WL_TX_POWER_40_DUMMY_CCK_CDD_S1x3_FIRST	OFFSETOF(txppr_t, b40_dummy1x3dsss)
#define WL_TX_POWER_OFDM40_CDD_S1x3_FIRST		OFFSETOF(txppr_t, b40_1x3cdd_ofdm)
#define WL_TX_POWER_40_S1x3_FIRST				OFFSETOF(txppr_t, b40_1x3cdd_mcs0)
#define WL_TX_POWER_40_STBC_S2x3_FIRST			OFFSETOF(txppr_t, b40_2x3stbc_mcs0)
#define WL_TX_POWER_40_S2x3_FIRST				OFFSETOF(txppr_t, b40_2x3sdm_mcs8)
#define WL_TX_POWER_40_S3x3_FIRST				OFFSETOF(txppr_t, b40_3x3sdm_mcs16)

#define WL_TX_POWER_40_S1X1_VHT					OFFSETOF(txppr_t, b40_1x1vht)
#define WL_TX_POWER_40_S1X2_CDD_VHT				OFFSETOF(txppr_t, b40_1x2cdd_vht)
#define WL_TX_POWER_40_S2X2_STBC_VHT			OFFSETOF(txppr_t, b40_2x2stbc_vht)
#define WL_TX_POWER_40_S2X2_VHT					OFFSETOF(txppr_t, b40_2x2sdm_vht)
#define WL_TX_POWER_40_S1X3_CDD_VHT				OFFSETOF(txppr_t, b40_1x3cdd_vht)
#define WL_TX_POWER_40_S2X3_STBC_VHT			OFFSETOF(txppr_t, b40_2x3stbc_vht)
#define WL_TX_POWER_40_S2X3_VHT					OFFSETOF(txppr_t, b40_2x3sdm_vht)
#define WL_TX_POWER_40_S3X3_VHT					OFFSETOF(txppr_t, b40_3x3sdm_vht)

/* 20 in 40MHz */
#define WL_TX_POWER_20UL_CCK_FIRST				OFFSETOF(txppr_t, b20in40_1x1dsss)
#define WL_TX_POWER_20UL_OFDM_FIRST				OFFSETOF(txppr_t, b20in40_1x1ofdm)
#define WL_TX_POWER_20UL_S1x1_FIRST				OFFSETOF(txppr_t, b20in40_1x1mcs0)

#define WL_TX_POWER_CCK_20U_CDD_S1x2_FIRST	OFFSETOF(txppr_t, b20in40_1x2dsss)
#define WL_TX_POWER_20UL_OFDM_CDD_FIRST		OFFSETOF(txppr_t, b20in40_1x2cdd_ofdm)
#define WL_TX_POWER_20UL_S1x2_FIRST			OFFSETOF(txppr_t, b20in40_1x2cdd_mcs0)
#define WL_TX_POWER_20UL_STBC_S2x2_FIRST	OFFSETOF(txppr_t, b20in40_2x2stbc_mcs0)
#define WL_TX_POWER_20UL_S2x2_FIRST			OFFSETOF(txppr_t, b20in40_2x2sdm_mcs8)

#define WL_TX_POWER_CCK_20U_CDD_S1x3_FIRST	OFFSETOF(txppr_t, b20in40_1x3dsss)
#define WL_TX_POWER_20UL_OFDM_CDD_S1x3_FIRST OFFSETOF(txppr_t, b20in40_1x3cdd_ofdm)
#define WL_TX_POWER_20UL_S1x3_FIRST			OFFSETOF(txppr_t, b20in40_1x3cdd_mcs0)
#define WL_TX_POWER_20UL_STBC_S2x3_FIRST	OFFSETOF(txppr_t, b20in40_2x3stbc_mcs0)
#define WL_TX_POWER_20UL_S2x3_FIRST			OFFSETOF(txppr_t, b20in40_2x3sdm_mcs8)
#define WL_TX_POWER_20UL_S3x3_FIRST			OFFSETOF(txppr_t, b20in40_3x3sdm_mcs16)

#define WL_TX_POWER_20UL_S1X1_VHT			OFFSETOF(txppr_t, b20in40_1x1vht)
#define WL_TX_POWER_20UL_S1X2_CDD_VHT		OFFSETOF(txppr_t, b20in40_1x2cdd_vht)
#define WL_TX_POWER_20UL_S2X2_STBC_VHT		OFFSETOF(txppr_t, b20in40_2x2stbc_vht)
#define WL_TX_POWER_20UL_S2X2_VHT			OFFSETOF(txppr_t, b20in40_2x2sdm_vht)
#define WL_TX_POWER_20UL_S1X3_CDD_VHT		OFFSETOF(txppr_t, b20in40_1x3cdd_vht)
#define WL_TX_POWER_20UL_S2X3_STBC_VHT		OFFSETOF(txppr_t, b20in40_2x3stbc_vht)
#define WL_TX_POWER_20UL_S2X3_VHT			OFFSETOF(txppr_t, b20in40_2x3sdm_vht)
#define WL_TX_POWER_20UL_S3X3_VHT			OFFSETOF(txppr_t, b20in40_3x3sdm_vht)

/* 80MHz */
#define WL_TX_POWER_80_DUMMY_CCK_FIRST		OFFSETOF(txppr_t, b80_dummy1x1dsss)
#define WL_TX_POWER_OFDM80_FIRST			OFFSETOF(txppr_t, b80_1x1ofdm)
#define WL_TX_POWER_MCS80_SISO_FIRST		OFFSETOF(txppr_t, b80_1x1mcs0)
#define WL_TX_POWER_80_S1x1_FIRST			OFFSETOF(txppr_t, b80_1x1mcs0)

#define WL_TX_POWER_80_DUMMY_CCK_CDD_S1x2_FIRST	OFFSETOF(txppr_t, b80_dummy1x2dsss)
#define WL_TX_POWER_OFDM80_CDD_FIRST			OFFSETOF(txppr_t, b80_1x2cdd_ofdm)
#define WL_TX_POWER_MCS80_CDD_FIRST				OFFSETOF(txppr_t, b80_1x2cdd_mcs0)
#define WL_TX_POWER_80_S1x2_FIRST				OFFSETOF(txppr_t, b80_1x2cdd_mcs0)
#define WL_TX_POWER_MCS80_STBC_FIRST			OFFSETOF(txppr_t, b80_2x2stbc_mcs0)
#define WL_TX_POWER_MCS80_SDM_FIRST				OFFSETOF(txppr_t, b80_2x2sdm_mcs8)
#define WL_TX_POWER_80_S2x2_FIRST				OFFSETOF(txppr_t, b80_2x2sdm_mcs8)

#define WL_TX_POWER_80_DUMMY_CCK_CDD_S1x3_FIRST	OFFSETOF(txppr_t, b80_dummy1x3dsss)
#define WL_TX_POWER_OFDM80_CDD_S1x3_FIRST		OFFSETOF(txppr_t, b80_1x3cdd_ofdm)
#define WL_TX_POWER_80_S1x3_FIRST				OFFSETOF(txppr_t, b80_1x3cdd_mcs0)
#define WL_TX_POWER_80_STBC_S2x3_FIRST			OFFSETOF(txppr_t, b80_2x3stbc_mcs0)
#define WL_TX_POWER_80_S2x3_FIRST				OFFSETOF(txppr_t, b80_2x3sdm_mcs8)
#define WL_TX_POWER_80_S3x3_FIRST				OFFSETOF(txppr_t, b80_3x3sdm_mcs16)

#define WL_TX_POWER_80_S1X1_VHT					OFFSETOF(txppr_t, b80_1x1vht)
#define WL_TX_POWER_80_S1X2_CDD_VHT				OFFSETOF(txppr_t, b80_1x2cdd_vht)
#define WL_TX_POWER_80_S2X2_STBC_VHT			OFFSETOF(txppr_t, b80_2x2stbc_vht)
#define WL_TX_POWER_80_S2X2_VHT					OFFSETOF(txppr_t, b80_2x2sdm_vht)
#define WL_TX_POWER_80_S1X3_CDD_VHT				OFFSETOF(txppr_t, b80_1x3cdd_vht)
#define WL_TX_POWER_80_S2X3_STBC_VHT			OFFSETOF(txppr_t, b80_2x3stbc_vht)
#define WL_TX_POWER_80_S2X3_VHT					OFFSETOF(txppr_t, b80_2x3sdm_vht)
#define WL_TX_POWER_80_S3X3_VHT					OFFSETOF(txppr_t, b80_3x3sdm_vht)

/* 20 in 80MHz */
#define WL_TX_POWER_20UUL_CCK_FIRST				OFFSETOF(txppr_t, b20in80_1x1dsss)
#define WL_TX_POWER_20UUL_OFDM_FIRST			OFFSETOF(txppr_t, b20in80_1x1ofdm)
#define WL_TX_POWER_20UUL_S1x1_FIRST			OFFSETOF(txppr_t, b20in80_1x1mcs0)

#define WL_TX_POWER_CCK_20UU_CDD_S1x2_FIRST		OFFSETOF(txppr_t, b20in80_1x2dsss)
#define WL_TX_POWER_20UUL_OFDM_CDD_FIRST		OFFSETOF(txppr_t, b20in80_1x2cdd_ofdm)
#define WL_TX_POWER_20UUL_S1x2_FIRST			OFFSETOF(txppr_t, b20in80_1x2cdd_mcs0)
#define WL_TX_POWER_20UUL_STBC_S2x2_FIRST		OFFSETOF(txppr_t, b20in80_2x2stbc_mcs0)
#define WL_TX_POWER_20UUL_S2x2_FIRST			OFFSETOF(txppr_t, b20in80_2x2sdm_mcs8)

#define WL_TX_POWER_CCK_20UU_CDD_S1x3_FIRST		OFFSETOF(txppr_t, b20in80_1x3dsss)
#define WL_TX_POWER_20UUL_OFDM_CDD_S1x3_FIRST	OFFSETOF(txppr_t, b20in80_1x3cdd_ofdm)
#define WL_TX_POWER_20UUL_S1x3_FIRST			OFFSETOF(txppr_t, b20in80_1x3cdd_mcs0)
#define WL_TX_POWER_20UUL_STBC_S2x3_FIRST		OFFSETOF(txppr_t, b20in80_2x3stbc_mcs0)
#define WL_TX_POWER_20UUL_S2x3_FIRST			OFFSETOF(txppr_t, b20in80_2x3sdm_mcs8)
#define WL_TX_POWER_20UUL_S3x3_FIRST			OFFSETOF(txppr_t, b20in80_3x3sdm_mcs16)

#define WL_TX_POWER_20UUL_S1X1_VHT			OFFSETOF(txppr_t, b20in80_1x1vht)
#define WL_TX_POWER_20UUL_S1X2_CDD_VHT		OFFSETOF(txppr_t, b20in80_1x2cdd_vht)
#define WL_TX_POWER_20UUL_S2X2_STBC_VHT		OFFSETOF(txppr_t, b20in80_2x2stbc_vht)
#define WL_TX_POWER_20UUL_S2X2_VHT			OFFSETOF(txppr_t, b20in80_2x2sdm_vht)
#define WL_TX_POWER_20UUL_S1X3_CDD_VHT		OFFSETOF(txppr_t, b20in80_1x3cdd_vht)
#define WL_TX_POWER_20UUL_S2X3_STBC_VHT		OFFSETOF(txppr_t, b20in80_2x3stbc_vht)
#define WL_TX_POWER_20UUL_S2X3_VHT			OFFSETOF(txppr_t, b20in80_2x3sdm_vht)
#define WL_TX_POWER_20UUL_S3X3_VHT			OFFSETOF(txppr_t, b20in80_3x3sdm_vht)

/* 40 in 80MHz */
#define WL_TX_POWER_40UUL_DUMMY_CCK_FIRST		OFFSETOF(txppr_t, b40in80_dummy1x1dsss)
#define WL_TX_POWER_40UUL_OFDM_FIRST			OFFSETOF(txppr_t, b40in80_1x1ofdm)
#define WL_TX_POWER_40UUL_S1x1_FIRST			OFFSETOF(txppr_t, b40in80_1x1mcs0)

#define WL_TX_POWER_CCK_40UU_DUMMY_CDD_S1x2_FIRST OFFSETOF(txppr_t, b40in80_dummy1x2dsss)
#define WL_TX_POWER_40UUL_OFDM_CDD_FIRST		OFFSETOF(txppr_t, b40in80_1x2cdd_ofdm)
#define WL_TX_POWER_40UUL_S1x2_FIRST			OFFSETOF(txppr_t, b40in80_1x2cdd_mcs0)
#define WL_TX_POWER_40UUL_STBC_S2x2_FIRST		OFFSETOF(txppr_t, b40in80_2x2stbc_mcs0)
#define WL_TX_POWER_40UUL_S2x2_FIRST			OFFSETOF(txppr_t, b40in80_2x2sdm_mcs8)

#define WL_TX_POWER_CCK_40UU_DUMMY_CDD_S1x3_FIRST OFFSETOF(txppr_t, b40in80_dummy1x3dsss)
#define WL_TX_POWER_40UUL_OFDM_CDD_S1x3_FIRST	OFFSETOF(txppr_t, b40in80_1x3cdd_ofdm)
#define WL_TX_POWER_40UUL_S1x3_FIRST			OFFSETOF(txppr_t, b40in80_1x3cdd_mcs0)
#define WL_TX_POWER_40UUL_STBC_S2x3_FIRST		OFFSETOF(txppr_t, b40in80_2x3stbc_mcs0)
#define WL_TX_POWER_40UUL_S2x3_FIRST			OFFSETOF(txppr_t, b40in80_2x3sdm_mcs8)
#define WL_TX_POWER_40UUL_S3x3_FIRST			OFFSETOF(txppr_t, b40in80_3x3sdm_mcs16)

#define WL_TX_POWER_40UUL_S1X1_VHT			OFFSETOF(txppr_t, b40in80_1x1vht)
#define WL_TX_POWER_40UUL_S1X2_CDD_VHT		OFFSETOF(txppr_t, b40in80_1x2cdd_vht)
#define WL_TX_POWER_40UUL_S2X2_STBC_VHT		OFFSETOF(txppr_t, b40in80_2x2stbc_vht)
#define WL_TX_POWER_40UUL_S2X2_VHT			OFFSETOF(txppr_t, b40in80_2x2sdm_vht)
#define WL_TX_POWER_40UUL_S1X3_CDD_VHT		OFFSETOF(txppr_t, b40in80_1x3cdd_vht)
#define WL_TX_POWER_40UUL_S2X3_STBC_VHT		OFFSETOF(txppr_t, b40in80_2x3stbc_vht)
#define WL_TX_POWER_40UUL_S2X3_VHT			OFFSETOF(txppr_t, b40in80_2x3sdm_vht)
#define WL_TX_POWER_40UUL_S3X3_VHT			OFFSETOF(txppr_t, b40in80_3x3sdm_vht)

#define WL_TX_POWER_MCS_32			OFFSETOF(txppr_t, mcs32) /* C_CHECK remove later */

#define WL_TX_POWER_RATES			sizeof(struct txppr)

/* sslpnphy specifics */
#define WL_TX_POWER_MCS20_SISO_FIRST_SSN	WL_TX_POWER_MCS20_SISO_FIRST
#define WL_TX_POWER_MCS40_SISO_FIRST_SSN	WL_TX_POWER_MCS40_SISO_FIRST

/* tx_power_t.flags bits */
#define WL_TX_POWER_F_ENABLED	1
#define WL_TX_POWER_F_HW	2
#define WL_TX_POWER_F_MIMO	4
#define WL_TX_POWER_F_SISO	8
#define WL_TX_POWER_F_HT	0x10

typedef struct {
	uint16 ver;				/* version of this struct */
	uint16 len;				/* length in bytes of this structure */
	uint32 flags;
	chanspec_t chanspec;			/* txpwr report for this channel */
	chanspec_t local_chanspec;		/* channel on which we are associated */
	uint8 ppr[WL_TX_POWER_RATES];		/* Latest target power */
} wl_txppr_t;

#define WL_TXPPR_VERSION	0
#define WL_TXPPR_LENGTH	(sizeof(wl_txppr_t))
#define TX_POWER_T_VERSION	43

/* Defines used with channel_bandwidth for curpower */
#define WL_BW_20MHZ 		0
#define WL_BW_40MHZ 		1
#define WL_BW_80MHZ 		2

/* tx_power_t.flags bits */
#ifdef PPR_API
#define WL_TX_POWER2_F_ENABLED	1
#define WL_TX_POWER2_F_HW		2
#define WL_TX_POWER2_F_MIMO		4
#define WL_TX_POWER2_F_SISO		8
#define WL_TX_POWER2_F_HT		0x10
#else
#define WL_TX_POWER_F_ENABLED	1
#define WL_TX_POWER_F_HW		2
#define WL_TX_POWER_F_MIMO		4
#define WL_TX_POWER_F_SISO		8
#define WL_TX_POWER_F_HT		0x10
#endif
typedef struct {
	uint32 flags;
	chanspec_t chanspec;			/* txpwr report for this channel */
	chanspec_t local_chanspec;		/* channel on which we are associated */
	uint8 local_max;			/* local max according to the AP */
	uint8 local_constraint;			/* local constraint according to the AP */
	int8  antgain[2];			/* Ant gain for each band - from SROM */
	uint8 rf_cores;				/* count of RF Cores being reported */
	uint8 est_Pout[4];			/* Latest tx power out estimate per RF chain */
	uint8 est_Pout_act[4];		/* Latest tx power out estimate per RF chain
	* without adjustment
	*/
	uint8 est_Pout_cck;			/* Latest CCK tx power out estimate */
	uint8 tx_power_max[4];		/* Maximum target power among all rates */
	uint tx_power_max_rate_ind[4];		/* Index of the rate with the max target power */
	uint8 user_limit[WL_TX_POWER_RATES];	/* User limit */
	int8 board_limit[WL_TX_POWER_RATES];	/* Max power board can support (SROM) */
	int8 target[WL_TX_POWER_RATES];			/* Latest target power */
	int8 clm_limits[WL_NUMRATES];		/* regulatory limits - 20, 40 or 80MHz */
	int8 clm_limits_subchan1[WL_NUMRATES];	/* regulatory limits - 20in40 or 40in80 */
	int8 clm_limits_subchan2[WL_NUMRATES];	/* regulatory limits - 20in80MHz */
	int8 sar;					/* SAR limit for display by wl executable */
	int8 channel_bandwidth;		/* 20, 40 or 80 MHz bandwidth? */
	uint8 version;				/* Version of the data format wlu <--> driver */
	uint8 display_core;			/* Displayed curpower core */
#ifdef PPR_API
} tx_power_new_t;
#else
} tx_power_t;
#endif

typedef struct tx_inst_power {
	uint8 txpwr_est_Pout[2];			/* Latest estimate for 2.4 and 5 Ghz */
	uint8 txpwr_est_Pout_gofdm;			/* Pwr estimate for 2.4 OFDM */
} tx_inst_power_t;


typedef struct {
	uint32 flags;
	chanspec_t chanspec;			/* txpwr report for this channel */
	chanspec_t local_chanspec;		/* channel on which we are associated */
	uint8 local_max;			/* local max according to the AP */
	uint8 local_constraint;			/* local constraint according to the AP */
	int8  antgain[2];			/* Ant gain for each band - from SROM */
	uint8 rf_cores;				/* count of RF Cores being reported */
	uint8 est_Pout[4];			/* Latest tx power out estimate per RF chain */
	uint8 est_Pout_act[4];                  /* Latest tx power out estimate per RF chain
						 * without adjustment
						 */
	uint8 est_Pout_cck;			/* Latest CCK tx power out estimate */
	uint8 tx_power_max[4];                  /* Maximum target power among all rates */
	uint tx_power_max_rate_ind[4];         /* Index of the rate with the max target power */
	txppr_t user_limit;			/* User limit */
	txppr_t reg_limit;			/* Regulatory power limit */
	txppr_t board_limit;			/* Max power board can support (SROM) */
	txppr_t target;				/* Latest target power */
} wl_txpwr_t;

#define WL_NUM_TXCHAIN_MAX	4
typedef struct wl_txchain_pwr_offsets {
	int8 offset[WL_NUM_TXCHAIN_MAX];	/* quarter dBm signed offset for each chain */
} wl_txchain_pwr_offsets_t;

/* 802.11h measurement types */
#define WLC_MEASURE_TPC			1
#define WLC_MEASURE_CHANNEL_BASIC	2
#define WLC_MEASURE_CHANNEL_CCA		3
#define WLC_MEASURE_CHANNEL_RPI		4

/* regulatory enforcement levels */
#define SPECT_MNGMT_OFF			0		/* both 11h and 11d disabled */
#define SPECT_MNGMT_LOOSE_11H		1		/* allow non-11h APs in scan lists */
#define SPECT_MNGMT_STRICT_11H		2		/* prune out non-11h APs from scan list */
#define SPECT_MNGMT_STRICT_11D		3		/* switch to 802.11D mode */
/* SPECT_MNGMT_LOOSE_11H_D - same as SPECT_MNGMT_LOOSE with the exception that Country IE
 * adoption is done regardless of capability spectrum_management
 */
#define SPECT_MNGMT_LOOSE_11H_D		4		/* operation defined above */

#define WL_CHAN_VALID_HW	(1 << 0)	/* valid with current HW */
#define WL_CHAN_VALID_SW	(1 << 1)	/* valid with current country setting */
#define WL_CHAN_BAND_5G		(1 << 2)	/* 5GHz-band channel */
#define WL_CHAN_RADAR		(1 << 3)	/* radar sensitive  channel */
#define WL_CHAN_INACTIVE	(1 << 4)	/* temporarily inactive due to radar */
#define WL_CHAN_PASSIVE		(1 << 5)	/* channel is in passive mode */
#define WL_CHAN_RESTRICTED	(1 << 6)	/* restricted use channel */

/* BTC mode used by "btc_mode" iovar */
#define	WL_BTC_DISABLE		0	/* disable BT coexistence */
#define WL_BTC_FULLTDM      1	/* full TDM COEX */
#define WL_BTC_ENABLE       1	/* full TDM COEX to maintain backward compatiblity */
#define WL_BTC_PREMPT      2    /* full TDM COEX with preemption */
#define WL_BTC_LITE        3	/* light weight coex for large isolation platform */
#define WL_BTC_PARALLEL		4   /* BT and WLAN run in parallel with separate antenna  */
#define WL_BTC_HYBRID		5   /* hybrid coex, only ack is allowed to transmit in BT slot */
#define WL_BTC_DEFAULT		8	/* set the default mode for the device */
#define WL_INF_BTC_DISABLE      0
#define WL_INF_BTC_ENABLE       1
#define WL_INF_BTC_AUTO         3

/* BTC wire used by "btc_wire" iovar */
#define	WL_BTC_DEFWIRE		0	/* use default wire setting */
#define WL_BTC_2WIRE		2	/* use 2-wire BTC */
#define WL_BTC_3WIRE		3	/* use 3-wire BTC */
#define WL_BTC_4WIRE		4	/* use 4-wire BTC */

/* BTC flags: BTC configuration that can be set by host */
#define WL_BTC_FLAG_PREMPT               (1 << 0)
#define WL_BTC_FLAG_BT_DEF               (1 << 1)
#define WL_BTC_FLAG_ACTIVE_PROT          (1 << 2)
#define WL_BTC_FLAG_SIM_RSP              (1 << 3)
#define WL_BTC_FLAG_PS_PROTECT           (1 << 4)
#define WL_BTC_FLAG_SIM_TX_LP	         (1 << 5)
#define WL_BTC_FLAG_ECI                  (1 << 6)
#define WL_BTC_FLAG_LIGHT                (1 << 7)
#define WL_BTC_FLAG_PARALLEL             (1 << 8)

/* Message levels */
#define WL_ERROR_VAL		0x00000001
#define WL_TRACE_VAL		0x00000002
#define WL_PRHDRS_VAL		0x00000004
#define WL_PRPKT_VAL		0x00000008
#define WL_INFORM_VAL		0x00000010
#define WL_TMP_VAL		0x00000020
#define WL_OID_VAL		0x00000040
#define WL_RATE_VAL		0x00000080
#define WL_ASSOC_VAL		0x00000100
#define WL_PRUSR_VAL		0x00000200
#define WL_PS_VAL		0x00000400
#define WL_TXPWR_VAL		0x00000800	/* retired in TOT on 6/10/2009 */
#define WL_PORT_VAL		0x00001000
#define WL_DUAL_VAL		0x00002000
#define WL_WSEC_VAL		0x00004000
#define WL_WSEC_DUMP_VAL	0x00008000
#define WL_LOG_VAL		0x00010000
#define WL_NRSSI_VAL		0x00020000	/* retired in TOT on 6/10/2009 */
#define WL_LOFT_VAL		0x00040000	/* retired in TOT on 6/10/2009 */
#define WL_REGULATORY_VAL	0x00080000
#define WL_PHYCAL_VAL		0x00100000	/* retired in TOT on 6/10/2009 */
#define WL_RADAR_VAL		0x00200000	/* retired in TOT on 6/10/2009 */
#define WL_MPC_VAL		0x00400000
#define WL_APSTA_VAL		0x00800000
#define WL_DFS_VAL		0x01000000
#define WL_BA_VAL		0x02000000	/* retired in TOT on 6/14/2010 */
#define WL_ACI_VAL		0x04000000
#define WL_MBSS_VAL		0x04000000
#define WL_CAC_VAL		0x08000000
#define WL_AMSDU_VAL		0x10000000
#define WL_AMPDU_VAL		0x20000000
#define WL_FFPLD_VAL		0x40000000

/* wl_msg_level is full. For new bits take the next one and AND with
 * wl_msg_level2 in wl_dbg.h
 */
#define WL_DPT_VAL 		0x00000001
#define WL_SCAN_VAL		0x00000002
#define WL_WOWL_VAL		0x00000004
#define WL_COEX_VAL		0x00000008
#define WL_RTDC_VAL		0x00000010
#define WL_PROTO_VAL		0x00000020
#define WL_BTA_VAL		0x00000040
#define WL_CHANINT_VAL		0x00000080
#define WL_THERMAL_VAL		0x00000100	/* retired in TOT on 6/10/2009 */
#define WL_P2P_VAL		0x00000200
#define WL_ITFR_VAL		0x00000400
#define WL_MCHAN_VAL		0x00000800
#define WL_TDLS_VAL		0x00001000
#define WL_MCNX_VAL		0x00002000
#define WL_PROT_VAL		0x00004000
#define WL_PSTA_VAL		0x00008000
#define WL_TBTT_VAL		0x00010000
#define WL_NIC_VAL		0x00020000
#define WL_PWRSEL_VAL	0x00040000
/* use top-bit for WL_TIME_STAMP_VAL because this is a modifier
 * rather than a message-type of its own
 */
#define WL_TIMESTAMP_VAL        0x80000000

/* max # of leds supported by GPIO (gpio pin# == led index#) */
#define	WL_LED_NUMGPIO		32	/* gpio 0-31 */

/* led per-pin behaviors */
#define	WL_LED_OFF		0		/* always off */
#define	WL_LED_ON		1		/* always on */
#define	WL_LED_ACTIVITY		2		/* activity */
#define	WL_LED_RADIO		3		/* radio enabled */
#define	WL_LED_ARADIO		4		/* 5  Ghz radio enabled */
#define	WL_LED_BRADIO		5		/* 2.4Ghz radio enabled */
#define	WL_LED_BGMODE		6		/* on if gmode, off if bmode */
#define	WL_LED_WI1		7
#define	WL_LED_WI2		8
#define	WL_LED_WI3		9
#define	WL_LED_ASSOC		10		/* associated state indicator */
#define	WL_LED_INACTIVE		11		/* null behavior (clears default behavior) */
#define	WL_LED_ASSOCACT		12		/* on when associated; blink fast for activity */
#define WL_LED_WI4		13
#define WL_LED_WI5		14
#define	WL_LED_BLINKSLOW	15		/* blink slow */
#define	WL_LED_BLINKMED		16		/* blink med */
#define	WL_LED_BLINKFAST	17		/* blink fast */
#define	WL_LED_BLINKCUSTOM	18		/* blink custom */
#define	WL_LED_BLINKPERIODIC	19		/* blink periodic (custom 1000ms / off 400ms) */
#define WL_LED_ASSOC_WITH_SEC 	20		/* when connected with security */
						/* keep on for 300 sec */
#define WL_LED_START_OFF 	21		/* off upon boot, could be turned on later */
#define	WL_LED_NUMBEHAVIOR	22

/* led behavior numeric value format */
#define	WL_LED_BEH_MASK		0x7f		/* behavior mask */
#define	WL_LED_AL_MASK		0x80		/* activelow (polarity) bit */

/* maximum channels returned by the get valid channels iovar */
#define WL_NUMCHANNELS		64

/* max number of chanspecs (used by the iovar to calc. buf space) */
#define WL_NUMCHANSPECS 110

/* WDS link local endpoint WPA role */
#define WL_WDS_WPA_ROLE_AUTH	0	/* authenticator */
#define WL_WDS_WPA_ROLE_SUP	1	/* supplicant */
#define WL_WDS_WPA_ROLE_AUTO	255	/* auto, based on mac addr value */

/* number of bytes needed to define a 128-bit mask for MAC event reporting */
#define WL_EVENTING_MASK_LEN	16

/*
 * Join preference iovar value is an array of tuples. Each tuple has a one-byte type,
 * a one-byte length, and a variable length value.  RSSI type tuple must be present
 * in the array.
 *
 * Types are defined in "join preference types" section.
 *
 * Length is the value size in octets. It is reserved for WL_JOIN_PREF_WPA type tuple
 * and must be set to zero.
 *
 * Values are defined below.
 *
 * 1. RSSI - 2 octets
 * offset 0: reserved
 * offset 1: reserved
 *
 * 2. WPA - 2 + 12 * n octets (n is # tuples defined below)
 * offset 0: reserved
 * offset 1: # of tuples
 * offset 2: tuple 1
 * offset 14: tuple 2
 * ...
 * offset 2 + 12 * (n - 1) octets: tuple n
 *
 * struct wpa_cfg_tuple {
 *   uint8 akm[DOT11_OUI_LEN+1];     akm suite
 *   uint8 ucipher[DOT11_OUI_LEN+1]; unicast cipher suite
 *   uint8 mcipher[DOT11_OUI_LEN+1]; multicast cipher suite
 * };
 *
 * multicast cipher suite can be specified as a specific cipher suite or WL_WPA_ACP_MCS_ANY.
 *
 * 3. BAND - 2 octets
 * offset 0: reserved
 * offset 1: see "band preference" and "band types"
 *
 * 4. BAND RSSI - 2 octets
 * offset 0: band types
 * offset 1: +ve RSSI boost balue in dB
 */

/* join preference types */
#define WL_JOIN_PREF_RSSI	1	/* by RSSI */
#define WL_JOIN_PREF_WPA	2	/* by akm and ciphers */
#define WL_JOIN_PREF_BAND	3	/* by 802.11 band */
#define WL_JOIN_PREF_RSSI_DELTA	4	/* by 802.11 band only if RSSI delta condition matches */
#define WL_JOIN_PREF_TRANS_PREF	5	/* defined by requesting AP */

/* band preference */
#define WLJP_BAND_ASSOC_PREF	255	/* use what WLC_SET_ASSOC_PREFER ioctl specifies */

/* any multicast cipher suite */
#define WL_WPA_ACP_MCS_ANY	"\x00\x00\x00\x00"

struct tsinfo_arg {
	uint8 octets[3];
};
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

#define	NFIFO			6	/* # tx/rx fifopairs */

#define	WL_CNT_T_VERSION	8	/* current version of wl_cnt_t struct */

typedef struct {
	uint16	version;	/* see definition of WL_CNT_T_VERSION */
	uint16	length;		/* length of entire structure */

	/* transmit stat counters */
	uint32	txframe;	/* tx data frames */
	uint32	txbyte;		/* tx data bytes */
	uint32	txretrans;	/* tx mac retransmits */
	uint32	txerror;	/* tx data errors (derived: sum of others) */
	uint32	txctl;		/* tx management frames */
	uint32	txprshort;	/* tx short preamble frames */
	uint32	txserr;		/* tx status errors */
	uint32	txnobuf;	/* tx out of buffers errors */
	uint32	txnoassoc;	/* tx discard because we're not associated */
	uint32	txrunt;		/* tx runt frames */
	uint32	txchit;		/* tx header cache hit (fastpath) */
	uint32	txcmiss;	/* tx header cache miss (slowpath) */

	/* transmit chip error counters */
	uint32	txuflo;		/* tx fifo underflows */
	uint32	txphyerr;	/* tx phy errors (indicated in tx status) */
	uint32	txphycrs;

	/* receive stat counters */
	uint32	rxframe;	/* rx data frames */
	uint32	rxbyte;		/* rx data bytes */
	uint32	rxerror;	/* rx data errors (derived: sum of others) */
	uint32	rxctl;		/* rx management frames */
	uint32	rxnobuf;	/* rx out of buffers errors */
	uint32	rxnondata;	/* rx non data frames in the data channel errors */
	uint32	rxbadds;	/* rx bad DS errors */
	uint32	rxbadcm;	/* rx bad control or management frames */
	uint32	rxfragerr;	/* rx fragmentation errors */
	uint32	rxrunt;		/* rx runt frames */
	uint32	rxgiant;	/* rx giant frames */
	uint32	rxnoscb;	/* rx no scb error */
	uint32	rxbadproto;	/* rx invalid frames */
	uint32	rxbadsrcmac;	/* rx frames with Invalid Src Mac */
	uint32	rxbadda;	/* rx frames tossed for invalid da */
	uint32	rxfilter;	/* rx frames filtered out */

	/* receive chip error counters */
	uint32	rxoflo;		/* rx fifo overflow errors */
	uint32	rxuflo[NFIFO];	/* rx dma descriptor underflow errors */

	uint32	d11cnt_txrts_off;	/* d11cnt txrts value when reset d11cnt */
	uint32	d11cnt_rxcrc_off;	/* d11cnt rxcrc value when reset d11cnt */
	uint32	d11cnt_txnocts_off;	/* d11cnt txnocts value when reset d11cnt */

	/* misc counters */
	uint32	dmade;		/* tx/rx dma descriptor errors */
	uint32	dmada;		/* tx/rx dma data errors */
	uint32	dmape;		/* tx/rx dma descriptor protocol errors */
	uint32	reset;		/* reset count */
	uint32	tbtt;		/* cnts the TBTT int's */
	uint32	txdmawar;
	uint32	pkt_callback_reg_fail;	/* callbacks register failure */

	/* MAC counters: 32-bit version of d11.h's macstat_t */
	uint32	txallfrm;	/* total number of frames sent, incl. Data, ACK, RTS, CTS,
				 * Control Management (includes retransmissions)
				 */
	uint32	txrtsfrm;	/* number of RTS sent out by the MAC */
	uint32	txctsfrm;	/* number of CTS sent out by the MAC */
	uint32	txackfrm;	/* number of ACK frames sent out */
	uint32	txdnlfrm;	/* Not used */
	uint32	txbcnfrm;	/* beacons transmitted */
	uint32	txfunfl[8];	/* per-fifo tx underflows */
	uint32	txtplunfl;	/* Template underflows (mac was too slow to transmit ACK/CTS
				 * or BCN)
				 */
	uint32	txphyerror;	/* Transmit phy error, type of error is reported in tx-status for
				 * driver enqueued frames
				 */
	uint32	rxfrmtoolong;	/* Received frame longer than legal limit (2346 bytes) */
	uint32	rxfrmtooshrt;	/* Received frame did not contain enough bytes for its frame type */
	uint32	rxinvmachdr;	/* Either the protocol version != 0 or frame type not
				 * data/control/management
				 */
	uint32	rxbadfcs;	/* number of frames for which the CRC check failed in the MAC */
	uint32	rxbadplcp;	/* parity check of the PLCP header failed */
	uint32	rxcrsglitch;	/* PHY was able to correlate the preamble but not the header */
	uint32	rxstrt;		/* Number of received frames with a good PLCP
				 * (i.e. passing parity check)
				 */
	uint32	rxdfrmucastmbss; /* Number of received DATA frames with good FCS and matching RA */
	uint32	rxmfrmucastmbss; /* number of received mgmt frames with good FCS and matching RA */
	uint32	rxcfrmucast;	/* number of received CNTRL frames with good FCS and matching RA */
	uint32	rxrtsucast;	/* number of unicast RTS addressed to the MAC (good FCS) */
	uint32	rxctsucast;	/* number of unicast CTS addressed to the MAC (good FCS) */
	uint32	rxackucast;	/* number of ucast ACKS received (good FCS) */
	uint32	rxdfrmocast;	/* number of received DATA frames (good FCS and not matching RA) */
	uint32	rxmfrmocast;	/* number of received MGMT frames (good FCS and not matching RA) */
	uint32	rxcfrmocast;	/* number of received CNTRL frame (good FCS and not matching RA) */
	uint32	rxrtsocast;	/* number of received RTS not addressed to the MAC */
	uint32	rxctsocast;	/* number of received CTS not addressed to the MAC */
	uint32	rxdfrmmcast;	/* number of RX Data multicast frames received by the MAC */
	uint32	rxmfrmmcast;	/* number of RX Management multicast frames received by the MAC */
	uint32	rxcfrmmcast;	/* number of RX Control multicast frames received by the MAC
				 * (unlikely to see these)
				 */
	uint32	rxbeaconmbss;	/* beacons received from member of BSS */
	uint32	rxdfrmucastobss; /* number of unicast frames addressed to the MAC from
				  * other BSS (WDS FRAME)
				  */
	uint32	rxbeaconobss;	/* beacons received from other BSS */
	uint32	rxrsptmout;	/* Number of response timeouts for transmitted frames
				 * expecting a response
				 */
	uint32	bcntxcancl;	/* transmit beacons canceled due to receipt of beacon (IBSS) */
	uint32	rxf0ovfl;	/* Number of receive fifo 0 overflows */
	uint32	rxf1ovfl;	/* Number of receive fifo 1 overflows (obsolete) */
	uint32	rxf2ovfl;	/* Number of receive fifo 2 overflows (obsolete) */
	uint32	txsfovfl;	/* Number of transmit status fifo overflows (obsolete) */
	uint32	pmqovfl;	/* Number of PMQ overflows */
	uint32	rxcgprqfrm;	/* Number of received Probe requests that made it into
				 * the PRQ fifo
				 */
	uint32	rxcgprsqovfl;	/* Rx Probe Request Que overflow in the AP */
	uint32	txcgprsfail;	/* Tx Probe Response Fail. AP sent probe response but did
				 * not get ACK
				 */
	uint32	txcgprssuc;	/* Tx Probe Response Success (ACK was received) */
	uint32	prs_timeout;	/* Number of probe requests that were dropped from the PRQ
				 * fifo because a probe response could not be sent out within
				 * the time limit defined in M_PRS_MAXTIME
				 */
	uint32	rxnack;		/* obsolete */
	uint32	frmscons;	/* obsolete */
	uint32	txnack;		/* obsolete */
	uint32	txglitch_nack;	/* obsolete */
	uint32	txburst;	/* obsolete */

	/* 802.11 MIB counters, pp. 614 of 802.11 reaff doc. */
	uint32	txfrag;		/* dot11TransmittedFragmentCount */
	uint32	txmulti;	/* dot11MulticastTransmittedFrameCount */
	uint32	txfail;		/* dot11FailedCount */
	uint32	txretry;	/* dot11RetryCount */
	uint32	txretrie;	/* dot11MultipleRetryCount */
	uint32	rxdup;		/* dot11FrameduplicateCount */
	uint32	txrts;		/* dot11RTSSuccessCount */
	uint32	txnocts;	/* dot11RTSFailureCount */
	uint32	txnoack;	/* dot11ACKFailureCount */
	uint32	rxfrag;		/* dot11ReceivedFragmentCount */
	uint32	rxmulti;	/* dot11MulticastReceivedFrameCount */
	uint32	rxcrc;		/* dot11FCSErrorCount */
	uint32	txfrmsnt;	/* dot11TransmittedFrameCount (bogus MIB?) */
	uint32	rxundec;	/* dot11WEPUndecryptableCount */

	/* WPA2 counters (see rxundec for DecryptFailureCount) */
	uint32	tkipmicfaill;	/* TKIPLocalMICFailures */
	uint32	tkipcntrmsr;	/* TKIPCounterMeasuresInvoked */
	uint32	tkipreplay;	/* TKIPReplays */
	uint32	ccmpfmterr;	/* CCMPFormatErrors */
	uint32	ccmpreplay;	/* CCMPReplays */
	uint32	ccmpundec;	/* CCMPDecryptErrors */
	uint32	fourwayfail;	/* FourWayHandshakeFailures */
	uint32	wepundec;	/* dot11WEPUndecryptableCount */
	uint32	wepicverr;	/* dot11WEPICVErrorCount */
	uint32	decsuccess;	/* DecryptSuccessCount */
	uint32	tkipicverr;	/* TKIPICVErrorCount */
	uint32	wepexcluded;	/* dot11WEPExcludedCount */

	uint32	txchanrej;	/* Tx frames suppressed due to channel rejection */
	uint32	psmwds;		/* Count PSM watchdogs */
	uint32	phywatchdog;	/* Count Phy watchdogs (triggered by ucode) */

	/* MBSS counters, AP only */
	uint32	prq_entries_handled;	/* PRQ entries read in */
	uint32	prq_undirected_entries;	/*    which were bcast bss & ssid */
	uint32	prq_bad_entries;	/*    which could not be translated to info */
	uint32	atim_suppress_count;	/* TX suppressions on ATIM fifo */
	uint32	bcn_template_not_ready;	/* Template marked in use on send bcn ... */
	uint32	bcn_template_not_ready_done; /* ...but "DMA done" interrupt rcvd */
	uint32	late_tbtt_dpc;	/* TBTT DPC did not happen in time */

	/* per-rate receive stat counters */
	uint32  rx1mbps;	/* packets rx at 1Mbps */
	uint32  rx2mbps;	/* packets rx at 2Mbps */
	uint32  rx5mbps5;	/* packets rx at 5.5Mbps */
	uint32  rx6mbps;	/* packets rx at 6Mbps */
	uint32  rx9mbps;	/* packets rx at 9Mbps */
	uint32  rx11mbps;	/* packets rx at 11Mbps */
	uint32  rx12mbps;	/* packets rx at 12Mbps */
	uint32  rx18mbps;	/* packets rx at 18Mbps */
	uint32  rx24mbps;	/* packets rx at 24Mbps */
	uint32  rx36mbps;	/* packets rx at 36Mbps */
	uint32  rx48mbps;	/* packets rx at 48Mbps */
	uint32  rx54mbps;	/* packets rx at 54Mbps */
	uint32  rx108mbps; 	/* packets rx at 108mbps */
	uint32  rx162mbps;	/* packets rx at 162mbps */
	uint32  rx216mbps;	/* packets rx at 216 mbps */
	uint32  rx270mbps;	/* packets rx at 270 mbps */
	uint32  rx324mbps;	/* packets rx at 324 mbps */
	uint32  rx378mbps;	/* packets rx at 378 mbps */
	uint32  rx432mbps;	/* packets rx at 432 mbps */
	uint32  rx486mbps;	/* packets rx at 486 mbps */
	uint32  rx540mbps;	/* packets rx at 540 mbps */

	/* pkteng rx frame stats */
	uint32	pktengrxducast; /* unicast frames rxed by the pkteng code */
	uint32	pktengrxdmcast; /* multicast frames rxed by the pkteng code */

	uint32	rfdisable;	/* count of radio disables */
	uint32	bphy_rxcrsglitch;	/* PHY count of bphy glitches */

	uint32	txexptime;	/* Tx frames suppressed due to timer expiration */

	uint32	txmpdu_sgi;	/* count for sgi transmit */
	uint32	rxmpdu_sgi;	/* count for sgi received */
	uint32	txmpdu_stbc;	/* count for stbc transmit */
	uint32	rxmpdu_stbc;	/* count for stbc received */

	uint32	rxundec_mcst;	/* dot11WEPUndecryptableCount */

	/* WPA2 counters (see rxundec for DecryptFailureCount) */
	uint32	tkipmicfaill_mcst;	/* TKIPLocalMICFailures */
	uint32	tkipcntrmsr_mcst;	/* TKIPCounterMeasuresInvoked */
	uint32	tkipreplay_mcst;	/* TKIPReplays */
	uint32	ccmpfmterr_mcst;	/* CCMPFormatErrors */
	uint32	ccmpreplay_mcst;	/* CCMPReplays */
	uint32	ccmpundec_mcst;	/* CCMPDecryptErrors */
	uint32	fourwayfail_mcst;	/* FourWayHandshakeFailures */
	uint32	wepundec_mcst;	/* dot11WEPUndecryptableCount */
	uint32	wepicverr_mcst;	/* dot11WEPICVErrorCount */
	uint32	decsuccess_mcst;	/* DecryptSuccessCount */
	uint32	tkipicverr_mcst;	/* TKIPICVErrorCount */
	uint32	wepexcluded_mcst;	/* dot11WEPExcludedCount */

	uint32	dma_hang;	/* count for dma hang */
	uint32	reinit;		/* count for reinit */

	uint32  pstatxucast;	/* count of ucast frames xmitted on all psta assoc */
	uint32  pstatxnoassoc;	/* count of txnoassoc frames xmitted on all psta assoc */
	uint32  pstarxucast;	/* count of ucast frames received on all psta assoc */
	uint32  pstarxbcmc;	/* count of bcmc frames received on all psta */
	uint32  pstatxbcmc;	/* count of bcmc frames transmitted on all psta */

	uint32  cso_passthrough; /* hw cso required but passthrough */
} wl_cnt_t;

#ifndef LINUX_POSTMOGRIFY_REMOVAL
typedef struct {
	uint16  version;    /* see definition of WL_CNT_T_VERSION */
	uint16  length;     /* length of entire structure */

	/* transmit stat counters */
	uint32  txframe;    /* tx data frames */
	uint32  txbyte;     /* tx data bytes */
	uint32  txretrans;  /* tx mac retransmits */
	uint32  txerror;    /* tx data errors (derived: sum of others) */
	uint32  txctl;      /* tx management frames */
	uint32  txprshort;  /* tx short preamble frames */
	uint32  txserr;     /* tx status errors */
	uint32  txnobuf;    /* tx out of buffers errors */
	uint32  txnoassoc;  /* tx discard because we're not associated */
	uint32  txrunt;     /* tx runt frames */
	uint32  txchit;     /* tx header cache hit (fastpath) */
	uint32  txcmiss;    /* tx header cache miss (slowpath) */

	/* transmit chip error counters */
	uint32  txuflo;     /* tx fifo underflows */
	uint32  txphyerr;   /* tx phy errors (indicated in tx status) */
	uint32  txphycrs;

	/* receive stat counters */
	uint32  rxframe;    /* rx data frames */
	uint32  rxbyte;     /* rx data bytes */
	uint32  rxerror;    /* rx data errors (derived: sum of others) */
	uint32  rxctl;      /* rx management frames */
	uint32  rxnobuf;    /* rx out of buffers errors */
	uint32  rxnondata;  /* rx non data frames in the data channel errors */
	uint32  rxbadds;    /* rx bad DS errors */
	uint32  rxbadcm;    /* rx bad control or management frames */
	uint32  rxfragerr;  /* rx fragmentation errors */
	uint32  rxrunt;     /* rx runt frames */
	uint32  rxgiant;    /* rx giant frames */
	uint32  rxnoscb;    /* rx no scb error */
	uint32  rxbadproto; /* rx invalid frames */
	uint32  rxbadsrcmac;    /* rx frames with Invalid Src Mac */
	uint32  rxbadda;    /* rx frames tossed for invalid da */
	uint32  rxfilter;   /* rx frames filtered out */

	/* receive chip error counters */
	uint32  rxoflo;     /* rx fifo overflow errors */
	uint32  rxuflo[NFIFO];  /* rx dma descriptor underflow errors */

	uint32  d11cnt_txrts_off;   /* d11cnt txrts value when reset d11cnt */
	uint32  d11cnt_rxcrc_off;   /* d11cnt rxcrc value when reset d11cnt */
	uint32  d11cnt_txnocts_off; /* d11cnt txnocts value when reset d11cnt */

	/* misc counters */
	uint32  dmade;      /* tx/rx dma descriptor errors */
	uint32  dmada;      /* tx/rx dma data errors */
	uint32  dmape;      /* tx/rx dma descriptor protocol errors */
	uint32  reset;      /* reset count */
	uint32  tbtt;       /* cnts the TBTT int's */
	uint32  txdmawar;
	uint32  pkt_callback_reg_fail;  /* callbacks register failure */

	/* MAC counters: 32-bit version of d11.h's macstat_t */
	uint32  txallfrm;   /* total number of frames sent, incl. Data, ACK, RTS, CTS,
			     * Control Management (includes retransmissions)
			     */
	uint32  txrtsfrm;   /* number of RTS sent out by the MAC */
	uint32  txctsfrm;   /* number of CTS sent out by the MAC */
	uint32  txackfrm;   /* number of ACK frames sent out */
	uint32  txdnlfrm;   /* Not used */
	uint32  txbcnfrm;   /* beacons transmitted */
	uint32  txfunfl[8]; /* per-fifo tx underflows */
	uint32  txtplunfl;  /* Template underflows (mac was too slow to transmit ACK/CTS
			     * or BCN)
			     */
	uint32  txphyerror; /* Transmit phy error, type of error is reported in tx-status for
			     * driver enqueued frames
			     */
	uint32  rxfrmtoolong;   /* Received frame longer than legal limit (2346 bytes) */
	uint32  rxfrmtooshrt;   /* Received frame did not contain enough bytes for its frame type */
	uint32  rxinvmachdr;    /* Either the protocol version != 0 or frame type not
				 * data/control/management
			   */
	uint32  rxbadfcs;   /* number of frames for which the CRC check failed in the MAC */
	uint32  rxbadplcp;  /* parity check of the PLCP header failed */
	uint32  rxcrsglitch;    /* PHY was able to correlate the preamble but not the header */
	uint32  rxstrt;     /* Number of received frames with a good PLCP
			     * (i.e. passing parity check)
			     */
	uint32  rxdfrmucastmbss; /* Number of received DATA frames with good FCS and matching RA */
	uint32  rxmfrmucastmbss; /* number of received mgmt frames with good FCS and matching RA */
	uint32  rxcfrmucast;    /* number of received CNTRL frames with good FCS and matching RA */
	uint32  rxrtsucast; /* number of unicast RTS addressed to the MAC (good FCS) */
	uint32  rxctsucast; /* number of unicast CTS addressed to the MAC (good FCS) */
	uint32  rxackucast; /* number of ucast ACKS received (good FCS) */
	uint32  rxdfrmocast;    /* number of received DATA frames (good FCS and not matching RA) */
	uint32  rxmfrmocast;    /* number of received MGMT frames (good FCS and not matching RA) */
	uint32  rxcfrmocast;    /* number of received CNTRL frame (good FCS and not matching RA) */
	uint32  rxrtsocast; /* number of received RTS not addressed to the MAC */
	uint32  rxctsocast; /* number of received CTS not addressed to the MAC */
	uint32  rxdfrmmcast;    /* number of RX Data multicast frames received by the MAC */
	uint32  rxmfrmmcast;    /* number of RX Management multicast frames received by the MAC */
	uint32  rxcfrmmcast;    /* number of RX Control multicast frames received by the MAC
				 * (unlikely to see these)
				 */
	uint32  rxbeaconmbss;   /* beacons received from member of BSS */
	uint32  rxdfrmucastobss; /* number of unicast frames addressed to the MAC from
				  * other BSS (WDS FRAME)
				  */
	uint32  rxbeaconobss;   /* beacons received from other BSS */
	uint32  rxrsptmout; /* Number of response timeouts for transmitted frames
			     * expecting a response
			     */
	uint32  bcntxcancl; /* transmit beacons canceled due to receipt of beacon (IBSS) */
	uint32  rxf0ovfl;   /* Number of receive fifo 0 overflows */
	uint32  rxf1ovfl;   /* Number of receive fifo 1 overflows (obsolete) */
	uint32  rxf2ovfl;   /* Number of receive fifo 2 overflows (obsolete) */
	uint32  txsfovfl;   /* Number of transmit status fifo overflows (obsolete) */
	uint32  pmqovfl;    /* Number of PMQ overflows */
	uint32  rxcgprqfrm; /* Number of received Probe requests that made it into
			     * the PRQ fifo
			     */
	uint32  rxcgprsqovfl;   /* Rx Probe Request Que overflow in the AP */
	uint32  txcgprsfail;    /* Tx Probe Response Fail. AP sent probe response but did
				 * not get ACK
				 */
	uint32  txcgprssuc; /* Tx Probe Response Success (ACK was received) */
	uint32  prs_timeout;    /* Number of probe requests that were dropped from the PRQ
				 * fifo because a probe response could not be sent out within
				 * the time limit defined in M_PRS_MAXTIME
				 */
	uint32  rxnack;
	uint32  frmscons;
	uint32  txnack;
	uint32  txglitch_nack;  /* obsolete */
	uint32  txburst;    /* obsolete */

	/* 802.11 MIB counters, pp. 614 of 802.11 reaff doc. */
	uint32  txfrag;     /* dot11TransmittedFragmentCount */
	uint32  txmulti;    /* dot11MulticastTransmittedFrameCount */
	uint32  txfail;     /* dot11FailedCount */
	uint32  txretry;    /* dot11RetryCount */
	uint32  txretrie;   /* dot11MultipleRetryCount */
	uint32  rxdup;      /* dot11FrameduplicateCount */
	uint32  txrts;      /* dot11RTSSuccessCount */
	uint32  txnocts;    /* dot11RTSFailureCount */
	uint32  txnoack;    /* dot11ACKFailureCount */
	uint32  rxfrag;     /* dot11ReceivedFragmentCount */
	uint32  rxmulti;    /* dot11MulticastReceivedFrameCount */
	uint32  rxcrc;      /* dot11FCSErrorCount */
	uint32  txfrmsnt;   /* dot11TransmittedFrameCount (bogus MIB?) */
	uint32  rxundec;    /* dot11WEPUndecryptableCount */

	/* WPA2 counters (see rxundec for DecryptFailureCount) */
	uint32  tkipmicfaill;   /* TKIPLocalMICFailures */
	uint32  tkipcntrmsr;    /* TKIPCounterMeasuresInvoked */
	uint32  tkipreplay; /* TKIPReplays */
	uint32  ccmpfmterr; /* CCMPFormatErrors */
	uint32  ccmpreplay; /* CCMPReplays */
	uint32  ccmpundec;  /* CCMPDecryptErrors */
	uint32  fourwayfail;    /* FourWayHandshakeFailures */
	uint32  wepundec;   /* dot11WEPUndecryptableCount */
	uint32  wepicverr;  /* dot11WEPICVErrorCount */
	uint32  decsuccess; /* DecryptSuccessCount */
	uint32  tkipicverr; /* TKIPICVErrorCount */
	uint32  wepexcluded;    /* dot11WEPExcludedCount */

	uint32  rxundec_mcst;   /* dot11WEPUndecryptableCount */

	/* WPA2 counters (see rxundec for DecryptFailureCount) */
	uint32  tkipmicfaill_mcst;  /* TKIPLocalMICFailures */
	uint32  tkipcntrmsr_mcst;   /* TKIPCounterMeasuresInvoked */
	uint32  tkipreplay_mcst;    /* TKIPReplays */
	uint32  ccmpfmterr_mcst;    /* CCMPFormatErrors */
	uint32  ccmpreplay_mcst;    /* CCMPReplays */
	uint32  ccmpundec_mcst; /* CCMPDecryptErrors */
	uint32  fourwayfail_mcst;   /* FourWayHandshakeFailures */
	uint32  wepundec_mcst;  /* dot11WEPUndecryptableCount */
	uint32  wepicverr_mcst; /* dot11WEPICVErrorCount */
	uint32  decsuccess_mcst;    /* DecryptSuccessCount */
	uint32  tkipicverr_mcst;    /* TKIPICVErrorCount */
	uint32  wepexcluded_mcst;   /* dot11WEPExcludedCount */

	uint32  txchanrej;  /* Tx frames suppressed due to channel rejection */
	uint32  txexptime;  /* Tx frames suppressed due to timer expiration */
	uint32  psmwds;     /* Count PSM watchdogs */
	uint32  phywatchdog;    /* Count Phy watchdogs (triggered by ucode) */

	/* MBSS counters, AP only */
	uint32  prq_entries_handled;    /* PRQ entries read in */
	uint32  prq_undirected_entries; /*    which were bcast bss & ssid */
	uint32  prq_bad_entries;    /*    which could not be translated to info */
	uint32  atim_suppress_count;    /* TX suppressions on ATIM fifo */
	uint32  bcn_template_not_ready; /* Template marked in use on send bcn ... */
	uint32  bcn_template_not_ready_done; /* ...but "DMA done" interrupt rcvd */
	uint32  late_tbtt_dpc;  /* TBTT DPC did not happen in time */

	/* per-rate receive stat counters */
	uint32  rx1mbps;    /* packets rx at 1Mbps */
	uint32  rx2mbps;    /* packets rx at 2Mbps */
	uint32  rx5mbps5;   /* packets rx at 5.5Mbps */
	uint32  rx6mbps;    /* packets rx at 6Mbps */
	uint32  rx9mbps;    /* packets rx at 9Mbps */
	uint32  rx11mbps;   /* packets rx at 11Mbps */
	uint32  rx12mbps;   /* packets rx at 12Mbps */
	uint32  rx18mbps;   /* packets rx at 18Mbps */
	uint32  rx24mbps;   /* packets rx at 24Mbps */
	uint32  rx36mbps;   /* packets rx at 36Mbps */
	uint32  rx48mbps;   /* packets rx at 48Mbps */
	uint32  rx54mbps;   /* packets rx at 54Mbps */
	uint32  rx108mbps;  /* packets rx at 108mbps */
	uint32  rx162mbps;  /* packets rx at 162mbps */
	uint32  rx216mbps;  /* packets rx at 216 mbps */
	uint32  rx270mbps;  /* packets rx at 270 mbps */
	uint32  rx324mbps;  /* packets rx at 324 mbps */
	uint32  rx378mbps;  /* packets rx at 378 mbps */
	uint32  rx432mbps;  /* packets rx at 432 mbps */
	uint32  rx486mbps;  /* packets rx at 486 mbps */
	uint32  rx540mbps;  /* packets rx at 540 mbps */

	/* pkteng rx frame stats */
	uint32  pktengrxducast; /* unicast frames rxed by the pkteng code */
	uint32  pktengrxdmcast; /* multicast frames rxed by the pkteng code */

	uint32  rfdisable;  /* count of radio disables */
	uint32  bphy_rxcrsglitch;   /* PHY count of bphy glitches */

	uint32  txmpdu_sgi; /* count for sgi transmit */
	uint32  rxmpdu_sgi; /* count for sgi received */
	uint32  txmpdu_stbc;    /* count for stbc transmit */
	uint32  rxmpdu_stbc;    /* count for stbc received */
} wl_cnt_ver_six_t;

#define	WL_DELTA_STATS_T_VERSION	1	/* current version of wl_delta_stats_t struct */

typedef struct {
	uint16 version;     /* see definition of WL_DELTA_STATS_T_VERSION */
	uint16 length;      /* length of entire structure */

	/* transmit stat counters */
	uint32 txframe;     /* tx data frames */
	uint32 txbyte;      /* tx data bytes */
	uint32 txretrans;   /* tx mac retransmits */
	uint32 txfail;      /* tx failures */

	/* receive stat counters */
	uint32 rxframe;     /* rx data frames */
	uint32 rxbyte;      /* rx data bytes */

	/* per-rate receive stat counters */
	uint32  rx1mbps;	/* packets rx at 1Mbps */
	uint32  rx2mbps;	/* packets rx at 2Mbps */
	uint32  rx5mbps5;	/* packets rx at 5.5Mbps */
	uint32  rx6mbps;	/* packets rx at 6Mbps */
	uint32  rx9mbps;	/* packets rx at 9Mbps */
	uint32  rx11mbps;	/* packets rx at 11Mbps */
	uint32  rx12mbps;	/* packets rx at 12Mbps */
	uint32  rx18mbps;	/* packets rx at 18Mbps */
	uint32  rx24mbps;	/* packets rx at 24Mbps */
	uint32  rx36mbps;	/* packets rx at 36Mbps */
	uint32  rx48mbps;	/* packets rx at 48Mbps */
	uint32  rx54mbps;	/* packets rx at 54Mbps */
	uint32  rx108mbps; 	/* packets rx at 108mbps */
	uint32  rx162mbps;	/* packets rx at 162mbps */
	uint32  rx216mbps;	/* packets rx at 216 mbps */
	uint32  rx270mbps;	/* packets rx at 270 mbps */
	uint32  rx324mbps;	/* packets rx at 324 mbps */
	uint32  rx378mbps;	/* packets rx at 378 mbps */
	uint32  rx432mbps;	/* packets rx at 432 mbps */
	uint32  rx486mbps;	/* packets rx at 486 mbps */
	uint32  rx540mbps;	/* packets rx at 540 mbps */
} wl_delta_stats_t;
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

#define WL_WME_CNT_VERSION	1	/* current version of wl_wme_cnt_t */

typedef struct {
	uint32 packets;
	uint32 bytes;
} wl_traffic_stats_t;

typedef struct {
	uint16	version;	/* see definition of WL_WME_CNT_VERSION */
	uint16	length;		/* length of entire structure */

	wl_traffic_stats_t tx[AC_COUNT];	/* Packets transmitted */
	wl_traffic_stats_t tx_failed[AC_COUNT];	/* Packets dropped or failed to transmit */
	wl_traffic_stats_t rx[AC_COUNT];	/* Packets received */
	wl_traffic_stats_t rx_failed[AC_COUNT];	/* Packets failed to receive */

	wl_traffic_stats_t forward[AC_COUNT];	/* Packets forwarded by AP */

	wl_traffic_stats_t tx_expired[AC_COUNT];	/* packets dropped due to lifetime expiry */

} wl_wme_cnt_t;

#ifndef LINUX_POSTMOGRIFY_REMOVAL
struct wl_msglevel2 {
	uint32 low;
	uint32 high;
};

typedef struct wl_mkeep_alive_pkt {
	uint16	version; /* Version for mkeep_alive */
	uint16	length; /* length of fixed parameters in the structure */
	uint32	period_msec;
	uint16	len_bytes;
	uint8	keep_alive_id; /* 0 - 3 for N = 4 */
	uint8	data[1];
} wl_mkeep_alive_pkt_t;

#define WL_MKEEP_ALIVE_VERSION		1
#define WL_MKEEP_ALIVE_FIXED_LEN	OFFSETOF(wl_mkeep_alive_pkt_t, data)
#define WL_MKEEP_ALIVE_PRECISION	500

#ifdef WLBA

#define WLC_BA_CNT_VERSION  1   /* current version of wlc_ba_cnt_t */

/* block ack related stats */
typedef struct wlc_ba_cnt {
	uint16  version;    /* WLC_BA_CNT_VERSION */
	uint16  length;     /* length of entire structure */

	/* transmit stat counters */
	uint32 txpdu;       /* pdus sent */
	uint32 txsdu;       /* sdus sent */
	uint32 txfc;        /* tx side flow controlled packets */
	uint32 txfci;       /* tx side flow control initiated */
	uint32 txretrans;   /* retransmitted pdus */
	uint32 txbatimer;   /* ba resend due to timer */
	uint32 txdrop;      /* dropped packets */
	uint32 txaddbareq;  /* addba req sent */
	uint32 txaddbaresp; /* addba resp sent */
	uint32 txdelba;     /* delba sent */
	uint32 txba;        /* ba sent */
	uint32 txbar;       /* bar sent */
	uint32 txpad[4];    /* future */

	/* receive side counters */
	uint32 rxpdu;       /* pdus recd */
	uint32 rxqed;       /* pdus buffered before sending up */
	uint32 rxdup;       /* duplicate pdus */
	uint32 rxnobuf;     /* pdus discarded due to no buf */
	uint32 rxaddbareq;  /* addba req recd */
	uint32 rxaddbaresp; /* addba resp recd */
	uint32 rxdelba;     /* delba recd */
	uint32 rxba;        /* ba recd */
	uint32 rxbar;       /* bar recd */
	uint32 rxinvba;     /* invalid ba recd */
	uint32 rxbaholes;   /* ba recd with holes */
	uint32 rxunexp;     /* unexpected packets */
	uint32 rxpad[4];    /* future */
} wlc_ba_cnt_t;
#endif /* WLBA */

/* structure for per-tid ampdu control */
struct ampdu_tid_control {
	uint8 tid;			/* tid */
	uint8 enable;			/* enable/disable */
};

/* structure for identifying ea/tid for sending addba/delba */
struct ampdu_ea_tid {
	struct ether_addr ea;		/* Station address */
	uint8 tid;			/* tid */
};
/* structure for identifying retry/tid for retry_limit_tid/rr_retry_limit_tid */
struct ampdu_retry_tid {
	uint8 tid;	/* tid */
	uint8 retry;	/* retry value */
};

/* Different discovery modes for dpt */
#define	DPT_DISCOVERY_MANUAL	0x01	/* manual discovery mode */
#define	DPT_DISCOVERY_AUTO	0x02	/* auto discovery mode */
#define	DPT_DISCOVERY_SCAN	0x04	/* scan-based discovery mode */

/* different path selection values */
#define DPT_PATHSEL_AUTO	0	/* auto mode for path selection */
#define DPT_PATHSEL_DIRECT	1	/* always use direct DPT path */
#define DPT_PATHSEL_APPATH	2	/* always use AP path */

/* different ops for deny list */
#define DPT_DENY_LIST_ADD 	1	/* add to dpt deny list */
#define DPT_DENY_LIST_REMOVE 	2	/* remove from dpt deny list */

/* different ops for manual end point */
#define DPT_MANUAL_EP_CREATE	1	/* create manual dpt endpoint */
#define DPT_MANUAL_EP_MODIFY	2	/* modify manual dpt endpoint */
#define DPT_MANUAL_EP_DELETE	3	/* delete manual dpt endpoint */

/* structure for dpt iovars */
typedef struct dpt_iovar {
	struct ether_addr ea;		/* Station address */
	uint8 mode;			/* mode: depends on iovar */
	uint32 pad;			/* future */
} dpt_iovar_t;

/* flags to indicate DPT status */
#define	DPT_STATUS_ACTIVE	0x01	/* link active (though may be suspended) */
#define	DPT_STATUS_AES		0x02	/* link secured through AES encryption */
#define	DPT_STATUS_FAILED	0x04	/* DPT link failed */

#define	DPT_FNAME_LEN		48	/* Max length of friendly name */

typedef struct dpt_status {
	uint8 status;			/* flags to indicate status */
	uint8 fnlen;			/* length of friendly name */
	uchar name[DPT_FNAME_LEN];	/* friendly name */
	uint32 rssi;			/* RSSI of the link */
	sta_info_t sta;			/* sta info */
} dpt_status_t;

/* structure for dpt list */
typedef struct dpt_list {
	uint32 num;			/* number of entries in struct */
	dpt_status_t status[1];		/* per station info */
} dpt_list_t;

/* structure for dpt friendly name */
typedef struct dpt_fname {
	uint8 len;			/* length of friendly name */
	uchar name[DPT_FNAME_LEN];	/* friendly name */
} dpt_fname_t;

#define BDD_FNAME_LEN       32  /* Max length of friendly name */
typedef struct bdd_fname {
	uint8 len;          /* length of friendly name */
	uchar name[BDD_FNAME_LEN];  /* friendly name */
} bdd_fname_t;

/* structure for addts arguments */
/* For ioctls that take a list of TSPEC */
struct tslist {
	int count;			/* number of tspecs */
	struct tsinfo_arg tsinfo[1];	/* variable length array of tsinfo */
};

#ifdef WLTDLS
/* different ops for manual end point */
#define TDLS_MANUAL_EP_CREATE	1	/* create manual dpt endpoint */
#define TDLS_MANUAL_EP_MODIFY	2	/* modify manual dpt endpoint */
#define TDLS_MANUAL_EP_DELETE	3	/* delete manual dpt endpoint */
#define TDLS_MANUAL_EP_PM		4	/*  put dpt endpoint in PM mode */
#define TDLS_MANUAL_EP_WAKE		5	/* wake up dpt endpoint from PM */
#define TDLS_MANUAL_EP_DISCOVERY	6	/* discover if endpoint is TDLS capable */
#define TDLS_MANUAL_EP_CHSW		7	/* channel switch */

/* structure for tdls iovars */
typedef struct tdls_iovar {
	struct ether_addr ea;		/* Station address */
	uint8 mode;			/* mode: depends on iovar */
	chanspec_t chanspec;
	uint32 pad;			/* future */
} tdls_iovar_t;

/* modes */
#define TDLS_WFD_IE_TX 0
#define TDLS_WFD_IE_RX 1
#define TDLS_WFD_IE_SIZE 255
/* structure for tdls wfd ie */
typedef struct tdls_wfd_ie_iovar {
	struct ether_addr ea;		/* Station address */
	uint8 mode;
	uint8 length;
	uint8 data[TDLS_WFD_IE_SIZE];
} tdls_wfd_ie_iovar_t;
#endif /* WLTDLS */

/* structure for addts/delts arguments */
typedef struct tspec_arg {
	uint16 version;			/* see definition of TSPEC_ARG_VERSION */
	uint16 length;			/* length of entire structure */
	uint flag;			/* bit field */
	/* TSPEC Arguments */
	struct tsinfo_arg tsinfo;	/* TS Info bit field */
	uint16 nom_msdu_size;		/* (Nominal or fixed) MSDU Size (bytes) */
	uint16 max_msdu_size;		/* Maximum MSDU Size (bytes) */
	uint min_srv_interval;		/* Minimum Service Interval (us) */
	uint max_srv_interval;		/* Maximum Service Interval (us) */
	uint inactivity_interval;	/* Inactivity Interval (us) */
	uint suspension_interval;	/* Suspension Interval (us) */
	uint srv_start_time;		/* Service Start Time (us) */
	uint min_data_rate;		/* Minimum Data Rate (bps) */
	uint mean_data_rate;		/* Mean Data Rate (bps) */
	uint peak_data_rate;		/* Peak Data Rate (bps) */
	uint max_burst_size;		/* Maximum Burst Size (bytes) */
	uint delay_bound;		/* Delay Bound (us) */
	uint min_phy_rate;		/* Minimum PHY Rate (bps) */
	uint16 surplus_bw;		/* Surplus Bandwidth Allowance (range 1.0 to 8.0) */
	uint16 medium_time;		/* Medium Time (32 us/s periods) */
	uint8 dialog_token;		/* dialog token */
} tspec_arg_t;

/* tspec arg for desired station */
typedef	struct tspec_per_sta_arg {
	struct ether_addr ea;
	struct tspec_arg ts;
} tspec_per_sta_arg_t;

/* structure for max bandwidth for each access category */
typedef	struct wme_max_bandwidth {
	uint32	ac[AC_COUNT];	/* max bandwidth for each access category */
} wme_max_bandwidth_t;

#define WL_WME_MBW_PARAMS_IO_BYTES (sizeof(wme_max_bandwidth_t))

/* current version of wl_tspec_arg_t struct */
#define	TSPEC_ARG_VERSION		2	/* current version of wl_tspec_arg_t struct */
#define TSPEC_ARG_LENGTH		55	/* argument length from tsinfo to medium_time */
#define TSPEC_DEFAULT_DIALOG_TOKEN	42	/* default dialog token */
#define TSPEC_DEFAULT_SBW_FACTOR	0x3000	/* default surplus bw */


#define WL_WOWL_KEEPALIVE_MAX_PACKET_SIZE  80
#define WLC_WOWL_MAX_KEEPALIVE	2

/* define for flag */
#define TSPEC_PENDING		0	/* TSPEC pending */
#define TSPEC_ACCEPTED		1	/* TSPEC accepted */
#define TSPEC_REJECTED		2	/* TSPEC rejected */
#define TSPEC_UNKNOWN		3	/* TSPEC unknown */
#define TSPEC_STATUS_MASK	7	/* TSPEC status mask */


/* Software feature flag defines used by wlfeatureflag */
#ifdef WLAFTERBURNER
#define WL_SWFL_ABBFL       0x0001 /* Allow Afterburner on systems w/o hardware BFL */
#define WL_SWFL_ABENCORE    0x0002 /* Allow AB on non-4318E chips */
#endif /* WLAFTERBURNER */
#define WL_SWFL_NOHWRADIO	0x0004
#define WL_SWFL_FLOWCONTROL     0x0008 /* Enable backpressure to OS stack */
#define WL_SWFL_WLBSSSORT	0x0010 /* Per-port supports sorting of BSS */

#define WL_LIFETIME_MAX 0xFFFF /* Max value in ms */

/* Packet lifetime configuration per ac */
typedef struct wl_lifetime {
	uint32 ac;	        /* access class */
	uint32 lifetime;    /* Packet lifetime value in ms */
} wl_lifetime_t;

/* Channel Switch Announcement param */
typedef struct wl_chan_switch {
	uint8 mode;		/* value 0 or 1 */
	uint8 count;		/* count # of beacons before switching */
	chanspec_t chspec;	/* chanspec */
	uint8 reg;		/* regulatory class */
} wl_chan_switch_t;

/* Roaming trigger definitions for WLC_SET_ROAM_TRIGGER.
 *
 * (-100 < value < 0)   value is used directly as a roaming trigger in dBm
 * (0 <= value) value specifies a logical roaming trigger level from
 *                      the list below
 *
 * WLC_GET_ROAM_TRIGGER always returns roaming trigger value in dBm, never
 * the logical roam trigger value.
 */
#define WLC_ROAM_TRIGGER_DEFAULT	0 /* default roaming trigger */
#define WLC_ROAM_TRIGGER_BANDWIDTH	1 /* optimize for bandwidth roaming trigger */
#define WLC_ROAM_TRIGGER_DISTANCE	2 /* optimize for distance roaming trigger */
#define WLC_ROAM_TRIGGER_AUTO		3 /* auto-detect environment */
#define WLC_ROAM_TRIGGER_MAX_VALUE	3 /* max. valid value */

#define WLC_ROAM_NEVER_ROAM_TRIGGER	(-100) /* Avoid Roaming by setting a large value */

/* Preferred Network Offload (PNO, formerly PFN) defines */
#define WPA_AUTH_PFN_ANY	0xffffffff	/* for PFN, match only ssid */

enum {
	PFN_LIST_ORDER,
	PFN_RSSI
};

enum {
	DISABLE,
	ENABLE
};

enum {
	OFF_ADAPT,
	SMART_ADAPT,
	STRICT_ADAPT,
	SLOW_ADAPT
};

#define SORT_CRITERIA_BIT		0
#define AUTO_NET_SWITCH_BIT		1
#define ENABLE_BKGRD_SCAN_BIT		2
#define IMMEDIATE_SCAN_BIT		3
#define	AUTO_CONNECT_BIT		4
#define	ENABLE_BD_SCAN_BIT		5
#define ENABLE_ADAPTSCAN_BIT		6
#define IMMEDIATE_EVENT_BIT		8
#define SUPPRESS_SSID_BIT		9
#define ENABLE_NET_OFFLOAD_BIT		10

#define SORT_CRITERIA_MASK		0x0001
#define AUTO_NET_SWITCH_MASK	0x0002
#define ENABLE_BKGRD_SCAN_MASK	0x0004
#define IMMEDIATE_SCAN_MASK		0x0008
#define	AUTO_CONNECT_MASK		0x0010

#define ENABLE_BD_SCAN_MASK		0x0020
#define ENABLE_ADAPTSCAN_MASK	0x00c0
#define IMMEDIATE_EVENT_MASK	0x0100
#define SUPPRESS_SSID_MASK	0x0200
#define ENABLE_NET_OFFLOAD_MASK	0x0400

#define PFN_VERSION				2
#define PFN_SCANRESULT_VERSION	1
#define MAX_PFN_LIST_COUNT	16

#define PFN_COMPLETE			1
#define PFN_INCOMPLETE			0

#define DEFAULT_BESTN			2
#define DEFAULT_MSCAN			0
#define DEFAULT_REPEAT			10
#define DEFAULT_EXP				2

/* PFN network info structure */
typedef struct wl_pfn_subnet_info {
	struct ether_addr BSSID;
	uint8	channel; /* channel number only */
	uint8	SSID_len;
	uint8	SSID[32];
} wl_pfn_subnet_info_t;

typedef struct wl_pfn_net_info {
	wl_pfn_subnet_info_t pfnsubnet;
	int16	RSSI; /* receive signal strength (in dBm) */
	uint16	timestamp; /* age in seconds */
} wl_pfn_net_info_t;

typedef struct wl_pfn_scanresults {
	uint32 version;
	uint32 status;
	uint32 count;
	wl_pfn_net_info_t netinfo[1];
} wl_pfn_scanresults_t;

/* PFN data structure */
typedef struct wl_pfn_param {
	int32 version;			/* PNO parameters version */
	int32 scan_freq;		/* Scan frequency */
	int32 lost_network_timeout;	/* Timeout in sec. to declare
					 * discovered network as lost
					 */
	int16 flags;			/* Bit field to control features
					 * of PFN such as sort criteria auto
					 * enable switch and background scan
					 */
	int16 rssi_margin;		/* Margin to avoid jitter for choosing a
					 * PFN based on RSSI sort criteria
					 */
	uint8 bestn; /* number of best networks in each scan */
	uint8 mscan; /* number of scans recorded */
	uint8 repeat; /* Minimum number of scan intervals
				     *before scan frequency changes in adaptive scan
				     */
	uint8 exp; /* Exponent of 2 for maximum scan interval */
	int32 slow_freq; /* slow scan period */
} wl_pfn_param_t;

typedef struct wl_pfn_bssid {
	struct ether_addr 	macaddr;
	/* Bit4: suppress_lost, Bit3: suppress_found */
	uint16				flags;
} wl_pfn_bssid_t;
#define WL_PFN_SUPPRESSFOUND_MASK	0x08
#define WL_PFN_SUPPRESSLOST_MASK	0x10

typedef struct wl_pfn_cfg {
	uint32				reporttype;
	int32				channel_num;
	uint16				channel_list[WL_NUMCHANNELS];
} wl_pfn_cfg_t;
#define WL_PFN_REPORT_ALLNET 	0
#define WL_PFN_REPORT_SSIDNET 	1
#define WL_PFN_REPORT_BSSIDNET 	2

typedef struct wl_pfn {
	wlc_ssid_t		ssid;			/* ssid name and its length */
	int32			flags;			/* bit2: hidden */
	int32			infra;			/* BSS Vs IBSS */
	int32			auth;			/* Open Vs Closed */
	int32			wpa_auth;		/* WPA type */
	int32			wsec;			/* wsec value */
} wl_pfn_t;
#define WL_PFN_HIDDEN_BIT		2
#define PNO_SCAN_MAX_FW		508*1000	/* max time scan time in msec */
#define PNO_SCAN_MAX_FW_SEC	PNO_SCAN_MAX_FW/1000 /* max time scan time in SEC */
#define PNO_SCAN_MIN_FW_SEC	10			/* min time scan time in SEC */
#define WL_PFN_HIDDEN_MASK		0x4

#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* TCP Checksum Offload defines */
#define TOE_TX_CSUM_OL		0x00000001
#define TOE_RX_CSUM_OL		0x00000002

#ifndef LINUX_POSTMOGRIFY_REMOVAL
/* TCP Checksum Offload error injection for testing */
#define TOE_ERRTEST_TX_CSUM	0x00000001
#define TOE_ERRTEST_RX_CSUM	0x00000002
#define TOE_ERRTEST_RX_CSUM2	0x00000004

struct toe_ol_stats_t {
	/* Num of tx packets that don't need to be checksummed */
	uint32 tx_summed;

	/* Num of tx packets where checksum is filled by offload engine */
	uint32 tx_iph_fill;
	uint32 tx_tcp_fill;
	uint32 tx_udp_fill;
	uint32 tx_icmp_fill;

	/*  Num of rx packets where toe finds out if checksum is good or bad */
	uint32 rx_iph_good;
	uint32 rx_iph_bad;
	uint32 rx_tcp_good;
	uint32 rx_tcp_bad;
	uint32 rx_udp_good;
	uint32 rx_udp_bad;
	uint32 rx_icmp_good;
	uint32 rx_icmp_bad;

	/* Num of tx packets in which csum error is injected */
	uint32 tx_tcp_errinj;
	uint32 tx_udp_errinj;
	uint32 tx_icmp_errinj;

	/* Num of rx packets in which csum error is injected */
	uint32 rx_tcp_errinj;
	uint32 rx_udp_errinj;
	uint32 rx_icmp_errinj;
};

/* ARP Offload feature flags for arp_ol iovar */
#define ARP_OL_AGENT		0x00000001
#define ARP_OL_SNOOP		0x00000002
#define ARP_OL_HOST_AUTO_REPLY	0x00000004
#define ARP_OL_PEER_AUTO_REPLY	0x00000008

/* ARP Offload error injection */
#define ARP_ERRTEST_REPLY_PEER	0x1
#define ARP_ERRTEST_REPLY_HOST	0x2

#define ARP_MULTIHOMING_MAX	8	/* Maximum local host IP addresses */
#define ND_MULTIHOMING_MAX 8	/* Maximum local host IP addresses */

/* Arp offload statistic counts */
struct arp_ol_stats_t {
	uint32  host_ip_entries;	/* Host IP table addresses (more than one if multihomed) */
	uint32  host_ip_overflow;	/* Host IP table additions skipped due to overflow */

	uint32  arp_table_entries;	/* ARP table entries */
	uint32  arp_table_overflow;	/* ARP table additions skipped due to overflow */

	uint32  host_request;		/* ARP requests from host */
	uint32  host_reply;		/* ARP replies from host */
	uint32  host_service;		/* ARP requests from host serviced by ARP Agent */

	uint32  peer_request;		/* ARP requests received from network */
	uint32  peer_request_drop;	/* ARP requests from network that were dropped */
	uint32  peer_reply;		/* ARP replies received from network */
	uint32  peer_reply_drop;	/* ARP replies from network that were dropped */
	uint32  peer_service;		/* ARP request from host serviced by ARP Agent */
};

/* NS offload statistic counts */
struct nd_ol_stats_t {
	uint32  host_ip_entries;    /* Host IP table addresses (more than one if multihomed) */
	uint32  host_ip_overflow;   /* Host IP table additions skipped due to overflow */
	uint32  peer_request;       /* NS requests received from network */
	uint32  peer_request_drop;  /* NS requests from network that were dropped */
	uint32  peer_reply_drop;    /* NA replies from network that were dropped */
	uint32  peer_service;       /* NS request from host serviced by firmware */
};

/*
 * Keep-alive packet offloading.
 */

/* NAT keep-alive packets format: specifies the re-transmission period, the packet
 * length, and packet contents.
 */
typedef struct wl_keep_alive_pkt {
	uint32	period_msec;	/* Retransmission period (0 to disable packet re-transmits) */
	uint16	len_bytes;	/* Size of packet to transmit (0 to disable packet re-transmits) */
	uint8	data[1];	/* Variable length packet to transmit.  Contents should include
				 * entire ethernet packet (enet header, IP header, UDP header,
				 * and UDP payload) in network byte order.
				 */
} wl_keep_alive_pkt_t;

#define WL_KEEP_ALIVE_FIXED_LEN		OFFSETOF(wl_keep_alive_pkt_t, data)

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
	uint32	offset;		/* Offset within received packet to start pattern matching.
				 * Offset '0' is the first byte of the ethernet header.
				 */
	uint32	size_bytes;	/* Size of the pattern.  Bitmask must be the same size. */
	uint8   mask_and_pattern[1]; /* Variable length mask and pattern data.  mask starts
				      * at offset 0.  Pattern immediately follows mask.
				      */
} wl_pkt_filter_pattern_t;

/* IOVAR "pkt_filter_add" parameter. Used to install packet filters. */
typedef struct wl_pkt_filter {
	uint32	id;		/* Unique filter id, specified by app. */
	uint32	type;		/* Filter type (WL_PKT_FILTER_TYPE_xxx). */
	uint32	negate_match;	/* Negate the result of filter matches */
	union {			/* Filter definitions */
		wl_pkt_filter_pattern_t pattern;	/* Pattern matching filter */
	} u;
} wl_pkt_filter_t;

#define WL_PKT_FILTER_FIXED_LEN		  OFFSETOF(wl_pkt_filter_t, u)
#define WL_PKT_FILTER_PATTERN_FIXED_LEN	  OFFSETOF(wl_pkt_filter_pattern_t, mask_and_pattern)

/* IOVAR "pkt_filter_enable" parameter. */
typedef struct wl_pkt_filter_enable {
	uint32	id;		/* Unique filter id */
	uint32	enable;		/* Enable/disable bool */
} wl_pkt_filter_enable_t;

/* IOVAR "pkt_filter_list" parameter. Used to retrieve a list of installed filters. */
typedef struct wl_pkt_filter_list {
	uint32	num;		/* Number of installed packet filters */
	wl_pkt_filter_t	filter[1];	/* Variable array of packet filters. */
} wl_pkt_filter_list_t;

#define WL_PKT_FILTER_LIST_FIXED_LEN	  OFFSETOF(wl_pkt_filter_list_t, filter)

/* IOVAR "pkt_filter_stats" parameter. Used to retrieve debug statistics. */
typedef struct wl_pkt_filter_stats {
	uint32	num_pkts_matched;	/* # filter matches for specified filter id */
	uint32	num_pkts_forwarded;	/* # packets fwded from dongle to host for all filters */
	uint32	num_pkts_discarded;	/* # packets discarded by dongle for all filters */
} wl_pkt_filter_stats_t;

/* Sequential Commands ioctl */
typedef struct wl_seq_cmd_ioctl {
	uint32 cmd;		/* common ioctl definition */
	uint32 len;		/* length of user buffer */
} wl_seq_cmd_ioctl_t;

#define WL_SEQ_CMD_ALIGN_BYTES	4

/* These are the set of get IOCTLs that should be allowed when using
 * IOCTL sequence commands. These are issued implicitly by wl.exe each time
 * it is invoked. We never want to buffer these, or else wl.exe will stop working.
 */
#define WL_SEQ_CMDS_GET_IOCTL_FILTER(cmd) \
	(((cmd) == WLC_GET_MAGIC)		|| \
	 ((cmd) == WLC_GET_VERSION)		|| \
	 ((cmd) == WLC_GET_AP)			|| \
	 ((cmd) == WLC_GET_INSTANCE))

/*
 * Packet engine interface
 */

#define WL_PKTENG_PER_TX_START			0x01
#define WL_PKTENG_PER_TX_STOP			0x02
#define WL_PKTENG_PER_RX_START			0x04
#define WL_PKTENG_PER_RX_WITH_ACK_START 	0x05
#define WL_PKTENG_PER_TX_WITH_ACK_START 	0x06
#define WL_PKTENG_PER_RX_STOP			0x08
#define WL_PKTENG_PER_MASK			0xff

#define WL_PKTENG_SYNCHRONOUS			0x100	/* synchronous flag */

typedef struct wl_pkteng {
	uint32 flags;
	uint32 delay;			/* Inter-packet delay */
	uint32 nframes;			/* Number of frames */
	uint32 length;			/* Packet length */
	uint8  seqno;			/* Enable/disable sequence no. */
	struct ether_addr dest;		/* Destination address */
	struct ether_addr src;		/* Source address */
} wl_pkteng_t;

#define NUM_80211b_RATES	4
#define NUM_80211ag_RATES	8
#define NUM_80211n_RATES	32
#define NUM_80211_RATES		(NUM_80211b_RATES+NUM_80211ag_RATES+NUM_80211n_RATES)
typedef struct wl_pkteng_stats {
	uint32 lostfrmcnt;		/* RX PER test: no of frames lost (skip seqno) */
	int32 rssi;			/* RSSI */
	int32 snr;			/* signal to noise ratio */
	uint16 rxpktcnt[NUM_80211_RATES+1];
} wl_pkteng_stats_t;


#define WL_WOWL_MAGIC       (1 << 0)    /* Wakeup on Magic packet */
#define WL_WOWL_NET         (1 << 1)    /* Wakeup on Netpattern */
#define WL_WOWL_DIS         (1 << 2)    /* Wakeup on loss-of-link due to Disassoc/Deauth */
#define WL_WOWL_RETR        (1 << 3)    /* Wakeup on retrograde TSF */
#define WL_WOWL_BCN         (1 << 4)    /* Wakeup on loss of beacon */
#define WL_WOWL_TST         (1 << 5)    /* Wakeup after test */
#define WL_WOWL_M1          (1 << 6)    /* Wakeup after PTK refresh */
#define WL_WOWL_EAPID       (1 << 7)    /* Wakeup after receipt of EAP-Identity Req */
#define WL_WOWL_PME_GPIO    (1 << 8)    /* Wakeind via PME(0) or GPIO(1) */
#define WL_WOWL_NEEDTKIP1   (1 << 9)    /* need tkip phase 1 key to be updated by the driver */
#define WL_WOWL_GTK_FAILURE (1 << 10)   /* enable wakeup if GTK fails */
#define WL_WOWL_EXTMAGPAT   (1 << 11)   /* support extended magic packets */
#define WL_WOWL_ARPOFFLOAD  (1 << 12)   /* support ARP/NS/keepalive offloading */
#define WL_WOWL_WPA2        (1 << 13)   /* read protocol version for EAPOL frames */
#define WL_WOWL_KEYROT      (1 << 14)   /* If the bit is set, use key rotaton */
#define WL_WOWL_BCAST       (1 << 15)   /* If the bit is set, frm received was bcast frame */

#define MAGIC_PKT_MINLEN 102    /* Magic pkt min length is 6 * 0xFF + 16 * ETHER_ADDR_LEN */

#define WOWL_PATTEN_TYPE_ARP	(1 << 0)	/* ARP offload Pattern */
#define WOWL_PATTEN_TYPE_NA		(1 << 1)	/* NA offload Pattern */

typedef struct {
	uint32 masksize;		/* Size of the mask in #of bytes */
	uint32 offset;			/* Offset to start looking for the packet in # of bytes */
	uint32 patternoffset;	/* Offset of start of pattern in the structure */
	uint32 patternsize;		/* Size of the pattern itself in #of bytes */
	uint32 id;				/* id */
	uint32 reasonsize;		/* Size of the wakeup reason code */
	uint32 flags;			/* Flags to tell the pattern type and other properties */
	/* Mask follows the structure above */
	/* Pattern follows the mask is at 'patternoffset' from the start */
} wl_wowl_pattern_t;

typedef struct {
	uint			count;
	wl_wowl_pattern_t	pattern[1];
} wl_wowl_pattern_list_t;

typedef struct {
	uint8	pci_wakeind;	/* Whether PCI PMECSR PMEStatus bit was set */
	uint16	ucode_wakeind;	/* What wakeup-event indication was set by ucode */
} wl_wowl_wakeind_t;


/* per AC rate control related data structure */
typedef struct wl_txrate_class {
	uint8		init_rate;
	uint8		min_rate;
	uint8		max_rate;
} wl_txrate_class_t;



/* Overlap BSS Scan parameters default, minimum, maximum */
#define WLC_OBSS_SCAN_PASSIVE_DWELL_DEFAULT		20	/* unit TU */
#define WLC_OBSS_SCAN_PASSIVE_DWELL_MIN			5	/* unit TU */
#define WLC_OBSS_SCAN_PASSIVE_DWELL_MAX			1000	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_DWELL_DEFAULT		10	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_DWELL_MIN			10	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_DWELL_MAX			1000	/* unit TU */
#define WLC_OBSS_SCAN_WIDTHSCAN_INTERVAL_DEFAULT	300	/* unit Sec */
#define WLC_OBSS_SCAN_WIDTHSCAN_INTERVAL_MIN		10	/* unit Sec */
#define WLC_OBSS_SCAN_WIDTHSCAN_INTERVAL_MAX		900	/* unit Sec */
#define WLC_OBSS_SCAN_CHANWIDTH_TRANSITION_DLY_DEFAULT	5
#define WLC_OBSS_SCAN_CHANWIDTH_TRANSITION_DLY_MIN	5
#define WLC_OBSS_SCAN_CHANWIDTH_TRANSITION_DLY_MAX	100
#define WLC_OBSS_SCAN_PASSIVE_TOTAL_PER_CHANNEL_DEFAULT	200	/* unit TU */
#define WLC_OBSS_SCAN_PASSIVE_TOTAL_PER_CHANNEL_MIN	200	/* unit TU */
#define WLC_OBSS_SCAN_PASSIVE_TOTAL_PER_CHANNEL_MAX	10000	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_TOTAL_PER_CHANNEL_DEFAULT	20	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_TOTAL_PER_CHANNEL_MIN	20	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVE_TOTAL_PER_CHANNEL_MAX	10000	/* unit TU */
#define WLC_OBSS_SCAN_ACTIVITY_THRESHOLD_DEFAULT	25	/* unit percent */
#define WLC_OBSS_SCAN_ACTIVITY_THRESHOLD_MIN		0	/* unit percent */
#define WLC_OBSS_SCAN_ACTIVITY_THRESHOLD_MAX		100	/* unit percent */

/* structure for Overlap BSS scan arguments */
typedef struct wl_obss_scan_arg {
	int16	passive_dwell;
	int16	active_dwell;
	int16	bss_widthscan_interval;
	int16	passive_total;
	int16	active_total;
	int16	chanwidth_transition_delay;
	int16	activity_threshold;
} wl_obss_scan_arg_t;

#define WL_OBSS_SCAN_PARAM_LEN	sizeof(wl_obss_scan_arg_t)
#define WL_MIN_NUM_OBSS_SCAN_ARG 7	/* minimum number of arguments required for OBSS Scan */

#define WL_COEX_INFO_MASK		0x07
#define WL_COEX_INFO_REQ		0x01
#define	WL_COEX_40MHZ_INTOLERANT	0x02
#define	WL_COEX_WIDTH20			0x04

#define	WLC_RSSI_INVALID	 0	/* invalid RSSI value */

#define MAX_RSSI_LEVELS 8

/* RSSI event notification configuration. */
typedef struct wl_rssi_event {
	uint32 rate_limit_msec;		/* # of events posted to application will be limited to
					 * one per specified period (0 to disable rate limit).
					 */
	uint8 num_rssi_levels;		/* Number of entries in rssi_levels[] below */
	int8 rssi_levels[MAX_RSSI_LEVELS];	/* Variable number of RSSI levels. An event
						 * will be posted each time the RSSI of received
						 * beacons/packets crosses a level.
						 */
} wl_rssi_event_t;

typedef struct wl_action_obss_coex_req {
	uint8 info;
	uint8 num;
	uint8 ch_list[1];
} wl_action_obss_coex_req_t;


/* IOVar parameter block for small MAC address array with type indicator */
#define WL_IOV_MAC_PARAM_LEN  4

#define WL_IOV_PKTQ_LOG_PRECS 16

typedef struct {
	uint32 num_addrs;
	char   addr_type[WL_IOV_MAC_PARAM_LEN];
	struct ether_addr ea[WL_IOV_MAC_PARAM_LEN];
} wl_iov_mac_params_t;


/* Parameter block for PKTQ_LOG statistics */
typedef struct {
	uint32 requested;      /* packets requested to be stored */
	uint32 stored;         /* packets stored */
	uint32 saved;          /* packets saved,
	                          because a lowest priority queue has given away one packet
	                       */
	uint32 selfsaved;      /* packets saved,
	                          because an older packet from the same queue has been dropped
	                       */
	uint32 full_dropped;   /* packets dropped,
	                          because pktq is full with higher precedence packets
	                       */
	uint32 dropped;        /* packets dropped because pktq per that precedence is full */
	uint32 sacrificed;     /* packets dropped,
	                          in order to save one from a queue of a highest priority
	                       */
	uint32 busy;           /* packets droped because of hardware/transmission error */
	uint32 retry;          /* packets re-sent because they were not received */
	uint32 ps_retry;       /* packets retried again prior to moving power save mode */
	uint32 retry_drop;     /* packets finally dropped after retry limit */
	uint32 max_avail;      /* the high-water mark of the queue capacity for packets -
	                          goes to zero as queue fills
	                       */
	uint32 max_used;       /* the high-water mark of the queue utilisation for packets -
	                          increases with use ('inverse' of max_avail)
	                       */
	uint32 queue_capacity; /* the maximum capacity of the queue */
} pktq_log_counters_v01_t;

#define sacrified sacrificed

typedef struct {
	uint8                num_prec[WL_IOV_MAC_PARAM_LEN];
	pktq_log_counters_v01_t  counters[WL_IOV_MAC_PARAM_LEN][WL_IOV_PKTQ_LOG_PRECS];
	char                 headings[1];
} pktq_log_format_v01_t;


typedef struct {
	uint32               version;
	wl_iov_mac_params_t  params;
	union {
		pktq_log_format_v01_t v01;
	} pktq_log;
} wl_iov_pktq_log_t;


/* **** EXTLOG **** */
#define EXTLOG_CUR_VER		0x0100

#define MAX_ARGSTR_LEN		18 /* At least big enough for storing ETHER_ADDR_STR_LEN */

/* log modules (bitmap) */
#define LOG_MODULE_COMMON	0x0001
#define LOG_MODULE_ASSOC	0x0002
#define LOG_MODULE_EVENT	0x0004
#define LOG_MODULE_MAX		3			/* Update when adding module */

/* log levels */
#define WL_LOG_LEVEL_DISABLE	0
#define WL_LOG_LEVEL_ERR	1
#define WL_LOG_LEVEL_WARN	2
#define WL_LOG_LEVEL_INFO	3
#define WL_LOG_LEVEL_MAX	WL_LOG_LEVEL_INFO	/* Update when adding level */

/* flag */
#define LOG_FLAG_EVENT		1

/* log arg_type */
#define LOG_ARGTYPE_NULL	0
#define LOG_ARGTYPE_STR		1	/* %s */
#define LOG_ARGTYPE_INT		2	/* %d */
#define LOG_ARGTYPE_INT_STR	3	/* %d...%s */
#define LOG_ARGTYPE_STR_INT	4	/* %s...%d */

typedef struct wlc_extlog_cfg {
	int max_number;
	uint16 module;	/* bitmap */
	uint8 level;
	uint8 flag;
	uint16 version;
} wlc_extlog_cfg_t;

typedef struct log_record {
	uint32 time;
	uint16 module;
	uint16 id;
	uint8 level;
	uint8 sub_unit;
	uint8 seq_num;
	int32 arg;
	char str[MAX_ARGSTR_LEN];
} log_record_t;

typedef struct wlc_extlog_req {
	uint32 from_last;
	uint32 num;
} wlc_extlog_req_t;

typedef struct wlc_extlog_results {
	uint16 version;
	uint16 record_len;
	uint32 num;
	log_record_t logs[1];
} wlc_extlog_results_t;

typedef struct log_idstr {
	uint16	id;
	uint16	flag;
	uint8	arg_type;
	const char	*fmt_str;
} log_idstr_t;

#define FMTSTRF_USER		1

/* flat ID definitions
 * New definitions HAVE TO BE ADDED at the end of the table. Otherwise, it will
 * affect backward compatibility with pre-existing apps
 */
typedef enum {
	FMTSTR_DRIVER_UP_ID = 0,
	FMTSTR_DRIVER_DOWN_ID = 1,
	FMTSTR_SUSPEND_MAC_FAIL_ID = 2,
	FMTSTR_NO_PROGRESS_ID = 3,
	FMTSTR_RFDISABLE_ID = 4,
	FMTSTR_REG_PRINT_ID = 5,
	FMTSTR_EXPTIME_ID = 6,
	FMTSTR_JOIN_START_ID = 7,
	FMTSTR_JOIN_COMPLETE_ID = 8,
	FMTSTR_NO_NETWORKS_ID = 9,
	FMTSTR_SECURITY_MISMATCH_ID = 10,
	FMTSTR_RATE_MISMATCH_ID = 11,
	FMTSTR_AP_PRUNED_ID = 12,
	FMTSTR_KEY_INSERTED_ID = 13,
	FMTSTR_DEAUTH_ID = 14,
	FMTSTR_DISASSOC_ID = 15,
	FMTSTR_LINK_UP_ID = 16,
	FMTSTR_LINK_DOWN_ID = 17,
	FMTSTR_RADIO_HW_OFF_ID = 18,
	FMTSTR_RADIO_HW_ON_ID = 19,
	FMTSTR_EVENT_DESC_ID = 20,
	FMTSTR_PNP_SET_POWER_ID = 21,
	FMTSTR_RADIO_SW_OFF_ID = 22,
	FMTSTR_RADIO_SW_ON_ID = 23,
	FMTSTR_PWD_MISMATCH_ID = 24,
	FMTSTR_FATAL_ERROR_ID = 25,
	FMTSTR_AUTH_FAIL_ID = 26,
	FMTSTR_ASSOC_FAIL_ID = 27,
	FMTSTR_IBSS_FAIL_ID = 28,
	FMTSTR_EXTAP_FAIL_ID = 29,
	FMTSTR_MAX_ID
} log_fmtstr_id_t;

#ifdef DONGLEOVERLAYS
typedef struct {
	uint32 flags_idx;	/* lower 8 bits: overlay index; upper 24 bits: flags */
	uint32 offset;		/* offset into overlay region to write code */
	uint32 len;			/* overlay code len */
	/* overlay code follows this struct */
} wl_ioctl_overlay_t;

#define OVERLAY_IDX_MASK		0x000000ff
#define OVERLAY_IDX_SHIFT		0
#define OVERLAY_FLAGS_MASK		0xffffff00
#define OVERLAY_FLAGS_SHIFT		8
/* overlay written to device memory immediately after loading the base image */
#define OVERLAY_FLAG_POSTLOAD	0x100
/* defer overlay download until the device responds w/WLC_E_OVL_DOWNLOAD event */
#define OVERLAY_FLAG_DEFER_DL	0x200
/* overlay downloaded prior to the host going to sleep */
#define OVERLAY_FLAG_PRESLEEP	0x400

#define OVERLAY_DOWNLOAD_CHUNKSIZE	1024
#endif /* DONGLEOVERLAYS */

#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* no default structure packing */
#include <packed_section_end.h>

/* require strict packing */
#include <packed_section_start.h>

#ifndef LINUX_POSTMOGRIFY_REMOVAL

/* Structures and constants used for "vndr_ie" IOVar interface */
#define VNDR_IE_CMD_LEN		4	/* length of the set command string:
					 * "add", "del" (+ NUL)
					 */

/* 802.11 Mgmt Packet flags */
#define VNDR_IE_BEACON_FLAG	0x1
#define VNDR_IE_PRBRSP_FLAG	0x2
#define VNDR_IE_ASSOCRSP_FLAG	0x4
#define VNDR_IE_AUTHRSP_FLAG	0x8
#define VNDR_IE_PRBREQ_FLAG	0x10
#define VNDR_IE_ASSOCREQ_FLAG	0x20
#define VNDR_IE_IWAPID_FLAG	0x40 /* vendor IE in IW advertisement protocol ID field */
#define VNDR_IE_CUSTOM_FLAG	0x100 /* allow custom IE id */

#define VNDR_IE_INFO_HDR_LEN	(sizeof(uint32))

typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 pktflag;			/* bitmask indicating which packet(s) contain this IE */
	vndr_ie_t vndr_ie_data;		/* vendor IE data */
} BWL_POST_PACKED_STRUCT vndr_ie_info_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	int iecount;			/* number of entries in the vndr_ie_list[] array */
	vndr_ie_info_t vndr_ie_list[1];	/* variable size list of vndr_ie_info_t structs */
} BWL_POST_PACKED_STRUCT vndr_ie_buf_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	char cmd[VNDR_IE_CMD_LEN];	/* vndr_ie IOVar set command : "add", "del" + NUL */
	vndr_ie_buf_t vndr_ie_buffer;	/* buffer containing Vendor IE list information */
} BWL_POST_PACKED_STRUCT vndr_ie_setbuf_t;

/* tag_ID/length/value_buffer tuple */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint8	id;
	uint8	len;
	uint8	data[1];
} BWL_POST_PACKED_STRUCT tlv_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 pktflag;			/* bitmask indicating which packet(s) contain this IE */
	tlv_t ie_data;		/* IE data */
} BWL_POST_PACKED_STRUCT ie_info_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	int iecount;			/* number of entries in the ie_list[] array */
	ie_info_t ie_list[1];	/* variable size list of ie_info_t structs */
} BWL_POST_PACKED_STRUCT ie_buf_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	char cmd[VNDR_IE_CMD_LEN];	/* ie IOVar set command : "add" + NUL */
	ie_buf_t ie_buffer;	/* buffer containing IE list information */
} BWL_POST_PACKED_STRUCT ie_setbuf_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 pktflag;		/* bitmask indicating which packet(s) contain this IE */
	uint8 id;		/* IE type */
} BWL_POST_PACKED_STRUCT ie_getbuf_t;

/* structures used to define format of wps ie data from probe requests */
/* passed up to applications via iovar "prbreq_wpsie" */
typedef BWL_PRE_PACKED_STRUCT struct sta_prbreq_wps_ie_hdr {
	struct ether_addr staAddr;
	uint16 ieLen;
} BWL_POST_PACKED_STRUCT sta_prbreq_wps_ie_hdr_t;

typedef BWL_PRE_PACKED_STRUCT struct sta_prbreq_wps_ie_data {
	sta_prbreq_wps_ie_hdr_t hdr;
	uint8 ieData[1];
} BWL_POST_PACKED_STRUCT sta_prbreq_wps_ie_data_t;

typedef BWL_PRE_PACKED_STRUCT struct sta_prbreq_wps_ie_list {
	uint32 totLen;
	uint8 ieDataList[1];
} BWL_POST_PACKED_STRUCT sta_prbreq_wps_ie_list_t;


#ifdef WLMEDIA_TXFAILEVENT
typedef BWL_PRE_PACKED_STRUCT struct {
	char   dest[ETHER_ADDR_LEN]; /* destination MAC */
	uint8  prio;            /* Packet Priority */
	uint8  flags;           /* Flags           */
	uint32 tsf_l;           /* TSF timer low   */
	uint32 tsf_h;           /* TSF timer high  */
	uint16 rates;           /* Main Rates      */
	uint16 txstatus;        /* TX Status       */
} BWL_POST_PACKED_STRUCT txfailinfo_t;
#endif /* WLMEDIA_TXFAILEVENT */

#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* no strict structure packing */
#include <packed_section_end.h>

#ifndef LINUX_POSTMOGRIFY_REMOVAL
/* Global ASSERT Logging */
#define ASSERTLOG_CUR_VER	0x0100
#define MAX_ASSRTSTR_LEN	64

typedef struct assert_record {
	uint32 time;
	uint8 seq_num;
	char str[MAX_ASSRTSTR_LEN];
} assert_record_t;

typedef struct assertlog_results {
	uint16 version;
	uint16 record_len;
	uint32 num;
	assert_record_t logs[1];
} assertlog_results_t;

#define LOGRRC_FIX_LEN	8
#define IOBUF_ALLOWED_NUM_OF_LOGREC(type, len) ((len - LOGRRC_FIX_LEN)/sizeof(type))


/* channel interference measurement (chanim) related defines */

/* chanim mode */
#define CHANIM_DISABLE	0	/* disabled */
#define CHANIM_DETECT	1	/* detection only */
#define CHANIM_EXT		2 	/* external state machine */
#define CHANIM_ACT		3	/* full internal state machine, detect + act */
#define CHANIM_MODE_MAX 4

/* define for apcs reason code */
#define APCS_INIT		0
#define APCS_IOCTL 		1
#define APCS_CHANIM 	2
#define APCS_CSTIMER	3
#define APCS_BTA		4

/* number of ACS record entries */
#define CHANIM_ACS_RECORD			10

/* CHANIM */
#define CCASTATS_TXDUR  0
#define CCASTATS_INBSS  1
#define CCASTATS_OBSS   2
#define CCASTATS_NOCTG  3
#define CCASTATS_NOPKT  4
#define CCASTATS_DOZE   5
#define CCASTATS_TXOP	6
#define CCASTATS_GDTXDUR        7
#define CCASTATS_BDTXDUR        8
#define CCASTATS_MAX    9

/* chanim acs record */
typedef struct {
	bool valid;
	uint8 trigger;
	chanspec_t selected_chspc;
	int8 bgnoise;
	uint32 glitch_cnt;
	uint8 ccastats;
	uint timestamp;
} chanim_acs_record_t;

typedef struct {
	chanim_acs_record_t acs_record[CHANIM_ACS_RECORD];
	uint8 count;
	uint timestamp;
} wl_acs_record_t;

typedef struct chanim_stats {
	uint32 glitchcnt;               /* normalized as per second count */
	uint32 badplcp;                 /* normalized as per second count */
	uint8 ccastats[CCASTATS_MAX]; 	/* normalized as 0-255 */
	int8 bgnoise;					/* background noise level (in dBm) */
	chanspec_t chanspec;
	uint32 timestamp;
} chanim_stats_t;

#define WL_CHANIM_STATS_VERSION 1
#define WL_CHANIM_COUNT_ALL	0xff
#define WL_CHANIM_COUNT_ONE	0x1

typedef struct {
	uint32 buflen;
	uint32 version;
	uint32 count;
	chanim_stats_t stats[1];
} wl_chanim_stats_t;

#define WL_CHANIM_STATS_FIXED_LEN OFFSETOF(wl_chanim_stats_t, stats)

/* Noise measurement metrics. */
#define NOISE_MEASURE_KNOISE	0x1

/* scb probe parameter */
typedef struct {
	uint32 scb_timeout;
	uint32 scb_activity_time;
	uint32 scb_max_probe;
} wl_scb_probe_t;

/* ap tpc modes */
#define	AP_TPC_OFF		0
#define	AP_TPC_BSS_PWR		1	/* BSS power control */
#define AP_TPC_AP_PWR		2	/* AP power control */
#define	AP_TPC_AP_BSS_PWR	3	/* Both AP and BSS power control */
#define AP_TPC_MAX_LINK_MARGIN	127

/* ap tpc modes */
#define	AP_TPC_OFF		0
#define	AP_TPC_BSS_PWR		1	/* BSS power control */
#define AP_TPC_AP_PWR		2	/* AP power control */
#define	AP_TPC_AP_BSS_PWR	3	/* Both AP and BSS power control */
#define AP_TPC_MAX_LINK_MARGIN	127

/* structure/defines for selective mgmt frame (smf) stats support */

#define SMFS_VERSION 1
/* selected mgmt frame (smf) stats element */
typedef struct wl_smfs_elem {
	uint32 count;
	uint16 code;  /* SC or RC code */
} wl_smfs_elem_t;

typedef struct wl_smf_stats {
	uint32 version;
	uint16 length;	/* reserved for future usage */
	uint8 type;
	uint8 codetype;
	uint32 ignored_cnt;
	uint32 malformed_cnt;
	uint32 count_total; /* count included the interested group */
	wl_smfs_elem_t elem[1];
} wl_smf_stats_t;

#define WL_SMFSTATS_FIXED_LEN OFFSETOF(wl_smf_stats_t, elem);

enum {
	SMFS_CODETYPE_SC,
	SMFS_CODETYPE_RC
};

/* reuse two number in the sc/rc space */
#define	SMFS_CODE_MALFORMED 0xFFFE
#define SMFS_CODE_IGNORED 	0xFFFD

typedef enum smfs_type {
	SMFS_TYPE_AUTH,
	SMFS_TYPE_ASSOC,
	SMFS_TYPE_REASSOC,
	SMFS_TYPE_DISASSOC_TX,
	SMFS_TYPE_DISASSOC_RX,
	SMFS_TYPE_DEAUTH_TX,
	SMFS_TYPE_DEAUTH_RX,
	SMFS_TYPE_MAX
} smfs_type_t;

#ifdef PHYMON

#define PHYMON_VERSION 1

typedef struct wl_phycal_core_state {
	/* Tx IQ/LO calibration coeffs */
	int16 tx_iqlocal_a;
	int16 tx_iqlocal_b;
	int8 tx_iqlocal_ci;
	int8 tx_iqlocal_cq;
	int8 tx_iqlocal_di;
	int8 tx_iqlocal_dq;
	int8 tx_iqlocal_ei;
	int8 tx_iqlocal_eq;
	int8 tx_iqlocal_fi;
	int8 tx_iqlocal_fq;

	/* Rx IQ calibration coeffs */
	int16 rx_iqcal_a;
	int16 rx_iqcal_b;

	uint8 tx_iqlocal_pwridx; /* Tx Power Index for Tx IQ/LO calibration */
	uint32 papd_epsilon_table[64]; /* PAPD epsilon table */
	int16 papd_epsilon_offset; /* PAPD epsilon offset */
	uint8 curr_tx_pwrindex; /* Tx power index */
	int8 idle_tssi; /* Idle TSSI */
	int8 est_tx_pwr; /* Estimated Tx Power (dB) */
	int8 est_rx_pwr; /* Estimated Rx Power (dB) from RSSI */
	uint16 rx_gaininfo; /* Rx gain applied on last Rx pkt */
	uint16 init_gaincode; /* initgain required for ACI */
	int8 estirr_tx;
	int8 estirr_rx;

} wl_phycal_core_state_t;

typedef struct wl_phycal_state {
	int version;
	int8 num_phy_cores; /* number of cores */
	int8 curr_temperature; /* on-chip temperature sensor reading */
	chanspec_t chspec; /* channspec for this state */
	bool aci_state; /* ACI state: ON/OFF */
	uint16 crsminpower; /* crsminpower required for ACI */
	uint16 crsminpowerl; /* crsminpowerl required for ACI */
	uint16 crsminpoweru; /* crsminpoweru required for ACI */
	wl_phycal_core_state_t phycal_core[1];
} wl_phycal_state_t;

#define WL_PHYCAL_STAT_FIXED_LEN OFFSETOF(wl_phycal_state_t, phycal_core)
#endif /* PHYMON */

/* discovery state */
typedef struct wl_p2p_disc_st {
	uint8 state;	/* see state */
	chanspec_t chspec;	/* valid in listen state */
	uint16 dwell;	/* valid in listen state, in ms */
} wl_p2p_disc_st_t;

/* state */
#define WL_P2P_DISC_ST_SCAN	0
#define WL_P2P_DISC_ST_LISTEN	1
#define WL_P2P_DISC_ST_SEARCH	2

/* scan request */
typedef struct wl_p2p_scan {
	uint8 type;		/* 'S' for WLC_SCAN, 'E' for "escan" */
	uint8 reserved[3];
	/* scan or escan parms... */
} wl_p2p_scan_t;

/* i/f request */
typedef struct wl_p2p_if {
	struct ether_addr addr;
	uint8 type;	/* see i/f type */
	chanspec_t chspec;	/* for p2p_ifadd GO */
} wl_p2p_if_t;

/* i/f type */
#define WL_P2P_IF_CLIENT	0
#define WL_P2P_IF_GO		1
#define WL_P2P_IF_DYNBCN_GO	2
#define WL_P2P_IF_DEV		3

/* i/f query */
typedef struct wl_p2p_ifq {
	uint bsscfgidx;
	char ifname[BCM_MSG_IFNAME_MAX];
} wl_p2p_ifq_t;

/* OppPS & CTWindow */
typedef struct wl_p2p_ops {
	uint8 ops;	/* 0: disable 1: enable */
	uint8 ctw;	/* >= 10 */
} wl_p2p_ops_t;

/* absence and presence request */
typedef struct wl_p2p_sched_desc {
	uint32 start;
	uint32 interval;
	uint32 duration;
	uint32 count;	/* see count */
} wl_p2p_sched_desc_t;

/* count */
#define WL_P2P_SCHED_RSVD	0
#define WL_P2P_SCHED_REPEAT	255	/* anything > 255 will be treated as 255 */

typedef struct wl_p2p_sched {
	uint8 type;	/* see schedule type */
	uint8 action;	/* see schedule action */
	uint8 option;	/* see schedule option */
	wl_p2p_sched_desc_t desc[1];
} wl_p2p_sched_t;
#define WL_P2P_SCHED_FIXED_LEN		3

/* schedule type */
#define WL_P2P_SCHED_TYPE_ABS		0	/* Scheduled Absence */
#define WL_P2P_SCHED_TYPE_REQ_ABS	1	/* Requested Absence */

/* schedule action during absence periods (for WL_P2P_SCHED_ABS type) */
#define WL_P2P_SCHED_ACTION_NONE	0	/* no action */
#define WL_P2P_SCHED_ACTION_DOZE	1	/* doze */
/* schedule option - WL_P2P_SCHED_TYPE_REQ_ABS */
#define WL_P2P_SCHED_ACTION_GOOFF	2	/* turn off GO beacon/prbrsp functions */
/* schedule option - WL_P2P_SCHED_TYPE_XXX */
#define WL_P2P_SCHED_ACTION_RESET	255	/* reset */

/* schedule option - WL_P2P_SCHED_TYPE_ABS */
#define WL_P2P_SCHED_OPTION_NORMAL	0	/* normal start/interval/duration/count */
#define WL_P2P_SCHED_OPTION_BCNPCT	1	/* percentage of beacon interval */
/* schedule option - WL_P2P_SCHED_TYPE_REQ_ABS */
#define WL_P2P_SCHED_OPTION_TSFOFS	2	/* normal start/internal/duration/count with
						 * start being an offset of the 'current' TSF
						 */

/* feature flags */
#define WL_P2P_FEAT_GO_CSA	(1 << 0)	/* GO moves with the STA using CSA method */
#define WL_P2P_FEAT_GO_NOLEGACY	(1 << 1)	/* GO does not probe respond to non-p2p probe
						 * requests
						 */
#define WL_P2P_FEAT_RESTRICT_DEV_RESP (1 << 2)	/* Restrict p2p dev interface from responding */

#ifdef WLNIC
/* nic_cnx iovar */
typedef struct wl_nic_cnx {
	uint8 opcode;
	struct ether_addr addr;
	/* the following are valid for WL_NIC_CNX_CONN */
	uint8 SSID_len;
	uint8 SSID[32];
	struct ether_addr abssid;
	uint8 join_period;
} wl_nic_cnx_t;

/* opcode */
#define WL_NIC_CNX_ADD	0	/* add NIC connection */
#define WL_NIC_CNX_DEL	1	/* delete NIC connection */
#define WL_NIC_CNX_IDX	2	/* query NIC connection index */
#define WL_NIC_CNX_CONN	3	/* join/create network */
#define WL_NIC_CNX_DIS	4	/* disconnect from network */

/* nic_cfg iovar */
typedef struct wl_nic_cfg {
	uint8 version;
	uint8 beacon_mode;
	uint16 beacon_interval;
	uint8 diluted_beacon_period;
	uint8 repeat_EQC;
	uint8 scan_length;
	uint8 scan_interval;
	uint8 scan_probability;
	uint8 awake_window_length;
	int8 TSF_correction;
	uint8 ASID;
	uint8 channel_usage_mode;
} wl_nic_cfg_t;

/* version */
#define WL_NIC_CFG_VER	1

/* beacon_mode */
#define WL_NIC_BCN_NORM		0
#define WL_NIC_BCN_DILUTED	1

/* channel_usage_mode */
#define WL_NIC_CHAN_STATIC	0
#define WL_NIC_CHAN_CYCLE	1

/* nic_cfg iovar */
typedef struct wl_nic_frm {
	uint8 type;
	struct ether_addr da;
	uint8 body[1];
} wl_nic_frm_t;

/* type */
#define WL_NIC_FRM_MYNET	1
#define WL_NIC_FRM_ACTION	2

/* i/f query */
typedef struct wl_nic_ifq {
	uint bsscfgidx;
	char ifname[BCM_MSG_IFNAME_MAX];
} wl_nic_ifq_t;

/* data mode */
/* nic_dm iovar */
typedef struct wl_nic_dm {
	uint8 enab;
	chanspec_t chspec;
} wl_nic_dm_t;
#endif /* WLNIC */

/* RFAWARE def */
#define BCM_ACTION_RFAWARE		0x77
#define BCM_ACTION_RFAWARE_DCS  0x01

/* DCS reason code define */
#define BCM_DCS_IOVAR		0x1
#define BCM_DCS_UNKNOWN		0xFF

typedef struct wl_bcmdcs_data {
	uint reason;
	chanspec_t chspec;
} wl_bcmdcs_data_t;

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

/* Proxy STA modes */
#define PSTA_MODE_DISABLED		0
#define PSTA_MODE_PROXY			1
#define PSTA_MODE_REPEATER		2


/* NAT configuration */
typedef struct {
	uint32 ipaddr;		/* interface ip address */
	uint32 ipaddr_mask;	/* interface ip address mask */
	uint32 ipaddr_gateway;	/* gateway ip address */
	uint8 mac_gateway[6];	/* gateway mac address */
	uint32 ipaddr_dns;	/* DNS server ip address, valid only for public if */
	uint8 mac_dns[6];	/* DNS server mac address,  valid only for public if */
	uint8 GUID[38];		/* interface GUID */
} nat_if_info_t;

typedef struct {
	uint op;		/* operation code */
	bool pub_if;		/* set for public if, clear for private if */
	nat_if_info_t if_info;	/* interface info */
} nat_cfg_t;

/* op code in nat_cfg */
#define NAT_OP_ENABLE		1	/* enable NAT on given interface */
#define NAT_OP_DISABLE		2	/* disable NAT on given interface */
#define NAT_OP_DISABLE_ALL	3	/* disable NAT on all interfaces */

/* NAT state */
#define NAT_STATE_ENABLED	1	/* NAT is enabled */
#define NAT_STATE_DISABLED	2	/* NAT is disabled */

typedef struct {
	int state;	/* NAT state returned */
} nat_state_t;

#ifdef PROP_TXSTATUS
/* Bit definitions for tlv iovar */
/*
 * enable RSSI signals:
 * WLFC_CTL_TYPE_RSSI
 */
#define WLFC_FLAGS_RSSI_SIGNALS		0x0001

/* enable (if/mac_open, if/mac_close,, mac_add, mac_del) signals:
 *
 * WLFC_CTL_TYPE_MAC_OPEN
 * WLFC_CTL_TYPE_MAC_CLOSE
 *
 * WLFC_CTL_TYPE_INTERFACE_OPEN
 * WLFC_CTL_TYPE_INTERFACE_CLOSE
 *
 * WLFC_CTL_TYPE_MACDESC_ADD
 * WLFC_CTL_TYPE_MACDESC_DEL
 *
 */
#define WLFC_FLAGS_XONXOFF_SIGNALS	0x0002

/* enable (status, fifo_credit, mac_credit) signals
 * WLFC_CTL_TYPE_MAC_REQUEST_CREDIT
 * WLFC_CTL_TYPE_TXSTATUS
 * WLFC_CTL_TYPE_FIFO_CREDITBACK
 */
#define WLFC_FLAGS_CREDIT_STATUS_SIGNALS	0x0004

#define WLFC_FLAGS_HOST_PROPTXSTATUS_ACTIVE	0x0008
#define WLFC_FLAGS_PSQ_GENERATIONFSM_ENABLE	0x0010
#define WLFC_FLAGS_PSQ_ZERO_BUFFER_ENABLE	0x0020
#define WLFC_FLAGS_HOST_RXRERODER_ACTIVE	0x0040
#endif /* PROP_TXSTATUS */

#define BTA_STATE_LOG_SZ	64

/* BTAMP Statemachine states */
enum {
	HCIReset = 1,
	HCIReadLocalAMPInfo,
	HCIReadLocalAMPASSOC,
	HCIWriteRemoteAMPASSOC,
	HCICreatePhysicalLink,
	HCIAcceptPhysicalLinkRequest,
	HCIDisconnectPhysicalLink,
	HCICreateLogicalLink,
	HCIAcceptLogicalLink,
	HCIDisconnectLogicalLink,
	HCILogicalLinkCancel,
	HCIAmpStateChange,
	HCIWriteLogicalLinkAcceptTimeout
};

typedef struct flush_txfifo {
	uint32 txfifobmp;
	uint32 hwtxfifoflush;
	struct ether_addr ea;
} flush_txfifo_t;

#define CHANNEL_5G_LOW_START	36	/* 5G low (36..48) CDD enable/disable bit mask */
#define CHANNEL_5G_MID_START	52	/* 5G mid (52..64) CDD enable/disable bit mask */
#define CHANNEL_5G_HIGH_START	100	/* 5G high (100..140) CDD enable/disable bit mask */
#define CHANNEL_5G_UPPER_START	149	/* 5G upper (149..161) CDD enable/disable bit mask */

enum {
	SPATIAL_MODE_2G_IDX = 0,
	SPATIAL_MODE_5G_LOW_IDX,
	SPATIAL_MODE_5G_MID_IDX,
	SPATIAL_MODE_5G_HIGH_IDX,
	SPATIAL_MODE_5G_UPPER_IDX,
	SPATIAL_MODE_MAX_IDX
};

/* IOVAR "mempool" parameter. Used to retrieve a list of memory pool statistics. */
typedef struct wl_mempool_stats {
	int	num;		/* Number of memory pools */
	bcm_mp_stats_t s[1];	/* Variable array of memory pool stats. */
} wl_mempool_stats_t;


/* D0 Coalescing */
#define IPV4_ARP_FILTER		0x0001
#define IPV4_NETBT_FILTER	0x0002
#define IPV4_LLMNR_FILTER	0x0004
#define IPV4_SSDP_FILTER	0x0008
#define IPV4_WSD_FILTER		0x0010
#define IPV6_NETBT_FILTER	0x0200
#define IPV6_LLMNR_FILTER	0x0400
#define IPV6_SSDP_FILTER	0x0800
#define IPV6_WSD_FILTER		0x1000

/* Network Offload Engine */
#define NWOE_OL_ENABLE		0x00000001

typedef struct {
	uint32 ipaddr;
	uint32 ipaddr_netmask;
	uint32 ipaddr_gateway;
} nwoe_ifconfig_t;

/*
 * Traffic management structures/defines.
 */

/* Traffic management bandwidth parameters */
#define TRF_MGMT_MAX_PRIORITIES                 3

#define TRF_MGMT_FLAG_ADD_DSCP                  0x0001  /* Add DSCP to IP TOS field */
#define TRF_MGMT_FLAG_DISABLE_SHAPING           0x0002  /* Only support traffic clasification */
#define TRF_MGMT_FLAG_DISABLE_PRIORITY_TAGGING  0x0004  /* Don't override packet's priority */

/* Traffic management priority classes */
typedef enum trf_mgmt_priority_class {
	trf_mgmt_priority_low           = 0,            /* Maps to 802.1p BO */
	trf_mgmt_priority_medium        = 1,            /* Maps to 802.1p BE */
	trf_mgmt_priority_high          = 2,            /* Maps to 802.1p VI */
	trf_mgmt_priority_invalid       = (trf_mgmt_priority_high + 1)
} trf_mgmt_priority_class_t;

/* Traffic management configuration parameters */
typedef struct trf_mgmt_config {
	uint32  trf_mgmt_enabled;                           /* 0 - disabled, 1 - enabled */
	uint32  flags;                                      /* See TRF_MGMT_FLAG_xxx defines */
	uint32  host_ip_addr;                               /* My IP address to determine subnet */
	uint32  host_subnet_mask;                           /* My subnet mask */
	uint32  downlink_bandwidth;                         /* In units of kbps */
	uint32  uplink_bandwidth;                           /* In units of kbps */
	uint32  min_tx_bandwidth[TRF_MGMT_MAX_PRIORITIES];  /* Minimum guaranteed tx bandwidth */
	uint32  min_rx_bandwidth[TRF_MGMT_MAX_PRIORITIES];  /* Minimum guaranteed rx bandwidth */
} trf_mgmt_config_t;

/* Traffic management filter */
typedef struct trf_mgmt_filter {
	struct ether_addr           dst_ether_addr;         /* His L2 address */
	uint32                      dst_ip_addr;            /* His IP address */
	uint16                      dst_port;               /* His L4 port */
	uint16                      src_port;               /* My L4 port */
	uint16                      prot;                   /* L4 protocol (only TCP or UDP) */
	uint16                      flags;                  /* TBD. For now, this must be zero. */
	trf_mgmt_priority_class_t   priority;               /* Priority for filtered packets */
} trf_mgmt_filter_t;

/* Traffic management filter list (variable length) */
typedef struct trf_mgmt_filter_list     {
	uint32              num_filters;
	trf_mgmt_filter_t   filter[1];
} trf_mgmt_filter_list_t;

/* Traffic management global info used for all queues */
typedef struct trf_mgmt_global_info {
	uint32  maximum_bytes_per_second;
	uint32  maximum_bytes_per_sampling_period;
	uint32  total_bytes_consumed_per_second;
	uint32  total_bytes_consumed_per_sampling_period;
	uint32  total_unused_bytes_per_sampling_period;
} trf_mgmt_global_info_t;

/* Traffic management shaping info per priority queue */
typedef struct trf_mgmt_shaping_info {
	uint32  gauranteed_bandwidth_percentage;
	uint32  guaranteed_bytes_per_second;
	uint32  guaranteed_bytes_per_sampling_period;
	uint32  num_bytes_produced_per_second;
	uint32  num_bytes_consumed_per_second;
	uint32  num_queued_packets;                         /* Number of packets in queue */
	uint32  num_queued_bytes;                           /* Number of bytes in queue */
} trf_mgmt_shaping_info_t;

/* Traffic management shaping info array */
typedef struct trf_mgmt_shaping_info_array {
	trf_mgmt_global_info_t   tx_global_shaping_info;
	trf_mgmt_shaping_info_t  tx_queue_shaping_info[TRF_MGMT_MAX_PRIORITIES];
	trf_mgmt_global_info_t   rx_global_shaping_info;
	trf_mgmt_shaping_info_t  rx_queue_shaping_info[TRF_MGMT_MAX_PRIORITIES];
} trf_mgmt_shaping_info_array_t;


/* Traffic management statistical counters */
typedef struct trf_mgmt_stats {
	uint32  num_processed_packets;      /* Number of packets processed */
	uint32  num_processed_bytes;        /* Number of bytes processed */
	uint32  num_discarded_packets;      /* Number of packets discarded from queue */
} trf_mgmt_stats_t;

/* Traffic management statisics array */
typedef struct trf_mgmt_stats_array     {
	trf_mgmt_stats_t  tx_queue_stats[TRF_MGMT_MAX_PRIORITIES];
	trf_mgmt_stats_t  rx_queue_stats[TRF_MGMT_MAX_PRIORITIES];
} trf_mgmt_stats_array_t;

typedef struct powersel_params {
	/* LPC Params exposed via IOVAR */
	int32		tp_ratio_thresh;  /* Throughput ratio threshold */
	uint8		rate_stab_thresh; /* Thresh for rate stability based on nupd */
	uint8		pwr_stab_thresh; /* Number of successes before power step down */
	uint8		pwr_sel_exp_time; /* Time lapse for expiry of database */
} powersel_params_t;

#endif /* LINUX_POSTMOGRIFY_REMOVAL */
#endif /* _wlioctl_h_ */
