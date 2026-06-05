/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NXP Wireless LAN device driver: ioctl data structures & APIs
 *
 * Copyright 2011-2024 NXP
 */

#ifndef _NXPWIFI_CFG_H_
#define _NXPWIFI_CFG_H_

#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/ieee80211.h>
#include <uapi/linux/if_arp.h>
#include <net/cfg80211.h>

#define NUM_WEP_KEYS                 4

#define NXPWIFI_BSS_COEX_COUNT	     2
#define NXPWIFI_MAX_BSS_NUM         (3)

#define NXPWIFI_MAX_CSA_COUNTERS     5

#define NXPWIFI_DMA_ALIGN_SZ	    64
#define NXPWIFI_RX_HEADROOM	    64
#define MAX_TXPD_SZ		    32
#define INTF_HDR_ALIGN		     4
/* special FW 4 address management header */
#define NXPWIFI_MIN_DATA_HEADER_LEN (NXPWIFI_DMA_ALIGN_SZ + INTF_HDR_ALIGN + \
				     MAX_TXPD_SZ)

#define NXPWIFI_MGMT_FRAME_HEADER_SIZE	8	/* sizeof(pkt_type)
						 *   + sizeof(tx_control)
						 */

#define FRMCTL_LEN                2
#define DURATION_LEN              2
#define SEQCTL_LEN                2
#define NXPWIFI_MGMT_HEADER_LEN   (FRMCTL_LEN + FRMCTL_LEN + ETH_ALEN + \
				   ETH_ALEN + ETH_ALEN + SEQCTL_LEN + ETH_ALEN)

#define AUTH_ALG_LEN              2
#define AUTH_TRANSACTION_LEN      2
#define AUTH_STATUS_LEN           2
#define NXPWIFI_AUTH_BODY_LEN     (AUTH_ALG_LEN + AUTH_TRANSACTION_LEN + \
				   AUTH_STATUS_LEN)

#define HOST_MLME_AUTH_PENDING    BIT(0)
#define HOST_MLME_AUTH_DONE       BIT(1)

#define HOST_MLME_MGMT_MASK       (BIT(IEEE80211_STYPE_AUTH >> 4) | \
				   BIT(IEEE80211_STYPE_DEAUTH >> 4) | \
				   BIT(IEEE80211_STYPE_DISASSOC >> 4))

#define AUTH_TX_DEFAULT_WAIT_TIME 2400

#define WLAN_AUTH_NONE            0xFFFF

#define NXPWIFI_MAX_TX_BASTREAM_SUPPORTED	2
#define NXPWIFI_MAX_RX_BASTREAM_SUPPORTED	16

#define NXPWIFI_STA_AMPDU_DEF_TXWINSIZE        64
#define NXPWIFI_STA_AMPDU_DEF_RXWINSIZE        64
#define NXPWIFI_STA_COEX_AMPDU_DEF_RXWINSIZE   16

#define NXPWIFI_UAP_AMPDU_DEF_TXWINSIZE        32

#define NXPWIFI_UAP_COEX_AMPDU_DEF_RXWINSIZE   16

#define NXPWIFI_UAP_AMPDU_DEF_RXWINSIZE        16
#define NXPWIFI_11AC_STA_AMPDU_DEF_TXWINSIZE   64
#define NXPWIFI_11AC_STA_AMPDU_DEF_RXWINSIZE   64
#define NXPWIFI_11AC_UAP_AMPDU_DEF_TXWINSIZE   64
#define NXPWIFI_11AC_UAP_AMPDU_DEF_RXWINSIZE   64

#define NXPWIFI_DEFAULT_BLOCK_ACK_TIMEOUT  0xffff

#define NXPWIFI_RATE_BITMAP_MCS0   32

#define NXPWIFI_RX_DATA_BUF_SIZE     (4 * 1024)
#define NXPWIFI_RX_CMD_BUF_SIZE	     (2 * 1024)

#define NXPWIFI_BEACON_PERIOD_MAX                  (4000)
#define NXPWIFI_BEACON_PERIOD_MIN                  (50)
#define NXPWIFI_INVALID_BEACON_PERIOD (NXPWIFI_BEACON_PERIOD_MAX + 1)
#define NXPWIFI_MAX_DTIM_PERIOD (100)
#define NXPWIFI_MIN_DTIM_PERIOD (1)
#define NXPWIFI_INVALID_DTIM_PERIOD (NXPWIFI_MAX_DTIM_PERIOD + 1)
#define NXPWIFI_RTS_THRESHOLD_MIN              (0)
#define NXPWIFI_RTS_THRESHOLD_MAX              (2347)
#define NXPWIFI_INVALID_RTS	(NXPWIFI_RTS_THRESHOLD_MAX + 1)
#define NXPWIFI_FRAG_THRESHOLD_MIN             (256)
#define NXPWIFI_FRAG_THRESHOLD_MAX             (2346)
#define NXPWIFI_INVALID_FRAG (NXPWIFI_FRAG_THRESHOLD_MAX + 1)
#define NXPWIFI_RETRY_LIMIT_MAX 14
#define NXPWIFI_INVALID_RETRY_LIMI (NXPWIFI_RETRY_LIMIT_MAX + 1)

