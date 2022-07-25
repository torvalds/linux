/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file function prototypes, data structure
 * and  definitions for all the host/station commands
 */

#ifndef _LBS_HOST_H_
#define _LBS_HOST_H_

#include "types.h"
#include "defs.h"

#define DEFAULT_AD_HOC_CHANNEL                  6

#define CMD_OPTION_WAITFORRSP                   0x0002

/* Host command IDs */

/*
 * Return command are almost always the same as the host command, but with
 * bit 15 set high.  There are a few exceptions, though...
 */
#define CMD_RET(cmd)                            (0x8000 | cmd)

/* Return command convention exceptions: */
#define CMD_RET_802_11_ASSOCIATE                0x8012

/* Command codes */
#define CMD_GET_HW_SPEC                         0x0003
#define CMD_EEPROM_UPDATE                       0x0004
#define CMD_802_11_RESET                        0x0005
#define CMD_802_11_SCAN                         0x0006
#define CMD_802_11_GET_LOG                      0x000b
#define CMD_MAC_MULTICAST_ADR                   0x0010
#define CMD_802_11_AUTHENTICATE                 0x0011
#define CMD_802_11_EEPROM_ACCESS                0x0059
#define CMD_802_11_ASSOCIATE                    0x0050
#define CMD_802_11_SET_WEP                      0x0013
#define CMD_802_11_GET_STAT                     0x0014
#define CMD_802_3_GET_STAT                      0x0015
#define CMD_802_11_SNMP_MIB                     0x0016
#define CMD_MAC_REG_MAP                         0x0017
#define CMD_BBP_REG_MAP                         0x0018
#define CMD_MAC_REG_ACCESS                      0x0019
#define CMD_BBP_REG_ACCESS                      0x001a
#define CMD_RF_REG_ACCESS                       0x001b
#define CMD_802_11_RADIO_CONTROL                0x001c
#define CMD_802_11_RF_CHANNEL                   0x001d
#define CMD_802_11_RF_TX_POWER                  0x001e
#define CMD_802_11_RSSI                         0x001f
#define CMD_802_11_RF_ANTENNA                   0x0020
#define CMD_802_11_PS_MODE                      0x0021
#define CMD_802_11_DATA_RATE                    0x0022
#define CMD_RF_REG_MAP                          0x0023
#define CMD_802_11_DEAUTHENTICATE               0x0024
#define CMD_802_11_REASSOCIATE                  0x0025
#define CMD_MAC_CONTROL                         0x0028
#define CMD_802_11_AD_HOC_START                 0x002b
#define CMD_802_11_AD_HOC_JOIN                  0x002c
#define CMD_802_11_QUERY_TKIP_REPLY_CNTRS       0x002e
#define CMD_802_11_ENABLE_RSN                   0x002f
#define CMD_802_11_SET_AFC                      0x003c
#define CMD_802_11_GET_AFC                      0x003d
#define CMD_802_11_DEEP_SLEEP                   0x003e
#define CMD_802_11_AD_HOC_STOP                  0x0040
#define CMD_802_11_HOST_SLEEP_CFG               0x0043
#define CMD_802_11_WAKEUP_CONFIRM               0x0044
#define CMD_802_11_HOST_SLEEP_ACTIVATE          0x0045
#define CMD_802_11_BEACON_STOP                  0x0049
#define CMD_802_11_MAC_ADDRESS                  0x004d
#define CMD_802_11_LED_GPIO_CTRL                0x004e
#define CMD_802_11_BAND_CONFIG                  0x0058
#define CMD_GSPI_BUS_CONFIG                     0x005a
#define CMD_802_11D_DOMAIN_INFO                 0x005b
#define CMD_802_11_KEY_MATERIAL                 0x005e
#define CMD_802_11_SLEEP_PARAMS                 0x0066
#define CMD_802_11_INACTIVITY_TIMEOUT           0x0067
#define CMD_802_11_SLEEP_PERIOD                 0x0068
#define CMD_802_11_TPC_CFG                      0x0072
#define CMD_802_11_PA_CFG                       0x0073
#define CMD_802_11_FW_WAKE_METHOD               0x0074
#define CMD_802_11_SUBSCRIBE_EVENT              0x0075
#define CMD_802_11_RATE_ADAPT_RATESET           0x0076
#define CMD_802_11_TX_RATE_QUERY                0x007f
#define CMD_GET_TSF                             0x0080
#define CMD_BT_ACCESS                           0x0087
#define CMD_FWT_ACCESS                          0x0095
#define CMD_802_11_MONITOR_MODE                 0x0098
#define CMD_MESH_ACCESS                         0x009b
#define CMD_MESH_CONFIG_OLD                     0x00a3
#define CMD_MESH_CONFIG                         0x00ac
#define CMD_SET_BOOT2_VER                       0x00a5
#define CMD_FUNC_INIT                           0x00a9
#define CMD_FUNC_SHUTDOWN                       0x00aa
#define CMD_802_11_BEACON_CTRL                  0x00b0

