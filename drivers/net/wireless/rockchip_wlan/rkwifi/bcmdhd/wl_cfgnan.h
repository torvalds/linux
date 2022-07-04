/*
 * Neighbor Awareness Networking
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

#ifndef _wl_cfgnan_h_
#define _wl_cfgnan_h_

/* NAN structs versioning b/w DHD and HAL
* define new version if any change in any of the shared structs
*/
#define NAN_HAL_VERSION_1	0x2

#define NAN_EVENT_BUFFER_SIZE_LARGE	1024u

#define NAN_RANGE_EXT_CANCEL_SUPPORT_VER 2
#define WL_NAN_IOV_BATCH_VERSION	0x8000
#define WL_NAN_AVAIL_REPEAT_INTVL	0x0200
#define WL_NAN_AVAIL_START_INTVL	160
#define WL_NAN_AVAIL_DURATION_INTVL	336
#define NAN_IOCTL_BUF_SIZE		256u
#define NAN_IOCTL_BUF_SIZE_MED		512u
#define NAN_IOCTL_BUF_SIZE_LARGE	1024u
#define NAN_EVENT_NAME_MAX_LEN		40u
#define NAN_RTT_IOVAR_BUF_SIZE		1024u
#define WL_NAN_EVENT_CLEAR_BIT		32
#define NAN_EVENT_MASK_ALL			0x7fffffff
#define NAN_MAX_AWAKE_DW_INTERVAL	5
#define NAN_MAXIMUM_ID_NUMBER 255
#define NAN_MAXIMUM_MASTER_PREFERENCE 254
#define NAN_ID_RESERVED	0
#define NAN_ID_MIN	1
#define NAN_ID_MAX	255
#define NAN_DEF_SOCIAL_CHAN_2G	6
#define NAN_DEF_SOCIAL_CHAN_5G	149
#define NAN_DEF_SEC_SOCIAL_CHAN_5G	44
#define NAN_MAX_SOCIAL_CHANNELS	3
/* Keeping RSSI threshold value to be -70dBm */
#define NAN_DEF_RSSI_NOTIF_THRESH -70
/* Keeping default RSSI mid value to be -70dBm */
#define NAN_DEF_RSSI_MID -75
/* Keeping default RSSI close value to be -60dBm */
#define NAN_DEF_RSSI_CLOSE -60
#define WL_AVAIL_BIT_MAP	"1111111111111111111111111111111100000000000000000000000000000000"
#define WL_5G_AVAIL_BIT_MAP	"0000000011111111111111111111111111111111000000000000000000000000"
#define WL_AVAIL_CHANNEL_2G	6
#define WL_AVAIL_BANDWIDTH_2G	WL_CHANSPEC_BW_20
#define WL_AVAIL_CHANNEL_5G	149
#define WL_AVAIL_BANDWIDTH_5G	WL_CHANSPEC_BW_80
#define NAN_RANGING_PERIOD WL_AVAIL_PERIOD_1024
#define NAN_SYNC_DEF_AWAKE_DW	1
#define NAN_RNG_TERM_FLAG_NONE	0

#define NAN_BLOOM_LENGTH_DEFAULT        240u
#define NAN_SRF_MAX_MAC (NAN_BLOOM_LENGTH_DEFAULT / ETHER_ADDR_LEN)
#define NAN_SRF_CTRL_FIELD_LEN 1u

#define MAX_IF_ADD_WAIT_TIME	1000
#define NAN_DP_ROLE_INITIATOR  0x0001
#define NAN_DP_ROLE_RESPONDER  0x0002

#define WL_NAN_OBUF_DATA_OFFSET  (OFFSETOF(bcm_iov_batch_buf_t, cmds[0]) + \
		OFFSETOF(bcm_iov_batch_subcmd_t, data[0]))
#define NAN_INVALID_ROLE(role)	(role > WL_NAN_ROLE_ANCHOR_MASTER)
#define NAN_INVALID_CHANSPEC(chanspec)	((chanspec == INVCHANSPEC) || \
	(chanspec == 0))
#define NAN_INVALID_EVENT(num)	((num < WL_NAN_EVENT_START) || \
	(num >= WL_NAN_EVENT_INVALID))
#define NAN_INVALID_PROXD_EVENT(num)	(num != WLC_E_PROXD_NAN_EVENT)
#define NAN_EVENT_BIT(event) (1U << (event - WL_NAN_EVENT_START))
#define NAN_EVENT_MAP(event) ((event) - WL_NAN_EVENT_START)
#define NAME_TO_STR(name) #name
#define NAN_ID_CTRL_SIZE ((NAN_MAXIMUM_ID_NUMBER/8) + 1)

#define tolower(c) bcm_tolower(c)

#define NMR2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5], (a)[6], (a)[7]
#define NMRSTR "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x"

#define NAN_DBG_ENTER() {WL_DBG(("Enter\n"));}
#define NAN_DBG_EXIT() {WL_DBG(("Exit\n"));}

/* Service Control Type length */
#define NAN_SVC_CONTROL_TYPE_MASK	((1 << NAN_SVC_CONTROL_TYPE_LEN) - 1)

#ifndef strtoul
#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
#endif

#define NAN_MAC_ADDR_LEN 6u
#define NAN_DP_MAX_APP_INFO_LEN	512u

#define NAN_SDE_CF_DP_REQUIRED      (1 << 2)
#define NAN_SDE_CF_DP_TYPE      (1 << 3)
#define NAN_SDE_CF_MULTICAST_TYPE   (1 << 4)
#define NAN_SDE_CF_SECURITY_REQUIRED    (1 << 6)
#define NAN_SDE_CF_RANGING_REQUIRED (1 << 7)
#define NAN_SDE_CF_RANGE_PRESENT    (1 << 8)

#define CHECK_BIT(m, n) ((m >> n) & 1)? 1 : 0
#define WL_NAN_EVENT_DIC_MAC_ADDR_BIT	0
#define WL_NAN_EVENT_START_EVENT	1
#define WL_NAN_EVENT_JOIN_EVENT		2

/* Disabling svc specific(as per part of sub & pub calls) events based on below bits */
#define WL_NAN_EVENT_SUPPRESS_TERMINATE_BIT	0
#define WL_NAN_EVENT_SUPPRESS_MATCH_EXP_BIT	1
#define WL_NAN_EVENT_SUPPRESS_RECEIVE_BIT	2
#define WL_NAN_EVENT_SUPPRESS_REPLIED_BIT	3

