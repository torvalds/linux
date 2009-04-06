/*
 * Merged with mainline ieee80211.h in Aug 2004.  Original ieee802_11
 * remains copyright by the original authors
 *
 * Portions of the merged code are based on Host AP (software wireless
 * LAN access point) driver for Intersil Prism2/2.5/3.
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <j@w1.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <j@w1.fi>
 *
 * Adaption to a generic IEEE 802.11 stack by James Ketrenos
 * <jketreno@linux.intel.com>
 * Copyright (c) 2004-2005, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 *
 * API Version History
 * 1.0.x -- Initial version
 * 1.1.x -- Added radiotap, QoS, TIM, ieee80211_geo APIs,
 *          various structure changes, and crypto API init method
 */
#ifndef IEEE80211_H
#define IEEE80211_H
#include <linux/if_ether.h>	/* ETH_ALEN */
#include <linux/kernel.h>	/* ARRAY_SIZE */
#include <linux/wireless.h>
#include <linux/ieee80211.h>

#include <net/lib80211.h>

#define IEEE80211_VERSION "git-1.1.13"

#define IEEE80211_DATA_LEN		2304
/* Maximum size for the MA-UNITDATA primitive, 802.11 standard section
   6.2.1.1.2.

   The figure in section 7.1.2 suggests a body size of up to 2312
   bytes is allowed, which is a bit confusing, I suspect this
   represents the 2304 bytes of real data, plus a possible 8 bytes of
   WEP IV and ICV. (this interpretation suggested by Ramiro Barreiro) */

#define IEEE80211_1ADDR_LEN 10
#define IEEE80211_2ADDR_LEN 16
#define IEEE80211_3ADDR_LEN 24
#define IEEE80211_4ADDR_LEN 30
#define IEEE80211_FCS_LEN    4
#define IEEE80211_HLEN			(IEEE80211_4ADDR_LEN)
#define IEEE80211_FRAME_LEN		(IEEE80211_DATA_LEN + IEEE80211_HLEN)

#define MIN_FRAG_THRESHOLD     256U
#define	MAX_FRAG_THRESHOLD     2346U

/* QOS control */
#define IEEE80211_QCTL_TID		0x000F

/* debug macros */