/* For the IEEE Power Save */
#define PS_MODE_ACTION_ENTER_PS                 0x0030
#define PS_MODE_ACTION_EXIT_PS                  0x0031
#define PS_MODE_ACTION_SLEEP_CONFIRMED          0x0034

#define CMD_ENABLE_RSN                          0x0001
#define CMD_DISABLE_RSN                         0x0000

#define CMD_ACT_GET                             0x0000
#define CMD_ACT_SET                             0x0001

/* Define action or option for CMD_802_11_SET_WEP */
#define CMD_ACT_ADD                             0x0002
#define CMD_ACT_REMOVE                          0x0004

#define CMD_TYPE_WEP_40_BIT                     0x01
#define CMD_TYPE_WEP_104_BIT                    0x02

#define CMD_NUM_OF_WEP_KEYS                     4

#define CMD_WEP_KEY_INDEX_MASK                  0x3fff

/* Define action or option for CMD_802_11_SCAN */
#define CMD_BSS_TYPE_BSS                        0x0001
#define CMD_BSS_TYPE_IBSS                       0x0002
#define CMD_BSS_TYPE_ANY                        0x0003

/* Define action or option for CMD_802_11_SCAN */
#define CMD_SCAN_TYPE_ACTIVE                    0x0000
#define CMD_SCAN_TYPE_PASSIVE                   0x0001

#define CMD_SCAN_RADIO_TYPE_BG                  0

#define CMD_SCAN_PROBE_DELAY_TIME               0

/* Define action or option for CMD_MAC_CONTROL */
#define CMD_ACT_MAC_RX_ON                       0x0001
#define CMD_ACT_MAC_TX_ON                       0x0002
#define CMD_ACT_MAC_LOOPBACK_ON                 0x0004
#define CMD_ACT_MAC_WEP_ENABLE                  0x0008
#define CMD_ACT_MAC_INT_ENABLE                  0x0010
#define CMD_ACT_MAC_MULTICAST_ENABLE            0x0020
#define CMD_ACT_MAC_BROADCAST_ENABLE            0x0040
#define CMD_ACT_MAC_PROMISCUOUS_ENABLE          0x0080
#define CMD_ACT_MAC_ALL_MULTICAST_ENABLE        0x0100
#define CMD_ACT_MAC_STRICT_PROTECTION_ENABLE    0x0400

/* Event flags for CMD_802_11_SUBSCRIBE_EVENT */
#define CMD_SUBSCRIBE_RSSI_LOW                  0x0001
#define CMD_SUBSCRIBE_SNR_LOW                   0x0002
#define CMD_SUBSCRIBE_FAILCOUNT                 0x0004
#define CMD_SUBSCRIBE_BCNMISS                   0x0008
#define CMD_SUBSCRIBE_RSSI_HIGH                 0x0010
#define CMD_SUBSCRIBE_SNR_HIGH                  0x0020

#define RADIO_PREAMBLE_LONG                     0x00
#define RADIO_PREAMBLE_SHORT                    0x02
#define RADIO_PREAMBLE_AUTO                     0x04

/* Define action or option for CMD_802_11_RF_CHANNEL */
#define CMD_OPT_802_11_RF_CHANNEL_GET           0x00
#define CMD_OPT_802_11_RF_CHANNEL_SET           0x01

