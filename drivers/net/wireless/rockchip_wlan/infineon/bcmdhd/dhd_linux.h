/*
 * DHD Linux header file (dhd_linux exports for cfg80211 and other components)
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_linux.h 701006 2017-05-23 08:25:04Z $
 */

/* wifi platform functions for power, interrupt and pre-alloc, either
 * from Android-like platform device data, or Broadcom wifi platform
 * device data.
 *
 */
#ifndef __DHD_LINUX_H__
#define __DHD_LINUX_H__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 1))
#include <linux/time64.h>
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 1)) */

#include <dngl_stats.h>
#include <dhd.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 1))
#include <linux/time64.h>
#define get_monotonic_boottime ktime_get_boottime_ts64
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 1)) */
/* Linux wireless extension support */
#if defined(WL_WIRELESS_EXT)
#include <wl_iw.h>
#endif /* defined(WL_WIRELESS_EXT) */
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif /* defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND) */
#if defined(CONFIG_WIFI_CONTROL_FUNC)
#include <linux/wlan_plat.h>
#endif // endif
#ifdef PCIE_FULL_DONGLE
#include <etd.h>
#endif /* PCIE_FULL_DONGLE */
#ifdef WL_MONITOR
#include <bcmmsgbuf.h>
#define MAX_RADIOTAP_SIZE      256 /* Maximum size to hold HE Radiotap header format */
#define MAX_MON_PKT_SIZE       (4096 + MAX_RADIOTAP_SIZE)
#endif /* WL_MONITOR */

#define FILE_DUMP_MAX_WAIT_TIME 4000

#define htod32(i) (i)
#define htod16(i) (i)
#define dtoh32(i) (i)
#define dtoh16(i) (i)
#define htodchanspec(i) (i)
#define dtohchanspec(i) (i)

#ifdef BLOCK_IPV6_PACKET
#define HEX_PREF_STR	"0x"
#define UNI_FILTER_STR	"010000000000"
#define ZERO_ADDR_STR	"000000000000"
#define ETHER_TYPE_STR	"0000"
#define IPV6_FILTER_STR	"20"
#define ZERO_TYPE_STR	"00"
#endif /* BLOCK_IPV6_PACKET */

typedef struct dhd_if_event {
	struct list_head	list;
	wl_event_data_if_t	event;
	char			name[IFNAMSIZ+1];
	uint8			mac[ETHER_ADDR_LEN];
} dhd_if_event_t;

/* Interface control information */
typedef struct dhd_if {
	struct dhd_info *info;			/* back pointer to dhd_info */
	/* OS/stack specifics */
	struct net_device *net;
	int				idx;			/* iface idx in dongle */
	uint			subunit;		/* subunit */
	uint8			mac_addr[ETHER_ADDR_LEN];	/* assigned MAC address */
	bool			set_macaddress;
	bool			set_multicast;
	uint8			bssidx;			/* bsscfg index for the interface */
	bool			attached;		/* Delayed attachment when unset */
	bool			txflowcontrol;	/* Per interface flow control indicator */
	char			name[IFNAMSIZ+1]; /* linux interface name */
	char			dngl_name[IFNAMSIZ+1]; /* corresponding dongle interface name */
	struct net_device_stats stats;
	struct list_head sta_list;		/* sll of associated stations */
	spinlock_t	sta_list_lock;		/* lock for manipulating sll */
	uint32  ap_isolate;			/* ap-isolation settings */
#ifdef DHD_L2_FILTER
	bool parp_enable;
	bool parp_discard;
	bool parp_allnode;
	arp_table_t *phnd_arp_table;
	/* for Per BSS modification */
	bool dhcp_unicast;
	bool block_ping;
	bool grat_arp;
	bool block_tdls;
#endif /* DHD_L2_FILTER */
#ifdef DHD_MCAST_REGEN
	bool mcast_regen_bss_enable;
#endif // endif
	bool rx_pkt_chainable;		/* set all rx packet to chainable config by default */
	cumm_ctr_t cumm_ctr;		/* cummulative queue length of child flowrings */
	uint8 tx_paths_active;
	bool del_in_progress;
	bool static_if;			/* used to avoid some operations on static_if */
#ifdef DHD_4WAYM4_FAIL_DISCONNECT
	struct delayed_work m4state_work;
	atomic_t m4state;
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */
#ifdef DHD_POST_EAPOL_M1_AFTER_ROAM_EVT
	bool recv_reassoc_evt;
	bool post_roam_evt;
#endif /* DHD_POST_EAPOL_M1_AFTER_ROAM_EVT */
#ifdef DHDTCPSYNC_FLOOD_BLK
	uint32 tsync_rcvd;
	uint32 tsyncack_txed;
	u64 last_sync;
	struct work_struct  blk_tsfl_work;
#endif /* DHDTCPSYNC_FLOOD_BLK */
#if defined(BCMSDIO)
	int role;
#endif /* BCMSDIO */
} dhd_if_t;