/* Disabling tranmsit followup events based on below bit */
#define WL_NAN_EVENT_SUPPRESS_FOLLOWUP_RECEIVE_BIT	0

#define C2S(x)  case x: id2str = #x
#define NAN_BLOOM_LENGTH_DEFAULT	240u
#define NAN_SRF_MAX_MAC			(NAN_BLOOM_LENGTH_DEFAULT / ETHER_ADDR_LEN)
#define NAN_MAX_PMK_LEN			32u
#define NAN_ERROR_STR_LEN		255u

/* NAN related Capabilities */
#define MAX_CONCURRENT_NAN_CLUSTERS		1u
#define MAX_PUBLISHES				8u
#define MAX_SUBSCRIBES				8u
#define MAX_SVC_NAME_LEN			255u
#define MAX_MATCH_FILTER_LEN			255u
#define MAX_TOTAL_MATCH_FILTER_LEN		510u
#define	NAN_MAX_SERVICE_SPECIFIC_INFO_LEN	255u
#define NAN_MAX_NDI				3u
#define MAX_NDP_SESSIONS			5u
#define MAX_APP_INFO_LEN			255u
#define	MAX_QUEUED_TX_FOLLOUP_MSGS		10u
#define	MAX_SDEA_SVC_INFO_LEN			255u
#define	MAX_SUBSCRIBE_ADDRESS			10u
#define	CIPHER_SUITE_SUPPORTED			1u
#define	MAX_SCID_LEN				0u
#define	IS_NDP_SECURITY_SUPPORTED		true
#define	NDP_SUPPORTED_BANDS			2u
#define NAN_MAX_RANGING_INST			8u
#define NAN_MAX_RANGING_SSN_ALLOWED		1u
#define NAN_MAX_SVC_INST			(MAX_PUBLISHES + MAX_SUBSCRIBES)
#define NAN_SVC_INST_SIZE			32u
#define NAN_START_STOP_TIMEOUT			5000u
#define NAN_MAX_NDP_PEER			8u
#define NAN_DISABLE_CMD_DELAY			530u
#define NAN_WAKELOCK_TIMEOUT			(NAN_DISABLE_CMD_DELAY + 100u)

#define NAN_NMI_RAND_PVT_CMD_VENDOR		(1 << 31)
#define NAN_NMI_RAND_CLUSTER_MERGE_ENAB		(1 << 30)
#define NAN_NMI_RAND_AUTODAM_LWT_MODE_ENAB	(1 << 29)

#ifdef WL_NAN_DEBUG
#define NAN_MUTEX_LOCK() {WL_DBG(("Mutex Lock: Enter: %s\n", __FUNCTION__)); \
	mutex_lock(&cfg->nancfg->nan_sync);}
#define NAN_MUTEX_UNLOCK() {mutex_unlock(&cfg->nancfg->nan_sync); \
	WL_DBG(("Mutex Unlock: Exit: %s\n", __FUNCTION__));}
#else
#define NAN_MUTEX_LOCK() {mutex_lock(&cfg->nancfg->nan_sync);}
#define NAN_MUTEX_UNLOCK() {mutex_unlock(&cfg->nancfg->nan_sync);}
#endif /* WL_NAN_DEBUG */
#define	NAN_ATTR_SUPPORT_2G_CONFIG		(1<<0)
#define	NAN_ATTR_SYNC_DISC_2G_BEACON_CONFIG	(1<<1)
#define	NAN_ATTR_SDF_2G_SUPPORT_CONFIG		(1<<2)
#define	NAN_ATTR_SUPPORT_5G_CONFIG		(1<<3)
#define	NAN_ATTR_SYNC_DISC_5G_BEACON_CONFIG	(1<<4)
#define	NAN_ATTR_SDF_5G_SUPPORT_CONFIG		(1<<5)
#define	NAN_ATTR_2G_DW_CONFIG			(1<<6)
#define	NAN_ATTR_5G_DW_CONFIG			(1<<7)
#define	NAN_ATTR_2G_CHAN_CONFIG			(1<<8)
#define	NAN_ATTR_5G_CHAN_CONFIG			(1<<9)
#define	NAN_ATTR_2G_DWELL_TIME_CONFIG		(1<<10)
#define	NAN_ATTR_5G_DWELL_TIME_CONFIG		(1<<11)
#define	NAN_ATTR_2G_SCAN_PERIOD_CONFIG		(1<<12)
#define	NAN_ATTR_5G_SCAN_PERIOD_CONFIG		(1<<13)
#define	NAN_ATTR_RSSI_CLOSE_CONFIG		(1<<14)
#define	NAN_ATTR_RSSI_MIDDLE_2G_CONFIG		(1<<15)
#define	NAN_ATTR_RSSI_PROXIMITY_2G_CONFIG	(1<<16)
#define	NAN_ATTR_RSSI_CLOSE_5G_CONFIG		(1<<17)
#define	NAN_ATTR_RSSI_MIDDLE_5G_CONFIG		(1<<18)
#define	NAN_ATTR_RSSI_PROXIMITY_5G_CONFIG	(1<<19)
#define	NAN_ATTR_RSSI_WINDOW_SIZE_CONFIG	(1<<20)
#define	NAN_ATTR_HOP_COUNT_LIMIT_CONFIG		(1<<21)
#define	NAN_ATTR_SID_BEACON_CONFIG		(1<<22)
#define	NAN_ATTR_HOP_COUNT_FORCE_CONFIG		(1<<23)
#define	NAN_ATTR_RAND_FACTOR_CONFIG		(1<<24)
#define	NAN_ATTR_CLUSTER_VAL_CONFIG		(1<<25)
#define	NAN_ATTR_IF_ADDR_CONFIG			(1<<26)
#define	NAN_ATTR_OUI_CONFIG			(1<<27)
#define	NAN_ATTR_SUB_SID_BEACON_CONFIG		(1<<28)
#define NAN_ATTR_DISC_BEACON_INTERVAL		(1<<29)
#define NAN_IOVAR_NAME_SIZE	4u
#define NAN_XTLV_ID_LEN_SIZE OFFSETOF(bcm_xtlv_t, data)
#define NAN_RANGING_INDICATE_CONTINUOUS_MASK   0x01
#define NAN_RANGE_REQ_CMD 0
#define NAN_RNG_REQ_ACCEPTED_BY_HOST    1
#define NAN_RNG_REQ_REJECTED_BY_HOST    0

#define NAN_RNG_REQ_ACCEPTED_BY_PEER	0
#define NAN_RNG_REQ_REJECTED_BY_PEER	1