/* Define action or option for CMD_802_11_DATA_RATE */
#define CMD_ACT_SET_TX_AUTO                     0x0000
#define CMD_ACT_SET_TX_FIX_RATE                 0x0001
#define CMD_ACT_GET_TX_RATE                     0x0002

/* Options for CMD_802_11_FW_WAKE_METHOD */
#define CMD_WAKE_METHOD_UNCHANGED               0x0000
#define CMD_WAKE_METHOD_COMMAND_INT             0x0001
#define CMD_WAKE_METHOD_GPIO                    0x0002

/* Object IDs for CMD_802_11_SNMP_MIB */
#define SNMP_MIB_OID_BSS_TYPE                   0x0000
#define SNMP_MIB_OID_OP_RATE_SET                0x0001
#define SNMP_MIB_OID_BEACON_PERIOD              0x0002  /* Reserved on v9+ */
#define SNMP_MIB_OID_DTIM_PERIOD                0x0003  /* Reserved on v9+ */
#define SNMP_MIB_OID_ASSOC_TIMEOUT              0x0004  /* Reserved on v9+ */
#define SNMP_MIB_OID_RTS_THRESHOLD              0x0005
#define SNMP_MIB_OID_SHORT_RETRY_LIMIT          0x0006
#define SNMP_MIB_OID_LONG_RETRY_LIMIT           0x0007
#define SNMP_MIB_OID_FRAG_THRESHOLD             0x0008
#define SNMP_MIB_OID_11D_ENABLE                 0x0009
#define SNMP_MIB_OID_11H_ENABLE                 0x000A

/* Define action or option for CMD_BT_ACCESS */
enum cmd_bt_access_opts {
	/* The bt commands start at 5 instead of 1 because the old dft commands
	 * are mapped to 1-4.  These old commands are no longer maintained and
	 * should not be called.
	 */
	CMD_ACT_BT_ACCESS_ADD = 5,
	CMD_ACT_BT_ACCESS_DEL,
	CMD_ACT_BT_ACCESS_LIST,
	CMD_ACT_BT_ACCESS_RESET,
	CMD_ACT_BT_ACCESS_SET_INVERT,
	CMD_ACT_BT_ACCESS_GET_INVERT
};

/* Define action or option for CMD_FWT_ACCESS */
enum cmd_fwt_access_opts {
	CMD_ACT_FWT_ACCESS_ADD = 1,
	CMD_ACT_FWT_ACCESS_DEL,
	CMD_ACT_FWT_ACCESS_LOOKUP,
	CMD_ACT_FWT_ACCESS_LIST,
	CMD_ACT_FWT_ACCESS_LIST_ROUTE,
	CMD_ACT_FWT_ACCESS_LIST_NEIGHBOR,
	CMD_ACT_FWT_ACCESS_RESET,
	CMD_ACT_FWT_ACCESS_CLEANUP,
	CMD_ACT_FWT_ACCESS_TIME,
};

/* Define action or option for CMD_802_11_HOST_SLEEP_CFG */
enum cmd_wol_cfg_opts {
	CMD_ACT_ACTION_NONE = 0,
	CMD_ACT_SET_WOL_RULE,
	CMD_ACT_GET_WOL_RULE,
	CMD_ACT_RESET_WOL_RULE,
};

/* Define action or option for CMD_MESH_ACCESS */
enum cmd_mesh_access_opts {
	CMD_ACT_MESH_GET_TTL = 1,
	CMD_ACT_MESH_SET_TTL,
	CMD_ACT_MESH_GET_STATS,
	CMD_ACT_MESH_GET_ANYCAST,
	CMD_ACT_MESH_SET_ANYCAST,
	CMD_ACT_MESH_SET_LINK_COSTS,
	CMD_ACT_MESH_GET_LINK_COSTS,
	CMD_ACT_MESH_SET_BCAST_RATE,
	CMD_ACT_MESH_GET_BCAST_RATE,
	CMD_ACT_MESH_SET_RREQ_DELAY,
	CMD_ACT_MESH_GET_RREQ_DELAY,
	CMD_ACT_MESH_SET_ROUTE_EXP,
	CMD_ACT_MESH_GET_ROUTE_EXP,
	CMD_ACT_MESH_SET_AUTOSTART_ENABLED,
	CMD_ACT_MESH_GET_AUTOSTART_ENABLED,
	CMD_ACT_MESH_SET_GET_PRB_RSP_LIMIT = 17,
};

