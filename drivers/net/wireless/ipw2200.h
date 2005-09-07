/******************************************************************************

  Copyright(c) 2003 - 2004 Intel Corporation. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  James P. Ketrenos <ipw2100-admin@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

******************************************************************************/

#ifndef __ipw2200_h__
#define __ipw2200_h__

#define WEXT_USECHANNELS 1

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/config.h>
#include <linux/init.h>

#include <linux/version.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/dma-mapping.h>

#include <linux/firmware.h>
#include <linux/wireless.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>

#include <net/ieee80211.h>

#define DRV_NAME	"ipw2200"

#include <linux/workqueue.h>

/* Authentication  and Association States */
enum connection_manager_assoc_states {
	CMAS_INIT = 0,
	CMAS_TX_AUTH_SEQ_1,
	CMAS_RX_AUTH_SEQ_2,
	CMAS_AUTH_SEQ_1_PASS,
	CMAS_AUTH_SEQ_1_FAIL,
	CMAS_TX_AUTH_SEQ_3,
	CMAS_RX_AUTH_SEQ_4,
	CMAS_AUTH_SEQ_2_PASS,
	CMAS_AUTH_SEQ_2_FAIL,
	CMAS_AUTHENTICATED,
	CMAS_TX_ASSOC,
	CMAS_RX_ASSOC_RESP,
	CMAS_ASSOCIATED,
	CMAS_LAST
};

#define IPW_WAIT                     (1<<0)
#define IPW_QUIET                    (1<<1)
#define IPW_ROAMING                  (1<<2)

#define IPW_POWER_MODE_CAM           0x00	//(always on)
#define IPW_POWER_INDEX_1            0x01
#define IPW_POWER_INDEX_2            0x02
#define IPW_POWER_INDEX_3            0x03
#define IPW_POWER_INDEX_4            0x04
#define IPW_POWER_INDEX_5            0x05
#define IPW_POWER_AC                 0x06
#define IPW_POWER_BATTERY            0x07
#define IPW_POWER_LIMIT              0x07
#define IPW_POWER_MASK               0x0F
#define IPW_POWER_ENABLED            0x10
#define IPW_POWER_LEVEL(x)           ((x) & IPW_POWER_MASK)

#define IPW_CMD_HOST_COMPLETE                 2
#define IPW_CMD_POWER_DOWN                    4
#define IPW_CMD_SYSTEM_CONFIG                 6
#define IPW_CMD_MULTICAST_ADDRESS             7
#define IPW_CMD_SSID                          8
#define IPW_CMD_ADAPTER_ADDRESS              11
#define IPW_CMD_PORT_TYPE                    12
#define IPW_CMD_RTS_THRESHOLD                15
#define IPW_CMD_FRAG_THRESHOLD               16
#define IPW_CMD_POWER_MODE                   17
#define IPW_CMD_WEP_KEY                      18
#define IPW_CMD_TGI_TX_KEY                   19
#define IPW_CMD_SCAN_REQUEST                 20
#define IPW_CMD_ASSOCIATE                    21
#define IPW_CMD_SUPPORTED_RATES              22
#define IPW_CMD_SCAN_ABORT                   23
#define IPW_CMD_TX_FLUSH                     24
#define IPW_CMD_QOS_PARAMETERS               25
#define IPW_CMD_SCAN_REQUEST_EXT             26
#define IPW_CMD_DINO_CONFIG                  30
#define IPW_CMD_RSN_CAPABILITIES             31
#define IPW_CMD_RX_KEY                       32
#define IPW_CMD_CARD_DISABLE                 33
#define IPW_CMD_SEED_NUMBER                  34
#define IPW_CMD_TX_POWER                     35
#define IPW_CMD_COUNTRY_INFO                 36
#define IPW_CMD_AIRONET_INFO                 37
#define IPW_CMD_AP_TX_POWER                  38
#define IPW_CMD_CCKM_INFO                    39
#define IPW_CMD_CCX_VER_INFO                 40
#define IPW_CMD_SET_CALIBRATION              41
#define IPW_CMD_SENSITIVITY_CALIB            42
#define IPW_CMD_RETRY_LIMIT                  51
#define IPW_CMD_IPW_PRE_POWER_DOWN           58
#define IPW_CMD_VAP_BEACON_TEMPLATE          60
#define IPW_CMD_VAP_DTIM_PERIOD              61
#define IPW_CMD_EXT_SUPPORTED_RATES          62
#define IPW_CMD_VAP_LOCAL_TX_PWR_CONSTRAINT  63
#define IPW_CMD_VAP_QUIET_INTERVALS          64
#define IPW_CMD_VAP_CHANNEL_SWITCH           65
#define IPW_CMD_VAP_MANDATORY_CHANNELS       66
#define IPW_CMD_VAP_CELL_PWR_LIMIT           67
#define IPW_CMD_VAP_CF_PARAM_SET             68
#define IPW_CMD_VAP_SET_BEACONING_STATE      69
#define IPW_CMD_MEASUREMENT                  80
#define IPW_CMD_POWER_CAPABILITY             81
#define IPW_CMD_SUPPORTED_CHANNELS           82
#define IPW_CMD_TPC_REPORT                   83
#define IPW_CMD_WME_INFO                     84
#define IPW_CMD_PRODUCTION_COMMAND	     85
#define IPW_CMD_LINKSYS_EOU_INFO             90

#define RFD_SIZE                              4
#define NUM_TFD_CHUNKS                        6

#define TX_QUEUE_SIZE                        32
#define RX_QUEUE_SIZE                        32

#define DINO_CMD_WEP_KEY                   0x08
#define DINO_CMD_TX                        0x0B
#define DCT_ANTENNA_A                      0x01
#define DCT_ANTENNA_B                      0x02

#define IPW_A_MODE                         0
#define IPW_B_MODE                         1
#define IPW_G_MODE                         2

/*
 * TX Queue Flag Definitions
 */

/* abort attempt if mgmt frame is rx'd */
#define DCT_FLAG_ABORT_MGMT                0x01

/* require CTS */
#define DCT_FLAG_CTS_REQUIRED              0x02

/* use short preamble */
#define DCT_FLAG_SHORT_PREMBL              0x04

/* RTS/CTS first */
#define DCT_FLAG_RTS_REQD                  0x08

/* dont calculate duration field */
#define DCT_FLAG_DUR_SET                   0x10

/* even if MAC WEP set (allows pre-encrypt) */
#define DCT_FLAG_NO_WEP              0x20

/* overwrite TSF field */
#define DCT_FLAG_TSF_REQD                  0x40

/* ACK rx is expected to follow */
#define DCT_FLAG_ACK_REQD                  0x80

#define DCT_FLAG_EXT_MODE_CCK  0x01
#define DCT_FLAG_EXT_MODE_OFDM 0x00

#define TX_RX_TYPE_MASK                    0xFF
#define TX_FRAME_TYPE                      0x00
#define TX_HOST_COMMAND_TYPE               0x01
#define RX_FRAME_TYPE                      0x09
#define RX_HOST_NOTIFICATION_TYPE          0x03
#define RX_HOST_CMD_RESPONSE_TYPE          0x04
#define RX_TX_FRAME_RESPONSE_TYPE          0x05
#define TFD_NEED_IRQ_MASK                  0x04

#define HOST_CMD_DINO_CONFIG               30

#define HOST_NOTIFICATION_STATUS_ASSOCIATED             10
#define HOST_NOTIFICATION_STATUS_AUTHENTICATE           11
#define HOST_NOTIFICATION_STATUS_SCAN_CHANNEL_RESULT    12
#define HOST_NOTIFICATION_STATUS_SCAN_COMPLETED         13
#define HOST_NOTIFICATION_STATUS_FRAG_LENGTH            14
#define HOST_NOTIFICATION_STATUS_LINK_DETERIORATION     15
#define HOST_NOTIFICATION_DINO_CONFIG_RESPONSE          16
#define HOST_NOTIFICATION_STATUS_BEACON_STATE           17
#define HOST_NOTIFICATION_STATUS_TGI_TX_KEY             18
#define HOST_NOTIFICATION_TX_STATUS                     19
#define HOST_NOTIFICATION_CALIB_KEEP_RESULTS            20
#define HOST_NOTIFICATION_MEASUREMENT_STARTED           21
#define HOST_NOTIFICATION_MEASUREMENT_ENDED             22
#define HOST_NOTIFICATION_CHANNEL_SWITCHED              23
#define HOST_NOTIFICATION_RX_DURING_QUIET_PERIOD        24
#define HOST_NOTIFICATION_NOISE_STATS			25
#define HOST_NOTIFICATION_S36_MEASUREMENT_ACCEPTED      30
#define HOST_NOTIFICATION_S36_MEASUREMENT_REFUSED       31

