/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

#ifndef __IWM_H__
#define __IWM_H__

#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>

#include "debug.h"
#include "hal.h"
#include "umac.h"
#include "lmac.h"
#include "eeprom.h"
#include "trace.h"

#define IWM_COPYRIGHT "Copyright(c) 2009 Intel Corporation"
#define IWM_AUTHOR "<ilw@linux.intel.com>"

#define IWM_SRC_LMAC	UMAC_HDI_IN_SOURCE_FHRX
#define IWM_SRC_UDMA	UMAC_HDI_IN_SOURCE_UDMA
#define IWM_SRC_UMAC	UMAC_HDI_IN_SOURCE_FW
#define IWM_SRC_NUM	3

#define IWM_POWER_INDEX_MIN	0
#define IWM_POWER_INDEX_MAX	5
#define IWM_POWER_INDEX_DEFAULT	3

struct iwm_conf {
	u32 sdio_ior_timeout;
	unsigned long calib_map;
	unsigned long expected_calib_map;
	u8 ct_kill_entry;
	u8 ct_kill_exit;
	bool reset_on_fatal_err;
	bool auto_connect;
	bool wimax_not_present;
	bool enable_qos;
	u32 mode;

	u32 power_index;
	u32 frag_threshold;
	u32 rts_threshold;
	bool cts_to_self;

	u32 assoc_timeout;
	u32 roam_timeout;
	u32 wireless_mode;

	u8 ibss_band;
	u8 ibss_channel;

	u8 mac_addr[ETH_ALEN];
};

enum {
	COEX_MODE_SA = 1,
	COEX_MODE_XOR,
	COEX_MODE_CM,
	COEX_MODE_MAX,
};

struct iwm_if_ops;
struct iwm_wifi_cmd;

struct pool_entry {
	int id;		/* group id */
	int sid;	/* super group id */
	int min_pages;	/* min capacity in pages */
	int max_pages;	/* max capacity in pages */
	int alloc_pages;	/* allocated # of pages. incresed by driver */
	int total_freed_pages;	/* total freed # of pages. incresed by UMAC */
};

struct spool_entry {
	int id;
	int max_pages;
	int alloc_pages;
};

struct iwm_tx_credit {
	spinlock_t lock;
	int pool_nr;
	unsigned long full_pools_map; /* bitmap for # of filled tx pools */
	struct pool_entry pools[IWM_MACS_OUT_GROUPS];
	struct spool_entry spools[IWM_MACS_OUT_SGROUPS];
};

struct iwm_notif {
	struct list_head pending;
	u32 cmd_id;
	void *cmd;
	u8 src;
	void *buf;
	unsigned long buf_size;
};

struct iwm_tid_info {
	__le16 last_seq_num;
	bool stopped;
	struct mutex mutex;
};

struct iwm_sta_info {
	u8 addr[ETH_ALEN];
	bool valid;
	bool qos;
	u8 color;
	struct iwm_tid_info tid_info[IWM_UMAC_TID_NR];
};

struct iwm_tx_info {
	u8 sta;
	u8 color;
	u8 tid;
};

struct iwm_rx_info {
	unsigned long rx_size;
	unsigned long rx_buf_size;
};

#define IWM_NUM_KEYS 4

struct iwm_umac_key_hdr {
	u8 mac[ETH_ALEN];
	u8 key_idx;
	u8 multicast; /* BCast encrypt & BCast decrypt of frames FROM mac */
} __packed;

struct iwm_key {
	struct iwm_umac_key_hdr hdr;
	u32 cipher;
	u8 key[WLAN_MAX_KEY_LEN];
	u8 seq[IW_ENCODE_SEQ_MAX_SIZE];
	int key_len;
	int seq_len;
};

#define IWM_RX_ID_HASH  0xff
#define IWM_RX_ID_GET_HASH(id) ((id) % IWM_RX_ID_HASH)

#define IWM_STA_TABLE_NUM	16
#define IWM_TX_LIST_SIZE	64
#define IWM_RX_LIST_SIZE        256

#define IWM_SCAN_ID_MAX 0xff

#define IWM_STATUS_READY		0
#define IWM_STATUS_SCANNING		1
#define IWM_STATUS_SCAN_ABORTING	2
#define IWM_STATUS_SME_CONNECTING	3
#define IWM_STATUS_ASSOCIATED		4
#define IWM_STATUS_RESETTING		5

struct iwm_tx_queue {
	int id;
	struct sk_buff_head queue;
	struct sk_buff_head stopped_queue;
	spinlock_t lock;
	struct workqueue_struct *wq;
	struct work_struct worker;
	u8 concat_buf[IWM_HAL_CONCATENATE_BUF_SIZE];
	int concat_count;
	u8 *concat_ptr;
};

/* Queues 0 ~ 3 for AC data, 5 for iPAN */
#define IWM_TX_QUEUES		5
#define IWM_TX_DATA_QUEUES	4
#define IWM_TX_CMD_QUEUE	4

struct iwm_bss_info {
	struct list_head node;
	struct cfg80211_bss *cfg_bss;
	struct iwm_umac_notif_bss_info *bss;
};

typedef int (*iwm_handler)(struct iwm_priv *priv, u8 *buf,
			   unsigned long buf_size, struct iwm_wifi_cmd *cmd);

#define IWM_WATCHDOG_PERIOD	(6 * HZ)

struct iwm_priv {
	struct wireless_dev *wdev;
	struct iwm_if_ops *bus_ops;

	struct iwm_conf conf;

	unsigned long status;

	struct list_head pending_notif;
	wait_queue_head_t notif_queue;

	wait_queue_head_t nonwifi_queue;

	unsigned long calib_done_map;
	struct {
		u8 *buf;
		u32 size;
	} calib_res[CALIBRATION_CMD_NUM];

