/*
 * Copyright (c) 2002,2003 Oliver Kurth
 *	     (c) 2003,2004 Joerg Albert <joerg.albert@gmx.de>
 *	     (c) 2007 Guido Guenther <agx@sigxcpu.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This driver was based on information from the Sourceforge driver
 * released and maintained by Atmel:
 *
 *  http://sourceforge.net/projects/atmelwlandriver/
 *
 * Although the code was completely re-written,
 * it would have been impossible without Atmel's decision to
 * release an Open Source driver (unfortunately the firmware was
 * kept binary only). Thanks for that decision to Atmel!
 */

#ifndef _AT76_USB_H
#define _AT76_USB_H

/*
 * ieee80211 definitions copied from net/ieee80211.h
 */

#define WEP_KEY_LEN		13
#define WEP_KEYS		4

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

struct ieee80211_info_element {
	u8 id;
	u8 len;
	u8 data[0];
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

struct ieee80211_auth {
	struct ieee80211_hdr_3addr header;
	__le16 algorithm;
	__le16 transaction;
	__le16 status;
	/* challenge */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_assoc_request {
	struct ieee80211_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	/* SSID, supported rates, RSN */
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

struct ieee80211_assoc_response {
	struct ieee80211_hdr_3addr header;
	__le16 capability;
	__le16 status;
	__le16 aid;
	/* supported rates */
	struct ieee80211_info_element info_element[0];
} __attribute__ ((packed));

struct ieee80211_disassoc {
	struct ieee80211_hdr_3addr header;
	__le16 reason;
} __attribute__ ((packed));

/* Board types */
enum board_type {
	BOARD_503_ISL3861 = 1,
	BOARD_503_ISL3863 = 2,
	BOARD_503 = 3,
	BOARD_503_ACC = 4,
	BOARD_505 = 5,
	BOARD_505_2958 = 6,
	BOARD_505A = 7,
	BOARD_505AMX = 8
};

/* our private ioctl's */
/* preamble length (0 - long, 1 - short, 2 - auto) */
#define AT76_SET_SHORT_PREAMBLE		(SIOCIWFIRSTPRIV + 0)
#define AT76_GET_SHORT_PREAMBLE		(SIOCIWFIRSTPRIV + 1)
/* which debug channels are enabled */
#define AT76_SET_DEBUG			(SIOCIWFIRSTPRIV + 2)
#define AT76_GET_DEBUG			(SIOCIWFIRSTPRIV + 3)
/* power save mode (incl. the Atmel proprietary smart save mode) */
#define AT76_SET_POWERSAVE_MODE		(SIOCIWFIRSTPRIV + 4)
#define AT76_GET_POWERSAVE_MODE		(SIOCIWFIRSTPRIV + 5)
/* min and max channel times for scan */
#define AT76_SET_SCAN_TIMES		(SIOCIWFIRSTPRIV + 6)
#define AT76_GET_SCAN_TIMES		(SIOCIWFIRSTPRIV + 7)
/* scan mode (0 - active, 1 - passive) */
#define AT76_SET_SCAN_MODE		(SIOCIWFIRSTPRIV + 8)
#define AT76_GET_SCAN_MODE		(SIOCIWFIRSTPRIV + 9)

#define CMD_STATUS_IDLE				0x00
#define CMD_STATUS_COMPLETE			0x01
#define CMD_STATUS_UNKNOWN			0x02
#define CMD_STATUS_INVALID_PARAMETER		0x03
#define CMD_STATUS_FUNCTION_NOT_SUPPORTED	0x04
#define CMD_STATUS_TIME_OUT			0x07
#define CMD_STATUS_IN_PROGRESS			0x08
#define CMD_STATUS_HOST_FAILURE			0xff
#define CMD_STATUS_SCAN_FAILED			0xf0

/* answers to get op mode */
#define OPMODE_NONE				0x00
#define OPMODE_NORMAL_NIC_WITH_FLASH		0x01
#define OPMODE_HW_CONFIG_MODE			0x02
#define OPMODE_DFU_MODE_WITH_FLASH		0x03
#define OPMODE_NORMAL_NIC_WITHOUT_FLASH		0x04

#define CMD_SET_MIB		0x01
#define CMD_GET_MIB		0x02
#define CMD_SCAN		0x03
#define CMD_JOIN		0x04
#define CMD_START_IBSS		0x05
#define CMD_RADIO_ON		0x06
#define CMD_RADIO_OFF		0x07
#define CMD_STARTUP		0x0B

#define MIB_LOCAL		0x01
#define MIB_MAC_ADDR		0x02
#define MIB_MAC			0x03
#define MIB_MAC_MGMT		0x05
#define MIB_MAC_WEP		0x06
#define MIB_PHY			0x07
#define MIB_FW_VERSION		0x08
#define MIB_MDOMAIN		0x09

#define ADHOC_MODE		1
#define INFRASTRUCTURE_MODE	2

/* values for struct mib_local, field preamble_type */
#define PREAMBLE_TYPE_LONG	0
#define PREAMBLE_TYPE_SHORT	1
#define PREAMBLE_TYPE_AUTO	2

/* values for tx_rate */
#define TX_RATE_1MBIT		0
#define TX_RATE_2MBIT		1
#define TX_RATE_5_5MBIT 	2
#define TX_RATE_11MBIT		3
#define TX_RATE_AUTO		4

/* power management modes */
#define AT76_PM_OFF		1
#define AT76_PM_ON		2
#define AT76_PM_SMART		3

struct hwcfg_r505 {
	u8 cr39_values[14];
	u8 reserved1[14];
	u8 bb_cr[14];
	u8 pidvid[4];
	u8 mac_addr[ETH_ALEN];
	u8 regulatory_domain;
	u8 reserved2[14];
	u8 cr15_values[14];
	u8 reserved3[3];
} __attribute__((packed));

struct hwcfg_rfmd {
	u8 cr20_values[14];
	u8 cr21_values[14];
	u8 bb_cr[14];
	u8 pidvid[4];
	u8 mac_addr[ETH_ALEN];
	u8 regulatory_domain;
	u8 low_power_values[14];
	u8 normal_power_values[14];
	u8 reserved1[3];
} __attribute__((packed));

struct hwcfg_intersil {
	u8 mac_addr[ETH_ALEN];
	u8 cr31_values[14];
	u8 cr58_values[14];
	u8 pidvid[4];
	u8 regulatory_domain;
	u8 reserved[1];
} __attribute__((packed));

union at76_hwcfg {
	struct hwcfg_intersil i;
	struct hwcfg_rfmd r3;
	struct hwcfg_r505 r5;
};

#define WEP_SMALL_KEY_LEN	(40 / 8)
#define WEP_LARGE_KEY_LEN	(104 / 8)

struct at76_card_config {
	u8 exclude_unencrypted;
	u8 promiscuous_mode;
	u8 short_retry_limit;
	u8 encryption_type;
	__le16 rts_threshold;
	__le16 fragmentation_threshold;	/* 256..2346 */
	u8 basic_rate_set[4];
	u8 auto_rate_fallback;	/* 0,1 */
	u8 channel;
	u8 privacy_invoked;
	u8 wep_default_key_id;	/* 0..3 */
	u8 current_ssid[32];
	u8 wep_default_key_value[4][WEP_KEY_LEN];
	u8 ssid_len;
	u8 short_preamble;
	__le16 beacon_period;
} __attribute__((packed));

struct at76_command {
	u8 cmd;
	u8 reserved;
	__le16 size;
	u8 data[0];
} __attribute__((packed));

/* Length of Atmel-specific Rx header before 802.11 frame */
#define AT76_RX_HDRLEN offsetof(struct at76_rx_buffer, packet)

struct at76_rx_buffer {
	__le16 wlength;
	u8 rx_rate;
	u8 newbss;
	u8 fragmentation;
	u8 rssi;
	u8 link_quality;
	u8 noise_level;
	__le32 rx_time;
	u8 packet[IEEE80211_FRAME_LEN + IEEE80211_FCS_LEN];
} __attribute__((packed));

/* Length of Atmel-specific Tx header before 802.11 frame */
#define AT76_TX_HDRLEN offsetof(struct at76_tx_buffer, packet)

struct at76_tx_buffer {
	__le16 wlength;
	u8 tx_rate;
	u8 padding;
	u8 reserved[4];
	u8 packet[IEEE80211_FRAME_LEN + IEEE80211_FCS_LEN];
} __attribute__((packed));

/* defines for scan_type below */
#define SCAN_TYPE_ACTIVE	0
#define SCAN_TYPE_PASSIVE	1

struct at76_req_scan {
	u8 bssid[ETH_ALEN];
	u8 essid[32];
	u8 scan_type;
	u8 channel;
	__le16 probe_delay;
	__le16 min_channel_time;
	__le16 max_channel_time;
	u8 essid_size;
	u8 international_scan;
} __attribute__((packed));

struct at76_req_ibss {
	u8 bssid[ETH_ALEN];
	u8 essid[32];
	u8 bss_type;
	u8 channel;
	u8 essid_size;
	u8 reserved[3];
} __attribute__((packed));

struct at76_req_join {
	u8 bssid[ETH_ALEN];
	u8 essid[32];
	u8 bss_type;
	u8 channel;
	__le16 timeout;
	u8 essid_size;
	u8 reserved;
} __attribute__((packed));

struct set_mib_buffer {
	u8 type;
	u8 size;
	u8 index;
	u8 reserved;
	union {
		u8 byte;
		__le16 word;
		u8 addr[ETH_ALEN];
	} data;
} __attribute__((packed));

struct mib_local {
	u16 reserved0;
	u8 beacon_enable;
	u8 txautorate_fallback;
	u8 reserved1;
	u8 ssid_size;
	u8 promiscuous_mode;
	u16 reserved2;
	u8 preamble_type;
	u16 reserved3;
} __attribute__((packed));

struct mib_mac_addr {
	u8 mac_addr[ETH_ALEN];
	u8 res[2];		/* ??? */
	u8 group_addr[4][ETH_ALEN];
	u8 group_addr_status[4];
} __attribute__((packed));

struct mib_mac {
	__le32 max_tx_msdu_lifetime;
	__le32 max_rx_lifetime;
	__le16 frag_threshold;
	__le16 rts_threshold;
	__le16 cwmin;
	__le16 cwmax;
	u8 short_retry_time;
	u8 long_retry_time;
	u8 scan_type;		/* active or passive */
	u8 scan_channel;
	__le16 probe_delay;	/* delay before ProbeReq in active scan, RO */
	__le16 min_channel_time;
	__le16 max_channel_time;
	__le16 listen_interval;
	u8 desired_ssid[32];
	u8 desired_bssid[ETH_ALEN];
	u8 desired_bsstype;	/* ad-hoc or infrastructure */
	u8 reserved2;
} __attribute__((packed));

struct mib_mac_mgmt {
	__le16 beacon_period;
	__le16 CFP_max_duration;
	__le16 medium_occupancy_limit;
	__le16 station_id;	/* assoc id */
	__le16 ATIM_window;
	u8 CFP_mode;
	u8 privacy_option_implemented;
	u8 DTIM_period;
	u8 CFP_period;
	u8 current_bssid[ETH_ALEN];
	u8 current_essid[32];
	u8 current_bss_type;
	u8 power_mgmt_mode;
	/* rfmd and 505 */
	u8 ibss_change;
	u8 res;
	u8 multi_domain_capability_implemented;
	u8 multi_domain_capability_enabled;
	u8 country_string[3];
	u8 reserved[3];
} __attribute__((packed));

struct mib_mac_wep {
	u8 privacy_invoked;	/* 0 disable encr., 1 enable encr */
	u8 wep_default_key_id;
	u8 wep_key_mapping_len;
	u8 exclude_unencrypted;
	__le32 wep_icv_error_count;
	__le32 wep_excluded_count;
	u8 wep_default_keyvalue[WEP_KEYS][WEP_KEY_LEN];
	u8 encryption_level;	/* 1 for 40bit, 2 for 104bit encryption */
} __attribute__((packed));

struct mib_phy {
	__le32 ed_threshold;

	__le16 slot_time;
	__le16 sifs_time;
	__le16 preamble_length;
	__le16 plcp_header_length;
	__le16 mpdu_max_length;
	__le16 cca_mode_supported;

	u8 operation_rate_set[4];
	u8 channel_id;
	u8 current_cca_mode;
	u8 phy_type;
	u8 current_reg_domain;
} __attribute__((packed));

struct mib_fw_version {
	u8 major;
	u8 minor;
	u8 patch;
	u8 build;
} __attribute__((packed));

struct mib_mdomain {
	u8 tx_powerlevel[14];
	u8 channel_list[14];	/* 0 for invalid channels */
} __attribute__((packed));

struct at76_fw_header {
	__le32 crc;		/* CRC32 of the whole image */
	__le32 board_type;	/* firmware compatibility code */
	u8 build;		/* firmware build number */
	u8 patch;		/* firmware patch level */
	u8 minor;		/* firmware minor version */
	u8 major;		/* firmware major version */
	__le32 str_offset;	/* offset of the copyright string */
	__le32 int_fw_offset;	/* internal firmware image offset */
	__le32 int_fw_len;	/* internal firmware image length */
	__le32 ext_fw_offset;	/* external firmware image offset */
	__le32 ext_fw_len;	/* external firmware image length */
} __attribute__((packed));

enum mac_state {
	MAC_INIT,
	MAC_SCANNING,
	MAC_AUTH,
	MAC_ASSOC,
	MAC_JOINING,
	MAC_CONNECTED,
	MAC_OWN_IBSS
};

/* a description of a regulatory domain and the allowed channels */
struct reg_domain {
	u16 code;
	char const *name;
	u32 channel_map;	/* if bit N is set, channel (N+1) is allowed */
};

/* how long do we keep a (I)BSS in the bss_list in jiffies
   this should be long enough for the user to retrieve the table
   (by iwlist ?) after the device started, because all entries from
   other channels than the one the device locks on get removed, too */
#define BSS_LIST_TIMEOUT	(120 * HZ)
/* struct to store BSS info found during scan */
#define BSS_LIST_MAX_RATE_LEN	32	/* 32 rates should be enough ... */

struct bss_info {
	struct list_head list;

	u8 bssid[ETH_ALEN];	/* bssid */
	u8 ssid[IW_ESSID_MAX_SIZE];	/* essid */
	u8 ssid_len;		/* length of ssid above */
	u8 channel;
	u16 capa;		/* BSS capabilities */
	u16 beacon_interval;	/* beacon interval, Kus (1024 microseconds) */
	u8 rates[BSS_LIST_MAX_RATE_LEN];	/* supported rates in units of
						   500 kbps, ORed with 0x80 for
						   basic rates */
	u8 rates_len;

	/* quality of received beacon */
	u8 rssi;
	u8 link_qual;
	u8 noise_level;

	unsigned long last_rx;	/* time (jiffies) of last beacon received */
};

/* a rx data buffer to collect rx fragments */
struct rx_data_buf {
	u8 sender[ETH_ALEN];	/* sender address */
	u16 seqnr;		/* sequence number */
	u16 fragnr;		/* last fragment received */
	unsigned long last_rx;	/* jiffies of last rx */
	struct sk_buff *skb;	/* == NULL if entry is free */
};

#define NR_RX_DATA_BUF		8

/* Data for one loaded firmware file */
struct fwentry {
	const char *const fwname;
	const struct firmware *fw;
	int extfw_size;
	int intfw_size;
	/* pointer to loaded firmware, no need to free */
	u8 *extfw;		/* external firmware, extfw_size bytes long */
	u8 *intfw;		/* internal firmware, intfw_size bytes long */
	enum board_type board_type;	/* board type */
	struct mib_fw_version fw_version;
	int loaded;		/* Loaded and parsed successfully */
};

struct at76_priv {
	struct usb_device *udev;	/* USB device pointer */
	struct net_device *netdev;	/* net device pointer */
	struct net_device_stats stats;	/* net device stats */
	struct iw_statistics wstats;	/* wireless stats */

	struct sk_buff *rx_skb;	/* skbuff for receiving data */
	void *bulk_out_buffer;	/* buffer for sending data */

	struct urb *tx_urb;	/* URB for sending data */
	struct urb *rx_urb;	/* URB for receiving data */

	unsigned int tx_pipe;	/* bulk out pipe */
	unsigned int rx_pipe;	/* bulk in pipe */

	struct mutex mtx;	/* locks this structure */

	/* work queues */
	struct work_struct work_assoc_done;
	struct work_struct work_join;
	struct work_struct work_new_bss;
	struct work_struct work_start_scan;
	struct work_struct work_set_promisc;
	struct work_struct work_submit_rx;
	struct delayed_work dwork_restart;
	struct delayed_work dwork_get_scan;
	struct delayed_work dwork_beacon;
	struct delayed_work dwork_auth;
	struct delayed_work dwork_assoc;

	struct tasklet_struct rx_tasklet;

	/* the WEP stuff */
	int wep_enabled;	/* 1 if WEP is enabled */
	int wep_key_id;		/* key id to be used */
	u8 wep_keys[WEP_KEYS][WEP_KEY_LEN];	/* the four WEP keys,
						   5 or 13 bytes are used */
	u8 wep_keys_len[WEP_KEYS];	/* the length of the above keys */

	int channel;
	int iw_mode;
	u8 bssid[ETH_ALEN];
	u8 essid[IW_ESSID_MAX_SIZE];
	int essid_size;
	int radio_on;
	int promisc;

	int preamble_type;	/* 0 - long, 1 - short, 2 - auto */
	int auth_mode;		/* authentication type: 0 open, 1 shared key */
	int txrate;		/* 0,1,2,3 = 1,2,5.5,11 Mbps, 4 is auto */
	int frag_threshold;	/* threshold for fragmentation of tx packets */
	int rts_threshold;	/* threshold for RTS mechanism */
	int short_retry_limit;

	int scan_min_time;	/* scan min channel time */
	int scan_max_time;	/* scan max channel time */
	int scan_mode;		/* SCAN_TYPE_ACTIVE, SCAN_TYPE_PASSIVE */
	int scan_need_any;	/* if set, need to scan for any ESSID */

	/* the list we got from scanning */
	spinlock_t bss_list_spinlock;	/* protects bss_list operations */
	struct list_head bss_list;	/* list of BSS we got beacons from */
	struct timer_list bss_list_timer;	/* timer to purge old entries
						   from bss_list */
	struct bss_info *curr_bss;	/* current BSS */
	u16 assoc_id;		/* current association ID, if associated */

	u8 wanted_bssid[ETH_ALEN];
	int wanted_bssid_valid;	/* != 0 if wanted_bssid is to be used */

	/* some data for infrastructure mode only */
	spinlock_t mgmt_spinlock;	/* this spinlock protects access to
					   next_mgmt_bulk */

	struct at76_tx_buffer *next_mgmt_bulk;	/* pending management msg to
						   send via bulk out */
	enum mac_state mac_state;
	enum {
		SCAN_IDLE,
		SCAN_IN_PROGRESS,
		SCAN_COMPLETED
	} scan_state;
	time_t last_scan;

	int retries;		/* remaining retries in case of timeout when
				 * sending AuthReq or AssocReq */
	u8 pm_mode;		/* power management mode */
	u32 pm_period;		/* power management period in microseconds */

	struct reg_domain const *domain;	/* reg domain description */

	/* iwspy support */
	spinlock_t spy_spinlock;
	struct iw_spy_data spy_data;

	struct iw_public_data wireless_data;

	/* These fields contain HW config provided by the device (not all of
	 * these fields are used by all board types) */
	u8 mac_addr[ETH_ALEN];
	u8 regulatory_domain;

	struct at76_card_config card_config;

	/* store rx fragments until complete */
	struct rx_data_buf rx_data[NR_RX_DATA_BUF];

	enum board_type board_type;
	struct mib_fw_version fw_version;

	unsigned int device_unplugged:1;
	unsigned int netdev_registered:1;
	struct set_mib_buffer mib_buf;	/* global buffer for set_mib calls */

	/* beacon counting */
	int beacon_period;	/* period of mgmt beacons, Kus */
	int beacons_received;
	unsigned long beacons_last_qual;	/* time we restarted counting
						   beacons */
};

struct at76_rx_radiotap {
	struct ieee80211_radiotap_header rt_hdr;
	__le64 rt_tsft;
	u8 rt_flags;
	u8 rt_rate;
	s8 rt_signal;
	s8 rt_noise;
};

#define AT76_RX_RADIOTAP_PRESENT		  \
	((1 << IEEE80211_RADIOTAP_TSFT)		| \
	(1 << IEEE80211_RADIOTAP_FLAGS)		| \
	(1 << IEEE80211_RADIOTAP_RATE)		| \
	(1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL)	| \
	(1 << IEEE80211_RADIOTAP_DB_ANTNOISE))

#define BEACON_MAX_DATA_LENGTH	1500

/* the maximum size of an AssocReq packet */
#define ASSOCREQ_MAX_SIZE \
  (AT76_TX_HDRLEN + sizeof(struct ieee80211_assoc_request) + \
   1 + 1 + IW_ESSID_MAX_SIZE + 1 + 1 + 4)

/* for shared secret auth, add the challenge text size */
#define AUTH_FRAME_SIZE (AT76_TX_HDRLEN + sizeof(struct ieee80211_auth))

/* Maximal number of AuthReq retries */
#define AUTH_RETRIES		3

/* Maximal number of AssocReq retries */
#define ASSOC_RETRIES		3

/* Beacon timeout in managed mode when we are connected */
#define BEACON_TIMEOUT		(10 * HZ)

/* Timeout for authentication response */
#define AUTH_TIMEOUT		(1 * HZ)

/* Timeout for association response */
#define ASSOC_TIMEOUT		(1 * HZ)

/* Polling interval when scan is running */
#define SCAN_POLL_INTERVAL	(HZ / 4)

/* Command completion timeout */
#define CMD_COMPLETION_TIMEOUT	(5 * HZ)

#define DEF_RTS_THRESHOLD	1536
#define DEF_FRAG_THRESHOLD	1536
#define DEF_SHORT_RETRY_LIMIT	8
#define DEF_CHANNEL		10
#define DEF_SCAN_MIN_TIME	10
#define DEF_SCAN_MAX_TIME	120

#define MAX_RTS_THRESHOLD	(MAX_FRAG_THRESHOLD + 1)

/* the max padding size for tx in bytes (see calc_padding) */
#define MAX_PADDING_SIZE	53

#endif				/* _AT76_USB_H */
