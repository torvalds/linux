/*
 * Custom OID/ioctl definitions for
 *
 *
 * Broadcom 802.11abg Networking Device Driver
 *
 * Definitions subject to change without notice.
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: wlioctl.h 609280 2016-01-01 06:31:38Z $
 */

#ifndef _wlioctl_h_
#define	_wlioctl_h_

#include <typedefs.h>
#include <proto/ethernet.h>
#include <proto/bcmip.h>
#include <proto/bcmeth.h>
#include <proto/bcmip.h>
#include <proto/bcmevent.h>
#include <proto/802.11.h>
#include <proto/802.1d.h>
#include <bcmwifi_channels.h>
#include <bcmwifi_rates.h>
#include <devctrl_if/wlioctl_defs.h>
#include <proto/bcmipv6.h>

#include <bcm_mpool_pub.h>
#include <bcmcdc.h>





typedef struct {
	uint32 num;
	chanspec_t list[1];
} chanspec_list_t;

#define RSN_KCK_LENGTH	16
#define RSN_KEK_LENGTH	16


#ifndef INTF_NAME_SIZ
#define INTF_NAME_SIZ	16
#endif

/* Used to send ioctls over the transport pipe */
typedef struct remote_ioctl {
	cdc_ioctl_t	msg;
	uint32		data_len;
	char           intf_name[INTF_NAME_SIZ];
} rem_ioctl_t;
#define REMOTE_SIZE	sizeof(rem_ioctl_t)


/* DFS Forced param */
typedef struct wl_dfs_forced_params {
	chanspec_t chspec;
	uint16 version;
	chanspec_list_t chspec_list;
} wl_dfs_forced_t;

#define DFS_PREFCHANLIST_VER 0x01
#define WL_CHSPEC_LIST_FIXED_SIZE	OFFSETOF(chanspec_list_t, list)
#define WL_DFS_FORCED_PARAMS_FIXED_SIZE \
	(WL_CHSPEC_LIST_FIXED_SIZE + OFFSETOF(wl_dfs_forced_t, chspec_list))
#define WL_DFS_FORCED_PARAMS_MAX_SIZE \
	WL_DFS_FORCED_PARAMS_FIXED_SIZE + (WL_NUMCHANNELS * sizeof(chanspec_t))

/* association decision information */
typedef struct {
	bool		assoc_approved;		/**< (re)association approved */
	uint16		reject_reason;		/**< reason code for rejecting association */
	struct		ether_addr   da;
	int64		sys_time;		/**< current system time */
} assoc_decision_t;

#define DFS_SCAN_S_IDLE		-1
#define DFS_SCAN_S_RADAR_FREE 0
#define DFS_SCAN_S_RADAR_FOUND 1
#define DFS_SCAN_S_INPROGESS 2
#define DFS_SCAN_S_SCAN_ABORTED 3
#define DFS_SCAN_S_SCAN_MODESW_INPROGRESS 4
#define DFS_SCAN_S_MAX 5


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
	uint8		ssid_len;	/**< the length of SSID */
	uint8		ssid[32];	/**< SSID string */
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

/* require default structure packing */
#define BWL_DEFAULT_PACKING
#include <packed_section_start.h>


/* Flags for OBSS IOVAR Parameters */
#define WL_OBSS_DYN_BWSW_FLAG_ACTIVITY_PERIOD        (0x01)
#define WL_OBSS_DYN_BWSW_FLAG_NOACTIVITY_PERIOD      (0x02)
#define WL_OBSS_DYN_BWSW_FLAG_NOACTIVITY_INCR_PERIOD (0x04)
#define WL_OBSS_DYN_BWSW_FLAG_PSEUDO_SENSE_PERIOD    (0x08)
#define WL_OBSS_DYN_BWSW_FLAG_RX_CRS_PERIOD          (0x10)
#define WL_OBSS_DYN_BWSW_FLAG_DUR_THRESHOLD          (0x20)
#define WL_OBSS_DYN_BWSW_FLAG_TXOP_PERIOD            (0x40)

/* OBSS IOVAR Version information */
#define WL_PROT_OBSS_CONFIG_PARAMS_VERSION 1
typedef BWL_PRE_PACKED_STRUCT struct {
	uint8 obss_bwsw_activity_cfm_count_cfg; /* configurable count in
		* seconds before we confirm that OBSS is present and
		* dynamically activate dynamic bwswitch.
		*/
	uint8 obss_bwsw_no_activity_cfm_count_cfg; /* configurable count in
		* seconds before we confirm that OBSS is GONE and
		* dynamically start pseudo upgrade. If in pseudo sense time, we
		* will see OBSS, [means that, we false detected that OBSS-is-gone
		* in watchdog] this count will be incremented in steps of
		* obss_bwsw_no_activity_cfm_count_incr_cfg for confirming OBSS
		* detection again. Note that, at present, max 30seconds is
		* allowed like this. [OBSS_BWSW_NO_ACTIVITY_MAX_INCR_DEFAULT]
		*/
	uint8 obss_bwsw_no_activity_cfm_count_incr_cfg; /* see above
		*/
	uint16 obss_bwsw_pseudo_sense_count_cfg; /* number of msecs/cnt to be in
		* pseudo state. This is used to sense/measure the stats from lq.
		*/
	uint8 obss_bwsw_rx_crs_threshold_cfg; /* RX CRS default threshold */
	uint8 obss_bwsw_dur_thres; /* OBSS dyn bwsw trigger/RX CRS Sec */
	uint8 obss_bwsw_txop_threshold_cfg; /* TXOP default threshold */
} BWL_POST_PACKED_STRUCT wlc_prot_dynbwsw_config_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 version;	/**< version field */
	uint32 config_mask;
	uint32 reset_mask;
	wlc_prot_dynbwsw_config_t config_params;
} BWL_POST_PACKED_STRUCT obss_config_params_t;


/* bsscfg type */
typedef enum bsscfg_type_t {
	BSSCFG_TYPE_GENERIC = 0,	/**< default */
	BSSCFG_TYPE_P2P = 1,	/**< The BSS is for p2p link */
	BSSCFG_TYPE_BTA = 2,
	BSSCFG_TYPE_TDLS = 4,
	BSSCFG_TYPE_AWDL = 5,
	BSSCFG_TYPE_PROXD = 6,
	BSSCFG_TYPE_NAN = 7,
	BSSCFG_TYPE_MAX
} bsscfg_type_t;

/* bsscfg subtype */
enum {
	BSSCFG_GENERIC_STA  = 1, /* GENERIC */
	BSSCFG_GENERIC_AP = 2, /* GENERIC */
	BSSCFG_P2P_GC   = 3, /* P2P */
	BSSCFG_P2P_GO   = 4, /* P2P */
	BSSCFG_P2P_DISC = 5, /* P2P */
};

typedef struct wlc_bsscfg_info {
	uint32 type;
	uint32 subtype;
} wlc_bsscfg_info_t;



/* Legacy structure to help keep backward compatible wl tool and tray app */

#define	LEGACY_WL_BSS_INFO_VERSION	107	/**< older version of wl_bss_info struct */

typedef struct wl_bss_info_107 {
	uint32		version;		/**< version field */
	uint32		length;			/**< byte length of data in this record,
						 * starting at version and including IEs
						 */
	struct ether_addr BSSID;
	uint16		beacon_period;		/**< units are Kusec */
	uint16		capability;		/**< Capability information */
	uint8		SSID_len;
	uint8		SSID[32];
	struct {
		uint	count;			/**< # rates in this set */
		uint8	rates[16];		/**< rates in 500kbps units w/hi bit set if basic */
	} rateset;				/**< supported rates */
	uint8		channel;		/**< Channel no. */
	uint16		atim_window;		/**< units are Kusec */
	uint8		dtim_period;		/**< DTIM period */
	int16		RSSI;			/**< receive signal strength (in dBm) */
	int8		phy_noise;		/**< noise (in dBm) */
	uint32		ie_length;		/**< byte length of Information Elements */
	/* variable length Information Elements */
} wl_bss_info_107_t;

/*
 * Per-BSS information structure.
 */

#define	LEGACY2_WL_BSS_INFO_VERSION	108		/**< old version of wl_bss_info struct */

/* BSS info structure
 * Applications MUST CHECK ie_offset field and length field to access IEs and
 * next bss_info structure in a vector (in wl_scan_results_t)
 */
typedef struct wl_bss_info_108 {
	uint32		version;		/**< version field */
	uint32		length;			/**< byte length of data in this record,
						 * starting at version and including IEs
						 */
	struct ether_addr BSSID;
	uint16		beacon_period;		/**< units are Kusec */
	uint16		capability;		/**< Capability information */
	uint8		SSID_len;
	uint8		SSID[32];
	struct {
		uint	count;			/**< # rates in this set */
		uint8	rates[16];		/**< rates in 500kbps units w/hi bit set if basic */
	} rateset;				/**< supported rates */
	chanspec_t	chanspec;		/**< chanspec for bss */
	uint16		atim_window;		/**< units are Kusec */
	uint8		dtim_period;		/**< DTIM period */
	int16		RSSI;			/**< receive signal strength (in dBm) */
	int8		phy_noise;		/**< noise (in dBm) */

	uint8		n_cap;			/**< BSS is 802.11N Capable */
	uint32		nbss_cap;		/**< 802.11N BSS Capabilities (based on HT_CAP_*) */
	uint8		ctl_ch;			/**< 802.11N BSS control channel number */
	uint32		reserved32[1];		/**< Reserved for expansion of BSS properties */
	uint8		flags;			/**< flags */
	uint8		reserved[3];		/**< Reserved for expansion of BSS properties */
	uint8		basic_mcs[MCSSET_LEN];	/**< 802.11N BSS required MCS set */

	uint16		ie_offset;		/**< offset at which IEs start, from beginning */
	uint32		ie_length;		/**< byte length of Information Elements */
	/* Add new fields here */
	/* variable length Information Elements */
} wl_bss_info_108_t;

#define	WL_BSS_INFO_VERSION	109		/**< current version of wl_bss_info struct */

/* BSS info structure
 * Applications MUST CHECK ie_offset field and length field to access IEs and
 * next bss_info structure in a vector (in wl_scan_results_t)
 */
typedef struct wl_bss_info {
	uint32		version;		/**< version field */
	uint32		length;			/**< byte length of data in this record,
						 * starting at version and including IEs
						 */
	struct ether_addr BSSID;
	uint16		beacon_period;		/**< units are Kusec */
	uint16		capability;		/**< Capability information */
	uint8		SSID_len;
	uint8		SSID[32];
	struct {
		uint	count;			/**< # rates in this set */
		uint8	rates[16];		/**< rates in 500kbps units w/hi bit set if basic */
	} rateset;				/**< supported rates */
	chanspec_t	chanspec;		/**< chanspec for bss */
	uint16		atim_window;		/**< units are Kusec */
	uint8		dtim_period;		/**< DTIM period */
	int16		RSSI;			/**< receive signal strength (in dBm) */
	int8		phy_noise;		/**< noise (in dBm) */

	uint8		n_cap;			/**< BSS is 802.11N Capable */
	uint32		nbss_cap;		/**< 802.11N+AC BSS Capabilities */
	uint8		ctl_ch;			/**< 802.11N BSS control channel number */
	uint8		padding1[3];		/**< explicit struct alignment padding */
	uint16		vht_rxmcsmap;	/**< VHT rx mcs map (802.11ac IE, VHT_CAP_MCS_MAP_*) */
	uint16		vht_txmcsmap;	/**< VHT tx mcs map (802.11ac IE, VHT_CAP_MCS_MAP_*) */
	uint8		flags;			/**< flags */
	uint8		vht_cap;		/**< BSS is vht capable */
	uint8		reserved[2];		/**< Reserved for expansion of BSS properties */
	uint8		basic_mcs[MCSSET_LEN];	/**< 802.11N BSS required MCS set */

	uint16		ie_offset;		/**< offset at which IEs start, from beginning */
	uint32		ie_length;		/**< byte length of Information Elements */
	int16		SNR;			/**< average SNR of during frame reception */
	uint16		vht_mcsmap;		/**< STA's Associated vhtmcsmap */
	uint16		vht_mcsmap_prop;	/**< STA's Associated prop vhtmcsmap */
	uint16		vht_txmcsmap_prop;	/**< prop VHT tx mcs prop */
	/* Add new fields here */
	/* variable length Information Elements */
} wl_bss_info_t;

#define	WL_GSCAN_BSS_INFO_VERSION	1	/* current version of wl_gscan_bss_info struct */
#define WL_GSCAN_INFO_FIXED_FIELD_SIZE   (sizeof(wl_gscan_bss_info_t) - sizeof(wl_bss_info_t))

typedef struct wl_gscan_bss_info {
	uint32      timestamp[2];
	wl_bss_info_t info;
	/* Do not add any more members below, fixed  */
	/* and variable length Information Elements to follow */
} wl_gscan_bss_info_t;


typedef struct wl_bsscfg {
	uint32  bsscfg_idx;
	uint32  wsec;
	uint32  WPA_auth;
	uint32  wsec_index;
	uint32  associated;
	uint32  BSS;
	uint32  phytest_on;
	struct ether_addr   prev_BSSID;
	struct ether_addr   BSSID;
	uint32  targetbss_wpa2_flags;
	uint32 assoc_type;
	uint32 assoc_state;
} wl_bsscfg_t;

typedef struct wl_if_add {
	uint32  bsscfg_flags;
	uint32  if_flags;
	uint32  ap;
	struct ether_addr   mac_addr;
	uint32  wlc_index;
} wl_if_add_t;

typedef struct wl_bss_config {
	uint32	atim_window;
	uint32	beacon_period;
	uint32	chanspec;
} wl_bss_config_t;

#define WL_BSS_USER_RADAR_CHAN_SELECT	0x1	/**< User application will randomly select
						 * radar channel.
						 */

#define DLOAD_HANDLER_VER			1	/**< Downloader version */
#define DLOAD_FLAG_VER_MASK		0xf000	/**< Downloader version mask */
#define DLOAD_FLAG_VER_SHIFT	12	/**< Downloader version shift */

#define DL_CRC_NOT_INUSE	0x0001
#define DL_BEGIN		0x0002
#define DL_END			0x0004

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

typedef struct wlc_ssid {
	uint32		SSID_len;
	uchar		SSID[DOT11_MAX_SSID_LEN];
} wlc_ssid_t;

typedef struct wlc_ssid_ext {
	bool       hidden;
	uint32		SSID_len;
	uchar		SSID[DOT11_MAX_SSID_LEN];
} wlc_ssid_ext_t;


#define MAX_PREFERRED_AP_NUM     5
typedef struct wlc_fastssidinfo {
	uint32				SSID_channel[MAX_PREFERRED_AP_NUM];
	wlc_ssid_t		SSID_info[MAX_PREFERRED_AP_NUM];
} wlc_fastssidinfo_t;

#ifdef CUSTOMER_HW_31_1

#define AP_NORM		0
#define AP_STEALTH  1
#define STREET_PASS_AP	2

#define NSC_MAX_TGT_SSID	20
typedef struct nsc_ssid_entry_list {
	wlc_ssid_t ssid_info;
	int ssid_type;
} nsc_ssid_entry_list_t;

typedef struct nsc_ssid_list {
	uint32 num_entries;				/* N wants 150 */
	nsc_ssid_entry_list_t ssid_entry[1];
} nsc_ssid_list_t;

#define NSC_TGT_SSID_BUFSZ	(sizeof(nsc_ssid_entry_list_t) * \
		(NSC_MAX_TGT_SSID - 1) + sizeof(nsc_ssid_list_t))

/* Default values from N */
#define NSC_SCPATT_ARRSZ	32

/* scan types */
#define UNI_SCAN	0
#define SP_SCAN_ACTIVE	1
#define SP_SCAN_PASSIVE	2
#define DOZE	3

/* what we found */
typedef struct nsc_scan_results {
	wlc_ssid_t ssid;
	struct ether_addr mac;
	int scantype;
	uint16 channel;
} nsc_scan_results_t;

typedef BWL_PRE_PACKED_STRUCT struct nsc_af_body {
	uint8			type;		/* should be 0x7f */
	uint8			oui[DOT11_OUI_LEN];	/* just like it says */
	uint8			subtype;
	uint8			ielen;		/* */
	uint8			data[1];	/* variable */
} BWL_POST_PACKED_STRUCT nsc_af_body_t;

typedef BWL_PRE_PACKED_STRUCT struct nsc_sdlist {
	uint8	scantype;
	uint16	duration;
	uint16	channel;		/* SP only */
	uint8	ssid_index;		/* SP only */
	uint16	rate;			/* SP only */
} BWL_POST_PACKED_STRUCT nsc_sdlist_t;

typedef struct nsc_scandes {
	uint32 	num_entries;	/* number of list entries */
	nsc_sdlist_t sdlist[1];	/* variable */
} nsc_scandes_t;

#define NSC_MAX_SDLIST_ENTRIES	8
#define NSC_SDDESC_BUFSZ	(sizeof(nsc_sdlist_t) * \
		(NSC_MAX_SDLIST_ENTRIES - 1) + sizeof(nsc_scandes_t))

#define SCAN_ARR_END	(NSC_MAX_SDLIST_ENTRIES)
#endif /* CUSTOMER_HW_31_1 */

typedef BWL_PRE_PACKED_STRUCT struct wnm_url {
	uint8   len;
	uint8   data[1];
} BWL_POST_PACKED_STRUCT wnm_url_t;

#define WNM_BSS_SELECT_TYPE_RSSI   0
#define WNM_BSS_SELECT_TYPE_CU   1

#define WNM_BSSLOAD_MONITOR_VERSION   1
typedef struct wnm_bssload_monitor_cfg {
	uint8 version;
	uint8 band;
	uint8 duration; /* duration between 1 to 20sec */
} wnm_bssload_monitor_cfg_t;

#define BSS_MAXTABLE_SIZE 10
#define WNM_BSS_SELECT_FACTOR_VERSION   1
typedef struct wnm_bss_select_factor_params {
	uint8 low;
	uint8 high;
	uint8 factor;
	uint8 pad;
} wnm_bss_select_factor_params_t;

typedef struct wnm_bss_select_factor_cfg {
	uint8 version;
	uint8 band;
	uint16 type;
	uint16 pad;
	uint16 count;
	wnm_bss_select_factor_params_t params[1];
} wnm_bss_select_factor_cfg_t;

#define WNM_BSS_SELECT_WEIGHT_VERSION   1
typedef struct wnm_bss_select_weight_cfg {
	uint8 version;
	uint8 band;
	uint16 type;
	uint16 weight; /* weightage for each type between 0 to 100 */
} wnm_bss_select_weight_cfg_t;

#define WNM_ROAM_TRIGGER_VERSION   1
typedef struct wnm_roam_trigger_cfg {
	uint8 version;
	uint8 band;
	uint16 type;
	int16 trigger; /* trigger for each type in new roam algorithm */
} wnm_roam_trigger_cfg_t;

typedef struct chan_scandata {
	uint8		txpower;
	uint8		pad;
	chanspec_t	channel;	/**< Channel num, bw, ctrl_sb and band */
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
	int8 		nprobes;		/**< 0, passive, otherwise active */
	int8    	split_scan;		/**< split scan */
	int8		band;			/**< band */
	int8		pad;
	wlc_ssid_t 	ssid[WLC_EXTDSCAN_MAX_SSID]; /* ssid list */
	uint32		tx_rate;		/**< in 500ksec units */
	wl_scan_type_t	scan_type;		/**< enum */
	int32 		channel_num;
	chan_scandata_t channel_list[1];	/**< list of chandata structs */
} wl_extdscan_params_t;

#define WL_EXTDSCAN_PARAMS_FIXED_SIZE 	(sizeof(wl_extdscan_params_t) - sizeof(chan_scandata_t))

#define WL_SCAN_PARAMS_SSID_MAX 	10

typedef struct wl_scan_params {
	wlc_ssid_t ssid;		/**< default: {0, ""} */
	struct ether_addr bssid;	/**< default: bcast */
	int8 bss_type;			/**< default: any,
					 * DOT11_BSSTYPE_ANY/INFRASTRUCTURE/INDEPENDENT
					 */
	uint8 scan_type;		/**< flags, 0 use default */
	int32 nprobes;			/**< -1 use default, number of probes per channel */
	int32 active_time;		/**< -1 use default, dwell time per channel for
					 * active scanning
					 */
	int32 passive_time;		/**< -1 use default, dwell time per channel
					 * for passive scanning
					 */
	int32 home_time;		/**< -1 use default, dwell time for the home channel
					 * between channel scans
					 */
	int32 channel_num;		/**< count of channels and ssids that follow
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
	uint16 channel_list[1];		/**< list of chanspecs */
} wl_scan_params_t;

/* size of wl_scan_params not including variable length array */
#define WL_SCAN_PARAMS_FIXED_SIZE 64
#define WL_MAX_ROAMSCAN_DATSZ	(WL_SCAN_PARAMS_FIXED_SIZE + (WL_NUMCHANNELS * sizeof(uint16)))

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


#define ESCAN_REQ_VERSION 1

/** event scan reduces amount of SOC memory needed to store scan results */
typedef struct wl_escan_params {
	uint32 version;
	uint16 action;
	uint16 sync_id;
	wl_scan_params_t params;
} wl_escan_params_t;

#define WL_ESCAN_PARAMS_FIXED_SIZE (OFFSETOF(wl_escan_params_t, params) + sizeof(wlc_ssid_t))

/** event scan reduces amount of SOC memory needed to store scan results */
typedef struct wl_escan_result {
	uint32 buflen;
	uint32 version;
	uint16 sync_id;
	uint16 bss_count;
	wl_bss_info_t bss_info[1];
} wl_escan_result_t;

#define WL_ESCAN_RESULTS_FIXED_SIZE (sizeof(wl_escan_result_t) - sizeof(wl_bss_info_t))

typedef struct wl_gscan_result {
	uint32 buflen;
	uint32 version;
	wl_gscan_bss_info_t bss_info[1];
} wl_gscan_result_t;

#define WL_GSCAN_RESULTS_FIXED_SIZE (sizeof(wl_gscan_result_t) - sizeof(wl_gscan_bss_info_t))

/* incremental scan results struct */
typedef struct wl_iscan_results {
	uint32 status;
	wl_scan_results_t results;
} wl_iscan_results_t;

/* size of wl_iscan_results not including variable length array */
#define WL_ISCAN_RESULTS_FIXED_SIZE \
	(WL_SCAN_RESULTS_FIXED_SIZE + OFFSETOF(wl_iscan_results_t, results))

#define SCANOL_PARAMS_VERSION	1

typedef struct scanol_params {
	uint32 version;
	uint32 flags;	/**< offload scanning flags */
	int32 active_time;	/**< -1 use default, dwell time per channel for active scanning */
	int32 passive_time;	/**< -1 use default, dwell time per channel for passive scanning */
	int32 idle_rest_time;	/**< -1 use default, time idle between scan cycle */
	int32 idle_rest_time_multiplier;
	int32 active_rest_time;
	int32 active_rest_time_multiplier;
	int32 scan_cycle_idle_rest_time;
	int32 scan_cycle_idle_rest_multiplier;
	int32 scan_cycle_active_rest_time;
	int32 scan_cycle_active_rest_multiplier;
	int32 max_rest_time;
	int32 max_scan_cycles;
	int32 nprobes;		/**< -1 use default, number of probes per channel */
	int32 scan_start_delay;
	uint32 nchannels;
	uint32 ssid_count;
	wlc_ssid_t ssidlist[1];
} scanol_params_t;

typedef struct wl_probe_params {
	wlc_ssid_t ssid;
	struct ether_addr bssid;
	struct ether_addr mac;
} wl_probe_params_t;

#define WL_MAXRATES_IN_SET		16	/**< max # of rates in a rateset */
typedef struct wl_rateset {
	uint32	count;			/**< # rates in this set */
	uint8	rates[WL_MAXRATES_IN_SET];	/**< rates in 500kbps units w/hi bit set if basic */
} wl_rateset_t;

typedef struct wl_rateset_args {
	uint32	count;			/**< # rates in this set */
	uint8	rates[WL_MAXRATES_IN_SET];	/**< rates in 500kbps units w/hi bit set if basic */
	uint8   mcs[MCSSET_LEN];        /* supported mcs index bit map */
	uint16 vht_mcs[VHT_CAP_MCS_MAP_NSS_MAX]; /* supported mcs index bit map per nss */
} wl_rateset_args_t;

#define TXBF_RATE_MCS_ALL		4
#define TXBF_RATE_VHT_ALL		4
#define TXBF_RATE_OFDM_ALL		8

typedef struct wl_txbf_rateset {
	uint8	txbf_rate_mcs[TXBF_RATE_MCS_ALL];	/**< one for each stream */
	uint8	txbf_rate_mcs_bcm[TXBF_RATE_MCS_ALL];	/**< one for each stream */
	uint16	txbf_rate_vht[TXBF_RATE_VHT_ALL];	/**< one for each stream */
	uint16	txbf_rate_vht_bcm[TXBF_RATE_VHT_ALL];	/**< one for each stream */
	uint8	txbf_rate_ofdm[TXBF_RATE_OFDM_ALL]; /**< bitmap of ofdm rates that enables txbf */
	uint8	txbf_rate_ofdm_bcm[TXBF_RATE_OFDM_ALL]; /* bitmap of ofdm rates that enables txbf */
	uint8	txbf_rate_ofdm_cnt;
	uint8	txbf_rate_ofdm_cnt_bcm;
} wl_txbf_rateset_t;

#define OFDM_RATE_MASK			0x0000007f
typedef uint8 ofdm_rates_t;

typedef struct wl_rates_info {
	wl_rateset_t rs_tgt;
	uint32 phy_type;
	int32 bandtype;
	uint8 cck_only;
	uint8 rate_mask;
	uint8 mcsallow;
	uint8 bw;
	uint8 txstreams;
} wl_rates_info_t;

/* uint32 list */
typedef struct wl_uint32_list {
	/* in - # of elements, out - # of entries */
	uint32 count;
	/* variable length uint32 list */
	uint32 element[1];
} wl_uint32_list_t;

/* used for association with a specific BSSID and chanspec list */
typedef struct wl_assoc_params {
	struct ether_addr bssid;	/**< 00:00:00:00:00:00: broadcast scan */
	uint16 bssid_cnt;		/**< 0: use chanspec_num, and the single bssid,
					* otherwise count of chanspecs in chanspec_list
					* AND paired bssids following chanspec_list
					* also, chanspec_num has to be set to zero
					* for bssid list to be used
					*/
	int32 chanspec_num;		/**< 0: all available channels,
					* otherwise count of chanspecs in chanspec_list
					*/
	chanspec_t chanspec_list[1];	/**< list of chanspecs */
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
	wl_assoc_params_t params;	/**< optional field, but it must include the fixed portion
					 * of the wl_assoc_params_t struct when it does present.
					 */
} wl_join_params_t;

#define WL_JOIN_PARAMS_FIXED_SIZE 	(OFFSETOF(wl_join_params_t, params) + \
					 WL_ASSOC_PARAMS_FIXED_SIZE)
/* scan params for extended join */
typedef struct wl_join_scan_params {
	uint8 scan_type;		/**< 0 use default, active or passive scan */
	int32 nprobes;			/**< -1 use default, number of probes per channel */
	int32 active_time;		/**< -1 use default, dwell time per channel for
					 * active scanning
					 */
	int32 passive_time;		/**< -1 use default, dwell time per channel
					 * for passive scanning
					 */
	int32 home_time;		/**< -1 use default, dwell time for the home channel
					 * between channel scans
					 */
} wl_join_scan_params_t;

/* extended join params */
typedef struct wl_extjoin_params {
	wlc_ssid_t ssid;		/**< {0, ""}: wildcard scan */
	wl_join_scan_params_t scan;
	wl_join_assoc_params_t assoc;	/**< optional field, but it must include the fixed portion
					 * of the wl_join_assoc_params_t struct when it does
					 * present.
					 */
} wl_extjoin_params_t;
#define WL_EXTJOIN_PARAMS_FIXED_SIZE 	(OFFSETOF(wl_extjoin_params_t, assoc) + \
					 WL_JOIN_ASSOC_PARAMS_FIXED_SIZE)

#define ANT_SELCFG_MAX		4	/**< max number of antenna configurations */
#define MAX_STREAMS_SUPPORTED	4	/**< max number of streams supported */
typedef struct {
	uint8 ant_config[ANT_SELCFG_MAX];	/**< antenna configuration */
	uint8 num_antcfg;	/**< number of available antenna configurations */
} wlc_antselcfg_t;

typedef struct {
	uint32 duration;	/**< millisecs spent sampling this channel */
	uint32 congest_ibss;	/**< millisecs in our bss (presumably this traffic will */
				/**<  move if cur bss moves channels) */
	uint32 congest_obss;	/**< traffic not in our bss */
	uint32 interference;	/**< millisecs detecting a non 802.11 interferer. */
	uint32 timestamp;	/**< second timestamp */
} cca_congest_t;

typedef struct {
	chanspec_t chanspec;	/**< Which channel? */
	uint16 num_secs;	/**< How many secs worth of data */
	cca_congest_t  secs[1];	/**< Data */
} cca_congest_channel_req_t;

typedef struct {
	uint32 duration;	/**< millisecs spent sampling this channel */
	uint32 congest;		/**< millisecs detecting busy CCA */
	uint32 timestamp;	/**< second timestamp */
} cca_congest_simple_t;

typedef struct {
	uint16 status;
	uint16 id;
	chanspec_t chanspec;	/**< Which channel? */
	uint16 len;
	union {
		cca_congest_simple_t  cca_busy;	/**< CCA busy */
		int noise;			/**< noise floor */
	};
} cca_chan_qual_event_t;


/* interference sources */
enum interference_source {
	ITFR_NONE = 0,		/**< interference */
	ITFR_PHONE,		/**< wireless phone */
	ITFR_VIDEO_CAMERA,	/**< wireless video camera */
	ITFR_MICROWAVE_OVEN,	/**< microwave oven */
	ITFR_BABY_MONITOR,	/**< wireless baby monitor */
	ITFR_BLUETOOTH,		/**< bluetooth */
	ITFR_VIDEO_CAMERA_OR_BABY_MONITOR,	/**< wireless camera or baby monitor */
	ITFR_BLUETOOTH_OR_BABY_MONITOR,	/**< bluetooth or baby monitor */
	ITFR_VIDEO_CAMERA_OR_PHONE,	/**< video camera or phone */
	ITFR_UNIDENTIFIED	/**< interference from unidentified source */
};

/* structure for interference source report */
typedef struct {
	uint32 flags;	/**< flags.  bit definitions below */
	uint32 source;	/**< last detected interference source */
	uint32 timestamp;	/**< second timestamp on interferenced flag change */
} interference_source_rep_t;

#define WLC_CNTRY_BUF_SZ	4		/**< Country string is 3 bytes + NUL */


typedef struct wl_country {
	char country_abbrev[WLC_CNTRY_BUF_SZ];	/**< nul-terminated country code used in
						 * the Country IE
						 */
	int32 rev;				/**< revision specifier for ccode
						 * on set, -1 indicates unspecified.
						 * on get, rev >= 0
						 */
	char ccode[WLC_CNTRY_BUF_SZ];		/**< nul-terminated built-in country code.
						 * variable length, but fixed size in
						 * struct allows simple allocation for
						 * expected country strings <= 3 chars.
						 */
} wl_country_t;

#define CCODE_INFO_VERSION 1

typedef enum wl_ccode_role {
	WLC_CCODE_ROLE_ACTIVE = 0,
	WLC_CCODE_ROLE_HOST,
	WLC_CCODE_ROLE_80211D_ASSOC,
	WLC_CCODE_ROLE_80211D_SCAN,
	WLC_CCODE_ROLE_DEFAULT,
	WLC_CCODE_LAST
} wl_ccode_role_t;
#define WLC_NUM_CCODE_INFO WLC_CCODE_LAST

typedef struct wl_ccode_entry {
	uint16 reserved;
	uint8 band;
	uint8 role;
	char	ccode[WLC_CNTRY_BUF_SZ];
} wl_ccode_entry_t;

typedef struct wl_ccode_info {
	uint16 version;
	uint16 count;   /* Number of ccodes entries in the set */
	wl_ccode_entry_t ccodelist[1];
} wl_ccode_info_t;
#define WL_CCODE_INFO_FIXED_LEN	OFFSETOF(wl_ccode_info_t, ccodelist)

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

typedef struct wl_rm_req_elt {
	int8	type;
	int8	flags;
	chanspec_t	chanspec;
	uint32	token;		/**< token for this measurement */
	uint32	tsf_h;		/**< TSF high 32-bits of Measurement start time */
	uint32	tsf_l;		/**< TSF low 32-bits */
	uint32	dur;		/**< TUs */
} wl_rm_req_elt_t;

typedef struct wl_rm_req {
	uint32	token;		/**< overall measurement set token */
	uint32	count;		/**< number of measurement requests */
	void	*cb;		/**< completion callback function: may be NULL */
	void	*cb_arg;	/**< arg to completion callback function */
	wl_rm_req_elt_t	req[1];	/**< variable length block of requests */
} wl_rm_req_t;
#define WL_RM_REQ_FIXED_LEN	OFFSETOF(wl_rm_req_t, req)

typedef struct wl_rm_rep_elt {
	int8	type;
	int8	flags;
	chanspec_t	chanspec;
	uint32	token;		/**< token for this measurement */
	uint32	tsf_h;		/**< TSF high 32-bits of Measurement start time */
	uint32	tsf_l;		/**< TSF low 32-bits */
	uint32	dur;		/**< TUs */
	uint32	len;		/**< byte length of data block */
	uint8	data[1];	/**< variable length data block */
} wl_rm_rep_elt_t;
#define WL_RM_REP_ELT_FIXED_LEN	24	/**< length excluding data block */

#define WL_RPI_REP_BIN_NUM 8
typedef struct wl_rm_rpi_rep {
	uint8	rpi[WL_RPI_REP_BIN_NUM];
	int8	rpi_max[WL_RPI_REP_BIN_NUM];
} wl_rm_rpi_rep_t;

typedef struct wl_rm_rep {
	uint32	token;		/**< overall measurement set token */
	uint32	len;		/**< length of measurement report block */
	wl_rm_rep_elt_t	rep[1];	/**< variable length block of reports */
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
	WLC_SUP_KEYXCHANGE_PREP_M4,	/**< Preparing to send handshake msg M4 */
	WLC_SUP_KEYXCHANGE_WAIT_G1,	/**< Waiting to receive handshake msg G1 */
	WLC_SUP_KEYXCHANGE_PREP_G2	/**< Preparing to send handshake msg G2 */
} sup_auth_status_t;

typedef struct wl_wsec_key {
	uint32		index;		/**< key index */
	uint32		len;		/**< key length */
	uint8		data[DOT11_MAX_KEY_SIZE];	/**< key data */
	uint32		pad_1[18];
	uint32		algo;		/**< CRYPTO_ALGO_AES_CCM, CRYPTO_ALGO_WEP128, etc */
	uint32		flags;		/**< misc flags */
	uint32		pad_2[2];
	int		pad_3;
	int		iv_initialized;	/**< has IV been initialized already? */
	int		pad_4;
	/* Rx IV */
	struct {
		uint32	hi;		/**< upper 32 bits of IV */
		uint16	lo;		/**< lower 16 bits of IV */
	} rxiv;
	uint32		pad_5[2];
	struct ether_addr ea;		/**< per station */
} wl_wsec_key_t;

#define WSEC_MIN_PSK_LEN	8
#define WSEC_MAX_PSK_LEN	64

/* Flag for key material needing passhash'ing */
#define WSEC_PASSPHRASE		(1<<0)

/* receptacle for WLC_SET_WSEC_PMK parameter */
typedef struct {
	ushort	key_len;		/**< octets in key material */
	ushort	flags;			/**< key handling qualification */
	uint8	key[WSEC_MAX_PSK_LEN];	/**< PMK material */
} wsec_pmk_t;

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

#define WL_STA_ANT_MAX		4	/**< max possible rx antennas */

typedef struct wl_assoc_info {
	uint32		req_len;
	uint32		resp_len;
	uint32		flags;
	struct dot11_assoc_req req;
	struct ether_addr reassoc_bssid; /* used in reassoc's */
	struct dot11_assoc_resp resp;
} wl_assoc_info_t;

typedef struct wl_led_info {
	uint32      index;      /* led index */
	uint32      behavior;
	uint8       activehi;
} wl_led_info_t;


/* srom read/write struct passed through ioctl */
typedef struct {
	uint	byteoff;	/**< byte offset */
	uint	nbytes;		/**< number of bytes */
	uint16	buf[1];
} srom_rw_t;

#define CISH_FLAG_PCIECIS	(1 << 15)	/* write CIS format bit for PCIe CIS */
/* similar cis (srom or otp) struct [iovar: may not be aligned] */
typedef struct {
	uint16	source;		/**< cis source */
	uint16	flags;		/**< flags */
	uint32	byteoff;	/**< byte offset */
	uint32	nbytes;		/**< number of bytes */
	/* data follows here */
} cis_rw_t;

/* R_REG and W_REG struct passed through ioctl */
typedef struct {
	uint32	byteoff;	/**< byte offset of the field in d11regs_t */
	uint32	val;		/**< read/write value of the field */
	uint32	size;		/**< sizeof the field */
	uint	band;		/**< band (optional) */
} rw_reg_t;

