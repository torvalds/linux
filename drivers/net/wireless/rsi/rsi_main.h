/**
 * Copyright (c) 2014 Redpine Signals Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __RSI_MAIN_H__
#define __RSI_MAIN_H__

#include <linux/string.h>
#include <linux/skbuff.h>
#include <net/mac80211.h>
#include <net/rsi_91x.h>

struct rsi_sta {
	struct ieee80211_sta *sta;
	s16 sta_id;
	u16 seq_start[IEEE80211_NUM_TIDS];
	bool start_tx_aggr[IEEE80211_NUM_TIDS];
};

struct rsi_hw;

#include "rsi_ps.h"

#define ERR_ZONE                        BIT(0)  /* For Error Msgs             */
#define INFO_ZONE                       BIT(1)  /* For General Status Msgs    */
#define INIT_ZONE                       BIT(2)  /* For Driver Init Seq Msgs   */
#define MGMT_TX_ZONE                    BIT(3)  /* For TX Mgmt Path Msgs      */
#define MGMT_RX_ZONE                    BIT(4)  /* For RX Mgmt Path Msgs      */
#define DATA_TX_ZONE                    BIT(5)  /* For TX Data Path Msgs      */
#define DATA_RX_ZONE                    BIT(6)  /* For RX Data Path Msgs      */
#define FSM_ZONE                        BIT(7)  /* For State Machine Msgs     */
#define ISR_ZONE                        BIT(8)  /* For Interrupt Msgs         */

enum RSI_FSM_STATES {
	FSM_FW_NOT_LOADED,
	FSM_CARD_NOT_READY,
	FSM_COMMON_DEV_PARAMS_SENT,
	FSM_BOOT_PARAMS_SENT,
	FSM_EEPROM_READ_MAC_ADDR,
	FSM_EEPROM_READ_RF_TYPE,
	FSM_RESET_MAC_SENT,
	FSM_RADIO_CAPS_SENT,
	FSM_BB_RF_PROG_SENT,
	FSM_MAC_INIT_DONE,

	NUM_FSM_STATES
};

extern u32 rsi_zone_enabled;
extern __printf(2, 3) void rsi_dbg(u32 zone, const char *fmt, ...);

#define RSI_MAX_BANDS			2
#define RSI_MAX_VIFS                    3
#define NUM_EDCA_QUEUES                 4
#define IEEE80211_ADDR_LEN              6
#define FRAME_DESC_SZ                   16
#define MIN_802_11_HDR_LEN              24
#define RSI_DEF_KEEPALIVE               90
#define RSI_WOW_KEEPALIVE                5
#define RSI_BCN_MISS_THRESHOLD           24

#define DATA_QUEUE_WATER_MARK           400
#define MIN_DATA_QUEUE_WATER_MARK       300
#define MULTICAST_WATER_MARK            200
#define MAC_80211_HDR_FRAME_CONTROL     0
#define WME_NUM_AC                      4
#define NUM_SOFT_QUEUES                 6
#define MAX_HW_QUEUES                   12
#define INVALID_QUEUE                   0xff
#define MAX_CONTINUOUS_VO_PKTS          8
#define MAX_CONTINUOUS_VI_PKTS          4

/* Hardware queue info */
#define BROADCAST_HW_Q			9
#define MGMT_HW_Q			10
#define BEACON_HW_Q			11

#define IEEE80211_MGMT_FRAME            0x00
#define IEEE80211_CTL_FRAME             0x04

#define RSI_MAX_ASSOC_STAS		32
#define IEEE80211_QOS_TID               0x0f
#define IEEE80211_NONQOS_TID            16

#define MAX_DEBUGFS_ENTRIES             4

#define TID_TO_WME_AC(_tid) (      \
	((_tid) == 0 || (_tid) == 3) ? BE_Q : \
	((_tid) < 3) ? BK_Q : \
	((_tid) < 6) ? VI_Q : \
	VO_Q)

#define WME_AC(_q) (    \
	((_q) == BK_Q) ? IEEE80211_AC_BK : \
	((_q) == BE_Q) ? IEEE80211_AC_BE : \
	((_q) == VI_Q) ? IEEE80211_AC_VI : \
	IEEE80211_AC_VO)

/* WoWLAN flags */
#define RSI_WOW_ENABLED			BIT(0)
#define RSI_WOW_NO_CONNECTION		BIT(1)

#define RSI_MAX_RX_PKTS		64

enum rsi_dev_model {
	RSI_DEV_9113 = 0,
	RSI_DEV_9116
};