enum nxpwifi_bcast_ssid_ctl {
	/* Hide SSID in beacons (SSID length = 0) */
	NXPWIFI_BCAST_SSID_HIDE_LEN_ZERO = 0,
	/* Do not hide SSID (normal broadcast) */
	NXPWIFI_BCAST_SSID_VISIBLE,
	/* Hide SSID, clear SSID content (ASCII 0),
	 * but keep the original SSID length
	 */
	NXPWIFI_BCAST_SSID_HIDE_LEN_RETAIN,
};

enum nxpwifi_radio_ctl {
	NXPWIFI_RADIO_CTL_DISABLE = 0,
	NXPWIFI_RADIO_CTL_ENABLE,
	__NXPWIFI_RADIO_CTL_MAX,
};

static inline bool nxpwifi_radio_ctl_valid(enum nxpwifi_radio_ctl v)
{
	return v < __NXPWIFI_RADIO_CTL_MAX;
}

#define NXPWIFI_WMM_VERSION                0x01
#define NXPWIFI_WMM_SUBTYPE                0x01

#define NXPWIFI_SDIO_BLOCK_SIZE            256

#define NXPWIFI_BUF_FLAG_REQUEUED_PKT      BIT(0)
#define NXPWIFI_BUF_FLAG_BRIDGED_PKT	   BIT(1)
#define NXPWIFI_BUF_FLAG_EAPOL_TX_STATUS   BIT(3)
#define NXPWIFI_BUF_FLAG_ACTION_TX_STATUS  BIT(4)
#define NXPWIFI_BUF_FLAG_AGGR_PKT          BIT(5)

#define NXPWIFI_BRIDGED_PKTS_THR_HIGH      1024
#define NXPWIFI_BRIDGED_PKTS_THR_LOW        128

/* 54M rates, index from 0 to 11 */
#define NXPWIFI_RATE_INDEX_MCS0 12
/* 12-27=MCS0-15(BW20) */
#define NXPWIFI_BW20_MCS_NUM 15

/* Rate index for OFDM 0 */
#define NXPWIFI_RATE_INDEX_OFDM0   4

#define NXPWIFI_MAX_STA_NUM		3
#define NXPWIFI_MAX_UAP_NUM		3

#define NXPWIFI_A_BAND_START_FREQ	5000

/* SDIO Aggr data packet special info */
#define SDIO_MAX_AGGR_BUF_SIZE		(256 * 255)
#define BLOCK_NUMBER_OFFSET		15
#define SDIO_HEADER_OFFSET		28

#define NXPWIFI_SIZE_4K 0x4000
#define NXPWIFI_EXT_CAPAB_IE_LEN    10

enum nxpwifi_bss_type {
	NXPWIFI_BSS_TYPE_STA = 0,
	NXPWIFI_BSS_TYPE_UAP = 1,
	NXPWIFI_BSS_TYPE_ANY = 0xff,
};

enum nxpwifi_bss_role {
	NXPWIFI_BSS_ROLE_STA = 0,
	NXPWIFI_BSS_ROLE_UAP = 1,
	NXPWIFI_BSS_ROLE_ANY = 0xff,
};

#define BSS_ROLE_BIT_MASK    BIT(0)

#define GET_BSS_ROLE(priv)   ((priv)->bss_role & BSS_ROLE_BIT_MASK)

enum nxpwifi_data_frame_type {
	NXPWIFI_DATA_FRAME_TYPE_ETH_II = 0,
	NXPWIFI_DATA_FRAME_TYPE_802_11,
};

struct nxpwifi_fw_image {
	u8 *helper_buf;
	u32 helper_len;
	u8 *fw_buf;
	u32 fw_len;
};

struct nxpwifi_802_11_ssid {
	u32 ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
};

struct nxpwifi_wait_queue {
	wait_queue_head_t wait;
	int status;
};

struct nxpwifi_rxinfo {
	struct sk_buff *parent;
	u8 bss_num;
	u8 bss_type;
	u8 use_count;
	u8 buf_type;
	u16 pkt_len;
};

struct nxpwifi_txinfo {
	u8 flags;
	u8 bss_num;
	u8 bss_type;
	u8 aggr_num;
	u32 pkt_len;
	u8 ack_frame_id;
	u64 cookie;
};

enum nxpwifi_wmm_ac_e {
	WMM_AC_BK,
	WMM_AC_BE,
	WMM_AC_VI,
	WMM_AC_VO
} __packed;

