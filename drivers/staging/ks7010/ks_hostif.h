/* SPDX-License-Identifier: GPL-2.0 */
/*
 *   Driver for KeyStream wireless LAN
 *
 *   Copyright (c) 2005-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 */

#ifndef _KS_HOSTIF_H_
#define _KS_HOSTIF_H_

#include <linux/compiler.h>
#include <linux/ieee80211.h>

/*
 * HOST-MAC I/F events
 */
#define HIF_DATA_REQ		0xE001
#define HIF_DATA_IND		0xE801
#define HIF_MIB_GET_REQ		0xE002
#define HIF_MIB_GET_CONF	0xE802
#define HIF_MIB_SET_REQ		0xE003
#define HIF_MIB_SET_CONF	0xE803
#define HIF_POWER_MGMT_REQ	0xE004
#define HIF_POWER_MGMT_CONF	0xE804
#define HIF_START_REQ		0xE005
#define HIF_START_CONF		0xE805
#define HIF_CONNECT_IND		0xE806
#define HIF_STOP_REQ		0xE006
#define HIF_STOP_CONF		0xE807
#define HIF_PS_ADH_SET_REQ	0xE007
#define HIF_PS_ADH_SET_CONF	0xE808
#define HIF_INFRA_SET_REQ	0xE008
#define HIF_INFRA_SET_CONF	0xE809
#define HIF_ADH_SET_REQ		0xE009
#define HIF_ADH_SET_CONF	0xE80A
#define HIF_AP_SET_REQ		0xE00A
#define HIF_AP_SET_CONF		0xE80B
#define HIF_ASSOC_INFO_IND	0xE80C
#define HIF_MIC_FAILURE_REQ	0xE00B
#define HIF_MIC_FAILURE_CONF	0xE80D
#define HIF_SCAN_REQ		0xE00C
#define HIF_SCAN_CONF		0xE80E
#define HIF_PHY_INFO_REQ	0xE00D
#define HIF_PHY_INFO_CONF	0xE80F
#define HIF_SLEEP_REQ		0xE00E
#define HIF_SLEEP_CONF		0xE810
#define HIF_PHY_INFO_IND	0xE811
#define HIF_SCAN_IND		0xE812
#define HIF_INFRA_SET2_REQ	0xE00F
#define HIF_INFRA_SET2_CONF	0xE813
#define HIF_ADH_SET2_REQ	0xE010
#define HIF_ADH_SET2_CONF	0xE814

#define HIF_REQ_MAX		0xE010

/*
 * HOST-MAC I/F data structure
 * Byte alignment Little Endian
 */

struct hostif_hdr {
	__le16 size;
	__le16 event;
} __packed;

struct hostif_data_request {
	struct hostif_hdr header;
	__le16 auth_type;
#define TYPE_DATA 0x0000
#define TYPE_AUTH 0x0001
	__le16 reserved;
	u8 data[0];
} __packed;

#define TYPE_PMK1 0x0001
#define TYPE_GMK1 0x0002
#define TYPE_GMK2 0x0003

#define CHANNEL_LIST_MAX_SIZE 14
struct channel_list {
	u8 size;
	u8 body[CHANNEL_LIST_MAX_SIZE];
	u8 pad;
} __packed;

/* MIB Attribute */
#define DOT11_MAC_ADDRESS                 0x21010100	/* MAC Address (R) */
#define DOT11_PRODUCT_VERSION             0x31024100	/* FirmWare Version (R) */
#define DOT11_RTS_THRESHOLD               0x21020100	/* RTS Threshold (R/W) */
#define DOT11_FRAGMENTATION_THRESHOLD     0x21050100	/* Fragment Threshold (R/W) */
#define DOT11_PRIVACY_INVOKED             0x15010100	/* WEP ON/OFF (W) */
#define DOT11_WEP_DEFAULT_KEY_ID          0x15020100	/* WEP Index (W) */
#define DOT11_WEP_DEFAULT_KEY_VALUE1      0x13020101	/* WEP Key#1(TKIP AES: PairwiseTemporalKey) (W) */
#define DOT11_WEP_DEFAULT_KEY_VALUE2      0x13020102	/* WEP Key#2(TKIP AES: GroupKey1) (W) */
#define DOT11_WEP_DEFAULT_KEY_VALUE3      0x13020103	/* WEP Key#3(TKIP AES: GroupKey2) (W) */
#define DOT11_WEP_DEFAULT_KEY_VALUE4      0x13020104	/* WEP Key#4 (W) */
#define DOT11_WEP_LIST                    0x13020100	/* WEP LIST */
#define	DOT11_DESIRED_SSID		  0x11090100	/* SSID */
#define	DOT11_CURRENT_CHANNEL		  0x45010100	/* channel set */
#define	DOT11_OPERATION_RATE_SET	  0x11110100	/* rate set */

