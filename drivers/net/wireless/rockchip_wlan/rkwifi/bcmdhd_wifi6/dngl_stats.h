/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common stats definitions for clients of dongle
 * ports
 *
 * Copyright (C) 1999-2019, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dngl_stats.h 716269 2017-08-17 09:22:46Z $
 */

#ifndef _dngl_stats_h_
#define _dngl_stats_h_

#include <ethernet.h>
#include <802.11.h>

typedef struct {
	unsigned long	rx_packets;		/* total packets received */
	unsigned long	tx_packets;		/* total packets transmitted */
	unsigned long	rx_bytes;		/* total bytes received */
	unsigned long	tx_bytes;		/* total bytes transmitted */
	unsigned long	rx_errors;		/* bad packets received */
	unsigned long	tx_errors;		/* packet transmit problems */
	unsigned long	rx_dropped;		/* packets dropped by dongle */
	unsigned long	tx_dropped;		/* packets dropped by dongle */
	unsigned long   multicast;      /* multicast packets received */
} dngl_stats_t;

typedef int32 wifi_radio;
typedef int32 wifi_channel;
typedef int32 wifi_rssi;
typedef struct { uint16 version; uint16 length; } ver_len;

typedef enum wifi_channel_width {
	WIFI_CHAN_WIDTH_20	  = 0,
	WIFI_CHAN_WIDTH_40	  = 1,
	WIFI_CHAN_WIDTH_80	  = 2,
	WIFI_CHAN_WIDTH_160   = 3,
	WIFI_CHAN_WIDTH_80P80 = 4,
	WIFI_CHAN_WIDTH_5	  = 5,
	WIFI_CHAN_WIDTH_10	  = 6,
	WIFI_CHAN_WIDTH_INVALID = -1
} wifi_channel_width_t;

typedef enum {
	WIFI_DISCONNECTED = 0,
	WIFI_AUTHENTICATING = 1,
	WIFI_ASSOCIATING = 2,
	WIFI_ASSOCIATED = 3,
	WIFI_EAPOL_STARTED = 4,   /* if done by firmware/driver */
	WIFI_EAPOL_COMPLETED = 5, /* if done by firmware/driver */
} wifi_connection_state;

typedef enum {
	WIFI_ROAMING_IDLE = 0,
	WIFI_ROAMING_ACTIVE = 1
} wifi_roam_state;

typedef enum {
	WIFI_INTERFACE_STA = 0,
	WIFI_INTERFACE_SOFTAP = 1,
	WIFI_INTERFACE_IBSS = 2,
	WIFI_INTERFACE_P2P_CLIENT = 3,
	WIFI_INTERFACE_P2P_GO = 4,
	WIFI_INTERFACE_NAN = 5,
	WIFI_INTERFACE_MESH = 6
} wifi_interface_mode;

#define WIFI_CAPABILITY_QOS          0x00000001     /* set for QOS association */
#define WIFI_CAPABILITY_PROTECTED    0x00000002     /* set for protected association (802.11
						     * beacon frame control protected bit set)
						     */
#define WIFI_CAPABILITY_INTERWORKING 0x00000004     /* set if 802.11 Extended Capabilities
						     * element interworking bit is set
						     */
#define WIFI_CAPABILITY_HS20         0x00000008     /* set for HS20 association */
#define WIFI_CAPABILITY_SSID_UTF8    0x00000010     /* set is 802.11 Extended Capabilities
						     * element UTF-8 SSID bit is set
						     */