#ifdef CONFIG_LIBIPW_DEBUG
extern u32 ieee80211_debug_level;
#define IEEE80211_DEBUG(level, fmt, args...) \
do { if (ieee80211_debug_level & (level)) \
  printk(KERN_DEBUG "ieee80211: %c %s " fmt, \
         in_interrupt() ? 'I' : 'U', __func__ , ## args); } while (0)
static inline bool ieee80211_ratelimit_debug(u32 level)
{
	return (ieee80211_debug_level & level) && net_ratelimit();
}
#else
#define IEEE80211_DEBUG(level, fmt, args...) do {} while (0)
static inline bool ieee80211_ratelimit_debug(u32 level)
{
	return false;
}
#endif				/* CONFIG_LIBIPW_DEBUG */

/*
 * To use the debug system:
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of:
 *
 * #define IEEE80211_DL_xxxx VALUE
 *
 * shifting value to the left one bit from the previous entry.  xxxx should be
 * the name of the classification (for example, WEP)
 *
 * You then need to either add a IEEE80211_xxxx_DEBUG() macro definition for your
 * classification, or use IEEE80211_DEBUG(IEEE80211_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * To add your debug level to the list of levels seen when you perform
 *
 * % cat /proc/net/ieee80211/debug_level
 *
 * you simply need to add your entry to the ieee80211_debug_level array.
 *
 * If you do not see debug_level in /proc/net/ieee80211 then you do not have
 * CONFIG_LIBIPW_DEBUG defined in your kernel configuration
 *
 */

#define IEEE80211_DL_INFO          (1<<0)
#define IEEE80211_DL_WX            (1<<1)
#define IEEE80211_DL_SCAN          (1<<2)
#define IEEE80211_DL_STATE         (1<<3)
#define IEEE80211_DL_MGMT          (1<<4)
#define IEEE80211_DL_FRAG          (1<<5)
#define IEEE80211_DL_DROP          (1<<7)

#define IEEE80211_DL_TX            (1<<8)
#define IEEE80211_DL_RX            (1<<9)
#define IEEE80211_DL_QOS           (1<<31)

#define IEEE80211_ERROR(f, a...) printk(KERN_ERR "ieee80211: " f, ## a)
#define IEEE80211_WARNING(f, a...) printk(KERN_WARNING "ieee80211: " f, ## a)
#define IEEE80211_DEBUG_INFO(f, a...)   IEEE80211_DEBUG(IEEE80211_DL_INFO, f, ## a)

#define IEEE80211_DEBUG_WX(f, a...)     IEEE80211_DEBUG(IEEE80211_DL_WX, f, ## a)
#define IEEE80211_DEBUG_SCAN(f, a...)   IEEE80211_DEBUG(IEEE80211_DL_SCAN, f, ## a)
#define IEEE80211_DEBUG_STATE(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_STATE, f, ## a)
#define IEEE80211_DEBUG_MGMT(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_MGMT, f, ## a)
#define IEEE80211_DEBUG_FRAG(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_FRAG, f, ## a)
#define IEEE80211_DEBUG_DROP(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_DROP, f, ## a)
#define IEEE80211_DEBUG_TX(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_TX, f, ## a)
#define IEEE80211_DEBUG_RX(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_RX, f, ## a)
#define IEEE80211_DEBUG_QOS(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_QOS, f, ## a)
#include <linux/netdevice.h>
#include <linux/if_arp.h>	/* ARPHRD_ETHER */

#ifndef WIRELESS_SPY
#define WIRELESS_SPY		/* enable iwspy support */
#endif
#include <net/iw_handler.h>	/* new driver API */

#define ETH_P_PREAUTH 0x88C7	/* IEEE 802.11i pre-authentication */

#ifndef ETH_P_80211_RAW
#define ETH_P_80211_RAW (ETH_P_ECONET + 1)
#endif

/* IEEE 802.11 defines */

#define P80211_OUI_LEN 3

struct ieee80211_snap_hdr {

	u8 dsap;		/* always 0xAA */
	u8 ssap;		/* always 0xAA */
	u8 ctrl;		/* always 0x03 */
	u8 oui[P80211_OUI_LEN];	/* organizational universal id */

} __attribute__ ((packed));

#define SNAP_SIZE sizeof(struct ieee80211_snap_hdr)

#define WLAN_FC_GET_VERS(fc) ((fc) & IEEE80211_FCTL_VERS)
#define WLAN_FC_GET_TYPE(fc) ((fc) & IEEE80211_FCTL_FTYPE)
#define WLAN_FC_GET_STYPE(fc) ((fc) & IEEE80211_FCTL_STYPE)

#define WLAN_GET_SEQ_FRAG(seq) ((seq) & IEEE80211_SCTL_FRAG)
#define WLAN_GET_SEQ_SEQ(seq)  (((seq) & IEEE80211_SCTL_SEQ) >> 4)

#define IEEE80211_STATMASK_SIGNAL (1<<0)
#define IEEE80211_STATMASK_RSSI (1<<1)
#define IEEE80211_STATMASK_NOISE (1<<2)
#define IEEE80211_STATMASK_RATE (1<<3)
#define IEEE80211_STATMASK_WEMASK 0x7

#define IEEE80211_CCK_MODULATION    (1<<0)
#define IEEE80211_OFDM_MODULATION   (1<<1)

#define IEEE80211_24GHZ_BAND     (1<<0)
#define IEEE80211_52GHZ_BAND     (1<<1)

#define IEEE80211_CCK_RATE_1MB		        0x02
#define IEEE80211_CCK_RATE_2MB		        0x04
#define IEEE80211_CCK_RATE_5MB		        0x0B
#define IEEE80211_CCK_RATE_11MB		        0x16
#define IEEE80211_OFDM_RATE_6MB		        0x0C
#define IEEE80211_OFDM_RATE_9MB		        0x12
#define IEEE80211_OFDM_RATE_12MB		0x18
#define IEEE80211_OFDM_RATE_18MB		0x24
#define IEEE80211_OFDM_RATE_24MB		0x30
#define IEEE80211_OFDM_RATE_36MB		0x48
#define IEEE80211_OFDM_RATE_48MB		0x60
#define IEEE80211_OFDM_RATE_54MB		0x6C
#define IEEE80211_BASIC_RATE_MASK		0x80

#define IEEE80211_CCK_RATE_1MB_MASK		(1<<0)
#define IEEE80211_CCK_RATE_2MB_MASK		(1<<1)
#define IEEE80211_CCK_RATE_5MB_MASK		(1<<2)
#define IEEE80211_CCK_RATE_11MB_MASK		(1<<3)
#define IEEE80211_OFDM_RATE_6MB_MASK		(1<<4)
#define IEEE80211_OFDM_RATE_9MB_MASK		(1<<5)
#define IEEE80211_OFDM_RATE_12MB_MASK		(1<<6)
#define IEEE80211_OFDM_RATE_18MB_MASK		(1<<7)
#define IEEE80211_OFDM_RATE_24MB_MASK		(1<<8)
#define IEEE80211_OFDM_RATE_36MB_MASK		(1<<9)
#define IEEE80211_OFDM_RATE_48MB_MASK		(1<<10)
#define IEEE80211_OFDM_RATE_54MB_MASK		(1<<11)

#define IEEE80211_CCK_RATES_MASK	        0x0000000F
#define IEEE80211_CCK_BASIC_RATES_MASK	(IEEE80211_CCK_RATE_1MB_MASK | \
	IEEE80211_CCK_RATE_2MB_MASK)
#define IEEE80211_CCK_DEFAULT_RATES_MASK	(IEEE80211_CCK_BASIC_RATES_MASK | \
        IEEE80211_CCK_RATE_5MB_MASK | \
        IEEE80211_CCK_RATE_11MB_MASK)