#define LOCAL_AP_SEARCH_INTEAVAL          0xF1010100	/* AP search interval (R/W) */
#define LOCAL_CURRENTADDRESS              0xF1050100	/* MAC Address change (W) */
#define LOCAL_MULTICAST_ADDRESS           0xF1060100	/* Multicast Address (W) */
#define LOCAL_MULTICAST_FILTER            0xF1060200	/* Multicast Address Filter enable/disable (W) */
#define LOCAL_SEARCHED_AP_LIST            0xF1030100	/* AP list (R) */
#define LOCAL_LINK_AP_STATUS              0xF1040100	/* Link AP status (R) */
#define	LOCAL_PACKET_STATISTICS		  0xF1020100	/* tx,rx packets statistics */
#define LOCAL_AP_SCAN_LIST_TYPE_SET	  0xF1030200	/* AP_SCAN_LIST_TYPE */

#define DOT11_RSN_ENABLED                 0x15070100	/* WPA enable/disable (W) */
#define LOCAL_RSN_MODE                    0x56010100	/* RSN mode WPA/WPA2 (W) */
#define DOT11_RSN_CONFIG_MULTICAST_CIPHER 0x51040100	/* GroupKeyCipherSuite (W) */
#define DOT11_RSN_CONFIG_UNICAST_CIPHER   0x52020100	/* PairwiseKeyCipherSuite (W) */
#define DOT11_RSN_CONFIG_AUTH_SUITE       0x53020100	/* AuthenticationKeyManagementSuite (W) */
#define DOT11_RSN_CONFIG_VERSION          0x51020100	/* RSN version (W) */
#define LOCAL_RSN_CONFIG_ALL              0x5F010100	/* RSN CONFIG ALL (W) */
#define DOT11_PMK_TSC                     0x55010100	/* PMK_TSC (W) */
#define DOT11_GMK1_TSC                    0x55010101	/* GMK1_TSC (W) */
#define DOT11_GMK2_TSC                    0x55010102	/* GMK2_TSC (W) */
#define DOT11_GMK3_TSC                    0x55010103	/* GMK3_TSC */
#define LOCAL_PMK                         0x58010100	/* Pairwise Master Key cache (W) */

#define LOCAL_REGION                      0xF10A0100	/* Region setting */

#define LOCAL_WPS_ENABLE                  0xF10B0100	/* WiFi Protected Setup */
#define LOCAL_WPS_PROBE_REQ               0xF10C0100	/* WPS Probe Request */

#define LOCAL_GAIN                        0xF10D0100	/* Carrer sense threshold for demo ato show */
#define LOCAL_EEPROM_SUM                  0xF10E0100	/* EEPROM checksum information */

struct hostif_mib_get_request {
	struct hostif_hdr header;
	__le32 mib_attribute;
} __packed;

struct hostif_mib_value {
	__le16 size;
	__le16 type;
#define MIB_VALUE_TYPE_NULL     0
#define MIB_VALUE_TYPE_INT      1
#define MIB_VALUE_TYPE_BOOL     2
#define MIB_VALUE_TYPE_COUNT32  3
#define MIB_VALUE_TYPE_OSTRING  4
	u8 body[0];
} __packed;

struct hostif_mib_get_confirm_t {
	struct hostif_hdr header;
	__le32 mib_status;
#define MIB_SUCCESS    0
#define MIB_INVALID    1
#define MIB_READ_ONLY  2
#define MIB_WRITE_ONLY 3
	__le32 mib_attribute;
	struct hostif_mib_value mib_value;
} __packed;

struct hostif_mib_set_request_t {
	struct hostif_hdr header;
	__le32 mib_attribute;
	struct hostif_mib_value mib_value;
} __packed;

struct hostif_power_mgmt_request {
	struct hostif_hdr header;
	__le32 mode;
#define POWER_ACTIVE  1
#define POWER_SAVE    2
	__le32 wake_up;
#define SLEEP_FALSE 0
#define SLEEP_TRUE  1	/* not used */
	__le32 receive_dtims;
#define DTIM_FALSE 0
#define DTIM_TRUE  1
} __packed;