#define HOST_NOTIFICATION_STATUS_BEACON_MISSING         1
#define IPW_MB_DISASSOCIATE_THRESHOLD_DEFAULT           24
#define IPW_MB_ROAMING_THRESHOLD_DEFAULT                8
#define IPW_REAL_RATE_RX_PACKET_THRESHOLD               300

#define MACADRR_BYTE_LEN                     6

#define DCR_TYPE_AP                       0x01
#define DCR_TYPE_WLAP                     0x02
#define DCR_TYPE_MU_ESS                   0x03
#define DCR_TYPE_MU_IBSS                  0x04
#define DCR_TYPE_MU_PIBSS                 0x05
#define DCR_TYPE_SNIFFER                  0x06
#define DCR_TYPE_MU_BSS        DCR_TYPE_MU_ESS

/**
 * Generic queue structure
 *
 * Contains common data for Rx and Tx queues
 */
struct clx2_queue {
	int n_bd;		       /**< number of BDs in this queue */
	int first_empty;	       /**< 1-st empty entry (index) */
	int last_used;		       /**< last used entry (index) */
	u32 reg_w;		     /**< 'write' reg (queue head), addr in domain 1 */
	u32 reg_r;		     /**< 'read' reg (queue tail), addr in domain 1 */
	dma_addr_t dma_addr;		/**< physical addr for BD's */
	int low_mark;		       /**< low watermark, resume queue if free space more than this */
	int high_mark;		       /**< high watermark, stop queue if free space less than this */
} __attribute__ ((packed));

struct machdr32 {
	u16 frame_ctl;
	u16 duration;		// watch out for endians!
	u8 addr1[MACADRR_BYTE_LEN];
	u8 addr2[MACADRR_BYTE_LEN];
	u8 addr3[MACADRR_BYTE_LEN];
	u16 seq_ctrl;		// more endians!
	u8 addr4[MACADRR_BYTE_LEN];
	u16 qos_ctrl;
} __attribute__ ((packed));

struct machdr30 {
	u16 frame_ctl;
	u16 duration;		// watch out for endians!
	u8 addr1[MACADRR_BYTE_LEN];
	u8 addr2[MACADRR_BYTE_LEN];
	u8 addr3[MACADRR_BYTE_LEN];
	u16 seq_ctrl;		// more endians!
	u8 addr4[MACADRR_BYTE_LEN];
} __attribute__ ((packed));

struct machdr26 {
	u16 frame_ctl;
	u16 duration;		// watch out for endians!
	u8 addr1[MACADRR_BYTE_LEN];
	u8 addr2[MACADRR_BYTE_LEN];
	u8 addr3[MACADRR_BYTE_LEN];
	u16 seq_ctrl;		// more endians!
	u16 qos_ctrl;
} __attribute__ ((packed));

struct machdr24 {
	u16 frame_ctl;
	u16 duration;		// watch out for endians!
	u8 addr1[MACADRR_BYTE_LEN];
	u8 addr2[MACADRR_BYTE_LEN];
	u8 addr3[MACADRR_BYTE_LEN];
	u16 seq_ctrl;		// more endians!
} __attribute__ ((packed));

// TX TFD with 32 byte MAC Header
struct tx_tfd_32 {
	struct machdr32 mchdr;	// 32
	u32 uivplaceholder[2];	// 8
} __attribute__ ((packed));

// TX TFD with 30 byte MAC Header
struct tx_tfd_30 {
	struct machdr30 mchdr;	// 30
	u8 reserved[2];		// 2
	u32 uivplaceholder[2];	// 8
} __attribute__ ((packed));

// tx tfd with 26 byte mac header
struct tx_tfd_26 {
	struct machdr26 mchdr;	// 26
	u8 reserved1[2];	// 2
	u32 uivplaceholder[2];	// 8
	u8 reserved2[4];	// 4
} __attribute__ ((packed));

// tx tfd with 24 byte mac header
struct tx_tfd_24 {
	struct machdr24 mchdr;	// 24
	u32 uivplaceholder[2];	// 8
	u8 reserved[8];		// 8
} __attribute__ ((packed));

#define DCT_WEP_KEY_FIELD_LENGTH 16

struct tfd_command {
	u8 index;
	u8 length;
	u16 reserved;
	u8 payload[0];
} __attribute__ ((packed));

struct tfd_data {
	/* Header */
	u32 work_area_ptr;
	u8 station_number;	/* 0 for BSS */
	u8 reserved1;
	u16 reserved2;

	/* Tx Parameters */
	u8 cmd_id;
	u8 seq_num;
	u16 len;
	u8 priority;
	u8 tx_flags;
	u8 tx_flags_ext;
	u8 key_index;
	u8 wepkey[DCT_WEP_KEY_FIELD_LENGTH];
	u8 rate;
	u8 antenna;
	u16 next_packet_duration;
	u16 next_frag_len;
	u16 back_off_counter;	//////txop;
	u8 retrylimit;
	u16 cwcurrent;
	u8 reserved3;

	/* 802.11 MAC Header */
	union {
		struct tx_tfd_24 tfd_24;
		struct tx_tfd_26 tfd_26;
		struct tx_tfd_30 tfd_30;
		struct tx_tfd_32 tfd_32;
	} tfd;

	/* Payload DMA info */
	u32 num_chunks;
	u32 chunk_ptr[NUM_TFD_CHUNKS];
	u16 chunk_len[NUM_TFD_CHUNKS];
} __attribute__ ((packed));

struct txrx_control_flags {
	u8 message_type;
	u8 rx_seq_num;
	u8 control_bits;
	u8 reserved;
} __attribute__ ((packed));

#define  TFD_SIZE                           128
#define  TFD_CMD_IMMEDIATE_PAYLOAD_LENGTH   (TFD_SIZE - sizeof(struct txrx_control_flags))

struct tfd_frame {
	struct txrx_control_flags control_flags;
	union {
		struct tfd_data data;
		struct tfd_command cmd;
		u8 raw[TFD_CMD_IMMEDIATE_PAYLOAD_LENGTH];
	} u;
} __attribute__ ((packed));

typedef void destructor_func(const void *);

/**
 * Tx Queue for DMA. Queue consists of circular buffer of
 * BD's and required locking structures.
 */
struct clx2_tx_queue {
	struct clx2_queue q;
	struct tfd_frame *bd;
	struct ieee80211_txb **txb;
};

/*
 * RX related structures and functions
 */
#define RX_FREE_BUFFERS 32
#define RX_LOW_WATERMARK 8

#define SUP_RATE_11A_MAX_NUM_CHANNELS  (8)
#define SUP_RATE_11B_MAX_NUM_CHANNELS  (4)
#define SUP_RATE_11G_MAX_NUM_CHANNELS  (12)

// Used for passing to driver number of successes and failures per rate
struct rate_histogram {
	union {
		u32 a[SUP_RATE_11A_MAX_NUM_CHANNELS];
		u32 b[SUP_RATE_11B_MAX_NUM_CHANNELS];
		u32 g[SUP_RATE_11G_MAX_NUM_CHANNELS];
	} success;
	union {
		u32 a[SUP_RATE_11A_MAX_NUM_CHANNELS];
		u32 b[SUP_RATE_11B_MAX_NUM_CHANNELS];
		u32 g[SUP_RATE_11G_MAX_NUM_CHANNELS];
	} failed;
} __attribute__ ((packed));

/* statistics command response */
struct ipw_cmd_stats {
	u8 cmd_id;
	u8 seq_num;
	u16 good_sfd;
	u16 bad_plcp;
	u16 wrong_bssid;
	u16 valid_mpdu;
	u16 bad_mac_header;
	u16 reserved_frame_types;
	u16 rx_ina;
	u16 bad_crc32;
	u16 invalid_cts;
	u16 invalid_acks;
	u16 long_distance_ina_fina;
	u16 dsp_silence_unreachable;
	u16 accumulated_rssi;
	u16 rx_ovfl_frame_tossed;
	u16 rssi_silence_threshold;
	u16 rx_ovfl_frame_supplied;
	u16 last_rx_frame_signal;
	u16 last_rx_frame_noise;
	u16 rx_autodetec_no_ofdm;
	u16 rx_autodetec_no_barker;
	u16 reserved;
} __attribute__ ((packed));

struct notif_channel_result {
	u8 channel_num;
	struct ipw_cmd_stats stats;
	u8 uReserved;
} __attribute__ ((packed));