struct version_info {
	u16 major;
	u16 minor;
	u8 release_num;
	u8 patch_num;
	union {
		struct {
			u8 fw_ver[8];
		} info;
	} ver;
} __packed;

struct skb_info {
	s8 rssi;
	u32 flags;
	u16 channel;
	s8 tid;
	s8 sta_id;
	u8 internal_hdr_size;
	struct ieee80211_vif *vif;
	u8 vap_id;
	bool have_key;
};

enum edca_queue {
	BK_Q,
	BE_Q,
	VI_Q,
	VO_Q,
	MGMT_SOFT_Q,
	MGMT_BEACON_Q
};

struct security_info {
	u32 ptk_cipher;
	u32 gtk_cipher;
};

struct wmm_qinfo {
	s32 weight;
	s32 wme_params;
	s32 pkt_contended;
	s32 txop;
};

struct transmit_q_stats {
	u32 total_tx_pkt_send[NUM_EDCA_QUEUES + 2];
	u32 total_tx_pkt_freed[NUM_EDCA_QUEUES + 2];
};

#define MAX_BGSCAN_CHANNELS_DUAL_BAND	38
#define MAX_BGSCAN_PROBE_REQ_LEN	0x64
#define RSI_DEF_BGSCAN_THRLD		0x0
#define RSI_DEF_ROAM_THRLD		0xa
#define RSI_BGSCAN_PERIODICITY		0x1e
#define RSI_ACTIVE_SCAN_TIME		0x14
#define RSI_PASSIVE_SCAN_TIME		0x46
#define RSI_CHANNEL_SCAN_TIME		20
struct rsi_bgscan_params {
	u16 bgscan_threshold;
	u16 roam_threshold;
	u16 bgscan_periodicity;
	u8 num_bgscan_channels;
	u8 two_probe;
	u16 active_scan_duration;
	u16 passive_scan_duration;
};

struct vif_priv {
	bool is_ht;
	bool sgi;
	u16 seq_start;
	int vap_id;
};

struct rsi_event {
	atomic_t event_condition;
	wait_queue_head_t event_queue;
};

struct rsi_thread {
	void (*thread_function)(void *);
	struct completion completion;
	struct task_struct *task;
	struct rsi_event event;
	atomic_t thread_done;
};

struct cqm_info {
	s8 last_cqm_event_rssi;
	int rssi_thold;
	u32 rssi_hyst;
};

enum rsi_dfs_regions {
	RSI_REGION_FCC = 0,
	RSI_REGION_ETSI,
	RSI_REGION_TELEC,
	RSI_REGION_WORLD
};

struct rsi_9116_features {
	u8 pll_mode;
	u8 rf_type;
	u8 wireless_mode;
	u8 afe_type;
	u8 enable_ppe;
	u8 dpd;
	u32 sifs_tx_enable;
	u32 ps_options;
};

struct rsi_rate_config {
	u32 configured_mask;	/* configured by mac80211 bits 0-11=legacy 12+ mcs */
	u16 fixed_hw_rate;
	bool fixed_enabled;
};

struct rsi_common {
	struct rsi_hw *priv;
	struct vif_priv vif_info[RSI_MAX_VIFS];

	void *coex_cb;
	bool mgmt_q_block;
	struct version_info lmac_ver;

	struct rsi_thread tx_thread;
	struct sk_buff_head tx_queue[NUM_EDCA_QUEUES + 2];
	struct completion wlan_init_completion;
	/* Mutex declaration */
	struct mutex mutex;
	/* Mutex used for tx thread */
	struct mutex tx_lock;
	/* Mutex used for rx thread */
	struct mutex rx_lock;
	u8 endpoint;

	/* Channel/band related */
	u8 band;
	u8 num_supp_bands;
	u8 channel_width;

	u16 rts_threshold;
	u32 bitrate_mask[RSI_MAX_BANDS];
	struct rsi_rate_config rate_config[RSI_MAX_BANDS];

	u8 rf_reset;
	struct transmit_q_stats tx_stats;
	struct security_info secinfo;
	struct wmm_qinfo tx_qinfo[NUM_EDCA_QUEUES];
	struct ieee80211_tx_queue_params edca_params[NUM_EDCA_QUEUES];
	u8 mac_addr[IEEE80211_ADDR_LEN];

	/* state related */
	u32 fsm_state;
	bool init_done;
	u8 bb_rf_prog_count;
	bool iface_down;

	/* Generic */
	u8 channel;
	u8 *rx_data_pkt;
	u8 mac_id;
	u8 radio_id;
	u16 rate_pwr[20];

