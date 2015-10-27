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
	struct host_if_drv *hWILCWFIDrv;
	struct host_if_pmkid_attr pmkid_list;
	struct WILC_WFI_stats netstats;
	u8 WILC_WFI_wep_default;
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

typedef struct {
	u16 frame_type;
	bool reg;

} struct_frame_reg;

struct wilc_vif {
	u8 src_addr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	struct host_if_drv *hif_drv;
	struct net_device *ndev;
};

struct wilc {
	int mac_status;
	bool initialized;
	#if (!defined WILC_SDIO) || (defined WILC_SDIO_IRQ_GPIO)
	unsigned short dev_irq_num;
	#endif
	int close;
	u8 vif_num;
	struct wilc_vif vif[NUM_CONCURRENT_IFC];
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

	unsigned char eth_src_address[NUM_CONCURRENT_IFC][6];

	const struct firmware *firmware;

#ifdef WILC_SDIO
	struct sdio_func *wilc_sdio_func;
#else
	struct spi_device *wilc_spidev;
#endif
};

typedef struct {
	u8 u8IfIdx;
	u8 iftype;
	int monitor_flag;
	int mac_opened;
	struct_frame_reg g_struct_frame_reg[num_reg_frame];
	struct net_device *wilc_netdev;
	struct net_device_stats netstats;
	struct wilc *wilc;
} perInterface_wlan_t;

struct WILC_WFI_mon_priv {
	struct net_device *real_ndev;
};

extern struct wilc *g_linux_wlan;
extern struct net_device *WILC_WFI_devs[];
void frmw_to_linux(struct wilc *wilc, u8 *buff, u32 size, u32 pkt_offset);
void linux_wlan_mac_indicate(struct wilc *wilc, int flag);
void linux_wlan_rx_complete(void);
void linux_wlan_dbg(u8 *buff);
int linux_wlan_lock_timeout(void *vp, u32 timeout);
void wl_wlan_cleanup(void);
int wilc_netdev_init(struct wilc **wilc);
void wilc1000_wlan_deinit(struct net_device *dev);
void WILC_WFI_mgmt_rx(struct wilc *wilc, u8 *buff, u32 size);
u16 Set_machw_change_vir_if(struct net_device *dev, bool bValue);
#endif