	struct iwm_umac_profile *umac_profile;
	bool umac_profile_active;

	u8 bssid[ETH_ALEN];
	u8 channel;
	u16 rate;
	u32 txpower;

	struct iwm_sta_info sta_table[IWM_STA_TABLE_NUM];
	struct list_head bss_list;

	void (*nonwifi_rx_handlers[UMAC_HDI_IN_OPCODE_NONWIFI_MAX])
	(struct iwm_priv *priv, u8 *buf, unsigned long buf_size);

	const iwm_handler *umac_handlers;
	const iwm_handler *lmac_handlers;
	DECLARE_BITMAP(lmac_handler_map, LMAC_COMMAND_ID_NUM);
	DECLARE_BITMAP(umac_handler_map, LMAC_COMMAND_ID_NUM);
	DECLARE_BITMAP(udma_handler_map, LMAC_COMMAND_ID_NUM);

	struct list_head wifi_pending_cmd;
	struct list_head nonwifi_pending_cmd;
	u16 wifi_seq_num;
	u8 nonwifi_seq_num;
	spinlock_t cmd_lock;

	u32 core_enabled;

	u8 scan_id;
	struct cfg80211_scan_request *scan_request;

	struct sk_buff_head rx_list;
	struct list_head rx_tickets;
	spinlock_t ticket_lock;
	struct list_head rx_packets[IWM_RX_ID_HASH];
	spinlock_t packet_lock[IWM_RX_ID_HASH];
	struct workqueue_struct *rx_wq;
	struct work_struct rx_worker;

	struct iwm_tx_credit tx_credit;
	struct iwm_tx_queue txq[IWM_TX_QUEUES];

	struct iwm_key keys[IWM_NUM_KEYS];
	s8 default_key;

	DECLARE_BITMAP(wifi_ntfy, WIFI_IF_NTFY_MAX);
	wait_queue_head_t wifi_ntfy_queue;

	wait_queue_head_t mlme_queue;

	struct iw_statistics wstats;
	struct delayed_work stats_request;
	struct delayed_work disconnect;
	struct delayed_work ct_kill_delay;

	struct iwm_debugfs dbg;

	u8 *eeprom;
	struct timer_list watchdog;
	struct work_struct reset_worker;
	struct work_struct auth_retry_worker;
	struct mutex mutex;

	u8 *req_ie;
	int req_ie_len;
	u8 *resp_ie;
	int resp_ie_len;

	struct iwm_fw_error_hdr *last_fw_err;
	char umac_version[8];
	char lmac_version[8];

	char private[0] __attribute__((__aligned__(NETDEV_ALIGN)));
};

static inline void *iwm_private(struct iwm_priv *iwm)
{
	BUG_ON(!iwm);
	return &iwm->private;
}

#define hw_to_iwm(h) (h->iwm)
#define iwm_to_dev(i) (wiphy_dev(i->wdev->wiphy))
#define iwm_to_wiphy(i) (i->wdev->wiphy)
#define wiphy_to_iwm(w) (struct iwm_priv *)(wiphy_priv(w))
#define iwm_to_wdev(i) (i->wdev)
#define wdev_to_iwm(w) (struct iwm_priv *)(wdev_priv(w))
#define iwm_to_ndev(i) (i->wdev->netdev)
#define ndev_to_iwm(n) (wdev_to_iwm(n->ieee80211_ptr))
#define skb_to_rx_info(s) ((struct iwm_rx_info *)(s->cb))
#define skb_to_tx_info(s) ((struct iwm_tx_info *)s->cb)

void *iwm_if_alloc(int sizeof_bus, struct device *dev,
		   struct iwm_if_ops *if_ops);
void iwm_if_free(struct iwm_priv *iwm);
int iwm_if_add(struct iwm_priv *iwm);
void iwm_if_remove(struct iwm_priv *iwm);
int iwm_mode_to_nl80211_iftype(int mode);
int iwm_priv_init(struct iwm_priv *iwm);
void iwm_priv_deinit(struct iwm_priv *iwm);
void iwm_reset(struct iwm_priv *iwm);
void iwm_resetting(struct iwm_priv *iwm);
void iwm_tx_credit_init_pools(struct iwm_priv *iwm,
			      struct iwm_umac_notif_alive *alive);
int iwm_tx_credit_alloc(struct iwm_priv *iwm, int id, int nb);
int iwm_notif_send(struct iwm_priv *iwm, struct iwm_wifi_cmd *cmd,
		   u8 cmd_id, u8 source, u8 *buf, unsigned long buf_size);
int iwm_notif_handle(struct iwm_priv *iwm, u32 cmd, u8 source, long timeout);
void iwm_init_default_profile(struct iwm_priv *iwm,
			      struct iwm_umac_profile *profile);
void iwm_link_on(struct iwm_priv *iwm);
void iwm_link_off(struct iwm_priv *iwm);
int iwm_up(struct iwm_priv *iwm);
int iwm_down(struct iwm_priv *iwm);

/* TX API */
int iwm_tid_to_queue(u16 tid);
void iwm_tx_credit_inc(struct iwm_priv *iwm, int id, int total_freed_pages);
void iwm_tx_worker(struct work_struct *work);
int iwm_xmit_frame(struct sk_buff *skb, struct net_device *netdev);

/* RX API */
void iwm_rx_setup_handlers(struct iwm_priv *iwm);
int iwm_rx_handle(struct iwm_priv *iwm, u8 *buf, unsigned long buf_size);
int iwm_rx_handle_resp(struct iwm_priv *iwm, u8 *buf, unsigned long buf_size,
		       struct iwm_wifi_cmd *cmd);
void iwm_rx_free(struct iwm_priv *iwm);

#endif