/* Structure used by GET/SET_ATTEN ioctls - it controls power in b/g-band */
/* PCL - Power Control Loop */
typedef struct {
	uint16	auto_ctrl;	/**< WL_ATTEN_XX */
	uint16	bb;		/**< Baseband attenuation */
	uint16	radio;		/**< Radio attenuation */
	uint16	txctl1;		/**< Radio TX_CTL1 value */
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

/* Used to get specific link/ac parameters */
typedef struct {
	int32 ac;
	uint8 val;
	struct ether_addr ea;
} link_val_t;


#define WL_PM_MUTE_TX_VER 1

typedef struct wl_pm_mute_tx {
	uint16 version;		/**< version */
	uint16 len;		/**< length */
	uint16 deadline;	/**< deadline timer (in milliseconds) */
	uint8  enable;		/**< set to 1 to enable mode; set to 0 to disable it */
} wl_pm_mute_tx_t;


typedef struct {
	uint16			ver;		/**< version of this struct */
	uint16			len;		/**< length in bytes of this structure */
	uint16			cap;		/**< sta's advertised capabilities */
	uint32			flags;		/**< flags defined below */
	uint32			idle;		/**< time since data pkt rx'd from sta */
	struct ether_addr	ea;		/**< Station address */
	wl_rateset_t		rateset;	/**< rateset in use */
	uint32			in;		/**< seconds elapsed since associated */
	uint32			listen_interval_inms; /* Min Listen interval in ms for this STA */
	uint32			tx_pkts;	/**< # of user packets transmitted (unicast) */
	uint32			tx_failures;	/**< # of user packets failed */
	uint32			rx_ucast_pkts;	/**< # of unicast packets received */
	uint32			rx_mcast_pkts;	/**< # of multicast packets received */
	uint32			tx_rate;	/**< Rate used by last tx frame */
	uint32			rx_rate;	/**< Rate of last successful rx frame */
	uint32			rx_decrypt_succeeds;	/**< # of packet decrypted successfully */
	uint32			rx_decrypt_failures;	/**< # of packet decrypted unsuccessfully */
	uint32			tx_tot_pkts;	/**< # of user tx pkts (ucast + mcast) */
	uint32			rx_tot_pkts;	/**< # of data packets recvd (uni + mcast) */
	uint32			tx_mcast_pkts;	/**< # of mcast pkts txed */
	uint64			tx_tot_bytes;	/**< data bytes txed (ucast + mcast) */
	uint64			rx_tot_bytes;	/**< data bytes recvd (ucast + mcast) */
	uint64			tx_ucast_bytes;	/**< data bytes txed (ucast) */
	uint64			tx_mcast_bytes;	/**< # data bytes txed (mcast) */
	uint64			rx_ucast_bytes;	/**< data bytes recvd (ucast) */
	uint64			rx_mcast_bytes;	/**< data bytes recvd (mcast) */
	int8			rssi[WL_STA_ANT_MAX]; /* average rssi per antenna
										   * of data frames
										   */
	int8			nf[WL_STA_ANT_MAX];	/**< per antenna noise floor */
	uint16			aid;		/**< association ID */
	uint16			ht_capabilities;	/**< advertised ht caps */
	uint16			vht_flags;		/**< converted vht flags */
	uint32			tx_pkts_retried;	/**< # of frames where a retry was
							 * necessary
							 */
	uint32			tx_pkts_retry_exhausted; /* # of user frames where a retry
							  * was exhausted
							  */
	int8			rx_lastpkt_rssi[WL_STA_ANT_MAX]; /* Per antenna RSSI of last
								  * received data frame.
								  */
	/* TX WLAN retry/failure statistics:
	 * Separated for host requested frames and WLAN locally generated frames.
	 * Include unicast frame only where the retries/failures can be counted.
	 */
	uint32			tx_pkts_total;		/**< # user frames sent successfully */
	uint32			tx_pkts_retries;	/**< # user frames retries */
	uint32			tx_pkts_fw_total;	/**< # FW generated sent successfully */
	uint32			tx_pkts_fw_retries;	/**< # retries for FW generated frames */
	uint32			tx_pkts_fw_retry_exhausted;	/**< # FW generated where a retry
								 * was exhausted
								 */
	uint32			rx_pkts_retried;	/**< # rx with retry bit set */
	uint32			tx_rate_fallback;	/**< lowest fallback TX rate */
} sta_info_t;

#define WL_OLD_STAINFO_SIZE	OFFSETOF(sta_info_t, tx_tot_pkts)

#define WL_STA_VER		4

typedef struct {
	uint32 auto_en;
	uint32 active_ant;
	uint32 rxcount;
	int32 avg_snr_per_ant0;
	int32 avg_snr_per_ant1;
	int32 avg_snr_per_ant2;
	uint32 swap_ge_rxcount0;
	uint32 swap_ge_rxcount1;
	uint32 swap_ge_snrthresh0;
	uint32 swap_ge_snrthresh1;
	uint32 swap_txfail0;
	uint32 swap_txfail1;
	uint32 swap_timer0;
	uint32 swap_timer1;
	uint32 swap_alivecheck0;
	uint32 swap_alivecheck1;
	uint32 rxcount_per_ant0;
	uint32 rxcount_per_ant1;
	uint32 acc_rxcount;
	uint32 acc_rxcount_per_ant0;
	uint32 acc_rxcount_per_ant1;
	uint32 tx_auto_en;
	uint32 tx_active_ant;
	uint32 rx_policy;
	uint32 tx_policy;
	uint32 cell_policy;
} wlc_swdiv_stats_t;

#define	WLC_NUMRATES	16	/**< max # of rates in a rateset */

typedef struct wlc_rateset {
	uint32	count;			/**< number of rates in rates[] */
	uint8	rates[WLC_NUMRATES];	/**< rates in 500kbps units w/hi bit set if basic */
	uint8	htphy_membership;	/**< HT PHY Membership */
	uint8	mcs[MCSSET_LEN];	/**< supported mcs index bit map */
	uint16  vht_mcsmap;		/**< supported vht mcs nss bit map */
	uint16  vht_mcsmap_prop;	/**< supported prop vht mcs nss bit map */
} wlc_rateset_t;

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
typedef struct maclist {
	uint count;			/**< number of MAC addresses */
	struct ether_addr ea[1];	/**< variable length array of MAC addresses */
} maclist_t;

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
	struct	ether_addr ea;	/**< MAC Address */
	uint8	ac_cat;	/**< Access Category */
	uint8	num_pkts;	/**< Number of packet entries to be averaged */
} wl_mac_ratehisto_cmd_t;	/**< MAC Specific Rate Histogram command */

/* Get MAC rate histogram response */
typedef struct {
	uint32	rate[DOT11_RATE_MAX + 1];	/**< Rates */
	uint32	mcs[WL_RATESET_SZ_HT_IOCTL * WL_TX_CHAINS_MAX];	/**< MCS counts */
	uint32	vht[WL_RATESET_SZ_VHT_MCS][WL_TX_CHAINS_MAX];	/**< VHT counts */
	uint32	tsf_timer[2][2];	/**< Start and End time for 8bytes value */
	uint32	prop11n_mcs[WLC_11N_LAST_PROP_MCS - WLC_11N_FIRST_PROP_MCS + 1]; /* MCS counts */
} wl_mac_ratehisto_res_t;	/**< MAC Specific Rate Histogram Response */

/* Linux network driver ioctl encoding */
typedef struct wl_ioctl {
	uint cmd;	/**< common ioctl definition */
	void *buf;	/**< pointer to user buffer */
	uint len;	/**< length of user buffer */
	uint8 set;		/**< 1=set IOCTL; 0=query IOCTL */
	uint used;	/**< bytes read or written (optional) */
	uint needed;	/**< bytes needed (optional) */
} wl_ioctl_t;

#ifdef CONFIG_COMPAT
typedef struct compat_wl_ioctl {
	uint cmd;	/**< common ioctl definition */
	uint32 buf;	/**< pointer to user buffer */
	uint len;	/**< length of user buffer */
	uint8 set;		/**< 1=set IOCTL; 0=query IOCTL */
	uint used;	/**< bytes read or written (optional) */
	uint needed;	/**< bytes needed (optional) */
} compat_wl_ioctl_t;
#endif /* CONFIG_COMPAT */

#define WL_NUM_RATES_CCK			4 /* 1, 2, 5.5, 11 Mbps */
#define WL_NUM_RATES_OFDM			8 /* 6, 9, 12, 18, 24, 36, 48, 54 Mbps SISO/CDD */
#define WL_NUM_RATES_MCS_1STREAM	8 /* MCS 0-7 1-stream rates - SISO/CDD/STBC/MCS */
#define WL_NUM_RATES_EXTRA_VHT		2 /* Additional VHT 11AC rates */
#define WL_NUM_RATES_VHT			10
#define WL_NUM_RATES_MCS32			1


/*
 * Structure for passing hardware and software
 * revision info up from the driver.
 */
