#ifndef __WL3501_H__
#define __WL3501_H__

#include <linux/spinlock.h>
#include <net/ieee80211.h>

/* define for WLA 2.0 */
#define WL3501_BLKSZ 256
/*
 * ID for input Signals of DRIVER block
 * bit[7-5] is block ID: 000
 * bit[4-0] is signal ID
*/
enum wl3501_signals {
	WL3501_SIG_ALARM,
	WL3501_SIG_MD_CONFIRM,
	WL3501_SIG_MD_IND,
	WL3501_SIG_ASSOC_CONFIRM,
	WL3501_SIG_ASSOC_IND,
	WL3501_SIG_AUTH_CONFIRM,
	WL3501_SIG_AUTH_IND,
	WL3501_SIG_DEAUTH_CONFIRM,
	WL3501_SIG_DEAUTH_IND,
	WL3501_SIG_DISASSOC_CONFIRM,
	WL3501_SIG_DISASSOC_IND,
	WL3501_SIG_GET_CONFIRM,
	WL3501_SIG_JOIN_CONFIRM,
	WL3501_SIG_PWR_MGMT_CONFIRM,
	WL3501_SIG_REASSOC_CONFIRM,
	WL3501_SIG_REASSOC_IND,
	WL3501_SIG_SCAN_CONFIRM,
	WL3501_SIG_SET_CONFIRM,
	WL3501_SIG_START_CONFIRM,
	WL3501_SIG_RESYNC_CONFIRM,
	WL3501_SIG_SITE_CONFIRM,
	WL3501_SIG_SAVE_CONFIRM,
	WL3501_SIG_RFTEST_CONFIRM,
/*
 * ID for input Signals of MLME block
 * bit[7-5] is block ID: 010
 * bit[4-0] is signal ID
 */
	WL3501_SIG_ASSOC_REQ = 0x20,
	WL3501_SIG_AUTH_REQ,
	WL3501_SIG_DEAUTH_REQ,
	WL3501_SIG_DISASSOC_REQ,
	WL3501_SIG_GET_REQ,
	WL3501_SIG_JOIN_REQ,
	WL3501_SIG_PWR_MGMT_REQ,
	WL3501_SIG_REASSOC_REQ,
	WL3501_SIG_SCAN_REQ,
	WL3501_SIG_SET_REQ,
	WL3501_SIG_START_REQ,
	WL3501_SIG_MD_REQ,
	WL3501_SIG_RESYNC_REQ,
	WL3501_SIG_SITE_REQ,
	WL3501_SIG_SAVE_REQ,
	WL3501_SIG_RF_TEST_REQ,
	WL3501_SIG_MM_CONFIRM = 0x60,
	WL3501_SIG_MM_IND,
};