struct ipv6_work_info_t {
	uint8			if_idx;
	char			ipv6_addr[IPV6_ADDR_LEN];
	unsigned long		event;
};

typedef struct dhd_dump {
	uint8 *buf;
	int bufsize;
	uint8 *hscb_buf;
	int hscb_bufsize;
} dhd_dump_t;
#ifdef DNGL_AXI_ERROR_LOGGING
typedef struct dhd_axi_error_dump {
	ulong fault_address;
	uint32 axid;
	struct hnd_ext_trap_axi_error_v1 etd_axi_error_v1;
} dhd_axi_error_dump_t;
#endif /* DNGL_AXI_ERROR_LOGGING */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
struct dhd_rx_tx_work {
	struct work_struct work;
	struct sk_buff *skb;
	struct net_device *net;
	struct dhd_pub *pub;
};
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#if defined(DHD_LB)
#if !defined(PCIE_FULL_DONGLE)
#error "DHD Loadbalancing only supported on PCIE_FULL_DONGLE"
#endif /* !PCIE_FULL_DONGLE */
#endif /* DHD_LB */

#if defined(DHD_LB_RXP) || defined(DHD_LB_RXC) || defined(DHD_LB_TXC) || \
	defined(DHD_LB_STATS)
#if !defined(DHD_LB)
#error "DHD loadbalance derivatives are supported only if DHD_LB is defined"
#endif /* !DHD_LB */
#endif /* DHD_LB_RXP || DHD_LB_RXC || DHD_LB_TXC || DHD_LB_STATS */

#if defined(DHD_LB)
/* Dynamic CPU selection for load balancing */
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <asm/atomic.h>

#if !defined(DHD_LB_PRIMARY_CPUS)
#define DHD_LB_PRIMARY_CPUS     0x0 /* Big CPU coreids mask */
#endif // endif
#if !defined(DHD_LB_SECONDARY_CPUS)
#define DHD_LB_SECONDARY_CPUS   0xFE /* Little CPU coreids mask */
#endif // endif

#define HIST_BIN_SIZE	9

#if defined(DHD_LB_TXP)
/* Pkttag not compatible with PROP_TXSTATUS or WLFC */
typedef struct dhd_tx_lb_pkttag_fr {
	struct net_device *net;
	int ifidx;
} dhd_tx_lb_pkttag_fr_t;

#define DHD_LB_TX_PKTTAG_SET_NETDEV(tag, netdevp)	((tag)->net = netdevp)
#define DHD_LB_TX_PKTTAG_NETDEV(tag)			((tag)->net)

#define DHD_LB_TX_PKTTAG_SET_IFIDX(tag, ifidx)	((tag)->ifidx = ifidx)
#define DHD_LB_TX_PKTTAG_IFIDX(tag)		((tag)->ifidx)
#endif /* DHD_LB_TXP */

#endif /* DHD_LB */

#ifdef FILTER_IE
#define FILTER_IE_PATH "/etc/wifi/filter_ie"
#define FILTER_IE_BUFSZ 1024 /* ioc buffsize for FILTER_IE */
#define FILE_BLOCK_READ_SIZE 256
#define WL_FILTER_IE_IOV_HDR_SIZE OFFSETOF(wl_filter_ie_iov_v1_t, tlvs)
#endif /* FILTER_IE */

#define NULL_CHECK(p, s, err)  \
			do { \
				if (!(p)) { \
					printk("NULL POINTER (%s) : %s\n", __FUNCTION__, (s)); \
					err = BCME_ERROR; \
					return err; \
				} \
			} while (0)