/* Define actions and types for CMD_MESH_CONFIG */
enum cmd_mesh_config_actions {
	CMD_ACT_MESH_CONFIG_STOP = 0,
	CMD_ACT_MESH_CONFIG_START,
	CMD_ACT_MESH_CONFIG_SET,
	CMD_ACT_MESH_CONFIG_GET,
};

enum cmd_mesh_config_types {
	CMD_TYPE_MESH_SET_BOOTFLAG = 1,
	CMD_TYPE_MESH_SET_BOOTTIME,
	CMD_TYPE_MESH_SET_DEF_CHANNEL,
	CMD_TYPE_MESH_SET_MESH_IE,
	CMD_TYPE_MESH_GET_DEFAULTS,
	CMD_TYPE_MESH_GET_MESH_IE, /* GET_DEFAULTS is superset of GET_MESHIE */
};

/* Card Event definition */
#define MACREG_INT_CODE_TX_PPA_FREE		0
#define MACREG_INT_CODE_TX_DMA_DONE		1
#define MACREG_INT_CODE_LINK_LOST_W_SCAN	2
#define MACREG_INT_CODE_LINK_LOST_NO_SCAN	3
#define MACREG_INT_CODE_LINK_SENSED		4
#define MACREG_INT_CODE_CMD_FINISHED		5
#define MACREG_INT_CODE_MIB_CHANGED		6
#define MACREG_INT_CODE_INIT_DONE		7
#define MACREG_INT_CODE_DEAUTHENTICATED		8
#define MACREG_INT_CODE_DISASSOCIATED		9
#define MACREG_INT_CODE_PS_AWAKE		10
#define MACREG_INT_CODE_PS_SLEEP		11
#define MACREG_INT_CODE_MIC_ERR_MULTICAST	13
#define MACREG_INT_CODE_MIC_ERR_UNICAST		14
#define MACREG_INT_CODE_WM_AWAKE		15
#define MACREG_INT_CODE_DEEP_SLEEP_AWAKE	16
#define MACREG_INT_CODE_ADHOC_BCN_LOST		17
#define MACREG_INT_CODE_HOST_AWAKE		18
#define MACREG_INT_CODE_STOP_TX			19
#define MACREG_INT_CODE_START_TX		20
#define MACREG_INT_CODE_CHANNEL_SWITCH		21
#define MACREG_INT_CODE_MEASUREMENT_RDY		22
#define MACREG_INT_CODE_WMM_CHANGE		23
#define MACREG_INT_CODE_BG_SCAN_REPORT		24
#define MACREG_INT_CODE_RSSI_LOW		25
#define MACREG_INT_CODE_SNR_LOW			26
#define MACREG_INT_CODE_MAX_FAIL		27
#define MACREG_INT_CODE_RSSI_HIGH		28
#define MACREG_INT_CODE_SNR_HIGH		29
#define MACREG_INT_CODE_MESH_AUTO_STARTED	35
#define MACREG_INT_CODE_FIRMWARE_READY		48


/* 802.11-related definitions */

/* TxPD descriptor */
struct txpd {
	/* union to cope up with later FW revisions */
	union {
		/* Current Tx packet status */
		__le32 tx_status;
		struct {
			/* BSS type: client, AP, etc. */
			u8 bss_type;
			/* BSS number */
			u8 bss_num;
			/* Reserved */
			__le16 reserved;
		} bss;
	} u;
	/* Tx control */
	__le32 tx_control;
	__le32 tx_packet_location;
	/* Tx packet length */
	__le16 tx_packet_length;
	struct_group_attr(tx_dest_addr, __packed,
		/* First 2 byte of destination MAC address */
		u8 tx_dest_addr_high[2];
		/* Last 4 byte of destination MAC address */
		u8 tx_dest_addr_low[4];
	);
	/* Pkt Priority */
	u8 priority;
	/* Pkt Trasnit Power control */
	u8 powermgmt;
	/* Amount of time the packet has been queued (units = 2ms) */
	u8 pktdelay_2ms;
	/* reserved */
	u8 reserved1;
} __packed;