struct nxpwifi_types_wmm_info {
	u8 oui[4];
	u8 subtype;
	u8 version;
	u8 qos_info;
	u8 reserved;
	struct ieee80211_wmm_ac_param ac[IEEE80211_NUM_ACS];
} __packed;

struct nxpwifi_arp_eth_header {
	struct arphdr hdr;
	u8 ar_sha[ETH_ALEN];
	u8 ar_sip[4];
	u8 ar_tha[ETH_ALEN];
	u8 ar_tip[4];
} __packed;

struct nxpwifi_chan_stats {
	u8 chan_num;
	u8 bandcfg;
	u8 flags;
	s8 noise;
	u16 total_bss;
	u16 cca_scan_dur;
	u16 cca_busy_dur;
} __packed;

#define NXPWIFI_HIST_MAX_SAMPLES	1048576
#define NXPWIFI_MAX_RX_RATES		     44
#define NXPWIFI_MAX_AC_RX_RATES		     74
#define NXPWIFI_MAX_SNR			    256
#define NXPWIFI_MAX_NOISE_FLR		    256
#define NXPWIFI_MAX_SIG_STRENGTH	    256

struct nxpwifi_histogram_data {
	atomic_t rx_rate[NXPWIFI_MAX_AC_RX_RATES];
	atomic_t snr[NXPWIFI_MAX_SNR];
	atomic_t noise_flr[NXPWIFI_MAX_NOISE_FLR];
	atomic_t sig_str[NXPWIFI_MAX_SIG_STRENGTH];
	atomic_t num_samples;
};

struct nxpwifi_iface_comb {
	u8 sta_intf;
	u8 uap_intf;
};

struct nxpwifi_radar_params {
	struct cfg80211_chan_def *chandef;
	u32 cac_time_ms;
} __packed;

struct nxpwifi_11h_intf_state {
	bool is_11h_enabled;
	bool is_11h_active;
} __packed;

#define NXPWIFI_FW_DUMP_IDX		0xff
#define NXPWIFI_FW_DUMP_MAX_MEMSIZE     0x160000
#define NXPWIFI_DRV_INFO_IDX		20
#define FW_DUMP_MAX_NAME_LEN		8
#define FW_DUMP_HOST_READY      0xEE
#define FW_DUMP_DONE			0xFF
#define FW_DUMP_READ_DONE		0xFE

/* Channel bandwidth */
#define CHANNEL_BW_20MHZ 0
#define CHANNEL_BW_40MHZ_ABOVE 1
#define CHANNEL_BW_40MHZ_BELOW 3
/* secondary channel is 80MHz bandwidth for 11ac */
#define CHANNEL_BW_80MHZ 4
#define CHANNEL_BW_160MHZ 5

struct memory_type_mapping {
	u8 mem_name[FW_DUMP_MAX_NAME_LEN];
	u8 *mem_ptr;
	u32 mem_size;
	u8 done_flag;
};

enum rdwr_status {
	RDWR_STATUS_SUCCESS = 0,
	RDWR_STATUS_FAILURE = 1,
	RDWR_STATUS_DONE = 2
};

enum nxpwifi_chan_band {
	BAND_2GHZ = 0,
	BAND_5GHZ,
	BAND_6GHZ,
	BAND_4GHZ,
};

enum nxpwifi_chan_width {
	CHAN_BW_20MHZ = 0,
	CHAN_BW_10MHZ,
	CHAN_BW_40MHZ,
	CHAN_BW_80MHZ,
	CHAN_BW_8080MHZ,
	CHAN_BW_160MHZ,
	CHAN_BW_5MHZ,
};

enum {
	NXPWIFI_SCAN_TYPE_UNCHANGED = 0,
	NXPWIFI_SCAN_TYPE_ACTIVE,
	NXPWIFI_SCAN_TYPE_PASSIVE
};

#define NXPWIFI_PROMISC_MODE            1
#define NXPWIFI_MULTICAST_MODE		2
#define	NXPWIFI_ALL_MULTI_MODE		4
#define NXPWIFI_MAX_MULTICAST_LIST_SIZE	32

struct nxpwifi_multicast_list {
	u32 mode;
	u32 num_multicast_addr;
	u8 mac_list[NXPWIFI_MAX_MULTICAST_LIST_SIZE][ETH_ALEN];
};

struct nxpwifi_chan_freq {
	u32 channel;
	u32 freq;
};

struct nxpwifi_ssid_bssid {
	struct cfg80211_ssid ssid;
	u8 bssid[ETH_ALEN];
};

enum {
	BAND_B = 1,
	BAND_G = 2,
	BAND_A = 4,
	BAND_GN = 8,
	BAND_AN = 16,
	BAND_GAC = 32,
	BAND_AAC = 64,
	BAND_GAX = 256,
	BAND_AAX = 512,
};

