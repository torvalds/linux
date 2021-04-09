/*
 * Broadcom Dongle Host Driver (DHD), Linux-specific network interface
 * Basically selected code segments from usb-cdc.c and usb-rndis.c
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
 * $Id: dhd_linux.c 710862 2017-07-14 07:43:59Z $
 */

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#ifdef SHOW_LOGTRACE
#include <linux/syscalls.h>
#include <event_log.h>
#endif /* SHOW_LOGTRACE */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/ip.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <net/addrconf.h>
#ifdef ENABLE_ADAPTIVE_SCHED
#include <linux/cpufreq.h>
#endif /* ENABLE_ADAPTIVE_SCHED */

#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include <epivers.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>


#include <ethernet.h>
#include <bcmevent.h>
#include <vlan.h>
#include <802.3.h>

#include <dngl_stats.h>
#include <dhd_linux_wq.h>
#include <dhd.h>
#include <dhd_linux.h>
#ifdef DHD_WET
#include <dhd_wet.h>
#endif /* DHD_WET */
#ifdef PCIE_FULL_DONGLE
#include <dhd_flowring.h>
#endif
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_config.h>
#ifdef WL_ESCAN
#include <wl_escan.h>
#endif
#include <dhd_dbg.h>
#include <dhd_debug.h>
#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif
#ifdef RTT_SUPPORT
#include <dhd_rtt.h>
#endif
#ifdef DHD_TIMESYNC
#include <dhd_timesync.h>
#endif /* DHD_TIMESYNC */

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#if defined(CONFIG_SOC_EXYNOS8895)
#include <linux/exynos-pci-ctrl.h>
#endif /* CONFIG_SOC_EXYNOS8895 */

#ifdef DHD_WMF
#include <dhd_wmf_linux.h>
#endif /* DHD_WMF */

#ifdef DHD_L2_FILTER
#include <bcmicmp.h>
#include <bcm_l2_filter.h>
#include <dhd_l2_filter.h>
#endif /* DHD_L2_FILTER */

#ifdef DHD_PSTA
#include <dhd_psta.h>
#endif /* DHD_PSTA */


#ifdef DHDTCPACK_SUPPRESS
#include <dhd_ip.h>
#endif /* DHDTCPACK_SUPPRESS */
#include <dhd_daemon.h>
#ifdef DHD_PKT_LOGGING
#include <dhd_pktlog.h>
#endif /* DHD_PKT_LOGGING */
#if defined(STAT_REPORT)
#include <wl_statreport.h>
#endif /* STAT_REPORT */
#ifdef DHD_DEBUG_PAGEALLOC
typedef void (*page_corrupt_cb_t)(void *handle, void *addr_corrupt, size_t len);
void dhd_page_corrupt_cb(void *handle, void *addr_corrupt, size_t len);
extern void register_page_corrupt_cb(page_corrupt_cb_t cb, void* handle);
#endif /* DHD_DEBUG_PAGEALLOC */


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
#endif
#if !defined(DHD_LB_SECONDARY_CPUS)
#define DHD_LB_SECONDARY_CPUS   0xFE /* Little CPU coreids mask */
#endif

#define HIST_BIN_SIZE	9

static void dhd_rx_napi_dispatcher_fn(struct work_struct * work);

#if defined(DHD_LB_TXP)
static void dhd_lb_tx_handler(unsigned long data);
static void dhd_tx_dispatcher_work(struct work_struct * work);
static void dhd_tx_dispatcher_fn(dhd_pub_t *dhdp);
static void dhd_lb_tx_dispatch(dhd_pub_t *dhdp);

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

#ifdef HOFFLOAD_MODULES
#include <linux/firmware.h>
#endif

#ifdef WLMEDIA_HTSF
#include <linux/time.h>
#include <htsf.h>

#define HTSF_MINLEN 200    /* min. packet length to timestamp */
#define HTSF_BUS_DELAY 150 /* assume a fix propagation in us  */
#define TSMAX  1000        /* max no. of timing record kept   */
#define NUMBIN 34

static uint32 tsidx = 0;
static uint32 htsf_seqnum = 0;
uint32 tsfsync;
struct timeval tsync;
static uint32 tsport = 5010;

typedef struct histo_ {
	uint32 bin[NUMBIN];
} histo_t;

#if !ISPOWEROF2(DHD_SDALIGN)
#error DHD_SDALIGN is not a power of 2!
#endif

static histo_t vi_d1, vi_d2, vi_d3, vi_d4;
#endif /* WLMEDIA_HTSF */

#ifdef WL_MONITOR
#include <bcmmsgbuf.h>
#include <bcmwifi_monitor.h>
#endif

#define htod32(i) (i)
#define htod16(i) (i)
#define dtoh32(i) (i)
#define dtoh16(i) (i)
#define htodchanspec(i) (i)
#define dtohchanspec(i) (i)

#ifdef STBLINUX
#ifdef quote_str
#undef quote_str
#endif /* quote_str */
#ifdef to_str
#undef to_str
#endif /* quote_str */
#define to_str(s) #s
#define quote_str(s) to_str(s)

static char *driver_target = "driver_target: "quote_str(BRCM_DRIVER_TARGET);
#endif /* STBLINUX */



#if defined(SOFTAP)
extern bool ap_cfg_running;
extern bool ap_fw_loaded;
#endif

extern void dhd_dump_eapol_4way_message(dhd_pub_t *dhd, char *ifname,
	char *dump_data, bool direction);

#ifdef FIX_CPU_MIN_CLOCK
#include <linux/pm_qos.h>
#endif /* FIX_CPU_MIN_CLOCK */

#ifdef SET_RANDOM_MAC_SOFTAP
#ifndef CONFIG_DHD_SET_RANDOM_MAC_VAL
#define CONFIG_DHD_SET_RANDOM_MAC_VAL	0x001A11
#endif
static u32 vendor_oui = CONFIG_DHD_SET_RANDOM_MAC_VAL;
#endif /* SET_RANDOM_MAC_SOFTAP */

#ifdef ENABLE_ADAPTIVE_SCHED
#define DEFAULT_CPUFREQ_THRESH		1000000	/* threshold frequency : 1000000 = 1GHz */
#ifndef CUSTOM_CPUFREQ_THRESH
#define CUSTOM_CPUFREQ_THRESH	DEFAULT_CPUFREQ_THRESH
#endif /* CUSTOM_CPUFREQ_THRESH */
#endif /* ENABLE_ADAPTIVE_SCHED */

/* enable HOSTIP cache update from the host side when an eth0:N is up */
#define AOE_IP_ALIAS_SUPPORT 1

#ifdef BCM_FD_AGGR
#include <bcm_rpc.h>
#include <bcm_rpc_tp.h>
#endif
#ifdef PROP_TXSTATUS
#include <wlfc_proto.h>
#include <dhd_wlfc.h>
#endif

#include <wl_android.h>

/* Maximum STA per radio */
#define DHD_MAX_STA     32



const uint8 wme_fifo2ac[] = { 0, 1, 2, 3, 1, 1 };
const uint8 prio2fifo[8] = { 1, 0, 0, 1, 2, 2, 3, 3 };
#define WME_PRIO2AC(prio)  wme_fifo2ac[prio2fifo[(prio)]]

#ifdef ARP_OFFLOAD_SUPPORT
void aoe_update_host_ipv4_table(dhd_pub_t *dhd_pub, u32 ipa, bool add, int idx);
static int dhd_inetaddr_notifier_call(struct notifier_block *this,
	unsigned long event, void *ptr);
static struct notifier_block dhd_inetaddr_notifier = {
	.notifier_call = dhd_inetaddr_notifier_call
};
/* to make sure we won't register the same notifier twice, otherwise a loop is likely to be
 * created in kernel notifier link list (with 'next' pointing to itself)
 */
static bool dhd_inetaddr_notifier_registered = FALSE;
#endif /* ARP_OFFLOAD_SUPPORT */

#if defined(CONFIG_IPV6) && defined(IPV6_NDO_SUPPORT)
int dhd_inet6addr_notifier_call(struct notifier_block *this,
	unsigned long event, void *ptr);
static struct notifier_block dhd_inet6addr_notifier = {
	.notifier_call = dhd_inet6addr_notifier_call
};
/* to make sure we won't register the same notifier twice, otherwise a loop is likely to be
 * created in kernel notifier link list (with 'next' pointing to itself)
 */
static bool dhd_inet6addr_notifier_registered = FALSE;
#endif /* CONFIG_IPV6 && IPV6_NDO_SUPPORT */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)
#include <linux/suspend.h>
volatile bool dhd_mmc_suspend = FALSE;
DECLARE_WAIT_QUEUE_HEAD(dhd_dpc_wait);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP) */

#if defined(OOB_INTR_ONLY) || defined(FORCE_WOWLAN)
extern void dhd_enable_oob_intr(struct dhd_bus *bus, bool enable);
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
static void dhd_hang_process(void *dhd_info, void *event_data, u8 event);
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
MODULE_LICENSE("GPL and additional rights");
#endif /* LinuxVer */

#if defined(MULTIPLE_SUPPLICANT)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
DEFINE_MUTEX(_dhd_mutex_lock_);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) */
#endif 
static int dhd_suspend_resume_helper(struct dhd_info *dhd, int val, int force);

#ifdef CONFIG_BCM_DETECT_CONSECUTIVE_HANG
#define MAX_CONSECUTIVE_HANG_COUNTS 5
#endif /* CONFIG_BCM_DETECT_CONSECUTIVE_HANG */

#include <dhd_bus.h>

#ifdef DHD_ULP
#include <dhd_ulp.h>
#endif /* DHD_ULP */

#ifdef BCM_FD_AGGR
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(BCM_RPC_TP_DNGL_AGG_MAX_BYTE)
#else
#ifndef PROP_TXSTATUS
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(net->mtu + net->hard_header_len + dhd->pub.hdrlen)
#else
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(net->mtu + net->hard_header_len + dhd->pub.hdrlen + 128)
#endif
#endif /* BCM_FD_AGGR */

#ifdef PROP_TXSTATUS
extern bool dhd_wlfc_skip_fc(void * dhdp, uint8 idx);
extern void dhd_wlfc_plat_init(void *dhd);
extern void dhd_wlfc_plat_deinit(void *dhd);
#endif /* PROP_TXSTATUS */
#ifdef USE_DYNAMIC_F2_BLKSIZE
extern uint sd_f2_blocksize;
extern int dhdsdio_func_blocksize(dhd_pub_t *dhd, int function_num, int block_size);
#endif /* USE_DYNAMIC_F2_BLKSIZE */

#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 15)
const char *
print_tainted()
{
	return "";
}
#endif	/* LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 15) */

/* Linux wireless extension support */
#if defined(WL_WIRELESS_EXT)
#include <wl_iw.h>
extern wl_iw_extra_params_t  g_wl_iw_params;
#endif /* defined(WL_WIRELESS_EXT) */

#ifdef CONFIG_PARTIALSUSPEND_SLP
#include <linux/partialsuspend_slp.h>
#define CONFIG_HAS_EARLYSUSPEND
#define DHD_USE_EARLYSUSPEND
#define register_early_suspend		register_pre_suspend
#define unregister_early_suspend	unregister_pre_suspend
#define early_suspend				pre_suspend
#define EARLY_SUSPEND_LEVEL_BLANK_SCREEN		50
#else
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif /* defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND) */
#endif /* CONFIG_PARTIALSUSPEND_SLP */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
#include <linux/nl80211.h>
#endif /* OEM_ANDROID && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)) */

#if defined(BCMPCIE)
extern int dhd_get_suspend_bcn_li_dtim(dhd_pub_t *dhd, int *dtim_period, int *bcn_interval);
#else
extern int dhd_get_suspend_bcn_li_dtim(dhd_pub_t *dhd);
#endif /* OEM_ANDROID && BCMPCIE */

#ifdef PKT_FILTER_SUPPORT
extern void dhd_pktfilter_offload_set(dhd_pub_t * dhd, char *arg);
extern void dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode);
extern void dhd_pktfilter_offload_delete(dhd_pub_t *dhd, int id);
#endif

#if defined(PKT_FILTER_SUPPORT) && defined(APF)
static int __dhd_apf_add_filter(struct net_device *ndev, uint32 filter_id,
	u8* program, uint32 program_len);
static int __dhd_apf_config_filter(struct net_device *ndev, uint32 filter_id,
	uint32 mode, uint32 enable);
static int __dhd_apf_delete_filter(struct net_device *ndev, uint32 filter_id);
#endif /* PKT_FILTER_SUPPORT && APF */



static INLINE int argos_register_notifier_init(struct net_device *net) { return 0;}
static INLINE int argos_register_notifier_deinit(void) { return 0;}

#if defined(BT_OVER_SDIO)
extern void wl_android_set_wifi_on_flag(bool enable);
#endif /* BT_OVER_SDIO */


#if defined(TRAFFIC_MGMT_DWM)
void traffic_mgmt_pkt_set_prio(dhd_pub_t *dhdp, void * pktbuf);
#endif 

#ifdef DHD_FW_COREDUMP
static void dhd_mem_dump(void *dhd_info, void *event_info, u8 event);
#endif /* DHD_FW_COREDUMP */
#ifdef DHD_LOG_DUMP
#define DLD_BUFFER_NUM  2
/* [0]: General, [1]: Special */
struct dhd_log_dump_buf g_dld_buf[DLD_BUFFER_NUM];
static const int dld_buf_size[] = {
	(1024 * 1024),	/* DHD_LOG_DUMP_BUFFER_SIZE */
	(8 * 1024)	/* DHD_LOG_DUMP_BUFFER_EX_SIZE */
};
static void dhd_log_dump_init(dhd_pub_t *dhd);
static void dhd_log_dump_deinit(dhd_pub_t *dhd);
static void dhd_log_dump(void *handle, void *event_info, u8 event);
void dhd_schedule_log_dump(dhd_pub_t *dhdp);
static int do_dhd_log_dump(dhd_pub_t *dhdp);
#endif /* DHD_LOG_DUMP */

#ifdef DHD_DEBUG_UART
#include <linux/kmod.h>
#define DHD_DEBUG_UART_EXEC_PATH	"/system/bin/wldu"
static void dhd_debug_uart_exec_rd(void *handle, void *event_info, u8 event);
static void dhd_debug_uart_exec(dhd_pub_t *dhdp, char *cmd);
#endif	/* DHD_DEBUG_UART */

static int dhd_reboot_callback(struct notifier_block *this, unsigned long code, void *unused);
static struct notifier_block dhd_reboot_notifier = {
	.notifier_call = dhd_reboot_callback,
	.priority = 1,
};

#ifdef BCMPCIE
static int is_reboot = 0;
#endif /* BCMPCIE */

#if defined(BT_OVER_SDIO)
#include "dhd_bt_interface.h"
dhd_pub_t	*g_dhd_pub = NULL;
#endif /* defined (BT_OVER_SDIO) */

atomic_t exit_in_progress = ATOMIC_INIT(0);

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
#ifdef DHD_WMF
	dhd_wmf_t		wmf;		/* per bsscfg wmf setting */
	bool	wmf_psta_disable;		/* enable/disable MC pkt to each mac
						 * of MC group behind PSTA
						 */
#endif /* DHD_WMF */
#ifdef PCIE_FULL_DONGLE
	struct list_head sta_list;		/* sll of associated stations */
#if !defined(BCM_GMAC3)
	spinlock_t	sta_list_lock;		/* lock for manipulating sll */
#endif /* ! BCM_GMAC3 */
#endif /* PCIE_FULL_DONGLE */
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
#endif /* DHD_L2_FILTER */
#ifdef DHD_MCAST_REGEN
	bool mcast_regen_bss_enable;
#endif
	bool rx_pkt_chainable;		/* set all rx packet to chainable config by default */
	cumm_ctr_t cumm_ctr;			/* cummulative queue length of child flowrings */
} dhd_if_t;

#ifdef WLMEDIA_HTSF
typedef struct {
	uint32 low;
	uint32 high;
} tsf_t;

typedef struct {
	uint32 last_cycle;
	uint32 last_sec;
	uint32 last_tsf;
	uint32 coef;     /* scaling factor */
	uint32 coefdec1; /* first decimal  */
	uint32 coefdec2; /* second decimal */
} htsf_t;

typedef struct {
	uint32 t1;
	uint32 t2;
	uint32 t3;
	uint32 t4;
} tstamp_t;

static tstamp_t ts[TSMAX];
static tstamp_t maxdelayts;
static uint32 maxdelay = 0, tspktcnt = 0, maxdelaypktno = 0;

#endif  /* WLMEDIA_HTSF */

struct ipv6_work_info_t {
	uint8			if_idx;
	char			ipv6_addr[IPV6_ADDR_LEN];
	unsigned long		event;
};
static void dhd_process_daemon_msg(struct sk_buff *skb);
static void dhd_destroy_to_notifier_skt(void);
static int dhd_create_to_notifier_skt(void);
static struct sock *nl_to_event_sk = NULL;
int sender_pid = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
struct netlink_kernel_cfg g_cfg = {
	.groups = 1,
	.input = dhd_process_daemon_msg,
};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)) */

typedef struct dhd_dump {
	uint8 *buf;
	int bufsize;
} dhd_dump_t;


/* When Perimeter locks are deployed, any blocking calls must be preceeded
 * with a PERIM UNLOCK and followed by a PERIM LOCK.
 * Examples of blocking calls are: schedule_timeout(), down_interruptible(),
 * wait_event_timeout().
 */

/* Local private structure (extension of pub) */
typedef struct dhd_info {
#if defined(WL_WIRELESS_EXT)
	wl_iw_t		iw;		/* wireless extensions state (must be first) */
#endif /* defined(WL_WIRELESS_EXT) */
	dhd_pub_t pub;
	dhd_if_t *iflist[DHD_MAX_IFS]; /* for supporting multiple interfaces */

	wifi_adapter_info_t *adapter;			/* adapter information, interrupt, fw path etc. */
	char fw_path[PATH_MAX];		/* path to firmware image */
	char nv_path[PATH_MAX];		/* path to nvram vars file */
	char clm_path[PATH_MAX];		/* path to clm vars file */
	char conf_path[PATH_MAX];	/* path to config vars file */
#ifdef DHD_UCODE_DOWNLOAD
	char uc_path[PATH_MAX];	/* path to ucode image */
#endif /* DHD_UCODE_DOWNLOAD */

	/* serialize dhd iovars */
	struct mutex dhd_iovar_mutex;

	struct semaphore proto_sem;
#ifdef PROP_TXSTATUS
	spinlock_t	wlfc_spinlock;

#ifdef BCMDBUS
	ulong		wlfc_lock_flags;
	ulong		wlfc_pub_lock_flags;
#endif /* BCMDBUS */
#endif /* PROP_TXSTATUS */
#ifdef WLMEDIA_HTSF
	htsf_t  htsf;
#endif
	wait_queue_head_t ioctl_resp_wait;
	wait_queue_head_t d3ack_wait;
	wait_queue_head_t dhd_bus_busy_state_wait;
	uint32	default_wd_interval;

	timer_list_compat_t timer;
	bool wd_timer_valid;
#ifdef DHD_PCIE_RUNTIMEPM
	timer_list_compat_t rpm_timer;
	bool rpm_timer_valid;
	tsk_ctl_t	  thr_rpm_ctl;
#endif /* DHD_PCIE_RUNTIMEPM */
	struct tasklet_struct tasklet;
	spinlock_t	sdlock;
	spinlock_t	txqlock;
	spinlock_t	rxqlock;
	spinlock_t	dhd_lock;
#ifdef BCMDBUS
	ulong		txqlock_flags;
#else

	struct semaphore sdsem;
	tsk_ctl_t	thr_dpc_ctl;
	tsk_ctl_t	thr_wdt_ctl;
#endif /* BCMDBUS */

	tsk_ctl_t	thr_rxf_ctl;
	spinlock_t	rxf_lock;
	bool		rxthread_enabled;

	/* Wakelocks */
#if defined(CONFIG_HAS_WAKELOCK) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	struct wake_lock wl_wifi;   /* Wifi wakelock */
	struct wake_lock wl_rxwake; /* Wifi rx wakelock */
	struct wake_lock wl_ctrlwake; /* Wifi ctrl wakelock */
	struct wake_lock wl_wdwake; /* Wifi wd wakelock */
	struct wake_lock wl_evtwake; /* Wifi event wakelock */
	struct wake_lock wl_pmwake;   /* Wifi pm handler wakelock */
	struct wake_lock wl_txflwake; /* Wifi tx flow wakelock */
#ifdef BCMPCIE_OOB_HOST_WAKE
	struct wake_lock wl_intrwake; /* Host wakeup wakelock */
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef DHD_USE_SCAN_WAKELOCK
	struct wake_lock wl_scanwake;  /* Wifi scan wakelock */
#endif /* DHD_USE_SCAN_WAKELOCK */
#endif /* CONFIG_HAS_WAKELOCK && LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	/* net_device interface lock, prevent race conditions among net_dev interface
	 * calls and wifi_on or wifi_off
	 */
	struct mutex dhd_net_if_mutex;
	struct mutex dhd_suspend_mutex;
#if defined(PKT_FILTER_SUPPORT) && defined(APF)
	struct mutex dhd_apf_mutex;
#endif /* PKT_FILTER_SUPPORT && APF */
#endif 
	spinlock_t wakelock_spinlock;
	spinlock_t wakelock_evt_spinlock;
	uint32 wakelock_counter;
	int wakelock_wd_counter;
	int wakelock_rx_timeout_enable;
	int wakelock_ctrl_timeout_enable;
	bool waive_wakelock;
	uint32 wakelock_before_waive;

	/* Thread to issue ioctl for multicast */
	wait_queue_head_t ctrl_wait;
	atomic_t pend_8021x_cnt;
	dhd_attach_states_t dhd_state;
#ifdef SHOW_LOGTRACE
	dhd_event_log_t event_data;
#endif /* SHOW_LOGTRACE */

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif /* CONFIG_HAS_EARLYSUSPEND && DHD_USE_EARLYSUSPEND */

#ifdef ARP_OFFLOAD_SUPPORT
	u32 pend_ipaddr;
#endif /* ARP_OFFLOAD_SUPPORT */
#ifdef BCM_FD_AGGR
	void *rpc_th;
	void *rpc_osh;
	timer_list_compat_t rpcth_timer;
	bool rpcth_timer_active;
	uint8 fdaggr;
#endif
#ifdef DHDTCPACK_SUPPRESS
	spinlock_t	tcpack_lock;
#endif /* DHDTCPACK_SUPPRESS */
#ifdef FIX_CPU_MIN_CLOCK
	bool cpufreq_fix_status;
	struct mutex cpufreq_fix;
	struct pm_qos_request dhd_cpu_qos;
#ifdef FIX_BUS_MIN_CLOCK
	struct pm_qos_request dhd_bus_qos;
#endif /* FIX_BUS_MIN_CLOCK */
#endif /* FIX_CPU_MIN_CLOCK */
	void			*dhd_deferred_wq;
#ifdef DEBUG_CPU_FREQ
	struct notifier_block freq_trans;
	int __percpu *new_freq;
#endif
	unsigned int unit;
	struct notifier_block pm_notifier;
#ifdef DHD_PSTA
	uint32	psta_mode;	/* PSTA or PSR */
#endif /* DHD_PSTA */
#ifdef DHD_WET
	        uint32  wet_mode;
#endif /* DHD_WET */
#ifdef DHD_DEBUG
	dhd_dump_t *dump;
	timer_list_compat_t join_timer;
	u32 join_timeout_val;
	bool join_timer_active;
	uint scan_time_count;
	timer_list_compat_t scan_timer;
	bool scan_timer_active;
#endif
#if defined(DHD_LB)
	/* CPU Load Balance dynamic CPU selection */

	/* Variable that tracks the currect CPUs available for candidacy */
	cpumask_var_t cpumask_curr_avail;

	/* Primary and secondary CPU mask */
	cpumask_var_t cpumask_primary, cpumask_secondary; /* configuration */
	cpumask_var_t cpumask_primary_new, cpumask_secondary_new; /* temp */

	struct notifier_block cpu_notifier;

	/* Tasklet to handle Tx Completion packet freeing */
	struct tasklet_struct tx_compl_tasklet;
	atomic_t                   tx_compl_cpu;

	/* Tasklet to handle RxBuf Post during Rx completion */
	struct tasklet_struct rx_compl_tasklet;
	atomic_t                   rx_compl_cpu;

	/* Napi struct for handling rx packet sendup. Packets are removed from
	 * H2D RxCompl ring and placed into rx_pend_queue. rx_pend_queue is then
	 * appended to rx_napi_queue (w/ lock) and the rx_napi_struct is scheduled
	 * to run to rx_napi_cpu.
	 */
	struct sk_buff_head   rx_pend_queue  ____cacheline_aligned;
	struct sk_buff_head   rx_napi_queue  ____cacheline_aligned;
	struct napi_struct    rx_napi_struct ____cacheline_aligned;
	atomic_t                   rx_napi_cpu; /* cpu on which the napi is dispatched */
	struct net_device    *rx_napi_netdev; /* netdev of primary interface */

	struct work_struct    rx_napi_dispatcher_work;
	struct work_struct	  tx_compl_dispatcher_work;
	struct work_struct    tx_dispatcher_work;

	/* Number of times DPC Tasklet ran */
	uint32	dhd_dpc_cnt;
	/* Number of times NAPI processing got scheduled */
	uint32	napi_sched_cnt;
	/* Number of times NAPI processing ran on each available core */
	uint32	*napi_percpu_run_cnt;
	/* Number of times RX Completions got scheduled */
	uint32	rxc_sched_cnt;
	/* Number of times RX Completion ran on each available core */
	uint32	*rxc_percpu_run_cnt;
	/* Number of times TX Completions got scheduled */
	uint32	txc_sched_cnt;
	/* Number of times TX Completions ran on each available core */
	uint32	*txc_percpu_run_cnt;
	/* CPU status */
	/* Number of times each CPU came online */
	uint32	*cpu_online_cnt;
	/* Number of times each CPU went offline */
	uint32	*cpu_offline_cnt;

	/* Number of times TX processing run on each core */
	uint32	*txp_percpu_run_cnt;
	/* Number of times TX start run on each core */
	uint32	*tx_start_percpu_run_cnt;

	/* Tx load balancing */

	/* TODO: Need to see if batch processing is really required in case of TX
	 * processing. In case of RX the Dongle can send a bunch of rx completions,
	 * hence we took a 3 queue approach
	 * enque - adds the skbs to rx_pend_queue
	 * dispatch - uses a lock and adds the list of skbs from pend queue to
	 *            napi queue
	 * napi processing - copies the pend_queue into a local queue and works
	 * on it.
	 * But for TX its going to be 1 skb at a time, so we are just thinking
	 * of using only one queue and use the lock supported skb queue functions
	 * to add and process it. If its in-efficient we'll re-visit the queue
	 * design.
	 */

	/* When the NET_TX tries to send a TX packet put it into tx_pend_queue */
	/* struct sk_buff_head		tx_pend_queue  ____cacheline_aligned;  */
	/*
	 * From the Tasklet that actually sends out data
	 * copy the list tx_pend_queue into tx_active_queue. There by we need
	 * to spinlock to only perform the copy the rest of the code ie to
	 * construct the tx_pend_queue and the code to process tx_active_queue
	 * can be lockless. The concept is borrowed as is from RX processing
	 */
	/* struct sk_buff_head		tx_active_queue  ____cacheline_aligned; */

	/* Control TXP in runtime, enable by default */
	atomic_t                lb_txp_active;

	/*
	 * When the NET_TX tries to send a TX packet put it into tx_pend_queue
	 * For now, the processing tasklet will also direcly operate on this
	 * queue
	 */
	struct sk_buff_head	tx_pend_queue  ____cacheline_aligned;

	/* cpu on which the DHD Tx is happenning */
	atomic_t		tx_cpu;

	/* CPU on which the Network stack is calling the DHD's xmit function */
	atomic_t		net_tx_cpu;

	/* Tasklet context from which the DHD's TX processing happens */
	struct tasklet_struct tx_tasklet;

	/*
	 * Consumer Histogram - NAPI RX Packet processing
	 * -----------------------------------------------
	 * On Each CPU, when the NAPI RX Packet processing call back was invoked
	 * how many packets were processed is captured in this data structure.
	 * Now its difficult to capture the "exact" number of packets processed.
	 * So considering the packet counter to be a 32 bit one, we have a
	 * bucket with 8 bins (2^1, 2^2 ... 2^8). The "number" of packets
	 * processed is rounded off to the next power of 2 and put in the
	 * approriate "bin" the value in the bin gets incremented.
	 * For example, assume that in CPU 1 if NAPI Rx runs 3 times
	 * and the packet count processed is as follows (assume the bin counters are 0)
	 * iteration 1 - 10 (the bin counter 2^4 increments to 1)
	 * iteration 2 - 30 (the bin counter 2^5 increments to 1)
	 * iteration 3 - 15 (the bin counter 2^4 increments by 1 to become 2)
	 */
	uint32 *napi_rx_hist[HIST_BIN_SIZE];
	uint32 *txc_hist[HIST_BIN_SIZE];
	uint32 *rxc_hist[HIST_BIN_SIZE];
#endif /* DHD_LB */

#ifdef SHOW_LOGTRACE
	struct work_struct	  event_log_dispatcher_work;
#endif /* SHOW_LOGTRACE */

#if defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW)
#endif /* defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW) */
	struct kobject dhd_kobj;
#ifdef SHOW_LOGTRACE
	struct sk_buff_head   evt_trace_queue     ____cacheline_aligned;
#endif
	timer_list_compat_t timesync_timer;
#if defined(BT_OVER_SDIO)
	char btfw_path[PATH_MAX];
#endif /* defined (BT_OVER_SDIO) */

#ifdef WL_MONITOR
	struct net_device *monitor_dev; /* monitor pseudo device */
	struct sk_buff *monitor_skb;
	uint	monitor_len;
	uint monitor_type;   /* monitor pseudo device */
	monitor_info_t *monitor_info;
#endif /* WL_MONITOR */
	uint32 shub_enable;
#if defined(BT_OVER_SDIO)
	struct mutex bus_user_lock; /* lock for sdio bus apis shared between WLAN & BT */
	int	bus_user_count; /* User counts of sdio bus shared between WLAN & BT */
#endif /* BT_OVER_SDIO */
#ifdef DHD_DEBUG_UART
	bool duart_execute;
#endif
#ifdef PCIE_INB_DW
	wait_queue_head_t ds_exit_wait;
#endif /* PCIE_INB_DW */
} dhd_info_t;

#ifdef WL_MONITOR
#define MONPKT_EXTRA_LEN	48
#endif

#define DHDIF_FWDER(dhdif)      FALSE

#if defined(BT_OVER_SDIO)
/* Flag to indicate if driver is initialized */
uint dhd_driver_init_done = TRUE;
#else
/* Flag to indicate if driver is initialized */
uint dhd_driver_init_done = FALSE;
#endif
/* Flag to indicate if we should download firmware on driver load */
uint dhd_download_fw_on_driverload = TRUE;

/* Definitions to provide path to the firmware and nvram
 * example nvram_path[MOD_PARAM_PATHLEN]="/projects/wlan/nvram.txt"
 */
char firmware_path[MOD_PARAM_PATHLEN];
char nvram_path[MOD_PARAM_PATHLEN];
char clm_path[MOD_PARAM_PATHLEN];
char config_path[MOD_PARAM_PATHLEN];
#ifdef DHD_UCODE_DOWNLOAD
char ucode_path[MOD_PARAM_PATHLEN];
#endif /* DHD_UCODE_DOWNLOAD */

module_param_string(clm_path, clm_path, MOD_PARAM_PATHLEN, 0660);


/* backup buffer for firmware and nvram path */
char fw_bak_path[MOD_PARAM_PATHLEN];
char nv_bak_path[MOD_PARAM_PATHLEN];

/* information string to keep firmware, chio, cheip version info visiable from log */
char info_string[MOD_PARAM_INFOLEN];
module_param_string(info_string, info_string, MOD_PARAM_INFOLEN, 0444);
int op_mode = 0;
int disable_proptx = 0;
module_param(op_mode, int, 0644);
extern int wl_control_wl_start(struct net_device *dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && (defined(BCMLXSDMMC) || defined(BCMDBUS))
struct semaphore dhd_registration_sem;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */

/* deferred handlers */
static void dhd_ifadd_event_handler(void *handle, void *event_info, u8 event);
static void dhd_ifdel_event_handler(void *handle, void *event_info, u8 event);
static void dhd_set_mac_addr_handler(void *handle, void *event_info, u8 event);
static void dhd_set_mcast_list_handler(void *handle, void *event_info, u8 event);

#ifdef DHD_UPDATE_INTF_MAC
static void dhd_ifupdate_event_handler(void *handle, void *event_info, u8 event);
#endif /* DHD_UPDATE_INTF_MAC */
#if defined(CONFIG_IPV6) && defined(IPV6_NDO_SUPPORT)
static void dhd_inet6_work_handler(void *dhd_info, void *event_data, u8 event);
#endif /* CONFIG_IPV6 && IPV6_NDO_SUPPORT */
#ifdef WL_CFG80211
extern void dhd_netdev_free(struct net_device *ndev);
#endif /* WL_CFG80211 */

#if (defined(DHD_WET) || defined(DHD_MCAST_REGEN) || defined(DHD_L2_FILTER))
/* update rx_pkt_chainable state of dhd interface */
static void dhd_update_rx_pkt_chainable_state(dhd_pub_t* dhdp, uint32 idx);
#endif /* DHD_WET || DHD_MCAST_REGEN || DHD_L2_FILTER */

#ifdef HOFFLOAD_MODULES
char dhd_hmem_module_string[MOD_PARAM_SRLEN];
module_param_string(dhd_hmem_module_string, dhd_hmem_module_string, MOD_PARAM_SRLEN, 0660);
#endif
/* Error bits */
module_param(dhd_msg_level, int, 0);
#if defined(WL_WIRELESS_EXT)
module_param(iw_msg_level, int, 0);
#endif
#ifdef WL_CFG80211
module_param(wl_dbg_level, int, 0);
#endif
module_param(android_msg_level, int, 0);
module_param(config_msg_level, int, 0);

#ifdef ARP_OFFLOAD_SUPPORT
/* ARP offload enable */
uint dhd_arp_enable = TRUE;
module_param(dhd_arp_enable, uint, 0);

/* ARP offload agent mode : Enable ARP Host Auto-Reply and ARP Peer Auto-Reply */

#ifdef ENABLE_ARP_SNOOP_MODE
uint dhd_arp_mode = ARP_OL_AGENT | ARP_OL_PEER_AUTO_REPLY | ARP_OL_SNOOP | ARP_OL_HOST_AUTO_REPLY;
#else
uint dhd_arp_mode = ARP_OL_AGENT | ARP_OL_PEER_AUTO_REPLY;
#endif /* ENABLE_ARP_SNOOP_MODE */

module_param(dhd_arp_mode, uint, 0);
#endif /* ARP_OFFLOAD_SUPPORT */

/* Disable Prop tx */
module_param(disable_proptx, int, 0644);
/* load firmware and/or nvram values from the filesystem */
module_param_string(firmware_path, firmware_path, MOD_PARAM_PATHLEN, 0660);
module_param_string(nvram_path, nvram_path, MOD_PARAM_PATHLEN, 0660);
module_param_string(config_path, config_path, MOD_PARAM_PATHLEN, 0);
#ifdef DHD_UCODE_DOWNLOAD
module_param_string(ucode_path, ucode_path, MOD_PARAM_PATHLEN, 0660);
#endif /* DHD_UCODE_DOWNLOAD */

/* Watchdog interval */

/* extend watchdog expiration to 2 seconds when DPC is running */
#define WATCHDOG_EXTEND_INTERVAL (2000)

uint dhd_watchdog_ms = CUSTOM_DHD_WATCHDOG_MS;
module_param(dhd_watchdog_ms, uint, 0);

#ifdef DHD_PCIE_RUNTIMEPM
uint dhd_runtimepm_ms = CUSTOM_DHD_RUNTIME_MS;
#endif /* DHD_PCIE_RUNTIMEPMT */
#if defined(DHD_DEBUG)
/* Console poll interval */
uint dhd_console_ms = 0;
module_param(dhd_console_ms, uint, 0644);
#else
uint dhd_console_ms = 0;
#endif /* DHD_DEBUG */

uint dhd_slpauto = TRUE;
module_param(dhd_slpauto, uint, 0);

#ifdef PKT_FILTER_SUPPORT
/* Global Pkt filter enable control */
uint dhd_pkt_filter_enable = TRUE;
module_param(dhd_pkt_filter_enable, uint, 0);
#endif

/* Pkt filter init setup */
uint dhd_pkt_filter_init = 0;
module_param(dhd_pkt_filter_init, uint, 0);

/* Pkt filter mode control */
#ifdef GAN_LITE_NAT_KEEPALIVE_FILTER
uint dhd_master_mode = FALSE;
#else
uint dhd_master_mode = FALSE;
#endif /* GAN_LITE_NAT_KEEPALIVE_FILTER */
module_param(dhd_master_mode, uint, 0);

int dhd_watchdog_prio = 0;
module_param(dhd_watchdog_prio, int, 0);

/* DPC thread priority */
int dhd_dpc_prio = CUSTOM_DPC_PRIO_SETTING;
module_param(dhd_dpc_prio, int, 0);

/* RX frame thread priority */
int dhd_rxf_prio = CUSTOM_RXF_PRIO_SETTING;
module_param(dhd_rxf_prio, int, 0);

#if !defined(BCMDBUS)
extern int dhd_dongle_ramsize;
module_param(dhd_dongle_ramsize, int, 0);
#endif /* !BCMDBUS */

#ifdef WL_CFG80211
int passive_channel_skip = 0;
module_param(passive_channel_skip, int, (S_IRUSR|S_IWUSR));
#endif /* WL_CFG80211 */

/* Keep track of number of instances */
static int dhd_found = 0;
static int instance_base = 0; /* Starting instance number */
module_param(instance_base, int, 0644);

#if defined(DHD_LB_RXP) && defined(PCIE_FULL_DONGLE)
static int dhd_napi_weight = 32;
module_param(dhd_napi_weight, int, 0644);
#endif /* DHD_LB_RXP && PCIE_FULL_DONGLE */

#ifdef PCIE_FULL_DONGLE
extern int h2d_max_txpost;
module_param(h2d_max_txpost, int, 0644);
#endif /* PCIE_FULL_DONGLE */

#ifdef DHD_ARP_DUMP
#include <linux/if_arp.h>
static const char arp_types[][10] = {
	"NA", "REQUEST", "RESPONSE"
};
static void dhd_arp_dump(char *ifname, uint8 *pktdata, bool tx);
#endif /* DHD_ARP_DUMP */

#ifdef DHD_DHCP_DUMP
struct bootp_fmt {
	struct iphdr ip_header;
	struct udphdr udp_header;
	uint8 op;
	uint8 htype;
	uint8 hlen;
	uint8 hops;
	uint32 transaction_id;
	uint16 secs;
	uint16 flags;
	uint32 client_ip;
	uint32 assigned_ip;
	uint32 server_ip;
	uint32 relay_ip;
	uint8 hw_address[16];
	uint8 server_name[64];
	uint8 file_name[128];
	uint8 options[312];
};

static const uint8 bootp_magic_cookie[4] = { 99, 130, 83, 99 };
static const char dhcp_ops[][10] = {
	"NA", "REQUEST", "REPLY"
};
static const char dhcp_types[][10] = {
	"NA", "DISCOVER", "OFFER", "REQUEST", "DECLINE", "ACK", "NAK", "RELEASE", "INFORM"
};
static void dhd_dhcp_dump(char *ifname, uint8 *pktdata, bool tx);
#endif /* DHD_DHCP_DUMP */

#ifdef DHD_ICMP_DUMP
#include <net/icmp.h>
static void dhd_icmp_dump(char *ifname, uint8 *pktdata, bool tx);
#endif /* DHD_ICMP_DUMP */

/* Functions to manage sysfs interface for dhd */
static int dhd_sysfs_init(dhd_info_t *dhd);
static void dhd_sysfs_exit(dhd_info_t *dhd);

#ifdef SHOW_LOGTRACE
#if defined(CUSTOMER_HW4_DEBUG)
static char *logstrs_path = PLATFORM_PATH"logstrs.bin";
static char *st_str_file_path = PLATFORM_PATH"rtecdc.bin";
static char *map_file_path = PLATFORM_PATH"rtecdc.map";
static char *rom_st_str_file_path = PLATFORM_PATH"roml.bin";
static char *rom_map_file_path = PLATFORM_PATH"roml.map";
#elif defined(CUSTOMER_HW2)
static char *logstrs_path = "/data/misc/wifi/logstrs.bin";
static char *st_str_file_path = "/data/misc/wifi/rtecdc.bin";
static char *map_file_path = "/data/misc/wifi/rtecdc.map";
static char *rom_st_str_file_path = "/data/misc/wifi/roml.bin";
static char *rom_map_file_path = "/data/misc/wifi/roml.map";
#else
static char *logstrs_path = "/installmedia/logstrs.bin";
static char *st_str_file_path = "/installmedia/rtecdc.bin";
static char *map_file_path = "/installmedia/rtecdc.map";
static char *rom_st_str_file_path = "/installmedia/roml.bin";
static char *rom_map_file_path = "/installmedia/roml.map";
#endif /* CUSTOMER_HW4_DEBUG || CUSTOMER_HW2 */
static char *ram_file_str = "rtecdc";
static char *rom_file_str = "roml";

module_param(logstrs_path, charp, S_IRUGO);
module_param(st_str_file_path, charp, S_IRUGO);
module_param(map_file_path, charp, S_IRUGO);
module_param(rom_st_str_file_path, charp, S_IRUGO);
module_param(rom_map_file_path, charp, S_IRUGO);

static int dhd_init_logstrs_array(osl_t *osh, dhd_event_log_t *temp);
static int dhd_read_map(osl_t *osh, char *fname, uint32 *ramstart, uint32 *rodata_start,
	uint32 *rodata_end);
static int dhd_init_static_strs_array(osl_t *osh, dhd_event_log_t *temp, char *str_file,
	char *map_file);
#endif /* SHOW_LOGTRACE */

#if defined(DHD_LB)

static void
dhd_lb_set_default_cpus(dhd_info_t *dhd)
{
	/* Default CPU allocation for the jobs */
	atomic_set(&dhd->rx_napi_cpu, 1);
	atomic_set(&dhd->rx_compl_cpu, 2);
	atomic_set(&dhd->tx_compl_cpu, 2);
	atomic_set(&dhd->tx_cpu, 2);
	atomic_set(&dhd->net_tx_cpu, 0);
}

static void
dhd_cpumasks_deinit(dhd_info_t *dhd)
{
	free_cpumask_var(dhd->cpumask_curr_avail);
	free_cpumask_var(dhd->cpumask_primary);
	free_cpumask_var(dhd->cpumask_primary_new);
	free_cpumask_var(dhd->cpumask_secondary);
	free_cpumask_var(dhd->cpumask_secondary_new);
}

static int
dhd_cpumasks_init(dhd_info_t *dhd)
{
	int id;
	uint32 cpus, num_cpus = num_possible_cpus();
	int ret = 0;

	DHD_ERROR(("%s CPU masks primary(big)=0x%x secondary(little)=0x%x\n", __FUNCTION__,
		DHD_LB_PRIMARY_CPUS, DHD_LB_SECONDARY_CPUS));

	if (!alloc_cpumask_var(&dhd->cpumask_curr_avail, GFP_KERNEL) ||
	    !alloc_cpumask_var(&dhd->cpumask_primary, GFP_KERNEL) ||
	    !alloc_cpumask_var(&dhd->cpumask_primary_new, GFP_KERNEL) ||
	    !alloc_cpumask_var(&dhd->cpumask_secondary, GFP_KERNEL) ||
	    !alloc_cpumask_var(&dhd->cpumask_secondary_new, GFP_KERNEL)) {
		DHD_ERROR(("%s Failed to init cpumasks\n", __FUNCTION__));
		ret = -ENOMEM;
		goto fail;
	}

	cpumask_copy(dhd->cpumask_curr_avail, cpu_online_mask);
	cpumask_clear(dhd->cpumask_primary);
	cpumask_clear(dhd->cpumask_secondary);

	if (num_cpus > 32) {
		DHD_ERROR(("%s max cpus must be 32, %d too big\n", __FUNCTION__, num_cpus));
		ASSERT(0);
	}

	cpus = DHD_LB_PRIMARY_CPUS;
	for (id = 0; id < num_cpus; id++) {
		if (isset(&cpus, id))
			cpumask_set_cpu(id, dhd->cpumask_primary);
	}

	cpus = DHD_LB_SECONDARY_CPUS;
	for (id = 0; id < num_cpus; id++) {
		if (isset(&cpus, id))
			cpumask_set_cpu(id, dhd->cpumask_secondary);
	}

	return ret;
fail:
	dhd_cpumasks_deinit(dhd);
	return ret;
}

/*
 * The CPU Candidacy Algorithm
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * The available CPUs for selection are divided into two groups
 *  Primary Set - A CPU mask that carries the First Choice CPUs
 *  Secondary Set - A CPU mask that carries the Second Choice CPUs.
 *
 * There are two types of Job, that needs to be assigned to
 * the CPUs, from one of the above mentioned CPU group. The Jobs are
 * 1) Rx Packet Processing - napi_cpu
 * 2) Completion Processiong (Tx, RX) - compl_cpu
 *
 * To begin with both napi_cpu and compl_cpu are on CPU0. Whenever a CPU goes
 * on-line/off-line the CPU candidacy algorithm is triggerd. The candidacy
 * algo tries to pickup the first available non boot CPU (CPU0) for napi_cpu.
 * If there are more processors free, it assigns one to compl_cpu.
 * It also tries to ensure that both napi_cpu and compl_cpu are not on the same
 * CPU, as much as possible.
 *
 * By design, both Tx and Rx completion jobs are run on the same CPU core, as it
 * would allow Tx completion skb's to be released into a local free pool from
 * which the rx buffer posts could have been serviced. it is important to note
 * that a Tx packet may not have a large enough buffer for rx posting.
 */
void dhd_select_cpu_candidacy(dhd_info_t *dhd)
{
	uint32 primary_available_cpus; /* count of primary available cpus */
	uint32 secondary_available_cpus; /* count of secondary available cpus */
	uint32 napi_cpu = 0; /* cpu selected for napi rx processing */
	uint32 compl_cpu = 0; /* cpu selected for completion jobs */
	uint32 tx_cpu = 0; /* cpu selected for tx processing job */

	cpumask_clear(dhd->cpumask_primary_new);
	cpumask_clear(dhd->cpumask_secondary_new);

	/*
	 * Now select from the primary mask. Even if a Job is
	 * already running on a CPU in secondary group, we still move
	 * to primary CPU. So no conditional checks.
	 */
	cpumask_and(dhd->cpumask_primary_new, dhd->cpumask_primary,
		dhd->cpumask_curr_avail);

	cpumask_and(dhd->cpumask_secondary_new, dhd->cpumask_secondary,
		dhd->cpumask_curr_avail);

	primary_available_cpus = cpumask_weight(dhd->cpumask_primary_new);

	if (primary_available_cpus > 0) {
		napi_cpu = cpumask_first(dhd->cpumask_primary_new);

		/* If no further CPU is available,
		 * cpumask_next returns >= nr_cpu_ids
		 */
		tx_cpu = cpumask_next(napi_cpu, dhd->cpumask_primary_new);
		if (tx_cpu >= nr_cpu_ids)
			tx_cpu = 0;

		/* In case there are no more CPUs, do completions & Tx in same CPU */
		compl_cpu = cpumask_next(tx_cpu, dhd->cpumask_primary_new);
		if (compl_cpu >= nr_cpu_ids)
			compl_cpu = tx_cpu;
	}

	DHD_INFO(("%s After primary CPU check napi_cpu %d compl_cpu %d tx_cpu %d\n",
		__FUNCTION__, napi_cpu, compl_cpu, tx_cpu));

	/* -- Now check for the CPUs from the secondary mask -- */
	secondary_available_cpus = cpumask_weight(dhd->cpumask_secondary_new);

	DHD_INFO(("%s Available secondary cpus %d nr_cpu_ids %d\n",
		__FUNCTION__, secondary_available_cpus, nr_cpu_ids));

	if (secondary_available_cpus > 0) {
		/* At this point if napi_cpu is unassigned it means no CPU
		 * is online from Primary Group
		 */
		if (napi_cpu == 0) {
			napi_cpu = cpumask_first(dhd->cpumask_secondary_new);
			tx_cpu = cpumask_next(napi_cpu, dhd->cpumask_secondary_new);
			compl_cpu = cpumask_next(tx_cpu, dhd->cpumask_secondary_new);
		} else if (tx_cpu == 0) {
			tx_cpu = cpumask_first(dhd->cpumask_secondary_new);
			compl_cpu = cpumask_next(tx_cpu, dhd->cpumask_secondary_new);
		} else if (compl_cpu == 0) {
			compl_cpu = cpumask_first(dhd->cpumask_secondary_new);
		}

		/* If no CPU was available for tx processing, choose CPU 0 */
		if (tx_cpu >= nr_cpu_ids)
			tx_cpu = 0;

		/* If no CPU was available for completion, choose CPU 0 */
		if (compl_cpu >= nr_cpu_ids)
			compl_cpu = 0;
	}
	if ((primary_available_cpus == 0) &&
		(secondary_available_cpus == 0)) {
		/* No CPUs available from primary or secondary mask */
		napi_cpu = 1;
		compl_cpu = 0;
		tx_cpu = 2;
	}

	DHD_INFO(("%s After secondary CPU check napi_cpu %d compl_cpu %d tx_cpu %d\n",
		__FUNCTION__, napi_cpu, compl_cpu, tx_cpu));

	ASSERT(napi_cpu < nr_cpu_ids);
	ASSERT(compl_cpu < nr_cpu_ids);
	ASSERT(tx_cpu < nr_cpu_ids);

	atomic_set(&dhd->rx_napi_cpu, napi_cpu);
	atomic_set(&dhd->tx_compl_cpu, compl_cpu);
	atomic_set(&dhd->rx_compl_cpu, compl_cpu);
	atomic_set(&dhd->tx_cpu, tx_cpu);

	return;
}

/*
 * Function to handle CPU Hotplug notifications.
 * One of the task it does is to trigger the CPU Candidacy algorithm
 * for load balancing.
 */
int
dhd_cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	unsigned long int cpu = (unsigned long int)hcpu;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	dhd_info_t *dhd = container_of(nfb, dhd_info_t, cpu_notifier);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	if (!dhd || !(dhd->dhd_state & DHD_ATTACH_STATE_LB_ATTACH_DONE)) {
		DHD_INFO(("%s(): LB data is not initialized yet.\n",
			__FUNCTION__));
		return NOTIFY_BAD;
	}

	switch (action)
	{
		case CPU_ONLINE:
		case CPU_ONLINE_FROZEN:
			DHD_LB_STATS_INCR(dhd->cpu_online_cnt[cpu]);
			cpumask_set_cpu(cpu, dhd->cpumask_curr_avail);
			dhd_select_cpu_candidacy(dhd);
			break;

		case CPU_DOWN_PREPARE:
		case CPU_DOWN_PREPARE_FROZEN:
			DHD_LB_STATS_INCR(dhd->cpu_offline_cnt[cpu]);
			cpumask_clear_cpu(cpu, dhd->cpumask_curr_avail);
			dhd_select_cpu_candidacy(dhd);
			break;
		default:
			break;
	}

	return NOTIFY_OK;
}

#if defined(DHD_LB_STATS)
void dhd_lb_stats_init(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	int i, j, num_cpus = num_possible_cpus();
	int alloc_size = sizeof(uint32) * num_cpus;

	if (dhdp == NULL) {
		DHD_ERROR(("%s(): Invalid argument dhd pubb pointer is NULL \n",
			__FUNCTION__));
		return;
	}

	dhd = dhdp->info;
	if (dhd == NULL) {
		DHD_ERROR(("%s(): DHD pointer is NULL \n", __FUNCTION__));
		return;
	}

	DHD_LB_STATS_CLR(dhd->dhd_dpc_cnt);
	DHD_LB_STATS_CLR(dhd->napi_sched_cnt);

	dhd->napi_percpu_run_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->napi_percpu_run_cnt) {
		DHD_ERROR(("%s(): napi_percpu_run_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->napi_percpu_run_cnt[i]);

	DHD_LB_STATS_CLR(dhd->rxc_sched_cnt);

	dhd->rxc_percpu_run_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->rxc_percpu_run_cnt) {
		DHD_ERROR(("%s(): rxc_percpu_run_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->rxc_percpu_run_cnt[i]);

	DHD_LB_STATS_CLR(dhd->txc_sched_cnt);

	dhd->txc_percpu_run_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->txc_percpu_run_cnt) {
		DHD_ERROR(("%s(): txc_percpu_run_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->txc_percpu_run_cnt[i]);

	dhd->cpu_online_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->cpu_online_cnt) {
		DHD_ERROR(("%s(): cpu_online_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->cpu_online_cnt[i]);

	dhd->cpu_offline_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->cpu_offline_cnt) {
		DHD_ERROR(("%s(): cpu_offline_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->cpu_offline_cnt[i]);

	dhd->txp_percpu_run_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->txp_percpu_run_cnt) {
		DHD_ERROR(("%s(): txp_percpu_run_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->txp_percpu_run_cnt[i]);

	dhd->tx_start_percpu_run_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->tx_start_percpu_run_cnt) {
		DHD_ERROR(("%s(): tx_start_percpu_run_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->tx_start_percpu_run_cnt[i]);

	for (j = 0; j < HIST_BIN_SIZE; j++) {
		dhd->napi_rx_hist[j] = (uint32 *)MALLOC(dhdp->osh, alloc_size);
		if (!dhd->napi_rx_hist[j]) {
			DHD_ERROR(("%s(): dhd->napi_rx_hist[%d] malloc failed \n",
				__FUNCTION__, j));
			return;
		}
		for (i = 0; i < num_cpus; i++) {
			DHD_LB_STATS_CLR(dhd->napi_rx_hist[j][i]);
		}
	}
#ifdef DHD_LB_TXC
	for (j = 0; j < HIST_BIN_SIZE; j++) {
		dhd->txc_hist[j] = (uint32 *)MALLOC(dhdp->osh, alloc_size);
		if (!dhd->txc_hist[j]) {
			DHD_ERROR(("%s(): dhd->txc_hist[%d] malloc failed \n",
			         __FUNCTION__, j));
			return;
		}
		for (i = 0; i < num_cpus; i++) {
			DHD_LB_STATS_CLR(dhd->txc_hist[j][i]);
		}
	}
#endif /* DHD_LB_TXC */
#ifdef DHD_LB_RXC
	for (j = 0; j < HIST_BIN_SIZE; j++) {
		dhd->rxc_hist[j] = (uint32 *)MALLOC(dhdp->osh, alloc_size);
		if (!dhd->rxc_hist[j]) {
			DHD_ERROR(("%s(): dhd->rxc_hist[%d] malloc failed \n",
				__FUNCTION__, j));
			return;
		}
		for (i = 0; i < num_cpus; i++) {
			DHD_LB_STATS_CLR(dhd->rxc_hist[j][i]);
		}
	}
#endif /* DHD_LB_RXC */
	return;
}

void dhd_lb_stats_deinit(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	int j, num_cpus = num_possible_cpus();
	int alloc_size = sizeof(uint32) * num_cpus;

	if (dhdp == NULL) {
		DHD_ERROR(("%s(): Invalid argument dhd pubb pointer is NULL \n",
			__FUNCTION__));
		return;
	}

	dhd = dhdp->info;
	if (dhd == NULL) {
		DHD_ERROR(("%s(): DHD pointer is NULL \n", __FUNCTION__));
		return;
	}

	if (dhd->napi_percpu_run_cnt) {
		MFREE(dhdp->osh, dhd->napi_percpu_run_cnt, alloc_size);
		dhd->napi_percpu_run_cnt = NULL;
	}
	if (dhd->rxc_percpu_run_cnt) {
		MFREE(dhdp->osh, dhd->rxc_percpu_run_cnt, alloc_size);
		dhd->rxc_percpu_run_cnt = NULL;
	}
	if (dhd->txc_percpu_run_cnt) {
		MFREE(dhdp->osh, dhd->txc_percpu_run_cnt, alloc_size);
		dhd->txc_percpu_run_cnt = NULL;
	}
	if (dhd->cpu_online_cnt) {
		MFREE(dhdp->osh, dhd->cpu_online_cnt, alloc_size);
		dhd->cpu_online_cnt = NULL;
	}
	if (dhd->cpu_offline_cnt) {
		MFREE(dhdp->osh, dhd->cpu_offline_cnt, alloc_size);
		dhd->cpu_offline_cnt = NULL;
	}

	if (dhd->txp_percpu_run_cnt) {
		MFREE(dhdp->osh, dhd->txp_percpu_run_cnt, alloc_size);
		dhd->txp_percpu_run_cnt = NULL;
	}
	if (dhd->tx_start_percpu_run_cnt) {
		MFREE(dhdp->osh, dhd->tx_start_percpu_run_cnt, alloc_size);
		dhd->tx_start_percpu_run_cnt = NULL;
	}

	for (j = 0; j < HIST_BIN_SIZE; j++) {
		if (dhd->napi_rx_hist[j]) {
			MFREE(dhdp->osh, dhd->napi_rx_hist[j], alloc_size);
			dhd->napi_rx_hist[j] = NULL;
		}
#ifdef DHD_LB_TXC
		if (dhd->txc_hist[j]) {
			MFREE(dhdp->osh, dhd->txc_hist[j], alloc_size);
			dhd->txc_hist[j] = NULL;
		}
#endif /* DHD_LB_TXC */
#ifdef DHD_LB_RXC
		if (dhd->rxc_hist[j]) {
			MFREE(dhdp->osh, dhd->rxc_hist[j], alloc_size);
			dhd->rxc_hist[j] = NULL;
		}
#endif /* DHD_LB_RXC */
	}

	return;
}

static void dhd_lb_stats_dump_histo(
	struct bcmstrbuf *strbuf, uint32 **hist)
{
	int i, j;
	uint32 *per_cpu_total;
	uint32 total = 0;
	uint32 num_cpus = num_possible_cpus();

	per_cpu_total = (uint32 *)kmalloc(sizeof(uint32) * num_cpus, GFP_ATOMIC);
	if (!per_cpu_total) {
		DHD_ERROR(("%s(): dhd->per_cpu_total malloc failed \n", __FUNCTION__));
		return;
	}
	bzero(per_cpu_total, sizeof(uint32) * num_cpus);

	bcm_bprintf(strbuf, "CPU: \t\t");
	for (i = 0; i < num_cpus; i++)
		bcm_bprintf(strbuf, "%d\t", i);
	bcm_bprintf(strbuf, "\nBin\n");

	for (i = 0; i < HIST_BIN_SIZE; i++) {
		bcm_bprintf(strbuf, "%d:\t\t", 1<<i);
		for (j = 0; j < num_cpus; j++) {
			bcm_bprintf(strbuf, "%d\t", hist[i][j]);
		}
		bcm_bprintf(strbuf, "\n");
	}
	bcm_bprintf(strbuf, "Per CPU Total \t");
	total = 0;
	for (i = 0; i < num_cpus; i++) {
		for (j = 0; j < HIST_BIN_SIZE; j++) {
			per_cpu_total[i] += (hist[j][i] * (1<<j));
		}
		bcm_bprintf(strbuf, "%d\t", per_cpu_total[i]);
		total += per_cpu_total[i];
	}
	bcm_bprintf(strbuf, "\nTotal\t\t%d \n", total);

	kfree(per_cpu_total);
	return;
}

static inline void dhd_lb_stats_dump_cpu_array(struct bcmstrbuf *strbuf, uint32 *p)
{
	int i, num_cpus = num_possible_cpus();

	bcm_bprintf(strbuf, "CPU: \t");
	for (i = 0; i < num_cpus; i++)
		bcm_bprintf(strbuf, "%d\t", i);
	bcm_bprintf(strbuf, "\n");

	bcm_bprintf(strbuf, "Val: \t");
	for (i = 0; i < num_cpus; i++)
		bcm_bprintf(strbuf, "%u\t", *(p+i));
	bcm_bprintf(strbuf, "\n");
	return;
}

void dhd_lb_stats_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	dhd_info_t *dhd;

	if (dhdp == NULL || strbuf == NULL) {
		DHD_ERROR(("%s(): Invalid argument dhdp %p strbuf %p \n",
			__FUNCTION__, dhdp, strbuf));
		return;
	}

	dhd = dhdp->info;
	if (dhd == NULL) {
		DHD_ERROR(("%s(): DHD pointer is NULL \n", __FUNCTION__));
		return;
	}

	bcm_bprintf(strbuf, "\ncpu_online_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->cpu_online_cnt);

	bcm_bprintf(strbuf, "\ncpu_offline_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->cpu_offline_cnt);

	bcm_bprintf(strbuf, "\nsched_cnt: dhd_dpc %u napi %u rxc %u txc %u\n",
		dhd->dhd_dpc_cnt, dhd->napi_sched_cnt, dhd->rxc_sched_cnt,
		dhd->txc_sched_cnt);

#ifdef DHD_LB_RXP
	bcm_bprintf(strbuf, "\nnapi_percpu_run_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->napi_percpu_run_cnt);
	bcm_bprintf(strbuf, "\nNAPI Packets Received Histogram:\n");
	dhd_lb_stats_dump_histo(strbuf, dhd->napi_rx_hist);
#endif /* DHD_LB_RXP */

#ifdef DHD_LB_RXC
	bcm_bprintf(strbuf, "\nrxc_percpu_run_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->rxc_percpu_run_cnt);
	bcm_bprintf(strbuf, "\nRX Completions (Buffer Post) Histogram:\n");
	dhd_lb_stats_dump_histo(strbuf, dhd->rxc_hist);
#endif /* DHD_LB_RXC */

#ifdef DHD_LB_TXC
	bcm_bprintf(strbuf, "\ntxc_percpu_run_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->txc_percpu_run_cnt);
	bcm_bprintf(strbuf, "\nTX Completions (Buffer Free) Histogram:\n");
	dhd_lb_stats_dump_histo(strbuf, dhd->txc_hist);
#endif /* DHD_LB_TXC */

#ifdef DHD_LB_TXP
	bcm_bprintf(strbuf, "\ntxp_percpu_run_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->txp_percpu_run_cnt);

	bcm_bprintf(strbuf, "\ntx_start_percpu_run_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->tx_start_percpu_run_cnt);
#endif /* DHD_LB_TXP */

	bcm_bprintf(strbuf, "\nCPU masks primary(big)=0x%x secondary(little)=0x%x\n",
		DHD_LB_PRIMARY_CPUS, DHD_LB_SECONDARY_CPUS);

	bcm_bprintf(strbuf, "napi_cpu %x tx_cpu %x\n",
		atomic_read(&dhd->rx_napi_cpu), atomic_read(&dhd->tx_cpu));

}

/* Given a number 'n' returns 'm' that is next larger power of 2 after n */
static inline uint32 next_larger_power2(uint32 num)
{
	num--;
	num |= (num >> 1);
	num |= (num >> 2);
	num |= (num >> 4);
	num |= (num >> 8);
	num |= (num >> 16);

	return (num + 1);
}

static void dhd_lb_stats_update_histo(uint32 **bin, uint32 count, uint32 cpu)
{
	uint32 bin_power;
	uint32 *p;
	bin_power = next_larger_power2(count);

	switch (bin_power) {
		case   1: p = bin[0] + cpu; break;
		case   2: p = bin[1] + cpu; break;
		case   4: p = bin[2] + cpu; break;
		case   8: p = bin[3] + cpu; break;
		case  16: p = bin[4] + cpu; break;
		case  32: p = bin[5] + cpu; break;
		case  64: p = bin[6] + cpu; break;
		case 128: p = bin[7] + cpu; break;
		default : p = bin[8] + cpu; break;
	}

	*p = *p + 1;
	return;
}

extern void dhd_lb_stats_update_napi_histo(dhd_pub_t *dhdp, uint32 count)
{
	int cpu;
	dhd_info_t *dhd = dhdp->info;

	cpu = get_cpu();
	put_cpu();
	dhd_lb_stats_update_histo(dhd->napi_rx_hist, count, cpu);

	return;
}

extern void dhd_lb_stats_update_txc_histo(dhd_pub_t *dhdp, uint32 count)
{
	int cpu;
	dhd_info_t *dhd = dhdp->info;

	cpu = get_cpu();
	put_cpu();
	dhd_lb_stats_update_histo(dhd->txc_hist, count, cpu);

	return;
}

extern void dhd_lb_stats_update_rxc_histo(dhd_pub_t *dhdp, uint32 count)
{
	int cpu;
	dhd_info_t *dhd = dhdp->info;

	cpu = get_cpu();
	put_cpu();
	dhd_lb_stats_update_histo(dhd->rxc_hist, count, cpu);

	return;
}

extern void dhd_lb_stats_txc_percpu_cnt_incr(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	DHD_LB_STATS_PERCPU_ARR_INCR(dhd->txc_percpu_run_cnt);
}

extern void dhd_lb_stats_rxc_percpu_cnt_incr(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	DHD_LB_STATS_PERCPU_ARR_INCR(dhd->rxc_percpu_run_cnt);
}
#endif /* DHD_LB_STATS */

#endif /* DHD_LB */

#if defined(DISABLE_FRAMEBURST_VSDB) && defined(USE_WFA_CERT_CONF)
int g_frameburst = 1;
#endif /* DISABLE_FRAMEBURST_VSDB && USE_WFA_CERT_CONF */

static int dhd_get_pend_8021x_cnt(dhd_info_t *dhd);

/* DHD Perimiter lock only used in router with bypass forwarding. */
#define DHD_PERIM_RADIO_INIT()              do { /* noop */ } while (0)
#define DHD_PERIM_LOCK_TRY(unit, flag)      do { /* noop */ } while (0)
#define DHD_PERIM_UNLOCK_TRY(unit, flag)    do { /* noop */ } while (0)

#ifdef PCIE_FULL_DONGLE
#if defined(BCM_GMAC3)
#define DHD_IF_STA_LIST_LOCK_INIT(ifp)      do { /* noop */ } while (0)
#define DHD_IF_STA_LIST_LOCK(ifp, flags)    ({ BCM_REFERENCE(flags); })
#define DHD_IF_STA_LIST_UNLOCK(ifp, flags)  ({ BCM_REFERENCE(flags); })

#if defined(DHD_IGMP_UCQUERY) || defined(DHD_UCAST_UPNP)
#define DHD_IF_WMF_UCFORWARD_LOCK(dhd, ifp, slist) ({ BCM_REFERENCE(slist); &(ifp)->sta_list; })
#define DHD_IF_WMF_UCFORWARD_UNLOCK(dhd, slist) ({ BCM_REFERENCE(slist); })
#endif /* DHD_IGMP_UCQUERY || DHD_UCAST_UPNP */

#else /* ! BCM_GMAC3 */
#define DHD_IF_STA_LIST_LOCK_INIT(ifp) spin_lock_init(&(ifp)->sta_list_lock)
#define DHD_IF_STA_LIST_LOCK(ifp, flags) \
	spin_lock_irqsave(&(ifp)->sta_list_lock, (flags))
#define DHD_IF_STA_LIST_UNLOCK(ifp, flags) \
	spin_unlock_irqrestore(&(ifp)->sta_list_lock, (flags))

#if defined(DHD_IGMP_UCQUERY) || defined(DHD_UCAST_UPNP)
static struct list_head * dhd_sta_list_snapshot(dhd_info_t *dhd, dhd_if_t *ifp,
	struct list_head *snapshot_list);
static void dhd_sta_list_snapshot_free(dhd_info_t *dhd, struct list_head *snapshot_list);
#define DHD_IF_WMF_UCFORWARD_LOCK(dhd, ifp, slist) ({ dhd_sta_list_snapshot(dhd, ifp, slist); })
#define DHD_IF_WMF_UCFORWARD_UNLOCK(dhd, slist) ({ dhd_sta_list_snapshot_free(dhd, slist); })
#endif /* DHD_IGMP_UCQUERY || DHD_UCAST_UPNP */

#endif /* ! BCM_GMAC3 */
#endif /* PCIE_FULL_DONGLE */

/* Control fw roaming */
uint dhd_roam_disable = 0;

#ifdef BCMDBGFS
extern void dhd_dbgfs_init(dhd_pub_t *dhdp);
extern void dhd_dbgfs_remove(void);
#endif


/* Control radio state */
uint dhd_radio_up = 1;

/* Network inteface name */
char iface_name[IFNAMSIZ] = {'\0'};
module_param_string(iface_name, iface_name, IFNAMSIZ, 0);

/* The following are specific to the SDIO dongle */

/* IOCTL response timeout */
int dhd_ioctl_timeout_msec = IOCTL_RESP_TIMEOUT;

/* DS Exit response timeout */
int ds_exit_timeout_msec = DS_EXIT_TIMEOUT;

/* Idle timeout for backplane clock */
int dhd_idletime = DHD_IDLETIME_TICKS;
module_param(dhd_idletime, int, 0);

/* Use polling */
uint dhd_poll = FALSE;
module_param(dhd_poll, uint, 0);

/* Use interrupts */
uint dhd_intr = TRUE;
module_param(dhd_intr, uint, 0);

/* SDIO Drive Strength (in milliamps) */
uint dhd_sdiod_drive_strength = 6;
module_param(dhd_sdiod_drive_strength, uint, 0);

#ifdef BCMSDIO
/* Tx/Rx bounds */
extern uint dhd_txbound;
extern uint dhd_rxbound;
module_param(dhd_txbound, uint, 0);
module_param(dhd_rxbound, uint, 0);

/* Deferred transmits */
extern uint dhd_deferred_tx;
module_param(dhd_deferred_tx, uint, 0);

#endif /* BCMSDIO */


#ifdef SDTEST
/* Echo packet generator (pkts/s) */
uint dhd_pktgen = 0;
module_param(dhd_pktgen, uint, 0);

/* Echo packet len (0 => sawtooth, max 2040) */
uint dhd_pktgen_len = 0;
module_param(dhd_pktgen_len, uint, 0);
#endif /* SDTEST */



#ifndef BCMDBUS
/* Allow delayed firmware download for debug purpose */
int allow_delay_fwdl = FALSE;
module_param(allow_delay_fwdl, int, 0);
#endif /* !BCMDBUS */

extern char dhd_version[];
extern char fw_version[];
extern char clm_version[];

int dhd_net_bus_devreset(struct net_device *dev, uint8 flag);
static void dhd_net_if_lock_local(dhd_info_t *dhd);
static void dhd_net_if_unlock_local(dhd_info_t *dhd);
static void dhd_suspend_lock(dhd_pub_t *dhdp);
static void dhd_suspend_unlock(dhd_pub_t *dhdp);

#ifdef WLMEDIA_HTSF
void htsf_update(dhd_info_t *dhd, void *data);
tsf_t prev_tsf, cur_tsf;

uint32 dhd_get_htsf(dhd_info_t *dhd, int ifidx);
static int dhd_ioctl_htsf_get(dhd_info_t *dhd, int ifidx);
static void dhd_dump_latency(void);
static void dhd_htsf_addtxts(dhd_pub_t *dhdp, void *pktbuf);
static void dhd_htsf_addrxts(dhd_pub_t *dhdp, void *pktbuf);
static void dhd_dump_htsfhisto(histo_t *his, char *s);
#endif /* WLMEDIA_HTSF */

/* Monitor interface */
int dhd_monitor_init(void *dhd_pub);
int dhd_monitor_uninit(void);


#if defined(WL_WIRELESS_EXT)
struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev);
#endif /* defined(WL_WIRELESS_EXT) */

#ifndef BCMDBUS
static void dhd_dpc(ulong data);
#endif /* !BCMDBUS */
/* forward decl */
extern int dhd_wait_pend8021x(struct net_device *dev);
void dhd_os_wd_timer_extend(void *bus, bool extend);

#ifdef TOE
#ifndef BDC
#error TOE requires BDC
#endif /* !BDC */
static int dhd_toe_get(dhd_info_t *dhd, int idx, uint32 *toe_ol);
static int dhd_toe_set(dhd_info_t *dhd, int idx, uint32 toe_ol);
#endif /* TOE */

static int dhd_wl_host_event(dhd_info_t *dhd, int ifidx, void *pktdata, uint16 pktlen,
		wl_event_msg_t *event_ptr, void **data_ptr);

#if defined(CONFIG_PM_SLEEP)
static int dhd_pm_callback(struct notifier_block *nfb, unsigned long action, void *ignored)
{
	int ret = NOTIFY_DONE;
	bool suspend = FALSE;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	dhd_info_t *dhdinfo = (dhd_info_t*)container_of(nfb, struct dhd_info, pm_notifier);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	BCM_REFERENCE(dhdinfo);
	BCM_REFERENCE(suspend);

	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		suspend = TRUE;
		break;

	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		suspend = FALSE;
		break;
	}

	printf("%s: action=%ld, suspend=%d, suspend_mode=%d\n",
		__FUNCTION__, action, suspend, dhdinfo->pub.conf->suspend_mode);
	if (suspend) {
		DHD_OS_WAKE_LOCK_WAIVE(&dhdinfo->pub);
		if (dhdinfo->pub.conf->suspend_mode == PM_NOTIFIER)
			dhd_suspend_resume_helper(dhdinfo, suspend, 0);
#if defined(SUPPORT_P2P_GO_PS) && defined(PROP_TXSTATUS)
		dhd_wlfc_suspend(&dhdinfo->pub);
#endif /* defined(SUPPORT_P2P_GO_PS) && defined(PROP_TXSTATUS) */
		dhd_conf_set_suspend_resume(&dhdinfo->pub, suspend, PM_NOTIFIER);
		DHD_OS_WAKE_LOCK_RESTORE(&dhdinfo->pub);
	} else {
		dhd_conf_set_suspend_resume(&dhdinfo->pub, suspend, PM_NOTIFIER);
#if defined(SUPPORT_P2P_GO_PS) && defined(PROP_TXSTATUS)
		dhd_wlfc_resume(&dhdinfo->pub);
#endif /* defined(SUPPORT_P2P_GO_PS) && defined(PROP_TXSTATUS) */
		if (dhdinfo->pub.conf->suspend_mode == PM_NOTIFIER)
			dhd_suspend_resume_helper(dhdinfo, suspend, 0);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && (LINUX_VERSION_CODE <= \
	KERNEL_VERSION(2, 6, 39))
	dhd_mmc_suspend = suspend;
	smp_mb();
#endif

	return ret;
}

/* to make sure we won't register the same notifier twice, otherwise a loop is likely to be
 * created in kernel notifier link list (with 'next' pointing to itself)
 */
static bool dhd_pm_notifier_registered = FALSE;

extern int register_pm_notifier(struct notifier_block *nb);
extern int unregister_pm_notifier(struct notifier_block *nb);
#endif /* CONFIG_PM_SLEEP */

/* Request scheduling of the bus rx frame */
static void dhd_sched_rxf(dhd_pub_t *dhdp, void *skb);
static void dhd_os_rxflock(dhd_pub_t *pub);
static void dhd_os_rxfunlock(dhd_pub_t *pub);

/** priv_link is the link between netdev and the dhdif and dhd_info structs. */
typedef struct dhd_dev_priv {
	dhd_info_t * dhd; /* cached pointer to dhd_info in netdevice priv */
	dhd_if_t   * ifp; /* cached pointer to dhd_if in netdevice priv */
	int          ifidx; /* interface index */
	void       * lkup;
} dhd_dev_priv_t;

#define DHD_DEV_PRIV_SIZE       (sizeof(dhd_dev_priv_t))
#define DHD_DEV_PRIV(dev)       ((dhd_dev_priv_t *)DEV_PRIV(dev))
#define DHD_DEV_INFO(dev)       (((dhd_dev_priv_t *)DEV_PRIV(dev))->dhd)
#define DHD_DEV_IFP(dev)        (((dhd_dev_priv_t *)DEV_PRIV(dev))->ifp)
#define DHD_DEV_IFIDX(dev)      (((dhd_dev_priv_t *)DEV_PRIV(dev))->ifidx)
#define DHD_DEV_LKUP(dev)		(((dhd_dev_priv_t *)DEV_PRIV(dev))->lkup)

#if defined(DHD_OF_SUPPORT)
extern int dhd_wlan_init(void);
#endif /* defined(DHD_OF_SUPPORT) */
/** Clear the dhd net_device's private structure. */
static inline void
dhd_dev_priv_clear(struct net_device * dev)
{
	dhd_dev_priv_t * dev_priv;
	ASSERT(dev != (struct net_device *)NULL);
	dev_priv = DHD_DEV_PRIV(dev);
	dev_priv->dhd = (dhd_info_t *)NULL;
	dev_priv->ifp = (dhd_if_t *)NULL;
	dev_priv->ifidx = DHD_BAD_IF;
	dev_priv->lkup = (void *)NULL;
}

/** Setup the dhd net_device's private structure. */
static inline void
dhd_dev_priv_save(struct net_device * dev, dhd_info_t * dhd, dhd_if_t * ifp,
                  int ifidx)
{
	dhd_dev_priv_t * dev_priv;
	ASSERT(dev != (struct net_device *)NULL);
	dev_priv = DHD_DEV_PRIV(dev);
	dev_priv->dhd = dhd;
	dev_priv->ifp = ifp;
	dev_priv->ifidx = ifidx;
}

#ifdef PCIE_FULL_DONGLE

/** Dummy objects are defined with state representing bad|down.
 * Performance gains from reducing branch conditionals, instruction parallelism,
 * dual issue, reducing load shadows, avail of larger pipelines.
 * Use DHD_XXX_NULL instead of (dhd_xxx_t *)NULL, whenever an object pointer
 * is accessed via the dhd_sta_t.
 */

/* Dummy dhd_info object */
dhd_info_t dhd_info_null = {
#if defined(BCM_GMAC3)
	.fwdh = FWDER_NULL,
#endif
	.pub = {
	         .info = &dhd_info_null,
#ifdef DHDTCPACK_SUPPRESS
	         .tcpack_sup_mode = TCPACK_SUP_REPLACE,
#endif /* DHDTCPACK_SUPPRESS */
#if defined(TRAFFIC_MGMT_DWM)
	         .dhd_tm_dwm_tbl = { .dhd_dwm_enabled = TRUE },
#endif
	         .up = FALSE,
	         .busstate = DHD_BUS_DOWN
	}
};
#define DHD_INFO_NULL (&dhd_info_null)
#define DHD_PUB_NULL  (&dhd_info_null.pub)

/* Dummy netdevice object */
struct net_device dhd_net_dev_null = {
	.reg_state = NETREG_UNREGISTERED
};
#define DHD_NET_DEV_NULL (&dhd_net_dev_null)

/* Dummy dhd_if object */
dhd_if_t dhd_if_null = {
#if defined(BCM_GMAC3)
	.fwdh = FWDER_NULL,
#endif
#ifdef WMF
	.wmf = { .wmf_enable = TRUE },
#endif
	.info = DHD_INFO_NULL,
	.net = DHD_NET_DEV_NULL,
	.idx = DHD_BAD_IF
};
#define DHD_IF_NULL  (&dhd_if_null)

#define DHD_STA_NULL ((dhd_sta_t *)NULL)

/** Interface STA list management. */

/** Fetch the dhd_if object, given the interface index in the dhd. */
static inline dhd_if_t *dhd_get_ifp(dhd_pub_t *dhdp, uint32 ifidx);

/** Alloc/Free a dhd_sta object from the dhd instances' sta_pool. */
static void dhd_sta_free(dhd_pub_t *pub, dhd_sta_t *sta);
static dhd_sta_t * dhd_sta_alloc(dhd_pub_t * dhdp);

/* Delete a dhd_sta or flush all dhd_sta in an interface's sta_list. */
static void dhd_if_del_sta_list(dhd_if_t * ifp);
static void	dhd_if_flush_sta(dhd_if_t * ifp);

/* Construct/Destruct a sta pool. */
static int dhd_sta_pool_init(dhd_pub_t *dhdp, int max_sta);
static void dhd_sta_pool_fini(dhd_pub_t *dhdp, int max_sta);
/* Clear the pool of dhd_sta_t objects for built-in type driver */
static void dhd_sta_pool_clear(dhd_pub_t *dhdp, int max_sta);


/* Return interface pointer */
static inline dhd_if_t *dhd_get_ifp(dhd_pub_t *dhdp, uint32 ifidx)
{
	ASSERT(ifidx < DHD_MAX_IFS);

	if (ifidx >= DHD_MAX_IFS)
		return NULL;

	return dhdp->info->iflist[ifidx];
}

/** Reset a dhd_sta object and free into the dhd pool. */
static void
dhd_sta_free(dhd_pub_t * dhdp, dhd_sta_t * sta)
{
	int prio;

	ASSERT((sta != DHD_STA_NULL) && (sta->idx != ID16_INVALID));

	ASSERT((dhdp->staid_allocator != NULL) && (dhdp->sta_pool != NULL));

	/*
	 * Flush and free all packets in all flowring's queues belonging to sta.
	 * Packets in flow ring will be flushed later.
	 */
	for (prio = 0; prio < (int)NUMPRIO; prio++) {
		uint16 flowid = sta->flowid[prio];

		if (flowid != FLOWID_INVALID) {
			unsigned long flags;
			flow_queue_t * queue = dhd_flow_queue(dhdp, flowid);
			flow_ring_node_t * flow_ring_node;

#ifdef DHDTCPACK_SUPPRESS
			/* Clean tcp_ack_info_tbl in order to prevent access to flushed pkt,
			 * when there is a newly coming packet from network stack.
			 */
			dhd_tcpack_info_tbl_clean(dhdp);
#endif /* DHDTCPACK_SUPPRESS */

			flow_ring_node = dhd_flow_ring_node(dhdp, flowid);
			DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);
			flow_ring_node->status = FLOW_RING_STATUS_STA_FREEING;

			if (!DHD_FLOW_QUEUE_EMPTY(queue)) {
				void * pkt;
				while ((pkt = dhd_flow_queue_dequeue(dhdp, queue)) != NULL) {
					PKTFREE(dhdp->osh, pkt, TRUE);
				}
			}

			DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
			ASSERT(DHD_FLOW_QUEUE_EMPTY(queue));
		}

		sta->flowid[prio] = FLOWID_INVALID;
	}

	id16_map_free(dhdp->staid_allocator, sta->idx);
	DHD_CUMM_CTR_INIT(&sta->cumm_ctr);
	sta->ifp = DHD_IF_NULL; /* dummy dhd_if object */
	sta->ifidx = DHD_BAD_IF;
	bzero(sta->ea.octet, ETHER_ADDR_LEN);
	INIT_LIST_HEAD(&sta->list);
	sta->idx = ID16_INVALID; /* implying free */
}

/** Allocate a dhd_sta object from the dhd pool. */
static dhd_sta_t *
dhd_sta_alloc(dhd_pub_t * dhdp)
{
	uint16 idx;
	dhd_sta_t * sta;
	dhd_sta_pool_t * sta_pool;

	ASSERT((dhdp->staid_allocator != NULL) && (dhdp->sta_pool != NULL));

	idx = id16_map_alloc(dhdp->staid_allocator);
	if (idx == ID16_INVALID) {
		DHD_ERROR(("%s: cannot get free staid\n", __FUNCTION__));
		return DHD_STA_NULL;
	}

	sta_pool = (dhd_sta_pool_t *)(dhdp->sta_pool);
	sta = &sta_pool[idx];

	ASSERT((sta->idx == ID16_INVALID) &&
	       (sta->ifp == DHD_IF_NULL) && (sta->ifidx == DHD_BAD_IF));

	DHD_CUMM_CTR_INIT(&sta->cumm_ctr);

	sta->idx = idx; /* implying allocated */

	return sta;
}

/** Delete all STAs in an interface's STA list. */
static void
dhd_if_del_sta_list(dhd_if_t *ifp)
{
	dhd_sta_t *sta, *next;
	unsigned long flags;

	DHD_IF_STA_LIST_LOCK(ifp, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
#if defined(BCM_GMAC3)
		if (ifp->fwdh) {
			/* Remove sta from WOFA forwarder. */
			fwder_deassoc(ifp->fwdh, (uint16 *)(sta->ea.octet), (uintptr_t)sta);
		}
#endif /* BCM_GMAC3 */
		list_del(&sta->list);
		dhd_sta_free(&ifp->info->pub, sta);
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	DHD_IF_STA_LIST_UNLOCK(ifp, flags);

	return;
}

/** Router/GMAC3: Flush all station entries in the forwarder's WOFA database. */
static void
dhd_if_flush_sta(dhd_if_t * ifp)
{
#if defined(BCM_GMAC3)

	if (ifp && (ifp->fwdh != FWDER_NULL)) {
		dhd_sta_t *sta, *next;
		unsigned long flags;

		DHD_IF_STA_LIST_LOCK(ifp, flags);

		list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
			/* Remove any sta entry from WOFA forwarder. */
			fwder_flush(ifp->fwdh, (uintptr_t)sta);
		}

		DHD_IF_STA_LIST_UNLOCK(ifp, flags);
	}
#endif /* BCM_GMAC3 */
}

/** Construct a pool of dhd_sta_t objects to be used by interfaces. */
static int
dhd_sta_pool_init(dhd_pub_t *dhdp, int max_sta)
{
	int idx, prio, sta_pool_memsz;
	dhd_sta_t * sta;
	dhd_sta_pool_t * sta_pool;
	void * staid_allocator;

	ASSERT(dhdp != (dhd_pub_t *)NULL);
	ASSERT((dhdp->staid_allocator == NULL) && (dhdp->sta_pool == NULL));

	/* dhd_sta objects per radio are managed in a table. id#0 reserved. */
	staid_allocator = id16_map_init(dhdp->osh, max_sta, 1);
	if (staid_allocator == NULL) {
		DHD_ERROR(("%s: sta id allocator init failure\n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* Pre allocate a pool of dhd_sta objects (one extra). */
	sta_pool_memsz = ((max_sta + 1) * sizeof(dhd_sta_t)); /* skip idx 0 */
	sta_pool = (dhd_sta_pool_t *)MALLOC(dhdp->osh, sta_pool_memsz);
	if (sta_pool == NULL) {
		DHD_ERROR(("%s: sta table alloc failure\n", __FUNCTION__));
		id16_map_fini(dhdp->osh, staid_allocator);
		return BCME_ERROR;
	}

	dhdp->sta_pool = sta_pool;
	dhdp->staid_allocator = staid_allocator;

	/* Initialize all sta(s) for the pre-allocated free pool. */
	bzero((uchar *)sta_pool, sta_pool_memsz);
	for (idx = max_sta; idx >= 1; idx--) { /* skip sta_pool[0] */
		sta = &sta_pool[idx];
		sta->idx = id16_map_alloc(staid_allocator);
		ASSERT(sta->idx <= max_sta);
	}
	/* Now place them into the pre-allocated free pool. */
	for (idx = 1; idx <= max_sta; idx++) {
		sta = &sta_pool[idx];
		for (prio = 0; prio < (int)NUMPRIO; prio++) {
			sta->flowid[prio] = FLOWID_INVALID; /* Flow rings do not exist */
		}
		dhd_sta_free(dhdp, sta);
	}

	return BCME_OK;
}

/** Destruct the pool of dhd_sta_t objects.
 * Caller must ensure that no STA objects are currently associated with an if.
 */
static void
dhd_sta_pool_fini(dhd_pub_t *dhdp, int max_sta)
{
	dhd_sta_pool_t * sta_pool = (dhd_sta_pool_t *)dhdp->sta_pool;

	if (sta_pool) {
		int idx;
		int sta_pool_memsz = ((max_sta + 1) * sizeof(dhd_sta_t));
		for (idx = 1; idx <= max_sta; idx++) {
			ASSERT(sta_pool[idx].ifp == DHD_IF_NULL);
			ASSERT(sta_pool[idx].idx == ID16_INVALID);
		}
		MFREE(dhdp->osh, dhdp->sta_pool, sta_pool_memsz);
		dhdp->sta_pool = NULL;
	}

	id16_map_fini(dhdp->osh, dhdp->staid_allocator);
	dhdp->staid_allocator = NULL;
}

/* Clear the pool of dhd_sta_t objects for built-in type driver */
static void
dhd_sta_pool_clear(dhd_pub_t *dhdp, int max_sta)
{
	int idx, prio, sta_pool_memsz;
	dhd_sta_t * sta;
	dhd_sta_pool_t * sta_pool;
	void *staid_allocator;

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return;
	}

	sta_pool = (dhd_sta_pool_t *)dhdp->sta_pool;
	staid_allocator = dhdp->staid_allocator;

	if (!sta_pool) {
		DHD_ERROR(("%s: sta_pool is NULL\n", __FUNCTION__));
		return;
	}

	if (!staid_allocator) {
		DHD_ERROR(("%s: staid_allocator is NULL\n", __FUNCTION__));
		return;
	}

	/* clear free pool */
	sta_pool_memsz = ((max_sta + 1) * sizeof(dhd_sta_t));
	bzero((uchar *)sta_pool, sta_pool_memsz);

	/* dhd_sta objects per radio are managed in a table. id#0 reserved. */
	id16_map_clear(staid_allocator, max_sta, 1);

	/* Initialize all sta(s) for the pre-allocated free pool. */
	for (idx = max_sta; idx >= 1; idx--) { /* skip sta_pool[0] */
		sta = &sta_pool[idx];
		sta->idx = id16_map_alloc(staid_allocator);
		ASSERT(sta->idx <= max_sta);
	}
	/* Now place them into the pre-allocated free pool. */
	for (idx = 1; idx <= max_sta; idx++) {
		sta = &sta_pool[idx];
		for (prio = 0; prio < (int)NUMPRIO; prio++) {
			sta->flowid[prio] = FLOWID_INVALID; /* Flow rings do not exist */
		}
		dhd_sta_free(dhdp, sta);
	}
}

/** Find STA with MAC address ea in an interface's STA list. */
dhd_sta_t *
dhd_find_sta(void *pub, int ifidx, void *ea)
{
	dhd_sta_t *sta;
	dhd_if_t *ifp;
	unsigned long flags;

	ASSERT(ea != NULL);
	ifp = dhd_get_ifp((dhd_pub_t *)pub, ifidx);
	if (ifp == NULL)
		return DHD_STA_NULL;

	DHD_IF_STA_LIST_LOCK(ifp, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry(sta, &ifp->sta_list, list) {
		if (!memcmp(sta->ea.octet, ea, ETHER_ADDR_LEN)) {
			DHD_INFO(("%s: found STA " MACDBG "\n",
				__FUNCTION__, MAC2STRDBG((char *)ea)));
			DHD_IF_STA_LIST_UNLOCK(ifp, flags);
			return sta;
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	DHD_IF_STA_LIST_UNLOCK(ifp, flags);

	return DHD_STA_NULL;
}

/** Add STA into the interface's STA list. */
dhd_sta_t *
dhd_add_sta(void *pub, int ifidx, void *ea)
{
	dhd_sta_t *sta;
	dhd_if_t *ifp;
	unsigned long flags;

	ASSERT(ea != NULL);
	ifp = dhd_get_ifp((dhd_pub_t *)pub, ifidx);
	if (ifp == NULL)
		return DHD_STA_NULL;

	sta = dhd_sta_alloc((dhd_pub_t *)pub);
	if (sta == DHD_STA_NULL) {
		DHD_ERROR(("%s: Alloc failed\n", __FUNCTION__));
		return DHD_STA_NULL;
	}

	memcpy(sta->ea.octet, ea, ETHER_ADDR_LEN);

	/* link the sta and the dhd interface */
	sta->ifp = ifp;
	sta->ifidx = ifidx;
#ifdef DHD_WMF
	sta->psta_prim = NULL;
#endif
	INIT_LIST_HEAD(&sta->list);

	DHD_IF_STA_LIST_LOCK(ifp, flags);

	list_add_tail(&sta->list, &ifp->sta_list);

#if defined(BCM_GMAC3)
	if (ifp->fwdh) {
		ASSERT(ISALIGNED(ea, 2));
		/* Add sta to WOFA forwarder. */
		fwder_reassoc(ifp->fwdh, (uint16 *)ea, (uintptr_t)sta);
	}
#endif /* BCM_GMAC3 */

	DHD_IF_STA_LIST_UNLOCK(ifp, flags);

	return sta;
}

/** Delete all STAs from the interface's STA list. */
void
dhd_del_all_sta(void *pub, int ifidx)
{
	dhd_sta_t *sta, *next;
	dhd_if_t *ifp;
	unsigned long flags;

	ifp = dhd_get_ifp((dhd_pub_t *)pub, ifidx);
	if (ifp == NULL)
		return;

	DHD_IF_STA_LIST_LOCK(ifp, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
#if defined(BCM_GMAC3)
		if (ifp->fwdh) { /* Found a sta, remove from WOFA forwarder. */
			ASSERT(ISALIGNED(sta->ea.octet, 2));
			fwder_deassoc(ifp->fwdh, (uint16 *)sta->ea.octet, (uintptr_t)sta);
		}
#endif /* BCM_GMAC3 */

		list_del(&sta->list);
		dhd_sta_free(&ifp->info->pub, sta);
#ifdef DHD_L2_FILTER
		if (ifp->parp_enable) {
			/* clear Proxy ARP cache of specific Ethernet Address */
			bcm_l2_filter_arp_table_update(((dhd_pub_t*)pub)->osh,
					ifp->phnd_arp_table, FALSE,
					sta->ea.octet, FALSE, ((dhd_pub_t*)pub)->tickcnt);
		}
#endif /* DHD_L2_FILTER */
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	DHD_IF_STA_LIST_UNLOCK(ifp, flags);

	return;
}

/** Delete STA from the interface's STA list. */
void
dhd_del_sta(void *pub, int ifidx, void *ea)
{
	dhd_sta_t *sta, *next;
	dhd_if_t *ifp;
	unsigned long flags;
	char macstr[ETHER_ADDR_STR_LEN];

	ASSERT(ea != NULL);
	ifp = dhd_get_ifp((dhd_pub_t *)pub, ifidx);
	if (ifp == NULL)
		return;

	DHD_IF_STA_LIST_LOCK(ifp, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
		if (!memcmp(sta->ea.octet, ea, ETHER_ADDR_LEN)) {
#if defined(BCM_GMAC3)
			if (ifp->fwdh) { /* Found a sta, remove from WOFA forwarder. */
				ASSERT(ISALIGNED(ea, 2));
				fwder_deassoc(ifp->fwdh, (uint16 *)ea, (uintptr_t)sta);
			}
#endif /* BCM_GMAC3 */
			DHD_MAC_TO_STR(((char *)ea), macstr);
			DHD_ERROR(("%s: Deleting STA  %s\n", __FUNCTION__, macstr));
			list_del(&sta->list);
			dhd_sta_free(&ifp->info->pub, sta);
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	DHD_IF_STA_LIST_UNLOCK(ifp, flags);
#ifdef DHD_L2_FILTER
	if (ifp->parp_enable) {
		/* clear Proxy ARP cache of specific Ethernet Address */
		bcm_l2_filter_arp_table_update(((dhd_pub_t*)pub)->osh, ifp->phnd_arp_table, FALSE,
			ea, FALSE, ((dhd_pub_t*)pub)->tickcnt);
	}
#endif /* DHD_L2_FILTER */
	return;
}

/** Add STA if it doesn't exist. Not reentrant. */
dhd_sta_t*
dhd_findadd_sta(void *pub, int ifidx, void *ea)
{
	dhd_sta_t *sta;

	sta = dhd_find_sta(pub, ifidx, ea);

	if (!sta) {
		/* Add entry */
		sta = dhd_add_sta(pub, ifidx, ea);
	}

	return sta;
}

#if defined(DHD_IGMP_UCQUERY) || defined(DHD_UCAST_UPNP)
#if !defined(BCM_GMAC3)
static struct list_head *
dhd_sta_list_snapshot(dhd_info_t *dhd, dhd_if_t *ifp, struct list_head *snapshot_list)
{
	unsigned long flags;
	dhd_sta_t *sta, *snapshot;

	INIT_LIST_HEAD(snapshot_list);

	DHD_IF_STA_LIST_LOCK(ifp, flags);

	list_for_each_entry(sta, &ifp->sta_list, list) {
		/* allocate one and add to snapshot */
		snapshot = (dhd_sta_t *)MALLOC(dhd->pub.osh, sizeof(dhd_sta_t));
		if (snapshot == NULL) {
			DHD_ERROR(("%s: Cannot allocate memory\n", __FUNCTION__));
			continue;
		}

		memcpy(snapshot->ea.octet, sta->ea.octet, ETHER_ADDR_LEN);

		INIT_LIST_HEAD(&snapshot->list);
		list_add_tail(&snapshot->list, snapshot_list);
	}

	DHD_IF_STA_LIST_UNLOCK(ifp, flags);

	return snapshot_list;
}

static void
dhd_sta_list_snapshot_free(dhd_info_t *dhd, struct list_head *snapshot_list)
{
	dhd_sta_t *sta, *next;

	list_for_each_entry_safe(sta, next, snapshot_list, list) {
		list_del(&sta->list);
		MFREE(dhd->pub.osh, sta, sizeof(dhd_sta_t));
	}
}
#endif /* !BCM_GMAC3 */
#endif /* DHD_IGMP_UCQUERY || DHD_UCAST_UPNP */

#else
static inline void dhd_if_flush_sta(dhd_if_t * ifp) { }
static inline void dhd_if_del_sta_list(dhd_if_t *ifp) {}
static inline int dhd_sta_pool_init(dhd_pub_t *dhdp, int max_sta) { return BCME_OK; }
static inline void dhd_sta_pool_fini(dhd_pub_t *dhdp, int max_sta) {}
static inline void dhd_sta_pool_clear(dhd_pub_t *dhdp, int max_sta) {}
dhd_sta_t *dhd_findadd_sta(void *pub, int ifidx, void *ea) { return NULL; }
dhd_sta_t *dhd_find_sta(void *pub, int ifidx, void *ea) { return NULL; }
void dhd_del_sta(void *pub, int ifidx, void *ea) {}
#endif /* PCIE_FULL_DONGLE */



#if defined(DHD_LB)

#if defined(DHD_LB_TXC) || defined(DHD_LB_RXC) || defined(DHD_LB_TXP)
/**
 * dhd_tasklet_schedule - Function that runs in IPI context of the destination
 * CPU and schedules a tasklet.
 * @tasklet: opaque pointer to the tasklet
 */
INLINE void
dhd_tasklet_schedule(void *tasklet)
{
	tasklet_schedule((struct tasklet_struct *)tasklet);
}
/**
 * dhd_tasklet_schedule_on - Executes the passed takslet in a given CPU
 * @tasklet: tasklet to be scheduled
 * @on_cpu: cpu core id
 *
 * If the requested cpu is online, then an IPI is sent to this cpu via the
 * smp_call_function_single with no wait and the tasklet_schedule function
 * will be invoked to schedule the specified tasklet on the requested CPU.
 */
INLINE void
dhd_tasklet_schedule_on(struct tasklet_struct *tasklet, int on_cpu)
{
	const int wait = 0;
	smp_call_function_single(on_cpu,
		dhd_tasklet_schedule, (void *)tasklet, wait);
}

/**
 * dhd_work_schedule_on - Executes the passed work in a given CPU
 * @work: work to be scheduled
 * @on_cpu: cpu core id
 *
 * If the requested cpu is online, then an IPI is sent to this cpu via the
 * schedule_work_on and the work function
 * will be invoked to schedule the specified work on the requested CPU.
 */

INLINE void
dhd_work_schedule_on(struct work_struct *work, int on_cpu)
{
	schedule_work_on(on_cpu, work);
}
#endif /* DHD_LB_TXC || DHD_LB_RXC || DHD_LB_TXP */

#if defined(DHD_LB_TXC)
/**
 * dhd_lb_tx_compl_dispatch - load balance by dispatching the tx_compl_tasklet
 * on another cpu. The tx_compl_tasklet will take care of DMA unmapping and
 * freeing the packets placed in the tx_compl workq
 */
void
dhd_lb_tx_compl_dispatch(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	int curr_cpu, on_cpu;

	if (dhd->rx_napi_netdev == NULL) {
		DHD_ERROR(("%s: dhd->rx_napi_netdev is NULL\n", __FUNCTION__));
		return;
	}

	DHD_LB_STATS_INCR(dhd->txc_sched_cnt);
	/*
	 * If the destination CPU is NOT online or is same as current CPU
	 * no need to schedule the work
	 */
	curr_cpu = get_cpu();
	put_cpu();

	on_cpu = atomic_read(&dhd->tx_compl_cpu);

	if ((on_cpu == curr_cpu) || (!cpu_online(on_cpu))) {
		dhd_tasklet_schedule(&dhd->tx_compl_tasklet);
	} else {
		schedule_work(&dhd->tx_compl_dispatcher_work);
	}
}

static void dhd_tx_compl_dispatcher_fn(struct work_struct * work)
{
	struct dhd_info *dhd =
		container_of(work, struct dhd_info, tx_compl_dispatcher_work);
	int cpu;

	get_online_cpus();
	cpu = atomic_read(&dhd->tx_compl_cpu);
	if (!cpu_online(cpu))
		dhd_tasklet_schedule(&dhd->tx_compl_tasklet);
	else
		dhd_tasklet_schedule_on(&dhd->tx_compl_tasklet, cpu);
	put_online_cpus();
}
#endif /* DHD_LB_TXC */

#if defined(DHD_LB_RXC)
/**
 * dhd_lb_rx_compl_dispatch - load balance by dispatching the rx_compl_tasklet
 * on another cpu. The rx_compl_tasklet will take care of reposting rx buffers
 * in the H2D RxBuffer Post common ring, by using the recycled pktids that were
 * placed in the rx_compl workq.
 *
 * @dhdp: pointer to dhd_pub object
 */
void
dhd_lb_rx_compl_dispatch(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	int curr_cpu, on_cpu;

	if (dhd->rx_napi_netdev == NULL) {
		DHD_ERROR(("%s: dhd->rx_napi_netdev is NULL\n", __FUNCTION__));
		return;
	}

	DHD_LB_STATS_INCR(dhd->rxc_sched_cnt);
	/*
	 * If the destination CPU is NOT online or is same as current CPU
	 * no need to schedule the work
	 */
	curr_cpu = get_cpu();
	put_cpu();
	on_cpu = atomic_read(&dhd->rx_compl_cpu);

	if ((on_cpu == curr_cpu) || (!cpu_online(on_cpu))) {
		dhd_tasklet_schedule(&dhd->rx_compl_tasklet);
	} else {
		dhd_rx_compl_dispatcher_fn(dhdp);
	}
}

static void dhd_rx_compl_dispatcher_fn(dhd_pub_t *dhdp)
{
	struct dhd_info *dhd = dhdp->info;
	int cpu;

	preempt_disable();
	cpu = atomic_read(&dhd->rx_compl_cpu);
	if (!cpu_online(cpu))
		dhd_tasklet_schedule(&dhd->rx_compl_tasklet);
	else {
		dhd_tasklet_schedule_on(&dhd->rx_compl_tasklet, cpu);
	}
	preempt_enable();
}
#endif /* DHD_LB_RXC */

#if defined(DHD_LB_TXP)
static void dhd_tx_dispatcher_work(struct work_struct * work)
{
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	struct dhd_info *dhd =
		container_of(work, struct dhd_info, tx_dispatcher_work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	dhd_tasklet_schedule(&dhd->tx_tasklet);
}

static void dhd_tx_dispatcher_fn(dhd_pub_t *dhdp)
{
	int cpu;
	int net_tx_cpu;
	dhd_info_t *dhd = dhdp->info;

	preempt_disable();
	cpu = atomic_read(&dhd->tx_cpu);
	net_tx_cpu = atomic_read(&dhd->net_tx_cpu);

	/*
	 * Now if the NET_TX has pushed the packet in the same
	 * CPU that is chosen for Tx processing, seperate it out
	 * i.e run the TX processing tasklet in compl_cpu
	 */
	if (net_tx_cpu == cpu)
		cpu = atomic_read(&dhd->tx_compl_cpu);

	if (!cpu_online(cpu)) {
		/*
		 * Ooohh... but the Chosen CPU is not online,
		 * Do the job in the current CPU itself.
		 */
		dhd_tasklet_schedule(&dhd->tx_tasklet);
	} else {
		/*
		 * Schedule tx_dispatcher_work to on the cpu which
		 * in turn will schedule tx_tasklet.
		 */
		dhd_work_schedule_on(&dhd->tx_dispatcher_work, cpu);
	}
	preempt_enable();
}

/**
 * dhd_lb_tx_dispatch - load balance by dispatching the tx_tasklet
 * on another cpu. The tx_tasklet will take care of actually putting
 * the skbs into appropriate flow ring and ringing H2D interrupt
 *
 * @dhdp: pointer to dhd_pub object
 */
static void
dhd_lb_tx_dispatch(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	int curr_cpu;

	curr_cpu = get_cpu();
	put_cpu();

	/* Record the CPU in which the TX request from Network stack came */
	atomic_set(&dhd->net_tx_cpu, curr_cpu);

	/* Schedule the work to dispatch ... */
	dhd_tx_dispatcher_fn(dhdp);

}
#endif /* DHD_LB_TXP */

#if defined(DHD_LB_RXP)
/**
 * dhd_napi_poll - Load balance napi poll function to process received
 * packets and send up the network stack using netif_receive_skb()
 *
 * @napi: napi object in which context this poll function is invoked
 * @budget: number of packets to be processed.
 *
 * Fetch the dhd_info given the rx_napi_struct. Move all packets from the
 * rx_napi_queue into a local rx_process_queue (lock and queue move and unlock).
 * Dequeue each packet from head of rx_process_queue, fetch the ifid from the
 * packet tag and sendup.
 */
static int
dhd_napi_poll(struct napi_struct *napi, int budget)
{
	int ifid;
	const int pkt_count = 1;
	const int chan = 0;
	struct sk_buff * skb;
	unsigned long flags;
	struct dhd_info *dhd;
	int processed = 0;
	struct sk_buff_head rx_process_queue;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	dhd = container_of(napi, struct dhd_info, rx_napi_struct);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	DHD_INFO(("%s napi_queue<%d> budget<%d>\n",
		__FUNCTION__, skb_queue_len(&dhd->rx_napi_queue), budget));
		__skb_queue_head_init(&rx_process_queue);

	/* extract the entire rx_napi_queue into local rx_process_queue */
	spin_lock_irqsave(&dhd->rx_napi_queue.lock, flags);
	skb_queue_splice_tail_init(&dhd->rx_napi_queue, &rx_process_queue);
	spin_unlock_irqrestore(&dhd->rx_napi_queue.lock, flags);

	while ((skb = __skb_dequeue(&rx_process_queue)) != NULL) {
		OSL_PREFETCH(skb->data);

		ifid = DHD_PKTTAG_IFID((dhd_pkttag_fr_t *)PKTTAG(skb));

		DHD_INFO(("%s dhd_rx_frame pkt<%p> ifid<%d>\n",
			__FUNCTION__, skb, ifid));

		dhd_rx_frame(&dhd->pub, ifid, skb, pkt_count, chan);
		processed++;
	}

	DHD_LB_STATS_UPDATE_NAPI_HISTO(&dhd->pub, processed);

	DHD_INFO(("%s processed %d\n", __FUNCTION__, processed));
	napi_complete(napi);

	return budget - 1;
}

/**
 * dhd_napi_schedule - Place the napi struct into the current cpus softnet napi
 * poll list. This function may be invoked via the smp_call_function_single
 * from a remote CPU.
 *
 * This function will essentially invoke __raise_softirq_irqoff(NET_RX_SOFTIRQ)
 * after the napi_struct is added to the softnet data's poll_list
 *
 * @info: pointer to a dhd_info struct
 */
static void
dhd_napi_schedule(void *info)
{
	dhd_info_t *dhd = (dhd_info_t *)info;

	DHD_INFO(("%s rx_napi_struct<%p> on cpu<%d>\n",
		__FUNCTION__, &dhd->rx_napi_struct, atomic_read(&dhd->rx_napi_cpu)));

	/* add napi_struct to softnet data poll list and raise NET_RX_SOFTIRQ */
	if (napi_schedule_prep(&dhd->rx_napi_struct)) {
		__napi_schedule(&dhd->rx_napi_struct);
		DHD_LB_STATS_PERCPU_ARR_INCR(dhd->napi_percpu_run_cnt);
	}

	/*
	 * If the rx_napi_struct was already running, then we let it complete
	 * processing all its packets. The rx_napi_struct may only run on one
	 * core at a time, to avoid out-of-order handling.
	 */
}

/**
 * dhd_napi_schedule_on - API to schedule on a desired CPU core a NET_RX_SOFTIRQ
 * action after placing the dhd's rx_process napi object in the the remote CPU's
 * softnet data's poll_list.
 *
 * @dhd: dhd_info which has the rx_process napi object
 * @on_cpu: desired remote CPU id
 */
static INLINE int
dhd_napi_schedule_on(dhd_info_t *dhd, int on_cpu)
{
	int wait = 0; /* asynchronous IPI */
	DHD_INFO(("%s dhd<%p> napi<%p> on_cpu<%d>\n",
		__FUNCTION__, dhd, &dhd->rx_napi_struct, on_cpu));

	if (smp_call_function_single(on_cpu, dhd_napi_schedule, dhd, wait)) {
		DHD_ERROR(("%s smp_call_function_single on_cpu<%d> failed\n",
			__FUNCTION__, on_cpu));
	}

	DHD_LB_STATS_INCR(dhd->napi_sched_cnt);

	return 0;
}

/*
 * Call get_online_cpus/put_online_cpus around dhd_napi_schedule_on
 * Why should we do this?
 * The candidacy algorithm is run from the call back function
 * registered to CPU hotplug notifier. This call back happens from Worker
 * context. The dhd_napi_schedule_on is also from worker context.
 * Note that both of this can run on two different CPUs at the same time.
 * So we can possibly have a window where a given CPUn is being brought
 * down from CPUm while we try to run a function on CPUn.
 * To prevent this its better have the whole code to execute an SMP
 * function under get_online_cpus.
 * This function call ensures that hotplug mechanism does not kick-in
 * until we are done dealing with online CPUs
 * If the hotplug worker is already running, no worries because the
 * candidacy algo would then reflect the same in dhd->rx_napi_cpu.
 *
 * The below mentioned code structure is proposed in
 * https://www.kernel.org/doc/Documentation/cpu-hotplug.txt
 * for the question
 * Q: I need to ensure that a particular cpu is not removed when there is some
 *    work specific to this cpu is in progress
 *
 * According to the documentation calling get_online_cpus is NOT required, if
 * we are running from tasklet context. Since dhd_rx_napi_dispatcher_fn can
 * run from Work Queue context we have to call these functions
 */
static void dhd_rx_napi_dispatcher_fn(struct work_struct * work)
{
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	struct dhd_info *dhd =
		container_of(work, struct dhd_info, rx_napi_dispatcher_work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	int cpu;

	get_online_cpus();
	cpu = atomic_read(&dhd->rx_napi_cpu);

	if (!cpu_online(cpu))
		dhd_napi_schedule(dhd);
	else
		dhd_napi_schedule_on(dhd, cpu);

	put_online_cpus();
}

/**
 * dhd_lb_rx_napi_dispatch - load balance by dispatching the rx_napi_struct
 * to run on another CPU. The rx_napi_struct's poll function will retrieve all
 * the packets enqueued into the rx_napi_queue and sendup.
 * The producer's rx packet queue is appended to the rx_napi_queue before
 * dispatching the rx_napi_struct.
 */
void
dhd_lb_rx_napi_dispatch(dhd_pub_t *dhdp)
{
	unsigned long flags;
	dhd_info_t *dhd = dhdp->info;
	int curr_cpu;
	int on_cpu;

	if (dhd->rx_napi_netdev == NULL) {
		DHD_ERROR(("%s: dhd->rx_napi_netdev is NULL\n", __FUNCTION__));
		return;
	}

	DHD_INFO(("%s append napi_queue<%d> pend_queue<%d>\n", __FUNCTION__,
		skb_queue_len(&dhd->rx_napi_queue), skb_queue_len(&dhd->rx_pend_queue)));

	/* append the producer's queue of packets to the napi's rx process queue */
	spin_lock_irqsave(&dhd->rx_napi_queue.lock, flags);
	skb_queue_splice_tail_init(&dhd->rx_pend_queue, &dhd->rx_napi_queue);
	spin_unlock_irqrestore(&dhd->rx_napi_queue.lock, flags);

	/*
	 * If the destination CPU is NOT online or is same as current CPU
	 * no need to schedule the work
	 */
	curr_cpu = get_cpu();
	put_cpu();

	on_cpu = atomic_read(&dhd->rx_napi_cpu);
	if ((on_cpu == curr_cpu) || (!cpu_online(on_cpu))) {
		dhd_napi_schedule(dhd);
	} else {
		schedule_work(&dhd->rx_napi_dispatcher_work);
	}
}

/**
 * dhd_lb_rx_pkt_enqueue - Enqueue the packet into the producer's queue
 */
void
dhd_lb_rx_pkt_enqueue(dhd_pub_t *dhdp, void *pkt, int ifidx)
{
	dhd_info_t *dhd = dhdp->info;

	DHD_INFO(("%s enqueue pkt<%p> ifidx<%d> pend_queue<%d>\n", __FUNCTION__,
		pkt, ifidx, skb_queue_len(&dhd->rx_pend_queue)));
	DHD_PKTTAG_SET_IFID((dhd_pkttag_fr_t *)PKTTAG(pkt), ifidx);
	__skb_queue_tail(&dhd->rx_pend_queue, pkt);
}
#endif /* DHD_LB_RXP */

#endif /* DHD_LB */


/** Returns dhd iflist index corresponding the the bssidx provided by apps */
int dhd_bssidx2idx(dhd_pub_t *dhdp, uint32 bssidx)
{
	dhd_if_t *ifp;
	dhd_info_t *dhd = dhdp->info;
	int i;

	ASSERT(bssidx < DHD_MAX_IFS);
	ASSERT(dhdp);

	for (i = 0; i < DHD_MAX_IFS; i++) {
		ifp = dhd->iflist[i];
		if (ifp && (ifp->bssidx == bssidx)) {
			DHD_TRACE(("Index manipulated for %s from %d to %d\n",
				ifp->name, bssidx, i));
			break;
		}
	}
	return i;
}

static inline int dhd_rxf_enqueue(dhd_pub_t *dhdp, void* skb)
{
	uint32 store_idx;
	uint32 sent_idx;

	if (!skb) {
		DHD_ERROR(("dhd_rxf_enqueue: NULL skb!!!\n"));
		return BCME_ERROR;
	}

	dhd_os_rxflock(dhdp);
	store_idx = dhdp->store_idx;
	sent_idx = dhdp->sent_idx;
	if (dhdp->skbbuf[store_idx] != NULL) {
		/* Make sure the previous packets are processed */
		dhd_os_rxfunlock(dhdp);
#ifdef RXF_DEQUEUE_ON_BUSY
		DHD_TRACE(("dhd_rxf_enqueue: pktbuf not consumed %p, store idx %d sent idx %d\n",
			skb, store_idx, sent_idx));
		return BCME_BUSY;
#else /* RXF_DEQUEUE_ON_BUSY */
		DHD_ERROR(("dhd_rxf_enqueue: pktbuf not consumed %p, store idx %d sent idx %d\n",
			skb, store_idx, sent_idx));
		/* removed msleep here, should use wait_event_timeout if we
		 * want to give rx frame thread a chance to run
		 */
#if defined(WAIT_DEQUEUE)
		OSL_SLEEP(1);
#endif
		return BCME_ERROR;
#endif /* RXF_DEQUEUE_ON_BUSY */
	}
	DHD_TRACE(("dhd_rxf_enqueue: Store SKB %p. idx %d -> %d\n",
		skb, store_idx, (store_idx + 1) & (MAXSKBPEND - 1)));
	dhdp->skbbuf[store_idx] = skb;
	dhdp->store_idx = (store_idx + 1) & (MAXSKBPEND - 1);
	dhd_os_rxfunlock(dhdp);

	return BCME_OK;
}

static inline void* dhd_rxf_dequeue(dhd_pub_t *dhdp)
{
	uint32 store_idx;
	uint32 sent_idx;
	void *skb;

	dhd_os_rxflock(dhdp);

	store_idx = dhdp->store_idx;
	sent_idx = dhdp->sent_idx;
	skb = dhdp->skbbuf[sent_idx];

	if (skb == NULL) {
		dhd_os_rxfunlock(dhdp);
		DHD_ERROR(("dhd_rxf_dequeue: Dequeued packet is NULL, store idx %d sent idx %d\n",
			store_idx, sent_idx));
		return NULL;
	}

	dhdp->skbbuf[sent_idx] = NULL;
	dhdp->sent_idx = (sent_idx + 1) & (MAXSKBPEND - 1);

	DHD_TRACE(("dhd_rxf_dequeue: netif_rx_ni(%p), sent idx %d\n",
		skb, sent_idx));

	dhd_os_rxfunlock(dhdp);

	return skb;
}

int dhd_process_cid_mac(dhd_pub_t *dhdp, bool prepost)
{
	if (prepost) { /* pre process */
		dhd_read_cis(dhdp);
		dhd_check_module_cid(dhdp);
		dhd_check_module_mac(dhdp);
		dhd_set_macaddr_from_file(dhdp);
	} else { /* post process */
		dhd_write_macaddr(&dhdp->mac);
		dhd_clear_cis(dhdp);
	}

	return 0;
}

// terence 20160615: fix building error if ARP_OFFLOAD_SUPPORT removed
#if defined(PKT_FILTER_SUPPORT)
#if defined(ARP_OFFLOAD_SUPPORT) && !defined(GAN_LITE_NAT_KEEPALIVE_FILTER)
static bool
_turn_on_arp_filter(dhd_pub_t *dhd, int op_mode_param)
{
	bool _apply = FALSE;
	/* In case of IBSS mode, apply arp pkt filter */
	if (op_mode_param & DHD_FLAG_IBSS_MODE) {
		_apply = TRUE;
		goto exit;
	}
	/* In case of P2P GO or GC, apply pkt filter to pass arp pkt to host */
	if (op_mode_param & (DHD_FLAG_P2P_GC_MODE | DHD_FLAG_P2P_GO_MODE)) {
		_apply = TRUE;
		goto exit;
	}

exit:
	return _apply;
}
#endif /* !GAN_LITE_NAT_KEEPALIVE_FILTER */

void
dhd_set_packet_filter(dhd_pub_t *dhd)
{
	int i;

	DHD_TRACE(("%s: enter\n", __FUNCTION__));
	if (dhd_pkt_filter_enable) {
		for (i = 0; i < dhd->pktfilter_count; i++) {
			dhd_pktfilter_offload_set(dhd, dhd->pktfilter[i]);
		}
	}
}

void
dhd_enable_packet_filter(int value, dhd_pub_t *dhd)
{
	int i;

	DHD_ERROR(("%s: enter, value = %d\n", __FUNCTION__, value));
	if ((dhd->op_mode & DHD_FLAG_HOSTAP_MODE) && value &&
			!dhd_conf_get_insuspend(dhd, AP_FILTER_IN_SUSPEND)) {
		DHD_ERROR(("%s: DHD_FLAG_HOSTAP_MODE\n", __FUNCTION__));
		return;
	}
	/* 1 - Enable packet filter, only allow unicast packet to send up */
	/* 0 - Disable packet filter */
	if (dhd_pkt_filter_enable && (!value ||
	    (dhd_support_sta_mode(dhd) && !dhd->dhcp_in_progress) ||
	    dhd_conf_get_insuspend(dhd, AP_FILTER_IN_SUSPEND)))
	{
		for (i = 0; i < dhd->pktfilter_count; i++) {
// terence 20160615: fix building error if ARP_OFFLOAD_SUPPORT removed
#if defined(ARP_OFFLOAD_SUPPORT) && !defined(GAN_LITE_NAT_KEEPALIVE_FILTER)
			if (value && (i == DHD_ARP_FILTER_NUM) &&
				!_turn_on_arp_filter(dhd, dhd->op_mode)) {
				DHD_TRACE(("Do not turn on ARP white list pkt filter:"
					"val %d, cnt %d, op_mode 0x%x\n",
					value, i, dhd->op_mode));
				continue;
			}
#endif /* !GAN_LITE_NAT_KEEPALIVE_FILTER */
			dhd_pktfilter_offload_enable(dhd, dhd->pktfilter[i],
				value, dhd_master_mode);
		}
	}
}

int
dhd_packet_filter_add_remove(dhd_pub_t *dhdp, int add_remove, int num)
{
	char *filterp = NULL;
	int filter_id = 0;

	switch (num) {
		case DHD_BROADCAST_FILTER_NUM:
			filterp = "101 0 0 0 0xFFFFFFFFFFFF 0xFFFFFFFFFFFF";
			filter_id = 101;
			break;
		case DHD_MULTICAST4_FILTER_NUM:
			filter_id = 102;
			if (FW_SUPPORTED((dhdp), pf6)) {
				if (dhdp->pktfilter[num] != NULL) {
					dhd_pktfilter_offload_delete(dhdp, filter_id);
					dhdp->pktfilter[num] = NULL;
				}
				if (!add_remove) {
					filterp = DISCARD_IPV4_MCAST;
					add_remove = 1;
					break;
				}
			}
			filterp = "102 0 0 0 0xFFFFFF 0x01005E";
			break;
		case DHD_MULTICAST6_FILTER_NUM:
			filter_id = 103;
			if (FW_SUPPORTED((dhdp), pf6)) {
				if (dhdp->pktfilter[num] != NULL) {
					dhd_pktfilter_offload_delete(dhdp, filter_id);
					dhdp->pktfilter[num] = NULL;
				}
				if (!add_remove) {
					filterp = DISCARD_IPV6_MCAST;
					add_remove = 1;
					break;
				}
			}
			filterp = "103 0 0 0 0xFFFF 0x3333";
			break;
		case DHD_MDNS_FILTER_NUM:
			filterp = "104 0 0 0 0xFFFFFFFFFFFF 0x01005E0000FB";
			filter_id = 104;
			break;
		case DHD_ARP_FILTER_NUM:
			filterp = "105 0 0 12 0xFFFF 0x0806";
			filter_id = 105;
			break;
		case DHD_BROADCAST_ARP_FILTER_NUM:
			filterp = "106 0 0 0 0xFFFFFFFFFFFF0000000000000806"
				" 0xFFFFFFFFFFFF0000000000000806";
			filter_id = 106;
			break;
		default:
			return -EINVAL;
	}

	/* Add filter */
	if (add_remove) {
		dhdp->pktfilter[num] = filterp;
		dhd_pktfilter_offload_set(dhdp, dhdp->pktfilter[num]);
	} else { /* Delete filter */
		if (dhdp->pktfilter[num]) {
			dhd_pktfilter_offload_delete(dhdp, filter_id);
			dhdp->pktfilter[num] = NULL;
		}
	}

	return 0;
}
#endif /* PKT_FILTER_SUPPORT */

static int dhd_set_suspend(int value, dhd_pub_t *dhd)
{
#ifndef SUPPORT_PM2_ONLY
	int power_mode = PM_MAX;
#endif /* SUPPORT_PM2_ONLY */
#ifdef SUPPORT_SENSORHUB
	shub_control_t shub_ctl;
#endif /* SUPPORT_SENSORHUB */
	/* wl_pkt_filter_enable_t	enable_parm; */
	int bcn_li_dtim = 0; /* Default bcn_li_dtim in resume mode is 0 */
	int ret = 0;
#ifdef DHD_USE_EARLYSUSPEND
#ifdef CUSTOM_BCN_TIMEOUT_IN_SUSPEND
	int bcn_timeout = 0;
#endif /* CUSTOM_BCN_TIMEOUT_IN_SUSPEND */
#ifdef CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND
	int roam_time_thresh = 0;   /* (ms) */
#endif /* CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND */
#ifndef ENABLE_FW_ROAM_SUSPEND
	uint roamvar = 1;
#endif /* ENABLE_FW_ROAM_SUSPEND */
#ifdef ENABLE_BCN_LI_BCN_WAKEUP
	int bcn_li_bcn;
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
	uint nd_ra_filter = 0;
#endif /* DHD_USE_EARLYSUSPEND */
#ifdef PASS_ALL_MCAST_PKTS
	struct dhd_info *dhdinfo;
	uint32 allmulti;
	uint i;
#endif /* PASS_ALL_MCAST_PKTS */
#ifdef ENABLE_IPMCAST_FILTER
	int ipmcast_l2filter;
#endif /* ENABLE_IPMCAST_FILTER */
#ifdef DYNAMIC_SWOOB_DURATION
#ifndef CUSTOM_INTR_WIDTH
#define CUSTOM_INTR_WIDTH 100
	int intr_width = 0;
#endif /* CUSTOM_INTR_WIDTH */
#endif /* DYNAMIC_SWOOB_DURATION */

#if defined(BCMPCIE)
	int lpas = 0;
	int dtim_period = 0;
	int bcn_interval = 0;
	int bcn_to_dly = 0;
#ifndef CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND
	int bcn_timeout = CUSTOM_BCN_TIMEOUT_SETTING;
#else
	bcn_timeout = CUSTOM_BCN_TIMEOUT_SETTING;
#endif /* CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND */
#endif /* OEM_ANDROID && BCMPCIE */

	if (!dhd)
		return -ENODEV;

#ifdef PASS_ALL_MCAST_PKTS
	dhdinfo = dhd->info;
#endif /* PASS_ALL_MCAST_PKTS */

	DHD_TRACE(("%s: enter, value = %d in_suspend=%d\n",
		__FUNCTION__, value, dhd->in_suspend));

	dhd_suspend_lock(dhd);

#ifdef CUSTOM_SET_CPUCORE
	DHD_TRACE(("%s set cpucore(suspend%d)\n", __FUNCTION__, value));
	/* set specific cpucore */
	dhd_set_cpucore(dhd, TRUE);
#endif /* CUSTOM_SET_CPUCORE */
	if (dhd->up) {
		if (value && dhd->in_suspend) {
#ifdef PKT_FILTER_SUPPORT
			dhd->early_suspended = 1;
#endif
			/* Kernel suspended */
			DHD_ERROR(("%s: force extra suspend setting\n", __FUNCTION__));

#ifndef SUPPORT_PM2_ONLY
			dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&power_mode,
				sizeof(power_mode), TRUE, 0);
#endif /* SUPPORT_PM2_ONLY */

#ifdef PKT_FILTER_SUPPORT
			/* Enable packet filter,
			 * only allow unicast packet to send up
			 */
			dhd_enable_packet_filter(1, dhd);
#ifdef APF
			dhd_dev_apf_enable_filter(dhd_linux_get_primary_netdev(dhd));
#endif /* APF */
#endif /* PKT_FILTER_SUPPORT */

#ifdef SUPPORT_SENSORHUB
			shub_ctl.enable = 1;
			shub_ctl.cmd = 0x000;
			shub_ctl.op_mode = 1;
			shub_ctl.interval = 0;
			if (dhd->info->shub_enable == 1) {
				ret = dhd_iovar(dhd, 0, "shub_msreq",
					(char *)&shub_ctl, sizeof(shub_ctl), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s SensorHub MS start: failed %d\n",
						__FUNCTION__, ret));
				}
			}
#endif /* SUPPORT_SENSORHUB */


#ifdef PASS_ALL_MCAST_PKTS
			allmulti = 0;
			for (i = 0; i < DHD_MAX_IFS; i++) {
				if (dhdinfo->iflist[i] && dhdinfo->iflist[i]->net)
					dhd_iovar(dhd, i, "allmulti", (char *)&allmulti,
							sizeof(allmulti), NULL, 0, TRUE);

			}
#endif /* PASS_ALL_MCAST_PKTS */

			/* If DTIM skip is set up as default, force it to wake
			 * each third DTIM for better power savings.  Note that
			 * one side effect is a chance to miss BC/MC packet.
			 */
#ifdef WLTDLS
			/* Do not set bcn_li_ditm on WFD mode */
			if (dhd->tdls_mode) {
				bcn_li_dtim = 0;
			} else
#endif /* WLTDLS */
#if defined(BCMPCIE)
			bcn_li_dtim = dhd_get_suspend_bcn_li_dtim(dhd, &dtim_period,
				&bcn_interval);
			dhd_iovar(dhd, 0, "bcn_li_dtim", (char *)&bcn_li_dtim,
					sizeof(bcn_li_dtim), NULL, 0, TRUE);

			if ((bcn_li_dtim * dtim_period * bcn_interval) >=
				MIN_DTIM_FOR_ROAM_THRES_EXTEND) {
				/*
				 * Increase max roaming threshold from 2 secs to 8 secs
				 * the real roam threshold is MIN(max_roam_threshold,
				 * bcn_timeout/2)
				 */
				lpas = 1;
				dhd_iovar(dhd, 0, "lpas", (char *)&lpas, sizeof(lpas), NULL,
						0, TRUE);

				bcn_to_dly = 1;
				/*
				 * if bcn_to_dly is 1, the real roam threshold is
				 * MIN(max_roam_threshold, bcn_timeout -1);
				 * notify link down event after roaming procedure complete
				 * if we hit bcn_timeout while we are in roaming progress.
				 */
				dhd_iovar(dhd, 0, "bcn_to_dly", (char *)&bcn_to_dly,
						sizeof(bcn_to_dly), NULL, 0, TRUE);
				/* Increase beacon timeout to 6 secs or use bigger one */
				bcn_timeout = max(bcn_timeout, BCN_TIMEOUT_IN_SUSPEND);
				dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
						sizeof(bcn_timeout), NULL, 0, TRUE);
			}
#else
			bcn_li_dtim = dhd_get_suspend_bcn_li_dtim(dhd);
			if (dhd_iovar(dhd, 0, "bcn_li_dtim", (char *)&bcn_li_dtim,
					sizeof(bcn_li_dtim), NULL, 0, TRUE) < 0)
				DHD_ERROR(("%s: set dtim failed\n", __FUNCTION__));
#endif /* OEM_ANDROID && BCMPCIE */

#ifdef DHD_USE_EARLYSUSPEND
#ifdef CUSTOM_BCN_TIMEOUT_IN_SUSPEND
			bcn_timeout = CUSTOM_BCN_TIMEOUT_IN_SUSPEND;
			dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
					sizeof(bcn_timeout), NULL, 0, TRUE);
#endif /* CUSTOM_BCN_TIMEOUT_IN_SUSPEND */
#ifdef CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND
			roam_time_thresh = CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND;
			dhd_iovar(dhd, 0, "roam_time_thresh", (char *)&roam_time_thresh,
					sizeof(roam_time_thresh), NULL, 0, TRUE);
#endif /* CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND */
#ifndef ENABLE_FW_ROAM_SUSPEND
			/* Disable firmware roaming during suspend */
			dhd_iovar(dhd, 0, "roam_off", (char *)&roamvar, sizeof(roamvar),
					NULL, 0, TRUE);
#endif /* ENABLE_FW_ROAM_SUSPEND */
#ifdef ENABLE_BCN_LI_BCN_WAKEUP
			bcn_li_bcn = 0;
			dhd_iovar(dhd, 0, "bcn_li_bcn", (char *)&bcn_li_bcn,
					sizeof(bcn_li_bcn), NULL, 0, TRUE);
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
#ifdef NDO_CONFIG_SUPPORT
			if (dhd->ndo_enable) {
				if (!dhd->ndo_host_ip_overflow) {
					/* enable ND offload on suspend */
					ret = dhd_ndo_enable(dhd, 1);
					if (ret < 0) {
						DHD_ERROR(("%s: failed to enable NDO\n",
							__FUNCTION__));
					}
				} else {
					DHD_INFO(("%s: NDO disabled on suspend due to"
							"HW capacity\n", __FUNCTION__));
				}
			}
#endif /* NDO_CONFIG_SUPPORT */
#ifndef APF
			if (FW_SUPPORTED(dhd, ndoe))
#else
			if (FW_SUPPORTED(dhd, ndoe) && !FW_SUPPORTED(dhd, apf))
#endif /* APF */
			{
				/* enable IPv6 RA filter in  firmware during suspend */
				nd_ra_filter = 1;
				ret = dhd_iovar(dhd, 0, "nd_ra_filter_enable",
						(char *)&nd_ra_filter, sizeof(nd_ra_filter),
						NULL, 0, TRUE);
				if (ret < 0)
					DHD_ERROR(("failed to set nd_ra_filter (%d)\n",
						ret));
			}
			dhd_os_suppress_logging(dhd, TRUE);
#ifdef ENABLE_IPMCAST_FILTER
			ipmcast_l2filter = 1;
			ret = dhd_iovar(dhd, 0, "ipmcast_l2filter",
					(char *)&ipmcast_l2filter, sizeof(ipmcast_l2filter),
					NULL, 0, TRUE);
#endif /* ENABLE_IPMCAST_FILTER */
#ifdef DYNAMIC_SWOOB_DURATION
			intr_width = CUSTOM_INTR_WIDTH;
			ret = dhd_iovar(dhd, 0, "bus:intr_width", (char *)&intr_width,
					sizeof(intr_width), NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("failed to set intr_width (%d)\n", ret));
			}
#endif /* DYNAMIC_SWOOB_DURATION */
#endif /* DHD_USE_EARLYSUSPEND */
			dhd_conf_set_suspend_resume(dhd, value, EARLY_SUSPEND);
		} else {
			dhd_conf_set_suspend_resume(dhd, value, EARLY_SUSPEND);
#ifdef PKT_FILTER_SUPPORT
			dhd->early_suspended = 0;
#endif
			/* Kernel resumed  */
			DHD_ERROR(("%s: Remove extra suspend setting \n", __FUNCTION__));

#ifdef SUPPORT_SENSORHUB
			shub_ctl.enable = 1;
			shub_ctl.cmd = 0x000;
			shub_ctl.op_mode = 0;
			shub_ctl.interval = 0;
			if (dhd->info->shub_enable == 1) {
				ret = dhd_iovar(dhd, 0, "shub_msreq",
						(char *)&shub_ctl, sizeof(shub_ctl),
						NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s SensorHub MS stop: failed %d\n",
						__FUNCTION__, ret));
				}
			}
#endif /* SUPPORT_SENSORHUB */

#ifdef DYNAMIC_SWOOB_DURATION
			intr_width = 0;
			ret = dhd_iovar(dhd, 0, "bus:intr_width", (char *)&intr_width,
					sizeof(intr_width), NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("failed to set intr_width (%d)\n", ret));
			}
#endif /* DYNAMIC_SWOOB_DURATION */
#ifndef SUPPORT_PM2_ONLY
			power_mode = PM_FAST;
			dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&power_mode,
				sizeof(power_mode), TRUE, 0);
#endif /* SUPPORT_PM2_ONLY */
#ifdef PKT_FILTER_SUPPORT
			/* disable pkt filter */
			dhd_enable_packet_filter(0, dhd);
#ifdef APF
			dhd_dev_apf_disable_filter(dhd_linux_get_primary_netdev(dhd));
#endif /* APF */
#endif /* PKT_FILTER_SUPPORT */
#ifdef PASS_ALL_MCAST_PKTS
			allmulti = 1;
			for (i = 0; i < DHD_MAX_IFS; i++) {
				if (dhdinfo->iflist[i] && dhdinfo->iflist[i]->net)
					dhd_iovar(dhd, i, "allmulti", (char *)&allmulti,
							sizeof(allmulti), NULL, 0, TRUE);
			}
#endif /* PASS_ALL_MCAST_PKTS */
#if defined(BCMPCIE)
			/* restore pre-suspend setting */
			ret = dhd_iovar(dhd, 0, "bcn_li_dtim", (char *)&bcn_li_dtim,
					sizeof(bcn_li_dtim), NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s:bcn_li_ditm fail:%d\n", __FUNCTION__, ret));
			}

			dhd_iovar(dhd, 0, "lpas", (char *)&lpas, sizeof(lpas), NULL, 0,
					TRUE);

			dhd_iovar(dhd, 0, "bcn_to_dly", (char *)&bcn_to_dly,
					sizeof(bcn_to_dly), NULL, 0, TRUE);

			dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
					sizeof(bcn_timeout), NULL, 0, TRUE);
#else
			/* restore pre-suspend setting for dtim_skip */
			ret = dhd_iovar(dhd, 0, "bcn_li_dtim", (char *)&bcn_li_dtim,
					sizeof(bcn_li_dtim), NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s:bcn_li_ditm fail:%d\n", __FUNCTION__, ret));
			}
#endif /* OEM_ANDROID && BCMPCIE */
#ifdef DHD_USE_EARLYSUSPEND
#ifdef CUSTOM_BCN_TIMEOUT_IN_SUSPEND
			bcn_timeout = CUSTOM_BCN_TIMEOUT;
			dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
					sizeof(bcn_timeout), NULL, 0, TRUE);
#endif /* CUSTOM_BCN_TIMEOUT_IN_SUSPEND */
#ifdef CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND
			roam_time_thresh = 2000;
			dhd_iovar(dhd, 0, "roam_time_thresh", (char *)&roam_time_thresh,
					sizeof(roam_time_thresh), NULL, 0, TRUE);

#endif /* CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND */
#ifndef ENABLE_FW_ROAM_SUSPEND
			roamvar = dhd_roam_disable;
			dhd_iovar(dhd, 0, "roam_off", (char *)&roamvar, sizeof(roamvar),
					NULL, 0, TRUE);
#endif /* ENABLE_FW_ROAM_SUSPEND */
#ifdef ENABLE_BCN_LI_BCN_WAKEUP
			bcn_li_bcn = 1;
			dhd_iovar(dhd, 0, "bcn_li_bcn", (char *)&bcn_li_bcn,
					sizeof(bcn_li_bcn), NULL, 0, TRUE);
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
#ifdef NDO_CONFIG_SUPPORT
			if (dhd->ndo_enable) {
				/* Disable ND offload on resume */
				ret = dhd_ndo_enable(dhd, 0);
				if (ret < 0) {
					DHD_ERROR(("%s: failed to disable NDO\n",
						__FUNCTION__));
				}
			}
#endif /* NDO_CONFIG_SUPPORT */
#ifndef APF
			if (FW_SUPPORTED(dhd, ndoe))
#else
			if (FW_SUPPORTED(dhd, ndoe) && !FW_SUPPORTED(dhd, apf))
#endif /* APF */
			{
				/* disable IPv6 RA filter in  firmware during suspend */
				nd_ra_filter = 0;
				ret = dhd_iovar(dhd, 0, "nd_ra_filter_enable",
						(char *)&nd_ra_filter, sizeof(nd_ra_filter),
						NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("failed to set nd_ra_filter (%d)\n",
						ret));
				}
			}
			dhd_os_suppress_logging(dhd, FALSE);
#ifdef ENABLE_IPMCAST_FILTER
			ipmcast_l2filter = 0;
			ret = dhd_iovar(dhd, 0, "ipmcast_l2filter",
					(char *)&ipmcast_l2filter, sizeof(ipmcast_l2filter),
					NULL, 0, TRUE);
#endif /* ENABLE_IPMCAST_FILTER */
#endif /* DHD_USE_EARLYSUSPEND */
		}
	}
	dhd_suspend_unlock(dhd);

	return 0;
}

static int dhd_suspend_resume_helper(struct dhd_info *dhd, int val, int force)
{
	dhd_pub_t *dhdp = &dhd->pub;
	int ret = 0;

	DHD_OS_WAKE_LOCK(dhdp);
	DHD_PERIM_LOCK(dhdp);

	/* Set flag when early suspend was called */
	dhdp->in_suspend = val;
	if ((force || !dhdp->suspend_disable_flag) &&
		(dhd_support_sta_mode(dhdp) || dhd_conf_get_insuspend(dhdp, ALL_IN_SUSPEND)))
	{
		ret = dhd_set_suspend(val, dhdp);
	}

	DHD_PERIM_UNLOCK(dhdp);
	DHD_OS_WAKE_UNLOCK(dhdp);
	return ret;
}

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
static void dhd_early_suspend(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);
	DHD_TRACE_HW4(("%s: enter\n", __FUNCTION__));

	if (dhd)
		dhd_suspend_resume_helper(dhd, 1, 0);
}

static void dhd_late_resume(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);
	DHD_TRACE_HW4(("%s: enter\n", __FUNCTION__));

	if (dhd)
		dhd_suspend_resume_helper(dhd, 0, 0);
}
#endif /* CONFIG_HAS_EARLYSUSPEND && DHD_USE_EARLYSUSPEND */

/*
 * Generalized timeout mechanism.  Uses spin sleep with exponential back-off until
 * the sleep time reaches one jiffy, then switches over to task delay.  Usage:
 *
 *      dhd_timeout_start(&tmo, usec);
 *      while (!dhd_timeout_expired(&tmo))
 *              if (poll_something())
 *                      break;
 *      if (dhd_timeout_expired(&tmo))
 *              fatal();
 */

void
dhd_timeout_start(dhd_timeout_t *tmo, uint usec)
{
	tmo->limit = usec;
	tmo->increment = 0;
	tmo->elapsed = 0;
	tmo->tick = jiffies_to_usecs(1);
}

int
dhd_timeout_expired(dhd_timeout_t *tmo)
{
	/* Does nothing the first call */
	if (tmo->increment == 0) {
		tmo->increment = 1;
		return 0;
	}

	if (tmo->elapsed >= tmo->limit)
		return 1;

	/* Add the delay that's about to take place */
	tmo->elapsed += tmo->increment;

	if ((!CAN_SLEEP()) || tmo->increment < tmo->tick) {
		OSL_DELAY(tmo->increment);
		tmo->increment *= 2;
		if (tmo->increment > tmo->tick)
			tmo->increment = tmo->tick;
	} else {
		wait_queue_head_t delay_wait;
		DECLARE_WAITQUEUE(wait, current);
		init_waitqueue_head(&delay_wait);
		add_wait_queue(&delay_wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		(void)schedule_timeout(1);
		remove_wait_queue(&delay_wait, &wait);
		set_current_state(TASK_RUNNING);
	}

	return 0;
}

int
dhd_net2idx(dhd_info_t *dhd, struct net_device *net)
{
	int i = 0;

	if (!dhd) {
		DHD_ERROR(("%s : DHD_BAD_IF return\n", __FUNCTION__));
		return DHD_BAD_IF;
	}

	while (i < DHD_MAX_IFS) {
		if (dhd->iflist[i] && dhd->iflist[i]->net && (dhd->iflist[i]->net == net))
			return i;
		i++;
	}

	return DHD_BAD_IF;
}

struct net_device * dhd_idx2net(void *pub, int ifidx)
{
	struct dhd_pub *dhd_pub = (struct dhd_pub *)pub;
	struct dhd_info *dhd_info;

	if (!dhd_pub || ifidx < 0 || ifidx >= DHD_MAX_IFS)
		return NULL;
	dhd_info = dhd_pub->info;
	if (dhd_info && dhd_info->iflist[ifidx])
		return dhd_info->iflist[ifidx]->net;
	return NULL;
}

int
dhd_ifname2idx(dhd_info_t *dhd, char *name)
{
	int i = DHD_MAX_IFS;

	ASSERT(dhd);

	if (name == NULL || *name == '\0')
		return 0;

	while (--i > 0)
		if (dhd->iflist[i] && !strncmp(dhd->iflist[i]->dngl_name, name, IFNAMSIZ))
				break;

	DHD_TRACE(("%s: return idx %d for \"%s\"\n", __FUNCTION__, i, name));

	return i;	/* default - the primary interface */
}

char *
dhd_ifname(dhd_pub_t *dhdp, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

	ASSERT(dhd);

	if (ifidx < 0 || ifidx >= DHD_MAX_IFS) {
		DHD_ERROR(("%s: ifidx %d out of range\n", __FUNCTION__, ifidx));
		return "<if_bad>";
	}

	if (dhd->iflist[ifidx] == NULL) {
		DHD_ERROR(("%s: null i/f %d\n", __FUNCTION__, ifidx));
		return "<if_null>";
	}

	if (dhd->iflist[ifidx]->net)
		return dhd->iflist[ifidx]->net->name;

	return "<if_none>";
}

uint8 *
dhd_bssidx2bssid(dhd_pub_t *dhdp, int idx)
{
	int i;
	dhd_info_t *dhd = (dhd_info_t *)dhdp;

	ASSERT(dhd);
	for (i = 0; i < DHD_MAX_IFS; i++)
	if (dhd->iflist[i] && dhd->iflist[i]->bssidx == idx)
		return dhd->iflist[i]->mac_addr;

	return NULL;
}


static void
_dhd_set_multicast_list(dhd_info_t *dhd, int ifidx)
{
	struct net_device *dev;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	struct netdev_hw_addr *ha;
#else
	struct dev_mc_list *mclist;
#endif
	uint32 allmulti, cnt;

	wl_ioctl_t ioc;
	char *buf, *bufp;
	uint buflen;
	int ret;

	if (!dhd->iflist[ifidx]) {
		DHD_ERROR(("%s : dhd->iflist[%d] was NULL\n", __FUNCTION__, ifidx));
		return;
	}
	dev = dhd->iflist[ifidx]->net;
	if (!dev)
		return;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
	netif_addr_lock_bh(dev);
#endif /* LINUX >= 2.6.27 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	cnt = netdev_mc_count(dev);
#else
	cnt = dev->mc_count;
#endif /* LINUX >= 2.6.35 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
	netif_addr_unlock_bh(dev);
#endif /* LINUX >= 2.6.27 */

	/* Determine initial value of allmulti flag */
	allmulti = (dev->flags & IFF_ALLMULTI) ? TRUE : FALSE;

#ifdef PASS_ALL_MCAST_PKTS
#ifdef PKT_FILTER_SUPPORT
	if (!dhd->pub.early_suspended)
#endif /* PKT_FILTER_SUPPORT */
		allmulti = TRUE;
#endif /* PASS_ALL_MCAST_PKTS */

	/* Send down the multicast list first. */


	buflen = sizeof("mcast_list") + sizeof(cnt) + (cnt * ETHER_ADDR_LEN);
	if (!(bufp = buf = MALLOC(dhd->pub.osh, buflen))) {
		DHD_ERROR(("%s: out of memory for mcast_list, cnt %d\n",
		           dhd_ifname(&dhd->pub, ifidx), cnt));
		return;
	}

	strncpy(bufp, "mcast_list", buflen - 1);
	bufp[buflen - 1] = '\0';
	bufp += strlen("mcast_list") + 1;

	cnt = htol32(cnt);
	memcpy(bufp, &cnt, sizeof(cnt));
	bufp += sizeof(cnt);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
	netif_addr_lock_bh(dev);
#endif /* LINUX >= 2.6.27 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	netdev_for_each_mc_addr(ha, dev) {
		if (!cnt)
			break;
		memcpy(bufp, ha->addr, ETHER_ADDR_LEN);
		bufp += ETHER_ADDR_LEN;
		cnt--;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#else /* LINUX < 2.6.35 */
	for (mclist = dev->mc_list; (mclist && (cnt > 0));
		cnt--, mclist = mclist->next) {
		memcpy(bufp, (void *)mclist->dmi_addr, ETHER_ADDR_LEN);
		bufp += ETHER_ADDR_LEN;
	}
#endif /* LINUX >= 2.6.35 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
	netif_addr_unlock_bh(dev);
#endif /* LINUX >= 2.6.27 */

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set mcast_list failed, cnt %d\n",
			dhd_ifname(&dhd->pub, ifidx), cnt));
		allmulti = cnt ? TRUE : allmulti;
	}

	MFREE(dhd->pub.osh, buf, buflen);

	/* Now send the allmulti setting.  This is based on the setting in the
	 * net_device flags, but might be modified above to be turned on if we
	 * were trying to set some addresses and dongle rejected it...
	 */

	allmulti = htol32(allmulti);
	ret = dhd_iovar(&dhd->pub, ifidx, "allmulti", (char *)&allmulti,
			sizeof(allmulti), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: set allmulti %d failed\n",
		           dhd_ifname(&dhd->pub, ifidx), ltoh32(allmulti)));
	}

	/* Finally, pick up the PROMISC flag as well, like the NIC driver does */

	allmulti = (dev->flags & IFF_PROMISC) ? TRUE : FALSE;

	allmulti = htol32(allmulti);

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_PROMISC;
	ioc.buf = &allmulti;
	ioc.len = sizeof(allmulti);
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set promisc %d failed\n",
		           dhd_ifname(&dhd->pub, ifidx), ltoh32(allmulti)));
	}
}

int
_dhd_set_mac_address(dhd_info_t *dhd, int ifidx, uint8 *addr)
{
	int ret;

	ret = dhd_iovar(&dhd->pub, ifidx, "cur_etheraddr", (char *)addr,
			ETHER_ADDR_LEN, NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: set cur_etheraddr failed\n", dhd_ifname(&dhd->pub, ifidx)));
	} else {
		memcpy(dhd->iflist[ifidx]->net->dev_addr, addr, ETHER_ADDR_LEN);
		if (ifidx == 0)
			memcpy(dhd->pub.mac.octet, addr, ETHER_ADDR_LEN);
	}

	return ret;
}

#ifdef DHD_WMF
void dhd_update_psta_interface_for_sta(dhd_pub_t* dhdp, char* ifname, void* ea,
		void* event_data)
{
	struct wl_psta_primary_intf_event *psta_prim_event =
			(struct wl_psta_primary_intf_event*)event_data;
	dhd_sta_t *psta_interface =  NULL;
	dhd_sta_t *sta = NULL;
	uint8 ifindex;
	ASSERT(ifname);
	ASSERT(psta_prim_event);
	ASSERT(ea);

	ifindex = (uint8)dhd_ifname2idx(dhdp->info, ifname);
	sta = dhd_find_sta(dhdp, ifindex, ea);
	if (sta != NULL) {
		psta_interface = dhd_find_sta(dhdp, ifindex,
				(void *)(psta_prim_event->prim_ea.octet));
		if (psta_interface != NULL) {
			sta->psta_prim = psta_interface;
		}
	}
}

/* Get wmf_psta_disable configuration configuration */
int dhd_get_wmf_psta_disable(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;
	ASSERT(idx < DHD_MAX_IFS);
	ifp = dhd->iflist[idx];
	return ifp->wmf_psta_disable;
}

/* Set wmf_psta_disable configuration configuration */
int dhd_set_wmf_psta_disable(dhd_pub_t *dhdp, uint32 idx, int val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;
	ASSERT(idx < DHD_MAX_IFS);
	ifp = dhd->iflist[idx];
	ifp->wmf_psta_disable = val;
	return 0;
}
#endif /* DHD_WMF */

#ifdef DHD_PSTA
/* Get psta/psr configuration configuration */
int dhd_get_psta_mode(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	return (int)dhd->psta_mode;
}
/* Set psta/psr configuration configuration */
int dhd_set_psta_mode(dhd_pub_t *dhdp, uint32 val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd->psta_mode = val;
	return 0;
}
#endif /* DHD_PSTA */

#if (defined(DHD_WET) || defined(DHD_MCAST_REGEN) || defined(DHD_L2_FILTER))
static void
dhd_update_rx_pkt_chainable_state(dhd_pub_t* dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	if (
#ifdef DHD_L2_FILTER
		(ifp->block_ping) ||
#endif
#ifdef DHD_WET
		(dhd->wet_mode) ||
#endif
#ifdef DHD_MCAST_REGEN
		(ifp->mcast_regen_bss_enable) ||
#endif
		FALSE) {
		ifp->rx_pkt_chainable = FALSE;
	}
}
#endif /* DHD_WET || DHD_MCAST_REGEN || DHD_L2_FILTER */

#ifdef DHD_WET
/* Get wet configuration configuration */
int dhd_get_wet_mode(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	return (int)dhd->wet_mode;
}

/* Set wet configuration configuration */
int dhd_set_wet_mode(dhd_pub_t *dhdp, uint32 val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd->wet_mode = val;
	dhd_update_rx_pkt_chainable_state(dhdp, 0);
	return 0;
}
#endif /* DHD_WET */

#if defined(WL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
int32 dhd_role_to_nl80211_iftype(int32 role)
{
	switch (role) {
	case WLC_E_IF_ROLE_STA:
		return NL80211_IFTYPE_STATION;
	case WLC_E_IF_ROLE_AP:
		return NL80211_IFTYPE_AP;
	case WLC_E_IF_ROLE_WDS:
		return NL80211_IFTYPE_WDS;
	case WLC_E_IF_ROLE_P2P_GO:
		return NL80211_IFTYPE_P2P_GO;
	case WLC_E_IF_ROLE_P2P_CLIENT:
		return NL80211_IFTYPE_P2P_CLIENT;
	case WLC_E_IF_ROLE_IBSS:
	case WLC_E_IF_ROLE_NAN:
		return NL80211_IFTYPE_ADHOC;
	default:
		return NL80211_IFTYPE_UNSPECIFIED;
	}
}
#endif /* WL_CFG80211 && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

static void
dhd_ifadd_event_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_if_event_t *if_event = event_info;
	struct net_device *ndev;
	int ifidx, bssidx;
	int ret;
#if defined(WL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	struct wl_if_event_info info;
#endif /* WL_CFG80211 && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

	if (event != DHD_WQ_WORK_IF_ADD) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	if (!if_event) {
		DHD_ERROR(("%s: event data is null \n", __FUNCTION__));
		return;
	}

	dhd_net_if_lock_local(dhd);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

	ifidx = if_event->event.ifidx;
	bssidx = if_event->event.bssidx;
	DHD_TRACE(("%s: registering if with ifidx %d\n", __FUNCTION__, ifidx));


#if defined(WL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	if (if_event->event.ifidx > 0) {
		bzero(&info, sizeof(info));
		info.ifidx = if_event->event.ifidx;
		info.bssidx = if_event->event.bssidx;
		info.role = if_event->event.role;
		strncpy(info.name, if_event->name, IFNAMSIZ);
		if (wl_cfg80211_post_ifcreate(dhd->pub.info->iflist[0]->net,
			&info, if_event->mac, NULL, true) != NULL) {
			/* Do the post interface create ops */
			DHD_ERROR(("Post ifcreate ops done. Returning \n"));
			goto done;
		}
	}
#endif /* WL_CFG80211 && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

	/* This path is for non-android case */
	/* The interface name in host and in event msg are same */
	/* if name in event msg is used to create dongle if list on host */
	ndev = dhd_allocate_if(&dhd->pub, ifidx, if_event->name,
		if_event->mac, bssidx, TRUE, if_event->name);
	if (!ndev) {
		DHD_ERROR(("%s: net device alloc failed  \n", __FUNCTION__));
		goto done;
	}

	DHD_PERIM_UNLOCK(&dhd->pub);
	ret = dhd_register_if(&dhd->pub, ifidx, TRUE);
	DHD_PERIM_LOCK(&dhd->pub);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: dhd_register_if failed\n", __FUNCTION__));
		dhd_remove_if(&dhd->pub, ifidx, TRUE);
		goto done;
	}
#ifndef PCIE_FULL_DONGLE
	/* Turn on AP isolation in the firmware for interfaces operating in AP mode */
	if (FW_SUPPORTED((&dhd->pub), ap) && (if_event->event.role != WLC_E_IF_ROLE_STA)) {
		uint32 var_int =  1;
		ret = dhd_iovar(&dhd->pub, ifidx, "ap_isolate", (char *)&var_int, sizeof(var_int),
				NULL, 0, TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: Failed to set ap_isolate to dongle\n", __FUNCTION__));
			dhd_remove_if(&dhd->pub, ifidx, TRUE);
		}
	}
#endif /* PCIE_FULL_DONGLE */

done:
	MFREE(dhd->pub.osh, if_event, sizeof(dhd_if_event_t));

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_net_if_unlock_local(dhd);
}

static void
dhd_ifdel_event_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	int ifidx;
	dhd_if_event_t *if_event = event_info;


	if (event != DHD_WQ_WORK_IF_DEL) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	if (!if_event) {
		DHD_ERROR(("%s: event data is null \n", __FUNCTION__));
		return;
	}

	dhd_net_if_lock_local(dhd);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

	ifidx = if_event->event.ifidx;
	DHD_TRACE(("Removing interface with idx %d\n", ifidx));

	DHD_PERIM_UNLOCK(&dhd->pub);
#if defined(WL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	if (if_event->event.ifidx > 0) {
		/* Do the post interface del ops */
		if (wl_cfg80211_post_ifdel(dhd->pub.info->iflist[ifidx]->net, true) == 0) {
			DHD_TRACE(("Post ifdel ops done. Returning \n"));
			DHD_PERIM_LOCK(&dhd->pub);
			goto done;
		}
	}
#endif /* WL_CFG80211 && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

	dhd_remove_if(&dhd->pub, ifidx, TRUE);
	DHD_PERIM_LOCK(&dhd->pub);

#if defined(WL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
done:
#endif /* WL_CFG80211 && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
	MFREE(dhd->pub.osh, if_event, sizeof(dhd_if_event_t));

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_net_if_unlock_local(dhd);
}

#ifdef DHD_UPDATE_INTF_MAC
static void
dhd_ifupdate_event_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	int ifidx;
	dhd_if_event_t *if_event = event_info;

	if (event != DHD_WQ_WORK_IF_UPDATE) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	if (!if_event) {
		DHD_ERROR(("%s: event data is null \n", __FUNCTION__));
		return;
	}

	dhd_net_if_lock_local(dhd);
	DHD_OS_WAKE_LOCK(&dhd->pub);

	ifidx = if_event->event.ifidx;
	DHD_TRACE(("%s: Update interface with idx %d\n", __FUNCTION__, ifidx));

	dhd_op_if_update(&dhd->pub, ifidx);

	MFREE(dhd->pub.osh, if_event, sizeof(dhd_if_event_t));

	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_net_if_unlock_local(dhd);
}

int dhd_op_if_update(dhd_pub_t *dhdpub, int ifidx)
{
	dhd_info_t *    dhdinfo = NULL;
	dhd_if_t   *    ifp = NULL;
	int             ret = 0;
	char            buf[128];

	if ((NULL==dhdpub)||(NULL==dhdpub->info)) {
		DHD_ERROR(("%s: *** DHD handler is NULL!\n", __FUNCTION__));
		return -1;
	} else {
		dhdinfo = (dhd_info_t *)dhdpub->info;
		ifp = dhdinfo->iflist[ifidx];
		if (NULL==ifp) {
		    DHD_ERROR(("%s: *** ifp handler is NULL!\n", __FUNCTION__));
		    return -2;
		}
	}

	DHD_TRACE(("%s: idx %d\n", __FUNCTION__, ifidx));
	// Get MAC address
	strcpy(buf, "cur_etheraddr");
	ret = dhd_wl_ioctl_cmd(&dhdinfo->pub, WLC_GET_VAR, buf, sizeof(buf), FALSE, ifp->idx);
	if (0>ret) {
		DHD_ERROR(("Failed to upudate the MAC address for itf=%s, ret=%d\n", ifp->name, ret));
		// avoid collision
		dhdinfo->iflist[ifp->idx]->mac_addr[5] += 1;
		// force locally administrate address
		ETHER_SET_LOCALADDR(&dhdinfo->iflist[ifp->idx]->mac_addr);
	} else {
		DHD_EVENT(("Got mac for itf %s, idx %d, MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
		           ifp->name, ifp->idx,
		           (unsigned char)buf[0], (unsigned char)buf[1], (unsigned char)buf[2],
		           (unsigned char)buf[3], (unsigned char)buf[4], (unsigned char)buf[5]));
		memcpy(dhdinfo->iflist[ifp->idx]->mac_addr, buf, ETHER_ADDR_LEN);
		if (dhdinfo->iflist[ifp->idx]->net) {
		    memcpy(dhdinfo->iflist[ifp->idx]->net->dev_addr, buf, ETHER_ADDR_LEN);
		}
	}

	return ret;
}
#endif /* DHD_UPDATE_INTF_MAC */

static void
dhd_set_mac_addr_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_if_t *ifp = event_info;

	if (event != DHD_WQ_WORK_SET_MAC) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	dhd_net_if_lock_local(dhd);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

	// terence 20160907: fix for not able to set mac when wlan0 is down
	if (ifp == NULL || !ifp->set_macaddress) {
		goto done;
	}
	if (ifp == NULL || !dhd->pub.up) {
		DHD_ERROR(("%s: interface info not available/down \n", __FUNCTION__));
		goto done;
	}

	DHD_ERROR(("%s: MACID is overwritten\n", __FUNCTION__));
	ifp->set_macaddress = FALSE;
	if (_dhd_set_mac_address(dhd, ifp->idx, ifp->mac_addr) == 0)
		DHD_INFO(("%s: MACID is overwritten\n",	__FUNCTION__));
	else
		DHD_ERROR(("%s: _dhd_set_mac_address() failed\n", __FUNCTION__));

done:
	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_net_if_unlock_local(dhd);
}

static void
dhd_set_mcast_list_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	int ifidx = (int)((long int)event_info);
	dhd_if_t *ifp = NULL;

	if (event != DHD_WQ_WORK_SET_MCAST_LIST) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	dhd_net_if_lock_local(dhd);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

	ifp = dhd->iflist[ifidx];

	if (ifp == NULL || !dhd->pub.up) {
		DHD_ERROR(("%s: interface info not available/down \n", __FUNCTION__));
		goto done;
	}

	if (ifp == NULL || !dhd->pub.up) {
		DHD_ERROR(("%s: interface info not available/down \n", __FUNCTION__));
		goto done;
	}

	ifidx = ifp->idx;


	_dhd_set_multicast_list(dhd, ifidx);
	DHD_INFO(("%s: set multicast list for if %d\n", __FUNCTION__, ifidx));

done:
	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_net_if_unlock_local(dhd);
}

static int
dhd_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;

	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	struct sockaddr *sa = (struct sockaddr *)addr;
	int ifidx;
	dhd_if_t *dhdif;

	ifidx = dhd_net2idx(dhd, dev);
	if (ifidx == DHD_BAD_IF)
		return -1;

	dhdif = dhd->iflist[ifidx];

	dhd_net_if_lock_local(dhd);
	memcpy(dhdif->mac_addr, sa->sa_data, ETHER_ADDR_LEN);
	dhdif->set_macaddress = TRUE;
	dhd_net_if_unlock_local(dhd);
	dhd_deferred_schedule_work(dhd->dhd_deferred_wq, (void *)dhdif, DHD_WQ_WORK_SET_MAC,
		dhd_set_mac_addr_handler, DHD_WQ_WORK_PRIORITY_LOW);
	return ret;
}

static void
dhd_set_multicast_list(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ifidx;

	ifidx = dhd_net2idx(dhd, dev);
	if (ifidx == DHD_BAD_IF)
		return;

	dhd->iflist[ifidx]->set_multicast = TRUE;
	dhd_deferred_schedule_work(dhd->dhd_deferred_wq, (void *)((long int)ifidx),
		DHD_WQ_WORK_SET_MCAST_LIST, dhd_set_mcast_list_handler, DHD_WQ_WORK_PRIORITY_LOW);

	// terence 20160907: fix for not able to set mac when wlan0 is down
	dhd_deferred_schedule_work(dhd->dhd_deferred_wq, (void *)dhd->iflist[ifidx],
		DHD_WQ_WORK_SET_MAC, dhd_set_mac_addr_handler, DHD_WQ_WORK_PRIORITY_LOW);
}

#ifdef DHD_UCODE_DOWNLOAD
/* Get ucode path */
char *
dhd_get_ucode_path(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	return dhd->uc_path;
}
#endif /* DHD_UCODE_DOWNLOAD */

#ifdef PROP_TXSTATUS
int
dhd_os_wlfc_block(dhd_pub_t *pub)
{
	dhd_info_t *di = (dhd_info_t *)(pub->info);
	ASSERT(di != NULL);
	/* terence 20161229: don't do spin lock if proptx not enabled */
	if (disable_proptx)
		return 1;
#ifdef BCMDBUS
	spin_lock_irqsave(&di->wlfc_spinlock, di->wlfc_lock_flags);
#else
	spin_lock_bh(&di->wlfc_spinlock);
#endif /* BCMDBUS */
	return 1;
}

int
dhd_os_wlfc_unblock(dhd_pub_t *pub)
{
	dhd_info_t *di = (dhd_info_t *)(pub->info);

	ASSERT(di != NULL);
	/* terence 20161229: don't do spin lock if proptx not enabled */
	if (disable_proptx)
		return 1;
#ifdef BCMDBUS
	spin_unlock_irqrestore(&di->wlfc_spinlock, di->wlfc_lock_flags);
#else
	spin_unlock_bh(&di->wlfc_spinlock);
#endif /* BCMDBUS */
	return 1;
}

#endif /* PROP_TXSTATUS */

#if defined(DHD_RX_DUMP) || defined(DHD_TX_DUMP)
typedef struct {
	uint16 type;
	const char *str;
} PKTTYPE_INFO;

static const PKTTYPE_INFO packet_type_info[] =
{
	{ ETHER_TYPE_IP, "IP" },
	{ ETHER_TYPE_ARP, "ARP" },
	{ ETHER_TYPE_BRCM, "BRCM" },
	{ ETHER_TYPE_802_1X, "802.1X" },
	{ ETHER_TYPE_WAI, "WAPI" },
	{ 0, ""}
};

static const char *_get_packet_type_str(uint16 type)
{
	int i;
	int n = sizeof(packet_type_info)/sizeof(packet_type_info[1]) - 1;

	for (i = 0; i < n; i++) {
		if (packet_type_info[i].type == type)
			return packet_type_info[i].str;
	}

	return packet_type_info[n].str;
}

void
dhd_trx_dump(struct net_device *ndev, uint8 *dump_data, uint datalen, bool tx)
{
	uint16 protocol;
	char *ifname;

	protocol = (dump_data[12] << 8) | dump_data[13];
	ifname = ndev ? ndev->name : "N/A";

	if (protocol != ETHER_TYPE_BRCM) {
		DHD_ERROR(("[dhd-%s] %s DUMP - %s\n", ifname, tx?"Tx":"Rx",
			_get_packet_type_str(protocol)));
#if defined(DHD_TX_FULL_DUMP) || defined(DHD_RX_FULL_DUMP)
		prhex("Data", dump_data, datalen);
#endif /* DHD_TX_FULL_DUMP || DHD_RX_FULL_DUMP */
	}
}
#endif /* DHD_TX_DUMP || DHD_RX_DUMP */

/*  This routine do not support Packet chain feature, Currently tested for
 *  proxy arp feature
 */
int dhd_sendup(dhd_pub_t *dhdp, int ifidx, void *p)
{
	struct sk_buff *skb;
	void *skbhead = NULL;
	void *skbprev = NULL;
	dhd_if_t *ifp;
	ASSERT(!PKTISCHAINED(p));
	skb = PKTTONATIVE(dhdp->osh, p);

	ifp = dhdp->info->iflist[ifidx];
	skb->dev = ifp->net;
#if defined(BCM_GMAC3)
	/* Forwarder capable interfaces use WOFA based forwarding */
	if (ifp->fwdh) {
		struct ether_header *eh = (struct ether_header *)PKTDATA(dhdp->osh, p);
		uint16 * da = (uint16 *)(eh->ether_dhost);
		uintptr_t wofa_data;
		ASSERT(ISALIGNED(da, 2));

		wofa_data = fwder_lookup(ifp->fwdh->mate, da, ifp->idx);
		if (wofa_data == WOFA_DATA_INVALID) { /* Unknown MAC address */
			if (fwder_transmit(ifp->fwdh, skb, 1, skb->dev) == FWDER_SUCCESS) {
				return BCME_OK;
			}
		}
		PKTFRMNATIVE(dhdp->osh, p);
		PKTFREE(dhdp->osh, p, FALSE);
		return BCME_OK;
	}
#endif /* BCM_GMAC3 */

	skb->protocol = eth_type_trans(skb, skb->dev);

	if (in_interrupt()) {
		bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE,
			__FUNCTION__, __LINE__);
		netif_rx(skb);
	} else {
		if (dhdp->info->rxthread_enabled) {
			if (!skbhead) {
				skbhead = skb;
			} else {
				PKTSETNEXT(dhdp->osh, skbprev, skb);
			}
			skbprev = skb;
		} else {
			/* If the receive is not processed inside an ISR,
			 * the softirqd must be woken explicitly to service
			 * the NET_RX_SOFTIRQ.	In 2.6 kernels, this is handled
			 * by netif_rx_ni(), but in earlier kernels, we need
			 * to do it manually.
			 */
			bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE,
				__FUNCTION__, __LINE__);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
			netif_rx_ni(skb);
#else
			ulong flags;
			netif_rx(skb);
			local_irq_save(flags);
			RAISE_RX_SOFTIRQ();
			local_irq_restore(flags);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) */
		}
	}

	if (dhdp->info->rxthread_enabled && skbhead)
		dhd_sched_rxf(dhdp, skbhead);

	return BCME_OK;
}

int BCMFASTPATH
__dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pktbuf)
{
	int ret = BCME_OK;
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct ether_header *eh = NULL;
#if defined(DHD_L2_FILTER)
	dhd_if_t *ifp = dhd_get_ifp(dhdp, ifidx);
#endif 

	/* Reject if down */
	if (!dhdp->up || (dhdp->busstate == DHD_BUS_DOWN)) {
		/* free the packet here since the caller won't */
		PKTCFREE(dhdp->osh, pktbuf, TRUE);
		return -ENODEV;
	}

#ifdef PCIE_FULL_DONGLE
	if (dhdp->busstate == DHD_BUS_SUSPEND) {
		DHD_ERROR(("%s : pcie is still in suspend state!!\n", __FUNCTION__));
		PKTCFREE(dhdp->osh, pktbuf, TRUE);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
		return -ENODEV;
#else
		return NETDEV_TX_BUSY;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20) */
	}
#endif /* PCIE_FULL_DONGLE */

#ifdef DHD_L2_FILTER
	/* if dhcp_unicast is enabled, we need to convert the */
	/* broadcast DHCP ACK/REPLY packets to Unicast. */
	if (ifp->dhcp_unicast) {
	    uint8* mac_addr;
	    uint8* ehptr = NULL;
	    int ret;
	    ret = bcm_l2_filter_get_mac_addr_dhcp_pkt(dhdp->osh, pktbuf, ifidx, &mac_addr);
	    if (ret == BCME_OK) {
		/*  if given mac address having valid entry in sta list
		 *  copy the given mac address, and return with BCME_OK
		*/
		if (dhd_find_sta(dhdp, ifidx, mac_addr)) {
		    ehptr = PKTDATA(dhdp->osh, pktbuf);
		    bcopy(mac_addr, ehptr + ETHER_DEST_OFFSET, ETHER_ADDR_LEN);
		}
	    }
	}

	if (ifp->grat_arp && DHD_IF_ROLE_AP(dhdp, ifidx)) {
	    if (bcm_l2_filter_gratuitous_arp(dhdp->osh, pktbuf) == BCME_OK) {
			PKTCFREE(dhdp->osh, pktbuf, TRUE);
			return BCME_ERROR;
	    }
	}

	if (ifp->parp_enable && DHD_IF_ROLE_AP(dhdp, ifidx)) {
		ret = dhd_l2_filter_pkt_handle(dhdp, ifidx, pktbuf, TRUE);

		/* Drop the packets if l2 filter has processed it already
		 * otherwise continue with the normal path
		 */
		if (ret == BCME_OK) {
			PKTCFREE(dhdp->osh, pktbuf, TRUE);
			return BCME_ERROR;
		}
	}
#endif /* DHD_L2_FILTER */
	/* Update multicast statistic */
	if (PKTLEN(dhdp->osh, pktbuf) >= ETHER_HDR_LEN) {
		uint8 *pktdata = (uint8 *)PKTDATA(dhdp->osh, pktbuf);
		eh = (struct ether_header *)pktdata;

		if (ETHER_ISMULTI(eh->ether_dhost))
			dhdp->tx_multicast++;
		if (ntoh16(eh->ether_type) == ETHER_TYPE_802_1X) {
#ifdef DHD_LOSSLESS_ROAMING
			uint8 prio = (uint8)PKTPRIO(pktbuf);

			/* back up 802.1x's priority */
			dhdp->prio_8021x = prio;
#endif /* DHD_LOSSLESS_ROAMING */
			DBG_EVENT_LOG(dhdp, WIFI_EVENT_DRIVER_EAPOL_FRAME_TRANSMIT_REQUESTED);
			atomic_inc(&dhd->pend_8021x_cnt);
			dhd_dump_eapol_4way_message(dhdp, dhd_ifname(dhdp, ifidx), pktdata, TRUE);
		}

		if (ntoh16(eh->ether_type) == ETHER_TYPE_IP) {
#ifdef DHD_DHCP_DUMP
			dhd_dhcp_dump(dhd_ifname(dhdp, ifidx), pktdata, TRUE);
#endif /* DHD_DHCP_DUMP */
#ifdef DHD_ICMP_DUMP
			dhd_icmp_dump(dhd_ifname(dhdp, ifidx), pktdata, TRUE);
#endif /* DHD_ICMP_DUMP */
		}
#ifdef DHD_ARP_DUMP
		if (ntoh16(eh->ether_type) == ETHER_TYPE_ARP) {
			dhd_arp_dump(dhd_ifname(dhdp, ifidx), pktdata, TRUE);
		}
#endif /* DHD_ARP_DUMP */
	} else {
			PKTCFREE(dhdp->osh, pktbuf, TRUE);
			return BCME_ERROR;
	}

	{
		/* Look into the packet and update the packet priority */
#ifndef PKTPRIO_OVERRIDE
		if (PKTPRIO(pktbuf) == 0)
#endif /* !PKTPRIO_OVERRIDE */
		{
#if defined(QOS_MAP_SET)
			pktsetprio_qms(pktbuf, wl_get_up_table(dhdp, ifidx), FALSE);
#else
			pktsetprio(pktbuf, FALSE);
#endif /* QOS_MAP_SET */
		}
	}


#if defined(TRAFFIC_MGMT_DWM)
	traffic_mgmt_pkt_set_prio(dhdp, pktbuf);

#ifdef BCM_GMAC3
	DHD_PKT_SET_DATAOFF(pktbuf, 0);
#endif /* BCM_GMAC3 */
#endif 

#ifdef PCIE_FULL_DONGLE
	/*
	 * Lkup the per interface hash table, for a matching flowring. If one is not
	 * available, allocate a unique flowid and add a flowring entry.
	 * The found or newly created flowid is placed into the pktbuf's tag.
	 */
	ret = dhd_flowid_update(dhdp, ifidx, dhdp->flow_prio_map[(PKTPRIO(pktbuf))], pktbuf);
	if (ret != BCME_OK) {
		PKTCFREE(dhd->pub.osh, pktbuf, TRUE);
		return ret;
	}
#endif

#if defined(DHD_TX_DUMP)
	dhd_trx_dump(dhd_idx2net(dhdp, ifidx), PKTDATA(dhdp->osh, pktbuf),
		PKTLEN(dhdp->osh, pktbuf), TRUE);
#endif
	/* terence 20150901: Micky add to ajust the 802.1X priority */
	/* Set the 802.1X packet with the highest priority 7 */
	if (dhdp->conf->pktprio8021x >= 0)
		pktset8021xprio(pktbuf, dhdp->conf->pktprio8021x);

#ifdef PROP_TXSTATUS
	if (dhd_wlfc_is_supported(dhdp)) {
		/* store the interface ID */
		DHD_PKTTAG_SETIF(PKTTAG(pktbuf), ifidx);

		/* store destination MAC in the tag as well */
		DHD_PKTTAG_SETDSTN(PKTTAG(pktbuf), eh->ether_dhost);

		/* decide which FIFO this packet belongs to */
		if (ETHER_ISMULTI(eh->ether_dhost))
			/* one additional queue index (highest AC + 1) is used for bc/mc queue */
			DHD_PKTTAG_SETFIFO(PKTTAG(pktbuf), AC_COUNT);
		else
			DHD_PKTTAG_SETFIFO(PKTTAG(pktbuf), WME_PRIO2AC(PKTPRIO(pktbuf)));
	} else
#endif /* PROP_TXSTATUS */
	{
		/* If the protocol uses a data header, apply it */
		dhd_prot_hdrpush(dhdp, ifidx, pktbuf);
	}

	/* Use bus module to send data frame */
#ifdef WLMEDIA_HTSF
	dhd_htsf_addtxts(dhdp, pktbuf);
#endif
#ifdef PROP_TXSTATUS
	{
		if (dhd_wlfc_commit_packets(dhdp, (f_commitpkt_t)dhd_bus_txdata,
			dhdp->bus, pktbuf, TRUE) == WLFC_UNSUPPORTED) {
			/* non-proptxstatus way */
#ifdef BCMPCIE
			ret = dhd_bus_txdata(dhdp->bus, pktbuf, (uint8)ifidx);
#else
			ret = dhd_bus_txdata(dhdp->bus, pktbuf);
#endif /* BCMPCIE */
		}
	}
#else
#ifdef BCMPCIE
	ret = dhd_bus_txdata(dhdp->bus, pktbuf, (uint8)ifidx);
#else
	ret = dhd_bus_txdata(dhdp->bus, pktbuf);
#endif /* BCMPCIE */
#endif /* PROP_TXSTATUS */
#ifdef BCMDBUS
	if (ret)
		PKTCFREE(dhdp->osh, pktbuf, TRUE);
#endif /* BCMDBUS */

	return ret;
}

int BCMFASTPATH
dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pktbuf)
{
	int ret = 0;
	unsigned long flags;

	DHD_GENERAL_LOCK(dhdp, flags);
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhdp)) {
		DHD_ERROR(("%s: returning as busstate=%d\n",
			__FUNCTION__, dhdp->busstate));
		DHD_GENERAL_UNLOCK(dhdp, flags);
		PKTCFREE(dhdp->osh, pktbuf, TRUE);
		return -ENODEV;
	}
	DHD_BUS_BUSY_SET_IN_SEND_PKT(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);

#ifdef DHD_PCIE_RUNTIMEPM
	if (dhdpcie_runtime_bus_wake(dhdp, FALSE, __builtin_return_address(0))) {
		DHD_ERROR(("%s : pcie is still in suspend state!!\n", __FUNCTION__));
		PKTCFREE(dhdp->osh, pktbuf, TRUE);
		ret = -EBUSY;
		goto exit;
	}
#endif /* DHD_PCIE_RUNTIMEPM */

	DHD_GENERAL_LOCK(dhdp, flags);
	if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(dhdp)) {
		DHD_ERROR(("%s: bus is in suspend(%d) or suspending(0x%x) state!!\n",
			__FUNCTION__, dhdp->busstate, dhdp->dhd_bus_busy_state));
		DHD_BUS_BUSY_CLEAR_IN_SEND_PKT(dhdp);
		dhd_os_busbusy_wake(dhdp);
		DHD_GENERAL_UNLOCK(dhdp, flags);
		PKTCFREE(dhdp->osh, pktbuf, TRUE);
		return -ENODEV;
	}
	DHD_GENERAL_UNLOCK(dhdp, flags);

	ret = __dhd_sendpkt(dhdp, ifidx, pktbuf);

#ifdef DHD_PCIE_RUNTIMEPM
exit:
#endif
	DHD_GENERAL_LOCK(dhdp, flags);
	DHD_BUS_BUSY_CLEAR_IN_SEND_PKT(dhdp);
	dhd_os_busbusy_wake(dhdp);
	DHD_GENERAL_UNLOCK(dhdp, flags);
	return ret;
}

#if defined(DHD_LB_TXP)

int BCMFASTPATH
dhd_lb_sendpkt(dhd_info_t *dhd, struct net_device *net,
	int ifidx, void *skb)
{
	DHD_LB_STATS_PERCPU_ARR_INCR(dhd->tx_start_percpu_run_cnt);

	/* If the feature is disabled run-time do TX from here */
	if (atomic_read(&dhd->lb_txp_active) == 0) {
		DHD_LB_STATS_PERCPU_ARR_INCR(dhd->txp_percpu_run_cnt);
		 return __dhd_sendpkt(&dhd->pub, ifidx, skb);
	}

	/* Store the address of net device and interface index in the Packet tag */
	DHD_LB_TX_PKTTAG_SET_NETDEV((dhd_tx_lb_pkttag_fr_t *)PKTTAG(skb), net);
	DHD_LB_TX_PKTTAG_SET_IFIDX((dhd_tx_lb_pkttag_fr_t *)PKTTAG(skb), ifidx);

	/* Enqueue the skb into tx_pend_queue */
	skb_queue_tail(&dhd->tx_pend_queue, skb);

	DHD_TRACE(("%s(): Added skb %p for netdev %p \r\n", __FUNCTION__, skb, net));

	/* Dispatch the Tx job to be processed by the tx_tasklet */
	dhd_lb_tx_dispatch(&dhd->pub);

	return NETDEV_TX_OK;
}
#endif /* DHD_LB_TXP */

int BCMFASTPATH
dhd_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	int ret;
	uint datalen;
	void *pktbuf;
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_if_t *ifp = NULL;
	int ifidx;
	unsigned long flags;
#ifdef WLMEDIA_HTSF
	uint8 htsfdlystat_sz = dhd->pub.htsfdlystat_sz;
#else
	uint8 htsfdlystat_sz = 0;
#endif 
#ifdef DHD_WMF
	struct ether_header *eh;
	uint8 *iph;
#endif /* DHD_WMF */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhd_query_bus_erros(&dhd->pub)) {
		return -ENODEV;
	}

	DHD_GENERAL_LOCK(&dhd->pub, flags);
	DHD_BUS_BUSY_SET_IN_TX(&dhd->pub);
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);

#ifdef DHD_PCIE_RUNTIMEPM
	if (dhdpcie_runtime_bus_wake(&dhd->pub, FALSE, dhd_start_xmit)) {
		/* In order to avoid pkt loss. Return NETDEV_TX_BUSY until run-time resumed. */
		/* stop the network queue temporarily until resume done */
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		if (!dhdpcie_is_resume_done(&dhd->pub)) {
			dhd_bus_stop_queue(dhd->pub.bus);
		}
		DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
		dhd_os_busbusy_wake(&dhd->pub);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
		return -ENODEV;
#else
		return NETDEV_TX_BUSY;
#endif
	}
#endif /* DHD_PCIE_RUNTIMEPM */

	DHD_GENERAL_LOCK(&dhd->pub, flags);
#ifdef BCMPCIE
	if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(&dhd->pub)) {
		DHD_ERROR(("%s: bus is in suspend(%d) or suspending(0x%x) state!!\n",
			__FUNCTION__, dhd->pub.busstate, dhd->pub.dhd_bus_busy_state));
		DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
#ifdef PCIE_FULL_DONGLE
		/* Stop tx queues if suspend is in progress */
		if (DHD_BUS_CHECK_ANY_SUSPEND_IN_PROGRESS(&dhd->pub)) {
			dhd_bus_stop_queue(dhd->pub.bus);
		}
#endif /* PCIE_FULL_DONGLE */
		dhd_os_busbusy_wake(&dhd->pub);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
		return -ENODEV;
#else
		return NETDEV_TX_BUSY;
#endif
	}
#else
	if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(&dhd->pub)) {
		DHD_ERROR(("%s: bus is in suspend(%d) or suspending(0x%x) state!!\n",
			__FUNCTION__, dhd->pub.busstate, dhd->pub.dhd_bus_busy_state));
	}
#endif

	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK_TRY(DHD_FWDER_UNIT(dhd), lock_taken);


#if defined(DHD_HANG_SEND_UP_TEST)
	if (dhd->pub.req_hang_type == HANG_REASON_BUS_DOWN) {
		dhd->pub.busstate = DHD_BUS_DOWN;
	}
#endif /* DHD_HANG_SEND_UP_TEST */

	/* Reject if down */
	if (dhd->pub.hang_was_sent || DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(&dhd->pub)) {
		DHD_ERROR(("%s: xmit rejected pub.up=%d busstate=%d \n",
			__FUNCTION__, dhd->pub.up, dhd->pub.busstate));
		netif_stop_queue(net);
		/* Send Event when bus down detected during data session */
		if (dhd->pub.up && !dhd->pub.hang_was_sent && !DHD_BUS_CHECK_REMOVE(&dhd->pub)) {
			DHD_ERROR(("%s: Event HANG sent up\n", __FUNCTION__));
			dhd->pub.hang_reason = HANG_REASON_BUS_DOWN;
			net_os_send_hang_message(net);
		}
		DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
		dhd_os_busbusy_wake(&dhd->pub);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), lock_taken);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
		return -ENODEV;
#else
		return NETDEV_TX_BUSY;
#endif
	}

	ifp = DHD_DEV_IFP(net);
	ifidx = DHD_DEV_IFIDX(net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx %d\n", __FUNCTION__, ifidx));
		netif_stop_queue(net);
		DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
		dhd_os_busbusy_wake(&dhd->pub);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), lock_taken);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
		return -ENODEV;
#else
		return NETDEV_TX_BUSY;
#endif
	}

	DHD_GENERAL_UNLOCK(&dhd->pub, flags);

	ASSERT(ifidx == dhd_net2idx(dhd, net));
	ASSERT((ifp != NULL) && ((ifidx < DHD_MAX_IFS) && (ifp == dhd->iflist[ifidx])));

	bcm_object_trace_opr(skb, BCM_OBJDBG_ADD_PKT, __FUNCTION__, __LINE__);

	/* re-align socket buffer if "skb->data" is odd address */
	if (((unsigned long)(skb->data)) & 0x1) {
		unsigned char *data = skb->data;
		uint32 length = skb->len;
		PKTPUSH(dhd->pub.osh, skb, 1);
		memmove(skb->data, data, length);
		PKTSETLEN(dhd->pub.osh, skb, length);
	}

	datalen  = PKTLEN(dhd->pub.osh, skb);

	/* Make sure there's enough room for any header */
	if (skb_headroom(skb) < dhd->pub.hdrlen + htsfdlystat_sz) {
		struct sk_buff *skb2;

		DHD_INFO(("%s: insufficient headroom\n",
		          dhd_ifname(&dhd->pub, ifidx)));
		dhd->pub.tx_realloc++;

		bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE, __FUNCTION__, __LINE__);
		skb2 = skb_realloc_headroom(skb, dhd->pub.hdrlen + htsfdlystat_sz);

		dev_kfree_skb(skb);
		if ((skb = skb2) == NULL) {
			DHD_ERROR(("%s: skb_realloc_headroom failed\n",
			           dhd_ifname(&dhd->pub, ifidx)));
			ret = -ENOMEM;
			goto done;
		}
		bcm_object_trace_opr(skb, BCM_OBJDBG_ADD_PKT, __FUNCTION__, __LINE__);
	}

	/* move from dhdsdio_sendfromq(), try to orphan skb early */
	if (dhd->pub.conf->orphan_move == 2)
		PKTORPHAN(skb, dhd->pub.conf->tsq);
	else if (dhd->pub.conf->orphan_move == 3)
		skb_orphan(skb);

	/* Convert to packet */
	if (!(pktbuf = PKTFRMNATIVE(dhd->pub.osh, skb))) {
		DHD_ERROR(("%s: PKTFRMNATIVE failed\n",
		           dhd_ifname(&dhd->pub, ifidx)));
		bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE, __FUNCTION__, __LINE__);
		dev_kfree_skb_any(skb);
		ret = -ENOMEM;
		goto done;
	}

#if defined(WLMEDIA_HTSF)
	if (htsfdlystat_sz && PKTLEN(dhd->pub.osh, pktbuf) >= ETHER_ADDR_LEN) {
		uint8 *pktdata = (uint8 *)PKTDATA(dhd->pub.osh, pktbuf);
		struct ether_header *eh = (struct ether_header *)pktdata;

		if (!ETHER_ISMULTI(eh->ether_dhost) &&
			(ntoh16(eh->ether_type) == ETHER_TYPE_IP)) {
			eh->ether_type = hton16(ETHER_TYPE_BRCM_PKTDLYSTATS);
		}
	}
#endif 
#ifdef DHD_WET
	/* wet related packet proto manipulation should be done in DHD
	   since dongle doesn't have complete payload
	 */
	if (WET_ENABLED(&dhd->pub) &&
			(dhd_wet_send_proc(dhd->pub.wet_info, pktbuf, &pktbuf) < 0)) {
		DHD_INFO(("%s:%s: wet send proc failed\n",
				__FUNCTION__, dhd_ifname(&dhd->pub, ifidx)));
		PKTFREE(dhd->pub.osh, pktbuf, FALSE);
		ret =  -EFAULT;
		goto done;
	}
#endif /* DHD_WET */

#ifdef DHD_WMF
	eh = (struct ether_header *)PKTDATA(dhd->pub.osh, pktbuf);
	iph = (uint8 *)eh + ETHER_HDR_LEN;

	/* WMF processing for multicast packets
	 * Only IPv4 packets are handled
	 */
	if (ifp->wmf.wmf_enable && (ntoh16(eh->ether_type) == ETHER_TYPE_IP) &&
		(IP_VER(iph) == IP_VER_4) && (ETHER_ISMULTI(eh->ether_dhost) ||
		((IPV4_PROT(iph) == IP_PROT_IGMP) && dhd->pub.wmf_ucast_igmp))) {
#if defined(DHD_IGMP_UCQUERY) || defined(DHD_UCAST_UPNP)
		void *sdu_clone;
		bool ucast_convert = FALSE;
#ifdef DHD_UCAST_UPNP
		uint32 dest_ip;

		dest_ip = ntoh32(*((uint32 *)(iph + IPV4_DEST_IP_OFFSET)));
		ucast_convert = dhd->pub.wmf_ucast_upnp && MCAST_ADDR_UPNP_SSDP(dest_ip);
#endif /* DHD_UCAST_UPNP */
#ifdef DHD_IGMP_UCQUERY
		ucast_convert |= dhd->pub.wmf_ucast_igmp_query &&
			(IPV4_PROT(iph) == IP_PROT_IGMP) &&
			(*(iph + IPV4_HLEN(iph)) == IGMPV2_HOST_MEMBERSHIP_QUERY);
#endif /* DHD_IGMP_UCQUERY */
		if (ucast_convert) {
			dhd_sta_t *sta;
			unsigned long flags;
			struct list_head snapshot_list;
			struct list_head *wmf_ucforward_list;

			ret = NETDEV_TX_OK;

			/* For non BCM_GMAC3 platform we need a snapshot sta_list to
			 * resolve double DHD_IF_STA_LIST_LOCK call deadlock issue.
			 */
			wmf_ucforward_list = DHD_IF_WMF_UCFORWARD_LOCK(dhd, ifp, &snapshot_list);

			/* Convert upnp/igmp query to unicast for each assoc STA */
			list_for_each_entry(sta, wmf_ucforward_list, list) {
				/* Skip sending to proxy interfaces of proxySTA */
				if (sta->psta_prim != NULL && !ifp->wmf_psta_disable) {
					continue;
				}
				if ((sdu_clone = PKTDUP(dhd->pub.osh, pktbuf)) == NULL) {
					ret = WMF_NOP;
					break;
				}
				dhd_wmf_forward(ifp->wmf.wmfh, sdu_clone, 0, sta, 1);
			}
			DHD_IF_WMF_UCFORWARD_UNLOCK(dhd, wmf_ucforward_list);

			DHD_GENERAL_LOCK(&dhd->pub, flags);
			DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
			dhd_os_busbusy_wake(&dhd->pub);
			DHD_GENERAL_UNLOCK(&dhd->pub, flags);
			DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), lock_taken);
			DHD_OS_WAKE_UNLOCK(&dhd->pub);

			if (ret == NETDEV_TX_OK)
				PKTFREE(dhd->pub.osh, pktbuf, TRUE);

			return ret;
		} else
#endif /* defined(DHD_IGMP_UCQUERY) || defined(DHD_UCAST_UPNP) */
		{
			/* There will be no STA info if the packet is coming from LAN host
			 * Pass as NULL
			 */
			ret = dhd_wmf_packets_handle(&dhd->pub, pktbuf, NULL, ifidx, 0);
			switch (ret) {
			case WMF_TAKEN:
			case WMF_DROP:
				/* Either taken by WMF or we should drop it.
				 * Exiting send path
				 */

				DHD_GENERAL_LOCK(&dhd->pub, flags);
				DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
				dhd_os_busbusy_wake(&dhd->pub);
				DHD_GENERAL_UNLOCK(&dhd->pub, flags);
				DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), lock_taken);
				DHD_OS_WAKE_UNLOCK(&dhd->pub);
				return NETDEV_TX_OK;
			default:
				/* Continue the transmit path */
				break;
			}
		}
	}
#endif /* DHD_WMF */
#ifdef DHD_PSTA
	/* PSR related packet proto manipulation should be done in DHD
	 * since dongle doesn't have complete payload
	 */
	if (PSR_ENABLED(&dhd->pub) && (dhd_psta_proc(&dhd->pub,
		ifidx, &pktbuf, TRUE) < 0)) {
			DHD_ERROR(("%s:%s: psta send proc failed\n", __FUNCTION__,
				dhd_ifname(&dhd->pub, ifidx)));
	}
#endif /* DHD_PSTA */

#ifdef DHDTCPACK_SUPPRESS
	if (dhd->pub.tcpack_sup_mode == TCPACK_SUP_HOLD) {
		/* If this packet has been hold or got freed, just return */
		if (dhd_tcpack_hold(&dhd->pub, pktbuf, ifidx)) {
			ret = 0;
			goto done;
		}
	} else {
		/* If this packet has replaced another packet and got freed, just return */
		if (dhd_tcpack_suppress(&dhd->pub, pktbuf)) {
			ret = 0;
			goto done;
		}
	}
#endif /* DHDTCPACK_SUPPRESS */

	/*
	 * If Load Balance is enabled queue the packet
	 * else send directly from here.
	 */
#if defined(DHD_LB_TXP)
	ret = dhd_lb_sendpkt(dhd, net, ifidx, pktbuf);
#else
	ret = __dhd_sendpkt(&dhd->pub, ifidx, pktbuf);
#endif

done:
	if (ret) {
		ifp->stats.tx_dropped++;
		dhd->pub.tx_dropped++;
	} else {
#ifdef PROP_TXSTATUS
		/* tx_packets counter can counted only when wlfc is disabled */
		if (!dhd_wlfc_is_supported(&dhd->pub))
#endif
		{
			dhd->pub.tx_packets++;
			ifp->stats.tx_packets++;
			ifp->stats.tx_bytes += datalen;
		}
	}


	DHD_GENERAL_LOCK(&dhd->pub, flags);
	DHD_BUS_BUSY_CLEAR_IN_TX(&dhd->pub);
	dhd_os_busbusy_wake(&dhd->pub);
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);
	DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), lock_taken);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	/* Return ok: we always eat the packet */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
	return 0;
#else
	return NETDEV_TX_OK;
#endif
}


void
dhd_txflowcontrol(dhd_pub_t *dhdp, int ifidx, bool state)
{
	struct net_device *net;
	dhd_info_t *dhd = dhdp->info;
	int i;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(dhd);

#ifdef DHD_LOSSLESS_ROAMING
	/* block flowcontrol during roaming */
	if ((dhdp->dequeue_prec_map == 1 << PRIO_8021D_NC) && state == ON) {
		return;
	}
#endif

	if (ifidx == ALL_INTERFACES) {
		/* Flow control on all active interfaces */
		dhdp->txoff = state;
		for (i = 0; i < DHD_MAX_IFS; i++) {
			if (dhd->iflist[i]) {
				net = dhd->iflist[i]->net;
				if (state == ON)
					netif_stop_queue(net);
				else
					netif_wake_queue(net);
			}
		}
	} else {
		if (dhd->iflist[ifidx]) {
			net = dhd->iflist[ifidx]->net;
			if (state == ON)
				netif_stop_queue(net);
			else
				netif_wake_queue(net);
		}
	}
}


#ifdef DHD_WMF
bool
dhd_is_rxthread_enabled(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;

	return dhd->rxthread_enabled;
}
#endif /* DHD_WMF */

#ifdef DHD_MCAST_REGEN
/*
 * Description: This function is called to do the reverse translation
 *
 * Input    eh - pointer to the ethernet header
 */
int32
dhd_mcast_reverse_translation(struct ether_header *eh)
{
	uint8 *iph;
	uint32 dest_ip;

	iph = (uint8 *)eh + ETHER_HDR_LEN;
	dest_ip = ntoh32(*((uint32 *)(iph + IPV4_DEST_IP_OFFSET)));

	/* Only IP packets are handled */
	if (eh->ether_type != hton16(ETHER_TYPE_IP))
		return BCME_ERROR;

	/* Non-IPv4 multicast packets are not handled */
	if (IP_VER(iph) != IP_VER_4)
		return BCME_ERROR;

	/*
	 * The packet has a multicast IP and unicast MAC. That means
	 * we have to do the reverse translation
	 */
	if (IPV4_ISMULTI(dest_ip) && !ETHER_ISMULTI(&eh->ether_dhost)) {
		ETHER_FILL_MCAST_ADDR_FROM_IP(eh->ether_dhost, dest_ip);
		return BCME_OK;
	}

	return BCME_ERROR;
}
#endif /* MCAST_REGEN */

#ifdef SHOW_LOGTRACE
static int
dhd_event_logtrace_pkt_process(dhd_pub_t *dhdp, struct sk_buff * skb)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	int ret = BCME_OK;
	uint datalen;
	bcm_event_msg_u_t evu;
	void *data = NULL;
	void *pktdata = NULL;
	bcm_event_t *pvt_data;
	uint pktlen;

	DHD_TRACE(("%s:Enter\n", __FUNCTION__));

	/* In dhd_rx_frame, header is stripped using skb_pull
	 * of size ETH_HLEN, so adjust pktlen accordingly
	 */
	pktlen = skb->len + ETH_HLEN;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)
	pktdata = (void *)skb_mac_header(skb);
#else
	pktdata = (void *)skb->mac.raw;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) */

	ret = wl_host_event_get_data(pktdata, pktlen, &evu);

	if (ret != BCME_OK) {
		DHD_ERROR(("%s: wl_host_event_get_data err = %d\n",
			__FUNCTION__, ret));
		goto exit;
	}

	datalen = ntoh32(evu.event.datalen);

	pvt_data = (bcm_event_t *)pktdata;
	data = &pvt_data[1];

	dhd_dbg_trace_evnt_handler(dhdp, data, &dhd->event_data, datalen);

exit:
	return ret;
}

static void
dhd_event_logtrace_process(struct work_struct * work)
{
/* Ignore compiler warnings due to -Werror=cast-qual */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	struct dhd_info *dhd =
		container_of(work, struct dhd_info, event_log_dispatcher_work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	dhd_pub_t *dhdp;
	struct sk_buff *skb;

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	dhdp = &dhd->pub;

	if (!dhdp) {
		DHD_ERROR(("%s: dhd pub is null \n", __FUNCTION__));
		return;
	}

	DHD_TRACE(("%s:Enter\n", __FUNCTION__));

	/* Run while(1) loop till all skbs are dequeued */
	while ((skb = skb_dequeue(&dhd->evt_trace_queue)) != NULL) {
#ifdef PCIE_FULL_DONGLE
		int ifid;
		ifid = DHD_PKTTAG_IFID((dhd_pkttag_fr_t *)PKTTAG(skb));
		if (ifid == DHD_EVENT_IF) {
			dhd_event_logtrace_infobuf_pkt_process(dhdp, skb, &dhd->event_data);
			/* For sending skb to network layer, convert it to Native PKT
			 * after that assign skb->dev with Primary interface n/w device
			 * as for infobuf events, we are sending special DHD_EVENT_IF
			 */
#ifdef DHD_USE_STATIC_CTRLBUF
			PKTFREE_STATIC(dhdp->osh, skb, FALSE);
#else
			PKTFREE(dhdp->osh, skb, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
			continue;
		}
		else {
			dhd_event_logtrace_pkt_process(dhdp, skb);
		}
#else
		dhd_event_logtrace_pkt_process(dhdp, skb);
#endif /* PCIE_FULL_DONGLE */

		/* Free skb buffer here if DHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT
		* macro is defined the Info Ring event and WLC_E_TRACE event is freed in DHD
		* else it is always sent up to network layers.
		*/
#ifdef DHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT
#ifdef DHD_USE_STATIC_CTRLBUF
		PKTFREE_STATIC(dhdp->osh, skb, FALSE);
#else
		PKTFREE(dhdp->osh, skb, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
#else /* !DHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT */
		/* Do not call netif_recieve_skb as this workqueue scheduler is not from NAPI
		 * Also as we are not in INTR context, do not call netif_rx, instead call
		 * netif_rx_ni (for kerenl >= 2.6) which  does netif_rx, disables irq, raise
		 * NET_IF_RX softirq and enables interrupts back
		 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
		netif_rx_ni(skb);
#else
		{
			ulong flags;
			netif_rx(skb);
			local_irq_save(flags);
			RAISE_RX_SOFTIRQ();
			local_irq_restore(flags);
		}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) */
#endif /* DHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT */
	}
}

void
dhd_event_logtrace_enqueue(dhd_pub_t *dhdp, int ifidx, void *pktbuf)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

#ifdef PCIE_FULL_DONGLE
	/* Add ifidx in the PKTTAG */
	DHD_PKTTAG_SET_IFID((dhd_pkttag_fr_t *)PKTTAG(pktbuf), ifidx);
#endif /* PCIE_FULL_DONGLE */
	skb_queue_tail(&dhd->evt_trace_queue, pktbuf);

	schedule_work(&dhd->event_log_dispatcher_work);
}

void
dhd_event_logtrace_flush_queue(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&dhd->evt_trace_queue)) != NULL) {
#ifdef DHD_USE_STATIC_CTRLBUF
		PKTFREE_STATIC(dhdp->osh, skb, FALSE);
#else
		PKTFREE(dhdp->osh, skb, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
	}
}
#endif /* SHOW_LOGTRACE */

/** Called when a frame is received by the dongle on interface 'ifidx' */
void
dhd_rx_frame(dhd_pub_t *dhdp, int ifidx, void *pktbuf, int numpkt, uint8 chan)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct sk_buff *skb;
	uchar *eth;
	uint len;
	void *data, *pnext = NULL;
	int i;
	dhd_if_t *ifp;
	wl_event_msg_t event;
	int tout_rx = 0;
	int tout_ctrl = 0;
	void *skbhead = NULL;
	void *skbprev = NULL;
	uint16 protocol;
	unsigned char *dump_data;
#ifdef DHD_MCAST_REGEN
	uint8 interface_role;
	if_flow_lkup_t *if_flow_lkup;
	unsigned long flags;
#endif
#ifdef DHD_WAKE_STATUS
	int pkt_wake = 0;
	wake_counts_t *wcp = NULL;
#endif /* DHD_WAKE_STATUS */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	for (i = 0; pktbuf && i < numpkt; i++, pktbuf = pnext) {
		struct ether_header *eh;

		pnext = PKTNEXT(dhdp->osh, pktbuf);
		PKTSETNEXT(dhdp->osh, pktbuf, NULL);

		/* info ring "debug" data, which is not a 802.3 frame, is sent/hacked with a
		 * special ifidx of DHD_EVENT_IF.  This is just internal to dhd to get the data from
		 * dhd_msgbuf.c:dhd_prot_infobuf_cmplt_process() to here (dhd_rx_frame).
		 */
		if (ifidx == DHD_EVENT_IF) {
			/* Event msg printing is called from dhd_rx_frame which is in Tasklet
			 * context in case of PCIe FD, in case of other bus this will be from
			 * DPC context. If we get bunch of events from Dongle then printing all
			 * of them from Tasklet/DPC context that too in data path is costly.
			 * Also in the new Dongle SW(4359, 4355 onwards) console prints too come as
			 * events with type WLC_E_TRACE.
			 * We'll print this console logs from the WorkQueue context by enqueing SKB
			 * here and Dequeuing will be done in WorkQueue and will be freed only if
			 * DHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT is defined
			 */
#ifdef SHOW_LOGTRACE
			dhd_event_logtrace_enqueue(dhdp, ifidx, pktbuf);
#else /* !SHOW_LOGTRACE */
		/* If SHOW_LOGTRACE not defined and ifidx is DHD_EVENT_IF,
		 * free the PKT here itself
		 */
#ifdef DHD_USE_STATIC_CTRLBUF
		PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
		PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
#endif /* SHOW_LOGTRACE */
			continue;
		}
#ifdef DHD_WAKE_STATUS
#ifdef BCMDBUS
		wcp = NULL;
#else
		pkt_wake = dhd_bus_get_bus_wake(dhdp);
		wcp = dhd_bus_get_wakecount(dhdp);
#endif /* BCMDBUS */
		if (wcp == NULL) {
			/* If wakeinfo count buffer is null do not update wake count values */
			pkt_wake = 0;
		}
#endif /* DHD_WAKE_STATUS */

		ifp = dhd->iflist[ifidx];
		if (ifp == NULL) {
			DHD_ERROR(("%s: ifp is NULL. drop packet\n",
				__FUNCTION__));
			PKTCFREE(dhdp->osh, pktbuf, FALSE);
			continue;
		}

		eh = (struct ether_header *)PKTDATA(dhdp->osh, pktbuf);

		/* Dropping only data packets before registering net device to avoid kernel panic */
#ifndef PROP_TXSTATUS_VSDB
		if ((!ifp->net || ifp->net->reg_state != NETREG_REGISTERED) &&
			(ntoh16(eh->ether_type) != ETHER_TYPE_BRCM))
#else
		if ((!ifp->net || ifp->net->reg_state != NETREG_REGISTERED || !dhd->pub.up) &&
			(ntoh16(eh->ether_type) != ETHER_TYPE_BRCM))
#endif /* PROP_TXSTATUS_VSDB */
		{
			DHD_ERROR(("%s: net device is NOT registered yet. drop packet\n",
			__FUNCTION__));
			PKTCFREE(dhdp->osh, pktbuf, FALSE);
			continue;
		}

#ifdef PROP_TXSTATUS
		if (dhd_wlfc_is_header_only_pkt(dhdp, pktbuf)) {
			/* WLFC may send header only packet when
			there is an urgent message but no packet to
			piggy-back on
			*/
			PKTCFREE(dhdp->osh, pktbuf, FALSE);
			continue;
		}
#endif
#ifdef DHD_L2_FILTER
		/* If block_ping is enabled drop the ping packet */
		if (ifp->block_ping) {
			if (bcm_l2_filter_block_ping(dhdp->osh, pktbuf) == BCME_OK) {
				PKTCFREE(dhdp->osh, pktbuf, FALSE);
				continue;
			}
		}
		if (ifp->grat_arp && DHD_IF_ROLE_STA(dhdp, ifidx)) {
		    if (bcm_l2_filter_gratuitous_arp(dhdp->osh, pktbuf) == BCME_OK) {
				PKTCFREE(dhdp->osh, pktbuf, FALSE);
				continue;
		    }
		}
		if (ifp->parp_enable && DHD_IF_ROLE_AP(dhdp, ifidx)) {
			int ret = dhd_l2_filter_pkt_handle(dhdp, ifidx, pktbuf, FALSE);

			/* Drop the packets if l2 filter has processed it already
			 * otherwise continue with the normal path
			 */
			if (ret == BCME_OK) {
				PKTCFREE(dhdp->osh, pktbuf, TRUE);
				continue;
			}
		}
#endif /* DHD_L2_FILTER */

#ifdef DHD_MCAST_REGEN
		DHD_FLOWID_LOCK(dhdp->flowid_lock, flags);
		if_flow_lkup = (if_flow_lkup_t *)dhdp->if_flow_lkup;
		ASSERT(if_flow_lkup);

		interface_role = if_flow_lkup[ifidx].role;
		DHD_FLOWID_UNLOCK(dhdp->flowid_lock, flags);

		if (ifp->mcast_regen_bss_enable && (interface_role != WLC_E_IF_ROLE_WDS) &&
				!DHD_IF_ROLE_AP(dhdp, ifidx) &&
				ETHER_ISUCAST(eh->ether_dhost)) {
			if (dhd_mcast_reverse_translation(eh) ==  BCME_OK) {
#ifdef DHD_PSTA
				/* Change bsscfg to primary bsscfg for unicast-multicast packets */
				if ((dhd_get_psta_mode(dhdp) == DHD_MODE_PSTA) ||
						(dhd_get_psta_mode(dhdp) == DHD_MODE_PSR)) {
					if (ifidx != 0) {
						/* Let the primary in PSTA interface handle this
						 * frame after unicast to Multicast conversion
						 */
						ifp = dhd_get_ifp(dhdp, 0);
						ASSERT(ifp);
					}
				}
			}
#endif /* PSTA */
		}
#endif /* MCAST_REGEN */

#ifdef DHD_WMF
		/* WMF processing for multicast packets */
		if (ifp->wmf.wmf_enable && (ETHER_ISMULTI(eh->ether_dhost))) {
			dhd_sta_t *sta;
			int ret;

			sta = dhd_find_sta(dhdp, ifidx, (void *)eh->ether_shost);
			ret = dhd_wmf_packets_handle(dhdp, pktbuf, sta, ifidx, 1);
			switch (ret) {
				case WMF_TAKEN:
					/* The packet is taken by WMF. Continue to next iteration */
					continue;
				case WMF_DROP:
					/* Packet DROP decision by WMF. Toss it */
					DHD_ERROR(("%s: WMF decides to drop packet\n",
						__FUNCTION__));
					PKTCFREE(dhdp->osh, pktbuf, FALSE);
					continue;
				default:
					/* Continue the transmit path */
					break;
			}
		}
#endif /* DHD_WMF */

#ifdef DHDTCPACK_SUPPRESS
		dhd_tcpdata_info_get(dhdp, pktbuf);
#endif
		skb = PKTTONATIVE(dhdp->osh, pktbuf);

		ASSERT(ifp);
		skb->dev = ifp->net;
#ifdef DHD_WET
		/* wet related packet proto manipulation should be done in DHD
		 * since dongle doesn't have complete payload
		 */
		if (WET_ENABLED(&dhd->pub) && (dhd_wet_recv_proc(dhd->pub.wet_info,
				pktbuf) < 0)) {
			DHD_INFO(("%s:%s: wet recv proc failed\n",
				__FUNCTION__, dhd_ifname(dhdp, ifidx)));
		}
#endif /* DHD_WET */

#ifdef DHD_PSTA
		if (PSR_ENABLED(dhdp) && (dhd_psta_proc(dhdp, ifidx, &pktbuf, FALSE) < 0)) {
				DHD_ERROR(("%s:%s: psta recv proc failed\n", __FUNCTION__,
					dhd_ifname(dhdp, ifidx)));
		}
#endif /* DHD_PSTA */

#ifdef PCIE_FULL_DONGLE
		if ((DHD_IF_ROLE_AP(dhdp, ifidx) || DHD_IF_ROLE_P2PGO(dhdp, ifidx)) &&
			(!ifp->ap_isolate)) {
			eh = (struct ether_header *)PKTDATA(dhdp->osh, pktbuf);
			if (ETHER_ISUCAST(eh->ether_dhost)) {
				if (dhd_find_sta(dhdp, ifidx, (void *)eh->ether_dhost)) {
					dhd_sendpkt(dhdp, ifidx, pktbuf);
					continue;
				}
			} else {
				void *npktbuf = PKTDUP(dhdp->osh, pktbuf);
				if (npktbuf)
					dhd_sendpkt(dhdp, ifidx, npktbuf);
			}
		}
#endif /* PCIE_FULL_DONGLE */

		/* Get the protocol, maintain skb around eth_type_trans()
		 * The main reason for this hack is for the limitation of
		 * Linux 2.4 where 'eth_type_trans' uses the 'net->hard_header_len'
		 * to perform skb_pull inside vs ETH_HLEN. Since to avoid
		 * coping of the packet coming from the network stack to add
		 * BDC, Hardware header etc, during network interface registration
		 * we set the 'net->hard_header_len' to ETH_HLEN + extra space required
		 * for BDC, Hardware header etc. and not just the ETH_HLEN
		 */
		eth = skb->data;
		len = skb->len;

		dump_data = skb->data;

		protocol = (skb->data[12] << 8) | skb->data[13];
		if (protocol == ETHER_TYPE_802_1X) {
			DBG_EVENT_LOG(dhdp, WIFI_EVENT_DRIVER_EAPOL_FRAME_RECEIVED);
			dhd_dump_eapol_4way_message(dhdp, dhd_ifname(dhdp, ifidx), dump_data, FALSE);
		}

		if (protocol != ETHER_TYPE_BRCM && protocol == ETHER_TYPE_IP) {
#ifdef DHD_DHCP_DUMP
			dhd_dhcp_dump(dhd_ifname(dhdp, ifidx), dump_data, FALSE);
#endif /* DHD_DHCP_DUMP */
#ifdef DHD_ICMP_DUMP
			dhd_icmp_dump(dhd_ifname(dhdp, ifidx), dump_data, FALSE);
#endif /* DHD_ICMP_DUMP */
		}
#ifdef DHD_ARP_DUMP
		if (ntoh16(eh->ether_type) == ETHER_TYPE_ARP) {
			dhd_arp_dump(dhd_ifname(dhdp, ifidx), dump_data, FALSE);
		}
#endif /* DHD_ARP_DUMP */
#ifdef DHD_RX_DUMP
		dhd_trx_dump(dhd_idx2net(dhdp, ifidx), dump_data, skb->len, FALSE);
#endif /* DHD_RX_DUMP */
#if defined(DHD_WAKE_STATUS) && defined(DHD_WAKEPKT_DUMP)
		if (pkt_wake) {
			prhex("[wakepkt_dump]", (char*)dump_data, MIN(len, 32));
		}
#endif /* DHD_WAKE_STATUS && DHD_WAKEPKT_DUMP */

		skb->protocol = eth_type_trans(skb, skb->dev);

		if (skb->pkt_type == PACKET_MULTICAST) {
			dhd->pub.rx_multicast++;
			ifp->stats.multicast++;
		}

		skb->data = eth;
		skb->len = len;

#ifdef WLMEDIA_HTSF
		dhd_htsf_addrxts(dhdp, pktbuf);
#endif
#ifdef DBG_PKT_MON
		DHD_DBG_PKT_MON_RX(dhdp, skb);
#endif /* DBG_PKT_MON */
#ifdef DHD_PKT_LOGGING
		DHD_PKTLOG_RX(dhdp, skb);
#endif /* DHD_PKT_LOGGING */
		/* Strip header, count, deliver upward */
		skb_pull(skb, ETH_HLEN);

		/* Process special event packets and then discard them */
		memset(&event, 0, sizeof(event));

		if (ntoh16(skb->protocol) == ETHER_TYPE_BRCM) {
			bcm_event_msg_u_t evu;
			int ret_event;
			int event_type;

			ret_event = wl_host_event_get_data(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)
			skb_mac_header(skb),
#else
			skb->mac.raw,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) */
			len, &evu);

			if (ret_event != BCME_OK) {
				DHD_ERROR(("%s: wl_host_event_get_data err = %d\n",
					__FUNCTION__, ret_event));
#ifdef DHD_USE_STATIC_CTRLBUF
				PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
				PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif
				continue;
			}

			memcpy(&event, &evu.event, sizeof(wl_event_msg_t));
			event_type = ntoh32_ua((void *)&event.event_type);
#ifdef SHOW_LOGTRACE
			/* Event msg printing is called from dhd_rx_frame which is in Tasklet
			 * context in case of PCIe FD, in case of other bus this will be from
			 * DPC context. If we get bunch of events from Dongle then printing all
			 * of them from Tasklet/DPC context that too in data path is costly.
			 * Also in the new Dongle SW(4359, 4355 onwards) console prints too come as
			 * events with type WLC_E_TRACE.
			 * We'll print this console logs from the WorkQueue context by enqueing SKB
			 * here and Dequeuing will be done in WorkQueue and will be freed only if
			 * DHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT is defined
			 */
			if (event_type == WLC_E_TRACE) {
				DHD_TRACE(("%s: WLC_E_TRACE\n", __FUNCTION__));
				dhd_event_logtrace_enqueue(dhdp, ifidx, pktbuf);
				continue;
			}
#endif /* SHOW_LOGTRACE */

			ret_event = dhd_wl_host_event(dhd, ifidx,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)
			skb_mac_header(skb),
#else
			skb->mac.raw,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) */
			len, &event, &data);

			wl_event_to_host_order(&event);
			if (!tout_ctrl)
				tout_ctrl = DHD_PACKET_TIMEOUT_MS;

#if defined(PNO_SUPPORT)
			if (event_type == WLC_E_PFN_NET_FOUND) {
				/* enforce custom wake lock to garantee that Kernel not suspended */
				tout_ctrl = CUSTOM_PNO_EVENT_LOCK_xTIME * DHD_PACKET_TIMEOUT_MS;
			}
#endif /* PNO_SUPPORT */
			if (numpkt != 1) {
				DHD_TRACE(("%s: Got BRCM event packet in a chained packet.\n",
				__FUNCTION__));
			}

#ifdef DHD_WAKE_STATUS
			if (unlikely(pkt_wake)) {
#ifdef DHD_WAKE_EVENT_STATUS
				if (event.event_type < WLC_E_LAST) {
					wcp->rc_event[event.event_type]++;
					wcp->rcwake++;
					pkt_wake = 0;
				}
#endif /* DHD_WAKE_EVENT_STATUS */
			}
#endif /* DHD_WAKE_STATUS */

			/* For delete virtual interface event, wl_host_event returns positive
			 * i/f index, do not proceed. just free the pkt.
			 */
			if ((event_type == WLC_E_IF) && (ret_event > 0)) {
				DHD_ERROR(("%s: interface is deleted. Free event packet\n",
				__FUNCTION__));
#ifdef DHD_USE_STATIC_CTRLBUF
				PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
				PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif
				continue;
			}

#if defined(DHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT) && !defined(SENDPROB)
#ifdef DHD_USE_STATIC_CTRLBUF
			PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
			PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
			continue;
#else
#ifdef SENDPROB
			if (!dhdp->recv_probereq || (event.event_type != WLC_E_PROBREQ_MSG)) {
#ifdef DHD_USE_STATIC_CTRLBUF
				PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
				PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif /* DHD_USE_STATIC_CTRLBUF */
				continue;
			}
#endif
			/*
			 * For the event packets, there is a possibility
			 * of ifidx getting modifed.Thus update the ifp
			 * once again.
			 */
			ASSERT(ifidx < DHD_MAX_IFS && dhd->iflist[ifidx]);
			ifp = dhd->iflist[ifidx];
#ifndef PROP_TXSTATUS_VSDB
			if (!(ifp && ifp->net && (ifp->net->reg_state == NETREG_REGISTERED)))
#else
			if (!(ifp && ifp->net && (ifp->net->reg_state == NETREG_REGISTERED) &&
				dhd->pub.up))
#endif /* PROP_TXSTATUS_VSDB */
			{
				DHD_ERROR(("%s: net device is NOT registered. drop event packet\n",
				__FUNCTION__));
#ifdef DHD_USE_STATIC_CTRLBUF
				PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
				PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif
				continue;
			}
#endif /* DHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT */
		} else {
			tout_rx = DHD_PACKET_TIMEOUT_MS;

#ifdef PROP_TXSTATUS
			dhd_wlfc_save_rxpath_ac_time(dhdp, (uint8)PKTPRIO(skb));
#endif /* PROP_TXSTATUS */

#ifdef DHD_WAKE_STATUS
			if (unlikely(pkt_wake)) {
				wcp->rxwake++;
#ifdef DHD_WAKE_RX_STATUS
#define ETHER_ICMP6_HEADER	20
#define ETHER_IPV6_SADDR (ETHER_ICMP6_HEADER + 2)
#define ETHER_IPV6_DAADR (ETHER_IPV6_SADDR + IPV6_ADDR_LEN)
#define ETHER_ICMPV6_TYPE (ETHER_IPV6_DAADR + IPV6_ADDR_LEN)

				if (ntoh16(skb->protocol) == ETHER_TYPE_ARP) /* ARP */
					wcp->rx_arp++;
				if (dump_data[0] == 0xFF) { /* Broadcast */
					wcp->rx_bcast++;
				} else if (dump_data[0] & 0x01) { /* Multicast */
					wcp->rx_mcast++;
					if (ntoh16(skb->protocol) == ETHER_TYPE_IPV6) {
					    wcp->rx_multi_ipv6++;
					    if ((skb->len > ETHER_ICMP6_HEADER) &&
					        (dump_data[ETHER_ICMP6_HEADER] == IPPROTO_ICMPV6)) {
					        wcp->rx_icmpv6++;
					        if (skb->len > ETHER_ICMPV6_TYPE) {
					            switch (dump_data[ETHER_ICMPV6_TYPE]) {
					            case NDISC_ROUTER_ADVERTISEMENT:
					                wcp->rx_icmpv6_ra++;
					                break;
					            case NDISC_NEIGHBOUR_ADVERTISEMENT:
					                wcp->rx_icmpv6_na++;
					                break;
					            case NDISC_NEIGHBOUR_SOLICITATION:
					                wcp->rx_icmpv6_ns++;
					                break;
					            }
					        }
					    }
					} else if (dump_data[2] == 0x5E) {
						wcp->rx_multi_ipv4++;
					} else {
						wcp->rx_multi_other++;
					}
				} else { /* Unicast */
					wcp->rx_ucast++;
				}
#undef ETHER_ICMP6_HEADER
#undef ETHER_IPV6_SADDR
#undef ETHER_IPV6_DAADR
#undef ETHER_ICMPV6_TYPE
#endif /* DHD_WAKE_RX_STATUS */
				pkt_wake = 0;
			}
#endif /* DHD_WAKE_STATUS */
		}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
		if (ifp->net)
			ifp->net->last_rx = jiffies;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0) */

		if (ntoh16(skb->protocol) != ETHER_TYPE_BRCM) {
			dhdp->dstats.rx_bytes += skb->len;
			dhdp->rx_packets++; /* Local count */
			ifp->stats.rx_bytes += skb->len;
			ifp->stats.rx_packets++;
		}

		if (in_interrupt()) {
			bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE,
				__FUNCTION__, __LINE__);
			DHD_PERIM_UNLOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
#if defined(DHD_LB_RXP)
			netif_receive_skb(skb);
#else /* !defined(DHD_LB_RXP) */
			netif_rx(skb);
#endif /* !defined(DHD_LB_RXP) */
			DHD_PERIM_LOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
		} else {
			if (dhd->rxthread_enabled) {
				if (!skbhead)
					skbhead = skb;
				else
					PKTSETNEXT(dhdp->osh, skbprev, skb);
				skbprev = skb;
			} else {

				/* If the receive is not processed inside an ISR,
				 * the softirqd must be woken explicitly to service
				 * the NET_RX_SOFTIRQ.	In 2.6 kernels, this is handled
				 * by netif_rx_ni(), but in earlier kernels, we need
				 * to do it manually.
				 */
				bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE,
					__FUNCTION__, __LINE__);

#if defined(DHD_LB_RXP)
				DHD_PERIM_UNLOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
				netif_receive_skb(skb);
				DHD_PERIM_LOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
#else /* !defined(DHD_LB_RXP) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
				DHD_PERIM_UNLOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
				netif_rx_ni(skb);
				DHD_PERIM_LOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
#else
				ulong flags;
				DHD_PERIM_UNLOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
				netif_rx(skb);
				DHD_PERIM_LOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
				local_irq_save(flags);
				RAISE_RX_SOFTIRQ();
				local_irq_restore(flags);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) */
#endif /* !defined(DHD_LB_RXP) */
			}
		}
	}

	if (dhd->rxthread_enabled && skbhead)
		dhd_sched_rxf(dhdp, skbhead);

	DHD_OS_WAKE_LOCK_RX_TIMEOUT_ENABLE(dhdp, tout_rx);
	DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(dhdp, tout_ctrl);
}

void
dhd_event(struct dhd_info *dhd, char *evpkt, int evlen, int ifidx)
{
	/* Linux version has nothing to do */
	return;
}

void
dhd_txcomplete(dhd_pub_t *dhdp, void *txp, bool success)
{
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct ether_header *eh;
	uint16 type;

	dhd_prot_hdrpull(dhdp, NULL, txp, NULL, NULL);


	eh = (struct ether_header *)PKTDATA(dhdp->osh, txp);
	type  = ntoh16(eh->ether_type);

	if ((type == ETHER_TYPE_802_1X) && (dhd_get_pend_8021x_cnt(dhd) > 0)) {
		atomic_dec(&dhd->pend_8021x_cnt);
	}

#ifdef PROP_TXSTATUS
	if (dhdp->wlfc_state && (dhdp->proptxstatus_mode != WLFC_FCMODE_NONE)) {
		dhd_if_t *ifp = dhd->iflist[DHD_PKTTAG_IF(PKTTAG(txp))];
		uint datalen  = PKTLEN(dhd->pub.osh, txp);
		if (ifp != NULL) {
			if (success) {
				dhd->pub.tx_packets++;
				ifp->stats.tx_packets++;
				ifp->stats.tx_bytes += datalen;
			} else {
				ifp->stats.tx_dropped++;
			}
		}
	}
#endif
}

static struct net_device_stats *
dhd_get_stats(struct net_device *net)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_if_t *ifp;
	int ifidx;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!dhd) {
		DHD_ERROR(("%s : dhd is NULL\n", __FUNCTION__));
		goto error;
	}

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: BAD_IF\n", __FUNCTION__));
		goto error;
	}

	ifp = dhd->iflist[ifidx];

	if (!ifp) {
		ASSERT(ifp);
		DHD_ERROR(("%s: ifp is NULL\n", __FUNCTION__));
		goto error;
	}

	if (dhd->pub.up) {
		/* Use the protocol to get dongle stats */
		dhd_prot_dstats(&dhd->pub);
	}
	return &ifp->stats;

error:
	memset(&net->stats, 0, sizeof(net->stats));
	return &net->stats;
}

#ifndef BCMDBUS
static int
dhd_watchdog_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;
	/* This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */
	if (dhd_watchdog_prio > 0) {
		struct sched_param param;
		param.sched_priority = (dhd_watchdog_prio < MAX_RT_PRIO)?
			dhd_watchdog_prio:(MAX_RT_PRIO-1);
		setScheduler(current, SCHED_FIFO, &param);
	}

	while (1) {
		if (down_interruptible (&tsk->sema) == 0) {
			unsigned long flags;
			unsigned long jiffies_at_start = jiffies;
			unsigned long time_lapse;
			DHD_OS_WD_WAKE_LOCK(&dhd->pub);

			SMP_RD_BARRIER_DEPENDS();
			if (tsk->terminated) {
				break;
			}

			if (dhd->pub.dongle_reset == FALSE) {
				DHD_TIMER(("%s:\n", __FUNCTION__));
				dhd_bus_watchdog(&dhd->pub);

#ifdef DHD_TIMESYNC
				/* Call the timesync module watchdog */
				dhd_timesync_watchdog(&dhd->pub);
#endif /* DHD_TIMESYNC */

				DHD_GENERAL_LOCK(&dhd->pub, flags);
				/* Count the tick for reference */
				dhd->pub.tickcnt++;
#ifdef DHD_L2_FILTER
				dhd_l2_filter_watchdog(&dhd->pub);
#endif /* DHD_L2_FILTER */
				time_lapse = jiffies - jiffies_at_start;

				/* Reschedule the watchdog */
				if (dhd->wd_timer_valid) {
					mod_timer(&dhd->timer,
					    jiffies +
					    msecs_to_jiffies(dhd_watchdog_ms) -
					    min(msecs_to_jiffies(dhd_watchdog_ms), time_lapse));
				}
				DHD_GENERAL_UNLOCK(&dhd->pub, flags);
			}
			DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
		} else {
			break;
		}
	}

	complete_and_exit(&tsk->completed, 0);
}

static void dhd_watchdog(ulong data)
{
	dhd_info_t *dhd = (dhd_info_t *)data;
	unsigned long flags;

	if (dhd->pub.dongle_reset) {
		return;
	}

	if (dhd->thr_wdt_ctl.thr_pid >= 0) {
		up(&dhd->thr_wdt_ctl.sema);
		return;
	}

	DHD_OS_WD_WAKE_LOCK(&dhd->pub);
	/* Call the bus module watchdog */
	dhd_bus_watchdog(&dhd->pub);

#ifdef DHD_TIMESYNC
	/* Call the timesync module watchdog */
	dhd_timesync_watchdog(&dhd->pub);
#endif /* DHD_TIMESYNC */

	DHD_GENERAL_LOCK(&dhd->pub, flags);
	/* Count the tick for reference */
	dhd->pub.tickcnt++;

#ifdef DHD_L2_FILTER
	dhd_l2_filter_watchdog(&dhd->pub);
#endif /* DHD_L2_FILTER */
	/* Reschedule the watchdog */
	if (dhd->wd_timer_valid)
		mod_timer(&dhd->timer, jiffies + msecs_to_jiffies(dhd_watchdog_ms));
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);
	DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
}

#ifdef DHD_PCIE_RUNTIMEPM
static int
dhd_rpm_state_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;

	while (1) {
		if (down_interruptible (&tsk->sema) == 0) {
			unsigned long flags;
			unsigned long jiffies_at_start = jiffies;
			unsigned long time_lapse;

			SMP_RD_BARRIER_DEPENDS();
			if (tsk->terminated) {
				break;
			}

			if (dhd->pub.dongle_reset == FALSE) {
				DHD_TIMER(("%s:\n", __FUNCTION__));
				if (dhd->pub.up) {
					dhd_runtimepm_state(&dhd->pub);
				}

				DHD_GENERAL_LOCK(&dhd->pub, flags);
				time_lapse = jiffies - jiffies_at_start;

				/* Reschedule the watchdog */
				if (dhd->rpm_timer_valid) {
					mod_timer(&dhd->rpm_timer,
						jiffies +
						msecs_to_jiffies(dhd_runtimepm_ms) -
						min(msecs_to_jiffies(dhd_runtimepm_ms),
							time_lapse));
				}
				DHD_GENERAL_UNLOCK(&dhd->pub, flags);
			}
		} else {
			break;
		}
	}

	complete_and_exit(&tsk->completed, 0);
}

static void dhd_runtimepm(ulong data)
{
	dhd_info_t *dhd = (dhd_info_t *)data;

	if (dhd->pub.dongle_reset) {
		return;
	}

	if (dhd->thr_rpm_ctl.thr_pid >= 0) {
		up(&dhd->thr_rpm_ctl.sema);
		return;
	}
}

void dhd_runtime_pm_disable(dhd_pub_t *dhdp)
{
	dhd_os_runtimepm_timer(dhdp, 0);
	dhdpcie_runtime_bus_wake(dhdp, TRUE, __builtin_return_address(0));
	DHD_ERROR(("DHD Runtime PM Disabled \n"));
}

void dhd_runtime_pm_enable(dhd_pub_t *dhdp)
{
	if (dhd_get_idletime(dhdp)) {
		dhd_os_runtimepm_timer(dhdp, dhd_runtimepm_ms);
		DHD_ERROR(("DHD Runtime PM Enabled \n"));
	}
}

#endif /* DHD_PCIE_RUNTIMEPM */


#ifdef ENABLE_ADAPTIVE_SCHED
static void
dhd_sched_policy(int prio)
{
	struct sched_param param;
	if (cpufreq_quick_get(0) <= CUSTOM_CPUFREQ_THRESH) {
		param.sched_priority = 0;
		setScheduler(current, SCHED_NORMAL, &param);
	} else {
		if (get_scheduler_policy(current) != SCHED_FIFO) {
			param.sched_priority = (prio < MAX_RT_PRIO)? prio : (MAX_RT_PRIO-1);
			setScheduler(current, SCHED_FIFO, &param);
		}
	}
}
#endif /* ENABLE_ADAPTIVE_SCHED */
#ifdef DEBUG_CPU_FREQ
static int dhd_cpufreq_notifier(struct notifier_block *nb, unsigned long val, void *data)
{
	dhd_info_t *dhd = container_of(nb, struct dhd_info, freq_trans);
	struct cpufreq_freqs *freq = data;
	if (dhd) {
		if (!dhd->new_freq)
			goto exit;
		if (val == CPUFREQ_POSTCHANGE) {
			DHD_ERROR(("cpu freq is changed to %u kHZ on CPU %d\n",
				freq->new, freq->cpu));
			*per_cpu_ptr(dhd->new_freq, freq->cpu) = freq->new;
		}
	}
exit:
	return 0;
}
#endif /* DEBUG_CPU_FREQ */

static int
dhd_dpc_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;

	/* This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */
	if (dhd_dpc_prio > 0)
	{
		struct sched_param param;
		param.sched_priority = (dhd_dpc_prio < MAX_RT_PRIO)?dhd_dpc_prio:(MAX_RT_PRIO-1);
		setScheduler(current, SCHED_FIFO, &param);
	}

#ifdef CUSTOM_DPC_CPUCORE
	set_cpus_allowed_ptr(current, cpumask_of(CUSTOM_DPC_CPUCORE));
#endif
#ifdef CUSTOM_SET_CPUCORE
	dhd->pub.current_dpc = current;
#endif /* CUSTOM_SET_CPUCORE */
	/* Run until signal received */
	while (1) {
		if (dhd->pub.conf->dpc_cpucore >= 0) {
			printf("%s: set dpc_cpucore %d\n", __FUNCTION__, dhd->pub.conf->dpc_cpucore);
			set_cpus_allowed_ptr(current, cpumask_of(dhd->pub.conf->dpc_cpucore));
			dhd->pub.conf->dpc_cpucore = -1;
		}
		if (!binary_sema_down(tsk)) {
#ifdef ENABLE_ADAPTIVE_SCHED
			dhd_sched_policy(dhd_dpc_prio);
#endif /* ENABLE_ADAPTIVE_SCHED */
			SMP_RD_BARRIER_DEPENDS();
			if (tsk->terminated) {
				break;
			}

			/* Call bus dpc unless it indicated down (then clean stop) */
			if (dhd->pub.busstate != DHD_BUS_DOWN) {
#ifdef DEBUG_DPC_THREAD_WATCHDOG
				int resched_cnt = 0;
#endif /* DEBUG_DPC_THREAD_WATCHDOG */
				dhd_os_wd_timer_extend(&dhd->pub, TRUE);
				while (dhd_bus_dpc(dhd->pub.bus)) {
					/* process all data */
#ifdef DEBUG_DPC_THREAD_WATCHDOG
					resched_cnt++;
					if (resched_cnt > MAX_RESCHED_CNT) {
						DHD_INFO(("%s Calling msleep to"
							"let other processes run. \n",
							__FUNCTION__));
						dhd->pub.dhd_bug_on = true;
						resched_cnt = 0;
						OSL_SLEEP(1);
					}
#endif /* DEBUG_DPC_THREAD_WATCHDOG */
				}
				dhd_os_wd_timer_extend(&dhd->pub, FALSE);
				DHD_OS_WAKE_UNLOCK(&dhd->pub);
			} else {
				if (dhd->pub.up)
					dhd_bus_stop(dhd->pub.bus, TRUE);
				DHD_OS_WAKE_UNLOCK(&dhd->pub);
			}
		} else {
			break;
		}
	}
	complete_and_exit(&tsk->completed, 0);
}

static int
dhd_rxf_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;
#if defined(WAIT_DEQUEUE)
#define RXF_WATCHDOG_TIME 250 /* BARK_TIME(1000) /  */
	ulong watchdogTime = OSL_SYSUPTIME(); /* msec */
#endif
	dhd_pub_t *pub = &dhd->pub;

	/* This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */
	if (dhd_rxf_prio > 0)
	{
		struct sched_param param;
		param.sched_priority = (dhd_rxf_prio < MAX_RT_PRIO)?dhd_rxf_prio:(MAX_RT_PRIO-1);
		setScheduler(current, SCHED_FIFO, &param);
	}

#ifdef CUSTOM_SET_CPUCORE
	dhd->pub.current_rxf = current;
#endif /* CUSTOM_SET_CPUCORE */
	/* Run until signal received */
	while (1) {
		if (dhd->pub.conf->rxf_cpucore >= 0) {
			printf("%s: set rxf_cpucore %d\n", __FUNCTION__, dhd->pub.conf->rxf_cpucore);
			set_cpus_allowed_ptr(current, cpumask_of(dhd->pub.conf->rxf_cpucore));
			dhd->pub.conf->rxf_cpucore = -1;
		}
		if (down_interruptible(&tsk->sema) == 0) {
			void *skb;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
			ulong flags;
#endif
#ifdef ENABLE_ADAPTIVE_SCHED
			dhd_sched_policy(dhd_rxf_prio);
#endif /* ENABLE_ADAPTIVE_SCHED */

			SMP_RD_BARRIER_DEPENDS();

			if (tsk->terminated) {
				break;
			}
			skb = dhd_rxf_dequeue(pub);

			if (skb == NULL) {
				continue;
			}
			while (skb) {
				void *skbnext = PKTNEXT(pub->osh, skb);
				PKTSETNEXT(pub->osh, skb, NULL);
				bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE,
					__FUNCTION__, __LINE__);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
				netif_rx_ni(skb);
#else
				netif_rx(skb);
				local_irq_save(flags);
				RAISE_RX_SOFTIRQ();
				local_irq_restore(flags);

#endif
				skb = skbnext;
			}
#if defined(WAIT_DEQUEUE)
			if (OSL_SYSUPTIME() - watchdogTime > RXF_WATCHDOG_TIME) {
				OSL_SLEEP(1);
				watchdogTime = OSL_SYSUPTIME();
			}
#endif

			DHD_OS_WAKE_UNLOCK(pub);
		} else {
			break;
		}
	}
	complete_and_exit(&tsk->completed, 0);
}

#ifdef BCMPCIE
void dhd_dpc_enable(dhd_pub_t *dhdp)
{
#if defined(DHD_LB_RXP) || defined(DHD_LB_TXP)
	dhd_info_t *dhd;

	if (!dhdp || !dhdp->info)
		return;
	dhd = dhdp->info;
#endif /* DHD_LB_RXP || DHD_LB_TXP */

#ifdef DHD_LB_RXP
	__skb_queue_head_init(&dhd->rx_pend_queue);
#endif /* DHD_LB_RXP */

#ifdef DHD_LB_TXP
	skb_queue_head_init(&dhd->tx_pend_queue);
#endif /* DHD_LB_TXP */
}
#endif /* BCMPCIE */

#ifdef BCMPCIE
void
dhd_dpc_kill(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;

	if (!dhdp) {
		return;
	}

	dhd = dhdp->info;

	if (!dhd) {
		return;
	}

	if (dhd->thr_dpc_ctl.thr_pid < 0) {
		tasklet_kill(&dhd->tasklet);
		DHD_ERROR(("%s: tasklet disabled\n", __FUNCTION__));
	}

#ifdef DHD_LB
#ifdef DHD_LB_RXP
	cancel_work_sync(&dhd->rx_napi_dispatcher_work);
	__skb_queue_purge(&dhd->rx_pend_queue);
#endif /* DHD_LB_RXP */
#ifdef DHD_LB_TXP
	cancel_work_sync(&dhd->tx_dispatcher_work);
	skb_queue_purge(&dhd->tx_pend_queue);
#endif /* DHD_LB_TXP */

	/* Kill the Load Balancing Tasklets */
#if defined(DHD_LB_TXC)
	tasklet_kill(&dhd->tx_compl_tasklet);
#endif /* DHD_LB_TXC */
#if defined(DHD_LB_RXC)
	tasklet_kill(&dhd->rx_compl_tasklet);
#endif /* DHD_LB_RXC */
#if defined(DHD_LB_TXP)
	tasklet_kill(&dhd->tx_tasklet);
#endif /* DHD_LB_TXP */
#endif /* DHD_LB */
}

void
dhd_dpc_tasklet_kill(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;

	if (!dhdp) {
		return;
	}

	dhd = dhdp->info;

	if (!dhd) {
		return;
	}

	if (dhd->thr_dpc_ctl.thr_pid < 0) {
		tasklet_kill(&dhd->tasklet);
	}
}
#endif /* BCMPCIE */

static void
dhd_dpc(ulong data)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)data;

	/* this (tasklet) can be scheduled in dhd_sched_dpc[dhd_linux.c]
	 * down below , wake lock is set,
	 * the tasklet is initialized in dhd_attach()
	 */
	/* Call bus dpc unless it indicated down (then clean stop) */
	if (dhd->pub.busstate != DHD_BUS_DOWN) {
#if defined(DHD_LB_STATS) && defined(PCIE_FULL_DONGLE)
		DHD_LB_STATS_INCR(dhd->dhd_dpc_cnt);
#endif /* DHD_LB_STATS && PCIE_FULL_DONGLE */
		if (dhd_bus_dpc(dhd->pub.bus)) {
			tasklet_schedule(&dhd->tasklet);
		}
	} else {
		dhd_bus_stop(dhd->pub.bus, TRUE);
	}
}

void
dhd_sched_dpc(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

	if (dhd->thr_dpc_ctl.thr_pid >= 0) {
		DHD_OS_WAKE_LOCK(dhdp);
		/* If the semaphore does not get up,
		* wake unlock should be done here
		*/
		if (!binary_sema_up(&dhd->thr_dpc_ctl)) {
			DHD_OS_WAKE_UNLOCK(dhdp);
		}
		return;
	} else {
		tasklet_schedule(&dhd->tasklet);
	}
}
#endif /* BCMDBUS */

static void
dhd_sched_rxf(dhd_pub_t *dhdp, void *skb)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
#ifdef RXF_DEQUEUE_ON_BUSY
	int ret = BCME_OK;
	int retry = 2;
#endif /* RXF_DEQUEUE_ON_BUSY */

	DHD_OS_WAKE_LOCK(dhdp);

	DHD_TRACE(("dhd_sched_rxf: Enter\n"));
#ifdef RXF_DEQUEUE_ON_BUSY
	do {
		ret = dhd_rxf_enqueue(dhdp, skb);
		if (ret == BCME_OK || ret == BCME_ERROR)
			break;
		else
			OSL_SLEEP(50); /* waiting for dequeueing */
	} while (retry-- > 0);

	if (retry <= 0 && ret == BCME_BUSY) {
		void *skbp = skb;

		while (skbp) {
			void *skbnext = PKTNEXT(dhdp->osh, skbp);
			PKTSETNEXT(dhdp->osh, skbp, NULL);
			bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE,
				__FUNCTION__, __LINE__);
			netif_rx_ni(skbp);
			skbp = skbnext;
		}
		DHD_ERROR(("send skb to kernel backlog without rxf_thread\n"));
	} else {
		if (dhd->thr_rxf_ctl.thr_pid >= 0) {
			up(&dhd->thr_rxf_ctl.sema);
		}
	}
#else /* RXF_DEQUEUE_ON_BUSY */
	do {
		if (dhd_rxf_enqueue(dhdp, skb) == BCME_OK)
			break;
	} while (1);
	if (dhd->thr_rxf_ctl.thr_pid >= 0) {
		up(&dhd->thr_rxf_ctl.sema);
	}
	return;
#endif /* RXF_DEQUEUE_ON_BUSY */
}

#if defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW)
#endif /* defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW) */

#ifdef TOE
/* Retrieve current toe component enables, which are kept as a bitmap in toe_ol iovar */
static int
dhd_toe_get(dhd_info_t *dhd, int ifidx, uint32 *toe_ol)
{
	char buf[32];
	int ret;

	ret = dhd_iovar(&dhd->pub, ifidx, "toe_ol", NULL, 0, (char *)&buf, sizeof(buf), FALSE);

	if (ret < 0) {
		if (ret == -EIO) {
			DHD_ERROR(("%s: toe not supported by device\n", dhd_ifname(&dhd->pub,
				ifidx)));
			return -EOPNOTSUPP;
		}

		DHD_INFO(("%s: could not get toe_ol: ret=%d\n", dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	memcpy(toe_ol, buf, sizeof(uint32));
	return 0;
}

/* Set current toe component enables in toe_ol iovar, and set toe global enable iovar */
static int
dhd_toe_set(dhd_info_t *dhd, int ifidx, uint32 toe_ol)
{
	int toe, ret;

	/* Set toe_ol as requested */
	ret = dhd_iovar(&dhd->pub, ifidx, "toe_ol", (char *)&toe_ol, sizeof(toe_ol), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: could not set toe_ol: ret=%d\n",
			dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	/* Enable toe globally only if any components are enabled. */
	toe = (toe_ol != 0);
	ret = dhd_iovar(&dhd->pub, ifidx, "toe", (char *)&toe, sizeof(toe), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: could not set toe: ret=%d\n", dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	return 0;
}
#endif /* TOE */

#if defined(WL_CFG80211) && defined(NUM_SCB_MAX_PROBE)
void dhd_set_scb_probe(dhd_pub_t *dhd)
{
	wl_scb_probe_t scb_probe;
	int ret;

	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		return;
	}

	ret = dhd_iovar(dhd, 0, "scb_probe", NULL, 0,
			(char *)&scb_probe, sizeof(scb_probe), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s: GET max_scb_probe failed\n", __FUNCTION__));
	}

	scb_probe.scb_max_probe = NUM_SCB_MAX_PROBE;

	ret = dhd_iovar(dhd, 0, "scb_probe", (char *)&scb_probe, sizeof(scb_probe),
			NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: max_scb_probe setting failed\n", __FUNCTION__));
		return;
	}
}
#endif /* WL_CFG80211 && NUM_SCB_MAX_PROBE */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
static void
dhd_ethtool_get_drvinfo(struct net_device *net, struct ethtool_drvinfo *info)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);

	snprintf(info->driver, sizeof(info->driver), "wl");
	snprintf(info->version, sizeof(info->version), "%lu", dhd->pub.drv_version);
}

struct ethtool_ops dhd_ethtool_ops = {
	.get_drvinfo = dhd_ethtool_get_drvinfo
};
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) */


#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 2)
static int
dhd_ethtool(dhd_info_t *dhd, void *uaddr)
{
	struct ethtool_drvinfo info;
	char drvname[sizeof(info.driver)];
	uint32 cmd;
#ifdef TOE
	struct ethtool_value edata;
	uint32 toe_cmpnt, csum_dir;
	int ret;
#endif

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* all ethtool calls start with a cmd word */
	if (copy_from_user(&cmd, uaddr, sizeof (uint32)))
		return -EFAULT;

	switch (cmd) {
	case ETHTOOL_GDRVINFO:
		/* Copy out any request driver name */
		if (copy_from_user(&info, uaddr, sizeof(info)))
			return -EFAULT;
		strncpy(drvname, info.driver, sizeof(drvname));
		drvname[sizeof(info.driver)-1] = '\0';

		/* clear struct for return */
		memset(&info, 0, sizeof(info));
		info.cmd = cmd;

		/* if dhd requested, identify ourselves */
		if (strcmp(drvname, "?dhd") == 0) {
			snprintf(info.driver, sizeof(info.driver), "dhd");
			strncpy(info.version, EPI_VERSION_STR, sizeof(info.version) - 1);
			info.version[sizeof(info.version) - 1] = '\0';
		}

		/* otherwise, require dongle to be up */
		else if (!dhd->pub.up) {
			DHD_ERROR(("%s: dongle is not up\n", __FUNCTION__));
			return -ENODEV;
		}

		/* finally, report dongle driver type */
		else if (dhd->pub.iswl)
			snprintf(info.driver, sizeof(info.driver), "wl");
		else
			snprintf(info.driver, sizeof(info.driver), "xx");

		snprintf(info.version, sizeof(info.version), "%lu", dhd->pub.drv_version);
		if (copy_to_user(uaddr, &info, sizeof(info)))
			return -EFAULT;
		DHD_CTL(("%s: given %*s, returning %s\n", __FUNCTION__,
		         (int)sizeof(drvname), drvname, info.driver));
		break;

#ifdef TOE
	/* Get toe offload components from dongle */
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_GTXCSUM:
		if ((ret = dhd_toe_get(dhd, 0, &toe_cmpnt)) < 0)
			return ret;

		csum_dir = (cmd == ETHTOOL_GTXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		edata.cmd = cmd;
		edata.data = (toe_cmpnt & csum_dir) ? 1 : 0;

		if (copy_to_user(uaddr, &edata, sizeof(edata)))
			return -EFAULT;
		break;

	/* Set toe offload components in dongle */
	case ETHTOOL_SRXCSUM:
	case ETHTOOL_STXCSUM:
		if (copy_from_user(&edata, uaddr, sizeof(edata)))
			return -EFAULT;

		/* Read the current settings, update and write back */
		if ((ret = dhd_toe_get(dhd, 0, &toe_cmpnt)) < 0)
			return ret;

		csum_dir = (cmd == ETHTOOL_STXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		if (edata.data != 0)
			toe_cmpnt |= csum_dir;
		else
			toe_cmpnt &= ~csum_dir;

		if ((ret = dhd_toe_set(dhd, 0, toe_cmpnt)) < 0)
			return ret;

		/* If setting TX checksum mode, tell Linux the new mode */
		if (cmd == ETHTOOL_STXCSUM) {
			if (edata.data)
				dhd->iflist[0]->net->features |= NETIF_F_IP_CSUM;
			else
				dhd->iflist[0]->net->features &= ~NETIF_F_IP_CSUM;
		}

		break;
#endif /* TOE */

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 2) */

static bool dhd_check_hang(struct net_device *net, dhd_pub_t *dhdp, int error)
{
	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return FALSE;
	}

	if (!dhdp->up)
		return FALSE;

#if !defined(BCMPCIE) && !defined(BCMDBUS)
	if (dhdp->info->thr_dpc_ctl.thr_pid < 0) {
		DHD_ERROR(("%s : skipped due to negative pid - unloading?\n", __FUNCTION__));
		return FALSE;
	}
#endif /* !BCMPCIE && !BCMDBUS */

	if ((error == -ETIMEDOUT) || (error == -EREMOTEIO) ||
		((dhdp->busstate == DHD_BUS_DOWN) && (!dhdp->dongle_reset))) {
#ifdef BCMPCIE
		DHD_ERROR(("%s: Event HANG send up due to  re=%d te=%d d3acke=%d e=%d s=%d\n",
			__FUNCTION__, dhdp->rxcnt_timeout, dhdp->txcnt_timeout,
			dhdp->d3ackcnt_timeout, error, dhdp->busstate));
#else
		DHD_ERROR(("%s: Event HANG send up due to  re=%d te=%d e=%d s=%d\n", __FUNCTION__,
			dhdp->rxcnt_timeout, dhdp->txcnt_timeout, error, dhdp->busstate));
#endif /* BCMPCIE */
		if (dhdp->hang_reason == 0) {
			if (dhdp->dongle_trap_occured) {
				dhdp->hang_reason = HANG_REASON_DONGLE_TRAP;
#ifdef BCMPCIE
			} else if (dhdp->d3ackcnt_timeout) {
				dhdp->hang_reason = HANG_REASON_D3_ACK_TIMEOUT;
#endif /* BCMPCIE */
			} else {
				dhdp->hang_reason = HANG_REASON_IOCTL_RESP_TIMEOUT;
			}
		}
		printf("%s\n", info_string);
		net_os_send_hang_message(net);
		return TRUE;
	}
	return FALSE;
}

#ifdef WL_MONITOR
bool
dhd_monitor_enabled(dhd_pub_t *dhd, int ifidx)
{
	return (dhd->info->monitor_type != 0);
}

void
dhd_rx_mon_pkt(dhd_pub_t *dhdp, host_rxbuf_cmpl_t* msg, void *pkt, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
#ifdef HOST_RADIOTAP_CONV
	uint16 len = 0, offset = 0;
	monitor_pkt_info_t pkt_info;
	memcpy(&pkt_info.marker, &msg->marker, sizeof(msg->marker));
	memcpy(&pkt_info.ts, &msg->ts, sizeof(monitor_pkt_ts_t));

	if (!dhd->monitor_skb) {
		if ((dhd->monitor_skb = dev_alloc_skb(MAX_MON_PKT_SIZE)) == NULL)
			return;
	}

	len = bcmwifi_monitor(dhd->monitor_info, &pkt_info, PKTDATA(dhdp->osh, pkt),
		PKTLEN(dhdp->osh, pkt), PKTDATA(dhdp->osh, dhd->monitor_skb), &offset);

	if (dhd->monitor_type && dhd->monitor_dev)
		dhd->monitor_skb->dev = dhd->monitor_dev;
	else {
		PKTFREE(dhdp->osh, pkt, FALSE);
		dev_kfree_skb(dhd->monitor_skb);
		return;
	}

	PKTFREE(dhdp->osh, pkt, FALSE);

	if (!len) {
		return;
	}

	skb_put(dhd->monitor_skb, len);
	skb_pull(dhd->monitor_skb, offset);

	dhd->monitor_skb->protocol = eth_type_trans(dhd->monitor_skb, dhd->monitor_skb->dev);
#else
	uint8 amsdu_flag = (msg->flags & BCMPCIE_PKT_FLAGS_MONITOR_MASK) >>
		BCMPCIE_PKT_FLAGS_MONITOR_SHIFT;
	switch (amsdu_flag) {
		case BCMPCIE_PKT_FLAGS_MONITOR_NO_AMSDU:
		default:
			if (!dhd->monitor_skb) {
				if ((dhd->monitor_skb = PKTTONATIVE(dhdp->osh, pkt)) == NULL)
					return;
			}

			if (dhd->monitor_type && dhd->monitor_dev)
				dhd->monitor_skb->dev = dhd->monitor_dev;
			else {
				PKTFREE(dhdp->osh, pkt, FALSE);
				dhd->monitor_skb = NULL;
				return;
			}

			dhd->monitor_skb->protocol =
				eth_type_trans(dhd->monitor_skb, dhd->monitor_skb->dev);
			dhd->monitor_len = 0;
			break;
		case BCMPCIE_PKT_FLAGS_MONITOR_FIRST_PKT:
			if (!dhd->monitor_skb) {
				if ((dhd->monitor_skb = dev_alloc_skb(MAX_MON_PKT_SIZE)) == NULL)
					return;
				dhd->monitor_len = 0;
			}
			if (dhd->monitor_type && dhd->monitor_dev)
				dhd->monitor_skb->dev = dhd->monitor_dev;
			else {
				PKTFREE(dhdp->osh, pkt, FALSE);
				dev_kfree_skb(dhd->monitor_skb);
				return;
			}
			memcpy(PKTDATA(dhdp->osh, dhd->monitor_skb),
				PKTDATA(dhdp->osh, pkt), PKTLEN(dhdp->osh, pkt));

			dhd->monitor_len = PKTLEN(dhdp->osh, pkt);
			PKTFREE(dhdp->osh, pkt, FALSE);
			return;
		case BCMPCIE_PKT_FLAGS_MONITOR_INTER_PKT:
			memcpy(PKTDATA(dhdp->osh, dhd->monitor_skb) + dhd->monitor_len,
				PKTDATA(dhdp->osh, pkt), PKTLEN(dhdp->osh, pkt));
			dhd->monitor_len += PKTLEN(dhdp->osh, pkt);

			PKTFREE(dhdp->osh, pkt, FALSE);
			return;
		case BCMPCIE_PKT_FLAGS_MONITOR_LAST_PKT:
			memcpy(PKTDATA(dhdp->osh, dhd->monitor_skb) + dhd->monitor_len,
				PKTDATA(dhdp->osh, pkt), PKTLEN(dhdp->osh, pkt));
			dhd->monitor_len += PKTLEN(dhdp->osh, pkt);

			PKTFREE(dhdp->osh, pkt, FALSE);
			skb_put(dhd->monitor_skb, dhd->monitor_len);
			dhd->monitor_skb->protocol =
				eth_type_trans(dhd->monitor_skb, dhd->monitor_skb->dev);
			dhd->monitor_len = 0;
			break;
	}

#endif /* HOST_RADIOTAP_CONV */
	if (in_interrupt()) {
		bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE,
			__FUNCTION__, __LINE__);
		DHD_PERIM_UNLOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
		netif_rx(dhd->monitor_skb);
		DHD_PERIM_LOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
	} else {
		/* If the receive is not processed inside an ISR,
		 * the softirqd must be woken explicitly to service
		 * the NET_RX_SOFTIRQ.	In 2.6 kernels, this is handled
		 * by netif_rx_ni(), but in earlier kernels, we need
		 * to do it manually.
		 */
		bcm_object_trace_opr(dhd->monitor_skb, BCM_OBJDBG_REMOVE,
			__FUNCTION__, __LINE__);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
		DHD_PERIM_UNLOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
		netif_rx_ni(dhd->monitor_skb);
		DHD_PERIM_LOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
#else
		ulong flags;
		DHD_PERIM_UNLOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
		netif_rx(dhd->monitor_skb);
		DHD_PERIM_LOCK_ALL((dhd->fwder_unit % FWDER_MAX_UNIT));
		local_irq_save(flags);
		RAISE_RX_SOFTIRQ();
		local_irq_restore(flags);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) */
	}

	dhd->monitor_skb = NULL;
}

typedef struct dhd_mon_dev_priv {
	struct net_device_stats stats;
} dhd_mon_dev_priv_t;

#define DHD_MON_DEV_PRIV_SIZE		(sizeof(dhd_mon_dev_priv_t))
#define DHD_MON_DEV_PRIV(dev)		((dhd_mon_dev_priv_t *)DEV_PRIV(dev))
#define DHD_MON_DEV_STATS(dev)		(((dhd_mon_dev_priv_t *)DEV_PRIV(dev))->stats)

static int
dhd_monitor_start(struct sk_buff *skb, struct net_device *dev)
{
	PKTFREE(NULL, skb, FALSE);
	return 0;
}

static int
dhd_monitor_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	return 0;
}

static struct net_device_stats*
dhd_monitor_get_stats(struct net_device *dev)
{
	return &DHD_MON_DEV_STATS(dev);
}

static const struct net_device_ops netdev_monitor_ops =
{
	.ndo_start_xmit = dhd_monitor_start,
	.ndo_get_stats = dhd_monitor_get_stats,
	.ndo_do_ioctl = dhd_monitor_ioctl
};

static void
dhd_add_monitor_if(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	struct net_device *dev;
	char *devname;

	if (event != DHD_WQ_WORK_IF_ADD) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	dev = alloc_etherdev(DHD_MON_DEV_PRIV_SIZE);
	if (!dev) {
		DHD_ERROR(("%s: alloc wlif failed\n", __FUNCTION__));
		return;
	}

	devname = "radiotap";

	snprintf(dev->name, sizeof(dev->name), "%s%u", devname, dhd->unit);

#ifndef ARPHRD_IEEE80211_PRISM  /* From Linux 2.4.18 */
#define ARPHRD_IEEE80211_PRISM 802
#endif

#ifndef ARPHRD_IEEE80211_RADIOTAP
#define ARPHRD_IEEE80211_RADIOTAP	803 /* IEEE 802.11 + radiotap header */
#endif /* ARPHRD_IEEE80211_RADIOTAP */

	dev->type = ARPHRD_IEEE80211_RADIOTAP;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
	dev->hard_start_xmit = dhd_monitor_start;
	dev->do_ioctl = dhd_monitor_ioctl;
	dev->get_stats = dhd_monitor_get_stats;
#else
	dev->netdev_ops = &netdev_monitor_ops;
#endif

	if (register_netdev(dev)) {
		DHD_ERROR(("%s, register_netdev failed for %s\n",
			__FUNCTION__, dev->name));
		free_netdev(dev);
	}

	bcmwifi_monitor_create(&dhd->monitor_info);
	dhd->monitor_dev = dev;
}

static void
dhd_del_monitor_if(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;

	if (event != DHD_WQ_WORK_IF_DEL) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	if (dhd->monitor_dev) {
		unregister_netdev(dhd->monitor_dev);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
		MFREE(dhd->osh, dhd->monitor_dev->priv, DHD_MON_DEV_PRIV_SIZE);
		MFREE(dhd->osh, dhd->monitor_dev, sizeof(struct net_device));
#else
		free_netdev(dhd->monitor_dev);
#endif /* 2.6.24 */

		dhd->monitor_dev = NULL;
	}

	if (dhd->monitor_info) {
		bcmwifi_monitor_delete(dhd->monitor_info);
		dhd->monitor_info = NULL;
	}
}

static void
dhd_set_monitor(dhd_pub_t *dhd, int ifidx, int val)
{
	dhd_info_t *info = dhd->info;

	DHD_TRACE(("%s: val %d\n", __FUNCTION__, val));
	if ((val && info->monitor_dev) || (!val && !info->monitor_dev)) {
		DHD_ERROR(("%s: Mismatched params, return\n", __FUNCTION__));
		return;
	}

	/* Delete monitor */
	if (!val) {
		info->monitor_type = val;
		dhd_deferred_schedule_work(info->dhd_deferred_wq, NULL, DHD_WQ_WORK_IF_DEL,
			dhd_del_monitor_if, DHD_WQ_WORK_PRIORITY_LOW);
		return;
	}

	/* Add monitor */
	info->monitor_type = val;
	dhd_deferred_schedule_work(info->dhd_deferred_wq, NULL, DHD_WQ_WORK_IF_ADD,
		dhd_add_monitor_if, DHD_WQ_WORK_PRIORITY_LOW);
}
#endif /* WL_MONITOR */

int dhd_ioctl_process(dhd_pub_t *pub, int ifidx, dhd_ioctl_t *ioc, void *data_buf)
{
	int bcmerror = BCME_OK;
	int buflen = 0;
	struct net_device *net;

#ifdef REPORT_FATAL_TIMEOUTS
	if (ioc->cmd == WLC_SET_WPA_AUTH) {
		int wpa_auth;

		wpa_auth = *((int *)ioc->buf);
		DHD_INFO(("wpa_auth:%d\n", wpa_auth));
		if (wpa_auth != WPA_AUTH_DISABLED) {
			/* If AP is with security then enable WLC_E_PSK_SUP event checking */
			dhd_set_join_error(pub, WLC_WPA_MASK);
		} else {
			/* If AP is with open then disable WLC_E_PSK_SUP event checking */
			dhd_clear_join_error(pub, WLC_WPA_MASK);
		}
	}

	if (ioc->cmd == WLC_SET_AUTH) {
		int auth;
		auth = *((int *)ioc->buf);
		DHD_INFO(("Auth:%d\n", auth));

		if (auth != WL_AUTH_OPEN_SYSTEM) {
			/* If AP is with security then enable WLC_E_PSK_SUP event checking */
			dhd_set_join_error(pub, WLC_WPA_MASK);
		} else {
			/* If AP is with open then disable WLC_E_PSK_SUP event checking */
			dhd_clear_join_error(pub, WLC_WPA_MASK);
		}
	}
#endif /* REPORT_FATAL_TIMEOUTS */
	net = dhd_idx2net(pub, ifidx);
	if (!net) {
		bcmerror = BCME_BADARG;
		goto done;
	}

	/* check for local dhd ioctl and handle it */
	if (ioc->driver == DHD_IOCTL_MAGIC) {
		/* This is a DHD IOVAR, truncate buflen to DHD_IOCTL_MAXLEN */
		if (data_buf)
			buflen = MIN(ioc->len, DHD_IOCTL_MAXLEN);
		bcmerror = dhd_ioctl((void *)pub, ioc, data_buf, buflen);
		if (bcmerror)
			pub->bcmerror = bcmerror;
		goto done;
	}

	/* This is a WL IOVAR, truncate buflen to WLC_IOCTL_MAXLEN */
	if (data_buf)
		buflen = MIN(ioc->len, WLC_IOCTL_MAXLEN);

#ifndef BCMDBUS
	/* send to dongle (must be up, and wl). */
	if (pub->busstate == DHD_BUS_DOWN || pub->busstate == DHD_BUS_LOAD) {
		if ((!pub->dongle_trap_occured) && allow_delay_fwdl) {
			int ret;
			if (atomic_read(&exit_in_progress)) {
				DHD_ERROR(("%s module exit in progress\n", __func__));
				bcmerror = BCME_DONGLE_DOWN;
				goto done;
			}
			ret = dhd_bus_start(pub);
			if (ret != 0) {
				DHD_ERROR(("%s: failed with code %d\n", __FUNCTION__, ret));
				bcmerror = BCME_DONGLE_DOWN;
				goto done;
			}
		} else {
			bcmerror = BCME_DONGLE_DOWN;
			goto done;
		}
	}

	if (!pub->iswl) {
		bcmerror = BCME_DONGLE_DOWN;
		goto done;
	}
#endif /* !BCMDBUS */

	/*
	 * Flush the TX queue if required for proper message serialization:
	 * Intercept WLC_SET_KEY IOCTL - serialize M4 send and set key IOCTL to
	 * prevent M4 encryption and
	 * intercept WLC_DISASSOC IOCTL - serialize WPS-DONE and WLC_DISASSOC IOCTL to
	 * prevent disassoc frame being sent before WPS-DONE frame.
	 */
	if (ioc->cmd == WLC_SET_KEY ||
	    (ioc->cmd == WLC_SET_VAR && data_buf != NULL &&
	     strncmp("wsec_key", data_buf, 9) == 0) ||
	    (ioc->cmd == WLC_SET_VAR && data_buf != NULL &&
	     strncmp("bsscfg:wsec_key", data_buf, 15) == 0) ||
	    ioc->cmd == WLC_DISASSOC)
		dhd_wait_pend8021x(net);

#ifdef WLMEDIA_HTSF
	if (data_buf) {
		/*  short cut wl ioctl calls here  */
		if (strcmp("htsf", data_buf) == 0) {
			dhd_ioctl_htsf_get(dhd, 0);
			return BCME_OK;
		}

		if (strcmp("htsflate", data_buf) == 0) {
			if (ioc->set) {
				memset(ts, 0, sizeof(tstamp_t)*TSMAX);
				memset(&maxdelayts, 0, sizeof(tstamp_t));
				maxdelay = 0;
				tspktcnt = 0;
				maxdelaypktno = 0;
				memset(&vi_d1.bin, 0, sizeof(uint32)*NUMBIN);
				memset(&vi_d2.bin, 0, sizeof(uint32)*NUMBIN);
				memset(&vi_d3.bin, 0, sizeof(uint32)*NUMBIN);
				memset(&vi_d4.bin, 0, sizeof(uint32)*NUMBIN);
			} else {
				dhd_dump_latency();
			}
			return BCME_OK;
		}
		if (strcmp("htsfclear", data_buf) == 0) {
			memset(&vi_d1.bin, 0, sizeof(uint32)*NUMBIN);
			memset(&vi_d2.bin, 0, sizeof(uint32)*NUMBIN);
			memset(&vi_d3.bin, 0, sizeof(uint32)*NUMBIN);
			memset(&vi_d4.bin, 0, sizeof(uint32)*NUMBIN);
			htsf_seqnum = 0;
			return BCME_OK;
		}
		if (strcmp("htsfhis", data_buf) == 0) {
			dhd_dump_htsfhisto(&vi_d1, "H to D");
			dhd_dump_htsfhisto(&vi_d2, "D to D");
			dhd_dump_htsfhisto(&vi_d3, "D to H");
			dhd_dump_htsfhisto(&vi_d4, "H to H");
			return BCME_OK;
		}
		if (strcmp("tsport", data_buf) == 0) {
			if (ioc->set) {
				memcpy(&tsport, data_buf + 7, 4);
			} else {
				DHD_ERROR(("current timestamp port: %d \n", tsport));
			}
			return BCME_OK;
		}
	}
#endif /* WLMEDIA_HTSF */

	if ((ioc->cmd == WLC_SET_VAR || ioc->cmd == WLC_GET_VAR) &&
		data_buf != NULL && strncmp("rpc_", data_buf, 4) == 0) {
#ifdef BCM_FD_AGGR
		bcmerror = dhd_fdaggr_ioctl(pub, ifidx, (wl_ioctl_t *)ioc, data_buf, buflen);
#else
		bcmerror = BCME_UNSUPPORTED;
#endif
		goto done;
	}
	bcmerror = dhd_wl_ioctl(pub, ifidx, (wl_ioctl_t *)ioc, data_buf, buflen);

#ifdef WL_MONITOR
	/* Intercept monitor ioctl here, add/del monitor if */
	if (bcmerror == BCME_OK && ioc->cmd == WLC_SET_MONITOR) {
		dhd_set_monitor(pub, ifidx, *(int32*)data_buf);
	}
#endif

#ifdef REPORT_FATAL_TIMEOUTS
	if (ioc->cmd == WLC_SCAN && bcmerror == 0) {
		dhd_start_scan_timer(pub);
	}
	if (ioc->cmd == WLC_SET_SSID && bcmerror == 0) {
		dhd_start_join_timer(pub);
	}
#endif  /* REPORT_FATAL_TIMEOUTS */

done:
	dhd_check_hang(net, pub, bcmerror);

	return bcmerror;
}

static int
dhd_ioctl_entry(struct net_device *net, struct ifreq *ifr, int cmd)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_ioctl_t ioc;
	int bcmerror = 0;
	int ifidx;
	int ret;
	void *local_buf = NULL;
	void __user *ioc_buf_user = NULL;
	u16 buflen = 0;

	if (atomic_read(&exit_in_progress)) {
		DHD_ERROR(("%s module exit in progress\n", __func__));
		bcmerror = BCME_DONGLE_DOWN;
		return OSL_ERROR(bcmerror);
	}

	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

	/* Interface up check for built-in type */
	if (!dhd_download_fw_on_driverload && dhd->pub.up == FALSE) {
		DHD_ERROR(("%s: Interface is down \n", __FUNCTION__));
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return OSL_ERROR(BCME_NOTUP);
	}

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d, cmd 0x%04x\n", __FUNCTION__, ifidx, cmd));

	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: BAD IF\n", __FUNCTION__));
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -1;
	}

#if defined(WL_WIRELESS_EXT)
	/* linux wireless extensions */
	if ((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST)) {
		/* may recurse, do NOT lock */
		ret = wl_iw_ioctl(net, ifr, cmd);
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}
#endif /* defined(WL_WIRELESS_EXT) */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 2)
	if (cmd == SIOCETHTOOL) {
		ret = dhd_ethtool(dhd, (void*)ifr->ifr_data);
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 2) */

	if (cmd == SIOCDEVPRIVATE+1) {
		ret = wl_android_priv_cmd(net, ifr, cmd);
		dhd_check_hang(net, &dhd->pub, ret);
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}

	if (cmd != SIOCDEVPRIVATE) {
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -EOPNOTSUPP;
	}

	memset(&ioc, 0, sizeof(ioc));

#ifdef CONFIG_COMPAT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0))
	if (in_compat_syscall())
#else
	if (is_compat_task())
#endif
	{
		compat_wl_ioctl_t compat_ioc;
		if (copy_from_user(&compat_ioc, ifr->ifr_data, sizeof(compat_wl_ioctl_t))) {
			bcmerror = BCME_BADADDR;
			goto done;
		}
		ioc.cmd = compat_ioc.cmd;
		if (ioc.cmd & WLC_SPEC_FLAG) {
			memset(&ioc, 0, sizeof(ioc));
			/* Copy the ioc control structure part of ioctl request */
			if (copy_from_user(&ioc, ifr->ifr_data, sizeof(wl_ioctl_t))) {
				bcmerror = BCME_BADADDR;
				goto done;
			}
			ioc.cmd &= ~WLC_SPEC_FLAG; /* Clear the FLAG */

			/* To differentiate between wl and dhd read 4 more byes */
			if ((copy_from_user(&ioc.driver, (char *)ifr->ifr_data + sizeof(wl_ioctl_t),
				sizeof(uint)) != 0)) {
				bcmerror = BCME_BADADDR;
				goto done;
			}

		} else { /* ioc.cmd & WLC_SPEC_FLAG */
			ioc.buf = compat_ptr(compat_ioc.buf);
			ioc.len = compat_ioc.len;
			ioc.set = compat_ioc.set;
			ioc.used = compat_ioc.used;
			ioc.needed = compat_ioc.needed;
			/* To differentiate between wl and dhd read 4 more byes */
			if ((copy_from_user(&ioc.driver, (char *)ifr->ifr_data + sizeof(compat_wl_ioctl_t),
				sizeof(uint)) != 0)) {
				bcmerror = BCME_BADADDR;
				goto done;
			}
		} /* ioc.cmd & WLC_SPEC_FLAG */
	} else
#endif /* CONFIG_COMPAT */
	{
		/* Copy the ioc control structure part of ioctl request */
		if (copy_from_user(&ioc, ifr->ifr_data, sizeof(wl_ioctl_t))) {
			bcmerror = BCME_BADADDR;
			goto done;
		}
#ifdef CONFIG_COMPAT
		ioc.cmd &= ~WLC_SPEC_FLAG; /* make sure it was clear when it isn't a compat task*/
#endif

		/* To differentiate between wl and dhd read 4 more byes */
		if ((copy_from_user(&ioc.driver, (char *)ifr->ifr_data + sizeof(wl_ioctl_t),
			sizeof(uint)) != 0)) {
			bcmerror = BCME_BADADDR;
			goto done;
		}
	}

	if (!capable(CAP_NET_ADMIN)) {
		bcmerror = BCME_EPERM;
		goto done;
	}

	/* Take backup of ioc.buf and restore later */
	ioc_buf_user = ioc.buf;

	if (ioc.len > 0) {
		buflen = MIN(ioc.len, DHD_IOCTL_MAXLEN);
		if (!(local_buf = MALLOC(dhd->pub.osh, buflen+1))) {
			bcmerror = BCME_NOMEM;
			goto done;
		}

		DHD_PERIM_UNLOCK(&dhd->pub);
		if (copy_from_user(local_buf, ioc.buf, buflen)) {
			DHD_PERIM_LOCK(&dhd->pub);
			bcmerror = BCME_BADADDR;
			goto done;
		}
		DHD_PERIM_LOCK(&dhd->pub);

		*((char *)local_buf + buflen) = '\0';

		/* For some platforms accessing userspace memory
		 * of ioc.buf is causing kernel panic, so to avoid that
		 * make ioc.buf pointing to kernel space memory local_buf
		 */
		ioc.buf = local_buf;
	}

	/* Skip all the non DHD iovars (wl iovars) after f/w hang */
	if (ioc.driver != DHD_IOCTL_MAGIC && dhd->pub.hang_was_sent) {
		DHD_TRACE(("%s: HANG was sent up earlier\n", __FUNCTION__));
		DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(&dhd->pub, DHD_EVENT_TIMEOUT_MS);
		bcmerror = BCME_DONGLE_DOWN;
		goto done;
	}

	bcmerror = dhd_ioctl_process(&dhd->pub, ifidx, &ioc, local_buf);

	/* Restore back userspace pointer to ioc.buf */
	ioc.buf = ioc_buf_user;

	if (!bcmerror && buflen && local_buf && ioc.buf) {
		DHD_PERIM_UNLOCK(&dhd->pub);
		if (copy_to_user(ioc.buf, local_buf, buflen))
			bcmerror = -EFAULT;
		DHD_PERIM_LOCK(&dhd->pub);
	}

done:
	if (local_buf)
		MFREE(dhd->pub.osh, local_buf, buflen+1);

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	return OSL_ERROR(bcmerror);
}


#ifdef FIX_CPU_MIN_CLOCK
static int dhd_init_cpufreq_fix(dhd_info_t *dhd)
{
	if (dhd) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
		mutex_init(&dhd->cpufreq_fix);
#endif
		dhd->cpufreq_fix_status = FALSE;
	}
	return 0;
}

static void dhd_fix_cpu_freq(dhd_info_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	mutex_lock(&dhd->cpufreq_fix);
#endif
	if (dhd && !dhd->cpufreq_fix_status) {
		pm_qos_add_request(&dhd->dhd_cpu_qos, PM_QOS_CPU_FREQ_MIN, 300000);
#ifdef FIX_BUS_MIN_CLOCK
		pm_qos_add_request(&dhd->dhd_bus_qos, PM_QOS_BUS_THROUGHPUT, 400000);
#endif /* FIX_BUS_MIN_CLOCK */
		DHD_ERROR(("pm_qos_add_requests called\n"));

		dhd->cpufreq_fix_status = TRUE;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	mutex_unlock(&dhd->cpufreq_fix);
#endif
}

static void dhd_rollback_cpu_freq(dhd_info_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	mutex_lock(&dhd ->cpufreq_fix);
#endif
	if (dhd && dhd->cpufreq_fix_status != TRUE) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
		mutex_unlock(&dhd->cpufreq_fix);
#endif
		return;
	}

	pm_qos_remove_request(&dhd->dhd_cpu_qos);
#ifdef FIX_BUS_MIN_CLOCK
	pm_qos_remove_request(&dhd->dhd_bus_qos);
#endif /* FIX_BUS_MIN_CLOCK */
	DHD_ERROR(("pm_qos_add_requests called\n"));

	dhd->cpufreq_fix_status = FALSE;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	mutex_unlock(&dhd->cpufreq_fix);
#endif
}
#endif /* FIX_CPU_MIN_CLOCK */

#if defined(BT_OVER_SDIO)

void
dhdsdio_bus_usr_cnt_inc(dhd_pub_t *dhdp)
{
	dhdp->info->bus_user_count++;
}

void
dhdsdio_bus_usr_cnt_dec(dhd_pub_t *dhdp)
{
	dhdp->info->bus_user_count--;
}

/* Return values:
 * Success: Returns 0
 * Failure: Returns -1 or errono code
 */
int
dhd_bus_get(wlan_bt_handle_t handle, bus_owner_t owner)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)handle;
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	int ret = 0;

	mutex_lock(&dhd->bus_user_lock);
	++dhd->bus_user_count;
	if (dhd->bus_user_count < 0) {
		DHD_ERROR(("%s(): bus_user_count is negative, which is invalid\n", __FUNCTION__));
		ret = -1;
		goto exit;
	}

	if (dhd->bus_user_count == 1) {

		dhd->pub.hang_was_sent = 0;

		/* First user, turn on WL_REG, start the bus */
		DHD_ERROR(("%s(): First user Turn On WL_REG & start the bus", __FUNCTION__));

		if (!wifi_platform_set_power(dhd->adapter, TRUE, WIFI_TURNON_DELAY)) {
			/* Enable F1 */
			ret = dhd_bus_resume(dhdp, 0);
			if (ret) {
				DHD_ERROR(("%s(): Failed to enable F1, err=%d\n",
					__FUNCTION__, ret));
				goto exit;
			}
		}

		dhd_update_fw_nv_path(dhd);
		/* update firmware and nvram path to sdio bus */
		dhd_bus_update_fw_nv_path(dhd->pub.bus,
			dhd->fw_path, dhd->nv_path);
		/* download the firmware, Enable F2 */
		/* TODO: Should be done only in case of FW switch */
		ret = dhd_bus_devreset(dhdp, FALSE);
		dhd_bus_resume(dhdp, 1);
		if (!ret) {
			if (dhd_sync_with_dongle(&dhd->pub) < 0) {
				DHD_ERROR(("%s(): Sync with dongle failed!!\n", __FUNCTION__));
				ret = -EFAULT;
			}
		} else {
			DHD_ERROR(("%s(): Failed to download, err=%d\n", __FUNCTION__, ret));
		}
	} else {
		DHD_ERROR(("%s(): BUS is already acquired, just increase the count %d \r\n",
			__FUNCTION__, dhd->bus_user_count));
	}
exit:
	mutex_unlock(&dhd->bus_user_lock);
	return ret;
}
EXPORT_SYMBOL(dhd_bus_get);

/* Return values:
 * Success: Returns 0
 * Failure: Returns -1 or errono code
 */
int
dhd_bus_put(wlan_bt_handle_t handle, bus_owner_t owner)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)handle;
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	int ret = 0;
	BCM_REFERENCE(owner);

	mutex_lock(&dhd->bus_user_lock);
	--dhd->bus_user_count;
	if (dhd->bus_user_count < 0) {
		DHD_ERROR(("%s(): bus_user_count is negative, which is invalid\n", __FUNCTION__));
		dhd->bus_user_count = 0;
		ret = -1;
		goto exit;
	}

	if (dhd->bus_user_count == 0) {
		/* Last user, stop the bus and turn Off WL_REG */
		DHD_ERROR(("%s(): There are no owners left Trunf Off WL_REG & stop the bus \r\n",
			__FUNCTION__));
#ifdef PROP_TXSTATUS
		if (dhd->pub.wlfc_enabled) {
			dhd_wlfc_deinit(&dhd->pub);
		}
#endif /* PROP_TXSTATUS */
#ifdef PNO_SUPPORT
		if (dhd->pub.pno_state) {
			dhd_pno_deinit(&dhd->pub);
		}
#endif /* PNO_SUPPORT */
#ifdef RTT_SUPPORT
		if (dhd->pub.rtt_state) {
			dhd_rtt_deinit(&dhd->pub);
		}
#endif /* RTT_SUPPORT */
		ret = dhd_bus_devreset(dhdp, TRUE);
		if (!ret) {
			dhd_bus_suspend(dhdp);
			wifi_platform_set_power(dhd->adapter, FALSE, WIFI_TURNOFF_DELAY);
		}
	} else {
		DHD_ERROR(("%s(): Other owners using bus, decrease the count %d \r\n",
			__FUNCTION__, dhd->bus_user_count));
	}
exit:
	mutex_unlock(&dhd->bus_user_lock);
	return ret;
}
EXPORT_SYMBOL(dhd_bus_put);

int
dhd_net_bus_get(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return dhd_bus_get(&dhd->pub, WLAN_MODULE);
}

int
dhd_net_bus_put(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return dhd_bus_put(&dhd->pub, WLAN_MODULE);
}

/*
 * Function to enable the Bus Clock
 * Returns BCME_OK on success and BCME_xxx on failure
 *
 * This function is not callable from non-sleepable context
 */
int dhd_bus_clk_enable(wlan_bt_handle_t handle, bus_owner_t owner)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)handle;

	int ret;

	dhd_os_sdlock(dhdp);
	/*
	 * The second argument is TRUE, that means, we expect
	 * the function to "wait" until the clocks are really
	 * available
	 */
	ret = __dhdsdio_clk_enable(dhdp->bus, owner, TRUE);
	dhd_os_sdunlock(dhdp);

	return ret;
}
EXPORT_SYMBOL(dhd_bus_clk_enable);

/*
 * Function to disable the Bus Clock
 * Returns BCME_OK on success and BCME_xxx on failure
 *
 * This function is not callable from non-sleepable context
 */
int dhd_bus_clk_disable(wlan_bt_handle_t handle, bus_owner_t owner)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)handle;

	int ret;

	dhd_os_sdlock(dhdp);
	/*
	 * The second argument is TRUE, that means, we expect
	 * the function to "wait" until the clocks are really
	 * disabled
	 */
	ret = __dhdsdio_clk_disable(dhdp->bus, owner, TRUE);
	dhd_os_sdunlock(dhdp);

	return ret;
}
EXPORT_SYMBOL(dhd_bus_clk_disable);

/*
 * Function to reset bt_use_count counter to zero.
 *
 * This function is not callable from non-sleepable context
 */
void dhd_bus_reset_bt_use_count(wlan_bt_handle_t handle)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)handle;

	/* take the lock and reset bt use count */
	dhd_os_sdlock(dhdp);
	dhdsdio_reset_bt_use_count(dhdp->bus);
	dhd_os_sdunlock(dhdp);
}
EXPORT_SYMBOL(dhd_bus_reset_bt_use_count);

#endif /* BT_OVER_SDIO */

#define MAX_TRY_CNT             5 /* Number of tries to disable deepsleep */
int dhd_deepsleep(dhd_info_t *dhd, int flag)
{
	char iovbuf[20];
	uint powervar = 0;
	dhd_pub_t *dhdp;
	int cnt = 0;
	int ret = 0;

	dhdp = &dhd->pub;

	switch (flag) {
		case 1 :  /* Deepsleep on */
			DHD_ERROR(("dhd_deepsleep: ON\n"));
			/* give some time to sysioc_work before deepsleep */
			OSL_SLEEP(200);
#ifdef PKT_FILTER_SUPPORT
			/* disable pkt filter */
			dhd_enable_packet_filter(0, dhdp);
#endif /* PKT_FILTER_SUPPORT */
			/* Disable MPC */
			powervar = 0;
			memset(iovbuf, 0, sizeof(iovbuf));
			bcm_mkiovar("mpc", (char *)&powervar, 4, iovbuf, sizeof(iovbuf));
			dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

			/* Enable Deepsleep */
			powervar = 1;
			memset(iovbuf, 0, sizeof(iovbuf));
			bcm_mkiovar("deepsleep", (char *)&powervar, 4, iovbuf, sizeof(iovbuf));
			dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
			break;

		case 0: /* Deepsleep Off */
			DHD_ERROR(("dhd_deepsleep: OFF\n"));

			/* Disable Deepsleep */
			for (cnt = 0; cnt < MAX_TRY_CNT; cnt++) {
				powervar = 0;
				memset(iovbuf, 0, sizeof(iovbuf));
				bcm_mkiovar("deepsleep", (char *)&powervar, 4,
					iovbuf, sizeof(iovbuf));
				dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf,
					sizeof(iovbuf), TRUE, 0);

				memset(iovbuf, 0, sizeof(iovbuf));
				bcm_mkiovar("deepsleep", (char *)&powervar, 4,
					iovbuf, sizeof(iovbuf));
				if ((ret = dhd_wl_ioctl_cmd(dhdp, WLC_GET_VAR, iovbuf,
					sizeof(iovbuf),	FALSE, 0)) < 0) {
					DHD_ERROR(("the error of dhd deepsleep status"
						" ret value :%d\n", ret));
				} else {
					if (!(*(int *)iovbuf)) {
						DHD_ERROR(("deepsleep mode is 0,"
							" count: %d\n", cnt));
						break;
					}
				}
			}

			/* Enable MPC */
			powervar = 1;
			memset(iovbuf, 0, sizeof(iovbuf));
			bcm_mkiovar("mpc", (char *)&powervar, 4, iovbuf, sizeof(iovbuf));
			dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
			break;
	}

	return 0;
}

static int
dhd_stop(struct net_device *net)
{
	int ifidx = 0;
#ifdef WL_CFG80211
	unsigned long flags = 0;
#endif /* WL_CFG80211 */
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);
	printf("%s: Enter %p\n", __FUNCTION__, net);
	dhd->pub.rxcnt_timeout = 0;
	dhd->pub.txcnt_timeout = 0;

#ifdef BCMPCIE
	dhd->pub.d3ackcnt_timeout = 0;
#endif /* BCMPCIE */

	if (dhd->pub.up == 0) {
		goto exit;
	}
#if defined(DHD_HANG_SEND_UP_TEST)
	if (dhd->pub.req_hang_type) {
		DHD_ERROR(("%s, Clear HANG test request 0x%x\n",
			__FUNCTION__, dhd->pub.req_hang_type));
		dhd->pub.req_hang_type = 0;
	}
#endif /* DHD_HANG_SEND_UP_TEST */

	dhd_if_flush_sta(DHD_DEV_IFP(net));

	/* Disable Runtime PM before interface down */
	DHD_DISABLE_RUNTIME_PM(&dhd->pub);

#ifdef FIX_CPU_MIN_CLOCK
	if (dhd_get_fw_mode(dhd) == DHD_FLAG_HOSTAP_MODE)
		dhd_rollback_cpu_freq(dhd);
#endif /* FIX_CPU_MIN_CLOCK */

	ifidx = dhd_net2idx(dhd, net);
	BCM_REFERENCE(ifidx);

	/* Set state and stop OS transmissions */
	netif_stop_queue(net);
#ifdef WL_CFG80211
	spin_lock_irqsave(&dhd->pub.up_lock, flags);
	dhd->pub.up = 0;
	spin_unlock_irqrestore(&dhd->pub.up_lock, flags);
#else
	dhd->pub.up = 0;
#endif /* WL_CFG80211 */

#ifdef WL_CFG80211
	if (ifidx == 0) {
		dhd_if_t *ifp;
		wl_cfg80211_down(net);

		ifp = dhd->iflist[0];
		ASSERT(ifp && ifp->net);
		/*
		 * For CFG80211: Clean up all the left over virtual interfaces
		 * when the primary Interface is brought down. [ifconfig wlan0 down]
		 */
		if (!dhd_download_fw_on_driverload) {
			if ((dhd->dhd_state & DHD_ATTACH_STATE_ADD_IF) &&
				(dhd->dhd_state & DHD_ATTACH_STATE_CFG80211)) {
				int i;
#ifdef WL_CFG80211_P2P_DEV_IF
				wl_cfg80211_del_p2p_wdev(net);
#endif /* WL_CFG80211_P2P_DEV_IF */

				dhd_net_if_lock_local(dhd);
				for (i = 1; i < DHD_MAX_IFS; i++)
					dhd_remove_if(&dhd->pub, i, FALSE);

				if (ifp && ifp->net) {
					dhd_if_del_sta_list(ifp);
				}
#ifdef ARP_OFFLOAD_SUPPORT
				if (dhd_inetaddr_notifier_registered) {
					dhd_inetaddr_notifier_registered = FALSE;
					unregister_inetaddr_notifier(&dhd_inetaddr_notifier);
				}
#endif /* ARP_OFFLOAD_SUPPORT */
#if defined(CONFIG_IPV6) && defined(IPV6_NDO_SUPPORT)
				if (dhd_inet6addr_notifier_registered) {
					dhd_inet6addr_notifier_registered = FALSE;
					unregister_inet6addr_notifier(&dhd_inet6addr_notifier);
				}
#endif /* CONFIG_IPV6 && IPV6_NDO_SUPPORT */
				dhd_net_if_unlock_local(dhd);
			}
#if 0
			// terence 20161024: remove this to prevent dev_close() get stuck in dhd_hang_process
			cancel_work_sync(dhd->dhd_deferred_wq);
#endif

#ifdef SHOW_LOGTRACE
			/* Wait till event_log_dispatcher_work finishes */
			cancel_work_sync(&dhd->event_log_dispatcher_work);
#endif /* SHOW_LOGTRACE */

#if defined(DHD_LB_RXP)
			__skb_queue_purge(&dhd->rx_pend_queue);
#endif /* DHD_LB_RXP */

#if defined(DHD_LB_TXP)
			skb_queue_purge(&dhd->tx_pend_queue);
#endif /* DHD_LB_TXP */
		}

		argos_register_notifier_deinit();
#ifdef DHDTCPACK_SUPPRESS
		dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_OFF);
#endif /* DHDTCPACK_SUPPRESS */
#if defined(DHD_LB_RXP)
		if (ifp->net == dhd->rx_napi_netdev) {
			DHD_INFO(("%s napi<%p> disabled ifp->net<%p,%s>\n",
				__FUNCTION__, &dhd->rx_napi_struct, net, net->name));
			skb_queue_purge(&dhd->rx_napi_queue);
			napi_disable(&dhd->rx_napi_struct);
			netif_napi_del(&dhd->rx_napi_struct);
			dhd->rx_napi_netdev = NULL;
		}
#endif /* DHD_LB_RXP */
	}
#endif /* WL_CFG80211 */

	DHD_SSSR_DUMP_DEINIT(&dhd->pub);

#ifdef PROP_TXSTATUS
	dhd_wlfc_cleanup(&dhd->pub, NULL, 0);
#endif
#ifdef SHOW_LOGTRACE
	if (!dhd_download_fw_on_driverload) {
		/* Release the skbs from queue for WLC_E_TRACE event */
		dhd_event_logtrace_flush_queue(&dhd->pub);
		if (dhd->dhd_state & DHD_ATTACH_LOGTRACE_INIT) {
			if (dhd->event_data.fmts) {
				MFREE(dhd->pub.osh, dhd->event_data.fmts,
					dhd->event_data.fmts_size);
				dhd->event_data.fmts = NULL;
			}
			if (dhd->event_data.raw_fmts) {
				MFREE(dhd->pub.osh, dhd->event_data.raw_fmts,
					dhd->event_data.raw_fmts_size);
				dhd->event_data.raw_fmts = NULL;
			}
			if (dhd->event_data.raw_sstr) {
				MFREE(dhd->pub.osh, dhd->event_data.raw_sstr,
					dhd->event_data.raw_sstr_size);
				dhd->event_data.raw_sstr = NULL;
			}
			if (dhd->event_data.rom_raw_sstr) {
				MFREE(dhd->pub.osh, dhd->event_data.rom_raw_sstr,
					dhd->event_data.rom_raw_sstr_size);
				dhd->event_data.rom_raw_sstr = NULL;
			}
			dhd->dhd_state &= ~DHD_ATTACH_LOGTRACE_INIT;
		}
	}
#endif /* SHOW_LOGTRACE */
#ifdef APF
	dhd_dev_apf_delete_filter(net);
#endif /* APF */

	/* Stop the protocol module */
	dhd_prot_stop(&dhd->pub);

	OLD_MOD_DEC_USE_COUNT;
exit:
#if defined(WL_WIRELESS_EXT)
	if (ifidx == 0) {
		wl_iw_down(net, &dhd->pub);
	}
#endif /* defined(WL_WIRELESS_EXT) */
#ifdef WL_ESCAN
	if (ifidx == 0) {
		wl_escan_down(net, &dhd->pub);
	}
#endif /* WL_ESCAN */
	if (ifidx == 0 && !dhd_download_fw_on_driverload) {
#if defined(BT_OVER_SDIO)
		dhd_bus_put(&dhd->pub, WLAN_MODULE);
		wl_android_set_wifi_on_flag(FALSE);
#else
		wl_android_wifi_off(net, TRUE);
#if defined(WL_EXT_IAPSTA) || defined(USE_IW) || defined(WL_ESCAN)
#ifdef WL_EXT_IAPSTA
		wl_ext_iapsta_dettach_netdev(net, ifidx);
#endif /* WL_EXT_IAPSTA */
#ifdef WL_ESCAN
			wl_escan_event_dettach(net, &dhd->pub);
#endif /* WL_ESCAN */
			wl_ext_event_dettach_netdev(net, ifidx);
#endif /* WL_EXT_IAPSTA || USE_IW || WL_ESCAN */
	} else {
		if (dhd->pub.conf->deepsleep)
			dhd_deepsleep(dhd, 1);
#endif /* BT_OVER_SDIO */
	}
	dhd->pub.hang_was_sent = 0;

	/* Clear country spec for for built-in type driver */
	if (!dhd_download_fw_on_driverload) {
		dhd->pub.dhd_cspec.country_abbrev[0] = 0x00;
		dhd->pub.dhd_cspec.rev = 0;
		dhd->pub.dhd_cspec.ccode[0] = 0x00;
	}

#ifdef BCMDBGFS
	dhd_dbgfs_remove();
#endif

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	/* Destroy wakelock */
	if (!dhd_download_fw_on_driverload &&
		(dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		DHD_OS_WAKE_LOCK_DESTROY(dhd);
		dhd->dhd_state &= ~DHD_ATTACH_STATE_WAKELOCKS_INIT;
	}
	printf("%s: Exit\n", __FUNCTION__);

	return 0;
}

#if defined(WL_CFG80211) && defined(USE_INITIAL_SHORT_DWELL_TIME)
extern bool g_first_broadcast_scan;
#endif 

#ifdef WL11U
static int dhd_interworking_enable(dhd_pub_t *dhd)
{
	uint32 enable = true;
	int ret = BCME_OK;

	ret = dhd_iovar(dhd, 0, "interworking", (char *)&enable, sizeof(enable), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: enableing interworking failed, ret=%d\n", __FUNCTION__, ret));
	}

	return ret;
}
#endif /* WL11u */

static int
dhd_open(struct net_device *net)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
#ifdef TOE
	uint32 toe_ol;
#endif
#ifdef BCM_FD_AGGR
	char iovbuf[WLC_IOCTL_SMLEN];
	dbus_config_t config;
	uint32 agglimit = 0;
	uint32 rpc_agg = BCM_RPC_TP_DNGL_AGG_DPC; /* host aggr not enabled yet */
#endif /* BCM_FD_AGGR */
	int ifidx;
	int32 ret = 0;
#if defined(OOB_INTR_ONLY)
	uint32 bus_type = -1;
	uint32 bus_num = -1;
	uint32 slot_num = -1;
	wifi_adapter_info_t *adapter = NULL;
#endif
#if defined(WL_EXT_IAPSTA) && defined(ISAM_PREINIT)
	int bytes_written = 0;
#endif

	if (!dhd_download_fw_on_driverload) {
		if (!dhd_driver_init_done) {
			DHD_ERROR(("%s: WLAN driver is not initialized\n", __FUNCTION__));
			return -1;
		}
	}

	printf("%s: Enter %p\n", __FUNCTION__, net);
	DHD_MUTEX_LOCK();
	/* Init wakelock */
	if (!dhd_download_fw_on_driverload) {
		if (!(dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
			DHD_OS_WAKE_LOCK_INIT(dhd);
			dhd->dhd_state |= DHD_ATTACH_STATE_WAKELOCKS_INIT;
		}
#ifdef SHOW_LOGTRACE
		skb_queue_head_init(&dhd->evt_trace_queue);

		if (!(dhd->dhd_state & DHD_ATTACH_LOGTRACE_INIT)) {
			ret = dhd_init_logstrs_array(dhd->pub.osh, &dhd->event_data);
			if (ret == BCME_OK) {
				dhd_init_static_strs_array(dhd->pub.osh, &dhd->event_data,
					st_str_file_path, map_file_path);
				dhd_init_static_strs_array(dhd->pub.osh, &dhd->event_data,
					rom_st_str_file_path, rom_map_file_path);
				dhd->dhd_state |= DHD_ATTACH_LOGTRACE_INIT;
			}
		}
#endif /* SHOW_LOGTRACE */
	}

#if defined(PREVENT_REOPEN_DURING_HANG)
	/* WAR : to prevent calling dhd_open abnormally in quick succession after hang event */
	if (dhd->pub.hang_was_sent == 1) {
		DHD_ERROR(("%s: HANG was sent up earlier\n", __FUNCTION__));
		/* Force to bring down WLAN interface in case dhd_stop() is not called
		 * from the upper layer when HANG event is triggered.
		 */
		if (!dhd_download_fw_on_driverload && dhd->pub.up == 1) {
			DHD_ERROR(("%s: WLAN interface is not brought down\n", __FUNCTION__));
			dhd_stop(net);
		} else {
			DHD_MUTEX_UNLOCK();
			return -1;
		}
	}
#endif /* PREVENT_REOPEN_DURING_HANG */


	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);
	dhd->pub.dongle_trap_occured = 0;
	dhd->pub.hang_was_sent = 0;
	dhd->pub.hang_reason = 0;
	dhd->pub.iovar_timeout_occured = 0;
#ifdef PCIE_FULL_DONGLE
	dhd->pub.d3ack_timeout_occured = 0;
#endif /* PCIE_FULL_DONGLE */

#ifdef DHD_LOSSLESS_ROAMING
	dhd->pub.dequeue_prec_map = ALLPRIO;
#endif
#if 0
	/*
	 * Force start if ifconfig_up gets called before START command
	 *  We keep WEXT's wl_control_wl_start to provide backward compatibility
	 *  This should be removed in the future
	 */
	ret = wl_control_wl_start(net);
	if (ret != 0) {
		DHD_ERROR(("%s: failed with code %d\n", __FUNCTION__, ret));
		ret = -1;
		goto exit;
	}
#endif

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d\n", __FUNCTION__, ifidx));

	if (ifidx < 0) {
		DHD_ERROR(("%s: Error: called with invalid IF\n", __FUNCTION__));
		ret = -1;
		goto exit;
	}

	if (!dhd->iflist[ifidx]) {
		DHD_ERROR(("%s: Error: called when IF already deleted\n", __FUNCTION__));
		ret = -1;
		goto exit;
	}

	if (ifidx == 0) {
		atomic_set(&dhd->pend_8021x_cnt, 0);
		if (!dhd_download_fw_on_driverload) {
			DHD_ERROR(("\n%s\n", dhd_version));
#if defined(WL_EXT_IAPSTA) || defined(USE_IW) || defined(WL_ESCAN)
			wl_ext_event_attach_netdev(net, ifidx, dhd->iflist[ifidx]->bssidx);
#ifdef WL_ESCAN
			wl_escan_event_attach(net, &dhd->pub);
#endif /* WL_ESCAN */
#ifdef WL_EXT_IAPSTA
			wl_ext_iapsta_attach_netdev(net, ifidx, dhd->iflist[ifidx]->bssidx);
#endif /* WL_EXT_IAPSTA */
#endif /* WL_EXT_IAPSTA || USE_IW || WL_ESCAN */
#if defined(USE_INITIAL_SHORT_DWELL_TIME)
			g_first_broadcast_scan = TRUE;
#endif 
#if defined(BT_OVER_SDIO)
			ret = dhd_bus_get(&dhd->pub, WLAN_MODULE);
			wl_android_set_wifi_on_flag(TRUE);
#else
			ret = wl_android_wifi_on(net);
#endif /* BT_OVER_SDIO */
			if (ret != 0) {
				DHD_ERROR(("%s : wl_android_wifi_on failed (%d)\n",
					__FUNCTION__, ret));
				ret = -1;
				goto exit;
			}
		}
#ifdef FIX_CPU_MIN_CLOCK
		if (dhd_get_fw_mode(dhd) == DHD_FLAG_HOSTAP_MODE) {
			dhd_init_cpufreq_fix(dhd);
			dhd_fix_cpu_freq(dhd);
		}
#endif /* FIX_CPU_MIN_CLOCK */
#if defined(OOB_INTR_ONLY)
		if (dhd->pub.conf->dpc_cpucore >= 0) {
			dhd_bus_get_ids(dhd->pub.bus, &bus_type, &bus_num, &slot_num);
			adapter = dhd_wifi_platform_get_adapter(bus_type, bus_num, slot_num);
			if (adapter) {
				printf("%s: set irq affinity hit %d\n", __FUNCTION__, dhd->pub.conf->dpc_cpucore);
				irq_set_affinity_hint(adapter->irq_num, cpumask_of(dhd->pub.conf->dpc_cpucore));
			}
		}
#endif

		if (dhd->pub.busstate != DHD_BUS_DATA) {
#ifdef BCMDBUS
			dhd_set_path(&dhd->pub);
			DHD_MUTEX_UNLOCK();
			wait_event_interruptible_timeout(dhd->adapter->status_event,
				wifi_get_adapter_status(dhd->adapter, WIFI_STATUS_FW_READY),
				msecs_to_jiffies(DHD_FW_READY_TIMEOUT));
			DHD_MUTEX_LOCK();
			if ((ret = dbus_up(dhd->pub.bus)) != 0) {
				DHD_ERROR(("%s: failed to dbus_up with code %d\n", __FUNCTION__, ret));
				goto exit;
			} else {
				dhd->pub.busstate = DHD_BUS_DATA;
			}
			if ((ret = dhd_sync_with_dongle(&dhd->pub)) < 0) {
				DHD_ERROR(("%s: failed with code %d\n", __FUNCTION__, ret));
				goto exit;
			}
#else
			/* try to bring up bus */
			DHD_PERIM_UNLOCK(&dhd->pub);
			ret = dhd_bus_start(&dhd->pub);
			DHD_PERIM_LOCK(&dhd->pub);
			if (ret) {
				DHD_ERROR(("%s: failed with code %d\n", __FUNCTION__, ret));
				ret = -1;
				goto exit;
			}
#endif /* !BCMDBUS */

		}
#ifdef WL_EXT_IAPSTA
		wl_ext_iapsta_attach_name(net, ifidx);
#endif
		if (dhd_download_fw_on_driverload) {
			if (dhd->pub.conf->deepsleep)
				dhd_deepsleep(dhd, 0);
		}

#ifdef BCM_FD_AGGR
		config.config_id = DBUS_CONFIG_ID_AGGR_LIMIT;


		memset(iovbuf, 0, sizeof(iovbuf));
		bcm_mkiovar("rpc_dngl_agglimit", (char *)&agglimit, 4,
			iovbuf, sizeof(iovbuf));

		if (!dhd_wl_ioctl_cmd(&dhd->pub, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) {
			agglimit = *(uint32 *)iovbuf;
			config.aggr_param.maxrxsf = agglimit >> BCM_RPC_TP_AGG_SF_SHIFT;
			config.aggr_param.maxrxsize = agglimit & BCM_RPC_TP_AGG_BYTES_MASK;
			DHD_ERROR(("rpc_dngl_agglimit %x : sf_limit %d bytes_limit %d\n",
				agglimit, config.aggr_param.maxrxsf, config.aggr_param.maxrxsize));
			if (bcm_rpc_tp_set_config(dhd->pub.info->rpc_th, &config)) {
				DHD_ERROR(("set tx/rx queue size and buffersize failed\n"));
			}
		} else {
			DHD_ERROR(("get rpc_dngl_agglimit failed\n"));
			rpc_agg &= ~BCM_RPC_TP_DNGL_AGG_DPC;
		}

		/* Set aggregation for TX */
		bcm_rpc_tp_agg_set(dhd->pub.info->rpc_th, BCM_RPC_TP_HOST_AGG_MASK,
			rpc_agg & BCM_RPC_TP_HOST_AGG_MASK);

		/* Set aggregation for RX */
		memset(iovbuf, 0, sizeof(iovbuf));
		bcm_mkiovar("rpc_agg", (char *)&rpc_agg, sizeof(rpc_agg), iovbuf, sizeof(iovbuf));
		if (!dhd_wl_ioctl_cmd(&dhd->pub, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) {
			dhd->pub.info->fdaggr = 0;
			if (rpc_agg & BCM_RPC_TP_HOST_AGG_MASK)
				dhd->pub.info->fdaggr |= BCM_FDAGGR_H2D_ENABLED;
			if (rpc_agg & BCM_RPC_TP_DNGL_AGG_MASK)
				dhd->pub.info->fdaggr |= BCM_FDAGGR_D2H_ENABLED;
		} else {
			DHD_ERROR(("%s(): Setting RX aggregation failed %d\n", __FUNCTION__, ret));
		}
#endif /* BCM_FD_AGGR */

#ifdef BT_OVER_SDIO
		if (dhd->pub.is_bt_recovery_required) {
			DHD_ERROR(("%s: Send Hang Notification 2 to BT\n", __FUNCTION__));
			bcmsdh_btsdio_process_dhd_hang_notification(TRUE);
		}
		dhd->pub.is_bt_recovery_required = FALSE;
#endif

		/* dhd_sync_with_dongle has been called in dhd_bus_start or wl_android_wifi_on */
		memcpy(net->dev_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);

#ifdef TOE
		/* Get current TOE mode from dongle */
		if (dhd_toe_get(dhd, ifidx, &toe_ol) >= 0 && (toe_ol & TOE_TX_CSUM_OL) != 0) {
			dhd->iflist[ifidx]->net->features |= NETIF_F_IP_CSUM;
		} else {
			dhd->iflist[ifidx]->net->features &= ~NETIF_F_IP_CSUM;
		}
#endif /* TOE */

#if defined(DHD_LB_RXP)
		__skb_queue_head_init(&dhd->rx_pend_queue);
		if (dhd->rx_napi_netdev == NULL) {
			dhd->rx_napi_netdev = dhd->iflist[ifidx]->net;
			memset(&dhd->rx_napi_struct, 0, sizeof(struct napi_struct));
			netif_napi_add(dhd->rx_napi_netdev, &dhd->rx_napi_struct,
				dhd_napi_poll, dhd_napi_weight);
			DHD_INFO(("%s napi<%p> enabled ifp->net<%p,%s>\n",
				__FUNCTION__, &dhd->rx_napi_struct, net, net->name));
			napi_enable(&dhd->rx_napi_struct);
			DHD_INFO(("%s load balance init rx_napi_struct\n", __FUNCTION__));
			skb_queue_head_init(&dhd->rx_napi_queue);
		} /* rx_napi_netdev == NULL */
#endif /* DHD_LB_RXP */

#if defined(DHD_LB_TXP)
		/* Use the variant that uses locks */
		skb_queue_head_init(&dhd->tx_pend_queue);
#endif /* DHD_LB_TXP */

#if defined(WL_CFG80211)
		if (unlikely(wl_cfg80211_up(net))) {
			DHD_ERROR(("%s: failed to bring up cfg80211\n", __FUNCTION__));
			ret = -1;
			goto exit;
		}
		if (!dhd_download_fw_on_driverload) {
#ifdef ARP_OFFLOAD_SUPPORT
			dhd->pend_ipaddr = 0;
			if (!dhd_inetaddr_notifier_registered) {
				dhd_inetaddr_notifier_registered = TRUE;
				register_inetaddr_notifier(&dhd_inetaddr_notifier);
			}
#endif /* ARP_OFFLOAD_SUPPORT */
#if defined(CONFIG_IPV6) && defined(IPV6_NDO_SUPPORT)
			if (!dhd_inet6addr_notifier_registered) {
				dhd_inet6addr_notifier_registered = TRUE;
				register_inet6addr_notifier(&dhd_inet6addr_notifier);
			}
#endif /* CONFIG_IPV6 && IPV6_NDO_SUPPORT */
		}

		argos_register_notifier_init(net);
#if defined(NUM_SCB_MAX_PROBE)
		dhd_set_scb_probe(&dhd->pub);
#endif /* NUM_SCB_MAX_PROBE */
#endif /* WL_CFG80211 */
#if defined(WL_WIRELESS_EXT)
		if (unlikely(wl_iw_up(net, &dhd->pub))) {
			DHD_ERROR(("%s: failed to bring up wext\n", __FUNCTION__));
			ret = -1;
			goto exit;
		}
#endif
#ifdef WL_ESCAN
		if (unlikely(wl_escan_up(net, &dhd->pub))) {
			DHD_ERROR(("%s: failed to bring up escan\n", __FUNCTION__));
			ret = -1;
			goto exit;
		}
#endif /* WL_ESCAN */
#if defined(ISAM_PREINIT)
		if (!dhd_download_fw_on_driverload) {
			if (dhd->pub.conf) {
				wl_android_ext_priv_cmd(net, dhd->pub.conf->isam_init, 0, &bytes_written);
				wl_android_ext_priv_cmd(net, dhd->pub.conf->isam_config, 0, &bytes_written);
				wl_android_ext_priv_cmd(net, dhd->pub.conf->isam_enable, 0, &bytes_written);
			}
		}
#endif
	}

	/* Allow transmit calls */
	netif_start_queue(net);
	dhd->pub.up = 1;

	OLD_MOD_INC_USE_COUNT;

#ifdef BCMDBGFS
	dhd_dbgfs_init(&dhd->pub);
#endif

exit:
	if (ret) {
		dhd_stop(net);
	}

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	DHD_MUTEX_UNLOCK();

	printf("%s: Exit ret=%d\n", __FUNCTION__, ret);
	return ret;
}

int dhd_do_driver_init(struct net_device *net)
{
	dhd_info_t *dhd = NULL;

	if (!net) {
		DHD_ERROR(("Primary Interface not initialized \n"));
		return -EINVAL;
	}

	DHD_MUTEX_IS_LOCK_RETURN();

	/*  && defined(OEM_ANDROID) && defined(BCMSDIO) */
	dhd = DHD_DEV_INFO(net);

	/* If driver is already initialized, do nothing
	 */
	if (dhd->pub.busstate == DHD_BUS_DATA) {
		DHD_TRACE(("Driver already Inititalized. Nothing to do"));
		return 0;
	}

	if (dhd_open(net) < 0) {
		DHD_ERROR(("Driver Init Failed \n"));
		return -1;
	}

	return 0;
}

int
dhd_event_ifadd(dhd_info_t *dhdinfo, wl_event_data_if_t *ifevent, char *name, uint8 *mac)
{

#ifdef WL_CFG80211
		if (wl_cfg80211_notify_ifadd(dhd_linux_get_primary_netdev(&dhdinfo->pub),
			ifevent->ifidx, name, mac, ifevent->bssidx) == BCME_OK)
		return BCME_OK;
#endif

	/* handle IF event caused by wl commands, SoftAP, WEXT and
	 * anything else. This has to be done asynchronously otherwise
	 * DPC will be blocked (and iovars will timeout as DPC has no chance
	 * to read the response back)
	 */
	if (ifevent->ifidx > 0) {
		dhd_if_event_t *if_event = MALLOC(dhdinfo->pub.osh, sizeof(dhd_if_event_t));
		if (if_event == NULL) {
			DHD_ERROR(("dhd_event_ifadd: Failed MALLOC, malloced %d bytes",
				MALLOCED(dhdinfo->pub.osh)));
			return BCME_NOMEM;
		}

		memcpy(&if_event->event, ifevent, sizeof(if_event->event));
		memcpy(if_event->mac, mac, ETHER_ADDR_LEN);
		strncpy(if_event->name, name, IFNAMSIZ);
		if_event->name[IFNAMSIZ - 1] = '\0';
		dhd_deferred_schedule_work(dhdinfo->dhd_deferred_wq, (void *)if_event,
			DHD_WQ_WORK_IF_ADD, dhd_ifadd_event_handler, DHD_WQ_WORK_PRIORITY_LOW);
	}

	return BCME_OK;
}

int
dhd_event_ifdel(dhd_info_t *dhdinfo, wl_event_data_if_t *ifevent, char *name, uint8 *mac)
{
	dhd_if_event_t *if_event;

#ifdef WL_CFG80211
		if (wl_cfg80211_notify_ifdel(dhd_linux_get_primary_netdev(&dhdinfo->pub),
			ifevent->ifidx, name, mac, ifevent->bssidx) == BCME_OK)
		return BCME_OK;
#endif /* WL_CFG80211 */

	/* handle IF event caused by wl commands, SoftAP, WEXT and
	 * anything else
	 */
	if_event = MALLOC(dhdinfo->pub.osh, sizeof(dhd_if_event_t));
	if (if_event == NULL) {
		DHD_ERROR(("dhd_event_ifdel: malloc failed for if_event, malloced %d bytes",
			MALLOCED(dhdinfo->pub.osh)));
		return BCME_NOMEM;
	}
	memcpy(&if_event->event, ifevent, sizeof(if_event->event));
	memcpy(if_event->mac, mac, ETHER_ADDR_LEN);
	strncpy(if_event->name, name, IFNAMSIZ);
	if_event->name[IFNAMSIZ - 1] = '\0';
	dhd_deferred_schedule_work(dhdinfo->dhd_deferred_wq, (void *)if_event, DHD_WQ_WORK_IF_DEL,
		dhd_ifdel_event_handler, DHD_WQ_WORK_PRIORITY_LOW);

	return BCME_OK;
}

int
dhd_event_ifchange(dhd_info_t *dhdinfo, wl_event_data_if_t *ifevent, char *name, uint8 *mac)
{
#ifdef DHD_UPDATE_INTF_MAC
	dhd_if_event_t *if_event;
#endif /* DHD_UPDATE_INTF_MAC */

#ifdef WL_CFG80211
	wl_cfg80211_notify_ifchange(dhd_linux_get_primary_netdev(&dhdinfo->pub),
		ifevent->ifidx, name, mac, ifevent->bssidx);
#endif /* WL_CFG80211 */

#ifdef DHD_UPDATE_INTF_MAC
	/* handle IF event caused by wl commands, SoftAP, WEXT, MBSS and
	 * anything else
	 */
	if_event = MALLOC(dhdinfo->pub.osh, sizeof(dhd_if_event_t));
	if (if_event == NULL) {
		DHD_ERROR(("dhd_event_ifdel: malloc failed for if_event, malloced %d bytes",
			MALLOCED(dhdinfo->pub.osh)));
		return BCME_NOMEM;
	}
	memcpy(&if_event->event, ifevent, sizeof(if_event->event));
	// construct a change event
	if_event->event.ifidx = dhd_ifname2idx(dhdinfo, name);
	if_event->event.opcode = WLC_E_IF_CHANGE;
	memcpy(if_event->mac, mac, ETHER_ADDR_LEN);
	strncpy(if_event->name, name, IFNAMSIZ);
	if_event->name[IFNAMSIZ - 1] = '\0';
	dhd_deferred_schedule_work(dhdinfo->dhd_deferred_wq, (void *)if_event, DHD_WQ_WORK_IF_UPDATE,
		dhd_ifupdate_event_handler, DHD_WQ_WORK_PRIORITY_LOW);
#endif /* DHD_UPDATE_INTF_MAC */

	return BCME_OK;
}

/* unregister and free the existing net_device interface (if any) in iflist and
 * allocate a new one. the slot is reused. this function does NOT register the
 * new interface to linux kernel. dhd_register_if does the job
 */
struct net_device*
dhd_allocate_if(dhd_pub_t *dhdpub, int ifidx, const char *name,
	uint8 *mac, uint8 bssidx, bool need_rtnl_lock, const char *dngl_name)
{
	dhd_info_t *dhdinfo = (dhd_info_t *)dhdpub->info;
	dhd_if_t *ifp;

	ASSERT(dhdinfo && (ifidx < DHD_MAX_IFS));
	ifp = dhdinfo->iflist[ifidx];

	if (ifp != NULL) {
		if (ifp->net != NULL) {
			DHD_ERROR(("%s: free existing IF %s ifidx:%d \n",
				__FUNCTION__, ifp->net->name, ifidx));

			if (ifidx == 0) {
				/* For primary ifidx (0), there shouldn't be
				 * any netdev present already.
				 */
				DHD_ERROR(("Primary ifidx populated already\n"));
				ASSERT(0);
				return NULL;
			}

			dhd_dev_priv_clear(ifp->net); /* clear net_device private */

			/* in unregister_netdev case, the interface gets freed by net->destructor
			 * (which is set to free_netdev)
			 */
			if (ifp->net->reg_state == NETREG_UNINITIALIZED) {
				free_netdev(ifp->net);
			} else {
				netif_stop_queue(ifp->net);
				if (need_rtnl_lock)
					unregister_netdev(ifp->net);
				else
					unregister_netdevice(ifp->net);
			}
			ifp->net = NULL;
		}
	} else {
		ifp = MALLOC(dhdinfo->pub.osh, sizeof(dhd_if_t));
		if (ifp == NULL) {
			DHD_ERROR(("%s: OOM - dhd_if_t(%zu)\n", __FUNCTION__, sizeof(dhd_if_t)));
			return NULL;
		}
	}

	memset(ifp, 0, sizeof(dhd_if_t));
	ifp->info = dhdinfo;
	ifp->idx = ifidx;
	ifp->bssidx = bssidx;
#ifdef DHD_MCAST_REGEN
	ifp->mcast_regen_bss_enable = FALSE;
#endif
	/* set to TRUE rx_pkt_chainable at alloc time */
	ifp->rx_pkt_chainable = TRUE;

	if (mac != NULL)
		memcpy(&ifp->mac_addr, mac, ETHER_ADDR_LEN);

	/* Allocate etherdev, including space for private structure */
	ifp->net = alloc_etherdev(DHD_DEV_PRIV_SIZE);
	if (ifp->net == NULL) {
		DHD_ERROR(("%s: OOM - alloc_etherdev(%zu)\n", __FUNCTION__, sizeof(dhdinfo)));
		goto fail;
	}

	/* Setup the dhd interface's netdevice private structure. */
	dhd_dev_priv_save(ifp->net, dhdinfo, ifp, ifidx);

	if (name && name[0]) {
		strncpy(ifp->net->name, name, IFNAMSIZ);
		ifp->net->name[IFNAMSIZ - 1] = '\0';
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 9))
#define IFP_NET_DESTRUCTOR	ifp->net->priv_destructor
#else
#define IFP_NET_DESTRUCTOR	ifp->net->destructor
#endif // endif

#ifdef WL_CFG80211
	if (ifidx == 0) {
		IFP_NET_DESTRUCTOR = free_netdev;
	} else {
		IFP_NET_DESTRUCTOR = dhd_netdev_free;
	}
#else
	IFP_NET_DESTRUCTOR = free_netdev;
#endif /* WL_CFG80211 */
	strncpy(ifp->name, ifp->net->name, IFNAMSIZ);
	ifp->name[IFNAMSIZ - 1] = '\0';
	dhdinfo->iflist[ifidx] = ifp;

/* initialize the dongle provided if name */
	if (dngl_name)
		strncpy(ifp->dngl_name, dngl_name, IFNAMSIZ);
	else if (name)
		strncpy(ifp->dngl_name, name, IFNAMSIZ);

#ifdef PCIE_FULL_DONGLE
	/* Initialize STA info list */
	INIT_LIST_HEAD(&ifp->sta_list);
	DHD_IF_STA_LIST_LOCK_INIT(ifp);
#endif /* PCIE_FULL_DONGLE */

#ifdef DHD_L2_FILTER
	ifp->phnd_arp_table = init_l2_filter_arp_table(dhdpub->osh);
	ifp->parp_allnode = TRUE;
#endif /* DHD_L2_FILTER */


	DHD_CUMM_CTR_INIT(&ifp->cumm_ctr);

	return ifp->net;

fail:
	if (ifp != NULL) {
		if (ifp->net != NULL) {
#if defined(DHD_LB_RXP) && defined(PCIE_FULL_DONGLE)
			if (ifp->net == dhdinfo->rx_napi_netdev) {
				napi_disable(&dhdinfo->rx_napi_struct);
				netif_napi_del(&dhdinfo->rx_napi_struct);
				skb_queue_purge(&dhdinfo->rx_napi_queue);
				dhdinfo->rx_napi_netdev = NULL;
			}
#endif /* DHD_LB_RXP && PCIE_FULL_DONGLE */
			dhd_dev_priv_clear(ifp->net);
			free_netdev(ifp->net);
			ifp->net = NULL;
		}
		MFREE(dhdinfo->pub.osh, ifp, sizeof(*ifp));
		ifp = NULL;
	}
	dhdinfo->iflist[ifidx] = NULL;
	return NULL;
}

/* unregister and free the the net_device interface associated with the indexed
 * slot, also free the slot memory and set the slot pointer to NULL
 */
int
dhd_remove_if(dhd_pub_t *dhdpub, int ifidx, bool need_rtnl_lock)
{
	dhd_info_t *dhdinfo = (dhd_info_t *)dhdpub->info;
	dhd_if_t *ifp;
#ifdef PCIE_FULL_DONGLE
	if_flow_lkup_t *if_flow_lkup = (if_flow_lkup_t *)dhdpub->if_flow_lkup;
#endif /* PCIE_FULL_DONGLE */

	ifp = dhdinfo->iflist[ifidx];

	if (ifp != NULL) {
		if (ifp->net != NULL) {
			DHD_ERROR(("deleting interface '%s' idx %d\n", ifp->net->name, ifp->idx));

			dhdinfo->iflist[ifidx] = NULL;
			/* in unregister_netdev case, the interface gets freed by net->destructor
			 * (which is set to free_netdev)
			 */
			if (ifp->net->reg_state == NETREG_UNINITIALIZED) {
				free_netdev(ifp->net);
			} else {
				netif_tx_disable(ifp->net);



#if defined(SET_RPS_CPUS)
				custom_rps_map_clear(ifp->net->_rx);
#endif /* SET_RPS_CPUS */
				if (need_rtnl_lock)
					unregister_netdev(ifp->net);
				else
					unregister_netdevice(ifp->net);
#if defined(WL_EXT_IAPSTA) || defined(USE_IW) || defined(WL_ESCAN)
#ifdef WL_EXT_IAPSTA
				wl_ext_iapsta_dettach_netdev(ifp->net, ifidx);
#endif /* WL_EXT_IAPSTA */
#ifdef WL_ESCAN
				wl_escan_event_dettach(ifp->net, dhdpub);
#endif /* WL_ESCAN */
				wl_ext_event_dettach_netdev(ifp->net, ifidx);
#endif /* WL_EXT_IAPSTA || USE_IW || WL_ESCAN */
			}
			ifp->net = NULL;
		}
#ifdef DHD_WMF
		dhd_wmf_cleanup(dhdpub, ifidx);
#endif /* DHD_WMF */
#ifdef DHD_L2_FILTER
		bcm_l2_filter_arp_table_update(dhdpub->osh, ifp->phnd_arp_table, TRUE,
			NULL, FALSE, dhdpub->tickcnt);
		deinit_l2_filter_arp_table(dhdpub->osh, ifp->phnd_arp_table);
		ifp->phnd_arp_table = NULL;
#endif /* DHD_L2_FILTER */


		dhd_if_del_sta_list(ifp);
#ifdef PCIE_FULL_DONGLE
		/* Delete flowrings of WDS interface */
		if (if_flow_lkup[ifidx].role == WLC_E_IF_ROLE_WDS) {
			dhd_flow_rings_delete(dhdpub, ifidx);
		}
#endif /* PCIE_FULL_DONGLE */
		DHD_CUMM_CTR_INIT(&ifp->cumm_ctr);

		MFREE(dhdinfo->pub.osh, ifp, sizeof(*ifp));
		ifp = NULL;
	}

	return BCME_OK;
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
static struct net_device_ops dhd_ops_pri = {
	.ndo_open = dhd_open,
	.ndo_stop = dhd_stop,
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif
};

static struct net_device_ops dhd_ops_virt = {
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif
};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)) */

#ifdef DEBUGGER
extern void debugger_init(void *bus_handle);
#endif


#ifdef SHOW_LOGTRACE
int
dhd_os_read_file(void *file, char *buf, uint32 size)
{
	struct file *filep = (struct file *)file;

	if (!file || !buf)
		return -1;

	return vfs_read(filep, buf, size, &filep->f_pos);
}

int
dhd_os_seek_file(void *file, int64 offset)
{
	struct file *filep = (struct file *)file;
	if (!file)
		return -1;

	/* offset can be -ve */
	filep->f_pos = filep->f_pos + offset;

	return 0;
}

static int
dhd_init_logstrs_array(osl_t *osh, dhd_event_log_t *temp)
{
	struct file *filep = NULL;
	struct kstat stat;
	mm_segment_t fs;
	char *raw_fmts =  NULL;
	int logstrs_size = 0;
	int error = 0;

	fs = get_fs();
	set_fs(KERNEL_DS);

	filep = filp_open(logstrs_path, O_RDONLY, 0);

	if (IS_ERR(filep)) {
		DHD_ERROR(("%s: Failed to open the file %s \n", __FUNCTION__, logstrs_path));
		goto fail;
	}
	error = vfs_stat(logstrs_path, &stat);
	if (error) {
		DHD_ERROR(("%s: Failed to stat file %s \n", __FUNCTION__, logstrs_path));
		goto fail;
	}
	logstrs_size = (int) stat.size;

	if (logstrs_size == 0) {
		DHD_ERROR(("%s: return as logstrs_size is 0\n", __FUNCTION__));
		goto fail1;
	}

	raw_fmts = MALLOC(osh, logstrs_size);
	if (raw_fmts == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory \n", __FUNCTION__));
		goto fail;
	}
	if (vfs_read(filep, raw_fmts, logstrs_size, &filep->f_pos) !=	logstrs_size) {
		DHD_ERROR(("%s: Failed to read file %s\n", __FUNCTION__, logstrs_path));
		goto fail;
	}

	if (dhd_parse_logstrs_file(osh, raw_fmts, logstrs_size, temp)
				== BCME_OK) {
		filp_close(filep, NULL);
		set_fs(fs);
		return BCME_OK;
	}

fail:
	if (raw_fmts) {
		MFREE(osh, raw_fmts, logstrs_size);
		raw_fmts = NULL;
	}

fail1:
	if (!IS_ERR(filep))
		filp_close(filep, NULL);

	set_fs(fs);
	temp->fmts = NULL;
	return BCME_ERROR;
}

static int
dhd_read_map(osl_t *osh, char *fname, uint32 *ramstart, uint32 *rodata_start,
		uint32 *rodata_end)
{
	struct file *filep = NULL;
	mm_segment_t fs;
	int err = BCME_ERROR;

	if (fname == NULL) {
		DHD_ERROR(("%s: ERROR fname is NULL \n", __FUNCTION__));
		return BCME_ERROR;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	filep = filp_open(fname, O_RDONLY, 0);
	if (IS_ERR(filep)) {
		DHD_ERROR(("%s: Failed to open %s \n",  __FUNCTION__, fname));
		goto fail;
	}

	if ((err = dhd_parse_map_file(osh, filep, ramstart,
			rodata_start, rodata_end)) < 0)
		goto fail;

fail:
	if (!IS_ERR(filep))
		filp_close(filep, NULL);

	set_fs(fs);

	return err;
}

static int
dhd_init_static_strs_array(osl_t *osh, dhd_event_log_t *temp, char *str_file, char *map_file)
{
	struct file *filep = NULL;
	mm_segment_t fs;
	char *raw_fmts =  NULL;
	uint32 logstrs_size = 0;

	int error = 0;
	uint32 ramstart = 0;
	uint32 rodata_start = 0;
	uint32 rodata_end = 0;
	uint32 logfilebase = 0;

	error = dhd_read_map(osh, map_file, &ramstart, &rodata_start, &rodata_end);
	if (error != BCME_OK) {
		DHD_ERROR(("readmap Error!! \n"));
		/* don't do event log parsing in actual case */
		if (strstr(str_file, ram_file_str) != NULL) {
			temp->raw_sstr = NULL;
		} else if (strstr(str_file, rom_file_str) != NULL) {
			temp->rom_raw_sstr = NULL;
		}
		return error;
	}
	DHD_ERROR(("ramstart: 0x%x, rodata_start: 0x%x, rodata_end:0x%x\n",
		ramstart, rodata_start, rodata_end));

	fs = get_fs();
	set_fs(KERNEL_DS);

	filep = filp_open(str_file, O_RDONLY, 0);
	if (IS_ERR(filep)) {
		DHD_ERROR(("%s: Failed to open the file %s \n",  __FUNCTION__, str_file));
		goto fail;
	}

	/* Full file size is huge. Just read required part */
	logstrs_size = rodata_end - rodata_start;

	if (logstrs_size == 0) {
		DHD_ERROR(("%s: return as logstrs_size is 0\n", __FUNCTION__));
		goto fail1;
	}

	raw_fmts = MALLOC(osh, logstrs_size);
	if (raw_fmts == NULL) {
		DHD_ERROR(("%s: Failed to allocate raw_fmts memory \n", __FUNCTION__));
		goto fail;
	}

	logfilebase = rodata_start - ramstart;

	error = generic_file_llseek(filep, logfilebase, SEEK_SET);
	if (error < 0) {
		DHD_ERROR(("%s: %s llseek failed %d \n", __FUNCTION__, str_file, error));
		goto fail;
	}

	error = vfs_read(filep, raw_fmts, logstrs_size, (&filep->f_pos));
	if (error != logstrs_size) {
		DHD_ERROR(("%s: %s read failed %d \n", __FUNCTION__, str_file, error));
		goto fail;
	}

	if (strstr(str_file, ram_file_str) != NULL) {
		temp->raw_sstr = raw_fmts;
		temp->raw_sstr_size = logstrs_size;
		temp->ramstart = ramstart;
		temp->rodata_start = rodata_start;
		temp->rodata_end = rodata_end;
	} else if (strstr(str_file, rom_file_str) != NULL) {
		temp->rom_raw_sstr = raw_fmts;
		temp->rom_raw_sstr_size = logstrs_size;
		temp->rom_ramstart = ramstart;
		temp->rom_rodata_start = rodata_start;
		temp->rom_rodata_end = rodata_end;
	}

	filp_close(filep, NULL);
	set_fs(fs);

	return BCME_OK;

fail:
	if (raw_fmts) {
		MFREE(osh, raw_fmts, logstrs_size);
		raw_fmts = NULL;
	}

fail1:
	if (!IS_ERR(filep))
		filp_close(filep, NULL);

	set_fs(fs);

	if (strstr(str_file, ram_file_str) != NULL) {
		temp->raw_sstr = NULL;
	} else if (strstr(str_file, rom_file_str) != NULL) {
		temp->rom_raw_sstr = NULL;
	}

	return error;
}

#endif /* SHOW_LOGTRACE */

#ifdef BCMDBUS
uint
dhd_get_rxsz(dhd_pub_t *pub)
{
	struct net_device *net = NULL;
	dhd_info_t *dhd = NULL;
	uint rxsz;

	/* Assign rxsz for dbus_attach */
	dhd = pub->info;
	net = dhd->iflist[0]->net;
	net->hard_header_len = ETH_HLEN + pub->hdrlen;
	rxsz = DBUS_RX_BUFFER_SIZE_DHD(net);

	return rxsz;
}

void
dhd_set_path(dhd_pub_t *pub)
{
	dhd_info_t *dhd = NULL;

	dhd = pub->info;

	/* try to download image and nvram to the dongle */
	if	(dhd_update_fw_nv_path(dhd) && dhd->pub.bus) {
		DHD_INFO(("%s: fw %s, nv %s, conf %s\n",
			__FUNCTION__, dhd->fw_path, dhd->nv_path, dhd->conf_path));
		dhd_bus_update_fw_nv_path(dhd->pub.bus,
				dhd->fw_path, dhd->nv_path, dhd->clm_path, dhd->conf_path);
	}
}
#endif

dhd_pub_t *
dhd_attach(osl_t *osh, struct dhd_bus *bus, uint bus_hdrlen
#ifdef BCMDBUS
	, void *data
#endif
)
{
	dhd_info_t *dhd = NULL;
	struct net_device *net = NULL;
	char if_name[IFNAMSIZ] = {'\0'};
#ifdef SHOW_LOGTRACE
	int ret;
#endif /* SHOW_LOGTRACE */
#if defined(BCMSDIO) || defined(BCMPCIE)
	uint32 bus_type = -1;
	uint32 bus_num = -1;
	uint32 slot_num = -1;
	wifi_adapter_info_t *adapter = NULL;
#elif defined(BCMDBUS)
	wifi_adapter_info_t *adapter = data;
#endif
#ifdef GET_CUSTOM_MAC_ENABLE
	char hw_ether[62];
#endif /* GET_CUSTOM_MAC_ENABLE */

	dhd_attach_states_t dhd_state = DHD_ATTACH_STATE_INIT;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef STBLINUX
	DHD_ERROR(("%s\n", driver_target));
#endif /* STBLINUX */
	/* will implement get_ids for DBUS later */
#if defined(BCMSDIO)
	dhd_bus_get_ids(bus, &bus_type, &bus_num, &slot_num);
#endif 
#if defined(BCMSDIO) || defined(BCMPCIE)
	adapter = dhd_wifi_platform_get_adapter(bus_type, bus_num, slot_num);
#endif

	/* Allocate primary dhd_info */
	dhd = wifi_platform_prealloc(adapter, DHD_PREALLOC_DHD_INFO, sizeof(dhd_info_t));
	if (dhd == NULL) {
		dhd = MALLOC(osh, sizeof(dhd_info_t));
		if (dhd == NULL) {
			DHD_ERROR(("%s: OOM - alloc dhd_info\n", __FUNCTION__));
			goto dhd_null_flag;
		}
	}
	memset(dhd, 0, sizeof(dhd_info_t));
	dhd_state |= DHD_ATTACH_STATE_DHD_ALLOC;

	dhd->unit = dhd_found + instance_base; /* do not increment dhd_found, yet */

	dhd->pub.osh = osh;
#ifdef DUMP_IOCTL_IOV_LIST
	dll_init(&(dhd->pub.dump_iovlist_head));
#endif /* DUMP_IOCTL_IOV_LIST */
	dhd->adapter = adapter;
	dhd->pub.adapter = (void *)adapter;
#ifdef DHD_DEBUG
	dll_init(&(dhd->pub.mw_list_head));
#endif /* DHD_DEBUG */
#ifdef BT_OVER_SDIO
	dhd->pub.is_bt_recovery_required = FALSE;
	mutex_init(&dhd->bus_user_lock);
#endif /* BT_OVER_SDIO */

#ifdef GET_CUSTOM_MAC_ENABLE
	wifi_platform_get_mac_addr(dhd->adapter, hw_ether);
	bcopy(hw_ether, dhd->pub.mac.octet, sizeof(struct ether_addr));
#endif /* GET_CUSTOM_MAC_ENABLE */
#ifdef CUSTOM_FORCE_NODFS_FLAG
	dhd->pub.dhd_cflags |= WLAN_PLAT_NODFS_FLAG;
	dhd->pub.force_country_change = TRUE;
#endif /* CUSTOM_FORCE_NODFS_FLAG */
#ifdef CUSTOM_COUNTRY_CODE
	get_customized_country_code(dhd->adapter,
		dhd->pub.dhd_cspec.country_abbrev, &dhd->pub.dhd_cspec,
		dhd->pub.dhd_cflags);
#endif /* CUSTOM_COUNTRY_CODE */
#ifndef BCMDBUS
	dhd->thr_dpc_ctl.thr_pid = DHD_PID_KT_TL_INVALID;
	dhd->thr_wdt_ctl.thr_pid = DHD_PID_KT_INVALID;
#ifdef DHD_WET
	dhd->pub.wet_info = dhd_get_wet_info(&dhd->pub);
#endif /* DHD_WET */
	/* Initialize thread based operation and lock */
	sema_init(&dhd->sdsem, 1);
#endif /* !BCMDBUS */

	/* Link to info module */
	dhd->pub.info = dhd;


	/* Link to bus module */
	dhd->pub.bus = bus;
	dhd->pub.hdrlen = bus_hdrlen;

	/* dhd_conf must be attached after linking dhd to dhd->pub.info,
	 * because dhd_detech will check .info is NULL or not.
	*/
	if (dhd_conf_attach(&dhd->pub) != 0) {
		DHD_ERROR(("dhd_conf_attach failed\n"));
		goto fail;
	}
#ifndef BCMDBUS
	dhd_conf_reset(&dhd->pub);
	dhd_conf_set_chiprev(&dhd->pub, dhd_bus_chip(bus), dhd_bus_chiprev(bus));
	dhd_conf_preinit(&dhd->pub);
#endif /* !BCMDBUS */

	/* Some DHD modules (e.g. cfg80211) configures operation mode based on firmware name.
	 * This is indeed a hack but we have to make it work properly before we have a better
	 * solution
	 */
	dhd_update_fw_nv_path(dhd);

	/* Set network interface name if it was provided as module parameter */
	if (iface_name[0]) {
		int len;
		char ch;
		strncpy(if_name, iface_name, IFNAMSIZ);
		if_name[IFNAMSIZ - 1] = 0;
		len = strlen(if_name);
		ch = if_name[len - 1];
		if ((ch > '9' || ch < '0') && (len < IFNAMSIZ - 2))
			strlcat(if_name, "%d", IFNAMSIZ);
	}

	/* Passing NULL to dngl_name to ensure host gets if_name in dngl_name member */
	net = dhd_allocate_if(&dhd->pub, 0, if_name, NULL, 0, TRUE, NULL);
	if (net == NULL) {
		goto fail;
	}


	dhd_state |= DHD_ATTACH_STATE_ADD_IF;
#ifdef DHD_L2_FILTER
	/* initialize the l2_filter_cnt */
	dhd->pub.l2_filter_cnt = 0;
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
	net->open = NULL;
#else
	net->netdev_ops = NULL;
#endif

	mutex_init(&dhd->dhd_iovar_mutex);
	sema_init(&dhd->proto_sem, 1);
#ifdef DHD_ULP
	if (!(dhd_ulp_init(osh, &dhd->pub)))
		goto fail;
#endif /* DHD_ULP */

#if defined(DHD_HANG_SEND_UP_TEST)
	dhd->pub.req_hang_type = 0;
#endif /* DHD_HANG_SEND_UP_TEST */

#ifdef PROP_TXSTATUS
	spin_lock_init(&dhd->wlfc_spinlock);

	dhd->pub.skip_fc = dhd_wlfc_skip_fc;
	dhd->pub.plat_init = dhd_wlfc_plat_init;
	dhd->pub.plat_deinit = dhd_wlfc_plat_deinit;

#ifdef DHD_WLFC_THREAD
	init_waitqueue_head(&dhd->pub.wlfc_wqhead);
	dhd->pub.wlfc_thread = kthread_create(dhd_wlfc_transfer_packets, &dhd->pub, "wlfc-thread");
	if (IS_ERR(dhd->pub.wlfc_thread)) {
		DHD_ERROR(("create wlfc thread failed\n"));
		goto fail;
	} else {
		wake_up_process(dhd->pub.wlfc_thread);
	}
#endif /* DHD_WLFC_THREAD */
#endif /* PROP_TXSTATUS */

	/* Initialize other structure content */
	init_waitqueue_head(&dhd->ioctl_resp_wait);
	init_waitqueue_head(&dhd->d3ack_wait);
#ifdef PCIE_INB_DW
	init_waitqueue_head(&dhd->ds_exit_wait);
#endif /* PCIE_INB_DW */
	init_waitqueue_head(&dhd->ctrl_wait);
	init_waitqueue_head(&dhd->dhd_bus_busy_state_wait);
	dhd->pub.dhd_bus_busy_state = 0;

	/* Initialize the spinlocks */
	spin_lock_init(&dhd->sdlock);
	spin_lock_init(&dhd->txqlock);
	spin_lock_init(&dhd->rxqlock);
	spin_lock_init(&dhd->dhd_lock);
	spin_lock_init(&dhd->rxf_lock);
#ifdef WLTDLS
	spin_lock_init(&dhd->pub.tdls_lock);
#endif /* WLTDLS */
#if defined(RXFRAME_THREAD)
	dhd->rxthread_enabled = TRUE;
#endif /* defined(RXFRAME_THREAD) */

#ifdef DHDTCPACK_SUPPRESS
	spin_lock_init(&dhd->tcpack_lock);
#endif /* DHDTCPACK_SUPPRESS */

	/* Initialize Wakelock stuff */
	spin_lock_init(&dhd->wakelock_spinlock);
	spin_lock_init(&dhd->wakelock_evt_spinlock);
	DHD_OS_WAKE_LOCK_INIT(dhd);
	dhd->wakelock_counter = 0;
#ifdef CONFIG_HAS_WAKELOCK
	// terence 20161023: can not destroy wl_wifi when wlan down, it will happen null pointer in dhd_ioctl_entry
	wake_lock_init(&dhd->wl_wifi, WAKE_LOCK_SUSPEND, "wlan_wake");
	wake_lock_init(&dhd->wl_wdwake, WAKE_LOCK_SUSPEND, "wlan_wd_wake");
#endif /* CONFIG_HAS_WAKELOCK */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	mutex_init(&dhd->dhd_net_if_mutex);
	mutex_init(&dhd->dhd_suspend_mutex);
#if defined(PKT_FILTER_SUPPORT) && defined(APF)
	mutex_init(&dhd->dhd_apf_mutex);
#endif /* PKT_FILTER_SUPPORT && APF */
#endif 
	dhd_state |= DHD_ATTACH_STATE_WAKELOCKS_INIT;

	/* Attach and link in the protocol */
	if (dhd_prot_attach(&dhd->pub) != 0) {
		DHD_ERROR(("dhd_prot_attach failed\n"));
		goto fail;
	}
	dhd_state |= DHD_ATTACH_STATE_PROT_ATTACH;

#ifdef DHD_TIMESYNC
	/* attach the timesync module */
	if (dhd_timesync_attach(&dhd->pub) != 0) {
		DHD_ERROR(("dhd_timesync_attach failed\n"));
		goto fail;
	}
	dhd_state |= DHD_ATTACH_TIMESYNC_ATTACH_DONE;
#endif /* DHD_TIMESYNC */

#ifdef WL_CFG80211
	spin_lock_init(&dhd->pub.up_lock);
	/* Attach and link in the cfg80211 */
	if (unlikely(wl_cfg80211_attach(net, &dhd->pub))) {
		DHD_ERROR(("wl_cfg80211_attach failed\n"));
		goto fail;
	}

	dhd_monitor_init(&dhd->pub);
	dhd_state |= DHD_ATTACH_STATE_CFG80211;
#endif
#ifdef DHD_LOG_DUMP
	dhd_log_dump_init(&dhd->pub);
#endif /* DHD_LOG_DUMP */
#if defined(WL_EXT_IAPSTA) || defined(USE_IW) || defined(WL_ESCAN)
	if (wl_ext_event_attach(net, &dhd->pub) != 0) {
		DHD_ERROR(("wl_ext_event_attach failed\n"));
		goto fail;
	}
#ifdef WL_ESCAN
	/* Attach and link in the escan */
	if (wl_escan_attach(net, &dhd->pub) != 0) {
		DHD_ERROR(("wl_escan_attach failed\n"));
		goto fail;
	}
#endif /* WL_ESCAN */
#ifdef WL_EXT_IAPSTA
	if (wl_ext_iapsta_attach(&dhd->pub) != 0) {
		DHD_ERROR(("wl_ext_iapsta_attach failed\n"));
		goto fail;
	}
#endif /* WL_EXT_IAPSTA */
#endif /* WL_EXT_IAPSTA || USE_IW || WL_ESCAN */
#if defined(WL_WIRELESS_EXT)
	/* Attach and link in the iw */
	if (wl_iw_attach(net, &dhd->pub) != 0) {
		DHD_ERROR(("wl_iw_attach failed\n"));
		goto fail;
	}
	dhd_state |= DHD_ATTACH_STATE_WL_ATTACH;
#endif /* defined(WL_WIRELESS_EXT) */

#ifdef SHOW_LOGTRACE
	ret = dhd_init_logstrs_array(osh, &dhd->event_data);
	if (ret == BCME_OK) {
		dhd_init_static_strs_array(osh, &dhd->event_data, st_str_file_path, map_file_path);
		dhd_init_static_strs_array(osh, &dhd->event_data, rom_st_str_file_path,
			rom_map_file_path);
		dhd_state |= DHD_ATTACH_LOGTRACE_INIT;
	}
#endif /* SHOW_LOGTRACE */

#ifdef DEBUGABILITY
	/* attach debug if support */
	if (dhd_os_dbg_attach(&dhd->pub)) {
		DHD_ERROR(("%s debug module attach failed\n", __FUNCTION__));
		goto fail;
	}

#ifdef DBG_PKT_MON
	dhd->pub.dbg->pkt_mon_lock = dhd_os_spin_lock_init(dhd->pub.osh);
#ifdef DBG_PKT_MON_INIT_DEFAULT
	dhd_os_dbg_attach_pkt_monitor(&dhd->pub);
#endif /* DBG_PKT_MON_INIT_DEFAULT */
#endif /* DBG_PKT_MON */
#endif /* DEBUGABILITY */
#ifdef DHD_PKT_LOGGING
	dhd_os_attach_pktlog(&dhd->pub);
#endif /* DHD_PKT_LOGGING */

	if (dhd_sta_pool_init(&dhd->pub, DHD_MAX_STA) != BCME_OK) {
		DHD_ERROR(("%s: Initializing %u sta\n", __FUNCTION__, DHD_MAX_STA));
		goto fail;
	}



#ifndef BCMDBUS
	/* Set up the watchdog timer */
	init_timer_compat(&dhd->timer, dhd_watchdog, dhd);
	dhd->default_wd_interval = dhd_watchdog_ms;

	if (dhd_watchdog_prio >= 0) {
		/* Initialize watchdog thread */
		PROC_START(dhd_watchdog_thread, dhd, &dhd->thr_wdt_ctl, 0, "dhd_watchdog_thread");
		if (dhd->thr_wdt_ctl.thr_pid < 0) {
			goto fail;
		}

	} else {
		dhd->thr_wdt_ctl.thr_pid = -1;
	}

#ifdef DHD_PCIE_RUNTIMEPM
	/* Setup up the runtime PM Idlecount timer */
	init_timer_compat(&dhd->rpm_timer, dhd_runtimepm, dhd);
	dhd->rpm_timer_valid = FALSE;

	dhd->thr_rpm_ctl.thr_pid = DHD_PID_KT_INVALID;
	PROC_START(dhd_rpm_state_thread, dhd, &dhd->thr_rpm_ctl, 0, "dhd_rpm_state_thread");
	if (dhd->thr_rpm_ctl.thr_pid < 0) {
		goto fail;
	}
#endif /* DHD_PCIE_RUNTIMEPM */

#ifdef DEBUGGER
	debugger_init((void *) bus);
#endif

	/* Set up the bottom half handler */
	if (dhd_dpc_prio >= 0) {
		/* Initialize DPC thread */
		PROC_START(dhd_dpc_thread, dhd, &dhd->thr_dpc_ctl, 0, "dhd_dpc");
		if (dhd->thr_dpc_ctl.thr_pid < 0) {
			goto fail;
		}
	} else {
		/*  use tasklet for dpc */
		tasklet_init(&dhd->tasklet, dhd_dpc, (ulong)dhd);
		dhd->thr_dpc_ctl.thr_pid = -1;
	}

	if (dhd->rxthread_enabled) {
		bzero(&dhd->pub.skbbuf[0], sizeof(void *) * MAXSKBPEND);
		/* Initialize RXF thread */
		PROC_START(dhd_rxf_thread, dhd, &dhd->thr_rxf_ctl, 0, "dhd_rxf");
		if (dhd->thr_rxf_ctl.thr_pid < 0) {
			goto fail;
		}
	}
#endif /* !BCMDBUS */
#ifdef SHOW_LOGTRACE
	skb_queue_head_init(&dhd->evt_trace_queue);
#endif /* SHOW_LOGTRACE */

	dhd_state |= DHD_ATTACH_STATE_THREADS_CREATED;

#if defined(CONFIG_PM_SLEEP)
	if (!dhd_pm_notifier_registered) {
		dhd_pm_notifier_registered = TRUE;
		dhd->pm_notifier.notifier_call = dhd_pm_callback;
		dhd->pm_notifier.priority = 10;
		register_pm_notifier(&dhd->pm_notifier);
	}

#endif /* CONFIG_PM_SLEEP */

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
	dhd->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 20;
	dhd->early_suspend.suspend = dhd_early_suspend;
	dhd->early_suspend.resume = dhd_late_resume;
	register_early_suspend(&dhd->early_suspend);
	dhd_state |= DHD_ATTACH_STATE_EARLYSUSPEND_DONE;
#endif /* CONFIG_HAS_EARLYSUSPEND && DHD_USE_EARLYSUSPEND */

#ifdef ARP_OFFLOAD_SUPPORT
	dhd->pend_ipaddr = 0;
	if (!dhd_inetaddr_notifier_registered) {
		dhd_inetaddr_notifier_registered = TRUE;
		register_inetaddr_notifier(&dhd_inetaddr_notifier);
	}
#endif /* ARP_OFFLOAD_SUPPORT */

#if defined(CONFIG_IPV6) && defined(IPV6_NDO_SUPPORT)
	if (!dhd_inet6addr_notifier_registered) {
		dhd_inet6addr_notifier_registered = TRUE;
		register_inet6addr_notifier(&dhd_inet6addr_notifier);
	}
#endif /* CONFIG_IPV6 && IPV6_NDO_SUPPORT */
	dhd->dhd_deferred_wq = dhd_deferred_work_init((void *)dhd);
#ifdef DEBUG_CPU_FREQ
	dhd->new_freq = alloc_percpu(int);
	dhd->freq_trans.notifier_call = dhd_cpufreq_notifier;
	cpufreq_register_notifier(&dhd->freq_trans, CPUFREQ_TRANSITION_NOTIFIER);
#endif
#ifdef DHDTCPACK_SUPPRESS
	dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_DEFAULT);
#endif /* DHDTCPACK_SUPPRESS */

#if defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW)
#endif /* defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW) */


#ifdef DHD_DEBUG_PAGEALLOC
	register_page_corrupt_cb(dhd_page_corrupt_cb, &dhd->pub);
#endif /* DHD_DEBUG_PAGEALLOC */

#if defined(DHD_LB)

	dhd_lb_set_default_cpus(dhd);

	/* Initialize the CPU Masks */
	if (dhd_cpumasks_init(dhd) == 0) {
		/* Now we have the current CPU maps, run through candidacy */
		dhd_select_cpu_candidacy(dhd);
		/*
		* If we are able to initialize CPU masks, lets register to the
		* CPU Hotplug framework to change the CPU for each job dynamically
		* using candidacy algorithm.
		*/
		dhd->cpu_notifier.notifier_call = dhd_cpu_callback;
		register_hotcpu_notifier(&dhd->cpu_notifier); /* Register a callback */
	} else {
		/*
		* We are unable to initialize CPU masks, so candidacy algorithm
		* won't run, but still Load Balancing will be honoured based
		* on the CPUs allocated for a given job statically during init
		*/
		dhd->cpu_notifier.notifier_call = NULL;
		DHD_ERROR(("%s():dhd_cpumasks_init failed CPUs for JOB would be static\n",
			__FUNCTION__));
	}

#ifdef DHD_LB_TXP
#ifdef DHD_LB_TXP_DEFAULT_ENAB
	/* Trun ON the feature by default */
	atomic_set(&dhd->lb_txp_active, 1);
#else
	/* Trun OFF the feature by default */
	atomic_set(&dhd->lb_txp_active, 0);
#endif /* DHD_LB_TXP_DEFAULT_ENAB */
#endif /* DHD_LB_TXP */

	DHD_LB_STATS_INIT(&dhd->pub);

	/* Initialize the Load Balancing Tasklets and Napi object */
#if defined(DHD_LB_TXC)
	tasklet_init(&dhd->tx_compl_tasklet,
		dhd_lb_tx_compl_handler, (ulong)(&dhd->pub));
	INIT_WORK(&dhd->tx_compl_dispatcher_work, dhd_tx_compl_dispatcher_fn);
	DHD_INFO(("%s load balance init tx_compl_tasklet\n", __FUNCTION__));
#endif /* DHD_LB_TXC */

#if defined(DHD_LB_RXC)
	tasklet_init(&dhd->rx_compl_tasklet,
		dhd_lb_rx_compl_handler, (ulong)(&dhd->pub));
	DHD_INFO(("%s load balance init rx_compl_tasklet\n", __FUNCTION__));
#endif /* DHD_LB_RXC */

#if defined(DHD_LB_RXP)
	__skb_queue_head_init(&dhd->rx_pend_queue);
	skb_queue_head_init(&dhd->rx_napi_queue);
	/* Initialize the work that dispatches NAPI job to a given core */
	INIT_WORK(&dhd->rx_napi_dispatcher_work, dhd_rx_napi_dispatcher_fn);
	DHD_INFO(("%s load balance init rx_napi_queue\n", __FUNCTION__));
#endif /* DHD_LB_RXP */

#if defined(DHD_LB_TXP)
	INIT_WORK(&dhd->tx_dispatcher_work, dhd_tx_dispatcher_work);
	skb_queue_head_init(&dhd->tx_pend_queue);
	/* Initialize the work that dispatches TX job to a given core */
	tasklet_init(&dhd->tx_tasklet,
		dhd_lb_tx_handler, (ulong)(dhd));
	DHD_INFO(("%s load balance init tx_pend_queue\n", __FUNCTION__));
#endif /* DHD_LB_TXP */

	dhd_state |= DHD_ATTACH_STATE_LB_ATTACH_DONE;
#endif /* DHD_LB */

#ifdef SHOW_LOGTRACE
	INIT_WORK(&dhd->event_log_dispatcher_work, dhd_event_logtrace_process);
#endif /* SHOW_LOGTRACE */

	DHD_SSSR_MEMPOOL_INIT(&dhd->pub);

#ifdef REPORT_FATAL_TIMEOUTS
	init_dhd_timeouts(&dhd->pub);
#endif /* REPORT_FATAL_TIMEOUTS */
#ifdef BCMPCIE
	dhd->pub.extended_trap_data = MALLOCZ(osh, BCMPCIE_EXT_TRAP_DATA_MAXLEN);
	if (dhd->pub.extended_trap_data == NULL) {
		DHD_ERROR(("%s: Failed to alloc extended_trap_data\n", __FUNCTION__));
	}
#endif /* BCMPCIE */

	(void)dhd_sysfs_init(dhd);

	dhd_state |= DHD_ATTACH_STATE_DONE;
	dhd->dhd_state = dhd_state;

	dhd_found++;

	return &dhd->pub;

fail:
	if (dhd_state >= DHD_ATTACH_STATE_DHD_ALLOC) {
		DHD_TRACE(("%s: Calling dhd_detach dhd_state 0x%x &dhd->pub %p\n",
			__FUNCTION__, dhd_state, &dhd->pub));
		dhd->dhd_state = dhd_state;
		dhd_detach(&dhd->pub);
		dhd_free(&dhd->pub);
	}
dhd_null_flag:
	return NULL;
}

int dhd_get_fw_mode(dhd_info_t *dhdinfo)
{
	if (strstr(dhdinfo->fw_path, "_apsta") != NULL)
		return DHD_FLAG_HOSTAP_MODE;
	if (strstr(dhdinfo->fw_path, "_p2p") != NULL)
		return DHD_FLAG_P2P_MODE;
	if (strstr(dhdinfo->fw_path, "_ibss") != NULL)
		return DHD_FLAG_IBSS_MODE;
	if (strstr(dhdinfo->fw_path, "_mfg") != NULL)
		return DHD_FLAG_MFG_MODE;

	return DHD_FLAG_STA_MODE;
}

int dhd_bus_get_fw_mode(dhd_pub_t *dhdp)
{
	return dhd_get_fw_mode(dhdp->info);
}

extern int rkwifi_set_firmware(char *fw, char *nvram);
bool dhd_update_fw_nv_path(dhd_info_t *dhdinfo)
{
	int fw_len;
	int nv_len;
	int clm_len;
	int conf_len;
	const char *fw = NULL;
	const char *nv = NULL;
	const char *clm = NULL;
	const char *conf = NULL;
#ifdef DHD_UCODE_DOWNLOAD
	int uc_len;
	const char *uc = NULL;
#endif /* DHD_UCODE_DOWNLOAD */
	char firmware[100] = {0};
	char nvram[100] = {0};
	wifi_adapter_info_t *adapter = dhdinfo->adapter;
	int fw_path_len = sizeof(dhdinfo->fw_path);
	int nv_path_len = sizeof(dhdinfo->nv_path);


	/* Update firmware and nvram path. The path may be from adapter info or module parameter
	 * The path from adapter info is used for initialization only (as it won't change).
	 *
	 * The firmware_path/nvram_path module parameter may be changed by the system at run
	 * time. When it changes we need to copy it to dhdinfo->fw_path. Also Android private
	 * command may change dhdinfo->fw_path. As such we need to clear the path info in
	 * module parameter after it is copied. We won't update the path until the module parameter
	 * is changed again (first character is not '\0')
	 */

	/* set default firmware and nvram path for built-in type driver */
//	if (!dhd_download_fw_on_driverload) {
		rkwifi_set_firmware(firmware, nvram);
#ifdef CONFIG_BCMDHD_FW_PATH
		fw = CONFIG_BCMDHD_FW_PATH;
#else
		fw = firmware;
#endif /* CONFIG_BCMDHD_FW_PATH */
#ifdef CONFIG_BCMDHD_NVRAM_PATH
		nv = CONFIG_BCMDHD_NVRAM_PATH;
#else
		nv = nvram;
#endif /* CONFIG_BCMDHD_NVRAM_PATH */
//	}

	/* check if we need to initialize the path */
	if (dhdinfo->fw_path[0] == '\0') {
		if (adapter && adapter->fw_path && adapter->fw_path[0] != '\0')
			fw = adapter->fw_path;

	}
	if (dhdinfo->nv_path[0] == '\0') {
		if (adapter && adapter->nv_path && adapter->nv_path[0] != '\0')
			nv = adapter->nv_path;
	}
	if (dhdinfo->clm_path[0] == '\0') {
		if (adapter && adapter->clm_path && adapter->clm_path[0] != '\0')
			clm = adapter->clm_path;
	}
	if (dhdinfo->conf_path[0] == '\0') {
		if (adapter && adapter->conf_path && adapter->conf_path[0] != '\0')
			conf = adapter->conf_path;
	}

	/* Use module parameter if it is valid, EVEN IF the path has not been initialized
	 *
	 * TODO: need a solution for multi-chip, can't use the same firmware for all chips
	 */
	if (firmware_path[0] != '\0')
		fw = firmware_path;
	if (nvram_path[0] != '\0')
		nv = nvram_path;
	if (clm_path[0] != '\0')
		clm = clm_path;
	if (config_path[0] != '\0')
		conf = config_path;
#ifdef DHD_UCODE_DOWNLOAD
	if (ucode_path[0] != '\0')
		uc = ucode_path;
#endif /* DHD_UCODE_DOWNLOAD */

	if (fw && fw[0] != '\0') {
		fw_len = strlen(fw);
		if (fw_len >= fw_path_len) {
			DHD_ERROR(("fw path len exceeds max len of dhdinfo->fw_path\n"));
			return FALSE;
		}
		strncpy(dhdinfo->fw_path, fw, fw_path_len);
		if (dhdinfo->fw_path[fw_len-1] == '\n')
		       dhdinfo->fw_path[fw_len-1] = '\0';
	}
	if (nv && nv[0] != '\0') {
		nv_len = strlen(nv);
		if (nv_len >= nv_path_len) {
			DHD_ERROR(("nvram path len exceeds max len of dhdinfo->nv_path\n"));
			return FALSE;
		}
		memset(dhdinfo->nv_path, 0, nv_path_len);
		strncpy(dhdinfo->nv_path, nv, nv_path_len);
#ifdef DHD_USE_SINGLE_NVRAM_FILE
		/* Remove "_net" or "_mfg" tag from current nvram path */
		{
			char *nvram_tag = "nvram_";
			char *ext_tag = ".txt";
			char *sp_nvram = strnstr(dhdinfo->nv_path, nvram_tag, nv_path_len);
			bool valid_buf = sp_nvram && ((uint32)(sp_nvram + strlen(nvram_tag) +
				strlen(ext_tag) - dhdinfo->nv_path) <= nv_path_len);
			if (valid_buf) {
				char *sp = sp_nvram + strlen(nvram_tag) - 1;
				uint32 padding_size = (uint32)(dhdinfo->nv_path +
					nv_path_len - sp);
				memset(sp, 0, padding_size);
				strncat(dhdinfo->nv_path, ext_tag, strlen(ext_tag));
				nv_len = strlen(dhdinfo->nv_path);
				DHD_INFO(("%s: new nvram path = %s\n",
					__FUNCTION__, dhdinfo->nv_path));
			} else if (sp_nvram) {
				DHD_ERROR(("%s: buffer space for nvram path is not enough\n",
					__FUNCTION__));
				return FALSE;
			} else {
				DHD_ERROR(("%s: Couldn't find the nvram tag. current"
					" nvram path = %s\n", __FUNCTION__, dhdinfo->nv_path));
			}
		}
#endif /* DHD_USE_SINGLE_NVRAM_FILE */
		if (dhdinfo->nv_path[nv_len-1] == '\n')
		       dhdinfo->nv_path[nv_len-1] = '\0';
	}
	if (clm && clm[0] != '\0') {
		clm_len = strlen(clm);
		if (clm_len >= sizeof(dhdinfo->clm_path)) {
			DHD_ERROR(("clm path len exceeds max len of dhdinfo->clm_path\n"));
			return FALSE;
		}
		strncpy(dhdinfo->clm_path, clm, sizeof(dhdinfo->clm_path));
		if (dhdinfo->clm_path[clm_len-1] == '\n')
		       dhdinfo->clm_path[clm_len-1] = '\0';
	}
	if (conf && conf[0] != '\0') {
		conf_len = strlen(conf);
		if (conf_len >= sizeof(dhdinfo->conf_path)) {
			DHD_ERROR(("config path len exceeds max len of dhdinfo->conf_path\n"));
			return FALSE;
		}
		strncpy(dhdinfo->conf_path, conf, sizeof(dhdinfo->conf_path));
		if (dhdinfo->conf_path[conf_len-1] == '\n')
		       dhdinfo->conf_path[conf_len-1] = '\0';
	}
#ifdef DHD_UCODE_DOWNLOAD
	if (uc && uc[0] != '\0') {
		uc_len = strlen(uc);
		if (uc_len >= sizeof(dhdinfo->uc_path)) {
			DHD_ERROR(("uc path len exceeds max len of dhdinfo->uc_path\n"));
			return FALSE;
		}
		strncpy(dhdinfo->uc_path, uc, sizeof(dhdinfo->uc_path));
		if (dhdinfo->uc_path[uc_len-1] == '\n')
		       dhdinfo->uc_path[uc_len-1] = '\0';
	}
#endif /* DHD_UCODE_DOWNLOAD */

#if 0
	/* clear the path in module parameter */
	if (dhd_download_fw_on_driverload) {
		firmware_path[0] = '\0';
		nvram_path[0] = '\0';
		clm_path[0] = '\0';
		config_path[0] = '\0';
	}
#endif
#ifdef DHD_UCODE_DOWNLOAD
	ucode_path[0] = '\0';
	DHD_ERROR(("ucode path: %s\n", dhdinfo->uc_path));
#endif /* DHD_UCODE_DOWNLOAD */

#ifndef BCMEMBEDIMAGE
	/* fw_path and nv_path are not mandatory for BCMEMBEDIMAGE */
	if (dhdinfo->fw_path[0] == '\0') {
		DHD_ERROR(("firmware path not found\n"));
		return FALSE;
	}
	if (dhdinfo->nv_path[0] == '\0') {
		DHD_ERROR(("nvram path not found\n"));
		return FALSE;
	}
#endif /* BCMEMBEDIMAGE */

	return TRUE;
}

#if defined(BT_OVER_SDIO)
extern bool dhd_update_btfw_path(dhd_info_t *dhdinfo, char* btfw_path)
{
	int fw_len;
	const char *fw = NULL;
	wifi_adapter_info_t *adapter = dhdinfo->adapter;


	/* Update bt firmware path. The path may be from adapter info or module parameter
	 * The path from adapter info is used for initialization only (as it won't change).
	 *
	 * The btfw_path module parameter may be changed by the system at run
	 * time. When it changes we need to copy it to dhdinfo->btfw_path. Also Android private
	 * command may change dhdinfo->btfw_path. As such we need to clear the path info in
	 * module parameter after it is copied. We won't update the path until the module parameter
	 * is changed again (first character is not '\0')
	 */

	/* set default firmware and nvram path for built-in type driver */
	if (!dhd_download_fw_on_driverload) {
#ifdef CONFIG_BCMDHD_BTFW_PATH
		fw = CONFIG_BCMDHD_BTFW_PATH;
#endif /* CONFIG_BCMDHD_FW_PATH */
	}

	/* check if we need to initialize the path */
	if (dhdinfo->btfw_path[0] == '\0') {
		if (adapter && adapter->btfw_path && adapter->btfw_path[0] != '\0')
			fw = adapter->btfw_path;
	}

	/* Use module parameter if it is valid, EVEN IF the path has not been initialized
	 */
	if (btfw_path[0] != '\0')
		fw = btfw_path;

	if (fw && fw[0] != '\0') {
		fw_len = strlen(fw);
		if (fw_len >= sizeof(dhdinfo->btfw_path)) {
			DHD_ERROR(("fw path len exceeds max len of dhdinfo->btfw_path\n"));
			return FALSE;
		}
		strncpy(dhdinfo->btfw_path, fw, sizeof(dhdinfo->btfw_path));
		if (dhdinfo->btfw_path[fw_len-1] == '\n')
		       dhdinfo->btfw_path[fw_len-1] = '\0';
	}

	/* clear the path in module parameter */
	btfw_path[0] = '\0';

	if (dhdinfo->btfw_path[0] == '\0') {
		DHD_ERROR(("bt firmware path not found\n"));
		return FALSE;
	}

	return TRUE;
}
#endif /* defined (BT_OVER_SDIO) */


#ifdef CUSTOMER_HW4_DEBUG
bool dhd_validate_chipid(dhd_pub_t *dhdp)
{
	uint chipid = dhd_bus_chip_id(dhdp);
	uint config_chipid;

#ifdef BCM4361_CHIP
	config_chipid = BCM4361_CHIP_ID;
#elif defined(BCM4359_CHIP)
	config_chipid = BCM4359_CHIP_ID;
#elif defined(BCM4358_CHIP)
	config_chipid = BCM4358_CHIP_ID;
#elif defined(BCM4354_CHIP)
	config_chipid = BCM4354_CHIP_ID;
#elif defined(BCM4339_CHIP)
	config_chipid = BCM4339_CHIP_ID;
#elif defined(BCM43349_CHIP)
	config_chipid = BCM43349_CHIP_ID;
#elif defined(BCM4335_CHIP)
	config_chipid = BCM4335_CHIP_ID;
#elif defined(BCM43241_CHIP)
	config_chipid = BCM4324_CHIP_ID;
#elif defined(BCM4330_CHIP)
	config_chipid = BCM4330_CHIP_ID;
#elif defined(BCM43430_CHIP)
	config_chipid = BCM43430_CHIP_ID;
#elif defined(BCM43018_CHIP)
	config_chipid = BCM43018_CHIP_ID;
#elif defined(BCM43455_CHIP)
	config_chipid = BCM4345_CHIP_ID;
#elif defined(BCM4334W_CHIP)
	config_chipid = BCM43342_CHIP_ID;
#elif defined(BCM43454_CHIP)
	config_chipid = BCM43454_CHIP_ID;
#elif defined(BCM43012_CHIP_)
	config_chipid = BCM43012_CHIP_ID;
#else
	DHD_ERROR(("%s: Unknown chip id, if you use new chipset,"
		" please add CONFIG_BCMXXXX into the Kernel and"
		" BCMXXXX_CHIP definition into the DHD driver\n",
		__FUNCTION__));
	config_chipid = 0;

	return FALSE;
#endif /* BCM4354_CHIP */

#ifdef SUPPORT_MULTIPLE_CHIP_4345X
	if (config_chipid == BCM43454_CHIP_ID || config_chipid == BCM4345_CHIP_ID) {
		return TRUE;
	}
#endif /* SUPPORT_MULTIPLE_CHIP_4345X */
#if defined(BCM4359_CHIP)
	if (chipid == BCM4355_CHIP_ID && config_chipid == BCM4359_CHIP_ID) {
		return TRUE;
	}
#endif /* BCM4359_CHIP */
#if defined(BCM4361_CHIP)
	if (chipid == BCM4347_CHIP_ID && config_chipid == BCM4361_CHIP_ID) {
		return TRUE;
	}
#endif /* BCM4361_CHIP */

	return config_chipid == chipid;
}
#endif /* CUSTOMER_HW4_DEBUG */

#if defined(BT_OVER_SDIO)
wlan_bt_handle_t dhd_bt_get_pub_hndl(void)
{
	DHD_ERROR(("%s: g_dhd_pub %p\n", __FUNCTION__, g_dhd_pub));
	/* assuming that dhd_pub_t type pointer is available from a global variable */
	return (wlan_bt_handle_t) g_dhd_pub;
} EXPORT_SYMBOL(dhd_bt_get_pub_hndl);

int dhd_download_btfw(wlan_bt_handle_t handle, char* btfw_path)
{
	int ret = -1;
	dhd_pub_t *dhdp = (dhd_pub_t *)handle;
	dhd_info_t *dhd = (dhd_info_t*)dhdp->info;


	/* Download BT firmware image to the dongle */
	if (dhd->pub.busstate == DHD_BUS_DATA && dhd_update_btfw_path(dhd, btfw_path)) {
		DHD_INFO(("%s: download btfw from: %s\n", __FUNCTION__, dhd->btfw_path));
		ret = dhd_bus_download_btfw(dhd->pub.bus, dhd->pub.osh, dhd->btfw_path);
		if (ret < 0) {
			DHD_ERROR(("%s: failed to download btfw from: %s\n",
				__FUNCTION__, dhd->btfw_path));
			return ret;
		}
	}
	return ret;
} EXPORT_SYMBOL(dhd_download_btfw);
#endif /* defined (BT_OVER_SDIO) */

#ifndef BCMDBUS
int
dhd_bus_start(dhd_pub_t *dhdp)
{
	int ret = -1;
	dhd_info_t *dhd = (dhd_info_t*)dhdp->info;
	unsigned long flags;

#if defined(DHD_DEBUG) && defined(BCMSDIO)
	int fw_download_start = 0, fw_download_end = 0, f2_sync_start = 0, f2_sync_end = 0;
#endif /* DHD_DEBUG && BCMSDIO */
	ASSERT(dhd);

	DHD_TRACE(("Enter %s:\n", __FUNCTION__));

	DHD_PERIM_LOCK(dhdp);
#ifdef HOFFLOAD_MODULES
	dhd_linux_get_modfw_address(dhdp);
#endif
	/* try to download image and nvram to the dongle */
	if  (dhd->pub.busstate == DHD_BUS_DOWN && dhd_update_fw_nv_path(dhd)) {
		/* Indicate FW Download has not yet done */
		dhd->pub.fw_download_done = FALSE;
		DHD_INFO(("%s download fw %s, nv %s, conf %s\n",
			__FUNCTION__, dhd->fw_path, dhd->nv_path, dhd->conf_path));
#if defined(DHD_DEBUG) && defined(BCMSDIO)
		fw_download_start = OSL_SYSUPTIME();
#endif /* DHD_DEBUG && BCMSDIO */
		ret = dhd_bus_download_firmware(dhd->pub.bus, dhd->pub.osh,
			dhd->fw_path, dhd->nv_path, dhd->clm_path, dhd->conf_path);
#if defined(DHD_DEBUG) && defined(BCMSDIO)
		fw_download_end = OSL_SYSUPTIME();
#endif /* DHD_DEBUG && BCMSDIO */
		if (ret < 0) {
			DHD_ERROR(("%s: failed to download firmware %s\n",
				__FUNCTION__, dhd->fw_path));
			DHD_PERIM_UNLOCK(dhdp);
			return ret;
		}
		/* Indicate FW Download has succeeded */
		dhd->pub.fw_download_done = TRUE;
	}
	if (dhd->pub.busstate != DHD_BUS_LOAD) {
		DHD_PERIM_UNLOCK(dhdp);
		return -ENETDOWN;
	}

#ifdef BCMSDIO
	dhd_os_sdlock(dhdp);
#endif /* BCMSDIO */

	/* Start the watchdog timer */
	dhd->pub.tickcnt = 0;
	dhd_os_wd_timer(&dhd->pub, dhd_watchdog_ms);

	/* Bring up the bus */
	if ((ret = dhd_bus_init(&dhd->pub, FALSE)) != 0) {

		DHD_ERROR(("%s, dhd_bus_init failed %d\n", __FUNCTION__, ret));
#ifdef BCMSDIO
		dhd_os_sdunlock(dhdp);
#endif /* BCMSDIO */
		DHD_PERIM_UNLOCK(dhdp);
		return ret;
	}

	DHD_ENABLE_RUNTIME_PM(&dhd->pub);

#ifdef DHD_ULP
	dhd_ulp_set_ulp_state(dhdp, DHD_ULP_DISABLED);
#endif /* DHD_ULP */
#if defined(OOB_INTR_ONLY) || defined(BCMPCIE_OOB_HOST_WAKE)
	/* Host registration for OOB interrupt */
	if (dhd_bus_oob_intr_register(dhdp)) {
		/* deactivate timer and wait for the handler to finish */
#if !defined(BCMPCIE_OOB_HOST_WAKE)
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		dhd->wd_timer_valid = FALSE;
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		del_timer_sync(&dhd->timer);

#endif /* !BCMPCIE_OOB_HOST_WAKE */
		DHD_DISABLE_RUNTIME_PM(&dhd->pub);
		DHD_PERIM_UNLOCK(dhdp);
		DHD_ERROR(("%s Host failed to register for OOB\n", __FUNCTION__));
		return -ENODEV;
	}

#if defined(BCMPCIE_OOB_HOST_WAKE)
	dhd_bus_oob_intr_set(dhdp, TRUE);
#else
	/* Enable oob at firmware */
	dhd_enable_oob_intr(dhd->pub.bus, TRUE);
#endif /* BCMPCIE_OOB_HOST_WAKE */
#elif defined(FORCE_WOWLAN)
	/* Enable oob at firmware */
	dhd_enable_oob_intr(dhd->pub.bus, TRUE);
#endif 
#ifdef PCIE_FULL_DONGLE
	{
		/* max_h2d_rings includes H2D common rings */
		uint32 max_h2d_rings = dhd_bus_max_h2d_queues(dhd->pub.bus);

		DHD_ERROR(("%s: Initializing %u h2drings\n", __FUNCTION__,
			max_h2d_rings));
		if ((ret = dhd_flow_rings_init(&dhd->pub, max_h2d_rings)) != BCME_OK) {
#ifdef BCMSDIO
			dhd_os_sdunlock(dhdp);
#endif /* BCMSDIO */
			DHD_PERIM_UNLOCK(dhdp);
			return ret;
		}
	}
#endif /* PCIE_FULL_DONGLE */

	/* Do protocol initialization necessary for IOCTL/IOVAR */
	ret = dhd_prot_init(&dhd->pub);
	if (unlikely(ret) != BCME_OK) {
		DHD_PERIM_UNLOCK(dhdp);
		DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}

	/* If bus is not ready, can't come up */
	if (dhd->pub.busstate != DHD_BUS_DATA) {
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		dhd->wd_timer_valid = FALSE;
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		del_timer_sync(&dhd->timer);
		DHD_ERROR(("%s failed bus is not ready\n", __FUNCTION__));
		DHD_DISABLE_RUNTIME_PM(&dhd->pub);
#ifdef BCMSDIO
		dhd_os_sdunlock(dhdp);
#endif /* BCMSDIO */
		DHD_PERIM_UNLOCK(dhdp);
		return -ENODEV;
	}

#ifdef BCMSDIO
	dhd_os_sdunlock(dhdp);
#endif /* BCMSDIO */

	/* Bus is ready, query any dongle information */
#if defined(DHD_DEBUG) && defined(BCMSDIO)
	f2_sync_start = OSL_SYSUPTIME();
#endif /* DHD_DEBUG && BCMSDIO */
	if ((ret = dhd_sync_with_dongle(&dhd->pub)) < 0) {
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		dhd->wd_timer_valid = FALSE;
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		del_timer_sync(&dhd->timer);
		DHD_ERROR(("%s failed to sync with dongle\n", __FUNCTION__));
		DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
		DHD_PERIM_UNLOCK(dhdp);
		return ret;
	}
#if defined(CONFIG_SOC_EXYNOS8895)
	DHD_ERROR(("%s: Enable L1ss EP side\n", __FUNCTION__));
	exynos_pcie_l1ss_ctrl(1, PCIE_L1SS_CTRL_WIFI);
#endif /* CONFIG_SOC_EXYNOS8895 */

#if defined(DHD_DEBUG) && defined(BCMSDIO)
	f2_sync_end = OSL_SYSUPTIME();
	DHD_ERROR(("Time taken for FW download and F2 ready is: %d msec\n",
			(fw_download_end - fw_download_start) + (f2_sync_end - f2_sync_start)));
#endif /* DHD_DEBUG && BCMSDIO */

#ifdef ARP_OFFLOAD_SUPPORT
	if (dhd->pend_ipaddr) {
#ifdef AOE_IP_ALIAS_SUPPORT
		aoe_update_host_ipv4_table(&dhd->pub, dhd->pend_ipaddr, TRUE, 0);
#endif /* AOE_IP_ALIAS_SUPPORT */
		dhd->pend_ipaddr = 0;
	}
#endif /* ARP_OFFLOAD_SUPPORT */

#if defined(TRAFFIC_MGMT_DWM)
	bzero(&dhd->pub.dhd_tm_dwm_tbl, sizeof(dhd_trf_mgmt_dwm_tbl_t));
#endif 
	DHD_PERIM_UNLOCK(dhdp);
	return 0;
}
#endif /* !BCMDBUS */

#ifdef WLTDLS
int _dhd_tdls_enable(dhd_pub_t *dhd, bool tdls_on, bool auto_on, struct ether_addr *mac)
{
	uint32 tdls = tdls_on;
	int ret = 0;
	uint32 tdls_auto_op = 0;
	uint32 tdls_idle_time = CUSTOM_TDLS_IDLE_MODE_SETTING;
	int32 tdls_rssi_high = CUSTOM_TDLS_RSSI_THRESHOLD_HIGH;
	int32 tdls_rssi_low = CUSTOM_TDLS_RSSI_THRESHOLD_LOW;
	BCM_REFERENCE(mac);
	if (!FW_SUPPORTED(dhd, tdls))
		return BCME_ERROR;

	if (dhd->tdls_enable == tdls_on)
		goto auto_mode;
	ret = dhd_iovar(dhd, 0, "tdls_enable", (char *)&tdls, sizeof(tdls), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: tdls %d failed %d\n", __FUNCTION__, tdls, ret));
		goto exit;
	}
	dhd->tdls_enable = tdls_on;
auto_mode:

	tdls_auto_op = auto_on;
	ret = dhd_iovar(dhd, 0, "tdls_auto_op", (char *)&tdls_auto_op, sizeof(tdls_auto_op), NULL,
			0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s: tdls_auto_op failed %d\n", __FUNCTION__, ret));
		goto exit;
	}

	if (tdls_auto_op) {
		ret = dhd_iovar(dhd, 0, "tdls_idle_time", (char *)&tdls_idle_time,
				sizeof(tdls_idle_time), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: tdls_idle_time failed %d\n", __FUNCTION__, ret));
			goto exit;
		}
		ret = dhd_iovar(dhd, 0, "tdls_rssi_high", (char *)&tdls_rssi_high,
				sizeof(tdls_rssi_high), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: tdls_rssi_high failed %d\n", __FUNCTION__, ret));
			goto exit;
		}
		ret = dhd_iovar(dhd, 0, "tdls_rssi_low", (char *)&tdls_rssi_low,
				sizeof(tdls_rssi_low), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: tdls_rssi_low failed %d\n", __FUNCTION__, ret));
			goto exit;
		}
	}

exit:
	return ret;
}

int dhd_tdls_enable(struct net_device *dev, bool tdls_on, bool auto_on, struct ether_addr *mac)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;
	if (dhd)
		ret = _dhd_tdls_enable(&dhd->pub, tdls_on, auto_on, mac);
	else
		ret = BCME_ERROR;
	return ret;
}

int
dhd_tdls_set_mode(dhd_pub_t *dhd, bool wfd_mode)
{
	int ret = 0;
	bool auto_on = false;
	uint32 mode =  wfd_mode;

#ifdef ENABLE_TDLS_AUTO_MODE
	if (wfd_mode) {
		auto_on = false;
	} else {
		auto_on = true;
	}
#else
	auto_on = false;
#endif /* ENABLE_TDLS_AUTO_MODE */
	ret = _dhd_tdls_enable(dhd, false, auto_on, NULL);
	if (ret < 0) {
		DHD_ERROR(("Disable tdls_auto_op failed. %d\n", ret));
		return ret;
	}

	ret = dhd_iovar(dhd, 0, "tdls_wfd_mode", (char *)&mode, sizeof(mode), NULL, 0, TRUE);
	if ((ret < 0) && (ret != BCME_UNSUPPORTED)) {
		DHD_ERROR(("%s: tdls_wfd_mode faile_wfd_mode %d\n", __FUNCTION__, ret));
		return ret;
	}

	ret = _dhd_tdls_enable(dhd, true, auto_on, NULL);
	if (ret < 0) {
		DHD_ERROR(("enable tdls_auto_op failed. %d\n", ret));
		return ret;
	}

	dhd->tdls_mode = mode;
	return ret;
}
#ifdef PCIE_FULL_DONGLE
int dhd_tdls_update_peer_info(dhd_pub_t *dhdp, wl_event_msg_t *event)
{
	dhd_pub_t *dhd_pub = dhdp;
	tdls_peer_node_t *cur = dhd_pub->peer_tbl.node;
	tdls_peer_node_t *new = NULL, *prev = NULL;
	int ifindex = dhd_ifname2idx(dhd_pub->info, event->ifname);
	uint8 *da = (uint8 *)&event->addr.octet[0];
	bool connect = FALSE;
	uint32 reason = ntoh32(event->reason);
	unsigned long flags;

	if (reason == WLC_E_TDLS_PEER_CONNECTED)
		connect = TRUE;
	else if (reason == WLC_E_TDLS_PEER_DISCONNECTED)
		connect = FALSE;
	else
	{
		DHD_ERROR(("%s: TDLS Event reason is unknown\n", __FUNCTION__));
		return BCME_ERROR;
	}
	if (ifindex == DHD_BAD_IF)
		return BCME_ERROR;

	if (connect) {
		while (cur != NULL) {
			if (!memcmp(da, cur->addr, ETHER_ADDR_LEN)) {
				DHD_ERROR(("%s: TDLS Peer exist already %d\n",
					__FUNCTION__, __LINE__));
				return BCME_ERROR;
			}
			cur = cur->next;
		}

		new = MALLOC(dhd_pub->osh, sizeof(tdls_peer_node_t));
		if (new == NULL) {
			DHD_ERROR(("%s: Failed to allocate memory\n", __FUNCTION__));
			return BCME_ERROR;
		}
		memcpy(new->addr, da, ETHER_ADDR_LEN);
		DHD_TDLS_LOCK(&dhdp->tdls_lock, flags);
		new->next = dhd_pub->peer_tbl.node;
		dhd_pub->peer_tbl.node = new;
		dhd_pub->peer_tbl.tdls_peer_count++;
		DHD_TDLS_UNLOCK(&dhdp->tdls_lock, flags);

	} else {
		while (cur != NULL) {
			if (!memcmp(da, cur->addr, ETHER_ADDR_LEN)) {
				dhd_flow_rings_delete_for_peer(dhd_pub, (uint8)ifindex, da);
				DHD_TDLS_LOCK(&dhdp->tdls_lock, flags);
				if (prev)
					prev->next = cur->next;
				else
					dhd_pub->peer_tbl.node = cur->next;
				MFREE(dhd_pub->osh, cur, sizeof(tdls_peer_node_t));
				dhd_pub->peer_tbl.tdls_peer_count--;
				DHD_TDLS_UNLOCK(&dhdp->tdls_lock, flags);
				return BCME_OK;
			}
			prev = cur;
			cur = cur->next;
		}
		DHD_ERROR(("%s: TDLS Peer Entry Not found\n", __FUNCTION__));
	}
	return BCME_OK;
}
#endif /* PCIE_FULL_DONGLE */
#endif 

bool dhd_is_concurrent_mode(dhd_pub_t *dhd)
{
	if (!dhd)
		return FALSE;

	if (dhd->op_mode & DHD_FLAG_CONCURR_MULTI_CHAN_MODE)
		return TRUE;
	else if ((dhd->op_mode & DHD_FLAG_CONCURR_SINGLE_CHAN_MODE) ==
		DHD_FLAG_CONCURR_SINGLE_CHAN_MODE)
		return TRUE;
	else
		return FALSE;
}
#if !defined(AP) && defined(WLP2P)
/* From Android JerryBean release, the concurrent mode is enabled by default and the firmware
 * name would be fw_bcmdhd.bin. So we need to determine whether P2P is enabled in the STA
 * firmware and accordingly enable concurrent mode (Apply P2P settings). SoftAP firmware
 * would still be named as fw_bcmdhd_apsta.
 */
uint32
dhd_get_concurrent_capabilites(dhd_pub_t *dhd)
{
	int32 ret = 0;
	char buf[WLC_IOCTL_SMLEN];
	bool mchan_supported = FALSE;
	/* if dhd->op_mode is already set for HOSTAP and Manufacturing
	 * test mode, that means we only will use the mode as it is
	 */
	if (dhd->op_mode & (DHD_FLAG_HOSTAP_MODE | DHD_FLAG_MFG_MODE))
		return 0;
	if (FW_SUPPORTED(dhd, vsdb)) {
		mchan_supported = TRUE;
	}
	if (!FW_SUPPORTED(dhd, p2p)) {
		DHD_TRACE(("Chip does not support p2p\n"));
		return 0;
	} else {
		/* Chip supports p2p but ensure that p2p is really implemented in firmware or not */
		memset(buf, 0, sizeof(buf));
		ret = dhd_iovar(dhd, 0, "p2p", NULL, 0, (char *)&buf,
				sizeof(buf), FALSE);
		if (ret < 0) {
			DHD_ERROR(("%s: Get P2P failed (error=%d)\n", __FUNCTION__, ret));
			return 0;
		} else {
			if (buf[0] == 1) {
				/* By default, chip supports single chan concurrency,
				* now lets check for mchan
				*/
				ret = DHD_FLAG_CONCURR_SINGLE_CHAN_MODE;
				if (mchan_supported)
					ret |= DHD_FLAG_CONCURR_MULTI_CHAN_MODE;
				if (FW_SUPPORTED(dhd, rsdb)) {
					ret |= DHD_FLAG_RSDB_MODE;
				}
#ifdef WL_SUPPORT_MULTIP2P
				if (FW_SUPPORTED(dhd, mp2p)) {
					ret |= DHD_FLAG_MP2P_MODE;
				}
#endif /* WL_SUPPORT_MULTIP2P */
#if defined(WL_ENABLE_P2P_IF) || defined(WL_CFG80211_P2P_DEV_IF)
				return ret;
#else
				return 0;
#endif /* WL_ENABLE_P2P_IF || WL_CFG80211_P2P_DEV_IF */
			}
		}
	}
	return 0;
}
#endif 

#ifdef SUPPORT_AP_POWERSAVE
#define RXCHAIN_PWRSAVE_PPS			10
#define RXCHAIN_PWRSAVE_QUIET_TIME		10
#define RXCHAIN_PWRSAVE_STAS_ASSOC_CHECK	0
int dhd_set_ap_powersave(dhd_pub_t *dhdp, int ifidx, int enable)
{
	int32 pps = RXCHAIN_PWRSAVE_PPS;
	int32 quiet_time = RXCHAIN_PWRSAVE_QUIET_TIME;
	int32 stas_assoc_check = RXCHAIN_PWRSAVE_STAS_ASSOC_CHECK;
	int ret;

	if (enable) {
		ret = dhd_iovar(dhdp, 0, "rxchain_pwrsave_enable", (char *)&enable, sizeof(enable),
				NULL, 0, TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("Failed to enable AP power save\n"));
		}
		ret = dhd_iovar(dhdp, 0, "rxchain_pwrsave_pps", (char *)&pps, sizeof(pps), NULL, 0,
				TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("Failed to set pps\n"));
		}
		ret = dhd_iovar(dhdp, 0, "rxchain_pwrsave_quiet_time", (char *)&quiet_time,
				sizeof(quiet_time), NULL, 0, TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("Failed to set quiet time\n"));
		}
		ret = dhd_iovar(dhdp, 0, "rxchain_pwrsave_stas_assoc_check",
				(char *)&stas_assoc_check, sizeof(stas_assoc_check), NULL, 0, TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("Failed to set stas assoc check\n"));
		}
	} else {
		ret = dhd_iovar(dhdp, 0, "rxchain_pwrsave_enable", (char *)&enable, sizeof(enable),
				NULL, 0, TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("Failed to disable AP power save\n"));
		}
	}

	return 0;
}
#endif /* SUPPORT_AP_POWERSAVE */




#if defined(WLADPS) || defined(WLADPS_PRIVATE_CMD)
int
dhd_enable_adps(dhd_pub_t *dhd, uint8 on)
{
	int i;
	int len;
	int ret = BCME_OK;

	bcm_iov_buf_t *iov_buf = NULL;
	wl_adps_params_v1_t *data = NULL;
	char buf[WL_EVENTING_MASK_LEN + 12];	/* Room for "event_msgs" + '\0' + bitvec  */

	len = OFFSETOF(bcm_iov_buf_t, data) + sizeof(*data);
	iov_buf = kmalloc(len, GFP_KERNEL);
	if (iov_buf == NULL) {
		DHD_ERROR(("%s - failed to allocate %d bytes for iov_buf\n", __FUNCTION__, len));
		ret = BCME_NOMEM;
		goto exit;
	}

	iov_buf->version = WL_ADPS_IOV_VER;
	iov_buf->len = sizeof(*data);
	iov_buf->id = WL_ADPS_IOV_MODE;

	data = (wl_adps_params_v1_t *)iov_buf->data;
	data->version = ADPS_SUB_IOV_VERSION_1;
	data->length = sizeof(*data);
	data->mode = on;

	for (i = 1; i <= MAX_BANDS; i++) {
		data->band = i;
		bcm_mkiovar("adps", (char *)iov_buf, len, buf, sizeof(buf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, sizeof(buf), TRUE, 0)) < 0) {
			if (ret == BCME_UNSUPPORTED) {
				DHD_ERROR(("%s adps is not supported\n", __FUNCTION__));
				ret = BCME_OK;
				goto exit;
			}
			else {
				DHD_ERROR(("%s fail to set adps %s for band %d (%d)\n",
					__FUNCTION__, on ? "On" : "Off", i, ret));
				goto exit;
			}
		}
	}

exit:
	if (iov_buf) {
		kfree(iov_buf);
	}
	return ret;
}
#endif /* WLADPS || WLADPS_PRIVATE_CMD */

int
dhd_preinit_ioctls(dhd_pub_t *dhd)
{
	int ret = 0;
	char eventmask[WL_EVENTING_MASK_LEN];
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */
	uint32 buf_key_b4_m4 = 1;
	uint8 msglen;
	eventmsgs_ext_t *eventmask_msg = NULL;
	char* iov_buf = NULL;
	int ret2 = 0;
	uint32 wnm_cap = 0;
#if defined(CUSTOM_AMPDU_BA_WSIZE)
	uint32 ampdu_ba_wsize = 0;
#endif 
#if defined(CUSTOM_AMPDU_MPDU)
	int32 ampdu_mpdu = 0;
#endif
#if defined(CUSTOM_AMPDU_RELEASE)
	int32 ampdu_release = 0;
#endif
#if defined(CUSTOM_AMSDU_AGGSF)
	int32 amsdu_aggsf = 0;
#endif
	shub_control_t shub_ctl;

#if defined(BCMSDIO) || defined(BCMDBUS)
#ifdef PROP_TXSTATUS
	int wlfc_enable = TRUE;
#ifndef DISABLE_11N
	uint32 hostreorder = 1;
	uint wl_down = 1;
#endif /* DISABLE_11N */
#endif /* PROP_TXSTATUS */
#endif /* BCMSDIO || BCMDBUS */
#ifndef PCIE_FULL_DONGLE
	uint32 wl_ap_isolate;
#endif /* PCIE_FULL_DONGLE */
	uint32 frameburst = CUSTOM_FRAMEBURST_SET;
	uint wnm_bsstrans_resp = 0;
#ifdef SUPPORT_SET_CAC
	uint32 cac = 1;
#endif /* SUPPORT_SET_CAC */
#ifdef DHD_ENABLE_LPC
	uint32 lpc = 1;
#endif /* DHD_ENABLE_LPC */
	uint power_mode = PM_FAST;
#if defined(BCMSDIO)
	uint32 dongle_align = DHD_SDALIGN;
	uint32 glom = CUSTOM_GLOM_SETTING;
#endif /* defined(BCMSDIO) */
#if defined(CUSTOMER_HW2) && defined(USE_WL_CREDALL)
	uint32 credall = 1;
#endif
	uint bcn_timeout = CUSTOM_BCN_TIMEOUT;
	uint scancache_enab = TRUE;
#ifdef ENABLE_BCN_LI_BCN_WAKEUP
	uint32 bcn_li_bcn = 1;
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
	uint retry_max = CUSTOM_ASSOC_RETRY_MAX;
#if defined(ARP_OFFLOAD_SUPPORT)
	int arpoe = 1;
#endif
	int scan_assoc_time = DHD_SCAN_ASSOC_ACTIVE_TIME;
	int scan_unassoc_time = DHD_SCAN_UNASSOC_ACTIVE_TIME;
	int scan_passive_time = DHD_SCAN_PASSIVE_TIME;
	char buf[WLC_IOCTL_SMLEN];
	char *ptr;
	uint32 listen_interval = CUSTOM_LISTEN_INTERVAL; /* Default Listen Interval in Beacons */
#if defined(DHD_8021X_DUMP) && defined(SHOW_LOGTRACE)
	wl_el_tag_params_t *el_tag = NULL;
#endif /* DHD_8021X_DUMP */
#ifdef ROAM_ENABLE
	uint roamvar = 0;
	int roam_trigger[2] = {CUSTOM_ROAM_TRIGGER_SETTING, WLC_BAND_ALL};
	int roam_scan_period[2] = {10, WLC_BAND_ALL};
	int roam_delta[2] = {CUSTOM_ROAM_DELTA_SETTING, WLC_BAND_ALL};
#ifdef FULL_ROAMING_SCAN_PERIOD_60_SEC
	int roam_fullscan_period = 60;
#else /* FULL_ROAMING_SCAN_PERIOD_60_SEC */
	int roam_fullscan_period = 120;
#endif /* FULL_ROAMING_SCAN_PERIOD_60_SEC */
#ifdef DISABLE_BCNLOSS_ROAM
	uint roam_bcnloss_off = 1;
#endif /* DISABLE_BCNLOSS_ROAM */
#else
#ifdef DISABLE_BUILTIN_ROAM
	uint roamvar = 1;
#endif /* DISABLE_BUILTIN_ROAM */
#endif /* ROAM_ENABLE */

#if defined(SOFTAP)
	uint dtim = 1;
#endif
#if (defined(AP) && !defined(WLP2P)) || (!defined(AP) && defined(WL_CFG80211))
	struct ether_addr p2p_ea;
#endif
#ifdef SOFTAP_UAPSD_OFF
	uint32 wme_apsd = 0;
#endif /* SOFTAP_UAPSD_OFF */
#if (defined(AP) || defined(WLP2P)) && !defined(SOFTAP_AND_GC)
	uint32 apsta = 1; /* Enable APSTA mode */
#elif defined(SOFTAP_AND_GC)
	uint32 apsta = 0;
	int ap_mode = 1;
#endif /* (defined(AP) || defined(WLP2P)) && !defined(SOFTAP_AND_GC) */
#ifdef GET_CUSTOM_MAC_ENABLE
	struct ether_addr ea_addr;
	char hw_ether[62];
#endif /* GET_CUSTOM_MAC_ENABLE */

#ifdef DISABLE_11N
	uint32 nmode = 0;
#endif /* DISABLE_11N */

#ifdef USE_WL_TXBF
	uint32 txbf = 1;
#endif /* USE_WL_TXBF */
#ifdef DISABLE_TXBFR
	uint32 txbf_bfr_cap = 0;
#endif /* DISABLE_TXBFR */
#if defined(PROP_TXSTATUS)
#ifdef USE_WFA_CERT_CONF
	uint32 proptx = 0;
#endif /* USE_WFA_CERT_CONF */
#endif /* PROP_TXSTATUS */
#if defined(SUPPORT_5G_1024QAM_VHT)
	uint32 vht_features = 0; /* init to 0, will be set based on each support */
#endif 
#ifdef DISABLE_11N_PROPRIETARY_RATES
	uint32 ht_features = 0;
#endif /* DISABLE_11N_PROPRIETARY_RATES */
#ifdef CUSTOM_PSPRETEND_THR
	uint32 pspretend_thr = CUSTOM_PSPRETEND_THR;
#endif
#ifdef CUSTOM_EVENT_PM_WAKE
	uint32 pm_awake_thresh = CUSTOM_EVENT_PM_WAKE;
#endif	/* CUSTOM_EVENT_PM_WAKE */
	uint32 rsdb_mode = 0;
#ifdef ENABLE_TEMP_THROTTLING
	wl_temp_control_t temp_control;
#endif /* ENABLE_TEMP_THROTTLING */
#ifdef DISABLE_PRUNED_SCAN
	uint32 scan_features = 0;
#endif /* DISABLE_PRUNED_SCAN */
#ifdef PKT_FILTER_SUPPORT
	dhd_pkt_filter_enable = TRUE;
#ifdef APF
	dhd->apf_set = FALSE;
#endif /* APF */
#endif /* PKT_FILTER_SUPPORT */
#ifdef WLTDLS
	dhd->tdls_enable = FALSE;
	dhd_tdls_set_mode(dhd, false);
#endif /* WLTDLS */
	dhd->suspend_bcn_li_dtim = CUSTOM_SUSPEND_BCN_LI_DTIM;
#ifdef ENABLE_MAX_DTIM_IN_SUSPEND
	dhd->max_dtim_enable = TRUE;
#else
	dhd->max_dtim_enable = FALSE;
#endif /* ENABLE_MAX_DTIM_IN_SUSPEND */
#ifdef CUSTOM_SET_OCLOFF
	dhd->ocl_off = FALSE;
#endif /* CUSTOM_SET_OCLOFF */
	DHD_TRACE(("Enter %s\n", __FUNCTION__));

#ifdef DHDTCPACK_SUPPRESS
	dhd_tcpack_suppress_set(dhd, dhd->conf->tcpack_sup_mode);
#endif
	dhd->op_mode = 0;

#if defined(CUSTOM_COUNTRY_CODE) && defined(CUSTOMER_HW2)
	/* clear AP flags */
	dhd->dhd_cflags &= ~WLAN_PLAT_AP_FLAG;
#endif /* CUSTOM_COUNTRY_CODE && CUSTOMER_HW2 */

#ifdef CUSTOMER_HW4_DEBUG
	if (!dhd_validate_chipid(dhd)) {
		DHD_ERROR(("%s: CONFIG_BCMXXX and CHIP ID(%x) is mismatched\n",
			__FUNCTION__, dhd_bus_chip_id(dhd)));
#ifndef SUPPORT_MULTIPLE_CHIPS
		ret = BCME_BADARG;
		goto done;
#endif /* !SUPPORT_MULTIPLE_CHIPS */
	}
#endif /* CUSTOMER_HW4_DEBUG */
	if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_MFG_MODE) ||
		(op_mode == DHD_FLAG_MFG_MODE)) {
		dhd->op_mode = DHD_FLAG_MFG_MODE;
#ifdef DHD_PCIE_RUNTIMEPM
		/* Disable RuntimePM in mfg mode */
		DHD_DISABLE_RUNTIME_PM(dhd);
		DHD_ERROR(("%s : Disable RuntimePM in Manufactring Firmware\n", __FUNCTION__));
#endif /* DHD_PCIE_RUNTIME_PM */
		/* Check and adjust IOCTL response timeout for Manufactring firmware */
		dhd_os_set_ioctl_resp_timeout(MFG_IOCTL_RESP_TIMEOUT);
		DHD_ERROR(("%s : Set IOCTL response time for Manufactring Firmware\n",
			__FUNCTION__));
	} else {
		dhd_os_set_ioctl_resp_timeout(IOCTL_RESP_TIMEOUT);
		DHD_INFO(("%s : Set IOCTL response time.\n", __FUNCTION__));
	}
#ifdef GET_CUSTOM_MAC_ENABLE
	memset(hw_ether, 0, sizeof(hw_ether));
	ret = wifi_platform_get_mac_addr(dhd->info->adapter, hw_ether);
#ifdef GET_CUSTOM_MAC_FROM_CONFIG
	if (!memcmp(&ether_null, &dhd->conf->hw_ether, ETHER_ADDR_LEN)) {
		ret = 0;
	} else
#endif
	if (!ret) {
		memset(buf, 0, sizeof(buf));
#ifdef GET_CUSTOM_MAC_FROM_CONFIG
		memcpy(hw_ether, &dhd->conf->hw_ether, sizeof(dhd->conf->hw_ether));
#endif
		bcopy(hw_ether, ea_addr.octet, sizeof(struct ether_addr));
		bcm_mkiovar("cur_etheraddr", (void *)&ea_addr, ETHER_ADDR_LEN, buf, sizeof(buf));
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, sizeof(buf), TRUE, 0);
		if (ret < 0) {
			memset(buf, 0, sizeof(buf));
			bcm_mkiovar("hw_ether", hw_ether, sizeof(hw_ether), buf, sizeof(buf));
			ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, sizeof(buf), TRUE, 0);
			if (ret) {
				DHD_ERROR(("%s: can't set MAC address MAC="MACDBG", error=%d\n",
					__FUNCTION__, MAC2STRDBG(hw_ether), ret));
				prhex("MACPAD", &hw_ether[ETHER_ADDR_LEN], sizeof(hw_ether)-ETHER_ADDR_LEN);
				ret = BCME_NOTUP;
				goto done;
			}
		}
	} else {
		DHD_ERROR(("%s: can't get custom MAC address, ret=%d\n", __FUNCTION__, ret));
		ret = BCME_NOTUP;
		goto done;
	}
#endif /* GET_CUSTOM_MAC_ENABLE */
	/* Get the default device MAC address directly from firmware */
	memset(buf, 0, sizeof(buf));
	bcm_mkiovar("cur_etheraddr", 0, 0, buf, sizeof(buf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, buf, sizeof(buf),
		FALSE, 0)) < 0) {
		DHD_ERROR(("%s: can't get MAC address , error=%d\n", __FUNCTION__, ret));
		ret = BCME_NOTUP;
		goto done;
	}
	/* Update public MAC address after reading from Firmware */
	memcpy(dhd->mac.octet, buf, ETHER_ADDR_LEN);

	if ((ret = dhd_apply_default_clm(dhd, dhd->clm_path)) < 0) {
		DHD_ERROR(("%s: CLM set failed. Abort initialization.\n", __FUNCTION__));
		goto done;
	}

	/* get a capabilities from firmware */
	{
		uint32 cap_buf_size = sizeof(dhd->fw_capabilities);
		memset(dhd->fw_capabilities, 0, cap_buf_size);
		ret = dhd_iovar(dhd, 0, "cap", NULL, 0, dhd->fw_capabilities, (cap_buf_size - 1),
				FALSE);
		if (ret < 0) {
			DHD_ERROR(("%s: Get Capability failed (error=%d)\n",
				__FUNCTION__, ret));
			return 0;
		}

		memmove(&dhd->fw_capabilities[1], dhd->fw_capabilities, (cap_buf_size - 1));
		dhd->fw_capabilities[0] = ' ';
		dhd->fw_capabilities[cap_buf_size - 2] = ' ';
		dhd->fw_capabilities[cap_buf_size - 1] = '\0';
	}

	if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_HOSTAP_MODE) ||
		(op_mode == DHD_FLAG_HOSTAP_MODE)) {
#ifdef SET_RANDOM_MAC_SOFTAP
		uint rand_mac;
#endif /* SET_RANDOM_MAC_SOFTAP */
		dhd->op_mode = DHD_FLAG_HOSTAP_MODE;
#if defined(ARP_OFFLOAD_SUPPORT)
			arpoe = 0;
#endif
#ifdef PKT_FILTER_SUPPORT
		if (dhd_conf_get_insuspend(dhd, AP_FILTER_IN_SUSPEND))
			dhd_pkt_filter_enable = TRUE;
		else
			dhd_pkt_filter_enable = FALSE;
#endif
#ifdef SET_RANDOM_MAC_SOFTAP
		SRANDOM32((uint)jiffies);
		rand_mac = RANDOM32();
		iovbuf[0] = (unsigned char)(vendor_oui >> 16) | 0x02;	/* local admin bit */
		iovbuf[1] = (unsigned char)(vendor_oui >> 8);
		iovbuf[2] = (unsigned char)vendor_oui;
		iovbuf[3] = (unsigned char)(rand_mac & 0x0F) | 0xF0;
		iovbuf[4] = (unsigned char)(rand_mac >> 8);
		iovbuf[5] = (unsigned char)(rand_mac >> 16);

		ret = dhd_iovar(dhd, 0, "cur_etheraddr", (char *)&iovbuf, ETHER_ADDR_LEN, NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
		} else
			memcpy(dhd->mac.octet, iovbuf, ETHER_ADDR_LEN);
#endif /* SET_RANDOM_MAC_SOFTAP */
#ifdef USE_DYNAMIC_F2_BLKSIZE
		dhdsdio_func_blocksize(dhd, 2, DYNAMIC_F2_BLKSIZE_FOR_NONLEGACY);
#endif /* USE_DYNAMIC_F2_BLKSIZE */
#ifdef SUPPORT_AP_POWERSAVE
		dhd_set_ap_powersave(dhd, 0, TRUE);
#endif /* SUPPORT_AP_POWERSAVE */
#ifdef SOFTAP_UAPSD_OFF
		ret = dhd_iovar(dhd, 0, "wme_apsd", (char *)&wme_apsd, sizeof(wme_apsd), NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: set wme_apsd 0 fail (error=%d)\n",
				__FUNCTION__, ret));
		}
#endif /* SOFTAP_UAPSD_OFF */
#if defined(CUSTOM_COUNTRY_CODE) && defined(CUSTOMER_HW2)
		/* set AP flag for specific country code of SOFTAP */
		dhd->dhd_cflags |= WLAN_PLAT_AP_FLAG | WLAN_PLAT_NODFS_FLAG;
#endif /* CUSTOM_COUNTRY_CODE && CUSTOMER_HW2 */
	} else if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_MFG_MODE) ||
		(op_mode == DHD_FLAG_MFG_MODE)) {
#if defined(ARP_OFFLOAD_SUPPORT)
		arpoe = 0;
#endif /* ARP_OFFLOAD_SUPPORT */
#ifdef PKT_FILTER_SUPPORT
		dhd_pkt_filter_enable = FALSE;
#endif /* PKT_FILTER_SUPPORT */
		dhd->op_mode = DHD_FLAG_MFG_MODE;
#ifdef USE_DYNAMIC_F2_BLKSIZE
		dhdsdio_func_blocksize(dhd, 2, DYNAMIC_F2_BLKSIZE_FOR_NONLEGACY);
#endif /* USE_DYNAMIC_F2_BLKSIZE */
		if (FW_SUPPORTED(dhd, rsdb)) {
			rsdb_mode = 0;
			ret = dhd_iovar(dhd, 0, "rsdb_mode", (char *)&rsdb_mode, sizeof(rsdb_mode),
					NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s Disable rsdb_mode is failed ret= %d\n",
					__FUNCTION__, ret));
			}
		}
	} else {
		uint32 concurrent_mode = 0;
		if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_P2P_MODE) ||
			(op_mode == DHD_FLAG_P2P_MODE)) {
#if defined(ARP_OFFLOAD_SUPPORT)
			arpoe = 0;
#endif
#ifdef PKT_FILTER_SUPPORT
			dhd_pkt_filter_enable = FALSE;
#endif
			dhd->op_mode = DHD_FLAG_P2P_MODE;
		} else if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_IBSS_MODE) ||
			(op_mode == DHD_FLAG_IBSS_MODE)) {
			dhd->op_mode = DHD_FLAG_IBSS_MODE;
		} else
			dhd->op_mode = DHD_FLAG_STA_MODE;
#if !defined(AP) && defined(WLP2P)
		if (dhd->op_mode != DHD_FLAG_IBSS_MODE &&
			(concurrent_mode = dhd_get_concurrent_capabilites(dhd))) {
#if defined(ARP_OFFLOAD_SUPPORT)
			arpoe = 1;
#endif
			dhd->op_mode |= concurrent_mode;
		}

		/* Check if we are enabling p2p */
		if (dhd->op_mode & DHD_FLAG_P2P_MODE) {
			ret = dhd_iovar(dhd, 0, "apsta", (char *)&apsta, sizeof(apsta), NULL, 0,
					TRUE);
			if (ret < 0)
				DHD_ERROR(("%s APSTA for P2P failed ret= %d\n", __FUNCTION__, ret));

#if defined(SOFTAP_AND_GC)
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_AP,
			(char *)&ap_mode, sizeof(ap_mode), TRUE, 0)) < 0) {
				DHD_ERROR(("%s WLC_SET_AP failed %d\n", __FUNCTION__, ret));
		}
#endif
			memcpy(&p2p_ea, &dhd->mac, ETHER_ADDR_LEN);
			ETHER_SET_LOCALADDR(&p2p_ea);
			ret = dhd_iovar(dhd, 0, "p2p_da_override", (char *)&p2p_ea, sizeof(p2p_ea),
					NULL, 0, TRUE);
			if (ret < 0)
				DHD_ERROR(("%s p2p_da_override ret= %d\n", __FUNCTION__, ret));
			else
				DHD_INFO(("dhd_preinit_ioctls: p2p_da_override succeeded\n"));
		}
#else
	(void)concurrent_mode;
#endif 
	}

#if defined(RSDB_MODE_FROM_FILE)
	(void)dhd_rsdb_mode_from_file(dhd);
#endif 

#ifdef DISABLE_PRUNED_SCAN
	if (FW_SUPPORTED(dhd, rsdb)) {
		ret = dhd_iovar(dhd, 0, "scan_features", (char *)&scan_features,
				sizeof(scan_features), iovbuf, sizeof(iovbuf), FALSE);
		if (ret < 0) {
			DHD_ERROR(("%s get scan_features is failed ret=%d\n",
				__FUNCTION__, ret));
		} else {
			memcpy(&scan_features, iovbuf, 4);
			scan_features &= ~RSDB_SCAN_DOWNGRADED_CH_PRUNE_ROAM;
			ret = dhd_iovar(dhd, 0, "scan_features", (char *)&scan_features,
					sizeof(scan_features), NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s set scan_features is failed ret=%d\n",
					__FUNCTION__, ret));
			}
		}
	}
#endif /* DISABLE_PRUNED_SCAN */

	DHD_ERROR(("Firmware up: op_mode=0x%04x, MAC="MACDBG"\n",
		dhd->op_mode, MAC2STRDBG(dhd->mac.octet)));
#ifdef CUSTOMER_HW2
#if defined(DHD_BLOB_EXISTENCE_CHECK)
	if (!dhd->pub.is_blob)
#endif /* DHD_BLOB_EXISTENCE_CHECK */
	{
		/* get a ccode and revision for the country code */
#if defined(CUSTOM_COUNTRY_CODE)
		get_customized_country_code(dhd->info->adapter, dhd->dhd_cspec.country_abbrev,
			&dhd->dhd_cspec, dhd->dhd_cflags);
#else
		get_customized_country_code(dhd->info->adapter, dhd->dhd_cspec.country_abbrev,
			&dhd->dhd_cspec);
#endif /* CUSTOM_COUNTRY_CODE */
	}
#endif /* CUSTOMER_HW2 */

#if defined(RXFRAME_THREAD) && defined(RXTHREAD_ONLYSTA)
	if (dhd->op_mode == DHD_FLAG_HOSTAP_MODE)
		dhd->info->rxthread_enabled = FALSE;
	else
		dhd->info->rxthread_enabled = TRUE;
#endif
	/* Set Country code  */
	if (dhd->dhd_cspec.ccode[0] != 0) {
		ret = dhd_iovar(dhd, 0, "country", (char *)&dhd->dhd_cspec, sizeof(wl_country_t),
				NULL, 0, TRUE);
		if (ret < 0)
			DHD_ERROR(("%s: country code setting failed\n", __FUNCTION__));
	}


	/* Set Listen Interval */
	ret = dhd_iovar(dhd, 0, "assoc_listen", (char *)&listen_interval, sizeof(listen_interval),
			NULL, 0, TRUE);
	if (ret < 0)
		DHD_ERROR(("%s assoc_listen failed %d\n", __FUNCTION__, ret));

#if defined(ROAM_ENABLE) || defined(DISABLE_BUILTIN_ROAM)
#ifdef USE_WFA_CERT_CONF
	if (sec_get_param_wfa_cert(dhd, SET_PARAM_ROAMOFF, &roamvar) == BCME_OK) {
		DHD_ERROR(("%s: read roam_off param =%d\n", __FUNCTION__, roamvar));
	}
#endif /* USE_WFA_CERT_CONF */
	/* Disable built-in roaming to allowed ext supplicant to take care of roaming */
	dhd_iovar(dhd, 0, "roam_off", (char *)&roamvar, sizeof(roamvar), NULL, 0, TRUE);
#endif /* ROAM_ENABLE || DISABLE_BUILTIN_ROAM */
#if defined(ROAM_ENABLE)
#ifdef DISABLE_BCNLOSS_ROAM
	dhd_iovar(dhd, 0, "roam_bcnloss_off", (char *)&roam_bcnloss_off, sizeof(roam_bcnloss_off),
			NULL, 0, TRUE);
#endif /* DISABLE_BCNLOSS_ROAM */
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_TRIGGER, roam_trigger,
		sizeof(roam_trigger), TRUE, 0)) < 0)
		DHD_ERROR(("%s: roam trigger set failed %d\n", __FUNCTION__, ret));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_SCAN_PERIOD, roam_scan_period,
		sizeof(roam_scan_period), TRUE, 0)) < 0)
		DHD_ERROR(("%s: roam scan period set failed %d\n", __FUNCTION__, ret));
	if ((dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_DELTA, roam_delta,
		sizeof(roam_delta), TRUE, 0)) < 0)
		DHD_ERROR(("%s: roam delta set failed %d\n", __FUNCTION__, ret));
	ret = dhd_iovar(dhd, 0, "fullroamperiod", (char *)&roam_fullscan_period,
			sizeof(roam_fullscan_period), NULL, 0, TRUE);
	if (ret < 0)
		DHD_ERROR(("%s: roam fullscan period set failed %d\n", __FUNCTION__, ret));
#endif /* ROAM_ENABLE */

#ifdef CUSTOM_EVENT_PM_WAKE
	ret = dhd_iovar(dhd, 0, "const_awake_thresh", (char *)&pm_awake_thresh,
			sizeof(pm_awake_thresh), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s set const_awake_thresh failed %d\n", __FUNCTION__, ret));
	}
#endif	/* CUSTOM_EVENT_PM_WAKE */
#ifdef WLTDLS
#ifdef ENABLE_TDLS_AUTO_MODE
	/* by default TDLS on and auto mode on */
	_dhd_tdls_enable(dhd, true, true, NULL);
#else
	/* by default TDLS on and auto mode off */
	_dhd_tdls_enable(dhd, true, false, NULL);
#endif /* ENABLE_TDLS_AUTO_MODE */
#endif /* WLTDLS */

#ifdef DHD_ENABLE_LPC
	/* Set lpc 1 */
	ret = dhd_iovar(dhd, 0, "lpc", (char *)&lpc, sizeof(lpc), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s Set lpc failed  %d\n", __FUNCTION__, ret));

		if (ret == BCME_NOTDOWN) {
			uint wl_down = 1;
			ret = dhd_wl_ioctl_cmd(dhd, WLC_DOWN,
				(char *)&wl_down, sizeof(wl_down), TRUE, 0);
			DHD_ERROR(("%s lpc fail WL_DOWN : %d, lpc = %d\n", __FUNCTION__, ret, lpc));

			ret = dhd_iovar(dhd, 0, "lpc", (char *)&lpc, sizeof(lpc), NULL, 0, TRUE);
			DHD_ERROR(("%s Set lpc ret --> %d\n", __FUNCTION__, ret));
		}
	}
#endif /* DHD_ENABLE_LPC */

#ifdef WLADPS
#ifdef WLADPS_SEAK_AP_WAR
	dhd->disabled_adps = FALSE;
#endif /* WLADPS_SEAK_AP_WAR */
	if (dhd->op_mode & DHD_FLAG_STA_MODE) {
#ifdef ADPS_MODE_FROM_FILE
		dhd_adps_mode_from_file(dhd);
#else
		if ((ret = dhd_enable_adps(dhd, ADPS_ENABLE)) != BCME_OK) {
			DHD_ERROR(("%s dhd_enable_adps failed %d\n",
					__FUNCTION__, ret));
		}
#endif /* ADPS_MODE_FROM_FILE */
	}
#endif /* WLADPS */

	/* Set PowerSave mode */
	(void) dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&power_mode, sizeof(power_mode), TRUE, 0);

#if defined(BCMSDIO)
	/* Match Host and Dongle rx alignment */
	dhd_iovar(dhd, 0, "bus:txglomalign", (char *)&dongle_align, sizeof(dongle_align),
			NULL, 0, TRUE);

#if defined(CUSTOMER_HW2) && defined(USE_WL_CREDALL)
	/* enable credall to reduce the chance of no bus credit happened. */
	dhd_iovar(dhd, 0, "bus:credall", (char *)&credall, sizeof(credall), NULL, 0, TRUE);
#endif

#ifdef USE_WFA_CERT_CONF
	if (sec_get_param_wfa_cert(dhd, SET_PARAM_BUS_TXGLOM_MODE, &glom) == BCME_OK) {
		DHD_ERROR(("%s, read txglom param =%d\n", __FUNCTION__, glom));
	}
#endif /* USE_WFA_CERT_CONF */
	if (glom != DEFAULT_GLOM_VALUE) {
		DHD_INFO(("%s set glom=0x%X\n", __FUNCTION__, glom));
		dhd_iovar(dhd, 0, "bus:txglom", (char *)&glom, sizeof(glom), NULL, 0, TRUE);
	}
#endif /* defined(BCMSDIO) */

	/* Setup timeout if Beacons are lost and roam is off to report link down */
	dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout, sizeof(bcn_timeout), NULL, 0, TRUE);

	/* Setup assoc_retry_max count to reconnect target AP in dongle */
	dhd_iovar(dhd, 0, "assoc_retry_max", (char *)&retry_max, sizeof(retry_max), NULL, 0, TRUE);

#if defined(AP) && !defined(WLP2P)
	dhd_iovar(dhd, 0, "apsta", (char *)&apsta, sizeof(apsta), NULL, 0, TRUE);

#endif /* defined(AP) && !defined(WLP2P) */

#ifdef MIMO_ANT_SETTING
	dhd_sel_ant_from_file(dhd);
#endif /* MIMO_ANT_SETTING */

#if defined(SOFTAP)
	if (ap_fw_loaded == TRUE) {
		dhd_wl_ioctl_cmd(dhd, WLC_SET_DTIMPRD, (char *)&dtim, sizeof(dtim), TRUE, 0);
	}
#endif 

#if defined(KEEP_ALIVE)
	{
	/* Set Keep Alive : be sure to use FW with -keepalive */
	int res;

#if defined(SOFTAP)
	if (ap_fw_loaded == FALSE)
#endif 
		if (!(dhd->op_mode &
			(DHD_FLAG_HOSTAP_MODE | DHD_FLAG_MFG_MODE))) {
			if ((res = dhd_keep_alive_onoff(dhd)) < 0)
				DHD_ERROR(("%s set keeplive failed %d\n",
				__FUNCTION__, res));
		}
	}
#endif /* defined(KEEP_ALIVE) */

#ifdef USE_WL_TXBF
	ret = dhd_iovar(dhd, 0, "txbf", (char *)&txbf, sizeof(txbf), NULL, 0, TRUE);
	if (ret < 0)
		DHD_ERROR(("%s Set txbf failed  %d\n", __FUNCTION__, ret));

#endif /* USE_WL_TXBF */

	ret = dhd_iovar(dhd, 0, "scancache", (char *)&scancache_enab, sizeof(scancache_enab), NULL,
			0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s Set scancache failed %d\n", __FUNCTION__, ret));
	}

#ifdef DISABLE_TXBFR
	ret = dhd_iovar(dhd, 0, "txbf_bfr_cap", (char *)&txbf_bfr_cap, sizeof(txbf_bfr_cap), NULL,
			0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s Clear txbf_bfr_cap failed  %d\n", __FUNCTION__, ret));
	}
#endif /* DISABLE_TXBFR */

#ifdef USE_WFA_CERT_CONF
#ifdef USE_WL_FRAMEBURST
	 if (sec_get_param_wfa_cert(dhd, SET_PARAM_FRAMEBURST, &frameburst) == BCME_OK) {
		DHD_ERROR(("%s, read frameburst param=%d\n", __FUNCTION__, frameburst));
	 }
#endif /* USE_WL_FRAMEBURST */
#ifdef DISABLE_FRAMEBURST_VSDB
	 g_frameburst = frameburst;
#endif /* DISABLE_FRAMEBURST_VSDB */
#endif /* USE_WFA_CERT_CONF */
#ifdef DISABLE_WL_FRAMEBURST_SOFTAP
	/* Disable Framebursting for SofAP */
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		frameburst = 0;
	}
#endif /* DISABLE_WL_FRAMEBURST_SOFTAP */
	/* Set frameburst to value */
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_FAKEFRAG, (char *)&frameburst,
		sizeof(frameburst), TRUE, 0)) < 0) {
		DHD_INFO(("%s frameburst not supported  %d\n", __FUNCTION__, ret));
	}

	iov_buf = (char*)kmalloc(WLC_IOCTL_SMLEN, GFP_KERNEL);
	if (iov_buf == NULL) {
		DHD_ERROR(("failed to allocate %d bytes for iov_buf\n", WLC_IOCTL_SMLEN));
		ret = BCME_NOMEM;
		goto done;
	}


#if defined(CUSTOM_AMPDU_BA_WSIZE)
	/* Set ampdu ba wsize to 64 or 16 */
#ifdef CUSTOM_AMPDU_BA_WSIZE
	ampdu_ba_wsize = CUSTOM_AMPDU_BA_WSIZE;
#endif
	if (ampdu_ba_wsize != 0) {
		ret = dhd_iovar(dhd, 0, "ampdu_ba_wsize", (char *)&ampdu_ba_wsize,
				sizeof(ampdu_ba_wsize), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set ampdu_ba_wsize to %d failed  %d\n",
				__FUNCTION__, ampdu_ba_wsize, ret));
		}
	}
#endif 

#ifdef ENABLE_TEMP_THROTTLING
	if (dhd->op_mode & DHD_FLAG_STA_MODE) {
		memset(&temp_control, 0, sizeof(temp_control));
		temp_control.enable = 1;
		temp_control.control_bit = TEMP_THROTTLE_CONTROL_BIT;
		ret = dhd_iovar(dhd, 0, "temp_throttle_control", (char *)&temp_control,
				sizeof(temp_control), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set temp_throttle_control to %d failed \n",
				__FUNCTION__, ret));
		}
	}
#endif /* ENABLE_TEMP_THROTTLING */

#if defined(CUSTOM_AMPDU_MPDU)
	ampdu_mpdu = CUSTOM_AMPDU_MPDU;
	if (ampdu_mpdu != 0 && (ampdu_mpdu <= ampdu_ba_wsize)) {
		ret = dhd_iovar(dhd, 0, "ampdu_mpdu", (char *)&ampdu_mpdu, sizeof(ampdu_mpdu),
				NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set ampdu_mpdu to %d failed  %d\n",
				__FUNCTION__, CUSTOM_AMPDU_MPDU, ret));
		}
	}
#endif /* CUSTOM_AMPDU_MPDU */

#if defined(CUSTOM_AMPDU_RELEASE)
	ampdu_release = CUSTOM_AMPDU_RELEASE;
	if (ampdu_release != 0 && (ampdu_release <= ampdu_ba_wsize)) {
		ret = dhd_iovar(dhd, 0, "ampdu_release", (char *)&ampdu_release,
				sizeof(ampdu_release), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set ampdu_release to %d failed  %d\n",
				__FUNCTION__, CUSTOM_AMPDU_RELEASE, ret));
		}
	}
#endif /* CUSTOM_AMPDU_RELEASE */

#if defined(CUSTOM_AMSDU_AGGSF)
	amsdu_aggsf = CUSTOM_AMSDU_AGGSF;
	if (amsdu_aggsf != 0) {
		ret = dhd_iovar(dhd, 0, "amsdu_aggsf", (char *)&amsdu_aggsf, sizeof(amsdu_aggsf),
				NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set amsdu_aggsf to %d failed %d\n",
				__FUNCTION__, CUSTOM_AMSDU_AGGSF, ret));
		}
	}
#endif /* CUSTOM_AMSDU_AGGSF */

#if defined(SUPPORT_5G_1024QAM_VHT)
#ifdef SUPPORT_5G_1024QAM_VHT
	if (dhd_get_chipid(dhd) == BCM4361_CHIP_ID) {
		vht_features |= 0x6; /* 5G 1024 QAM support */
	}
#endif /* SUPPORT_5G_1024QAM_VHT */
	if (vht_features) {
		ret = dhd_iovar(dhd, 0, "vht_features", (char *)&vht_features, sizeof(vht_features),
				NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s vht_features set failed %d\n", __FUNCTION__, ret));

			if (ret == BCME_NOTDOWN) {
				uint wl_down = 1;
				ret = dhd_wl_ioctl_cmd(dhd, WLC_DOWN,
					(char *)&wl_down, sizeof(wl_down), TRUE, 0);
				DHD_ERROR(("%s vht_features fail WL_DOWN : %d,"
					" vht_features = 0x%x\n",
					__FUNCTION__, ret, vht_features));

				ret = dhd_iovar(dhd, 0, "vht_features", (char *)&vht_features,
					sizeof(vht_features), NULL, 0, TRUE);
				DHD_ERROR(("%s vht_features set. ret --> %d\n", __FUNCTION__, ret));
			}
		}
	}
#endif 
#ifdef DISABLE_11N_PROPRIETARY_RATES
	ret = dhd_iovar(dhd, 0, "ht_features", (char *)&ht_features, sizeof(ht_features), NULL, 0,
			TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s ht_features set failed %d\n", __FUNCTION__, ret));
	}
#endif /* DISABLE_11N_PROPRIETARY_RATES */
#ifdef CUSTOM_PSPRETEND_THR
	/* Turn off MPC in AP mode */
	ret = dhd_iovar(dhd, 0, "pspretend_threshold", (char *)&pspretend_thr,
			sizeof(pspretend_thr), NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s pspretend_threshold for HostAPD failed  %d\n",
			__FUNCTION__, ret));
	}
#endif

	ret = dhd_iovar(dhd, 0, "buf_key_b4_m4", (char *)&buf_key_b4_m4, sizeof(buf_key_b4_m4),
			NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s buf_key_b4_m4 set failed %d\n", __FUNCTION__, ret));
	}
#ifdef SUPPORT_SET_CAC
	bcm_mkiovar("cac", (char *)&cac, sizeof(cac), iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s Failed to set cac to %d, %d\n", __FUNCTION__, cac, ret));
	}
#endif /* SUPPORT_SET_CAC */
#ifdef DHD_ULP
	/* Get the required details from dongle during preinit ioctl */
	dhd_ulp_preinit(dhd);
#endif /* DHD_ULP */

	/* Read event_msgs mask */
	ret = dhd_iovar(dhd, 0, "event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf,
			sizeof(iovbuf), FALSE);
	if (ret < 0) {
		DHD_ERROR(("%s read Event mask failed %d\n", __FUNCTION__, ret));
		goto done;
	}
	bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);

	/* Setup event_msgs */
	setbit(eventmask, WLC_E_SET_SSID);
	setbit(eventmask, WLC_E_PRUNE);
	setbit(eventmask, WLC_E_AUTH);
	setbit(eventmask, WLC_E_AUTH_IND);
	setbit(eventmask, WLC_E_ASSOC);
	setbit(eventmask, WLC_E_REASSOC);
	setbit(eventmask, WLC_E_REASSOC_IND);
	if (!(dhd->op_mode & DHD_FLAG_IBSS_MODE))
		setbit(eventmask, WLC_E_DEAUTH);
	setbit(eventmask, WLC_E_DEAUTH_IND);
	setbit(eventmask, WLC_E_DISASSOC_IND);
	setbit(eventmask, WLC_E_DISASSOC);
	setbit(eventmask, WLC_E_JOIN);
	setbit(eventmask, WLC_E_BSSID);
	setbit(eventmask, WLC_E_START);
	setbit(eventmask, WLC_E_ASSOC_IND);
	setbit(eventmask, WLC_E_PSK_SUP);
	setbit(eventmask, WLC_E_LINK);
	setbit(eventmask, WLC_E_MIC_ERROR);
	setbit(eventmask, WLC_E_ASSOC_REQ_IE);
	setbit(eventmask, WLC_E_ASSOC_RESP_IE);
#ifdef LIMIT_BORROW
	setbit(eventmask, WLC_E_ALLOW_CREDIT_BORROW);
#endif
#ifndef WL_CFG80211
	setbit(eventmask, WLC_E_PMKID_CACHE);
//	setbit(eventmask, WLC_E_TXFAIL); // terence 20181106: remove unnecessary event
#endif
	setbit(eventmask, WLC_E_JOIN_START);
//	setbit(eventmask, WLC_E_SCAN_COMPLETE); // terence 20150628: remove redundant event
#ifdef DHD_DEBUG
	setbit(eventmask, WLC_E_SCAN_CONFIRM_IND);
#endif
#ifdef WLMEDIA_HTSF
	setbit(eventmask, WLC_E_HTSFSYNC);
#endif /* WLMEDIA_HTSF */
#ifdef PNO_SUPPORT
	setbit(eventmask, WLC_E_PFN_NET_FOUND);
	setbit(eventmask, WLC_E_PFN_BEST_BATCHING);
	setbit(eventmask, WLC_E_PFN_BSSID_NET_FOUND);
	setbit(eventmask, WLC_E_PFN_BSSID_NET_LOST);
#endif /* PNO_SUPPORT */
	/* enable dongle roaming event */
	setbit(eventmask, WLC_E_ROAM);
#ifdef WLTDLS
	setbit(eventmask, WLC_E_TDLS_PEER_EVENT);
#endif /* WLTDLS */
#ifdef WL_ESCAN
	setbit(eventmask, WLC_E_ESCAN_RESULT);
#endif /* WL_ESCAN */
#ifdef RTT_SUPPORT
	setbit(eventmask, WLC_E_PROXD);
#endif /* RTT_SUPPORT */
#ifdef WL_CFG80211
	setbit(eventmask, WLC_E_ESCAN_RESULT);
	setbit(eventmask, WLC_E_AP_STARTED);
	setbit(eventmask, WLC_E_ACTION_FRAME_RX);
	if (dhd->op_mode & DHD_FLAG_P2P_MODE) {
		setbit(eventmask, WLC_E_P2P_DISC_LISTEN_COMPLETE);
	}
#endif /* WL_CFG80211 */

#if defined(SHOW_LOGTRACE) && defined(LOGTRACE_FROM_FILE)
	if (dhd_logtrace_from_file(dhd)) {
		setbit(eventmask, WLC_E_TRACE);
	} else {
		clrbit(eventmask, WLC_E_TRACE);
	}
#elif defined(SHOW_LOGTRACE)
	setbit(eventmask, WLC_E_TRACE);
#else
	clrbit(eventmask, WLC_E_TRACE);
#endif /* defined(SHOW_LOGTRACE) && defined(LOGTRACE_FROM_FILE) */

	setbit(eventmask, WLC_E_CSA_COMPLETE_IND);
#ifdef DHD_WMF
	setbit(eventmask, WLC_E_PSTA_PRIMARY_INTF_IND);
#endif
#ifdef CUSTOM_EVENT_PM_WAKE
	setbit(eventmask, WLC_E_EXCESS_PM_WAKE_EVENT);
#endif	/* CUSTOM_EVENT_PM_WAKE */
#ifdef DHD_LOSSLESS_ROAMING
	setbit(eventmask, WLC_E_ROAM_PREP);
#endif
#if defined(PCIE_FULL_DONGLE) && defined(DHD_LOSSLESS_ROAMING)
	dhd_update_flow_prio_map(dhd, DHD_FLOW_PRIO_LLR_MAP);
#endif /* defined(PCIE_FULL_DONGLE) && defined(DHD_LOSSLESS_ROAMING) */

#if defined(BCMPCIE) && defined(EAPOL_PKT_PRIO)
	dhd_update_flow_prio_map(dhd, DHD_FLOW_PRIO_LLR_MAP);
#endif /* defined(BCMPCIE) && defined(EAPOL_PKT_PRIO) */

	/* Write updated Event mask */
	ret = dhd_iovar(dhd, 0, "event_msgs", eventmask, WL_EVENTING_MASK_LEN, NULL, 0, TRUE);
	if (ret < 0) {
		DHD_ERROR(("%s Set Event mask failed %d\n", __FUNCTION__, ret));
		goto done;
	}

	/* make up event mask ext message iovar for event larger than 128 */
	msglen = ROUNDUP(WLC_E_LAST, NBBY)/NBBY + EVENTMSGS_EXT_STRUCT_SIZE;
	eventmask_msg = (eventmsgs_ext_t*)kmalloc(msglen, GFP_KERNEL);
	if (eventmask_msg == NULL) {
		DHD_ERROR(("failed to allocate %d bytes for event_msg_ext\n", msglen));
		ret = BCME_NOMEM;
		goto done;
	}
	bzero(eventmask_msg, msglen);
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->len = ROUNDUP(WLC_E_LAST, NBBY)/NBBY;

	/* Read event_msgs_ext mask */
	ret2 = dhd_iovar(dhd, 0, "event_msgs_ext", (char *)eventmask_msg, msglen, iov_buf,
			WLC_IOCTL_SMLEN, FALSE);

	if (ret2 == 0) { /* event_msgs_ext must be supported */
		bcopy(iov_buf, eventmask_msg, msglen);
#ifdef RSSI_MONITOR_SUPPORT
		setbit(eventmask_msg->mask, WLC_E_RSSI_LQM);
#endif /* RSSI_MONITOR_SUPPORT */
#ifdef GSCAN_SUPPORT
		setbit(eventmask_msg->mask, WLC_E_PFN_GSCAN_FULL_RESULT);
		setbit(eventmask_msg->mask, WLC_E_PFN_SCAN_COMPLETE);
		setbit(eventmask_msg->mask, WLC_E_PFN_SSID_EXT);
		setbit(eventmask_msg->mask, WLC_E_ROAM_EXP_EVENT);
#endif /* GSCAN_SUPPORT */
		setbit(eventmask_msg->mask, WLC_E_RSSI_LQM);
#ifdef BT_WIFI_HANDOVER
		setbit(eventmask_msg->mask, WLC_E_BT_WIFI_HANDOVER_REQ);
#endif /* BT_WIFI_HANDOVER */
#ifdef DBG_PKT_MON
		setbit(eventmask_msg->mask, WLC_E_ROAM_PREP);
#endif /* DBG_PKT_MON */
#ifdef DHD_ULP
		setbit(eventmask_msg->mask, WLC_E_ULP);
#endif
#ifdef ENABLE_TEMP_THROTTLING
		setbit(eventmask_msg->mask, WLC_E_TEMP_THROTTLE);
#endif /* ENABLE_TEMP_THROTTLING */

		/* Write updated Event mask */
		eventmask_msg->ver = EVENTMSGS_VER;
		eventmask_msg->command = EVENTMSGS_SET_MASK;
		eventmask_msg->len = ROUNDUP(WLC_E_LAST, NBBY)/NBBY;
		ret = dhd_iovar(dhd, 0, "event_msgs_ext", (char *)eventmask_msg, msglen, NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s write event mask ext failed %d\n", __FUNCTION__, ret));
			goto done;
		}
	} else if (ret2 == BCME_UNSUPPORTED || ret2 == BCME_VERSION) {
		/* Skip for BCME_UNSUPPORTED or BCME_VERSION */
		DHD_ERROR(("%s event_msgs_ext not support or version mismatch %d\n",
			__FUNCTION__, ret2));
	} else {
		DHD_ERROR(("%s read event mask ext failed %d\n", __FUNCTION__, ret2));
		ret = ret2;
		goto done;
	}

#if defined(DHD_8021X_DUMP) && defined(SHOW_LOGTRACE)
	/* Enabling event log trace for EAP events */
	el_tag = (wl_el_tag_params_t *)kmalloc(sizeof(wl_el_tag_params_t), GFP_KERNEL);
	if (el_tag == NULL) {
		DHD_ERROR(("failed to allocate %d bytes for event_msg_ext\n",
				(int)sizeof(wl_el_tag_params_t)));
		ret = BCME_NOMEM;
		goto done;
	}
	el_tag->tag = EVENT_LOG_TAG_4WAYHANDSHAKE;
	el_tag->set = 1;
	el_tag->flags = EVENT_LOG_TAG_FLAG_LOG;
	bcm_mkiovar("event_log_tag_control", (char *)el_tag,
			sizeof(*el_tag), iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif /* DHD_8021X_DUMP */

	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_CHANNEL_TIME, (char *)&scan_assoc_time,
		sizeof(scan_assoc_time), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_UNASSOC_TIME, (char *)&scan_unassoc_time,
		sizeof(scan_unassoc_time), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_PASSIVE_TIME, (char *)&scan_passive_time,
		sizeof(scan_passive_time), TRUE, 0);

#ifdef ARP_OFFLOAD_SUPPORT
	/* Set and enable ARP offload feature for STA only  */
#if defined(SOFTAP)
	if (arpoe && !ap_fw_loaded)
#else
	if (arpoe)
#endif
	{
		dhd_arp_offload_enable(dhd, TRUE);
		dhd_arp_offload_set(dhd, dhd_arp_mode);
	} else {
		dhd_arp_offload_enable(dhd, FALSE);
		dhd_arp_offload_set(dhd, 0);
	}
	dhd_arp_enable = arpoe;
#endif /* ARP_OFFLOAD_SUPPORT */

#ifdef PKT_FILTER_SUPPORT
	/* Setup default defintions for pktfilter , enable in suspend */
	if (dhd_master_mode) {
		dhd->pktfilter_count = 6;
		dhd->pktfilter[DHD_BROADCAST_FILTER_NUM] = NULL;
		if (!FW_SUPPORTED(dhd, pf6)) {
			dhd->pktfilter[DHD_MULTICAST4_FILTER_NUM] = NULL;
			dhd->pktfilter[DHD_MULTICAST6_FILTER_NUM] = NULL;
		} else {
			/* Immediately pkt filter TYPE 6 Discard IPv4/IPv6 Multicast Packet */
			dhd->pktfilter[DHD_MULTICAST4_FILTER_NUM] = DISCARD_IPV4_MCAST;
			dhd->pktfilter[DHD_MULTICAST6_FILTER_NUM] = DISCARD_IPV6_MCAST;
		}
		/* apply APP pktfilter */
		dhd->pktfilter[DHD_ARP_FILTER_NUM] = "105 0 0 12 0xFFFF 0x0806";

		/* Setup filter to allow only unicast */
		dhd->pktfilter[DHD_UNICAST_FILTER_NUM] = "100 0 0 0 0x01 0x00";

		/* Add filter to pass multicastDNS packet and NOT filter out as Broadcast */
		dhd->pktfilter[DHD_MDNS_FILTER_NUM] = NULL;

		dhd->pktfilter[DHD_BROADCAST_ARP_FILTER_NUM] = NULL;
		if (FW_SUPPORTED(dhd, pf6)) {
			/* Immediately pkt filter TYPE 6 Dicard Broadcast IP packet */
			dhd->pktfilter[DHD_IP4BCAST_DROP_FILTER_NUM] =
				"107 1 6 IP4_H:16 0xf0 !0xe0 IP4_H:19 0xff 0xff";
			dhd->pktfilter_count = 8;
		}

#ifdef GAN_LITE_NAT_KEEPALIVE_FILTER
		dhd->pktfilter_count = 4;
		/* Setup filter to block broadcast and NAT Keepalive packets */
		/* discard all broadcast packets */
		dhd->pktfilter[DHD_UNICAST_FILTER_NUM] = "100 0 0 0 0xffffff 0xffffff";
		/* discard NAT Keepalive packets */
		dhd->pktfilter[DHD_BROADCAST_FILTER_NUM] = "102 0 0 36 0xffffffff 0x11940009";
		/* discard NAT Keepalive packets */
		dhd->pktfilter[DHD_MULTICAST4_FILTER_NUM] = "104 0 0 38 0xffffffff 0x11940009";
		dhd->pktfilter[DHD_MULTICAST6_FILTER_NUM] = NULL;
#endif /* GAN_LITE_NAT_KEEPALIVE_FILTER */
	} else
		dhd_conf_discard_pkt_filter(dhd);
	dhd_conf_add_pkt_filter(dhd);

#if defined(SOFTAP)
	if (ap_fw_loaded) {
		dhd_enable_packet_filter(0, dhd);
	}
#endif /* defined(SOFTAP) */
	dhd_set_packet_filter(dhd);
#endif /* PKT_FILTER_SUPPORT */
#ifdef DISABLE_11N
	ret = dhd_iovar(dhd, 0, "nmode", (char *)&nmode, sizeof(nmode), NULL, 0, TRUE);
	if (ret < 0)
		DHD_ERROR(("%s wl nmode 0 failed %d\n", __FUNCTION__, ret));
#endif /* DISABLE_11N */

#ifdef ENABLE_BCN_LI_BCN_WAKEUP
	dhd_iovar(dhd, 0, "bcn_li_bcn", (char *)&bcn_li_bcn, sizeof(bcn_li_bcn), NULL, 0, TRUE);
#endif /* ENABLE_BCN_LI_BCN_WAKEUP */
	/* query for 'clmver' to get clm version info from firmware */
	memset(buf, 0, sizeof(buf));
	ret = dhd_iovar(dhd, 0, "clmver", NULL, 0, buf, sizeof(buf), FALSE);
	if (ret < 0)
		DHD_ERROR(("%s clmver failed %d\n", __FUNCTION__, ret));
	else {
		char *ver_temp_buf = NULL, *ver_date_buf = NULL;
		int len;

		if ((ver_temp_buf = bcmstrstr(buf, "Data:")) == NULL) {
			DHD_ERROR(("Couldn't find \"Data:\"\n"));
		} else {
			ver_date_buf = bcmstrstr(buf, "Creation:");
			ptr = (ver_temp_buf + strlen("Data:"));
			if ((ver_temp_buf = bcmstrtok(&ptr, "\n", 0)) == NULL) {
				DHD_ERROR(("Couldn't find New line character\n"));
			} else {
				memset(clm_version, 0, CLM_VER_STR_LEN);
				len = snprintf(clm_version, CLM_VER_STR_LEN - 1, "%s", ver_temp_buf);
				if (ver_date_buf) {
					ptr = (ver_date_buf + strlen("Creation:"));
					ver_date_buf = bcmstrtok(&ptr, "\n", 0);
					if (ver_date_buf)
						snprintf(clm_version+len, CLM_VER_STR_LEN-1-len,
							" (%s)", ver_date_buf);
				}
				DHD_INFO(("CLM version = %s\n", clm_version));
			}
		}
	}

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	ptr = buf;
	ret = dhd_iovar(dhd, 0, "ver", NULL, 0, (char *)&buf, sizeof(buf), FALSE);
	if (ret < 0)
		DHD_ERROR(("%s failed %d\n", __FUNCTION__, ret));
	else {
		bcmstrtok(&ptr, "\n", 0);
		strncpy(fw_version, buf, FW_VER_STR_LEN);
		fw_version[FW_VER_STR_LEN-1] = '\0';
		dhd_set_version_info(dhd, buf);
#ifdef WRITE_WLANINFO
		sec_save_wlinfo(buf, EPI_VERSION_STR, dhd->info->nv_path, clm_version);
#endif /* WRITE_WLANINFO */
	}
#ifdef GEN_SOFTAP_INFO_FILE
	sec_save_softap_info();
#endif /* GEN_SOFTAP_INFO_FILE */

#if defined(BCMSDIO)
	dhd_txglom_enable(dhd, dhd->conf->bus_rxglom);
#endif /* defined(BCMSDIO) */

#if defined(BCMSDIO) || defined(BCMDBUS)
#ifdef PROP_TXSTATUS
	if (disable_proptx ||
#ifdef PROP_TXSTATUS_VSDB
		/* enable WLFC only if the firmware is VSDB when it is in STA mode */
		(dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
		 dhd->op_mode != DHD_FLAG_IBSS_MODE) ||
#endif /* PROP_TXSTATUS_VSDB */
		FALSE) {
		wlfc_enable = FALSE;
	}
	ret = dhd_conf_get_disable_proptx(dhd);
	if (ret == 0){
		disable_proptx = 0;
		wlfc_enable = TRUE;
	} else if (ret >= 1) {
		disable_proptx = 1;
		wlfc_enable = FALSE;
		/* terence 20161229: we should set ampdu_hostreorder=0 when disable_proptx=1 */
		hostreorder = 0;
	}

#if defined(PROP_TXSTATUS)
#ifdef USE_WFA_CERT_CONF
	if (sec_get_param_wfa_cert(dhd, SET_PARAM_PROPTX, &proptx) == BCME_OK) {
		DHD_ERROR(("%s , read proptx param=%d\n", __FUNCTION__, proptx));
		wlfc_enable = proptx;
	}
#endif /* USE_WFA_CERT_CONF */
#endif /* PROP_TXSTATUS */

#ifndef DISABLE_11N
	ret = dhd_wl_ioctl_cmd(dhd, WLC_DOWN, (char *)&wl_down, sizeof(wl_down), TRUE, 0);
	ret2 = dhd_iovar(dhd, 0, "ampdu_hostreorder", (char *)&hostreorder, sizeof(hostreorder),
			NULL, 0, TRUE);
	if (ret2 < 0) {
		DHD_ERROR(("%s wl ampdu_hostreorder failed %d\n", __FUNCTION__, ret2));
		if (ret2 != BCME_UNSUPPORTED)
			ret = ret2;

		if (ret == BCME_NOTDOWN) {
			uint wl_down = 1;
			ret2 = dhd_wl_ioctl_cmd(dhd, WLC_DOWN, (char *)&wl_down,
				sizeof(wl_down), TRUE, 0);
			DHD_ERROR(("%s ampdu_hostreorder fail WL_DOWN : %d, hostreorder :%d\n",
				__FUNCTION__, ret2, hostreorder));

			ret2 = dhd_iovar(dhd, 0, "ampdu_hostreorder", (char *)&hostreorder,
					sizeof(hostreorder), NULL, 0, TRUE);
			DHD_ERROR(("%s wl ampdu_hostreorder. ret --> %d\n", __FUNCTION__, ret2));
			if (ret2 != BCME_UNSUPPORTED)
					ret = ret2;
		}
		if (ret2 != BCME_OK)
			hostreorder = 0;
	}
#endif /* DISABLE_11N */


	if (wlfc_enable) {
		dhd_wlfc_init(dhd);
		/* terence 20161229: enable ampdu_hostreorder if tlv enabled */
		dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "ampdu_hostreorder", 1, 0, TRUE);
	}
#ifndef DISABLE_11N
	else if (hostreorder)
		dhd_wlfc_hostreorder_init(dhd);
#endif /* DISABLE_11N */
#else
	/* terence 20161229: disable ampdu_hostreorder if PROP_TXSTATUS not defined */
	printf("%s: not define PROP_TXSTATUS\n", __FUNCTION__);
	dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "ampdu_hostreorder", 0, 0, TRUE);
#endif /* PROP_TXSTATUS */
#endif /* BCMSDIO || BCMDBUS */
#ifndef PCIE_FULL_DONGLE
	/* For FD we need all the packets at DHD to handle intra-BSS forwarding */
	if (FW_SUPPORTED(dhd, ap)) {
		wl_ap_isolate = AP_ISOLATE_SENDUP_ALL;
		ret = dhd_iovar(dhd, 0, "ap_isolate", (char *)&wl_ap_isolate, sizeof(wl_ap_isolate),
				NULL, 0, TRUE);
		if (ret < 0)
			DHD_ERROR(("%s failed %d\n", __FUNCTION__, ret));
	}
#endif /* PCIE_FULL_DONGLE */
#ifdef PNO_SUPPORT
	if (!dhd->pno_state) {
		dhd_pno_init(dhd);
	}
#endif
#ifdef RTT_SUPPORT
	if (!dhd->rtt_state) {
		ret = dhd_rtt_init(dhd);
		if (ret < 0) {
			DHD_ERROR(("%s failed to initialize RTT\n", __FUNCTION__));
		}
	}
#endif
#ifdef WL11U
	dhd_interworking_enable(dhd);
#endif /* WL11U */

#ifdef SUPPORT_SENSORHUB
	DHD_ERROR(("%s: SensorHub enabled %d\n",
			__FUNCTION__, dhd->info->shub_enable));
	ret2 = dhd_iovar(dhd, 0, "shub", NULL, 0,
			(char *)&shub_ctl, sizeof(shub_ctl), FALSE);
	if (ret2 < 0) {
		DHD_ERROR(("%s failed to get shub hub enable information %d\n",
			__FUNCTION__, ret2));
		dhd->info->shub_enable = 0;
	} else {
		dhd->info->shub_enable = shub_ctl.enable;
		DHD_ERROR(("%s: checking sensorhub enable %d\n",
			__FUNCTION__, dhd->info->shub_enable));
	}
#else
	DHD_ERROR(("%s: SensorHub diabled %d\n",
			__FUNCTION__, dhd->info->shub_enable));
	dhd->info->shub_enable = FALSE;
	shub_ctl.enable = FALSE;
	ret2 = dhd_iovar(dhd, 0, "shub", (char *)&shub_ctl, sizeof(shub_ctl),
			NULL, 0, TRUE);
	if (ret2 < 0) {
		DHD_ERROR(("%s failed to set ShubHub disable\n",
			__FUNCTION__));
	}
#endif /* SUPPORT_SENSORHUB */


#ifdef NDO_CONFIG_SUPPORT
	dhd->ndo_enable = FALSE;
	dhd->ndo_host_ip_overflow = FALSE;
	dhd->ndo_max_host_ip = NDO_MAX_HOST_IP_ENTRIES;
#endif /* NDO_CONFIG_SUPPORT */

	/* ND offload version supported */
	dhd->ndo_version = dhd_ndo_get_version(dhd);
	if (dhd->ndo_version > 0) {
		DHD_INFO(("%s: ndo version %d\n", __FUNCTION__, dhd->ndo_version));

#ifdef NDO_CONFIG_SUPPORT
		/* enable Unsolicited NA filter */
		ret = dhd_ndo_unsolicited_na_filter_enable(dhd, 1);
		if (ret < 0) {
			DHD_ERROR(("%s failed to enable Unsolicited NA filter\n", __FUNCTION__));
		}
#endif /* NDO_CONFIG_SUPPORT */
	}

	/* check dongle supports wbtext or not */
	dhd->wbtext_support = FALSE;
	if (dhd_wl_ioctl_get_intiovar(dhd, "wnm_bsstrans_resp", &wnm_bsstrans_resp,
			WLC_GET_VAR, FALSE, 0) != BCME_OK) {
		DHD_ERROR(("failed to get wnm_bsstrans_resp\n"));
	}
	if (wnm_bsstrans_resp == WL_BSSTRANS_POLICY_PRODUCT_WBTEXT) {
		dhd->wbtext_support = TRUE;
	}
#ifndef WBTEXT
	/* driver can turn off wbtext feature through makefile */
	if (dhd->wbtext_support) {
		if (dhd_wl_ioctl_set_intiovar(dhd, "wnm_bsstrans_resp",
				WL_BSSTRANS_POLICY_ROAM_ALWAYS,
				WLC_SET_VAR, FALSE, 0) != BCME_OK) {
			DHD_ERROR(("failed to disable WBTEXT\n"));
		}
	}
#endif /* !WBTEXT */

	/* WNM capabilities */
	wnm_cap = 0
#ifdef WL11U
		| WL_WNM_BSSTRANS | WL_WNM_NOTIF
#endif
#ifdef WBTEXT
		| WL_WNM_BSSTRANS | WL_WNM_MAXIDLE
#endif
		;
	if (dhd_iovar(dhd, 0, "wnm", (char *)&wnm_cap, sizeof(wnm_cap), NULL, 0, TRUE) < 0) {
		DHD_ERROR(("failed to set WNM capabilities\n"));
	}

	dhd_conf_postinit_ioctls(dhd);
done:

	if (eventmask_msg)
		kfree(eventmask_msg);
	if (iov_buf)
		kfree(iov_buf);
#if defined(DHD_8021X_DUMP) && defined(SHOW_LOGTRACE)
	if (el_tag)
		kfree(el_tag);
#endif /* DHD_8021X_DUMP */
	return ret;
}


int
dhd_iovar(dhd_pub_t *pub, int ifidx, char *name, char *param_buf, uint param_len, char *res_buf,
		uint res_len, int set)
{
	char *buf = NULL;
	int input_len;
	wl_ioctl_t ioc;
	int ret;

	if (res_len > WLC_IOCTL_MAXLEN || param_len > WLC_IOCTL_MAXLEN)
		return BCME_BADARG;

	input_len = strlen(name) + 1 + param_len;
	if (input_len > WLC_IOCTL_MAXLEN)
		return BCME_BADARG;

	buf = NULL;
	if (set) {
		if (res_buf || res_len != 0) {
			DHD_ERROR(("%s: SET wrong arguemnet\n", __FUNCTION__));
			ret = BCME_BADARG;
			goto exit;
		}
		buf = kzalloc(input_len, GFP_KERNEL);
		if (!buf) {
			DHD_ERROR(("%s: mem alloc failed\n", __FUNCTION__));
			ret = BCME_NOMEM;
			goto exit;
		}
		ret = bcm_mkiovar(name, param_buf, param_len, buf, input_len);
		if (!ret) {
			ret = BCME_NOMEM;
			goto exit;
		}

		ioc.cmd = WLC_SET_VAR;
		ioc.buf = buf;
		ioc.len = input_len;
		ioc.set = set;

		ret = dhd_wl_ioctl(pub, ifidx, &ioc, ioc.buf, ioc.len);
	} else {
		if (!res_buf || !res_len) {
			DHD_ERROR(("%s: GET failed. resp_buf NULL or length 0.\n", __FUNCTION__));
			ret = BCME_BADARG;
			goto exit;
		}

		if (res_len < input_len) {
			DHD_INFO(("%s: res_len(%d) < input_len(%d)\n", __FUNCTION__,
					res_len, input_len));
			buf = kzalloc(input_len, GFP_KERNEL);
			if (!buf) {
				DHD_ERROR(("%s: mem alloc failed\n", __FUNCTION__));
				ret = BCME_NOMEM;
				goto exit;
			}
			ret = bcm_mkiovar(name, param_buf, param_len, buf, input_len);
			if (!ret) {
				ret = BCME_NOMEM;
				goto exit;
			}

			ioc.cmd = WLC_GET_VAR;
			ioc.buf = buf;
			ioc.len = input_len;
			ioc.set = set;

			ret = dhd_wl_ioctl(pub, ifidx, &ioc, ioc.buf, ioc.len);

			if (ret == BCME_OK) {
				memcpy(res_buf, buf, res_len);
			}
		} else {
			memset(res_buf, 0, res_len);
			ret = bcm_mkiovar(name, param_buf, param_len, res_buf, res_len);
			if (!ret) {
				ret = BCME_NOMEM;
				goto exit;
			}

			ioc.cmd = WLC_GET_VAR;
			ioc.buf = res_buf;
			ioc.len = res_len;
			ioc.set = set;

			ret = dhd_wl_ioctl(pub, ifidx, &ioc, ioc.buf, ioc.len);
		}
	}
exit:
	kfree(buf);
	return ret;
}

int
dhd_getiovar(dhd_pub_t *pub, int ifidx, char *name, char *cmd_buf,
	uint cmd_len, char **resptr, uint resp_len)
{
	int len = resp_len;
	int ret;
	char *buf = *resptr;
	wl_ioctl_t ioc;
	if (resp_len > WLC_IOCTL_MAXLEN)
		return BCME_BADARG;

	memset(buf, 0, resp_len);

	ret = bcm_mkiovar(name, cmd_buf, cmd_len, buf, len);
	if (ret == 0) {
		return BCME_BUFTOOSHORT;
	}

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = len;
	ioc.set = 0;

	ret = dhd_wl_ioctl(pub, ifidx, &ioc, ioc.buf, ioc.len);

	return ret;
}


int dhd_change_mtu(dhd_pub_t *dhdp, int new_mtu, int ifidx)
{
	struct dhd_info *dhd = dhdp->info;
	struct net_device *dev = NULL;

	ASSERT(dhd && dhd->iflist[ifidx]);
	dev = dhd->iflist[ifidx]->net;
	ASSERT(dev);

	if (netif_running(dev)) {
		DHD_ERROR(("%s: Must be down to change its MTU\n", dev->name));
		return BCME_NOTDOWN;
	}

#define DHD_MIN_MTU 1500
#define DHD_MAX_MTU 1752

	if ((new_mtu < DHD_MIN_MTU) || (new_mtu > DHD_MAX_MTU)) {
		DHD_ERROR(("%s: MTU size %d is invalid.\n", __FUNCTION__, new_mtu));
		return BCME_BADARG;
	}

	dev->mtu = new_mtu;
	return 0;
}

#ifdef ARP_OFFLOAD_SUPPORT
/* add or remove AOE host ip(s) (up to 8 IPs on the interface)  */
void
aoe_update_host_ipv4_table(dhd_pub_t *dhd_pub, u32 ipa, bool add, int idx)
{
	u32 ipv4_buf[MAX_IPV4_ENTRIES]; /* temp save for AOE host_ip table */
	int i;
	int ret;

	bzero(ipv4_buf, sizeof(ipv4_buf));

	/* display what we've got */
	ret = dhd_arp_get_arp_hostip_table(dhd_pub, ipv4_buf, sizeof(ipv4_buf), idx);
	DHD_ARPOE(("%s: hostip table read from Dongle:\n", __FUNCTION__));
#ifdef AOE_DBG
	dhd_print_buf(ipv4_buf, 32, 4); /* max 8 IPs 4b each */
#endif
	/* now we saved hoste_ip table, clr it in the dongle AOE */
	dhd_aoe_hostip_clr(dhd_pub, idx);

	if (ret) {
		DHD_ERROR(("%s failed\n", __FUNCTION__));
		return;
	}

	for (i = 0; i < MAX_IPV4_ENTRIES; i++) {
		if (add && (ipv4_buf[i] == 0)) {
				ipv4_buf[i] = ipa;
				add = FALSE; /* added ipa to local table  */
				DHD_ARPOE(("%s: Saved new IP in temp arp_hostip[%d]\n",
				__FUNCTION__, i));
		} else if (ipv4_buf[i] == ipa) {
			ipv4_buf[i]	= 0;
			DHD_ARPOE(("%s: removed IP:%x from temp table %d\n",
				__FUNCTION__, ipa, i));
		}

		if (ipv4_buf[i] != 0) {
			/* add back host_ip entries from our local cache */
			dhd_arp_offload_add_ip(dhd_pub, ipv4_buf[i], idx);
			DHD_ARPOE(("%s: added IP:%x to dongle arp_hostip[%d]\n\n",
				__FUNCTION__, ipv4_buf[i], i));
		}
	}
#ifdef AOE_DBG
	/* see the resulting hostip table */
	dhd_arp_get_arp_hostip_table(dhd_pub, ipv4_buf, sizeof(ipv4_buf), idx);
	DHD_ARPOE(("%s: read back arp_hostip table:\n", __FUNCTION__));
	dhd_print_buf(ipv4_buf, 32, 4); /* max 8 IPs 4b each */
#endif
}

/*
 * Notification mechanism from kernel to our driver. This function is called by the Linux kernel
 * whenever there is an event related to an IP address.
 * ptr : kernel provided pointer to IP address that has changed
 */
static int dhd_inetaddr_notifier_call(struct notifier_block *this,
	unsigned long event,
	void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;

	dhd_info_t *dhd;
	dhd_pub_t *dhd_pub;
	int idx;

	if (!dhd_arp_enable)
		return NOTIFY_DONE;
	if (!ifa || !(ifa->ifa_dev->dev))
		return NOTIFY_DONE;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
	/* Filter notifications meant for non Broadcom devices */
	if ((ifa->ifa_dev->dev->netdev_ops != &dhd_ops_pri) &&
	    (ifa->ifa_dev->dev->netdev_ops != &dhd_ops_virt)) {
#if defined(WL_ENABLE_P2P_IF)
		if (!wl_cfgp2p_is_ifops(ifa->ifa_dev->dev->netdev_ops))
#endif /* WL_ENABLE_P2P_IF */
			return NOTIFY_DONE;
	}
#endif /* LINUX_VERSION_CODE */

	dhd = DHD_DEV_INFO(ifa->ifa_dev->dev);
	if (!dhd)
		return NOTIFY_DONE;

	dhd_pub = &dhd->pub;

	if (dhd_pub->arp_version == 1) {
		idx = 0;
	} else {
		for (idx = 0; idx < DHD_MAX_IFS; idx++) {
			if (dhd->iflist[idx] && dhd->iflist[idx]->net == ifa->ifa_dev->dev)
			break;
		}
		if (idx < DHD_MAX_IFS)
			DHD_TRACE(("ifidx : %p %s %d\n", dhd->iflist[idx]->net,
				dhd->iflist[idx]->name, dhd->iflist[idx]->idx));
		else {
			DHD_ERROR(("Cannot find ifidx for(%s) set to 0\n", ifa->ifa_label));
			idx = 0;
		}
	}

	switch (event) {
		case NETDEV_UP:
			DHD_ARPOE(("%s: [%s] Up IP: 0x%x\n",
				__FUNCTION__, ifa->ifa_label, ifa->ifa_address));

			if (dhd->pub.busstate != DHD_BUS_DATA) {
				DHD_ERROR(("%s: bus not ready, exit\n", __FUNCTION__));
				if (dhd->pend_ipaddr) {
					DHD_ERROR(("%s: overwrite pending ipaddr: 0x%x\n",
						__FUNCTION__, dhd->pend_ipaddr));
				}
				dhd->pend_ipaddr = ifa->ifa_address;
				break;
			}

#ifdef AOE_IP_ALIAS_SUPPORT
			DHD_ARPOE(("%s:add aliased IP to AOE hostip cache\n",
				__FUNCTION__));
			aoe_update_host_ipv4_table(dhd_pub, ifa->ifa_address, TRUE, idx);
#endif /* AOE_IP_ALIAS_SUPPORT */
			dhd_conf_set_garp(dhd_pub, idx, ifa->ifa_address, TRUE);
			break;

		case NETDEV_DOWN:
			DHD_ARPOE(("%s: [%s] Down IP: 0x%x\n",
				__FUNCTION__, ifa->ifa_label, ifa->ifa_address));
			dhd->pend_ipaddr = 0;
#ifdef AOE_IP_ALIAS_SUPPORT
			DHD_ARPOE(("%s:interface is down, AOE clr all for this if\n",
				__FUNCTION__));
			if ((dhd_pub->op_mode & DHD_FLAG_HOSTAP_MODE) ||
				(ifa->ifa_dev->dev != dhd_linux_get_primary_netdev(dhd_pub))) {
				aoe_update_host_ipv4_table(dhd_pub, ifa->ifa_address, FALSE, idx);
			} else
#endif /* AOE_IP_ALIAS_SUPPORT */
			{
				dhd_aoe_hostip_clr(&dhd->pub, idx);
				dhd_aoe_arp_clr(&dhd->pub, idx);
			}
			dhd_conf_set_garp(dhd_pub, idx, ifa->ifa_address, FALSE);
			break;

		default:
			DHD_ARPOE(("%s: do noting for [%s] Event: %lu\n",
				__func__, ifa->ifa_label, event));
			break;
	}
	return NOTIFY_DONE;
}
#endif /* ARP_OFFLOAD_SUPPORT */

#if defined(CONFIG_IPV6) && defined(IPV6_NDO_SUPPORT)
/* Neighbor Discovery Offload: defered handler */
static void
dhd_inet6_work_handler(void *dhd_info, void *event_data, u8 event)
{
	struct ipv6_work_info_t *ndo_work = (struct ipv6_work_info_t *)event_data;
	dhd_info_t *dhd = (dhd_info_t *)dhd_info;
	dhd_pub_t *dhdp;
	int ret;

	if (!dhd) {
		DHD_ERROR(("%s: invalid dhd_info\n", __FUNCTION__));
		goto done;
	}
	dhdp = &dhd->pub;

	if (event != DHD_WQ_WORK_IPV6_NDO) {
		DHD_ERROR(("%s: unexpected event\n", __FUNCTION__));
		goto done;
	}

	if (!ndo_work) {
		DHD_ERROR(("%s: ipv6 work info is not initialized\n", __FUNCTION__));
		return;
	}

	switch (ndo_work->event) {
		case NETDEV_UP:
#ifndef NDO_CONFIG_SUPPORT
			DHD_TRACE(("%s: Enable NDO \n ", __FUNCTION__));
			ret = dhd_ndo_enable(dhdp, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s: Enabling NDO Failed %d\n", __FUNCTION__, ret));
			}
#endif /* !NDO_CONFIG_SUPPORT */
			DHD_TRACE(("%s: Add a host ip for NDO\n", __FUNCTION__));
			if (dhdp->ndo_version > 0) {
				/* inet6 addr notifier called only for unicast address */
				ret = dhd_ndo_add_ip_with_type(dhdp, &ndo_work->ipv6_addr[0],
					WL_ND_IPV6_ADDR_TYPE_UNICAST, ndo_work->if_idx);
			} else {
				ret = dhd_ndo_add_ip(dhdp, &ndo_work->ipv6_addr[0],
						ndo_work->if_idx);
			}
			if (ret < 0) {
				DHD_ERROR(("%s: Adding a host ip for NDO failed %d\n",
					__FUNCTION__, ret));
			}
			break;
		case NETDEV_DOWN:
			if (dhdp->ndo_version > 0) {
				DHD_TRACE(("%s: Remove a host ip for NDO\n", __FUNCTION__));
				ret = dhd_ndo_remove_ip_by_addr(dhdp,
					&ndo_work->ipv6_addr[0], ndo_work->if_idx);
			} else {
				DHD_TRACE(("%s: Clear host ip table for NDO \n", __FUNCTION__));
				ret = dhd_ndo_remove_ip(dhdp, ndo_work->if_idx);
			}
			if (ret < 0) {
				DHD_ERROR(("%s: Removing host ip for NDO failed %d\n",
					__FUNCTION__, ret));
				goto done;
			}
#ifdef NDO_CONFIG_SUPPORT
			if (dhdp->ndo_host_ip_overflow) {
				ret = dhd_dev_ndo_update_inet6addr(
					dhd_idx2net(dhdp, ndo_work->if_idx));
				if ((ret < 0) && (ret != BCME_NORESOURCE)) {
					DHD_ERROR(("%s: Updating host ip for NDO failed %d\n",
						__FUNCTION__, ret));
					goto done;
				}
			}
#else /* !NDO_CONFIG_SUPPORT */
			DHD_TRACE(("%s: Disable NDO\n ", __FUNCTION__));
			ret = dhd_ndo_enable(dhdp, FALSE);
			if (ret < 0) {
				DHD_ERROR(("%s: disabling NDO Failed %d\n", __FUNCTION__, ret));
				goto done;
			}
#endif /* NDO_CONFIG_SUPPORT */
			break;

		default:
			DHD_ERROR(("%s: unknown notifier event \n", __FUNCTION__));
			break;
	}
done:
	/* free ndo_work. alloced while scheduling the work */
	if (ndo_work) {
		kfree(ndo_work);
	}

	return;
}

/*
 * Neighbor Discovery Offload: Called when an interface
 * is assigned with ipv6 address.
 * Handles only primary interface
 */
int dhd_inet6addr_notifier_call(struct notifier_block *this, unsigned long event, void *ptr)
{
	dhd_info_t *dhd;
	dhd_pub_t *dhdp;
	struct inet6_ifaddr *inet6_ifa = ptr;
	struct ipv6_work_info_t *ndo_info;
	int idx;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
	/* Filter notifications meant for non Broadcom devices */
	if (inet6_ifa->idev->dev->netdev_ops != &dhd_ops_pri) {
			return NOTIFY_DONE;
	}
#endif /* LINUX_VERSION_CODE */

	dhd = DHD_DEV_INFO(inet6_ifa->idev->dev);
	if (!dhd) {
		return NOTIFY_DONE;
	}
	dhdp = &dhd->pub;

	/* Supports only primary interface */
	idx = dhd_net2idx(dhd, inet6_ifa->idev->dev);
	if (idx != 0) {
		return NOTIFY_DONE;
	}

	/* FW capability */
	if (!FW_SUPPORTED(dhdp, ndoe)) {
		return NOTIFY_DONE;
	}

	ndo_info = (struct ipv6_work_info_t *)kzalloc(sizeof(struct ipv6_work_info_t), GFP_ATOMIC);
	if (!ndo_info) {
		DHD_ERROR(("%s: ipv6 work alloc failed\n", __FUNCTION__));
		return NOTIFY_DONE;
	}

	/* fill up ndo_info */
	ndo_info->event = event;
	ndo_info->if_idx = idx;
	memcpy(ndo_info->ipv6_addr, &inet6_ifa->addr, IPV6_ADDR_LEN);

	/* defer the work to thread as it may block kernel */
	dhd_deferred_schedule_work(dhd->dhd_deferred_wq, (void *)ndo_info, DHD_WQ_WORK_IPV6_NDO,
		dhd_inet6_work_handler, DHD_WQ_WORK_PRIORITY_LOW);
	return NOTIFY_DONE;
}
#endif /* CONFIG_IPV6 && IPV6_NDO_SUPPORT */

int
dhd_register_if(dhd_pub_t *dhdp, int ifidx, bool need_rtnl_lock)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	dhd_if_t *ifp;
	struct net_device *net = NULL;
	int err = 0;
	uint8 temp_addr[ETHER_ADDR_LEN] = { 0x00, 0x90, 0x4c, 0x11, 0x22, 0x33 };

	DHD_TRACE(("%s: ifidx %d\n", __FUNCTION__, ifidx));

	if (dhd == NULL || dhd->iflist[ifidx] == NULL) {
		DHD_ERROR(("%s: Invalid Interface\n", __FUNCTION__));
		return BCME_ERROR;
	}

	ASSERT(dhd && dhd->iflist[ifidx]);
	ifp = dhd->iflist[ifidx];
	net = ifp->net;
	ASSERT(net && (ifp->idx == ifidx));

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
	ASSERT(!net->open);
	net->get_stats = dhd_get_stats;
	net->do_ioctl = dhd_ioctl_entry;
	net->hard_start_xmit = dhd_start_xmit;
	net->set_mac_address = dhd_set_mac_address;
	net->set_multicast_list = dhd_set_multicast_list;
	net->open = net->stop = NULL;
#else
	ASSERT(!net->netdev_ops);
	net->netdev_ops = &dhd_ops_virt;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31) */

	/* Ok, link into the network layer... */
	if (ifidx == 0) {
		/*
		 * device functions for the primary interface only
		 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
		net->open = dhd_open;
		net->stop = dhd_stop;
#else
		net->netdev_ops = &dhd_ops_pri;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31) */
		if (!ETHER_ISNULLADDR(dhd->pub.mac.octet))
			memcpy(temp_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);
	} else {
		/*
		 * We have to use the primary MAC for virtual interfaces
		 */
		memcpy(temp_addr, ifp->mac_addr, ETHER_ADDR_LEN);
		/*
		 * Android sets the locally administered bit to indicate that this is a
		 * portable hotspot.  This will not work in simultaneous AP/STA mode,
		 * nor with P2P.  Need to set the Donlge's MAC address, and then use that.
		 */
		if (!memcmp(temp_addr, dhd->iflist[0]->mac_addr,
			ETHER_ADDR_LEN)) {
			DHD_ERROR(("%s interface [%s]: set locally administered bit in MAC\n",
			__func__, net->name));
			temp_addr[0] |= 0x02;
		}
	}

	net->hard_header_len = ETH_HLEN + dhd->pub.hdrlen;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	net->ethtool_ops = &dhd_ethtool_ops;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) */

#if defined(WL_WIRELESS_EXT)
#if WIRELESS_EXT < 19
	net->get_wireless_stats = dhd_get_wireless_stats;
#endif /* WIRELESS_EXT < 19 */
#if WIRELESS_EXT > 12
	net->wireless_handlers = &wl_iw_handler_def;
#endif /* WIRELESS_EXT > 12 */
#endif /* defined(WL_WIRELESS_EXT) */

	dhd->pub.rxsz = DBUS_RX_BUFFER_SIZE_DHD(net);

#ifdef WLMESH
	if (ifidx >= 2 && dhdp->conf->fw_type == FW_TYPE_MESH) {
		temp_addr[4] ^= 0x80;
		temp_addr[4] += ifidx;
		temp_addr[5] += ifidx;
	}
#endif
	memcpy(net->dev_addr, temp_addr, ETHER_ADDR_LEN);

	if (ifidx == 0)
		printf("%s\n", dhd_version);
	else {
#if defined(WL_EXT_IAPSTA) || defined(USE_IW) || defined(WL_ESCAN)
		wl_ext_event_attach_netdev(net, ifidx, ifp->bssidx);
#ifdef WL_ESCAN
		wl_escan_event_attach(net, dhdp);
#endif /* WL_ESCAN */
#ifdef WL_EXT_IAPSTA
		wl_ext_iapsta_attach_netdev(net, ifidx, ifp->bssidx);
#endif /* WL_EXT_IAPSTA */
#endif /* WL_EXT_IAPSTA || USE_IW || WL_ESCAN */
	}
	if (ifidx != 0) {
		if (_dhd_set_mac_address(dhd, ifidx, net->dev_addr) == 0)
			DHD_INFO(("%s: MACID is overwritten\n", __FUNCTION__));
		else
			DHD_ERROR(("%s: _dhd_set_mac_address() failed\n", __FUNCTION__));
	}

	if (need_rtnl_lock)
		err = register_netdev(net);
	else
		err = register_netdevice(net);

	if (err != 0) {
		DHD_ERROR(("couldn't register the net device [%s], err %d\n", net->name, err));
		goto fail;
	}
	if (ifidx == 0) {
#if defined(WL_EXT_IAPSTA) || defined(USE_IW) || defined(WL_ESCAN)
		wl_ext_event_attach_netdev(net, ifidx, ifp->bssidx);
#ifdef WL_ESCAN
		wl_escan_event_attach(net, dhdp);
#endif /* WL_ESCAN */
#ifdef WL_EXT_IAPSTA
		wl_ext_iapsta_attach_netdev(net, ifidx, ifp->bssidx);
#endif /* WL_EXT_IAPSTA */
#endif /* WL_EXT_IAPSTA || USE_IW || WL_ESCAN */
	}
#ifdef WL_EXT_IAPSTA
	wl_ext_iapsta_attach_name(net, ifidx);
#endif /* WL_EXT_IAPSTA */



	printf("Register interface [%s]  MAC: "MACDBG"\n\n", net->name,
#if defined(CUSTOMER_HW4_DEBUG)
		MAC2STRDBG(dhd->pub.mac.octet));
#else
		MAC2STRDBG(net->dev_addr));
#endif /* CUSTOMER_HW4_DEBUG */

#if defined(SOFTAP) && defined(WL_WIRELESS_EXT) && !defined(WL_CFG80211)
//		wl_iw_iscan_set_scan_broadcast_prep(net, 1);
#endif

#if (defined(BCMPCIE) || (defined(BCMLXSDMMC) && (LINUX_VERSION_CODE >= \
	KERNEL_VERSION(2, 6, 27))) || defined(BCMDBUS))
	if (ifidx == 0) {
#if defined(BCMLXSDMMC) && !defined(DHD_PRELOAD)
		up(&dhd_registration_sem);
#endif /* BCMLXSDMMC */
		if (!dhd_download_fw_on_driverload) {
#ifdef WL_CFG80211
			wl_terminate_event_handler(net);
#endif /* WL_CFG80211 */
#if defined(DHD_LB_RXP)
			__skb_queue_purge(&dhd->rx_pend_queue);
#endif /* DHD_LB_RXP */

#if defined(DHD_LB_TXP)
			skb_queue_purge(&dhd->tx_pend_queue);
#endif /* DHD_LB_TXP */

#ifdef SHOW_LOGTRACE
			/* Release the skbs from queue for WLC_E_TRACE event */
			dhd_event_logtrace_flush_queue(dhdp);
#endif /* SHOW_LOGTRACE */

#ifdef DHDTCPACK_SUPPRESS
			dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_OFF);
#endif /* DHDTCPACK_SUPPRESS */
			dhd_net_bus_devreset(net, TRUE);
#ifdef BCMLXSDMMC
			dhd_net_bus_suspend(net);
#endif /* BCMLXSDMMC */
			wifi_platform_set_power(dhdp->info->adapter, FALSE, WIFI_TURNOFF_DELAY);
#if defined(BT_OVER_SDIO)
			dhd->bus_user_count--;
#endif /* BT_OVER_SDIO */
		}
#if defined(WL_WIRELESS_EXT)
		wl_iw_down(net, &dhd->pub);
#endif /* defined(WL_WIRELESS_EXT) */
	}
#endif /* OEM_ANDROID && (BCMPCIE || (BCMLXSDMMC && KERNEL_VERSION >= 2.6.27)) */
	return 0;

fail:
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
	net->open = NULL;
#else
	net->netdev_ops = NULL;
#endif
	return err;
}

void
dhd_bus_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhdp) {
		dhd = (dhd_info_t *)dhdp->info;
		if (dhd) {

			/*
			 * In case of Android cfg80211 driver, the bus is down in dhd_stop,
			 *  calling stop again will cuase SD read/write errors.
			 */
			if (dhd->pub.busstate != DHD_BUS_DOWN && dhd_download_fw_on_driverload) {
				/* Stop the protocol module */
				dhd_prot_stop(&dhd->pub);

				/* Stop the bus module */
#ifdef BCMDBUS
				/* Force Dongle terminated */
				if (dhd_wl_ioctl_cmd(dhdp, WLC_TERMINATED, NULL, 0, TRUE, 0) < 0)
					DHD_ERROR(("%s Setting WLC_TERMINATED failed\n",
						__FUNCTION__));
				dbus_stop(dhd->pub.bus);
				dhd->pub.busstate = DHD_BUS_DOWN;
#else
				dhd_bus_stop(dhd->pub.bus, TRUE);
#endif /* BCMDBUS */
			}

#if defined(OOB_INTR_ONLY) || defined(BCMPCIE_OOB_HOST_WAKE)
			dhd_bus_oob_intr_unregister(dhdp);
#endif 
		}
	}
}


void dhd_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	unsigned long flags;
	int timer_valid = FALSE;
	struct net_device *dev;
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = NULL;
#endif
#ifdef HOFFLOAD_MODULES
	struct module_metadata *hmem = NULL;
#endif
	if (!dhdp)
		return;

	dhd = (dhd_info_t *)dhdp->info;
	if (!dhd)
		return;

	dev = dhd->iflist[0]->net;

	if (dev) {
		rtnl_lock();
		if (dev->flags & IFF_UP) {
			/* If IFF_UP is still up, it indicates that
			 * "ifconfig wlan0 down" hasn't been called.
			 * So invoke dev_close explicitly here to
			 * bring down the interface.
			 */
			DHD_TRACE(("IFF_UP flag is up. Enforcing dev_close from detach \n"));
			dev_close(dev);
		}
		rtnl_unlock();
	}

	DHD_TRACE(("%s: Enter state 0x%x\n", __FUNCTION__, dhd->dhd_state));

	dhd->pub.up = 0;
	if (!(dhd->dhd_state & DHD_ATTACH_STATE_DONE)) {
		/* Give sufficient time for threads to start running in case
		 * dhd_attach() has failed
		 */
		OSL_SLEEP(100);
	}
#ifdef DHD_WET
	dhd_free_wet_info(&dhd->pub, dhd->pub.wet_info);
#endif /* DHD_WET */
#if defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW)
#endif /* defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW) */

#ifdef PROP_TXSTATUS
#ifdef DHD_WLFC_THREAD
	if (dhd->pub.wlfc_thread) {
		kthread_stop(dhd->pub.wlfc_thread);
		dhdp->wlfc_thread_go = TRUE;
		wake_up_interruptible(&dhdp->wlfc_wqhead);
	}
	dhd->pub.wlfc_thread = NULL;
#endif /* DHD_WLFC_THREAD */
#endif /* PROP_TXSTATUS */

#ifdef DHD_TIMESYNC
	if (dhd->dhd_state & DHD_ATTACH_TIMESYNC_ATTACH_DONE) {
		dhd_timesync_detach(dhdp);
	}
#endif /* DHD_TIMESYNC */
#ifdef WL_CFG80211
	if (dev) {
		wl_cfg80211_down(dev);
	}
#endif /* WL_CFG80211 */

	if (dhd->dhd_state & DHD_ATTACH_STATE_PROT_ATTACH) {
		dhd_bus_detach(dhdp);
#ifdef BCMPCIE
		if (is_reboot == SYS_RESTART) {
			extern bcmdhd_wifi_platdata_t *dhd_wifi_platdata;
			if (dhd_wifi_platdata && !dhdp->dongle_reset) {
				dhdpcie_bus_clock_stop(dhdp->bus);
				wifi_platform_set_power(dhd_wifi_platdata->adapters,
					FALSE, WIFI_TURNOFF_DELAY);
			}
		}
#endif /* BCMPCIE */
#ifndef PCIE_FULL_DONGLE
		if (dhdp->prot)
			dhd_prot_detach(dhdp);
#endif /* !PCIE_FULL_DONGLE */
	}

#ifdef ARP_OFFLOAD_SUPPORT
	if (dhd_inetaddr_notifier_registered) {
		dhd_inetaddr_notifier_registered = FALSE;
		unregister_inetaddr_notifier(&dhd_inetaddr_notifier);
	}
#endif /* ARP_OFFLOAD_SUPPORT */
#if defined(CONFIG_IPV6) && defined(IPV6_NDO_SUPPORT)
	if (dhd_inet6addr_notifier_registered) {
		dhd_inet6addr_notifier_registered = FALSE;
		unregister_inet6addr_notifier(&dhd_inet6addr_notifier);
	}
#endif /* CONFIG_IPV6 && IPV6_NDO_SUPPORT */
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
	if (dhd->dhd_state & DHD_ATTACH_STATE_EARLYSUSPEND_DONE) {
		if (dhd->early_suspend.suspend)
			unregister_early_suspend(&dhd->early_suspend);
	}
#endif /* CONFIG_HAS_EARLYSUSPEND && DHD_USE_EARLYSUSPEND */

#if defined(WL_WIRELESS_EXT)
	if (dhd->dhd_state & DHD_ATTACH_STATE_WL_ATTACH) {
		/* Detatch and unlink in the iw */
		wl_iw_detach(dev, dhdp);
	}
#endif /* defined(WL_WIRELESS_EXT) */
#if defined(WL_EXT_IAPSTA) || defined(USE_IW) || defined(WL_ESCAN)
#ifdef WL_EXT_IAPSTA
	wl_ext_iapsta_dettach(dhdp);
#endif /* WL_EXT_IAPSTA */
#ifdef WL_ESCAN
	wl_escan_detach(dev, dhdp);
#endif /* WL_ESCAN */
	wl_ext_event_dettach(dhdp);
#endif /* WL_EXT_IAPSTA || USE_IW || WL_ESCAN */

#ifdef DHD_ULP
	dhd_ulp_deinit(dhd->pub.osh, dhdp);
#endif /* DHD_ULP */

	/* delete all interfaces, start with virtual  */
	if (dhd->dhd_state & DHD_ATTACH_STATE_ADD_IF) {
		int i = 1;
		dhd_if_t *ifp;

		/* Cleanup virtual interfaces */
		dhd_net_if_lock_local(dhd);
		for (i = 1; i < DHD_MAX_IFS; i++) {
			if (dhd->iflist[i]) {
				dhd_remove_if(&dhd->pub, i, TRUE);
			}
		}
		dhd_net_if_unlock_local(dhd);

		/*  delete primary interface 0 */
		ifp = dhd->iflist[0];
		ASSERT(ifp);
		ASSERT(ifp->net);
		if (ifp && ifp->net) {
#ifdef WL_CFG80211
			cfg = wl_get_cfg(ifp->net);
#endif
			/* in unregister_netdev case, the interface gets freed by net->destructor
			 * (which is set to free_netdev)
			 */
			if (ifp->net->reg_state == NETREG_UNINITIALIZED) {
				free_netdev(ifp->net);
			} else {
				argos_register_notifier_deinit();
#ifdef SET_RPS_CPUS
				custom_rps_map_clear(ifp->net->_rx);
#endif /* SET_RPS_CPUS */
				netif_tx_disable(ifp->net);
				unregister_netdev(ifp->net);
			}
#ifdef PCIE_FULL_DONGLE
			ifp->net = DHD_NET_DEV_NULL;
#else
			ifp->net = NULL;
#endif /* PCIE_FULL_DONGLE */

#ifdef DHD_WMF
			dhd_wmf_cleanup(dhdp, 0);
#endif /* DHD_WMF */
#ifdef DHD_L2_FILTER
			bcm_l2_filter_arp_table_update(dhdp->osh, ifp->phnd_arp_table, TRUE,
				NULL, FALSE, dhdp->tickcnt);
			deinit_l2_filter_arp_table(dhdp->osh, ifp->phnd_arp_table);
			ifp->phnd_arp_table = NULL;
#endif /* DHD_L2_FILTER */


			dhd_if_del_sta_list(ifp);

			MFREE(dhd->pub.osh, ifp, sizeof(*ifp));
			dhd->iflist[0] = NULL;
		}
	}

	/* Clear the watchdog timer */
	DHD_GENERAL_LOCK(&dhd->pub, flags);
	timer_valid = dhd->wd_timer_valid;
	dhd->wd_timer_valid = FALSE;
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);
	if (timer_valid)
		del_timer_sync(&dhd->timer);
	DHD_DISABLE_RUNTIME_PM(&dhd->pub);

#ifdef BCMDBUS
	tasklet_kill(&dhd->tasklet);
#else
	if (dhd->dhd_state & DHD_ATTACH_STATE_THREADS_CREATED) {
#ifdef DHD_PCIE_RUNTIMEPM
		if (dhd->thr_rpm_ctl.thr_pid >= 0) {
			PROC_STOP(&dhd->thr_rpm_ctl);
		}
#endif /* DHD_PCIE_RUNTIMEPM */
		if (dhd->thr_wdt_ctl.thr_pid >= 0) {
			PROC_STOP(&dhd->thr_wdt_ctl);
		}

		if (dhd->rxthread_enabled && dhd->thr_rxf_ctl.thr_pid >= 0) {
			PROC_STOP(&dhd->thr_rxf_ctl);
		}

		if (dhd->thr_dpc_ctl.thr_pid >= 0) {
			PROC_STOP(&dhd->thr_dpc_ctl);
		} else
		{
			tasklet_kill(&dhd->tasklet);
		}
	}
#endif /* BCMDBUS */

#ifdef DHD_LB
	if (dhd->dhd_state & DHD_ATTACH_STATE_LB_ATTACH_DONE) {
		/* Clear the flag first to avoid calling the cpu notifier */
		dhd->dhd_state &= ~DHD_ATTACH_STATE_LB_ATTACH_DONE;

		/* Kill the Load Balancing Tasklets */
#ifdef DHD_LB_RXP
		cancel_work_sync(&dhd->rx_napi_dispatcher_work);
		__skb_queue_purge(&dhd->rx_pend_queue);
#endif /* DHD_LB_RXP */
#ifdef DHD_LB_TXP
		cancel_work_sync(&dhd->tx_dispatcher_work);
		tasklet_kill(&dhd->tx_tasklet);
		__skb_queue_purge(&dhd->tx_pend_queue);
#endif /* DHD_LB_TXP */
#ifdef DHD_LB_TXC
		cancel_work_sync(&dhd->tx_compl_dispatcher_work);
		tasklet_kill(&dhd->tx_compl_tasklet);
#endif /* DHD_LB_TXC */
#ifdef DHD_LB_RXC
		tasklet_kill(&dhd->rx_compl_tasklet);
#endif /* DHD_LB_RXC */

		if (dhd->cpu_notifier.notifier_call != NULL) {
			unregister_cpu_notifier(&dhd->cpu_notifier);
		}
		dhd_cpumasks_deinit(dhd);
		DHD_LB_STATS_DEINIT(&dhd->pub);
	}
#endif /* DHD_LB */

	DHD_SSSR_MEMPOOL_DEINIT(&dhd->pub);

#ifdef DHD_LOG_DUMP
	dhd_log_dump_deinit(&dhd->pub);
#endif /* DHD_LOG_DUMP */
#ifdef WL_CFG80211
	if (dhd->dhd_state & DHD_ATTACH_STATE_CFG80211) {
		if (!cfg) {
			DHD_ERROR(("cfg NULL!\n"));
			ASSERT(0);
		} else {
			wl_cfg80211_detach(cfg);
			dhd_monitor_uninit();
		}
	}
#endif

#ifdef DEBUGABILITY
	if (dhdp->dbg) {
#ifdef DBG_PKT_MON
		dhd_os_dbg_detach_pkt_monitor(dhdp);
		dhd_os_spin_lock_deinit(dhd->pub.osh, dhd->pub.dbg->pkt_mon_lock);
#endif /* DBG_PKT_MON */
		dhd_os_dbg_detach(dhdp);
	}
#endif /* DEBUGABILITY */
#ifdef SHOW_LOGTRACE
#ifdef DHD_PKT_LOGGING
	dhd_os_detach_pktlog(dhdp);
#endif /* DHD_PKT_LOGGING */
	/* Release the skbs from queue for WLC_E_TRACE event */
	dhd_event_logtrace_flush_queue(dhdp);

	if (dhd->dhd_state & DHD_ATTACH_LOGTRACE_INIT) {
		if (dhd->event_data.fmts) {
			MFREE(dhd->pub.osh, dhd->event_data.fmts,
					dhd->event_data.fmts_size);
			dhd->event_data.fmts = NULL;
		}
		if (dhd->event_data.raw_fmts) {
			MFREE(dhd->pub.osh, dhd->event_data.raw_fmts,
					dhd->event_data.raw_fmts_size);
			dhd->event_data.raw_fmts = NULL;
		}
		if (dhd->event_data.raw_sstr) {
			MFREE(dhd->pub.osh, dhd->event_data.raw_sstr,
					dhd->event_data.raw_sstr_size);
			dhd->event_data.raw_sstr = NULL;
		}
		if (dhd->event_data.rom_raw_sstr) {
			MFREE(dhd->pub.osh, dhd->event_data.rom_raw_sstr,
					dhd->event_data.rom_raw_sstr_size);
			dhd->event_data.rom_raw_sstr = NULL;
		}
		dhd->dhd_state &= ~DHD_ATTACH_LOGTRACE_INIT;
	}
#endif /* SHOW_LOGTRACE */
#ifdef BCMPCIE
	if (dhdp->extended_trap_data)
	{
		MFREE(dhdp->osh, dhdp->extended_trap_data, BCMPCIE_EXT_TRAP_DATA_MAXLEN);
		dhdp->extended_trap_data = NULL;
	}
#endif /* BCMPCIE */
#ifdef PNO_SUPPORT
	if (dhdp->pno_state)
		dhd_pno_deinit(dhdp);
#endif
#ifdef RTT_SUPPORT
	if (dhdp->rtt_state) {
		dhd_rtt_deinit(dhdp);
	}
#endif
#if defined(CONFIG_PM_SLEEP)
	if (dhd_pm_notifier_registered) {
		unregister_pm_notifier(&dhd->pm_notifier);
		dhd_pm_notifier_registered = FALSE;
	}
#endif /* CONFIG_PM_SLEEP */

#ifdef DEBUG_CPU_FREQ
		if (dhd->new_freq)
			free_percpu(dhd->new_freq);
		dhd->new_freq = NULL;
		cpufreq_unregister_notifier(&dhd->freq_trans, CPUFREQ_TRANSITION_NOTIFIER);
#endif
	DHD_TRACE(("wd wakelock count:%d\n", dhd->wakelock_wd_counter));
#ifdef CONFIG_HAS_WAKELOCK
	dhd->wakelock_wd_counter = 0;
	wake_lock_destroy(&dhd->wl_wdwake);
	// terence 20161023: can not destroy wl_wifi when wlan down, it will happen null pointer in dhd_ioctl_entry
	wake_lock_destroy(&dhd->wl_wifi);
#endif /* CONFIG_HAS_WAKELOCK */
	if (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT) {
		DHD_OS_WAKE_LOCK_DESTROY(dhd);
	}



#ifdef DHDTCPACK_SUPPRESS
	/* This will free all MEM allocated for TCPACK SUPPRESS */
	dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_OFF);
#endif /* DHDTCPACK_SUPPRESS */

#ifdef PCIE_FULL_DONGLE
	dhd_flow_rings_deinit(dhdp);
	if (dhdp->prot)
		dhd_prot_detach(dhdp);
#endif

#if defined(WLTDLS) && defined(PCIE_FULL_DONGLE)
		dhd_free_tdls_peer_list(dhdp);
#endif

#ifdef HOFFLOAD_MODULES
	hmem = &dhdp->hmem;
	dhd_free_module_memory(dhdp->bus, hmem);
#endif /* HOFFLOAD_MODULES */
#if defined(BT_OVER_SDIO)
	mutex_destroy(&dhd->bus_user_lock);
#endif /* BT_OVER_SDIO */
#ifdef DUMP_IOCTL_IOV_LIST
	dhd_iov_li_delete(dhdp, &(dhdp->dump_iovlist_head));
#endif /* DUMP_IOCTL_IOV_LIST */
#ifdef DHD_DEBUG
	/* memory waste feature list initilization */
	dhd_mw_list_delete(dhdp, &(dhdp->mw_list_head));
#endif /* DHD_DEBUG */
#ifdef WL_MONITOR
	dhd_del_monitor_if(dhd, NULL, DHD_WQ_WORK_IF_DEL);
#endif /* WL_MONITOR */

	/* Prefer adding de-init code above this comment unless necessary.
	 * The idea is to cancel work queue, sysfs and flags at the end.
	 */
	dhd_deferred_work_deinit(dhd->dhd_deferred_wq);
	dhd->dhd_deferred_wq = NULL;

#ifdef SHOW_LOGTRACE
	/* Wait till event_log_dispatcher_work finishes */
	cancel_work_sync(&dhd->event_log_dispatcher_work);
#endif /* SHOW_LOGTRACE */

	dhd_sysfs_exit(dhd);
	dhd->pub.fw_download_done = FALSE;
	dhd_conf_detach(dhdp);
}


void
dhd_free(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhdp) {
		int i;
		for (i = 0; i < ARRAYSIZE(dhdp->reorder_bufs); i++) {
			if (dhdp->reorder_bufs[i]) {
				reorder_info_t *ptr;
				uint32 buf_size = sizeof(struct reorder_info);

				ptr = dhdp->reorder_bufs[i];

				buf_size += ((ptr->max_idx + 1) * sizeof(void*));
				DHD_REORDER(("free flow id buf %d, maxidx is %d, buf_size %d\n",
					i, ptr->max_idx, buf_size));

				MFREE(dhdp->osh, dhdp->reorder_bufs[i], buf_size);
				dhdp->reorder_bufs[i] = NULL;
			}
		}

		dhd_sta_pool_fini(dhdp, DHD_MAX_STA);

		dhd = (dhd_info_t *)dhdp->info;
		if (dhdp->soc_ram) {
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
			DHD_OS_PREFREE(dhdp, dhdp->soc_ram, dhdp->soc_ram_length);
#else
			MFREE(dhdp->osh, dhdp->soc_ram, dhdp->soc_ram_length);
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_MEMDUMP */
			dhdp->soc_ram = NULL;
		}
#ifdef CACHE_FW_IMAGES
		if (dhdp->cached_fw) {
			MFREE(dhdp->osh, dhdp->cached_fw, dhdp->bus->ramsize);
			dhdp->cached_fw = NULL;
		}

		if (dhdp->cached_nvram) {
			MFREE(dhdp->osh, dhdp->cached_nvram, MAX_NVRAMBUF_SIZE);
			dhdp->cached_nvram = NULL;
		}
#endif
		if (dhd) {
#ifdef REPORT_FATAL_TIMEOUTS
			deinit_dhd_timeouts(&dhd->pub);
#endif /* REPORT_FATAL_TIMEOUTS */

			/* If pointer is allocated by dhd_os_prealloc then avoid MFREE */
			if (dhd != (dhd_info_t *)dhd_os_prealloc(dhdp,
					DHD_PREALLOC_DHD_INFO, 0, FALSE))
				MFREE(dhd->pub.osh, dhd, sizeof(*dhd));
			dhd = NULL;
		}
	}
}

void
dhd_clear(dhd_pub_t *dhdp)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhdp) {
		int i;
#ifdef DHDTCPACK_SUPPRESS
		/* Clean up timer/data structure for any remaining/pending packet or timer. */
		dhd_tcpack_info_tbl_clean(dhdp);
#endif /* DHDTCPACK_SUPPRESS */
		for (i = 0; i < ARRAYSIZE(dhdp->reorder_bufs); i++) {
			if (dhdp->reorder_bufs[i]) {
				reorder_info_t *ptr;
				uint32 buf_size = sizeof(struct reorder_info);

				ptr = dhdp->reorder_bufs[i];

				buf_size += ((ptr->max_idx + 1) * sizeof(void*));
				DHD_REORDER(("free flow id buf %d, maxidx is %d, buf_size %d\n",
					i, ptr->max_idx, buf_size));

				MFREE(dhdp->osh, dhdp->reorder_bufs[i], buf_size);
				dhdp->reorder_bufs[i] = NULL;
			}
		}

		dhd_sta_pool_clear(dhdp, DHD_MAX_STA);

		if (dhdp->soc_ram) {
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
			DHD_OS_PREFREE(dhdp, dhdp->soc_ram, dhdp->soc_ram_length);
#else
			MFREE(dhdp->osh, dhdp->soc_ram, dhdp->soc_ram_length);
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_MEMDUMP */
			dhdp->soc_ram = NULL;
		}
	}
}

static void
dhd_module_cleanup(void)
{
	printf("%s: Enter\n", __FUNCTION__);

	dhd_bus_unregister();

	wl_android_exit();

	dhd_wifi_platform_unregister_drv();
	printf("%s: Exit\n", __FUNCTION__);
}

static void
dhd_module_exit(void)
{
	atomic_set(&exit_in_progress, 1);
	dhd_module_cleanup();
	unregister_reboot_notifier(&dhd_reboot_notifier);
	dhd_destroy_to_notifier_skt();
}

static int
dhd_module_init(void)
{
	int err;
	int retry = 0;

	printf("%s: in %s\n", __FUNCTION__, dhd_version);

	DHD_PERIM_RADIO_INIT();


	if (firmware_path[0] != '\0') {
		strncpy(fw_bak_path, firmware_path, MOD_PARAM_PATHLEN);
		fw_bak_path[MOD_PARAM_PATHLEN-1] = '\0';
	}

	if (nvram_path[0] != '\0') {
		strncpy(nv_bak_path, nvram_path, MOD_PARAM_PATHLEN);
		nv_bak_path[MOD_PARAM_PATHLEN-1] = '\0';
	}

	do {
		err = dhd_wifi_platform_register_drv();
		if (!err) {
			register_reboot_notifier(&dhd_reboot_notifier);
			break;
		} else {
			DHD_ERROR(("%s: Failed to load the driver, try cnt %d\n",
				__FUNCTION__, retry));
			strncpy(firmware_path, fw_bak_path, MOD_PARAM_PATHLEN);
			firmware_path[MOD_PARAM_PATHLEN-1] = '\0';
			strncpy(nvram_path, nv_bak_path, MOD_PARAM_PATHLEN);
			nvram_path[MOD_PARAM_PATHLEN-1] = '\0';
		}
	} while (retry--);

	dhd_create_to_notifier_skt();

	if (err) {
		DHD_ERROR(("%s: Failed to load driver max retry reached**\n", __FUNCTION__));
	} else {
		if (!dhd_download_fw_on_driverload) {
			dhd_driver_init_done = TRUE;
		}
	}

	printf("%s: Exit err=%d\n", __FUNCTION__, err);
	return err;
}

static int
dhd_reboot_callback(struct notifier_block *this, unsigned long code, void *unused)
{
	DHD_TRACE(("%s: code = %ld\n", __FUNCTION__, code));
	if (code == SYS_RESTART) {
#ifdef BCMPCIE
		is_reboot = code;
#endif /* BCMPCIE */
	}
	return NOTIFY_DONE;
}

#ifdef CONFIG_WIFI_LOAD_DRIVER_WHEN_KERNEL_BOOTUP
static int wifi_init_thread(void *data)
{
	dhd_module_init();
	return 0;
}
#endif

int rockchip_wifi_init_module_rkwifi(void)
{
#ifdef CONFIG_WIFI_LOAD_DRIVER_WHEN_KERNEL_BOOTUP
	struct task_struct *kthread = NULL;

	kthread = kthread_run(wifi_init_thread, NULL, "wifi_init_thread");
	if (IS_ERR(kthread))
		pr_err("create wifi_init_thread failed.\n");
#else
	dhd_module_init();
#endif
	return 0;
}

void rockchip_wifi_exit_module_rkwifi(void)
{
	dhd_module_exit();
}
#ifdef CONFIG_WIFI_BUILD_MODULE
module_init(rockchip_wifi_init_module_rkwifi);
module_exit(rockchip_wifi_exit_module_rkwifi);
#else
#ifdef CONFIG_WIFI_LOAD_DRIVER_WHEN_KERNEL_BOOTUP
late_initcall(rockchip_wifi_init_module_rkwifi);
module_exit(rockchip_wifi_exit_module_rkwifi);
#else
module_init(rockchip_wifi_init_module_rkwifi);
module_exit(rockchip_wifi_exit_module_rkwifi);
#endif
#endif
#if 0
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
#if defined(CONFIG_DEFERRED_INITCALLS) && !defined(EXYNOS_PCIE_MODULE_PATCH)
#if defined(CONFIG_MACH_UNIVERSAL7420) || defined(CONFIG_SOC_EXYNOS8890) || \
	defined(CONFIG_ARCH_MSM8996) || defined(CONFIG_SOC_EXYNOS8895) || \
	defined(CONFIG_ARCH_MSM8998)
deferred_module_init_sync(dhd_module_init);
#else
deferred_module_init(dhd_module_init);
#endif /* CONFIG_MACH_UNIVERSAL7420 || CONFIG_SOC_EXYNOS8890 ||
	* CONFIG_ARCH_MSM8996 || CONFIG_SOC_EXYNOS8895 || CONFIG_ARCH_MSM8998
	*/
#elif defined(USE_LATE_INITCALL_SYNC)
late_initcall_sync(dhd_module_init);
#else
late_initcall(dhd_module_init);
#endif /* USE_LATE_INITCALL_SYNC */
#else
module_init(dhd_module_init);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) */

module_exit(dhd_module_exit);

#endif
/*
 * OS specific functions required to implement DHD driver in OS independent way
 */
int
dhd_os_proto_block(dhd_pub_t *pub)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		DHD_PERIM_UNLOCK(pub);

		down(&dhd->proto_sem);

		DHD_PERIM_LOCK(pub);
		return 1;
	}

	return 0;
}

int
dhd_os_proto_unblock(dhd_pub_t *pub)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		up(&dhd->proto_sem);
		return 1;
	}

	return 0;
}

void
dhd_os_dhdiovar_lock(dhd_pub_t *pub)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		mutex_lock(&dhd->dhd_iovar_mutex);
	}
}

void
dhd_os_dhdiovar_unlock(dhd_pub_t *pub)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		mutex_unlock(&dhd->dhd_iovar_mutex);
	}
}

unsigned int
dhd_os_get_ioctl_resp_timeout(void)
{
	return ((unsigned int)dhd_ioctl_timeout_msec);
}

void
dhd_os_set_ioctl_resp_timeout(unsigned int timeout_msec)
{
	dhd_ioctl_timeout_msec = (int)timeout_msec;
}

int
dhd_os_ioctl_resp_wait(dhd_pub_t *pub, uint *condition)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	/* Convert timeout in millsecond to jiffies */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	timeout = msecs_to_jiffies(dhd_ioctl_timeout_msec);
#else
	timeout = dhd_ioctl_timeout_msec * HZ / 1000;
#endif

	DHD_PERIM_UNLOCK(pub);

	timeout = wait_event_timeout(dhd->ioctl_resp_wait, (*condition), timeout);

	DHD_PERIM_LOCK(pub);

	return timeout;
}

int
dhd_os_ioctl_resp_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	wake_up(&dhd->ioctl_resp_wait);
	return 0;
}

int
dhd_os_d3ack_wait(dhd_pub_t *pub, uint *condition)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	/* Convert timeout in millsecond to jiffies */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	timeout = msecs_to_jiffies(dhd_ioctl_timeout_msec);
#else
	timeout = dhd_ioctl_timeout_msec * HZ / 1000;
#endif

	DHD_PERIM_UNLOCK(pub);

	timeout = wait_event_timeout(dhd->d3ack_wait, (*condition), timeout);

	DHD_PERIM_LOCK(pub);

	return timeout;
}

#ifdef PCIE_INB_DW
int
dhd_os_ds_exit_wait(dhd_pub_t *pub, uint *condition)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	/* Convert timeout in millsecond to jiffies */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	timeout = msecs_to_jiffies(ds_exit_timeout_msec);
#else
	timeout = ds_exit_timeout_msec * HZ / 1000;
#endif

	DHD_PERIM_UNLOCK(pub);

	timeout = wait_event_timeout(dhd->ds_exit_wait, (*condition), timeout);

	DHD_PERIM_LOCK(pub);

	return timeout;
}

int
dhd_os_ds_exit_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	wake_up(&dhd->ds_exit_wait);
	return 0;
}

#endif /* PCIE_INB_DW */

int
dhd_os_d3ack_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	wake_up(&dhd->d3ack_wait);
	return 0;
}

int
dhd_os_busbusy_wait_negation(dhd_pub_t *pub, uint *condition)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	/* Wait for bus usage contexts to gracefully exit within some timeout value
	 * Set time out to little higher than dhd_ioctl_timeout_msec,
	 * so that IOCTL timeout should not get affected.
	 */
	/* Convert timeout in millsecond to jiffies */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	timeout = msecs_to_jiffies(DHD_BUS_BUSY_TIMEOUT);
#else
	timeout = DHD_BUS_BUSY_TIMEOUT * HZ / 1000;
#endif

	timeout = wait_event_timeout(dhd->dhd_bus_busy_state_wait, !(*condition), timeout);

	return timeout;
}

/*
 * Wait until the condition *var == condition is met.
 * Returns 0 if the @condition evaluated to false after the timeout elapsed
 * Returns 1 if the @condition evaluated to true
 */
int
dhd_os_busbusy_wait_condition(dhd_pub_t *pub, uint *var, uint condition)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	/* Convert timeout in millsecond to jiffies */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	timeout = msecs_to_jiffies(DHD_BUS_BUSY_TIMEOUT);
#else
	timeout = DHD_BUS_BUSY_TIMEOUT * HZ / 1000;
#endif

	timeout = wait_event_timeout(dhd->dhd_bus_busy_state_wait, (*var == condition), timeout);

	return timeout;
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
/* Fix compilation error for FC11 */
INLINE
#endif
int
dhd_os_busbusy_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	/* Call wmb() to make sure before waking up the other event value gets updated */
	OSL_SMP_WMB();
	wake_up(&dhd->dhd_bus_busy_state_wait);
	return 0;
}

void
dhd_os_wd_timer_extend(void *bus, bool extend)
{
#ifndef BCMDBUS
	dhd_pub_t *pub = bus;
	dhd_info_t *dhd = (dhd_info_t *)pub->info;

	if (extend)
		dhd_os_wd_timer(bus, WATCHDOG_EXTEND_INTERVAL);
	else
		dhd_os_wd_timer(bus, dhd->default_wd_interval);
#endif /* !BCMDBUS */
}


void
dhd_os_wd_timer(void *bus, uint wdtick)
{
#ifndef BCMDBUS
	dhd_pub_t *pub = bus;
	dhd_info_t *dhd = (dhd_info_t *)pub->info;
	unsigned long flags;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!dhd) {
		DHD_ERROR(("%s: dhd NULL\n", __FUNCTION__));
		return;
	}

	DHD_GENERAL_LOCK(pub, flags);

	/* don't start the wd until fw is loaded */
	if (pub->busstate == DHD_BUS_DOWN) {
		DHD_GENERAL_UNLOCK(pub, flags);
		return;
	}

	/* Totally stop the timer */
	if (!wdtick && dhd->wd_timer_valid == TRUE) {
		dhd->wd_timer_valid = FALSE;
		DHD_GENERAL_UNLOCK(pub, flags);
		del_timer_sync(&dhd->timer);
		return;
	}

	if (wdtick) {
		dhd_watchdog_ms = (uint)wdtick;
		/* Re arm the timer, at last watchdog period */
		mod_timer(&dhd->timer, jiffies + msecs_to_jiffies(dhd_watchdog_ms));
		dhd->wd_timer_valid = TRUE;
	}
	DHD_GENERAL_UNLOCK(pub, flags);
#endif /* !BCMDBUS */
}

#ifdef DHD_PCIE_RUNTIMEPM
void
dhd_os_runtimepm_timer(void *bus, uint tick)
{
	dhd_pub_t *pub = bus;
	dhd_info_t *dhd = (dhd_info_t *)pub->info;
	unsigned long flags;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	DHD_GENERAL_LOCK(pub, flags);

	/* don't start the RPM until fw is loaded */
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(pub)) {
		DHD_GENERAL_UNLOCK(pub, flags);
		return;
	}

	/* If tick is non-zero, the request is to start the timer */
	if (tick) {
		/* Start the timer only if its not already running */
		if (dhd->rpm_timer_valid == FALSE) {
			mod_timer(&dhd->rpm_timer, jiffies + msecs_to_jiffies(dhd_runtimepm_ms));
			dhd->rpm_timer_valid = TRUE;
		}
	} else {
		/* tick is zero, we have to stop the timer */
		/* Stop the timer only if its running, otherwise we don't have to do anything */
		if (dhd->rpm_timer_valid == TRUE) {
			dhd->rpm_timer_valid = FALSE;
			DHD_GENERAL_UNLOCK(pub, flags);
			del_timer_sync(&dhd->rpm_timer);
			/* we have already released the lock, so just go to exit */
			goto exit;
		}
	}

	DHD_GENERAL_UNLOCK(pub, flags);
exit:
	return;

}

#endif /* DHD_PCIE_RUNTIMEPM */

void *
dhd_os_open_image(char *filename)
{
	struct file *fp;
	int size;

	fp = filp_open(filename, O_RDONLY, 0);
	/*
	 * 2.6.11 (FC4) supports filp_open() but later revs don't?
	 * Alternative:
	 * fp = open_namei(AT_FDCWD, filename, O_RD, 0);
	 * ???
	 */
	 if (IS_ERR(fp)) {
		 fp = NULL;
		 goto err;
	 }

	 if (!S_ISREG(file_inode(fp)->i_mode)) {
		 DHD_ERROR(("%s: %s is not regular file\n", __FUNCTION__, filename));
		 fp = NULL;
		 goto err;
	 }

	 size = i_size_read(file_inode(fp));
	 if (size <= 0) {
		 DHD_ERROR(("%s: %s file size invalid %d\n", __FUNCTION__, filename, size));
		 fp = NULL;
		 goto err;
	 }

	 DHD_ERROR(("%s: %s (%d bytes) open success\n", __FUNCTION__, filename, size));

err:
	 return fp;
}

int
dhd_os_get_image_block(char *buf, int len, void *image)
{
	struct file *fp = (struct file *)image;
	int rdlen;
	int size;

	if (!image) {
		return 0;
	}

	size = i_size_read(file_inode(fp));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	rdlen = kernel_read(fp, buf, MIN(len, size), &fp->f_pos);
#else
	rdlen = kernel_read(fp, fp->f_pos, buf, MIN(len, size));
#endif

	if (len >= size && size != rdlen) {
		return -EIO;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
	if (rdlen > 0) {
		fp->f_pos += rdlen;
	}
#endif

	return rdlen;
}

int
dhd_os_get_image_size(void *image)
{
	struct file *fp = (struct file *)image;
	int size;
	if (!image) {
		return 0;
	}

	size = i_size_read(file_inode(fp));

	return size;
}

#if defined(BT_OVER_SDIO)
int
dhd_os_gets_image(dhd_pub_t *pub, char *str, int len, void *image)
{
	struct file *fp = (struct file *)image;
	int rd_len;
	uint str_len = 0;
	char *str_end = NULL;

	if (!image)
		return 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	rd_len = kernel_read(fp, str, len, &fp->f_pos);
#else
	rd_len = kernel_read(fp, fp->f_pos, str, len);
#endif
	str_end = strnchr(str, len, '\n');
	if (str_end == NULL) {
		goto err;
	}
	str_len = (uint)(str_end - str);

	/* Advance file pointer past the string length */
	fp->f_pos += str_len + 1;
	bzero(str_end, rd_len - str_len);

err:
	return str_len;
}
#endif /* defined (BT_OVER_SDIO) */


void
dhd_os_close_image(void *image)
{
	if (image)
		filp_close((struct file *)image, NULL);
}

void
dhd_os_sdlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);

#ifdef BCMDBUS
	spin_lock_bh(&dhd->sdlock);
#else
	if (dhd_dpc_prio >= 0)
		down(&dhd->sdsem);
	else
		spin_lock_bh(&dhd->sdlock);
#endif /* !BCMDBUS */
}

void
dhd_os_sdunlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);

#ifdef BCMDBUS
	spin_unlock_bh(&dhd->sdlock);
#else
	if (dhd_dpc_prio >= 0)
		up(&dhd->sdsem);
	else
		spin_unlock_bh(&dhd->sdlock);
#endif /* !BCMDBUS */
}

void
dhd_os_sdlock_txq(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
#ifdef BCMDBUS
	spin_lock_irqsave(&dhd->txqlock, dhd->txqlock_flags);
#else
	spin_lock_bh(&dhd->txqlock);
#endif /* BCMDBUS */
}

void
dhd_os_sdunlock_txq(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
#ifdef BCMDBUS
	spin_unlock_irqrestore(&dhd->txqlock, dhd->txqlock_flags);
#else
	spin_unlock_bh(&dhd->txqlock);
#endif /* BCMDBUS */
}

void
dhd_os_sdlock_rxq(dhd_pub_t *pub)
{
#if 0
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_lock_bh(&dhd->rxqlock);
#endif
}

void
dhd_os_sdunlock_rxq(dhd_pub_t *pub)
{
#if 0
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_unlock_bh(&dhd->rxqlock);
#endif
}

static void
dhd_os_rxflock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_lock_bh(&dhd->rxf_lock);

}

static void
dhd_os_rxfunlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_unlock_bh(&dhd->rxf_lock);
}

#ifdef DHDTCPACK_SUPPRESS
unsigned long
dhd_os_tcpacklock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;
	unsigned long flags = 0;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
#ifdef BCMSDIO
		spin_lock_bh(&dhd->tcpack_lock);
#else
		spin_lock_irqsave(&dhd->tcpack_lock, flags);
#endif /* BCMSDIO */
	}

	return flags;
}

void
dhd_os_tcpackunlock(dhd_pub_t *pub, unsigned long flags)
{
	dhd_info_t *dhd;

#ifdef BCMSDIO
	BCM_REFERENCE(flags);
#endif /* BCMSDIO */

	dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
#ifdef BCMSDIO
		spin_unlock_bh(&dhd->tcpack_lock);
#else
		spin_unlock_irqrestore(&dhd->tcpack_lock, flags);
#endif /* BCMSDIO */
	}
}
#endif /* DHDTCPACK_SUPPRESS */

uint8* dhd_os_prealloc(dhd_pub_t *dhdpub, int section, uint size, bool kmalloc_if_fail)
{
	uint8* buf;
	gfp_t flags = CAN_SLEEP() ? GFP_KERNEL: GFP_ATOMIC;

	buf = (uint8*)wifi_platform_prealloc(dhdpub->info->adapter, section, size);
	if (buf == NULL && kmalloc_if_fail)
		buf = kmalloc(size, flags);

	return buf;
}

void dhd_os_prefree(dhd_pub_t *dhdpub, void *addr, uint size)
{
}

#if defined(WL_WIRELESS_EXT)
struct iw_statistics *
dhd_get_wireless_stats(struct net_device *dev)
{
	int res = 0;
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (!dhd->pub.up) {
		return NULL;
	}

	res = wl_iw_get_wireless_stats(dev, &dhd->iw.wstats);

	if (res == 0)
		return &dhd->iw.wstats;
	else
		return NULL;
}
#endif /* defined(WL_WIRELESS_EXT) */

static int
dhd_wl_host_event(dhd_info_t *dhd, int ifidx, void *pktdata, uint16 pktlen,
	wl_event_msg_t *event, void **data)
{
	int bcmerror = 0;
#ifdef WL_CFG80211
	unsigned long flags = 0;
#endif /* WL_CFG80211 */
	ASSERT(dhd != NULL);

#ifdef SHOW_LOGTRACE
	bcmerror = wl_process_host_event(&dhd->pub, &ifidx, pktdata, pktlen, event, data,
		&dhd->event_data);
#else
	bcmerror = wl_process_host_event(&dhd->pub, &ifidx, pktdata, pktlen, event, data,
		NULL);
#endif /* SHOW_LOGTRACE */

	if (bcmerror != BCME_OK)
		return (bcmerror);

#if defined(WL_EXT_IAPSTA) || defined(USE_IW)
	wl_ext_event_send(dhd->pub.event_params, event, *data);
#endif

#ifdef WL_CFG80211
	ASSERT(dhd->iflist[ifidx] != NULL);
	ASSERT(dhd->iflist[ifidx]->net != NULL);
	if (dhd->iflist[ifidx]->net) {
		spin_lock_irqsave(&dhd->pub.up_lock, flags);
		if (dhd->pub.up) {
			wl_cfg80211_event(dhd->iflist[ifidx]->net, event, *data);
		}
		spin_unlock_irqrestore(&dhd->pub.up_lock, flags);
	}
#endif /* defined(WL_CFG80211) */

	return (bcmerror);
}

/* send up locally generated event */
void
dhd_sendup_event(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data)
{
	/* Just return from here */
	return;
}

#ifdef LOG_INTO_TCPDUMP
void
dhd_sendup_log(dhd_pub_t *dhdp, void *data, int data_len)
{
	struct sk_buff *p, *skb;
	uint32 pktlen;
	int len;
	dhd_if_t *ifp;
	dhd_info_t *dhd;
	uchar *skb_data;
	int ifidx = 0;
	struct ether_header eth;

	pktlen = sizeof(eth) + data_len;
	dhd = dhdp->info;

	if ((p = PKTGET(dhdp->osh, pktlen, FALSE))) {
		ASSERT(ISALIGNED((uintptr)PKTDATA(dhdp->osh, p), sizeof(uint32)));

		bcopy(&dhdp->mac, &eth.ether_dhost, ETHER_ADDR_LEN);
		bcopy(&dhdp->mac, &eth.ether_shost, ETHER_ADDR_LEN);
		ETHER_TOGGLE_LOCALADDR(&eth.ether_shost);
		eth.ether_type = hton16(ETHER_TYPE_BRCM);

		bcopy((void *)&eth, PKTDATA(dhdp->osh, p), sizeof(eth));
		bcopy(data, PKTDATA(dhdp->osh, p) + sizeof(eth), data_len);
		skb = PKTTONATIVE(dhdp->osh, p);
		skb_data = skb->data;
		len = skb->len;

		ifidx = dhd_ifname2idx(dhd, "wlan0");
		ifp = dhd->iflist[ifidx];
		if (ifp == NULL)
			 ifp = dhd->iflist[0];

		ASSERT(ifp);
		skb->dev = ifp->net;
		skb->protocol = eth_type_trans(skb, skb->dev);
		skb->data = skb_data;
		skb->len = len;

		/* Strip header, count, deliver upward */
		skb_pull(skb, ETH_HLEN);

		bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE,
			__FUNCTION__, __LINE__);
		/* Send the packet */
		if (in_interrupt()) {
			netif_rx(skb);
		} else {
			netif_rx_ni(skb);
		}
	} else {
		/* Could not allocate a sk_buf */
		DHD_ERROR(("%s: unable to alloc sk_buf\n", __FUNCTION__));
	}
}
#endif /* LOG_INTO_TCPDUMP */

void dhd_wait_for_event(dhd_pub_t *dhd, bool *lockvar)
{
#if defined(BCMSDIO) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
	struct dhd_info *dhdinfo =  dhd->info;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	int timeout = msecs_to_jiffies(IOCTL_RESP_TIMEOUT);
#else
	int timeout = (IOCTL_RESP_TIMEOUT / 1000) * HZ;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */

	dhd_os_sdunlock(dhd);
	wait_event_timeout(dhdinfo->ctrl_wait, (*lockvar == FALSE), timeout);
	dhd_os_sdlock(dhd);
#endif /* defined(BCMSDIO) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)) */
	return;
}

void dhd_wait_event_wakeup(dhd_pub_t *dhd)
{
#if defined(BCMSDIO) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
	struct dhd_info *dhdinfo =  dhd->info;
	if (waitqueue_active(&dhdinfo->ctrl_wait))
		wake_up(&dhdinfo->ctrl_wait);
#endif
	return;
}

#if defined(BCMSDIO) || defined(BCMPCIE) || defined(BCMDBUS)
int
dhd_net_bus_devreset(struct net_device *dev, uint8 flag)
{
	int ret;

	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (flag == TRUE) {
		/* Issue wl down command before resetting the chip */
		if (dhd_wl_ioctl_cmd(&dhd->pub, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_TRACE(("%s: wl down failed\n", __FUNCTION__));
		}
#ifdef PROP_TXSTATUS
		if (dhd->pub.wlfc_enabled) {
			dhd_wlfc_deinit(&dhd->pub);
		}
#endif /* PROP_TXSTATUS */
#ifdef PNO_SUPPORT
		if (dhd->pub.pno_state) {
			dhd_pno_deinit(&dhd->pub);
		}
#endif
#ifdef RTT_SUPPORT
		if (dhd->pub.rtt_state) {
			dhd_rtt_deinit(&dhd->pub);
		}
#endif /* RTT_SUPPORT */

#if defined(DBG_PKT_MON) && !defined(DBG_PKT_MON_INIT_DEFAULT)
		dhd_os_dbg_detach_pkt_monitor(&dhd->pub);
#endif /* DBG_PKT_MON */
	}

#ifdef BCMSDIO
	if (!flag) {
		dhd_update_fw_nv_path(dhd);
		/* update firmware and nvram path to sdio bus */
		dhd_bus_update_fw_nv_path(dhd->pub.bus,
			dhd->fw_path, dhd->nv_path, dhd->clm_path, dhd->conf_path);
	}
#endif /* BCMSDIO */

	ret = dhd_bus_devreset(&dhd->pub, flag);
	if (ret) {
		DHD_ERROR(("%s: dhd_bus_devreset: %d\n", __FUNCTION__, ret));
		return ret;
	}

	return ret;
}

#ifdef BCMSDIO
int
dhd_net_bus_suspend(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return dhd_bus_suspend(&dhd->pub);
}

int
dhd_net_bus_resume(struct net_device *dev, uint8 stage)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return dhd_bus_resume(&dhd->pub, stage);
}

#endif /* BCMSDIO */
#endif /* BCMSDIO || BCMPCIE || BCMDBUS */

int net_os_set_suspend_disable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd) {
		ret = dhd->pub.suspend_disable_flag;
		dhd->pub.suspend_disable_flag = val;
	}
	return ret;
}

int net_os_set_suspend(struct net_device *dev, int val, int force)
{
	int ret = 0;
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (dhd) {
#ifdef CONFIG_MACH_UNIVERSAL7420
#endif /* CONFIG_MACH_UNIVERSAL7420 */
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
		ret = dhd_set_suspend(val, &dhd->pub);
#else
		ret = dhd_suspend_resume_helper(dhd, val, force);
#endif
#ifdef WL_CFG80211
		wl_cfg80211_update_power_mode(dev);
#endif
	}
	return ret;
}

int net_os_set_suspend_bcn_li_dtim(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (dhd)
		dhd->pub.suspend_bcn_li_dtim = val;

	return 0;
}

int net_os_set_max_dtim_enable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (dhd) {
		DHD_ERROR(("%s: use MAX bcn_li_dtim in suspend %s\n",
			__FUNCTION__, (val ? "Enable" : "Disable")));
		if (val) {
			dhd->pub.max_dtim_enable = TRUE;
		} else {
			dhd->pub.max_dtim_enable = FALSE;
		}
	} else {
		return -1;
	}

	return 0;
}

#ifdef PKT_FILTER_SUPPORT
int net_os_rxfilter_add_remove(struct net_device *dev, int add_remove, int num)
{
	int ret = 0;

#ifndef GAN_LITE_NAT_KEEPALIVE_FILTER
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (!dhd_master_mode)
		add_remove = !add_remove;
	DHD_ERROR(("%s: add_remove = %d, num = %d\n", __FUNCTION__, add_remove, num));
	if (!dhd || (num == DHD_UNICAST_FILTER_NUM)) {
		return 0;
	}


	if (num >= dhd->pub.pktfilter_count) {
		return -EINVAL;
	}

	ret = dhd_packet_filter_add_remove(&dhd->pub, add_remove, num);
#endif /* !GAN_LITE_NAT_KEEPALIVE_FILTER */

	return ret;
}

int dhd_os_enable_packet_filter(dhd_pub_t *dhdp, int val)

{
	int ret = 0;

	/* Packet filtering is set only if we still in early-suspend and
	 * we need either to turn it ON or turn it OFF
	 * We can always turn it OFF in case of early-suspend, but we turn it
	 * back ON only if suspend_disable_flag was not set
	*/
	if (dhdp && dhdp->up) {
		if (dhdp->in_suspend) {
			if (!val || (val && !dhdp->suspend_disable_flag))
				dhd_enable_packet_filter(val, dhdp);
		}
	}
	return ret;
}

/* function to enable/disable packet for Network device */
int net_os_enable_packet_filter(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	DHD_ERROR(("%s: val = %d\n", __FUNCTION__, val));
	return dhd_os_enable_packet_filter(&dhd->pub, val);
}
#endif /* PKT_FILTER_SUPPORT */

int
dhd_dev_init_ioctl(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret;

	if ((ret = dhd_sync_with_dongle(&dhd->pub)) < 0)
		goto done;

done:
	return ret;
}

int
dhd_dev_get_feature_set(struct net_device *dev)
{
	dhd_info_t *ptr = *(dhd_info_t **)netdev_priv(dev);
	dhd_pub_t *dhd = (&ptr->pub);
	int feature_set = 0;

	if (FW_SUPPORTED(dhd, sta))
		feature_set |= WIFI_FEATURE_INFRA;
	if (FW_SUPPORTED(dhd, dualband))
		feature_set |= WIFI_FEATURE_INFRA_5G;
	if (FW_SUPPORTED(dhd, p2p))
		feature_set |= WIFI_FEATURE_P2P;
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE)
		feature_set |= WIFI_FEATURE_SOFT_AP;
	if (FW_SUPPORTED(dhd, tdls))
		feature_set |= WIFI_FEATURE_TDLS;
	if (FW_SUPPORTED(dhd, vsdb))
		feature_set |= WIFI_FEATURE_TDLS_OFFCHANNEL;
	if (FW_SUPPORTED(dhd, nan)) {
		feature_set |= WIFI_FEATURE_NAN;
		/* NAN is essentail for d2d rtt */
		if (FW_SUPPORTED(dhd, rttd2d))
			feature_set |= WIFI_FEATURE_D2D_RTT;
	}
#ifdef RTT_SUPPORT
	if (dhd->rtt_supported) {
		feature_set |= WIFI_FEATURE_D2D_RTT;
		feature_set |= WIFI_FEATURE_D2AP_RTT;
	}
#endif /* RTT_SUPPORT */
#ifdef LINKSTAT_SUPPORT
	feature_set |= WIFI_FEATURE_LINKSTAT;
#endif /* LINKSTAT_SUPPORT */

#ifdef PNO_SUPPORT
	if (dhd_is_pno_supported(dhd)) {
		feature_set |= WIFI_FEATURE_PNO;
#ifdef GSCAN_SUPPORT
		/* terence 20171115: remove to get GTS PASS
		 * com.google.android.gts.wifi.WifiHostTest#testWifiScannerBatchTimestamp
		 */
//		feature_set |= WIFI_FEATURE_GSCAN;
//		feature_set |= WIFI_FEATURE_HAL_EPNO;
#endif /* GSCAN_SUPPORT */
	}
#endif /* PNO_SUPPORT */
#ifdef RSSI_MONITOR_SUPPORT
	if (FW_SUPPORTED(dhd, rssi_mon)) {
		feature_set |= WIFI_FEATURE_RSSI_MONITOR;
	}
#endif /* RSSI_MONITOR_SUPPORT */
#ifdef WL11U
	feature_set |= WIFI_FEATURE_HOTSPOT;
#endif /* WL11U */
#ifdef NDO_CONFIG_SUPPORT
	feature_set |= WIFI_FEATURE_CONFIG_NDO;
#endif /* NDO_CONFIG_SUPPORT */
#ifdef KEEP_ALIVE
	feature_set |= WIFI_FEATURE_MKEEP_ALIVE;
#endif /* KEEP_ALIVE */

	return feature_set;
}

int
dhd_dev_get_feature_set_matrix(struct net_device *dev, int num)
{
	int feature_set_full;
	int ret = 0;

	feature_set_full = dhd_dev_get_feature_set(dev);

	/* Common feature set for all interface */
	ret = (feature_set_full & WIFI_FEATURE_INFRA) |
		(feature_set_full & WIFI_FEATURE_INFRA_5G) |
		(feature_set_full & WIFI_FEATURE_D2D_RTT) |
		(feature_set_full & WIFI_FEATURE_D2AP_RTT) |
		(feature_set_full & WIFI_FEATURE_RSSI_MONITOR) |
		(feature_set_full & WIFI_FEATURE_EPR);

	/* Specific feature group for each interface */
	switch (num) {
	case 0:
		ret |= (feature_set_full & WIFI_FEATURE_P2P) |
			/* Not supported yet */
			/* (feature_set_full & WIFI_FEATURE_NAN) | */
			(feature_set_full & WIFI_FEATURE_TDLS) |
			(feature_set_full & WIFI_FEATURE_PNO) |
			(feature_set_full & WIFI_FEATURE_HAL_EPNO) |
			(feature_set_full & WIFI_FEATURE_BATCH_SCAN) |
			(feature_set_full & WIFI_FEATURE_GSCAN) |
			(feature_set_full & WIFI_FEATURE_HOTSPOT) |
			(feature_set_full & WIFI_FEATURE_ADDITIONAL_STA);
		break;

	case 1:
		ret |= (feature_set_full & WIFI_FEATURE_P2P);
		/* Not yet verified NAN with P2P */
		/* (feature_set_full & WIFI_FEATURE_NAN) | */
		break;

	case 2:
		ret |= (feature_set_full & WIFI_FEATURE_NAN) |
			(feature_set_full & WIFI_FEATURE_TDLS) |
			(feature_set_full & WIFI_FEATURE_TDLS_OFFCHANNEL);
		break;

	default:
		ret = WIFI_FEATURE_INVALID;
		DHD_ERROR(("%s: Out of index(%d) for get feature set\n", __FUNCTION__, num));
		break;
	}

	return ret;
}

#ifdef CUSTOM_FORCE_NODFS_FLAG
int
dhd_dev_set_nodfs(struct net_device *dev, u32 nodfs)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (nodfs)
		dhd->pub.dhd_cflags |= WLAN_PLAT_NODFS_FLAG;
	else
		dhd->pub.dhd_cflags &= ~WLAN_PLAT_NODFS_FLAG;
	dhd->pub.force_country_change = TRUE;
	return 0;
}
#endif /* CUSTOM_FORCE_NODFS_FLAG */

#ifdef NDO_CONFIG_SUPPORT
int
dhd_dev_ndo_cfg(struct net_device *dev, u8 enable)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	dhd_pub_t *dhdp = &dhd->pub;
	int ret = 0;

	if (enable) {
		/* enable ND offload feature (will be enabled in FW on suspend) */
		dhdp->ndo_enable = TRUE;

		/* Update changes of anycast address & DAD failed address */
		ret = dhd_dev_ndo_update_inet6addr(dev);
		if ((ret < 0) && (ret != BCME_NORESOURCE)) {
			DHD_ERROR(("%s: failed to update host ip addr: %d\n", __FUNCTION__, ret));
			return ret;
		}
	} else {
		/* disable ND offload feature */
		dhdp->ndo_enable = FALSE;

		/* disable ND offload in FW */
		ret = dhd_ndo_enable(dhdp, 0);
		if (ret < 0) {
			DHD_ERROR(("%s: failed to disable NDO: %d\n", __FUNCTION__, ret));
		}
	}
	return ret;
}

/* #pragma used as a WAR to fix build failure,
* ignore dropping of 'const' qualifier in 'list_entry' macro
* this pragma disables the warning only for the following function
*/
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"

static int
dhd_dev_ndo_get_valid_inet6addr_count(struct inet6_dev *inet6)
{
	struct inet6_ifaddr *ifa;
	struct ifacaddr6 *acaddr = NULL;
	int addr_count = 0;

	/* lock */
	read_lock_bh(&inet6->lock);

	/* Count valid unicast address */
	list_for_each_entry(ifa, &inet6->addr_list, if_list) {
		if ((ifa->flags & IFA_F_DADFAILED) == 0) {
			addr_count++;
		}
	}

	/* Count anycast address */
	acaddr = inet6->ac_list;
	while (acaddr) {
		addr_count++;
		acaddr = acaddr->aca_next;
	}

	/* unlock */
	read_unlock_bh(&inet6->lock);

	return addr_count;
}

int
dhd_dev_ndo_update_inet6addr(struct net_device *dev)
{
	dhd_info_t *dhd;
	dhd_pub_t *dhdp;
	struct inet6_dev *inet6;
	struct inet6_ifaddr *ifa;
	struct ifacaddr6 *acaddr = NULL;
	struct in6_addr *ipv6_addr = NULL;
	int cnt, i;
	int ret = BCME_OK;

	/*
	 * this function evaulates host ip address in struct inet6_dev
	 * unicast addr in inet6_dev->addr_list
	 * anycast addr in inet6_dev->ac_list
	 * while evaluating inet6_dev, read_lock_bh() is required to prevent
	 * access on null(freed) pointer.
	 */

	if (dev) {
		inet6 = dev->ip6_ptr;
		if (!inet6) {
			DHD_ERROR(("%s: Invalid inet6_dev\n", __FUNCTION__));
			return BCME_ERROR;
		}

		dhd = DHD_DEV_INFO(dev);
		if (!dhd) {
			DHD_ERROR(("%s: Invalid dhd_info\n", __FUNCTION__));
			return BCME_ERROR;
		}
		dhdp = &dhd->pub;

		if (dhd_net2idx(dhd, dev) != 0) {
			DHD_ERROR(("%s: Not primary interface\n", __FUNCTION__));
			return BCME_ERROR;
		}
	} else {
		DHD_ERROR(("%s: Invalid net_device\n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* Check host IP overflow */
	cnt = dhd_dev_ndo_get_valid_inet6addr_count(inet6);
	if (cnt > dhdp->ndo_max_host_ip) {
		if (!dhdp->ndo_host_ip_overflow) {
			dhdp->ndo_host_ip_overflow = TRUE;
			/* Disable ND offload in FW */
			DHD_INFO(("%s: Host IP overflow, disable NDO\n", __FUNCTION__));
			ret = dhd_ndo_enable(dhdp, 0);
		}

		return ret;
	}

	/*
	 * Allocate ipv6 addr buffer to store addresses to be added/removed.
	 * driver need to lock inet6_dev while accessing structure. but, driver
	 * cannot use ioctl while inet6_dev locked since it requires scheduling
	 * hence, copy addresses to the buffer and do ioctl after unlock.
	 */
	ipv6_addr = (struct in6_addr *)MALLOC(dhdp->osh,
		sizeof(struct in6_addr) * dhdp->ndo_max_host_ip);
	if (!ipv6_addr) {
		DHD_ERROR(("%s: failed to alloc ipv6 addr buffer\n", __FUNCTION__));
		return BCME_NOMEM;
	}

	/* Find DAD failed unicast address to be removed */
	cnt = 0;
	read_lock_bh(&inet6->lock);
	list_for_each_entry(ifa, &inet6->addr_list, if_list) {
		/* DAD failed unicast address */
		if ((ifa->flags & IFA_F_DADFAILED) &&
			(cnt < dhdp->ndo_max_host_ip)) {
				memcpy(&ipv6_addr[cnt], &ifa->addr, sizeof(struct in6_addr));
				cnt++;
		}
	}
	read_unlock_bh(&inet6->lock);

	/* Remove DAD failed unicast address */
	for (i = 0; i < cnt; i++) {
		DHD_INFO(("%s: Remove DAD failed addr\n", __FUNCTION__));
		ret = dhd_ndo_remove_ip_by_addr(dhdp, (char *)&ipv6_addr[i], 0);
		if (ret < 0) {
			goto done;
		}
	}

	/* Remove all anycast address */
	ret = dhd_ndo_remove_ip_by_type(dhdp, WL_ND_IPV6_ADDR_TYPE_ANYCAST, 0);
	if (ret < 0) {
		goto done;
	}

	/*
	 * if ND offload was disabled due to host ip overflow,
	 * attempt to add valid unicast address.
	 */
	if (dhdp->ndo_host_ip_overflow) {
		/* Find valid unicast address */
		cnt = 0;
		read_lock_bh(&inet6->lock);
		list_for_each_entry(ifa, &inet6->addr_list, if_list) {
			/* valid unicast address */
			if (!(ifa->flags & IFA_F_DADFAILED) &&
				(cnt < dhdp->ndo_max_host_ip)) {
					memcpy(&ipv6_addr[cnt], &ifa->addr,
						sizeof(struct in6_addr));
					cnt++;
			}
		}
		read_unlock_bh(&inet6->lock);

		/* Add valid unicast address */
		for (i = 0; i < cnt; i++) {
			ret = dhd_ndo_add_ip_with_type(dhdp,
				(char *)&ipv6_addr[i], WL_ND_IPV6_ADDR_TYPE_UNICAST, 0);
			if (ret < 0) {
				goto done;
			}
		}
	}

	/* Find anycast address */
	cnt = 0;
	read_lock_bh(&inet6->lock);
	acaddr = inet6->ac_list;
	while (acaddr) {
		if (cnt < dhdp->ndo_max_host_ip) {
			memcpy(&ipv6_addr[cnt], &acaddr->aca_addr, sizeof(struct in6_addr));
			cnt++;
		}
		acaddr = acaddr->aca_next;
	}
	read_unlock_bh(&inet6->lock);

	/* Add anycast address */
	for (i = 0; i < cnt; i++) {
		ret = dhd_ndo_add_ip_with_type(dhdp,
			(char *)&ipv6_addr[i], WL_ND_IPV6_ADDR_TYPE_ANYCAST, 0);
		if (ret < 0) {
			goto done;
		}
	}

	/* Now All host IP addr were added successfully */
	if (dhdp->ndo_host_ip_overflow) {
		dhdp->ndo_host_ip_overflow = FALSE;
		if (dhdp->in_suspend) {
			/* drvier is in (early) suspend state, need to enable ND offload in FW */
			DHD_INFO(("%s: enable NDO\n", __FUNCTION__));
			ret = dhd_ndo_enable(dhdp, 1);
		}
	}

done:
	if (ipv6_addr) {
		MFREE(dhdp->osh, ipv6_addr, sizeof(struct in6_addr) * dhdp->ndo_max_host_ip);
	}

	return ret;
}
#pragma GCC diagnostic pop

#endif /* NDO_CONFIG_SUPPORT */

#ifdef PNO_SUPPORT
/* Linux wrapper to call common dhd_pno_stop_for_ssid */
int
dhd_dev_pno_stop_for_ssid(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	return (dhd_pno_stop_for_ssid(&dhd->pub));
}

/* Linux wrapper to call common dhd_pno_set_for_ssid */
int
dhd_dev_pno_set_for_ssid(struct net_device *dev, wlc_ssid_ext_t* ssids_local, int nssid,
	uint16  scan_fr, int pno_repeat, int pno_freq_expo_max, uint16 *channel_list, int nchan)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	return (dhd_pno_set_for_ssid(&dhd->pub, ssids_local, nssid, scan_fr,
		pno_repeat, pno_freq_expo_max, channel_list, nchan));
}

/* Linux wrapper to call common dhd_pno_enable */
int
dhd_dev_pno_enable(struct net_device *dev, int enable)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	return (dhd_pno_enable(&dhd->pub, enable));
}

/* Linux wrapper to call common dhd_pno_set_for_hotlist */
int
dhd_dev_pno_set_for_hotlist(struct net_device *dev, wl_pfn_bssid_t *p_pfn_bssid,
	struct dhd_pno_hotlist_params *hotlist_params)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return (dhd_pno_set_for_hotlist(&dhd->pub, p_pfn_bssid, hotlist_params));
}
/* Linux wrapper to call common dhd_dev_pno_stop_for_batch */
int
dhd_dev_pno_stop_for_batch(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return (dhd_pno_stop_for_batch(&dhd->pub));
}

/* Linux wrapper to call common dhd_dev_pno_set_for_batch */
int
dhd_dev_pno_set_for_batch(struct net_device *dev, struct dhd_pno_batch_params *batch_params)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return (dhd_pno_set_for_batch(&dhd->pub, batch_params));
}

/* Linux wrapper to call common dhd_dev_pno_get_for_batch */
int
dhd_dev_pno_get_for_batch(struct net_device *dev, char *buf, int bufsize)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return (dhd_pno_get_for_batch(&dhd->pub, buf, bufsize, PNO_STATUS_NORMAL));
}
#endif /* PNO_SUPPORT */

#if defined(PNO_SUPPORT)
#ifdef GSCAN_SUPPORT
bool
dhd_dev_is_legacy_pno_enabled(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_is_legacy_pno_enabled(&dhd->pub));
}

int
dhd_dev_set_epno(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	if (!dhd) {
		return BCME_ERROR;
	}
	return dhd_pno_set_epno(&dhd->pub);
}
int
dhd_dev_flush_fw_epno(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	if (!dhd) {
		return BCME_ERROR;
	}
	return dhd_pno_flush_fw_epno(&dhd->pub);
}

/* Linux wrapper to call common dhd_pno_set_cfg_gscan */
int
dhd_dev_pno_set_cfg_gscan(struct net_device *dev, dhd_pno_gscan_cmd_cfg_t type,
 void *buf, bool flush)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_set_cfg_gscan(&dhd->pub, type, buf, flush));
}

/* Linux wrapper to call common dhd_wait_batch_results_complete */
int
dhd_dev_wait_batch_results_complete(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_wait_batch_results_complete(&dhd->pub));
}

/* Linux wrapper to call common dhd_pno_lock_batch_results */
int
dhd_dev_pno_lock_access_batch_results(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_lock_batch_results(&dhd->pub));
}
/* Linux wrapper to call common dhd_pno_unlock_batch_results */
void
dhd_dev_pno_unlock_access_batch_results(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_unlock_batch_results(&dhd->pub));
}

/* Linux wrapper to call common dhd_pno_initiate_gscan_request */
int
dhd_dev_pno_run_gscan(struct net_device *dev, bool run, bool flush)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_initiate_gscan_request(&dhd->pub, run, flush));
}

/* Linux wrapper to call common dhd_pno_enable_full_scan_result */
int
dhd_dev_pno_enable_full_scan_result(struct net_device *dev, bool real_time_flag)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_enable_full_scan_result(&dhd->pub, real_time_flag));
}

/* Linux wrapper to call common dhd_handle_hotlist_scan_evt */
void *
dhd_dev_hotlist_scan_event(struct net_device *dev,
      const void  *data, int *send_evt_bytes, hotlist_type_t type)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_handle_hotlist_scan_evt(&dhd->pub, data, send_evt_bytes, type));
}

/* Linux wrapper to call common dhd_process_full_gscan_result */
void *
dhd_dev_process_full_gscan_result(struct net_device *dev,
const void  *data, uint32 len, int *send_evt_bytes)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_process_full_gscan_result(&dhd->pub, data, len, send_evt_bytes));
}

void
dhd_dev_gscan_hotlist_cache_cleanup(struct net_device *dev, hotlist_type_t type)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	dhd_gscan_hotlist_cache_cleanup(&dhd->pub, type);

	return;
}

int
dhd_dev_gscan_batch_cache_cleanup(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_gscan_batch_cache_cleanup(&dhd->pub));
}

/* Linux wrapper to call common dhd_retreive_batch_scan_results */
int
dhd_dev_retrieve_batch_scan(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_retreive_batch_scan_results(&dhd->pub));
}

/* Linux wrapper to call common dhd_pno_process_epno_result */
void * dhd_dev_process_epno_result(struct net_device *dev,
	const void  *data, uint32 event, int *send_evt_bytes)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_process_epno_result(&dhd->pub, data, event, send_evt_bytes));
}

int
dhd_dev_set_lazy_roam_cfg(struct net_device *dev,
             wlc_roam_exp_params_t *roam_param)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	wl_roam_exp_cfg_t roam_exp_cfg;
	int err;

	if (!roam_param) {
		return BCME_BADARG;
	}

	DHD_ERROR(("a_band_boost_thr %d a_band_penalty_thr %d\n",
	      roam_param->a_band_boost_threshold, roam_param->a_band_penalty_threshold));
	DHD_ERROR(("a_band_boost_factor %d a_band_penalty_factor %d cur_bssid_boost %d\n",
	      roam_param->a_band_boost_factor, roam_param->a_band_penalty_factor,
	      roam_param->cur_bssid_boost));
	DHD_ERROR(("alert_roam_trigger_thr %d a_band_max_boost %d\n",
	      roam_param->alert_roam_trigger_threshold, roam_param->a_band_max_boost));

	memcpy(&roam_exp_cfg.params, roam_param, sizeof(*roam_param));
	roam_exp_cfg.version = ROAM_EXP_CFG_VERSION;
	roam_exp_cfg.flags = ROAM_EXP_CFG_PRESENT;
	if (dhd->pub.lazy_roam_enable) {
		roam_exp_cfg.flags |= ROAM_EXP_ENABLE_FLAG;
	}
	err = dhd_iovar(&dhd->pub, 0, "roam_exp_params",
			(char *)&roam_exp_cfg, sizeof(roam_exp_cfg), NULL, 0,
			TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : Failed to execute roam_exp_params %d\n", __FUNCTION__, err));
	}
	return err;
}

int
dhd_dev_lazy_roam_enable(struct net_device *dev, uint32 enable)
{
	int err;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	wl_roam_exp_cfg_t roam_exp_cfg;

	memset(&roam_exp_cfg, 0, sizeof(roam_exp_cfg));
	roam_exp_cfg.version = ROAM_EXP_CFG_VERSION;
	if (enable) {
		roam_exp_cfg.flags = ROAM_EXP_ENABLE_FLAG;
	}

	err = dhd_iovar(&dhd->pub, 0, "roam_exp_params",
			(char *)&roam_exp_cfg, sizeof(roam_exp_cfg), NULL, 0,
			TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : Failed to execute roam_exp_params %d\n", __FUNCTION__, err));
	} else {
		dhd->pub.lazy_roam_enable = (enable != 0);
	}
	return err;
}

int
dhd_dev_set_lazy_roam_bssid_pref(struct net_device *dev,
       wl_bssid_pref_cfg_t *bssid_pref, uint32 flush)
{
	int err;
	int len;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	bssid_pref->version = BSSID_PREF_LIST_VERSION;
	/* By default programming bssid pref flushes out old values */
	bssid_pref->flags = (flush && !bssid_pref->count) ? ROAM_EXP_CLEAR_BSSID_PREF: 0;
	len = sizeof(wl_bssid_pref_cfg_t);
	len += (bssid_pref->count - 1) * sizeof(wl_bssid_pref_list_t);
	err = dhd_iovar(&(dhd->pub), 0, "roam_exp_bssid_pref", (char *)bssid_pref,
		len, NULL, 0, TRUE);
	if (err != BCME_OK) {
		DHD_ERROR(("%s : Failed to execute roam_exp_bssid_pref %d\n", __FUNCTION__, err));
	}
	return err;
}

int
dhd_dev_set_blacklist_bssid(struct net_device *dev, maclist_t *blacklist,
    uint32 len, uint32 flush)
{
	int err;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	int macmode;

	if (blacklist) {
		err = dhd_wl_ioctl_cmd(&(dhd->pub), WLC_SET_MACLIST, (char *)blacklist,
				len, TRUE, 0);
		if (err != BCME_OK) {
			DHD_ERROR(("%s : WLC_SET_MACLIST failed %d\n", __FUNCTION__, err));
			return err;
		}
	}
	/* By default programming blacklist flushes out old values */
	macmode = (flush && !blacklist) ? WLC_MACMODE_DISABLED : WLC_MACMODE_DENY;
	err = dhd_wl_ioctl_cmd(&(dhd->pub), WLC_SET_MACMODE, (char *)&macmode,
	              sizeof(macmode), TRUE, 0);
	if (err != BCME_OK) {
		DHD_ERROR(("%s : WLC_SET_MACMODE failed %d\n", __FUNCTION__, err));
	}
	return err;
}

int
dhd_dev_set_whitelist_ssid(struct net_device *dev, wl_ssid_whitelist_t *ssid_whitelist,
    uint32 len, uint32 flush)
{
	int err;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	wl_ssid_whitelist_t whitelist_ssid_flush;

	if (!ssid_whitelist) {
		if (flush) {
			ssid_whitelist = &whitelist_ssid_flush;
			ssid_whitelist->ssid_count = 0;
		} else {
			DHD_ERROR(("%s : Nothing to do here\n", __FUNCTION__));
			return BCME_BADARG;
		}
	}
	ssid_whitelist->version = SSID_WHITELIST_VERSION;
	ssid_whitelist->flags = flush ? ROAM_EXP_CLEAR_SSID_WHITELIST : 0;
	err = dhd_iovar(&(dhd->pub), 0, "roam_exp_ssid_whitelist", (char *)ssid_whitelist,
			len, NULL, 0, TRUE);
	if (err != BCME_OK) {
		DHD_ERROR(("%s : Failed to execute roam_exp_bssid_pref %d\n", __FUNCTION__, err));
	}
	return err;
}
#endif /* GSCAN_SUPPORT */

#if defined(GSCAN_SUPPORT) || defined(DHD_GET_VALID_CHANNELS)
/* Linux wrapper to call common dhd_pno_get_gscan */
void *
dhd_dev_pno_get_gscan(struct net_device *dev, dhd_pno_gscan_cmd_cfg_t type,
	void *info, uint32 *len)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_pno_get_gscan(&dhd->pub, type, info, len));
}
#endif /* GSCAN_SUPPORT || DHD_GET_VALID_CHANNELS */
#endif 

#ifdef  RSSI_MONITOR_SUPPORT
int
dhd_dev_set_rssi_monitor_cfg(struct net_device *dev, int start,
             int8 max_rssi, int8 min_rssi)
{
	int err;
	wl_rssi_monitor_cfg_t rssi_monitor;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	rssi_monitor.version = RSSI_MONITOR_VERSION;
	rssi_monitor.max_rssi = max_rssi;
	rssi_monitor.min_rssi = min_rssi;
	rssi_monitor.flags = start ? 0: RSSI_MONITOR_STOP;
	err = dhd_iovar(&(dhd->pub), 0, "rssi_monitor", (char *)&rssi_monitor,
		sizeof(rssi_monitor), NULL, 0, TRUE);
	if (err < 0 && err != BCME_UNSUPPORTED) {
		DHD_ERROR(("%s : Failed to execute rssi_monitor %d\n", __FUNCTION__, err));
	}
	return err;
}
#endif /* RSSI_MONITOR_SUPPORT */

#ifdef DHDTCPACK_SUPPRESS
int dhd_dev_set_tcpack_sup_mode_cfg(struct net_device *dev, uint8 enable)
{
	int err;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	err = dhd_tcpack_suppress_set(&(dhd->pub), enable);
	if (err != BCME_OK) {
		DHD_ERROR(("%s : Failed to execute rssi_monitor %d\n", __FUNCTION__, err));
	}
	return err;
}
#endif /* DHDTCPACK_SUPPRESS */

int
dhd_dev_cfg_rand_mac_oui(struct net_device *dev, uint8 *oui)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	dhd_pub_t *dhdp = &dhd->pub;

	if (!dhdp || !oui) {
		DHD_ERROR(("NULL POINTER : %s\n",
			__FUNCTION__));
		return BCME_ERROR;
	}
	if (ETHER_ISMULTI(oui)) {
		DHD_ERROR(("Expected unicast OUI\n"));
		return BCME_ERROR;
	} else {
		uint8 *rand_mac_oui = dhdp->rand_mac_oui;
		memcpy(rand_mac_oui, oui, DOT11_OUI_LEN);
		DHD_ERROR(("Random MAC OUI to be used - %02x:%02x:%02x\n", rand_mac_oui[0],
		    rand_mac_oui[1], rand_mac_oui[2]));
	}
	return BCME_OK;
}

int
dhd_set_rand_mac_oui(dhd_pub_t *dhd)
{
	int err;
	wl_pfn_macaddr_cfg_t wl_cfg;
	uint8 *rand_mac_oui = dhd->rand_mac_oui;

	memset(&wl_cfg.macaddr, 0, ETHER_ADDR_LEN);
	memcpy(&wl_cfg.macaddr, rand_mac_oui, DOT11_OUI_LEN);
	wl_cfg.version = WL_PFN_MACADDR_CFG_VER;
	if (ETHER_ISNULLADDR(&wl_cfg.macaddr)) {
		wl_cfg.flags = 0;
	} else {
		wl_cfg.flags = (WL_PFN_MAC_OUI_ONLY_MASK | WL_PFN_SET_MAC_UNASSOC_MASK);
	}

	DHD_ERROR(("Setting rand mac oui to FW - %02x:%02x:%02x\n", rand_mac_oui[0],
		rand_mac_oui[1], rand_mac_oui[2]));

	err = dhd_iovar(dhd, 0, "pfn_macaddr", (char *)&wl_cfg, sizeof(wl_cfg), NULL, 0, TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn_macaddr %d\n", __FUNCTION__, err));
	}
	return err;
}

#ifdef RTT_SUPPORT
#ifdef WL_CFG80211
/* Linux wrapper to call common dhd_pno_set_cfg_gscan */
int
dhd_dev_rtt_set_cfg(struct net_device *dev, void *buf)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_rtt_set_cfg(&dhd->pub, buf));
}

int
dhd_dev_rtt_cancel_cfg(struct net_device *dev, struct ether_addr *mac_list, int mac_cnt)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_rtt_stop(&dhd->pub, mac_list, mac_cnt));
}

int
dhd_dev_rtt_register_noti_callback(struct net_device *dev, void *ctx, dhd_rtt_compl_noti_fn noti_fn)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_rtt_register_noti_callback(&dhd->pub, ctx, noti_fn));
}

int
dhd_dev_rtt_unregister_noti_callback(struct net_device *dev, dhd_rtt_compl_noti_fn noti_fn)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_rtt_unregister_noti_callback(&dhd->pub, noti_fn));
}

int
dhd_dev_rtt_capability(struct net_device *dev, rtt_capabilities_t *capa)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	return (dhd_rtt_capability(&dhd->pub, capa));
}

int
dhd_dev_rtt_avail_channel(struct net_device *dev, wifi_channel_info *channel_info)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	return (dhd_rtt_avail_channel(&dhd->pub, channel_info));
}

int
dhd_dev_rtt_enable_responder(struct net_device *dev, wifi_channel_info *channel_info)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	return (dhd_rtt_enable_responder(&dhd->pub, channel_info));
}

int dhd_dev_rtt_cancel_responder(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	return (dhd_rtt_cancel_responder(&dhd->pub));
}
#endif /* WL_CFG80211 */
#endif /* RTT_SUPPORT */

#ifdef KEEP_ALIVE
#define KA_TEMP_BUF_SIZE 512
#define KA_FRAME_SIZE 300

int
dhd_dev_start_mkeep_alive(dhd_pub_t *dhd_pub, uint8 mkeep_alive_id, uint8 *ip_pkt,
	uint16 ip_pkt_len, uint8* src_mac, uint8* dst_mac, uint32 period_msec)
{
	const int		ETHERTYPE_LEN = 2;
	char			*pbuf = NULL;
	const char		*str;
	wl_mkeep_alive_pkt_t	mkeep_alive_pkt;
	wl_mkeep_alive_pkt_t	*mkeep_alive_pktp = NULL;
	int			buf_len = 0;
	int			str_len = 0;
	int			res = BCME_ERROR;
	int			len_bytes = 0;
	int			i = 0;

	/* ether frame to have both max IP pkt (256 bytes) and ether header */
	char			*pmac_frame = NULL;
	char			*pmac_frame_begin = NULL;

	/*
	 * The mkeep_alive packet is for STA interface only; if the bss is configured as AP,
	 * dongle shall reject a mkeep_alive request.
	 */
	if (!dhd_support_sta_mode(dhd_pub))
		return res;

	DHD_TRACE(("%s execution\n", __FUNCTION__));

	if ((pbuf = kzalloc(KA_TEMP_BUF_SIZE, GFP_KERNEL)) == NULL) {
		DHD_ERROR(("failed to allocate buf with size %d\n", KA_TEMP_BUF_SIZE));
		res = BCME_NOMEM;
		return res;
	}

	if ((pmac_frame = kzalloc(KA_FRAME_SIZE, GFP_KERNEL)) == NULL) {
		DHD_ERROR(("failed to allocate mac_frame with size %d\n", KA_FRAME_SIZE));
		res = BCME_NOMEM;
		goto exit;
	}
	pmac_frame_begin = pmac_frame;

	/*
	 * Get current mkeep-alive status.
	 */
	res = dhd_iovar(dhd_pub, 0, "mkeep_alive", &mkeep_alive_id, sizeof(mkeep_alive_id), pbuf,
			KA_TEMP_BUF_SIZE, FALSE);
	if (res < 0) {
		DHD_ERROR(("%s: Get mkeep_alive failed (error=%d)\n", __FUNCTION__, res));
		goto exit;
	} else {
		/* Check available ID whether it is occupied */
		mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *) pbuf;
		if (dtoh32(mkeep_alive_pktp->period_msec != 0)) {
			DHD_ERROR(("%s: Get mkeep_alive failed, ID %u is in use.\n",
				__FUNCTION__, mkeep_alive_id));

			/* Current occupied ID info */
			DHD_ERROR(("%s: mkeep_alive\n", __FUNCTION__));
			DHD_ERROR(("   Id    : %d\n"
				"   Period: %d msec\n"
				"   Length: %d\n"
				"   Packet: 0x",
				mkeep_alive_pktp->keep_alive_id,
				dtoh32(mkeep_alive_pktp->period_msec),
				dtoh16(mkeep_alive_pktp->len_bytes)));

			for (i = 0; i < mkeep_alive_pktp->len_bytes; i++) {
				DHD_ERROR(("%02x", mkeep_alive_pktp->data[i]));
			}
			DHD_ERROR(("\n"));

			res = BCME_NOTFOUND;
			goto exit;
		}
	}

	/* Request the specified ID */
	memset(&mkeep_alive_pkt, 0, sizeof(wl_mkeep_alive_pkt_t));
	memset(pbuf, 0, KA_TEMP_BUF_SIZE);
	str = "mkeep_alive";
	str_len = strlen(str);
	strncpy(pbuf, str, str_len);
	pbuf[str_len] = '\0';

	mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *) (pbuf + str_len + 1);
	mkeep_alive_pkt.period_msec = htod32(period_msec);
	buf_len = str_len + 1;
	mkeep_alive_pkt.version = htod16(WL_MKEEP_ALIVE_VERSION);
	mkeep_alive_pkt.length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);

	/* ID assigned */
	mkeep_alive_pkt.keep_alive_id = mkeep_alive_id;

	buf_len += WL_MKEEP_ALIVE_FIXED_LEN;

	/*
	 * Build up Ethernet Frame
	 */

	/* Mapping dest mac addr */
	memcpy(pmac_frame, dst_mac, ETHER_ADDR_LEN);
	pmac_frame += ETHER_ADDR_LEN;

	/* Mapping src mac addr */
	memcpy(pmac_frame, src_mac, ETHER_ADDR_LEN);
	pmac_frame += ETHER_ADDR_LEN;

	/* Mapping Ethernet type (ETHERTYPE_IP: 0x0800) */
	*(pmac_frame++) = 0x08;
	*(pmac_frame++) = 0x00;

	/* Mapping IP pkt */
	memcpy(pmac_frame, ip_pkt, ip_pkt_len);
	pmac_frame += ip_pkt_len;

	/*
	 * Length of ether frame (assume to be all hexa bytes)
	 *     = src mac + dst mac + ether type + ip pkt len
	 */
	len_bytes = ETHER_ADDR_LEN*2 + ETHERTYPE_LEN + ip_pkt_len;
	memcpy(mkeep_alive_pktp->data, pmac_frame_begin, len_bytes);
	buf_len += len_bytes;
	mkeep_alive_pkt.len_bytes = htod16(len_bytes);

	/*
	 * Keep-alive attributes are set in local variable (mkeep_alive_pkt), and
	 * then memcpy'ed into buffer (mkeep_alive_pktp) since there is no
	 * guarantee that the buffer is properly aligned.
	 */
	memcpy((char *)mkeep_alive_pktp, &mkeep_alive_pkt, WL_MKEEP_ALIVE_FIXED_LEN);

	res = dhd_wl_ioctl_cmd(dhd_pub, WLC_SET_VAR, pbuf, buf_len, TRUE, 0);
exit:
	kfree(pmac_frame_begin);
	kfree(pbuf);
	return res;
}

int
dhd_dev_stop_mkeep_alive(dhd_pub_t *dhd_pub, uint8 mkeep_alive_id)
{
	char			*pbuf;
	wl_mkeep_alive_pkt_t	mkeep_alive_pkt;
	wl_mkeep_alive_pkt_t	*mkeep_alive_pktp;
	int			res = BCME_ERROR;
	int			i;

	/*
	 * The mkeep_alive packet is for STA interface only; if the bss is configured as AP,
	 * dongle shall reject a mkeep_alive request.
	 */
	if (!dhd_support_sta_mode(dhd_pub))
		return res;

	DHD_TRACE(("%s execution\n", __FUNCTION__));

	/*
	 * Get current mkeep-alive status. Skip ID 0 which is being used for NULL pkt.
	 */
	if ((pbuf = kmalloc(KA_TEMP_BUF_SIZE, GFP_KERNEL)) == NULL) {
		DHD_ERROR(("failed to allocate buf with size %d\n", KA_TEMP_BUF_SIZE));
		return res;
	}

	res = dhd_iovar(dhd_pub, 0, "mkeep_alive", &mkeep_alive_id,
			sizeof(mkeep_alive_id), pbuf, KA_TEMP_BUF_SIZE, FALSE);
	if (res < 0) {
		DHD_ERROR(("%s: Get mkeep_alive failed (error=%d)\n", __FUNCTION__, res));
		goto exit;
	} else {
		/* Check occupied ID */
		mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *) pbuf;
		DHD_INFO(("%s: mkeep_alive\n", __FUNCTION__));
		DHD_INFO(("   Id    : %d\n"
			"   Period: %d msec\n"
			"   Length: %d\n"
			"   Packet: 0x",
			mkeep_alive_pktp->keep_alive_id,
			dtoh32(mkeep_alive_pktp->period_msec),
			dtoh16(mkeep_alive_pktp->len_bytes)));

		for (i = 0; i < mkeep_alive_pktp->len_bytes; i++) {
			DHD_INFO(("%02x", mkeep_alive_pktp->data[i]));
		}
		DHD_INFO(("\n"));
	}

	/* Make it stop if available */
	if (dtoh32(mkeep_alive_pktp->period_msec != 0)) {
		DHD_INFO(("stop mkeep_alive on ID %d\n", mkeep_alive_id));
		memset(&mkeep_alive_pkt, 0, sizeof(wl_mkeep_alive_pkt_t));

		mkeep_alive_pkt.period_msec = 0;
		mkeep_alive_pkt.version = htod16(WL_MKEEP_ALIVE_VERSION);
		mkeep_alive_pkt.length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);
		mkeep_alive_pkt.keep_alive_id = mkeep_alive_id;

		res = dhd_iovar(dhd_pub, 0, "mkeep_alive",
				(char *)&mkeep_alive_pkt,
				WL_MKEEP_ALIVE_FIXED_LEN, NULL, 0, TRUE);
	} else {
		DHD_ERROR(("%s: ID %u does not exist.\n", __FUNCTION__, mkeep_alive_id));
		res = BCME_NOTFOUND;
	}
exit:
	kfree(pbuf);
	return res;
}
#endif /* KEEP_ALIVE */

#if defined(PKT_FILTER_SUPPORT) && defined(APF)
static void _dhd_apf_lock_local(dhd_info_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	if (dhd) {
		mutex_lock(&dhd->dhd_apf_mutex);
	}
#endif
}

static void _dhd_apf_unlock_local(dhd_info_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	if (dhd) {
		mutex_unlock(&dhd->dhd_apf_mutex);
	}
#endif
}

static int
__dhd_apf_add_filter(struct net_device *ndev, uint32 filter_id,
	u8* program, uint32 program_len)
{
	dhd_info_t *dhd = DHD_DEV_INFO(ndev);
	dhd_pub_t *dhdp = &dhd->pub;
	wl_pkt_filter_t * pkt_filterp;
	wl_apf_program_t *apf_program;
	char *buf;
	u32 cmd_len, buf_len;
	int ifidx, ret;
	gfp_t kflags;
	char cmd[] = "pkt_filter_add";

	ifidx = dhd_net2idx(dhd, ndev);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	cmd_len = sizeof(cmd);

	/* Check if the program_len is more than the expected len
	 * and if the program is NULL return from here.
	 */
	if ((program_len > WL_APF_PROGRAM_MAX_SIZE) || (program == NULL)) {
		DHD_ERROR(("%s Invalid program_len: %d, program: %pK\n",
				__FUNCTION__, program_len, program));
		return -EINVAL;
	}
	buf_len = cmd_len + WL_PKT_FILTER_FIXED_LEN +
		WL_APF_PROGRAM_FIXED_LEN + program_len;

	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	buf = kzalloc(buf_len, kflags);
	if (unlikely(!buf)) {
		DHD_ERROR(("%s: MALLOC failure, %d bytes\n", __FUNCTION__, buf_len));
		return -ENOMEM;
	}

	memcpy(buf, cmd, cmd_len);

	pkt_filterp = (wl_pkt_filter_t *) (buf + cmd_len);
	pkt_filterp->id = htod32(filter_id);
	pkt_filterp->negate_match = htod32(FALSE);
	pkt_filterp->type = htod32(WL_PKT_FILTER_TYPE_APF_MATCH);

	apf_program = &pkt_filterp->u.apf_program;
	apf_program->version = htod16(WL_APF_INTERNAL_VERSION);
	apf_program->instr_len = htod16(program_len);
	memcpy(apf_program->instrs, program, program_len);

	ret = dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, buf, buf_len, TRUE, ifidx);
	if (unlikely(ret)) {
		DHD_ERROR(("%s: failed to add APF filter, id=%d, ret=%d\n",
			__FUNCTION__, filter_id, ret));
	}

	if (buf) {
		kfree(buf);
	}
	return ret;
}

static int
__dhd_apf_config_filter(struct net_device *ndev, uint32 filter_id,
	uint32 mode, uint32 enable)
{
	dhd_info_t *dhd = DHD_DEV_INFO(ndev);
	dhd_pub_t *dhdp = &dhd->pub;
	wl_pkt_filter_enable_t * pkt_filterp;
	char *buf;
	u32 cmd_len, buf_len;
	int ifidx, ret;
	gfp_t kflags;
	char cmd[] = "pkt_filter_enable";

	ifidx = dhd_net2idx(dhd, ndev);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	cmd_len = sizeof(cmd);
	buf_len = cmd_len + sizeof(*pkt_filterp);

	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	buf = kzalloc(buf_len, kflags);
	if (unlikely(!buf)) {
		DHD_ERROR(("%s: MALLOC failure, %d bytes\n", __FUNCTION__, buf_len));
		return -ENOMEM;
	}

	memcpy(buf, cmd, cmd_len);

	pkt_filterp = (wl_pkt_filter_enable_t *) (buf + cmd_len);
	pkt_filterp->id = htod32(filter_id);
	pkt_filterp->enable = htod32(enable);

	ret = dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, buf, buf_len, TRUE, ifidx);
	if (unlikely(ret)) {
		DHD_ERROR(("%s: failed to enable APF filter, id=%d, ret=%d\n",
			__FUNCTION__, filter_id, ret));
		goto exit;
	}

	ret = dhd_wl_ioctl_set_intiovar(dhdp, "pkt_filter_mode", dhd_master_mode,
		WLC_SET_VAR, TRUE, ifidx);
	if (unlikely(ret)) {
		DHD_ERROR(("%s: failed to set APF filter mode, id=%d, ret=%d\n",
			__FUNCTION__, filter_id, ret));
	}

exit:
	if (buf) {
		kfree(buf);
	}
	return ret;
}

static int
__dhd_apf_delete_filter(struct net_device *ndev, uint32 filter_id)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(ndev);
	dhd_pub_t *dhdp = &dhd->pub;
	int ifidx, ret;

	ifidx = dhd_net2idx(dhd, ndev);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	ret = dhd_wl_ioctl_set_intiovar(dhdp, "pkt_filter_delete",
		htod32(filter_id), WLC_SET_VAR, TRUE, ifidx);
	if (unlikely(ret)) {
		DHD_ERROR(("%s: failed to delete APF filter, id=%d, ret=%d\n",
			__FUNCTION__, filter_id, ret));
	}

	return ret;
}

void dhd_apf_lock(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	_dhd_apf_lock_local(dhd);
}

void dhd_apf_unlock(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	_dhd_apf_unlock_local(dhd);
}

int
dhd_dev_apf_get_version(struct net_device *ndev, uint32 *version)
{
	dhd_info_t *dhd = DHD_DEV_INFO(ndev);
	dhd_pub_t *dhdp = &dhd->pub;
	int ifidx, ret;

	if (!FW_SUPPORTED(dhdp, apf)) {
		DHD_ERROR(("%s: firmware doesn't support APF\n", __FUNCTION__));

		/*
		 * Notify Android framework that APF is not supported by setting
		 * version as zero.
		 */
		*version = 0;
		return BCME_OK;
	}

	ifidx = dhd_net2idx(dhd, ndev);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	ret = dhd_wl_ioctl_get_intiovar(dhdp, "apf_ver", version,
		WLC_GET_VAR, FALSE, ifidx);
	if (unlikely(ret)) {
		DHD_ERROR(("%s: failed to get APF version, ret=%d\n",
			__FUNCTION__, ret));
	}

	return ret;
}

int
dhd_dev_apf_get_max_len(struct net_device *ndev, uint32 *max_len)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(ndev);
	dhd_pub_t *dhdp = &dhd->pub;
	int ifidx, ret;

	if (!FW_SUPPORTED(dhdp, apf)) {
		DHD_ERROR(("%s: firmware doesn't support APF\n", __FUNCTION__));
		*max_len = 0;
		return BCME_OK;
	}

	ifidx = dhd_net2idx(dhd, ndev);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	ret = dhd_wl_ioctl_get_intiovar(dhdp, "apf_size_limit", max_len,
		WLC_GET_VAR, FALSE, ifidx);
	if (unlikely(ret)) {
		DHD_ERROR(("%s: failed to get APF size limit, ret=%d\n",
			__FUNCTION__, ret));
	}

	return ret;
}

int
dhd_dev_apf_add_filter(struct net_device *ndev, u8* program,
	uint32 program_len)
{
	dhd_info_t *dhd = DHD_DEV_INFO(ndev);
	dhd_pub_t *dhdp = &dhd->pub;
	int ret;

	DHD_APF_LOCK(ndev);

	/* delete, if filter already exists */
	if (dhdp->apf_set) {
		ret = __dhd_apf_delete_filter(ndev, PKT_FILTER_APF_ID);
		if (unlikely(ret)) {
			goto exit;
		}
		dhdp->apf_set = FALSE;
	}

	ret = __dhd_apf_add_filter(ndev, PKT_FILTER_APF_ID, program, program_len);
	if (ret) {
		goto exit;
	}
	dhdp->apf_set = TRUE;

	if (dhdp->in_suspend && dhdp->apf_set && !(dhdp->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		/* Driver is still in (early) suspend state, enable APF filter back */
		ret = __dhd_apf_config_filter(ndev, PKT_FILTER_APF_ID,
			PKT_FILTER_MODE_FORWARD_ON_MATCH, TRUE);
	}
exit:
	DHD_APF_UNLOCK(ndev);

	return ret;
}

int
dhd_dev_apf_enable_filter(struct net_device *ndev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(ndev);
	dhd_pub_t *dhdp = &dhd->pub;
	int ret = 0;

	DHD_APF_LOCK(ndev);

	if (dhdp->apf_set && !(dhdp->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		ret = __dhd_apf_config_filter(ndev, PKT_FILTER_APF_ID,
			PKT_FILTER_MODE_FORWARD_ON_MATCH, TRUE);
	}

	DHD_APF_UNLOCK(ndev);

	return ret;
}

int
dhd_dev_apf_disable_filter(struct net_device *ndev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(ndev);
	dhd_pub_t *dhdp = &dhd->pub;
	int ret = 0;

	DHD_APF_LOCK(ndev);

	if (dhdp->apf_set) {
		ret = __dhd_apf_config_filter(ndev, PKT_FILTER_APF_ID,
			PKT_FILTER_MODE_FORWARD_ON_MATCH, FALSE);
	}

	DHD_APF_UNLOCK(ndev);

	return ret;
}

int
dhd_dev_apf_delete_filter(struct net_device *ndev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(ndev);
	dhd_pub_t *dhdp = &dhd->pub;
	int ret = 0;

	DHD_APF_LOCK(ndev);

	if (dhdp->apf_set) {
		ret = __dhd_apf_delete_filter(ndev, PKT_FILTER_APF_ID);
		if (!ret) {
			dhdp->apf_set = FALSE;
		}
	}

	DHD_APF_UNLOCK(ndev);

	return ret;
}
#endif /* PKT_FILTER_SUPPORT && APF */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
static void dhd_hang_process(void *dhd_info, void *event_info, u8 event)
{
	dhd_info_t *dhd;
	struct net_device *dev;

	dhd = (dhd_info_t *)dhd_info;
	if (!dhd || !dhd->iflist[0])
		return;
	dev = dhd->iflist[0]->net;

	if (dev) {
		/*
		 * For HW2, dev_close need to be done to recover
		 * from upper layer after hang. For Interposer skip
		 * dev_close so that dhd iovars can be used to take
		 * socramdump after crash, also skip for HW4 as
		 * handling of hang event is different
		 */
#if !defined(CUSTOMER_HW2_INTERPOSER)
		rtnl_lock();
		dev_close(dev);
		rtnl_unlock();
#endif 
#if defined(WL_WIRELESS_EXT)
		wl_iw_send_priv_event(dev, "HANG");
#endif
#if defined(WL_CFG80211)
		wl_cfg80211_hang(dev, WLAN_REASON_UNSPECIFIED);
#endif
	}
}

#ifdef EXYNOS_PCIE_LINKDOWN_RECOVERY
extern dhd_pub_t *link_recovery;
void dhd_host_recover_link(void)
{
	DHD_ERROR(("****** %s ******\n", __FUNCTION__));
	link_recovery->hang_reason = HANG_REASON_PCIE_LINK_DOWN;
	dhd_bus_set_linkdown(link_recovery, TRUE);
	dhd_os_send_hang_message(link_recovery);
}
EXPORT_SYMBOL(dhd_host_recover_link);
#endif /* EXYNOS_PCIE_LINKDOWN_RECOVERY */

int dhd_os_send_hang_message(dhd_pub_t *dhdp)
{
	int ret = 0;
	if (dhdp) {
#if defined(DHD_HANG_SEND_UP_TEST)
		if (dhdp->req_hang_type) {
			DHD_ERROR(("%s, Clear HANG test request 0x%x\n",
				__FUNCTION__, dhdp->req_hang_type));
			dhdp->req_hang_type = 0;
		}
#endif /* DHD_HANG_SEND_UP_TEST */

		if (!dhdp->hang_was_sent) {
#if defined(CONFIG_BCM_DETECT_CONSECUTIVE_HANG)
			dhdp->hang_counts++;
			if (dhdp->hang_counts >= MAX_CONSECUTIVE_HANG_COUNTS) {
				DHD_ERROR(("%s, Consecutive hang from Dongle :%u\n",
					__func__, dhdp->hang_counts));
				BUG_ON(1);
			}
#endif /* CONFIG_BCM_DETECT_CONSECUTIVE_HANG */
#ifdef DHD_DEBUG_UART
			/* If PCIe lane has broken, execute the debug uart application
			 * to gether a ramdump data from dongle via uart
			 */
			if (!dhdp->info->duart_execute) {
				dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq,
					(void *)dhdp, DHD_WQ_WORK_DEBUG_UART_DUMP,
					dhd_debug_uart_exec_rd, DHD_WQ_WORK_PRIORITY_HIGH);
			}
#endif	/* DHD_DEBUG_UART */
			dhdp->hang_was_sent = 1;
#ifdef BT_OVER_SDIO
			dhdp->is_bt_recovery_required = TRUE;
#endif
			dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq, (void *)dhdp,
				DHD_WQ_WORK_HANG_MSG, dhd_hang_process, DHD_WQ_WORK_PRIORITY_HIGH);
			DHD_ERROR(("%s: Event HANG send up due to  re=%d te=%d s=%d\n", __FUNCTION__,
				dhdp->rxcnt_timeout, dhdp->txcnt_timeout, dhdp->busstate));
		}
	}
	return ret;
}

int net_os_send_hang_message(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd) {
		/* Report FW problem when enabled */
		if (dhd->pub.hang_report) {
#ifdef BT_OVER_SDIO
			if (netif_running(dev)) {
#endif /* BT_OVER_SDIO */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
				ret = dhd_os_send_hang_message(&dhd->pub);
#else
				ret = wl_cfg80211_hang(dev, WLAN_REASON_UNSPECIFIED);
#endif
#ifdef BT_OVER_SDIO
			}
			DHD_ERROR(("%s: HANG -> Reset BT\n", __FUNCTION__));
			bcmsdh_btsdio_process_dhd_hang_notification(!netif_running(dev));
#endif /* BT_OVER_SDIO */
		} else {
			DHD_ERROR(("%s: FW HANG ignored (for testing purpose) and not sent up\n",
				__FUNCTION__));
		}
	}
	return ret;
}

int net_os_send_hang_message_reason(struct net_device *dev, const char *string_num)
{
	dhd_info_t *dhd = NULL;
	dhd_pub_t *dhdp = NULL;
	int reason;

	dhd = DHD_DEV_INFO(dev);
	if (dhd) {
		dhdp = &dhd->pub;
	}

	if (!dhd || !dhdp) {
		return 0;
	}

	reason = bcm_strtoul(string_num, NULL, 0);
	DHD_INFO(("%s: Enter, reason=0x%x\n", __FUNCTION__, reason));

	if ((reason <= HANG_REASON_MASK) || (reason >= HANG_REASON_MAX)) {
		reason = 0;
	}

	dhdp->hang_reason = reason;

	return net_os_send_hang_message(dev);
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) && OEM_ANDROID */


int dhd_net_wifi_platform_set_power(struct net_device *dev, bool on, unsigned long delay_msec)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return wifi_platform_set_power(dhd->adapter, on, delay_msec);
}

bool dhd_force_country_change(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (dhd && dhd->pub.up)
		return dhd->pub.force_country_change;
	return FALSE;
}

void dhd_get_customized_country_code(struct net_device *dev, char *country_iso_code,
	wl_country_t *cspec)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
#if defined(DHD_BLOB_EXISTENCE_CHECK)
	if (!dhd->pub.is_blob)
#endif /* DHD_BLOB_EXISTENCE_CHECK */
	{
#if defined(CUSTOM_COUNTRY_CODE)
		get_customized_country_code(dhd->adapter, country_iso_code, cspec,
			dhd->pub.dhd_cflags);
#else
		get_customized_country_code(dhd->adapter, country_iso_code, cspec);
#endif /* CUSTOM_COUNTRY_CODE */
	}

	BCM_REFERENCE(dhd);
}

void dhd_bus_country_set(struct net_device *dev, wl_country_t *cspec, bool notify)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#endif

	if (dhd && dhd->pub.up) {
		memcpy(&dhd->pub.dhd_cspec, cspec, sizeof(wl_country_t));
#ifdef WL_CFG80211
		wl_update_wiphybands(cfg, notify);
#endif
	}
}

void dhd_bus_band_set(struct net_device *dev, uint band)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#endif
	if (dhd && dhd->pub.up) {
#ifdef WL_CFG80211
		wl_update_wiphybands(cfg, true);
#endif
	}
}

int dhd_net_set_fw_path(struct net_device *dev, char *fw)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (!fw || fw[0] == '\0')
		return -EINVAL;

	strncpy(dhd->fw_path, fw, sizeof(dhd->fw_path) - 1);
	dhd->fw_path[sizeof(dhd->fw_path)-1] = '\0';

#if defined(SOFTAP)
	if (strstr(fw, "apsta") != NULL) {
		DHD_INFO(("GOT APSTA FIRMWARE\n"));
		ap_fw_loaded = TRUE;
	} else {
		DHD_INFO(("GOT STA FIRMWARE\n"));
		ap_fw_loaded = FALSE;
	}
#endif 
	return 0;
}

void dhd_net_if_lock(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	dhd_net_if_lock_local(dhd);
}

void dhd_net_if_unlock(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	dhd_net_if_unlock_local(dhd);
}

static void dhd_net_if_lock_local(dhd_info_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	if (dhd)
		mutex_lock(&dhd->dhd_net_if_mutex);
#endif
}

static void dhd_net_if_unlock_local(dhd_info_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	if (dhd)
		mutex_unlock(&dhd->dhd_net_if_mutex);
#endif
}

static void dhd_suspend_lock(dhd_pub_t *pub)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	if (dhd)
		mutex_lock(&dhd->dhd_suspend_mutex);
#endif
}

static void dhd_suspend_unlock(dhd_pub_t *pub)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	if (dhd)
		mutex_unlock(&dhd->dhd_suspend_mutex);
#endif
}

unsigned long dhd_os_general_spin_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags = 0;

	if (dhd)
		spin_lock_irqsave(&dhd->dhd_lock, flags);

	return flags;
}

void dhd_os_general_spin_unlock(dhd_pub_t *pub, unsigned long flags)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd)
		spin_unlock_irqrestore(&dhd->dhd_lock, flags);
}

/* Linux specific multipurpose spinlock API */
void *
dhd_os_spin_lock_init(osl_t *osh)
{
	/* Adding 4 bytes since the sizeof(spinlock_t) could be 0 */
	/* if CONFIG_SMP and CONFIG_DEBUG_SPINLOCK are not defined */
	/* and this results in kernel asserts in internal builds */
	spinlock_t * lock = MALLOC(osh, sizeof(spinlock_t) + 4);
	if (lock)
		spin_lock_init(lock);
	return ((void *)lock);
}
void
dhd_os_spin_lock_deinit(osl_t *osh, void *lock)
{
	if (lock)
		MFREE(osh, lock, sizeof(spinlock_t) + 4);
}
unsigned long
dhd_os_spin_lock(void *lock)
{
	unsigned long flags = 0;

	if (lock)
		spin_lock_irqsave((spinlock_t *)lock, flags);

	return flags;
}
void
dhd_os_spin_unlock(void *lock, unsigned long flags)
{
	if (lock)
		spin_unlock_irqrestore((spinlock_t *)lock, flags);
}

static int
dhd_get_pend_8021x_cnt(dhd_info_t *dhd)
{
	return (atomic_read(&dhd->pend_8021x_cnt));
}

#define MAX_WAIT_FOR_8021X_TX	100

int
dhd_wait_pend8021x(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int timeout = msecs_to_jiffies(10);
	int ntimes = MAX_WAIT_FOR_8021X_TX;
	int pend = dhd_get_pend_8021x_cnt(dhd);

	while (ntimes && pend) {
		if (pend) {
			set_current_state(TASK_INTERRUPTIBLE);
			DHD_PERIM_UNLOCK(&dhd->pub);
			schedule_timeout(timeout);
			DHD_PERIM_LOCK(&dhd->pub);
			set_current_state(TASK_RUNNING);
			ntimes--;
		}
		pend = dhd_get_pend_8021x_cnt(dhd);
	}
	if (ntimes == 0)
	{
		atomic_set(&dhd->pend_8021x_cnt, 0);
		DHD_ERROR(("%s: TIMEOUT\n", __FUNCTION__));
	}
	return pend;
}

#if defined(DHD_DEBUG)
int write_file(const char * file_name, uint32 flags, uint8 *buf, int size)
{
	int ret = 0;
	struct file *fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
	/* change to KERNEL_DS address limit */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/* open file to write */
	fp = filp_open(file_name, flags, 0664);
	if (IS_ERR(fp)) {
		DHD_ERROR(("open file error, err = %ld\n", PTR_ERR(fp)));
		ret = -1;
		goto exit;
	}

	/* Write buf to file */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	ret = kernel_write(fp, buf, size, &pos);
#else
	ret = vfs_write(fp, buf, size, &pos);
#endif
	if (ret < 0) {
		DHD_ERROR(("write file error, err = %d\n", ret));
		goto exit;
	}

	/* Sync file from filesystem to physical media */
	ret = vfs_fsync(fp, 0);
	if (ret < 0) {
		DHD_ERROR(("sync file error, error = %d\n", ret));
		goto exit;
	}
	ret = BCME_OK;

exit:
	/* close file before return */
	if (!IS_ERR(fp))
		filp_close(fp, current->files);

	/* restore previous address limit */
	set_fs(old_fs);

	return ret;
}
#endif 

#ifdef DHD_DEBUG
static void
dhd_convert_memdump_type_to_str(uint32 type, char *buf)
{
	char *type_str = NULL;

	switch (type) {
		case DUMP_TYPE_RESUMED_ON_TIMEOUT:
			type_str = "resumed_on_timeout";
			break;
		case DUMP_TYPE_D3_ACK_TIMEOUT:
			type_str = "D3_ACK_timeout";
			break;
		case DUMP_TYPE_DONGLE_TRAP:
			type_str = "Dongle_Trap";
			break;
		case DUMP_TYPE_MEMORY_CORRUPTION:
			type_str = "Memory_Corruption";
			break;
		case DUMP_TYPE_PKTID_AUDIT_FAILURE:
			type_str = "PKTID_AUDIT_Fail";
			break;
		case DUMP_TYPE_PKTID_INVALID:
			type_str = "PKTID_INVALID";
			break;
		case DUMP_TYPE_SCAN_TIMEOUT:
			type_str = "SCAN_timeout";
			break;
		case DUMP_TYPE_JOIN_TIMEOUT:
			type_str = "JOIN_timeout";
			break;
		case DUMP_TYPE_SCAN_BUSY:
			type_str = "SCAN_Busy";
			break;
		case DUMP_TYPE_BY_SYSDUMP:
			type_str = "BY_SYSDUMP";
			break;
		case DUMP_TYPE_BY_LIVELOCK:
			type_str = "BY_LIVELOCK";
			break;
		case DUMP_TYPE_AP_LINKUP_FAILURE:
			type_str = "BY_AP_LINK_FAILURE";
			break;
		case DUMP_TYPE_AP_ABNORMAL_ACCESS:
			type_str = "INVALID_ACCESS";
			break;
		case DUMP_TYPE_CFG_VENDOR_TRIGGERED:
			type_str = "CFG_VENDOR_TRIGGERED";
			break;
		case DUMP_TYPE_RESUMED_ON_TIMEOUT_RX:
			type_str = "ERROR_RX_TIMED_OUT";
			break;
		case DUMP_TYPE_RESUMED_ON_TIMEOUT_TX:
			type_str = "ERROR_TX_TIMED_OUT";
			break;
		case DUMP_TYPE_RESUMED_ON_INVALID_RING_RDWR:
			type_str = "BY_INVALID_RING_RDWR";
			break;
		case DUMP_TYPE_DONGLE_HOST_EVENT:
			type_str = "BY_DONGLE_HOST_EVENT";
			break;
		case DUMP_TYPE_TRANS_ID_MISMATCH:
			type_str = "BY_TRANS_ID_MISMATCH";
			break;
		case DUMP_TYPE_HANG_ON_IFACE_OP_FAIL:
			type_str = "HANG_IFACE_OP_FAIL";
			break;
#ifdef SUPPORT_LINKDOWN_RECOVERY
		case DUMP_TYPE_READ_SHM_FAIL:
			type_str = "READ_SHM_FAIL";
			break;
#endif /* SUPPORT_LINKDOWN_RECOVERY */
		default:
			type_str = "Unknown_type";
			break;
	}

	strncpy(buf, type_str, strlen(type_str));
	buf[strlen(type_str)] = 0;
}

int
write_dump_to_file(dhd_pub_t *dhd, uint8 *buf, int size, char *fname)
{
	int ret = 0;
	char memdump_path[128];
	char memdump_type[32];
	struct osl_timespec curtime;
	uint32 file_mode;

	/* Init file name */
	memset(memdump_path, 0, sizeof(memdump_path));
	memset(memdump_type, 0, sizeof(memdump_type));
	osl_do_gettimeofday(&curtime);
	dhd_convert_memdump_type_to_str(dhd->memdump_type, memdump_type);
#ifdef CUSTOMER_HW4_DEBUG
	snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_%ld.%ld",
		DHD_COMMON_DUMP_PATH, fname, memdump_type,
		(unsigned long)curtime.tv_sec, (unsigned long)curtime.tv_usec);
	file_mode = O_CREAT | O_WRONLY | O_SYNC;
#elif defined(CUSTOMER_HW2)
	snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_%ld.%ld",
		"/data/misc/wifi/", fname, memdump_type,
		(unsigned long)curtime.tv_sec, (unsigned long)curtime.tv_usec);
	file_mode = O_CREAT | O_WRONLY | O_SYNC;
#elif (defined(BOARD_PANDA) || defined(__ARM_ARCH_7A__))
	snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_%ld.%ld",
		"/data/misc/wifi/", fname, memdump_type,
		(unsigned long)curtime.tv_sec, (unsigned long)curtime.tv_usec);
	file_mode = O_CREAT | O_WRONLY;
#else
	snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_%ld.%ld",
		"/installmedia/", fname, memdump_type,
		(unsigned long)curtime.tv_sec, (unsigned long)curtime.tv_usec);
	/* Extra flags O_DIRECT and O_SYNC are required for Brix Android, as we are
	 * calling BUG_ON immediately after collecting the socram dump.
	 * So the file write operation should directly write the contents into the
	 * file instead of caching it. O_TRUNC flag ensures that file will be re-written
	 * instead of appending.
	 */
	file_mode = O_CREAT | O_WRONLY | O_SYNC;
	{
		struct file *fp = filp_open(memdump_path, file_mode, 0664);
		/* Check if it is live Brix image having /installmedia, else use /data */
		if (IS_ERR(fp)) {
			DHD_ERROR(("open file %s, try /data/\n", memdump_path));
			snprintf(memdump_path, sizeof(memdump_path), "%s%s_%s_%ld.%ld",
				"/data/", fname, memdump_type,
				(unsigned long)curtime.tv_sec, (unsigned long)curtime.tv_usec);
		} else {
			filp_close(fp, NULL);
		}
	}
#endif /* CUSTOMER_HW4_DEBUG */

	/* print SOCRAM dump file path */
	DHD_ERROR(("%s: file_path = %s\n", __FUNCTION__, memdump_path));

	/* Write file */
	ret = write_file(memdump_path, file_mode, buf, size);

	return ret;
}
#endif /* DHD_DEBUG */

int dhd_os_wake_lock_timeout(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd && (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		ret = dhd->wakelock_rx_timeout_enable > dhd->wakelock_ctrl_timeout_enable ?
			dhd->wakelock_rx_timeout_enable : dhd->wakelock_ctrl_timeout_enable;
#ifdef CONFIG_HAS_WAKELOCK
		if (dhd->wakelock_rx_timeout_enable)
			wake_lock_timeout(&dhd->wl_rxwake,
				msecs_to_jiffies(dhd->wakelock_rx_timeout_enable));
		if (dhd->wakelock_ctrl_timeout_enable)
			wake_lock_timeout(&dhd->wl_ctrlwake,
				msecs_to_jiffies(dhd->wakelock_ctrl_timeout_enable));
#endif
		dhd->wakelock_rx_timeout_enable = 0;
		dhd->wakelock_ctrl_timeout_enable = 0;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int net_os_wake_lock_timeout(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_timeout(&dhd->pub);
	return ret;
}

int dhd_os_wake_lock_rx_timeout_enable(dhd_pub_t *pub, int val)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd && (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (val > dhd->wakelock_rx_timeout_enable)
			dhd->wakelock_rx_timeout_enable = val;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return 0;
}

int dhd_os_wake_lock_ctrl_timeout_enable(dhd_pub_t *pub, int val)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd && (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (val > dhd->wakelock_ctrl_timeout_enable)
			dhd->wakelock_ctrl_timeout_enable = val;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return 0;
}

int dhd_os_wake_lock_ctrl_timeout_cancel(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd && (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		dhd->wakelock_ctrl_timeout_enable = 0;
#ifdef CONFIG_HAS_WAKELOCK
		if (wake_lock_active(&dhd->wl_ctrlwake))
			wake_unlock(&dhd->wl_ctrlwake);
#endif
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return 0;
}

int net_os_wake_lock_rx_timeout_enable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_rx_timeout_enable(&dhd->pub, val);
	return ret;
}

int net_os_wake_lock_ctrl_timeout_enable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_ctrl_timeout_enable(&dhd->pub, val);
	return ret;
}


#if defined(DHD_TRACE_WAKE_LOCK)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
#include <linux/hashtable.h>
#else
#include <linux/hash.h>
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0) */


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
/* Define 2^5 = 32 bucket size hash table */
DEFINE_HASHTABLE(wklock_history, 5);
#else
/* Define 2^5 = 32 bucket size hash table */
struct hlist_head wklock_history[32] = { [0 ... 31] = HLIST_HEAD_INIT };
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0) */

int trace_wklock_onoff = 1;
typedef enum dhd_wklock_type {
	DHD_WAKE_LOCK,
	DHD_WAKE_UNLOCK,
	DHD_WAIVE_LOCK,
	DHD_RESTORE_LOCK
} dhd_wklock_t;

struct wk_trace_record {
	unsigned long addr;	            /* Address of the instruction */
	dhd_wklock_t lock_type;         /* lock_type */
	unsigned long long counter;		/* counter information */
	struct hlist_node wklock_node;  /* hash node */
};

static struct wk_trace_record *find_wklock_entry(unsigned long addr)
{
	struct wk_trace_record *wklock_info;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	hash_for_each_possible(wklock_history, wklock_info, wklock_node, addr)
#else
	struct hlist_node *entry;
	int index = hash_long(addr, ilog2(ARRAY_SIZE(wklock_history)));
	hlist_for_each_entry(wklock_info, entry, &wklock_history[index], wklock_node)
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0) */
	{
		if (wklock_info->addr == addr) {
			return wklock_info;
		}
	}
	return NULL;
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
#define HASH_ADD(hashtable, node, key) \
	do { \
		hash_add(hashtable, node, key); \
	} while (0);
#else
#define HASH_ADD(hashtable, node, key) \
	do { \
		int index = hash_long(key, ilog2(ARRAY_SIZE(hashtable))); \
		hlist_add_head(node, &hashtable[index]); \
	} while (0);
#endif /* KERNEL_VER < KERNEL_VERSION(3, 7, 0) */

#define STORE_WKLOCK_RECORD(wklock_type) \
	do { \
		struct wk_trace_record *wklock_info = NULL; \
		unsigned long func_addr = (unsigned long)__builtin_return_address(0); \
		wklock_info = find_wklock_entry(func_addr); \
		if (wklock_info) { \
			if (wklock_type == DHD_WAIVE_LOCK || wklock_type == DHD_RESTORE_LOCK) { \
				wklock_info->counter = dhd->wakelock_counter; \
			} else { \
				wklock_info->counter++; \
			} \
		} else { \
			wklock_info = kzalloc(sizeof(*wklock_info), GFP_ATOMIC); \
			if (!wklock_info) {\
				printk("Can't allocate wk_trace_record \n"); \
			} else { \
				wklock_info->addr = func_addr; \
				wklock_info->lock_type = wklock_type; \
				if (wklock_type == DHD_WAIVE_LOCK || \
						wklock_type == DHD_RESTORE_LOCK) { \
					wklock_info->counter = dhd->wakelock_counter; \
				} else { \
					wklock_info->counter++; \
				} \
				HASH_ADD(wklock_history, &wklock_info->wklock_node, func_addr); \
			} \
		} \
	} while (0);

static inline void dhd_wk_lock_rec_dump(void)
{
	int bkt;
	struct wk_trace_record *wklock_info;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	hash_for_each(wklock_history, bkt, wklock_info, wklock_node)
#else
	struct hlist_node *entry = NULL;
	int max_index = ARRAY_SIZE(wklock_history);
	for (bkt = 0; bkt < max_index; bkt++)
		hlist_for_each_entry(wklock_info, entry, &wklock_history[bkt], wklock_node)
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0) */
		{
			switch (wklock_info->lock_type) {
				case DHD_WAKE_LOCK:
					printk("wakelock lock : %pS  lock_counter : %llu \n",
						(void *)wklock_info->addr, wklock_info->counter);
					break;
				case DHD_WAKE_UNLOCK:
					printk("wakelock unlock : %pS, unlock_counter : %llu \n",
						(void *)wklock_info->addr, wklock_info->counter);
					break;
				case DHD_WAIVE_LOCK:
					printk("wakelock waive : %pS  before_waive : %llu \n",
						(void *)wklock_info->addr, wklock_info->counter);
					break;
				case DHD_RESTORE_LOCK:
					printk("wakelock restore : %pS, after_waive : %llu \n",
						(void *)wklock_info->addr, wklock_info->counter);
					break;
			}
		}
}

static void dhd_wk_lock_trace_init(struct dhd_info *dhd)
{
	unsigned long flags;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0))
	int i;
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0) */

	spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	hash_init(wklock_history);
#else
	for (i = 0; i < ARRAY_SIZE(wklock_history); i++)
		INIT_HLIST_HEAD(&wklock_history[i]);
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0) */
	spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
}

static void dhd_wk_lock_trace_deinit(struct dhd_info *dhd)
{
	int bkt;
	struct wk_trace_record *wklock_info;
	struct hlist_node *tmp;
	unsigned long flags;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0))
	struct hlist_node *entry = NULL;
	int max_index = ARRAY_SIZE(wklock_history);
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0) */

	spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	hash_for_each_safe(wklock_history, bkt, tmp, wklock_info, wklock_node)
#else
	for (bkt = 0; bkt < max_index; bkt++)
		hlist_for_each_entry_safe(wklock_info, entry, tmp,
			&wklock_history[bkt], wklock_node)
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0)) */
		{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
			hash_del(&wklock_info->wklock_node);
#else
			hlist_del_init(&wklock_info->wklock_node);
#endif /* KERNEL_VER >= KERNEL_VERSION(3, 7, 0)) */
			kfree(wklock_info);
		}
	spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
}

void dhd_wk_lock_stats_dump(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	unsigned long flags;

	printk(KERN_ERR"DHD Printing wl_wake Lock/Unlock Record \r\n");
	spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
	dhd_wk_lock_rec_dump();
	spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);

}
#else
#define STORE_WKLOCK_RECORD(wklock_type)
#endif /* ! DHD_TRACE_WAKE_LOCK */

int dhd_os_wake_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd && (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (dhd->wakelock_counter == 0 && !dhd->waive_wakelock) {
#ifdef CONFIG_HAS_WAKELOCK
			wake_lock(&dhd->wl_wifi);
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
			dhd_bus_dev_pm_stay_awake(pub);
#endif
		}
#ifdef DHD_TRACE_WAKE_LOCK
		if (trace_wklock_onoff) {
			STORE_WKLOCK_RECORD(DHD_WAKE_LOCK);
		}
#endif /* DHD_TRACE_WAKE_LOCK */
		dhd->wakelock_counter++;
		ret = dhd->wakelock_counter;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}

	return ret;
}

void dhd_event_wake_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
#ifdef CONFIG_HAS_WAKELOCK
		wake_lock(&dhd->wl_evtwake);
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
		dhd_bus_dev_pm_stay_awake(pub);
#endif
	}
}

void
dhd_pm_wake_lock_timeout(dhd_pub_t *pub, int val)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		wake_lock_timeout(&dhd->wl_pmwake, msecs_to_jiffies(val));
	}
#endif /* CONFIG_HAS_WAKE_LOCK */
}

void
dhd_txfl_wake_lock_timeout(dhd_pub_t *pub, int val)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		wake_lock_timeout(&dhd->wl_txflwake, msecs_to_jiffies(val));
	}
#endif /* CONFIG_HAS_WAKE_LOCK */
}

int net_os_wake_lock(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock(&dhd->pub);
	return ret;
}

int dhd_os_wake_unlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	dhd_os_wake_lock_timeout(pub);
	if (dhd && (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);

		if (dhd->wakelock_counter > 0) {
			dhd->wakelock_counter--;
#ifdef DHD_TRACE_WAKE_LOCK
			if (trace_wklock_onoff) {
				STORE_WKLOCK_RECORD(DHD_WAKE_UNLOCK);
			}
#endif /* DHD_TRACE_WAKE_LOCK */
			if (dhd->wakelock_counter == 0 && !dhd->waive_wakelock) {
#ifdef CONFIG_HAS_WAKELOCK
				wake_unlock(&dhd->wl_wifi);
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
				dhd_bus_dev_pm_relax(pub);
#endif
			}
			ret = dhd->wakelock_counter;
		}
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

void dhd_event_wake_unlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
#ifdef CONFIG_HAS_WAKELOCK
		wake_unlock(&dhd->wl_evtwake);
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
		dhd_bus_dev_pm_relax(pub);
#endif
	}
}

void dhd_pm_wake_unlock(dhd_pub_t *pub)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		/* if wl_pmwake is active, unlock it */
		if (wake_lock_active(&dhd->wl_pmwake)) {
			wake_unlock(&dhd->wl_pmwake);
		}
	}
#endif /* CONFIG_HAS_WAKELOCK */
}

void dhd_txfl_wake_unlock(dhd_pub_t *pub)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		/* if wl_txflwake is active, unlock it */
		if (wake_lock_active(&dhd->wl_txflwake)) {
			wake_unlock(&dhd->wl_txflwake);
		}
	}
#endif /* CONFIG_HAS_WAKELOCK */
}

int dhd_os_check_wakelock(dhd_pub_t *pub)
{
#if defined(CONFIG_HAS_WAKELOCK) || (defined(BCMSDIO) && (LINUX_VERSION_CODE > \
	KERNEL_VERSION(2, 6, 36)))
	dhd_info_t *dhd;

	if (!pub)
		return 0;
	dhd = (dhd_info_t *)(pub->info);
#endif /* CONFIG_HAS_WAKELOCK || BCMSDIO */

#ifdef CONFIG_HAS_WAKELOCK
	/* Indicate to the SD Host to avoid going to suspend if internal locks are up */
	if (dhd && (wake_lock_active(&dhd->wl_wifi) ||
		(wake_lock_active(&dhd->wl_wdwake))))
		return 1;
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
	if (dhd && (dhd->wakelock_counter > 0) && dhd_bus_dev_pm_enabled(pub))
		return 1;
#endif
	return 0;
}

int
dhd_os_check_wakelock_all(dhd_pub_t *pub)
{
#if defined(CONFIG_HAS_WAKELOCK) || (defined(BCMSDIO) && (LINUX_VERSION_CODE > \
	KERNEL_VERSION(2, 6, 36)))
#if defined(CONFIG_HAS_WAKELOCK)
	int l1, l2, l3, l4, l7, l8, l9;
	int l5 = 0, l6 = 0;
	int c, lock_active;
#endif /* CONFIG_HAS_WAKELOCK */
	dhd_info_t *dhd;

	if (!pub) {
		return 0;
	}
	dhd = (dhd_info_t *)(pub->info);
	if (!dhd) {
		return 0;
	}
#endif /* CONFIG_HAS_WAKELOCK || BCMSDIO */

#ifdef CONFIG_HAS_WAKELOCK
	c = dhd->wakelock_counter;
	l1 = wake_lock_active(&dhd->wl_wifi);
	l2 = wake_lock_active(&dhd->wl_wdwake);
	l3 = wake_lock_active(&dhd->wl_rxwake);
	l4 = wake_lock_active(&dhd->wl_ctrlwake);
	l7 = wake_lock_active(&dhd->wl_evtwake);
#ifdef BCMPCIE_OOB_HOST_WAKE
	l5 = wake_lock_active(&dhd->wl_intrwake);
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef DHD_USE_SCAN_WAKELOCK
	l6 = wake_lock_active(&dhd->wl_scanwake);
#endif /* DHD_USE_SCAN_WAKELOCK */
	l8 = wake_lock_active(&dhd->wl_pmwake);
	l9 = wake_lock_active(&dhd->wl_txflwake);
	lock_active = (l1 || l2 || l3 || l4 || l5 || l6 || l7 || l8 || l9);

	/* Indicate to the Host to avoid going to suspend if internal locks are up */
	if (lock_active) {
		DHD_ERROR(("%s wakelock c-%d wl-%d wd-%d rx-%d "
			"ctl-%d intr-%d scan-%d evt-%d, pm-%d, txfl-%d\n",
			__FUNCTION__, c, l1, l2, l3, l4, l5, l6, l7, l8, l9));
		return 1;
	}
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
	if (dhd && (dhd->wakelock_counter > 0) && dhd_bus_dev_pm_enabled(pub)) {
		return 1;
	}
#endif /* defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36)) */
	return 0;
}

int net_os_wake_unlock(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_unlock(&dhd->pub);
	return ret;
}

int dhd_os_wd_wake_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
#ifdef CONFIG_HAS_WAKELOCK
		/* if wakelock_wd_counter was never used : lock it at once */
		if (!dhd->wakelock_wd_counter)
			wake_lock(&dhd->wl_wdwake);
#endif
		dhd->wakelock_wd_counter++;
		ret = dhd->wakelock_wd_counter;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int dhd_os_wd_wake_unlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (dhd->wakelock_wd_counter) {
			dhd->wakelock_wd_counter = 0;
#ifdef CONFIG_HAS_WAKELOCK
			wake_unlock(&dhd->wl_wdwake);
#endif
		}
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

#ifdef BCMPCIE_OOB_HOST_WAKE
void
dhd_os_oob_irq_wake_lock_timeout(dhd_pub_t *pub, int val)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		wake_lock_timeout(&dhd->wl_intrwake, msecs_to_jiffies(val));
	}
#endif /* CONFIG_HAS_WAKELOCK */
}

void
dhd_os_oob_irq_wake_unlock(dhd_pub_t *pub)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		/* if wl_intrwake is active, unlock it */
		if (wake_lock_active(&dhd->wl_intrwake)) {
			wake_unlock(&dhd->wl_intrwake);
		}
	}
#endif /* CONFIG_HAS_WAKELOCK */
}
#endif /* BCMPCIE_OOB_HOST_WAKE */

#ifdef DHD_USE_SCAN_WAKELOCK
void
dhd_os_scan_wake_lock_timeout(dhd_pub_t *pub, int val)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		wake_lock_timeout(&dhd->wl_scanwake, msecs_to_jiffies(val));
	}
#endif /* CONFIG_HAS_WAKELOCK */
}

void
dhd_os_scan_wake_unlock(dhd_pub_t *pub)
{
#ifdef CONFIG_HAS_WAKELOCK
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		/* if wl_scanwake is active, unlock it */
		if (wake_lock_active(&dhd->wl_scanwake)) {
			wake_unlock(&dhd->wl_scanwake);
		}
	}
#endif /* CONFIG_HAS_WAKELOCK */
}
#endif /* DHD_USE_SCAN_WAKELOCK */

/* waive wakelocks for operations such as IOVARs in suspend function, must be closed
 * by a paired function call to dhd_wakelock_restore. returns current wakelock counter
 */
int dhd_os_wake_lock_waive(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd && (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT)) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);

		/* dhd_wakelock_waive/dhd_wakelock_restore must be paired */
		if (dhd->waive_wakelock == FALSE) {
#ifdef DHD_TRACE_WAKE_LOCK
			if (trace_wklock_onoff) {
				STORE_WKLOCK_RECORD(DHD_WAIVE_LOCK);
			}
#endif /* DHD_TRACE_WAKE_LOCK */
			/* record current lock status */
			dhd->wakelock_before_waive = dhd->wakelock_counter;
			dhd->waive_wakelock = TRUE;
		}
		ret = dhd->wakelock_wd_counter;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int dhd_os_wake_lock_restore(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (!dhd)
		return 0;
	if ((dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT) == 0)
		return 0;

	spin_lock_irqsave(&dhd->wakelock_spinlock, flags);

	/* dhd_wakelock_waive/dhd_wakelock_restore must be paired */
	if (!dhd->waive_wakelock)
		goto exit;

	dhd->waive_wakelock = FALSE;
	/* if somebody else acquires wakelock between dhd_wakelock_waive/dhd_wakelock_restore,
	 * we need to make it up by calling wake_lock or pm_stay_awake. or if somebody releases
	 * the lock in between, do the same by calling wake_unlock or pm_relax
	 */
#ifdef DHD_TRACE_WAKE_LOCK
	if (trace_wklock_onoff) {
		STORE_WKLOCK_RECORD(DHD_RESTORE_LOCK);
	}
#endif /* DHD_TRACE_WAKE_LOCK */

	if (dhd->wakelock_before_waive == 0 && dhd->wakelock_counter > 0) {
#ifdef CONFIG_HAS_WAKELOCK
		wake_lock(&dhd->wl_wifi);
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
		dhd_bus_dev_pm_stay_awake(&dhd->pub);
#endif
	} else if (dhd->wakelock_before_waive > 0 && dhd->wakelock_counter == 0) {
#ifdef CONFIG_HAS_WAKELOCK
		wake_unlock(&dhd->wl_wifi);
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
		dhd_bus_dev_pm_relax(&dhd->pub);
#endif
	}
	dhd->wakelock_before_waive = 0;
exit:
	ret = dhd->wakelock_wd_counter;
	spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	return ret;
}

void dhd_os_wake_lock_init(struct dhd_info *dhd)
{
	DHD_TRACE(("%s: initialize wake_lock_counters\n", __FUNCTION__));
	dhd->wakelock_counter = 0;
	dhd->wakelock_rx_timeout_enable = 0;
	dhd->wakelock_ctrl_timeout_enable = 0;
#ifdef CONFIG_HAS_WAKELOCK
	// terence 20161023: can not destroy wl_wifi when wlan down, it will happen null pointer in dhd_ioctl_entry
	wake_lock_init(&dhd->wl_rxwake, WAKE_LOCK_SUSPEND, "wlan_rx_wake");
	wake_lock_init(&dhd->wl_ctrlwake, WAKE_LOCK_SUSPEND, "wlan_ctrl_wake");
	wake_lock_init(&dhd->wl_evtwake, WAKE_LOCK_SUSPEND, "wlan_evt_wake");
	wake_lock_init(&dhd->wl_pmwake, WAKE_LOCK_SUSPEND, "wlan_pm_wake");
	wake_lock_init(&dhd->wl_txflwake, WAKE_LOCK_SUSPEND, "wlan_txfl_wake");
#ifdef BCMPCIE_OOB_HOST_WAKE
	wake_lock_init(&dhd->wl_intrwake, WAKE_LOCK_SUSPEND, "wlan_oob_irq_wake");
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef DHD_USE_SCAN_WAKELOCK
	wake_lock_init(&dhd->wl_scanwake, WAKE_LOCK_SUSPEND, "wlan_scan_wake");
#endif /* DHD_USE_SCAN_WAKELOCK */
#endif /* CONFIG_HAS_WAKELOCK */
#ifdef DHD_TRACE_WAKE_LOCK
	dhd_wk_lock_trace_init(dhd);
#endif /* DHD_TRACE_WAKE_LOCK */
}

void dhd_os_wake_lock_destroy(struct dhd_info *dhd)
{
	DHD_TRACE(("%s: deinit wake_lock_counters\n", __FUNCTION__));
#ifdef CONFIG_HAS_WAKELOCK
	dhd->wakelock_counter = 0;
	dhd->wakelock_rx_timeout_enable = 0;
	dhd->wakelock_ctrl_timeout_enable = 0;
	// terence 20161023: can not destroy wl_wifi when wlan down, it will happen null pointer in dhd_ioctl_entry
	wake_lock_destroy(&dhd->wl_rxwake);
	wake_lock_destroy(&dhd->wl_ctrlwake);
	wake_lock_destroy(&dhd->wl_evtwake);
	wake_lock_destroy(&dhd->wl_pmwake);
	wake_lock_destroy(&dhd->wl_txflwake);
#ifdef BCMPCIE_OOB_HOST_WAKE
	wake_lock_destroy(&dhd->wl_intrwake);
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef DHD_USE_SCAN_WAKELOCK
	wake_lock_destroy(&dhd->wl_scanwake);
#endif /* DHD_USE_SCAN_WAKELOCK */
#ifdef DHD_TRACE_WAKE_LOCK
	dhd_wk_lock_trace_deinit(dhd);
#endif /* DHD_TRACE_WAKE_LOCK */
#endif /* CONFIG_HAS_WAKELOCK */
}

bool dhd_os_check_if_up(dhd_pub_t *pub)
{
	if (!pub)
		return FALSE;
	return pub->up;
}

/* function to collect firmware, chip id and chip version info */
void dhd_set_version_info(dhd_pub_t *dhdp, char *fw)
{
	int i;

	i = snprintf(info_string, sizeof(info_string),
		"  Driver: %s\n  Firmware: %s\n  CLM: %s ", EPI_VERSION_STR, fw, clm_version);
	printf("%s\n", info_string);

	if (!dhdp)
		return;

	i = snprintf(&info_string[i], sizeof(info_string) - i,
		"\n  Chip: %x Rev %x", dhd_conf_get_chip(dhdp),
		dhd_conf_get_chiprev(dhdp));
}

int dhd_ioctl_entry_local(struct net_device *net, wl_ioctl_t *ioc, int cmd)
{
	int ifidx;
	int ret = 0;
	dhd_info_t *dhd = NULL;

	if (!net || !DEV_PRIV(net)) {
		DHD_ERROR(("%s invalid parameter\n", __FUNCTION__));
		return -EINVAL;
	}

	dhd = DHD_DEV_INFO(net);
	if (!dhd)
		return -EINVAL;

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, ioc, ioc->buf, ioc->len);
	dhd_check_hang(net, &dhd->pub, ret);

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	return ret;
}

bool dhd_os_check_hang(dhd_pub_t *dhdp, int ifidx, int ret)
{
	struct net_device *net;

	net = dhd_idx2net(dhdp, ifidx);
	if (!net) {
		DHD_ERROR(("%s : Invalid index : %d\n", __FUNCTION__, ifidx));
		return -EINVAL;
	}

	return dhd_check_hang(net, dhdp, ret);
}

/* Return instance */
int dhd_get_instance(dhd_pub_t *dhdp)
{
	return dhdp->info->unit;
}


#ifdef PROP_TXSTATUS

void dhd_wlfc_plat_init(void *dhd)
{
#ifdef USE_DYNAMIC_F2_BLKSIZE
	dhdsdio_func_blocksize((dhd_pub_t *)dhd, 2, DYNAMIC_F2_BLKSIZE_FOR_NONLEGACY);
#endif /* USE_DYNAMIC_F2_BLKSIZE */
	return;
}

void dhd_wlfc_plat_deinit(void *dhd)
{
#ifdef USE_DYNAMIC_F2_BLKSIZE
	dhdsdio_func_blocksize((dhd_pub_t *)dhd, 2, sd_f2_blocksize);
#endif /* USE_DYNAMIC_F2_BLKSIZE */
	return;
}

bool dhd_wlfc_skip_fc(void * dhdp, uint8 idx)
{
#ifdef SKIP_WLFC_ON_CONCURRENT

#ifdef WL_CFG80211
	struct net_device * net =  dhd_idx2net((dhd_pub_t *)dhdp, idx);
	if (net)
	/* enable flow control in vsdb mode */
	return !(wl_cfg80211_is_concurrent_mode(net));
#else
	return TRUE; /* skip flow control */
#endif /* WL_CFG80211 */

#else
	return FALSE;
#endif /* SKIP_WLFC_ON_CONCURRENT */
	return FALSE;
}
#endif /* PROP_TXSTATUS */

#ifdef BCMDBGFS
#include <linux/debugfs.h>

typedef struct dhd_dbgfs {
	struct dentry	*debugfs_dir;
	struct dentry	*debugfs_mem;
	dhd_pub_t	*dhdp;
	uint32		size;
} dhd_dbgfs_t;

dhd_dbgfs_t g_dbgfs;

extern uint32 dhd_readregl(void *bp, uint32 addr);
extern uint32 dhd_writeregl(void *bp, uint32 addr, uint32 data);

static int
dhd_dbg_state_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t
dhd_dbg_state_read(struct file *file, char __user *ubuf,
                       size_t count, loff_t *ppos)
{
	ssize_t rval;
	uint32 tmp;
	loff_t pos = *ppos;
	size_t ret;

	if (pos < 0)
		return -EINVAL;
	if (pos >= g_dbgfs.size || !count)
		return 0;
	if (count > g_dbgfs.size - pos)
		count = g_dbgfs.size - pos;

	/* Basically enforce aligned 4 byte reads. It's up to the user to work out the details */
	tmp = dhd_readregl(g_dbgfs.dhdp->bus, file->f_pos & (~3));

	ret = copy_to_user(ubuf, &tmp, 4);
	if (ret == count)
		return -EFAULT;

	count -= ret;
	*ppos = pos + count;
	rval = count;

	return rval;
}


static ssize_t
dhd_debugfs_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	size_t ret;
	uint32 buf;

	if (pos < 0)
		return -EINVAL;
	if (pos >= g_dbgfs.size || !count)
		return 0;
	if (count > g_dbgfs.size - pos)
		count = g_dbgfs.size - pos;

	ret = copy_from_user(&buf, ubuf, sizeof(uint32));
	if (ret == count)
		return -EFAULT;

	/* Basically enforce aligned 4 byte writes. It's up to the user to work out the details */
	dhd_writeregl(g_dbgfs.dhdp->bus, file->f_pos & (~3), buf);

	return count;
}


loff_t
dhd_debugfs_lseek(struct file *file, loff_t off, int whence)
{
	loff_t pos = -1;

	switch (whence) {
		case 0:
			pos = off;
			break;
		case 1:
			pos = file->f_pos + off;
			break;
		case 2:
			pos = g_dbgfs.size - off;
	}
	return (pos < 0 || pos > g_dbgfs.size) ? -EINVAL : (file->f_pos = pos);
}

static const struct file_operations dhd_dbg_state_ops = {
	.read   = dhd_dbg_state_read,
	.write	= dhd_debugfs_write,
	.open   = dhd_dbg_state_open,
	.llseek	= dhd_debugfs_lseek
};

static void dhd_dbgfs_create(void)
{
	if (g_dbgfs.debugfs_dir) {
		g_dbgfs.debugfs_mem = debugfs_create_file("mem", 0644, g_dbgfs.debugfs_dir,
			NULL, &dhd_dbg_state_ops);
	}
}

void dhd_dbgfs_init(dhd_pub_t *dhdp)
{
	g_dbgfs.dhdp = dhdp;
	g_dbgfs.size = 0x20000000; /* Allow access to various cores regs */

	g_dbgfs.debugfs_dir = debugfs_create_dir("dhd", 0);
	if (IS_ERR(g_dbgfs.debugfs_dir)) {
		g_dbgfs.debugfs_dir = NULL;
		return;
	}

	dhd_dbgfs_create();

	return;
}

void dhd_dbgfs_remove(void)
{
	debugfs_remove(g_dbgfs.debugfs_mem);
	debugfs_remove(g_dbgfs.debugfs_dir);

	bzero((unsigned char *) &g_dbgfs, sizeof(g_dbgfs));
}
#endif /* BCMDBGFS */

#ifdef WLMEDIA_HTSF

static
void dhd_htsf_addtxts(dhd_pub_t *dhdp, void *pktbuf)
{
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct sk_buff *skb;
	uint32 htsf = 0;
	uint16 dport = 0, oldmagic = 0xACAC;
	char *p1;
	htsfts_t ts;

	/*  timestamp packet  */

	p1 = (char*) PKTDATA(dhdp->osh, pktbuf);

	if (PKTLEN(dhdp->osh, pktbuf) > HTSF_MINLEN) {
/*		memcpy(&proto, p1+26, 4);  	*/
		memcpy(&dport, p1+40, 2);
/* 	proto = ((ntoh32(proto))>> 16) & 0xFF;  */
		dport = ntoh16(dport);
	}

	/* timestamp only if  icmp or udb iperf with port 5555 */
/*	if (proto == 17 && dport == tsport) { */
	if (dport >= tsport && dport <= tsport + 20) {

		skb = (struct sk_buff *) pktbuf;

		htsf = dhd_get_htsf(dhd, 0);
		memset(skb->data + 44, 0, 2); /* clear checksum */
		memcpy(skb->data+82, &oldmagic, 2);
		memcpy(skb->data+84, &htsf, 4);

		memset(&ts, 0, sizeof(htsfts_t));
		ts.magic  = HTSFMAGIC;
		ts.prio   = PKTPRIO(pktbuf);
		ts.seqnum = htsf_seqnum++;
		ts.c10    = get_cycles();
		ts.t10    = htsf;
		ts.endmagic = HTSFENDMAGIC;

		memcpy(skb->data + HTSF_HOSTOFFSET, &ts, sizeof(ts));
	}
}

static void dhd_dump_htsfhisto(histo_t *his, char *s)
{
	int pktcnt = 0, curval = 0, i;
	for (i = 0; i < (NUMBIN-2); i++) {
		curval += 500;
		printf("%d ",  his->bin[i]);
		pktcnt += his->bin[i];
	}
	printf(" max: %d TotPkt: %d neg: %d [%s]\n", his->bin[NUMBIN-2], pktcnt,
		his->bin[NUMBIN-1], s);
}

static
void sorttobin(int value, histo_t *histo)
{
	int i, binval = 0;

	if (value < 0) {
		histo->bin[NUMBIN-1]++;
		return;
	}
	if (value > histo->bin[NUMBIN-2])  /* store the max value  */
		histo->bin[NUMBIN-2] = value;

	for (i = 0; i < (NUMBIN-2); i++) {
		binval += 500; /* 500m s bins */
		if (value <= binval) {
			histo->bin[i]++;
			return;
		}
	}
	histo->bin[NUMBIN-3]++;
}

static
void dhd_htsf_addrxts(dhd_pub_t *dhdp, void *pktbuf)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct sk_buff *skb;
	char *p1;
	uint16 old_magic;
	int d1, d2, d3, end2end;
	htsfts_t *htsf_ts;
	uint32 htsf;

	skb = PKTTONATIVE(dhdp->osh, pktbuf);
	p1 = (char*)PKTDATA(dhdp->osh, pktbuf);

	if (PKTLEN(osh, pktbuf) > HTSF_MINLEN) {
		memcpy(&old_magic, p1+78, 2);
		htsf_ts = (htsfts_t*) (p1 + HTSF_HOSTOFFSET - 4);
	} else {
		return;
	}

	if (htsf_ts->magic == HTSFMAGIC) {
		htsf_ts->tE0 = dhd_get_htsf(dhd, 0);
		htsf_ts->cE0 = get_cycles();
	}

	if (old_magic == 0xACAC) {

		tspktcnt++;
		htsf = dhd_get_htsf(dhd, 0);
		memcpy(skb->data+92, &htsf, sizeof(uint32));

		memcpy(&ts[tsidx].t1, skb->data+80, 16);

		d1 = ts[tsidx].t2 - ts[tsidx].t1;
		d2 = ts[tsidx].t3 - ts[tsidx].t2;
		d3 = ts[tsidx].t4 - ts[tsidx].t3;
		end2end = ts[tsidx].t4 - ts[tsidx].t1;

		sorttobin(d1, &vi_d1);
		sorttobin(d2, &vi_d2);
		sorttobin(d3, &vi_d3);
		sorttobin(end2end, &vi_d4);

		if (end2end > 0 && end2end >  maxdelay) {
			maxdelay = end2end;
			maxdelaypktno = tspktcnt;
			memcpy(&maxdelayts, &ts[tsidx], 16);
		}
		if (++tsidx >= TSMAX)
			tsidx = 0;
	}
}

uint32 dhd_get_htsf(dhd_info_t *dhd, int ifidx)
{
	uint32 htsf = 0, cur_cycle, delta, delta_us;
	uint32    factor, baseval, baseval2;
	cycles_t t;

	t = get_cycles();
	cur_cycle = t;

	if (cur_cycle >  dhd->htsf.last_cycle)
		delta = cur_cycle -  dhd->htsf.last_cycle;
	else {
		delta = cur_cycle + (0xFFFFFFFF -  dhd->htsf.last_cycle);
	}

	delta = delta >> 4;

	if (dhd->htsf.coef) {
		/* times ten to get the first digit */
	        factor = (dhd->htsf.coef*10 + dhd->htsf.coefdec1);
		baseval  = (delta*10)/factor;
		baseval2 = (delta*10)/(factor+1);
		delta_us  = (baseval -  (((baseval - baseval2) * dhd->htsf.coefdec2)) / 10);
		htsf = (delta_us << 4) +  dhd->htsf.last_tsf + HTSF_BUS_DELAY;
	} else {
		DHD_ERROR(("-------dhd->htsf.coef = 0 -------\n"));
	}

	return htsf;
}

static void dhd_dump_latency(void)
{
	int i, max = 0;
	int d1, d2, d3, d4, d5;

	printf("T1       T2       T3       T4           d1  d2   t4-t1     i    \n");
	for (i = 0; i < TSMAX; i++) {
		d1 = ts[i].t2 - ts[i].t1;
		d2 = ts[i].t3 - ts[i].t2;
		d3 = ts[i].t4 - ts[i].t3;
		d4 = ts[i].t4 - ts[i].t1;
		d5 = ts[max].t4-ts[max].t1;
		if (d4 > d5 && d4 > 0)  {
			max = i;
		}
		printf("%08X %08X %08X %08X \t%d %d %d   %d i=%d\n",
			ts[i].t1, ts[i].t2, ts[i].t3, ts[i].t4,
			d1, d2, d3, d4, i);
	}

	printf("current idx = %d \n", tsidx);

	printf("Highest latency %d pkt no.%d total=%d\n", maxdelay, maxdelaypktno, tspktcnt);
	printf("%08X %08X %08X %08X \t%d %d %d   %d\n",
	maxdelayts.t1, maxdelayts.t2, maxdelayts.t3, maxdelayts.t4,
	maxdelayts.t2 - maxdelayts.t1,
	maxdelayts.t3 - maxdelayts.t2,
	maxdelayts.t4 - maxdelayts.t3,
	maxdelayts.t4 - maxdelayts.t1);
}


static int
dhd_ioctl_htsf_get(dhd_info_t *dhd, int ifidx)
{
	char buf[32];
	int ret;
	uint32 s1, s2;

	struct tsf {
		uint32 low;
		uint32 high;
	} tsf_buf;

	memset(&tsf_buf, 0, sizeof(tsf_buf));

	s1 = dhd_get_htsf(dhd, 0);
	ret = dhd_iovar(&dhd->pub, ifidx, "tsf", NULL, 0, buf, sizeof(buf), FALSE);
	if (ret < 0) {
		if (ret == -EIO) {
			DHD_ERROR(("%s: tsf is not supported by device\n",
				dhd_ifname(&dhd->pub, ifidx)));
			return -EOPNOTSUPP;
		}
		return ret;
	}
	s2 = dhd_get_htsf(dhd, 0);

	memcpy(&tsf_buf, buf, sizeof(tsf_buf));
	printf(" TSF_h=%04X lo=%08X Calc:htsf=%08X, coef=%d.%d%d delta=%d ",
		tsf_buf.high, tsf_buf.low, s2, dhd->htsf.coef, dhd->htsf.coefdec1,
		dhd->htsf.coefdec2, s2-tsf_buf.low);
	printf("lasttsf=%08X lastcycle=%08X\n", dhd->htsf.last_tsf, dhd->htsf.last_cycle);
	return 0;
}

void htsf_update(dhd_info_t *dhd, void *data)
{
	static ulong  cur_cycle = 0, prev_cycle = 0;
	uint32 htsf, tsf_delta = 0;
	uint32 hfactor = 0, cyc_delta, dec1 = 0, dec2, dec3, tmp;
	ulong b, a;
	cycles_t t;

	/* cycles_t in inlcude/mips/timex.h */

	t = get_cycles();

	prev_cycle = cur_cycle;
	cur_cycle = t;

	if (cur_cycle > prev_cycle)
		cyc_delta = cur_cycle - prev_cycle;
	else {
		b = cur_cycle;
		a = prev_cycle;
		cyc_delta = cur_cycle + (0xFFFFFFFF - prev_cycle);
	}

	if (data == NULL)
		printf(" tsf update ata point er is null \n");

	memcpy(&prev_tsf, &cur_tsf, sizeof(tsf_t));
	memcpy(&cur_tsf, data, sizeof(tsf_t));

	if (cur_tsf.low == 0) {
		DHD_INFO((" ---- 0 TSF, do not update, return\n"));
		return;
	}

	if (cur_tsf.low > prev_tsf.low)
		tsf_delta = (cur_tsf.low - prev_tsf.low);
	else {
		DHD_INFO((" ---- tsf low is smaller cur_tsf= %08X, prev_tsf=%08X, \n",
		 cur_tsf.low, prev_tsf.low));
		if (cur_tsf.high > prev_tsf.high) {
			tsf_delta = cur_tsf.low + (0xFFFFFFFF - prev_tsf.low);
			DHD_INFO((" ---- Wrap around tsf coutner  adjusted TSF=%08X\n", tsf_delta));
		} else {
			return; /* do not update */
		}
	}

	if (tsf_delta)  {
		hfactor = cyc_delta / tsf_delta;
		tmp  = 	(cyc_delta - (hfactor * tsf_delta))*10;
		dec1 =  tmp/tsf_delta;
		dec2 =  ((tmp - dec1*tsf_delta)*10) / tsf_delta;
		tmp  = 	(tmp   - (dec1*tsf_delta))*10;
		dec3 =  ((tmp - dec2*tsf_delta)*10) / tsf_delta;

		if (dec3 > 4) {
			if (dec2 == 9) {
				dec2 = 0;
				if (dec1 == 9) {
					dec1 = 0;
					hfactor++;
				} else {
					dec1++;
				}
			} else {
				dec2++;
			}
		}
	}

	if (hfactor) {
		htsf = ((cyc_delta * 10)  / (hfactor*10+dec1)) + prev_tsf.low;
		dhd->htsf.coef = hfactor;
		dhd->htsf.last_cycle = cur_cycle;
		dhd->htsf.last_tsf = cur_tsf.low;
		dhd->htsf.coefdec1 = dec1;
		dhd->htsf.coefdec2 = dec2;
	} else {
		htsf = prev_tsf.low;
	}
}

#endif /* WLMEDIA_HTSF */

#ifdef CUSTOM_SET_CPUCORE
void dhd_set_cpucore(dhd_pub_t *dhd, int set)
{
	int e_dpc = 0, e_rxf = 0, retry_set = 0;

	if (!(dhd->chan_isvht80)) {
		DHD_ERROR(("%s: chan_status(%d) cpucore!!!\n", __FUNCTION__, dhd->chan_isvht80));
		return;
	}

	if (DPC_CPUCORE) {
		do {
			if (set == TRUE) {
				e_dpc = set_cpus_allowed_ptr(dhd->current_dpc,
					cpumask_of(DPC_CPUCORE));
			} else {
				e_dpc = set_cpus_allowed_ptr(dhd->current_dpc,
					cpumask_of(PRIMARY_CPUCORE));
			}
			if (retry_set++ > MAX_RETRY_SET_CPUCORE) {
				DHD_ERROR(("%s: dpc(%d) invalid cpu!\n", __FUNCTION__, e_dpc));
				return;
			}
			if (e_dpc < 0)
				OSL_SLEEP(1);
		} while (e_dpc < 0);
	}
	if (RXF_CPUCORE) {
		do {
			if (set == TRUE) {
				e_rxf = set_cpus_allowed_ptr(dhd->current_rxf,
					cpumask_of(RXF_CPUCORE));
			} else {
				e_rxf = set_cpus_allowed_ptr(dhd->current_rxf,
					cpumask_of(PRIMARY_CPUCORE));
			}
			if (retry_set++ > MAX_RETRY_SET_CPUCORE) {
				DHD_ERROR(("%s: rxf(%d) invalid cpu!\n", __FUNCTION__, e_rxf));
				return;
			}
			if (e_rxf < 0)
				OSL_SLEEP(1);
		} while (e_rxf < 0);
	}
#ifdef DHD_OF_SUPPORT
	interrupt_set_cpucore(set, DPC_CPUCORE, PRIMARY_CPUCORE);
#endif /* DHD_OF_SUPPORT */
	DHD_TRACE(("%s: set(%d) cpucore success!\n", __FUNCTION__, set));

	return;
}
#endif /* CUSTOM_SET_CPUCORE */

#ifdef DHD_MCAST_REGEN
/* Get interface specific ap_isolate configuration */
int dhd_get_mcast_regen_bss_enable(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	return ifp->mcast_regen_bss_enable;
}

/* Set interface specific mcast_regen configuration */
int dhd_set_mcast_regen_bss_enable(dhd_pub_t *dhdp, uint32 idx, int val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	ifp->mcast_regen_bss_enable = val;

	/* Disable rx_pkt_chain feature for interface, if mcast_regen feature
	 * is enabled
	 */
	dhd_update_rx_pkt_chainable_state(dhdp, idx);
	return BCME_OK;
}
#endif	/* DHD_MCAST_REGEN */

/* Get interface specific ap_isolate configuration */
int dhd_get_ap_isolate(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	return ifp->ap_isolate;
}

/* Set interface specific ap_isolate configuration */
int dhd_set_ap_isolate(dhd_pub_t *dhdp, uint32 idx, int val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	if (ifp)
		ifp->ap_isolate = val;

	return 0;
}

#ifdef DHD_FW_COREDUMP
#if defined(CONFIG_X86)
#define MEMDUMPINFO_LIVE "/installmedia/.memdump.info"
#define MEMDUMPINFO_INST "/data/.memdump.info"
#endif /* CONFIG_X86 && OEM_ANDROID */

#ifdef CUSTOMER_HW4_DEBUG
#define MEMDUMPINFO PLATFORM_PATH".memdump.info"
#elif defined(CUSTOMER_HW2)
#define MEMDUMPINFO "/data/misc/wifi/.memdump.info"
#elif (defined(BOARD_PANDA) || defined(__ARM_ARCH_7A__))
#define MEMDUMPINFO "/data/misc/wifi/.memdump.info"
#else
#define MEMDUMPINFO "/data/misc/wifi/.memdump.info"
#endif /* CUSTOMER_HW4_DEBUG */

void dhd_get_memdump_info(dhd_pub_t *dhd)
{
	struct file *fp = NULL;
	uint32 mem_val = DUMP_MEMFILE_MAX;
	int ret = 0;
	char *filepath = MEMDUMPINFO;

	/* Read memdump info from the file */
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
#if defined(CONFIG_X86)
		/* Check if it is Live Brix Image */
		if (strcmp(filepath, MEMDUMPINFO_LIVE) != 0) {
			goto done;
		}
		/* Try if it is Installed Brix Image */
		filepath = MEMDUMPINFO_INST;
		DHD_ERROR(("%s: Try File [%s]\n", __FUNCTION__, filepath));
		fp = filp_open(filepath, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			DHD_ERROR(("%s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
			goto done;
		}
#else /* Non Brix Android platform */
		goto done;
#endif /* CONFIG_X86 && OEM_ANDROID */
	}

	/* Handle success case */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	ret = kernel_read(fp, (char *)&mem_val, 4, NULL);
#else
	ret = kernel_read(fp, 0, (char *)&mem_val, 4);
#endif
	if (ret < 0) {
		DHD_ERROR(("%s: File read error, ret=%d\n", __FUNCTION__, ret));
		filp_close(fp, NULL);
		goto done;
	}

	mem_val = bcm_atoi((char *)&mem_val);

	filp_close(fp, NULL);

#ifdef DHD_INIT_DEFAULT_MEMDUMP
	if (mem_val == 0 || mem_val == DUMP_MEMFILE_MAX)
		mem_val = DUMP_MEMFILE_BUGON;
#endif /* DHD_INIT_DEFAULT_MEMDUMP */

done:
#ifdef CUSTOMER_HW4_DEBUG
	dhd->memdump_enabled = (mem_val < DUMP_MEMFILE_MAX) ? mem_val : DUMP_DISABLED;
#else
	dhd->memdump_enabled = (mem_val < DUMP_MEMFILE_MAX) ? mem_val : DUMP_MEMFILE;
#endif /* CUSTOMER_HW4_DEBUG */

	DHD_ERROR(("%s: MEMDUMP ENABLED = %d\n", __FUNCTION__, dhd->memdump_enabled));
}

void dhd_schedule_memdump(dhd_pub_t *dhdp, uint8 *buf, uint32 size)
{
	dhd_dump_t *dump = NULL;
	dump = (dhd_dump_t *)MALLOC(dhdp->osh, sizeof(dhd_dump_t));
	if (dump == NULL) {
		DHD_ERROR(("%s: dhd dump memory allocation failed\n", __FUNCTION__));
		return;
	}
	dump->buf = buf;
	dump->bufsize = size;

#if defined(CONFIG_ARM64)
	DHD_ERROR(("%s: buf(va)=%llx, buf(pa)=%llx, bufsize=%d\n", __FUNCTION__,
		(uint64)buf, (uint64)__virt_to_phys((ulong)buf), size));
#elif defined(__ARM_ARCH_7A__)
	DHD_ERROR(("%s: buf(va)=%x, buf(pa)=%x, bufsize=%d\n", __FUNCTION__,
		(uint32)buf, (uint32)__virt_to_phys((ulong)buf), size));
#endif /* __ARM_ARCH_7A__ */
	if (dhdp->memdump_enabled == DUMP_MEMONLY) {
		BUG_ON(1);
	}

#ifdef DHD_LOG_DUMP
	if (dhdp->memdump_type != DUMP_TYPE_BY_SYSDUMP) {
		dhd_schedule_log_dump(dhdp);
	}
#endif /* DHD_LOG_DUMP */
	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq, (void *)dump,
		DHD_WQ_WORK_SOC_RAM_DUMP, dhd_mem_dump, DHD_WQ_WORK_PRIORITY_HIGH);
}

static void
dhd_mem_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_dump_t *dump = event_info;

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	if (!dump) {
		DHD_ERROR(("%s: dump is NULL\n", __FUNCTION__));
		return;
	}

	if (write_dump_to_file(&dhd->pub, dump->buf, dump->bufsize, "mem_dump")) {
		DHD_ERROR(("%s: writing SoC_RAM dump to the file failed\n", __FUNCTION__));
		dhd->pub.memdump_success = FALSE;
	}

	if (dhd->pub.memdump_enabled == DUMP_MEMFILE_BUGON &&
#ifdef DHD_LOG_DUMP
		dhd->pub.memdump_type != DUMP_TYPE_BY_SYSDUMP &&
#endif /* DHD_LOG_DUMP */
#ifdef DHD_DEBUG_UART
		dhd->pub.memdump_success == TRUE &&
#endif	/* DHD_DEBUG_UART */
		dhd->pub.memdump_type != DUMP_TYPE_CFG_VENDOR_TRIGGERED) {

#ifdef SHOW_LOGTRACE
		/* Wait till event_log_dispatcher_work finishes */
		cancel_work_sync(&dhd->event_log_dispatcher_work);
#endif /* SHOW_LOGTRACE */

		BUG_ON(1);
	}
	MFREE(dhd->pub.osh, dump, sizeof(dhd_dump_t));
}
#endif /* DHD_FW_COREDUMP */

#ifdef DHD_SSSR_DUMP

static void
dhd_sssr_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_pub_t *dhdp;
	int i;
	char before_sr_dump[128];
	char after_sr_dump[128];

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	dhdp = &dhd->pub;

	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		/* Init file name */
		memset(before_sr_dump, 0, sizeof(before_sr_dump));
		memset(after_sr_dump, 0, sizeof(after_sr_dump));

		snprintf(before_sr_dump, sizeof(before_sr_dump), "%s_%d_%s",
			"sssr_core", i, "before_SR");
		snprintf(after_sr_dump, sizeof(after_sr_dump), "%s_%d_%s",
			"sssr_core", i, "after_SR");

		if (dhdp->sssr_d11_before[i] && dhdp->sssr_d11_outofreset[i]) {
			if (write_dump_to_file(dhdp, (uint8 *)dhdp->sssr_d11_before[i],
				dhdp->sssr_reg_info.mac_regs[i].sr_size, before_sr_dump)) {
				DHD_ERROR(("%s: writing SSSR MAIN dump before to the file failed\n",
					__FUNCTION__));
			}
		}
		if (dhdp->sssr_d11_after[i] && dhdp->sssr_d11_outofreset[i]) {
			if (write_dump_to_file(dhdp, (uint8 *)dhdp->sssr_d11_after[i],
				dhdp->sssr_reg_info.mac_regs[i].sr_size, after_sr_dump)) {
				DHD_ERROR(("%s: writing SSSR AUX dump after to the file failed\n",
					__FUNCTION__));
			}
		}
	}

	if (dhdp->sssr_vasip_buf_before) {
		if (write_dump_to_file(dhdp, (uint8 *)dhdp->sssr_vasip_buf_before,
			dhdp->sssr_reg_info.vasip_regs.vasip_sr_size, "sssr_vasip_before_SR")) {
			DHD_ERROR(("%s: writing SSSR VASIP dump before to the file failed\n",
				__FUNCTION__));
		}
	}

	if (dhdp->sssr_vasip_buf_after) {
		if (write_dump_to_file(dhdp, (uint8 *)dhdp->sssr_vasip_buf_after,
			dhdp->sssr_reg_info.vasip_regs.vasip_sr_size, "sssr_vasip_after_SR")) {
			DHD_ERROR(("%s: writing SSSR VASIP dump after to the file failed\n",
				__FUNCTION__));
		}
	}

}

void
dhd_schedule_sssr_dump(dhd_pub_t *dhdp)
{
	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq, NULL,
		DHD_WQ_WORK_SSSR_DUMP, dhd_sssr_dump, DHD_WQ_WORK_PRIORITY_HIGH);
}
#endif /* DHD_SSSR_DUMP */

#ifdef DHD_LOG_DUMP
static void
dhd_log_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	if (do_dhd_log_dump(&dhd->pub)) {
		DHD_ERROR(("%s: writing debug dump to the file failed\n", __FUNCTION__));
		return;
	}
}

void dhd_schedule_log_dump(dhd_pub_t *dhdp)
{
	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq,
		(void*)NULL, DHD_WQ_WORK_DHD_LOG_DUMP,
		dhd_log_dump, DHD_WQ_WORK_PRIORITY_HIGH);
}

static int
do_dhd_log_dump(dhd_pub_t *dhdp)
{
	int ret = 0, i = 0;
	struct file *fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
	unsigned int wr_size = 0;
	char dump_path[128];
	struct osl_timespec curtime;
	uint32 file_mode;
	unsigned long flags = 0;
	struct dhd_log_dump_buf *dld_buf = &g_dld_buf[0];

	const char *pre_strs =
		"-------------------- General log ---------------------------\n";

	const char *post_strs =
		"-------------------- Specific log --------------------------\n";

	if (!dhdp) {
		return -1;
	}

	DHD_ERROR(("DHD version: %s\n", dhd_version));
	DHD_ERROR(("F/W version: %s\n", fw_version));

	/* change to KERNEL_DS address limit */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/* Init file name */
	memset(dump_path, 0, sizeof(dump_path));
	osl_do_gettimeofday(&curtime);
	snprintf(dump_path, sizeof(dump_path), "%s_%ld.%ld",
		DHD_COMMON_DUMP_PATH "debug_dump",
		(unsigned long)curtime.tv_sec, (unsigned long)curtime.tv_usec);
	file_mode = O_CREAT | O_WRONLY | O_SYNC;

	DHD_ERROR(("debug_dump_path = %s\n", dump_path));
	fp = filp_open(dump_path, file_mode, 0664);
	if (IS_ERR(fp)) {
		ret = PTR_ERR(fp);
		DHD_ERROR(("open file error, err = %d\n", ret));
		goto exit;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	ret = kernel_write(fp, pre_strs, strlen(pre_strs), &pos);
#else
	ret = vfs_write(fp, pre_strs, strlen(pre_strs), &pos);
#endif
	if (ret < 0) {
		DHD_ERROR(("write file error, err = %d\n", ret));
		goto exit;
	}

	do {
		unsigned int buf_size = (unsigned int)(dld_buf->max -
			(unsigned long)dld_buf->buffer);
		if (dld_buf->wraparound) {
			wr_size = buf_size;
		} else {
			if (!dld_buf->buffer[0]) { /* print log if buf is empty. */
				DHD_ERROR_EX(("Buffer is empty. No event/log.\n"));
			}
			wr_size = (unsigned int)(dld_buf->present - dld_buf->front);
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
		ret = kernel_write(fp, dld_buf->buffer, wr_size, &pos);
#else
		ret = vfs_write(fp, dld_buf->buffer, wr_size, &pos);
#endif
		if (ret < 0) {
			DHD_ERROR(("write file error, err = %d\n", ret));
			goto exit;
		}

		/* re-init dhd_log_dump_buf structure */
		spin_lock_irqsave(&dld_buf->lock, flags);
		dld_buf->wraparound = 0;
		dld_buf->present = dld_buf->front;
		dld_buf->remain = buf_size;
		bzero(dld_buf->buffer, buf_size);
		spin_unlock_irqrestore(&dld_buf->lock, flags);
		ret = BCME_OK;

		if (++i < DLD_BUFFER_NUM) {
			dld_buf = &g_dld_buf[i];
		} else {
			break;
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
		ret = kernel_write(fp, post_strs, strlen(post_strs), &pos);
#else
		ret = vfs_write(fp, post_strs, strlen(post_strs), &pos);
#endif
		if (ret < 0) {
			DHD_ERROR(("write file error, err = %d\n", ret));
			goto exit;
		}
	} while (1);

exit:
#if defined(STAT_REPORT)
	if (!IS_ERR(fp) && ret >= 0) {
		wl_stat_report_file_save(dhdp, fp);
	}
#endif /* STAT_REPORT */

	if (!IS_ERR(fp)) {
		filp_close(fp, NULL);
	}
	set_fs(old_fs);

	return ret;
}
#endif /* DHD_LOG_DUMP */


#ifdef BCMASSERT_LOG
#ifdef CUSTOMER_HW4_DEBUG
#define ASSERTINFO PLATFORM_PATH".assert.info"
#elif defined(CUSTOMER_HW2)
#define ASSERTINFO "/data/misc/wifi/.assert.info"
#else
#define ASSERTINFO "/installmedia/.assert.info"
#endif /* CUSTOMER_HW4_DEBUG */
void dhd_get_assert_info(dhd_pub_t *dhd)
{
	struct file *fp = NULL;
	char *filepath = ASSERTINFO;
	int mem_val = -1;

	/*
	 * Read assert info from the file
	 * 0: Trigger Kernel crash by panic()
	 * 1: Print out the logs and don't trigger Kernel panic. (default)
	 * 2: Trigger Kernel crash by BUG()
	 * File doesn't exist: Keep default value (1).
	 */
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
	} else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
		ssize_t ret = kernel_read(fp, (char *)&mem_val, 4, NULL);
#else
		int ret = kernel_read(fp, 0, (char *)&mem_val, 4);
#endif
		if (ret < 0) {
			DHD_ERROR(("%s: File read error, ret=%d\n", __FUNCTION__, ret));
		} else {
			mem_val = bcm_atoi((char *)&mem_val);
			DHD_ERROR(("%s: ASSERT ENABLED = %d\n", __FUNCTION__, mem_val));
		}
		filp_close(fp, NULL);
	}
#ifdef CUSTOMER_HW4_DEBUG
		/* By default. set to 1, No Kernel Panic */
		g_assert_type = (mem_val >= 0) ? mem_val : 1;
#else
		/* By default. set to 0, Kernel Panic */
		g_assert_type = (mem_val >= 0) ? mem_val : 0;
#endif
}
#endif /* BCMASSERT_LOG */

/*
 * This call is to get the memdump size so that,
 * halutil can alloc that much buffer in user space.
 */
int
dhd_os_socram_dump(struct net_device *dev, uint32 *dump_size)
{
	int ret = BCME_OK;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	dhd_pub_t *dhdp = &dhd->pub;

	if (dhdp->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s: bus is down\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(dhdp)) {
		DHD_ERROR(("%s: bus is in suspend(%d) or suspending(0x%x) state, so skip\n",
			__FUNCTION__, dhdp->busstate, dhdp->dhd_bus_busy_state));
		return BCME_ERROR;
	}

	ret = dhd_common_socram_dump(dhdp);
	if (ret == BCME_OK) {
		*dump_size = dhdp->soc_ram_length;
	}
	return ret;
}

/*
 * This is to get the actual memdup after getting the memdump size
 */
int
dhd_os_get_socram_dump(struct net_device *dev, char **buf, uint32 *size)
{
	int ret = BCME_OK;
	int orig_len = 0;
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	dhd_pub_t *dhdp = &dhd->pub;
	if (buf == NULL)
		return BCME_ERROR;
	orig_len = *size;
	if (dhdp->soc_ram) {
		if (orig_len >= dhdp->soc_ram_length) {
			memcpy(*buf, dhdp->soc_ram, dhdp->soc_ram_length);
			/* reset the storage of dump */
			memset(dhdp->soc_ram, 0, dhdp->soc_ram_length);
			*size = dhdp->soc_ram_length;
		} else {
			ret = BCME_BUFTOOSHORT;
			DHD_ERROR(("The length of the buffer is too short"
				" to save the memory dump with %d\n", dhdp->soc_ram_length));
		}
	} else {
		DHD_ERROR(("socram_dump is not ready to get\n"));
		ret = BCME_NOTREADY;
	}
	return ret;
}

int
dhd_os_get_version(struct net_device *dev, bool dhd_ver, char **buf, uint32 size)
{
	char *fw_str;

	if (size == 0)
		return BCME_BADARG;

	fw_str = strstr(info_string, "Firmware: ");
	if (fw_str == NULL) {
		return BCME_ERROR;
	}

	memset(*buf, 0, size);
	if (dhd_ver) {
		strncpy(*buf, dhd_version, size - 1);
	} else {
		strncpy(*buf, fw_str, size - 1);
	}
	return BCME_OK;
}

#ifdef DHD_WMF
/* Returns interface specific WMF configuration */
dhd_wmf_t* dhd_wmf_conf(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];
	return &ifp->wmf;
}
#endif /* DHD_WMF */

#if defined(TRAFFIC_MGMT_DWM)
void traffic_mgmt_pkt_set_prio(dhd_pub_t *dhdp, void * pktbuf)
{
	struct ether_header *eh;
	struct ethervlan_header *evh;
	uint8 *pktdata, *ip_body;
	uint8  dwm_filter;
	uint8 tos_tc = 0;
	uint8 dscp   = 0;
	pktdata = (uint8 *)PKTDATA(dhdp->osh, pktbuf);
	eh = (struct ether_header *) pktdata;
	ip_body = NULL;

	if (dhdp->dhd_tm_dwm_tbl.dhd_dwm_enabled) {
		if (eh->ether_type == hton16(ETHER_TYPE_8021Q)) {
			evh = (struct ethervlan_header *)eh;
			if ((evh->ether_type == hton16(ETHER_TYPE_IP)) ||
				(evh->ether_type == hton16(ETHER_TYPE_IPV6))) {
				ip_body = pktdata + sizeof(struct ethervlan_header);
			}
		} else if ((eh->ether_type == hton16(ETHER_TYPE_IP)) ||
			(eh->ether_type == hton16(ETHER_TYPE_IPV6))) {
			ip_body = pktdata + sizeof(struct ether_header);
		}
		if (ip_body) {
			tos_tc = IP_TOS46(ip_body);
			dscp = tos_tc >> IPV4_TOS_DSCP_SHIFT;
		}

		if (dscp < DHD_DWM_TBL_SIZE) {
			dwm_filter = dhdp->dhd_tm_dwm_tbl.dhd_dwm_tbl[dscp];
			if (DHD_TRF_MGMT_DWM_IS_FILTER_SET(dwm_filter)) {
				PKTSETPRIO(pktbuf, DHD_TRF_MGMT_DWM_PRIO(dwm_filter));
			}
		}
	}
}
#endif 

bool dhd_sta_associated(dhd_pub_t *dhdp, uint32 bssidx, uint8 *mac)
{
	return dhd_find_sta(dhdp, bssidx, mac) ? TRUE : FALSE;
}

#ifdef DHD_L2_FILTER
arp_table_t*
dhd_get_ifp_arp_table_handle(dhd_pub_t *dhdp, uint32 bssidx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(bssidx < DHD_MAX_IFS);

	ifp = dhd->iflist[bssidx];
	return ifp->phnd_arp_table;
}

int dhd_get_parp_status(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	if (ifp)
		return ifp->parp_enable;
	else
		return FALSE;
}

/* Set interface specific proxy arp configuration */
int dhd_set_parp_status(dhd_pub_t *dhdp, uint32 idx, int val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;
	ASSERT(idx < DHD_MAX_IFS);
	ifp = dhd->iflist[idx];

	if (!ifp)
	    return BCME_ERROR;

	/* At present all 3 variables are being
	 * handled at once
	 */
	ifp->parp_enable = val;
	ifp->parp_discard = val;
	ifp->parp_allnode = val;

	/* Flush ARP entries when disabled */
	if (val == FALSE) {
		bcm_l2_filter_arp_table_update(dhdp->osh, ifp->phnd_arp_table, TRUE, NULL,
			FALSE, dhdp->tickcnt);
	}
	return BCME_OK;
}

bool dhd_parp_discard_is_enabled(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	ASSERT(ifp);
	return ifp->parp_discard;
}

bool
dhd_parp_allnode_is_enabled(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	ASSERT(ifp);

	return ifp->parp_allnode;
}

int dhd_get_dhcp_unicast_status(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	ASSERT(ifp);

	return ifp->dhcp_unicast;
}

int dhd_set_dhcp_unicast_status(dhd_pub_t *dhdp, uint32 idx, int val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;
	ASSERT(idx < DHD_MAX_IFS);
	ifp = dhd->iflist[idx];

	ASSERT(ifp);

	ifp->dhcp_unicast = val;
	return BCME_OK;
}

int dhd_get_block_ping_status(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	ASSERT(ifp);

	return ifp->block_ping;
}

int dhd_set_block_ping_status(dhd_pub_t *dhdp, uint32 idx, int val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;
	ASSERT(idx < DHD_MAX_IFS);
	ifp = dhd->iflist[idx];

	ASSERT(ifp);

	ifp->block_ping = val;
	/* Disable rx_pkt_chain feature for interface if block_ping option is
	 * enabled
	 */
	dhd_update_rx_pkt_chainable_state(dhdp, idx);
	return BCME_OK;
}

int dhd_get_grat_arp_status(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	ASSERT(ifp);

	return ifp->grat_arp;
}

int dhd_set_grat_arp_status(dhd_pub_t *dhdp, uint32 idx, int val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;
	ASSERT(idx < DHD_MAX_IFS);
	ifp = dhd->iflist[idx];

	ASSERT(ifp);

	ifp->grat_arp = val;

	return BCME_OK;
}
#endif /* DHD_L2_FILTER */


#if defined(SET_RPS_CPUS)
int dhd_rps_cpus_enable(struct net_device *net, int enable)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_if_t *ifp;
	int ifidx;
	char * RPS_CPU_SETBUF;

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	if (ifidx == PRIMARY_INF) {
		if (dhd->pub.op_mode == DHD_FLAG_IBSS_MODE) {
			DHD_INFO(("%s : set for IBSS.\n", __FUNCTION__));
			RPS_CPU_SETBUF = RPS_CPUS_MASK_IBSS;
		} else {
			DHD_INFO(("%s : set for BSS.\n", __FUNCTION__));
			RPS_CPU_SETBUF = RPS_CPUS_MASK;
		}
	} else if (ifidx == VIRTUAL_INF) {
		DHD_INFO(("%s : set for P2P.\n", __FUNCTION__));
		RPS_CPU_SETBUF = RPS_CPUS_MASK_P2P;
	} else {
		DHD_ERROR(("%s : Invalid index : %d.\n", __FUNCTION__, ifidx));
		return -EINVAL;
	}

	ifp = dhd->iflist[ifidx];
	if (ifp) {
		if (enable) {
			DHD_INFO(("%s : set rps_cpus as [%s]\n", __FUNCTION__, RPS_CPU_SETBUF));
			custom_rps_map_set(ifp->net->_rx, RPS_CPU_SETBUF, strlen(RPS_CPU_SETBUF));
		} else {
			custom_rps_map_clear(ifp->net->_rx);
		}
	} else {
		DHD_ERROR(("%s : ifp is NULL!!\n", __FUNCTION__));
		return -ENODEV;
	}
	return BCME_OK;
}

int custom_rps_map_set(struct netdev_rx_queue *queue, char *buf, size_t len)
{
	struct rps_map *old_map, *map;
	cpumask_var_t mask;
	int err, cpu, i;
	static DEFINE_SPINLOCK(rps_map_lock);

	DHD_INFO(("%s : Entered.\n", __FUNCTION__));

	if (!alloc_cpumask_var(&mask, GFP_KERNEL)) {
		DHD_ERROR(("%s : alloc_cpumask_var fail.\n", __FUNCTION__));
		return -ENOMEM;
	}

	err = bitmap_parse(buf, len, cpumask_bits(mask), nr_cpumask_bits);
	if (err) {
		free_cpumask_var(mask);
		DHD_ERROR(("%s : bitmap_parse fail.\n", __FUNCTION__));
		return err;
	}

	map = kzalloc(max_t(unsigned int,
		RPS_MAP_SIZE(cpumask_weight(mask)), L1_CACHE_BYTES),
		GFP_KERNEL);
	if (!map) {
		free_cpumask_var(mask);
		DHD_ERROR(("%s : map malloc fail.\n", __FUNCTION__));
		return -ENOMEM;
	}

	i = 0;
	for_each_cpu(cpu, mask) {
		map->cpus[i++] = cpu;
	}

	if (i) {
		map->len = i;
	} else {
		kfree(map);
		map = NULL;
		free_cpumask_var(mask);
		DHD_ERROR(("%s : mapping cpu fail.\n", __FUNCTION__));
		return -1;
	}

	spin_lock(&rps_map_lock);
	old_map = rcu_dereference_protected(queue->rps_map,
		lockdep_is_held(&rps_map_lock));
	rcu_assign_pointer(queue->rps_map, map);
	spin_unlock(&rps_map_lock);

	if (map) {
		static_key_slow_inc(&rps_needed);
	}
	if (old_map) {
		kfree_rcu(old_map, rcu);
		static_key_slow_dec(&rps_needed);
	}
	free_cpumask_var(mask);

	DHD_INFO(("%s : Done. mapping cpu nummber : %d\n", __FUNCTION__, map->len));
	return map->len;
}

void custom_rps_map_clear(struct netdev_rx_queue *queue)
{
	struct rps_map *map;

	DHD_INFO(("%s : Entered.\n", __FUNCTION__));

	map = rcu_dereference_protected(queue->rps_map, 1);
	if (map) {
		RCU_INIT_POINTER(queue->rps_map, NULL);
		kfree_rcu(map, rcu);
		DHD_INFO(("%s : rps_cpus map clear.\n", __FUNCTION__));
	}
}
#endif 



#ifdef DHD_DEBUG_PAGEALLOC

void
dhd_page_corrupt_cb(void *handle, void *addr_corrupt, size_t len)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)handle;

	DHD_ERROR(("%s: Got dhd_page_corrupt_cb 0x%p %d\n",
		__FUNCTION__, addr_corrupt, (uint32)len));

	DHD_OS_WAKE_LOCK(dhdp);
	prhex("Page Corruption:", addr_corrupt, len);
	dhd_dump_to_kernelog(dhdp);
#if defined(BCMPCIE) && defined(DHD_FW_COREDUMP)
	/* Load the dongle side dump to host memory and then BUG_ON() */
	dhdp->memdump_enabled = DUMP_MEMONLY;
	dhdp->memdump_type = DUMP_TYPE_MEMORY_CORRUPTION;
	dhd_bus_mem_dump(dhdp);
#endif /* BCMPCIE && DHD_FW_COREDUMP */
	DHD_OS_WAKE_UNLOCK(dhdp);
}
EXPORT_SYMBOL(dhd_page_corrupt_cb);
#endif /* DHD_DEBUG_PAGEALLOC */

#if defined(BCMPCIE) && defined(DHD_PKTID_AUDIT_ENABLED)
void
dhd_pktid_error_handler(dhd_pub_t *dhdp)
{
	DHD_ERROR(("%s: Got Pkt Id Audit failure \n", __FUNCTION__));
	DHD_OS_WAKE_LOCK(dhdp);
	dhd_dump_to_kernelog(dhdp);
#ifdef DHD_FW_COREDUMP
	/* Load the dongle side dump to host memory */
	if (dhdp->memdump_enabled == DUMP_DISABLED) {
		dhdp->memdump_enabled = DUMP_MEMFILE;
	}
	dhdp->memdump_type = DUMP_TYPE_PKTID_AUDIT_FAILURE;
	dhd_bus_mem_dump(dhdp);
#endif /* DHD_FW_COREDUMP */
	dhdp->hang_reason = HANG_REASON_PCIE_PKTID_ERROR;
	dhd_os_check_hang(dhdp, 0, -EREMOTEIO);
	DHD_OS_WAKE_UNLOCK(dhdp);
}
#endif /* BCMPCIE && DHD_PKTID_AUDIT_ENABLED */

struct net_device *
dhd_linux_get_primary_netdev(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;

	if (dhd->iflist[0] && dhd->iflist[0]->net)
		return dhd->iflist[0]->net;
	else
		return NULL;
}

#ifdef DHD_ARP_DUMP
#define ARP_PRINT(str) \
	do { \
		DHD_ERROR(("[dhd-%s] " str " [%s] : %s(%s) %s %s(%s)\n", \
			ifname, tx?"TX":"RX", \
			tx?sabuf:dabuf, tx?seabuf:deabuf, \
			tx?"->":"<-", tx?dabuf:sabuf, tx?deabuf:seabuf)); \
	} while (0)

#define ARP_PRINT_OTHER(str) \
	do { \
		DHD_ERROR(("[dhd-%s] " str " [%s] : %s(%s) %s %s(%s) op_code=%d\n", \
			ifname, tx?"TX":"RX", \
			tx?sabuf:dabuf, tx?seabuf:deabuf, \
			tx?"->":"<-", tx?dabuf:sabuf, tx?deabuf:seabuf, opcode)); \
	} while (0)

static void
dhd_arp_dump(char *ifname, uint8 *pktdata, bool tx)
{
	uint8 *pkt = (uint8 *)&pktdata[ETHER_HDR_LEN];
	struct bcmarp *arph = (struct bcmarp *)pkt;
	uint16 opcode;
	char sabuf[20]="", dabuf[20]="";
	char seabuf[ETHER_ADDR_STR_LEN]="";
	char deabuf[ETHER_ADDR_STR_LEN]="";

	if (!(dump_msg_level & DUMP_ARP_VAL))
		return;

	/* validation check */
	if (arph->htype != hton16(HTYPE_ETHERNET) ||
		arph->hlen != ETHER_ADDR_LEN ||
		arph->plen != 4) {
		return;
	}

	opcode = ntoh16(arph->oper);
	bcm_ip_ntoa((struct ipv4_addr *)arph->src_ip, sabuf);
	bcm_ip_ntoa((struct ipv4_addr *)arph->dst_ip, dabuf);
	bcm_ether_ntoa((struct ether_addr *)arph->dst_eth, deabuf);
	bcm_ether_ntoa((struct ether_addr *)arph->src_eth, seabuf);
	if (opcode == ARP_OPC_REQUEST) {
		ARP_PRINT("ARP REQUEST ");
	} else if (opcode == ARP_OPC_REPLY) {
		ARP_PRINT("ARP RESPONSE");
	} else {
		ARP_PRINT_OTHER("ARP OTHER");
	}
}
#endif /* DHD_ARP_DUMP */

#ifdef DHD_DHCP_DUMP
#define DHCP_PRINT(str) \
	do { \
		DHD_ERROR(("[dhd-%s] " str " %8s, %8s [%s] : %s(%s) %s %s(%s)\n", \
			ifname, dhcp_types[dhcp_type], dhcp_ops[b->op], \
			tx?"TX":"RX", \
			tx?sabuf:dabuf, tx?seabuf:deabuf, \
			tx?"->":"<-", tx?dabuf:sabuf, tx?deabuf:seabuf)); \
	} while (0)
static void
dhd_dhcp_dump(char *ifname, uint8 *pktdata, bool tx)
{
	struct bootp_fmt *b = (struct bootp_fmt *) &pktdata[ETHER_HDR_LEN];
	struct iphdr *h = &b->ip_header;
	uint8 *ptr, *opt, *end = (uint8 *) b + ntohs(b->ip_header.tot_len);
	int dhcp_type = 0, len, opt_len;
	uint32 ip_saddr, ip_daddr;
	char sabuf[20]="", dabuf[20]="";
	char seabuf[ETHER_ADDR_STR_LEN]="";
	char deabuf[ETHER_ADDR_STR_LEN]="";

	if (!(dump_msg_level & DUMP_DHCP_VAL))
		return;

	/* check IP header */
	if (h->ihl != 5 || h->version != 4 || h->protocol != IPPROTO_UDP) {
		return;
	}

	/* check UDP port for bootp (67, 68) */
	if (b->udp_header.source != htons(67) && b->udp_header.source != htons(68) &&
			b->udp_header.dest != htons(67) && b->udp_header.dest != htons(68)) {
		return;
	}

	/* check header length */
	if (ntohs(h->tot_len) < ntohs(b->udp_header.len) + sizeof(struct iphdr)) {
		return;
	}
	ip_saddr = h->saddr;
	ip_daddr = h->daddr;
	bcm_ip_ntoa((struct ipv4_addr *)&ip_saddr, sabuf);
	bcm_ip_ntoa((struct ipv4_addr *)&ip_daddr, dabuf);
	bcm_ether_ntoa((struct ether_addr *)pktdata, deabuf);
	bcm_ether_ntoa((struct ether_addr *)(pktdata+6), seabuf);

	len = ntohs(b->udp_header.len) - sizeof(struct udphdr);
	opt_len = len
		- (sizeof(*b) - sizeof(struct iphdr) - sizeof(struct udphdr) - sizeof(b->options));

	/* parse bootp options */
	if (opt_len >= 4 && !memcmp(b->options, bootp_magic_cookie, 4)) {
		ptr = &b->options[4];
		while (ptr < end && *ptr != 0xff) {
			opt = ptr++;
			if (*opt == 0) {
				continue;
			}
			ptr += *ptr + 1;
			if (ptr >= end) {
				break;
			}
			/* 53 is dhcp type */
			if (*opt == 53) {
				if (opt[1]) {
					dhcp_type = opt[2];
					DHCP_PRINT("DHCP");
					break;
				}
			}
		}
	}
}
#endif /* DHD_DHCP_DUMP */

#ifdef DHD_ICMP_DUMP
#define ICMP_PRINT(str) \
	do { \
		DHD_ERROR(("[dhd-%s] " str " [%2s] : %s(%s) %s %s(%s)\n", \
			ifname, tx?"TX":"RX", tx?sabuf:dabuf, tx?seabuf:deabuf, \
			tx?"->":"<-", tx?dabuf:sabuf, tx?deabuf:seabuf)); \
	} while (0)
static void
dhd_icmp_dump(char *ifname, uint8 *pktdata, bool tx)
{
	uint8 *pkt = (uint8 *)&pktdata[ETHER_HDR_LEN];
	struct iphdr *iph = (struct iphdr *)pkt;
	struct icmphdr *icmph;
	uint32 ip_saddr, ip_daddr;
	char sabuf[20]="", dabuf[20]="";
	char seabuf[ETHER_ADDR_STR_LEN]="";
	char deabuf[ETHER_ADDR_STR_LEN]="";

	if (!(dump_msg_level & DUMP_ICMP_VAL))
		return;

	/* check IP header */
	if (iph->ihl != 5 || iph->version != 4 || iph->protocol != IP_PROT_ICMP) {
		return;
	}

	icmph = (struct icmphdr *)((uint8 *)pkt + sizeof(struct iphdr));
	ip_saddr = iph->saddr;
	ip_daddr = iph->daddr;
	bcm_ip_ntoa((struct ipv4_addr *)&ip_saddr, sabuf);
	bcm_ip_ntoa((struct ipv4_addr *)&ip_daddr, dabuf);
	bcm_ether_ntoa((struct ether_addr *)pktdata, deabuf);
	bcm_ether_ntoa((struct ether_addr *)(pktdata+6), seabuf);
	if (icmph->type == ICMP_ECHO) {
		ICMP_PRINT("PING REQUEST");
	} else if (icmph->type == ICMP_ECHOREPLY) {
		ICMP_PRINT("PING REPLY  ");
	} else {
		ICMP_PRINT("ICMP        ");
	}
}
#endif /* DHD_ICMP_DUMP */

#ifdef SHOW_LOGTRACE
void
dhd_get_read_buf_ptr(dhd_pub_t *dhd_pub, trace_buf_info_t *trace_buf_info)
{
	dhd_dbg_ring_status_t ring_status;
	uint32 rlen;

	rlen = dhd_dbg_ring_pull_single(dhd_pub, FW_VERBOSE_RING_ID, trace_buf_info->buf,
		TRACE_LOG_BUF_MAX_SIZE, TRUE);
	trace_buf_info->size = rlen;
	trace_buf_info->availability = NEXT_BUF_NOT_AVAIL;
	if (rlen == 0) {
		trace_buf_info->availability = BUF_NOT_AVAILABLE;
		return;
	}
	dhd_dbg_get_ring_status(dhd_pub, FW_VERBOSE_RING_ID, &ring_status);
	if (ring_status.written_bytes != ring_status.read_bytes) {
		trace_buf_info->availability = NEXT_BUF_AVAIL;
	}
}
#endif /* SHOW_LOGTRACE */

bool
dhd_fw_download_status(dhd_pub_t * dhd_pub)
{
	return dhd_pub->fw_download_done;
}

int
dhd_create_to_notifier_skt(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	/* Kernel 3.7 onwards this API accepts only 3 arguments. */
	/* Kernel version 3.6 is a special case which accepts 4 arguments */
	nl_to_event_sk = netlink_kernel_create(&init_net, BCM_NL_USER, &g_cfg);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	/* Kernel version 3.5 and below use this old API format */
	nl_to_event_sk = netlink_kernel_create(&init_net, BCM_NL_USER, 0,
			dhd_process_daemon_msg, NULL, THIS_MODULE);
#else
	nl_to_event_sk = netlink_kernel_create(&init_net, BCM_NL_USER, THIS_MODULE, &g_cfg);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)) */
	if (!nl_to_event_sk)
	{
		printf("Error creating socket.\n");
		return -1;
	}
	DHD_INFO(("nl_to socket created successfully...\n"));
	return 0;
}

void
dhd_destroy_to_notifier_skt(void)
{
	DHD_INFO(("Destroying nl_to socket\n"));
	if (nl_to_event_sk) {
		netlink_kernel_release(nl_to_event_sk);
	}
}

static void
dhd_recv_msg_from_daemon(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	bcm_to_info_t *cmd;

	nlh = (struct nlmsghdr *)skb->data;
	cmd = (bcm_to_info_t *)nlmsg_data(nlh);
	if ((cmd->magic == BCM_TO_MAGIC) && (cmd->reason == REASON_DAEMON_STARTED)) {
		sender_pid = ((struct nlmsghdr *)(skb->data))->nlmsg_pid;
		DHD_INFO(("DHD Daemon Started\n"));
	}
}

int
dhd_send_msg_to_daemon(struct sk_buff *skb, void *data, int size)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb_out;

	if (!nl_to_event_sk) {
		DHD_INFO(("No socket available\n"));
		return -1;
	}

	BCM_REFERENCE(skb);
	if (sender_pid == 0) {
		DHD_INFO(("Invalid PID 0\n"));
		return -1;
	}

	if ((skb_out = nlmsg_new(size, 0)) == NULL) {
		DHD_ERROR(("%s: skb alloc failed\n", __FUNCTION__));
		return -1;
	}
	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, size, 0);
	NETLINK_CB(skb_out).dst_group = 0; /* Unicast */
	memcpy(nlmsg_data(nlh), (char *)data, size);

	if ((nlmsg_unicast(nl_to_event_sk, skb_out, sender_pid)) < 0) {
		DHD_INFO(("Error sending message\n"));
	}
	return 0;
}


static void
dhd_process_daemon_msg(struct sk_buff *skb)
{
	bcm_to_info_t to_info;

	to_info.magic = BCM_TO_MAGIC;
	to_info.reason = REASON_DAEMON_STARTED;
	to_info.trap = NO_TRAP;

	dhd_recv_msg_from_daemon(skb);
	dhd_send_msg_to_daemon(skb, &to_info, sizeof(to_info));
}

#ifdef REPORT_FATAL_TIMEOUTS
static void
dhd_send_trap_to_fw(dhd_pub_t * pub, int reason, int trap)
{
	bcm_to_info_t to_info;

	to_info.magic = BCM_TO_MAGIC;
	to_info.reason = reason;
	to_info.trap = trap;

	DHD_ERROR(("Sending Event reason:%d trap:%d\n", reason, trap));
	dhd_send_msg_to_daemon(NULL, (void *)&to_info, sizeof(bcm_to_info_t));
}

void
dhd_send_trap_to_fw_for_timeout(dhd_pub_t * pub, timeout_reasons_t reason)
{
	int to_reason;
	int trap = NO_TRAP;
	switch (reason) {
	        case DHD_REASON_COMMAND_TO:
	                to_reason = REASON_COMMAND_TO;
	                trap = DO_TRAP;
	                break;
	        case DHD_REASON_JOIN_TO:
	                to_reason = REASON_JOIN_TO;
	                break;
	        case DHD_REASON_SCAN_TO:
	                to_reason = REASON_SCAN_TO;
	                break;
	        case DHD_REASON_OQS_TO:
	                to_reason = REASON_OQS_TO;
	                trap = DO_TRAP;
	                break;
	        default:
	                to_reason = REASON_UNKOWN;
	}
	dhd_send_trap_to_fw(pub, to_reason, trap);
}
#endif /* REPORT_FATAL_TIMEOUTS */

#ifdef DHD_LOG_DUMP
void
dhd_log_dump_init(dhd_pub_t *dhd)
{
	struct dhd_log_dump_buf *dld_buf;
	int i = 0;
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
	int prealloc_idx = DHD_PREALLOC_DHD_LOG_DUMP_BUF;
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_MEMDUMP */

	for (i = 0; i < DLD_BUFFER_NUM; i++) {
		dld_buf = &g_dld_buf[i];
		spin_lock_init(&dld_buf->lock);
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
		dld_buf->buffer = DHD_OS_PREALLOC(dhd, prealloc_idx++, dld_buf_size[i]);
#else
		dld_buf->buffer = kmalloc(dld_buf_size[i], GFP_KERNEL);
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_MEMDUMP */

		if (!dld_buf->buffer) {
			dld_buf->buffer = kmalloc(dld_buf_size[i], GFP_KERNEL);
			DHD_ERROR(("Try to allocate memory using kmalloc().\n"));

			if (!dld_buf->buffer) {
				DHD_ERROR(("Failed to allocate memory for dld_buf[%d].\n", i));
				goto fail;
			}
		}

		dld_buf->wraparound = 0;
		dld_buf->max = (unsigned long)dld_buf->buffer + dld_buf_size[i];
		dld_buf->present = dld_buf->front = dld_buf->buffer;
		dld_buf->remain = dld_buf_size[i];
		dld_buf->enable = 1;
	}
	return;

fail:
	for (i = 0; i < DLD_BUFFER_NUM; i++) {
		if (dld_buf[i].buffer) {
			kfree(dld_buf[i].buffer);
		}
	}
}

void
dhd_log_dump_deinit(dhd_pub_t *dhd)
{
	struct dhd_log_dump_buf *dld_buf;
	int i = 0;

	for (i = 0; i < DLD_BUFFER_NUM; i++) {
		dld_buf = &g_dld_buf[i];
		dld_buf->enable = 0;
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
		DHD_OS_PREFREE(dhd, dld_buf->buffer, dld_buf_size[i]);
#else
		kfree(dld_buf->buffer);
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_MEMDUMP */
	}
}

void
dhd_log_dump_write(int type, const char *fmt, ...)
{
	int len = 0;
	char tmp_buf[DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE] = {0, };
	va_list args;
	unsigned long flags = 0;
	struct dhd_log_dump_buf *dld_buf = NULL;

	switch (type)
	{
		case DLD_BUF_TYPE_GENERAL:
			dld_buf = &g_dld_buf[type];
			break;
		case DLD_BUF_TYPE_SPECIAL:
			dld_buf = &g_dld_buf[type];
			break;
		default:
			DHD_ERROR(("%s: Unknown DHD_LOG_DUMP_BUF_TYPE(%d).\n",
				__FUNCTION__, type));
			return;
	}

	if (dld_buf->enable != 1) {
		return;
	}

	va_start(args, fmt);

	len = vsnprintf(tmp_buf, DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE, fmt, args);
	/* Non ANSI C99 compliant returns -1,
	 * ANSI compliant return len >= DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE
	 */
	if (len < 0) {
		return;
	}

	if (len >= DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE) {
		len = DHD_LOG_DUMP_MAX_TEMP_BUFFER_SIZE - 1;
		tmp_buf[len] = '\0';
	}

	/* make a critical section to eliminate race conditions */
	spin_lock_irqsave(&dld_buf->lock, flags);
	if (dld_buf->remain < len) {
		dld_buf->wraparound = 1;
		dld_buf->present = dld_buf->front;
		dld_buf->remain = dld_buf_size[type];
	}

	strncpy(dld_buf->present, tmp_buf, len);
	dld_buf->remain -= len;
	dld_buf->present += len;
	spin_unlock_irqrestore(&dld_buf->lock, flags);

	/* double check invalid memory operation */
	ASSERT((unsigned long)dld_buf->present <= dld_buf->max);
	va_end(args);
}

char*
dhd_log_dump_get_timestamp(void)
{
	static char buf[16];
	u64 ts_nsec;
	unsigned long rem_nsec;

	ts_nsec = local_clock();
	rem_nsec = do_div(ts_nsec, 1000000000);
	snprintf(buf, sizeof(buf), "%5lu.%06lu",
		(unsigned long)ts_nsec, rem_nsec / 1000);

	return buf;
}
#endif /* DHD_LOG_DUMP */

int
dhd_write_file(const char *filepath, char *buf, int buf_len)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	int ret = 0;

	/* change to KERNEL_DS address limit */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/* File is always created. */
	fp = filp_open(filepath, O_RDWR | O_CREAT, 0664);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: Couldn't open file '%s' err %ld\n",
			__FUNCTION__, filepath, PTR_ERR(fp)));
		ret = BCME_ERROR;
	} else {
		if (fp->f_mode & FMODE_WRITE) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
			ret = kernel_write(fp, buf, buf_len, &fp->f_pos);
#else
			ret = vfs_write(fp, buf, buf_len, &fp->f_pos);
#endif
			if (ret < 0) {
				DHD_ERROR(("%s: Couldn't write file '%s'\n",
					__FUNCTION__, filepath));
				ret = BCME_ERROR;
			} else {
				ret = BCME_OK;
			}
		}
		filp_close(fp, NULL);
	}

	/* restore previous address limit */
	set_fs(old_fs);

	return ret;
}

int
dhd_read_file(const char *filepath, char *buf, int buf_len)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	int ret;

	/* change to KERNEL_DS address limit */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		set_fs(old_fs);
		DHD_ERROR(("%s: File %s doesn't exist\n", __FUNCTION__, filepath));
		return BCME_ERROR;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	ret = kernel_read(fp, buf, buf_len, NULL);
#else
	ret = kernel_read(fp, 0, buf, buf_len);
#endif
	filp_close(fp, NULL);

	/* restore previous address limit */
	set_fs(old_fs);

	/* Return the number of bytes read */
	if (ret > 0) {
		/* Success to read */
		ret = 0;
	} else {
		DHD_ERROR(("%s: Couldn't read the file %s, ret=%d\n",
			__FUNCTION__, filepath, ret));
		ret = BCME_ERROR;
	}

	return ret;
}

int
dhd_write_file_and_check(const char *filepath, char *buf, int buf_len)
{
	int ret;

	ret = dhd_write_file(filepath, buf, buf_len);
	if (ret < 0) {
		return ret;
	}

	/* Read the file again and check if the file size is not zero */
	memset(buf, 0, buf_len);
	ret = dhd_read_file(filepath, buf, buf_len);

	return ret;
}

#ifdef DHD_LB_TXP
#define DHD_LB_TXBOUND	64
/*
 * Function that performs the TX processing on a given CPU
 */
bool
dhd_lb_tx_process(dhd_info_t *dhd)
{
	struct sk_buff *skb;
	int cnt = 0;
	struct net_device *net;
	int ifidx;
	bool resched = FALSE;

	DHD_TRACE(("%s(): TX Processing \r\n", __FUNCTION__));
	if (dhd == NULL) {
		DHD_ERROR((" Null pointer DHD \r\n"));
		return resched;
	}

	DHD_LB_STATS_PERCPU_ARR_INCR(dhd->txp_percpu_run_cnt);

	/* Base Loop to perform the actual Tx */
	do {
		skb = skb_dequeue(&dhd->tx_pend_queue);
		if (skb == NULL) {
			DHD_TRACE(("Dequeued a Null Packet \r\n"));
			break;
		}
		cnt++;

		net =  DHD_LB_TX_PKTTAG_NETDEV((dhd_tx_lb_pkttag_fr_t *)PKTTAG(skb));
		ifidx = DHD_LB_TX_PKTTAG_IFIDX((dhd_tx_lb_pkttag_fr_t *)PKTTAG(skb));

		BCM_REFERENCE(net);
		DHD_TRACE(("Processing skb %p for net %p index %d \r\n", skb,
			net, ifidx));

		__dhd_sendpkt(&dhd->pub, ifidx, skb);

		if (cnt >= DHD_LB_TXBOUND) {
			resched = TRUE;
			break;
		}

	} while (1);

	DHD_INFO(("%s(): Processed %d packets \r\n", __FUNCTION__, cnt));

	return resched;
}

void
dhd_lb_tx_handler(unsigned long data)
{
	dhd_info_t *dhd = (dhd_info_t *)data;

	if (dhd_lb_tx_process(dhd)) {
		dhd_tasklet_schedule(&dhd->tx_tasklet);
	}
}

#endif /* DHD_LB_TXP */

/* ----------------------------------------------------------------------------
 * Infrastructure code for sysfs interface support for DHD
 *
 * What is sysfs interface?
 * https://www.kernel.org/doc/Documentation/filesystems/sysfs.txt
 *
 * Why sysfs interface?
 * This is the Linux standard way of changing/configuring Run Time parameters
 * for a driver. We can use this interface to control "linux" specific driver
 * parameters.
 *
 * -----------------------------------------------------------------------------
 */

#include <linux/sysfs.h>
#include <linux/kobject.h>

#if defined(DHD_TRACE_WAKE_LOCK)

/* Function to show the history buffer */
static ssize_t
show_wklock_trace(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;
	dhd_info_t *dhd = (dhd_info_t *)dev;

	buf[ret] = '\n';
	buf[ret+1] = 0;

	dhd_wk_lock_stats_dump(&dhd->pub);
	return ret+1;
}

/* Function to enable/disable wakelock trace */
static ssize_t
wklock_trace_onoff(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long onoff;
	unsigned long flags;
	dhd_info_t *dhd = (dhd_info_t *)dev;

	onoff = bcm_strtoul(buf, NULL, 10);
	if (onoff != 0 && onoff != 1) {
		return -EINVAL;
	}

	spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
	trace_wklock_onoff = onoff;
	spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	if (trace_wklock_onoff) {
		printk("ENABLE WAKLOCK TRACE\n");
	} else {
		printk("DISABLE WAKELOCK TRACE\n");
	}

	return (ssize_t)(onoff+1);
}
#endif /* DHD_TRACE_WAKE_LOCK */

#if defined(DHD_LB_TXP)
static ssize_t
show_lbtxp(struct dhd_info *dev, char *buf)
{
	ssize_t ret = 0;
	unsigned long onoff;
	dhd_info_t *dhd = (dhd_info_t *)dev;

	onoff = atomic_read(&dhd->lb_txp_active);
	ret = scnprintf(buf, PAGE_SIZE - 1, "%lu \n",
		onoff);
	return ret;
}

static ssize_t
lbtxp_onoff(struct dhd_info *dev, const char *buf, size_t count)
{
	unsigned long onoff;
	dhd_info_t *dhd = (dhd_info_t *)dev;
	int i;

	onoff = bcm_strtoul(buf, NULL, 10);

	sscanf(buf, "%lu", &onoff);
	if (onoff != 0 && onoff != 1) {
		return -EINVAL;
	}
	atomic_set(&dhd->lb_txp_active, onoff);

	/* Since the scheme is changed clear the counters */
	for (i = 0; i < NR_CPUS; i++) {
		DHD_LB_STATS_CLR(dhd->txp_percpu_run_cnt[i]);
		DHD_LB_STATS_CLR(dhd->tx_start_percpu_run_cnt[i]);
	}

	return count;
}

#endif /* DHD_LB_TXP */
/*
 * Generic Attribute Structure for DHD.
 * If we have to add a new sysfs entry under /sys/bcm-dhd/, we have
 * to instantiate an object of type dhd_attr,  populate it with
 * the required show/store functions (ex:- dhd_attr_cpumask_primary)
 * and add the object to default_attrs[] array, that gets registered
 * to the kobject of dhd (named bcm-dhd).
 */

struct dhd_attr {
	struct attribute attr;
	ssize_t(*show)(struct dhd_info *, char *);
	ssize_t(*store)(struct dhd_info *, const char *, size_t count);
};

#if defined(DHD_TRACE_WAKE_LOCK)
static struct dhd_attr dhd_attr_wklock =
	__ATTR(wklock_trace, 0660, show_wklock_trace, wklock_trace_onoff);
#endif /* defined(DHD_TRACE_WAKE_LOCK */

#if defined(DHD_LB_TXP)
static struct dhd_attr dhd_attr_lbtxp =
	__ATTR(lbtxp, 0660, show_lbtxp, lbtxp_onoff);
#endif /* DHD_LB_TXP */

/* Attribute object that gets registered with "bcm-dhd" kobject tree */
static struct attribute *default_attrs[] = {
#if defined(DHD_TRACE_WAKE_LOCK)
	&dhd_attr_wklock.attr,
#endif /* DHD_TRACE_WAKE_LOCK */
#if defined(DHD_LB_TXP)
	&dhd_attr_lbtxp.attr,
#endif /* DHD_LB_TXP */
	NULL
};

#define to_dhd(k) container_of(k, struct dhd_info, dhd_kobj)
#define to_attr(a) container_of(a, struct dhd_attr, attr)

/*
 * bcm-dhd kobject show function, the "attr" attribute specifices to which
 * node under "bcm-dhd" the show function is called.
 */
static ssize_t dhd_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	dhd_info_t *dhd = to_dhd(kobj);
	struct dhd_attr *d_attr = to_attr(attr);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	int ret;

	if (d_attr->show)
		ret = d_attr->show(dhd, buf);
	else
		ret = -EIO;

	return ret;
}

/*
 * bcm-dhd kobject show function, the "attr" attribute specifices to which
 * node under "bcm-dhd" the store function is called.
 */
static ssize_t dhd_store(struct kobject *kobj, struct attribute *attr,
	const char *buf, size_t count)
{
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	dhd_info_t *dhd = to_dhd(kobj);
	struct dhd_attr *d_attr = to_attr(attr);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	int ret;

	if (d_attr->store)
		ret = d_attr->store(dhd, buf, count);
	else
		ret = -EIO;

	return ret;

}

static struct sysfs_ops dhd_sysfs_ops = {
	.show = dhd_show,
	.store = dhd_store,
};

static struct kobj_type dhd_ktype = {
	.sysfs_ops = &dhd_sysfs_ops,
	.default_attrs = default_attrs,
};

/* Create a kobject and attach to sysfs interface */
static int dhd_sysfs_init(dhd_info_t *dhd)
{
	int ret = -1;

	if (dhd == NULL) {
		DHD_ERROR(("%s(): dhd is NULL \r\n", __FUNCTION__));
		return ret;
	}

	/* Initialize the kobject */
	ret = kobject_init_and_add(&dhd->dhd_kobj, &dhd_ktype, NULL, "bcm-dhd");
	if (ret) {
		kobject_put(&dhd->dhd_kobj);
		DHD_ERROR(("%s(): Unable to allocate kobject \r\n", __FUNCTION__));
		return ret;
	}

	/*
	 * We are always responsible for sending the uevent that the kobject
	 * was added to the system.
	 */
	kobject_uevent(&dhd->dhd_kobj, KOBJ_ADD);

	return ret;
}

/* Done with the kobject and detach the sysfs interface */
static void dhd_sysfs_exit(dhd_info_t *dhd)
{
	if (dhd == NULL) {
		DHD_ERROR(("%s(): dhd is NULL \r\n", __FUNCTION__));
		return;
	}

	/* Releae the kobject */
	if (dhd->dhd_kobj.state_initialized)
		kobject_put(&dhd->dhd_kobj);
}

#ifdef DHD_DEBUG_UART
bool
dhd_debug_uart_is_running(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (dhd->duart_execute) {
		return TRUE;
	}

	return FALSE;
}

static void
dhd_debug_uart_exec_rd(void *handle, void *event_info, u8 event)
{
	dhd_pub_t *dhdp = handle;
	dhd_debug_uart_exec(dhdp, "rd");
}

static void
dhd_debug_uart_exec(dhd_pub_t *dhdp, char *cmd)
{
	int ret;

	char *argv[] = {DHD_DEBUG_UART_EXEC_PATH, cmd, NULL};
	char *envp[] = {"HOME=/", "TERM=linux", "PATH=/sbin:/system/bin", NULL};

#ifdef DHD_FW_COREDUMP
	if (dhdp->memdump_enabled == DUMP_MEMFILE_BUGON)
#endif
	{
		if (dhdp->hang_reason == HANG_REASON_PCIE_LINK_DOWN ||
#ifdef DHD_FW_COREDUMP
			dhdp->memdump_success == FALSE ||
#endif
			FALSE) {
			dhdp->info->duart_execute = TRUE;
			DHD_ERROR(("DHD: %s - execute %s %s\n",
				__FUNCTION__, DHD_DEBUG_UART_EXEC_PATH, cmd));
			ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
			DHD_ERROR(("DHD: %s - %s %s ret = %d\n",
				__FUNCTION__, DHD_DEBUG_UART_EXEC_PATH, cmd, ret));
			dhdp->info->duart_execute = FALSE;

#ifdef DHD_LOG_DUMP
			if (dhdp->memdump_type != DUMP_TYPE_BY_SYSDUMP)
#endif
			{
				BUG_ON(1);
			}
		}
	}
}
#endif	/* DHD_DEBUG_UART */

#if defined(DHD_BLOB_EXISTENCE_CHECK)
void
dhd_set_blob_support(dhd_pub_t *dhdp, char *fw_path)
{
	struct file *fp;
	char *filepath = CONFIG_BCMDHD_CLM_PATH;

	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s: ----- blob file dosen't exist -----\n", __FUNCTION__));
		dhdp->is_blob = FALSE;
	} else {
		DHD_ERROR(("%s: ----- blob file exist -----\n", __FUNCTION__));
		dhdp->is_blob = TRUE;
#if defined(CONCATE_BLOB)
		strncat(fw_path, "_blob", strlen("_blob"));
#else
		BCM_REFERENCE(fw_path);
#endif /* SKIP_CONCATE_BLOB */
		filp_close(fp, NULL);
	}
}
#endif /* DHD_BLOB_EXISTENCE_CHECK */

#if defined(PCIE_FULL_DONGLE)
/** test / loopback */
void
dmaxfer_free_dmaaddr_handler(void *handle, void *event_info, u8 event)
{
	dmaxref_mem_map_t *dmmap = (dmaxref_mem_map_t *)event_info;
	dhd_info_t *dhd_info = (dhd_info_t *)handle;
	dhd_pub_t *dhdp = &dhd_info->pub;

	if (event != DHD_WQ_WORK_DMA_LB_MEM_REL) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if ((dhd_info == NULL) || (dhdp == NULL)) {
		DHD_ERROR(("%s: invalid dhd_info\n", __FUNCTION__));
		return;
	}

	if (dmmap == NULL) {
		DHD_ERROR(("%s: dmmap is null\n", __FUNCTION__));
		return;
	}
	dmaxfer_free_prev_dmaaddr(dhdp, dmmap);
}


void
dhd_schedule_dmaxfer_free(dhd_pub_t *dhdp, dmaxref_mem_map_t *dmmap)
{
	dhd_info_t *dhd_info = dhdp->info;

	dhd_deferred_schedule_work(dhd_info->dhd_deferred_wq, (void *)dmmap,
		DHD_WQ_WORK_DMA_LB_MEM_REL, dmaxfer_free_dmaaddr_handler, DHD_WQ_WORK_PRIORITY_LOW);
}
#endif /* PCIE_FULL_DONGLE */
/* ---------------------------- End of sysfs implementation ------------------------------------- */
#ifdef HOFFLOAD_MODULES
void
dhd_linux_get_modfw_address(dhd_pub_t *dhd)
{
	const char* module_name = NULL;
	const struct firmware *module_fw;
	struct module_metadata *hmem = &dhd->hmem;

	if (dhd_hmem_module_string[0] != '\0') {
		module_name = dhd_hmem_module_string;
	} else {
		DHD_ERROR(("%s No module image name specified\n", __FUNCTION__));
		return;
	}
	if (request_firmware(&module_fw, module_name, dhd_bus_to_dev(dhd->bus))) {
		DHD_ERROR(("modules.img not available\n"));
		return;
	}
	if (!dhd_alloc_module_memory(dhd->bus, module_fw->size, hmem)) {
		release_firmware(module_fw);
		return;
	}
	memcpy(hmem->data, module_fw->data, module_fw->size);
	release_firmware(module_fw);
}
#endif /* HOFFLOAD_MODULES */

#ifdef SET_PCIE_IRQ_CPU_CORE
void
dhd_set_irq_cpucore(dhd_pub_t *dhdp, int set)
{
	unsigned int irq;
	if (!dhdp) {
		DHD_ERROR(("%s : dhd is NULL\n", __FUNCTION__));
		return;
	}

	if (!dhdp->bus) {
		DHD_ERROR(("%s : dhd->bus is NULL\n", __FUNCTION__));
		return;
	}

	if (dhdpcie_get_pcieirq(dhdp->bus, &irq)) {
		return;
	}

	set_irq_cpucore(irq, set);
}
#endif /* SET_PCIE_IRQ_CPU_CORE */

#if defined(DHD_HANG_SEND_UP_TEST)
void
dhd_make_hang_with_reason(struct net_device *dev, const char *string_num)
{
	dhd_info_t *dhd = NULL;
	dhd_pub_t *dhdp = NULL;
	uint reason = HANG_REASON_MAX;
	char buf[WLC_IOCTL_SMLEN] = {0, };
	uint32 fw_test_code = 0;
	dhd = DHD_DEV_INFO(dev);

	if (dhd) {
		dhdp = &dhd->pub;
	}

	if (!dhd || !dhdp) {
		return;
	}

	reason = (uint) bcm_strtoul(string_num, NULL, 0);
	DHD_ERROR(("Enter %s, reason=0x%x\n", __FUNCTION__,  reason));

	if (reason == 0) {
		if (dhdp->req_hang_type) {
			DHD_ERROR(("%s, Clear HANG test request 0x%x\n",
				__FUNCTION__, dhdp->req_hang_type));
			dhdp->req_hang_type = 0;
			return;
		} else {
			DHD_ERROR(("%s, No requested HANG test\n", __FUNCTION__));
			return;
		}
	} else if ((reason <= HANG_REASON_MASK) || (reason >= HANG_REASON_MAX)) {
		DHD_ERROR(("Invalid HANG request, reason 0x%x\n", reason));
		return;
	}

	if (dhdp->req_hang_type != 0) {
		DHD_ERROR(("Already HANG requested for test\n"));
		return;
	}

	switch (reason) {
		case HANG_REASON_IOCTL_RESP_TIMEOUT:
			DHD_ERROR(("Make HANG!!!: IOCTL response timeout(0x%x)\n", reason));
			dhdp->req_hang_type = reason;
			fw_test_code = 102; /* resumed on timeour */
			bcm_mkiovar("bus:disconnect", (void *)&fw_test_code, 4, buf, sizeof(buf));
			dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, buf, sizeof(buf), TRUE, 0);
			break;
		case HANG_REASON_DONGLE_TRAP:
			DHD_ERROR(("Make HANG!!!: Dongle trap (0x%x)\n", reason));
			dhdp->req_hang_type = reason;
			fw_test_code = 99; /* dongle trap */
			bcm_mkiovar("bus:disconnect", (void *)&fw_test_code, 4, buf, sizeof(buf));
			dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, buf, sizeof(buf), TRUE, 0);
			break;
		case HANG_REASON_D3_ACK_TIMEOUT:
			DHD_ERROR(("Make HANG!!!: D3 ACK timeout (0x%x)\n", reason));
			dhdp->req_hang_type = reason;
			break;
		case HANG_REASON_BUS_DOWN:
			DHD_ERROR(("Make HANG!!!: BUS down(0x%x)\n", reason));
			dhdp->req_hang_type = reason;
			break;
		case HANG_REASON_PCIE_LINK_DOWN:
		case HANG_REASON_MSGBUF_LIVELOCK:
			dhdp->req_hang_type = 0;
			DHD_ERROR(("Does not support requested HANG(0x%x)\n", reason));
			break;
		case HANG_REASON_IFACE_OP_FAILURE:
			DHD_ERROR(("Make HANG!!!: P2P inrerface delete failure(0x%x)\n", reason));
			dhdp->req_hang_type = reason;
			break;
		case HANG_REASON_HT_AVAIL_ERROR:
			dhdp->req_hang_type = 0;
			DHD_ERROR(("PCIe does not support requested HANG(0x%x)\n", reason));
			break;
		case HANG_REASON_PCIE_RC_LINK_UP_FAIL:
			DHD_ERROR(("Make HANG!!!:Link Up(0x%x)\n", reason));
			dhdp->req_hang_type = reason;
			break;
		default:
			dhdp->req_hang_type = 0;
			DHD_ERROR(("Unknown HANG request (0x%x)\n", reason));
			break;
	}
}
#endif /* DHD_HANG_SEND_UP_TEST */
#ifdef DHD_WAKE_STATUS
wake_counts_t*
dhd_get_wakecount(dhd_pub_t *dhdp)
{
#ifdef BCMDBUS
	return NULL;
#else
	return dhd_bus_get_wakecount(dhdp);
#endif /* BCMDBUS */
}
#endif /* DHD_WAKE_STATUS */

#ifdef BCM_ASLR_HEAP
uint32
dhd_get_random_number(void)
{
	uint32 rand = 0;
	get_random_bytes_arch(&rand, sizeof(rand));
	return rand;
}
#endif /* BCM_ASLR_HEAP */

#ifdef DHD_PKT_LOGGING
void
dhd_pktlog_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;

	if (!dhd) {
		DHD_ERROR(("%s: dhd is NULL\n", __FUNCTION__));
		return;
	}

	if (dhd_pktlog_write_file(&dhd->pub)) {
		DHD_ERROR(("%s: writing pktlog dump to the file failed\n", __FUNCTION__));
		return;
	}
}

void
dhd_schedule_pktlog_dump(dhd_pub_t *dhdp)
{
	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq,
			(void*)NULL, DHD_WQ_WORK_PKTLOG_DUMP,
			dhd_pktlog_dump, DHD_WQ_WORK_PRIORITY_HIGH);
}
#endif /* DHD_PKT_LOGGING */

void *dhd_get_pub(struct net_device *dev)
{
	dhd_info_t *dhdinfo = *(dhd_info_t **)netdev_priv(dev);
	if (dhdinfo)
		return (void *)&dhdinfo->pub;
	else {
		printf("%s: null dhdinfo\n", __FUNCTION__);
		return NULL;
	}
}

bool dhd_os_wd_timer_enabled(void *bus)
{
	dhd_pub_t *pub = bus;
	dhd_info_t *dhd = (dhd_info_t *)pub->info;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	if (!dhd) {
		DHD_ERROR(("%s: dhd NULL\n", __FUNCTION__));
		return FALSE;
	}
	return dhd->wd_timer_valid;
}