enum power_mgmt_mode_type {
	POWER_MGMT_ACTIVE,
	POWER_MGMT_SAVE1,
	POWER_MGMT_SAVE2
};

#define	RESULT_SUCCESS            0
#define	RESULT_INVALID_PARAMETERS 1
#define	RESULT_NOT_SUPPORTED      2
/* #define	RESULT_ALREADY_RUNNING    3 */
#define	RESULT_ALREADY_RUNNING    7

struct hostif_start_request {
	struct hostif_hdr header;
	__le16 mode;
#define MODE_PSEUDO_ADHOC   0
#define MODE_INFRASTRUCTURE 1
#define MODE_AP             2	/* not used */
#define MODE_ADHOC          3
} __packed;

struct ssid {
	u8 size;
	u8 body[IEEE80211_MAX_SSID_LEN];
	u8 ssid_pad;
} __packed;

#define RATE_SET_MAX_SIZE 16
struct rate_set8 {
	u8 size;
	u8 body[8];
	u8 rate_pad;
} __packed;

struct fh_parms {
	__le16 dwell_time;
	u8 hop_set;
	u8 hop_pattern;
	u8 hop_index;
} __packed;

struct ds_parms {
	u8 channel;
} __packed;

struct cf_parms {
	u8 count;
	u8 period;
	__le16 max_duration;
	__le16 dur_remaining;
} __packed;

struct ibss_parms {
	__le16 atim_window;
} __packed;

struct rsn_t {
	u8 size;
#define RSN_BODY_SIZE 64
	u8 body[RSN_BODY_SIZE];
} __packed;

struct erp_params_t {
	u8 erp_info;
} __packed;

struct rate_set16 {
	u8 size;
	u8 body[16];
	u8 rate_pad;
} __packed;

struct ap_info {
	u8 bssid[6];	/* +00 */
	u8 rssi;	/* +06 */
	u8 sq;	/* +07 */
	u8 noise;	/* +08 */
	u8 pad0;	/* +09 */
	__le16 beacon_period;	/* +10 */
	__le16 capability;	/* +12 */
	u8 frame_type;	/* +14 */
	u8 ch_info;	/* +15 */
	__le16 body_size;	/* +16 */
	u8 body[1024];	/* +18 */
	/* +1032 */
} __packed;

struct link_ap_info {
	u8 bssid[6];	/* +00 */
	u8 rssi;	/* +06 */
	u8 sq;	/* +07 */
	u8 noise;	/* +08 */
	u8 pad0;	/* +09 */
	__le16 beacon_period;	/* +10 */
	__le16 capability;	/* +12 */
	struct rate_set8 rate_set;	/* +14 */
	struct fh_parms fh_parameter;	/* +24 */
	struct ds_parms ds_parameter;	/* +29 */
	struct cf_parms cf_parameter;	/* +30 */
	struct ibss_parms ibss_parameter;	/* +36 */
	struct erp_params_t erp_parameter;	/* +38 */
	u8 pad1;	/* +39 */
	struct rate_set8 ext_rate_set;	/* +40 */
	u8 DTIM_period;	/* +50 */
	u8 rsn_mode;	/* +51 */
#define RSN_MODE_NONE	0
#define RSN_MODE_WPA	1
#define RSN_MODE_WPA2	2
	struct {
		u8 size;	/* +52 */
		u8 body[128];	/* +53 */
	} __packed rsn;
} __packed;

#define RESULT_CONNECT    0
#define RESULT_DISCONNECT 1

struct hostif_stop_request {
	struct hostif_hdr header;
} __packed;

#define D_11B_ONLY_MODE		0
#define D_11G_ONLY_MODE		1
#define D_11BG_COMPATIBLE_MODE	2
#define D_11A_ONLY_MODE		3

#define CTS_MODE_FALSE	0
#define CTS_MODE_TRUE	1

struct hostif_request {
	__le16 phy_type;
	__le16 cts_mode;
	__le16 scan_type;
	__le16 capability;
	struct rate_set16 rate_set;
} __packed;

/**
 * struct hostif_ps_adhoc_set_request - pseudo adhoc mode
 * @capability: bit5  : preamble
 *              bit6  : pbcc - Not supported always 0
 *              bit10 : ShortSlotTime
 *              bit13 : DSSS-OFDM - Not supported always 0
 */