#define WIFI_CAPABILITY_COUNTRY      0x00000020     /* set is 802.11 Country Element is present */
#define PACK_ATTRIBUTE __attribute__ ((packed))
typedef struct {
	wifi_interface_mode mode;     /* interface mode */
	uint8 mac_addr[6];               /* interface mac address (self) */
	uint8 PAD[2];
	wifi_connection_state state;  /* connection state (valid for STA, CLI only) */
	wifi_roam_state roaming;      /* roaming state */
	uint32 capabilities;             /* WIFI_CAPABILITY_XXX (self) */
	uint8 ssid[DOT11_MAX_SSID_LEN+1]; /* null terminated SSID */
	uint8 bssid[ETHER_ADDR_LEN];     /* bssid */
	uint8 PAD[1];
	uint8 ap_country_str[3];         /* country string advertised by AP */
	uint8 country_str[3];            /* country string for this association */
	uint8 PAD[2];
} wifi_interface_info;

typedef wifi_interface_info *wifi_interface_handle;

/* channel information */
typedef struct {
	wifi_channel_width_t width;   /* channel width (20, 40, 80, 80+80, 160) */
	wifi_channel center_freq;   /* primary 20 MHz channel */
	wifi_channel center_freq0;  /* center frequency (MHz) first segment */
	wifi_channel center_freq1;  /* center frequency (MHz) second segment */
} wifi_channel_info;

/* wifi rate */
typedef struct {
	uint32 preamble;   /* 0: OFDM, 1:CCK, 2:HT 3:VHT 4..7 reserved */
	uint32 nss;   	/* 0:1x1, 1:2x2, 3:3x3, 4:4x4 */
	uint32 bw;   	/* 0:20MHz, 1:40Mhz, 2:80Mhz, 3:160Mhz */
	uint32 rateMcsIdx; /* OFDM/CCK rate code would be as per ieee std
			    * in the units of 0.5mbps
			    */
			/* HT/VHT it would be mcs index */
	uint32 reserved;   /* reserved */
	uint32 bitrate;    /* units of 100 Kbps */
} wifi_rate;

typedef struct {
	uint32 preamble   :3;   /* 0: OFDM, 1:CCK, 2:HT 3:VHT 4..7 reserved */
	uint32 nss        :2;   /* 0:1x1, 1:2x2, 3:3x3, 4:4x4 */
	uint32 bw         :3;   /* 0:20MHz, 1:40Mhz, 2:80Mhz, 3:160Mhz */
	uint32 rateMcsIdx :8;   /* OFDM/CCK rate code would be as per ieee std
					* in the units of 0.5mbps HT/VHT it would be
					* mcs index
					*/
	uint32 reserved  :16;   /* reserved */
	uint32 bitrate;         /* units of 100 Kbps */
} wifi_rate_v1;

/* channel statistics */
typedef struct {
	wifi_channel_info channel;  /* channel */
	uint32 on_time;			/* msecs the radio is awake (32 bits number
				         * accruing over time)
					 */
	uint32 cca_busy_time;          /* msecs the CCA register is busy (32 bits number
					* accruing over time)
					*/
} wifi_channel_stat;

/* radio statistics */
typedef struct {
	struct {
		uint16 version;
		uint16 length;
	};
	wifi_radio radio;               /* wifi radio (if multiple radio supported) */
	uint32 on_time;                    /* msecs the radio is awake (32 bits number
					    * accruing over time)
					    */
	uint32 tx_time;                    /* msecs the radio is transmitting (32 bits
					    * number accruing over time)
					    */
	uint32 rx_time;                    /* msecs the radio is in active receive (32 bits
					    * number accruing over time)
					    */
	uint32 on_time_scan;               /* msecs the radio is awake due to all scan (32 bits
					    * number accruing over time)
					    */
	uint32 on_time_nbd;                /* msecs the radio is awake due to NAN (32 bits
					    * number accruing over time)
					    */
	uint32 on_time_gscan;              /* msecs the radio is awake due to G?scan (32 bits
					    * number accruing over time)
					    */
	uint32 on_time_roam_scan;          /* msecs the radio is awake due to roam?scan (32 bits
					    * number accruing over time)
					    */
	uint32 on_time_pno_scan;           /* msecs the radio is awake due to PNO scan (32 bits
					    * number accruing over time)
					    */
	uint32 on_time_hs20;               /* msecs the radio is awake due to HS2.0 scans and
					    * GAS exchange (32 bits number accruing over time)
					    */
	uint32 num_channels;               /* number of channels */
	wifi_channel_stat channels[1];   /* channel statistics */
} wifi_radio_stat;