/* RxPD Descriptor */
struct rxpd {
	/* union to cope up with later FW revisions */
	union {
		/* Current Rx packet status */
		__le16 status;
		struct {
			/* BSS type: client, AP, etc. */
			u8 bss_type;
			/* BSS number */
			u8 bss_num;
		} __packed bss;
	} __packed u;

	/* SNR */
	u8 snr;

	/* Tx control */
	u8 rx_control;

	/* Pkt length */
	__le16 pkt_len;

	/* Noise Floor */
	u8 nf;

	/* Rx Packet Rate */
	u8 rx_rate;

	/* Pkt addr */
	__le32 pkt_ptr;

	/* Next Rx RxPD addr */
	__le32 next_rxpd_ptr;

	/* Pkt Priority */
	u8 priority;
	u8 reserved[3];
} __packed;

struct cmd_header {
	__le16 command;
	__le16 size;
	__le16 seqnum;
	__le16 result;
} __packed;

/* Generic structure to hold all key types. */
struct enc_key {
	u16 len;
	u16 flags;  /* KEY_INFO_* from defs.h */
	u16 type; /* KEY_TYPE_* from defs.h */
	u8 key[32];
};

/* lbs_offset_value */
struct lbs_offset_value {
	u32 offset;
	u32 value;
} __packed;

#define MAX_11D_TRIPLETS	83

struct mrvl_ie_domain_param_set {
	struct mrvl_ie_header header;

	u8 country_code[IEEE80211_COUNTRY_STRING_LEN];
	struct ieee80211_country_ie_triplet triplet[MAX_11D_TRIPLETS];
} __packed;

struct cmd_ds_802_11d_domain_info {
	struct cmd_header hdr;

	__le16 action;
	struct mrvl_ie_domain_param_set domain;
} __packed;

/*
 * Define data structure for CMD_GET_HW_SPEC
 * This structure defines the response for the GET_HW_SPEC command
 */
struct cmd_ds_get_hw_spec {
	struct cmd_header hdr;

	/* HW Interface version number */
	__le16 hwifversion;
	/* HW version number */
	__le16 version;
	/* Max number of TxPD FW can handle */
	__le16 nr_txpd;
	/* Max no of Multicast address */
	__le16 nr_mcast_adr;
	/* MAC address */
	u8 permanentaddr[6];

	/* region Code */
	__le16 regioncode;

	/* Number of antenna used */
	__le16 nr_antenna;

	/* FW release number, example 0x01030304 = 2.3.4p1 */
	__le32 fwrelease;

	/* Base Address of TxPD queue */
	__le32 wcb_base;
	/* Read Pointer of RxPd queue */
	__le32 rxpd_rdptr;

	/* Write Pointer of RxPd queue */
	__le32 rxpd_wrptr;

	/*FW/HW capability */
	__le32 fwcapinfo;
} __packed;

struct cmd_ds_802_11_subscribe_event {
	struct cmd_header hdr;

	__le16 action;
	__le16 events;

	/* A TLV to the CMD_802_11_SUBSCRIBE_EVENT command can contain a
	 * number of TLVs. From the v5.1 manual, those TLVs would add up to
	 * 40 bytes. However, future firmware might add additional TLVs, so I
	 * bump this up a bit.
	 */
	uint8_t tlv[128];
} __packed;

/*
 * This scan handle Country Information IE(802.11d compliant)
 * Define data structure for CMD_802_11_SCAN
 */
struct cmd_ds_802_11_scan {
	struct cmd_header hdr;

	uint8_t bsstype;
	uint8_t bssid[ETH_ALEN];
	uint8_t tlvbuffer[];
} __packed;

struct cmd_ds_802_11_scan_rsp {
	struct cmd_header hdr;

	__le16 bssdescriptsize;
	uint8_t nr_sets;
	uint8_t bssdesc_and_tlvbuffer[];
} __packed;

struct cmd_ds_802_11_get_log {
	struct cmd_header hdr;