struct notif_scan_complete {
	u8 scan_type;
	u8 num_channels;
	u8 status;
	u8 reserved;
} __attribute__ ((packed));

struct notif_frag_length {
	u16 frag_length;
	u16 reserved;
} __attribute__ ((packed));

struct notif_beacon_state {
	u32 state;
	u32 number;
} __attribute__ ((packed));

struct notif_tgi_tx_key {
	u8 key_state;
	u8 security_type;
	u8 station_index;
	u8 reserved;
} __attribute__ ((packed));

struct notif_link_deterioration {
	struct ipw_cmd_stats stats;
	u8 rate;
	u8 modulation;
	struct rate_histogram histogram;
	u8 reserved1;
	u16 reserved2;
} __attribute__ ((packed));

struct notif_association {
	u8 state;
} __attribute__ ((packed));

struct notif_authenticate {
	u8 state;
	struct machdr24 addr;
	u16 status;
} __attribute__ ((packed));

struct notif_calibration {
	u8 data[104];
} __attribute__ ((packed));

struct notif_noise {
	u32 value;
} __attribute__ ((packed));

struct ipw_rx_notification {
	u8 reserved[8];
	u8 subtype;
	u8 flags;
	u16 size;
	union {
		struct notif_association assoc;
		struct notif_authenticate auth;
		struct notif_channel_result channel_result;
		struct notif_scan_complete scan_complete;
		struct notif_frag_length frag_len;
		struct notif_beacon_state beacon_state;
		struct notif_tgi_tx_key tgi_tx_key;
		struct notif_link_deterioration link_deterioration;
		struct notif_calibration calibration;
		struct notif_noise noise;
		u8 raw[0];
	} u;
} __attribute__ ((packed));

struct ipw_rx_frame {
	u32 reserved1;
	u8 parent_tsf[4];	// fw_use[0] is boolean for OUR_TSF_IS_GREATER
	u8 received_channel;	// The channel that this frame was received on.
	// Note that for .11b this does not have to be
	// the same as the channel that it was sent.
	// Filled by LMAC
	u8 frameStatus;
	u8 rate;
	u8 rssi;
	u8 agc;
	u8 rssi_dbm;
	u16 signal;
	u16 noise;
	u8 antennaAndPhy;
	u8 control;		// control bit should be on in bg
	u8 rtscts_rate;		// rate of rts or cts (in rts cts sequence rate
	// is identical)
	u8 rtscts_seen;		// 0x1 RTS seen ; 0x2 CTS seen
	u16 length;
	u8 data[0];
} __attribute__ ((packed));

struct ipw_rx_header {
	u8 message_type;
	u8 rx_seq_num;
	u8 control_bits;
	u8 reserved;
} __attribute__ ((packed));

struct ipw_rx_packet {
	struct ipw_rx_header header;
	union {
		struct ipw_rx_frame frame;
		struct ipw_rx_notification notification;
	} u;
} __attribute__ ((packed));

#define IPW_RX_NOTIFICATION_SIZE sizeof(struct ipw_rx_header) + 12
#define IPW_RX_FRAME_SIZE        sizeof(struct ipw_rx_header) + \
                                 sizeof(struct ipw_rx_frame)

struct ipw_rx_mem_buffer {
	dma_addr_t dma_addr;
	struct ipw_rx_buffer *rxb;
	struct sk_buff *skb;
	struct list_head list;
};				/* Not transferred over network, so not  __attribute__ ((packed)) */

struct ipw_rx_queue {
	struct ipw_rx_mem_buffer pool[RX_QUEUE_SIZE + RX_FREE_BUFFERS];
	struct ipw_rx_mem_buffer *queue[RX_QUEUE_SIZE];
	u32 processed;		/* Internal index to last handled Rx packet */
	u32 read;		/* Shared index to newest available Rx buffer */
	u32 write;		/* Shared index to oldest written Rx packet */
	u32 free_count;		/* Number of pre-allocated buffers in rx_free */
	/* Each of these lists is used as a FIFO for ipw_rx_mem_buffers */
	struct list_head rx_free;	/* Own an SKBs */
	struct list_head rx_used;	/* No SKB allocated */
	spinlock_t lock;
};				/* Not transferred over network, so not  __attribute__ ((packed)) */

struct alive_command_responce {
	u8 alive_command;
	u8 sequence_number;
	u16 software_revision;
	u8 device_identifier;
	u8 reserved1[5];
	u16 reserved2;
	u16 reserved3;
	u16 clock_settle_time;
	u16 powerup_settle_time;
	u16 reserved4;
	u8 time_stamp[5];	/* month, day, year, hours, minutes */
	u8 ucode_valid;
} __attribute__ ((packed));

#define IPW_MAX_RATES 12

struct ipw_rates {
	u8 num_rates;
	u8 rates[IPW_MAX_RATES];
} __attribute__ ((packed));

struct command_block {
	unsigned int control;
	u32 source_addr;
	u32 dest_addr;
	unsigned int status;
} __attribute__ ((packed));

#define CB_NUMBER_OF_ELEMENTS_SMALL 64
struct fw_image_desc {
	unsigned long last_cb_index;
	unsigned long current_cb_index;
	struct command_block cb_list[CB_NUMBER_OF_ELEMENTS_SMALL];
	void *v_addr;
	unsigned long p_addr;
	unsigned long len;
};

struct ipw_sys_config {
	u8 bt_coexistence;
	u8 reserved1;
	u8 answer_broadcast_ssid_probe;
	u8 accept_all_data_frames;
	u8 accept_non_directed_frames;
	u8 exclude_unicast_unencrypted;
	u8 disable_unicast_decryption;
	u8 exclude_multicast_unencrypted;
	u8 disable_multicast_decryption;
	u8 antenna_diversity;
	u8 pass_crc_to_host;
	u8 dot11g_auto_detection;
	u8 enable_cts_to_self;
	u8 enable_multicast_filtering;
	u8 bt_coexist_collision_thr;
	u8 reserved2;
	u8 accept_all_mgmt_bcpr;
	u8 accept_all_mgtm_frames;
	u8 pass_noise_stats_to_host;
	u8 reserved3;
} __attribute__ ((packed));

struct ipw_multicast_addr {
	u8 num_of_multicast_addresses;
	u8 reserved[3];
	u8 mac1[6];
	u8 mac2[6];
	u8 mac3[6];
	u8 mac4[6];
} __attribute__ ((packed));

struct ipw_wep_key {
	u8 cmd_id;
	u8 seq_num;
	u8 key_index;
	u8 key_size;
	u8 key[16];
} __attribute__ ((packed));

struct ipw_tgi_tx_key {
	u8 key_id;
	u8 security_type;
	u8 station_index;
	u8 flags;
	u8 key[16];
	u32 tx_counter[2];
} __attribute__ ((packed));

#define IPW_SCAN_CHANNELS 54

struct ipw_scan_request {
	u8 scan_type;
	u16 dwell_time;
	u8 channels_list[IPW_SCAN_CHANNELS];
	u8 channels_reserved[3];
} __attribute__ ((packed));

enum {
	IPW_SCAN_PASSIVE_TILL_FIRST_BEACON_SCAN = 0,
	IPW_SCAN_PASSIVE_FULL_DWELL_SCAN,
	IPW_SCAN_ACTIVE_DIRECT_SCAN,
	IPW_SCAN_ACTIVE_BROADCAST_SCAN,
	IPW_SCAN_ACTIVE_BROADCAST_AND_DIRECT_SCAN,
	IPW_SCAN_TYPES
};

struct ipw_scan_request_ext {
	u32 full_scan_index;
	u8 channels_list[IPW_SCAN_CHANNELS];
	u8 scan_type[IPW_SCAN_CHANNELS / 2];
	u8 reserved;
	u16 dwell_time[IPW_SCAN_TYPES];
} __attribute__ ((packed));

extern inline u8 ipw_get_scan_type(struct ipw_scan_request_ext *scan, u8 index)
{
	if (index % 2)
		return scan->scan_type[index / 2] & 0x0F;
	else
		return (scan->scan_type[index / 2] & 0xF0) >> 4;
}

extern inline void ipw_set_scan_type(struct ipw_scan_request_ext *scan,
				     u8 index, u8 scan_type)
{
	if (index % 2)
		scan->scan_type[index / 2] =
		    (scan->scan_type[index / 2] & 0xF0) | (scan_type & 0x0F);
	else
		scan->scan_type[index / 2] =
		    (scan->scan_type[index / 2] & 0x0F) |
		    ((scan_type & 0x0F) << 4);
}

