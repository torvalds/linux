/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL1271_H__
#define __WL1271_H__

#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <net/mac80211.h>

#include "wl1271_conf.h"
#include "wl1271_ini.h"

#define DRIVER_NAME "wl1271"
#define DRIVER_PREFIX DRIVER_NAME ": "

enum {
	DEBUG_NONE	= 0,
	DEBUG_IRQ	= BIT(0),
	DEBUG_SPI	= BIT(1),
	DEBUG_BOOT	= BIT(2),
	DEBUG_MAILBOX	= BIT(3),
	DEBUG_TESTMODE	= BIT(4),
	DEBUG_EVENT	= BIT(5),
	DEBUG_TX	= BIT(6),
	DEBUG_RX	= BIT(7),
	DEBUG_SCAN	= BIT(8),
	DEBUG_CRYPT	= BIT(9),
	DEBUG_PSM	= BIT(10),
	DEBUG_MAC80211	= BIT(11),
	DEBUG_CMD	= BIT(12),
	DEBUG_ACX	= BIT(13),
	DEBUG_SDIO	= BIT(14),
	DEBUG_FILTERS   = BIT(15),
	DEBUG_ADHOC     = BIT(16),
	DEBUG_ALL	= ~0,
};

#define DEBUG_LEVEL (DEBUG_NONE)

#define DEBUG_DUMP_LIMIT 1024