#define IEEE80211_OFDM_RATES_MASK		0x00000FF0
#define IEEE80211_OFDM_BASIC_RATES_MASK	(IEEE80211_OFDM_RATE_6MB_MASK | \
	IEEE80211_OFDM_RATE_12MB_MASK | \
	IEEE80211_OFDM_RATE_24MB_MASK)
#define IEEE80211_OFDM_DEFAULT_RATES_MASK	(IEEE80211_OFDM_BASIC_RATES_MASK | \
	IEEE80211_OFDM_RATE_9MB_MASK  | \
	IEEE80211_OFDM_RATE_18MB_MASK | \
	IEEE80211_OFDM_RATE_36MB_MASK | \
	IEEE80211_OFDM_RATE_48MB_MASK | \
	IEEE80211_OFDM_RATE_54MB_MASK)
#define IEEE80211_DEFAULT_RATES_MASK (IEEE80211_OFDM_DEFAULT_RATES_MASK | \
                                IEEE80211_CCK_DEFAULT_RATES_MASK)

#define IEEE80211_NUM_OFDM_RATES	    8
#define IEEE80211_NUM_CCK_RATES	            4
#define IEEE80211_OFDM_SHIFT_MASK_A         4

/* NOTE: This data is for statistical purposes; not all hardware provides this
 *       information for frames received.
 *       For ieee80211_rx_mgt, you need to set at least the 'len' parameter.
 */
struct ieee80211_rx_stats {
	u32 mac_time;
	s8 rssi;
	u8 signal;
	u8 noise;
	u16 rate;		/* in 100 kbps */
	u8 received_channel;
	u8 control;
	u8 mask;
	u8 freq;
	u16 len;
	u64 tsf;
	u32 beacon_time;
};

/* IEEE 802.11 requires that STA supports concurrent reception of at least
 * three fragmented frames. This define can be increased to support more
 * concurrent frames, but it should be noted that each entry can consume about
 * 2 kB of RAM and increasing cache size will slow down frame reassembly. */
#define IEEE80211_FRAG_CACHE_LEN 4

struct ieee80211_frag_entry {
	unsigned long first_frag_time;
	unsigned int seq;
	unsigned int last_frag;
	struct sk_buff *skb;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
};

struct ieee80211_stats {
	unsigned int tx_unicast_frames;
	unsigned int tx_multicast_frames;
	unsigned int tx_fragments;
	unsigned int tx_unicast_octets;
	unsigned int tx_multicast_octets;
	unsigned int tx_deferred_transmissions;
	unsigned int tx_single_retry_frames;
	unsigned int tx_multiple_retry_frames;
	unsigned int tx_retry_limit_exceeded;
	unsigned int tx_discards;
	unsigned int rx_unicast_frames;
	unsigned int rx_multicast_frames;
	unsigned int rx_fragments;
	unsigned int rx_unicast_octets;
	unsigned int rx_multicast_octets;
	unsigned int rx_fcs_errors;
	unsigned int rx_discards_no_buffer;
	unsigned int tx_discards_wrong_sa;
	unsigned int rx_discards_undecryptable;
	unsigned int rx_message_in_msg_fragments;
	unsigned int rx_message_in_bad_msg_fragments;
};

struct ieee80211_device;

#define SEC_KEY_1		(1<<0)
#define SEC_KEY_2		(1<<1)
#define SEC_KEY_3		(1<<2)
#define SEC_KEY_4		(1<<3)
#define SEC_ACTIVE_KEY		(1<<4)
#define SEC_AUTH_MODE		(1<<5)
#define SEC_UNICAST_GROUP	(1<<6)
#define SEC_LEVEL		(1<<7)
#define SEC_ENABLED		(1<<8)
#define SEC_ENCRYPT		(1<<9)

#define SEC_LEVEL_0		0	/* None */
#define SEC_LEVEL_1		1	/* WEP 40 and 104 bit */
#define SEC_LEVEL_2		2	/* Level 1 + TKIP */
#define SEC_LEVEL_2_CKIP	3	/* Level 1 + CKIP */
#define SEC_LEVEL_3		4	/* Level 2 + CCMP */

#define SEC_ALG_NONE		0
#define SEC_ALG_WEP		1
#define SEC_ALG_TKIP		2
#define SEC_ALG_CCMP		3

#define WEP_KEYS		4
#define WEP_KEY_LEN		13
#define SCM_KEY_LEN		32
#define SCM_TEMPORAL_KEY_LENGTH	16

struct ieee80211_security {
	u16 active_key:2, enabled:1, unicast_uses_group:1, encrypt:1;
	u8 auth_mode;
	u8 encode_alg[WEP_KEYS];
	u8 key_sizes[WEP_KEYS];
	u8 keys[WEP_KEYS][SCM_KEY_LEN];
	u8 level;
	u16 flags;
} __attribute__ ((packed));