struct ipw_associate {
	u8 channel;
	u8 auth_type:4, auth_key:4;
	u8 assoc_type;
	u8 reserved;
	u16 policy_support;
	u8 preamble_length;
	u8 ieee_mode;
	u8 bssid[ETH_ALEN];
	u32 assoc_tsf_msw;
	u32 assoc_tsf_lsw;
	u16 capability;
	u16 listen_interval;
	u16 beacon_interval;
	u8 dest[ETH_ALEN];
	u16 atim_window;
	u8 smr;
	u8 reserved1;
	u16 reserved2;
} __attribute__ ((packed));

struct ipw_supported_rates {
	u8 ieee_mode;
	u8 num_rates;
	u8 purpose;
	u8 reserved;
	u8 supported_rates[IPW_MAX_RATES];
} __attribute__ ((packed));

struct ipw_rts_threshold {
	u16 rts_threshold;
	u16 reserved;
} __attribute__ ((packed));

struct ipw_frag_threshold {
	u16 frag_threshold;
	u16 reserved;
} __attribute__ ((packed));

struct ipw_retry_limit {
	u8 short_retry_limit;
	u8 long_retry_limit;
	u16 reserved;
} __attribute__ ((packed));

struct ipw_dino_config {
	u32 dino_config_addr;
	u16 dino_config_size;
	u8 dino_response;
	u8 reserved;
} __attribute__ ((packed));

struct ipw_aironet_info {
	u8 id;
	u8 length;
	u16 reserved;
} __attribute__ ((packed));

struct ipw_rx_key {
	u8 station_index;
	u8 key_type;
	u8 key_id;
	u8 key_flag;
	u8 key[16];
	u8 station_address[6];
	u8 key_index;
	u8 reserved;
} __attribute__ ((packed));

struct ipw_country_channel_info {
	u8 first_channel;
	u8 no_channels;
	s8 max_tx_power;
} __attribute__ ((packed));

struct ipw_country_info {
	u8 id;
	u8 length;
	u8 country_str[3];
	struct ipw_country_channel_info groups[7];
} __attribute__ ((packed));

struct ipw_channel_tx_power {
	u8 channel_number;
	s8 tx_power;
} __attribute__ ((packed));

#define SCAN_ASSOCIATED_INTERVAL (HZ)
#define SCAN_INTERVAL (HZ / 10)
#define MAX_A_CHANNELS  37
#define MAX_B_CHANNELS  14

struct ipw_tx_power {
	u8 num_channels;
	u8 ieee_mode;
	struct ipw_channel_tx_power channels_tx_power[MAX_A_CHANNELS];
} __attribute__ ((packed));

struct ipw_qos_parameters {
	u16 cw_min[4];
	u16 cw_max[4];
	u8 aifs[4];
	u8 flag[4];
	u16 tx_op_limit[4];
} __attribute__ ((packed));

struct ipw_rsn_capabilities {
	u8 id;
	u8 length;
	u16 version;
} __attribute__ ((packed));

struct ipw_sensitivity_calib {
	u16 beacon_rssi_raw;
	u16 reserved;
} __attribute__ ((packed));

/**
 * Host command structure.
 *
 * On input, the following fields should be filled:
 * - cmd
 * - len
 * - status_len
 * - param (if needed)
 *
 * On output,
 * - \a status contains status;
 * - \a param filled with status parameters.
 */
struct ipw_cmd {
	u32 cmd;   /**< Host command */
	u32 status;/**< Status */
	u32 status_len;
		   /**< How many 32 bit parameters in the status */
	u32 len;   /**< incoming parameters length, bytes */
  /**
   * command parameters.
   * There should be enough space for incoming and
   * outcoming parameters.
   * Incoming parameters listed 1-st, followed by outcoming params.
   * nParams=(len+3)/4+status_len
   */
	u32 param[0];
} __attribute__ ((packed));

#define STATUS_HCMD_ACTIVE      (1<<0)	/**< host command in progress */

#define STATUS_INT_ENABLED      (1<<1)
#define STATUS_RF_KILL_HW       (1<<2)
#define STATUS_RF_KILL_SW       (1<<3)
#define STATUS_RF_KILL_MASK     (STATUS_RF_KILL_HW | STATUS_RF_KILL_SW)

#define STATUS_INIT             (1<<5)
#define STATUS_AUTH             (1<<6)
#define STATUS_ASSOCIATED       (1<<7)
#define STATUS_STATE_MASK       (STATUS_INIT | STATUS_AUTH | STATUS_ASSOCIATED)

#define STATUS_ASSOCIATING      (1<<8)
#define STATUS_DISASSOCIATING   (1<<9)
#define STATUS_ROAMING          (1<<10)
#define STATUS_EXIT_PENDING     (1<<11)
#define STATUS_DISASSOC_PENDING (1<<12)
#define STATUS_STATE_PENDING    (1<<13)

#define STATUS_SCAN_PENDING     (1<<20)
#define STATUS_SCANNING         (1<<21)
#define STATUS_SCAN_ABORTING    (1<<22)

#define STATUS_INDIRECT_BYTE    (1<<28)	/* sysfs entry configured for access */
#define STATUS_INDIRECT_DWORD   (1<<29)	/* sysfs entry configured for access */
#define STATUS_DIRECT_DWORD     (1<<30)	/* sysfs entry configured for access */

#define STATUS_SECURITY_UPDATED (1<<31)	/* Security sync needed */

#define CFG_STATIC_CHANNEL      (1<<0)	/* Restrict assoc. to single channel */
#define CFG_STATIC_ESSID        (1<<1)	/* Restrict assoc. to single SSID */
#define CFG_STATIC_BSSID        (1<<2)	/* Restrict assoc. to single BSSID */
#define CFG_CUSTOM_MAC          (1<<3)
#define CFG_PREAMBLE            (1<<4)
#define CFG_ADHOC_PERSIST       (1<<5)
#define CFG_ASSOCIATE           (1<<6)
#define CFG_FIXED_RATE          (1<<7)
#define CFG_ADHOC_CREATE        (1<<8)

#define CAP_SHARED_KEY          (1<<0)	/* Off = OPEN */
#define CAP_PRIVACY_ON          (1<<1)	/* Off = No privacy */

#define MAX_STATIONS            32
#define IPW_INVALID_STATION     (0xff)

struct ipw_station_entry {
	u8 mac_addr[ETH_ALEN];
	u8 reserved;
	u8 support_mode;
};

#define AVG_ENTRIES 8
struct average {
	s16 entries[AVG_ENTRIES];
	u8 pos;
	u8 init;
	s32 sum;
};

struct ipw_priv {
	/* ieee device used by generic ieee processing code */
	struct ieee80211_device *ieee;
	struct ieee80211_security sec;

	/* spinlock */
	spinlock_t lock;

	/* basic pci-network driver stuff */
	struct pci_dev *pci_dev;
	struct net_device *net_dev;

	/* pci hardware address support */
	void __iomem *hw_base;
	unsigned long hw_len;

	struct fw_image_desc sram_desc;

	/* result of ucode download */
	struct alive_command_responce dino_alive;

	wait_queue_head_t wait_command_queue;
	wait_queue_head_t wait_state;

	/* Rx and Tx DMA processing queues */
	struct ipw_rx_queue *rxq;
	struct clx2_tx_queue txq_cmd;
	struct clx2_tx_queue txq[4];
	u32 status;
	u32 config;
	u32 capability;

	u8 last_rx_rssi;
	u8 last_noise;
	struct average average_missed_beacons;
	struct average average_rssi;
	struct average average_noise;
	u32 port_type;
	int rx_bufs_min;	  /**< minimum number of bufs in Rx queue */
	int rx_pend_max;	  /**< maximum pending buffers for one IRQ */
	u32 hcmd_seq;		  /**< sequence number for hcmd */
	u32 missed_beacon_threshold;
	u32 roaming_threshold;

	struct ipw_associate assoc_request;
	struct ieee80211_network *assoc_network;

	unsigned long ts_scan_abort;
	struct ipw_supported_rates rates;
	struct ipw_rates phy[3];	   /**< PHY restrictions, per band */
	struct ipw_rates supp;		   /**< software defined */
	struct ipw_rates extended;	   /**< use for corresp. IE, AP only */

	struct notif_link_deterioration last_link_deterioration; /** for statistics */
	struct ipw_cmd *hcmd; /**< host command currently executed */

	wait_queue_head_t hcmd_wq;     /**< host command waits for execution */
	u32 tsf_bcn[2];		     /**< TSF from latest beacon */

	struct notif_calibration calib;	/**< last calibration */