typedef struct wlc_rev_info {
	uint		vendorid;	/**< PCI vendor id */
	uint		deviceid;	/**< device id of chip */
	uint		radiorev;	/**< radio revision */
	uint		chiprev;	/**< chip revision */
	uint		corerev;	/**< core revision */
	uint		boardid;	/**< board identifier (usu. PCI sub-device id) */
	uint		boardvendor;	/**< board vendor (usu. PCI sub-vendor id) */
	uint		boardrev;	/**< board revision */
	uint		driverrev;	/**< driver version */
	uint		ucoderev;	/**< microcode version */
	uint		bus;		/**< bus type */
	uint		chipnum;	/**< chip number */
	uint		phytype;	/**< phy type */
	uint		phyrev;		/**< phy revision */
	uint		anarev;		/**< anacore rev */
	uint		chippkg;	/**< chip package info */
	uint		nvramrev;	/**< nvram revision number */
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

/* Transfer info about an IOVar from the driver */
/* Max supported IOV name size in bytes, + 1 for nul termination */
#define WLC_IOV_NAME_LEN 30
typedef struct wlc_iov_trx_s {
	uint8 module;
	uint8 type;
	char name[WLC_IOV_NAME_LEN];
} wlc_iov_trx_t;

/* bump this number if you change the ioctl interface */
#define WLC_IOCTL_VERSION	2
#define WLC_IOCTL_VERSION_LEGACY_IOTYPES	1

#ifdef CONFIG_USBRNDIS_RETAIL
/* struct passed in for WLC_NDCONFIG_ITEM */
typedef struct {
	char *name;
	void *param;
} ndconfig_item_t;
#endif



#define WL_PHY_PAVARS_LEN	32	/**< Phytype, Bandrange, chain, a[0], b[0], c[0], d[0] .. */


#define WL_PHY_PAVAR_VER	1	/**< pavars version */
#define WL_PHY_PAVARS2_NUM	3	/**< a1, b0, b1 */
typedef struct wl_pavars2 {
	uint16 ver;		/**< version of this struct */
	uint16 len;		/**< len of this structure */
	uint16 inuse;		/**< driver return 1 for a1,b0,b1 in current band range */
	uint16 phy_type;	/**< phy type */
	uint16 bandrange;
	uint16 chain;
	uint16 inpa[WL_PHY_PAVARS2_NUM];	/**< phy pavars for one band range */
} wl_pavars2_t;

typedef struct wl_po {
	uint16	phy_type;	/**< Phy type */
	uint16	band;
	uint16	cckpo;
	uint32	ofdmpo;
	uint16	mcspo[8];
} wl_po_t;

#define WL_NUM_RPCALVARS 5	/**< number of rpcal vars */

typedef struct wl_rpcal {
	uint16 value;
	uint16 update;
} wl_rpcal_t;

typedef struct wl_aci_args {
	int enter_aci_thresh; /* Trigger level to start detecting ACI */
	int exit_aci_thresh; /* Trigger level to exit ACI mode */
	int usec_spin; /* microsecs to delay between rssi samples */
	int glitch_delay; /* interval between ACI scans when glitch count is consistently high */
	uint16 nphy_adcpwr_enter_thresh;	/**< ADC power to enter ACI mitigation mode */
	uint16 nphy_adcpwr_exit_thresh;	/**< ADC power to exit ACI mitigation mode */
	uint16 nphy_repeat_ctr;		/**< Number of tries per channel to compute power */
	uint16 nphy_num_samples;	/**< Number of samples to compute power on one channel */
	uint16 nphy_undetect_window_sz;	/**< num of undetects to exit ACI Mitigation mode */
	uint16 nphy_b_energy_lo_aci;	/**< low ACI power energy threshold for bphy */
	uint16 nphy_b_energy_md_aci;	/**< mid ACI power energy threshold for bphy */
	uint16 nphy_b_energy_hi_aci;	/**< high ACI power energy threshold for bphy */
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

#define WL_ACI_ARGS_LEGACY_LENGTH	16	/**< bytes of pre NPHY aci args */
#define	WL_SAMPLECOLLECT_T_VERSION	2	/**< version of wl_samplecollect_args_t struct */
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
	uint8 downsamp;
	uint8 be_deaf;
	uint8 agc;		/**< loop from init gain and going down */
	uint8 filter;		/**< override high pass corners to lowest */
	/* add'l version 2 fields */
	uint8 trigger_state;
	uint8 module_sel1;
	uint8 module_sel2;
	uint16 nsamps;
	int bitStart;
	uint32 gpioCapMask;
} wl_samplecollect_args_t;

#define	WL_SAMPLEDATA_T_VERSION		1	/**< version of wl_samplecollect_args_t struct */
/* version for unpacked sample data, int16 {(I,Q),Core(0..N)} */
#define	WL_SAMPLEDATA_T_VERSION_SPEC_AN 2

typedef struct wl_sampledata {
	uint16 version;	/**< structure version */
	uint16 size;	/**< size of structure */
	uint16 tag;	/**< Header/Data */
	uint16 length;	/**< data length */
	uint32 flag;	/**< bit def */
} wl_sampledata_t;


/* WL_OTA START */
/* OTA Test Status */
enum {
	WL_OTA_TEST_IDLE = 0,	/**< Default Idle state */
	WL_OTA_TEST_ACTIVE = 1,	/**< Test Running */
	WL_OTA_TEST_SUCCESS = 2,	/**< Successfully Finished Test */
	WL_OTA_TEST_FAIL = 3	/**< Test Failed in the Middle */
};
/* OTA SYNC Status */
enum {
	WL_OTA_SYNC_IDLE = 0,	/**< Idle state */
	WL_OTA_SYNC_ACTIVE = 1,	/**< Waiting for Sync */
	WL_OTA_SYNC_FAIL = 2	/**< Sync pkt not recieved */
};

/* Various error states dut can get stuck during test */
enum {
	WL_OTA_SKIP_TEST_CAL_FAIL = 1,		/**< Phy calibration failed */
	WL_OTA_SKIP_TEST_SYNCH_FAIL = 2,		/**< Sync Packet not recieved */
	WL_OTA_SKIP_TEST_FILE_DWNLD_FAIL = 3,	/**< Cmd flow file download failed */
	WL_OTA_SKIP_TEST_NO_TEST_FOUND = 4,	/**< No test found in Flow file */
	WL_OTA_SKIP_TEST_WL_NOT_UP = 5,		/**< WL UP failed */
	WL_OTA_SKIP_TEST_UNKNOWN_CALL		/**< Unintentional scheduling on ota test */
};

/* Differentiator for ota_tx and ota_rx */
enum {
	WL_OTA_TEST_TX = 0,		/**< ota_tx */
	WL_OTA_TEST_RX = 1,		/**< ota_rx */
};

/* Catch 3 modes of operation: 20Mhz, 40Mhz, 20 in 40 Mhz */
enum {
	WL_OTA_TEST_BW_20_IN_40MHZ = 0,		/**< 20 in 40 operation */
	WL_OTA_TEST_BW_20MHZ = 1,		/**< 20 Mhz operation */
	WL_OTA_TEST_BW_40MHZ = 2,		/**< full 40Mhz operation */
	WL_OTA_TEST_BW_80MHZ = 3		/* full 80Mhz operation */
};

#define HT_MCS_INUSE	0x00000080	/* HT MCS in use,indicates b0-6 holds an mcs */
#define VHT_MCS_INUSE	0x00000100	/* VHT MCS in use,indicates b0-6 holds an mcs */
#define OTA_RATE_MASK 0x0000007f	/* rate/mcs value */
#define OTA_STF_SISO	0
#define OTA_STF_CDD		1
#define OTA_STF_STBC	2
#define OTA_STF_SDM		3

typedef struct ota_rate_info {
	uint8 rate_cnt;					/**< Total number of rates */
	uint16 rate_val_mbps[WL_OTA_TEST_MAX_NUM_RATE];	/**< array of rates from 1mbps to 130mbps */
							/**< for legacy rates : ratein mbps * 2 */
							/**< for HT rates : mcs index */
} ota_rate_info_t;

typedef struct ota_power_info {
	int8 pwr_ctrl_on;	/**< power control on/off */
	int8 start_pwr;		/**< starting power/index */
	int8 delta_pwr;		/**< delta power/index */
	int8 end_pwr;		/**< end power/index */
} ota_power_info_t;

typedef struct ota_packetengine {
	uint16 delay;           /* Inter-packet delay */
				/**< for ota_tx, delay is tx ifs in micro seconds */
				/* for ota_rx, delay is wait time in milliseconds */
	uint16 nframes;         /* Number of frames */
	uint16 length;          /* Packet length */
} ota_packetengine_t;

/* Test info vector */
typedef struct wl_ota_test_args {
	uint8 cur_test;			/**< test phase */
	uint8 chan;			/**< channel */
	uint8 bw;			/**< bandwidth */
	uint8 control_band;		/**< control band */
	uint8 stf_mode;			/**< stf mode */
	ota_rate_info_t rt_info;	/**< Rate info */
	ota_packetengine_t pkteng;	/**< packeteng info */
	uint8 txant;			/**< tx antenna */
	uint8 rxant;			/**< rx antenna */
	ota_power_info_t pwr_info;	/**< power sweep info */
	uint8 wait_for_sync;		/**< wait for sync or not */
	uint8 ldpc;
	uint8 sgi;
	/* Update WL_OTA_TESTVEC_T_VERSION for adding new members to this structure */
} wl_ota_test_args_t;

#define WL_OTA_TESTVEC_T_VERSION		1	/* version of wl_ota_test_vector_t struct */
typedef struct wl_ota_test_vector {
	uint16 version;
	wl_ota_test_args_t test_arg[WL_OTA_TEST_MAX_NUM_SEQ];	/**< Test argument struct */
	uint16 test_cnt;					/**< Total no of test */
	uint8 file_dwnld_valid;					/**< File successfully downloaded */
	uint8 sync_timeout;					/**< sync packet timeout */
	int8 sync_fail_action;					/**< sync fail action */
	struct ether_addr sync_mac;				/**< macaddress for sync pkt */
	struct ether_addr tx_mac;				/**< macaddress for tx */
	struct ether_addr rx_mac;				/**< macaddress for rx */
	int8 loop_test;					/**< dbg feature to loop the test */
	uint16 test_rxcnt;
	/* Update WL_OTA_TESTVEC_T_VERSION for adding new members to this structure */
} wl_ota_test_vector_t;


/* struct copied back form dongle to host to query the status */
typedef struct wl_ota_test_status {
	int16 cur_test_cnt;		/**< test phase */
	int8 skip_test_reason;		/**< skip test reasoin */
	wl_ota_test_args_t test_arg;	/**< cur test arg details */
	uint16 test_cnt;		/**< total no of test downloaded */
	uint8 file_dwnld_valid;		/**< file successfully downloaded ? */
	uint8 sync_timeout;		/**< sync timeout */
	int8 sync_fail_action;		/**< sync fail action */
	struct ether_addr sync_mac;	/**< macaddress for sync pkt */
	struct ether_addr tx_mac;	/**< tx mac address */
	struct ether_addr rx_mac;	/**< rx mac address */
	uint8  test_stage;		/**< check the test status */
	int8 loop_test;		/**< Debug feature to puts test enfine in a loop */
	uint8 sync_status;		/**< sync status */
} wl_ota_test_status_t;
typedef struct wl_ota_rx_rssi {
	uint16	pktcnt;		/* Pkt count used for this rx test */
	chanspec_t chanspec;	/* Channel info on which the packets are received */
	int16	rssi;		/* Average RSSI of the first 50% packets received */
} wl_ota_rx_rssi_t;

#define	WL_OTARSSI_T_VERSION		1	/* version of wl_ota_test_rssi_t struct */
#define WL_OTA_TEST_RSSI_FIXED_SIZE	OFFSETOF(wl_ota_test_rssi_t, rx_rssi)

typedef struct wl_ota_test_rssi {
	uint8 version;
	uint8	testcnt;		/* total measured RSSI values, valid on output only */
	wl_ota_rx_rssi_t rx_rssi[1]; /* Variable length array of wl_ota_rx_rssi_t */
} wl_ota_test_rssi_t;

/* WL_OTA END */

/* wl_radar_args_t */
typedef struct {
	int npulses;	/**< required number of pulses at n * t_int */
	int ncontig;	/**< required number of pulses at t_int */
	int min_pw;	/**< minimum pulse width (20 MHz clocks) */
	int max_pw;	/**< maximum pulse width (20 MHz clocks) */
	uint16 thresh0;	/**< Radar detection, thresh 0 */
	uint16 thresh1;	/**< Radar detection, thresh 1 */
	uint16 blank;	/**< Radar detection, blank control */
	uint16 fmdemodcfg;	/**< Radar detection, fmdemod config */
	int npulses_lp;  /* Radar detection, minimum long pulses */
	int min_pw_lp; /* Minimum pulsewidth for long pulses */
	int max_pw_lp; /* Maximum pulsewidth for long pulses */
	int min_fm_lp; /* Minimum fm for long pulses */
	int max_span_lp;  /* Maximum deltat for long pulses */
	int min_deltat; /* Minimum spacing between pulses */
	int max_deltat; /* Maximum spacing between pulses */
	uint16 autocorr;	/**< Radar detection, autocorr on or off */
	uint16 st_level_time;	/**< Radar detection, start_timing level */
	uint16 t2_min; /* minimum clocks needed to remain in state 2 */
	uint32 version; /* version */
	uint32 fra_pulse_err;	/**< sample error margin for detecting French radar pulsed */
	int npulses_fra;  /* Radar detection, minimum French pulses set */
	int npulses_stg2;  /* Radar detection, minimum staggered-2 pulses set */
	int npulses_stg3;  /* Radar detection, minimum staggered-3 pulses set */
	uint16 percal_mask;	/**< defines which period cal is masked from radar detection */
	int quant;	/**< quantization resolution to pulse positions */
	uint32 min_burst_intv_lp;	/**< minimum burst to burst interval for bin3 radar */
	uint32 max_burst_intv_lp;	/**< maximum burst to burst interval for bin3 radar */
	int nskip_rst_lp;	/**< number of skipped pulses before resetting lp buffer */
	int max_pw_tol;	/**< maximum tolerance allowd in detected pulse width for radar detection */
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
	uint16 thresh0_20_hi;	/* Radar detection, thresh 0 (range 5470-5725MHz) for BW 20MHz */
	uint16 thresh1_20_hi;	/* Radar detection, thresh 1 (range 5470-5725MHz) for BW 20MHz */
	uint16 thresh0_40_hi;	/* Radar detection, thresh 0 (range 5470-5725MHz) for BW 40MHz */
	uint16 thresh1_40_hi;	/* Radar detection, thresh 1 (range 5470-5725MHz) for BW 40MHz */
	uint16 thresh0_80_hi;	/* Radar detection, thresh 0 (range 5470-5725MHz) for BW 80MHz */
	uint16 thresh1_80_hi;	/* Radar detection, thresh 1 (range 5470-5725MHz) for BW 80MHz */
#ifdef WL11AC160
	uint16 thresh0_160_lo;	/* Radar detection, thresh 0 (range 5250-5350MHz) for BW 160MHz */
	uint16 thresh1_160_lo;	/* Radar detection, thresh 1 (range 5250-5350MHz) for BW 160MHz */
	uint16 thresh0_160_hi;	/* Radar detection, thresh 0 (range 5470-5725MHz) for BW 160MHz */
	uint16 thresh1_160_hi;	/* Radar detection, thresh 1 (range 5470-5725MHz) for BW 160MHz */
#endif /* WL11AC160 */
} wl_radar_thr_t;

#define WL_RADAR_THR_VERSION	2

/* RSSI per antenna */
typedef struct {
	uint32	version;		/**< version field */
	uint32	count;			/**< number of valid antenna rssi */
	int8 rssi_ant[WL_RSSI_ANT_MAX];	/**< rssi per antenna */
} wl_rssi_ant_t;

/* data structure used in 'dfs_status' wl interface, which is used to query dfs status */
typedef struct {
	uint state;		/**< noted by WL_DFS_CACSTATE_XX. */
	uint duration;		/**< time spent in ms in state. */
	/* as dfs enters ISM state, it removes the operational channel from quiet channel
	 * list and notes the channel in channel_cleared. set to 0 if no channel is cleared
	 */
	chanspec_t chanspec_cleared;
	/* chanspec cleared used to be a uint, add another to uint16 to maintain size */
	uint16 pad;
} wl_dfs_status_t;

typedef struct {
	uint state;		/* noted by WL_DFS_CACSTATE_XX */
	uint duration;		/* time spent in ms in state */
	chanspec_t chanspec;	/* chanspec of this core */
	chanspec_t chanspec_last_cleared; /* chanspec last cleared for operation by scanning */
	uint16 sub_type;	/* currently just the index of the core or the respective PLL */
	uint16 pad;
} wl_dfs_sub_status_t;

#define WL_DFS_STATUS_ALL_VERSION	(1)
typedef struct {
	uint16 version;		/* version field; current max version 1 */
	uint16 num_sub_status;
	wl_dfs_sub_status_t  dfs_sub_status[1]; /* struct array of length num_sub_status */
} wl_dfs_status_all_t;

#define WL_DFS_AP_MOVE_VERSION	(1)
typedef struct wl_dfs_ap_move_status {
	int8 version;            /* version field; current max version 1 */
	int8 move_status;        /* DFS move status */
	chanspec_t chanspec;     /* New AP Chanspec */
	wl_dfs_status_all_t scan_status; /* status; see dfs_status_all for wl_dfs_status_all_t */
} wl_dfs_ap_move_status_t;


/* data structure used in 'radar_status' wl interface, which is use to query radar det status */
typedef struct {
	bool detected;
	int count;
	bool pretended;
	uint32 radartype;
	uint32 timenow;
	uint32 timefromL;
	int lp_csect_single;
	int detected_pulse_index;
	int nconsecq_pulses;
	chanspec_t ch;
	int pw[10];
	int intv[10];
	int fm[10];
} wl_radar_status_t;

#define NUM_PWRCTRL_RATES 12

typedef struct {
	uint8 txpwr_band_max[NUM_PWRCTRL_RATES];	/**< User set target */
	uint8 txpwr_limit[NUM_PWRCTRL_RATES];		/**< reg and local power limit */
	uint8 txpwr_local_max;				/**< local max according to the AP */
	uint8 txpwr_local_constraint;			/**< local constraint according to the AP */
	uint8 txpwr_chan_reg_max;			/**< Regulatory max for this channel */
	uint8 txpwr_target[2][NUM_PWRCTRL_RATES];	/**< Latest target for 2.4 and 5 Ghz */
	uint8 txpwr_est_Pout[2];			/**< Latest estimate for 2.4 and 5 Ghz */
	uint8 txpwr_opo[NUM_PWRCTRL_RATES];		/**< On G phy, OFDM power offset */
	uint8 txpwr_bphy_cck_max[NUM_PWRCTRL_RATES];	/**< Max CCK power for this band (SROM) */
	uint8 txpwr_bphy_ofdm_max;			/**< Max OFDM power for this band (SROM) */
	uint8 txpwr_aphy_max[NUM_PWRCTRL_RATES];	/**< Max power for A band (SROM) */
	int8  txpwr_antgain[2];				/**< Ant gain for each band - from SROM */
	uint8 txpwr_est_Pout_gofdm;			/**< Pwr estimate for 2.4 OFDM */
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

#define WL_NUM_2x2_ELEMENTS		4
#define WL_NUM_3x3_ELEMENTS		6
#define WL_NUM_4x4_ELEMENTS		10

typedef struct {
	uint16 ver;				/**< version of this struct */
	uint16 len;				/**< length in bytes of this structure */
	uint32 flags;
	chanspec_t chanspec;			/**< txpwr report for this channel */
	chanspec_t local_chanspec;		/**< channel on which we are associated */
	uint32     buflen;			/**< ppr buffer length */
	uint8      pprbuf[1];			/**< Latest target power buffer */
} wl_txppr_t;

#define WL_TXPPR_VERSION	1
#define WL_TXPPR_LENGTH	(sizeof(wl_txppr_t))
#define TX_POWER_T_VERSION	45
/* number of ppr serialization buffers, it should be reg, board and target */
#define WL_TXPPR_SER_BUF_NUM	(3)

typedef struct chanspec_txpwr_max {
	chanspec_t chanspec;   /* chanspec */
	uint8 txpwr_max;       /* max txpwr in all the rates */
	uint8 padding;
} chanspec_txpwr_max_t;

typedef struct  wl_chanspec_txpwr_max {
	uint16 ver;			/**< version of this struct */
	uint16 len;			/**< length in bytes of this structure */
	uint32 count;		/**< number of elements of (chanspec, txpwr_max) pair */
	chanspec_txpwr_max_t txpwr[1];	/**< array of (chanspec, max_txpwr) pair */
} wl_chanspec_txpwr_max_t;

#define WL_CHANSPEC_TXPWR_MAX_VER	1
#define WL_CHANSPEC_TXPWR_MAX_LEN	(sizeof(wl_chanspec_txpwr_max_t))

typedef struct tx_inst_power {
	uint8 txpwr_est_Pout[2];			/**< Latest estimate for 2.4 and 5 Ghz */
	uint8 txpwr_est_Pout_gofdm;			/**< Pwr estimate for 2.4 OFDM */
} tx_inst_power_t;

#define WL_NUM_TXCHAIN_MAX	4
typedef struct wl_txchain_pwr_offsets {
	int8 offset[WL_NUM_TXCHAIN_MAX];	/**< quarter dBm signed offset for each chain */
} wl_txchain_pwr_offsets_t;
/* maximum channels returned by the get valid channels iovar */
#define WL_NUMCHANNELS		64

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
 * offset 1: +ve RSSI boost value in dB
 */

struct tsinfo_arg {
	uint8 octets[3];
};

#define RATE_CCK_1MBPS 0
#define RATE_CCK_2MBPS 1
#define RATE_CCK_5_5MBPS 2
#define RATE_CCK_11MBPS 3

#define RATE_LEGACY_OFDM_6MBPS 0
#define RATE_LEGACY_OFDM_9MBPS 1
#define RATE_LEGACY_OFDM_12MBPS 2
#define RATE_LEGACY_OFDM_18MBPS 3
#define RATE_LEGACY_OFDM_24MBPS 4
#define RATE_LEGACY_OFDM_36MBPS 5
#define RATE_LEGACY_OFDM_48MBPS 6
#define RATE_LEGACY_OFDM_54MBPS 7

#define WL_BSSTRANS_RSSI_RATE_MAP_VERSION 1

typedef struct wl_bsstrans_rssi {
	int8 rssi_2g;	/**< RSSI in dbm for 2.4 G */
	int8 rssi_5g;	/**< RSSI in dbm for 5G, unused for cck */
} wl_bsstrans_rssi_t;

#define RSSI_RATE_MAP_MAX_STREAMS 4	/**< max streams supported */

/* RSSI to rate mapping, all 20Mhz, no SGI */
typedef struct wl_bsstrans_rssi_rate_map {
	uint16 ver;
	uint16 len; /* length of entire structure */
	wl_bsstrans_rssi_t cck[WL_NUM_RATES_CCK]; /* 2.4G only */
	wl_bsstrans_rssi_t ofdm[WL_NUM_RATES_OFDM]; /* 6 to 54mbps */
	wl_bsstrans_rssi_t phy_n[RSSI_RATE_MAP_MAX_STREAMS][WL_NUM_RATES_MCS_1STREAM]; /* MCS0-7 */
	wl_bsstrans_rssi_t phy_ac[RSSI_RATE_MAP_MAX_STREAMS][WL_NUM_RATES_VHT]; /* MCS0-9 */
} wl_bsstrans_rssi_rate_map_t;

#define WL_BSSTRANS_ROAMTHROTTLE_VERSION 1

/* Configure number of scans allowed per throttle period */
typedef struct wl_bsstrans_roamthrottle {
	uint16 ver;
	uint16 period;
	uint16 scans_allowed;
} wl_bsstrans_roamthrottle_t;

#define	NFIFO			6	/**< # tx/rx fifopairs */
#define NREINITREASONCOUNT	8
#define REINITREASONIDX(_x)	(((_x) < NREINITREASONCOUNT) ? (_x) : 0)

#define	WL_CNT_T_VERSION	30	/**< current version of wl_cnt_t struct */
#define WL_CNT_VERSION_6	6
#define WL_CNT_VERSION_11	11

#define WLC_WITH_XTLV_CNT

/*
 * tlv IDs uniquely identifies counter component
 * packed into wl_cmd_t container
 */
enum wl_cnt_xtlv_id {
	WL_CNT_XTLV_WLC = 0x100,		/**< WLC layer counters */
	WL_CNT_XTLV_CNTV_LE10_UCODE = 0x200,	/**< wl counter ver < 11 UCODE MACSTAT */
	WL_CNT_XTLV_LT40_UCODE_V1 = 0x300,	/**< corerev < 40 UCODE MACSTAT */
	WL_CNT_XTLV_GE40_UCODE_V1 = 0x400,	/**< corerev >= 40 UCODE MACSTAT */
	WL_CNT_XTLV_GE64_UCODEX_V1 = 0x800	/* corerev >= 64 UCODEX MACSTAT */
};

/* The number of variables in wl macstat cnt struct.
 * (wl_cnt_ge40mcst_v1_t, wl_cnt_lt40mcst_v1_t, wl_cnt_v_le10_mcst_t)
 */
#define WL_CNT_MCST_VAR_NUM 64
/* sizeof(wl_cnt_ge40mcst_v1_t), sizeof(wl_cnt_lt40mcst_v1_t), and sizeof(wl_cnt_v_le10_mcst_t) */
#define WL_CNT_MCST_STRUCT_SZ ((uint)sizeof(uint32) * WL_CNT_MCST_VAR_NUM)

#define INVALID_CNT_VAL (uint32)(-1)
#define WL_CNT_MCXST_STRUCT_SZ ((uint)sizeof(wl_cnt_ge64mcxst_v1_t))

#define WL_XTLV_CNTBUF_MAX_SIZE ((uint)(OFFSETOF(wl_cnt_info_t, data)) +        \
		(uint)BCM_XTLV_HDR_SIZE + (uint)sizeof(wl_cnt_wlc_t) +          \
		(uint)BCM_XTLV_HDR_SIZE + WL_CNT_MCST_STRUCT_SZ +              \
		(uint)BCM_XTLV_HDR_SIZE + WL_CNT_MCXST_STRUCT_SZ)

#define WL_CNTBUF_MAX_SIZE MAX(WL_XTLV_CNTBUF_MAX_SIZE, (uint)sizeof(wl_cnt_ver_11_t))

/* Top structure of counters IOVar buffer */
typedef struct {
	uint16	version;	/**< see definition of WL_CNT_T_VERSION */
	uint16	datalen;	/**< length of data including all paddings. */
	uint8   data [1];	/**< variable length payload:
				 * 1 or more bcm_xtlv_t type of tuples.
				 * each tuple is padded to multiple of 4 bytes.
				 * 'datalen' field of this structure includes all paddings.
				 */
} wl_cnt_info_t;

/* wlc layer counters */
typedef struct {
	/* transmit stat counters */
	uint32	txframe;	/**< tx data frames */
	uint32	txbyte;		/**< tx data bytes */
	uint32	txretrans;	/**< tx mac retransmits */
	uint32	txerror;	/**< tx data errors (derived: sum of others) */
	uint32	txctl;		/**< tx management frames */
	uint32	txprshort;	/**< tx short preamble frames */
	uint32	txserr;		/**< tx status errors */
	uint32	txnobuf;	/**< tx out of buffers errors */
	uint32	txnoassoc;	/**< tx discard because we're not associated */
	uint32	txrunt;		/**< tx runt frames */
	uint32	txchit;		/**< tx header cache hit (fastpath) */
	uint32	txcmiss;	/**< tx header cache miss (slowpath) */

	/* transmit chip error counters */
	uint32	txuflo;		/**< tx fifo underflows */
	uint32	txphyerr;	/**< tx phy errors (indicated in tx status) */
	uint32	txphycrs;

	/* receive stat counters */
	uint32	rxframe;	/**< rx data frames */
	uint32	rxbyte;		/**< rx data bytes */
	uint32	rxerror;	/**< rx data errors (derived: sum of others) */
	uint32	rxctl;		/**< rx management frames */
	uint32	rxnobuf;	/**< rx out of buffers errors */
	uint32	rxnondata;	/**< rx non data frames in the data channel errors */
	uint32	rxbadds;	/**< rx bad DS errors */
	uint32	rxbadcm;	/**< rx bad control or management frames */
	uint32	rxfragerr;	/**< rx fragmentation errors */
	uint32	rxrunt;		/**< rx runt frames */
	uint32	rxgiant;	/**< rx giant frames */
	uint32	rxnoscb;	/**< rx no scb error */
	uint32	rxbadproto;	/**< rx invalid frames */
	uint32	rxbadsrcmac;	/**< rx frames with Invalid Src Mac */
	uint32	rxbadda;	/**< rx frames tossed for invalid da */
	uint32	rxfilter;	/**< rx frames filtered out */

	/* receive chip error counters */
	uint32	rxoflo;		/**< rx fifo overflow errors */
	uint32	rxuflo[NFIFO];	/**< rx dma descriptor underflow errors */

	uint32	d11cnt_txrts_off;	/**< d11cnt txrts value when reset d11cnt */
	uint32	d11cnt_rxcrc_off;	/**< d11cnt rxcrc value when reset d11cnt */
	uint32	d11cnt_txnocts_off;	/**< d11cnt txnocts value when reset d11cnt */

	/* misc counters */
	uint32	dmade;		/**< tx/rx dma descriptor errors */
	uint32	dmada;		/**< tx/rx dma data errors */
	uint32	dmape;		/**< tx/rx dma descriptor protocol errors */
	uint32	reset;		/**< reset count */
	uint32	tbtt;		/**< cnts the TBTT int's */
	uint32	txdmawar;
	uint32	pkt_callback_reg_fail;	/**< callbacks register failure */

	/* 802.11 MIB counters, pp. 614 of 802.11 reaff doc. */
	uint32	txfrag;		/**< dot11TransmittedFragmentCount */
	uint32	txmulti;	/**< dot11MulticastTransmittedFrameCount */
	uint32	txfail;		/**< dot11FailedCount */
	uint32	txretry;	/**< dot11RetryCount */
	uint32	txretrie;	/**< dot11MultipleRetryCount */
	uint32	rxdup;		/**< dot11FrameduplicateCount */
	uint32	txrts;		/**< dot11RTSSuccessCount */
	uint32	txnocts;	/**< dot11RTSFailureCount */
	uint32	txnoack;	/**< dot11ACKFailureCount */
	uint32	rxfrag;		/**< dot11ReceivedFragmentCount */
	uint32	rxmulti;	/**< dot11MulticastReceivedFrameCount */
	uint32	rxcrc;		/**< dot11FCSErrorCount */
	uint32	txfrmsnt;	/**< dot11TransmittedFrameCount (bogus MIB?) */
	uint32	rxundec;	/**< dot11WEPUndecryptableCount */

	/* WPA2 counters (see rxundec for DecryptFailureCount) */
	uint32	tkipmicfaill;	/**< TKIPLocalMICFailures */
	uint32	tkipcntrmsr;	/**< TKIPCounterMeasuresInvoked */
	uint32	tkipreplay;	/**< TKIPReplays */
	uint32	ccmpfmterr;	/**< CCMPFormatErrors */
	uint32	ccmpreplay;	/**< CCMPReplays */
	uint32	ccmpundec;	/**< CCMPDecryptErrors */
	uint32	fourwayfail;	/**< FourWayHandshakeFailures */
	uint32	wepundec;	/**< dot11WEPUndecryptableCount */
	uint32	wepicverr;	/**< dot11WEPICVErrorCount */
	uint32	decsuccess;	/**< DecryptSuccessCount */
	uint32	tkipicverr;	/**< TKIPICVErrorCount */
	uint32	wepexcluded;	/**< dot11WEPExcludedCount */

	uint32	txchanrej;	/**< Tx frames suppressed due to channel rejection */
	uint32	psmwds;		/**< Count PSM watchdogs */
	uint32	phywatchdog;	/**< Count Phy watchdogs (triggered by ucode) */

	/* MBSS counters, AP only */
	uint32	prq_entries_handled;	/**< PRQ entries read in */
	uint32	prq_undirected_entries;	/**<    which were bcast bss & ssid */
	uint32	prq_bad_entries;	/**<    which could not be translated to info */
	uint32	atim_suppress_count;	/**< TX suppressions on ATIM fifo */
	uint32	bcn_template_not_ready;	/**< Template marked in use on send bcn ... */
	uint32	bcn_template_not_ready_done; /* ...but "DMA done" interrupt rcvd */
	uint32	late_tbtt_dpc;	/**< TBTT DPC did not happen in time */

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
	uint32  rx108mbps;	/* packets rx at 108mbps */
	uint32  rx162mbps;	/* packets rx at 162mbps */
	uint32  rx216mbps;	/* packets rx at 216 mbps */
	uint32  rx270mbps;	/* packets rx at 270 mbps */
	uint32  rx324mbps;	/* packets rx at 324 mbps */
	uint32  rx378mbps;	/* packets rx at 378 mbps */
	uint32  rx432mbps;	/* packets rx at 432 mbps */
	uint32  rx486mbps;	/* packets rx at 486 mbps */
	uint32  rx540mbps;	/* packets rx at 540 mbps */

	uint32	rfdisable;	/**< count of radio disables */

	uint32	txexptime;	/**< Tx frames suppressed due to timer expiration */

	uint32	txmpdu_sgi;	/**< count for sgi transmit */
	uint32	rxmpdu_sgi;	/**< count for sgi received */
	uint32	txmpdu_stbc;	/**< count for stbc transmit */
	uint32	rxmpdu_stbc;	/**< count for stbc received */

	uint32	rxundec_mcst;	/**< dot11WEPUndecryptableCount */

	/* WPA2 counters (see rxundec for DecryptFailureCount) */
	uint32	tkipmicfaill_mcst;	/**< TKIPLocalMICFailures */
	uint32	tkipcntrmsr_mcst;	/**< TKIPCounterMeasuresInvoked */
	uint32	tkipreplay_mcst;	/**< TKIPReplays */
	uint32	ccmpfmterr_mcst;	/**< CCMPFormatErrors */
	uint32	ccmpreplay_mcst;	/**< CCMPReplays */
	uint32	ccmpundec_mcst;	/**< CCMPDecryptErrors */
	uint32	fourwayfail_mcst;	/**< FourWayHandshakeFailures */
	uint32	wepundec_mcst;	/**< dot11WEPUndecryptableCount */
	uint32	wepicverr_mcst;	/**< dot11WEPICVErrorCount */
	uint32	decsuccess_mcst;	/**< DecryptSuccessCount */
	uint32	tkipicverr_mcst;	/**< TKIPICVErrorCount */
	uint32	wepexcluded_mcst;	/**< dot11WEPExcludedCount */

	uint32	dma_hang;	/**< count for dma hang */
	uint32	reinit;		/**< count for reinit */

	uint32  pstatxucast;	/**< count of ucast frames xmitted on all psta assoc */
	uint32  pstatxnoassoc;	/**< count of txnoassoc frames xmitted on all psta assoc */
	uint32  pstarxucast;	/**< count of ucast frames received on all psta assoc */
	uint32  pstarxbcmc;	/**< count of bcmc frames received on all psta */
	uint32  pstatxbcmc;	/**< count of bcmc frames transmitted on all psta */

	uint32  cso_passthrough; /* hw cso required but passthrough */
	uint32	cso_normal;	/**< hw cso hdr for normal process */
	uint32	chained;	/**< number of frames chained */
	uint32	chainedsz1;	/**< number of chain size 1 frames */
	uint32	unchained;	/**< number of frames not chained */
	uint32	maxchainsz;	/**< max chain size so far */
	uint32	currchainsz;	/**< current chain size */
	uint32	pciereset;	/**< Secondary Bus Reset issued by driver */
	uint32	cfgrestore;	/**< configspace restore by driver */
	uint32	reinitreason[NREINITREASONCOUNT]; /* reinitreason counters; 0: Unknown reason */
	uint32	rxrtry;

	uint32  rxmpdu_mu;      /* Number of MU MPDUs received */

	/* detailed control/management frames */
	uint32  txbar;          /**< Number of TX BAR */
	uint32  rxbar;          /**< Number of RX BAR */
	uint32  txpspoll;       /**< Number of TX PS-poll */
	uint32  rxpspoll;       /**< Number of RX PS-poll */
	uint32  txnull;         /**< Number of TX NULL_DATA */
	uint32  rxnull;         /**< Number of RX NULL_DATA */
	uint32  txqosnull;      /**< Number of TX NULL_QoSDATA */
	uint32  rxqosnull;      /**< Number of RX NULL_QoSDATA */
	uint32  txassocreq;     /**< Number of TX ASSOC request */
	uint32  rxassocreq;     /**< Number of RX ASSOC request */
	uint32  txreassocreq;   /**< Number of TX REASSOC request */
	uint32  rxreassocreq;   /**< Number of RX REASSOC request */
	uint32  txdisassoc;     /**< Number of TX DISASSOC */
	uint32  rxdisassoc;     /**< Number of RX DISASSOC */
	uint32  txassocrsp;     /**< Number of TX ASSOC response */
	uint32  rxassocrsp;     /**< Number of RX ASSOC response */
	uint32  txreassocrsp;   /**< Number of TX REASSOC response */
	uint32  rxreassocrsp;   /**< Number of RX REASSOC response */
	uint32  txauth;         /**< Number of TX AUTH */
	uint32  rxauth;         /**< Number of RX AUTH */
	uint32  txdeauth;       /**< Number of TX DEAUTH */
	uint32  rxdeauth;       /**< Number of RX DEAUTH */
	uint32  txprobereq;     /**< Number of TX probe request */
	uint32  rxprobereq;     /**< Number of RX probe request */
	uint32  txprobersp;     /**< Number of TX probe response */
	uint32  rxprobersp;     /**< Number of RX probe response */
	uint32  txaction;       /**< Number of TX action frame */
	uint32  rxaction;       /**< Number of RX action frame */
} wl_cnt_wlc_t;

/* MACXSTAT counters for ucodex (corerev >= 64) */
typedef struct {
	uint32 macxsusp;
	uint32 m2vmsg;
	uint32 v2mmsg;
	uint32 mboxout;
	uint32 musnd;
	uint32 sfb2v;
} wl_cnt_ge64mcxst_v1_t;

/* MACSTAT counters for ucode (corerev >= 40) */
typedef struct {
	/* MAC counters: 32-bit version of d11.h's macstat_t */
	uint32	txallfrm;	/**< total number of frames sent, incl. Data, ACK, RTS, CTS,
				 * Control Management (includes retransmissions)
				 */
	uint32	txrtsfrm;	/**< number of RTS sent out by the MAC */
	uint32	txctsfrm;	/**< number of CTS sent out by the MAC */
	uint32	txackfrm;	/**< number of ACK frames sent out */
	uint32	txdnlfrm;	/**< number of Null-Data transmission generated from template  */
	uint32	txbcnfrm;	/**< beacons transmitted */
	uint32	txfunfl[6];	/**< per-fifo tx underflows */
	uint32	txampdu;	/**< number of AMPDUs transmitted */
	uint32	txmpdu;		/**< number of MPDUs transmitted */
	uint32	txtplunfl;	/**< Template underflows (mac was too slow to transmit ACK/CTS
				 * or BCN)
				 */
	uint32	txphyerror;	/**< Transmit phy error, type of error is reported in tx-status for
				 * driver enqueued frames
				 */
	uint32  pktengrxducast; /* unicast frames rxed by the pkteng code */
	uint32  pktengrxdmcast; /* multicast frames rxed by the pkteng code */
	uint32	rxfrmtoolong;	/**< Received frame longer than legal limit (2346 bytes) */
	uint32	rxfrmtooshrt; /**< Received frame did not contain enough bytes for its frame type */
	uint32	rxanyerr;	/**< Any RX error that is not counted by other counters. */
	uint32	rxbadfcs;	/**< number of frames for which the CRC check failed in the MAC */
	uint32	rxbadplcp;	/**< parity check of the PLCP header failed */
	uint32	rxcrsglitch;	/**< PHY was able to correlate the preamble but not the header */
	uint32	rxstrt;		/**< Number of received frames with a good PLCP
				 * (i.e. passing parity check)
				 */
	uint32	rxdtucastmbss; /**< number of received DATA frames with good FCS and matching RA */
	uint32	rxmgucastmbss; /**< number of received mgmt frames with good FCS and matching RA */
	uint32	rxctlucast; /**< number of received CNTRL frames with good FCS and matching RA */
	uint32	rxrtsucast;	/**< number of unicast RTS addressed to the MAC (good FCS) */
	uint32	rxctsucast;	/**< number of unicast CTS addressed to the MAC (good FCS) */
	uint32	rxackucast;	/**< number of ucast ACKS received (good FCS) */
	uint32	rxdtocast; /**< number of received DATA frames (good FCS and not matching RA) */
	uint32	rxmgocast; /**< number of received MGMT frames (good FCS and not matching RA) */
	uint32	rxctlocast; /**< number of received CNTRL frame (good FCS and not matching RA) */
	uint32	rxrtsocast;	/**< number of received RTS not addressed to the MAC */
	uint32	rxctsocast;	/**< number of received CTS not addressed to the MAC */
	uint32	rxdtmcast;	/**< number of RX Data multicast frames received by the MAC */
	uint32	rxmgmcast;	/**< number of RX Management multicast frames received by the MAC */
	uint32	rxctlmcast;	/**< number of RX Control multicast frames received by the MAC
				 * (unlikely to see these)
				 */
	uint32	rxbeaconmbss;	/**< beacons received from member of BSS */
	uint32	rxdtucastobss; /* number of unicast frames addressed to the MAC from
				  * other BSS (WDS FRAME)
				  */
	uint32	rxbeaconobss;	/**< beacons received from other BSS */
	uint32	rxrsptmout;	/**< number of response timeouts for transmitted frames
				 * expecting a response
				 */
	uint32	bcntxcancl;	/**< transmit beacons canceled due to receipt of beacon (IBSS) */
	uint32	rxnodelim;	/**< number of no valid delimiter detected by ampdu parser */
	uint32	rxf0ovfl;	/**< number of receive fifo 0 overflows */
	uint32	rxf1ovfl;	/**< number of receive fifo 1 overflows */
	uint32	rxhlovfl;	/**< number of length / header fifo overflows */
	uint32	missbcn_dbg;	/**< number of beacon missed to receive */
	uint32	pmqovfl;	/**< number of PMQ overflows */
	uint32	rxcgprqfrm;	/**< number of received Probe requests that made it into
				 * the PRQ fifo
				 */
	uint32	rxcgprsqovfl;	/**< Rx Probe Request Que overflow in the AP */
	uint32	txcgprsfail;	/**< Tx Probe Response Fail. AP sent probe response but did
				 * not get ACK
				 */
	uint32	txcgprssuc;	/**< Tx Probe Response Success (ACK was received) */
	uint32	prs_timeout;	/**< number of probe requests that were dropped from the PRQ
				 * fifo because a probe response could not be sent out within
				 * the time limit defined in M_PRS_MAXTIME
				 */
	uint32	txrtsfail;	/**< number of rts transmission failure that reach retry limit */
	uint32	txucast;	/**< number of unicast tx expecting response other than cts/cwcts */
	uint32  txinrtstxop;	/**< number of data frame transmissions during rts txop */
	uint32	rxback;		/**< blockack rxcnt */
	uint32	txback;		/**< blockack txcnt */
	uint32	bphy_rxcrsglitch;	/**< PHY count of bphy glitches */
	uint32	rxdrop20s;	/**< drop secondary cnt */
	uint32	rxtoolate;	/**< receive too late */
	uint32  bphy_badplcp;	/**< number of bad PLCP reception on BPHY rate */
} wl_cnt_ge40mcst_v1_t;

/* MACSTAT counters for ucode (corerev < 40) */
typedef struct {
	/* MAC counters: 32-bit version of d11.h's macstat_t */
	uint32	txallfrm;	/**< total number of frames sent, incl. Data, ACK, RTS, CTS,
				 * Control Management (includes retransmissions)
				 */
	uint32	txrtsfrm;	/**< number of RTS sent out by the MAC */
	uint32	txctsfrm;	/**< number of CTS sent out by the MAC */
	uint32	txackfrm;	/**< number of ACK frames sent out */
	uint32	txdnlfrm;	/**< number of Null-Data transmission generated from template  */
	uint32	txbcnfrm;	/**< beacons transmitted */
	uint32	txfunfl[6];	/**< per-fifo tx underflows */
	uint32	txampdu;	/**< number of AMPDUs transmitted */
	uint32	txmpdu;		/**< number of MPDUs transmitted */
	uint32	txtplunfl;	/**< Template underflows (mac was too slow to transmit ACK/CTS
				 * or BCN)
				 */
	uint32	txphyerror;	/**< Transmit phy error, type of error is reported in tx-status for
				 * driver enqueued frames
				 */
	uint32  pktengrxducast; /**< unicast frames rxed by the pkteng code */
	uint32  pktengrxdmcast; /**< multicast frames rxed by the pkteng code */
	uint32	rxfrmtoolong;	/**< Received frame longer than legal limit (2346 bytes) */
	uint32	rxfrmtooshrt; /**< Received frame did not contain enough bytes for its frame type */
	uint32	rxanyerr;	/**< Any RX error that is not counted by other counters. */
	uint32	rxbadfcs;	/**< number of frames for which the CRC check failed in the MAC */
	uint32	rxbadplcp;	/**< parity check of the PLCP header failed */
	uint32	rxcrsglitch;	/**< PHY was able to correlate the preamble but not the header */
	uint32	rxstrt;		/**< Number of received frames with a good PLCP
				 * (i.e. passing parity check)
				 */
	uint32	rxdtucastmbss; /**< number of received DATA frames with good FCS and matching RA */
	uint32	rxmgucastmbss; /**< number of received mgmt frames with good FCS and matching RA */
	uint32	rxctlucast; /**< number of received CNTRL frames with good FCS and matching RA */
	uint32	rxrtsucast;	/**< number of unicast RTS addressed to the MAC (good FCS) */
	uint32	rxctsucast;	/**< number of unicast CTS addressed to the MAC (good FCS) */
	uint32	rxackucast;	/**< number of ucast ACKS received (good FCS) */
	uint32	rxdtocast;  /**< number of received DATA frames (good FCS and not matching RA) */
	uint32	rxmgocast;  /**< number of received MGMT frames (good FCS and not matching RA) */
	uint32	rxctlocast; /**< number of received CNTRL frame (good FCS and not matching RA) */
	uint32	rxrtsocast;	/**< number of received RTS not addressed to the MAC */
	uint32	rxctsocast;	/**< number of received CTS not addressed to the MAC */
	uint32	rxdtmcast;	/**< number of RX Data multicast frames received by the MAC */
	uint32	rxmgmcast;	/**< number of RX Management multicast frames received by the MAC */
	uint32	rxctlmcast;	/**< number of RX Control multicast frames received by the MAC
				 * (unlikely to see these)
				 */
	uint32	rxbeaconmbss;	/**< beacons received from member of BSS */
	uint32	rxdtucastobss; /* number of unicast frames addressed to the MAC from
				  * other BSS (WDS FRAME)
				  */
	uint32	rxbeaconobss;	/**< beacons received from other BSS */
	uint32	rxrsptmout;	/**< number of response timeouts for transmitted frames
				 * expecting a response
				 */
	uint32	bcntxcancl;	/**< transmit beacons canceled due to receipt of beacon (IBSS) */
	uint32	rxnodelim;	/**< number of no valid delimiter detected by ampdu parser */
	uint32	rxf0ovfl;	/**< number of receive fifo 0 overflows */
	uint32	dbgoff46;
	uint32	dbgoff47;
	uint32	dbgoff48;	/**< Used for counting txstatus queue overflow (corerev <= 4)  */
	uint32	pmqovfl;	/**< number of PMQ overflows */
	uint32	rxcgprqfrm;	/**< number of received Probe requests that made it into
				 * the PRQ fifo
				 */
	uint32	rxcgprsqovfl;	/**< Rx Probe Request Que overflow in the AP */
	uint32	txcgprsfail;	/**< Tx Probe Response Fail. AP sent probe response but did
				 * not get ACK
				 */
	uint32	txcgprssuc;	/**< Tx Probe Response Success (ACK was received) */
	uint32	prs_timeout;	/**< number of probe requests that were dropped from the PRQ
				 * fifo because a probe response could not be sent out within
				 * the time limit defined in M_PRS_MAXTIME
				 */
	uint32	txrtsfail;	/**< number of rts transmission failure that reach retry limit */
	uint32	txucast;	/**< number of unicast tx expecting response other than cts/cwcts */
	uint32  txinrtstxop;	/**< number of data frame transmissions during rts txop */
	uint32	rxback;		/**< blockack rxcnt */
	uint32	txback;		/**< blockack txcnt */
	uint32	bphy_rxcrsglitch;	/**< PHY count of bphy glitches */
	uint32	phywatch;
	uint32	rxtoolate;	/**< receive too late */
	uint32  bphy_badplcp;	/**< number of bad PLCP reception on BPHY rate */
} wl_cnt_lt40mcst_v1_t;

/* MACSTAT counters for "wl counter" version <= 10 */
typedef struct {
	/* MAC counters: 32-bit version of d11.h's macstat_t */
	uint32	txallfrm;	/**< total number of frames sent, incl. Data, ACK, RTS, CTS,
				 * Control Management (includes retransmissions)
				 */
	uint32	txrtsfrm;	/**< number of RTS sent out by the MAC */
	uint32	txctsfrm;	/**< number of CTS sent out by the MAC */
	uint32	txackfrm;	/**< number of ACK frames sent out */
	uint32	txdnlfrm;	/**< number of Null-Data transmission generated from template  */
	uint32	txbcnfrm;	/**< beacons transmitted */
	uint32	txfunfl[6];	/**< per-fifo tx underflows */
	uint32	txfbw;		/**< transmit at fallback bw (dynamic bw) */
	uint32	PAD0;		/**< number of MPDUs transmitted */
	uint32	txtplunfl;	/**< Template underflows (mac was too slow to transmit ACK/CTS
				 * or BCN)
				 */
	uint32	txphyerror;	/**< Transmit phy error, type of error is reported in tx-status for
				 * driver enqueued frames
				 */
	uint32  pktengrxducast; /* unicast frames rxed by the pkteng code */
	uint32  pktengrxdmcast; /* multicast frames rxed by the pkteng code */
	uint32	rxfrmtoolong;	/**< Received frame longer than legal limit (2346 bytes) */
	uint32	rxfrmtooshrt; /**< Received frame did not contain enough bytes for its frame type */
	uint32	rxinvmachdr;	/**< Either the protocol version != 0 or frame type not
				 * data/control/management
				 */
	uint32	rxbadfcs;	/**< number of frames for which the CRC check failed in the MAC */
	uint32	rxbadplcp;	/**< parity check of the PLCP header failed */
	uint32	rxcrsglitch;	/**< PHY was able to correlate the preamble but not the header */
	uint32	rxstrt;		/**< Number of received frames with a good PLCP
				 * (i.e. passing parity check)
				 */
	uint32	rxdfrmucastmbss; /* number of received DATA frames with good FCS and matching RA */
	uint32	rxmfrmucastmbss; /* number of received mgmt frames with good FCS and matching RA */
	uint32	rxcfrmucast; /**< number of received CNTRL frames with good FCS and matching RA */
	uint32	rxrtsucast;  /**< number of unicast RTS addressed to the MAC (good FCS) */
	uint32	rxctsucast;  /**< number of unicast CTS addressed to the MAC (good FCS) */
	uint32	rxackucast;	/**< number of ucast ACKS received (good FCS) */
	uint32	rxdfrmocast; /**< number of received DATA frames (good FCS and not matching RA) */
	uint32	rxmfrmocast; /**< number of received MGMT frames (good FCS and not matching RA) */
	uint32	rxcfrmocast; /**< number of received CNTRL frame (good FCS and not matching RA) */
	uint32	rxrtsocast;	/**< number of received RTS not addressed to the MAC */
	uint32	rxctsocast;	/**< number of received CTS not addressed to the MAC */
	uint32	rxdfrmmcast;	/**< number of RX Data multicast frames received by the MAC */
	uint32	rxmfrmmcast;	/**< number of RX Management multicast frames received by the MAC */
	uint32	rxcfrmmcast;	/**< number of RX Control multicast frames received by the MAC
				 * (unlikely to see these)
				 */
	uint32	rxbeaconmbss;	/**< beacons received from member of BSS */
	uint32	rxdfrmucastobss; /**< number of unicast frames addressed to the MAC from
				  * other BSS (WDS FRAME)
				  */
	uint32	rxbeaconobss;	/**< beacons received from other BSS */
	uint32	rxrsptmout;	/**< number of response timeouts for transmitted frames
				 * expecting a response
				 */
	uint32	bcntxcancl;	/**< transmit beacons canceled due to receipt of beacon (IBSS) */
	uint32	PAD1;
	uint32	rxf0ovfl;	/**< number of receive fifo 0 overflows */
	uint32	rxf1ovfl;	/**< Number of receive fifo 1 overflows (obsolete) */
	uint32	rxf2ovfl;	/**< Number of receive fifo 2 overflows (obsolete) */
	uint32	txsfovfl;	/**< Number of transmit status fifo overflows (obsolete) */
	uint32	pmqovfl;	/**< number of PMQ overflows */
	uint32	rxcgprqfrm;	/**< number of received Probe requests that made it into
				 * the PRQ fifo
				 */
	uint32	rxcgprsqovfl;	/**< Rx Probe Request Que overflow in the AP */
	uint32	txcgprsfail;	/**< Tx Probe Response Fail. AP sent probe response but did
				 * not get ACK
				 */
	uint32	txcgprssuc;	/**< Tx Probe Response Success (ACK was received) */
	uint32	prs_timeout;	/**< number of probe requests that were dropped from the PRQ
				 * fifo because a probe response could not be sent out within
				 * the time limit defined in M_PRS_MAXTIME
				 */
	uint32	rxnack;		/**< obsolete */
	uint32	frmscons;	/**< obsolete */
	uint32  txnack;		/**< obsolete */
	uint32	rxback;		/**< blockack rxcnt */
	uint32	txback;		/**< blockack txcnt */
	uint32	bphy_rxcrsglitch;	/**< PHY count of bphy glitches */
	uint32	rxdrop20s;	/**< drop secondary cnt */
	uint32	rxtoolate;	/**< receive too late */
	uint32  bphy_badplcp;	/**< number of bad PLCP reception on BPHY rate */
} wl_cnt_v_le10_mcst_t;

typedef struct {
	uint16	version;	/**< see definition of WL_CNT_T_VERSION */
	uint16	length;		/**< length of entire structure */

	/* transmit stat counters */
	uint32	txframe;	/**< tx data frames */
	uint32	txbyte;		/**< tx data bytes */
	uint32	txretrans;	/**< tx mac retransmits */
	uint32	txerror;	/**< tx data errors (derived: sum of others) */
	uint32	txctl;		/**< tx management frames */
	uint32	txprshort;	/**< tx short preamble frames */
	uint32	txserr;		/**< tx status errors */
	uint32	txnobuf;	/**< tx out of buffers errors */
	uint32	txnoassoc;	/**< tx discard because we're not associated */
	uint32	txrunt;		/**< tx runt frames */
	uint32	txchit;		/**< tx header cache hit (fastpath) */
	uint32	txcmiss;	/**< tx header cache miss (slowpath) */

	/* transmit chip error counters */
	uint32	txuflo;		/**< tx fifo underflows */
	uint32	txphyerr;	/**< tx phy errors (indicated in tx status) */
	uint32	txphycrs;

	/* receive stat counters */
	uint32	rxframe;	/**< rx data frames */
	uint32	rxbyte;		/**< rx data bytes */
	uint32	rxerror;	/**< rx data errors (derived: sum of others) */
	uint32	rxctl;		/**< rx management frames */
	uint32	rxnobuf;	/**< rx out of buffers errors */
	uint32	rxnondata;	/**< rx non data frames in the data channel errors */
	uint32	rxbadds;	/**< rx bad DS errors */
	uint32	rxbadcm;	/**< rx bad control or management frames */
	uint32	rxfragerr;	/**< rx fragmentation errors */
	uint32	rxrunt;		/**< rx runt frames */
	uint32	rxgiant;	/**< rx giant frames */
	uint32	rxnoscb;	/**< rx no scb error */
	uint32	rxbadproto;	/**< rx invalid frames */
	uint32	rxbadsrcmac;	/**< rx frames with Invalid Src Mac */
	uint32	rxbadda;	/**< rx frames tossed for invalid da */
	uint32	rxfilter;	/**< rx frames filtered out */

	/* receive chip error counters */
	uint32	rxoflo;		/**< rx fifo overflow errors */
	uint32	rxuflo[NFIFO];	/**< rx dma descriptor underflow errors */

	uint32	d11cnt_txrts_off;	/**< d11cnt txrts value when reset d11cnt */
	uint32	d11cnt_rxcrc_off;	/**< d11cnt rxcrc value when reset d11cnt */
	uint32	d11cnt_txnocts_off;	/**< d11cnt txnocts value when reset d11cnt */

	/* misc counters */
	uint32	dmade;		/**< tx/rx dma descriptor errors */
	uint32	dmada;		/**< tx/rx dma data errors */
	uint32	dmape;		/**< tx/rx dma descriptor protocol errors */
	uint32	reset;		/**< reset count */
	uint32	tbtt;		/**< cnts the TBTT int's */
	uint32	txdmawar;
	uint32	pkt_callback_reg_fail;	/**< callbacks register failure */

	/* MAC counters: 32-bit version of d11.h's macstat_t */
	uint32	txallfrm;	/**< total number of frames sent, incl. Data, ACK, RTS, CTS,
				 * Control Management (includes retransmissions)
				 */
	uint32	txrtsfrm;	/**< number of RTS sent out by the MAC */
	uint32	txctsfrm;	/**< number of CTS sent out by the MAC */
	uint32	txackfrm;	/**< number of ACK frames sent out */
	uint32	txdnlfrm;	/**< Not used */
	uint32	txbcnfrm;	/**< beacons transmitted */
	uint32	txfunfl[6];	/**< per-fifo tx underflows */
	uint32	rxtoolate;	/**< receive too late */
	uint32  txfbw;		/**< transmit at fallback bw (dynamic bw) */
	uint32	txtplunfl;	/**< Template underflows (mac was too slow to transmit ACK/CTS
				 * or BCN)
				 */
	uint32	txphyerror;	/**< Transmit phy error, type of error is reported in tx-status for
				 * driver enqueued frames
				 */
	uint32	rxfrmtoolong;	/**< Received frame longer than legal limit (2346 bytes) */
	uint32	rxfrmtooshrt; /**< Received frame did not contain enough bytes for its frame type */
	uint32	rxinvmachdr;	/**< Either the protocol version != 0 or frame type not
				 * data/control/management
				 */
	uint32	rxbadfcs;	/**< number of frames for which the CRC check failed in the MAC */
	uint32	rxbadplcp;	/**< parity check of the PLCP header failed */
	uint32	rxcrsglitch;	/**< PHY was able to correlate the preamble but not the header */
	uint32	rxstrt;		/**< Number of received frames with a good PLCP
				 * (i.e. passing parity check)
				 */
	uint32	rxdfrmucastmbss; /* Number of received DATA frames with good FCS and matching RA */
	uint32	rxmfrmucastmbss; /* number of received mgmt frames with good FCS and matching RA */
	uint32	rxcfrmucast; /**< number of received CNTRL frames with good FCS and matching RA */
	uint32	rxrtsucast;	/**< number of unicast RTS addressed to the MAC (good FCS) */
	uint32	rxctsucast;	/**< number of unicast CTS addressed to the MAC (good FCS) */
	uint32	rxackucast;	/**< number of ucast ACKS received (good FCS) */
	uint32	rxdfrmocast; /**< number of received DATA frames (good FCS and not matching RA) */
	uint32	rxmfrmocast; /**< number of received MGMT frames (good FCS and not matching RA) */
	uint32	rxcfrmocast; /**< number of received CNTRL frame (good FCS and not matching RA) */
	uint32	rxrtsocast;	/**< number of received RTS not addressed to the MAC */
	uint32	rxctsocast;	/**< number of received CTS not addressed to the MAC */
	uint32	rxdfrmmcast;	/**< number of RX Data multicast frames received by the MAC */
	uint32	rxmfrmmcast;	/**< number of RX Management multicast frames received by the MAC */
	uint32	rxcfrmmcast;	/**< number of RX Control multicast frames received by the MAC
				 * (unlikely to see these)
				 */
	uint32	rxbeaconmbss;	/**< beacons received from member of BSS */
	uint32	rxdfrmucastobss; /* number of unicast frames addressed to the MAC from
				  * other BSS (WDS FRAME)
				  */
	uint32	rxbeaconobss;	/**< beacons received from other BSS */
	uint32	rxrsptmout;	/**< Number of response timeouts for transmitted frames
				 * expecting a response
				 */
	uint32	bcntxcancl;	/**< transmit beacons canceled due to receipt of beacon (IBSS) */
	uint32	rxf0ovfl;	/**< Number of receive fifo 0 overflows */
	uint32	rxf1ovfl;	/**< Number of receive fifo 1 overflows (obsolete) */
	uint32	rxf2ovfl;	/**< Number of receive fifo 2 overflows (obsolete) */
	uint32	txsfovfl;	/**< Number of transmit status fifo overflows (obsolete) */
	uint32	pmqovfl;	/**< Number of PMQ overflows */
	uint32	rxcgprqfrm;	/**< Number of received Probe requests that made it into
				 * the PRQ fifo
				 */
	uint32	rxcgprsqovfl;	/**< Rx Probe Request Que overflow in the AP */
	uint32	txcgprsfail;	/**< Tx Probe Response Fail. AP sent probe response but did
				 * not get ACK
				 */
	uint32	txcgprssuc;	/**< Tx Probe Response Success (ACK was received) */
	uint32	prs_timeout;	/**< Number of probe requests that were dropped from the PRQ
				 * fifo because a probe response could not be sent out within
				 * the time limit defined in M_PRS_MAXTIME
				 */
	uint32	rxnack;		/**< obsolete */
	uint32	frmscons;	/**< obsolete */
	uint32  txnack;		/**< obsolete */
	uint32	rxback;		/**< blockack rxcnt */
	uint32	txback;		/**< blockack txcnt */

	/* 802.11 MIB counters, pp. 614 of 802.11 reaff doc. */
	uint32	txfrag;		/**< dot11TransmittedFragmentCount */
	uint32	txmulti;	/**< dot11MulticastTransmittedFrameCount */
	uint32	txfail;		/**< dot11FailedCount */
	uint32	txretry;	/**< dot11RetryCount */
	uint32	txretrie;	/**< dot11MultipleRetryCount */
	uint32	rxdup;		/**< dot11FrameduplicateCount */
	uint32	txrts;		/**< dot11RTSSuccessCount */
	uint32	txnocts;	/**< dot11RTSFailureCount */
	uint32	txnoack;	/**< dot11ACKFailureCount */
	uint32	rxfrag;		/**< dot11ReceivedFragmentCount */
	uint32	rxmulti;	/**< dot11MulticastReceivedFrameCount */
	uint32	rxcrc;		/**< dot11FCSErrorCount */
	uint32	txfrmsnt;	/**< dot11TransmittedFrameCount (bogus MIB?) */
	uint32	rxundec;	/**< dot11WEPUndecryptableCount */

	/* WPA2 counters (see rxundec for DecryptFailureCount) */
	uint32	tkipmicfaill;	/**< TKIPLocalMICFailures */
	uint32	tkipcntrmsr;	/**< TKIPCounterMeasuresInvoked */
	uint32	tkipreplay;	/**< TKIPReplays */
	uint32	ccmpfmterr;	/**< CCMPFormatErrors */
	uint32	ccmpreplay;	/**< CCMPReplays */
	uint32	ccmpundec;	/**< CCMPDecryptErrors */
	uint32	fourwayfail;	/**< FourWayHandshakeFailures */
	uint32	wepundec;	/**< dot11WEPUndecryptableCount */
	uint32	wepicverr;	/**< dot11WEPICVErrorCount */
	uint32	decsuccess;	/**< DecryptSuccessCount */
	uint32	tkipicverr;	/**< TKIPICVErrorCount */
	uint32	wepexcluded;	/**< dot11WEPExcludedCount */

	uint32	txchanrej;	/**< Tx frames suppressed due to channel rejection */
	uint32	psmwds;		/**< Count PSM watchdogs */
	uint32	phywatchdog;	/**< Count Phy watchdogs (triggered by ucode) */

	/* MBSS counters, AP only */
	uint32	prq_entries_handled;	/**< PRQ entries read in */
	uint32	prq_undirected_entries;	/**<    which were bcast bss & ssid */
	uint32	prq_bad_entries;	/**<    which could not be translated to info */
	uint32	atim_suppress_count;	/**< TX suppressions on ATIM fifo */
	uint32	bcn_template_not_ready;	/**< Template marked in use on send bcn ... */
	uint32	bcn_template_not_ready_done; /* ...but "DMA done" interrupt rcvd */
	uint32	late_tbtt_dpc;	/**< TBTT DPC did not happen in time */

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
	uint32  rx108mbps;	/* packets rx at 108mbps */
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

	uint32	rfdisable;	/**< count of radio disables */
	uint32	bphy_rxcrsglitch;	/**< PHY count of bphy glitches */
	uint32  bphy_badplcp;

	uint32	txexptime;	/**< Tx frames suppressed due to timer expiration */

	uint32	txmpdu_sgi;	/**< count for sgi transmit */
	uint32	rxmpdu_sgi;	/**< count for sgi received */
	uint32	txmpdu_stbc;	/**< count for stbc transmit */
	uint32	rxmpdu_stbc;	/**< count for stbc received */

	uint32	rxundec_mcst;	/**< dot11WEPUndecryptableCount */

	/* WPA2 counters (see rxundec for DecryptFailureCount) */
	uint32	tkipmicfaill_mcst;	/**< TKIPLocalMICFailures */
	uint32	tkipcntrmsr_mcst;	/**< TKIPCounterMeasuresInvoked */
	uint32	tkipreplay_mcst;	/**< TKIPReplays */
	uint32	ccmpfmterr_mcst;	/**< CCMPFormatErrors */
	uint32	ccmpreplay_mcst;	/**< CCMPReplays */
	uint32	ccmpundec_mcst;	/**< CCMPDecryptErrors */
	uint32	fourwayfail_mcst;	/**< FourWayHandshakeFailures */
	uint32	wepundec_mcst;	/**< dot11WEPUndecryptableCount */
	uint32	wepicverr_mcst;	/**< dot11WEPICVErrorCount */
	uint32	decsuccess_mcst;	/**< DecryptSuccessCount */
	uint32	tkipicverr_mcst;	/**< TKIPICVErrorCount */
	uint32	wepexcluded_mcst;	/**< dot11WEPExcludedCount */

	uint32	dma_hang;	/**< count for dma hang */
	uint32	reinit;		/**< count for reinit */

	uint32  pstatxucast;	/**< count of ucast frames xmitted on all psta assoc */
	uint32  pstatxnoassoc;	/**< count of txnoassoc frames xmitted on all psta assoc */
	uint32  pstarxucast;	/**< count of ucast frames received on all psta assoc */
	uint32  pstarxbcmc;	/**< count of bcmc frames received on all psta */
	uint32  pstatxbcmc;	/**< count of bcmc frames transmitted on all psta */

	uint32  cso_passthrough; /* hw cso required but passthrough */
	uint32	cso_normal;	/**< hw cso hdr for normal process */
	uint32	chained;	/**< number of frames chained */
	uint32	chainedsz1;	/**< number of chain size 1 frames */
	uint32	unchained;	/**< number of frames not chained */
	uint32	maxchainsz;	/**< max chain size so far */
	uint32	currchainsz;	/**< current chain size */
	uint32	rxdrop20s;	/**< drop secondary cnt */
	uint32	pciereset;	/**< Secondary Bus Reset issued by driver */
	uint32	cfgrestore;	/**< configspace restore by driver */
	uint32	reinitreason[NREINITREASONCOUNT]; /* reinitreason counters; 0: Unknown reason */
	uint32  rxrtry;		/**< num of received packets with retry bit on */
	uint32	txmpdu;		/**< macstat cnt only valid in ver 11. number of MPDUs txed.  */
	uint32	rxnodelim;	/**< macstat cnt only valid in ver 11.
				 * number of occasions that no valid delimiter is detected
				 * by ampdu parser.
				 */
	uint32  rxmpdu_mu;      /* Number of MU MPDUs received */

	/* detailed control/management frames */
	uint32  txbar;          /**< Number of TX BAR */
	uint32  rxbar;          /**< Number of RX BAR */
	uint32  txpspoll;       /**< Number of TX PS-poll */
	uint32  rxpspoll;       /**< Number of RX PS-poll */
	uint32  txnull;         /**< Number of TX NULL_DATA */
	uint32  rxnull;         /**< Number of RX NULL_DATA */
	uint32  txqosnull;      /**< Number of TX NULL_QoSDATA */
	uint32  rxqosnull;      /**< Number of RX NULL_QoSDATA */
	uint32  txassocreq;     /**< Number of TX ASSOC request */
	uint32  rxassocreq;     /**< Number of RX ASSOC request */
	uint32  txreassocreq;   /**< Number of TX REASSOC request */
	uint32  rxreassocreq;   /**< Number of RX REASSOC request */
	uint32  txdisassoc;     /**< Number of TX DISASSOC */
	uint32  rxdisassoc;     /**< Number of RX DISASSOC */
	uint32  txassocrsp;     /**< Number of TX ASSOC response */
	uint32  rxassocrsp;     /**< Number of RX ASSOC response */
	uint32  txreassocrsp;   /**< Number of TX REASSOC response */
	uint32  rxreassocrsp;   /**< Number of RX REASSOC response */
	uint32  txauth;         /**< Number of TX AUTH */
	uint32  rxauth;         /**< Number of RX AUTH */
	uint32  txdeauth;       /**< Number of TX DEAUTH */
	uint32  rxdeauth;       /**< Number of RX DEAUTH */
	uint32  txprobereq;     /**< Number of TX probe request */
	uint32  rxprobereq;     /**< Number of RX probe request */
	uint32  txprobersp;     /**< Number of TX probe response */
	uint32  rxprobersp;     /**< Number of RX probe response */
	uint32  txaction;       /**< Number of TX action frame */
	uint32  rxaction;       /**< Number of RX action frame */

} wl_cnt_ver_11_t;

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
	uint32  txfunfl[6]; /* per-fifo tx underflows */
	uint32	rxtoolate;	/* receive too late */
	uint32  txfbw;	    /* transmit at fallback bw (dynamic bw) */
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
	uint32  txnack;		/* obsolete */
	uint32	rxback;		/* blockack rxcnt */
	uint32	txback;		/* blockack txcnt */

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
	uint32  bphy_badplcp;

	uint32  txmpdu_sgi; /* count for sgi transmit */
	uint32  rxmpdu_sgi; /* count for sgi received */
	uint32  txmpdu_stbc;    /* count for stbc transmit */
	uint32  rxmpdu_stbc;    /* count for stbc received */

	uint32	rxdrop20s;	/* drop secondary cnt */
} wl_cnt_ver_6_t;