/*

 802.11 data frame from AP

      ,-------------------------------------------------------------------.
Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
      |------|------|---------|---------|---------|------|---------|------|
Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  frame  |  fcs |
      |      | tion | (BSSID) |         |         | ence |  data   |      |
      `-------------------------------------------------------------------'

Total: 28-2340 bytes

*/

#define BEACON_PROBE_SSID_ID_POSITION 12

struct ieee80211_hdr_1addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 payload[0];
} __attribute__ ((packed));

struct ieee80211_hdr_2addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 payload[0];
} __attribute__ ((packed));

struct ieee80211_hdr_3addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 payload[0];
} __attribute__ ((packed));

struct ieee80211_hdr_4addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];
	u8 payload[0];
} __attribute__ ((packed));

struct ieee80211_hdr_3addrqos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 payload[0];
	__le16 qos_ctl;
} __attribute__ ((packed));

struct ieee80211_info_element {
	u8 id;
	u8 len;
	u8 data[0];
} __attribute__ ((packed));

/*
 * These are the data types that can make up management packets
 *
	u16 auth_algorithm;
	u16 auth_sequence;
	u16 beacon_interval;
	u16 capability;
	u8 current_ap[ETH_ALEN];
	u16 listen_interval;
	struct {
		u16 association_id:14, reserved:2;
	} __attribute__ ((packed));
	u32 time_stamp[2];
	u16 reason;
	u16 status;
*/