#define wl1271_error(fmt, arg...) \
	printk(KERN_ERR DRIVER_PREFIX "ERROR " fmt "\n", ##arg)

#define wl1271_warning(fmt, arg...) \
	printk(KERN_WARNING DRIVER_PREFIX "WARNING " fmt "\n", ##arg)

#define wl1271_notice(fmt, arg...) \
	printk(KERN_INFO DRIVER_PREFIX fmt "\n", ##arg)

#define wl1271_info(fmt, arg...) \
	printk(KERN_DEBUG DRIVER_PREFIX fmt "\n", ##arg)

#define wl1271_debug(level, fmt, arg...) \
	do { \
		if (level & DEBUG_LEVEL) \
			printk(KERN_DEBUG DRIVER_PREFIX fmt "\n", ##arg); \
	} while (0)

#define wl1271_dump(level, prefix, buf, len)	\
	do { \
		if (level & DEBUG_LEVEL) \
			print_hex_dump(KERN_DEBUG, DRIVER_PREFIX prefix, \
				       DUMP_PREFIX_OFFSET, 16, 1,	\
				       buf,				\
				       min_t(size_t, len, DEBUG_DUMP_LIMIT), \
				       0);				\
	} while (0)

#define wl1271_dump_ascii(level, prefix, buf, len)	\
	do { \
		if (level & DEBUG_LEVEL) \
			print_hex_dump(KERN_DEBUG, DRIVER_PREFIX prefix, \
				       DUMP_PREFIX_OFFSET, 16, 1,	\
				       buf,				\
				       min_t(size_t, len, DEBUG_DUMP_LIMIT), \
				       true);				\
	} while (0)

#define WL1271_DEFAULT_RX_CONFIG (CFG_UNI_FILTER_EN |	\
				  CFG_BSSID_FILTER_EN | \
				  CFG_MC_FILTER_EN)

#define WL1271_DEFAULT_RX_FILTER (CFG_RX_RCTS_ACK | CFG_RX_PRSP_EN |  \
				  CFG_RX_MGMT_EN | CFG_RX_DATA_EN |   \
				  CFG_RX_CTL_EN | CFG_RX_BCN_EN |     \
				  CFG_RX_AUTH_EN | CFG_RX_ASSOC_EN)

#define WL1271_FW_NAME "wl1271-fw.bin"
#define WL1271_NVS_NAME "wl1271-nvs.bin"

#define WL1271_TX_SECURITY_LO16(s) ((u16)((s) & 0xffff))
#define WL1271_TX_SECURITY_HI32(s) ((u32)(((s) >> 16) & 0xffffffff))

#define WL1271_CIPHER_SUITE_GEM 0x00147201

#define WL1271_BUSY_WORD_CNT 1
#define WL1271_BUSY_WORD_LEN (WL1271_BUSY_WORD_CNT * sizeof(u32))

#define WL1271_ELP_HW_STATE_ASLEEP 0
#define WL1271_ELP_HW_STATE_IRQ    1

#define WL1271_DEFAULT_BEACON_INT  100
#define WL1271_DEFAULT_DTIM_PERIOD 1

#define ACX_TX_DESCRIPTORS         32

#define WL1271_AGGR_BUFFER_SIZE (4 * PAGE_SIZE)

enum wl1271_state {
	WL1271_STATE_OFF,
	WL1271_STATE_ON,
	WL1271_STATE_PLT,
};

enum wl1271_partition_type {
	PART_DOWN,
	PART_WORK,
	PART_DRPW,

	PART_TABLE_LEN
};

struct wl1271_partition {
	u32 size;
	u32 start;
};

struct wl1271_partition_set {
	struct wl1271_partition mem;
	struct wl1271_partition reg;
	struct wl1271_partition mem2;
	struct wl1271_partition mem3;
};

struct wl1271;

/* FIXME: I'm not sure about this structure name */
struct wl1271_chip {
	u32 id;
	char fw_ver[21];
};

struct wl1271_stats {
	struct acx_statistics *fw_stats;
	unsigned long fw_stats_update;

	unsigned int retry_count;
	unsigned int excessive_retries;
};

struct wl1271_debugfs {
	struct dentry *rootdir;
	struct dentry *fw_statistics;

	struct dentry *tx_internal_desc_overflow;

	struct dentry *rx_out_of_mem;
	struct dentry *rx_hdr_overflow;
	struct dentry *rx_hw_stuck;
	struct dentry *rx_dropped;
	struct dentry *rx_fcs_err;
	struct dentry *rx_xfr_hint_trig;
	struct dentry *rx_path_reset;
	struct dentry *rx_reset_counter;

	struct dentry *dma_rx_requested;
	struct dentry *dma_rx_errors;
	struct dentry *dma_tx_requested;
	struct dentry *dma_tx_errors;

	struct dentry *isr_cmd_cmplt;
	struct dentry *isr_fiqs;
	struct dentry *isr_rx_headers;
	struct dentry *isr_rx_mem_overflow;
	struct dentry *isr_rx_rdys;
	struct dentry *isr_irqs;
	struct dentry *isr_tx_procs;
	struct dentry *isr_decrypt_done;
	struct dentry *isr_dma0_done;
	struct dentry *isr_dma1_done;
	struct dentry *isr_tx_exch_complete;
	struct dentry *isr_commands;
	struct dentry *isr_rx_procs;
	struct dentry *isr_hw_pm_mode_changes;
	struct dentry *isr_host_acknowledges;
	struct dentry *isr_pci_pm;
	struct dentry *isr_wakeups;
	struct dentry *isr_low_rssi;

	struct dentry *wep_addr_key_count;
	struct dentry *wep_default_key_count;
	/* skipping wep.reserved */
	struct dentry *wep_key_not_found;
	struct dentry *wep_decrypt_fail;
	struct dentry *wep_packets;
	struct dentry *wep_interrupt;

	struct dentry *pwr_ps_enter;
	struct dentry *pwr_elp_enter;
	struct dentry *pwr_missing_bcns;
	struct dentry *pwr_wake_on_host;
	struct dentry *pwr_wake_on_timer_exp;
	struct dentry *pwr_tx_with_ps;
	struct dentry *pwr_tx_without_ps;
	struct dentry *pwr_rcvd_beacons;
	struct dentry *pwr_power_save_off;
	struct dentry *pwr_enable_ps;
	struct dentry *pwr_disable_ps;
	struct dentry *pwr_fix_tsf_ps;
	/* skipping cont_miss_bcns_spread for now */
	struct dentry *pwr_rcvd_awake_beacons;

	struct dentry *mic_rx_pkts;
	struct dentry *mic_calc_failure;

	struct dentry *aes_encrypt_fail;
	struct dentry *aes_decrypt_fail;
	struct dentry *aes_encrypt_packets;
	struct dentry *aes_decrypt_packets;
	struct dentry *aes_encrypt_interrupt;
	struct dentry *aes_decrypt_interrupt;

	struct dentry *event_heart_beat;
	struct dentry *event_calibration;
	struct dentry *event_rx_mismatch;
	struct dentry *event_rx_mem_empty;
	struct dentry *event_rx_pool;
	struct dentry *event_oom_late;
	struct dentry *event_phy_transmit_error;
	struct dentry *event_tx_stuck;

	struct dentry *ps_pspoll_timeouts;
	struct dentry *ps_upsd_timeouts;
	struct dentry *ps_upsd_max_sptime;
	struct dentry *ps_upsd_max_apturn;
	struct dentry *ps_pspoll_max_apturn;
	struct dentry *ps_pspoll_utilization;
	struct dentry *ps_upsd_utilization;

	struct dentry *rxpipe_rx_prep_beacon_drop;
	struct dentry *rxpipe_descr_host_int_trig_rx_data;
	struct dentry *rxpipe_beacon_buffer_thres_host_int_trig_rx_data;
	struct dentry *rxpipe_missed_beacon_host_int_trig_rx_data;
	struct dentry *rxpipe_tx_xfr_host_int_trig_rx_data;

	struct dentry *tx_queue_len;

	struct dentry *retry_count;
	struct dentry *excessive_retries;
	struct dentry *gpio_power;
};

#define NUM_TX_QUEUES              4
#define NUM_RX_PKT_DESC            8

/* FW status registers */
struct wl1271_fw_status {
	__le32 intr;
	u8  fw_rx_counter;
	u8  drv_rx_counter;
	u8  reserved;
	u8  tx_results_counter;
	__le32 rx_pkt_descs[NUM_RX_PKT_DESC];
	__le32 tx_released_blks[NUM_TX_QUEUES];
	__le32 fw_localtime;
	__le32 padding[2];
} __packed;

struct wl1271_rx_mem_pool_addr {
	u32 addr;
	u32 addr_extra;
};

struct wl1271_scan {
	struct cfg80211_scan_request *req;
	bool *scanned_ch;
	bool failed;
	u8 state;
	u8 ssid[IW_ESSID_MAX_SIZE+1];
	size_t ssid_len;
};

struct wl1271_if_operations {
	void (*read)(struct wl1271 *wl, int addr, void *buf, size_t len,
		     bool fixed);
	void (*write)(struct wl1271 *wl, int addr, void *buf, size_t len,
		     bool fixed);
	void (*reset)(struct wl1271 *wl);
	void (*init)(struct wl1271 *wl);
	int (*power)(struct wl1271 *wl, bool enable);
	struct device* (*dev)(struct wl1271 *wl);
	void (*enable_irq)(struct wl1271 *wl);
	void (*disable_irq)(struct wl1271 *wl);
};

struct wl1271 {
	struct platform_device *plat_dev;
	struct ieee80211_hw *hw;
	bool mac80211_registered;

	void *if_priv;

	struct wl1271_if_operations *if_ops;

	void (*set_power)(bool enable);
	int irq;
	int ref_clock;

	spinlock_t wl_lock;

	enum wl1271_state state;
	struct mutex mutex;

#define WL1271_FLAG_STA_RATES_CHANGED  (0)
#define WL1271_FLAG_STA_ASSOCIATED     (1)
#define WL1271_FLAG_JOINED             (2)
#define WL1271_FLAG_GPIO_POWER         (3)
#define WL1271_FLAG_TX_QUEUE_STOPPED   (4)
#define WL1271_FLAG_IN_ELP             (5)
#define WL1271_FLAG_PSM                (6)
#define WL1271_FLAG_PSM_REQUESTED      (7)
#define WL1271_FLAG_IRQ_PENDING        (8)
#define WL1271_FLAG_IRQ_RUNNING        (9)
#define WL1271_FLAG_IDLE              (10)
#define WL1271_FLAG_IDLE_REQUESTED    (11)
#define WL1271_FLAG_PSPOLL_FAILURE    (12)
#define WL1271_FLAG_STA_STATE_SENT    (13)
	unsigned long flags;

	struct wl1271_partition_set part;

	struct wl1271_chip chip;

	int cmd_box_addr;
	int event_box_addr;

	u8 *fw;
	size_t fw_len;
	struct wl1271_nvs_file *nvs;
	size_t nvs_len;

	s8 hw_pg_ver;

	u8 bssid[ETH_ALEN];
	u8 mac_addr[ETH_ALEN];
	u8 bss_type;
	u8 set_bss_type;
	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;
	int channel;

	struct wl1271_acx_mem_map *target_mem_map;

	/* Accounting for allocated / available TX blocks on HW */
	u32 tx_blocks_freed[NUM_TX_QUEUES];
	u32 tx_blocks_available;
	u32 tx_results_count;

	/* Transmitted TX packets counter for chipset interface */
	u32 tx_packets_count;

	/* Time-offset between host and chipset clocks */
	s64 time_offset;

	/* Session counter for the chipset */
	int session_counter;

	/* Frames scheduled for transmission, not handled yet */
	struct sk_buff_head tx_queue;

	struct work_struct tx_work;

	/* Pending TX frames */
	struct sk_buff *tx_frames[ACX_TX_DESCRIPTORS];
	int tx_frames_cnt;

	/* Security sequence number counters */
	u8 tx_security_last_seq;
	s64 tx_security_seq;

	/* FW Rx counter */
	u32 rx_counter;

	/* Rx memory pool address */
	struct wl1271_rx_mem_pool_addr rx_mem_pool_addr;

	/* Intermediate buffer, used for packet aggregation */
	u8 *aggr_buf;

	/* The target interrupt mask */
	struct work_struct irq_work;

	/* Hardware recovery work */
	struct work_struct recovery_work;

	/* The mbox event mask */
	u32 event_mask;

	/* Mailbox pointers */
	u32 mbox_ptr[2];

	/* Are we currently scanning */
	struct wl1271_scan scan;
	struct delayed_work scan_complete_work;

	/* Our association ID */
	u16 aid;

	/* currently configured rate set */
	u32 sta_rate_set;
	u32 basic_rate_set;
	u32 basic_rate;
	u32 rate_set;

	/* The current band */
	enum ieee80211_band band;

	/* Beaconing interval (needed for ad-hoc) */
	u32 beacon_int;

	/* Default key (for WEP) */
	u32 default_key;

	unsigned int filters;
	unsigned int rx_config;
	unsigned int rx_filter;

	struct completion *elp_compl;
	struct delayed_work elp_work;
	struct delayed_work pspoll_work;

	/* counter for ps-poll delivery failures */
	int ps_poll_failures;

	/* retry counter for PSM entries */
	u8 psm_entry_retry;

	/* in dBm */
	int power_level;

	int rssi_thold;
	int last_rssi_event;

	struct wl1271_stats stats;
	struct wl1271_debugfs debugfs;

	__le32 buffer_32;
	u32 buffer_cmd;
	u32 buffer_busyword[WL1271_BUSY_WORD_CNT];

	struct wl1271_fw_status *fw_status;
	struct wl1271_tx_hw_res_if *tx_res_if;

	struct ieee80211_vif *vif;

	/* Current chipset configuration */
	struct conf_drv_settings conf;

	bool sg_enabled;

	bool enable_11a;

	struct list_head list;

	/* Most recently reported noise in dBm */
	s8 noise;
};

int wl1271_plt_start(struct wl1271 *wl);
int wl1271_plt_stop(struct wl1271 *wl);

#define JOIN_TIMEOUT 5000 /* 5000 milliseconds to join */

#define SESSION_COUNTER_MAX 7 /* maximum value for the session counter */

#define WL1271_DEFAULT_POWER_LEVEL 0

#define WL1271_TX_QUEUE_LOW_WATERMARK  10
#define WL1271_TX_QUEUE_HIGH_WATERMARK 25

/* WL1271 needs a 200ms sleep after power on, and a 20ms sleep before power
   on in case is has been shut down shortly before */
#define WL1271_PRE_POWER_ON_SLEEP 20 /* in miliseconds */
#define WL1271_POWER_ON_SLEEP 200 /* in miliseconds */

#endif