#define NXPWIFI_WPA_PASSHPHRASE_LEN 64
struct wpa_param {
	u8 pairwise_cipher_wpa;
	u8 pairwise_cipher_wpa2;
	u8 group_cipher;
	u32 length;
	u8 passphrase[NXPWIFI_WPA_PASSHPHRASE_LEN];
};

struct wep_key {
	u8 key_index;
	u8 is_default;
	u16 length;
	u8 key[WLAN_KEY_LEN_WEP104];
};

#define KEY_MGMT_ON_HOST        0x03
#define NXPWIFI_AUTH_MODE_AUTO  0xFF
#define BAND_CONFIG_BG          0x00
#define BAND_CONFIG_A           0x01
#define NXPWIFI_SEC_CHAN_BELOW	0x03
#define NXPWIFI_SEC_CHAN_ABOVE	0x01
#define NXPWIFI_SUPPORTED_RATES                 14
#define NXPWIFI_SUPPORTED_RATES_EXT             32
#define NXPWIFI_PRIO_BK				2
#define NXPWIFI_PRIO_VI				5
#define NXPWIFI_SUPPORTED_CHANNELS		2
#define NXPWIFI_OPERATING_CLASSES		16

struct nxpwifi_uap_bss_param {
	u8 mac_addr[ETH_ALEN];
	u8 channel;
	u8 band_cfg;
	u16 rts_threshold;
	u16 frag_threshold;
	u8 retry_limit;
	struct nxpwifi_802_11_ssid ssid;
	u8 bcast_ssid_ctl;
	u8 radio_ctl;
	u8 dtim_period;
	u16 beacon_period;
	u16 auth_mode;
	u16 protocol;
	u16 key_mgmt;
	u16 key_mgmt_operation;
	struct wpa_param wpa_cfg;
	struct wep_key wep_cfg[NUM_WEP_KEYS];
	struct ieee80211_ht_cap ht_cap;
	struct ieee80211_vht_cap vht_cap;
	u8 rates[NXPWIFI_SUPPORTED_RATES];
	u32 sta_ao_timer;
	u32 ps_sta_ao_timer;
	u8 power_constraint;
	struct ieee80211_wmm_param_ie wmm_element;
};

struct nxpwifi_ds_get_stats {
	u32 mcast_tx_frame;
	u32 failed;
	u32 retry;
	u32 multi_retry;
	u32 frame_dup;
	u32 rts_success;
	u32 rts_failure;
	u32 ack_failure;
	u32 rx_frag;
	u32 mcast_rx_frame;
	u32 fcs_error;
	u32 tx_frame;
	u32 wep_icv_error[4];
	u32 bcn_rcv_cnt;
	u32 bcn_miss_cnt;
};

#define NXPWIFI_MAX_VER_STR_LEN    128

struct nxpwifi_ver_ext {
	u32 version_str_sel;
	char version_str[NXPWIFI_MAX_VER_STR_LEN];
};

struct nxpwifi_bss_info {
	u32 bss_mode;
	struct cfg80211_ssid ssid;
	u32 bss_chan;
	u8 country_code[3];
	u32 media_connected;
	u32 max_power_level;
	u32 min_power_level;
	signed int bcn_nf_last;
	u32 wep_status;
	u32 is_hs_configured;
	u32 is_deep_sleep;
	u8 bssid[ETH_ALEN];
};

struct nxpwifi_sta_info {
	u8 peer_mac[ETH_ALEN];
	struct station_parameters *params;
};

#define MAX_NUM_TID     8

#define MAX_RX_WINSIZE  64

struct nxpwifi_ds_rx_reorder_tbl {
	u16 tid;
	u8 ta[ETH_ALEN];
	u32 start_win;
	u32 win_size;
	u32 buffer[MAX_RX_WINSIZE];
};

struct nxpwifi_ds_tx_ba_stream_tbl {
	u16 tid;
	u8 ra[ETH_ALEN];
	u8 amsdu;
};

#define DBG_CMD_NUM    5
#define NXPWIFI_DBG_SDIO_MP_NUM    10