#define	WL_DELTA_STATS_T_VERSION	2	/* current version of wl_delta_stats_t struct */

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
	uint32  rx108mbps;	/* packets rx at 108mbps */
	uint32  rx162mbps;	/* packets rx at 162mbps */
	uint32  rx216mbps;	/* packets rx at 216 mbps */
	uint32  rx270mbps;	/* packets rx at 270 mbps */
	uint32  rx324mbps;	/* packets rx at 324 mbps */
	uint32  rx378mbps;	/* packets rx at 378 mbps */
	uint32  rx432mbps;	/* packets rx at 432 mbps */
	uint32  rx486mbps;	/* packets rx at 486 mbps */
	uint32  rx540mbps;	/* packets rx at 540 mbps */

	/* phy stats */
	uint32 rxbadplcp;
	uint32 rxcrsglitch;
	uint32 bphy_rxcrsglitch;
	uint32 bphy_badplcp;

} wl_delta_stats_t;

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

/* TCP Keep-Alive conn struct */
typedef struct wl_mtcpkeep_alive_conn_pkt {
	struct ether_addr saddr;		/* src mac address */
	struct ether_addr daddr;		/* dst mac address */
	struct ipv4_addr sipaddr;		/* source IP addr */
	struct ipv4_addr dipaddr;		/* dest IP addr */
	uint16 sport;				/* src port */
	uint16 dport;				/* dest port */
	uint32 seq;				/* seq number */
	uint32 ack;				/* ACK number */
	uint16 tcpwin;				/* TCP window */
} wl_mtcpkeep_alive_conn_pkt_t;

/* TCP Keep-Alive interval struct */
typedef struct wl_mtcpkeep_alive_timers_pkt {
	uint16 interval;		/* interval timer */
	uint16 retry_interval;		/* retry_interval timer */
	uint16 retry_count;		/* retry_count */
} wl_mtcpkeep_alive_timers_pkt_t;

typedef struct wake_info {
	uint32 wake_reason;
	uint32 wake_info_len;		/* size of packet */
	uchar  packet[1];
} wake_info_t;

typedef struct wake_pkt {
	uint32 wake_pkt_len;		/* size of packet */
	uchar  packet[1];
} wake_pkt_t;


#define WL_MTCPKEEP_ALIVE_VERSION		1

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

/* struct for ampdu tx/rx aggregation control */
struct ampdu_aggr {
	int8 aggr_override;	/* aggr overrided by dongle. Not to be set by host. */
	uint16 conf_TID_bmap;	/* bitmap of TIDs to configure */
	uint16 enab_TID_bmap;	/* enable/disable per TID */
};

/* structure for identifying ea/tid for sending addba/delba */
struct ampdu_ea_tid {
	struct ether_addr ea;		/* Station address */
	uint8 tid;			/* tid */
	uint8 initiator;	/* 0 is recipient, 1 is originator */
};
/* structure for identifying retry/tid for retry_limit_tid/rr_retry_limit_tid */
struct ampdu_retry_tid {
	uint8 tid;	/* tid */
	uint8 retry;	/* retry value */
};

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
/* structure for tdls iovars */
typedef struct tdls_iovar {
	struct ether_addr ea;		/* Station address */
	uint8 mode;			/* mode: depends on iovar */
	chanspec_t chanspec;
	uint32 pad;			/* future */
} tdls_iovar_t;

#define TDLS_WFD_IE_SIZE		512
/* structure for tdls wfd ie */
typedef struct tdls_wfd_ie_iovar {
	struct ether_addr ea;		/* Station address */
	uint8 mode;
	uint16 length;
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
	uint8 frame_type;		/* csa frame type, unicast or broadcast */
} wl_chan_switch_t;

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
/* report found/lost events for SSID and BSSID networks seperately */
#define REPORT_SEPERATELY_BIT		11

#define SORT_CRITERIA_MASK	0x0001
#define AUTO_NET_SWITCH_MASK	0x0002
#define ENABLE_BKGRD_SCAN_MASK	0x0004
#define IMMEDIATE_SCAN_MASK	0x0008
#define AUTO_CONNECT_MASK	0x0010

#define ENABLE_BD_SCAN_MASK	0x0020
#define ENABLE_ADAPTSCAN_MASK	0x00c0
#define IMMEDIATE_EVENT_MASK	0x0100
#define SUPPRESS_SSID_MASK	0x0200
#define ENABLE_NET_OFFLOAD_MASK	0x0400
/* report found/lost events for SSID and BSSID networks seperately */
#define REPORT_SEPERATELY_MASK	0x0800

#define PFN_VERSION			2
#define PFN_SCANRESULT_VERSION		1
#define MAX_PFN_LIST_COUNT		16

#define PFN_COMPLETE			1
#define PFN_INCOMPLETE			0

#define DEFAULT_BESTN			2
#define DEFAULT_MSCAN			0
#define DEFAULT_REPEAT			10
#define DEFAULT_EXP			2

#define PFN_PARTIAL_SCAN_BIT		0
#define PFN_PARTIAL_SCAN_MASK		1
#define PFN_SWC_RSSI_WINDOW_MAX   8
#define PFN_SWC_MAX_NUM_APS       16
#define PFN_HOTLIST_MAX_NUM_APS   64

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

typedef struct wl_pfn_lnet_info {
	wl_pfn_subnet_info_t pfnsubnet; /* BSSID + channel + SSID len + SSID */
	uint16	flags; /* partial scan, etc */
	int16	RSSI; /* receive signal strength (in dBm) */
	uint32	timestamp; /* age in miliseconds */
	uint16	rtt0; /* estimated distance to this AP in centimeters */
	uint16	rtt1; /* standard deviation of the distance to this AP in centimeters */
} wl_pfn_lnet_info_t;

typedef struct wl_pfn_lscanresults {
	uint32 version;
	uint32 status;
	uint32 count;
	wl_pfn_lnet_info_t netinfo[1];
} wl_pfn_lscanresults_t;

/* this is used to report on 1-* pfn scan results */
typedef struct wl_pfn_scanresults {
	uint32 version;
	uint32 status;
	uint32 count;
	wl_pfn_net_info_t netinfo[1];
} wl_pfn_scanresults_t;

typedef struct wl_pfn_significant_net {
	uint16 flags;
	uint16 channel;
	struct ether_addr BSSID;
	int8 rssi[PFN_SWC_RSSI_WINDOW_MAX];
} wl_pfn_significant_net_t;


typedef struct wl_pfn_swc_results {
	uint32 version;
	uint32 pkt_count;
	uint32 total_count;
	wl_pfn_significant_net_t list[1];
} wl_pfn_swc_results_t;

/* used to report exactly one scan result */
/* plus reports detailed scan info in bss_info */
typedef struct wl_pfn_scanresult {
	uint32 version;
	uint32 status;
	uint32 count;
	wl_pfn_net_info_t netinfo;
	wl_bss_info_t bss_info;
} wl_pfn_scanresult_t;

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
	struct ether_addr  macaddr;
	/* Bit4: suppress_lost, Bit3: suppress_found */
	uint16             flags;
} wl_pfn_bssid_t;

typedef struct wl_pfn_significant_bssid {
	struct ether_addr	macaddr;
	int8    rssi_low_threshold;
	int8    rssi_high_threshold;
} wl_pfn_significant_bssid_t;
#define WL_PFN_SUPPRESSFOUND_MASK	0x08
#define WL_PFN_SUPPRESSLOST_MASK	0x10
#define WL_PFN_RSSI_MASK		0xff00
#define WL_PFN_RSSI_SHIFT		8

typedef struct wl_pfn_cfg {
	uint32	reporttype;
	int32	channel_num;
	uint16	channel_list[WL_NUMCHANNELS];
	uint32	flags;
} wl_pfn_cfg_t;

#define CH_BUCKET_REPORT_REGULAR                0
#define CH_BUCKET_REPORT_FULL_RESULT            2
#define CH_BUCKET_GSCAN                         4


typedef struct wl_pfn_gscan_channel_bucket {
	uint16 bucket_end_index;
	uint8 bucket_freq_multiple;
	uint8 report_flag;
} wl_pfn_gscan_channel_bucket_t;

#define GSCAN_SEND_ALL_RESULTS_MASK    (1 << 0)
#define GSCAN_CFG_FLAGS_ONLY_MASK      (1 << 7)

typedef struct wl_pfn_gscan_cfg {
	/* BIT0 1 = send probes/beacons to HOST
	* BIT2 Reserved
	 * Add any future flags here
	 * BIT7 1 = no other useful cfg sent
	 */
	uint8 flags;
	/* Buffer filled threshold in % to generate an event */
	uint8   buffer_threshold;
	/* No. of BSSIDs with "change" to generate an evt
	 * change - crosses rssi threshold/lost
	 */
	uint8   swc_nbssid_threshold;
	/* Max=8 (for now) Size of rssi cache buffer */
	uint8  swc_rssi_window_size;
	uint16  count_of_channel_buckets;
	uint16  lost_ap_window;
	wl_pfn_gscan_channel_bucket_t channel_bucket[1];
} wl_pfn_gscan_cfg_t;


#define WL_PFN_REPORT_ALLNET    0
#define WL_PFN_REPORT_SSIDNET   1
#define WL_PFN_REPORT_BSSIDNET  2
#define WL_PFN_CFG_FLAGS_PROHIBITED	0x00000001	/* Accept and use prohibited channels */
#define WL_PFN_CFG_FLAGS_RESERVED	0xfffffffe	/* Remaining reserved for future use */

typedef struct wl_pfn {
	wlc_ssid_t		ssid;			/* ssid name and its length */
	int32			flags;			/* bit2: hidden */
	int32			infra;			/* BSS Vs IBSS */
	int32			auth;			/* Open Vs Closed */
	int32			wpa_auth;		/* WPA type */
	int32			wsec;			/* wsec value */
} wl_pfn_t;

typedef struct wl_pfn_list {
	uint32		version;
	uint32		enabled;
	uint32		count;
	wl_pfn_t	pfn[1];
} wl_pfn_list_t;

#define WL_PFN_MAC_OUI_ONLY_MASK      1
#define WL_PFN_SET_MAC_UNASSOC_MASK   2
/* To configure pfn_macaddr */
typedef struct wl_pfn_macaddr_cfg {
	uint8 version;
	uint8 flags;
	struct ether_addr macaddr;
} wl_pfn_macaddr_cfg_t;
#define WL_PFN_MACADDR_CFG_VER 1
typedef BWL_PRE_PACKED_STRUCT struct pfn_olmsg_params_t {
	wlc_ssid_t ssid;
	uint32	cipher_type;
	uint32	auth_type;
	uint8	channels[4];
} BWL_POST_PACKED_STRUCT pfn_olmsg_params;

#define WL_PFN_HIDDEN_BIT		2
#define WL_PFN_HIDDEN_MASK		0x4

#ifndef BESTN_MAX
#define BESTN_MAX			3
#endif

#ifndef MSCAN_MAX
#define MSCAN_MAX			90
#endif

/*
 * WLFCTS definition
 */
typedef struct wl_txstatus_additional_info {
	uint32 rspec;
	uint32 enq_ts;
	uint32 last_ts;
	uint32 entry_ts;
	uint16 seq;
	uint8  rts_cnt;
	uint8  tx_cnt;
} wl_txstatus_additional_info_t;

/* Service discovery */
typedef struct {
	uint8	transaction_id;	/* Transaction id */
	uint8	protocol;	/* Service protocol type */
	uint16	query_len;	/* Length of query */
	uint16	response_len;	/* Length of response */
	uint8	qrbuf[1];
} wl_p2po_qr_t;

typedef struct {
	uint16			period;			/* extended listen period */
	uint16			interval;		/* extended listen interval */
	uint16			count;			/* count to repeat */
	uint16                  pad;                    /* pad for 32bit align */
} wl_p2po_listen_t;

/* GAS state machine tunable parameters.  Structure field values of 0 means use the default. */
typedef struct wl_gas_config {
	uint16 max_retransmit;		/* Max # of firmware/driver retransmits on no Ack
					 * from peer (on top of the ucode retries).
					 */
	uint16 response_timeout;	/* Max time to wait for a GAS-level response
					 * after sending a packet.
					 */
	uint16 max_comeback_delay;	/* Max GAS response comeback delay.
					 * Exceeding this fails the GAS exchange.
					 */
	uint16 max_retries;		/* Max # of GAS state machine retries on failure
					 * of a GAS frame exchange.
					 */
} wl_gas_config_t;

/* P2P Find Offload parameters */
typedef BWL_PRE_PACKED_STRUCT struct wl_p2po_find_config {
	uint16 version;			/* Version of this struct */
	uint16 length;			/* sizeof(wl_p2po_find_config_t) */
	int32 search_home_time;		/* P2P search state home time when concurrent
					 * connection exists.  -1 for default.
					 */
	uint8 num_social_channels;
			/* Number of social channels up to WL_P2P_SOCIAL_CHANNELS_MAX.
			 * 0 means use default social channels.
			 */
	uint8 flags;
	uint16 social_channels[1];	/* Variable length array of social channels */
} BWL_POST_PACKED_STRUCT wl_p2po_find_config_t;
#define WL_P2PO_FIND_CONFIG_VERSION 2	/* value for version field */

/* wl_p2po_find_config_t flags */
#define P2PO_FIND_FLAG_SCAN_ALL_APS 0x01	/* Whether to scan for all APs in the p2po_find
						 * periodic scans of all channels.
						 * 0 means scan for only P2P devices.
						 * 1 means scan for P2P devices plus non-P2P APs.
						 */


/* For adding a WFDS service to seek */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 seek_hdl;		/* unique id chosen by host */
	uint8 addr[6];			/* Seek service from a specific device with this
					 * MAC address, all 1's for any device.
					 */
	uint8 service_hash[P2P_WFDS_HASH_LEN];
	uint8 service_name_len;
	uint8 service_name[MAX_WFDS_SEEK_SVC_NAME_LEN];
					/* Service name to seek, not null terminated */
	uint8 service_info_req_len;
	uint8 service_info_req[1];	/* Service info request, not null terminated.
					 * Variable length specified by service_info_req_len.
					 * Maximum length is MAX_WFDS_SEEK_SVC_INFO_LEN.
					 */
} BWL_POST_PACKED_STRUCT wl_p2po_wfds_seek_add_t;

/* For deleting a WFDS service to seek */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 seek_hdl;		/* delete service specified by id */
} BWL_POST_PACKED_STRUCT wl_p2po_wfds_seek_del_t;


/* For adding a WFDS service to advertise */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 advertise_hdl;		/* unique id chosen by host */
	uint8 service_hash[P2P_WFDS_HASH_LEN];
	uint32 advertisement_id;
	uint16 service_config_method;
	uint8 service_name_len;
	uint8 service_name[MAX_WFDS_SVC_NAME_LEN];
					/* Service name , not null terminated */
	uint8 service_status;
	uint16 service_info_len;
	uint8 service_info[1];		/* Service info, not null terminated.
					 * Variable length specified by service_info_len.
					 * Maximum length is MAX_WFDS_ADV_SVC_INFO_LEN.
					 */
} BWL_POST_PACKED_STRUCT wl_p2po_wfds_advertise_add_t;

/* For deleting a WFDS service to advertise */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 advertise_hdl;	/* delete service specified by hdl */
} BWL_POST_PACKED_STRUCT wl_p2po_wfds_advertise_del_t;

/* P2P Offload discovery mode for the p2po_state iovar */
typedef enum {
	WL_P2PO_DISC_STOP,
	WL_P2PO_DISC_LISTEN,
	WL_P2PO_DISC_DISCOVERY
} disc_mode_t;

/* ANQP offload */

#define ANQPO_MAX_QUERY_SIZE		256
typedef struct {
	uint16 max_retransmit;		/* ~0 use default, max retransmit on no ACK from peer */
	uint16 response_timeout;	/* ~0 use default, msec to wait for resp after tx packet */
	uint16 max_comeback_delay;	/* ~0 use default, max comeback delay in resp else fail */
	uint16 max_retries;			/* ~0 use default, max retries on failure */
	uint16 query_len;			/* length of ANQP query */
	uint8 query_data[1];		/* ANQP encoded query (max ANQPO_MAX_QUERY_SIZE) */
} wl_anqpo_set_t;

typedef struct {
	uint16 channel;				/* channel of the peer */
	struct ether_addr addr;		/* addr of the peer */
} wl_anqpo_peer_t;

#define ANQPO_MAX_PEER_LIST			64
typedef struct {
	uint16 count;				/* number of peers in list */
	wl_anqpo_peer_t peer[1];	/* max ANQPO_MAX_PEER_LIST */
} wl_anqpo_peer_list_t;

#define ANQPO_MAX_IGNORE_SSID		64
typedef struct {
	bool is_clear;				/* set to clear list (not used on GET) */
	uint16 count;				/* number of SSID in list */
	wlc_ssid_t ssid[1];			/* max ANQPO_MAX_IGNORE_SSID */
} wl_anqpo_ignore_ssid_list_t;

#define ANQPO_MAX_IGNORE_BSSID		64
typedef struct {
	bool is_clear;				/* set to clear list (not used on GET) */
	uint16 count;				/* number of addr in list */
	struct ether_addr bssid[1];	/* max ANQPO_MAX_IGNORE_BSSID */
} wl_anqpo_ignore_bssid_list_t;


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

#define MAX_WAKE_PACKET_CACHE_BYTES 128 /* Maximum cached wake packet */

#define MAX_WAKE_PACKET_BYTES	    (DOT11_A3_HDR_LEN +			    \
				     DOT11_QOS_LEN +			    \
				     sizeof(struct dot11_llc_snap_header) + \
				     ETHER_MAX_DATA)

typedef struct pm_wake_packet {
	uint32	status;		/* Is the wake reason a packet (if all the other field's valid) */
	uint32	pattern_id;	/* Pattern ID that matched */
	uint32	original_packet_size;
	uint32	saved_packet_size;
	uchar	packet[MAX_WAKE_PACKET_CACHE_BYTES];
} pm_wake_packet_t;

/* Packet filter types. Currently, only pattern matching is supported. */
typedef enum wl_pkt_filter_type {
	WL_PKT_FILTER_TYPE_PATTERN_MATCH=0,	/* Pattern matching filter */
	WL_PKT_FILTER_TYPE_MAGIC_PATTERN_MATCH=1, /* Magic packet match */
	WL_PKT_FILTER_TYPE_PATTERN_LIST_MATCH=2, /* A pattern list (match all to match filter) */
	WL_PKT_FILTER_TYPE_ENCRYPTED_PATTERN_MATCH=3, /* SECURE WOWL magic / net pattern match */
} wl_pkt_filter_type_t;

#define WL_PKT_FILTER_TYPE wl_pkt_filter_type_t

/* String mapping for types that may be used by applications or debug */
#define WL_PKT_FILTER_TYPE_NAMES \
	{ "PATTERN", WL_PKT_FILTER_TYPE_PATTERN_MATCH },       \
	{ "MAGIC",   WL_PKT_FILTER_TYPE_MAGIC_PATTERN_MATCH }, \
	{ "PATLIST", WL_PKT_FILTER_TYPE_PATTERN_LIST_MATCH }

/* Secured WOWL packet was encrypted, need decrypted before check filter match */
typedef struct wl_pkt_decrypter {
	uint8* (*dec_cb)(void* dec_ctx, const void *sdu, int sending);
	void*  dec_ctx;
} wl_pkt_decrypter_t;

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
				      * at offset 0.  Pattern immediately follows mask. for
				      * secured pattern, put the descrypter pointer to the
				      * beginning, mask and pattern postponed correspondingly
				      */
} wl_pkt_filter_pattern_t;

/* A pattern list is a numerically specified list of modified pattern structures. */
typedef struct wl_pkt_filter_pattern_listel {
	uint16 rel_offs;	/* Offset to begin match (relative to 'base' below) */
	uint16 base_offs;	/* Base for offset (defined below) */
	uint16 size_bytes;	/* Size of mask/pattern */
	uint16 match_flags;	/* Addition flags controlling the match */
	uint8  mask_and_data[1]; /* Variable length mask followed by data, each size_bytes */
} wl_pkt_filter_pattern_listel_t;

typedef struct wl_pkt_filter_pattern_list {
	uint8 list_cnt;		/* Number of elements in the list */
	uint8 PAD1[1];		/* Reserved (possible version: reserved) */
	uint16 totsize;		/* Total size of this pattern list (includes this struct) */
	wl_pkt_filter_pattern_listel_t patterns[1]; /* Variable number of list elements */
} wl_pkt_filter_pattern_list_t;

/* IOVAR "pkt_filter_add" parameter. Used to install packet filters. */
typedef struct wl_pkt_filter {
	uint32	id;		/* Unique filter id, specified by app. */
	uint32	type;		/* Filter type (WL_PKT_FILTER_TYPE_xxx). */
	uint32	negate_match;	/* Negate the result of filter matches */
	union {			/* Filter definitions */
		wl_pkt_filter_pattern_t pattern;	/* Pattern matching filter */
		wl_pkt_filter_pattern_list_t patlist; /* List of patterns to match */
	} u;
} wl_pkt_filter_t;

/* IOVAR "tcp_keep_set" parameter. Used to install tcp keep_alive stuff. */
typedef struct wl_tcp_keep_set {
	uint32	val1;
	uint32	val2;
} wl_tcp_keep_set_t;

#define WL_PKT_FILTER_FIXED_LEN		  OFFSETOF(wl_pkt_filter_t, u)
#define WL_PKT_FILTER_PATTERN_FIXED_LEN	  OFFSETOF(wl_pkt_filter_pattern_t, mask_and_pattern)
#define WL_PKT_FILTER_PATTERN_LIST_FIXED_LEN OFFSETOF(wl_pkt_filter_pattern_list_t, patterns)
#define WL_PKT_FILTER_PATTERN_LISTEL_FIXED_LEN	\
			OFFSETOF(wl_pkt_filter_pattern_listel_t, mask_and_data)

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

/* IOVAR "pkt_filter_ports" parameter.  Configure TCP/UDP port filters. */
typedef struct wl_pkt_filter_ports {
	uint8 version;		/* Be proper */
	uint8 reserved;		/* Be really proper */
	uint16 count;		/* Number of ports following */
	/* End of fixed data */
	uint16 ports[1];	/* Placeholder for ports[<count>] */
} wl_pkt_filter_ports_t;

#define WL_PKT_FILTER_PORTS_FIXED_LEN	OFFSETOF(wl_pkt_filter_ports_t, ports)

#define WL_PKT_FILTER_PORTS_VERSION	0
#define WL_PKT_FILTER_PORTS_MAX		128

#define RSN_REPLAY_LEN 8
typedef struct _gtkrefresh {
	uchar	KCK[RSN_KCK_LENGTH];
	uchar	KEK[RSN_KEK_LENGTH];
	uchar	ReplayCounter[RSN_REPLAY_LEN];
} gtk_keyinfo_t, *pgtk_keyinfo_t;

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

typedef struct wl_pkteng {
	uint32 flags;
	uint32 delay;			/* Inter-packet delay */
	uint32 nframes;			/* Number of frames */
	uint32 length;			/* Packet length */
	uint8  seqno;			/* Enable/disable sequence no. */
	struct ether_addr dest;		/* Destination address */
	struct ether_addr src;		/* Source address */
} wl_pkteng_t;

typedef struct wl_pkteng_stats {
	uint32 lostfrmcnt;		/* RX PER test: no of frames lost (skip seqno) */
	int32 rssi;			/* RSSI */
	int32 snr;			/* signal to noise ratio */
	uint16 rxpktcnt[NUM_80211_RATES+1];
	uint8 rssi_qdb;			/* qdB portion of the computed rssi */
} wl_pkteng_stats_t;

typedef struct wl_txcal_params {
	wl_pkteng_t pkteng;
	uint8 gidx_start;
	int8 gidx_step;
	uint8 gidx_stop;
} wl_txcal_params_t;


typedef enum {
	wowl_pattern_type_bitmap = 0,
	wowl_pattern_type_arp,
	wowl_pattern_type_na
} wowl_pattern_type_t;

typedef struct wl_wowl_pattern {
	uint32		    masksize;		/* Size of the mask in #of bytes */
	uint32		    offset;		/* Pattern byte offset in packet */
	uint32		    patternoffset;	/* Offset of start of pattern in the structure */
	uint32		    patternsize;	/* Size of the pattern itself in #of bytes */
	uint32		    id;			/* id */
	uint32		    reasonsize;		/* Size of the wakeup reason code */
	wowl_pattern_type_t type;		/* Type of pattern */
	/* Mask follows the structure above */
	/* Pattern follows the mask is at 'patternoffset' from the start */
} wl_wowl_pattern_t;

typedef struct wl_wowl_pattern_list {
	uint			count;
	wl_wowl_pattern_t	pattern[1];
} wl_wowl_pattern_list_t;

typedef struct wl_wowl_wakeind {
	uint8	pci_wakeind;	/* Whether PCI PMECSR PMEStatus bit was set */
	uint32	ucode_wakeind;	/* What wakeup-event indication was set by ucode */
} wl_wowl_wakeind_t;

typedef struct {
	uint32		pktlen;		    /* size of packet */
	void		*sdu;
} tcp_keepalive_wake_pkt_infop_t;

/* per AC rate control related data structure */
typedef struct wl_txrate_class {
	uint8		init_rate;
	uint8		min_rate;
	uint8		max_rate;
} wl_txrate_class_t;

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

/* CCA based channel quality event configuration */
#define WL_CHAN_QUAL_CCA	0
#define WL_CHAN_QUAL_NF		1
#define WL_CHAN_QUAL_NF_LTE	2
#define WL_CHAN_QUAL_TOTAL	3

#define MAX_CHAN_QUAL_LEVELS	8

typedef struct wl_chan_qual_metric {
	uint8 id;				/* metric ID */
	uint8 num_levels;               	/* Number of entries in rssi_levels[] below */
	uint16 flags;
	int16 htol[MAX_CHAN_QUAL_LEVELS];	/* threshold level array: hi-to-lo */
	int16 ltoh[MAX_CHAN_QUAL_LEVELS];	/* threshold level array: lo-to-hi */
} wl_chan_qual_metric_t;

typedef struct wl_chan_qual_event {
	uint32 rate_limit_msec;		/* # of events posted to application will be limited to
					 * one per specified period (0 to disable rate limit).
					 */
	uint16 flags;
	uint16 num_metrics;
	wl_chan_qual_metric_t metric[WL_CHAN_QUAL_TOTAL];	/* metric array */
} wl_chan_qual_event_t;

typedef struct wl_action_obss_coex_req {
	uint8 info;
	uint8 num;
	uint8 ch_list[1];
} wl_action_obss_coex_req_t;


/* IOVar parameter block for small MAC address array with type indicator */
#define WL_IOV_MAC_PARAM_LEN  4

#define WL_IOV_PKTQ_LOG_PRECS 16

typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 num_addrs;
	char   addr_type[WL_IOV_MAC_PARAM_LEN];
	struct ether_addr ea[WL_IOV_MAC_PARAM_LEN];
} BWL_POST_PACKED_STRUCT wl_iov_mac_params_t;

/* This is extra info that follows wl_iov_mac_params_t */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 addr_info[WL_IOV_MAC_PARAM_LEN];
} BWL_POST_PACKED_STRUCT wl_iov_mac_extra_params_t;

/* Combined structure */
typedef struct {
	wl_iov_mac_params_t params;
	wl_iov_mac_extra_params_t extra_params;
} wl_iov_mac_full_params_t;

/* Parameter block for PKTQ_LOG statistics */
#define PKTQ_LOG_COUNTERS_V4 \
	/* packets requested to be stored */ \
	uint32 requested; \
	/* packets stored */ \
	uint32 stored; \
	/* packets saved, because a lowest priority queue has given away one packet */ \
	uint32 saved; \
	/* packets saved, because an older packet from the same queue has been dropped */ \
	uint32 selfsaved; \
	/* packets dropped, because pktq is full with higher precedence packets */ \
	uint32 full_dropped; \
	 /* packets dropped because pktq per that precedence is full */ \
	uint32 dropped; \
	/* packets dropped, in order to save one from a queue of a highest priority */ \
	uint32 sacrificed; \
	/* packets droped because of hardware/transmission error */ \
	uint32 busy; \
	/* packets re-sent because they were not received */ \
	uint32 retry; \
	/* packets retried again (ps pretend) prior to moving power save mode */ \
	uint32 ps_retry; \
	 /* suppressed packet count */ \
	uint32 suppress; \
	/* packets finally dropped after retry limit */ \
	uint32 retry_drop; \
	/* the high-water mark of the queue capacity for packets - goes to zero as queue fills */ \
	uint32 max_avail; \
	/* the high-water mark of the queue utilisation for packets - ('inverse' of max_avail) */ \
	uint32 max_used; \
	 /* the maximum capacity of the queue */ \
	uint32 queue_capacity; \
	/* count of rts attempts that failed to receive cts */ \
	uint32 rtsfail; \
	/* count of packets sent (acked) successfully */ \
	uint32 acked; \
	/* running total of phy rate of packets sent successfully */ \
	uint32 txrate_succ; \
	/* running total of phy 'main' rate */ \
	uint32 txrate_main; \
	/* actual data transferred successfully */ \
	uint32 throughput; \
	/* time difference since last pktq_stats */ \
	uint32 time_delta;

typedef struct {
	PKTQ_LOG_COUNTERS_V4
} pktq_log_counters_v04_t;

/* v5 is the same as V4 with extra parameter */
typedef struct {
	PKTQ_LOG_COUNTERS_V4
	/* cumulative time to transmit */
	uint32 airtime;
} pktq_log_counters_v05_t;

typedef struct {
	uint8                num_prec[WL_IOV_MAC_PARAM_LEN];
	pktq_log_counters_v04_t  counters[WL_IOV_MAC_PARAM_LEN][WL_IOV_PKTQ_LOG_PRECS];
	uint32               counter_info[WL_IOV_MAC_PARAM_LEN];
	uint32               pspretend_time_delta[WL_IOV_MAC_PARAM_LEN];
	char                 headings[1];
} pktq_log_format_v04_t;

typedef struct {
	uint8                num_prec[WL_IOV_MAC_PARAM_LEN];
	pktq_log_counters_v05_t  counters[WL_IOV_MAC_PARAM_LEN][WL_IOV_PKTQ_LOG_PRECS];
	uint32               counter_info[WL_IOV_MAC_PARAM_LEN];
	uint32               pspretend_time_delta[WL_IOV_MAC_PARAM_LEN];
	char                 headings[1];
} pktq_log_format_v05_t;


typedef struct {
	uint32               version;
	wl_iov_mac_params_t  params;
	union {
		pktq_log_format_v04_t v04;
		pktq_log_format_v05_t v05;
	} pktq_log;
} wl_iov_pktq_log_t;

/* PKTQ_LOG_AUTO, PKTQ_LOG_DEF_PREC flags introduced in v05, they are ignored by v04 */
#define PKTQ_LOG_AUTO     (1 << 31)
#define PKTQ_LOG_DEF_PREC (1 << 30)


#define LEGACY1_WL_PFN_MACADDR_CFG_VER 0

#define WL_PFN_MAC_OUI_ONLY_MASK      1
#define WL_PFN_SET_MAC_UNASSOC_MASK   2
#define WL_PFN_RESTRICT_LA_MAC_MASK   4
#define WL_PFN_MACADDR_FLAG_MASK     0x7


/*
 * SCB_BS_DATA iovar definitions start.
 */
#define SCB_BS_DATA_STRUCT_VERSION	1

/* The actual counters maintained for each station */
typedef BWL_PRE_PACKED_STRUCT struct {
	/* The following counters are a subset of what pktq_stats provides per precedence. */
	uint32 retry;          /* packets re-sent because they were not received */
	uint32 retry_drop;     /* packets finally dropped after retry limit */
	uint32 rtsfail;        /* count of rts attempts that failed to receive cts */
	uint32 acked;          /* count of packets sent (acked) successfully */
	uint32 txrate_succ;    /* running total of phy rate of packets sent successfully */
	uint32 txrate_main;    /* running total of phy 'main' rate */
	uint32 throughput;     /* actual data transferred successfully */
	uint32 time_delta;     /* time difference since last pktq_stats */
	uint32 airtime;        /* cumulative total medium access delay in useconds */
} BWL_POST_PACKED_STRUCT iov_bs_data_counters_t;

/* The structure for individual station information. */
typedef BWL_PRE_PACKED_STRUCT struct {
	struct ether_addr	station_address;	/* The station MAC address */
	uint16			station_flags;		/* Bit mask of flags, for future use. */
	iov_bs_data_counters_t	station_counters;	/* The actual counter values */
} BWL_POST_PACKED_STRUCT iov_bs_data_record_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	uint16	structure_version;	/* Structure version number (for wl/wlu matching) */
	uint16	structure_count;	/* Number of iov_bs_data_record_t records following */
	iov_bs_data_record_t	structure_record[1];	/* 0 - structure_count records */
} BWL_POST_PACKED_STRUCT iov_bs_data_struct_t;

/* Bitmask of options that can be passed in to the iovar. */
enum {
	SCB_BS_DATA_FLAG_NO_RESET = (1<<0)	/* Do not clear the counters after reading */
};
/*
 * SCB_BS_DATA iovar definitions end.
 */

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
#endif /* DONGLEOVERLAYS */

/* 11k Neighbor Report element (unversioned, deprecated) */
typedef struct nbr_element {
	uint8 id;
	uint8 len;
	struct ether_addr bssid;
	uint32 bssid_info;
	uint8 reg;
	uint8 channel;
	uint8 phytype;
	uint8 pad;
} nbr_element_t;

#define WL_RRM_NBR_RPT_VER		1
/* 11k Neighbor Report element */
typedef struct nbr_rpt_elem {
	uint8 version;
	uint8 id;
	uint8 len;
	uint8 pad;
	struct ether_addr bssid;
	uint8 pad_1[2];
	uint32 bssid_info;
	uint8 reg;
	uint8 channel;
	uint8 phytype;
	uint8 pad_2;
	wlc_ssid_t ssid;
	uint8 bss_trans_preference;
	uint8 pad_3[3];
} nbr_rpt_elem_t;

typedef enum event_msgs_ext_command {
	EVENTMSGS_NONE		=	0,
	EVENTMSGS_SET_BIT	=	1,
	EVENTMSGS_RESET_BIT	=	2,
	EVENTMSGS_SET_MASK	=	3
} event_msgs_ext_command_t;

#define EVENTMSGS_VER 1
#define EVENTMSGS_EXT_STRUCT_SIZE	OFFSETOF(eventmsgs_ext_t, mask[0])

/* len-	for SET it would be mask size from the application to the firmware */
/*		for GET it would be actual firmware mask size */
/* maxgetsize -	is only used for GET. indicate max mask size that the */
/*				application can read from the firmware */
typedef struct eventmsgs_ext
{
	uint8	ver;
	uint8	command;
	uint8	len;
	uint8	maxgetsize;
	uint8	mask[1];
} eventmsgs_ext_t;

typedef BWL_PRE_PACKED_STRUCT struct pcie_bus_tput_params {
	/* no of host dma descriptors programmed by the firmware before a commit */
	uint16		max_dma_descriptors;

	uint16		host_buf_len; /* length of host buffer */
	dmaaddr_t	host_buf_addr; /* physical address for bus_throughput_buf */
} BWL_POST_PACKED_STRUCT pcie_bus_tput_params_t;
typedef BWL_PRE_PACKED_STRUCT struct pcie_bus_tput_stats {
	uint16		time_taken; /* no of secs the test is run */
	uint16		nbytes_per_descriptor; /* no of bytes of data dma ed per descriptor */

	/* no of desciptors fo which dma is sucessfully completed within the test time */
	uint32		count;
} BWL_POST_PACKED_STRUCT pcie_bus_tput_stats_t;

#define MAX_ROAMOFFL_BSSID_NUM	100

typedef BWL_PRE_PACKED_STRUCT struct roamoffl_bssid_list {
	int32 cnt;
	struct ether_addr bssid[1];
} BWL_POST_PACKED_STRUCT roamoffl_bssid_list_t;

/* no default structure packing */
#include <packed_section_end.h>

typedef struct keepalives_max_idle {
	uint16  keepalive_count;        /* nmbr of keepalives per bss_max_idle period */
	uint8   mkeepalive_index;       /* mkeepalive_index for keepalive frame to be used */
	uint8   PAD;			/* to align next field */
	uint16  max_interval;           /* seconds */
} keepalives_max_idle_t;

#define PM_IGNORE_BCMC_PROXY_ARP (1 << 0)
#define PM_IGNORE_BCMC_ALL_DMS_ACCEPTED (1 << 1)

/* require strict packing */
#include <packed_section_start.h>

/* ##### Power Stats section ##### */

#define WL_PWRSTATS_VERSION	2

/* Input structure for pwrstats IOVAR */
typedef BWL_PRE_PACKED_STRUCT struct wl_pwrstats_query {
	uint16 length;		/* Number of entries in type array. */
	uint16 type[1];		/* Types (tags) to retrieve.
				 * Length 0 (no types) means get all.
				 */
} BWL_POST_PACKED_STRUCT wl_pwrstats_query_t;

/* This structure is for version 2; version 1 will be deprecated in by FW */
typedef BWL_PRE_PACKED_STRUCT struct wl_pwrstats {
	uint16 version;		      /* Version = 2 is TLV format */
	uint16 length;		      /* Length of entire structure */
	uint8 data[1];		      /* TLV data, a series of structures,
				       * each starting with type and length.
				       *
				       * Padded as necessary so each section
				       * starts on a 4-byte boundary.
				       *
				       * Both type and len are uint16, but the
				       * upper nibble of length is reserved so
				       * valid len values are 0-4095.
				       */
} BWL_POST_PACKED_STRUCT wl_pwrstats_t;
#define WL_PWR_STATS_HDRLEN	OFFSETOF(wl_pwrstats_t, data)

/* Type values for the data section */
#define WL_PWRSTATS_TYPE_PHY		0 /* struct wl_pwr_phy_stats */
#define WL_PWRSTATS_TYPE_SCAN		1 /* struct wl_pwr_scan_stats */
#define WL_PWRSTATS_TYPE_USB_HSIC	2 /* struct wl_pwr_usb_hsic_stats */
#define WL_PWRSTATS_TYPE_PM_AWAKE1	3 /* struct wl_pwr_pm_awake_stats_v1 */
#define WL_PWRSTATS_TYPE_CONNECTION	4 /* struct wl_pwr_connect_stats; assoc and key-exch time */
#define WL_PWRSTATS_TYPE_PCIE		6 /* struct wl_pwr_pcie_stats */
#define WL_PWRSTATS_TYPE_PM_AWAKE2	7 /* struct wl_pwr_pm_awake_stats_v2 */

