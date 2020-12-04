/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Neighbor Awareness Networking
 *
 * Copyright (C) 1999-2019, Broadcom Corporation
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
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: wl_cfgnan.h 559906 2015-05-29 02:42:35Z $
 */

#ifndef _wl_cfgnan_h_
#define _wl_cfgnan_h_

#define NAN_IOCTL_BUF_SIZE			512
#define NAN_EVENT_NAME_MAX_LEN		40
#define NAN_CONFIG_ATTR_MAX_LEN		24
#define NAN_RTT_IOVAR_BUF_SIZE		1024
#define WL_NAN_EVENT_CLEAR_BIT		32
#define NAN_EVENT_MASK_ALL			0x7fffffff

#define NAN_INVALID_ID(id)	(id > 255)
#define NAN_INVALID_ROLE(role)	(role > WL_NAN_ROLE_ANCHOR_MASTER)
#define NAN_INVALID_CHANSPEC(chanspec)	((chanspec == INVCHANSPEC) || \
	(chanspec == 0))
#define NAN_INVALID_EVENT(num)	((num < WL_NAN_EVENT_START) || \
	(num >= WL_NAN_EVENT_INVALID))
#define NAN_INVALID_PROXD_EVENT(num)	(num != WLC_E_PROXD_NAN_EVENT)
#define NAN_EVENT_BIT(event) (1U << (event - WL_NAN_EVENT_START))
#define NAME_TO_STR(name) #name

#define SUPP_EVENT_PREFIX		"CTRL-EVENT-"
#define EVENT_RTT_STATUS_STR	"NAN-RTT-STATUS"

#define TIMESTAMP_PREFIX	"TSF="			/* timestamp */
#define AMR_PREFIX			"AMR="			/* anchor master rank */
#define DISTANCE_PREFIX		"DIST="			/* distance */
#define ATTR_PREFIX			"ATTR="			/* attribute */
#define ROLE_PREFIX			"ROLE="			/* role */
#define CHAN_PREFIX			"CHAN="			/* channel */
#define BITMAP_PREFIX		"BMAP="			/* bitmap */
#define DEBUG_PREFIX		"DEBUG="		/* debug enable/disable flag */
#define DW_LEN_PREFIX		"DW_LEN="		/* discovery window length */
#define DW_INT_PREFIX		"DW_INT="		/* discovery window interval */
#define STATUS_PREFIX		"STATUS="		/* status */
#define PUB_ID_PREFIX		"PUB_ID="		/* publisher id */
#define SUB_ID_PREFIX		"SUB_ID="		/* subscriber id */
#define INSTANCE_ID_PREFIX		"LOCAL_ID="		/* Instance id */
#define REMOTE_INSTANCE_ID_PREFIX		"PEER_ID="		/* Peer id */

#ifdef NAN_P2P_CONFIG
#define P2P_IE_PREFIX		"P2P_IE="		/* p2p ie  id */
#define IE_EN_PREFIX		"ENBLE_IE="		/* enable p2p ie  */
#endif
#define PUB_PR_PREFIX		"PUB_PR="		/* publish period */
#define PUB_INT_PREFIX		"PUB_INT="		/* publish interval (ttl) */
#define CLUS_ID_PREFIX		"CLUS_ID="		/* cluster id */
#define IF_ADDR_PREFIX		"IF_ADDR="		/* IF address */
#define MAC_ADDR_PREFIX		"MAC_ADDR="		/* mac address */
#define SVC_HASH_PREFIX		"SVC_HASH="		/* service hash */
#define SVC_INFO_PREFIX		"SVC_INFO="		/* service information */
#define HOP_COUNT_PREFIX	"HOP_COUNT="	/* hop count */
#define MASTER_PREF_PREFIX	"MASTER_PREF="	/* master preference */
#define ACTIVE_OPTION		"ACTIVE"		/* Active Subscribe. */
#define SOLICITED_OPTION	"SOLICITED"		/* Solicited Publish. */
#define UNSOLICITED_OPTION	"UNSOLICITED"	/* Unsolicited Publish. */
/* anchor master beacon transmission time */
#define AMBTT_PREFIX		"AMBTT="
/* passive scan period for cluster merge */
#define SCAN_PERIOD_PREFIX	"SCAN_PERIOD="
/* passive scan interval for cluster merge */
#define SCAN_INTERVAL_PREFIX	"SCAN_INTERVAL="
#define BCN_INTERVAL_PREFIX		"BCN_INTERVAL="