enum wl3501_mib_attribs {
	WL3501_MIB_ATTR_STATION_ID,
	WL3501_MIB_ATTR_AUTH_ALGORITHMS,
	WL3501_MIB_ATTR_AUTH_TYPE,
	WL3501_MIB_ATTR_MEDIUM_OCCUPANCY_LIMIT,
	WL3501_MIB_ATTR_CF_POLLABLE,
	WL3501_MIB_ATTR_CFP_PERIOD,
	WL3501_MIB_ATTR_CFPMAX_DURATION,
	WL3501_MIB_ATTR_AUTH_RESP_TMOUT,
	WL3501_MIB_ATTR_RX_DTIMS,
	WL3501_MIB_ATTR_PRIV_OPT_IMPLEMENTED,
	WL3501_MIB_ATTR_PRIV_INVOKED,
	WL3501_MIB_ATTR_WEP_DEFAULT_KEYS,
	WL3501_MIB_ATTR_WEP_DEFAULT_KEY_ID,
	WL3501_MIB_ATTR_WEP_KEY_MAPPINGS,
	WL3501_MIB_ATTR_WEP_KEY_MAPPINGS_LEN,
	WL3501_MIB_ATTR_EXCLUDE_UNENCRYPTED,
	WL3501_MIB_ATTR_WEP_ICV_ERROR_COUNT,
	WL3501_MIB_ATTR_WEP_UNDECRYPTABLE_COUNT,
	WL3501_MIB_ATTR_WEP_EXCLUDED_COUNT,
	WL3501_MIB_ATTR_MAC_ADDR,
	WL3501_MIB_ATTR_GROUP_ADDRS,
	WL3501_MIB_ATTR_RTS_THRESHOLD,
	WL3501_MIB_ATTR_SHORT_RETRY_LIMIT,
	WL3501_MIB_ATTR_LONG_RETRY_LIMIT,
	WL3501_MIB_ATTR_FRAG_THRESHOLD,
	WL3501_MIB_ATTR_MAX_TX_MSDU_LIFETIME,
	WL3501_MIB_ATTR_MAX_RX_LIFETIME,
	WL3501_MIB_ATTR_MANUFACTURER_ID,
	WL3501_MIB_ATTR_PRODUCT_ID,
	WL3501_MIB_ATTR_TX_FRAG_COUNT,
	WL3501_MIB_ATTR_MULTICAST_TX_FRAME_COUNT,
	WL3501_MIB_ATTR_FAILED_COUNT,
	WL3501_MIB_ATTR_RX_FRAG_COUNT,
	WL3501_MIB_ATTR_MULTICAST_RX_COUNT,
	WL3501_MIB_ATTR_FCS_ERROR_COUNT,
	WL3501_MIB_ATTR_RETRY_COUNT,
	WL3501_MIB_ATTR_MULTIPLE_RETRY_COUNT,
	WL3501_MIB_ATTR_RTS_SUCCESS_COUNT,
	WL3501_MIB_ATTR_RTS_FAILURE_COUNT,
	WL3501_MIB_ATTR_ACK_FAILURE_COUNT,
	WL3501_MIB_ATTR_FRAME_DUPLICATE_COUNT,
	WL3501_MIB_ATTR_PHY_TYPE,
	WL3501_MIB_ATTR_REG_DOMAINS_SUPPORT,
	WL3501_MIB_ATTR_CURRENT_REG_DOMAIN,
	WL3501_MIB_ATTR_SLOT_TIME,
	WL3501_MIB_ATTR_CCA_TIME,
	WL3501_MIB_ATTR_RX_TX_TURNAROUND_TIME,
	WL3501_MIB_ATTR_TX_PLCP_DELAY,
	WL3501_MIB_ATTR_RX_TX_SWITCH_TIME,
	WL3501_MIB_ATTR_TX_RAMP_ON_TIME,
	WL3501_MIB_ATTR_TX_RF_DELAY,
	WL3501_MIB_ATTR_SIFS_TIME,
	WL3501_MIB_ATTR_RX_RF_DELAY,
	WL3501_MIB_ATTR_RX_PLCP_DELAY,
	WL3501_MIB_ATTR_MAC_PROCESSING_DELAY,
	WL3501_MIB_ATTR_TX_RAMP_OFF_TIME,
	WL3501_MIB_ATTR_PREAMBLE_LEN,
	WL3501_MIB_ATTR_PLCP_HEADER_LEN,
	WL3501_MIB_ATTR_MPDU_DURATION_FACTOR,
	WL3501_MIB_ATTR_AIR_PROPAGATION_TIME,
	WL3501_MIB_ATTR_TEMP_TYPE,
	WL3501_MIB_ATTR_CW_MIN,
	WL3501_MIB_ATTR_CW_MAX,
	WL3501_MIB_ATTR_SUPPORT_DATA_RATES_TX,
	WL3501_MIB_ATTR_SUPPORT_DATA_RATES_RX,
	WL3501_MIB_ATTR_MPDU_MAX_LEN,
	WL3501_MIB_ATTR_SUPPORT_TX_ANTENNAS,
	WL3501_MIB_ATTR_CURRENT_TX_ANTENNA,
	WL3501_MIB_ATTR_SUPPORT_RX_ANTENNAS,
	WL3501_MIB_ATTR_DIVERSITY_SUPPORT,
	WL3501_MIB_ATTR_DIVERSITY_SELECTION_RS,
	WL3501_MIB_ATTR_NR_SUPPORTED_PWR_LEVELS,
	WL3501_MIB_ATTR_TX_PWR_LEVEL1,
	WL3501_MIB_ATTR_TX_PWR_LEVEL2,
	WL3501_MIB_ATTR_TX_PWR_LEVEL3,
	WL3501_MIB_ATTR_TX_PWR_LEVEL4,
	WL3501_MIB_ATTR_TX_PWR_LEVEL5,
	WL3501_MIB_ATTR_TX_PWR_LEVEL6,
	WL3501_MIB_ATTR_TX_PWR_LEVEL7,
	WL3501_MIB_ATTR_TX_PWR_LEVEL8,
	WL3501_MIB_ATTR_CURRENT_TX_PWR_LEVEL,
	WL3501_MIB_ATTR_CURRENT_CHAN,
	WL3501_MIB_ATTR_CCA_MODE_SUPPORTED,
	WL3501_MIB_ATTR_CURRENT_CCA_MODE,
	WL3501_MIB_ATTR_ED_THRESHOLD,
	WL3501_MIB_ATTR_SINTHESIZER_LOCKED,
	WL3501_MIB_ATTR_CURRENT_PWR_STATE,
	WL3501_MIB_ATTR_DOZE_TURNON_TIME,
	WL3501_MIB_ATTR_RCR33,
	WL3501_MIB_ATTR_DEFAULT_CHAN,
	WL3501_MIB_ATTR_SSID,
	WL3501_MIB_ATTR_PWR_MGMT_ENABLE,
	WL3501_MIB_ATTR_NET_CAPABILITY,
	WL3501_MIB_ATTR_ROUTING,
};