#define NAN_RNG_GEOFENCE_MAX_RETRY_CNT	3u

#define NAN_MAX_CHANNEL_INFO_SUPPORTED	4u
/*
* Discovery Beacon Interval config,
* Default value is 128 msec in 2G DW and 176 msec in 2G/5G DW.
*/
#define NAN_DISC_BCN_INTERVAL_2G_DEF 128u
#define NAN_DISC_BCN_INTERVAL_5G_DEF 176u

typedef uint32 nan_data_path_id;

typedef enum nan_range_status {
	NAN_RANGING_INVALID = 0,
	NAN_RANGING_REQUIRED = 1,
	NAN_RANGING_SETUP_IN_PROGRESS = 2,
	NAN_RANGING_SESSION_IN_PROGRESS = 3
} nan_range_status_t;

typedef enum nan_range_role {
	NAN_RANGING_ROLE_INVALID = 0,
	NAN_RANGING_ROLE_INITIATOR = 1,
	NAN_RANGING_ROLE_RESPONDER = 2
} nan_range_role_t;

typedef struct nan_svc_inst {
	uint8  inst_id;      /* publisher/subscriber id */
	uint8  inst_type;    /* publisher/subscriber */
} nan_svc_inst_t;

/* Range Status Flag bits for svc info */
#define SVC_RANGE_REP_EVENT_ONCE 0x01

/* Range Status Flag bits for svc info */
#define SVC_RANGE_REP_EVENT_ONCE 0x01

#define NAN_RANGING_SETUP_IS_IN_PROG(status) \
	((status) == NAN_RANGING_SETUP_IN_PROGRESS)

#define NAN_RANGING_IS_IN_PROG(status) \
	(((status) == NAN_RANGING_SETUP_IN_PROGRESS) || \
	((status) == NAN_RANGING_SESSION_IN_PROGRESS))

typedef struct nan_svc_info {
	bool valid;
	nan_data_path_id ndp_id[NAN_MAX_SVC_INST];
	uint8 svc_hash[WL_NAN_SVC_HASH_LEN];        /* service hash */
	uint8 svc_id;
	uint8 ranging_required;
	uint8 ranging_ind;
	uint8 status;
	uint32 ranging_interval;
	uint32 ingress_limit;
	uint32 egress_limit;
	uint32 flags;
	uint8 tx_match_filter[MAX_MATCH_FILTER_LEN];        /* TX match filter */
	uint8 tx_match_filter_len;
	uint8 svc_range_status; /* For managing any svc range status flags */
} nan_svc_info_t;

/* NAN Peer DP state */
typedef enum {
	NAN_PEER_DP_NOT_CONNECTED = 0,
	NAN_PEER_DP_CONNECTING = 1,
	NAN_PEER_DP_CONNECTED = 2
} nan_peer_dp_state_t;

typedef struct nan_ndp_peer {
	uint8 peer_dp_state;
	uint8 dp_count;
	struct ether_addr peer_addr;
} nan_ndp_peer_t;

#define INVALID_DISTANCE		0xFFFFFFFF
#define NAN_RTT_FTM_SSN_RETRIES		2

typedef struct nan_ranging_inst {
	uint8 range_id;
	nan_range_status_t range_status;
	struct ether_addr peer_addr;
	int range_type;
	uint8 num_svc_ctx;
	nan_svc_info_t *svc_idx[MAX_SUBSCRIBES];
	uint32 prev_distance_mm;
	nan_range_role_t range_role;
	bool in_use;
	uint8 geof_retry_count;
	uint8 ftm_ssn_retry_count;
	bool role_concurrency_status;
} nan_ranging_inst_t;

#define DUMP_NAN_RTT_INST(inst) { printf("svc instance ID %d", (inst)->svc_inst_id); \
	printf("Range ID %d", (inst)->range_id); \
	printf("range_status %d", (inst)->range_status); \
	printf("Range Type %d", (inst)->range_type); \
	printf("Peer MAC "MACDBG"\n", MAC2STRDBG((inst)->peer_addr.octet)); \
	}

#define DUMP_NAN_RTT_RPT(rpt) { printf("Range ID %d", (rpt)->rng_id); \
	printf("Distance in MM %d", (rpt)->dist_mm); \
	printf("range_indication %d", (rpt)->indication); \
	printf("Peer MAC "MACDBG"\n", MAC2STRDBG((rpt)->peer_m_addr.octet)); \
	}
/*
 * Data request Initiator/Responder
 * app/service related info
 */
typedef struct nan_data_path_app_info {
	uint16 ndp_app_info_len;
	uint8 ndp_app_info[NAN_DP_MAX_APP_INFO_LEN];
} nan_data_path_app_info_t;

/* QoS configuration */
typedef enum {
	NAN_DP_CONFIG_NO_QOS = 0,
	NAN_DP_CONFIG_QOS
} nan_data_path_qos_cfg_t;

/* Data request Responder's response */
typedef enum {
	NAN_DP_REQUEST_ACCEPT = 0,
	NAN_DP_REQUEST_REJECT
} nan_data_path_response_code_t;

/* NAN DP security Configuration */
typedef enum {
	NAN_DP_CONFIG_NO_SECURITY = 0,
	NAN_DP_CONFIG_SECURITY
} nan_data_path_security_cfg_status_t;

/* NAN Security Key Input Type */
typedef enum {
	NAN_SECURITY_KEY_INPUT_PMK = 1,
	NAN_SECURITY_KEY_INPUT_PASSPHRASE
} nan_security_key_input_type;

/* Configuration params of Data request Initiator/Responder */
typedef struct nan_data_path_cfg {
	/* Status Indicating Security/No Security */
	nan_data_path_security_cfg_status_t security_cfg;
	nan_data_path_qos_cfg_t qos_cfg;
} nan_data_path_cfg_t;

enum nan_dp_states {
	NAN_DP_STATE_DISABLED = 0,
	NAN_DP_STATE_ENABLED = 1
};

enum {
	SRF_TYPE_BLOOM_FILTER = 0,
	SRF_TYPE_SEQ_MAC_ADDR = 1
};

/* NAN Match indication type */
typedef enum {
    NAN_MATCH_ALG_MATCH_ONCE		= 0,
    NAN_MATCH_ALG_MATCH_CONTINUOUS	= 1,
    NAN_MATCH_ALG_MATCH_NEVER		= 2
} nan_match_alg;

typedef struct nan_str_data {
	uint32 dlen;
	uint8 *data;
} nan_str_data_t;

