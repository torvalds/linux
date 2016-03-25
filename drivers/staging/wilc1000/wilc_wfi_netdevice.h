/*!
 *  @file	wilc_wfi_netdevice.h
 *  @brief	Definitions for the network module
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
#ifndef WILC_WFI_NETDEVICE
#define WILC_WFI_NETDEVICE

#define WILC_WFI_RX_INTR 0x0001
#define WILC_WFI_TX_INTR 0x0002

#define WILC_WFI_TIMEOUT 5
#define WILC_MAX_NUM_PMKIDS  16
#define PMKID_LEN  16
#define PMKID_FOUND 1
 #define NUM_STA_ASSOCIATED 8

#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <net/ieee80211_radiotap.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <asm/checksum.h>
#include "host_interface.h"
#include "wilc_wlan.h"
#include <linux/wireless.h>

#define FLOW_CONTROL_LOWER_THRESHOLD	128
#define FLOW_CONTROL_UPPER_THRESHOLD	256

enum stats_flags {
	WILC_WFI_RX_PKT = BIT(0),
	WILC_WFI_TX_PKT = BIT(1),
};

struct WILC_WFI_stats {
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

#define num_reg_frame 2

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
	u8 au8Sta_AssociatedBss[MAX_NUM_STA][ETH_ALEN];
};

/*Parameters needed for host interface for  remaining on channel*/
struct wilc_wfi_p2pListenParams {
	struct ieee80211_channel *pstrListenChan;
	enum nl80211_channel_type tenuChannelType;
	u32 u32ListenDuration;
	u64 u64ListenCookie;
	u32 u32ListenSessionID;
};

struct wilc_priv {
	struct wireless_dev *wdev;
	struct cfg80211_scan_request *pstrScanReq;

	struct wilc_wfi_p2pListenParams strRemainOnChanParams;
	u64 u64tx_cookie;

	bool bCfgScanning;
	u32 u32RcvdChCount;

	u8 au8AssociatedBss[ETH_ALEN];
	struct sta_info assoc_stainfo;
	struct net_device_stats stats;
	u8 monitor_flag;
	int status;
	struct WILC_WFI_packet *ppool;
	struct WILC_WFI_packet *rx_queue; /* List of incoming packets */
	int rx_int_enabled;
	int tx_packetlen;
	u8 *tx_packetdata;
	struct sk_buff *skb;
	spinlock_t lock;
	struct net_device *dev;
	struct napi_struct napi;
	struct host_if_drv *hif_drv;
	struct host_if_pmkid_attr pmkid_list;
	struct WILC_WFI_stats netstats;
	u8 WILC_WFI_wep_key[4][WLAN_KEY_LEN_WEP104];
	u8 WILC_WFI_wep_key_len[4];
	/* The real interface that the monitor is on */
	struct net_device *real_ndev;
	struct wilc_wfi_key *wilc_gtk[MAX_NUM_STA];
	struct wilc_wfi_key *wilc_ptk[MAX_NUM_STA];
	u8 wilc_groupkey;
	/* semaphores */
	struct semaphore SemHandleUpdateStats;
	struct semaphore hSemScanReq;
	/*  */
	bool gbAutoRateAdjusted;

	bool bInP2PlistenState;

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
	struct frame_reg frame_reg[num_reg_frame];
	struct net_device_stats netstats;
	struct wilc *wilc;
	u8 src_addr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	struct host_if_drv *hif_drv;
	struct net_device *ndev;
	u8 mode;
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

	struct semaphore txq_add_to_head_cs;
	spinlock_t txq_spinlock;

	struct mutex rxq_cs;
	struct mutex hif_cs;

	struct semaphore cfg_event;
	struct semaphore sync_event;
	struct semaphore txq_event;

	struct semaphore txq_thread_started;

	struct task_struct *txq_thread;

	int quit;
	int cfg_frame_in_use;
	struct wilc_cfg_frame cfg_frame;
	u32 cfg_frame_offset;
	int cfg_seq_no;

	u8 *rx_buffer;
	u32 rx_buffer_offset;
	u8 *tx_buffer;

	unsigned long txq_spinlock_flags;

	struct txq_entry_t *txq_head;
	struct txq_entry_t *txq_tail;
	int txq_entries;
	int txq_exit;

	struct rxq_entry_t *rxq_head;
	struct rxq_entry_t *rxq_tail;
	int rxq_entries;
	int rxq_exit;

	unsigned char eth_src_address[NUM_CONCURRENT_IFC][6];

	const struct firmware *firmware;

	struct device *dev;
	bool suspend_event;

	struct rf_info dummy_statistics;
};

struct WILC_WFI_mon_priv {
	struct net_device *real_ndev;
};

int wilc1000_wlan_init(struct net_device *dev, struct wilc_vif *vif);

void wilc_frmw_to_linux(struct wilc *wilc, u8 *buff, u32 size, u32 pkt_offset);
void wilc_mac_indicate(struct wilc *wilc, int flag);
int wilc_lock_timeout(struct wilc *wilc, void *, u32 timeout);
void wilc_netdev_cleanup(struct wilc *wilc);
int wilc_netdev_init(struct wilc **wilc, struct device *, int io_type, int gpio,
		     const struct wilc_hif_func *ops);
void wilc1000_wlan_deinit(struct net_device *dev);
void WILC_WFI_mgmt_rx(struct wilc *wilc, u8 *buff, u32 size);
int wilc_wlan_get_firmware(struct net_device *dev);
int wilc_wlan_set_bssid(struct net_device *wilc_netdev, u8 *bssid, u8 mode);

#endif
