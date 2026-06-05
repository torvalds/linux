/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * nxpwifi: main data structures and prototypes
 *
 * Copyright 2011-2024 NXP
 */

#ifndef _NXPWIFI_MAIN_H_
#define _NXPWIFI_MAIN_H_

#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <net/sock.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/ctype.h>
#include <linux/of.h>
#include <linux/xarray.h>
#include <linux/inetdevice.h>
#include <linux/devcoredump.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/of_irq.h>
#include <linux/workqueue.h>
#include <net/ieee80211_radiotap.h>

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "sdio.h"

extern char nxpwifi_driver_version[];

struct nxpwifi_adapter;
struct nxpwifi_private;

/* command type */
enum {
	NXPWIFI_ASYNC_CMD,
	NXPWIFI_SYNC_CMD
};

#define NXPWIFI_MAX_AP				64

#define NXPWIFI_MAX_PKTS_TXQ			16

#define NXPWIFI_DEFAULT_WATCHDOG_TIMEOUT	(5 * HZ)

#define NXPWIFI_TIMER_10S			10000
#define NXPWIFI_TIMER_1S			1000

#define MAX_TX_PENDING      400
#define LOW_TX_PENDING      380

#define HIGH_RX_PENDING     50
#define LOW_RX_PENDING      20

#define NXPWIFI_UPLD_SIZE               (2312)

#define MAX_EVENT_SIZE                  2048

#define NXPWIFI_FW_DUMP_SIZE       (2 * 1024 * 1024)

#define ARP_FILTER_MAX_BUF_SIZE         68

#define NXPWIFI_KEY_BUFFER_SIZE			16
#define NXPWIFI_DEFAULT_LISTEN_INTERVAL 10
#define NXPWIFI_MAX_REGION_CODE         9

#define DEFAULT_BCN_AVG_FACTOR          8
#define DEFAULT_DATA_AVG_FACTOR         8

#define FIRST_VALID_CHANNEL				0xff

#define DEFAULT_BCN_MISS_TIMEOUT		5

#define MAX_SCAN_BEACON_BUFFER			8000

#define SCAN_BEACON_ENTRY_PAD			6

#define NXPWIFI_PASSIVE_SCAN_CHAN_TIME	110
#define NXPWIFI_ACTIVE_SCAN_CHAN_TIME	40
#define NXPWIFI_SPECIFIC_SCAN_CHAN_TIME	40
#define NXPWIFI_DEF_SCAN_CHAN_GAP_TIME  50

#define SCAN_RSSI(RSSI)					(0x100 - ((u8)(RSSI)))

#define NXPWIFI_MAX_TOTAL_SCAN_TIME	(NXPWIFI_TIMER_10S - NXPWIFI_TIMER_1S)

#define WPA_GTK_OUI_OFFSET				2
#define RSN_GTK_OUI_OFFSET				2

#define NXPWIFI_OUI_NOT_PRESENT			0
#define NXPWIFI_OUI_PRESENT				1

#define PKT_TYPE_MGMT	0xE5
#define PKT_TYPE_802DOT11 0x05
/* check if any data / resp / event is received from card */
#define IS_CARD_RX_RCVD(adapter) ({ \
	typeof(adapter) (_adapter) = adapter; \
	((_adapter)->cmd_resp_received || \
	 (_adapter)->event_received || \
	 (_adapter)->data_received); \
	})

#define NXPWIFI_TYPE_DATA			0
#define NXPWIFI_TYPE_CMD			1
#define NXPWIFI_TYPE_EVENT			3
#define NXPWIFI_TYPE_VDLL			4
#define NXPWIFI_TYPE_AGGR_DATA			10

#define MAX_BITMAP_RATES_SIZE			18

#define MAX_CHANNEL_BAND_BG     14
#define MAX_CHANNEL_BAND_A      165

#define MAX_FREQUENCY_BAND_BG   2484

#define NXPWIFI_EVENT_HEADER_LEN           4
#define NXPWIFI_UAP_EVENT_EXTRA_HEADER	   2

#define NXPWIFI_TYPE_LEN			4
#define NXPWIFI_USB_TYPE_CMD			0xF00DFACE
#define NXPWIFI_USB_TYPE_DATA			0xBEADC0DE
#define NXPWIFI_USB_TYPE_EVENT			0xBEEFFACE

/* tx_timeout threshold to trigger card reset */
#define TX_TIMEOUT_THRESHOLD	6

#define NXPWIFI_DRV_INFO_SIZE_MAX 0x40000

/* address alignment helper */
#define NXPWIFI_ALIGN_ADDR(p, a) ({ \
	typeof(a) (_a) = a; \
	(((long)(p) + (_a) - 1) & ~((_a) - 1)); \
	})

#define NXPWIFI_MAC_LOCAL_ADMIN_BIT		41

/* bit helper */
#define MBIT(x)    (((u32)1) << (x))

/* enum nxpwifi_debug_level  -  nxp wifi debug level */
enum NXPWIFI_DEBUG_LEVEL {
	NXPWIFI_DBG_MSG = 0x00000001,
	NXPWIFI_DBG_FATAL = 0x00000002,
	NXPWIFI_DBG_ERROR = 0x00000004,
	NXPWIFI_DBG_DATA = 0x00000008,
	NXPWIFI_DBG_CMD = 0x00000010,
	NXPWIFI_DBG_EVENT = 0x00000020,
	NXPWIFI_DBG_INTR = 0x00000040,
	NXPWIFI_DBG_IOCTL = 0x00000080,
	NXPWIFI_DBG_MPA_D = 0x00008000,
	NXPWIFI_DBG_DAT_D = 0x00010000,
	NXPWIFI_DBG_CMD_D = 0x00020000,
	NXPWIFI_DBG_EVT_D = 0x00040000,
	NXPWIFI_DBG_FW_D = 0x00080000,
	NXPWIFI_DBG_IF_D = 0x00100000,
	NXPWIFI_DBG_ENTRY = 0x10000000,
	NXPWIFI_DBG_WARN = 0x20000000,
	NXPWIFI_DBG_INFO = 0x40000000,
	NXPWIFI_DBG_DUMP = 0x80000000,
	NXPWIFI_DBG_ANY = 0xffffffff
};

#define NXPWIFI_DEFAULT_DEBUG_MASK	(NXPWIFI_DBG_MSG | \
					NXPWIFI_DBG_FATAL | \
					NXPWIFI_DBG_ERROR)

__printf(3, 4)
void _nxpwifi_dbg(const struct nxpwifi_adapter *adapter, int mask,
		  const char *fmt, ...);