struct nxpwifi_debug_info {
	unsigned int debug_mask;
	u32 int_counter;
	u32 packets_out[MAX_NUM_TID];
	u32 tx_buf_size;
	u32 curr_tx_buf_size;
	u32 tx_tbl_num;
	struct nxpwifi_ds_tx_ba_stream_tbl
		tx_tbl[NXPWIFI_MAX_TX_BASTREAM_SUPPORTED];
	u32 rx_tbl_num;
	struct nxpwifi_ds_rx_reorder_tbl rx_tbl
		[NXPWIFI_MAX_RX_BASTREAM_SUPPORTED];
	u16 ps_mode;
	u32 ps_state;
	u8 is_deep_sleep;
	u8 pm_wakeup_card_req;
	u32 pm_wakeup_fw_try;
	u8 is_hs_configured;
	u8 hs_activated;
	u32 num_cmd_host_to_card_failure;
	u32 num_cmd_sleep_cfm_host_to_card_failure;
	u32 num_tx_host_to_card_failure;
	u32 num_event_deauth;
	u32 num_event_disassoc;
	u32 num_event_link_lost;
	u32 num_cmd_deauth;
	u32 num_cmd_assoc_success;
	u32 num_cmd_assoc_failure;
	u32 num_tx_timeout;
	u8 is_cmd_timedout;
	u16 timeout_cmd_id;
	u16 timeout_cmd_act;
	u16 last_cmd_id[DBG_CMD_NUM];
	u16 last_cmd_act[DBG_CMD_NUM];
	u16 last_cmd_index;
	u16 last_cmd_resp_id[DBG_CMD_NUM];
	u16 last_cmd_resp_index;
	u16 last_event[DBG_CMD_NUM];
	u16 last_event_index;
	u8 data_sent;
	u8 cmd_sent;
	u8 cmd_resp_received;
	u8 event_received;
	u32 last_mp_wr_bitmap[NXPWIFI_DBG_SDIO_MP_NUM];
	u32 last_mp_wr_ports[NXPWIFI_DBG_SDIO_MP_NUM];
	u32 last_mp_wr_len[NXPWIFI_DBG_SDIO_MP_NUM];
	u32 last_mp_curr_wr_port[NXPWIFI_DBG_SDIO_MP_NUM];
	u8 last_sdio_mp_index;
};

#define NXPWIFI_KEY_INDEX_UNICAST	0x40000000
#define PN_LEN				16

struct nxpwifi_ds_encrypt_key {
	u32 key_disable;
	u32 key_index;
	u32 key_len;
	u32 key_cipher;
	u8 key_material[WLAN_MAX_KEY_LEN];
	u8 mac_addr[ETH_ALEN];
	u8 pn[PN_LEN];		/* packet number */
	u8 pn_len;
	u8 is_igtk_key;
	u8 is_current_wep_key;
	u8 is_rx_seq_valid;
	u8 is_igtk_def_key;
};

struct nxpwifi_power_cfg {
	u32 is_power_auto;
	u32 is_power_fixed;
	u32 power_level;
};

struct nxpwifi_ds_hs_cfg {
	u32 is_invoke_hostcmd;
	/*
	 * Bit0: non-unicast data
	 * Bit1: unicast data
	 * Bit2: mac events
	 * Bit3: magic packet
	 */
	u32 conditions;
	u32 gpio;
	u32 gap;
};

struct nxpwifi_ds_wakeup_reason {
	u16  hs_wakeup_reason;
};

#define DEEP_SLEEP_ON  1
#define DEEP_SLEEP_OFF 0
#define DEEP_SLEEP_IDLE_TIME	100
#define PS_MODE_AUTO		1

struct nxpwifi_ds_auto_ds {
	u16 auto_ds;
	u16 idle_time;
};

struct nxpwifi_ds_pm_cfg {
	union {
		u32 ps_mode;
		struct nxpwifi_ds_hs_cfg hs_cfg;
		struct nxpwifi_ds_auto_ds auto_deep_sleep;
		u32 sleep_period;
	} param;
};

struct nxpwifi_11ac_vht_cfg {
	u8 band_config;
	u8 misc_config;
	u32 cap_info;
	u32 mcs_tx_set;
	u32 mcs_rx_set;
};

struct nxpwifi_ds_11n_tx_cfg {
	u16 tx_htcap;
	u16 tx_htinfo;
	u16 misc_config; /* Needed for 802.11AC cards only */
};

struct nxpwifi_ds_11n_amsdu_aggr_ctrl {
	u16 enable;
	u16 curr_buf_size;
};

struct nxpwifi_ds_ant_cfg {
	u32 tx_ant;
	u32 rx_ant;
};

#define NXPWIFI_NUM_OF_CMD_BUFFER	50
#define NXPWIFI_SIZE_OF_CMD_BUFFER	2048

enum {
	NXPWIFI_IE_TYPE_GEN_IE = 0,
	NXPWIFI_IE_TYPE_ARP_FILTER,
};

enum {
	NXPWIFI_REG_MAC = 1,
	NXPWIFI_REG_BBP,
	NXPWIFI_REG_RF,
	NXPWIFI_REG_PMIC,
	NXPWIFI_REG_CAU,
};

struct nxpwifi_ds_reg_rw {
	u32 type;
	u32 offset;
	u32 value;
};

#define MAX_EEPROM_DATA 256