	/* ordinal interface with firmware */
	u32 table0_addr;
	u32 table0_len;
	u32 table1_addr;
	u32 table1_len;
	u32 table2_addr;
	u32 table2_len;

	/* context information */
	u8 essid[IW_ESSID_MAX_SIZE];
	u8 essid_len;
	u8 nick[IW_ESSID_MAX_SIZE];
	u16 rates_mask;
	u8 channel;
	struct ipw_sys_config sys_config;
	u32 power_mode;
	u8 bssid[ETH_ALEN];
	u16 rts_threshold;
	u8 mac_addr[ETH_ALEN];
	u8 num_stations;
	u8 stations[MAX_STATIONS][ETH_ALEN];

	u32 notif_missed_beacons;

	/* Statistics and counters normalized with each association */
	u32 last_missed_beacons;
	u32 last_tx_packets;
	u32 last_rx_packets;
	u32 last_tx_failures;
	u32 last_rx_err;
	u32 last_rate;

	u32 missed_adhoc_beacons;
	u32 missed_beacons;
	u32 rx_packets;
	u32 tx_packets;
	u32 quality;

	/* eeprom */
	u8 eeprom[0x100];	/* 256 bytes of eeprom */
	int eeprom_delay;

	struct iw_statistics wstats;

	struct workqueue_struct *workqueue;

	struct work_struct adhoc_check;
	struct work_struct associate;
	struct work_struct disassociate;
	struct work_struct rx_replenish;
	struct work_struct request_scan;
	struct work_struct adapter_restart;
	struct work_struct rf_kill;
	struct work_struct up;
	struct work_struct down;
	struct work_struct gather_stats;
	struct work_struct abort_scan;
	struct work_struct roam;
	struct work_struct scan_check;

	struct tasklet_struct irq_tasklet;

#define IPW_2200BG  1
#define IPW_2915ABG 2
	u8 adapter;

#define IPW_DEFAULT_TX_POWER 0x14
	u8 tx_power;

#ifdef CONFIG_PM
	u32 pm_state[16];
#endif

	/* network state */

	/* Used to pass the current INTA value from ISR to Tasklet */
	u32 isr_inta;

	/* debugging info */
	u32 indirect_dword;
	u32 direct_dword;
	u32 indirect_byte;
};				/*ipw_priv */

/* debug macros */

