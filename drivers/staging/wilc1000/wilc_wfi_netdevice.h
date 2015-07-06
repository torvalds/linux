/*!
 *  @file	wilc_wfi_netdevice.h
 *  @brief	Definitions for the network module
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
#ifndef WILC_WFI_NETDEVICE
#define WILC_WFI_NETDEVICE

/* These are the flags in the statusword */
#define WILC_WFI_RX_INTR 0x0001
#define WILC_WFI_TX_INTR 0x0002

/* Default timeout period */
#define WILC_WFI_TIMEOUT 5   /* In jiffies */
#define WILC_MAX_NUM_PMKIDS  16
#define PMKID_LEN  16
#define PMKID_FOUND 1
 #define NUM_STA_ASSOCIATED 8

#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h> /* kmalloc() */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/interrupt.h> /* mark_bh */
#include <linux/time.h>
#include <linux/in.h>
#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/tcp.h>         /* struct tcphdr */
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
#include <linux/wireless.h>     /* tony, 2013-06-12 */

#define FLOW_CONTROL_LOWER_THRESHOLD	128
#define FLOW_CONTROL_UPPER_THRESHOLD	256

/*iftype*/
enum stats_flags {
	WILC_WFI_RX_PKT = 1 << 0,
	WILC_WFI_TX_PKT = 1 << 1,
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

#define RX_BH_KTHREAD 0
#define RX_BH_WORK_QUEUE 1
#define RX_BH_THREADED_IRQ 2
#define num_reg_frame 2
/*
 * If you use RX_BH_WORK_QUEUE on LPC3131: You may lose the first interrupt on
 * LPC3131 which is important to get the MAC start status when you are blocked inside
 * linux_wlan_firmware_download() which blocks mac_open().
 */
#if defined (NM73131_0_BOARD)
 #define RX_BH_TYPE  RX_BH_KTHREAD
#elif defined (PANDA_BOARD)
 #define RX_BH_TYPE  RX_BH_THREADED_IRQ
#else
 #define RX_BH_TYPE  RX_BH_KTHREAD
#endif

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

#ifdef WILC_P2P
/*Parameters needed for host interface for  remaining on channel*/
struct wilc_wfi_p2pListenParams {
	struct ieee80211_channel *pstrListenChan;
	enum nl80211_channel_type tenuChannelType;
	u32 u32ListenDuration;
	u64 u64ListenCookie;
	u32 u32ListenSessionID;
};

#endif  /*WILC_P2P*/

struct WILC_WFI_priv {
	struct wireless_dev *wdev;
	struct cfg80211_scan_request *pstrScanReq;

	#ifdef WILC_P2P
	struct wilc_wfi_p2pListenParams strRemainOnChanParams;
	u64 u64tx_cookie;
	#endif

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
	WILC_WFIDrvHandle hWILCWFIDrv;
	WILC_WFIDrvHandle hWILCWFIDrv_2;
	tstrHostIFpmkidAttr pmkid_list;
	struct WILC_WFI_stats netstats;
	u8 WILC_WFI_wep_default;
	u8 WILC_WFI_wep_key[4][WLAN_KEY_LEN_WEP104];
	u8 WILC_WFI_wep_key_len[4];
	struct net_device *real_ndev;   /* The real interface that the monitor is on */
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

#define NUM_CONCURRENT_IFC 2
typedef struct {
	uint8_t aSrcAddress[ETH_ALEN];
	uint8_t aBSSID[ETH_ALEN];
	uint32_t drvHandler;
	struct net_device *wilc_netdev;
} tstrInterfaceInfo;
typedef struct {
	int mac_status;
	int wilc1000_initialized;
	#if (!defined WILC_SDIO) || (defined WILC_SDIO_IRQ_GPIO)
	unsigned short dev_irq_num;
	#endif
	wilc_wlan_oup_t oup;
	int close;
	uint8_t u8NoIfcs;
	tstrInterfaceInfo strInterfaceInfo[NUM_CONCURRENT_IFC];
	uint8_t open_ifcs;
	struct mutex txq_cs;

	/*Added by Amr - BugID_4720*/
	struct mutex txq_add_to_head_cs;
	spinlock_t txq_spinlock;

	struct mutex rxq_cs;
	struct mutex hif_cs;

	/* struct mutex txq_event; */
	struct semaphore rxq_event;
	struct semaphore cfg_event;
	struct semaphore sync_event;

	struct semaphore txq_event;
	/* struct completion txq_event; */

#if (RX_BH_TYPE == RX_BH_WORK_QUEUE)
	struct work_struct rx_work_queue;
#elif (RX_BH_TYPE == RX_BH_KTHREAD)
	struct task_struct *rx_bh_thread;
	struct semaphore rx_sem;
#endif
	struct semaphore rxq_thread_started;
	struct semaphore txq_thread_started;

	struct task_struct *rxq_thread;
	struct task_struct *txq_thread;

	unsigned char eth_src_address[NUM_CONCURRENT_IFC][6];
	/* unsigned char eth_dst_address[6]; */

	const struct firmware *wilc_firmware; /* Bug 4703 */

	struct net_device *real_ndev;
#ifdef WILC_SDIO
	int already_claim;
	struct sdio_func *wilc_sdio_func;
#else
	struct spi_device *wilc_spidev;
#endif

} linux_wlan_t;

typedef struct {
	uint8_t u8IfIdx;
	u8 iftype;
	int monitor_flag;
	int mac_opened;
	#ifdef WILC_P2P
	struct_frame_reg g_struct_frame_reg[num_reg_frame];
	#endif
	struct net_device *wilc_netdev;
	struct net_device_stats netstats;

} perInterface_wlan_t;

struct WILC_WFI_mon_priv {
	struct net_device *real_ndev;
};

extern struct net_device *WILC_WFI_devs[];

#endif