typedef struct nan_mac_list {
	uint32 num_mac_addr;
	uint8 *list;
} nan_mac_list_t;

typedef struct nan_channel_info {
	uint32 channel;
	uint32 bandwidth;
	uint32 nss;
} nan_channel_info_t;

typedef struct nan_ndl_sched_info {
	uint32 num_channels;
	nan_channel_info_t channel_info[NAN_MAX_CHANNEL_INFO_SUPPORTED];
} nan_ndl_sched_info_t;

typedef struct wl_nan_sid_beacon_tune {
	uint8 sid_enable;	/* flag for sending service id in beacon */
	uint8 sid_count;	/* Limit for number of SIDs to be included in Beacons */
	uint8 sub_sid_enable;	/* flag for sending subscribe service id in beacon */
	uint8 sub_sid_count;	/* Limit for number of SUb SIDs to be included in Beacons */
} wl_nan_sid_beacon_ctrl_t;

typedef struct nan_avail_cmd_data {
	chanspec_t chanspec[NAN_MAX_SOCIAL_CHANNELS];    /* channel */
	uint32 bmap;            /* bitmap */
	uint8 duration;
	uint8 avail_period;
	/* peer mac address reqd for ranging avail type */
	struct ether_addr peer_nmi;
	bool no_config_avail;
} nan_avail_cmd_data;

typedef struct nan_discover_cmd_data {
	nan_str_data_t svc_info;        /* service information */
	nan_str_data_t sde_svc_info;	/* extended service information */
	nan_str_data_t svc_hash;        /* service hash */
	nan_str_data_t rx_match;        /* matching filter rx */
	nan_str_data_t tx_match;        /* matching filter tx */
	nan_str_data_t key;        /* Security key information */
	nan_str_data_t scid;        /* security context information */
	nan_data_path_cfg_t ndp_cfg;
	struct ether_addr mac_addr;     /* mac address */
	nan_mac_list_t mac_list;   /* mac list */
	wl_nan_instance_id_t pub_id;    /* publisher id */
	wl_nan_instance_id_t sub_id;    /* subscriber id */
	wl_nan_instance_id_t local_id;  /* Local id */
	wl_nan_instance_id_t remote_id; /* Remote id */
	uint32 status;
	uint32 ttl;             /* time to live */
	uint32 period;          /* publish period */
	uint32 flags;           /* Flag bits */
	bool sde_control_config; /* whether sde_control present */
	uint16 sde_control_flag;
	uint16 token; /* transmit fup token id */
	uint8 csid;	/* cipher suite type */
	nan_security_key_input_type key_type;	/* cipher suite type */
	uint8 priority;         /* Priority of Transmit */
	uint8 life_count;       /* life count of the instance */
	uint8 srf_type;         /* SRF type */
	uint8 srf_include;      /* SRF include */
	uint8 use_srf;          /* use SRF */
	uint8 recv_ind_flag;    /* Receive Indication Flag */
	uint8 disc_ind_cfg;	/* Discovery Ind cfg */
	uint8 ranging_indication;
	uint32 ranging_intvl_msec; /* ranging interval in msec */
	uint32 ingress_limit;
	uint32 egress_limit;
	bool response;
	uint8 service_responder_policy;
	bool svc_update;
} nan_discover_cmd_data_t;

typedef struct nan_datapath_cmd_data {
	nan_avail_cmd_data avail_params;	/* Avail config params */
	nan_str_data_t svc_hash;        /* service hash */
	nan_str_data_t svc_info;        /* service information */
	nan_str_data_t key;        /* security key information */
	nan_data_path_response_code_t rsp_code;
	nan_data_path_id ndp_instance_id;
	nan_data_path_cfg_t ndp_cfg;
	wl_nan_instance_id_t pub_id;    /* publisher id */
	nan_security_key_input_type key_type;	/* cipher suite type */
	struct ether_addr if_addr;      /* if addr */
	struct ether_addr mac_addr;     /* mac address */
	chanspec_t chanspec[NAN_MAX_SOCIAL_CHANNELS];    /* channel */
	uint32 status;
	uint32 bmap;            /* bitmap */
	uint16 service_instance_id;
	uint16 sde_control_flag;
	uint8 csid;	/* cipher suite type */
	uint8 peer_disc_mac_addr[ETHER_ADDR_LEN];
	uint8 peer_ndi_mac_addr[ETHER_ADDR_LEN];
	uint8 num_ndp_instances;
	uint8 duration;
	char ndp_iface[IFNAMSIZ+1];
} nan_datapath_cmd_data_t;

typedef struct nan_rssi_cmd_data {
	int8 rssi_middle_2dot4g_val;
	int8 rssi_close_2dot4g_val;
	int8 rssi_proximity_2dot4g_val;
	int8 rssi_proximity_5g_val;
	int8 rssi_middle_5g_val;
	int8 rssi_close_5g_val;
	uint16 rssi_window_size; /* Window size over which rssi calculated */
} nan_rssi_cmd_data_t;

typedef struct election_metrics {
	uint8 random_factor;    /* Configured random factor */
	uint8 master_pref;     /* configured master preference */
} election_metrics_t;

typedef struct nan_awake_dws {
	uint8 dw_interval_2g;   /* 2G DW interval */
	uint8 dw_interval_5g;   /* 5G DW interval */
} nan_awake_dws_t;

typedef struct nan_config_cmd_data {
	nan_rssi_cmd_data_t rssi_attr;	/* RSSI related data */
	election_metrics_t metrics;
	nan_awake_dws_t awake_dws;	/* Awake DWs */
	nan_avail_cmd_data avail_params;	/* Avail config params */
	nan_str_data_t p2p_info;        /* p2p information */
	nan_str_data_t scid;        /* security context information */
	struct ether_addr clus_id;      /* cluster id */
	struct ether_addr mac_addr;     /* mac address */
	wl_nan_sid_beacon_ctrl_t sid_beacon;    /* sending service id in beacon */
	chanspec_t chanspec[NAN_MAX_SOCIAL_CHANNELS];    /* channel */
	uint32 status;
	uint32 bmap;            /* bitmap */
	uint32 nan_oui;         /* configured nan oui */
	uint32 warmup_time;     /* Warm up time */
	uint8 duration;
	uint8 hop_count_limit;  /* hop count limit */
	uint8 support_5g;       /* To decide dual band support */
	uint8 support_2g;       /* To decide dual band support */
	uint8 beacon_2g_val;
	uint8 beacon_5g_val;
	uint8 sdf_2g_val;
	uint8 sdf_5g_val;
	uint8 dwell_time[NAN_MAX_SOCIAL_CHANNELS];
	uint8 scan_period[NAN_MAX_SOCIAL_CHANNELS];
	uint8 config_cluster_val;
	uint8 disc_ind_cfg;	/* Discovery Ind cfg */
	uint8 csid;	/* cipher suite type */
	uint32 nmi_rand_intvl; /* nmi randomization interval */
	uint32 use_ndpe_attr;
	uint8 enable_merge;
	uint16 cluster_low;
	uint16 cluster_high;
	wl_nan_disc_bcn_interval_t disc_bcn_interval;
	uint32 dw_early_termination;
} nan_config_cmd_data_t;