struct nxpwifi_ds_read_eeprom {
	u16 offset;
	u16 byte_count;
	u8 value[MAX_EEPROM_DATA];
};

struct nxpwifi_ds_mem_rw {
	u32 addr;
	u32 value;
};

#define IEEE_MAX_IE_SIZE		256

#define NXPWIFI_IE_HDR_SIZE	(sizeof(struct nxpwifi_ie) - IEEE_MAX_IE_SIZE)

struct nxpwifi_ds_misc_gen_ie {
	u32 type;
	u32 len;
	u8 ie_data[IEEE_MAX_IE_SIZE];
};

struct nxpwifi_ds_misc_cmd {
	u32 len;
	u8 cmd[NXPWIFI_SIZE_OF_CMD_BUFFER];
};

#define BITMASK_BCN_RSSI_LOW	BIT(0)
#define BITMASK_BCN_RSSI_HIGH	BIT(4)

enum subsc_evt_rssi_state {
	EVENT_HANDLED,
	RSSI_LOW_RECVD,
	RSSI_HIGH_RECVD
};

struct subsc_evt_cfg {
	u8 abs_value;
	u8 evt_freq;
};

struct nxpwifi_ds_misc_subsc_evt {
	u16 action;
	u16 events;
	struct subsc_evt_cfg bcn_l_rssi_cfg;
	struct subsc_evt_cfg bcn_h_rssi_cfg;
};

#define NXPWIFI_MEF_MAX_BYTESEQ		6	/* non-adjustable */
#define NXPWIFI_MEF_MAX_FILTERS		10

struct nxpwifi_mef_filter {
	u16 repeat;
	u16 offset;
	s8 byte_seq[NXPWIFI_MEF_MAX_BYTESEQ + 1];
	u8 filt_type;
	u8 filt_action;
};

struct nxpwifi_mef_entry {
	u8 mode;
	u8 action;
	struct nxpwifi_mef_filter filter[NXPWIFI_MEF_MAX_FILTERS];
};

struct nxpwifi_ds_mef_cfg {
	u32 criteria;
	u16 num_entries;
	struct nxpwifi_mef_entry *mef_entry;
};

#define NXPWIFI_MAX_VSIE_LEN       (256)
#define NXPWIFI_MAX_VSIE_NUM       (8)
#define NXPWIFI_VSIE_MASK_CLEAR    0x00
#define NXPWIFI_VSIE_MASK_SCAN     0x01
#define NXPWIFI_VSIE_MASK_ASSOC    0x02
#define NXPWIFI_VSIE_MASK_BGSCAN   0x08

enum {
	NXPWIFI_FUNC_INIT = 1,
	NXPWIFI_FUNC_SHUTDOWN,
};

enum COALESCE_OPERATION {
	RECV_FILTER_MATCH_TYPE_EQ = 0x80,
	RECV_FILTER_MATCH_TYPE_NE,
};

enum COALESCE_PACKET_TYPE {
	PACKET_TYPE_UNICAST = 1,
	PACKET_TYPE_MULTICAST = 2,
	PACKET_TYPE_BROADCAST = 3
};

#define NXPWIFI_COALESCE_MAX_RULES	8
#define NXPWIFI_COALESCE_MAX_BYTESEQ	4	/* non-adjustable */
#define NXPWIFI_COALESCE_MAX_FILTERS	4
#define NXPWIFI_MAX_COALESCING_DELAY	100     /* in msecs */

struct filt_field_param {
	u8 operation;
	u8 operand_len;
	u16 offset;
	u8 operand_byte_stream[NXPWIFI_COALESCE_MAX_BYTESEQ];
};

struct nxpwifi_coalesce_rule {
	u16 max_coalescing_delay;
	u8 num_of_fields;
	u8 pkt_type;
	struct filt_field_param params[NXPWIFI_COALESCE_MAX_FILTERS];
};

struct nxpwifi_ds_coalesce_cfg {
	u16 num_of_rules;
	struct nxpwifi_coalesce_rule rule[NXPWIFI_COALESCE_MAX_RULES];
};

struct nxpwifi_11ax_he_cap_cfg {
	u16 id;
	u16 len;
	u8 ext_id;
	struct ieee80211_he_cap_elem cap_elem;
	u8 he_txrx_mcs_support[4];
	u8 val[28];
};

#define HE_CAP_MAX_SIZE   54

struct nxpwifi_11ax_he_cfg {
	u8 band;
	union {
		struct nxpwifi_11ax_he_cap_cfg he_cap_cfg;
		u8 data[HE_CAP_MAX_SIZE];
	};
};