/* Bits for wake reasons */
#define WLC_PMD_WAKE_SET		0x1
#define WLC_PMD_PM_AWAKE_BCN		0x2
#define WLC_PMD_BTA_ACTIVE		0x4
#define WLC_PMD_SCAN_IN_PROGRESS	0x8
#define WLC_PMD_RM_IN_PROGRESS		0x10
#define WLC_PMD_AS_IN_PROGRESS		0x20
#define WLC_PMD_PM_PEND			0x40
#define WLC_PMD_PS_POLL			0x80
#define WLC_PMD_CHK_UNALIGN_TBTT	0x100
#define WLC_PMD_APSD_STA_UP		0x200
#define WLC_PMD_TX_PEND_WAR		0x400
#define WLC_PMD_GPTIMER_STAY_AWAKE	0x800
#define WLC_PMD_PM2_RADIO_SOFF_PEND	0x2000
#define WLC_PMD_NON_PRIM_STA_UP		0x4000
#define WLC_PMD_AP_UP			0x8000

typedef BWL_PRE_PACKED_STRUCT struct wlc_pm_debug {
	uint32 timestamp;	     /* timestamp in millisecond */
	uint32 reason;		     /* reason(s) for staying awake */
} BWL_POST_PACKED_STRUCT wlc_pm_debug_t;

/* WL_PWRSTATS_TYPE_PM_AWAKE1 structures (for 6.25 firmware) */
#define WLC_STA_AWAKE_STATES_MAX_V1	30
#define WLC_PMD_EVENT_MAX_V1		32
/* Data sent as part of pwrstats IOVAR (and EXCESS_PM_WAKE event) */
typedef BWL_PRE_PACKED_STRUCT struct pm_awake_data_v1 {
	uint32 curr_time;	/* ms */
	uint32 hw_macc;		/* HW maccontrol */
	uint32 sw_macc;		/* SW maccontrol */
	uint32 pm_dur;		/* Total sleep time in PM, msecs */
	uint32 mpc_dur;		/* Total sleep time in MPC, msecs */

	/* int32 drifts = remote - local; +ve drift => local-clk slow */
	int32 last_drift;	/* Most recent TSF drift from beacon */
	int32 min_drift;	/* Min TSF drift from beacon in magnitude */
	int32 max_drift;	/* Max TSF drift from beacon in magnitude */

	uint32 avg_drift;	/* Avg TSF drift from beacon */

	/* Wake history tracking */
	uint8  pmwake_idx;				   /* for stepping through pm_state */
	wlc_pm_debug_t pm_state[WLC_STA_AWAKE_STATES_MAX_V1]; /* timestamped wake bits */
	uint32 pmd_event_wake_dur[WLC_PMD_EVENT_MAX_V1];      /* cumulative usecs per wake reason */
	uint32 drift_cnt;	/* Count of drift readings over which avg_drift was computed */
} BWL_POST_PACKED_STRUCT pm_awake_data_v1_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_pwr_pm_awake_stats_v1 {
	uint16 type;	     /* WL_PWRSTATS_TYPE_PM_AWAKE */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */

	pm_awake_data_v1_t awake_data;
	uint32 frts_time;	/* Cumulative ms spent in frts since driver load */
	uint32 frts_end_cnt;	/* No of times frts ended since driver load */
} BWL_POST_PACKED_STRUCT wl_pwr_pm_awake_stats_v1_t;

/* WL_PWRSTATS_TYPE_PM_AWAKE2 structures */
/* Data sent as part of pwrstats IOVAR */
typedef BWL_PRE_PACKED_STRUCT struct pm_awake_data_v2 {
	uint32 curr_time;	/* ms */
	uint32 hw_macc;		/* HW maccontrol */
	uint32 sw_macc;		/* SW maccontrol */
	uint32 pm_dur;		/* Total sleep time in PM, msecs */
	uint32 mpc_dur;		/* Total sleep time in MPC, msecs */

	/* int32 drifts = remote - local; +ve drift => local-clk slow */
	int32 last_drift;	/* Most recent TSF drift from beacon */
	int32 min_drift;	/* Min TSF drift from beacon in magnitude */
	int32 max_drift;	/* Max TSF drift from beacon in magnitude */

	uint32 avg_drift;	/* Avg TSF drift from beacon */

	/* Wake history tracking */

	/* pmstate array (type wlc_pm_debug_t) start offset */
	uint16 pm_state_offset;
	/* pmstate number of array entries */
	uint16 pm_state_len;

	/* array (type uint32) start offset */
	uint16 pmd_event_wake_dur_offset;
	/* pmd_event_wake_dur number of array entries */
	uint16 pmd_event_wake_dur_len;

	uint32 drift_cnt;	/* Count of drift readings over which avg_drift was computed */
	uint8  pmwake_idx;	/* for stepping through pm_state */
	uint8  pad[3];
	uint32 frts_time;	/* Cumulative ms spent in frts since driver load */
	uint32 frts_end_cnt;	/* No of times frts ended since driver load */
} BWL_POST_PACKED_STRUCT pm_awake_data_v2_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_pwr_pm_awake_stats_v2 {
	uint16 type;	     /* WL_PWRSTATS_TYPE_PM_AWAKE */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */

	pm_awake_data_v2_t awake_data;
} BWL_POST_PACKED_STRUCT wl_pwr_pm_awake_stats_v2_t;

/* Original bus structure is for HSIC */
typedef BWL_PRE_PACKED_STRUCT struct bus_metrics {
	uint32 suspend_ct;	/* suspend count */
	uint32 resume_ct;	/* resume count */
	uint32 disconnect_ct;	/* disconnect count */
	uint32 reconnect_ct;	/* reconnect count */
	uint32 active_dur;	/* msecs in bus, usecs for user */
	uint32 suspend_dur;	/* msecs in bus, usecs for user */
	uint32 disconnect_dur;	/* msecs in bus, usecs for user */
} BWL_POST_PACKED_STRUCT bus_metrics_t;

/* Bus interface info for USB/HSIC */
typedef BWL_PRE_PACKED_STRUCT struct wl_pwr_usb_hsic_stats {
	uint16 type;	     /* WL_PWRSTATS_TYPE_USB_HSIC */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */

	bus_metrics_t hsic;	/* stats from hsic bus driver */
} BWL_POST_PACKED_STRUCT wl_pwr_usb_hsic_stats_t;

typedef BWL_PRE_PACKED_STRUCT struct pcie_bus_metrics {
	uint32 d3_suspend_ct;	/* suspend count */
	uint32 d0_resume_ct;	/* resume count */
	uint32 perst_assrt_ct;	/* PERST# assert count */
	uint32 perst_deassrt_ct;	/* PERST# de-assert count */
	uint32 active_dur;	/* msecs */
	uint32 d3_suspend_dur;	/* msecs */
	uint32 perst_dur;	/* msecs */
	uint32 l0_cnt;		/* L0 entry count */
	uint32 l0_usecs;	/* L0 duration in usecs */
	uint32 l1_cnt;		/* L1 entry count */
	uint32 l1_usecs;	/* L1 duration in usecs */
	uint32 l1_1_cnt;	/* L1_1ss entry count */
	uint32 l1_1_usecs;	/* L1_1ss duration in usecs */
	uint32 l1_2_cnt;	/* L1_2ss entry count */
	uint32 l1_2_usecs;	/* L1_2ss duration in usecs */
	uint32 l2_cnt;		/* L2 entry count */
	uint32 l2_usecs;	/* L2 duration in usecs */
	uint32 timestamp;	/* Timestamp on when stats are collected */
	uint32 num_h2d_doorbell;	/* # of doorbell interrupts - h2d */
	uint32 num_d2h_doorbell;	/* # of doorbell interrupts - d2h */
	uint32 num_submissions; /* # of submissions */
	uint32 num_completions; /* # of completions */
	uint32 num_rxcmplt;	/* # of rx completions */
	uint32 num_rxcmplt_drbl;	/* of drbl interrupts for rx complt. */
	uint32 num_txstatus;	/* # of tx completions */
	uint32 num_txstatus_drbl;	/* of drbl interrupts for tx complt. */
	uint32 ltr_active_ct;	/* # of times chip went to LTR ACTIVE */
	uint32 ltr_active_dur;	/* # of msecs chip was in LTR ACTIVE */
	uint32 ltr_sleep_ct;	/* # of times chip went to LTR SLEEP */
	uint32 ltr_sleep_dur;	/* # of msecs chip was in LTR SLEEP */
	uint32 deepsleep_count; /* # of times chip went to deepsleep */
	uint32 deepsleep_dur;   /* # of msecs chip was in deepsleep */
} BWL_POST_PACKED_STRUCT pcie_bus_metrics_t;

/* Bus interface info for PCIE */
typedef BWL_PRE_PACKED_STRUCT struct wl_pwr_pcie_stats {
	uint16 type;	     /* WL_PWRSTATS_TYPE_PCIE */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */
	pcie_bus_metrics_t pcie;	/* stats from pcie bus driver */
} BWL_POST_PACKED_STRUCT wl_pwr_pcie_stats_t;

/* Scan information history per category */
typedef BWL_PRE_PACKED_STRUCT struct scan_data {
	uint32 count;		/* Number of scans performed */
	uint32 dur;		/* Total time (in us) used */
} BWL_POST_PACKED_STRUCT scan_data_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_pwr_scan_stats {
	uint16 type;	     /* WL_PWRSTATS_TYPE_SCAN */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */

	/* Scan history */
	scan_data_t user_scans;	  /* User-requested scans: (i/e/p)scan */
	scan_data_t assoc_scans;  /* Scans initiated by association requests */
	scan_data_t roam_scans;	  /* Scans initiated by the roam engine */
	scan_data_t pno_scans[8]; /* For future PNO bucketing (BSSID, SSID, etc) */
	scan_data_t other_scans;  /* Scan engine usage not assigned to the above */
} BWL_POST_PACKED_STRUCT wl_pwr_scan_stats_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_pwr_connect_stats {
	uint16 type;	     /* WL_PWRSTATS_TYPE_SCAN */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */

	/* Connection (Association + Key exchange) data */
	uint32 count;	/* Number of connections performed */
	uint32 dur;		/* Total time (in ms) used */
} BWL_POST_PACKED_STRUCT wl_pwr_connect_stats_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_pwr_phy_stats {
	uint16 type;	    /* WL_PWRSTATS_TYPE_PHY */
	uint16 len;	    /* Up to 4K-1, top 4 bits are reserved */
	uint32 tx_dur;	    /* TX Active duration in us */
	uint32 rx_dur;	    /* RX Active duration in us */
} BWL_POST_PACKED_STRUCT wl_pwr_phy_stats_t;


/* ##### End of Power Stats section ##### */

/* IPV4 Arp offloads for ndis context */
BWL_PRE_PACKED_STRUCT struct hostip_id {
	struct ipv4_addr ipa;
	uint8 id;
} BWL_POST_PACKED_STRUCT;

/* Return values */
#define ND_REPLY_PEER		0x1	/* Reply was sent to service NS request from peer */
#define ND_REQ_SINK			0x2	/* Input packet should be discarded */
#define ND_FORCE_FORWARD	0X3	/* For the dongle to forward req to HOST */

/* Neighbor Solicitation Response Offload IOVAR param */
typedef BWL_PRE_PACKED_STRUCT struct nd_param {
	struct ipv6_addr	host_ip[2];
	struct ipv6_addr	solicit_ip;
	struct ipv6_addr	remote_ip;
	uint8	host_mac[ETHER_ADDR_LEN];
	uint32	offload_id;
} BWL_POST_PACKED_STRUCT nd_param_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_pfn_roam_thresh {
	uint32 pfn_alert_thresh; /* time in ms */
	uint32 roam_alert_thresh; /* time in ms */
} BWL_POST_PACKED_STRUCT wl_pfn_roam_thresh_t;


/* Reasons for wl_pmalert_t */
#define PM_DUR_EXCEEDED			(1<<0)
#define MPC_DUR_EXCEEDED		(1<<1)
#define ROAM_ALERT_THRESH_EXCEEDED	(1<<2)
#define PFN_ALERT_THRESH_EXCEEDED	(1<<3)
#define CONST_AWAKE_DUR_ALERT		(1<<4)
#define CONST_AWAKE_DUR_RECOVERY	(1<<5)

#define MIN_PM_ALERT_LEN 9

/* Data sent in EXCESS_PM_WAKE event */
#define WL_PM_ALERT_VERSION 3

#define MAX_P2P_BSS_DTIM_PRD 4

/* This structure is for version 3; version 2 will be deprecated in by FW */
typedef BWL_PRE_PACKED_STRUCT struct wl_pmalert {
	uint16 version;		/* Version = 3 is TLV format */
	uint16 length;		/* Length of entire structure */
	uint32 reasons;		/* reason(s) for pm_alert */
	uint8 data[1];		/* TLV data, a series of structures,
				 * each starting with type and length.
				 *
				 * Padded as necessary so each section
				 * starts on a 4-byte boundary.
				 *
				 * Both type and len are uint16, but the
				 * upper nibble of length is reserved so
				 * valid len values are 0-4095.
				*/
} BWL_POST_PACKED_STRUCT wl_pmalert_t;

/* Type values for the data section */
#define WL_PMALERT_FIXED	0 /* struct wl_pmalert_fixed_t, fixed fields */
#define WL_PMALERT_PMSTATE	1 /* struct wl_pmalert_pmstate_t, variable */
#define WL_PMALERT_EVENT_DUR	2 /* struct wl_pmalert_event_dur_t, variable */
#define WL_PMALERT_UCODE_DBG	3 /* struct wl_pmalert_ucode_dbg_t, variable */
#define WL_PMALERT_PS_ALLOWED_HIST	4 /* struct wl_pmalert_ps_allowed_history, variable */
#define WL_PMALERT_EXT_UCODE_DBG	5 /* struct wl_pmalert_ext_ucode_dbg_t, variable */
#define WL_PMALERT_EPM_START_EVENT_DUR	6 /* struct wl_pmalert_event_dur_t, variable */

typedef BWL_PRE_PACKED_STRUCT struct wl_pmalert_fixed {
	uint16 type;	     /* WL_PMALERT_FIXED */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */
	uint32 prev_stats_time;	/* msecs */
	uint32 curr_time;	/* ms */
	uint32 prev_pm_dur;	/* msecs */
	uint32 pm_dur;		/* Total sleep time in PM, msecs */
	uint32 prev_mpc_dur;	/* msecs */
	uint32 mpc_dur;		/* Total sleep time in MPC, msecs */
	uint32 hw_macc;		/* HW maccontrol */
	uint32 sw_macc;		/* SW maccontrol */

	/* int32 drifts = remote - local; +ve drift -> local-clk slow */
	int32 last_drift;	/* Most recent TSF drift from beacon */
	int32 min_drift;	/* Min TSF drift from beacon in magnitude */
	int32 max_drift;	/* Max TSF drift from beacon in magnitude */

	uint32 avg_drift;	/* Avg TSF drift from beacon */
	uint32 drift_cnt;	/* Count of drift readings over which avg_drift was computed */
	uint32 frts_time;	/* Cumulative ms spent in data frts since driver load */
	uint32 frts_end_cnt;	/* No of times frts ended since driver load */
	uint32 prev_frts_dur;	/* Data frts duration at start of pm-period */
	uint32 cal_dur;		/* Cumulative ms spent in calibration */
	uint32 prev_cal_dur;	/* cal duration at start of pm-period */
} BWL_POST_PACKED_STRUCT wl_pmalert_fixed_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_pmalert_pmstate {
	uint16 type;	     /* WL_PMALERT_PMSTATE */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */

	uint8 pmwake_idx;   /* for stepping through pm_state */
	uint8 pad[3];
	/* Array of pmstate; len of array is based on tlv len */
	wlc_pm_debug_t pmstate[1];
} BWL_POST_PACKED_STRUCT wl_pmalert_pmstate_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_pmalert_event_dur {
	uint16 type;	     /* WL_PMALERT_EVENT_DUR */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */

	/* Array of event_dur, len of array is based on tlv len */
	uint32 event_dur[1];
} BWL_POST_PACKED_STRUCT wl_pmalert_event_dur_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_pmalert_ucode_dbg {
	uint16 type;	     /* WL_PMALERT_UCODE_DBG */
	uint16 len;	     /* Up to 4K-1, top 4 bits are reserved */
	uint32 macctrl;
	uint16 m_p2p_hps;
	uint32 psm_brc;
	uint32 ifsstat;
	uint16 m_p2p_bss_dtim_prd[MAX_P2P_BSS_DTIM_PRD];
	uint32 psmdebug[20];
	uint32 phydebug[20];
} BWL_POST_PACKED_STRUCT wl_pmalert_ucode_dbg_t;


/* Structures and constants used for "vndr_ie" IOVar interface */
#define VNDR_IE_CMD_LEN		4	/* length of the set command string:
					 * "add", "del" (+ NUL)
					 */

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

typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 flags;
	chanspec_t chanspec;			/* txpwr report for this channel */
	chanspec_t local_chanspec;		/* channel on which we are associated */
	uint8 local_max;			/* local max according to the AP */
	uint8 local_constraint;			/* local constraint according to the AP */
	int8  antgain[2];			/* Ant gain for each band - from SROM */
	uint8 rf_cores;				/* count of RF Cores being reported */
	uint8 est_Pout[4];			/* Latest tx power out estimate per RF chain */
	uint8 est_Pout_act[4]; /* Latest tx power out estimate per RF chain w/o adjustment */
	uint8 est_Pout_cck;			/* Latest CCK tx power out estimate */
	uint8 tx_power_max[4];		/* Maximum target power among all rates */
	uint tx_power_max_rate_ind[4];		/* Index of the rate with the max target power */
	int8 sar;					/* SAR limit for display by wl executable */
	int8 channel_bandwidth;		/* 20, 40 or 80 MHz bandwidth? */
	uint8 version;				/* Version of the data format wlu <--> driver */
	uint8 display_core;			/* Displayed curpower core */
	int8 target_offsets[4];		/* Target power offsets for current rate per core */
	uint32 last_tx_ratespec;	/* Ratespec for last transmition */
	uint   user_target;		/* user limit */
	uint32 ppr_len;		/* length of each ppr serialization buffer */
	int8 SARLIMIT[MAX_STREAMS_SUPPORTED];
	uint8  pprdata[1];		/* ppr serialization buffer */
} BWL_POST_PACKED_STRUCT tx_pwr_rpt_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	struct ipv4_addr	ipv4_addr;
	struct ether_addr nexthop;
} BWL_POST_PACKED_STRUCT ibss_route_entry_t;
typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 num_entry;
	ibss_route_entry_t route_entry[1];
} BWL_POST_PACKED_STRUCT ibss_route_tbl_t;

#define MAX_IBSS_ROUTE_TBL_ENTRY	64

#define TXPWR_TARGET_VERSION  0
typedef BWL_PRE_PACKED_STRUCT struct {
	int32 version;		/* version number */
	chanspec_t chanspec;	/* txpwr report for this channel */
	int8 txpwr[WL_STA_ANT_MAX]; /* Max tx target power, in qdb */
	uint8 rf_cores;		/* count of RF Cores being reported */
} BWL_POST_PACKED_STRUCT txpwr_target_max_t;

#define BSS_PEER_INFO_PARAM_CUR_VER	0
/* Input structure for IOV_BSS_PEER_INFO */
typedef BWL_PRE_PACKED_STRUCT	struct {
	uint16			version;
	struct	ether_addr ea;	/* peer MAC address */
} BWL_POST_PACKED_STRUCT bss_peer_info_param_t;

#define BSS_PEER_INFO_CUR_VER		0

typedef BWL_PRE_PACKED_STRUCT struct {
	uint16			version;
	struct ether_addr	ea;
	int32			rssi;
	uint32			tx_rate;	/* current tx rate */
	uint32			rx_rate;	/* current rx rate */
	wl_rateset_t		rateset;	/* rateset in use */
	uint32			age;		/* age in seconds */
} BWL_POST_PACKED_STRUCT bss_peer_info_t;

#define BSS_PEER_LIST_INFO_CUR_VER	0

typedef BWL_PRE_PACKED_STRUCT struct {
	uint16			version;
	uint16			bss_peer_info_len;	/* length of bss_peer_info_t */
	uint32			count;			/* number of peer info */
	bss_peer_info_t		peer_info[1];		/* peer info */
} BWL_POST_PACKED_STRUCT bss_peer_list_info_t;

#define BSS_PEER_LIST_INFO_FIXED_LEN OFFSETOF(bss_peer_list_info_t, peer_info)

#define AIBSS_BCN_FORCE_CONFIG_VER_0	0

/* structure used to configure AIBSS beacon force xmit */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint16  version;
	uint16	len;
	uint32 initial_min_bcn_dur;	/* dur in ms to check a bcn in bcn_flood period */
	uint32 min_bcn_dur;	/* dur in ms to check a bcn after bcn_flood period */
	uint32 bcn_flood_dur; /* Initial bcn xmit period in ms */
} BWL_POST_PACKED_STRUCT aibss_bcn_force_config_t;

#define AIBSS_TXFAIL_CONFIG_VER_0    0
#define AIBSS_TXFAIL_CONFIG_VER_1    1
#define AIBSS_TXFAIL_CONFIG_CUR_VER		AIBSS_TXFAIL_CONFIG_VER_1

/* structure used to configure aibss tx fail event */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint16  version;
	uint16  len;
	uint32 bcn_timeout;     /* dur in seconds to receive 1 bcn */
	uint32 max_tx_retry;     /* no of consecutive no acks to send txfail event */
	uint32 max_atim_failure; /* no of consecutive atim failure */
} BWL_POST_PACKED_STRUCT aibss_txfail_config_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_aibss_if {
	uint16 version;
	uint16 len;
	uint32 flags;
	struct ether_addr addr;
	chanspec_t chspec;
} BWL_POST_PACKED_STRUCT wl_aibss_if_t;

typedef BWL_PRE_PACKED_STRUCT struct wlc_ipfo_route_entry {
	struct ipv4_addr ip_addr;
	struct ether_addr nexthop;
} BWL_POST_PACKED_STRUCT wlc_ipfo_route_entry_t;

typedef BWL_PRE_PACKED_STRUCT struct wlc_ipfo_route_tbl {
	uint32 num_entry;
	wlc_ipfo_route_entry_t route_entry[1];
} BWL_POST_PACKED_STRUCT wlc_ipfo_route_tbl_t;

#define WL_IPFO_ROUTE_TBL_FIXED_LEN 4
#define WL_MAX_IPFO_ROUTE_TBL_ENTRY	64

/* no strict structure packing */
#include <packed_section_end.h>

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


	/* chanim acs record */
	typedef struct {
		bool valid;
		uint8 trigger;
		chanspec_t selected_chspc;
		int8 bgnoise;
		uint32 glitch_cnt;
		uint8 ccastats;
		uint8 chan_idle;
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
		uint8 ccastats[CCASTATS_MAX];   /* normalized as 0-255 */
		int8 bgnoise;                   /* background noise level (in dBm) */
		chanspec_t chanspec;            /* ctrl chanspec of the interface */
		uint32 timestamp;               /* time stamp at which the stats are collected */
		uint32 bphy_glitchcnt;          /* normalized as per second count */
		uint32 bphy_badplcp;            /* normalized as per second count */
		uint8 chan_idle;                /* normalized as 0~255 */
	} chanim_stats_t;

#define WL_CHANIM_STATS_VERSION 2

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

typedef struct wl_p2p_sched {
	uint8 type;	/* see schedule type */
	uint8 action;	/* see schedule action */
	uint8 option;	/* see schedule option */
	wl_p2p_sched_desc_t desc[1];
} wl_p2p_sched_t;

typedef struct wl_p2p_wfds_hash {
	uint32	advt_id;
	uint16	nw_cfg_method;
	uint8	wfds_hash[6];
	uint8	name_len;
	uint8	service_name[MAX_WFDS_SVC_NAME_LEN];
} wl_p2p_wfds_hash_t;

typedef struct wl_bcmdcs_data {
	uint reason;
	chanspec_t chspec;
} wl_bcmdcs_data_t;


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

typedef struct {
	int state;	/* NAT state returned */
} nat_state_t;


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

enum {
	SPATIAL_MODE_2G_IDX = 0,
	SPATIAL_MODE_5G_LOW_IDX,
	SPATIAL_MODE_5G_MID_IDX,
	SPATIAL_MODE_5G_HIGH_IDX,
	SPATIAL_MODE_5G_UPPER_IDX,
	SPATIAL_MODE_MAX_IDX
};

#define WLC_TXCORE_MAX	4	/* max number of txcore supports */
#define WLC_SUBBAND_MAX	4	/* max number of sub-band supports */
typedef struct {
	uint8	band2g[WLC_TXCORE_MAX];
	uint8	band5g[WLC_SUBBAND_MAX][WLC_TXCORE_MAX];
} sar_limit_t;

#define WLC_TXCAL_CORE_MAX 2	/* max number of txcore supports for txcal */
#define MAX_NUM_TXCAL_MEAS 128
#define MAX_NUM_PWR_STEP 40
#define TXCAL_ROUNDING_FIX 1
typedef struct wl_txcal_meas {
#ifdef TXCAL_ROUNDING_FIX
	uint16 tssi[WLC_TXCAL_CORE_MAX][MAX_NUM_TXCAL_MEAS];
#else
	uint8 tssi[WLC_TXCAL_CORE_MAX][MAX_NUM_TXCAL_MEAS];
#endif /* TXCAL_ROUNDING_FIX */
	int16 pwr[WLC_TXCAL_CORE_MAX][MAX_NUM_TXCAL_MEAS];
	uint8 valid_cnt;
} wl_txcal_meas_t;

typedef struct wl_txcal_power_tssi {
	uint8 set_core;
	uint8 channel;
	int16 tempsense[WLC_TXCAL_CORE_MAX];
	int16 pwr_start[WLC_TXCAL_CORE_MAX];
	uint8 pwr_start_idx[WLC_TXCAL_CORE_MAX];
	uint8 num_entries[WLC_TXCAL_CORE_MAX];
	uint8 tssi[WLC_TXCAL_CORE_MAX][MAX_NUM_PWR_STEP];
	bool gen_tbl;
} wl_txcal_power_tssi_t;

/* IOVAR "mempool" parameter. Used to retrieve a list of memory pool statistics. */
typedef struct wl_mempool_stats {
	int	num;		/* Number of memory pools */
	bcm_mp_stats_t s[1];	/* Variable array of memory pool stats. */
} wl_mempool_stats_t;

typedef struct {
	uint32 ipaddr;
	uint32 ipaddr_netmask;
	uint32 ipaddr_gateway;
} nwoe_ifconfig_t;

