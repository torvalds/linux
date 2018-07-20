/* SPDX-License-Identifier: GPL-2.0 */
#ifndef WILC_WFI_NETDEVICE
#define WILC_WFI_NETDEVICE

#include <linux/tcp.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <net/ieee80211_radiotap.h>
#include <linux/if_arp.h>

#include "host_interface.h"
#include "wilc_wlan.h"

#define FLOW_CONTROL_LOWER_THRESHOLD		128
#define FLOW_CONTROL_UPPER_THRESHOLD		256

#define WILC_MAX_NUM_PMKIDS			16
#define PMKID_LEN				16
#define PMKID_FOUND				1
#define NUM_STA_ASSOCIATED			8

#define NUM_REG_FRAME				2

#define TCP_ACK_FILTER_LINK_SPEED_THRESH	54
#define DEFAULT_LINK_SPEED			72

#define GET_PKT_OFFSET(a) (((a) >> 22) & 0x1ff)

struct wilc_wfi_stats {
	unsigned long rx_packets;
	unsigned long tx_packets;
	unsigned long rx_bytes;
	unsigned long tx_bytes;
	u64 rx_time;
	u64 tx_time;

};

/*
 * This structure is private to each device. It is used to pass
 * packets in and out, so there is place for a packet
 */

struct wilc_wfi_key {
	u8 *key;
	u8 *seq;
	int key_len;
	int seq_len;
	u32 cipher;
};

struct wilc_wfi_wep_key {
	u8 *key;
	u8 key_len;
	u8 key_idx;
};

struct sta_info {
	u8 sta_associated_bss[MAX_NUM_STA][ETH_ALEN];
};

/*Parameters needed for host interface for  remaining on channel*/
struct wilc_wfi_p2p_listen_params {
	struct ieee80211_channel *listen_ch;
	u32 listen_duration;
	u64 listen_cookie;
	u32 listen_session_id;
};

struct wilc_priv {
	struct wireless_dev *wdev;
	struct cfg80211_scan_request *scan_req;

	struct wilc_wfi_p2p_listen_params remain_on_ch_params;
	u64 tx_cookie;

	bool cfg_scanning;
	u32 rcvd_ch_cnt;

	u8 associated_bss[ETH_ALEN];
	struct sta_info assoc_stainfo;
	struct sk_buff *skb;
	struct net_device *dev;
	struct host_if_drv *hif_drv;
	struct host_if_pmkid_attr pmkid_list;
	u8 wep_key[4][WLAN_KEY_LEN_WEP104];
	u8 wep_key_len[4];
	/* The real interface that the monitor is on */
	struct net_device *real_ndev;
	struct wilc_wfi_key *wilc_gtk[MAX_NUM_STA];
	struct wilc_wfi_key *wilc_ptk[MAX_NUM_STA];
	u8 wilc_groupkey;
	/* mutexes */
	struct mutex scan_req_lock;
	bool p2p_listen_state;

};

struct frame_reg {
	u16 type;
	bool reg;
};

struct wilc_vif {
	u8 idx;
	u8 iftype;
	int monitor_flag;
	int mac_opened;
	struct frame_reg frame_reg[NUM_REG_FRAME];
	struct net_device_stats netstats;
	struct wilc *wilc;
	u8 src_addr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	struct host_if_drv *hif_drv;
	struct net_device *ndev;
	u8 mode;
	u8 ifc_id;
};

struct wilc {
	const struct wilc_hif_func *hif_func;
	int io_type;
	int mac_status;
	int gpio;
	bool initialized;
	int dev_irq_num;
	int close;
	u8 vif_num;
	struct wilc_vif *vif[NUM_CONCURRENT_IFC];
	u8 open_ifcs;
	/*protect head of transmit queue*/
	struct mutex txq_add_to_head_cs;
	/*protect txq_entry_t transmit queue*/
	spinlock_t txq_spinlock;
	/*protect rxq_entry_t receiver queue*/
	struct mutex rxq_cs;
	struct mutex hif_cs;

	struct completion cfg_event;
	struct completion sync_event;
	struct completion txq_event;
	struct completion txq_thread_started;

	struct task_struct *txq_thread;

	int quit;
	int cfg_frame_in_use;
	struct wilc_cfg_frame cfg_frame;
	u32 cfg_frame_offset;
	int cfg_seq_no;

	u8 *rx_buffer;
	u32 rx_buffer_offset;
	u8 *tx_buffer;

	struct txq_entry_t txq_head;
	int txq_entries;

	struct rxq_entry_t rxq_head;

	const struct firmware *firmware;

	struct device *dev;
	bool suspend_event;

	struct rf_info dummy_statistics;
};

struct wilc_wfi_mon_priv {
	struct net_device *real_ndev;
};

void wilc_frmw_to_linux(struct wilc *wilc, u8 *buff, u32 size, u32 pkt_offset);
void wilc_mac_indicate(struct wilc *wilc);
void wilc_netdev_cleanup(struct wilc *wilc);
int wilc_netdev_init(struct wilc **wilc, struct device *dev, int io_type,
		     const struct wilc_hif_func *ops);
void wilc_wfi_mgmt_rx(struct wilc *wilc, u8 *buff, u32 size);
int wilc_wlan_set_bssid(struct net_device *wilc_netdev, u8 *bssid, u8 mode);

#endif