	__le32 mcasttxframe;
	__le32 failed;
	__le32 retry;
	__le32 multiretry;
	__le32 framedup;
	__le32 rtssuccess;
	__le32 rtsfailure;
	__le32 ackfailure;
	__le32 rxfrag;
	__le32 mcastrxframe;
	__le32 fcserror;
	__le32 txframe;
	__le32 wepundecryptable;
} __packed;

struct cmd_ds_mac_control {
	struct cmd_header hdr;
	__le16 action;
	u16 reserved;
} __packed;

struct cmd_ds_mac_multicast_adr {
	struct cmd_header hdr;
	__le16 action;
	__le16 nr_of_adrs;
	u8 maclist[ETH_ALEN * MRVDRV_MAX_MULTICAST_LIST_SIZE];
} __packed;

struct cmd_ds_802_11_authenticate {
	struct cmd_header hdr;

	u8 bssid[ETH_ALEN];
	u8 authtype;
	u8 reserved[10];
} __packed;

struct cmd_ds_802_11_deauthenticate {
	struct cmd_header hdr;

	u8 macaddr[ETH_ALEN];
	__le16 reasoncode;
} __packed;

struct cmd_ds_802_11_associate {
	struct cmd_header hdr;

	u8 bssid[6];
	__le16 capability;
	__le16 listeninterval;
	__le16 bcnperiod;
	u8 dtimperiod;
	/* 512 permitted - enough for required and most optional IEs */
	u8 iebuf[];
} __packed;

struct cmd_ds_802_11_associate_response {
	struct cmd_header hdr;

	__le16 capability;
	__le16 statuscode;
	__le16 aid;
	/* max 512 */
	u8 iebuf[];
} __packed;

struct cmd_ds_802_11_set_wep {
	struct cmd_header hdr;

	/* ACT_ADD, ACT_REMOVE or ACT_ENABLE */
	__le16 action;

	/* key Index selected for Tx */
	__le16 keyindex;

	/* 40, 128bit or TXWEP */
	uint8_t keytype[4];
	uint8_t keymaterial[4][16];
} __packed;

struct cmd_ds_802_11_snmp_mib {
	struct cmd_header hdr;

	__le16 action;
	__le16 oid;
	__le16 bufsize;
	u8 value[128];
} __packed;

struct cmd_ds_reg_access {
	struct cmd_header hdr;

	__le16 action;
	__le16 offset;
	union {
		u8 bbp_rf;  /* for BBP and RF registers */
		__le32 mac; /* for MAC registers */
	} value;
} __packed;

struct cmd_ds_802_11_radio_control {
	struct cmd_header hdr;

	__le16 action;
	__le16 control;
} __packed;

struct cmd_ds_802_11_beacon_control {
	struct cmd_header hdr;

	__le16 action;
	__le16 beacon_enable;
	__le16 beacon_period;
} __packed;

struct cmd_ds_802_11_sleep_params {
	struct cmd_header hdr;

	/* ACT_GET/ACT_SET */
	__le16 action;

	/* Sleep clock error in ppm */
	__le16 error;

	/* Wakeup offset in usec */
	__le16 offset;

	/* Clock stabilization time in usec */
	__le16 stabletime;

	/* control periodic calibration */
	uint8_t calcontrol;

	/* control the use of external sleep clock */
	uint8_t externalsleepclk;

	/* reserved field, should be set to zero */
	__le16 reserved;
} __packed;

struct cmd_ds_802_11_rf_channel {
	struct cmd_header hdr;

	__le16 action;
	__le16 channel;
	__le16 rftype;      /* unused */
	__le16 reserved;    /* unused */
	u8 channellist[32]; /* unused */
} __packed;

struct cmd_ds_802_11_rssi {
	struct cmd_header hdr;

	/*
	 * request:  number of beacons (N) to average the SNR and NF over
	 * response: SNR of most recent beacon
	 */
	__le16 n_or_snr;

	/*
	 * The following fields are only set in the response.
	 * In the request these are reserved and should be set to 0.
	 */
	__le16 nf;       /* most recent beacon noise floor */
	__le16 avg_snr;  /* average SNR weighted by N from request */
	__le16 avg_nf;   /* average noise floor weighted by N from request */
} __packed;