typedef struct nan_event_hdr {
	uint32 flags;							/* future use */
	uint16 event_subtype;
} nan_event_hdr_t;

typedef struct nan_event_data {
	uint8 svc_name[WL_NAN_SVC_HASH_LEN];    /* service name */
	uint8 enabled;        /* NAN Enabled */
	uint8 nan_de_evt_type;  /* DE event type */
	uint8 status;           /* status */
	uint8 ndp_id;           /* data path instance id */
	uint8 security;         /* data path security */
	uint8 type;
	uint8 attr_num;
	uint8 reason;          /* reason */
	wl_nan_instance_id_t pub_id;          /* publisher id */
	wl_nan_instance_id_t sub_id;          /* subscriber id */
	wl_nan_instance_id_t local_inst_id;   /* local instance id */
	wl_nan_instance_id_t requestor_id;    /* Requestor instance id */
	int publish_rssi;	/* discovery rssi value */
	int sub_rssi;	/* Sub rssi value */
	int fup_rssi;		/* followup rssi */
	uint16 attr_list_len;  /* sizeof attributes attached to payload */
	nan_str_data_t svc_info;        /* service info */
	nan_str_data_t vend_info;       /* vendor info */
	nan_str_data_t sde_svc_info;	/* extended service information */
	nan_str_data_t tx_match_filter;	/* tx match filter */
	nan_str_data_t rx_match_filter;	/* rx match filter */
	struct ether_addr local_nmi;      /* local nmi */
	struct ether_addr clus_id;        /* cluster id */
	struct ether_addr remote_nmi;     /* remote nmi */
	struct ether_addr initiator_ndi;        /* initiator_ndi */
	struct ether_addr responder_ndi;        /* responder_ndi */
	uint16 token; /* transmit fup token id */
	uint8 peer_cipher_suite; /* peer cipher suite type */
	nan_str_data_t scid;        /* security context information */
	char nan_reason[NAN_ERROR_STR_LEN]; /* Describe the NAN reason type */
	uint16 sde_control_flag;
	uint8 ranging_result_present;
	uint32 range_measurement_cm;
	uint32 ranging_ind;
	uint8 rng_id;
} nan_event_data_t;

/*
 *   Various NAN Protocol Response code
*/
typedef enum {
	/* NAN Protocol Response Codes */
	NAN_STATUS_SUCCESS = 0,
	/*  NAN Discovery Engine/Host driver failures */
	NAN_STATUS_INTERNAL_FAILURE = 1,
	/*  NAN OTA failures */
	NAN_STATUS_PROTOCOL_FAILURE = 2,
	/* if the publish/subscribe id is invalid */
	NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID = 3,
	/* If we run out of resources allocated */
	NAN_STATUS_NO_RESOURCE_AVAILABLE = 4,
	/* if invalid params are passed */
	NAN_STATUS_INVALID_PARAM = 5,
	/*  if the requestor instance id is invalid */
	NAN_STATUS_INVALID_REQUESTOR_INSTANCE_ID = 6,
	/*  if the ndp id is invalid */
	NAN_STATUS_INVALID_NDP_ID = 7,
	/* if NAN is enabled when wifi is turned off */
	NAN_STATUS_NAN_NOT_ALLOWED = 8,
	/* if over the air ack is not received */
	NAN_STATUS_NO_OTA_ACK = 9,
	/* If NAN is already enabled and we are try to re-enable the same */
	NAN_STATUS_ALREADY_ENABLED = 10,
	/* If followup message internal queue is full */
	NAN_STATUS_FOLLOWUP_QUEUE_FULL = 11,
	/* Unsupported concurrency session enabled, NAN disabled notified */
	NAN_STATUS_UNSUPPORTED_CONCURRENCY_NAN_DISABLED = 12
} nan_status_type_t;

typedef struct {
	nan_status_type_t status;
	char nan_reason[NAN_ERROR_STR_LEN]; /* Describe the NAN reason type */
} nan_hal_status_t;

typedef struct nan_parse_event_ctx {
	struct bcm_cfg80211 *cfg;
	nan_event_data_t *nan_evt_data;
} nan_parse_event_ctx_t;

/* Capabilities info supported by FW */
typedef struct nan_hal_capabilities {
	uint32 max_concurrent_nan_clusters;
	uint32 max_publishes;
	uint32 max_subscribes;
	uint32 max_service_name_len;
	uint32 max_match_filter_len;
	uint32 max_total_match_filter_len;
	uint32 max_service_specific_info_len;
	uint32 max_vsa_data_len;
	uint32 max_mesh_data_len;
	uint32 max_ndi_interfaces;
	uint32 max_ndp_sessions;
	uint32 max_app_info_len;
	uint32 max_queued_transmit_followup_msgs;
	uint32 ndp_supported_bands;
	uint32 cipher_suites_supported;
	uint32 max_scid_len;
	bool is_ndp_security_supported;
	uint32 max_sdea_service_specific_info_len;
	uint32 max_subscribe_address;
	uint32 ndpe_attr_supported;
} nan_hal_capabilities_t;

typedef struct _nan_hal_resp {
	uint16 instance_id;
	uint16 subcmd;
	int32 status;
	int32 value;
	/* Identifier for the instance of the NDP */
	uint16 ndp_instance_id;
	/* Publisher NMI */
	uint8 pub_nmi[NAN_MAC_ADDR_LEN];
	/* SVC_HASH */
	uint8 svc_hash[WL_NAN_SVC_HASH_LEN];
	char nan_reason[NAN_ERROR_STR_LEN]; /* Describe the NAN reason type */
	char pad[3];
	nan_hal_capabilities_t capabilities;
} nan_hal_resp_t;

typedef struct wl_nan_iov {
	uint16 nan_iov_len;
	uint8 *nan_iov_buf;
} wl_nan_iov_t;