#if !defined(CONFIG_WIFI_CONTROL_FUNC)
#define WLAN_PLAT_NODFS_FLAG	0x01
#define WLAN_PLAT_AP_FLAG	0x02
struct wifi_platform_data {
	int (*set_power)(int val);
	int (*set_reset)(int val);
	int (*set_carddetect)(int val);
	void *(*mem_prealloc)(int section, unsigned long size);
	int (*get_mac_addr)(unsigned char *buf);
#ifdef BCMSDIO
	int (*get_wake_irq)(void);
#endif // endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 58)) || defined(CUSTOM_COUNTRY_CODE)
	void *(*get_country_code)(char *ccode, u32 flags);
#else /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 58)) || defined (CUSTOM_COUNTRY_CODE) */
	void *(*get_country_code)(char *ccode);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 58)) */
};
#endif /* CONFIG_WIFI_CONTROL_FUNC */

#define DHD_REGISTRATION_TIMEOUT  12000  /* msec : allowed time to finished dhd registration */

typedef struct wifi_adapter_info {
	const char	*name;
	uint		irq_num;
	uint		intr_flags;
	const char	*fw_path;
	const char	*nv_path;
	void		*wifi_plat_data;	/* wifi ctrl func, for backward compatibility */
	uint		bus_type;
	uint		bus_num;
	uint		slot_num;
#if defined(BT_OVER_SDIO)
	const char	*btfw_path;
#endif /* defined (BT_OVER_SDIO) */
} wifi_adapter_info_t;

typedef struct bcmdhd_wifi_platdata {
	uint				num_adapters;
	wifi_adapter_info_t	*adapters;
} bcmdhd_wifi_platdata_t;

/** Per STA params. A list of dhd_sta objects are managed in dhd_if */
typedef struct dhd_sta {
	cumm_ctr_t cumm_ctr;    /* cummulative queue length of child flowrings */
	uint16 flowid[NUMPRIO]; /* allocated flow ring ids (by priority) */
	void * ifp;             /* associated dhd_if */
	struct ether_addr ea;   /* stations ethernet mac address */
	struct list_head list;  /* link into dhd_if::sta_list */
	int idx;                /* index of self in dhd_pub::sta_pool[] */
	int ifidx;              /* index of interface in dhd */
} dhd_sta_t;
typedef dhd_sta_t dhd_sta_pool_t;

#ifdef DHD_4WAYM4_FAIL_DISCONNECT
typedef enum {
	M3_RXED,
	M4_TXFAILED
} msg_4way_state_t;
#define MAX_4WAY_TIMEOUT_MS 2000
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */

#ifdef DHD_SEND_HANG_PRIVCMD_ERRORS
extern uint32 report_hang_privcmd_err;
#endif /* DHD_SEND_HANG_PRIVCMD_ERRORS */

#if defined(ARGOS_NOTIFY_CB)
int argos_register_notifier_init(struct net_device *net);
int argos_register_notifier_deinit(void);

extern int sec_argos_register_notifier(struct notifier_block *n, char *label);
extern int sec_argos_unregister_notifier(struct notifier_block *n, char *label);

typedef struct {
	struct net_device *wlan_primary_netdev;
	int argos_rps_cpus_enabled;
} argos_rps_ctrl;

#define RPS_TPUT_THRESHOLD		300
#define DELAY_TO_CLEAR_RPS_CPUS		300
#endif // endif

#if defined(BT_OVER_SDIO)
extern void wl_android_set_wifi_on_flag(bool enable);
#endif /* BT_OVER_SDIO */

#ifdef DHD_LOG_DUMP
/* 0: DLD_BUF_TYPE_GENERAL, 1: DLD_BUF_TYPE_PRESERVE
* 2: DLD_BUF_TYPE_SPECIAL
*/
#define DLD_BUFFER_NUM 3

#ifndef CUSTOM_LOG_DUMP_BUFSIZE_MB
#define CUSTOM_LOG_DUMP_BUFSIZE_MB	4 /* DHD_LOG_DUMP_BUF_SIZE 4 MB static memory in kernel */
#endif /* CUSTOM_LOG_DUMP_BUFSIZE_MB */

#define LOG_DUMP_TOTAL_BUFSIZE (1024 * 1024 * CUSTOM_LOG_DUMP_BUFSIZE_MB)

/*
 * Below are different sections that use the prealloced buffer
 * and sum of the sizes of these should not cross LOG_DUMP_TOTAL_BUFSIZE
 */