/* Traffic management priority classes */
typedef enum trf_mgmt_priority_class {
	trf_mgmt_priority_low           = 0,        /* Maps to 802.1p BK */
	trf_mgmt_priority_medium        = 1,        /* Maps to 802.1p BE */
	trf_mgmt_priority_high          = 2,        /* Maps to 802.1p VI */
	trf_mgmt_priority_nochange	= 3,	    /* do not update the priority */
	trf_mgmt_priority_invalid       = (trf_mgmt_priority_nochange + 1)
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
	uint32                      dscp;                   /* DSCP */
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
typedef struct trf_mgmt_stats_array {
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

typedef struct lpc_params {
	/* LPC Params exposed via IOVAR */
	uint8		rate_stab_thresh; /* Thresh for rate stability based on nupd */
	uint8		pwr_stab_thresh; /* Number of successes before power step down */
	uint8		lpc_exp_time; /* Time lapse for expiry of database */
	uint8		pwrup_slow_step; /* Step size for slow step up */
	uint8		pwrup_fast_step; /* Step size for fast step up */
	uint8		pwrdn_slow_step; /* Step size for slow step down */
} lpc_params_t;

/* tx pkt delay statistics */
#define	SCB_RETRY_SHORT_DEF	7	/* Default Short retry Limit */
#define WLPKTDLY_HIST_NBINS	16	/* number of bins used in the Delay histogram */

/* structure to store per-AC delay statistics */
typedef struct scb_delay_stats {
	uint32 txmpdu_lost;	/* number of MPDUs lost */
	uint32 txmpdu_cnt[SCB_RETRY_SHORT_DEF]; /* retry times histogram */
	uint32 delay_sum[SCB_RETRY_SHORT_DEF]; /* cumulative packet latency */
	uint32 delay_min;	/* minimum packet latency observed */
	uint32 delay_max;	/* maximum packet latency observed */
	uint32 delay_avg;	/* packet latency average */
	uint32 delay_hist[WLPKTDLY_HIST_NBINS];	/* delay histogram */
} scb_delay_stats_t;

/* structure for txdelay event */
typedef struct txdelay_event {
	uint8	status;
	int		rssi;
	chanim_stats_t		chanim_stats;
	scb_delay_stats_t	delay_stats[AC_COUNT];
} txdelay_event_t;

/* structure for txdelay parameters */
typedef struct txdelay_params {
	uint16	ratio;	/* Avg Txdelay Delta */
	uint8	cnt;	/* Sample cnt */
	uint8	period;	/* Sample period */
	uint8	tune;	/* Debug */
} txdelay_params_t;

enum {
	WNM_SERVICE_DMS = 1,
	WNM_SERVICE_FMS = 2,
	WNM_SERVICE_TFS = 3
};

/* Definitions for WNM/NPS TCLAS */
typedef struct wl_tclas {
	uint8 user_priority;
	uint8 fc_len;
	dot11_tclas_fc_t fc;
} wl_tclas_t;

#define WL_TCLAS_FIXED_SIZE	OFFSETOF(wl_tclas_t, fc)

typedef struct wl_tclas_list {
	uint32 num;
	wl_tclas_t tclas[1];
} wl_tclas_list_t;

/* Definitions for WNM/NPS Traffic Filter Service */
typedef struct wl_tfs_req {
	uint8 tfs_id;
	uint8 tfs_actcode;
	uint8 tfs_subelem_id;
	uint8 send;
} wl_tfs_req_t;

typedef struct wl_tfs_filter {
	uint8 status;			/* Status returned by the AP */
	uint8 tclas_proc;		/* TCLAS processing value (0:and, 1:or)  */
	uint8 tclas_cnt;		/* count of all wl_tclas_t in tclas array */
	uint8 tclas[1];			/* VLA of wl_tclas_t */
} wl_tfs_filter_t;
#define WL_TFS_FILTER_FIXED_SIZE	OFFSETOF(wl_tfs_filter_t, tclas)

typedef struct wl_tfs_fset {
	struct ether_addr ea;		/* Address of AP/STA involved with this filter set */
	uint8 tfs_id;			/* TFS ID field chosen by STA host */
	uint8 status;			/* Internal status TFS_STATUS_xxx */
	uint8 actcode;			/* Action code DOT11_TFS_ACTCODE_xxx */
	uint8 token;			/* Token used in last request frame */
	uint8 notify;			/* Notify frame sent/received because of this set */
	uint8 filter_cnt;		/* count of all wl_tfs_filter_t in filter array */
	uint8 filter[1];		/* VLA of wl_tfs_filter_t */
} wl_tfs_fset_t;
#define WL_TFS_FSET_FIXED_SIZE		OFFSETOF(wl_tfs_fset_t, filter)

enum {
	TFS_STATUS_DISABLED = 0,	/* TFS filter set disabled by user */
	TFS_STATUS_DISABLING = 1,	/* Empty request just sent to AP */
	TFS_STATUS_VALIDATED = 2,	/* Filter set validated by AP (but maybe not enabled!) */
	TFS_STATUS_VALIDATING = 3,	/* Filter set just sent to AP */
	TFS_STATUS_NOT_ASSOC = 4,	/* STA not associated */
	TFS_STATUS_NOT_SUPPORT = 5,	/* TFS not supported by AP */
	TFS_STATUS_DENIED = 6,		/* Filter set refused by AP (=> all sets are disabled!) */
};

typedef struct wl_tfs_status {
	uint8 fset_cnt;			/* count of all wl_tfs_fset_t in fset array */
	wl_tfs_fset_t fset[1];		/* VLA of wl_tfs_fset_t */
} wl_tfs_status_t;

typedef struct wl_tfs_set {
	uint8 send;			/* Immediatly register registered sets on AP side */
	uint8 tfs_id;			/* ID of a specific set (existing or new), or nul for all */
	uint8 actcode;			/* Action code for this filter set */
	uint8 tclas_proc;		/* TCLAS processing operator for this filter set */
} wl_tfs_set_t;

typedef struct wl_tfs_term {
	uint8 del;			/* Delete internal set once confirmation received */
	uint8 tfs_id;			/* ID of a specific set (existing), or nul for all */
} wl_tfs_term_t;


#define DMS_DEP_PROXY_ARP (1 << 0)

/* Definitions for WNM/NPS Directed Multicast Service */
enum {
	DMS_STATUS_DISABLED = 0,	/* DMS desc disabled by user */
	DMS_STATUS_ACCEPTED = 1,	/* Request accepted by AP */
	DMS_STATUS_NOT_ASSOC = 2,	/* STA not associated */
	DMS_STATUS_NOT_SUPPORT = 3,	/* DMS not supported by AP */
	DMS_STATUS_DENIED = 4,		/* Request denied by AP */
	DMS_STATUS_TERM = 5,		/* Request terminated by AP */
	DMS_STATUS_REMOVING = 6,	/* Remove request just sent */
	DMS_STATUS_ADDING = 7,		/* Add request just sent */
	DMS_STATUS_ERROR = 8,		/* Non compliant AP behvior */
	DMS_STATUS_IN_PROGRESS = 9, /* Request just sent */
	DMS_STATUS_REQ_MISMATCH = 10 /* Conditions for sending DMS req not met */
};

typedef struct wl_dms_desc {
	uint8 user_id;
	uint8 status;
	uint8 token;
	uint8 dms_id;
	uint8 tclas_proc;
	uint8 mac_len;		/* length of all ether_addr in data array, 0 if STA */
	uint8 tclas_len;	/* length of all wl_tclas_t in data array */
	uint8 data[1];		/* VLA of 'ether_addr' and 'wl_tclas_t' (in this order ) */
} wl_dms_desc_t;

#define WL_DMS_DESC_FIXED_SIZE	OFFSETOF(wl_dms_desc_t, data)

typedef struct wl_dms_status {
	uint32 cnt;
	wl_dms_desc_t desc[1];
} wl_dms_status_t;

typedef struct wl_dms_set {
	uint8 send;
	uint8 user_id;
	uint8 tclas_proc;
} wl_dms_set_t;

typedef struct wl_dms_term {
	uint8 del;
	uint8 user_id;
} wl_dms_term_t;

typedef struct wl_service_term {
	uint8 service;
	union {
		wl_dms_term_t dms;
	} u;
} wl_service_term_t;

/* Definitions for WNM/NPS BSS Transistion */
typedef struct wl_bsstrans_req {
	uint16 tbtt;			/* time of BSS to end of life, in unit of TBTT */
	uint16 dur;			/* time of BSS to keep off, in unit of minute */
	uint8 reqmode;			/* request mode of BSS transition request */
	uint8 unicast;			/* request by unicast or by broadcast */
} wl_bsstrans_req_t;

enum {
	BSSTRANS_RESP_AUTO = 0,		/* Currently equivalent to ENABLE */
	BSSTRANS_RESP_DISABLE = 1,	/* Never answer BSS Trans Req frames */
	BSSTRANS_RESP_ENABLE = 2,	/* Always answer Req frames with preset data */
	BSSTRANS_RESP_WAIT = 3,		/* Send ind, wait and/or send preset data (NOT IMPL) */
	BSSTRANS_RESP_IMMEDIATE = 4	/* After an ind, set data and send resp (NOT IMPL) */
};

typedef struct wl_bsstrans_resp {
	uint8 policy;
	uint8 status;
	uint8 delay;
	struct ether_addr target;
} wl_bsstrans_resp_t;

/* "wnm_bsstrans_policy" argument programs behavior after BSSTRANS Req reception.
 * BSS-Transition feature is used by multiple programs such as NPS-PF, VE-PF,
 * Band-steering, Hotspot 2.0 and customer requirements. Each PF and its test plan
 * mandates different behavior on receiving BSS-transition request. To accomodate
 * such divergent behaviors these policies have been created.
 */
enum {
	WL_BSSTRANS_POLICY_ROAM_ALWAYS = 0,	/* Roam (or disassociate) in all cases */
	WL_BSSTRANS_POLICY_ROAM_IF_MODE = 1,	/* Roam only if requested by Request Mode field */
	WL_BSSTRANS_POLICY_ROAM_IF_PREF = 2,	/* Roam only if Preferred BSS provided */
	WL_BSSTRANS_POLICY_WAIT = 3,		/* Wait for deauth and send Accepted status */
	WL_BSSTRANS_POLICY_PRODUCT = 4,		/* Policy for real product use cases (non-pf) */
};

/* Definitions for WNM/NPS TIM Broadcast */
typedef struct wl_timbc_offset {
	int16 offset;		/* offset in us */
	uint16 fix_intv;	/* override interval sent from STA */
	uint16 rate_override;	/* use rate override to send high rate TIM broadcast frame */
	uint8 tsf_present;	/* show timestamp in TIM broadcast frame */
} wl_timbc_offset_t;

typedef struct wl_timbc_set {
	uint8 interval;		/* Interval in DTIM wished or required. */
	uint8 flags;		/* Bitfield described below */
	uint16 rate_min;	/* Minimum rate required for High/Low TIM frames. Optionnal */
	uint16 rate_max;	/* Maximum rate required for High/Low TIM frames. Optionnal */
} wl_timbc_set_t;

enum {
	WL_TIMBC_SET_TSF_REQUIRED = 1,	/* Enable TIMBC only if TSF in TIM frames */
	WL_TIMBC_SET_NO_OVERRIDE = 2,	/* ... if AP does not override interval */
	WL_TIMBC_SET_PROXY_ARP = 4,	/* ... if AP support Proxy ARP */
	WL_TIMBC_SET_DMS_ACCEPTED = 8	/* ... if all DMS desc have been accepted */
};

typedef struct wl_timbc_status {
	uint8 status_sta;		/* Status from internal state machine (check below) */
	uint8 status_ap;		/* From AP response frame (check 8.4.2.86 from 802.11) */
	uint8 interval;
	uint8 pad;
	int32 offset;
	uint16 rate_high;
	uint16 rate_low;
} wl_timbc_status_t;

enum {
	WL_TIMBC_STATUS_DISABLE = 0,		/* TIMBC disabled by user */
	WL_TIMBC_STATUS_REQ_MISMATCH = 1,	/* AP settings do no match user requirements */
	WL_TIMBC_STATUS_NOT_ASSOC = 2,		/* STA not associated */
	WL_TIMBC_STATUS_NOT_SUPPORT = 3,	/* TIMBC not supported by AP */
	WL_TIMBC_STATUS_DENIED = 4,		/* Req to disable TIMBC sent to AP */
	WL_TIMBC_STATUS_ENABLE = 5		/* TIMBC enabled */
};

/* Definitions for PM2 Dynamic Fast Return To Sleep */
typedef struct wl_pm2_sleep_ret_ext {
	uint8 logic;			/* DFRTS logic: see WL_DFRTS_LOGIC_* below */
	uint16 low_ms;			/* Low FRTS timeout */
	uint16 high_ms;			/* High FRTS timeout */
	uint16 rx_pkts_threshold;	/* switching threshold: # rx pkts */
	uint16 tx_pkts_threshold;	/* switching threshold: # tx pkts */
	uint16 txrx_pkts_threshold;	/* switching threshold: # (tx+rx) pkts */
	uint32 rx_bytes_threshold;	/* switching threshold: # rx bytes */
	uint32 tx_bytes_threshold;	/* switching threshold: # tx bytes */
	uint32 txrx_bytes_threshold;	/* switching threshold: # (tx+rx) bytes */
} wl_pm2_sleep_ret_ext_t;

#define WL_DFRTS_LOGIC_OFF	0	/* Feature is disabled */
#define WL_DFRTS_LOGIC_OR	1	/* OR all non-zero threshold conditions */
#define WL_DFRTS_LOGIC_AND	2	/* AND all non-zero threshold conditions */

/* Values for the passive_on_restricted_mode iovar.  When set to non-zero, this iovar
 * disables automatic conversions of a channel from passively scanned to
 * actively scanned.  These values only have an effect for country codes such
 * as XZ where some 5 GHz channels are defined to be passively scanned.
 */
#define WL_PASSACTCONV_DISABLE_NONE	0	/* Enable permanent and temporary conversions */
#define WL_PASSACTCONV_DISABLE_ALL	1	/* Disable permanent and temporary conversions */
#define WL_PASSACTCONV_DISABLE_PERM	2	/* Disable only permanent conversions */

/* Definitions for Reliable Multicast */
#define WL_RMC_CNT_VERSION	   1
#define WL_RMC_TR_VERSION	   1
#define WL_RMC_MAX_CLIENT	   32
#define WL_RMC_FLAG_INBLACKLIST	   1
#define WL_RMC_FLAG_ACTIVEACKER	   2
#define WL_RMC_FLAG_RELMCAST	   4
#define WL_RMC_MAX_TABLE_ENTRY     4

#define WL_RMC_VER		   1
#define WL_RMC_INDEX_ACK_ALL       255
#define WL_RMC_NUM_OF_MC_STREAMS   4
#define WL_RMC_MAX_TRS_PER_GROUP   1
#define WL_RMC_MAX_TRS_IN_ACKALL   1
#define WL_RMC_ACK_MCAST0          0x02
#define WL_RMC_ACK_MCAST_ALL       0x01
#define WL_RMC_ACTF_TIME_MIN       300	 /* time in ms */
#define WL_RMC_ACTF_TIME_MAX       20000 /* time in ms */
#define WL_RMC_MAX_NUM_TRS	   32	 /* maximun transmitters allowed */
#define WL_RMC_ARTMO_MIN           350	 /* time in ms */
#define WL_RMC_ARTMO_MAX           40000	 /* time in ms */

/* RMC events in action frames */
enum rmc_opcodes {
	RELMCAST_ENTRY_OP_DISABLE = 0,   /* Disable multi-cast group */
	RELMCAST_ENTRY_OP_DELETE  = 1,   /* Delete multi-cast group */
	RELMCAST_ENTRY_OP_ENABLE  = 2,   /* Enable multi-cast group */
	RELMCAST_ENTRY_OP_ACK_ALL = 3    /* Enable ACK ALL bit in AMT */
};

/* RMC operational modes */
enum rmc_modes {
	WL_RMC_MODE_RECEIVER    = 0,	 /* Receiver mode by default */
	WL_RMC_MODE_TRANSMITTER = 1,	 /* Transmitter mode using wl ackreq */
	WL_RMC_MODE_INITIATOR   = 2	 /* Initiator mode using wl ackreq */
};

/* Each RMC mcast client info */
typedef struct wl_relmcast_client {
	uint8 flag;			/* status of client such as AR, R, or blacklisted */
	int16 rssi;			/* rssi value of RMC client */
	struct ether_addr addr;		/* mac address of RMC client */
} wl_relmcast_client_t;

/* RMC Counters */
typedef struct wl_rmc_cnts {
	uint16  version;		/* see definition of WL_CNT_T_VERSION */
	uint16  length;			/* length of entire structure */
	uint16	dupcnt;			/* counter for duplicate rmc MPDU */
	uint16	ackreq_err;		/* counter for wl ackreq error    */
	uint16	af_tx_err;		/* error count for action frame transmit   */
	uint16	null_tx_err;		/* error count for rmc null frame transmit */
	uint16	af_unicast_tx_err;	/* error count for rmc unicast frame transmit */
	uint16	mc_no_amt_slot;		/* No mcast AMT entry available */
	/* Unused. Keep for rom compatibility */
	uint16	mc_no_glb_slot;		/* No mcast entry available in global table */
	uint16	mc_not_mirrored;	/* mcast group is not mirrored */
	uint16	mc_existing_tr;		/* mcast group is already taken by transmitter */
	uint16	mc_exist_in_amt;	/* mcast group is already programmed in amt */
	/* Unused. Keep for rom compatibility */
	uint16	mc_not_exist_in_gbl;	/* mcast group is not in global table */
	uint16	mc_not_exist_in_amt;	/* mcast group is not in AMT table */
	uint16	mc_utilized;		/* mcast addressed is already taken */
	uint16	mc_taken_other_tr;	/* multi-cast addressed is already taken */
	uint32	rmc_rx_frames_mac;      /* no of mc frames received from mac */
	uint32	rmc_tx_frames_mac;      /* no of mc frames transmitted to mac */
	uint32	mc_null_ar_cnt;         /* no. of times NULL AR is received */
	uint32	mc_ar_role_selected;	/* no. of times took AR role */
	uint32	mc_ar_role_deleted;	/* no. of times AR role cancelled */
	uint32	mc_noacktimer_expired;  /* no. of times noack timer expired */
	uint16  mc_no_wl_clk;           /* no wl clk detected when trying to access amt */
	uint16  mc_tr_cnt_exceeded;     /* No of transmitters in the network exceeded */
} wl_rmc_cnts_t;

/* RMC Status */
typedef struct wl_relmcast_st {
	uint8         ver;		/* version of RMC */
	uint8         num;		/* number of clients detected by transmitter */
	wl_relmcast_client_t clients[WL_RMC_MAX_CLIENT];
	uint16        err;		/* error status (used in infra) */
	uint16        actf_time;	/* action frame time period */
} wl_relmcast_status_t;

/* Entry for each STA/node */
typedef struct wl_rmc_entry {
	/* operation on multi-cast entry such add,
	 * delete, ack-all
	 */
	int8    flag;
	struct ether_addr addr;		/* multi-cast group mac address */
} wl_rmc_entry_t;

/* RMC table */
typedef struct wl_rmc_entry_table {
	uint8   index;			/* index to a particular mac entry in table */
	uint8   opcode;			/* opcodes or operation on entry */
	wl_rmc_entry_t entry[WL_RMC_MAX_TABLE_ENTRY];
} wl_rmc_entry_table_t;

typedef struct wl_rmc_trans_elem {
	struct ether_addr tr_mac;	/* transmitter mac */
	struct ether_addr ar_mac;	/* ar mac */
	uint16 artmo;			/* AR timeout */
	uint8 amt_idx;			/* amt table entry */
	uint16 flag;			/* entry will be acked, not acked, programmed, full etc */
} wl_rmc_trans_elem_t;

/* RMC transmitters */
typedef struct wl_rmc_trans_in_network {
	uint8         ver;		/* version of RMC */
	uint8         num_tr;		/* number of transmitters in the network */
	wl_rmc_trans_elem_t trs[WL_RMC_MAX_NUM_TRS];
} wl_rmc_trans_in_network_t;

/* To update vendor specific ie for RMC */
typedef struct wl_rmc_vsie {
	uint8	oui[DOT11_OUI_LEN];
	uint16	payload;	/* IE Data Payload */
} wl_rmc_vsie_t;


/* structures  & defines for proximity detection  */
enum proxd_method {
	PROXD_UNDEFINED_METHOD = 0,
	PROXD_RSSI_METHOD = 1,
	PROXD_TOF_METHOD = 2
};

/* structures for proximity detection device role */
#define WL_PROXD_MODE_DISABLE	0
#define WL_PROXD_MODE_NEUTRAL	1
#define WL_PROXD_MODE_INITIATOR	2
#define WL_PROXD_MODE_TARGET	3

#define WL_PROXD_ACTION_STOP		0
#define WL_PROXD_ACTION_START		1

#define WL_PROXD_FLAG_TARGET_REPORT	0x1
#define WL_PROXD_FLAG_REPORT_FAILURE	0x2
#define WL_PROXD_FLAG_INITIATOR_REPORT	0x4
#define WL_PROXD_FLAG_NOCHANSWT		0x8
#define WL_PROXD_FLAG_NETRUAL		0x10
#define WL_PROXD_FLAG_INITIATOR_RPTRTT	0x20
#define WL_PROXD_FLAG_ONEWAY		0x40
#define WL_PROXD_FLAG_SEQ_EN		0x80

#define WL_PROXD_RANDOM_WAKEUP	0x8000
#define WL_PROXD_MAXREPORT	8

typedef struct wl_proxd_iovar {
	uint16	method;		/* Proxmity Detection method */
	uint16	mode;		/* Mode (neutral, initiator, target) */
} wl_proxd_iovar_t;

/*
 * structures for proximity detection parameters
 * consists of two parts, common and method specific params
 * common params should be placed at the beginning
 */

/* require strict packing */
#include <packed_section_start.h>

typedef	BWL_PRE_PACKED_STRUCT struct	wl_proxd_params_common	{
	chanspec_t	chanspec;	/* channel spec */
	int16		tx_power;	/* tx power of Proximity Detection(PD) frames (in dBm) */
	uint16		tx_rate;	/* tx rate of PD rames  (in 500kbps units) */
	uint16		timeout;	/* timeout value */
	uint16		interval;	/* interval between neighbor finding attempts (in TU) */
	uint16		duration;	/* duration of neighbor finding attempts (in ms) */
} BWL_POST_PACKED_STRUCT wl_proxd_params_common_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_proxd_params_rssi_method {
	chanspec_t	chanspec;	/* chanspec for home channel */
	int16		tx_power;	/* tx power of Proximity Detection frames (in dBm) */
	uint16		tx_rate;	/* tx rate of PD frames, 500kbps units */
	uint16		timeout;	/* state machine wait timeout of the frames (in ms) */
	uint16		interval;	/* interval between neighbor finding attempts (in TU) */
	uint16		duration;	/* duration of neighbor finding attempts (in ms) */
					/* method specific ones go after this line */
	int16		rssi_thresh;	/* RSSI threshold (in dBm) */
	uint16		maxconvergtmo;	/* max wait converge timeout (in ms) */
} wl_proxd_params_rssi_method_t;

#define Q1_NS			25	/* Q1 time units */

#define TOF_BW_NUM		3	/* number of bandwidth that the TOF can support */
#define TOF_BW_SEQ_NUM		(TOF_BW_NUM+2)	/* number of total index */
enum tof_bw_index {
	TOF_BW_20MHZ_INDEX = 0,
	TOF_BW_40MHZ_INDEX = 1,
	TOF_BW_80MHZ_INDEX = 2,
	TOF_BW_SEQTX_INDEX = 3,
	TOF_BW_SEQRX_INDEX = 4
};

#define BANDWIDTH_BASE	20	/* base value of bandwidth */
#define TOF_BW_20MHZ    (BANDWIDTH_BASE << TOF_BW_20MHZ_INDEX)
#define TOF_BW_40MHZ    (BANDWIDTH_BASE << TOF_BW_40MHZ_INDEX)
#define TOF_BW_80MHZ    (BANDWIDTH_BASE << TOF_BW_80MHZ_INDEX)
#define TOF_BW_10MHZ    10

#define NFFT_BASE		64	/* base size of fft */
#define TOF_NFFT_20MHZ  (NFFT_BASE << TOF_BW_20MHZ_INDEX)
#define TOF_NFFT_40MHZ  (NFFT_BASE << TOF_BW_40MHZ_INDEX)
#define TOF_NFFT_80MHZ  (NFFT_BASE << TOF_BW_80MHZ_INDEX)

typedef BWL_PRE_PACKED_STRUCT struct wl_proxd_params_tof_method {
	chanspec_t	chanspec;	/* chanspec for home channel */
	int16		tx_power;	/* tx power of Proximity Detection(PD) frames (in dBm) */
	uint16		tx_rate;	/* tx rate of PD rames  (in 500kbps units) */
	uint16		timeout;	/* state machine wait timeout of the frames (in ms) */
	uint16		interval;	/* interval between neighbor finding attempts (in TU) */
	uint16		duration;	/* duration of neighbor finding attempts (in ms) */
	/* specific for the method go after this line */
	struct ether_addr tgt_mac;	/* target mac addr for TOF method */
	uint16		ftm_cnt;	/* number of the frames txed by initiator */
	uint16		retry_cnt;	/* number of retransmit attampts for ftm frames */
	int16		vht_rate;	/* ht or vht rate */
	/* add more params required for other methods can be added here  */
} BWL_POST_PACKED_STRUCT wl_proxd_params_tof_method_t;

typedef struct wl_proxd_seq_config
{
	int16 N_tx_log2;
	int16 N_rx_log2;
	int16 N_tx_scale;
	int16 N_rx_scale;
	int16 w_len;
	int16 w_offset;
} wl_proxd_seq_config_t;


typedef BWL_PRE_PACKED_STRUCT struct wl_proxd_params_tof_tune {
	uint32		Ki;			/* h/w delay K factor for initiator */
	uint32		Kt;			/* h/w delay K factor for target */
	int16		vhtack;			/* enable/disable VHT ACK */
	int16		N_log2[TOF_BW_SEQ_NUM]; /* simple threshold crossing */
	int16		w_offset[TOF_BW_NUM];	/* offset of threshold crossing window(per BW) */
	int16		w_len[TOF_BW_NUM];	/* length of threshold crossing window(per BW) */
	int32		maxDT;			/* max time difference of T4/T1 or T3/T2 */
	int32		minDT;			/* min time difference of T4/T1 or T3/T2 */
	uint8		totalfrmcnt;	/* total count of transfered measurement frames */
	uint16		rsv_media;		/* reserve media value for TOF */
	uint32		flags;			/* flags */
	uint8		core;			/* core to use for tx */
	uint8		force_K;		/* set to force value of K  */
	int16		N_scale[TOF_BW_SEQ_NUM]; /* simple threshold crossing */
	uint8		sw_adj;			/* enable sw assisted timestamp adjustment */
	uint8		hw_adj;			/* enable hw assisted timestamp adjustment */
	uint8		seq_en;			/* enable ranging sequence */
	uint8		ftm_cnt[TOF_BW_SEQ_NUM]; /* number of ftm frames based on bandwidth */
	int16		N_log2_2g;		/* simple threshold crossing for 2g channel */
	int16		N_scale_2g;		/* simple threshold crossing for 2g channel */
	wl_proxd_seq_config_t seq_5g20;
} BWL_POST_PACKED_STRUCT wl_proxd_params_tof_tune_t;

typedef struct wl_proxd_params_iovar {
	uint16	method;			/* Proxmity Detection method */
	union {
		/* common params for pdsvc */
		wl_proxd_params_common_t	cmn_params;	/* common parameters */
		/*  method specific */
		wl_proxd_params_rssi_method_t	rssi_params;	/* RSSI method parameters */
		wl_proxd_params_tof_method_t	tof_params;	/* TOF meothod parameters */
		/* tune parameters */
		wl_proxd_params_tof_tune_t	tof_tune;	/* TOF tune parameters */
	} u;				/* Method specific optional parameters */
} wl_proxd_params_iovar_t;

#define PROXD_COLLECT_GET_STATUS	0
#define PROXD_COLLECT_SET_STATUS	1
#define PROXD_COLLECT_QUERY_HEADER	2
#define PROXD_COLLECT_QUERY_DATA	3
#define PROXD_COLLECT_QUERY_DEBUG	4
#define PROXD_COLLECT_REMOTE_REQUEST	5
#define PROXD_COLLECT_DONE			6

typedef BWL_PRE_PACKED_STRUCT struct wl_proxd_collect_query {
	uint32		method;		/* method */
	uint8		request;	/* Query request. */
	uint8		status;		/* 0 -- disable, 1 -- enable collection, */
					/* 2 -- enable collection & debug */
	uint16		index;		/* The current frame index [0 to total_frames - 1]. */
	uint16		mode;		/* Initiator or Target */
	bool		busy;		/* tof sm is busy */
	bool		remote;		/* Remote collect data */
} BWL_POST_PACKED_STRUCT wl_proxd_collect_query_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_proxd_collect_header {
	uint16		total_frames;			/* The totral frames for this collect. */
	uint16		nfft;				/* nfft value */
	uint16		bandwidth;			/* bandwidth */
	uint16		channel;			/* channel number */
	uint32		chanspec;			/* channel spec */
	uint32		fpfactor;			/* avb timer value factor */
	uint16		fpfactor_shift;			/* avb timer value shift bits */
	int32		distance;			/* distance calculated by fw */
	uint32		meanrtt;			/* mean of RTTs */
	uint32		modertt;			/* mode of RTTs */
	uint32		medianrtt;			/* median of RTTs */
	uint32		sdrtt;				/* standard deviation of RTTs */
	uint32		clkdivisor;			/* clock divisor */
	uint16		chipnum;			/* chip type */
	uint8		chiprev;			/* chip revision */
	uint8		phyver;				/* phy version */
	struct ether_addr	loaclMacAddr;		/* local mac address */
	struct ether_addr	remoteMacAddr;		/* remote mac address */
	wl_proxd_params_tof_tune_t params;
} BWL_POST_PACKED_STRUCT wl_proxd_collect_header_t;


#ifdef WL_NAN
/*  ********************** NAN wl interface struct types and defs ******************** */

#define WL_NAN_IOCTL_VERSION	0x1
#define NAN_IOC_BUFSZ  256 /**< some sufficient ioc buff size for our module */
#define NAN_IOC_BUFSZ_EXT  1024 /* some sufficient ioc buff size for dump commands */

/*   wl_nan_sub_cmd may also be used in dhd  */
typedef struct wl_nan_sub_cmd wl_nan_sub_cmd_t;
typedef int (cmd_handler_t)(void *wl, const wl_nan_sub_cmd_t *cmd, char **argv);
/* nan cmd list entry  */
struct wl_nan_sub_cmd {
	char *name;
	uint8  version;		/* cmd  version */
	uint16 id;			/* id for the dongle f/w switch/case  */
	uint16 type;		/* base type of argument */
	cmd_handler_t *handler; /* cmd handler  */
};

/* container for nan iovtls & events */
typedef BWL_PRE_PACKED_STRUCT struct wl_nan_ioc {
	uint16	version;	/* interface command or event version */
	uint16	id;			/* nan ioctl cmd  ID  */
	uint16	len;		/* total length of all tlv records in data[]  */
	uint16	pad;		/* pad to be 32 bit aligment */
	uint8	data [1];	/* var len payload of bcm_xtlv_t type */
} BWL_POST_PACKED_STRUCT wl_nan_ioc_t;

typedef struct wl_nan_status {
	uint8 inited;
	uint8 joined;
	uint8 role;
	uint8 hop_count;
	uint32 chspec;
	uint8 amr[8];			/* Anchor Master Rank */
	uint32 cnt_pend_txfrm;		/* pending TX frames */
	uint32 cnt_bcn_tx;		/* TX disc/sync beacon count */
	uint32 cnt_bcn_rx;		/* RX disc/sync beacon count */
	uint32 cnt_svc_disc_tx;		/* TX svc disc frame count */
	uint32 cnt_svc_disc_rx;		/* RX svc disc frame count */
	struct ether_addr cid;
	uint32 chspec_5g;
} wl_nan_status_t;

typedef struct wl_nan_count {
	uint32 cnt_bcn_tx;		/* TX disc/sync beacon count */
	uint32 cnt_bcn_rx;		/* RX disc/sync beacon count */
	uint32 cnt_svc_disc_tx;		/* TX svc disc frame count */
	uint32 cnt_svc_disc_rx;		/* RX svc disc frame count */
} wl_nan_count_t;

/* various params and ctl swithce for nan_debug instance  */
typedef struct nan_debug_params {
	uint8	enabled; /* runtime debuging enabled */
	uint8	collect; /* enables debug svc sdf monitor mode  */
	uint16	cmd;	/* debug cmd to perform a debug action */
	uint32	msglevel; /* msg level if enabled */
	uint16	status;
} nan_debug_params_t;

/* time slot */
#define NAN_MAX_TIMESLOT	32
typedef struct nan_timeslot {
	uint32	abitmap; /* available bitmap */
	uint32 chanlist[NAN_MAX_TIMESLOT];
} nan_timeslot_t;

/* nan passive scan params */
#define NAN_SCAN_MAX_CHCNT 8
typedef struct nan_scan_params {
	uint16 scan_time;
	uint16 home_time;
	uint16 ms_intvl; /* interval between merge scan */
	uint16 ms_dur;  /* duration of merge scan */
	uint16 chspec_num;
	uint8 pad[2];
	chanspec_t chspec_list[NAN_SCAN_MAX_CHCNT]; /* act. used 3, 5 rfu */
} nan_scan_params_t;

enum wl_nan_role {
	WL_NAN_ROLE_AUTO = 0,
	WL_NAN_ROLE_NON_MASTER_NON_SYNC = 1,
	WL_NAN_ROLE_NON_MASTER_SYNC = 2,
	WL_NAN_ROLE_MASTER = 3,
	WL_NAN_ROLE_ANCHOR_MASTER = 4
};
#define NAN_MASTER_RANK_LEN 8
/* nan cmd IDs */
enum wl_nan_cmds {
	 /* nan cfg /disc & dbg ioctls */
	WL_NAN_CMD_ENABLE = 1,
	WL_NAN_CMD_ATTR = 2,
	WL_NAN_CMD_NAN_JOIN = 3,
	WL_NAN_CMD_LEAVE = 4,
	WL_NAN_CMD_MERGE = 5,
	WL_NAN_CMD_STATUS = 6,
	WL_NAN_CMD_TSRESERVE = 7,
	WL_NAN_CMD_TSSCHEDULE = 8,
	WL_NAN_CMD_TSRELEASE = 9,
	WL_NAN_CMD_OUI = 10,

	WL_NAN_CMD_COUNT = 15,
	WL_NAN_CMD_CLEARCOUNT = 16,

	/*  discovery engine commands */
	WL_NAN_CMD_PUBLISH = 20,
	WL_NAN_CMD_SUBSCRIBE = 21,
	WL_NAN_CMD_CANCEL_PUBLISH = 22,
	WL_NAN_CMD_CANCEL_SUBSCRIBE = 23,
	WL_NAN_CMD_TRANSMIT = 24,
	WL_NAN_CMD_CONNECTION = 25,
	WL_NAN_CMD_SHOW = 26,
	WL_NAN_CMD_STOP = 27,	/* stop nan for a given cluster ID  */
	/*  nan debug iovars & cmds  */
	WL_NAN_CMD_SCAN_PARAMS = 46,
	WL_NAN_CMD_SCAN = 47,
	WL_NAN_CMD_SCAN_RESULTS = 48,
	WL_NAN_CMD_EVENT_MASK = 49,
	WL_NAN_CMD_EVENT_CHECK = 50,
	WL_NAN_CMD_DUMP = 51,
	WL_NAN_CMD_CLEAR = 52,
	WL_NAN_CMD_RSSI = 53,

	WL_NAN_CMD_DEBUG = 60,
	WL_NAN_CMD_TEST1 = 61,
	WL_NAN_CMD_TEST2 = 62,
	WL_NAN_CMD_TEST3 = 63,
	WL_NAN_CMD_DISC_RESULTS = 64
};

/*
 * tlv IDs uniquely identifies  cmd parameters
 * packed into wl_nan_ioc_t container
 */
enum wl_nan_cmd_xtlv_id {
	/* 0x00 ~ 0xFF: standard TLV ID whose data format is the same as NAN attribute TLV */
	WL_NAN_XTLV_ZERO = 0,		/* used as tlv buf end marker */
#ifdef NAN_STD_TLV 				/* rfu, don't use yet */
	WL_NAN_XTLV_MASTER_IND = 1, /* == NAN_ATTR_MASTER_IND, */
	WL_NAN_XTLV_CLUSTER = 2,	/* == NAN_ATTR_CLUSTER, */
	WL_NAN_XTLV_VENDOR = 221,	/* == NAN_ATTR_VENDOR, */
#endif
	/* 0x02 ~ 0xFF: reserved. In case to use with the same data format as NAN attribute TLV */
	/* 0x100 ~ : private TLV ID defined just for NAN command */
	/* common types */
	WL_NAN_XTLV_MAC_ADDR = 0x102,	/* used in various cmds */
	WL_NAN_XTLV_REASON = 0x103,
	WL_NAN_XTLV_ENABLED = 0x104,
	/* explicit types, primarily for discovery engine iovars  */
	WL_NAN_XTLV_SVC_PARAMS = 0x120,     /* Contains required params: wl_nan_disc_params_t */
	WL_NAN_XTLV_MATCH_RX = 0x121,       /* Matching filter to evaluate on receive */
	WL_NAN_XTLV_MATCH_TX = 0x122,       /* Matching filter to send */
	WL_NAN_XTLV_SVC_INFO = 0x123,       /* Service specific info */
	WL_NAN_XTLV_SVC_NAME = 0x124,       /* Optional UTF-8 service name, for debugging. */
	WL_NAN_XTLV_INSTANCE_ID = 0x125,    /* Identifies unique publish or subscribe instance */
	WL_NAN_XTLV_PRIORITY = 0x126,       /* used in transmit cmd context */
	WL_NAN_XTLV_REQUESTOR_ID = 0x127,	/* Requestor instance ID */
	WL_NAN_XTLV_VNDR = 0x128,		/* Vendor specific attribute */
	WL_NAN_XTLV_SR_FILTER = 0x129,          /* Service Response Filter */
	WL_NAN_XTLV_FOLLOWUP = 0x130,	/* Service Info for Follow-Up SDF */
	WL_NAN_XTLV_PEER_INSTANCE_ID = 0x131, /* Used to parse remote instance Id */
	/* explicit types, primarily for NAN MAC iovars   */
	WL_NAN_XTLV_DW_LEN = 0x140,            /* discovery win length */
	WL_NAN_XTLV_BCN_INTERVAL = 0x141,      /* beacon interval, both sync and descovery bcns?  */
	WL_NAN_XTLV_CLUSTER_ID = 0x142,
	WL_NAN_XTLV_IF_ADDR = 0x143,
	WL_NAN_XTLV_MC_ADDR = 0x144,
	WL_NAN_XTLV_ROLE = 0x145,
	WL_NAN_XTLV_START = 0x146,

	WL_NAN_XTLV_MASTER_PREF = 0x147,
	WL_NAN_XTLV_DW_INTERVAL = 0x148,
	WL_NAN_XTLV_PTBTT_OVERRIDE = 0x149,
	/*  nan status command xtlvs  */
	WL_NAN_XTLV_MAC_INITED = 0x14a,
	WL_NAN_XTLV_MAC_ENABLED = 0x14b,
	WL_NAN_XTLV_MAC_CHANSPEC = 0x14c,
	WL_NAN_XTLV_MAC_AMR = 0x14d,	/* anchormaster rank u8 amr[8] */
	WL_NAN_XTLV_MAC_HOPCNT = 0x14e,
	WL_NAN_XTLV_MAC_AMBTT = 0x14f,
	WL_NAN_XTLV_MAC_TXRATE = 0x150,
	WL_NAN_XTLV_MAC_STATUS = 0x151,  /* xtlv payload is nan_status_t */
	WL_NAN_XTLV_NAN_SCANPARAMS = 0x152,  /* payload is nan_scan_params_t */
	WL_NAN_XTLV_DEBUGPARAMS = 0x153,  /* payload is nan_scan_params_t */
	WL_NAN_XTLV_SUBSCR_ID = 0x154,   /* subscriber id  */
	WL_NAN_XTLV_PUBLR_ID = 0x155,	/* publisher id */
	WL_NAN_XTLV_EVENT_MASK = 0x156,
	WL_NAN_XTLV_MASTER_RANK = 0x158,
	WL_NAN_XTLV_WARM_UP_TIME = 0x159,
	WL_NAN_XTLV_PM_OPTION = 0x15a,
	WL_NAN_XTLV_OUI = 0x15b,	/* NAN OUI */
	WL_NAN_XTLV_MAC_COUNT = 0x15c,  /* xtlv payload is nan_count_t */
	/* nan timeslot management */
	WL_NAN_XTLV_TSRESERVE = 0x160,
	WL_NAN_XTLV_TSRELEASE = 0x161,
	WL_NAN_XTLV_IDLE_DW_TIMEOUT = 0x162,
	WL_NAN_XTLV_IDLE_DW_LEN = 0x163,
	WL_NAN_XTLV_RND_FACTOR = 0x164,
	WL_NAN_XTLV_SVC_DISC_TXTIME = 0x165,     /* svc disc frame tx time in DW */
	WL_NAN_XTLV_OPERATING_BAND = 0x166,
	WL_NAN_XTLV_STOP_BCN_TX = 0x167,
	WL_NAN_XTLV_CONCUR_SCAN = 0x168,
	WL_NAN_XTLV_DUMP_CLR_TYPE = 0x175, /* wl nan dump/clear subtype */
	WL_NAN_XTLV_PEER_RSSI = 0x176, /* xtlv payload for wl nan dump rssi */
	WL_NAN_XTLV_MAC_CHANSPEC_1 = 0x17A,	/* to get chanspec[1] */
	WL_NAN_XTLV_DISC_RESULTS = 0x17B,        /* get disc results */
	WL_NAN_XTLV_MAC_STATS = 0x17C /* xtlv payload for wl nan dump stats */
};

/* Flag bits for Publish and Subscribe (wl_nan_disc_params_t flags) */
#define WL_NAN_RANGE_LIMITED           0x0040
/* Bits specific to Publish */
/* Unsolicited transmissions */
#define WL_NAN_PUB_UNSOLICIT           0x1000
/* Solicited transmissions */
#define WL_NAN_PUB_SOLICIT             0x2000
#define WL_NAN_PUB_BOTH                0x3000
/* Set for broadcast solicited transmission
 * Do not set for unicast solicited transmission
 */
#define WL_NAN_PUB_BCAST               0x4000
/* Generate event on each solicited transmission */
#define WL_NAN_PUB_EVENT               0x8000
/* Used for one-time solicited Publish functions to indicate transmision occurred */
#define WL_NAN_PUB_SOLICIT_PENDING	0x10000
/* Follow-up frames */
#define WL_NAN_FOLLOWUP			0x20000
/* Bits specific to Subscribe */
/* Active subscribe mode (Leave unset for passive) */
#define WL_NAN_SUB_ACTIVE              0x1000

/* Special values for time to live (ttl) parameter */
#define WL_NAN_TTL_UNTIL_CANCEL	0xFFFFFFFF
/* Publish -  runs until first transmission
 * Subscribe - runs until first  DiscoveryResult event
 */
#define WL_NAN_TTL_FIRST	0

/* The service hash (service id) is exactly this many bytes. */
#define WL_NAN_SVC_HASH_LEN	6

/* Number of hash functions per bloom filter */
#define WL_NAN_HASHES_PER_BLOOM 4

/* Instance ID type (unique identifier) */
typedef uint8 wl_nan_instance_id_t;

/* no. of max last disc results */
#define WL_NAN_MAX_DISC_RESULTS	3

/** Mandatory parameters for publish/subscribe iovars - NAN_TLV_SVC_PARAMS */
typedef struct wl_nan_disc_params_s {
	/* Periodicity of unsolicited/query transmissions, in DWs */
	uint32 period;
	/* Time to live in DWs */
	uint32 ttl;
	/* Flag bits */
	uint32 flags;
	/* Publish or subscribe service id, i.e. hash of the service name */
	uint8 svc_hash[WL_NAN_SVC_HASH_LEN];
	/* pad to make 4 byte alignment, can be used for something else in the future */
	uint8 pad;
	/* Publish or subscribe id */
	wl_nan_instance_id_t instance_id;
} wl_nan_disc_params_t;

/* recent discovery results */
typedef struct wl_nan_disc_result_s
{
	wl_nan_instance_id_t instance_id;	/* instance id of pub/sub req */
	wl_nan_instance_id_t peer_instance_id;	/* peer instance id of pub/sub req/resp */
	uint8 svc_hash[WL_NAN_SVC_HASH_LEN];	/* service descp string */
	struct ether_addr peer_mac;	/* peer mac address */
} wl_nan_disc_result_t;

/* list of recent discovery results */
typedef struct wl_nan_disc_results_s
{
	wl_nan_disc_result_t disc_result[WL_NAN_MAX_DISC_RESULTS];
} wl_nan_disc_results_list_t;

/*
* desovery interface event structures *
*/

/* NAN Ranging */

/* Bit defines for global flags */
#define WL_NAN_RANGING_ENABLE		1 /* enable RTT */
#define WL_NAN_RANGING_RANGED		2 /* Report to host if ranged as target */
typedef struct nan_ranging_config {
	uint32 chanspec;		/* Ranging chanspec */
	uint16 timeslot;		/* NAN RTT start time slot  1-511 */
	uint16 duration;		/* NAN RTT duration in ms */
	struct ether_addr allow_mac;	/* peer initiated ranging: the allowed peer mac
					 * address, a unicast (for one peer) or
					 * a broadcast for all. Setting it to all zeros
					 * means responding to none,same as not setting
					 * the flag bit NAN_RANGING_RESPOND
					 */
	uint16 flags;
} wl_nan_ranging_config_t;

/* list of peers for self initiated ranging */
/* Bit defines for per peer flags */
#define WL_NAN_RANGING_REPORT (1<<0)	/* Enable reporting range to target */
typedef struct nan_ranging_peer {
	uint32 chanspec;		/* desired chanspec for this peer */
	uint32 abitmap;			/* available bitmap */
	struct ether_addr ea;		/* peer MAC address */
	uint8 frmcnt;			/* frame count */
	uint8 retrycnt;			/* retry count */
	uint16 flags;			/* per peer flags, report or not */
} wl_nan_ranging_peer_t;
typedef struct nan_ranging_list {
	uint8 count;			/* number of MAC addresses */
	uint8 num_peers_done;		/* host set to 0, when read, shows number of peers
					 * completed, success or fail
					 */
	uint8 num_dws;			/* time period to do the ranging, specified in dws */
	uint8 reserve;			/* reserved field */
	wl_nan_ranging_peer_t rp[1];	/* variable length array of peers */
} wl_nan_ranging_list_t;

/* ranging results, a list for self initiated ranging and one for peer initiated ranging */
/* There will be one structure for each peer */
#define WL_NAN_RANGING_STATUS_SUCCESS		1
#define WL_NAN_RANGING_STATUS_FAIL			2
#define WL_NAN_RANGING_STATUS_TIMEOUT		3
#define WL_NAN_RANGING_STATUS_ABORT		4 /* with partial results if sounding count > 0 */
typedef struct nan_ranging_result {
	uint8 status;			/* 1: Success, 2: Fail 3: Timeout 4: Aborted */
	uint8 sounding_count;		/* number of measurements completed (0 = failure) */
	struct ether_addr ea;		/* initiator MAC address */
	uint32 chanspec;		/* Chanspec where the ranging was done */
	uint32 timestamp;		/* 32bits of the TSF timestamp ranging was completed at */
	uint32 distance;		/* mean distance in meters expressed as Q4 number.
					 * Only valid when sounding_count > 0. Examples:
					 * 0x08 = 0.5m
					 * 0x10 = 1m
					 * 0x18 = 1.5m
					 * set to 0xffffffff to indicate invalid number
					 */
	int32 rtt_var;			/* standard deviation in 10th of ns of RTTs measured.
					 * Only valid when sounding_count > 0
					 */
	struct ether_addr tgtea;	/* target MAC address */
} wl_nan_ranging_result_t;
typedef struct nan_ranging_event_data {
	uint8 mode;			/* 1: Result of host initiated ranging */
					/* 2: Result of peer initiated ranging */
	uint8 reserved;
	uint8 success_count;		/* number of peers completed successfully */
	uint8 count;			/* number of peers in the list */
	wl_nan_ranging_result_t rr[1];	/* variable array of ranging peers */
} wl_nan_ranging_event_data_t;
enum {
	WL_NAN_RSSI_DATA = 1,
	WL_NAN_STATS_DATA = 2,
/*
 * ***** ADD before this line ****
 */
	WL_NAN_INVALID
};

typedef struct wl_nan_stats {
	/* general */
	uint32 cnt_dw; /* DW slots */
	uint32 cnt_disc_bcn_sch; /* disc beacon slots */
	uint32 cnt_amr_exp; /* count of ambtt expiries resetting roles */
	uint32 cnt_bcn_upd; /* count of beacon template updates */
	uint32 cnt_bcn_tx; /* count of sync & disc bcn tx */
	uint32 cnt_bcn_rx; /* count of sync & disc bcn rx */
	uint32 cnt_sync_bcn_tx; /* count of sync bcn tx within DW */
	uint32 cnt_disc_bcn_tx; /* count of disc bcn tx */
	uint32 cnt_sdftx_bcmc; /* count of bcast/mcast sdf tx */
	uint32 cnt_sdftx_uc; /* count of unicast sdf tx */
	uint32 cnt_sdftx_fail; /* count of unicast sdf tx fails */
	uint32 cnt_sdf_rx; /* count of  sdf rx */
	/* NAN roles */
	uint32 cnt_am; /* anchor master */
	uint32 cnt_master; /* master */
	uint32 cnt_nms; /* non master sync */
	uint32 cnt_nmns; /* non master non sync */
	/* TX */
	uint32 cnt_err_txtime; /* error in txtime */
	uint32 cnt_err_unsch_tx; /* tx while not in DW/ disc bcn slot */
	uint32 cnt_err_bcn_tx; /*  beacon tx error */
	uint32 cnt_sync_bcn_tx_miss; /* no. of times time delta between 2 cosequetive
						* sync beacons is more than dw interval
						*/
	/* SCANS */
	uint32 cnt_mrg_scan; /* count of merge scans completed */
	uint32 cnt_err_ms_rej; /* number of merge scan failed */
	uint32 cnt_scan_results; /* no. of nan beacons scanned */
	uint32 cnt_join_scan_rej; /* no. of join scans rejected */
	uint32 cnt_nan_scan_abort; /* no. of join scans rejected */
	/* enable/disable */
	uint32 cnt_nan_enab; /* no. of times nan feature got enabled */
	uint32 cnt_nan_disab; /* no. of times nan feature got disabled */
} wl_nan_stats_t;

#define WL_NAN_MAC_MAX_NAN_PEERS 6
#define WL_NAN_MAC_MAX_RSSI_DATA_PER_PEER  10

typedef struct wl_nan_nbr_rssi {
	uint8 rx_chan; /* channel number on which bcn rcvd */
	int rssi_raw;  /* received rssi value */
	int rssi_avg;  /* normalized rssi value */
} wl_nan_peer_rssi_t;

typedef struct wl_nan_peer_rssi_entry {
	struct ether_addr mac;  /* peer mac address */
	uint8 flags;   /* TODO:rssi data order: latest first, oldest first etc */
	uint8 rssi_cnt;   /* rssi data sample present */
	wl_nan_peer_rssi_t rssi[WL_NAN_MAC_MAX_RSSI_DATA_PER_PEER]; /* RSSI data frm peer */
} wl_nan_peer_rssi_entry_t;

#define WL_NAN_PEER_RSSI      0x1
#define WL_NAN_PEER_RSSI_LIST 0x2

typedef struct wl_nan_nbr_rssi_data {
	uint8 flags;   /* this is a list or single rssi data */
	uint8 peer_cnt; /* number of peers */
	uint16 pad; /* padding */
	wl_nan_peer_rssi_entry_t peers[1]; /* peers data list */
} wl_nan_peer_rssi_data_t;

/* ********************* end of NAN section ******************************** */
#endif /* WL_NAN */


#define RSSI_THRESHOLD_SIZE 16
#define MAX_IMP_RESP_SIZE 256

typedef BWL_PRE_PACKED_STRUCT struct wl_proxd_rssi_bias {
	int32		version;			/* version */
	int32		threshold[RSSI_THRESHOLD_SIZE];	/* threshold */
	int32		peak_offset;			/* peak offset */
	int32		bias;				/* rssi bias */
	int32		gd_delta;			/* GD - GD_ADJ */
	int32		imp_resp[MAX_IMP_RESP_SIZE];	/* (Hi*Hi)+(Hr*Hr) */
} BWL_POST_PACKED_STRUCT wl_proxd_rssi_bias_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_proxd_rssi_bias_avg {
	int32		avg_threshold[RSSI_THRESHOLD_SIZE];	/* avg threshold */
	int32		avg_peak_offset;			/* avg peak offset */
	int32		avg_rssi;				/* avg rssi */
	int32		avg_bias;				/* avg bias */
} BWL_POST_PACKED_STRUCT wl_proxd_rssi_bias_avg_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_proxd_collect_info {
	uint16		type;	 /* type: 0 channel table, 1 channel smoothing table, 2 and 3 seq */
	uint16		index;		/* The current frame index, from 1 to total_frames. */
	uint16		tof_cmd;	/* M_TOF_CMD      */
	uint16		tof_rsp;	/* M_TOF_RSP      */
	uint16		tof_avb_rxl;	/* M_TOF_AVB_RX_L */
	uint16		tof_avb_rxh;	/* M_TOF_AVB_RX_H */
	uint16		tof_avb_txl;	/* M_TOF_AVB_TX_L */
	uint16		tof_avb_txh;	/* M_TOF_AVB_TX_H */
	uint16		tof_id;		/* M_TOF_ID */
	uint8		tof_frame_type;
	uint8		tof_frame_bw;
	int8		tof_rssi;
	int32		tof_cfo;
	int32		gd_adj_ns;	/* gound delay */
	int32		gd_h_adj_ns;	/* group delay + threshold crossing */
#ifdef RSSI_REFINE
	wl_proxd_rssi_bias_t rssi_bias; /* RSSI refinement info */
#endif
	int16		nfft;		/* number of samples stored in H */

} BWL_POST_PACKED_STRUCT wl_proxd_collect_info_t;

#define k_tof_collect_H_pad  1
#define k_tof_collect_H_size (256+16+k_tof_collect_H_pad)
#define k_tof_collect_Hraw_size (2*k_tof_collect_H_size)
typedef BWL_PRE_PACKED_STRUCT struct wl_proxd_collect_data {
	wl_proxd_collect_info_t  info;
	uint32	H[k_tof_collect_H_size]; /* raw data read from phy used to adjust timestamps */

} BWL_POST_PACKED_STRUCT wl_proxd_collect_data_t;

typedef BWL_PRE_PACKED_STRUCT struct wl_proxd_debug_data {
	uint8		count;		/* number of packets */
	uint8		stage;		/* state machone stage */
	uint8		received;	/* received or txed */
	uint8		paket_type;	/* packet type */
	uint8		category;	/* category field */
	uint8		action;		/* action field */
	uint8		token;		/* token number */
	uint8		follow_token;	/* following token number */
	uint16		index;		/* index of the packet */
	uint16		tof_cmd;	/* M_TOF_CMD */
	uint16		tof_rsp;	/* M_TOF_RSP */
	uint16		tof_avb_rxl;	/* M_TOF_AVB_RX_L */
	uint16		tof_avb_rxh;	/* M_TOF_AVB_RX_H */
	uint16		tof_avb_txl;	/* M_TOF_AVB_TX_L */
	uint16		tof_avb_txh;	/* M_TOF_AVB_TX_H */
	uint16		tof_id;		/* M_TOF_ID */
	uint16		tof_status0;	/* M_TOF_STATUS_0 */
	uint16		tof_status2;	/* M_TOF_STATUS_2 */
	uint16		tof_chsm0;	/* M_TOF_CHNSM_0 */
	uint16		tof_phyctl0;	/* M_TOF_PHYCTL0 */
	uint16		tof_phyctl1;	/* M_TOF_PHYCTL1 */
	uint16		tof_phyctl2;	/* M_TOF_PHYCTL2 */
	uint16		tof_lsig;	/* M_TOF_LSIG */
	uint16		tof_vhta0;	/* M_TOF_VHTA0 */
	uint16		tof_vhta1;	/* M_TOF_VHTA1 */
	uint16		tof_vhta2;	/* M_TOF_VHTA2 */
	uint16		tof_vhtb0;	/* M_TOF_VHTB0 */
	uint16		tof_vhtb1;	/* M_TOF_VHTB1 */
	uint16		tof_apmductl;	/* M_TOF_AMPDU_CTL */
	uint16		tof_apmdudlim;	/* M_TOF_AMPDU_DLIM */
	uint16		tof_apmdulen;	/* M_TOF_AMPDU_LEN */
} BWL_POST_PACKED_STRUCT wl_proxd_debug_data_t;

/* version of the wl_wsec_info structure */
#define WL_WSEC_INFO_VERSION 0x01

/* start enum value for BSS properties */
#define WL_WSEC_INFO_BSS_BASE 0x0100

/* size of len and type fields of wl_wsec_info_tlv_t struct */
#define WL_WSEC_INFO_TLV_HDR_LEN OFFSETOF(wl_wsec_info_tlv_t, data)

/* Allowed wl_wsec_info properties; not all of them may be supported. */
typedef enum {
	WL_WSEC_INFO_NONE = 0,
	WL_WSEC_INFO_MAX_KEYS = 1,
	WL_WSEC_INFO_NUM_KEYS = 2,
	WL_WSEC_INFO_NUM_HW_KEYS = 3,
	WL_WSEC_INFO_MAX_KEY_IDX = 4,
	WL_WSEC_INFO_NUM_REPLAY_CNTRS = 5,
	WL_WSEC_INFO_SUPPORTED_ALGOS = 6,
	WL_WSEC_INFO_MAX_KEY_LEN = 7,
	WL_WSEC_INFO_FLAGS = 8,
	/* add global/per-wlc properties above */
	WL_WSEC_INFO_BSS_FLAGS = (WL_WSEC_INFO_BSS_BASE + 1),
	WL_WSEC_INFO_BSS_WSEC = (WL_WSEC_INFO_BSS_BASE + 2),
	WL_WSEC_INFO_BSS_TX_KEY_ID = (WL_WSEC_INFO_BSS_BASE + 3),
	WL_WSEC_INFO_BSS_ALGO = (WL_WSEC_INFO_BSS_BASE + 4),
	WL_WSEC_INFO_BSS_KEY_LEN = (WL_WSEC_INFO_BSS_BASE + 5),
	/* add per-BSS properties above */
	WL_WSEC_INFO_MAX = 0xffff
} wl_wsec_info_type_t;

/* tlv used to return wl_wsec_info properties */
typedef struct {
	uint16 type;
	uint16 len;		/* data length */
	uint8 data[1];	/* data follows */
} wl_wsec_info_tlv_t;

/* input/output data type for wsec_info iovar */
typedef struct wl_wsec_info {
	uint8 version; /* structure version */
	uint8 pad[2];
	uint8 num_tlvs;
	wl_wsec_info_tlv_t tlvs[1]; /* tlv data follows */
} wl_wsec_info_t;

/*
 * scan MAC definitions
 */

/* common iovar struct */
typedef struct wl_scanmac {
	uint16 subcmd_id;	/* subcommand id */
	uint16 len;		/* total length of data[] */
	uint8 data[1];		/* subcommand data */
} wl_scanmac_t;

/* subcommand ids */
#define WL_SCANMAC_SUBCMD_ENABLE   0
#define WL_SCANMAC_SUBCMD_BSSCFG   1   /* only GET supported */
#define WL_SCANMAC_SUBCMD_CONFIG   2

/* scanmac enable data struct */
typedef struct wl_scanmac_enable {
	uint8 enable;	/* 1 - enable, 0 - disable */
	uint8 pad[3];	/* 4-byte struct alignment */
} wl_scanmac_enable_t;

/* scanmac bsscfg data struct */
typedef struct wl_scanmac_bsscfg {
	uint32 bsscfg;	/* bsscfg index */
} wl_scanmac_bsscfg_t;

/* scanmac config data struct */
typedef struct wl_scanmac_config {
	struct ether_addr mac;	/* 6 bytes of MAC address or MAC prefix (i.e. OUI) */
	struct ether_addr random_mask;	/* randomized bits on each scan */
	uint16 scan_bitmap;	/* scans to use this MAC address */
	uint8 pad[2];	/* 4-byte struct alignment */
} wl_scanmac_config_t;

/* scan bitmap */
#define WL_SCANMAC_SCAN_UNASSOC		(0x01 << 0)	/* unassociated scans */
#define WL_SCANMAC_SCAN_ASSOC_ROAM	(0x01 << 1)	/* associated roam scans */
#define WL_SCANMAC_SCAN_ASSOC_PNO	(0x01 << 2)	/* associated PNO scans */
#define WL_SCANMAC_SCAN_ASSOC_HOST	(0x01 << 3)	/* associated host scans */

/* no default structure packing */
#include <packed_section_end.h>

enum rssi_reason {
	RSSI_REASON_UNKNOW = 0,
	RSSI_REASON_LOWRSSI = 1,
	RSSI_REASON_NSYC = 2,
	RSSI_REASON_TIMEOUT = 3
};

enum tof_reason {
	TOF_REASON_OK = 0,
	TOF_REASON_REQEND = 1,
	TOF_REASON_TIMEOUT = 2,
	TOF_REASON_NOACK = 3,
	TOF_REASON_INVALIDAVB = 4,
	TOF_REASON_INITIAL = 5,
	TOF_REASON_ABORT = 6
};

enum rssi_state {
	RSSI_STATE_POLL = 0,
	RSSI_STATE_TPAIRING = 1,
	RSSI_STATE_IPAIRING = 2,
	RSSI_STATE_THANDSHAKE = 3,
	RSSI_STATE_IHANDSHAKE = 4,
	RSSI_STATE_CONFIRMED = 5,
	RSSI_STATE_PIPELINE = 6,
	RSSI_STATE_NEGMODE = 7,
	RSSI_STATE_MONITOR = 8,
	RSSI_STATE_LAST = 9
};

enum tof_state {
	TOF_STATE_IDLE	 = 0,
	TOF_STATE_IWAITM = 1,
	TOF_STATE_TWAITM = 2,
	TOF_STATE_ILEGACY = 3,
	TOF_STATE_IWAITCL = 4,
	TOF_STATE_TWAITCL = 5,
	TOF_STATE_ICONFIRM = 6,
	TOF_STATE_IREPORT = 7
};

enum tof_mode_type {
	TOF_LEGACY_UNKNOWN	= 0,
	TOF_LEGACY_AP		= 1,
	TOF_NONLEGACY_AP	= 2
};

enum tof_way_type {
	TOF_TYPE_ONE_WAY = 0,
	TOF_TYPE_TWO_WAY = 1,
	TOF_TYPE_REPORT = 2
};

enum tof_rate_type {
	TOF_FRAME_RATE_VHT = 0,
	TOF_FRAME_RATE_LEGACY = 1
};

#define TOF_ADJ_TYPE_NUM	4	/* number of assisted timestamp adjustment */
enum tof_adj_mode {
	TOF_ADJ_SOFTWARE = 0,
	TOF_ADJ_HARDWARE = 1,
	TOF_ADJ_SEQ = 2,
	TOF_ADJ_NONE = 3
};

#define FRAME_TYPE_NUM		4	/* number of frame type */
enum frame_type {
	FRAME_TYPE_CCK	= 0,
	FRAME_TYPE_OFDM	= 1,
	FRAME_TYPE_11N	= 2,
	FRAME_TYPE_11AC	= 3
};

typedef struct wl_proxd_status_iovar {
	uint16			method;				/* method */
	uint8			mode;				/* mode */
	uint8			peermode;			/* peer mode */
	uint8			state;				/* state */
	uint8			reason;				/* reason code */
	uint32			distance;			/* distance */
	uint32			txcnt;				/* tx pkt counter */
	uint32			rxcnt;				/* rx pkt counter */
	struct ether_addr	peer;				/* peer mac address */
	int8			avg_rssi;			/* average rssi */
	int8			hi_rssi;			/* highest rssi */
	int8			low_rssi;			/* lowest rssi */
	uint32			dbgstatus;			/* debug status */
	uint16			frame_type_cnt[FRAME_TYPE_NUM];	/* frame types */
	uint8			adj_type_cnt[TOF_ADJ_TYPE_NUM];	/* adj types HW/SW */
} wl_proxd_status_iovar_t;

#ifdef NET_DETECT
typedef struct net_detect_adapter_features {
	bool	wowl_enabled;
	bool	net_detect_enabled;
	bool	nlo_enabled;
} net_detect_adapter_features_t;

typedef enum net_detect_bss_type {
	nd_bss_any = 0,
	nd_ibss,
	nd_ess
} net_detect_bss_type_t;

typedef struct net_detect_profile {
	wlc_ssid_t		ssid;
	net_detect_bss_type_t   bss_type;	/* Ignore for now since Phase 1 is only for ESS */
	uint32			cipher_type;	/* DOT11_CIPHER_ALGORITHM enumeration values */
	uint32			auth_type;	/* DOT11_AUTH_ALGORITHM enumeration values */
} net_detect_profile_t;

typedef struct net_detect_profile_list {
	uint32			num_nd_profiles;
	net_detect_profile_t	nd_profile[0];
} net_detect_profile_list_t;

typedef struct net_detect_config {
	bool			    nd_enabled;
	uint32			    scan_interval;
	uint32			    wait_period;
	bool			    wake_if_connected;
	bool			    wake_if_disconnected;
	net_detect_profile_list_t   nd_profile_list;
} net_detect_config_t;

typedef enum net_detect_wake_reason {
	nd_reason_unknown,
	nd_net_detected,
	nd_wowl_event,
	nd_ucode_error
} net_detect_wake_reason_t;

typedef struct net_detect_wake_data {
	net_detect_wake_reason_t    nd_wake_reason;
	uint32			    nd_wake_date_length;
	uint8			    nd_wake_data[0];	    /* Wake data (currently unused) */
} net_detect_wake_data_t;

#endif /* NET_DETECT */

/* (unversioned, deprecated) */
typedef struct bcnreq {
	uint8 bcn_mode;
	int dur;
	int channel;
	struct ether_addr da;
	uint16 random_int;
	wlc_ssid_t ssid;
	uint16 reps;
} bcnreq_t;

#define WL_RRM_BCN_REQ_VER		1
typedef struct bcn_req {
	uint8 version;
	uint8 bcn_mode;
	uint8 pad_1[2];
	int32 dur;
	int32 channel;
	struct ether_addr da;
	uint16 random_int;
	wlc_ssid_t ssid;
	uint16 reps;
	uint8 req_elements;
	uint8 pad_2;
	chanspec_list_t chspec_list;
} bcn_req_t;

typedef struct rrmreq {
	struct ether_addr da;
	uint8 reg;
	uint8 chan;
	uint16 random_int;
	uint16 dur;
	uint16 reps;
} rrmreq_t;

typedef struct framereq {
	struct ether_addr da;
	uint8 reg;
	uint8 chan;
	uint16 random_int;
	uint16 dur;
	struct ether_addr ta;
	uint16 reps;
} framereq_t;

typedef struct statreq {
	struct ether_addr da;
	struct ether_addr peer;
	uint16 random_int;
	uint16 dur;
	uint8 group_id;
	uint16 reps;
} statreq_t;

#define WL_RRM_RPT_VER		0
#define WL_RRM_RPT_MAX_PAYLOAD	256
#define WL_RRM_RPT_MIN_PAYLOAD	7
#define WL_RRM_RPT_FALG_ERR	0
#define WL_RRM_RPT_FALG_GRP_ID_PROPR	(1 << 0)
#define WL_RRM_RPT_FALG_GRP_ID_0	(1 << 1)
typedef struct {
	uint16 ver;		/* version */
	struct ether_addr addr;	/* STA MAC addr */
	uint32 timestamp;	/* timestamp of the report */
	uint16 flag;		/* flag */
	uint16 len;		/* length of payload data */
	unsigned char data[WL_RRM_RPT_MAX_PAYLOAD];
} statrpt_t;

typedef struct wlc_l2keepalive_ol_params {
	uint8	flags;
	uint8	prio;
	uint16	period_ms;
} wlc_l2keepalive_ol_params_t;

typedef struct wlc_dwds_config {
	uint32		enable;
	uint32		mode; /* STA/AP interface */
	struct ether_addr ea;
} wlc_dwds_config_t;

typedef struct wl_el_set_params_s {
	uint8 set;	/* Set number */
	uint32 size;	/* Size to make/expand */
} wl_el_set_params_t;

typedef struct wl_el_tag_params_s {
	uint16 tag;
	uint8 set;
	uint8 flags;
} wl_el_tag_params_t;

/* Video Traffic Interference Monitor config */
#define INTFER_VERSION		1
typedef struct wl_intfer_params {
	uint16 version;			/* version */
	uint8 period;			/* sample period */
	uint8 cnt;			/* sample cnt */
	uint8 txfail_thresh;	/* non-TCP txfail threshold */
	uint8 tcptxfail_thresh;	/* tcptxfail threshold */
} wl_intfer_params_t;

typedef struct wl_staprio_cfg {
	struct ether_addr ea;	/* mac addr */
	uint8 prio;		/* scb priority */
} wl_staprio_cfg_t;

typedef enum wl_stamon_cfg_cmd_type {
	STAMON_CFG_CMD_DEL = 0,
	STAMON_CFG_CMD_ADD = 1
} wl_stamon_cfg_cmd_type_t;

typedef struct wlc_stamon_sta_config {
	wl_stamon_cfg_cmd_type_t cmd; /* 0 - delete, 1 - add */
	struct ether_addr ea;
} wlc_stamon_sta_config_t;

#ifdef SR_DEBUG
typedef struct /* pmu_reg */{
	uint32  pmu_control;
	uint32  pmu_capabilities;
	uint32  pmu_status;
	uint32  res_state;
	uint32  res_pending;
	uint32  pmu_timer1;
	uint32  min_res_mask;
	uint32  max_res_mask;
	uint32  pmu_chipcontrol1[4];
	uint32  pmu_regcontrol[5];
	uint32  pmu_pllcontrol[5];
	uint32  pmu_rsrc_up_down_timer[31];
	uint32  rsrc_dep_mask[31];
} pmu_reg_t;
#endif /* pmu_reg */

typedef struct wl_taf_define {
	struct ether_addr ea;	/* STA MAC or 0xFF... */
	uint16 version;         /* version */
	uint32 sch;             /* method index */
	uint32 prio;            /* priority */
	uint32 misc;            /* used for return value */
	char   text[1];         /* used to pass and return ascii text */
} wl_taf_define_t;

/* Received Beacons lengths information */
#define WL_LAST_BCNS_INFO_FIXED_LEN		OFFSETOF(wlc_bcn_len_hist_t, bcnlen_ring)
typedef struct wlc_bcn_len_hist {
	uint16	ver;				/* version field */
	uint16	cur_index;			/* current pointed index in ring buffer */
	uint32	max_bcnlen;		/* Max beacon length received */
	uint32	min_bcnlen;		/* Min beacon length received */
	uint32	ringbuff_len;		/* Length of the ring buffer 'bcnlen_ring' */
	uint32	bcnlen_ring[1];	/* ring buffer storing received beacon lengths */
} wlc_bcn_len_hist_t;

/* WDS net interface types */
#define WL_WDSIFTYPE_NONE  0x0 /* The interface type is neither WDS nor DWDS. */
#define WL_WDSIFTYPE_WDS   0x1 /* The interface is WDS type. */
#define WL_WDSIFTYPE_DWDS  0x2 /* The interface is DWDS type. */

typedef struct wl_bssload_static {
	bool is_static;
	uint16 sta_count;
	uint8 chan_util;
	uint16 aac;
} wl_bssload_static_t;


/* IO Var Operations - the Value of iov_op In wlc_ap_doiovar */
typedef enum wlc_ap_iov_operation {
	WLC_AP_IOV_OP_DELETE                   = -1,
	WLC_AP_IOV_OP_DISABLE                  = 0,
	WLC_AP_IOV_OP_ENABLE                   = 1,
	WLC_AP_IOV_OP_MANUAL_AP_BSSCFG_CREATE  = 2,
	WLC_AP_IOV_OP_MANUAL_STA_BSSCFG_CREATE = 3,
	WLC_AP_IOV_OP_MOVE                     = 4
} wlc_ap_iov_oper_t;

/* LTE coex info */
/* Analogue of HCI Set MWS Signaling cmd */
typedef struct {
	uint16	mws_rx_assert_offset;
	uint16	mws_rx_assert_jitter;
	uint16	mws_rx_deassert_offset;
	uint16	mws_rx_deassert_jitter;
	uint16	mws_tx_assert_offset;
	uint16	mws_tx_assert_jitter;
	uint16	mws_tx_deassert_offset;
	uint16	mws_tx_deassert_jitter;
	uint16	mws_pattern_assert_offset;
	uint16	mws_pattern_assert_jitter;
	uint16	mws_inact_dur_assert_offset;
	uint16	mws_inact_dur_assert_jitter;
	uint16	mws_scan_freq_assert_offset;
	uint16	mws_scan_freq_assert_jitter;
	uint16	mws_prio_assert_offset_req;
} wci2_config_t;

/* Analogue of HCI MWS Channel Params */
typedef struct {
	uint16	mws_rx_center_freq; /* MHz */
	uint16	mws_tx_center_freq;
	uint16	mws_rx_channel_bw;  /* KHz */
	uint16	mws_tx_channel_bw;
	uint8	mws_channel_en;
	uint8	mws_channel_type;   /* Don't care for WLAN? */
} mws_params_t;

/* MWS wci2 message */
typedef struct {
	uint8	mws_wci2_data; /* BT-SIG msg */
	uint16	mws_wci2_interval; /* Interval in us */
	uint16	mws_wci2_repeat; /* No of msgs to send */
} mws_wci2_msg_t;

typedef struct {
	uint32 config;	/* MODE: AUTO (-1), Disable (0), Enable (1) */
	uint32 status;	/* Current state: Disabled (0), Enabled (1) */
} wl_config_t;

#define WLC_RSDB_MODE_AUTO_MASK 0x80
#define WLC_RSDB_EXTRACT_MODE(val) ((int8)((val) & (~(WLC_RSDB_MODE_AUTO_MASK))))

#define	WL_IF_STATS_T_VERSION 1	/* current version of wl_if_stats structure */

/* per interface counters */
typedef struct wl_if_stats {
	uint16	version;		/* version of the structure */
	uint16	length;			/* length of the entire structure */
	uint32	PAD;			/* padding */

	/* transmit stat counters */
	uint64	txframe;		/* tx data frames */
	uint64	txbyte;			/* tx data bytes */
	uint64	txerror;		/* tx data errors (derived: sum of others) */
	uint64  txnobuf;		/* tx out of buffer errors */
	uint64  txrunt;			/* tx runt frames */
	uint64  txfail;			/* tx failed frames */
	uint64	txretry;		/* tx retry frames */
	uint64	txretrie;		/* tx multiple retry frames */
	uint64	txfrmsnt;		/* tx sent frames */
	uint64	txmulti;		/* tx mulitcast sent frames */
	uint64	txfrag;			/* tx fragments sent */

	/* receive stat counters */
	uint64	rxframe;		/* rx data frames */
	uint64	rxbyte;			/* rx data bytes */
	uint64	rxerror;		/* rx data errors (derived: sum of others) */
	uint64	rxnobuf;		/* rx out of buffer errors */
	uint64  rxrunt;			/* rx runt frames */
	uint64  rxfragerr;		/* rx fragment errors */
	uint64	rxmulti;		/* rx multicast frames */
}
wl_if_stats_t;

typedef struct wl_band {
	uint16		bandtype;		/* WL_BAND_2G, WL_BAND_5G */
	uint16		bandunit;		/* bandstate[] index */
	uint16		phytype;		/* phytype */
	uint16		phyrev;
}
wl_band_t;

#define	WL_WLC_VERSION_T_VERSION 1 /* current version of wlc_version structure */

/* wlc interface version */
typedef struct wl_wlc_version {
	uint16	version;		/* version of the structure */
	uint16	length;			/* length of the entire structure */

	/* epi version numbers */
	uint16	epi_ver_major;		/* epi major version number */
	uint16	epi_ver_minor;		/* epi minor version number */
	uint16	epi_rc_num;		/* epi RC number */
	uint16	epi_incr_num;		/* epi increment number */

	/* wlc interface version numbers */
	uint16	wlc_ver_major;		/* wlc interface major version number */
	uint16	wlc_ver_minor;		/* wlc interface minor version number */
}
wl_wlc_version_t;

/* Version of WLC interface to be returned as a part of wl_wlc_version structure.
 * For the discussion related to versions update policy refer to
 * http://hwnbu-twiki.broadcom.com/bin/view/Mwgroup/WlShimAbstractionLayer
 * For now the policy is to increment WLC_VERSION_MAJOR each time
 * there is a change that involves both WLC layer and per-port layer.
 * WLC_VERSION_MINOR is currently not in use.
 */
#define WLC_VERSION_MAJOR	3
#define WLC_VERSION_MINOR	0

/* begin proxd definitions */
#include <packed_section_start.h>

#define WL_PROXD_API_VERSION 0x0300	/* version 3.0 */

/* Minimum supported API version */
#define WL_PROXD_API_MIN_VERSION 0x0300

/* proximity detection methods */
enum {
	WL_PROXD_METHOD_NONE	= 0,
	WL_PROXD_METHOD_RSVD1	= 1, /* backward compatibility - RSSI, not supported */
	WL_PROXD_METHOD_TOF		= 2,
	WL_PROXD_METHOD_RSVD2	= 3, /* 11v only - if needed */
	WL_PROXD_METHOD_FTM		= 4, /* IEEE rev mc/2014 */
	WL_PROXD_METHOD_MAX
};
typedef int16 wl_proxd_method_t;

/* global and method configuration flags */
enum {
	WL_PROXD_FLAG_NONE 			= 0x00000000,
	WL_PROXD_FLAG_RX_ENABLED 	= 0x00000001, /* respond to requests */
	WL_PROXD_FLAG_RX_RANGE_REQ	= 0x00000002, /* 11mc range requests enabled */
	WL_PROXD_FLAG_TX_LCI		= 0x00000004, /* transmit location, if available */
	WL_PROXD_FLAG_TX_CIVIC		= 0x00000008, /* tx civic loc, if available */
	WL_PROXD_FLAG_RX_AUTO_BURST	= 0x00000010, /* respond to requests w/o host action */
	WL_PROXD_FLAG_TX_AUTO_BURST	= 0x00000020, /* continue requests w/o host action */
	WL_PROXD_FLAG_AVAIL_PUBLISH = 0x00000040, /* publish availability */
	WL_PROXD_FLAG_AVAIL_SCHEDULE = 0x00000080, /* schedule using availability */
	WL_PROXD_FLAG_ALL 			= 0xffffffff
};
typedef uint32 wl_proxd_flags_t;

#define WL_PROXD_FLAGS_AVAIL (WL_PROXD_FLAG_AVAIL_PUBLISH | \
	WL_PROXD_FLAG_AVAIL_SCHEDULE)

/* session flags */
enum {
	WL_PROXD_SESSION_FLAG_NONE 			= 0x00000000,  /* no flags */
	WL_PROXD_SESSION_FLAG_INITIATOR 	= 0x00000001,  /* local device is initiator */
	WL_PROXD_SESSION_FLAG_TARGET 		= 0x00000002,  /* local device is target */
	WL_PROXD_SESSION_FLAG_ONE_WAY		= 0x00000004,  /* (initiated) 1-way rtt */
	WL_PROXD_SESSION_FLAG_AUTO_BURST	= 0x00000008,  /* created w/ rx_auto_burst */
	WL_PROXD_SESSION_FLAG_PERSIST		= 0x00000010,  /* good until cancelled */
	WL_PROXD_SESSION_FLAG_RTT_DETAIL	= 0x00000020,  /* rtt detail in results */
	WL_PROXD_SESSION_FLAG_TOF_COMPAT	= 0x00000040,  /* TOF  compatibility - TBD */
	WL_PROXD_SESSION_FLAG_AOA			= 0x00000080,  /* AOA along w/ RTT */
	WL_PROXD_SESSION_FLAG_RX_AUTO_BURST	= 0x00000100,  /* Same as proxd flags above */
	WL_PROXD_SESSION_FLAG_TX_AUTO_BURST	= 0x00000200,  /* Same as proxd flags above */
	WL_PROXD_SESSION_FLAG_NAN_BSS		= 0x00000400,  /* Use NAN BSS, if applicable */
	WL_PROXD_SESSION_FLAG_TS1			= 0x00000800,  /* e.g. FTM1 - cap or rx */
	WL_PROXD_SESSION_FLAG_REPORT_FAILURE= 0x00002000, /* report failure to target */
	WL_PROXD_SESSION_FLAG_INITIATOR_RPT	= 0x00004000, /* report distance to target */
	WL_PROXD_SESSION_FLAG_NOCHANSWT		= 0x00008000, /* No channel switching */
	WL_PROXD_SESSION_FLAG_NETRUAL		= 0x00010000, /* netrual mode */
	WL_PROXD_SESSION_FLAG_SEQ_EN		= 0x00020000, /* Toast */
	WL_PROXD_SESSION_FLAG_NO_PARAM_OVRD	= 0x00040000, /* no param override from target */
	WL_PROXD_SESSION_FLAG_ASAP			= 0x00080000, /* ASAP session */
	WL_PROXD_SESSION_FLAG_REQ_LCI		= 0x00100000, /* transmit LCI req */
	WL_PROXD_SESSION_FLAG_REQ_CIV		= 0x00200000, /* transmit civic loc req */
	WL_PROXD_SESSION_FLAG_COLLECT		= 0x80000000,	/* debug - collect */
	WL_PROXD_SESSION_FLAG_ALL 			= 0xffffffff
};
typedef uint32 wl_proxd_session_flags_t;

/* time units - mc supports up to 0.1ns resolution */
enum {
	WL_PROXD_TMU_TU			= 0,		/* 1024us */
	WL_PROXD_TMU_SEC		= 1,
	WL_PROXD_TMU_MILLI_SEC	= 2,
	WL_PROXD_TMU_MICRO_SEC	= 3,
	WL_PROXD_TMU_NANO_SEC	= 4,
	WL_PROXD_TMU_PICO_SEC	= 5
};
typedef int16 wl_proxd_tmu_t;

/* time interval e.g. 10ns */
typedef struct wl_proxd_intvl {
	uint32 intvl;
	wl_proxd_tmu_t tmu;
	uint8	pad[2];
} wl_proxd_intvl_t;

/* commands that can apply to proxd, method or a session */
enum {
	WL_PROXD_CMD_NONE				= 0,
	WL_PROXD_CMD_GET_VERSION		= 1,
	WL_PROXD_CMD_ENABLE 			= 2,
	WL_PROXD_CMD_DISABLE 			= 3,
	WL_PROXD_CMD_CONFIG 			= 4,
	WL_PROXD_CMD_START_SESSION 		= 5,
	WL_PROXD_CMD_BURST_REQUEST 		= 6,
	WL_PROXD_CMD_STOP_SESSION 		= 7,
	WL_PROXD_CMD_DELETE_SESSION 	= 8,
	WL_PROXD_CMD_GET_RESULT 		= 9,
	WL_PROXD_CMD_GET_INFO 			= 10,
	WL_PROXD_CMD_GET_STATUS 		= 11,
	WL_PROXD_CMD_GET_SESSIONS 		= 12,
	WL_PROXD_CMD_GET_COUNTERS 		= 13,
	WL_PROXD_CMD_CLEAR_COUNTERS 	= 14,
	WL_PROXD_CMD_COLLECT 			= 15,
	WL_PROXD_CMD_TUNE 				= 16,
	WL_PROXD_CMD_DUMP 				= 17,
	WL_PROXD_CMD_START_RANGING		= 18,
	WL_PROXD_CMD_STOP_RANGING		= 19,
	WL_PROXD_CMD_GET_RANGING_INFO	= 20,
	WL_PROXD_CMD_IS_TLV_SUPPORTED	= 21,

	WL_PROXD_CMD_MAX
};
typedef int16 wl_proxd_cmd_t;

/* session ids:
 * id 0 is reserved
 * ids 1..0x7fff - allocated by host/app
 * 0x8000-0xffff - allocated by firmware, used for auto/rx
 */
enum {
	 WL_PROXD_SESSION_ID_GLOBAL = 0
};

#define WL_PROXD_SID_HOST_MAX 0x7fff
#define WL_PROXD_SID_HOST_ALLOC(_sid) ((_sid) > 0 && (_sid) <= WL_PROXD_SID_HOST_MAX)

/* maximum number sessions that can be allocated, may be less if tunable */
#define WL_PROXD_MAX_SESSIONS 16

typedef uint16 wl_proxd_session_id_t;

/* status - TBD BCME_ vs proxd status - range reserved for BCME_ */
enum {
	WL_PROXD_E_POLICY			= -1045,
	WL_PROXD_E_INCOMPLETE		= -1044,
	WL_PROXD_E_OVERRIDDEN		= -1043,
	WL_PROXD_E_ASAP_FAILED		= -1042,
	WL_PROXD_E_NOTSTARTED		= -1041,
	WL_PROXD_E_INVALIDAVB		= -1040,
	WL_PROXD_E_INCAPABLE		= -1039,
	WL_PROXD_E_MISMATCH			= -1038,
	WL_PROXD_E_DUP_SESSION		= -1037,
	WL_PROXD_E_REMOTE_FAIL		= -1036,
	WL_PROXD_E_REMOTE_INCAPABLE = -1035,
	WL_PROXD_E_SCHED_FAIL		= -1034,
	WL_PROXD_E_PROTO			= -1033,
	WL_PROXD_E_EXPIRED			= -1032,
	WL_PROXD_E_TIMEOUT			= -1031,
	WL_PROXD_E_NOACK			= -1030,
	WL_PROXD_E_DEFERRED			= -1029,
	WL_PROXD_E_INVALID_SID		= -1028,
	WL_PROXD_E_REMOTE_CANCEL 	= -1027,
	WL_PROXD_E_CANCELED			= -1026,	/* local */
	WL_PROXD_E_INVALID_SESSION	= -1025,
	WL_PROXD_E_BAD_STATE		= -1024,
	WL_PROXD_E_ERROR			= -1,
	WL_PROXD_E_OK				= 0
};
typedef int32 wl_proxd_status_t;

/* session states */
enum {
	WL_PROXD_SESSION_STATE_NONE				= 0,
	WL_PROXD_SESSION_STATE_CREATED			= 1,
	WL_PROXD_SESSION_STATE_CONFIGURED		= 2,
	WL_PROXD_SESSION_STATE_STARTED			= 3,
	WL_PROXD_SESSION_STATE_DELAY			= 4,
	WL_PROXD_SESSION_STATE_USER_WAIT		= 5,
	WL_PROXD_SESSION_STATE_SCHED_WAIT		= 6,
	WL_PROXD_SESSION_STATE_BURST			= 7,
	WL_PROXD_SESSION_STATE_STOPPING			= 8,
	WL_PROXD_SESSION_STATE_ENDED			= 9,
	WL_PROXD_SESSION_STATE_DESTROYING		= -1
};
typedef int16 wl_proxd_session_state_t;

/* RTT sample flags */
enum {
	WL_PROXD_RTT_SAMPLE_NONE 		= 0x00,
	WL_PROXD_RTT_SAMPLE_DISCARD 	= 0x01
};
typedef uint8 wl_proxd_rtt_sample_flags_t;

typedef struct wl_proxd_rtt_sample {
	uint8				id;			/* id for the sample - non-zero */
	wl_proxd_rtt_sample_flags_t	flags;
	int16				rssi;
	wl_proxd_intvl_t	rtt;		/* round trip time */
	uint32 				ratespec;
} wl_proxd_rtt_sample_t;

/*  result flags */
enum {
	WL_PRXOD_RESULT_FLAG_NONE	= 0x0000,
	WL_PROXD_RESULT_FLAG_NLOS	= 0x0001,	/* LOS - if available */
	WL_PROXD_RESULT_FLAG_LOS    = 0x0002,	/* NLOS - if available */
	WL_PROXD_RESULT_FLAG_FATAL	= 0x0004,	/* Fatal error during burst */
	WL_PROXD_RESULT_FLAG_ALL 	= 0xffff
};
typedef int16 wl_proxd_result_flags_t;

/* rtt measurement result */
typedef struct wl_proxd_rtt_result {
	wl_proxd_session_id_t			sid;
	wl_proxd_result_flags_t 		flags;
	wl_proxd_status_t				status;
	struct ether_addr				peer;
	wl_proxd_session_state_t 		state; 		/* current state */
	union {
		wl_proxd_intvl_t			retry_after; /* hint for errors */
		wl_proxd_intvl_t			burst_duration; /* burst duration */
	} u;
	wl_proxd_rtt_sample_t			avg_rtt;
	uint32							avg_dist;	/* 1/256m units */
	uint16							sd_rtt;	/* RTT standard deviation */
	uint8						num_valid_rtt; /* valid rtt cnt */
	uint8						num_ftm; /* actual num of ftm cnt */
	uint16							burst_num;	/* in a session */
	uint16							num_rtt;	/* 0 if no detail */
	wl_proxd_rtt_sample_t			rtt[1];		/* variable */
} wl_proxd_rtt_result_t;

/* aoa measurement result */
typedef struct wl_proxd_aoa_result {
	wl_proxd_session_id_t			sid;
	wl_proxd_result_flags_t			flags;
	wl_proxd_status_t				status;
	struct ether_addr				peer;
	wl_proxd_session_state_t 		state;
	uint16							burst_num;
	uint8							pad[2];
	/* wl_proxd_aoa_sample_t sample_avg; TBD */
} BWL_POST_PACKED_STRUCT wl_proxd_aoa_result_t;

/* global stats */
typedef struct wl_proxd_counters {
	uint32 tx;					/* tx frame count */
	uint32 rx;					/* rx frame count */
	uint32 burst;				/* total number of burst */
	uint32 sessions;			/* total number of sessions */
	uint32 max_sessions;		/* max concurrency */
	uint32 sched_fail;			/* scheduling failures */
	uint32 timeouts;			/* timeouts */
	uint32 protoerr;			/* protocol errors */
	uint32 noack;				/* tx w/o ack */
	uint32 txfail;				/* any tx falure */
	uint32 lci_req_tx;			/* tx LCI requests */
	uint32 lci_req_rx;			/* rx LCI requests */
	uint32 lci_rep_tx;			/* tx LCI reports */
	uint32 lci_rep_rx;			/* rx LCI reports */
	uint32 civic_req_tx;		/* tx civic requests */
	uint32 civic_req_rx;		/* rx civic requests */
	uint32 civic_rep_tx;		/* tx civic reports */
	uint32 civic_rep_rx;		/* rx civic reports */
	uint32 rctx;				/* ranging contexts created */
	uint32 rctx_done;			/* count of ranging done */
	uint32 publish_err;     /* availability publishing errors */
	uint32 on_chan;         /* count of scheduler onchan */
	uint32 off_chan;        /* count of scheduler offchan */
} wl_proxd_counters_t;

typedef struct wl_proxd_counters wl_proxd_session_counters_t;

enum {
	WL_PROXD_CAP_NONE 		= 0x0000,
	WL_PROXD_CAP_ALL 		= 0xffff
};
typedef int16 wl_proxd_caps_t;

/* method capabilities */
enum {
	WL_PROXD_FTM_CAP_NONE = 0x0000,
	WL_PROXD_FTM_CAP_FTM1 = 0x0001
};
typedef uint16 wl_proxd_ftm_caps_t;

typedef struct BWL_PRE_PACKED_STRUCT wl_proxd_tlv_id_list {
	uint16			num_ids;
	uint16			ids[1];
} BWL_POST_PACKED_STRUCT wl_proxd_tlv_id_list_t;

typedef struct wl_proxd_session_id_list {
	uint16 num_ids;
	wl_proxd_session_id_t ids[1];
} wl_proxd_session_id_list_t;

/* tlvs returned for get_info on ftm method
 * configuration:
 * 		proxd flags
 *  	event mask
 *  	debug mask
 *  	session defaults (session tlvs)
 * status tlv - not supported for ftm method
 * info tlv
 */
typedef struct wl_proxd_ftm_info {
	wl_proxd_ftm_caps_t caps;
	uint16 max_sessions;
	uint16 num_sessions;
	uint16 rx_max_burst;
} wl_proxd_ftm_info_t;

/* tlvs returned for get_info on session
 * session config (tlvs)
 * session info tlv
 */
typedef struct wl_proxd_ftm_session_info {
	uint16 sid;
	uint8 bss_index;
	uint8 pad;
	struct ether_addr bssid;
	wl_proxd_session_state_t state;
	wl_proxd_status_t status;
	uint16	burst_num;
} wl_proxd_ftm_session_info_t;

typedef struct wl_proxd_ftm_session_status {
	uint16 sid;
	wl_proxd_session_state_t state;
	wl_proxd_status_t status;
	uint16	burst_num;
} wl_proxd_ftm_session_status_t;

/* rrm range request */
typedef struct wl_proxd_range_req {
	uint16 			num_repeat;
	uint16			init_delay_range;	/* in TUs */
	uint8			pad;
	uint8			num_nbr;			/* number of (possible) neighbors */
	nbr_element_t   nbr[1];
} wl_proxd_range_req_t;

#define WL_PROXD_LCI_LAT_OFF 	0
#define WL_PROXD_LCI_LONG_OFF 	5
#define WL_PROXD_LCI_ALT_OFF 	10

#define WL_PROXD_LCI_GET_LAT(_lci, _lat, _lat_err) { \
	unsigned _off = WL_PROXD_LCI_LAT_OFF; \
	_lat_err = (_lci)->data[(_off)] & 0x3f; \
	_lat = (_lci)->data[(_off)+1]; \
	_lat |= (_lci)->data[(_off)+2] << 8; \
	_lat |= (_lci)->data[_(_off)+3] << 16; \
	_lat |= (_lci)->data[(_off)+4] << 24; \
	_lat <<= 2; \
	_lat |= (_lci)->data[(_off)] >> 6; \
}