#define nxpwifi_dbg(adapter, mask, fmt, ...)				\
	_nxpwifi_dbg(adapter, NXPWIFI_DBG_##mask, fmt, ##__VA_ARGS__)

#define DEBUG_DUMP_DATA_MAX_LEN		128
#define nxpwifi_dbg_dump(adapter, dbg_mask, str, buf, len)	\
do {								\
	if ((adapter)->debug_mask & NXPWIFI_DBG_##dbg_mask)	\
		print_hex_dump(KERN_DEBUG, str,			\
			       DUMP_PREFIX_OFFSET, 16, 1,	\
			       buf, len, false);		\
} while (0)

/* Min BGSCAN interval 15 second */
#define NXPWIFI_BGSCAN_INTERVAL 15000
/* bgscan interval (ms) and default repeat count */
#define NXPWIFI_BGSCAN_REPEAT_COUNT 6

struct nxpwifi_dbg {
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
	u16 timeout_cmd_id;
	u16 timeout_cmd_act;
	u16 last_cmd_id[DBG_CMD_NUM];
	u16 last_cmd_act[DBG_CMD_NUM];
	u16 last_cmd_index;
	u16 last_cmd_resp_id[DBG_CMD_NUM];
	u16 last_cmd_resp_index;
	u16 last_event[DBG_CMD_NUM];
	u16 last_event_index;
	u32 last_mp_wr_bitmap[NXPWIFI_DBG_SDIO_MP_NUM];
	u32 last_mp_wr_ports[NXPWIFI_DBG_SDIO_MP_NUM];
	u32 last_mp_wr_len[NXPWIFI_DBG_SDIO_MP_NUM];
	u32 last_mp_curr_wr_port[NXPWIFI_DBG_SDIO_MP_NUM];
	u8 last_sdio_mp_index;
};

enum NXPWIFI_HARDWARE_STATUS {
	NXPWIFI_HW_STATUS_READY,
	NXPWIFI_HW_STATUS_INITIALIZING,
	NXPWIFI_HW_STATUS_RESET,
	NXPWIFI_HW_STATUS_NOT_READY
};

enum NXPWIFI_802_11_POWER_MODE {
	NXPWIFI_802_11_POWER_MODE_CAM,
	NXPWIFI_802_11_POWER_MODE_PSP
};

struct nxpwifi_tx_param {
	u32 next_pkt_len;
};

enum NXPWIFI_PS_STATE {
	PS_STATE_AWAKE,
	PS_STATE_PRE_SLEEP,
	PS_STATE_SLEEP_CFM,
	PS_STATE_SLEEP
};

enum nxpwifi_iface_type {
	NXPWIFI_SDIO
};

struct nxpwifi_add_ba_param {
	u32 tx_win_size;
	u32 rx_win_size;
	u32 timeout;
	u8 tx_amsdu;
	u8 rx_amsdu;
};

struct nxpwifi_tx_aggr {
	u8 ampdu_user;
	u8 ampdu_ap;
	u8 amsdu;
};

enum nxpwifi_ba_status {
	BA_SETUP_NONE = 0,
	BA_SETUP_INPROGRESS,
	BA_SETUP_COMPLETE
};

struct nxpwifi_ra_list_tbl {
	struct list_head list;
	struct sk_buff_head skb_head;
	u8 ra[ETH_ALEN];
	u32 is_11n_enabled;
	u16 max_amsdu;
	u16 ba_pkt_count;
	u8 ba_packet_thr;
	enum nxpwifi_ba_status ba_status;
	u8 amsdu_in_ampdu;
	u16 total_pkt_count;
	bool tx_paused;
};

struct nxpwifi_tid_tbl {
	struct list_head ra_list;
};

#define WMM_HIGHEST_PRIORITY		7
#define HIGH_PRIO_TID				7
#define LOW_PRIO_TID				0
#define NO_PKT_PRIO_TID				-1
#define NXPWIFI_WMM_DRV_DELAY_MAX 510

struct nxpwifi_wmm_desc {
	struct nxpwifi_tid_tbl tid_tbl_ptr[MAX_NUM_TID];
	u32 packets_out[MAX_NUM_TID];
	u32 pkts_paused[MAX_NUM_TID];
	/* protects ra_list */
	spinlock_t ra_list_spinlock;
	struct nxpwifi_wmm_ac_status ac_status[IEEE80211_NUM_ACS];
	enum nxpwifi_wmm_ac_e ac_down_graded_vals[IEEE80211_NUM_ACS];
	u32 drv_pkt_delay_max;
	u8 queue_priority[IEEE80211_NUM_ACS];
	u32 user_pri_pkt_tx_ctrl[WMM_HIGHEST_PRIORITY + 1];	/* UP: 0 to 7 */
	/* number of queued TX packets */
	atomic_t tx_pkts_queued;
	/* highest priority currently queued */
	atomic_t highest_queued_prio;
};

struct nxpwifi_802_11_security {
	u8 wpa_enabled;
	u8 wpa2_enabled;
	u8 wep_enabled;
	u32 authentication_mode;
	u8 is_authtype_auto;
	u32 encryption_mode;
};

struct ieee_types_vendor_specific {
	struct ieee80211_vendor_ie vend_hdr;
	u8 data[IEEE_MAX_IE_SIZE - sizeof(struct ieee80211_vendor_ie)];
} __packed;

struct nxpwifi_bssdescriptor {
	u8 mac_address[ETH_ALEN];
	struct cfg80211_ssid ssid;
	u32 privacy;
	s32 rssi;
	u32 channel;
	u32 freq;
	u16 beacon_period;
	u8 erp_flags;
	u32 bss_mode;
	u8 supported_rates[NXPWIFI_SUPPORTED_RATES];
	u8 data_rates[NXPWIFI_SUPPORTED_RATES];
	u16 bss_band;
	u64 fw_tsf;
	u64 timestamp;
	union ieee_types_phy_param_set phy_param_set;
	struct ieee_types_cf_param_set cf_param_set;
	u16 cap_info_bitmap;
	struct ieee80211_wmm_param_ie wmm_ie;
	u8 disable_11n;
	struct ieee80211_ht_cap *bcn_ht_cap;
	u16 ht_cap_offset;
	struct ieee80211_ht_operation *bcn_ht_oper;
	u16 ht_info_offset;
	u8 *bcn_bss_co_2040;
	u16 bss_co_2040_offset;
	u8 *bcn_ext_cap;
	u16 ext_cap_offset;
	struct ieee80211_vht_cap *bcn_vht_cap;
	u16 vht_cap_offset;
	struct ieee80211_vht_operation *bcn_vht_oper;
	u16 vht_info_offset;
	struct ieee_types_oper_mode_ntf *oper_mode;
	u16 oper_mode_offset;
	u8 disable_11ac;
	struct ieee80211_he_cap_elem *bcn_he_cap;
	u16 he_cap_offset;
	struct ieee80211_he_operation *bcn_he_oper;
	u16 he_info_offset;
	u8 disable_11ax;
	struct ieee_types_vendor_specific *bcn_wpa_ie;
	u16 wpa_offset;
	struct element *bcn_rsn_ie;
	u16 rsn_offset;
	struct element *bcn_rsnx_ie;
	u16 rsnx_offset;
	u8 *beacon_buf;
	u32 beacon_buf_size;
	u8 sensed_11h;
	u8 local_constraint;
	u8 chan_sw_ie_present;
};

struct nxpwifi_current_bss_params {
	struct nxpwifi_bssdescriptor bss_descriptor;
	bool wmm_enabled;
	bool wmm_uapsd_enabled;
	u8 band;
	u32 num_of_rates;
	u8 data_rates[NXPWIFI_SUPPORTED_RATES];
};

struct nxpwifi_sleep_period {
	u16 period;
	u16 reserved;
};

struct nxpwifi_wep_key {
	u32 length;
	u32 key_index;
	u32 key_length;
	u8 key_material[NXPWIFI_KEY_BUFFER_SIZE];
};

#define MAX_REGION_CHANNEL_NUM  2

struct nxpwifi_chan_freq_power {
	u16 channel;
	u32 freq;
	u16 max_tx_power;
	u8 unsupported;
};

enum state_11d_t {
	DISABLE_11D = 0,
	ENABLE_11D = 1,
};

#define NXPWIFI_MAX_TRIPLET_802_11D		83

struct nxpwifi_802_11d_domain_reg {
	u8 dfs_region;
	u8 country_code[IEEE80211_COUNTRY_STRING_LEN];
	u8 no_of_triplet;
	struct ieee80211_country_ie_triplet
		triplet[NXPWIFI_MAX_TRIPLET_802_11D];
};

struct nxpwifi_vendor_spec_cfg_ie {
	u16 mask;
	u16 flag;
	u8 ie[NXPWIFI_MAX_VSIE_LEN];
};

struct wps {
	u8 session_enable;
};

struct nxpwifi_roc_cfg {
	u64 cookie;
	struct ieee80211_channel chan;
};

enum nxpwifi_iface_work_flags {
	NXPWIFI_IFACE_WORK_DEVICE_DUMP,
	NXPWIFI_IFACE_WORK_CARD_RESET,
};

enum nxpwifi_adapter_work_flags {
	NXPWIFI_SURPRISE_REMOVED,
	NXPWIFI_IS_CMD_TIMEDOUT,
	NXPWIFI_IS_SUSPENDED,
	NXPWIFI_IS_HS_CONFIGURED,
	NXPWIFI_IS_HS_ENABLING,
	NXPWIFI_IS_REQUESTING_FW_VEREXT,
};

struct nxpwifi_band_config {
	u8 chan_band:2;
	u8 chan_width:2;
	u8 chan2_offset:2;
	u8 scan_mode:2;
} __packed;

struct nxpwifi_channel_band {
	struct nxpwifi_band_config band_config;
	u8 channel;
};

struct nxpwifi_private {
	struct nxpwifi_adapter *adapter;
	u8 bss_type;
	u8 bss_role;
	u8 bss_priority;
	u8 bss_num;
	u8 bss_started;
	u8 auth_flag;
	u16 auth_alg;
	u8 frame_type;
	u8 curr_addr[ETH_ALEN];
	u8 media_connected;
	u8 port_open;
	u8 usb_port;
	u32 num_tx_timeout;
	/* track consecutive timeout */
	u8 tx_timeout_cnt;
	struct net_device *netdev;
	struct net_device_stats stats;
	u32 curr_pkt_filter;
	u32 bss_mode;
	u32 pkt_tx_ctrl;
	u16 tx_power_level;
	u8 max_tx_power_level;
	u8 min_tx_power_level;
	u32 tx_ant;
	u32 rx_ant;
	u8 tx_rate;
	u8 tx_htinfo;
	u8 rxpd_htinfo;
	u8 rxpd_rate;
	u16 rate_bitmap;
	u16 bitmap_rates[MAX_BITMAP_RATES_SIZE];
	u32 data_rate;
	u8 is_data_rate_auto;
	u16 bcn_avg_factor;
	u16 data_avg_factor;
	s16 data_rssi_last;
	s16 data_nf_last;
	s16 data_rssi_avg;
	s16 data_nf_avg;
	s16 bcn_rssi_last;
	s16 bcn_nf_last;
	s16 bcn_rssi_avg;
	s16 bcn_nf_avg;
	struct nxpwifi_bssdescriptor *attempted_bss_desc;
	struct cfg80211_ssid prev_ssid;
	u8 prev_bssid[ETH_ALEN];
	struct nxpwifi_current_bss_params curr_bss_params;
	u16 beacon_period;
	u8 dtim_period;
	u16 listen_interval;
	u16 atim_window;
	struct nxpwifi_802_11_security sec_info;
	struct nxpwifi_wep_key wep_key[NUM_WEP_KEYS];
	u16 wep_key_curr_index;
	u8 wpa_ie[256];
	u16 wpa_ie_len;
	u8 wpa_is_gtk_set;
	struct host_cmd_ds_802_11_key_material aes_key;
	u8 *wps_ie;
	u16 wps_ie_len;
	u8 wmm_required;
	bool wmm_enabled;
	u8 wmm_qosinfo;
	struct nxpwifi_wmm_desc wmm;
	atomic_t wmm_tx_pending[IEEE80211_NUM_ACS];
	struct list_head sta_list;
	/* spin lock for associated station list */
	spinlock_t sta_list_spinlock;
	struct list_head tx_ba_stream_tbl_ptr[MAX_NUM_TID];
	/* spin lock for tx_ba_stream_tbl_ptr queue */
	struct spinlock tx_ba_stream_tbl_lock[MAX_NUM_TID];
	struct nxpwifi_tx_aggr aggr_prio_tbl[MAX_NUM_TID];
	struct nxpwifi_add_ba_param add_ba_param;
	u16 rx_seq[MAX_NUM_TID];
	u8 tos_to_tid_inv[MAX_NUM_TID];
	struct list_head rx_reorder_tbl_ptr[MAX_NUM_TID];
	/* spin lock for rx_reorder_tbl_ptr queue */
	struct spinlock rx_reorder_tbl_lock[MAX_NUM_TID];
#define NXPWIFI_ASSOC_RSP_BUF_SIZE  500
	u8 assoc_rsp_buf[NXPWIFI_ASSOC_RSP_BUF_SIZE];
	u32 assoc_rsp_size;
	struct cfg80211_bss *req_bss;

#define NXPWIFI_GENIE_BUF_SIZE      256
	u8 gen_ie_buf[NXPWIFI_GENIE_BUF_SIZE];
	u8 gen_ie_buf_len;

	struct nxpwifi_vendor_spec_cfg_ie vs_ie[NXPWIFI_MAX_VSIE_NUM];

#define NXPWIFI_ASSOC_TLV_BUF_SIZE  256
	u8 assoc_tlv_buf[NXPWIFI_ASSOC_TLV_BUF_SIZE];
	u8 assoc_tlv_buf_len;

	u8 *curr_bcn_buf;
	u32 curr_bcn_size;
	/* spin lock for beacon buffer */
	spinlock_t curr_bcn_buf_lock;
	struct wireless_dev wdev;
	struct nxpwifi_chan_freq_power cfp;
	u32 versionstrsel;
	char version_str[NXPWIFI_VERSION_STR_LENGTH];
#ifdef CONFIG_DEBUG_FS
	struct dentry *dfs_dev_dir;
#endif
	u16 current_key_index;
	struct cfg80211_scan_request *scan_request;
	u8 cfg_bssid[6];
	struct wps wps;
	u8 scan_block;
	s32 cqm_rssi_thold;
	u32 cqm_rssi_hyst;
	u8 subsc_evt_rssi_state;
	struct nxpwifi_ds_misc_subsc_evt async_subsc_evt_storage;
	struct nxpwifi_ie mgmt_ie[MAX_MGMT_IE_INDEX];
	u16 beacon_idx;
	u16 proberesp_idx;
	u16 assocresp_idx;
	u16 gen_idx;
	u8 ap_11n_enabled;
	u8 ap_11ac_enabled;
	u8 ap_11ax_enabled;
	u16 config_bands;
	/* 11AX */
	u8 user_he_cap_len;
	u8 user_he_cap[HE_CAP_MAX_SIZE];
	u8 user_2g_he_cap_len;
	u8 user_2g_he_cap[HE_CAP_MAX_SIZE];
	bool host_mlme_reg;
	u32 mgmt_frame_mask;
	struct nxpwifi_roc_cfg roc_cfg;
	bool scan_aborting;
	u8 sched_scanning;
	u8 csa_chan;
	unsigned long csa_expire_time;
	u8 del_list_idx;
	bool hs2_enabled;
	struct nxpwifi_uap_bss_param bss_cfg;
	struct cfg80211_chan_def bss_chandef;
	struct station_parameters *sta_params;
	struct xarray ack_status_frames;
	/* spin lock for ack status */
	spinlock_t ack_status_lock;
	/** rx histogram data */
	struct nxpwifi_histogram_data *hist_data;
	struct cfg80211_chan_def dfs_chandef;
	struct wiphy_work reset_conn_state_work;
	struct wiphy_delayed_work dfs_cac_work;
	struct wiphy_delayed_work dfs_chan_sw_work;
	bool uap_stop_tx;
	struct cfg80211_ap_update ap_update_info;
	struct nxpwifi_11h_intf_state state_11h;
	struct nxpwifi_ds_mem_rw mem_rw;
	struct sk_buff_head bypass_txq;
	struct nxpwifi_user_scan_chan hidden_chan[NXPWIFI_USER_SCAN_CHAN_MAX];
	u8 assoc_resp_ht_param;
	bool ht_param_present;
	u16 last_deauth_reason;
};

struct nxpwifi_tx_ba_stream_tbl {
	struct list_head list;
	struct rcu_head rcu;
	int tid;
	u8 ra[ETH_ALEN];
	enum nxpwifi_ba_status ba_status;
	u8 amsdu;
};

struct nxpwifi_rx_reorder_tbl;

struct reorder_tmr_cnxt {
	struct timer_list timer;
	struct nxpwifi_rx_reorder_tbl *ptr;
	struct nxpwifi_private *priv;
	u8 timer_is_set;
};

struct nxpwifi_rx_reorder_tbl {
	struct list_head list;
	struct list_head tmp_list;
	struct rcu_head rcu;
	int tid;
	u8 ta[ETH_ALEN];
	int init_win;
	int start_win;
	int win_size;
	void **rx_reorder_ptr;
	struct reorder_tmr_cnxt timer_context;
	u8 amsdu;
	u8 flags;
};

struct nxpwifi_bss_prio_node {
	struct list_head list;
	struct nxpwifi_private *priv;
};

struct nxpwifi_bss_prio_tbl {
	struct list_head bss_prio_head;
	spinlock_t bss_prio_lock; /* protects BSS priority */
	struct nxpwifi_bss_prio_node *bss_prio_cur;
};

struct cmd_ctrl_node {
	struct list_head list;
	struct nxpwifi_private *priv;
	u32 cmd_no;
	u32 cmd_flag;
	struct sk_buff *cmd_skb;
	struct sk_buff *resp_skb;
	void *data_buf;
	u32 wait_q_enabled;
	struct sk_buff *skb;
	u8 *condition;
	u8 cmd_wait_q_woken;
	int (*cmd_resp)(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *resp,
			u16 cmdresp_no,
			void *data_buf);
};

struct nxpwifi_bss_priv {
	u16 band;
	u64 fw_tsf;
};

struct nxpwifi_station_stats {
	u64 last_rx;
	s8 rssi;
	u64 rx_bytes;
	u64 tx_bytes;
	u32 rx_packets;
	u32 tx_packets;
	u32 tx_failed;
	u8 last_tx_rate;
	u8 last_tx_htinfo;
};

/*AP - side structure tracking associated STA info */
struct nxpwifi_sta_node {
	struct list_head list;
	struct rcu_head rcu;
	u8 mac_addr[ETH_ALEN];
	u8 is_wmm_enabled;
	u8 is_11n_enabled;
	u8 is_11ac_enabled;
	u8 is_11ax_enabled;
	u8 ampdu_sta[MAX_NUM_TID];
	u16 rx_seq[MAX_NUM_TID];
	u16 max_amsdu;
	struct nxpwifi_station_stats stats;
	u8 tx_pause;
};

#define NXPWIFI_TYPE_AGGR_DATA_V2 11
#define NXPWIFI_BUS_AGGR_MODE_LEN_V2 (2)
#define NXPWIFI_BUS_AGGR_MAX_LEN 16000
#define NXPWIFI_BUS_AGGR_MAX_NUM 10
struct bus_aggr_params {
	u16 enable;
	u16 mode;
	u16 tx_aggr_max_size;
	u16 tx_aggr_max_num;
	u16 tx_aggr_align;
};

struct vdll_dnld_ctrl {
	u8 *pending_block;
	u16 pending_block_len;
	u8 *vdll_mem;
	u32 vdll_len;
	struct sk_buff *skb;
};

struct nxpwifi_if_ops {
	int (*init_if)(struct nxpwifi_adapter *adapter);
	void (*cleanup_if)(struct nxpwifi_adapter *adapter);
	int (*check_fw_status)(struct nxpwifi_adapter *adapter, u32 poll_num);
	int (*check_winner_status)(struct nxpwifi_adapter *adapter);
	int (*prog_fw)(struct nxpwifi_adapter *adapter,
		       struct nxpwifi_fw_image *fw);
	int (*register_dev)(struct nxpwifi_adapter *adapter);
	void (*unregister_dev)(struct nxpwifi_adapter *adapter);
	int (*enable_int)(struct nxpwifi_adapter *adapter);
	void (*disable_int)(struct nxpwifi_adapter *adapter);
	int (*process_int_status)(struct nxpwifi_adapter *adapter, u8 istat);
	int (*host_to_card)(struct nxpwifi_adapter *adapter, u8 type,
			    struct sk_buff *skb,
			    struct nxpwifi_tx_param *tx_param);
	int (*wakeup)(struct nxpwifi_adapter *adapter);
	int (*wakeup_complete)(struct nxpwifi_adapter *adapter);

	/* interface-specific operations */
	void (*update_mp_end_port)(struct nxpwifi_adapter *adapter, u16 port);
	void (*cleanup_mpa_buf)(struct nxpwifi_adapter *adapter);
	int (*cmdrsp_complete)(struct nxpwifi_adapter *adapter,
			       struct sk_buff *skb);
	int (*event_complete)(struct nxpwifi_adapter *adapter,
			      struct sk_buff *skb);
	int (*dnld_fw)(struct nxpwifi_adapter *adapter,
		       struct nxpwifi_fw_image *fw);
	void (*card_reset)(struct nxpwifi_adapter *adapter);
	int (*reg_dump)(struct nxpwifi_adapter *adapter, char *drv_buf);
	void (*device_dump)(struct nxpwifi_adapter *adapter);
	void (*deaggr_pkt)(struct nxpwifi_adapter *adapter,
			   struct sk_buff *skb);
	void (*up_dev)(struct nxpwifi_adapter *adapter);
};

#define NXPWIFI_DEFAULT_REGION_CODE     NXPWIFI_REGION_FCC

struct nxpwifi_adapter {
	u8 iface_type;
	unsigned int debug_mask;
	struct nxpwifi_iface_comb iface_limit;
	struct nxpwifi_iface_comb curr_iface_comb;
	struct nxpwifi_private *priv[NXPWIFI_MAX_BSS_NUM];
	u8 priv_num;
	const struct firmware *firmware;
	char fw_name[32];
	int winner;
	struct device *dev;
	struct wiphy *wiphy;
	u8 perm_addr[ETH_ALEN];
	unsigned long work_flags;
	u32 fw_release_number;
	u8 intf_hdr_len;
	void *card;
	struct nxpwifi_if_ops if_ops;
	atomic_t bypass_tx_pending;
	atomic_t rx_pending;
	atomic_t tx_pending;
	atomic_t cmd_pending;
	atomic_t tx_hw_pending;
	struct workqueue_struct *workqueue;
	struct work_struct main_work;
	struct workqueue_struct *rx_workqueue;
	struct work_struct rx_work;
	struct wiphy_work host_mlme_work;
	bool rx_work_enabled;
	bool rx_processing;
	bool delay_main_work;
	atomic_t rx_ba_teardown_pending;
	atomic_t iface_changing;
	struct nxpwifi_bss_prio_tbl bss_prio_tbl[NXPWIFI_MAX_BSS_NUM];
	u32 nxpwifi_processing;
	u16 tx_buf_size;
	u16 curr_tx_buf_size;
	/* SDIO single port rx aggregation capability */
	bool host_disable_sdio_rx_aggr;
	bool sdio_rx_aggr_enable;
	u16 sdio_rx_block_size;
	u32 ioport;
	enum NXPWIFI_HARDWARE_STATUS hw_status;
	u16 number_of_antenna;
	u32 fw_cap_info;
	u32 fw_cap_ext;
	u16 user_htstream;
	u64 uuid_lo;
	u64 uuid_hi;
	/* interrupt lock */
	spinlock_t int_lock;
	u8 int_status;
	u32 event_cause;
	struct sk_buff *event_skb;
	u8 upld_buf[NXPWIFI_UPLD_SIZE];
	u8 data_sent;
	u8 cmd_sent;
	u8 cmd_resp_received;
	bool event_received;
	u8 data_received;
	u8 assoc_resp_received;
	struct nxpwifi_private *priv_link_lost;
	u8 host_mlme_link_lost;
	u16 seq_num;
	struct cmd_ctrl_node *cmd_pool;
	struct cmd_ctrl_node *curr_cmd;
	/* spin lock for command */
	spinlock_t nxpwifi_cmd_lock;
	struct timer_list cmd_timer;
	struct list_head cmd_free_q;
	spinlock_t cmd_free_q_lock; /* protects cmd_free_q */
	struct list_head cmd_pending_q;
	spinlock_t cmd_pending_q_lock; /* protects cmd_pending_q */
	struct list_head scan_pending_q;
	spinlock_t scan_pending_q_lock; /* protects scan_pending_q */
	struct sk_buff_head tx_data_q;
	atomic_t tx_queued;
	u32 scan_processing;
	enum nxpwifi_region_code region_code;
	struct nxpwifi_802_11d_domain_reg domain_reg;
	u16 scan_probes;
	u32 scan_mode;
	u16 specific_scan_time;
	u16 active_scan_time;
	u16 passive_scan_time;
	u16 scan_chan_gap_time;
	u16 fw_bands;
	u8 tx_lock_flag;
	struct nxpwifi_sleep_period sleep_period;
	u16 ps_mode;
	u32 ps_state;
	u8 need_to_wakeup;
	u16 multiple_dtim;
	u16 local_listen_interval;
	u16 null_pkt_interval;
	struct sk_buff *sleep_cfm;
	u16 bcn_miss_time_out;
	u8 is_deep_sleep;
	u8 delay_null_pkt;
	u16 delay_to_ps;
	u16 enhanced_ps_mode;
	u8 pm_wakeup_card_req;
	u16 gen_null_pkt;
	u16 pps_uapsd_mode;
	u32 pm_wakeup_fw_try;
	struct timer_list wakeup_timer;
	struct nxpwifi_hs_config_param hs_cfg;
	u8 hs_activated;
	u8 hs_activated_manually;
	u16 hs_activate_wait_q_woken;
	wait_queue_head_t hs_activate_wait_q;
	u8 event_body[MAX_EVENT_SIZE];
	u32 hw_dot_11n_dev_cap;
	u8 hw_dev_mcs_support;
	u8 hw_mpdu_density;
	u8 user_dev_mcs_support;
	u8 sec_chan_offset;
	struct nxpwifi_dbg dbg;
	u8 arp_filter[ARP_FILTER_MAX_BUF_SIZE];
	u32 arp_filter_size;
	struct nxpwifi_wait_queue cmd_wait_q;
	u8 scan_wait_q_woken;
	spinlock_t queue_lock; /* protects TX queues */
	u8 dfs_region;
	u8 country_code[IEEE80211_COUNTRY_STRING_LEN];
	u16 max_mgmt_ie_index;
	const struct firmware *cal_data;
	/* 11AC capability fields */
	u32 is_hw_11ac_capable;
	u32 hw_dot_11ac_dev_cap;
	u32 hw_dot_11ac_mcs_support;
	u32 usr_dot_11ac_dev_cap_bg;
	u32 usr_dot_11ac_dev_cap_a;
	u32 usr_dot_11ac_mcs_support;
	/* 11AX capability fields */
	u8 is_hw_11ax_capable;
	u8 hw_he_cap_len;
	u8 hw_he_cap[HE_CAP_MAX_SIZE];
	u8 hw_2g_he_cap_len;
	u8 hw_2g_he_cap[HE_CAP_MAX_SIZE];
	atomic_t pending_bridged_pkts;
	struct completion *fw_done; /* FW init completion */
	bool is_up;
	bool ext_scan;
	u8 fw_api_ver;
	u8 fw_hotfix_ver;
	u8 key_api_major_ver, key_api_minor_ver;
	u8 max_sta_conn;
	struct memory_type_mapping *mem_type_mapping_tbl;
	u8 num_mem_types;
	bool scan_chan_gap_enabled;
	struct sk_buff_head rx_mlme_q;
	struct sk_buff_head rx_data_q;
	struct nxpwifi_chan_stats *chan_stats;
	u32 num_in_chan_stats;
	int survey_idx;
	u8 coex_scan;
	u8 coex_min_scan_time;
	u8 coex_max_scan_time;
	u8 coex_win_size;
	u8 coex_tx_win_size;
	u8 coex_rx_win_size;
	u8 active_scan_triggered;
	bool usb_mc_status;
	bool usb_mc_setup;
	struct cfg80211_wowlan_nd_info *nd_info;
	struct ieee80211_regdomain *regd;
	/* Aggregation parameters*/
	struct bus_aggr_params bus_aggr;
	void *devdump_data; /* device dump storage */
	int devdump_len;	/* device dump length */
	bool ignore_btcoex_events;
	struct vdll_dnld_ctrl vdll_ctrl;
	u64 roc_cookie_counter;
	u32 enable_net_mon;
	bool wowlan_enabled;
	bool chandef_valid;
	struct cfg80211_chan_def chandef;
	atomic_t uap_count;
};

void nxpwifi_process_tx_queue(struct nxpwifi_adapter *adapter);

void nxpwifi_init_lock_list(struct nxpwifi_adapter *adapter);

void nxpwifi_set_trans_start(struct net_device *dev);

void nxpwifi_stop_net_dev_queue(struct net_device *netdev,
				struct nxpwifi_adapter *adapter);

void nxpwifi_wake_up_net_dev_queue(struct net_device *netdev,
				   struct nxpwifi_adapter *adapter);

int nxpwifi_init_priv(struct nxpwifi_private *priv);
void nxpwifi_free_priv(struct nxpwifi_private *priv);

int nxpwifi_init_fw(struct nxpwifi_adapter *adapter);

void nxpwifi_shutdown_drv(struct nxpwifi_adapter *adapter);

int nxpwifi_dnld_fw(struct nxpwifi_adapter *adapter,
		    struct nxpwifi_fw_image *fw);

int nxpwifi_recv_packet(struct nxpwifi_private *priv, struct sk_buff *skb);
int nxpwifi_uap_recv_packet(struct nxpwifi_private *priv,
			    struct sk_buff *skb);

void nxpwifi_host_mlme_disconnect(struct nxpwifi_private *priv,
				  u16 reason_code, u8 *sa);

int nxpwifi_process_mgmt_packet(struct nxpwifi_private *priv,
				struct sk_buff *skb);
int nxpwifi_recv_packet_to_monif(struct nxpwifi_private *priv,
				 struct sk_buff *skb);
int nxpwifi_complete_cmd(struct nxpwifi_adapter *adapter,
			 struct cmd_ctrl_node *cmd_node);

void nxpwifi_cmd_timeout_func(struct timer_list *t);

int nxpwifi_get_debug_info(struct nxpwifi_private *priv,
			   struct nxpwifi_debug_info *info);

int nxpwifi_alloc_cmd_buffer(struct nxpwifi_adapter *adapter);
void nxpwifi_free_cmd_buffer(struct nxpwifi_adapter *adapter);
void nxpwifi_free_cmd_buffers(struct nxpwifi_adapter *adapter);
void nxpwifi_cancel_all_pending_cmd(struct nxpwifi_adapter *adapter);
void nxpwifi_cancel_pending_scan_cmd(struct nxpwifi_adapter *adapter);
void nxpwifi_cancel_scan(struct nxpwifi_adapter *adapter);

void nxpwifi_recycle_cmd_node(struct nxpwifi_adapter *adapter,
			      struct cmd_ctrl_node *cmd_node);

void nxpwifi_insert_cmd_to_pending_q(struct nxpwifi_adapter *adapter,
				     struct cmd_ctrl_node *cmd_node);

int nxpwifi_exec_next_cmd(struct nxpwifi_adapter *adapter);
int nxpwifi_process_cmdresp(struct nxpwifi_adapter *adapter);
void nxpwifi_process_assoc_resp(struct nxpwifi_adapter *adapter);
int nxpwifi_handle_rx_packet(struct nxpwifi_adapter *adapter,
			     struct sk_buff *skb);
int nxpwifi_process_tx(struct nxpwifi_private *priv, struct sk_buff *skb,
		       struct nxpwifi_tx_param *tx_param);
int nxpwifi_send_null_packet(struct nxpwifi_private *priv, u8 flags);
int nxpwifi_write_data_complete(struct nxpwifi_adapter *adapter,
				struct sk_buff *skb, int aggr, int status);
void nxpwifi_clean_txrx(struct nxpwifi_private *priv);
u8 nxpwifi_check_last_packet_indication(struct nxpwifi_private *priv);
void nxpwifi_check_ps_cond(struct nxpwifi_adapter *adapter);
void nxpwifi_process_sleep_confirm_resp(struct nxpwifi_adapter *adapter,
					u8 *pbuf, u32 upld_len);
void nxpwifi_process_hs_config(struct nxpwifi_adapter *adapter);
void nxpwifi_hs_activated_event(struct nxpwifi_private *priv,
				u8 activated);
int nxpwifi_set_hs_params(struct nxpwifi_private *priv, u16 action,
			  int cmd_type, struct nxpwifi_ds_hs_cfg *hs_cfg);
int nxpwifi_ret_802_11_hs_cfg(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *resp);
int nxpwifi_process_rx_packet(struct nxpwifi_private *priv,
			      struct sk_buff *skb);
int nxpwifi_process_sta_rx_packet(struct nxpwifi_private *priv,
				  struct sk_buff *skb);
int nxpwifi_process_uap_rx_packet(struct nxpwifi_private *priv,
				  struct sk_buff *skb);
int nxpwifi_handle_uap_rx_forward(struct nxpwifi_private *priv,
				  struct sk_buff *skb);
void nxpwifi_delete_all_station_list(struct nxpwifi_private *priv);
void nxpwifi_wmm_del_peer_ra_list(struct nxpwifi_private *priv,
				  const u8 *ra_addr);
void nxpwifi_process_sta_txpd(struct nxpwifi_private *priv,
			      struct sk_buff *skb);
void nxpwifi_process_uap_txpd(struct nxpwifi_private *priv,
			      struct sk_buff *skb);
int nxpwifi_cmd_802_11_scan(struct host_cmd_ds_command *cmd,
			    struct nxpwifi_scan_cmd_config *scan_cfg);
void nxpwifi_queue_scan_cmd(struct nxpwifi_private *priv,
			    struct cmd_ctrl_node *cmd_node);
int nxpwifi_ret_802_11_scan(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *resp);
int nxpwifi_associate(struct nxpwifi_private *priv,
		      struct nxpwifi_bssdescriptor *bss_desc);
int nxpwifi_cmd_802_11_associate(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *cmd,
				 struct nxpwifi_bssdescriptor *bss_desc);
int nxpwifi_ret_802_11_associate(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *resp);
u8 nxpwifi_band_to_radio_type(u16 config_bands);
int nxpwifi_deauthenticate(struct nxpwifi_private *priv, u8 *mac);
void nxpwifi_deauthenticate_all(struct nxpwifi_adapter *adapter);
int nxpwifi_cmd_802_11_bg_scan_query(struct host_cmd_ds_command *cmd);
struct nxpwifi_chan_freq_power *nxpwifi_get_cfp(struct nxpwifi_private *priv,
						u8 band, u16 channel, u32 freq);
u32 nxpwifi_index_to_data_rate(struct nxpwifi_private *priv,
			       u8 index, u8 ht_info);
u32 nxpwifi_index_to_acs_data_rate(struct nxpwifi_private *priv,
				   u8 index, u8 ht_info);
int nxpwifi_cmd_append_vsie_tlv(struct nxpwifi_private *priv, u16 vsie_mask,
				u8 **buffer);
u32 nxpwifi_get_active_data_rates(struct nxpwifi_private *priv,
				  u8 *rates);
u32 nxpwifi_get_supported_rates(struct nxpwifi_private *priv, u8 *rates);
u32 nxpwifi_get_rates_from_cfg80211(struct nxpwifi_private *priv,
				    u8 *rates, u8 radio_type);
u8 nxpwifi_is_rate_auto(struct nxpwifi_private *priv);
void nxpwifi_save_curr_bcn(struct nxpwifi_private *priv);
void nxpwifi_free_curr_bcn(struct nxpwifi_private *priv);
int nxpwifi_is_command_pending(struct nxpwifi_adapter *adapter);
void nxpwifi_init_priv_params(struct nxpwifi_private *priv,
			      struct net_device *dev);
void nxpwifi_set_ba_params(struct nxpwifi_private *priv);
void nxpwifi_update_ampdu_txwinsize(struct nxpwifi_adapter *pmadapter);
void nxpwifi_set_11ac_ba_params(struct nxpwifi_private *priv);
int nxpwifi_cmd_802_11_scan_ext(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *cmd,
				void *data_buf);
int nxpwifi_ret_802_11_scan_ext(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *resp);
int nxpwifi_handle_event_ext_scan_report(struct nxpwifi_private *priv,
					 void *buf);
int nxpwifi_cmd_802_11_bg_scan_config(struct nxpwifi_private *priv,
				      struct host_cmd_ds_command *cmd,
				      void *data_buf);
int nxpwifi_stop_bg_scan(struct nxpwifi_private *priv);

/* check if RA-based queuing */
static inline u8
nxpwifi_queuing_ra_based(struct nxpwifi_private *priv)
{
	/* In STA mode DA==RA; subject to future revision */
	if (priv->bss_mode == NL80211_IFTYPE_STATION &&
	    (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA))
		return false;

	return true;
}

/* copy rates from src to dest */
static inline u32
nxpwifi_copy_rates(u8 *dest, u32 pos, u8 *src, int len)
{
	int i;

	for (i = 0; i < len && src[i]; i++, pos++) {
		if (pos >= NXPWIFI_SUPPORTED_RATES)
			break;
		dest[pos] = src[i];
	}

	return pos;
}

/* return priv matching the given BSS type and number */
static inline struct nxpwifi_private *
nxpwifi_get_priv_by_id(struct nxpwifi_adapter *adapter,
		       u8 bss_num, u8 bss_type)
{
	int i;

	for (i = 0; i < adapter->priv_num; i++) {
		if (adapter->priv[i]->bss_mode ==
		    NL80211_IFTYPE_UNSPECIFIED)
			continue;
		if (adapter->priv[i]->bss_num == bss_num &&
		    adapter->priv[i]->bss_type == bss_type)
			break;
	}
	return ((i < adapter->priv_num) ? adapter->priv[i] : NULL);
}

/* return first priv matching BSS role */
static inline struct nxpwifi_private *
nxpwifi_get_priv(struct nxpwifi_adapter *adapter,
		 enum nxpwifi_bss_role bss_role)
{
	int i;

	for (i = 0; i < adapter->priv_num; i++) {
		if (bss_role == NXPWIFI_BSS_ROLE_ANY ||
		    GET_BSS_ROLE(adapter->priv[i]) == bss_role)
			break;
	}

	return ((i < adapter->priv_num) ? adapter->priv[i] : NULL);
}

/* find unused BSS number for new interface */
static inline u8
nxpwifi_get_unused_bss_num(struct nxpwifi_adapter *adapter, u8 bss_type)
{
	u8 i, j;
	int index[NXPWIFI_MAX_BSS_NUM];

	memset(index, 0, sizeof(index));
	for (i = 0; i < adapter->priv_num; i++)
		if (adapter->priv[i]->bss_type == bss_type &&
		    !(adapter->priv[i]->bss_mode ==
		      NL80211_IFTYPE_UNSPECIFIED)) {
			index[adapter->priv[i]->bss_num] = 1;
		}
	for (j = 0; j < NXPWIFI_MAX_BSS_NUM; j++)
		if (!index[j])
			return j;
	return -ENOENT;
}

/* return unused private entry for requested bss type */
static inline struct nxpwifi_private *
nxpwifi_get_unused_priv_by_bss_type(struct nxpwifi_adapter *adapter,
				    u8 bss_type)
{
	u8 i;

	for (i = 0; i < adapter->priv_num; i++)
		if (adapter->priv[i]->bss_mode ==
		   NL80211_IFTYPE_UNSPECIFIED) {
			adapter->priv[i]->bss_num =
				nxpwifi_get_unused_bss_num(adapter, bss_type);
			break;
		}

	return ((i < adapter->priv_num) ? adapter->priv[i] : NULL);
}

/* return private structure attached to netdev */
static inline struct nxpwifi_private *
nxpwifi_netdev_get_priv(struct net_device *dev)
{
	return (struct nxpwifi_private *)(*(unsigned long *)netdev_priv(dev));
}

/* return true if skb contains a management frame */
static inline bool nxpwifi_is_skb_mgmt_frame(struct sk_buff *skb)
{
	return (get_unaligned_le32(skb->data) == PKT_TYPE_MGMT);
}

/* channel closed by CSA */
static inline u8
nxpwifi_11h_get_csa_closed_channel(struct nxpwifi_private *priv)
{
	if (!priv->csa_chan)
		return 0;

	/* clear CSA if DFS switch timeout expired */
	if (time_after(jiffies, priv->csa_expire_time)) {
		priv->csa_chan = 0;
		priv->csa_expire_time = 0;
	}

	return priv->csa_chan;
}

static inline u8 nxpwifi_is_any_intf_active(struct nxpwifi_private *priv)
{
	struct nxpwifi_private *priv_tmp;
	int i;

	for (i = 0; i < priv->adapter->priv_num; i++) {
		priv_tmp = priv->adapter->priv[i];
		if ((GET_BSS_ROLE(priv_tmp) == NXPWIFI_BSS_ROLE_UAP &&
		     priv_tmp->bss_started) ||
		    (GET_BSS_ROLE(priv_tmp) == NXPWIFI_BSS_ROLE_STA &&
		     priv_tmp->media_connected))
			return 1;
	}

	return 0;
}

int nxpwifi_init_shutdown_fw(struct nxpwifi_private *priv,
			     u32 func_init_shutdown);

int nxpwifi_add_card(void *card, struct completion *fw_done,
		     struct nxpwifi_if_ops *if_ops, u8 iface_type,
		     struct device *dev);
void nxpwifi_remove_card(struct nxpwifi_adapter *adapter);

void nxpwifi_get_version(struct nxpwifi_adapter *adapter, char *version,
			 int maxlen);
int
nxpwifi_request_set_multicast_list(struct nxpwifi_private *priv,
				   struct nxpwifi_multicast_list *mcast_list);
int nxpwifi_copy_mcast_addr(struct nxpwifi_multicast_list *mlist,
			    struct net_device *dev);
int nxpwifi_wait_queue_complete(struct nxpwifi_adapter *adapter,
				struct cmd_ctrl_node *cmd_queued);
int nxpwifi_bss_start(struct nxpwifi_private *priv, struct cfg80211_bss *bss,
		      struct cfg80211_ssid *req_ssid);
int nxpwifi_cancel_hs(struct nxpwifi_private *priv, int cmd_type);
bool nxpwifi_enable_hs(struct nxpwifi_adapter *adapter);
int nxpwifi_disable_auto_ds(struct nxpwifi_private *priv);
int nxpwifi_drv_get_data_rate(struct nxpwifi_private *priv, u32 *rate);

int nxpwifi_scan_networks(struct nxpwifi_private *priv,
			  const struct nxpwifi_user_scan_cfg *user_scan_in);
int nxpwifi_set_radio(struct nxpwifi_private *priv, u8 option);

int nxpwifi_set_encode(struct nxpwifi_private *priv, struct key_params *kp,
		       const u8 *key, int key_len, u8 key_index,
		       const u8 *mac_addr, int disable);

int nxpwifi_set_gen_ie(struct nxpwifi_private *priv, const u8 *ie, int ie_len);

int nxpwifi_get_ver_ext(struct nxpwifi_private *priv, u32 version_str_sel);

int nxpwifi_remain_on_chan_cfg(struct nxpwifi_private *priv, u16 action,
			       struct ieee80211_channel *chan,
			       unsigned int duration);

int nxpwifi_get_stats_info(struct nxpwifi_private *priv,
			   struct nxpwifi_ds_get_stats *log);

int nxpwifi_reg_write(struct nxpwifi_private *priv, u32 reg_type,
		      u32 reg_offset, u32 reg_value);

int nxpwifi_reg_read(struct nxpwifi_private *priv, u32 reg_type,
		     u32 reg_offset, u32 *value);

int nxpwifi_eeprom_read(struct nxpwifi_private *priv, u16 offset, u16 bytes,
			u8 *value);

int nxpwifi_set_11n_httx_cfg(struct nxpwifi_private *priv, int data);

int nxpwifi_get_11n_httx_cfg(struct nxpwifi_private *priv, int *data);

int nxpwifi_set_tx_rate_cfg(struct nxpwifi_private *priv, int tx_rate_index);

int nxpwifi_get_tx_rate_cfg(struct nxpwifi_private *priv, int *tx_rate_index);

int nxpwifi_drv_set_power(struct nxpwifi_private *priv, u32 *ps_mode);

int nxpwifi_drv_get_driver_version(struct nxpwifi_adapter *adapter,
				   char *version, int max_len);

int nxpwifi_set_tx_power(struct nxpwifi_private *priv,
			 struct nxpwifi_power_cfg *power_cfg);

void nxpwifi_main_process(struct nxpwifi_adapter *adapter);

void nxpwifi_queue_tx_pkt(struct nxpwifi_private *priv, struct sk_buff *skb);

int nxpwifi_get_bss_info(struct nxpwifi_private *priv,
			 struct nxpwifi_bss_info *info);
int nxpwifi_fill_new_bss_desc(struct nxpwifi_private *priv,
			      struct cfg80211_bss *bss,
			      struct nxpwifi_bssdescriptor *bss_desc);
int nxpwifi_update_bss_desc_with_ie(struct nxpwifi_adapter *adapter,
				    struct nxpwifi_bssdescriptor *bss_entry);
int nxpwifi_check_network_compatibility(struct nxpwifi_private *priv,
					struct nxpwifi_bssdescriptor *bss_desc);

u8 nxpwifi_chan_type_to_sec_chan_offset(enum nl80211_channel_type chan_type);
u8 nxpwifi_get_chan_type(struct nxpwifi_private *priv);

struct wireless_dev *nxpwifi_add_virtual_intf(struct wiphy *wiphy,
					      const char *name,
					      unsigned char name_assign_type,
					      enum nl80211_iftype type,
					      struct vif_params *params);
int nxpwifi_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev);

int nxpwifi_add_wowlan_magic_pkt_filter(struct nxpwifi_adapter *adapter);

int nxpwifi_set_mgmt_ies(struct nxpwifi_private *priv,
			 struct cfg80211_beacon_data *data);
int nxpwifi_del_mgmt_ies(struct nxpwifi_private *priv);
u8 *nxpwifi_11d_code_2_region(u8 code);
void nxpwifi_init_11h_params(struct nxpwifi_private *priv);
int nxpwifi_is_11h_active(struct nxpwifi_private *priv);
int nxpwifi_11h_activate(struct nxpwifi_private *priv, bool flag);
void nxpwifi_11h_process_join(struct nxpwifi_private *priv, u8 **buffer,
			      struct nxpwifi_bssdescriptor *bss_desc);
int nxpwifi_11h_handle_event_chanswann(struct nxpwifi_private *priv);

extern const struct ethtool_ops nxpwifi_ethtool_ops;

void nxpwifi_del_all_sta_list(struct nxpwifi_private *priv);
void nxpwifi_del_sta_entry(struct nxpwifi_private *priv, const u8 *mac);
void
nxpwifi_set_sta_ht_cap(struct nxpwifi_private *priv, const u8 *ies,
		       int ies_len, struct nxpwifi_sta_node *node);
struct nxpwifi_sta_node *
nxpwifi_add_sta_entry(struct nxpwifi_private *priv, const u8 *mac);
struct nxpwifi_sta_node *
nxpwifi_get_sta_entry(struct nxpwifi_private *priv, const u8 *mac);
struct nxpwifi_sta_node *
nxpwifi_get_sta_entry_rcu(struct nxpwifi_private *priv, const u8 *mac);
int nxpwifi_init_channel_scan_gap(struct nxpwifi_adapter *adapter);

int nxpwifi_cmd_issue_chan_report_request(struct nxpwifi_private *priv,
					  struct host_cmd_ds_command *cmd,
					  void *data_buf);
int nxpwifi_11h_handle_chanrpt_ready(struct nxpwifi_private *priv,
				     struct sk_buff *skb);

void nxpwifi_parse_tx_status_event(struct nxpwifi_private *priv,
				   void *event_body);

struct sk_buff *
nxpwifi_clone_skb_for_tx_status(struct nxpwifi_private *priv,
				struct sk_buff *skb, u8 flag, u64 *cookie);
void nxpwifi_reset_conn_state_work(struct wiphy *wiphy, struct wiphy_work *work);
void nxpwifi_dfs_cac_work(struct wiphy *wiphy, struct wiphy_work *work);
void nxpwifi_dfs_chan_sw_work(struct wiphy *wiphy, struct wiphy_work *work);
void nxpwifi_abort_cac(struct nxpwifi_private *priv);
int nxpwifi_stop_radar_detection(struct nxpwifi_private *priv,
				 struct cfg80211_chan_def *chandef);
int nxpwifi_11h_handle_radar_detected(struct nxpwifi_private *priv,
				      struct sk_buff *skb);

void nxpwifi_hist_data_set(struct nxpwifi_private *priv, u8 rx_rate, s8 snr,
			   s8 nflr);
void nxpwifi_hist_data_reset(struct nxpwifi_private *priv);
void nxpwifi_hist_data_add(struct nxpwifi_private *priv,
			   u8 rx_rate, s8 snr, s8 nflr);
u8 nxpwifi_adjust_data_rate(struct nxpwifi_private *priv,
			    u8 rx_rate, u8 ht_info);

void nxpwifi_drv_info_dump(struct nxpwifi_adapter *adapter);
void nxpwifi_prepare_fw_dump_info(struct nxpwifi_adapter *adapter);
void nxpwifi_upload_device_dump(struct nxpwifi_adapter *adapter);
void *nxpwifi_alloc_dma_align_buf(int rx_len, gfp_t flags);
void nxpwifi_fw_dump_event(struct nxpwifi_private *priv);
int nxpwifi_get_wakeup_reason(struct nxpwifi_private *priv, u16 action,
			      int cmd_type,
			      struct nxpwifi_ds_wakeup_reason *wakeup_reason);
int nxpwifi_get_chan_info(struct nxpwifi_private *priv,
			  struct nxpwifi_channel_band *channel_band);
void nxpwifi_coex_ampdu_rxwinsize(struct nxpwifi_adapter *adapter);
void nxpwifi_11n_delba(struct nxpwifi_private *priv, int tid);
int nxpwifi_send_domain_info_cmd_fw(struct wiphy *wiphy, enum nl80211_band band);
int nxpwifi_set_mac_address(struct nxpwifi_private *priv,
			    struct net_device *dev,
			    bool external, u8 *new_mac);
void nxpwifi_devdump_tmo_func(unsigned long function_context);

#ifdef CONFIG_DEBUG_FS
void nxpwifi_debugfs_init(void);
void nxpwifi_debugfs_remove(void);

void nxpwifi_dev_debugfs_init(struct nxpwifi_private *priv);
void nxpwifi_dev_debugfs_remove(struct nxpwifi_private *priv);
#endif
int nxpwifi_reinit_sw(struct nxpwifi_adapter *adapter);
void nxpwifi_shutdown_sw(struct nxpwifi_adapter *adapter);
bool nxpwifi_is_valid_region_code(enum nxpwifi_region_code code);
#endif /* !_NXPWIFI_MAIN_H_ */