#define NAN_EVENT_STR_STARTED               "NAN-STARTED"
#define NAN_EVENT_STR_JOINED                "NAN-JOINED"
#define NAN_EVENT_STR_ROLE_CHANGE           "NAN-ROLE-CHANGE"
#define NAN_EVENT_STR_SCAN_COMPLETE         "NAN-SCAN-COMPLETE"
#define NAN_EVENT_STR_SDF_RX                "NAN-SDF-RX"
#define NAN_EVENT_STR_REPLIED               "NAN-REPLIED"
#define NAN_EVENT_STR_TERMINATED            "NAN-TERMINATED"
#define NAN_EVENT_STR_FOLLOWUP_RX           "NAN-FOLLOWUP-RX"
#define NAN_EVENT_STR_STATUS_CHANGE         "NAN-STATUS-CHANGE"
#define NAN_EVENT_STR_MERGED                "NAN-MERGED"
#define NAN_EVENT_STR_STOPPED               "NAN-STOPPED"
#define NAN_EVENT_STR_P2P_RX                "NAN-P2P-RX"
#define NAN_EVENT_STR_WINDOW_BEGUN_P2P      "NAN-WINDOW-BEGUN-P2P"
#define NAN_EVENT_STR_WINDOW_BEGUN_MESH     "NAN-WINDOW-BEGUN-MESH"
#define NAN_EVENT_STR_WINDOW_BEGUN_IBSS     "NAN-WINDOW-BEGUN-IBSS"
#define NAN_EVENT_STR_WINDOW_BEGUN_RANGING  "NAN-WINDOW-BEGUN-RANGING"
#define NAN_EVENT_STR_INVALID               "NAN-INVALID"

typedef struct nan_str_data {
	u8 *data;
	u32 dlen;
} nan_str_data_t;

typedef struct nan_config_attr {
	char name[NAN_CONFIG_ATTR_MAX_LEN];	/* attribute name */
	u16 type;							/* attribute xtlv type */
} nan_config_attr_t;

typedef struct nan_cmd_data {
	nan_config_attr_t attr;			/* set config attributes */
	nan_str_data_t svc_hash;		/* service hash */
	nan_str_data_t svc_info;		/* service information */
	nan_str_data_t p2p_info;		/* p2p information */
	struct ether_addr mac_addr;		/* mac address */
	struct ether_addr clus_id;		/* cluster id */
	struct ether_addr if_addr;		/* if addr */
	u32 beacon_int;					/* beacon interval */
	u32 pub_int;					/* publish interval (ttl) */
	u32 pub_pr;						/* publish period */
	u32 bmap;						/* bitmap */
	u32 role;						/* role */
	u16 pub_id;						/* publisher id */
	u16 sub_id;						/* subscriber id */
	u16 local_id;					/* Local id */
	u16 remote_id;					/* Remote id */
	uint32 flags;					/* Flag bits */
	u16 dw_len;						/* discovery window length */
	u16 master_pref;				/* master preference */
	chanspec_t chanspec;			/* channel */
	u8 debug_flag;					/* debug enable/disable flag */
} nan_cmd_data_t;

typedef int (nan_func_t)(struct net_device *ndev, struct bcm_cfg80211 *cfg,
	char *cmd, int size, nan_cmd_data_t *cmd_data);

typedef struct nan_cmd {
	const char *name;					/* command name */
	nan_func_t *func;					/* command hadler */
} nan_cmd_t;

typedef struct nan_event_hdr {
	u16 event_subtype;
	u32 flags;							/* future use */
} nan_event_hdr_t;

typedef struct wl_nan_tlv_data {
	wl_nan_status_t nstatus;			/* status data */
	wl_nan_disc_params_t params;		/* discovery parameters */
	struct ether_addr mac_addr;			/* peer mac address */
	struct ether_addr clus_id;			/* cluster id */
	nan_str_data_t svc_info;			/* service info */
	nan_str_data_t vend_info;			/* vendor info */
	/* anchor master beacon transmission time */
	u32 ambtt;
	u32 dev_role;						/* device role */
	u16 inst_id;						/* instance id */
	u16 peer_inst_id;					/* Peer instance id */
	u16 pub_id;							/* publisher id */
	u16 sub_id;							/* subscriber id */
	u16 master_pref;					/* master preference */
	chanspec_t chanspec;				/* channel */
	u8 amr[NAN_MASTER_RANK_LEN];		/* anchor master role */
	u8 svc_name[WL_NAN_SVC_HASH_LEN];	/* service name */
	u8 hop_count;						/* hop count */
	u8 enabled;							/* nan status flag */
	nan_scan_params_t scan_params;		/* scan_param */
} wl_nan_tlv_data_t;

extern int wl_cfgnan_set_vars_cbfn(void *ctx, uint8 *tlv_buf,
	uint16 type, uint16 len);
extern int wl_cfgnan_enable_events(struct net_device *ndev,
	struct bcm_cfg80211 *cfg);
extern int wl_cfgnan_start_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_stop_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_support_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_status_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_pub_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_p2p_ie_add_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_p2p_ie_enable_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_p2p_ie_del_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);

extern int wl_cfgnan_sub_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_cancel_pub_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_cancel_sub_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_transmit_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_set_config_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_rtt_config_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
extern int wl_cfgnan_rtt_find_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
#ifdef WL_NAN_DEBUG
extern int wl_cfgnan_debug_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *cmd, int size, nan_cmd_data_t *cmd_data);
#endif /* WL_NAN_DEBUG */
extern int wl_cfgnan_cmd_handler(struct net_device *dev,
	struct bcm_cfg80211 *cfg, char *cmd, int cmd_len);
extern s32 wl_cfgnan_notify_nan_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
extern s32 wl_cfgnan_notify_proxd_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);

#endif	/* _wl_cfgnan_h_ */