#define WL_PROXD_LCI_GET_LONG(_lci, _lcilong, _long_err) { \
	unsigned _off = WL_PROXD_LCI_LONG_OFF; \
	_long_err = (_lci)->data[(_off)] & 0x3f; \
	_lcilong = (_lci)->data[(_off)+1]; \
	_lcilong |= (_lci)->data[(_off)+2] << 8; \
	_lcilong |= (_lci)->data[_(_off)+3] << 16; \
	_lcilong |= (_lci)->data[(_off)+4] << 24; \
	__lcilong <<= 2; \
	_lcilong |= (_lci)->data[(_off)] >> 6; \
}

#define WL_PROXD_LCI_GET_ALT(_lci, _alt_type, _alt, _alt_err) { \
	unsigned _off = WL_PROXD_LCI_ALT_OFF; \
	_alt_type = (_lci)->data[_off] & 0x0f; \
	_alt_err = (_lci)->data[(_off)] >> 4; \
	_alt_err |= ((_lci)->data[(_off)+1] & 0x03) << 4; \
	_alt = (_lci)->data[(_off)+2]; \
	_alt |= (_lci)->data[(_off)+3] << 8; \
	_alt |= (_lci)->data[_(_off)+4] << 16; \
	_alt <<= 6; \
	_alt |= (_lci)->data[(_off) + 1] >> 2; \
}