struct ieee80211_auth {
	struct ieee80211_hdr_3addr header;
	__le16 algorithm;
	__le16 transaction;
	__le16 status;
	/* challenge */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_channel_switch {
	u8 id;
	u8 len;
	u8 mode;
	u8 channel;
	u8 count;
} __attribute__ ((packed));

struct ieee80211_action {
	struct ieee80211_hdr_3addr header;
	u8 category;
	u8 action;
	union {
		struct ieee80211_action_exchange {
			u8 token;
			struct ieee80211_info_element info_element[0];
		} exchange;
		struct ieee80211_channel_switch channel_switch;

	} format;
} __attribute__ ((packed));

struct ieee80211_disassoc {
	struct ieee80211_hdr_3addr header;
	__le16 reason;
} __attribute__ ((packed));

/* Alias deauth for disassoc */
#define ieee80211_deauth ieee80211_disassoc

struct ieee80211_probe_request {
	struct ieee80211_hdr_3addr header;
	/* SSID, supported rates */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_probe_response {
	struct ieee80211_hdr_3addr header;
	__le32 time_stamp[2];
	__le16 beacon_interval;
	__le16 capability;
	/* SSID, supported rates, FH params, DS params,
	 * CF params, IBSS params, TIM (if beacon), RSN */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

/* Alias beacon for probe_response */
#define ieee80211_beacon ieee80211_probe_response

struct ieee80211_assoc_request {
	struct ieee80211_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	/* SSID, supported rates, RSN */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_reassoc_request {
	struct ieee80211_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	u8 current_ap[ETH_ALEN];
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_assoc_response {
	struct ieee80211_hdr_3addr header;
	__le16 capability;
	__le16 status;
	__le16 aid;
	/* supported rates */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_txb {
	u8 nr_frags;
	u8 encrypted;
	u8 rts_included;
	u8 reserved;
	u16 frag_size;
	u16 payload_size;
	struct sk_buff *fragments[0];
};

/* SWEEP TABLE ENTRIES NUMBER */
#define MAX_SWEEP_TAB_ENTRIES		  42
#define MAX_SWEEP_TAB_ENTRIES_PER_PACKET  7
/* MAX_RATES_LENGTH needs to be 12.  The spec says 8, and many APs
 * only use 8, and then use extended rates for the remaining supported
 * rates.  Other APs, however, stick all of their supported rates on the
 * main rates information element... */
#define MAX_RATES_LENGTH                  ((u8)12)
#define MAX_RATES_EX_LENGTH               ((u8)16)
#define MAX_NETWORK_COUNT                  128

#define CRC_LENGTH                 4U

#define MAX_WPA_IE_LEN 64

#define NETWORK_HAS_OFDM       (1<<1)
#define NETWORK_HAS_CCK        (1<<2)

/* QoS structure */
#define NETWORK_HAS_QOS_PARAMETERS      (1<<3)
#define NETWORK_HAS_QOS_INFORMATION     (1<<4)
#define NETWORK_HAS_QOS_MASK            (NETWORK_HAS_QOS_PARAMETERS | \
					 NETWORK_HAS_QOS_INFORMATION)

/* 802.11h */
#define NETWORK_HAS_POWER_CONSTRAINT    (1<<5)
#define NETWORK_HAS_CSA                 (1<<6)
#define NETWORK_HAS_QUIET               (1<<7)
#define NETWORK_HAS_IBSS_DFS            (1<<8)
#define NETWORK_HAS_TPC_REPORT          (1<<9)

#define NETWORK_HAS_ERP_VALUE           (1<<10)

#define QOS_QUEUE_NUM                   4
#define QOS_OUI_LEN                     3
#define QOS_OUI_TYPE                    2
#define QOS_ELEMENT_ID                  221
#define QOS_OUI_INFO_SUB_TYPE           0
#define QOS_OUI_PARAM_SUB_TYPE          1
#define QOS_VERSION_1                   1
#define QOS_AIFSN_MIN_VALUE             2

struct ieee80211_qos_information_element {
	u8 elementID;
	u8 length;
	u8 qui[QOS_OUI_LEN];
	u8 qui_type;
	u8 qui_subtype;
	u8 version;
	u8 ac_info;
} __attribute__ ((packed));

struct ieee80211_qos_ac_parameter {
	u8 aci_aifsn;
	u8 ecw_min_max;
	__le16 tx_op_limit;
} __attribute__ ((packed));

struct ieee80211_qos_parameter_info {
	struct ieee80211_qos_information_element info_element;
	u8 reserved;
	struct ieee80211_qos_ac_parameter ac_params_record[QOS_QUEUE_NUM];
} __attribute__ ((packed));

struct ieee80211_qos_parameters {
	__le16 cw_min[QOS_QUEUE_NUM];
	__le16 cw_max[QOS_QUEUE_NUM];
	u8 aifs[QOS_QUEUE_NUM];
	u8 flag[QOS_QUEUE_NUM];
	__le16 tx_op_limit[QOS_QUEUE_NUM];
} __attribute__ ((packed));

struct ieee80211_qos_data {
	struct ieee80211_qos_parameters parameters;
	int active;
	int supported;
	u8 param_count;
	u8 old_param_count;
};

struct ieee80211_tim_parameters {
	u8 tim_count;
	u8 tim_period;
} __attribute__ ((packed));

/*******************************************************/

enum {				/* ieee80211_basic_report.map */
	IEEE80211_BASIC_MAP_BSS = (1 << 0),
	IEEE80211_BASIC_MAP_OFDM = (1 << 1),
	IEEE80211_BASIC_MAP_UNIDENTIFIED = (1 << 2),
	IEEE80211_BASIC_MAP_RADAR = (1 << 3),
	IEEE80211_BASIC_MAP_UNMEASURED = (1 << 4),
	/* Bits 5-7 are reserved */

};
struct ieee80211_basic_report {
	u8 channel;
	__le64 start_time;
	__le16 duration;
	u8 map;
} __attribute__ ((packed));

enum {				/* ieee80211_measurement_request.mode */
	/* Bit 0 is reserved */
	IEEE80211_MEASUREMENT_ENABLE = (1 << 1),
	IEEE80211_MEASUREMENT_REQUEST = (1 << 2),
	IEEE80211_MEASUREMENT_REPORT = (1 << 3),
	/* Bits 4-7 are reserved */
};

enum {
	IEEE80211_REPORT_BASIC = 0,	/* required */
	IEEE80211_REPORT_CCA = 1,	/* optional */
	IEEE80211_REPORT_RPI = 2,	/* optional */
	/* 3-255 reserved */
};

struct ieee80211_measurement_params {
	u8 channel;
	__le64 start_time;
	__le16 duration;
} __attribute__ ((packed));

struct ieee80211_measurement_request {
	struct ieee80211_info_element ie;
	u8 token;
	u8 mode;
	u8 type;
	struct ieee80211_measurement_params params[0];
} __attribute__ ((packed));

struct ieee80211_measurement_report {
	struct ieee80211_info_element ie;
	u8 token;
	u8 mode;
	u8 type;
	union {
		struct ieee80211_basic_report basic[0];
	} u;
} __attribute__ ((packed));

struct ieee80211_tpc_report {
	u8 transmit_power;
	u8 link_margin;
} __attribute__ ((packed));

struct ieee80211_channel_map {
	u8 channel;
	u8 map;
} __attribute__ ((packed));

struct ieee80211_ibss_dfs {
	struct ieee80211_info_element ie;
	u8 owner[ETH_ALEN];
	u8 recovery_interval;
	struct ieee80211_channel_map channel_map[0];
};

struct ieee80211_csa {
	u8 mode;
	u8 channel;
	u8 count;
} __attribute__ ((packed));

struct ieee80211_quiet {
	u8 count;
	u8 period;
	u8 duration;
	u8 offset;
} __attribute__ ((packed));

struct ieee80211_network {
	/* These entries are used to identify a unique network */
	u8 bssid[ETH_ALEN];
	u8 channel;
	/* Ensure null-terminated for any debug msgs */
	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;

	struct ieee80211_qos_data qos_data;

	/* These are network statistics */
	struct ieee80211_rx_stats stats;
	u16 capability;
	u8 rates[MAX_RATES_LENGTH];
	u8 rates_len;
	u8 rates_ex[MAX_RATES_EX_LENGTH];
	u8 rates_ex_len;
	unsigned long last_scanned;
	u8 mode;
	u32 flags;
	u32 last_associate;
	u32 time_stamp[2];
	u16 beacon_interval;
	u16 listen_interval;
	u16 atim_window;
	u8 erp_value;
	u8 wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8 rsn_ie[MAX_WPA_IE_LEN];
	size_t rsn_ie_len;
	struct ieee80211_tim_parameters tim;

	/* 802.11h info */

	/* Power Constraint - mandatory if spctrm mgmt required */
	u8 power_constraint;

	/* TPC Report - mandatory if spctrm mgmt required */
	struct ieee80211_tpc_report tpc_report;

	/* IBSS DFS - mandatory if spctrm mgmt required and IBSS
	 * NOTE: This is variable length and so must be allocated dynamically */
	struct ieee80211_ibss_dfs *ibss_dfs;

	/* Channel Switch Announcement - optional if spctrm mgmt required */
	struct ieee80211_csa csa;

	/* Quiet - optional if spctrm mgmt required */
	struct ieee80211_quiet quiet;

	struct list_head list;
};

enum ieee80211_state {
	IEEE80211_UNINITIALIZED = 0,
	IEEE80211_INITIALIZED,
	IEEE80211_ASSOCIATING,
	IEEE80211_ASSOCIATED,
	IEEE80211_AUTHENTICATING,
	IEEE80211_AUTHENTICATED,
	IEEE80211_SHUTDOWN
};

#define DEFAULT_MAX_SCAN_AGE (15 * HZ)
#define DEFAULT_FTS 2346

#define CFG_IEEE80211_RESERVE_FCS (1<<0)
#define CFG_IEEE80211_COMPUTE_FCS (1<<1)
#define CFG_IEEE80211_RTS (1<<2)

#define IEEE80211_24GHZ_MIN_CHANNEL 1
#define IEEE80211_24GHZ_MAX_CHANNEL 14
#define IEEE80211_24GHZ_CHANNELS (IEEE80211_24GHZ_MAX_CHANNEL - \
				  IEEE80211_24GHZ_MIN_CHANNEL + 1)

#define IEEE80211_52GHZ_MIN_CHANNEL 34
#define IEEE80211_52GHZ_MAX_CHANNEL 165
#define IEEE80211_52GHZ_CHANNELS (IEEE80211_52GHZ_MAX_CHANNEL - \
				  IEEE80211_52GHZ_MIN_CHANNEL + 1)

enum {
	IEEE80211_CH_PASSIVE_ONLY = (1 << 0),
	IEEE80211_CH_80211H_RULES = (1 << 1),
	IEEE80211_CH_B_ONLY = (1 << 2),
	IEEE80211_CH_NO_IBSS = (1 << 3),
	IEEE80211_CH_UNIFORM_SPREADING = (1 << 4),
	IEEE80211_CH_RADAR_DETECT = (1 << 5),
	IEEE80211_CH_INVALID = (1 << 6),
};

struct ieee80211_channel {
	u32 freq;	/* in MHz */
	u8 channel;
	u8 flags;
	u8 max_power;	/* in dBm */
};

struct ieee80211_geo {
	u8 name[4];
	u8 bg_channels;
	u8 a_channels;
	struct ieee80211_channel bg[IEEE80211_24GHZ_CHANNELS];
	struct ieee80211_channel a[IEEE80211_52GHZ_CHANNELS];
};

struct ieee80211_device {
	struct net_device *dev;
	struct ieee80211_security sec;

	/* Bookkeeping structures */
	struct ieee80211_stats ieee_stats;

	struct ieee80211_geo geo;

	/* Probe / Beacon management */
	struct list_head network_free_list;
	struct list_head network_list;
	struct ieee80211_network *networks;
	int scans;
	int scan_age;

	int iw_mode;		/* operating mode (IW_MODE_*) */
	struct iw_spy_data spy_data;	/* iwspy support */

	spinlock_t lock;

	int tx_headroom;	/* Set to size of any additional room needed at front
				 * of allocated Tx SKBs */
	u32 config;

	/* WEP and other encryption related settings at the device level */
	int open_wep;		/* Set to 1 to allow unencrypted frames */

	int reset_on_keychange;	/* Set to 1 if the HW needs to be reset on
				 * WEP key changes */

	/* If the host performs {en,de}cryption, then set to 1 */
	int host_encrypt;
	int host_encrypt_msdu;
	int host_decrypt;
	/* host performs multicast decryption */
	int host_mc_decrypt;

	/* host should strip IV and ICV from protected frames */
	/* meaningful only when hardware decryption is being used */
	int host_strip_iv_icv;

	int host_open_frag;
	int host_build_iv;
	int ieee802_1x;		/* is IEEE 802.1X used */

	/* WPA data */
	int wpa_enabled;
	int drop_unencrypted;
	int privacy_invoked;
	size_t wpa_ie_len;
	u8 *wpa_ie;

	struct lib80211_crypt_info crypt_info;

	int bcrx_sta_key;	/* use individual keys to override default keys even
				 * with RX of broad/multicast frames */

	/* Fragmentation structures */
	struct ieee80211_frag_entry frag_cache[IEEE80211_FRAG_CACHE_LEN];
	unsigned int frag_next_idx;
	u16 fts;		/* Fragmentation Threshold */
	u16 rts;		/* RTS threshold */

	/* Association info */
	u8 bssid[ETH_ALEN];

	enum ieee80211_state state;

	int mode;		/* A, B, G */
	int modulation;		/* CCK, OFDM */
	int freq_band;		/* 2.4Ghz, 5.2Ghz, Mixed */
	int abg_true;		/* ABG flag              */

	int perfect_rssi;
	int worst_rssi;

	u16 prev_seq_ctl;	/* used to drop duplicate frames */

	/* Callback functions */
	void (*set_security) (struct net_device * dev,
			      struct ieee80211_security * sec);
	int (*hard_start_xmit) (struct ieee80211_txb * txb,
				struct net_device * dev, int pri);
	int (*reset_port) (struct net_device * dev);
	int (*is_queue_full) (struct net_device * dev, int pri);

	int (*handle_management) (struct net_device * dev,
				  struct ieee80211_network * network, u16 type);
	int (*is_qos_active) (struct net_device *dev, struct sk_buff *skb);

	/* Typical STA methods */
	int (*handle_auth) (struct net_device * dev,
			    struct ieee80211_auth * auth);
	int (*handle_deauth) (struct net_device * dev,
			      struct ieee80211_deauth * auth);
	int (*handle_action) (struct net_device * dev,
			      struct ieee80211_action * action,
			      struct ieee80211_rx_stats * stats);
	int (*handle_disassoc) (struct net_device * dev,
				struct ieee80211_disassoc * assoc);
	int (*handle_beacon) (struct net_device * dev,
			      struct ieee80211_beacon * beacon,
			      struct ieee80211_network * network);
	int (*handle_probe_response) (struct net_device * dev,
				      struct ieee80211_probe_response * resp,
				      struct ieee80211_network * network);
	int (*handle_probe_request) (struct net_device * dev,
				     struct ieee80211_probe_request * req,
				     struct ieee80211_rx_stats * stats);
	int (*handle_assoc_response) (struct net_device * dev,
				      struct ieee80211_assoc_response * resp,
				      struct ieee80211_network * network);

	/* Typical AP methods */
	int (*handle_assoc_request) (struct net_device * dev);
	int (*handle_reassoc_request) (struct net_device * dev,
				       struct ieee80211_reassoc_request * req);

	/* This must be the last item so that it points to the data
	 * allocated beyond this structure by alloc_ieee80211 */
	u8 priv[0];
};

#define IEEE_A            (1<<0)
#define IEEE_B            (1<<1)
#define IEEE_G            (1<<2)
#define IEEE_MODE_MASK    (IEEE_A|IEEE_B|IEEE_G)

static inline void *ieee80211_priv(struct net_device *dev)
{
	return ((struct ieee80211_device *)netdev_priv(dev))->priv;
}

static inline int ieee80211_is_valid_mode(struct ieee80211_device *ieee,
					  int mode)
{
	/*
	 * It is possible for both access points and our device to support
	 * combinations of modes, so as long as there is one valid combination
	 * of ap/device supported modes, then return success
	 *
	 */
	if ((mode & IEEE_A) &&
	    (ieee->modulation & IEEE80211_OFDM_MODULATION) &&
	    (ieee->freq_band & IEEE80211_52GHZ_BAND))
		return 1;

	if ((mode & IEEE_G) &&
	    (ieee->modulation & IEEE80211_OFDM_MODULATION) &&
	    (ieee->freq_band & IEEE80211_24GHZ_BAND))
		return 1;

	if ((mode & IEEE_B) &&
	    (ieee->modulation & IEEE80211_CCK_MODULATION) &&
	    (ieee->freq_band & IEEE80211_24GHZ_BAND))
		return 1;

	return 0;
}

static inline int ieee80211_get_hdrlen(u16 fc)
{
	int hdrlen = IEEE80211_3ADDR_LEN;
	u16 stype = WLAN_FC_GET_STYPE(fc);

	switch (WLAN_FC_GET_TYPE(fc)) {
	case IEEE80211_FTYPE_DATA:
		if ((fc & IEEE80211_FCTL_FROMDS) && (fc & IEEE80211_FCTL_TODS))
			hdrlen = IEEE80211_4ADDR_LEN;
		if (stype & IEEE80211_STYPE_QOS_DATA)
			hdrlen += 2;
		break;
	case IEEE80211_FTYPE_CTL:
		switch (WLAN_FC_GET_STYPE(fc)) {
		case IEEE80211_STYPE_CTS:
		case IEEE80211_STYPE_ACK:
			hdrlen = IEEE80211_1ADDR_LEN;
			break;
		default:
			hdrlen = IEEE80211_2ADDR_LEN;
			break;
		}
		break;
	}

	return hdrlen;
}

static inline u8 *ieee80211_get_payload(struct ieee80211_hdr *hdr)
{
	switch (ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_control))) {
	case IEEE80211_1ADDR_LEN:
		return ((struct ieee80211_hdr_1addr *)hdr)->payload;
	case IEEE80211_2ADDR_LEN:
		return ((struct ieee80211_hdr_2addr *)hdr)->payload;
	case IEEE80211_3ADDR_LEN:
		return ((struct ieee80211_hdr_3addr *)hdr)->payload;
	case IEEE80211_4ADDR_LEN:
		return ((struct ieee80211_hdr_4addr *)hdr)->payload;
	}
	return NULL;
}

static inline int ieee80211_is_ofdm_rate(u8 rate)
{
	switch (rate & ~IEEE80211_BASIC_RATE_MASK) {
	case IEEE80211_OFDM_RATE_6MB:
	case IEEE80211_OFDM_RATE_9MB:
	case IEEE80211_OFDM_RATE_12MB:
	case IEEE80211_OFDM_RATE_18MB:
	case IEEE80211_OFDM_RATE_24MB:
	case IEEE80211_OFDM_RATE_36MB:
	case IEEE80211_OFDM_RATE_48MB:
	case IEEE80211_OFDM_RATE_54MB:
		return 1;
	}
	return 0;
}

static inline int ieee80211_is_cck_rate(u8 rate)
{
	switch (rate & ~IEEE80211_BASIC_RATE_MASK) {
	case IEEE80211_CCK_RATE_1MB:
	case IEEE80211_CCK_RATE_2MB:
	case IEEE80211_CCK_RATE_5MB:
	case IEEE80211_CCK_RATE_11MB:
		return 1;
	}
	return 0;
}

/* ieee80211.c */
extern void free_ieee80211(struct net_device *dev);
extern struct net_device *alloc_ieee80211(int sizeof_priv);
extern int ieee80211_change_mtu(struct net_device *dev, int new_mtu);

extern void ieee80211_networks_age(struct ieee80211_device *ieee,
				   unsigned long age_secs);

extern int ieee80211_set_encryption(struct ieee80211_device *ieee);

/* ieee80211_tx.c */
extern int ieee80211_xmit(struct sk_buff *skb, struct net_device *dev);
extern void ieee80211_txb_free(struct ieee80211_txb *);

/* ieee80211_rx.c */
extern void ieee80211_rx_any(struct ieee80211_device *ieee,
		     struct sk_buff *skb, struct ieee80211_rx_stats *stats);
extern int ieee80211_rx(struct ieee80211_device *ieee, struct sk_buff *skb,
			struct ieee80211_rx_stats *rx_stats);
/* make sure to set stats->len */
extern void ieee80211_rx_mgt(struct ieee80211_device *ieee,
			     struct ieee80211_hdr_4addr *header,
			     struct ieee80211_rx_stats *stats);
extern void ieee80211_network_reset(struct ieee80211_network *network);

/* ieee80211_geo.c */
extern const struct ieee80211_geo *ieee80211_get_geo(struct ieee80211_device
						     *ieee);
extern int ieee80211_set_geo(struct ieee80211_device *ieee,
			     const struct ieee80211_geo *geo);

extern int ieee80211_is_valid_channel(struct ieee80211_device *ieee,
				      u8 channel);
extern int ieee80211_channel_to_index(struct ieee80211_device *ieee,
				      u8 channel);
extern u8 ieee80211_freq_to_channel(struct ieee80211_device *ieee, u32 freq);
extern u8 ieee80211_get_channel_flags(struct ieee80211_device *ieee,
				      u8 channel);
extern const struct ieee80211_channel *ieee80211_get_channel(struct
							     ieee80211_device
							     *ieee, u8 channel);
extern u32 ieee80211_channel_to_freq(struct ieee80211_device * ieee,
				      u8 channel);

/* ieee80211_wx.c */
extern int ieee80211_wx_get_scan(struct ieee80211_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *key);
extern int ieee80211_wx_set_encode(struct ieee80211_device *ieee,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *key);
extern int ieee80211_wx_get_encode(struct ieee80211_device *ieee,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *key);
extern int ieee80211_wx_set_encodeext(struct ieee80211_device *ieee,
				      struct iw_request_info *info,
				      union iwreq_data *wrqu, char *extra);
extern int ieee80211_wx_get_encodeext(struct ieee80211_device *ieee,
				      struct iw_request_info *info,
				      union iwreq_data *wrqu, char *extra);

static inline void ieee80211_increment_scans(struct ieee80211_device *ieee)
{
	ieee->scans++;
}

static inline int ieee80211_get_scans(struct ieee80211_device *ieee)
{
	return ieee->scans;
}

#endif				/* IEEE80211_H */