#define LOG_DUMP_GENERAL_MAX_BUFSIZE (256 * 1024 * CUSTOM_LOG_DUMP_BUFSIZE_MB)
#define LOG_DUMP_PRESERVE_MAX_BUFSIZE (128 * 1024 * CUSTOM_LOG_DUMP_BUFSIZE_MB)
#define LOG_DUMP_ECNTRS_MAX_BUFSIZE (256 * 1024 * CUSTOM_LOG_DUMP_BUFSIZE_MB)
#define LOG_DUMP_RTT_MAX_BUFSIZE (256 * 1024 * CUSTOM_LOG_DUMP_BUFSIZE_MB)
#define LOG_DUMP_FILTER_MAX_BUFSIZE (128 * 1024 * CUSTOM_LOG_DUMP_BUFSIZE_MB)

#if LOG_DUMP_TOTAL_BUFSIZE < (LOG_DUMP_GENERAL_MAX_BUFSIZE + \
	LOG_DUMP_PRESERVE_MAX_BUFSIZE + LOG_DUMP_ECNTRS_MAX_BUFSIZE + LOG_DUMP_RTT_MAX_BUFSIZE \
	+ LOG_DUMP_FILTER_MAX_BUFSIZE)
#error "LOG_DUMP_TOTAL_BUFSIZE is lesser than sum of all rings"
#endif // endif

/* Special buffer is allocated as separately in prealloc */
#define LOG_DUMP_SPECIAL_MAX_BUFSIZE (8 * 1024)

#define LOG_DUMP_MAX_FILESIZE (8 *1024 * 1024) /* 8 MB default */
#ifdef CONFIG_LOG_BUF_SHIFT
/* 15% of kernel log buf size, if for example klog buf size is 512KB
* 15% of 512KB ~= 80KB
*/
#define LOG_DUMP_KERNEL_TAIL_FLUSH_SIZE \
	(15 * ((1 << CONFIG_LOG_BUF_SHIFT)/100))
#endif /* CONFIG_LOG_BUF_SHIFT */

#define LOG_DUMP_COOKIE_BUFSIZE	1024u

typedef struct {
	char *hdr_str;
	log_dump_section_type_t sec_type;
} dld_hdr_t;

typedef struct {
	int attr;
	char *hdr_str;
	log_dump_section_type_t sec_type;
	int log_type;
} dld_log_hdr_t;

#define DHD_PRINT_BUF_NAME_LEN 30
#endif /* DHD_LOG_DUMP */

int dhd_wifi_platform_register_drv(void);
void dhd_wifi_platform_unregister_drv(void);
wifi_adapter_info_t* dhd_wifi_platform_get_adapter(uint32 bus_type, uint32 bus_num,
	uint32 slot_num);
int wifi_platform_set_power(wifi_adapter_info_t *adapter, bool on, unsigned long msec);
int wifi_platform_bus_enumerate(wifi_adapter_info_t *adapter, bool device_present);
int wifi_platform_get_irq_number(wifi_adapter_info_t *adapter, unsigned long *irq_flags_ptr);
int wifi_platform_get_mac_addr(wifi_adapter_info_t *adapter, unsigned char *buf);
#ifdef CUSTOM_COUNTRY_CODE
void *wifi_platform_get_country_code(wifi_adapter_info_t *adapter, char *ccode,
	u32 flags);
#else
void *wifi_platform_get_country_code(wifi_adapter_info_t *adapter, char *ccode);
#endif /* CUSTOM_COUNTRY_CODE */
void* wifi_platform_prealloc(wifi_adapter_info_t *adapter, int section, unsigned long size);
void* wifi_platform_get_prealloc_func_ptr(wifi_adapter_info_t *adapter);

int dhd_get_fw_mode(struct dhd_info *dhdinfo);
bool dhd_update_fw_nv_path(struct dhd_info *dhdinfo);

#if defined(BT_OVER_SDIO)
int dhd_net_bus_get(struct net_device *dev);
int dhd_net_bus_put(struct net_device *dev);
#endif /* BT_OVER_SDIO */
#if defined(WLADPS) || defined(WLADPS_PRIVATE_CMD)
#define ADPS_ENABLE	1
#define ADPS_DISABLE	0

int dhd_enable_adps(dhd_pub_t *dhd, uint8 on);
#endif /* WLADPS || WLADPS_PRIVATE_CMD */
#ifdef DHDTCPSYNC_FLOOD_BLK
extern void dhd_reset_tcpsync_info_by_ifp(dhd_if_t *ifp);
extern void dhd_reset_tcpsync_info_by_dev(struct net_device *dev);
#endif /* DHDTCPSYNC_FLOOD_BLK */

int compat_kernel_read(struct file *file, loff_t offset, char *addr, unsigned long count);

#endif /* __DHD_LINUX_H__ */