enum wl3501_net_type {
	WL3501_NET_TYPE_INFRA,
	WL3501_NET_TYPE_ADHOC,
	WL3501_NET_TYPE_ANY_BSS,
};

enum wl3501_scan_type {
	WL3501_SCAN_TYPE_ACTIVE,
	WL3501_SCAN_TYPE_PASSIVE,
};

enum wl3501_tx_result {
	WL3501_TX_RESULT_SUCCESS,
	WL3501_TX_RESULT_NO_BSS,
	WL3501_TX_RESULT_RETRY_LIMIT,
};

enum wl3501_sys_type {
	WL3501_SYS_TYPE_OPEN,
	WL3501_SYS_TYPE_SHARE_KEY,
};

enum wl3501_status {
	WL3501_STATUS_SUCCESS,
	WL3501_STATUS_INVALID,
	WL3501_STATUS_TIMEOUT,
	WL3501_STATUS_REFUSED,
	WL3501_STATUS_MANY_REQ,
	WL3501_STATUS_ALREADY_BSS,
};

#define WL3501_MGMT_CAPABILITY_ESS		0x0001  /* see 802.11 p.58 */
#define WL3501_MGMT_CAPABILITY_IBSS		0x0002  /*      - " -	   */
#define WL3501_MGMT_CAPABILITY_CF_POLLABLE	0x0004  /*      - " -	   */
#define WL3501_MGMT_CAPABILITY_CF_POLL_REQUEST	0x0008  /*      - " -	   */
#define WL3501_MGMT_CAPABILITY_PRIVACY		0x0010  /*      - " -	   */

#define IW_REG_DOMAIN_FCC	0x10	/* Channel 1 to 11	USA    */
#define IW_REG_DOMAIN_DOC	0x20	/* Channel 1 to 11	Canada */
#define IW_REG_DOMAIN_ETSI	0x30	/* Channel 1 to 13	Europe */
#define IW_REG_DOMAIN_SPAIN	0x31	/* Channel 10 to 11	Spain  */
#define IW_REG_DOMAIN_FRANCE	0x32	/* Channel 10 to 13	France */
#define IW_REG_DOMAIN_MKK	0x40	/* Channel 14		Japan  */
#define IW_REG_DOMAIN_MKK1	0x41	/* Channel 1-14		Japan  */
#define IW_REG_DOMAIN_ISRAEL	0x50	/* Channel 3 - 9	Israel */

#define IW_MGMT_RATE_LABEL_MANDATORY 128 /* MSB */

enum iw_mgmt_rate_labels {
	IW_MGMT_RATE_LABEL_1MBIT   = 2,
	IW_MGMT_RATE_LABEL_2MBIT   = 4,
	IW_MGMT_RATE_LABEL_5_5MBIT = 11,
	IW_MGMT_RATE_LABEL_11MBIT  = 22,
};

enum iw_mgmt_info_element_ids {
	IW_MGMT_INFO_ELEMENT_SSID,		  /* Service Set Identity */
	IW_MGMT_INFO_ELEMENT_SUPPORTED_RATES,
	IW_MGMT_INFO_ELEMENT_FH_PARAMETER_SET,
	IW_MGMT_INFO_ELEMENT_DS_PARAMETER_SET,
	IW_MGMT_INFO_ELEMENT_CS_PARAMETER_SET,
	IW_MGMT_INFO_ELEMENT_CS_TIM,		  /* Traffic Information Map */
	IW_MGMT_INFO_ELEMENT_IBSS_PARAMETER_SET,
	/* 7-15: Reserved, unused */
	IW_MGMT_INFO_ELEMENT_CHALLENGE_TEXT = 16,
	/* 17-31 Reserved for challenge text extension */
	/* 32-255 Reserved, unused */
};