#ifdef CONFIG_IPW_DEBUG
#define IPW_DEBUG(level, fmt, args...) \
do { if (ipw_debug_level & (level)) \
  printk(KERN_DEBUG DRV_NAME": %c %s " fmt, \
         in_interrupt() ? 'I' : 'U', __FUNCTION__ , ## args); } while (0)
#else
#define IPW_DEBUG(level, fmt, args...) do {} while (0)
#endif				/* CONFIG_IPW_DEBUG */

/*
 * To use the debug system;
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of:
 *
 * #define IPW_DL_xxxx VALUE
 *
 * shifting value to the left one bit from the previous entry.  xxxx should be
 * the name of the classification (for example, WEP)
 *
 * You then need to either add a IPW_xxxx_DEBUG() macro definition for your
 * classification, or use IPW_DEBUG(IPW_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * To add your debug level to the list of levels seen when you perform
 *
 * % cat /proc/net/ipw/debug_level
 *
 * you simply need to add your entry to the ipw_debug_levels array.
 *
 * If you do not see debug_level in /proc/net/ipw then you do not have
 * CONFIG_IPW_DEBUG defined in your kernel configuration
 *
 */

#define IPW_DL_ERROR         (1<<0)
#define IPW_DL_WARNING       (1<<1)
#define IPW_DL_INFO          (1<<2)
#define IPW_DL_WX            (1<<3)
#define IPW_DL_HOST_COMMAND  (1<<5)
#define IPW_DL_STATE         (1<<6)

#define IPW_DL_NOTIF         (1<<10)
#define IPW_DL_SCAN          (1<<11)
#define IPW_DL_ASSOC         (1<<12)
#define IPW_DL_DROP          (1<<13)
#define IPW_DL_IOCTL         (1<<14)

#define IPW_DL_MANAGE        (1<<15)
#define IPW_DL_FW            (1<<16)
#define IPW_DL_RF_KILL       (1<<17)
#define IPW_DL_FW_ERRORS     (1<<18)

#define IPW_DL_ORD           (1<<20)

#define IPW_DL_FRAG          (1<<21)
#define IPW_DL_WEP           (1<<22)
#define IPW_DL_TX            (1<<23)
#define IPW_DL_RX            (1<<24)
#define IPW_DL_ISR           (1<<25)
#define IPW_DL_FW_INFO       (1<<26)
#define IPW_DL_IO            (1<<27)
#define IPW_DL_TRACE         (1<<28)

#define IPW_DL_STATS         (1<<29)

#define IPW_ERROR(f, a...) printk(KERN_ERR DRV_NAME ": " f, ## a)
#define IPW_WARNING(f, a...) printk(KERN_WARNING DRV_NAME ": " f, ## a)
#define IPW_DEBUG_INFO(f, a...)    IPW_DEBUG(IPW_DL_INFO, f, ## a)

#define IPW_DEBUG_WX(f, a...)     IPW_DEBUG(IPW_DL_WX, f, ## a)
#define IPW_DEBUG_SCAN(f, a...)   IPW_DEBUG(IPW_DL_SCAN, f, ## a)
#define IPW_DEBUG_STATUS(f, a...) IPW_DEBUG(IPW_DL_STATUS, f, ## a)
#define IPW_DEBUG_TRACE(f, a...)  IPW_DEBUG(IPW_DL_TRACE, f, ## a)
#define IPW_DEBUG_RX(f, a...)     IPW_DEBUG(IPW_DL_RX, f, ## a)
#define IPW_DEBUG_TX(f, a...)     IPW_DEBUG(IPW_DL_TX, f, ## a)
#define IPW_DEBUG_ISR(f, a...)    IPW_DEBUG(IPW_DL_ISR, f, ## a)
#define IPW_DEBUG_MANAGEMENT(f, a...) IPW_DEBUG(IPW_DL_MANAGE, f, ## a)
#define IPW_DEBUG_WEP(f, a...)    IPW_DEBUG(IPW_DL_WEP, f, ## a)
#define IPW_DEBUG_HC(f, a...) IPW_DEBUG(IPW_DL_HOST_COMMAND, f, ## a)
#define IPW_DEBUG_FRAG(f, a...) IPW_DEBUG(IPW_DL_FRAG, f, ## a)
#define IPW_DEBUG_FW(f, a...) IPW_DEBUG(IPW_DL_FW, f, ## a)
#define IPW_DEBUG_RF_KILL(f, a...) IPW_DEBUG(IPW_DL_RF_KILL, f, ## a)
#define IPW_DEBUG_DROP(f, a...) IPW_DEBUG(IPW_DL_DROP, f, ## a)
#define IPW_DEBUG_IO(f, a...) IPW_DEBUG(IPW_DL_IO, f, ## a)
#define IPW_DEBUG_ORD(f, a...) IPW_DEBUG(IPW_DL_ORD, f, ## a)
#define IPW_DEBUG_FW_INFO(f, a...) IPW_DEBUG(IPW_DL_FW_INFO, f, ## a)
#define IPW_DEBUG_NOTIF(f, a...) IPW_DEBUG(IPW_DL_NOTIF, f, ## a)
#define IPW_DEBUG_STATE(f, a...) IPW_DEBUG(IPW_DL_STATE | IPW_DL_ASSOC | IPW_DL_INFO, f, ## a)
#define IPW_DEBUG_ASSOC(f, a...) IPW_DEBUG(IPW_DL_ASSOC | IPW_DL_INFO, f, ## a)
#define IPW_DEBUG_STATS(f, a...) IPW_DEBUG(IPW_DL_STATS, f, ## a)

#include <linux/ctype.h>

/*
* Register bit definitions
*/

/* Dino control registers bits */

#define DINO_ENABLE_SYSTEM 0x80
#define DINO_ENABLE_CS     0x40
#define DINO_RXFIFO_DATA   0x01
#define DINO_CONTROL_REG   0x00200000

#define CX2_INTA_RW       0x00000008
#define CX2_INTA_MASK_R   0x0000000C
#define CX2_INDIRECT_ADDR 0x00000010
#define CX2_INDIRECT_DATA 0x00000014
#define CX2_AUTOINC_ADDR  0x00000018
#define CX2_AUTOINC_DATA  0x0000001C
#define CX2_RESET_REG     0x00000020
#define CX2_GP_CNTRL_RW   0x00000024

#define CX2_READ_INT_REGISTER 0xFF4

#define CX2_GP_CNTRL_BIT_INIT_DONE	0x00000004

#define CX2_REGISTER_DOMAIN1_END        0x00001000
#define CX2_SRAM_READ_INT_REGISTER 	0x00000ff4

#define CX2_SHARED_LOWER_BOUND          0x00000200
#define CX2_INTERRUPT_AREA_LOWER_BOUND  0x00000f80

#define CX2_NIC_SRAM_LOWER_BOUND        0x00000000
#define CX2_NIC_SRAM_UPPER_BOUND        0x00030000

#define CX2_BIT_INT_HOST_SRAM_READ_INT_REGISTER (1 << 29)
#define CX2_GP_CNTRL_BIT_CLOCK_READY    0x00000001
#define CX2_GP_CNTRL_BIT_HOST_ALLOWS_STANDBY 0x00000002

/*
 * RESET Register Bit Indexes
 */
#define CBD_RESET_REG_PRINCETON_RESET 0x00000001	/* Bit 0 (LSB) */
#define CX2_RESET_REG_SW_RESET        0x00000080	/* Bit 7       */
#define CX2_RESET_REG_MASTER_DISABLED 0x00000100	/* Bit 8       */
#define CX2_RESET_REG_STOP_MASTER     0x00000200	/* Bit 9       */
#define CX2_ARC_KESHET_CONFIG         0x08000000	/* Bit 27      */
#define CX2_START_STANDBY             0x00000004	/* Bit 2       */

#define CX2_CSR_CIS_UPPER_BOUND	0x00000200
#define CX2_DOMAIN_0_END 0x1000
#define CLX_MEM_BAR_SIZE 0x1000

#define CX2_BASEBAND_CONTROL_STATUS	0X00200000
#define CX2_BASEBAND_TX_FIFO_WRITE	0X00200004
#define CX2_BASEBAND_RX_FIFO_READ	0X00200004
#define CX2_BASEBAND_CONTROL_STORE	0X00200010

#define CX2_INTERNAL_CMD_EVENT 	0X00300004
#define CX2_BASEBAND_POWER_DOWN 0x00000001

#define CX2_MEM_HALT_AND_RESET  0x003000e0

/* defgroup bits_halt_reset MEM_HALT_AND_RESET register bits */
#define CX2_BIT_HALT_RESET_ON	0x80000000
#define CX2_BIT_HALT_RESET_OFF 	0x00000000

#define CB_LAST_VALID     0x20000000
#define CB_INT_ENABLED    0x40000000
#define CB_VALID          0x80000000
#define CB_SRC_LE         0x08000000
#define CB_DEST_LE        0x04000000
#define CB_SRC_AUTOINC    0x00800000
#define CB_SRC_IO_GATED   0x00400000
#define CB_DEST_AUTOINC   0x00080000
#define CB_SRC_SIZE_LONG  0x00200000
#define CB_DEST_SIZE_LONG 0x00020000

/* DMA DEFINES */

#define DMA_CONTROL_SMALL_CB_CONST_VALUE 0x00540000
#define DMA_CB_STOP_AND_ABORT            0x00000C00
#define DMA_CB_START                     0x00000100

#define CX2_SHARED_SRAM_SIZE               0x00030000
#define CX2_SHARED_SRAM_DMA_CONTROL        0x00027000
#define CB_MAX_LENGTH                      0x1FFF

#define CX2_HOST_EEPROM_DATA_SRAM_SIZE 0xA18
#define CX2_EEPROM_IMAGE_SIZE          0x100

/* DMA defs */
#define CX2_DMA_I_CURRENT_CB  0x003000D0
#define CX2_DMA_O_CURRENT_CB  0x003000D4
#define CX2_DMA_I_DMA_CONTROL 0x003000A4
#define CX2_DMA_I_CB_BASE     0x003000A0

#define CX2_TX_CMD_QUEUE_BD_BASE        (0x00000200)
#define CX2_TX_CMD_QUEUE_BD_SIZE        (0x00000204)
#define CX2_TX_QUEUE_0_BD_BASE          (0x00000208)
#define CX2_TX_QUEUE_0_BD_SIZE          (0x0000020C)
#define CX2_TX_QUEUE_1_BD_BASE          (0x00000210)
#define CX2_TX_QUEUE_1_BD_SIZE          (0x00000214)
#define CX2_TX_QUEUE_2_BD_BASE          (0x00000218)
#define CX2_TX_QUEUE_2_BD_SIZE          (0x0000021C)
#define CX2_TX_QUEUE_3_BD_BASE          (0x00000220)
#define CX2_TX_QUEUE_3_BD_SIZE          (0x00000224)
#define CX2_RX_BD_BASE                  (0x00000240)
#define CX2_RX_BD_SIZE                  (0x00000244)
#define CX2_RFDS_TABLE_LOWER            (0x00000500)

#define CX2_TX_CMD_QUEUE_READ_INDEX     (0x00000280)
#define CX2_TX_QUEUE_0_READ_INDEX       (0x00000284)
#define CX2_TX_QUEUE_1_READ_INDEX       (0x00000288)
#define CX2_TX_QUEUE_2_READ_INDEX       (0x0000028C)
#define CX2_TX_QUEUE_3_READ_INDEX       (0x00000290)
#define CX2_RX_READ_INDEX               (0x000002A0)

#define CX2_TX_CMD_QUEUE_WRITE_INDEX    (0x00000F80)
#define CX2_TX_QUEUE_0_WRITE_INDEX      (0x00000F84)
#define CX2_TX_QUEUE_1_WRITE_INDEX      (0x00000F88)
#define CX2_TX_QUEUE_2_WRITE_INDEX      (0x00000F8C)
#define CX2_TX_QUEUE_3_WRITE_INDEX      (0x00000F90)
#define CX2_RX_WRITE_INDEX              (0x00000FA0)

/*
 * EEPROM Related Definitions
 */

#define IPW_EEPROM_DATA_SRAM_ADDRESS (CX2_SHARED_LOWER_BOUND + 0x814)
#define IPW_EEPROM_DATA_SRAM_SIZE    (CX2_SHARED_LOWER_BOUND + 0x818)
#define IPW_EEPROM_LOAD_DISABLE      (CX2_SHARED_LOWER_BOUND + 0x81C)
#define IPW_EEPROM_DATA              (CX2_SHARED_LOWER_BOUND + 0x820)
#define IPW_EEPROM_UPPER_ADDRESS     (CX2_SHARED_LOWER_BOUND + 0x9E0)

#define IPW_STATION_TABLE_LOWER      (CX2_SHARED_LOWER_BOUND + 0xA0C)
#define IPW_STATION_TABLE_UPPER      (CX2_SHARED_LOWER_BOUND + 0xB0C)
#define IPW_REQUEST_ATIM             (CX2_SHARED_LOWER_BOUND + 0xB0C)
#define IPW_ATIM_SENT                (CX2_SHARED_LOWER_BOUND + 0xB10)
#define IPW_WHO_IS_AWAKE             (CX2_SHARED_LOWER_BOUND + 0xB14)
#define IPW_DURING_ATIM_WINDOW       (CX2_SHARED_LOWER_BOUND + 0xB18)

#define MSB                             1
#define LSB                             0
#define WORD_TO_BYTE(_word)             ((_word) * sizeof(u16))

#define GET_EEPROM_ADDR(_wordoffset,_byteoffset) \
    ( WORD_TO_BYTE(_wordoffset) + (_byteoffset) )

/* EEPROM access by BYTE */
#define EEPROM_PME_CAPABILITY   (GET_EEPROM_ADDR(0x09,MSB))	/* 1 byte   */
#define EEPROM_MAC_ADDRESS      (GET_EEPROM_ADDR(0x21,LSB))	/* 6 byte   */
#define EEPROM_VERSION          (GET_EEPROM_ADDR(0x24,MSB))	/* 1 byte   */
#define EEPROM_NIC_TYPE         (GET_EEPROM_ADDR(0x25,LSB))	/* 1 byte   */
#define EEPROM_SKU_CAPABILITY   (GET_EEPROM_ADDR(0x25,MSB))	/* 1 byte   */
#define EEPROM_COUNTRY_CODE     (GET_EEPROM_ADDR(0x26,LSB))	/* 3 bytes  */
#define EEPROM_IBSS_CHANNELS_BG (GET_EEPROM_ADDR(0x28,LSB))	/* 2 bytes  */
#define EEPROM_IBSS_CHANNELS_A  (GET_EEPROM_ADDR(0x29,MSB))	/* 5 bytes  */
#define EEPROM_BSS_CHANNELS_BG  (GET_EEPROM_ADDR(0x2c,LSB))	/* 2 bytes  */
#define EEPROM_HW_VERSION       (GET_EEPROM_ADDR(0x72,LSB))	/* 2 bytes  */

/* NIC type as found in the one byte EEPROM_NIC_TYPE  offset*/
#define EEPROM_NIC_TYPE_STANDARD        0
#define EEPROM_NIC_TYPE_DELL            1
#define EEPROM_NIC_TYPE_FUJITSU         2
#define EEPROM_NIC_TYPE_IBM             3
#define EEPROM_NIC_TYPE_HP              4

#define FW_MEM_REG_LOWER_BOUND          0x00300000
#define FW_MEM_REG_EEPROM_ACCESS        (FW_MEM_REG_LOWER_BOUND + 0x40)

#define EEPROM_BIT_SK                   (1<<0)
#define EEPROM_BIT_CS                   (1<<1)
#define EEPROM_BIT_DI                   (1<<2)
#define EEPROM_BIT_DO                   (1<<4)

#define EEPROM_CMD_READ                 0x2

/* Interrupts masks */
#define CX2_INTA_NONE   0x00000000

#define CX2_INTA_BIT_RX_TRANSFER                   0x00000002
#define CX2_INTA_BIT_STATUS_CHANGE                 0x00000010
#define CX2_INTA_BIT_BEACON_PERIOD_EXPIRED         0x00000020

//Inta Bits for CF
#define CX2_INTA_BIT_TX_CMD_QUEUE                  0x00000800
#define CX2_INTA_BIT_TX_QUEUE_1                    0x00001000
#define CX2_INTA_BIT_TX_QUEUE_2                    0x00002000
#define CX2_INTA_BIT_TX_QUEUE_3                    0x00004000
#define CX2_INTA_BIT_TX_QUEUE_4                    0x00008000

#define CX2_INTA_BIT_SLAVE_MODE_HOST_CMD_DONE      0x00010000

#define CX2_INTA_BIT_PREPARE_FOR_POWER_DOWN        0x00100000
#define CX2_INTA_BIT_POWER_DOWN                    0x00200000

#define CX2_INTA_BIT_FW_INITIALIZATION_DONE        0x01000000
#define CX2_INTA_BIT_FW_CARD_DISABLE_PHY_OFF_DONE  0x02000000
#define CX2_INTA_BIT_RF_KILL_DONE                  0x04000000
#define CX2_INTA_BIT_FATAL_ERROR             0x40000000
#define CX2_INTA_BIT_PARITY_ERROR            0x80000000

/* Interrupts enabled at init time. */
#define CX2_INTA_MASK_ALL                        \
        (CX2_INTA_BIT_TX_QUEUE_1               | \
	 CX2_INTA_BIT_TX_QUEUE_2               | \
	 CX2_INTA_BIT_TX_QUEUE_3               | \
	 CX2_INTA_BIT_TX_QUEUE_4               | \
	 CX2_INTA_BIT_TX_CMD_QUEUE             | \
	 CX2_INTA_BIT_RX_TRANSFER              | \
	 CX2_INTA_BIT_FATAL_ERROR              | \
	 CX2_INTA_BIT_PARITY_ERROR             | \
	 CX2_INTA_BIT_STATUS_CHANGE            | \
	 CX2_INTA_BIT_FW_INITIALIZATION_DONE   | \
	 CX2_INTA_BIT_BEACON_PERIOD_EXPIRED    | \
	 CX2_INTA_BIT_SLAVE_MODE_HOST_CMD_DONE | \
	 CX2_INTA_BIT_PREPARE_FOR_POWER_DOWN   | \
	 CX2_INTA_BIT_POWER_DOWN               | \
         CX2_INTA_BIT_RF_KILL_DONE )

#define IPWSTATUS_ERROR_LOG     (CX2_SHARED_LOWER_BOUND + 0x410)
#define IPW_EVENT_LOG     (CX2_SHARED_LOWER_BOUND + 0x414)

/* FW event log definitions */
#define EVENT_ELEM_SIZE     (3 * sizeof(u32))
#define EVENT_START_OFFSET  (1 * sizeof(u32) + 2 * sizeof(u16))

/* FW error log definitions */
#define ERROR_ELEM_SIZE     (7 * sizeof(u32))
#define ERROR_START_OFFSET  (1 * sizeof(u32))

enum {
	IPW_FW_ERROR_OK = 0,
	IPW_FW_ERROR_FAIL,
	IPW_FW_ERROR_MEMORY_UNDERFLOW,
	IPW_FW_ERROR_MEMORY_OVERFLOW,
	IPW_FW_ERROR_BAD_PARAM,
	IPW_FW_ERROR_BAD_CHECKSUM,
	IPW_FW_ERROR_NMI_INTERRUPT,
	IPW_FW_ERROR_BAD_DATABASE,
	IPW_FW_ERROR_ALLOC_FAIL,
	IPW_FW_ERROR_DMA_UNDERRUN,
	IPW_FW_ERROR_DMA_STATUS,
	IPW_FW_ERROR_DINOSTATUS_ERROR,
	IPW_FW_ERROR_EEPROMSTATUS_ERROR,
	IPW_FW_ERROR_SYSASSERT,
	IPW_FW_ERROR_FATAL_ERROR
};

#define AUTH_OPEN       0
#define AUTH_SHARED_KEY 1
#define AUTH_IGNORE     3

#define HC_ASSOCIATE      0
#define HC_REASSOCIATE    1
#define HC_DISASSOCIATE   2
#define HC_IBSS_START     3
#define HC_IBSS_RECONF    4
#define HC_DISASSOC_QUIET 5

#define IPW_RATE_CAPABILITIES 1
#define IPW_RATE_CONNECT      0

/*
 * Rate values and masks
 */
#define IPW_TX_RATE_1MB  0x0A
#define IPW_TX_RATE_2MB  0x14
#define IPW_TX_RATE_5MB  0x37
#define IPW_TX_RATE_6MB  0x0D
#define IPW_TX_RATE_9MB  0x0F
#define IPW_TX_RATE_11MB 0x6E
#define IPW_TX_RATE_12MB 0x05
#define IPW_TX_RATE_18MB 0x07
#define IPW_TX_RATE_24MB 0x09
#define IPW_TX_RATE_36MB 0x0B
#define IPW_TX_RATE_48MB 0x01
#define IPW_TX_RATE_54MB 0x03

#define IPW_ORD_TABLE_ID_MASK             0x0000FF00
#define IPW_ORD_TABLE_VALUE_MASK          0x000000FF

#define IPW_ORD_TABLE_0_MASK              0x0000F000
#define IPW_ORD_TABLE_1_MASK              0x0000F100
#define IPW_ORD_TABLE_2_MASK              0x0000F200
#define IPW_ORD_TABLE_3_MASK              0x0000F300
#define IPW_ORD_TABLE_4_MASK              0x0000F400
#define IPW_ORD_TABLE_5_MASK              0x0000F500
#define IPW_ORD_TABLE_6_MASK              0x0000F600
#define IPW_ORD_TABLE_7_MASK              0x0000F700

/*
 * Table 0 Entries (all entries are 32 bits)
 */
enum {
	IPW_ORD_STAT_TX_CURR_RATE = IPW_ORD_TABLE_0_MASK + 1,
	IPW_ORD_STAT_FRAG_TRESHOLD,
	IPW_ORD_STAT_RTS_THRESHOLD,
	IPW_ORD_STAT_TX_HOST_REQUESTS,
	IPW_ORD_STAT_TX_HOST_COMPLETE,
	IPW_ORD_STAT_TX_DIR_DATA,
	IPW_ORD_STAT_TX_DIR_DATA_B_1,
	IPW_ORD_STAT_TX_DIR_DATA_B_2,
	IPW_ORD_STAT_TX_DIR_DATA_B_5_5,
	IPW_ORD_STAT_TX_DIR_DATA_B_11,
	/* Hole */

	IPW_ORD_STAT_TX_DIR_DATA_G_1 = IPW_ORD_TABLE_0_MASK + 19,
	IPW_ORD_STAT_TX_DIR_DATA_G_2,
	IPW_ORD_STAT_TX_DIR_DATA_G_5_5,
	IPW_ORD_STAT_TX_DIR_DATA_G_6,
	IPW_ORD_STAT_TX_DIR_DATA_G_9,
	IPW_ORD_STAT_TX_DIR_DATA_G_11,
	IPW_ORD_STAT_TX_DIR_DATA_G_12,
	IPW_ORD_STAT_TX_DIR_DATA_G_18,
	IPW_ORD_STAT_TX_DIR_DATA_G_24,
	IPW_ORD_STAT_TX_DIR_DATA_G_36,
	IPW_ORD_STAT_TX_DIR_DATA_G_48,
	IPW_ORD_STAT_TX_DIR_DATA_G_54,
	IPW_ORD_STAT_TX_NON_DIR_DATA,
	IPW_ORD_STAT_TX_NON_DIR_DATA_B_1,
	IPW_ORD_STAT_TX_NON_DIR_DATA_B_2,
	IPW_ORD_STAT_TX_NON_DIR_DATA_B_5_5,
	IPW_ORD_STAT_TX_NON_DIR_DATA_B_11,
	/* Hole */

	IPW_ORD_STAT_TX_NON_DIR_DATA_G_1 = IPW_ORD_TABLE_0_MASK + 44,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_2,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_5_5,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_6,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_9,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_11,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_12,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_18,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_24,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_36,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_48,
	IPW_ORD_STAT_TX_NON_DIR_DATA_G_54,
	IPW_ORD_STAT_TX_RETRY,
	IPW_ORD_STAT_TX_FAILURE,
	IPW_ORD_STAT_RX_ERR_CRC,
	IPW_ORD_STAT_RX_ERR_ICV,
	IPW_ORD_STAT_RX_NO_BUFFER,
	IPW_ORD_STAT_FULL_SCANS,
	IPW_ORD_STAT_PARTIAL_SCANS,
	IPW_ORD_STAT_TGH_ABORTED_SCANS,
	IPW_ORD_STAT_TX_TOTAL_BYTES,
	IPW_ORD_STAT_CURR_RSSI_RAW,
	IPW_ORD_STAT_RX_BEACON,
	IPW_ORD_STAT_MISSED_BEACONS,
	IPW_ORD_TABLE_0_LAST
};

#define IPW_RSSI_TO_DBM 112

/* Table 1 Entries
 */
enum {
	IPW_ORD_TABLE_1_LAST = IPW_ORD_TABLE_1_MASK | 1,
};

/*
 * Table 2 Entries
 *
 * FW_VERSION:    16 byte string
 * FW_DATE:       16 byte string (only 14 bytes used)
 * UCODE_VERSION: 4 byte version code
 * UCODE_DATE:    5 bytes code code
 * ADDAPTER_MAC:  6 byte MAC address
 * RTC:           4 byte clock
 */
enum {
	IPW_ORD_STAT_FW_VERSION = IPW_ORD_TABLE_2_MASK | 1,
	IPW_ORD_STAT_FW_DATE,
	IPW_ORD_STAT_UCODE_VERSION,
	IPW_ORD_STAT_UCODE_DATE,
	IPW_ORD_STAT_ADAPTER_MAC,
	IPW_ORD_STAT_RTC,
	IPW_ORD_TABLE_2_LAST
};

/* Table 3 */
enum {
	IPW_ORD_STAT_TX_PACKET = IPW_ORD_TABLE_3_MASK | 0,
	IPW_ORD_STAT_TX_PACKET_FAILURE,
	IPW_ORD_STAT_TX_PACKET_SUCCESS,
	IPW_ORD_STAT_TX_PACKET_ABORTED,
	IPW_ORD_TABLE_3_LAST
};

/* Table 4 */
enum {
	IPW_ORD_TABLE_4_LAST = IPW_ORD_TABLE_4_MASK
};

/* Table 5 */
enum {
	IPW_ORD_STAT_AVAILABLE_AP_COUNT = IPW_ORD_TABLE_5_MASK,
	IPW_ORD_STAT_AP_ASSNS,
	IPW_ORD_STAT_ROAM,
	IPW_ORD_STAT_ROAM_CAUSE_MISSED_BEACONS,
	IPW_ORD_STAT_ROAM_CAUSE_UNASSOC,
	IPW_ORD_STAT_ROAM_CAUSE_RSSI,
	IPW_ORD_STAT_ROAM_CAUSE_LINK_QUALITY,
	IPW_ORD_STAT_ROAM_CAUSE_AP_LOAD_BALANCE,
	IPW_ORD_STAT_ROAM_CAUSE_AP_NO_TX,
	IPW_ORD_STAT_LINK_UP,
	IPW_ORD_STAT_LINK_DOWN,
	IPW_ORD_ANTENNA_DIVERSITY,
	IPW_ORD_CURR_FREQ,
	IPW_ORD_TABLE_5_LAST
};

/* Table 6 */
enum {
	IPW_ORD_COUNTRY_CODE = IPW_ORD_TABLE_6_MASK,
	IPW_ORD_CURR_BSSID,
	IPW_ORD_CURR_SSID,
	IPW_ORD_TABLE_6_LAST
};

/* Table 7 */
enum {
	IPW_ORD_STAT_PERCENT_MISSED_BEACONS = IPW_ORD_TABLE_7_MASK,
	IPW_ORD_STAT_PERCENT_TX_RETRIES,
	IPW_ORD_STAT_PERCENT_LINK_QUALITY,
	IPW_ORD_STAT_CURR_RSSI_DBM,
	IPW_ORD_TABLE_7_LAST
};

#define IPW_ORDINALS_TABLE_LOWER        (CX2_SHARED_LOWER_BOUND + 0x500)
#define IPW_ORDINALS_TABLE_0            (CX2_SHARED_LOWER_BOUND + 0x180)
#define IPW_ORDINALS_TABLE_1            (CX2_SHARED_LOWER_BOUND + 0x184)
#define IPW_ORDINALS_TABLE_2            (CX2_SHARED_LOWER_BOUND + 0x188)
#define IPW_MEM_FIXED_OVERRIDE          (CX2_SHARED_LOWER_BOUND + 0x41C)

struct ipw_fixed_rate {
	u16 tx_rates;
	u16 reserved;
} __attribute__ ((packed));

#define CX2_INDIRECT_ADDR_MASK (~0x3ul)

struct host_cmd {
	u8 cmd;
	u8 len;
	u16 reserved;
	u32 param[TFD_CMD_IMMEDIATE_PAYLOAD_LENGTH];
} __attribute__ ((packed));

#define CFG_BT_COEXISTENCE_MIN                  0x00
#define CFG_BT_COEXISTENCE_DEFER                0x02
#define CFG_BT_COEXISTENCE_KILL                 0x04
#define CFG_BT_COEXISTENCE_WME_OVER_BT          0x08
#define CFG_BT_COEXISTENCE_OOB                  0x10
#define CFG_BT_COEXISTENCE_MAX                  0xFF
#define CFG_BT_COEXISTENCE_DEF                  0x80	/* read Bt from EEPROM */

#define CFG_CTS_TO_ITSELF_ENABLED_MIN	0x0
#define CFG_CTS_TO_ITSELF_ENABLED_MAX	0x1
#define CFG_CTS_TO_ITSELF_ENABLED_DEF	CFG_CTS_TO_ITSELF_ENABLED_MIN

#define CFG_SYS_ANTENNA_BOTH                      0x000
#define CFG_SYS_ANTENNA_A                         0x001
#define CFG_SYS_ANTENNA_B                         0x003

/*
 * The definitions below were lifted off the ipw2100 driver, which only
 * supports 'b' mode, so I'm sure these are not exactly correct.
 *
 * Somebody fix these!!
 */
#define REG_MIN_CHANNEL             0
#define REG_MAX_CHANNEL             14

#define REG_CHANNEL_MASK            0x00003FFF
#define IPW_IBSS_11B_DEFAULT_MASK   0x87ff

static const long ipw_frequencies[] = {
	2412, 2417, 2422, 2427,
	2432, 2437, 2442, 2447,
	2452, 2457, 2462, 2467,
	2472, 2484
};

#define FREQ_COUNT ARRAY_SIZE(ipw_frequencies)

#define IPW_MAX_CONFIG_RETRIES 10

static inline u32 frame_hdr_len(struct ieee80211_hdr *hdr)
{
	u32 retval;
	u16 fc;

	retval = sizeof(struct ieee80211_hdr);
	fc = le16_to_cpu(hdr->frame_ctl);

	/*
	 * Function     ToDS    FromDS
	 * IBSS         0       0
	 * To AP        1       0
	 * From AP      0       1
	 * WDS (bridge) 1       1
	 *
	 * Only WDS frames use Address4 among them. --YZ
	 */
	if (!(fc & IEEE80211_FCTL_TODS) || !(fc & IEEE80211_FCTL_FROMDS))
		retval -= ETH_ALEN;

	return retval;
}

#endif				/* __ipw2200_h__ */