typedef struct {
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
} wifi_radio_stat_h;

/* per rate statistics */
typedef struct {
	wifi_rate_v1 rate;     /* rate information */
	uint32 tx_mpdu;        /* number of successfully transmitted data pkts (ACK rcvd) */
	uint32 rx_mpdu;        /* number of received data pkts */
	uint32 mpdu_lost;      /* number of data packet losses (no ACK) */
	uint32 retries;        /* total number of data pkt retries */
	uint32 retries_short;  /* number of short data pkt retries */
	uint32 retries_long;   /* number of long data pkt retries */
} wifi_rate_stat_v1;

typedef struct {
	uint16 version;
	uint16 length;
	uint32 tx_mpdu;        /* number of successfully transmitted data pkts (ACK rcvd) */
	uint32 rx_mpdu;        /* number of received data pkts */
	uint32 mpdu_lost;      /* number of data packet losses (no ACK) */
	uint32 retries;        /* total number of data pkt retries */
	uint32 retries_short;  /* number of short data pkt retries */
	uint32 retries_long;   /* number of long data pkt retries */
	wifi_rate rate;
} wifi_rate_stat;

/* access categories */
typedef enum {
	WIFI_AC_VO  = 0,
	WIFI_AC_VI  = 1,
	WIFI_AC_BE  = 2,
	WIFI_AC_BK  = 3,
	WIFI_AC_MAX = 4
} wifi_traffic_ac;

/* wifi peer type */
typedef enum
{
	WIFI_PEER_STA,
	WIFI_PEER_AP,
	WIFI_PEER_P2P_GO,
	WIFI_PEER_P2P_CLIENT,
	WIFI_PEER_NAN,
	WIFI_PEER_TDLS,
	WIFI_PEER_INVALID
} wifi_peer_type;

/* per peer statistics */
typedef struct {
	wifi_peer_type type;           /* peer type (AP, TDLS, GO etc.) */
	uint8 peer_mac_address[6];        /* mac address */
	uint32 capabilities;              /* peer WIFI_CAPABILITY_XXX */
	uint32 num_rate;                  /* number of rates */
	wifi_rate_stat rate_stats[1];   /* per rate statistics, number of entries  = num_rate */
} wifi_peer_info;

/* per access category statistics */
typedef struct {
	wifi_traffic_ac ac;             /* access category (VI, VO, BE, BK) */
	uint32 tx_mpdu;                    /* number of successfully transmitted unicast data pkts
					    * (ACK rcvd)
					    */
	uint32 rx_mpdu;                    /* number of received unicast mpdus */
	uint32 tx_mcast;                   /* number of succesfully transmitted multicast
					    * data packets
					    */
					   /* STA case: implies ACK received from AP for the
					    * unicast packet in which mcast pkt was sent
					    */
	uint32 rx_mcast;                   /* number of received multicast data packets */
	uint32 rx_ampdu;                   /* number of received unicast a-mpdus */
	uint32 tx_ampdu;                   /* number of transmitted unicast a-mpdus */
	uint32 mpdu_lost;                  /* number of data pkt losses (no ACK) */
	uint32 retries;                    /* total number of data pkt retries */
	uint32 retries_short;              /* number of short data pkt retries */
	uint32 retries_long;               /* number of long data pkt retries */
	uint32 contention_time_min;        /* data pkt min contention time (usecs) */
	uint32 contention_time_max;        /* data pkt max contention time (usecs) */
	uint32 contention_time_avg;        /* data pkt avg contention time (usecs) */
	uint32 contention_num_samples;     /* num of data pkts used for contention statistics */
} wifi_wmm_ac_stat;