	/* WMM algo related */
	u8 selected_qnum;
	u32 pkt_cnt;
	u8 min_weight;

	/* bgscan related */
	struct cqm_info cqm_info;

	bool hw_data_qs_blocked;
	u8 driver_mode;
	u8 coex_mode;
	u16 oper_mode;
	u8 lp_ps_handshake_mode;
	u8 ulp_ps_handshake_mode;
	u8 uapsd_bitmap;
	u8 rf_power_val;
	u8 wlan_rf_power_mode;
	u8 obm_ant_sel_val;
	int tx_power;
	u8 ant_in_use;
	/* Mutex used for writing packet to bus */
	struct mutex tx_bus_mutex;
	bool hibernate_resume;
	bool reinit_hw;
	u8 wow_flags;
	u16 beacon_interval;
	u8 dtim_cnt;

	/* AP mode parameters */
	u8 beacon_enabled;
	u16 beacon_cnt;
	struct rsi_sta stations[RSI_MAX_ASSOC_STAS + 1];
	int num_stations;
	int max_stations;
	struct ieee80211_key_conf *key;

	/* Wi-Fi direct mode related */
	bool p2p_enabled;
	struct timer_list roc_timer;
	struct ieee80211_vif *roc_vif;

	bool eapol4_confirm;
	bool bt_defer_attach;
	void *bt_adapter;

	struct cfg80211_scan_request *hwscan;
	struct rsi_bgscan_params bgscan;
	struct rsi_9116_features w9116_features;
	u8 bgscan_en;
	u8 mac_ops_resumed;
};

struct eepromrw_info {
	u32 offset;
	u32 length;
	u8  write;
	u16 eeprom_erase;
	u8 data[480];
};

struct eeprom_read {
	u16 length;
	u16 off_set;
};

struct rsi_hw {
	struct rsi_common *priv;
	enum rsi_dev_model device_model;
	struct ieee80211_hw *hw;
	struct ieee80211_vif *vifs[RSI_MAX_VIFS];
	struct ieee80211_tx_queue_params edca_params[NUM_EDCA_QUEUES];
	struct ieee80211_supported_band sbands[NUM_NL80211_BANDS];

	struct device *device;
	u8 sc_nvifs;

	enum rsi_host_intf rsi_host_intf;
	u16 block_size;
	enum ps_state ps_state;
	struct rsi_ps_info ps_info;
	spinlock_t ps_lock; /*To protect power save config*/
	u32 usb_buffer_status_reg;
#ifdef CONFIG_RSI_DEBUGFS
	struct rsi_debugfs *dfsentry;
	u8 num_debugfs_entries;
#endif
	char *fw_file_name;
	struct timer_list bl_cmd_timer;
	bool blcmd_timer_expired;
	u32 flash_capacity;
	struct eepromrw_info eeprom;
	u32 interrupt_status;
	u8 dfs_region;
	char country[2];
	void *rsi_dev;
	struct rsi_host_intf_ops *host_intf_ops;
	int (*check_hw_queue_status)(struct rsi_hw *adapter, u8 q_num);
	int (*determine_event_timeout)(struct rsi_hw *adapter);
};

void rsi_print_version(struct rsi_common *common);

struct rsi_host_intf_ops {
	int (*read_pkt)(struct rsi_hw *adapter, u8 *pkt, u32 len);
	int (*write_pkt)(struct rsi_hw *adapter, u8 *pkt, u32 len);
	int (*master_access_msword)(struct rsi_hw *adapter, u16 ms_word);
	int (*read_reg_multiple)(struct rsi_hw *adapter, u32 addr,
				 u8 *data, u16 count);
	int (*write_reg_multiple)(struct rsi_hw *adapter, u32 addr,
				  u8 *data, u16 count);
	int (*master_reg_read)(struct rsi_hw *adapter, u32 addr,
			       u32 *read_buf, u16 size);
	int (*master_reg_write)(struct rsi_hw *adapter,
				unsigned long addr, unsigned long data,
				u16 size);
	int (*load_data_master_write)(struct rsi_hw *adapter, u32 addr,
				      u32 instructions_size, u16 block_size,
				      u8 *fw);
	int (*reinit_device)(struct rsi_hw *adapter);
	int (*ta_reset)(struct rsi_hw *adapter);
};

enum rsi_host_intf rsi_get_host_intf(void *priv);
void rsi_set_bt_context(void *priv, void *bt_context);
void rsi_attach_bt(struct rsi_common *common);

#endif