struct cmd_ds_802_11_mac_address {
	struct cmd_header hdr;

	__le16 action;
	u8 macadd[ETH_ALEN];
} __packed;

struct cmd_ds_802_11_rf_tx_power {
	struct cmd_header hdr;

	__le16 action;
	__le16 curlevel;
	s8 maxlevel;
	s8 minlevel;
} __packed;

/* MONITOR_MODE only exists in OLPC v5 firmware */
struct cmd_ds_802_11_monitor_mode {
	struct cmd_header hdr;

	__le16 action;
	__le16 mode;
} __packed;

struct cmd_ds_set_boot2_ver {
	struct cmd_header hdr;

	__le16 action;
	__le16 version;
} __packed;

struct cmd_ds_802_11_fw_wake_method {
	struct cmd_header hdr;

	__le16 action;
	__le16 method;
} __packed;

struct cmd_ds_802_11_ps_mode {
	struct cmd_header hdr;

	__le16 action;

	/*
	 * Interval for keepalive in PS mode:
	 * 0x0000 = don't change
	 * 0x001E = firmware default
	 * 0xFFFF = disable
	 */
	__le16 nullpktinterval;

	/*
	 * Number of DTIM intervals to wake up for:
	 * 0 = don't change
	 * 1 = firmware default
	 * 5 = max
	 */
	__le16 multipledtim;

	__le16 reserved;
	__le16 locallisteninterval;

	/*
	 * AdHoc awake period (FW v9+ only):
	 * 0 = don't change
	 * 1 = always awake (IEEE standard behavior)
	 * 2 - 31 = sleep for (n - 1) periods and awake for 1 period
	 * 32 - 254 = invalid
	 * 255 = sleep at each ATIM
	 */
	__le16 adhoc_awake_period;
} __packed;

struct cmd_confirm_sleep {
	struct cmd_header hdr;

	__le16 action;
	__le16 nullpktinterval;
	__le16 multipledtim;
	__le16 reserved;
	__le16 locallisteninterval;
} __packed;

struct cmd_ds_802_11_data_rate {
	struct cmd_header hdr;

	__le16 action;
	__le16 reserved;
	u8 rates[MAX_RATES];
} __packed;

struct cmd_ds_802_11_rate_adapt_rateset {
	struct cmd_header hdr;
	__le16 action;
	__le16 enablehwauto;
	__le16 bitmap;
} __packed;

struct cmd_ds_802_11_ad_hoc_start {
	struct cmd_header hdr;

	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 bsstype;
	__le16 beaconperiod;
	u8 dtimperiod;   /* Reserved on v9 and later */
	struct ieee_ie_ibss_param_set ibss;
	u8 reserved1[4];
	struct ieee_ie_ds_param_set ds;
	u8 reserved2[4];
	__le16 probedelay;  /* Reserved on v9 and later */
	__le16 capability;
	u8 rates[MAX_RATES];
	u8 tlv_memory_size_pad[100];
} __packed;

struct cmd_ds_802_11_ad_hoc_result {
	struct cmd_header hdr;

	u8 pad[3];
	u8 bssid[ETH_ALEN];
} __packed;

struct adhoc_bssdesc {
	u8 bssid[ETH_ALEN];
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 type;
	__le16 beaconperiod;
	u8 dtimperiod;
	__le64 timestamp;
	__le64 localtime;
	struct ieee_ie_ds_param_set ds;
	u8 reserved1[4];
	struct ieee_ie_ibss_param_set ibss;
	u8 reserved2[4];
	__le16 capability;
	u8 rates[MAX_RATES];

	/*
	 * DO NOT ADD ANY FIELDS TO THIS STRUCTURE. It is used below in the
	 * Adhoc join command and will cause a binary layout mismatch with
	 * the firmware
	 */
} __packed;

struct cmd_ds_802_11_ad_hoc_join {
	struct cmd_header hdr;

	struct adhoc_bssdesc bss;
	__le16 failtimeout;   /* Reserved on v9 and later */
	__le16 probedelay;    /* Reserved on v9 and later */
} __packed;