struct iw_mgmt_info_element {
	u8 id; /* one of enum iw_mgmt_info_element_ids,
		  but sizeof(enum) > sizeof(u8) :-( */
	u8 len;
	u8 data[0];
} __attribute__ ((packed));

struct iw_mgmt_essid_pset {
	struct iw_mgmt_info_element el;
	u8 			    essid[IW_ESSID_MAX_SIZE];
} __attribute__ ((packed));

/*
 * According to 802.11 Wireless Netowors, the definitive guide - O'Reilly
 * Pg 75
 */ 
#define IW_DATA_RATE_MAX_LABELS 8

struct iw_mgmt_data_rset {
	struct iw_mgmt_info_element el;
	u8 			    data_rate_labels[IW_DATA_RATE_MAX_LABELS];
} __attribute__ ((packed));

struct iw_mgmt_ds_pset {
	struct iw_mgmt_info_element el;
	u8 			    chan;
} __attribute__ ((packed));

struct iw_mgmt_cf_pset {
	struct iw_mgmt_info_element el;
	u8 			    cfp_count;
	u8 			    cfp_period;
	u16 			    cfp_max_duration;
	u16 			    cfp_dur_remaining;
} __attribute__ ((packed));

struct iw_mgmt_ibss_pset {
	struct iw_mgmt_info_element el;
	u16 			    atim_window;
} __attribute__ ((packed));

struct wl3501_tx_hdr {
	u16	tx_cnt;
	u8	sync[16];
	u16	sfd;
	u8	signal;
	u8	service;
	u16	len;
	u16	crc16;
	u16	frame_ctrl;
	u16	duration_id;
	u8	addr1[ETH_ALEN];
	u8	addr2[ETH_ALEN];
	u8	addr3[ETH_ALEN];
	u16	seq_ctrl;
	u8	addr4[ETH_ALEN];
};

struct wl3501_rx_hdr {
	u16	rx_next_blk;
	u16	rc_next_frame_blk;
	u8	rx_blk_ctrl;
	u8	rx_next_frame;
	u8	rx_next_frame1;
	u8	rssi;
	char	time[8];
	u8	signal;
	u8	service;
	u16	len;
	u16	crc16;
	u16	frame_ctrl;
	u16	duration;
	u8	addr1[ETH_ALEN];
	u8	addr2[ETH_ALEN];
	u8	addr3[ETH_ALEN];
	u16	seq;
	u8	addr4[ETH_ALEN];
};

struct wl3501_start_req {
	u16			    next_blk;
	u8			    sig_id;
	u8			    bss_type;
	u16			    beacon_period;
	u16			    dtim_period;
	u16			    probe_delay;
	u16			    cap_info;
	struct iw_mgmt_essid_pset   ssid;
	struct iw_mgmt_data_rset    bss_basic_rset;
	struct iw_mgmt_data_rset    operational_rset;
	struct iw_mgmt_cf_pset	    cf_pset;
	struct iw_mgmt_ds_pset	    ds_pset;
	struct iw_mgmt_ibss_pset    ibss_pset;
};

struct wl3501_assoc_req {
	u16	next_blk;
	u8	sig_id;
	u8	reserved;
	u16	timeout;
	u16	cap_info;
	u16	listen_interval;
	u8	mac_addr[ETH_ALEN];
};

struct wl3501_assoc_confirm {
	u16	next_blk;
	u8	sig_id;
	u8	reserved;
	u16	status;
};

struct wl3501_assoc_ind {
	u16	next_blk;
	u8	sig_id;
	u8	mac_addr[ETH_ALEN];
};

struct wl3501_auth_req {
	u16	next_blk;
	u8	sig_id;
	u8	reserved;
	u16	type;
	u16	timeout;
	u8	mac_addr[ETH_ALEN];
};

struct wl3501_auth_confirm {
	u16	next_blk;
	u8	sig_id;
	u8	reserved;
	u16	type;
	u16	status;
	u8	mac_addr[ETH_ALEN];
};