#define WL_PROXD_LCI_VERSION(_lci) ((_lci)->data[15] >> 6)

/* availability. advertising mechanism bss specific */
/* availablity flags */
enum {
	WL_PROXD_AVAIL_NONE = 0,
	WL_PROXD_AVAIL_NAN_PUBLISHED = 0x0001,
	WL_PROXD_AVAIL_SCHEDULED = 0x0002        /* scheduled by proxd */
};
typedef int16 wl_proxd_avail_flags_t;

/* time reference */
enum {
	WL_PROXD_TREF_NONE = 0,
	WL_PROXD_TREF_DEV_TSF = 1,
	WL_PROXD_TREF_NAN_DW = 2,
	WL_PROXD_TREF_TBTT = 3,
	WL_PROXD_TREF_MAX		/* last entry */
};
typedef int16 wl_proxd_time_ref_t;

/* proxd channel-time slot */
typedef struct {
	wl_proxd_intvl_t start;         /* from ref */
	wl_proxd_intvl_t duration;      /* from start */
	uint32  chanspec;
} wl_proxd_time_slot_t;

typedef struct wl_proxd_avail24 {
	wl_proxd_avail_flags_t flags; /* for query only */
	wl_proxd_time_ref_t time_ref;
	uint16	max_slots; /* for query only */
	uint16  num_slots;
	wl_proxd_time_slot_t slots[1];	/* ROM compat - not used */
	wl_proxd_intvl_t 	repeat;
	wl_proxd_time_slot_t ts0[1];
} wl_proxd_avail24_t;
#define WL_PROXD_AVAIL24_TIMESLOT(_avail24, _i) (&(_avail24)->ts0[(_i)])
#define WL_PROXD_AVAIL24_TIMESLOT_OFFSET(_avail24) OFFSETOF(wl_proxd_avail24_t, ts0)
#define WL_PROXD_AVAIL24_TIMESLOTS(_avail24) WL_PROXD_AVAIL24_TIMESLOT(_avail24, 0)
#define WL_PROXD_AVAIL24_SIZE(_avail24, _num_slots) (\
	WL_PROXD_AVAIL24_TIMESLOT_OFFSET(_avail24) + \
	(_num_slots) * sizeof(*WL_PROXD_AVAIL24_TIMESLOT(_avail24, 0)))

typedef struct wl_proxd_avail {
	wl_proxd_avail_flags_t flags; /* for query only */
	wl_proxd_time_ref_t time_ref;
	uint16	max_slots; /* for query only */
	uint16  num_slots;
	wl_proxd_intvl_t 	repeat;
	wl_proxd_time_slot_t slots[1];
} wl_proxd_avail_t;
#define WL_PROXD_AVAIL_TIMESLOT(_avail, _i) (&(_avail)->slots[(_i)])
#define WL_PROXD_AVAIL_TIMESLOT_OFFSET(_avail) OFFSETOF(wl_proxd_avail_t, slots)

#define WL_PROXD_AVAIL_TIMESLOTS(_avail) WL_PROXD_AVAIL_TIMESLOT(_avail, 0)
#define WL_PROXD_AVAIL_SIZE(_avail, _num_slots) (\
	WL_PROXD_AVAIL_TIMESLOT_OFFSET(_avail) + \
	(_num_slots) * sizeof(*WL_PROXD_AVAIL_TIMESLOT(_avail, 0)))

/* collect support TBD */

/* debugging */
enum {
	WL_PROXD_DEBUG_NONE		= 0x00000000,
	WL_PROXD_DEBUG_LOG		= 0x00000001,
	WL_PROXD_DEBUG_IOV		= 0x00000002,
	WL_PROXD_DEBUG_EVENT	= 0x00000004,
	WL_PROXD_DEBUG_SESSION	= 0x00000008,
	WL_PROXD_DEBUG_PROTO	= 0x00000010,
	WL_PROXD_DEBUG_SCHED	= 0x00000020,
	WL_PROXD_DEBUG_RANGING	= 0x00000040,
	WL_PROXD_DEBUG_ALL		= 0xffffffff
};
typedef uint32 wl_proxd_debug_mask_t;

/* tlv IDs - data length 4 bytes unless overridden by type, alignment 32 bits */
enum {
	WL_PROXD_TLV_ID_NONE 			= 0,
	WL_PROXD_TLV_ID_METHOD 			= 1,
	WL_PROXD_TLV_ID_FLAGS 			= 2,
	WL_PROXD_TLV_ID_CHANSPEC 		= 3,	/* note: uint32 */
	WL_PROXD_TLV_ID_TX_POWER 		= 4,
	WL_PROXD_TLV_ID_RATESPEC 		= 5,
	WL_PROXD_TLV_ID_BURST_DURATION 	= 6, 	/* intvl - length of burst */
	WL_PROXD_TLV_ID_BURST_PERIOD 	= 7,	/* intvl - between bursts */
	WL_PROXD_TLV_ID_BURST_FTM_SEP 	= 8,	/* intvl - between FTMs */
	WL_PROXD_TLV_ID_BURST_NUM_FTM 	= 9,	/* uint16 - per burst */
	WL_PROXD_TLV_ID_NUM_BURST 		= 10,	/* uint16 */
	WL_PROXD_TLV_ID_FTM_RETRIES 	= 11,	/* uint16 at FTM level */
	WL_PROXD_TLV_ID_BSS_INDEX		= 12,	/* uint8 */
	WL_PROXD_TLV_ID_BSSID 			= 13,
	WL_PROXD_TLV_ID_INIT_DELAY 		= 14,  	/* intvl - optional, non-standalone only */
	WL_PROXD_TLV_ID_BURST_TIMEOUT	= 15,	/* expect response within - intvl */
	WL_PROXD_TLV_ID_EVENT_MASK 		= 16,	/* interested events - in/out */
	WL_PROXD_TLV_ID_FLAGS_MASK 		= 17,	/* interested flags - in only */
	WL_PROXD_TLV_ID_PEER_MAC		= 18,	/* mac address of peer */
	WL_PROXD_TLV_ID_FTM_REQ			= 19,	/* dot11_ftm_req */
	WL_PROXD_TLV_ID_LCI_REQ 		= 20,
	WL_PROXD_TLV_ID_LCI 			= 21,
	WL_PROXD_TLV_ID_CIVIC_REQ		= 22,
	WL_PROXD_TLV_ID_CIVIC			= 23,
	WL_PROXD_TLV_ID_AVAIL24			= 24,		/* ROM compatibility */
	WL_PROXD_TLV_ID_SESSION_FLAGS	= 25,
	WL_PROXD_TLV_ID_SESSION_FLAGS_MASK	= 26,	/* in only */
	WL_PROXD_TLV_ID_RX_MAX_BURST = 27,		/* uint16 - limit bursts per session */
	WL_PROXD_TLV_ID_RANGING_INFO	= 28,	/* ranging info */
	WL_PROXD_TLV_ID_RANGING_FLAGS	= 29,	/* uint16 */
	WL_PROXD_TLV_ID_RANGING_FLAGS_MASK	= 30,	/* uint16, in only */
	WL_PROXD_TLV_ID_NAN_MAP_ID          = 31,
	WL_PROXD_TLV_ID_DEV_ADDR            = 32,
	WL_PROXD_TLV_ID_AVAIL			= 33,		/* wl_proxd_avail_t  */
	WL_PROXD_TLV_ID_TLV_ID        = 34,    /* uint16 tlv-id */
	WL_PROXD_TLV_ID_FTM_REQ_RETRIES  = 35, /* uint16 FTM request retries */

	/* output - 512 + x */
	WL_PROXD_TLV_ID_STATUS 			= 512,
	WL_PROXD_TLV_ID_COUNTERS 		= 513,
	WL_PROXD_TLV_ID_INFO 			= 514,
	WL_PROXD_TLV_ID_RTT_RESULT 		= 515,
	WL_PROXD_TLV_ID_AOA_RESULT		= 516,
	WL_PROXD_TLV_ID_SESSION_INFO 	= 517,
	WL_PROXD_TLV_ID_SESSION_STATUS	= 518,
	WL_PROXD_TLV_ID_SESSION_ID_LIST = 519,

	/* debug tlvs can be added starting 1024 */
	WL_PROXD_TLV_ID_DEBUG_MASK	= 1024,
	WL_PROXD_TLV_ID_COLLECT 	= 1025,		/* output only */
	WL_PROXD_TLV_ID_STRBUF		= 1026,

	WL_PROXD_TLV_ID_MAX
};

typedef struct wl_proxd_tlv {
	uint16 id;
	uint16 len;
	uint8  data[1];
} wl_proxd_tlv_t;

/* proxd iovar - applies to proxd, method or session */
typedef struct wl_proxd_iov {
	uint16 					version;
	uint16 					len;
	wl_proxd_cmd_t 			cmd;
	wl_proxd_method_t 		method;
	wl_proxd_session_id_t 	sid;
	uint8					pad[2];
	wl_proxd_tlv_t 			tlvs[1];	/* variable */
} wl_proxd_iov_t;

#define WL_PROXD_IOV_HDR_SIZE OFFSETOF(wl_proxd_iov_t, tlvs)

/* The following event definitions may move to bcmevent.h, but sharing proxd types
 * across needs more invasive changes unrelated to proxd
 */
enum {
	WL_PROXD_EVENT_NONE				= 0,	/* not an event, reserved */
	WL_PROXD_EVENT_SESSION_CREATE	= 1,
	WL_PROXD_EVENT_SESSION_START	= 2,
	WL_PROXD_EVENT_FTM_REQ			= 3,
	WL_PROXD_EVENT_BURST_START		= 4,
	WL_PROXD_EVENT_BURST_END		= 5,
	WL_PROXD_EVENT_SESSION_END		= 6,
	WL_PROXD_EVENT_SESSION_RESTART	= 7,
	WL_PROXD_EVENT_BURST_RESCHED	= 8,	/* burst rescheduled - e.g. partial TSF */
	WL_PROXD_EVENT_SESSION_DESTROY	= 9,
	WL_PROXD_EVENT_RANGE_REQ 		= 10,
	WL_PROXD_EVENT_FTM_FRAME		= 11,
	WL_PROXD_EVENT_DELAY			= 12,
	WL_PROXD_EVENT_VS_INITIATOR_RPT = 13,	/* (target) rx initiator-report */
	WL_PROXD_EVENT_RANGING			= 14,
	WL_PROXD_EVENT_LCI_MEAS_REP 	= 15,	/* LCI measurement report */
	WL_PROXD_EVENT_CIVIC_MEAS_REP 	= 16,	/* civic measurement report */

	WL_PROXD_EVENT_MAX
};
typedef int16 wl_proxd_event_type_t;

/* proxd event mask - upto 32 events for now */
typedef uint32 wl_proxd_event_mask_t;

#define WL_PROXD_EVENT_MASK_ALL 0xfffffffe
#define WL_PROXD_EVENT_MASK_EVENT(_event_type) (1 << (_event_type))
#define WL_PROXD_EVENT_ENABLED(_mask, _event_type) (\
	((_mask) & WL_PROXD_EVENT_MASK_EVENT(_event_type)) != 0)

/* proxd event - applies to proxd, method or session */
typedef struct wl_proxd_event {
	uint16					version;
	uint16					len;
	wl_proxd_event_type_t 	type;
	wl_proxd_method_t 		method;
	wl_proxd_session_id_t 	sid;
	uint8					pad[2];
	wl_proxd_tlv_t 			tlvs[1];	/* variable */
} wl_proxd_event_t;

enum {
	WL_PROXD_RANGING_STATE_NONE = 0,
	WL_PROXD_RANGING_STATE_NOTSTARTED = 1,
	WL_PROXD_RANGING_STATE_INPROGRESS = 2,
	WL_PROXD_RANGING_STATE_DONE = 3
};
typedef int16 wl_proxd_ranging_state_t;

/* proxd ranging flags */
enum {
	WL_PROXD_RANGING_FLAG_NONE = 0x0000,  /* no flags */
	WL_PROXD_RANGING_FLAG_DEL_SESSIONS_ON_STOP = 0x0001,
	WL_PROXD_RANGING_FLAG_ALL = 0xffff
};
typedef uint16 wl_proxd_ranging_flags_t;

struct wl_proxd_ranging_info {
	wl_proxd_status_t   status;
	wl_proxd_ranging_state_t state;
	wl_proxd_ranging_flags_t flags;
	uint16	num_sids;
	uint16	num_done;
};
typedef struct wl_proxd_ranging_info wl_proxd_ranging_info_t;
#include <packed_section_end.h>
/* end proxd definitions */

/* require strict packing */
#include <packed_section_start.h>
/* Data returned by the bssload_report iovar.
 * This is also the WLC_E_BSS_LOAD event data.
 */
typedef BWL_PRE_PACKED_STRUCT struct wl_bssload {
	uint16 sta_count;		/* station count */
	uint16 aac;			/* available admission capacity */
	uint8 chan_util;		/* channel utilization */
} BWL_POST_PACKED_STRUCT wl_bssload_t;

/* Maximum number of configurable BSS Load levels.  The number of BSS Load
 * ranges is always 1 more than the number of configured levels.  eg. if
 * 3 levels of 10, 20, 30 are configured then this defines 4 load ranges:
 * 0-10, 11-20, 21-30, 31-255.  A WLC_E_BSS_LOAD event is generated each time
 * the utilization level crosses into another range, subject to the rate limit.
 */
#define MAX_BSSLOAD_LEVELS 8
#define MAX_BSSLOAD_RANGES (MAX_BSSLOAD_LEVELS + 1)

/* BSS Load event notification configuration. */
typedef struct wl_bssload_cfg {
	uint32 rate_limit_msec;	/* # of events posted to application will be limited to
				 * one per specified period (0 to disable rate limit).
				 */
	uint8 num_util_levels;	/* Number of entries in util_levels[] below */
	uint8 util_levels[MAX_BSSLOAD_LEVELS];
				/* Variable number of BSS Load utilization levels in
				 * low to high order.  An event will be posted each time
				 * a received beacon's BSS Load IE channel utilization
				 * value crosses a level.
				 */
} wl_bssload_cfg_t;

/* Multiple roaming profile suport */
#define WL_MAX_ROAM_PROF_BRACKETS	4

#define WL_MAX_ROAM_PROF_VER	1

#define WL_ROAM_PROF_NONE	(0 << 0)
#define WL_ROAM_PROF_LAZY	(1 << 0)
#define WL_ROAM_PROF_NO_CI	(1 << 1)
#define WL_ROAM_PROF_SUSPEND	(1 << 2)
#define WL_ROAM_PROF_SYNC_DTIM	(1 << 6)
#define WL_ROAM_PROF_DEFAULT	(1 << 7)	/* backward compatible single default profile */

#define WL_FACTOR_TABLE_MAX_LIMIT 5

typedef struct wl_roam_prof {
	int8	roam_flags;		/* bit flags */
	int8	roam_trigger;		/* RSSI trigger level per profile/RSSI bracket */
	int8	rssi_lower;
	int8	roam_delta;
	int8	rssi_boost_thresh;	/* Min RSSI to qualify for RSSI boost */
	int8	rssi_boost_delta;	/* RSSI boost for AP in the other band */
	uint16	nfscan;			/* nuber of full scan to start with */
	uint16	fullscan_period;
	uint16	init_scan_period;
	uint16	backoff_multiplier;
	uint16	max_scan_period;
	uint8		channel_usage;
	uint8		cu_avg_calc_dur;
} wl_roam_prof_t;

typedef struct wl_roam_prof_band {
	uint32	band;			/* Must be just one band */
	uint16	ver;			/* version of this struct */
	uint16	len;			/* length in bytes of this structure */
	wl_roam_prof_t roam_prof[WL_MAX_ROAM_PROF_BRACKETS];
} wl_roam_prof_band_t;

/* Data structures for Interface Create/Remove  */

#define WL_INTERFACE_CREATE_VER	(0)

/*
 * The flags filed of the wl_interface_create is designed to be
 * a Bit Mask. As of now only Bit 0 and Bit 1 are used as mentioned below.
 * The rest of the bits can be used, incase we have to provide
 * more information to the dongle
 */

/*
 * Bit 0 of flags field is used to inform whether the interface requested to
 * be created is STA or AP.
 * 0 - Create a STA interface
 * 1 - Create an AP interface
 */
#define WL_INTERFACE_CREATE_STA	(0 << 0)
#define WL_INTERFACE_CREATE_AP	(1 << 0)

/*
 * Bit 1 of flags field is used to inform whether MAC is present in the
 * data structure or not.
 * 0 - Ignore mac_addr field
 * 1 - Use the mac_addr field
 */
#define WL_INTERFACE_MAC_DONT_USE	(0 << 1)
#define WL_INTERFACE_MAC_USE		(1 << 1)

typedef struct wl_interface_create {
	uint16	ver;			/* version of this struct */
	uint32  flags;			/* flags that defines the operation */
	struct	ether_addr   mac_addr;	/* Optional Mac address */
} wl_interface_create_t;

typedef struct wl_interface_info {
	uint16	ver;			/* version of this struct */
	struct ether_addr    mac_addr;	/* MAC address of the interface */
	char	ifname[BCM_MSG_IFNAME_MAX]; /* name of interface */
	uint8	bsscfgidx;		/* source bsscfg index */
} wl_interface_info_t;

/* no default structure packing */
#include <packed_section_end.h>

#define TBOW_MAX_SSID_LEN        32
#define TBOW_MAX_PASSPHRASE_LEN  63

#define WL_TBOW_SETUPINFO_T_VERSION 1 /* version of tbow_setup_netinfo_t */
typedef struct tbow_setup_netinfo {
	uint32 version;
	uint8 opmode;
	uint8 pad;
	uint8 macaddr[ETHER_ADDR_LEN];
	uint32 ssid_len;
	uint8 ssid[TBOW_MAX_SSID_LEN];
	uint8 passphrase_len;
	uint8 passphrase[TBOW_MAX_PASSPHRASE_LEN];
	chanspec_t chanspec;
} tbow_setup_netinfo_t;

typedef enum tbow_ho_opmode {
	TBOW_HO_MODE_START_GO = 0,
	TBOW_HO_MODE_START_STA,
	TBOW_HO_MODE_START_GC,
	TBOW_HO_MODE_TEST_GO,
	TBOW_HO_MODE_STOP_GO = 0x10,
	TBOW_HO_MODE_STOP_STA,
	TBOW_HO_MODE_STOP_GC,
	TBOW_HO_MODE_TEARDOWN
} tbow_ho_opmode_t;

/* Beacon trim feature statistics */
/* Configuration params */
#define M_BCNTRIM_N				(0)	/* Enable/Disable Beacon Trim */
#define M_BCNTRIM_TIMEND		(1)	/* Waiting time for TIM IE to end */
#define M_BCNTRIM_TSFTLRN		(2)	/* TSF tolerance value (usecs) */
/* PSM internal use */
#define M_BCNTRIM_PREVBCNLEN	(3)	/* Beacon length excluding the TIM IE */
#define M_BCNTRIM_N_COUNTER		(4)	/* PSM's local beacon trim counter */
#define M_BCNTRIM_STATE			(5)	/* PSM's Beacon trim status register */
#define M_BCNTRIM_TIMLEN		(6)	/* TIM IE Length */
#define M_BCNTRIM_BMPCTL		(7)	/* Bitmap control word */
#define M_BCNTRIM_TSF_L			(8)	/* Lower TSF word */
#define M_BCNTRIM_TSF_ML		(9)	/* Lower middle TSF word */
#define M_BCNTRIM_RSSI			(10) /* Partial beacon RSSI */
#define M_BCNTRIM_CHANNEL		(11) /* Partial beacon channel */
/* Trimming Counters */
#define M_BCNTRIM_SBCNRXED		(12) /* Self-BSSID beacon received */
#define M_BCNTRIM_CANTRIM		(13) /* Num of beacons which can be trimmed */
#define M_BCNTRIM_TRIMMED		(14) /* # beacons which were trimmed */
#define M_BCNTRIM_BCNLENCNG		(15) /* # beacons trimmed due to length change */
#define M_BCNTRIM_TSFADJ		(16) /* # beacons not trimmed due to large TSF delta */
#define M_BCNTRIM_TIMNOTFOUND	(17) /* # beacons not trimmed due to TIM missing */
#define M_RXTSFTMRVAL_WD0		(18)
#define M_RXTSFTMRVAL_WD1		(19)
#define M_RXTSFTMRVAL_WD2		(20)
#define M_RXTSFTMRVAL_WD3		(21)
#define BCNTRIM_STATS_NUMPARAMS	(22) /* 16 bit words */

#define TXPWRCAP_MAX_NUM_CORES 8
#define TXPWRCAP_MAX_NUM_ANTENNAS (TXPWRCAP_MAX_NUM_CORES * 2)

typedef struct wl_txpwrcap_tbl {
	uint8 num_antennas_per_core[TXPWRCAP_MAX_NUM_CORES];
	/* Stores values for valid antennas */
	int8 pwrcap_cell_on[TXPWRCAP_MAX_NUM_ANTENNAS]; /* qdBm units */
	int8 pwrcap_cell_off[TXPWRCAP_MAX_NUM_ANTENNAS]; /* qdBm units */
} wl_txpwrcap_tbl_t;

/* -------------- dynamic BTCOEX --------------- */
/* require strict packing */
#include <packed_section_start.h>

#define DCTL_TROWS	2	/* currently practical number of rows  */
#define DCTL_TROWS_MAX	4	/*  2 extra rows RFU */
/* DYNCTL profile flags */
#define DCTL_FLAGS_DYNCTL	(1 << 0)	/*  1 - enabled, 0 - legacy only */
#define DCTL_FLAGS_DESENSE	(1 << 1)	/* auto desense is enabled */
#define DCTL_FLAGS_MSWITCH	(1 << 2)	/* mode switching is enabled */
/* for now AGG on/off is handled separately  */
#define DCTL_FLAGS_TX_AGG_OFF	(1 << 3) /* TBD: allow TX agg Off */
#define DCTL_FLAGS_RX_AGG_OFF	(1 << 4) /* TBD: allow RX agg Off */
/* used for dry run testing only */
#define DCTL_FLAGS_DRYRUN	(1 << 7) /* Eenables dynctl dry run mode  */
#define IS_DYNCTL_ON(prof)	((prof->flags & DCTL_FLAGS_DYNCTL) != 0)
#define IS_DESENSE_ON(prof)	((prof->flags & DCTL_FLAGS_DESENSE) != 0)
#define IS_MSWITCH_ON(prof)	((prof->flags & DCTL_FLAGS_MSWITCH) != 0)
/* desense level currently in use */
#define DESENSE_OFF	0
#define DFLT_DESENSE_MID	12
#define DFLT_DESENSE_HIGH	2

/*
 * dynctl data points(a set of btpwr & wlrssi thresholds)
 * for mode & desense switching
 */
typedef struct btc_thr_data {
	int8	mode;	/* used by desense sw */
	int8	bt_pwr;	/* BT tx power threshold */
	int8	bt_rssi;	/* BT rssi threshold */
	/* wl rssi range when mode or desense change may be needed */
	int8	wl_rssi_high;
	int8	wl_rssi_low;
} btc_thr_data_t;

/* dynctl. profile data structure  */
#define DCTL_PROFILE_VER 0x01
typedef BWL_PRE_PACKED_STRUCT struct  dctl_prof {
	uint8 version;  /* dynctl profile version */
	/* dynctl profile flags bit:0 - dynctl On, bit:1 dsns On, bit:2 mode sw On, */
	uint8 flags;  /* bit[6:3] reserved, bit7 - Dryrun (sim) - On */
	/*  wl desense levels to apply */
	uint8	dflt_dsns_level;
	uint8	low_dsns_level;
	uint8	mid_dsns_level;
	uint8	high_dsns_level;
	/* mode switching hysteresis in dBm */
	int8	msw_btrssi_hyster;
	/* default btcoex mode */
	uint8	default_btc_mode;
	 /* num of active rows in mode switching table */
	uint8	msw_rows;
	/* num of rows in desense table */
	uint8	dsns_rows;
	/* dynctl mode switching data table  */
	btc_thr_data_t msw_data[DCTL_TROWS_MAX];
	/* dynctl desense switching data table */
	btc_thr_data_t dsns_data[DCTL_TROWS_MAX];
} BWL_POST_PACKED_STRUCT dctl_prof_t;

/*  dynctl status info */
typedef BWL_PRE_PACKED_STRUCT struct  dynctl_status {
	bool sim_on;	/* true if simulation is On */
	uint16	bt_pwr_shm; /* BT per/task power as read from ucode  */
	int8	bt_pwr;		/* BT pwr extracted & converted to dBm */
	int8	bt_rssi;	/* BT rssi in dBm */
	int8	wl_rssi;	/* last wl rssi reading used by btcoex */
	uint8	dsns_level; /* current desense level */
	uint8	btc_mode;   /* current btcoex mode */
	/* add more status items if needed,  pad to 4 BB if needed */
} BWL_POST_PACKED_STRUCT dynctl_status_t;

/*  dynctl simulation (dryrun data) */
typedef BWL_PRE_PACKED_STRUCT struct  dynctl_sim {
	bool sim_on;	/* simulation mode on/off */
	int8 btpwr;		/* simulated BT power in dBm */
	int8 btrssi;	/* simulated BT rssi in dBm */
	int8 wlrssi;	/* simulated WL rssi in dBm */
} BWL_POST_PACKED_STRUCT dynctl_sim_t;
/* no default structure packing */
#include <packed_section_end.h>

/* PTK key maintained per SCB */
#define RSN_TEMP_ENCR_KEY_LEN 16
typedef struct wpa_ptk {
	uint8 kck[RSN_KCK_LENGTH]; /* EAPOL-Key Key Confirmation Key (KCK) */
	uint8 kek[RSN_KEK_LENGTH]; /* EAPOL-Key Key Encryption Key (KEK) */
	uint8 tk1[RSN_TEMP_ENCR_KEY_LEN]; /* Temporal Key 1 (TK1) */
	uint8 tk2[RSN_TEMP_ENCR_KEY_LEN]; /* Temporal Key 2 (TK2) */
} wpa_ptk_t;

/* GTK key maintained per SCB */
typedef struct wpa_gtk {
	uint32 idx;
	uint32 key_len;
	uint8  key[DOT11_MAX_KEY_SIZE];
} wpa_gtk_t;

/* FBT Auth Response Data structure */
typedef struct wlc_fbt_auth_resp {
	uint8 macaddr[ETHER_ADDR_LEN]; /* station mac address */
	uint8 pad[2];
	uint8 pmk_r1_name[WPA2_PMKID_LEN];
	wpa_ptk_t ptk; /* pairwise key */
	wpa_gtk_t gtk; /* group key */
	uint32 ie_len;
	uint8 status;  /* Status of parsing FBT authentication
					Request in application
					*/
	uint8 ies[1]; /* IEs contains MDIE, RSNIE,
					FBTIE (ANonce, SNonce,R0KH-ID, R1KH-ID)
					*/
} wlc_fbt_auth_resp_t;

/* FBT Action Response frame */
typedef struct wlc_fbt_action_resp {
	uint16 version; /* structure version */
	uint16 length; /* length of structure */
	uint8 macaddr[ETHER_ADDR_LEN]; /* station mac address */
	uint8 data_len;  /* len of ie from Category */
	uint8 data[1]; /* data contains category, action, sta address, target ap,
						status code,fbt response frame body
						*/
} wlc_fbt_action_resp_t;

#define MACDBG_PMAC_ADDR_INPUT_MAXNUM 16
#define MACDBG_PMAC_OBJ_TYPE_LEN 8

typedef struct _wl_macdbg_pmac_param_t {
	char type[MACDBG_PMAC_OBJ_TYPE_LEN];
	uint8 step;
	uint8 num;
	uint32 bitmap;
	bool addr_raw;
	uint8 addr_num;
	uint16 addr[MACDBG_PMAC_ADDR_INPUT_MAXNUM];
} wl_macdbg_pmac_param_t;

/* IOVAR 'svmp_mem' parameter. Used to read/clear svmp memory */
typedef struct svmp_mem {
	uint32 addr;	/* offset to read svmp memory from vasip base address */
	uint16 len;	/* length in count of uint16's */
	uint16 val;	/* set the range of addr/len with a value */
} svmp_mem_t;

#define WL_NAN_BAND_STR_SIZE 5       /* sizeof ("auto") */

/* Definitions of different NAN Bands */
enum {  /* mode selection for reading/writing tx iqlo cal coefficients */
	NAN_BAND_AUTO,
	NAN_BAND_B,
	NAN_BAND_A,
	NAN_BAND_INVALID = 0xFF
};

#if defined(WL_LINKSTAT)
typedef struct {
	uint32 preamble;
	uint32 nss;
	uint32 bw;
	uint32 rateMcsIdx;
	uint32 reserved;
	uint32 bitrate;
} wifi_rate;

typedef struct {
	uint16 version;
	uint16 length;
	uint32 tx_mpdu;
	uint32 rx_mpdu;
	uint32 mpdu_lost;
	uint32 retries;
	uint32 retries_short;
	uint32 retries_long;
	wifi_rate rate;
} wifi_rate_stat_t;

typedef int32 wifi_radio;

typedef struct {
	uint16 version;
	uint16 length;
	wifi_radio radio;
	uint32 on_time;
	uint32 tx_time;
	uint32 rx_time;
	uint32 on_time_scan;
	uint32 on_time_nbd;
	uint32 on_time_gscan;
	uint32 on_time_roam_scan;
	uint32 on_time_pno_scan;
	uint32 on_time_hs20;
	uint32 num_channels;
	uint8 channels[1];
} wifi_radio_stat;
#endif /* WL_LINKSTAT */

#ifdef WL11ULB
/* ULB Mode configured via "ulb_mode" IOVAR */
enum {
    ULB_MODE_DISABLED = 0,
    ULB_MODE_STD_ALONE_MODE = 1,    /* Standalone ULB Mode */
    ULB_MODE_DYN_MODE = 2,      /* Dynamic ULB Mode */
	/* Add all other enums before this */
    MAX_SUPP_ULB_MODES
};

/* ULB BWs configured via "ulb_bw" IOVAR during Standalone Mode Only.
 * Values of this enumeration are also used to specify 'Current Operational Bandwidth'
 * and 'Primary Operational Bandwidth' sub-fields in 'ULB Operations' field (used in
 * 'ULB Operations' Attribute or 'ULB Mode Switch' Attribute)
 */
typedef enum {
    ULB_BW_DISABLED = 0,
    ULB_BW_10MHZ    = 1,    /* Standalone ULB BW in 10 MHz BW */
    ULB_BW_5MHZ = 2,    /* Standalone ULB BW in 5 MHz BW */
    ULB_BW_2P5MHZ   = 3,    /* Standalone ULB BW in 2.5 MHz BW */
	/* Add all other enums before this */
    MAX_SUPP_ULB_BW
} ulb_bw_type_t;
#endif /* WL11ULB */

#ifdef MFP
/* values for IOV_MFP arg */
enum {
    WL_MFP_NONE = 0,
    WL_MFP_CAPABLE,
    WL_MFP_REQUIRED
};
#endif /* MFP */

#if defined(WLRCC)
#define MAX_ROAM_CHANNEL      20

typedef struct {
	int n;
	chanspec_t channels[MAX_ROAM_CHANNEL];
} wl_roam_channel_list_t;
#endif 


/*
 * Neighbor Discover Offload: enable NDO feature
 * Called  by ipv6 event handler when interface comes up
 * Set RA rate limit interval value(%)
 */
typedef struct nd_ra_ol_limits {
	uint16 version;         /* version of the iovar buffer */
	uint16 type;            /* type of data provided */
	uint16 length;          /* length of the entire structure */
	uint16 pad1;            /* pad union to 4 byte boundary */
	union {
		struct {
			uint16 min_time;         /* seconds, min time for RA offload hold */
			uint16 lifetime_percent;
			/* percent, lifetime percentage for offload hold time */
		} lifetime_relative;
		struct {
			uint16 hold_time;        /* seconds, RA offload hold time */
			uint16 pad2;             /* unused */
		} fixed;
	} limits;
} nd_ra_ol_limits_t;

#define ND_RA_OL_LIMITS_VER 1

/* nd_ra_ol_limits sub-types */
#define ND_RA_OL_LIMITS_REL_TYPE   0     /* relative, percent of RA lifetime */
#define ND_RA_OL_LIMITS_FIXED_TYPE 1     /* fixed time */

/* buffer lengths for the different nd_ra_ol_limits types */
#define ND_RA_OL_LIMITS_REL_TYPE_LEN   12
#define ND_RA_OL_LIMITS_FIXED_TYPE_LEN  10

#define ND_RA_OL_SET    "SET"
#define ND_RA_OL_GET    "GET"
#define ND_PARAM_SIZE   50
#define ND_VALUE_SIZE   5
#define ND_PARAMS_DELIMETER " "
#define ND_PARAM_VALUE_DELLIMETER   '='
#define ND_LIMIT_STR_FMT ("%50s %50s")

#define ND_RA_TYPE  "TYPE"
#define ND_RA_MIN_TIME  "MIN"
#define ND_RA_PER   "PER"
#define ND_RA_HOLD  "HOLD"

/*
 * Temperature Throttling control mode
 */
typedef struct wl_temp_control {
	bool enable;
	uint16 control_bit;
} wl_temp_control_t;

#endif /* _wlioctl_h_ */