#ifdef WL_NAN_DISC_CACHE

#define NAN_MAX_CACHE_DISC_RESULT 16
typedef struct {
	bool valid;
	wl_nan_instance_id_t pub_id;
	wl_nan_instance_id_t sub_id;
	uint8 svc_hash[WL_NAN_SVC_HASH_LEN];
	struct ether_addr peer;
	int8 publish_rssi;
	uint8 peer_cipher_suite;
	uint8 security;
	nan_str_data_t svc_info;        /* service info */
	nan_str_data_t vend_info;       /* vendor info */
	nan_str_data_t sde_svc_info;	/* extended service information */
	nan_str_data_t tx_match_filter; /* tx match filter */
	uint16 sde_control_flag;
} nan_disc_result_cache;

typedef struct nan_datapath_sec_info {
	nan_data_path_id ndp_instance_id;
	wl_nan_instance_id_t pub_id;    /* publisher id */
	struct ether_addr mac_addr;     /* mac address */
} nan_datapath_sec_info_cmd_data_t;
#endif /* WL_NAN_DISC_CACHE */

typedef enum {
	NAN_RANGING_AUTO_RESPONSE_ENABLE = 0,
	NAN_RANGING_AUTO_RESPONSE_DISABLE
} NanRangingAutoResponseCfg;

typedef struct wl_ndi_data
{
	u8 ifname[IFNAMSIZ];
	u8 in_use;
	u8 created;
	struct net_device *nan_ndev;
} wl_ndi_data_t;

typedef struct wl_nancfg
{
	struct bcm_cfg80211 *cfg;
	bool nan_enable;
	nan_svc_inst_t nan_inst_ctrl[NAN_ID_CTRL_SIZE];
	struct ether_addr initiator_ndi;
	uint8 nan_dp_state;
	bool nan_init_state; /* nan initialization state */
	wait_queue_head_t ndp_if_change_event;
	uint8 support_5g;
	u8 nan_nmi_mac[ETH_ALEN];
	u8 nan_dp_count;
	struct delayed_work	nan_disable;
	int nan_disc_count;
	nan_disc_result_cache *nan_disc_cache;
	nan_svc_info_t svc_info[NAN_MAX_SVC_INST];
	nan_ranging_inst_t nan_ranging_info[NAN_MAX_RANGING_INST];
	wl_nan_ver_t version;
	struct mutex nan_sync;
	uint8 svc_inst_id_mask[NAN_SVC_INST_SIZE];
	uint8 inst_id_start;
	/* wait queue and condition variable for nan event */
	bool nan_event_recvd;
	wait_queue_head_t nan_event_wait;
	bool notify_user;
	bool mac_rand;
	uint8 max_ndp_count;       /* Max no. of NDPs */
	nan_ndp_peer_t *nan_ndp_peer_info;
	nan_data_path_id ndp_id[NAN_MAX_NDP_PEER];
	uint8 ndpe_enabled;
	uint8 max_ndi_supported;
	wl_ndi_data_t *ndi;
	bool ranging_enable;
} wl_nancfg_t;

bool wl_cfgnan_is_enabled(struct bcm_cfg80211 *cfg);
int wl_cfgnan_check_nan_disable_pending(struct bcm_cfg80211 *cfg,
bool force_disable, bool is_sync_reqd);
int wl_cfgnan_start_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_config_cmd_data_t *cmd_data, uint32 nan_attr_mask);
int wl_cfgnan_stop_handler(struct net_device *ndev, struct bcm_cfg80211 *cfg);
void wl_cfgnan_delayed_disable(struct work_struct *work);
int wl_cfgnan_config_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_config_cmd_data_t *cmd_data, uint32 nan_attr_mask);
int wl_cfgnan_support_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_config_cmd_data_t *cmd_data);
int wl_cfgnan_status_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_config_cmd_data_t *cmd_data);
int wl_cfgnan_publish_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_discover_cmd_data_t *cmd_data);
int wl_cfgnan_subscribe_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_discover_cmd_data_t *cmd_data);
int wl_cfgnan_cancel_pub_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_discover_cmd_data_t *cmd_data);
int wl_cfgnan_cancel_sub_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_discover_cmd_data_t *cmd_data);
int wl_cfgnan_transmit_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_discover_cmd_data_t *cmd_data);
s32 wl_cfgnan_notify_nan_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
int wl_cfgnan_generate_inst_id(struct bcm_cfg80211 *cfg, uint8 *p_inst_id);
int wl_cfgnan_remove_inst_id(struct bcm_cfg80211 *cfg, uint8 inst_id);
int wl_cfgnan_get_capablities_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_hal_capabilities_t *capabilities);
int wl_cfgnan_data_path_iface_create_delete_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, char *ifname, uint16 type, uint8 busstate);
int wl_cfgnan_data_path_request_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_datapath_cmd_data_t *cmd_data,
	uint8 *ndp_instance_id);
int wl_cfgnan_data_path_response_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_datapath_cmd_data_t *cmd_data);
int wl_cfgnan_data_path_end_handler(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, nan_data_path_id ndp_instance_id,
	int *status);
const char * nan_event_to_str(u16 cmd);

#ifdef WL_NAN_DISC_CACHE
int wl_cfgnan_sec_info_handler(struct bcm_cfg80211 *cfg,
	nan_datapath_sec_info_cmd_data_t *cmd_data, nan_hal_resp_t *nan_req_resp);
/* ranging quest and response iovar handler */
#endif /* WL_NAN_DISC_CACHE */
bool wl_cfgnan_is_dp_active(struct net_device *ndev);
bool wl_cfgnan_data_dp_exists_with_peer(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer_addr);
s32 wl_cfgnan_delete_ndp(struct bcm_cfg80211 *cfg, struct net_device *nan_ndev);
int wl_cfgnan_set_enable_merge(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, uint8 enable, uint32 *status);
int wl_cfgnan_attach(struct bcm_cfg80211 *cfg);
void wl_cfgnan_detach(struct bcm_cfg80211 *cfg);
int wl_cfgnan_get_status(struct net_device *ndev, wl_nan_conf_status_t *nan_status);

#ifdef RTT_SUPPORT
int wl_cfgnan_trigger_ranging(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, void *event_data, nan_svc_info_t *svc,
	uint8 range_req, bool accept_req);
nan_ranging_inst_t *wl_cfgnan_get_ranging_inst(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer, nan_range_role_t range_role);
nan_ranging_inst_t* wl_cfgnan_check_for_ranging(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer);
int wl_cfgnan_trigger_geofencing_ranging(struct net_device *dev,
	struct ether_addr *peer_addr);