#define NXPWIFI_11AXCMD_CFG_ID_SR_OBSS_PD_OFFSET 1
#define NXPWIFI_11AXCMD_CFG_ID_SR_ENABLE         2
#define NXPWIFI_11AXCMD_CFG_ID_BEAM_CHANGE       3
#define NXPWIFI_11AXCMD_CFG_ID_HTC_ENABLE        4
#define NXPWIFI_11AXCMD_CFG_ID_TXOP_RTS          5
#define NXPWIFI_11AXCMD_CFG_ID_TX_OMI            6
#define NXPWIFI_11AXCMD_CFG_ID_OBSSNBRU_TOLTIME  7
#define NXPWIFI_11AXCMD_CFG_ID_SET_BSRP          8
#define NXPWIFI_11AXCMD_CFG_ID_LLDE              9

#define NXPWIFI_11AXCMD_SR_SUBID                 0x102
#define NXPWIFI_11AXCMD_BEAM_SUBID               0x103
#define NXPWIFI_11AXCMD_HTC_SUBID                0x104
#define NXPWIFI_11AXCMD_TXOMI_SUBID              0x105
#define NXPWIFI_11AXCMD_OBSS_TOLTIME_SUBID       0x106
#define NXPWIFI_11AXCMD_TXOPRTS_SUBID            0x108
#define NXPWIFI_11AXCMD_SET_BSRP_SUBID           0x109
#define NXPWIFI_11AXCMD_LLDE_SUBID               0x110

#define NXPWIFI_11AX_TWT_SETUP_SUBID             0x114
#define NXPWIFI_11AX_TWT_TEARDOWN_SUBID          0x115
#define NXPWIFI_11AX_TWT_REPORT_SUBID            0x116
#define NXPWIFI_11AX_TWT_INFORMATION_SUBID       0x119
#define NXPWIFI_11AX_BTWT_AP_CONFIG_SUBID        0x120
#define BTWT_AGREEMENT_MAX 5

struct nxpwifi_11axcmdcfg_obss_pd_offset {
	/* <NON_SRG_OffSET, SRG_OFFSET> */
	u8 offset[2];
};

struct nxpwifi_11axcmdcfg_sr_control {
	/* 1 enable, 0 disable */
	u8 control;
};

struct nxpwifi_11ax_sr_cmd {
	/* type */
	u16 type;
	/* length of TLV */
	u16 len;
	/* value */
	union {
		struct nxpwifi_11axcmdcfg_obss_pd_offset obss_pd_offset;
		struct nxpwifi_11axcmdcfg_sr_control sr_control;
	} param;
};

struct nxpwifi_11ax_beam_cmd {
	/* command value: 1 is disable, 0 is enable */
	u8 value;
};

struct nxpwifi_11ax_htc_cmd {
	/* command value: 1 is enable, 0 is disable */
	u8 value;
};

struct nxpwifi_11ax_txomi_cmd {
	/* 11ax spec 9.2.4.6a.2 OM Control 12 bits. Bit 0 to bit 11 */
	u16 omi;
	/*
	 * tx option
	 * 0: send OMI in QoS NULL; 1: send OMI in QoS data; 0xFF: set OMI in
	 * both
	 */
	u8 tx_option;
	/*
	 * if OMI is sent in QoS data, specify the number of consecutive data
	 * packets containing the OMI
	 */
	u8 num_data_pkts;
};

struct nxpwifi_11ax_toltime_cmd {
	/* OBSS Narrow Bandwidth RU Tolerance Time */
	u32 tol_time;
};

struct nxpwifi_11ax_txop_cmd {
	/*
	 * Two byte rts threshold value of which only 10 bits, bit 0 to bit 9
	 * are valid
	 */
	u16 rts_thres;
};

struct nxpwifi_11ax_set_bsrp_cmd {
	/* command value: 1 is enable, 0 is disable */
	u8 value;
};

struct nxpwifi_11ax_llde_cmd {
	/* Uplink LLDE: enable=1,disable=0 */
	u8 llde;
	/* operation mode: default=0,carplay=1,gameplay=2 */
	u8 mode;
	/* trigger frame rate: auto=0xff */
	u8 fixrate;
	/* cap airtime limit index: auto=0xff */
	u8 trigger_limit;
	/* cap peak UL rate */
	u8 peak_ul_rate;
	/* Downlink LLDE: enable=1,disable=0 */
	u8 dl_llde;
	/* Set trigger frame interval(us): auto=0 */
	u16 poll_interval;
	/* Set TxOp duration */
	u16 tx_op_duration;
	/* for other configurations */
	u16 llde_ctrl;
	u16 mu_rts_successcnt;
	u16 mu_rts_failcnt;
	u16 basic_trigger_successcnt;
	u16 basic_trigger_failcnt;
	u16 tbppdu_nullcnt;
	u16 tbppdu_datacnt;
};