struct hostif_ps_adhoc_set_request {
	struct hostif_hdr header;
	struct hostif_request request;
	__le16 channel;
} __packed;

#define AUTH_TYPE_OPEN_SYSTEM 0
#define AUTH_TYPE_SHARED_KEY  1

/**
 * struct hostif_infrastructure_set_request
 * @capability: bit5  : preamble
 *              bit6  : pbcc - Not supported always 0
 *              bit10 : ShortSlotTime
 *              bit13 : DSSS-OFDM - Not supported always 0
 */
struct hostif_infrastructure_set_request {
	struct hostif_hdr header;
	struct hostif_request request;
	struct ssid ssid;
	__le16 beacon_lost_count;
	__le16 auth_type;
	struct channel_list channel_list;
	u8 bssid[ETH_ALEN];
} __packed;

/**
 * struct hostif_adhoc_set_request
 * @capability: bit5  : preamble
 *              bit6  : pbcc - Not supported always 0
 *              bit10 : ShortSlotTime
 *              bit13 : DSSS-OFDM - Not supported always 0
 */
struct hostif_adhoc_set_request {
	struct hostif_hdr header;
	struct hostif_request request;
	struct ssid ssid;
	__le16 channel;
} __packed;

/**
 * struct hostif_adhoc_set2_request
 * @capability: bit5  : preamble
 *              bit6  : pbcc - Not supported always 0
 *              bit10 : ShortSlotTime
 *              bit13 : DSSS-OFDM - Not supported always 0
 */
struct hostif_adhoc_set2_request {
	struct hostif_hdr header;
	struct hostif_request request;
	__le16 reserved;
	struct ssid ssid;
	struct channel_list channel_list;
	u8 bssid[ETH_ALEN];
} __packed;

struct association_request {
	u8 type;
	u8 pad;
	__le16 capability;
	__le16 listen_interval;
	u8 ap_address[6];
	__le16 req_ies_size;
} __packed;

struct association_response {
	u8 type;
	u8 pad;
	__le16 capability;
	__le16 status;
	__le16 association_id;
	__le16 resp_ies_size;
} __packed;

struct hostif_bss_scan_request {
	struct hostif_hdr header;
	u8 scan_type;
#define ACTIVE_SCAN  0
#define PASSIVE_SCAN 1
	u8 pad[3];
	__le32 ch_time_min;
	__le32 ch_time_max;
	struct channel_list channel_list;
	struct ssid ssid;
} __packed;

struct hostif_phy_information_request {
	struct hostif_hdr header;
	__le16 type;
#define NORMAL_TYPE	0
#define TIME_TYPE	1
	__le16 time;	/* unit 100ms */
} __packed;

enum sleep_mode_type {
	SLP_ACTIVE,
	SLP_SLEEP
};

struct hostif_sleep_request {
	struct hostif_hdr header;
} __packed;

struct hostif_mic_failure_request {
	struct hostif_hdr header;
	__le16 failure_count;
	__le16 timer;
} __packed;

#define BASIC_RATE	0x80
#define RATE_MASK	0x7F

#define TX_RATE_AUTO      0xff
#define TX_RATE_1M_FIXED  0
#define TX_RATE_2M_FIXED  1
#define TX_RATE_1_2M_AUTO 2
#define TX_RATE_5M_FIXED  3
#define TX_RATE_11M_FIXED 4

#define TX_RATE_FULL_AUTO	0
#define TX_RATE_11_AUTO		1
#define TX_RATE_11B_AUTO	2
#define TX_RATE_11BG_AUTO	3
#define TX_RATE_MANUAL_AUTO	4
#define TX_RATE_FIXED		5

/* 11b rate */
#define TX_RATE_1M	(uint8_t)(10 / 5)	/* 11b 11g basic rate */
#define TX_RATE_2M	(uint8_t)(20 / 5)	/* 11b 11g basic rate */
#define TX_RATE_5M	(uint8_t)(55 / 5)	/* 11g basic rate */
#define TX_RATE_11M	(uint8_t)(110 / 5)	/* 11g basic rate */

/* 11g rate */
#define TX_RATE_6M	(uint8_t)(60 / 5)	/* 11g basic rate */
#define TX_RATE_12M	(uint8_t)(120 / 5)	/* 11g basic rate */
#define TX_RATE_24M	(uint8_t)(240 / 5)	/* 11g basic rate */
#define TX_RATE_9M	(uint8_t)(90 / 5)
#define TX_RATE_18M	(uint8_t)(180 / 5)
#define TX_RATE_36M	(uint8_t)(360 / 5)
#define TX_RATE_48M	(uint8_t)(480 / 5)
#define TX_RATE_54M	(uint8_t)(540 / 5)