int wl_cfgnan_suspend_geofence_rng_session(struct net_device *ndev,
	struct ether_addr *peer, int suspend_reason, u8 cancel_flags);
void wl_cfgnan_suspend_all_geofence_rng_sessions(struct net_device *ndev,
	int suspend_reason, u8 cancel_flags);
int wl_cfgnan_terminate_directed_rtt_sessions(struct net_device *ndev, struct bcm_cfg80211 *cfg);
void wl_cfgnan_reset_geofence_ranging(struct bcm_cfg80211 *cfg,
	nan_ranging_inst_t * rng_inst, int sched_reason, bool need_rtt_mutex);
void wl_cfgnan_reset_geofence_ranging_for_cur_target(dhd_pub_t *dhd, int sched_reason);
void wl_cfgnan_process_range_report(struct bcm_cfg80211 *cfg,
	wl_nan_ev_rng_rpt_ind_t *range_res, int rtt_status);
int wl_cfgnan_cancel_ranging(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, uint8 *range_id, uint8 flags, uint32 *status);
bool wl_cfgnan_ranging_allowed(struct bcm_cfg80211 *cfg);
uint8 wl_cfgnan_cancel_rng_responders(struct net_device *ndev);
bool wl_cfgnan_check_role_concurrency(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer_addr);
bool wl_cfgnan_update_geofence_target_idx(struct bcm_cfg80211 *cfg);
bool wl_cfgnan_ranging_is_in_prog_for_peer(struct bcm_cfg80211 *cfg,
	struct ether_addr *peer_addr);
#endif /* RTT_SUPPORT */