struct cmd_ds_802_11_ad_hoc_stop {
	struct cmd_header hdr;
} __packed;

struct cmd_ds_802_11_enable_rsn {
	struct cmd_header hdr;

	__le16 action;
	__le16 enable;
} __packed;

struct MrvlIEtype_keyParamSet {
	/* type ID */
	__le16 type;

	/* length of Payload */
	__le16 length;

	/* type of key: WEP=0, TKIP=1, AES=2 */
	__le16 keytypeid;

	/* key control Info specific to a keytypeid */
	__le16 keyinfo;

	/* length of key */
	__le16 keylen;

	/* key material of size keylen */
	u8 key[32];
} __packed;

#define MAX_WOL_RULES 		16

struct host_wol_rule {
	uint8_t rule_no;
	uint8_t rule_ops;
	__le16 sig_offset;
	__le16 sig_length;
	__le16 reserve;
	__be32 sig_mask;
	__be32 signature;
} __packed;

struct wol_config {
	uint8_t action;
	uint8_t pattern;
	uint8_t no_rules_in_cmd;
	uint8_t result;
	struct host_wol_rule rule[MAX_WOL_RULES];
} __packed;

struct cmd_ds_host_sleep {
	struct cmd_header hdr;
	__le32 criteria;
	uint8_t gpio;
	uint16_t gap;
	struct wol_config wol_conf;
} __packed;



struct cmd_ds_802_11_key_material {
	struct cmd_header hdr;

	__le16 action;
	struct MrvlIEtype_keyParamSet keyParamSet[2];
} __packed;

struct cmd_ds_802_11_eeprom_access {
	struct cmd_header hdr;
	__le16 action;
	__le16 offset;
	__le16 len;
	/* firmware says it returns a maximum of 20 bytes */
#define LBS_EEPROM_READ_LEN 20
	u8 value[LBS_EEPROM_READ_LEN];
} __packed;

struct cmd_ds_802_11_tpc_cfg {
	struct cmd_header hdr;

	__le16 action;
	uint8_t enable;
	int8_t P0;
	int8_t P1;
	int8_t P2;
	uint8_t usesnr;
} __packed;


struct cmd_ds_802_11_pa_cfg {
	struct cmd_header hdr;

	__le16 action;
	uint8_t enable;
	int8_t P0;
	int8_t P1;
	int8_t P2;
} __packed;


struct cmd_ds_802_11_led_ctrl {
	struct cmd_header hdr;

	__le16 action;
	__le16 numled;
	u8 data[256];
} __packed;

/* Automatic Frequency Control */
struct cmd_ds_802_11_afc {
	struct cmd_header hdr;

	__le16 afc_auto;
	union {
		struct {
			__le16 threshold;
			__le16 period;
		};
		struct {
			__le16 timing_offset; /* signed */
			__le16 carrier_offset; /* signed */
		};
	};
} __packed;

struct cmd_tx_rate_query {
	__le16 txrate;
} __packed;

struct cmd_ds_get_tsf {
	__le64 tsfvalue;
} __packed;

struct cmd_ds_bt_access {
	struct cmd_header hdr;

	__le16 action;
	__le32 id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
} __packed;

struct cmd_ds_fwt_access {
	struct cmd_header hdr;

	__le16 action;
	__le32 id;
	u8 valid;
	u8 da[ETH_ALEN];
	u8 dir;
	u8 ra[ETH_ALEN];
	__le32 ssn;
	__le32 dsn;
	__le32 metric;
	u8 rate;
	u8 hopcount;
	u8 ttl;
	__le32 expiration;
	u8 sleepmode;
	__le32 snr;
	__le32 references;
	u8 prec[ETH_ALEN];
} __packed;

struct cmd_ds_mesh_config {
	struct cmd_header hdr;

	__le16 action;
	__le16 channel;
	__le16 type;
	__le16 length;
	u8 data[128];	/* last position reserved */
} __packed;

struct cmd_ds_mesh_access {
	struct cmd_header hdr;

	__le16 action;
	__le32 data[32];	/* last position reserved */
} __packed;

/* Number of stats counters returned by the firmware */
#define MESH_STATS_NUM 8
#endif