/* interface statistics */
typedef struct {
	wifi_interface_handle iface;          /* wifi interface */
	wifi_interface_info info;             /* current state of the interface */
	uint32 beacon_rx;                     /* access point beacon received count from
					       * connected AP
					       */
	uint64 average_tsf_offset;	/* average beacon offset encountered (beacon_TSF - TBTT)
					* The average_tsf_offset field is used so as to calculate
					* the typical beacon contention time on the channel as well
					* may be used to debug beacon synchronization and related
					* power consumption issue
					*/
	uint32 leaky_ap_detected;	/* indicate that this AP
					* typically leaks packets beyond
					* the driver guard time.
					*/
	uint32 leaky_ap_avg_num_frames_leaked;	/* average number of frame leaked by AP after
					* frame with PM bit set was ACK'ed by AP
					*/
	uint32 leaky_ap_guard_time;		/* guard time currently in force
					* (when implementing IEEE power management
					* based on frame control PM bit), How long
					* driver waits before shutting down the radio and after
					* receiving an ACK for a data frame with PM bit set)
					*/
	uint32 mgmt_rx;                       /* access point mgmt frames received count from
				       * connected AP (including Beacon)
				       */
	uint32 mgmt_action_rx;                /* action frames received count */
	uint32 mgmt_action_tx;                /* action frames transmit count */
	wifi_rssi rssi_mgmt;                  /* access Point Beacon and Management frames RSSI
					       * (averaged)
					       */
	wifi_rssi rssi_data;                  /* access Point Data Frames RSSI (averaged) from
					       * connected AP
					       */
	wifi_rssi rssi_ack;                   /* access Point ACK RSSI (averaged) from
					       * connected AP
					       */
	wifi_wmm_ac_stat ac[WIFI_AC_MAX];     /* per ac data packet statistics */
	uint32 num_peers;                        /* number of peers */
	wifi_peer_info peer_info[1];           /* per peer statistics */
} wifi_iface_stat;

#ifdef CONFIG_COMPAT
/* interface statistics */
typedef struct {
	compat_uptr_t iface;          /* wifi interface */
	wifi_interface_info info;             /* current state of the interface */
	uint32 beacon_rx;                     /* access point beacon received count from
					       * connected AP
					       */
	uint64 average_tsf_offset;	/* average beacon offset encountered (beacon_TSF - TBTT)
					* The average_tsf_offset field is used so as to calculate
					* the typical beacon contention time on the channel as well
					* may be used to debug beacon synchronization and related
					* power consumption issue
					*/
	uint32 leaky_ap_detected;	/* indicate that this AP
					* typically leaks packets beyond
					* the driver guard time.
					*/
	uint32 leaky_ap_avg_num_frames_leaked;	/* average number of frame leaked by AP after
					* frame with PM bit set was ACK'ed by AP
					*/
	uint32 leaky_ap_guard_time;		/* guard time currently in force
					* (when implementing IEEE power management
					* based on frame control PM bit), How long
					* driver waits before shutting down the radio and after
					* receiving an ACK for a data frame with PM bit set)
					*/
	uint32 mgmt_rx;                       /* access point mgmt frames received count from
				       * connected AP (including Beacon)
				       */
	uint32 mgmt_action_rx;                /* action frames received count */
	uint32 mgmt_action_tx;                /* action frames transmit count */
	wifi_rssi rssi_mgmt;                  /* access Point Beacon and Management frames RSSI
					       * (averaged)
					       */
	wifi_rssi rssi_data;                  /* access Point Data Frames RSSI (averaged) from
					       * connected AP
					       */
	wifi_rssi rssi_ack;                   /* access Point ACK RSSI (averaged) from
					       * connected AP
					       */
	wifi_wmm_ac_stat ac[WIFI_AC_MAX];     /* per ac data packet statistics */
	uint32 num_peers;                        /* number of peers */
	wifi_peer_info peer_info[1];           /* per peer statistics */
} compat_wifi_iface_stat;
#endif /* CONFIG_COMPAT */

#endif /* _dngl_stats_h_ */