struct nxpwifi_11ax_cmd_cfg {
	u32 sub_command;
	u32 sub_id;
	union {
		struct nxpwifi_11ax_sr_cmd sr_cfg;
		struct nxpwifi_11ax_beam_cmd beam_cfg;
		struct nxpwifi_11ax_htc_cmd htc_cfg;
		struct nxpwifi_11ax_txomi_cmd txomi_cfg;
		struct nxpwifi_11ax_toltime_cmd toltime_cfg;
		struct nxpwifi_11ax_txop_cmd txop_cfg;
		struct nxpwifi_11ax_set_bsrp_cmd setbsrp_cfg;
		struct nxpwifi_11ax_llde_cmd llde_cfg;
	} param;
};

struct nxpwifi_twt_setup {
	/** Implicit, 0: TWT session is explicit, 1: Session is implicit */
	u8 implicit;
	/** Announced, 0: Unannounced, 1: Announced TWT */
	u8 announced;
	/** Trigger Enabled, 0: Non-Trigger enabled, 1: Trigger enabled TWT */
	u8 trigger_enabled;
	/** TWT Information Disabled, 0: TWT info enabled, 1: TWT info disabled */
	u8 twt_info_disabled;
	/*
	 * Negotiation Type, 0: Future Individual TWT SP start time, 1:
	 * Next Wake TBTT time
	 */
	u8 negotiation_type;
	/*
	 * TWT Wakeup Duration, time after which the TWT requesting STA can
	 * transition to doze state
	 */
	u8 twt_wakeup_duration;
	/** Flow Identifier. Range: [0-7]*/
	u8 flow_identifier;
	/*
	 * Hard Constraint, 0: FW can tweak the TWT setup parameters if it is
	 * rejected by AP.
	 * 1: Firmware should not tweak any parameters.
	 */
	u8 hard_constraint;
	/** TWT Exponent, Range: [0-63] */
	u8 twt_exponent;
	/** TWT Mantissa Range: [0-sizeof(UINT16)] */
	__le16 twt_mantissa;
	/** TWT Request Type, 0: REQUEST_TWT, 1: SUGGEST_TWT*/
	u8 twt_request;
	/** TWT Setup State. Set to 0 by driver, filled by FW in response*/
	u8 twt_setup_state;
	/** TWT link lost timeout threshold */
	__le16 bcn_miss_threshold;
} __packed;

struct nxpwifi_twt_teardown {
	/** TWT Flow Identifier. Range: [0-7] */
	u8 flow_identifier;
	/*
	 * Negotiation Type. 0: Future Individual TWT SP start time, 1: Next
	 * Wake TBTT time
	 */
	u8 negotiation_type;
	/** Tear down all TWT. 1: To teardown all TWT, 0 otherwise */
	u8 teardown_all_twt;
	/** TWT Teardown State. Set to 0 by driver, filled by FW in response */
	u8 twt_teardown_state;
	/** Reserved, set to 0. */
	u8 reserved[3];
} __packed;

#define NXPWIFI_BTWT_REPORT_LEN 9
#define NXPWIFI_BTWT_REPORT_MAX_NUM 4
struct nxpwifi_twt_report {
	/** TWT report type, 0: BTWT id */
	u8 type;
	/** TWT report length of value in data */
	u8 length;
	u8 reserve[2];
	/** TWT report payload for FW response to fill */
	u8 data[NXPWIFI_BTWT_REPORT_LEN * NXPWIFI_BTWT_REPORT_MAX_NUM];
} __packed;

struct nxpwifi_twt_information {
	/** TWT Flow Identifier. Range: [0-7] */
	u8 flow_identifier;
	/*
	 * Suspend Duration. Range: [0-UINT32_MAX]
	 * 0:Suspend forever;
	 * Else:Suspend agreement for specific duration in milli seconds,
	 * after than resume the agreement and enter SP immediately
	 */
	__le32 suspend_duration;
	/** TWT Information State. Set to 0 by driver, filled by FW in response */
	u8 twt_information_state;
} __packed;

struct btwt_set {
	u8 btwt_id;
	__le16 ap_bcast_mantissa;
	u8 ap_bcast_exponent;
	u8 nominalwake;
} __packed;

#define BTWT_AGREEMENT_MAX 5
struct nxpwifi_btwt_ap_config {
	u8 ap_bcast_bet_sta_wait;
	__le16 ap_bcast_offset;
	u8 bcast_twtli;
	u8 count;
	struct btwt_set btwt_sets[BTWT_AGREEMENT_MAX];
} __packed;

struct nxpwifi_twt_cfg {
	u16 action;
	u16 sub_id;
	union {
		struct nxpwifi_twt_setup twt_setup;
		struct nxpwifi_twt_teardown twt_teardown;
		struct nxpwifi_twt_report twt_report;
		struct nxpwifi_twt_information twt_information;
		struct nxpwifi_btwt_ap_config btwt_ap_config;
	} param;
};
#endif /* !_NXPWIFI_CFG_H_ */