static inline bool is_11b_rate(u8 rate)
{
	return (((rate & RATE_MASK) == TX_RATE_1M) ||
		((rate & RATE_MASK) == TX_RATE_2M) ||
		((rate & RATE_MASK) == TX_RATE_5M) ||
		((rate & RATE_MASK) == TX_RATE_11M));
}

static inline bool is_ofdm_rate(u8 rate)
{
	return (((rate & RATE_MASK) == TX_RATE_6M)  ||
		((rate & RATE_MASK) == TX_RATE_12M) ||
		((rate & RATE_MASK) == TX_RATE_24M) ||
		((rate & RATE_MASK) == TX_RATE_9M)  ||
		((rate & RATE_MASK) == TX_RATE_18M) ||
		((rate & RATE_MASK) == TX_RATE_36M) ||
		((rate & RATE_MASK) == TX_RATE_48M) ||
		((rate & RATE_MASK) == TX_RATE_54M));
}

static inline bool is_11bg_rate(u8 rate)
{
	return (is_11b_rate(rate) || is_ofdm_rate(rate));
}

static inline bool is_ofdm_ext_rate(u8 rate)
{
	return (((rate & RATE_MASK) == TX_RATE_9M)  ||
		((rate & RATE_MASK) == TX_RATE_18M) ||
		((rate & RATE_MASK) == TX_RATE_36M) ||
		((rate & RATE_MASK) == TX_RATE_48M) ||
		((rate & RATE_MASK) == TX_RATE_54M));
}

enum connect_status_type {
	CONNECT_STATUS,
	DISCONNECT_STATUS
};

enum preamble_type {
	LONG_PREAMBLE,
	SHORT_PREAMBLE
};

enum multicast_filter_type {
	MCAST_FILTER_MCAST,
	MCAST_FILTER_MCASTALL,
	MCAST_FILTER_PROMISC,
};

#define NIC_MAX_MCAST_LIST 32

#define HIF_EVENT_MASK 0xE800

static inline bool is_hif_ind(unsigned short event)
{
	return (((event & HIF_EVENT_MASK) == HIF_EVENT_MASK) &&
		(((event & ~HIF_EVENT_MASK) == 0x0001) ||
		 ((event & ~HIF_EVENT_MASK) == 0x0006) ||
		 ((event & ~HIF_EVENT_MASK) == 0x000C) ||
		 ((event & ~HIF_EVENT_MASK) == 0x0011) ||
		 ((event & ~HIF_EVENT_MASK) == 0x0012)));
}

static inline bool is_hif_conf(unsigned short event)
{
	return (((event & HIF_EVENT_MASK) == HIF_EVENT_MASK) &&
		((event & ~HIF_EVENT_MASK) > 0x0000) &&
		((event & ~HIF_EVENT_MASK) < 0x0012) &&
		!is_hif_ind(event));
}

#ifdef __KERNEL__

#include "ks_wlan.h"

/* function prototype */
int hostif_data_request(struct ks_wlan_private *priv, struct sk_buff *skb);
void hostif_receive(struct ks_wlan_private *priv, unsigned char *p,
		    unsigned int size);
void hostif_sme_enqueue(struct ks_wlan_private *priv, uint16_t event);
int hostif_init(struct ks_wlan_private *priv);
void hostif_exit(struct ks_wlan_private *priv);
int ks_wlan_hw_tx(struct ks_wlan_private *priv, void *p, unsigned long size,
		  void (*complete_handler)(struct ks_wlan_private *priv,
					   struct sk_buff *skb),
		  struct sk_buff *skb);
void send_packet_complete(struct ks_wlan_private *priv, struct sk_buff *skb);

void ks_wlan_hw_wakeup_request(struct ks_wlan_private *priv);
int ks_wlan_hw_power_save(struct ks_wlan_private *priv);

#define KS7010_SIZE_ALIGNMENT	32

static inline size_t hif_align_size(size_t size)
{
	return ALIGN(size, KS7010_SIZE_ALIGNMENT);
}

#endif /* __KERNEL__ */

#endif /* _KS_HOSTIF_H_ */