struct wl3501_get_req {
	u16	next_blk;
	u8	sig_id;
	u8	reserved;
	u16	mib_attrib;
};

struct wl3501_get_confirm {
	u16	next_blk;
	u8	sig_id;
	u8	reserved;
	u16	mib_status;
	u16	mib_attrib;
	u8	mib_value[100];
};

struct wl3501_join_req {
	u16			    next_blk;
	u8			    sig_id;
	u8			    reserved;
	struct iw_mgmt_data_rset    operational_rset;
	u16			    reserved2;
	u16			    timeout;
	u16			    probe_delay;
	u8			    timestamp[8];
	u8			    local_time[8];
	u16			    beacon_period;
	u16			    dtim_period;
	u16			    cap_info;
	u8			    bss_type;
	u8			    bssid[ETH_ALEN];
	struct iw_mgmt_essid_pset   ssid;
	struct iw_mgmt_ds_pset	    ds_pset;
	struct iw_mgmt_cf_pset	    cf_pset;
	struct iw_mgmt_ibss_pset    ibss_pset;
	struct iw_mgmt_data_rset    bss_basic_rset;
};

struct wl3501_join_confirm {
	u16	next_blk;
	u8	sig_id;
	u8	reserved;
	u16	status;
};

struct wl3501_pwr_mgmt_req {
	u16	next_blk;
	u8	sig_id;
	u8	pwr_save;
	u8	wake_up;
	u8	receive_dtims;
};

struct wl3501_pwr_mgmt_confirm {
	u16	next_blk;
	u8	sig_id;
	u8	reserved;
	u16	status;
};

struct wl3501_scan_req {
	u16			    next_blk;
	u8			    sig_id;
	u8			    bss_type;
	u16			    probe_delay;
	u16			    min_chan_time;
	u16			    max_chan_time;
	u8			    chan_list[14];
	u8			    bssid[ETH_ALEN];
	struct iw_mgmt_essid_pset   ssid;
	enum wl3501_scan_type	    scan_type;
};

struct wl3501_scan_confirm {
	u16			    next_blk;
	u8			    sig_id;
	u8			    reserved;
	u16			    status;
	char			    timestamp[8];
	char			    localtime[8];
	u16			    beacon_period;
	u16			    dtim_period;
	u16			    cap_info;
	u8			    bss_type;
	u8			    bssid[ETH_ALEN];
	struct iw_mgmt_essid_pset   ssid;
	struct iw_mgmt_ds_pset	    ds_pset;
	struct iw_mgmt_cf_pset	    cf_pset;
	struct iw_mgmt_ibss_pset    ibss_pset;
	struct iw_mgmt_data_rset    bss_basic_rset;
	u8			    rssi;
};

struct wl3501_start_confirm {
	u16	next_blk;
	u8	sig_id;
	u8	reserved;
	u16	status;
};

struct wl3501_md_req {
	u16	next_blk;
	u8	sig_id;
	u8	routing;
	u16	data;
	u16	size;
	u8	pri;
	u8	service_class;
	u8	daddr[ETH_ALEN];
	u8	saddr[ETH_ALEN];
};

struct wl3501_md_ind {
	u16	next_blk;
	u8	sig_id;
	u8	routing;
	u16	data;
	u16	size;
	u8	reception;
	u8	pri;
	u8	service_class;
	u8	daddr[ETH_ALEN];
	u8	saddr[ETH_ALEN];
};

struct wl3501_md_confirm {
	u16	next_blk;
	u8	sig_id;
	u8	reserved;
	u16	data;
	u8	status;
	u8	pri;
	u8	service_class;
};

struct wl3501_resync_req {
	u16	next_blk;
	u8	sig_id;
};

/* Definitions for supporting clone adapters. */
/* System Interface Registers (SIR space) */
#define WL3501_NIC_GCR ((u8)0x00)	/* SIR0 - General Conf Register */
#define WL3501_NIC_BSS ((u8)0x01)	/* SIR1 - Bank Switching Select Reg */
#define WL3501_NIC_LMAL ((u8)0x02)	/* SIR2 - Local Mem addr Reg [7:0] */
#define WL3501_NIC_LMAH ((u8)0x03)	/* SIR3 - Local Mem addr Reg [14:8] */
#define WL3501_NIC_IODPA ((u8)0x04)	/* SIR4 - I/O Data Port A */
#define WL3501_NIC_IODPB ((u8)0x05)	/* SIR5 - I/O Data Port B */
#define WL3501_NIC_IODPC ((u8)0x06)	/* SIR6 - I/O Data Port C */
#define WL3501_NIC_IODPD ((u8)0x07)	/* SIR7 - I/O Data Port D */