typedef enum {
	NAN_ATTRIBUTE_HEADER                            = 100,
	NAN_ATTRIBUTE_HANDLE                            = 101,
	NAN_ATTRIBUTE_TRANSAC_ID                        = 102,

	/* NAN Enable request attributes */
	NAN_ATTRIBUTE_2G_SUPPORT                        = 103,
	NAN_ATTRIBUTE_5G_SUPPORT                        = 104,
	NAN_ATTRIBUTE_CLUSTER_LOW                       = 105,
	NAN_ATTRIBUTE_CLUSTER_HIGH                      = 106,
	NAN_ATTRIBUTE_SID_BEACON                        = 107,
	NAN_ATTRIBUTE_SYNC_DISC_2G_BEACON               = 108,
	NAN_ATTRIBUTE_SYNC_DISC_5G_BEACON               = 109,
	NAN_ATTRIBUTE_SDF_2G_SUPPORT                    = 110,
	NAN_ATTRIBUTE_SDF_5G_SUPPORT                    = 111,
	NAN_ATTRIBUTE_RSSI_CLOSE                        = 112,
	NAN_ATTRIBUTE_RSSI_MIDDLE                       = 113,
	NAN_ATTRIBUTE_RSSI_PROXIMITY                    = 114,
	NAN_ATTRIBUTE_HOP_COUNT_LIMIT                   = 115,
	NAN_ATTRIBUTE_RANDOM_TIME                       = 116,
	NAN_ATTRIBUTE_MASTER_PREF                       = 117,
	NAN_ATTRIBUTE_PERIODIC_SCAN_INTERVAL            = 118,

	/* Nan Publish/Subscribe request attributes */
	NAN_ATTRIBUTE_PUBLISH_ID                        = 119,
	NAN_ATTRIBUTE_TTL                               = 120,
	NAN_ATTRIBUTE_PERIOD                            = 121,
	NAN_ATTRIBUTE_REPLIED_EVENT_FLAG                = 122,
	NAN_ATTRIBUTE_PUBLISH_TYPE                      = 123,
	NAN_ATTRIBUTE_TX_TYPE                           = 124,
	NAN_ATTRIBUTE_PUBLISH_COUNT                     = 125,
	NAN_ATTRIBUTE_SERVICE_NAME_LEN                  = 126,
	NAN_ATTRIBUTE_SERVICE_NAME                      = 127,
	NAN_ATTRIBUTE_SERVICE_SPECIFIC_INFO_LEN         = 128,
	NAN_ATTRIBUTE_SERVICE_SPECIFIC_INFO             = 129,
	NAN_ATTRIBUTE_RX_MATCH_FILTER_LEN               = 130,
	NAN_ATTRIBUTE_RX_MATCH_FILTER                   = 131,
	NAN_ATTRIBUTE_TX_MATCH_FILTER_LEN               = 132,
	NAN_ATTRIBUTE_TX_MATCH_FILTER                   = 133,
	NAN_ATTRIBUTE_SUBSCRIBE_ID                      = 134,
	NAN_ATTRIBUTE_SUBSCRIBE_TYPE                    = 135,
	NAN_ATTRIBUTE_SERVICERESPONSEFILTER             = 136,
	NAN_ATTRIBUTE_SERVICERESPONSEINCLUDE            = 137,
	NAN_ATTRIBUTE_USESERVICERESPONSEFILTER          = 138,
	NAN_ATTRIBUTE_SSIREQUIREDFORMATCHINDICATION     = 139,
	NAN_ATTRIBUTE_SUBSCRIBE_MATCH                   = 140,
	NAN_ATTRIBUTE_SUBSCRIBE_COUNT                   = 141,
	NAN_ATTRIBUTE_MAC_ADDR                          = 142,
	NAN_ATTRIBUTE_MAC_ADDR_LIST                     = 143,
	NAN_ATTRIBUTE_MAC_ADDR_LIST_NUM_ENTRIES         = 144,
	NAN_ATTRIBUTE_PUBLISH_MATCH                     = 145,

	/* Nan Event attributes */
	NAN_ATTRIBUTE_ENABLE_STATUS                     = 146,
	NAN_ATTRIBUTE_JOIN_STATUS                       = 147,
	NAN_ATTRIBUTE_ROLE                              = 148,
	NAN_ATTRIBUTE_MASTER_RANK                       = 149,
	NAN_ATTRIBUTE_ANCHOR_MASTER_RANK                = 150,
	NAN_ATTRIBUTE_CNT_PEND_TXFRM                    = 151,
	NAN_ATTRIBUTE_CNT_BCN_TX                        = 152,
	NAN_ATTRIBUTE_CNT_BCN_RX                        = 153,
	NAN_ATTRIBUTE_CNT_SVC_DISC_TX                   = 154,
	NAN_ATTRIBUTE_CNT_SVC_DISC_RX                   = 155,
	NAN_ATTRIBUTE_AMBTT                             = 156,
	NAN_ATTRIBUTE_CLUSTER_ID                        = 157,
	NAN_ATTRIBUTE_INST_ID                           = 158,
	NAN_ATTRIBUTE_OUI                               = 159,
	NAN_ATTRIBUTE_STATUS                            = 160,
	NAN_ATTRIBUTE_DE_EVENT_TYPE                     = 161,
	NAN_ATTRIBUTE_MERGE                             = 162,
	NAN_ATTRIBUTE_IFACE                             = 163,
	NAN_ATTRIBUTE_CHANNEL                           = 164,
	NAN_ATTRIBUTE_PEER_ID                           = 165,
	NAN_ATTRIBUTE_NDP_ID                            = 167,
	NAN_ATTRIBUTE_SECURITY                          = 168,
	NAN_ATTRIBUTE_QOS                               = 169,
	NAN_ATTRIBUTE_RSP_CODE                          = 170,
	NAN_ATTRIBUTE_INST_COUNT                        = 171,
	NAN_ATTRIBUTE_PEER_DISC_MAC_ADDR                = 172,
	NAN_ATTRIBUTE_PEER_NDI_MAC_ADDR                 = 173,
	NAN_ATTRIBUTE_IF_ADDR                           = 174,
	NAN_ATTRIBUTE_WARMUP_TIME                       = 175,
	NAN_ATTRIBUTE_RECV_IND_CFG                      = 176,
	NAN_ATTRIBUTE_RSSI_CLOSE_5G                     = 177,
	NAN_ATTRIBUTE_RSSI_MIDDLE_5G                    = 178,
	NAN_ATTRIBUTE_RSSI_PROXIMITY_5G                 = 179,
	NAN_ATTRIBUTE_CONNMAP                           = 180,
	NAN_ATTRIBUTE_24G_CHANNEL                       = 181,
	NAN_ATTRIBUTE_5G_CHANNEL                        = 182,
	NAN_ATTRIBUTE_DWELL_TIME                        = 183,
	NAN_ATTRIBUTE_SCAN_PERIOD                       = 184,
	NAN_ATTRIBUTE_RSSI_WINDOW_SIZE			= 185,
	NAN_ATTRIBUTE_CONF_CLUSTER_VAL			= 186,
	NAN_ATTRIBUTE_AVAIL_BIT_MAP                     = 187,
	NAN_ATTRIBUTE_ENTRY_CONTROL			= 188,
	NAN_ATTRIBUTE_CIPHER_SUITE_TYPE                 = 189,
	NAN_ATTRIBUTE_KEY_TYPE                          = 190,
	NAN_ATTRIBUTE_KEY_LEN                           = 191,
	NAN_ATTRIBUTE_SCID                              = 192,
	NAN_ATTRIBUTE_SCID_LEN                          = 193,
	NAN_ATTRIBUTE_SDE_CONTROL_CONFIG_DP             = 194,
	NAN_ATTRIBUTE_SDE_CONTROL_SECURITY		= 195,
	NAN_ATTRIBUTE_SDE_CONTROL_DP_TYPE		= 196,
	NAN_ATTRIBUTE_SDE_CONTROL_RANGE_SUPPORT		= 197,
	NAN_ATTRIBUTE_NO_CONFIG_AVAIL			= 198,
	NAN_ATTRIBUTE_2G_AWAKE_DW			= 199,
	NAN_ATTRIBUTE_5G_AWAKE_DW			= 200,
	NAN_ATTRIBUTE_RANGING_INTERVAL			= 201,
	NAN_ATTRIBUTE_RANGING_INDICATION		= 202,
	NAN_ATTRIBUTE_RANGING_INGRESS_LIMIT		= 203,
	NAN_ATTRIBUTE_RANGING_EGRESS_LIMIT		= 204,
	NAN_ATTRIBUTE_RANGING_AUTO_ACCEPT		= 205,
	NAN_ATTRIBUTE_RANGING_RESULT			= 206,
	NAN_ATTRIBUTE_DISC_IND_CFG			= 207,
	NAN_ATTRIBUTE_RSSI_THRESHOLD_FLAG		= 208,
	NAN_ATTRIBUTE_KEY_DATA                          = 209,
	NAN_ATTRIBUTE_SDEA_SERVICE_SPECIFIC_INFO_LEN    = 210,
	NAN_ATTRIBUTE_SDEA_SERVICE_SPECIFIC_INFO        = 211,
	NAN_ATTRIBUTE_REASON				= 212,
	NAN_ATTRIBUTE_DWELL_TIME_5G                     = 215,
	NAN_ATTRIBUTE_SCAN_PERIOD_5G                    = 216,
	NAN_ATTRIBUTE_SVC_RESPONDER_POLICY              = 217,
	NAN_ATTRIBUTE_EVENT_MASK			= 218,
	NAN_ATTRIBUTE_SUB_SID_BEACON                    = 219,
	NAN_ATTRIBUTE_RANDOMIZATION_INTERVAL            = 220,
	NAN_ATTRIBUTE_CMD_RESP_DATA			= 221,
	NAN_ATTRIBUTE_CMD_USE_NDPE			= 222,
	NAN_ATTRIBUTE_ENABLE_MERGE			= 223,
	NAN_ATTRIBUTE_DISCOVERY_BEACON_INTERVAL		= 224,
	NAN_ATTRIBUTE_NSS				= 225,
	NAN_ATTRIBUTE_ENABLE_RANGING			= 226,
	NAN_ATTRIBUTE_DW_EARLY_TERM			= 227,
	NAN_ATTRIBUTE_CHANNEL_INFO			= 228,
	NAN_ATTRIBUTE_NUM_CHANNELS			= 229,
	NAN_ATTRIBUTE_INSTANT_MODE_ENABLE		= 230,
	NAN_ATTRIBUTE_INSTANT_COMM_CHAN			= 231,
	NAN_ATTRIBUTE_MAX				= 232
} NAN_ATTRIBUTE;

enum geofence_suspend_reason {
	RTT_GEO_SUSPN_HOST_DIR_RTT_TRIG = 0,
	RTT_GEO_SUSPN_PEER_RTT_TRIGGER = 1,
	RTT_GEO_SUSPN_HOST_NDP_TRIGGER = 2,
	RTT_GEO_SUSPN_PEER_NDP_TRIGGER = 3,
	RTT_GEO_SUSPN_RANGE_RES_REPORTED = 4
};
#endif	/* _wl_cfgnan_h_ */