/* Bits in GCR */
#define WL3501_GCR_SWRESET ((u8)0x80)
#define WL3501_GCR_CORESET ((u8)0x40)
#define WL3501_GCR_DISPWDN ((u8)0x20)
#define WL3501_GCR_ECWAIT  ((u8)0x10)
#define WL3501_GCR_ECINT   ((u8)0x08)
#define WL3501_GCR_INT2EC  ((u8)0x04)
#define WL3501_GCR_ENECINT ((u8)0x02)
#define WL3501_GCR_DAM     ((u8)0x01)

/* Bits in BSS (Bank Switching Select Register) */
#define WL3501_BSS_FPAGE0 ((u8)0x20)	/* Flash memory page0 */
#define WL3501_BSS_FPAGE1 ((u8)0x28)
#define WL3501_BSS_FPAGE2 ((u8)0x30)
#define WL3501_BSS_FPAGE3 ((u8)0x38)
#define WL3501_BSS_SPAGE0 ((u8)0x00)	/* SRAM page0 */
#define WL3501_BSS_SPAGE1 ((u8)0x08)
#define WL3501_BSS_SPAGE2 ((u8)0x10)
#define WL3501_BSS_SPAGE3 ((u8)0x18)

/* Define Driver Interface */
/* Refer IEEE 802.11 */
/* Tx packet header, include PLCP and MPDU */
/* Tx PLCP Header */
struct wl3501_80211_tx_plcp_hdr {
	u8	sync[16];
	u16	sfd;
	u8	signal;
	u8	service;
	u16	len;
	u16	crc16;
} __attribute__ ((packed));

struct wl3501_80211_tx_hdr {
	struct wl3501_80211_tx_plcp_hdr	pclp_hdr;
	struct ieee80211_hdr_4addr		mac_hdr;
} __attribute__ ((packed));

/*
   Reserve the beginning Tx space for descriptor use.

   TxBlockOffset -->	*----*----*----*----* \
	(TxFreeDesc)	|  0 |  1 |  2 |  3 |  \
			|  4 |  5 |  6 |  7 |   |
			|  8 |  9 | 10 | 11 |   TX_DESC * 20
			| 12 | 13 | 14 | 15 |   |
			| 16 | 17 | 18 | 19 |  /
   TxBufferBegin -->	*----*----*----*----* /
   (TxBufferHead)	| 		    |
   (TxBufferTail)	| 		    |
			|    Send Buffer    |
			| 		    |
			|		    |
			*-------------------*
   TxBufferEnd    -------------------------/

*/

struct wl3501_card {
	int				base_addr;
	u8				mac_addr[ETH_ALEN];
	spinlock_t			lock;
	wait_queue_head_t		wait;
	struct wl3501_get_confirm	sig_get_confirm;
	struct wl3501_pwr_mgmt_confirm	sig_pwr_mgmt_confirm;
	u16				tx_buffer_size;
	u16				tx_buffer_head;
	u16				tx_buffer_tail;
	u16				tx_buffer_cnt;
	u16				esbq_req_start;
	u16				esbq_req_end;
	u16				esbq_req_head;
	u16				esbq_req_tail;
	u16				esbq_confirm_start;
	u16				esbq_confirm_end;
	u16				esbq_confirm;
	struct iw_mgmt_essid_pset  	essid;
	struct iw_mgmt_essid_pset  	keep_essid;
	u8				bssid[ETH_ALEN];
	int				net_type;
	char				nick[32];
	char				card_name[32];
	char				firmware_date[32];
	u8				chan;
	u8				cap_info;
	u16				start_seg;
	u16				bss_cnt;
	u16				join_sta_bss;
	u8				rssi;
	u8				adhoc_times;
	u8				reg_domain;
	u8				version[2];
	struct wl3501_scan_confirm	bss_set[20];
	struct net_device_stats 	stats;
	struct iw_statistics		wstats;
	struct iw_spy_data		spy_data;
	struct iw_public_data		wireless_data;
	struct dev_node_t		node;
	struct pcmcia_device		*p_dev;
};
#endif
